// Editable color models share the selected color and preserve its opacity.
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlClient, ControlCommand } from "./control.ts";
import { openChipDropdown } from "./annot-color-dropdown.ts";
import { assemblePdf, cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import {
  captureWindowPixels,
  captureWindowToPng,
  enumChildWindows,
  enumWindows,
  findChildWindow,
  findTopWindow,
  getClassName,
  getControlText,
  getWindowLong,
  getWindowPid,
  getWindowRect,
  getWindowText,
  isWindowVisible,
  sendMessage,
  sendText,
  VK_RETURN,
  WM_COMMAND,
} from "./winapi.ts";
import {
  clickAt,
  findCanvas,
  killAndWait,
  launchControlled,
  parkCursorAway,
  pressKey,
  sendCommandSync,
} from "./win-automation.ts";

const CB_GETCOUNT = 0x0146;
const CB_SETCURSEL = 0x014e;
const CBN_SELCHANGE = 1;
const GWL_ID = -12;
const TITLE = "Annotation Colors";
const ORIGINAL_COLOR = "#1153e5";
const ORIGINAL_ALPHA = 128;
const MODELS = ["RGB", "HEX", "CMYK", "HSV", "HSL"] as const;
type Model = (typeof MODELS)[number];

function colorDialog(pid: number): number {
  let dialog = 0;
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) !== pid || !isWindowVisible(hwnd) || getWindowText(hwnd) !== TITLE) return true;
    dialog = hwnd;
    return false;
  });
  return dialog;
}

function visibleEdits(dialog: number): number[] {
  const edits: number[] = [];
  enumChildWindows(dialog, (hwnd) => {
    if (getClassName(hwnd) === "Edit" && isWindowVisible(hwnd)) edits.push(hwnd);
    return true;
  });
  edits.sort((a, b) => getWindowRect(a).left - getWindowRect(b).left);
  return edits;
}

async function setModel(dialog: number, combo: number, model: Model): Promise<number[]> {
  sendMessage(combo, CB_SETCURSEL, MODELS.indexOf(model), 0);
  sendMessage(dialog, WM_COMMAND, (getWindowLong(combo, GWL_ID) & 0xffff) | (CBN_SELCHANGE << 16), combo);
  const count = model === "HEX" ? 1 : model === "CMYK" ? 4 : 3;
  const edits = await pollUntil(
    () => visibleEdits(dialog),
    (controls) => controls.length === count,
    { error: (controls) => `color-picker-formats: ${model} has ${controls.length} edits, want ${count}` },
  );
  const bounds = getWindowRect(dialog);
  for (let i = 0; i < edits.length; i++) {
    const rect = getWindowRect(edits[i]!);
    if (rect.left < bounds.left || rect.right > bounds.right || rect.top < bounds.top || rect.bottom > bounds.bottom) {
      throw new Error(`color-picker-formats: ${model} input ${i} extends outside the picker`);
    }
    if (
      rect.right <= rect.left ||
      rect.bottom <= rect.top ||
      (i > 0 && rect.left < getWindowRect(edits[i - 1]!).right)
    ) {
      throw new Error(`color-picker-formats: ${model} inputs overlap or have empty bounds`);
    }
  }
  return edits;
}

async function assertHex(dialog: number, combo: number, color: string): Promise<void> {
  const [edit] = await setModel(dialog, combo, "HEX");
  const actual = getControlText(edit!).toLowerCase();
  if (actual !== color) throw new Error(`color-picker-formats: color is ${actual}, want ${color}`);
}

async function assertInvalid(dialog: number, edit: number, value: string): Promise<void> {
  sendText(edit, value);
  await pressKey(edit, VK_RETURN, 80);
  if (!isWindowVisible(dialog)) throw new Error(`color-picker-formats: Select accepted invalid value '${value}'`);
}

