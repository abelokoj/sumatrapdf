// #1136: keyboard navigation of the home page file list.
//  - arrows move a selection, drawn with a light blue outline
//  - Enter opens the selected file
//  - the first entry is selected at startup
//  - Up from the first row moves focus to the search box, Down there comes back
//  - filtering re-selects the first entry
//
// Everything is asserted through observable effects (which document opens, which
// control has focus) rather than pixels, so it doesn't depend on the drawing.
import { copyFileSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ROOT, runStandalone, tmpPath } from "./util";
import { sleep } from "./winapi";
import { findCanvas, findChildByClass, launchControlled, waitForTitle, killAndWait } from "./win-automation";
import type { ControlClient, HomeSelection } from "./control.ts";

const VK_RETURN = 0x0d;
const VK_UP = 0x26;
const VK_RIGHT = 0x27;
const VK_DOWN = 0x28;
const nFiles = 6;

function makeAppDir(name: string): string {
  const dir = tmpPath(`issue-1136-${name}`);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(join(dir, "sub"), { recursive: true });
  const src = join(ROOT, "ext", "a-zlib", "zlib.3.pdf");
  const states: string[] = [];
  for (let i = 0; i < nFiles; i++) {
    const p = join(dir, "sub", `doc-${String(i).padStart(2, "0")}.pdf`);
    copyFileSync(src, p);
    states.push(`\t[\n\t\tFilePath = ${p}\n\t\tOpenCount = ${nFiles - i}\n\t]`);
  }
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nRememberOpenedFiles = true\n` +
      `HomePageViewMode = thumbnails\nFileStates [\n${states.join("\n")}\n]\n`,
  );
  return dir;
}

// Send the actual Home key handler and capture its effect on the app thread,
// before another desktop activation can change the native edit focus.
async function key(client: ControlClient, target: "canvas" | "search", vk: number): Promise<HomeSelection> {
  await waitForHome(client, () => true, "Home layout was not ready before the key");
  const h = await client.homeSelection(target === "canvas" ? "canvas-key" : "search-key", vk);
  // Keys can invalidate the layout. Keep the original bounded readiness wait
  // before sampling its selection, without replaying the key.
  if (!h.ready && vk !== VK_RETURN) {
    return await waitForHome(client, () => true, "Home layout was not ready after the key");
  }
  return h;
}

function assertHome(h: HomeSelection, pred: (h: HomeSelection) => boolean, what: string): void {
  if (!h.ready || !pred(h)) {
    throw new Error(`issue-1136: ${what} (state: ${h.raw})`);
  }
}

// Waits for the home page to report the state a key was supposed to produce.
// Polling the app beats sleeping after each key: a key posted while the focus
// is still moving lands on the wrong window and is silently lost, which is what
// made this test flaky ("focus did not move", or the wrong file opened).
async function waitForHome(
  client: ControlClient,
  pred: (h: HomeSelection) => boolean,
  what: string,
  timeoutMs = 8000,
): Promise<HomeSelection> {
  const deadline = Date.now() + timeoutMs;
  let last: HomeSelection | null = null;
  for (;;) {
    last = await client.homeSelection();
    if (last.ready && pred(last)) {
      return last;
    }
    if (Date.now() > deadline) {
      throw new Error(`issue-1136: ${what} (last: ${last.raw})`);
    }
    await sleep(50);
  }
}

async function withHomePage(
  name: string,
  fn: (frame: number, canvas: number, searchEdit: number, client: ControlClient) => Promise<void>,
  launchArgs: string[] = [],
): Promise<void> {
  // Keep keyboard assertions independent of the user's mouse; do not move it.
  const { proc, client, frame } = await launchControlled(["-appdata", makeAppDir(name), ...launchArgs], {
    env: { SUMATRA_TEST_HOME_KEYBOARD_ONLY: "1" },
  });
  try {
    await client.waitForSessionRestored();
    const canvas = findCanvas(frame);
    if (!canvas) {
      throw new Error("issue-1136: home-page canvas not found");
    }
    // the first entry is selected once the home page has laid out; the search
    // box is created in the same pass and Up needs it to exist
    await waitForHome(
      client,
      (h) => h.entries === nFiles && h.searchBox && h.sel === 0 && !h.searchFocus,
      "home page never reached its initial selection",
    );
    const searchEdit = findChildByClass(canvas, "Edit");
    if (!searchEdit) {
      throw new Error("issue-1136: home-page search edit not found");
    }
    await fn(frame, canvas, searchEdit, client);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

export async function testit(): Promise<void> {
  // arrows move the selection and Enter opens it. The first entry is selected
  // at startup, so two Rights land on the third document
  const checkEnter = async (
    frame: number,
    canvas: number,
    _searchEdit: number,
    client: ControlClient,
  ): Promise<void> => {
    assertHome(
      await key(client, "canvas", VK_RIGHT),
      (h) => h.sel === 1,
      "Right did not move the selection to the second entry",
    );
    assertHome(
      await key(client, "canvas", VK_RIGHT),
      (h) => h.sel === 2,
      "Right did not move the selection to the third entry",
    );
    await key(client, "canvas", VK_RETURN);
    const title = await waitForTitle(frame, (t) => t.includes("doc-02.pdf"));
    if (!title.includes("doc-02.pdf")) {
      throw new Error(`Enter did not open the selected file, title: '${title}'`);
    }
  };
  await withHomePage("enter", checkEnter);
  // The hosted desktop is 1024x720. Crossing to the third entry at this size
  // also scrolls the thumbnails, which must preserve the keyboard selection.
  await withHomePage("enter-hosted-size", checkEnter, ["-window-pos", "1024x720@0x0"]);

  // Up from the first row goes to the search box, Down there comes back to the
  // list; then filtering re-selects the first (only) match, so Enter opens it
  await withHomePage("search", async (frame, canvas, searchEdit, client) => {
    assertHome(
      await key(client, "canvas", VK_UP),
      (h) => h.searchFocus,
      "Up from the first row did not focus the search box",
    );
    assertHome(
      await key(client, "search", VK_DOWN),
      (h) => !h.searchFocus,
      "Down did not move the focus back to the list",
    );

    // move off the first entry, then filter down to a single different file:
    // the selection must reset to it
    assertHome(
      await key(client, "canvas", VK_RIGHT),
      (h) => h.sel === 1,
      "Right did not move the selection off the first entry",
    );
    assertHome(
      await client.homeSelection("find-search"),
      (h) => h.searchFocus,
      "CmdFindFirst did not focus the search box",
    );
    for (const ch of "doc-04") {
      await client.homeSelection("search-char", ch.charCodeAt(0));
    }
    await waitForHome(
      client,
      (h) => h.entries === 1 && h.path.includes("doc-04.pdf"),
      "typing in the search box did not filter down to doc-04",
    );
    assertHome(
      await key(client, "search", VK_DOWN),
      (h) => !h.searchFocus,
      "Down did not move the focus to the filtered list",
    );
    await key(client, "canvas", VK_RETURN);
    const title = await waitForTitle(frame, (t) => t.includes("doc-04.pdf"));
    if (!title.includes("doc-04.pdf")) {
      throw new Error(`filtering should re-select the first match, opened title: '${title}'`);
    }
  });
}

if (import.meta.main) {
  await runStandalone(testit);
}
