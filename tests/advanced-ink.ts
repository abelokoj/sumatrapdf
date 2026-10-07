import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve, sep } from "node:path";
import { ControlClient, ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, runStandalone, TESTS_TMP_DIR, tmpPath } from "./util.ts";
import {
  clientToScreen,
  getWindowRect,
  readWindowDCColumn,
  MK_LBUTTON,
  packCoords,
  sendMessage,
  setCursorPos,
  setProcessDpiAware,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  WM_MOUSEMOVE,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

type Point = { x: number; y: number };
type Ink = { rect: number[]; screen: number[]; strokes: number; points: number; width: number };

async function dump(client: ControlClient): Promise<string> {
  await client.waitForRenderIdle();
  return String((await client.request(ControlCommand.TestMarkupAnnots, []))[1] ?? "");
}

function ink(raw: string): Ink[] {
  const result: Ink[] = [];
  const pattern = /type=Ink[^\n]*rect=([^ ]+) screen=([^\n]+)\nink strokes=(\d+) points=(\d+) opacity=\d+ width=(\d+)/g;
  for (const match of raw.matchAll(pattern))
    result.push({
      rect: match[1]!.split(",").map(Number),
      screen: match[2]!.split(",").map(Number),
      strokes: +match[3]!,
      points: +match[4]!,
      width: +match[5]!,
    });
  return result;
}

async function gesture(canvas: number, points: Point[], client?: ControlClient): Promise<void> {
  const first = points[0]!;
  const screen = clientToScreen(canvas, first.x, first.y);
  setCursorPos(screen.x, screen.y);
  sendMessage(canvas, WM_MOUSEMOVE, 0, packCoords(first.x, first.y));
  sendMessage(canvas, WM_LBUTTONDOWN, MK_LBUTTON, packCoords(first.x, first.y));
  if (client) {
    const down = await dump(client);
    if (!down.includes("lasso active=1 drawing=1"))
      throw new Error(`Lasso did not start at ${JSON.stringify(first)}\n${down}`);
  }
  for (const point of points.slice(1)) sendMessage(canvas, WM_MOUSEMOVE, MK_LBUTTON, packCoords(point.x, point.y));
  const last = points.at(-1)!;
  sendMessage(canvas, WM_LBUTTONUP, 0, packCoords(last.x, last.y));
}

function requireInk(raw: string, count: number, predicate: (items: Ink[]) => boolean, message: string): Ink[] {
  const items = ink(raw);
  if (items.length !== count || !predicate(items)) throw new Error(`${message}\n${raw}`);
  return items;
}

function strokePixels(canvas: number, item: Ink): number[] {
  const window = getWindowRect(canvas);
  const origin = clientToScreen(canvas, 0, 0);
  const x = Math.round(item.screen[0]! + item.screen[2]! / 2) + origin.x - window.left;
  const y = Math.round(item.screen[1]! + item.screen[3]! / 2) + origin.y - window.top;
  const pixels: number[] = [];
  for (let column = x - 15; column <= x + 15; column++) pixels.push(...readWindowDCColumn(canvas, column, y - 12, 25));
  return pixels;
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const directory = tmpPath("advanced-ink");
  if (!resolve(directory).startsWith(resolve(TESTS_TMP_DIR) + sep)) throw new Error("Unsafe test directory");
  rmSync(directory, { recursive: true, force: true });
  mkdirSync(directory, { recursive: true });
  const appdata = join(directory, "appdata");
  mkdirSync(appdata);
  writeFileSync(
    join(appdata, "SumatraPDFEnhanced-settings.txt"),
    "UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\nAnnotations [\nInkColor = #ff0000\n]\n",
  );
  const pdf = join(directory, "ink.pdf");
  writeFileSync(
    pdf,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [4 0 R 5 0 R] >>",
      "<< /Type /Annot /Subtype /Ink /Rect [98 598 242 642] /BS << /W 4 >> /C [0 0 1] /InkList [[100 600 240 640]] >>",
      "<< /Type /Annot /Subtype /Ink /Rect [118 538 262 582] /BS << /W 4 >> /C [0 0 0] /InkList [[120 540 260 580]] >>",
    ]),
    "latin1",
  );
  const original = readFileSync(pdf);
  const { proc, client, frame } = await launchControlled(["-appdata", appdata, "-zoom", "100", pdf]);
  try {
    const canvas = findCanvas(frame);
    if (!canvas) throw new Error("Ink canvas missing");
    sendCommandSync(frame, cmdId("CmdAnnotationLasso"));
    const initial = requireInk(
      await dump(client),
      2,
      (items) => items.every((item) => item.strokes === 1 && item.points === 2 && item.width === 4),
      "Initial ink fixture missing",
    );
    const left = Math.min(...initial.map((item) => item.screen[0]!)) - 20;
    const top = Math.min(...initial.map((item) => item.screen[1]!)) - 20;
    const right = Math.max(...initial.map((item) => item.screen[0]! + item.screen[2]!)) + 20;
    const bottom = Math.max(...initial.map((item) => item.screen[1]! + item.screen[3]!)) + 20;
    await gesture(
      canvas,
      [
        { x: left, y: top },
        { x: right, y: top },
        { x: right, y: bottom },
        { x: left, y: bottom },
        { x: left, y: top },
      ],
      client,
    );
    const selection = await dump(client);
    if (!selection.includes("lasso active=1 drawing=0 transforming=0 selected=2"))
      throw new Error(`Lasso selection failed\n${selection}`);
    const beforePixels = strokePixels(canvas, initial[0]!);
    sendCommandSync(frame, cmdId("CmdLassoThicker"));
    requireInk(
      await dump(client),
      2,
      (items) => items.every((item) => item.width === 5),
      "Multi-selection thickness failed",
    );
    const afterPixels = strokePixels(canvas, initial[0]!);
    if (
      !beforePixels.some(
        (pixel, index) => pixel !== 0xffffffff && afterPixels[index] !== 0xffffffff && pixel !== afterPixels[index],
      )
    )
      throw new Error("Selection thickness changed the native PDF but left the visible stroke stale");
    sendCommandSync(frame, cmdId("CmdUndo"));
    requireInk(
      await dump(client),
      2,
      (items) => items.every((item) => item.width === 4),
      "Selection thickness did not undo in one step",
    );
    sendCommandSync(frame, cmdId("CmdLassoRotateRight"));
    requireInk(
      await dump(client),
      2,
      (items) => items.some((item, index) => JSON.stringify(item.rect) !== JSON.stringify(initial[index]!.rect)),
      "Selection rotation did not change native coordinates",
    );
    sendCommandSync(frame, cmdId("CmdUndo"));
    requireInk(
      await dump(client),
      2,
      (items) => items.every((item, index) => JSON.stringify(item.rect) === JSON.stringify(initial[index]!.rect)),
      "Selection rotation failed to undo",
    );
    sendCommandSync(frame, cmdId("CmdLassoDuplicate"));
    requireInk(await dump(client), 4, () => true, "Lasso did not duplicate both annotations");
    sendCommandSync(frame, cmdId("CmdUndo"));
    const beforeErase = requireInk(await dump(client), 2, () => true, "Duplicate did not undo in one step");
    sendCommandSync(frame, cmdId("CmdInkSegmentEraser"));
    await gesture(
      canvas,
      beforeErase.map((item) => ({
        x: Math.round(item.screen[0]! + item.screen[2]! / 2),
        y: Math.round(item.screen[1]! + item.screen[3]! / 2),
      })),
    );
    requireInk(
      await dump(client),
      2,
      (items) => items.every((item) => item.strokes === 2 && item.points === 4),
      "Segment eraser did not preserve both sides of both strokes",
    );
    sendCommandSync(frame, cmdId("CmdUndo"));
    requireInk(
      await dump(client),
      2,
      (items) => items.every((item) => item.strokes === 1 && item.points === 2),
      "Eraser gesture did not undo both cuts in one step",
    );
    if (!readFileSync(pdf).equals(original)) throw new Error("Editing changed the original PDF without saving");
  } finally {
    client.close();
    await killAndWait(proc);
  }
  console.log("advanced-ink: OK");
}

if (import.meta.main) await runStandalone(testit);