function assertButtonVisible(dialog: number, edits: number[], model: Model): void {
  const painted = captureWindowPixels(dialog, "window");
  if (!painted) throw new Error("color-picker-formats: picker capture failed");
  const bounds = getWindowRect(dialog);
  const input = getWindowRect(edits[0]!);
  const belowInputs = Math.max(...edits.map((edit) => getWindowRect(edit).bottom)) - bounds.top;
  let top = painted.h;
  let bottom = -1;
  // The default Select button is amber; other amber swatches are left of it.
  for (let y = Math.max(0, belowInputs); y < painted.h; y++) {
    let amber = 0;
    for (let x = Math.floor(painted.w * 0.75); x < painted.w; x++) {
      const p = (y * painted.w + x) * 4;
      const b = painted.data[p]!,
        g = painted.data[p + 1]!,
        r = painted.data[p + 2]!;
      if (r > 190 && g > 100 && g < r * 0.97 && b < 80) amber++;
    }
    if (amber < 10) continue;
    top = Math.min(top, y);
    bottom = y;
  }
  const height = bottom - top + 1;
  const minimum = Math.ceil((input.bottom - input.top) * 0.8);
  if (height < minimum || bottom >= painted.h - 2) {
    throw new Error(`color-picker-formats: ${model} Select button is clipped (${height}px visible, need ${minimum}px)`);
  }
}

async function openPicker(client: ControlClient, pid: number): Promise<number> {
  await openChipDropdown(client, pid, "color");
  const popup = findTopWindow(pid, "SumatraAnnotColorPopup");
  const raw = String((await client.request(ControlCommand.TestMarkupAnnots, []))[1] ?? "");
  const edit = /annotColorPopup .* edit=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(raw);
  if (!edit || !+edit[3]! || !+edit[4]!) throw new Error(`color-picker-formats: no Edit colors icon\n${raw}`);
  const bounds = getWindowRect(popup);
  await clickAt(popup, +edit[1]! + +edit[3]! / 2 - bounds.left, +edit[2]! + +edit[4]! / 2 - bounds.top, 0);
  return pollUntil(
    () => colorDialog(pid),
    (hwnd) => hwnd !== 0,
    { error: "color-picker-formats: picker did not open" },
  );
}

