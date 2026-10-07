// Wheel bursts keep their full distance, settle promptly, and reverse without
// leaving a stale destination. Uses a warmed PDF so this measures input easing.
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve, sep } from "node:path";
import { cmdId, makePdf, runStandalone, TESTS_TMP_DIR, tmpPath } from "./util.ts";
import {
  clientToScreen,
  getScrollInfo,
  packCoords,
  sendMessage,
  setCursorPos,
  setProcessDpiAware,
  sleep,
} from "./winapi.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

const WM_MOUSEWHEEL = 0x020a;
const WM_VSCROLL = 0x0115;
const SB_TOP = 6;

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const directory = tmpPath("pdf-scroll-responsiveness");
  if (!resolve(directory).startsWith(resolve(TESTS_TMP_DIR) + sep)) throw new Error("Unsafe test directory");
  rmSync(directory, { recursive: true, force: true });
  mkdirSync(directory, { recursive: true });
  writeFileSync(
    join(directory, "SumatraPDFEnhanced-settings.txt"),
    "SmoothScroll = true\nScrollLineAmount = 16\nScrollEdgeTurnsPage = false\nMouseWheelTurnsPage = false\nRestoreSession = false\nCheckForUpdates = false\n",
  );
  const pdf = join(directory, "reader.pdf");
  writeFileSync(pdf, makePdf(8, 612, 0, 0), "latin1");
  const { proc, client, frame } = await launchControlled(["-appdata", directory, pdf]);
  try {
    sendCommandSync(frame, cmdId("CmdZoomFitWidthAndContinuous"));
    await client.waitForRenderIdle();
    const canvas = findCanvas(frame);
    if (!canvas) throw new Error("PDF canvas missing");
    const point = clientToScreen(canvas, 250, 250);
    setCursorPos(point.x, point.y);
    const wheel = (delta: number) =>
      sendMessage(canvas, WM_MOUSEWHEEL, packCoords(0, delta), packCoords(point.x, point.y));
    sendMessage(canvas, WM_VSCROLL, SB_TOP, 0);
    wheel(-120);
    await client.waitForRenderIdle();
    const notch = getScrollInfo(canvas).pos;
    if (notch <= 0) throw new Error("Wheel did not move the reader");
    const latencies: number[] = [];
    for (let attempt = 0; attempt < 3; attempt++) {
      sendMessage(canvas, WM_VSCROLL, SB_TOP, 0);
      const started = performance.now();
      for (let i = 0; i < 6; i++) wheel(-120);
      const target = 6 * notch;
      let previous = 0;
      while (getScrollInfo(canvas).pos < target * 0.95 && performance.now() - started < 1500) {
        const position = getScrollInfo(canvas).pos;
        if (position < previous || position > target) throw new Error("Wheel burst bounced or overshot");
        previous = position;
        await sleep(5);
      }
      latencies.push(performance.now() - started);
      await client.waitForRenderIdle();
      if (getScrollInfo(canvas).pos !== target) throw new Error("Fast wheel input lost part of its distance");
    }
    latencies.sort((a, b) => a - b);
    if (latencies[1]! > 180)
      throw new Error(`PDF wheel response is sluggish: median 95% travel ${latencies[1]!.toFixed(1)} ms`);
    sendMessage(canvas, WM_VSCROLL, SB_TOP, 0);
    for (let i = 0; i < 12; i++) wheel(-120);
    for (let i = 0; i < 5; i++) wheel(120);
    await client.waitForRenderIdle();
    if (getScrollInfo(canvas).pos !== 7 * notch) throw new Error("Reversing the wheel left a stale target");
    console.log(`PDF wheel median 95% travel: ${latencies[1]!.toFixed(1)} ms; bursts and reversal: OK`);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
