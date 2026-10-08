// Switching the global find-UI mode must migrate every visible find UI, not
// leave compact and floating variants mixed across multiple windows.
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand, type ControlClient } from "./control.ts";
import { cmdId, ROOT, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import { enumChildWindows, getClassName, sendMessage, setProcessDpiAware, setWindowPos } from "./winapi.ts";

const WM_GETFONT = 0x0031;

type FindUiState = {
  windows: number;
  docs: number;
  pref: number;
  compact: number;
  floating: number;
  firstTextLen: number;
  raw: string;
};

async function findUiRequest(client: ControlClient, action: string): Promise<FindUiState> {
  const response = await client.request(ControlCommand.TestFindUiState, [action]);
  const code = Number(response[0] ?? -1);
  const raw = String(response[1] ?? "");
  if (code !== 0) {
    throw new Error(`find-ui-state: ${action} failed (${code}): ${raw.trim()}`);
  }
  const values: Record<string, number> = {};
  for (const match of raw.matchAll(/(\w+)=(\d+)/g)) {
    values[match[1]] = Number(match[2]);
  }
  return {
    windows: values.windows ?? 0,
    docs: values.docs ?? 0,
    pref: values.pref ?? -1,
    compact: values.compact ?? 0,
    floating: values.floating ?? 0,
    firstTextLen: values.firstTextLen ?? -1,
    raw,
  };
}

async function waitForTwoDocuments(client: ControlClient): Promise<void> {
  const deadline = Date.now() + 8000;
  let state = await findUiRequest(client, "state");
  while ((state.windows !== 2 || state.docs !== 2) && Date.now() < deadline) {
    await Bun.sleep(40);
    state = await findUiRequest(client, "state");
  }
  if (state.windows !== 2 || state.docs !== 2) {
    throw new Error(`find-ui-state: duplicate window did not load: ${state.raw.trim()}`);
  }
}

function expectState(state: FindUiState, pref: number, compact: number, floating: number): void {
  if (state.pref !== pref || state.compact !== compact || state.floating !== floating) {
    throw new Error(
      `find-ui-state: expected pref=${pref} compact=${compact} floating=${floating}, got ${state.raw.trim()}`,
    );
  }
}

async function testToolbarSizing(): Promise<void> {
  setProcessDpiAware();
  for (const scale of [100, 150, 200]) {
    const dir = tmpPath(`find-toolbar-${scale}`);
    mkdirSync(dir, { recursive: true });
    writeFileSync(
      join(dir, "SumatraPDFEnhanced-settings.txt"),
      `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nSearchUIFloating = false\nToolbarSize = 24\nInterfaceScale = ${scale}\nTheme = Sumatra Light\n`,
    );
    const pdf = join(ROOT, "ext", "a-zlib", "zlib.3.pdf");
    const { proc, client, frame } = await launchControlled(["-appdata", dir, "-window-pos", "2400x1000@60x60", pdf]);
    try {
      await client.waitForRenderIdle();
      for (const theme of ["light", "dark"]) {
        if (theme === "dark") sendCommandSync(frame, cmdId("CmdToggleLightDarkTheme"));
        await findUiRequest(client, "show-all");
        const state = await findUiRequest(client, "state");
        const metrics = Object.fromEntries([...state.raw.matchAll(/(\w+)=(\d+)/g)].map((m) => [m[1], Number(m[2])]));
        if (metrics.slot <= 0 || metrics.bar > metrics.slot || metrics.widen !== 1 || metrics.close !== 1) {
          throw new Error(`find toolbar ${scale}% ${theme}: oversized row or missing controls: ${state.raw}`);
        }
        let pageFont = 0n;
        let searchFont = 0n;
        enumChildWindows(frame, (child) => {
          if (getClassName(child) === "Edit") pageFont = sendMessage(child, WM_GETFONT, 0, 0);
          return true;
        });
        enumChildWindows(metrics.hwnd, (child) => {
          if (getClassName(child) === "Edit") searchFont = sendMessage(child, WM_GETFONT, 0, 0);
          return true;
        });
        if (!pageFont || pageFont !== searchFont) throw new Error("Search font differs from the toolbar font");
        for (const width of [1200, 1024, 800]) {
          setWindowPos(frame, 60, 60, width, 720);
          const resized = await findUiRequest(client, "state");
          const resizedMetrics = Object.fromEntries(
            [...resized.raw.matchAll(/(\w+)=(\d+)/g)].map((m) => [m[1], Number(m[2])]),
          );
          if (
            resizedMetrics.slot <= 0 ||
            resizedMetrics.bar > resizedMetrics.slot ||
            resizedMetrics.widen !== 1 ||
            resizedMetrics.close !== 1
          )
            throw new Error(`Resizing to ${width}px clipped search controls: ${resized.raw}`);
        }
        setWindowPos(frame, 60, 60, 2400, 1000);
        await findUiRequest(client, "set-first-text");
        let switched = await findUiRequest(client, "toggle-first");
        expectState(switched, 1, 0, 1);
        if (switched.firstTextLen !== "stale-term".length) throw new Error("Widening lost the query");
        switched = await findUiRequest(client, "toggle-first");
        expectState(switched, 0, 1, 0);
        if (switched.firstTextLen !== "stale-term".length) throw new Error("Docking lost the query");
      }
    } finally {
      client.close();
      await killAndWait(proc);
    }
  }
}

export async function testit(): Promise<void> {
  await testToolbarSizing();
  const dir = tmpPath("find-ui-state");
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nSearchUIFloating = false\n",
  );

  const pdf = join(ROOT, "ext", "a-zlib", "zlib.3.pdf");
  const { proc, client, frame } = await launchControlled(["-appdata", dir, pdf]);
  try {
    await client.waitForRenderIdle();
    sendCommandSync(frame, cmdId("CmdDuplicateInNewWindow"));
    await waitForTwoDocuments(client);

    expectState(await findUiRequest(client, "show-all"), 0, 2, 0);
    expectState(await findUiRequest(client, "toggle-first"), 1, 0, 2);

    let state = await findUiRequest(client, "set-first-text");
    if (state.firstTextLen !== "stale-term".length) {
      throw new Error(`find-ui-state: failed to seed floating term: ${state.raw.trim()}`);
    }
    await findUiRequest(client, "toggle-first");
    state = await findUiRequest(client, "clear-first");
    if (state.firstTextLen !== 0) {
      throw new Error(`find-ui-state: failed to clear compact term: ${state.raw.trim()}`);
    }
    state = await findUiRequest(client, "toggle-first");
    if (state.firstTextLen !== 0) {
      throw new Error(`find-ui-state: empty term was replaced after switching: ${state.raw.trim()}`);
    }

    // Return to compact mode, hide the first bar, and recreate it through the
    // same path used by theme changes. Its hidden term still backs F3.
    expectState(await findUiRequest(client, "toggle-first"), 0, 2, 0);
    state = await findUiRequest(client, "set-first-text");
    if (state.firstTextLen !== "stale-term".length) {
      throw new Error(`find-ui-state: failed to seed hidden-term test: ${state.raw.trim()}`);
    }
    expectState(await findUiRequest(client, "hide-first"), 0, 1, 0);
    state = await findUiRequest(client, "theme-recreate-first");
    if (state.firstTextLen !== "stale-term".length) {
      throw new Error(`find-ui-state: hidden term was lost during theme recreation: ${state.raw.trim()}`);
    }
    console.log("find-ui-state: OK");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) {
  await runStandalone(testit);
}