async function testAppearance(theme: string, scale: number): Promise<number> {
  const dir = tmpPath(`color-picker-formats-${theme}-${scale}`);
  rmSync(dir, { recursive: true, force: true });
  const appdata = join(dir, "appdata");
  mkdirSync(appdata, { recursive: true });
  writeFileSync(
    join(appdata, "SumatraPDFEnhanced-settings.txt"),
    `UiLanguage = en\nRestoreSession = false\nShowStartPage = false\nCheckForUpdates = false\nTheme = ${theme}\nInterfaceScale = ${scale}\n`,
  );
  const pdf = join(dir, "color.pdf");
  writeFileSync(
    pdf,
    assemblePdf([
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [4 0 R] >>",
      "<< /Type /Annot /Subtype /Square /P 3 0 R /Rect [72 420 192 540] /C [0.0666666667 0.3254901961 0.8980392157] /CA 0.5019607843 /BS << /W 2 >> >>",
    ]),
    "latin1",
  );
  parkCursorAway();
  const { proc, client, frame } = await launchControlled(["-appdata", appdata, pdf]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    sendCommandSync(frame, cmdId("CmdToggleEditPDF"));
    const raw = String((await client.request(ControlCommand.TestMarkupAnnots, []))[1] ?? "");
    const square = /type=Square[^\n]*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(raw);
    if (!square) throw new Error(`color-picker-formats: missing square\n${raw}`);
    await clickAt(findCanvas(frame), +square[1]! + +square[3]! / 2, +square[2]! + +square[4]! / 2, 0);
    const before = String((await client.request(ControlCommand.TestAnnotEditorLayout, [0, 0]))[1] ?? "");
    const loaded = / color=(\S+) interiorColor=\S+ opacity=(\d+)/.exec(before);
    if (!loaded || loaded[1] !== ORIGINAL_COLOR || Math.abs(+loaded[2]! - ORIGINAL_ALPHA) > 1) {
      throw new Error(`color-picker-formats: fixture color/opacity failed to load: ${before}`);
    }
    const initialAlpha = +loaded[2]!;
    console.log(`color-picker-formats: ${theme} ${scale}% loaded ${loaded[1]}/${initialAlpha}`);
    const dialog = await openPicker(client, proc.pid!);
    const combo = findChildWindow(dialog, "ComboBox");
    if (!combo || Number(sendMessage(combo, CB_GETCOUNT, 0, 0)) !== MODELS.length) {
      throw new Error("color-picker-formats: picker must offer RGB, HEX, CMYK, HSV and HSL");
    }

    // Merely changing representation must preserve every selected RGB byte.
    let inputHeight = 0;
    for (const model of MODELS) {
      const edits = await setModel(dialog, combo, model);
      const rect = getWindowRect(edits[0]!);
      inputHeight = rect.bottom - rect.top;
      await assertHex(dialog, combo, ORIGINAL_COLOR);
    }

    const cases: [Model, string[], string][] = [
      ["RGB", ["255", "128", "0"], "#ff8000"],
      ["HEX", ["0x12aBef"], "#12abef"],
      ["CMYK", ["0", "100%", "100%", "0"], "#ff0000"],
      ["HSV", ["120", "100%", "100%"], "#00ff00"],
      ["HSL", ["240", "100%", "50%"], "#0000ff"],
    ];
    for (const [model, values, expected] of cases) {
      const edits = await setModel(dialog, combo, model);
      values.forEach((value, i) => sendText(edits[i]!, value));
      assertButtonVisible(dialog, edits, model);
      if (
        (theme === "Sumatra Dark" && scale === 200 && model === "CMYK") ||
        (theme === "Sumatra Light" && scale === 100 && model === "HEX")
      ) {
        captureWindowToPng(dialog, join(dir, `picker-${model.toLowerCase()}.png`));
      }
      await assertHex(dialog, combo, expected);
      const invalidEdits = await setModel(dialog, combo, model);
      await assertInvalid(dialog, invalidEdits[0]!, model === "HEX" ? "#12345g" : "-1");
      await assertHex(dialog, combo, expected);
    }
    const [hex] = await setModel(dialog, combo, "HEX");
    sendText(hex!, "#0011aa");
    captureWindowToPng(dialog, join(dir, "picker.png"));
    await pressKey(hex!, VK_RETURN, 0);
    await pollUntil(
      () => colorDialog(proc.pid!),
      (hwnd) => hwnd === 0,
      { error: "color-picker-formats: Select did not close" },
    );
    await client.waitForRenderIdle();
    const result = String((await client.request(ControlCommand.TestAnnotEditorLayout, [0, 0]))[1] ?? "");
    const selected = / color=(\S+) interiorColor=\S+ opacity=(\d+)/.exec(result);
    if (!selected || selected[1] !== "#0011aa" || +selected[2]! !== initialAlpha) {
      throw new Error(
        `color-picker-formats: changed color or opacity incorrectly (initial ${initialAlpha}): ${result}`,
      );
    }
    console.log(`color-picker-formats: ${theme} ${scale}% modes editable; opacity ${initialAlpha} preserved`);
    return inputHeight;
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

export async function testit(): Promise<void> {
  for (const theme of ["Sumatra Light", "Sumatra Dark"]) {
    const small = await testAppearance(theme, 100);
    const large = await testAppearance(theme, 200);
    if (large < small * 1.5) {
      throw new Error(`color-picker-formats: ${theme} edit height failed to scale (${small} -> ${large})`);
    }
  }
  console.log("color-picker-formats: OK");
}

if (import.meta.main) await runStandalone(testit);
