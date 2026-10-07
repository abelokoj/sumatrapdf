const REPOSITORY = "abelokoj/sumatrapdf-enhanced";
const RELEASES_URL = `https://github.com/${REPOSITORY}/releases`;
const PACKAGE_TYPES = ["install.exe", "portable.exe", "portable.zip"];
const ARCHITECTURES = ["x64", "arm64"];
const FALLBACK_VERSION = "v0.1.1";
const FALLBACK_TAG = `enhanced-${FALLBACK_VERSION}`;

let release = { version: FALLBACK_VERSION, tag: FALLBACK_TAG, assets: null };

function updateDownloads() {
  const architecture = document.querySelector('input[name="architecture"]:checked').value;

  document.querySelectorAll("[data-package]").forEach((link) => {
    const type = link.dataset.package;
    const assetName = `SumatraPDF-Enhanced-${release.version}-${architecture}-${type}`;
    const asset = release.assets?.find((item) => item.name === assetName);
    link.href = asset?.browser_download_url ?? `${RELEASES_URL}/download/${release.tag}/${assetName}`;
    link.setAttribute(
      "aria-label",
      `Download ${link.querySelector("strong").textContent} for Windows ${architecture}, ${release.version}`,
    );
  });

  document.getElementById("release-version").textContent = release.version;
}

async function loadRelease() {
  try {
    const response = await fetch(`https://api.github.com/repos/${REPOSITORY}/releases/latest`, {
      headers: { Accept: "application/vnd.github+json" },
      signal: AbortSignal.timeout(8000),
    });
    if (!response.ok) return;

    const latest = await response.json();
    if (!/^enhanced-v\d+\.\d+\.\d+$/.test(latest.tag_name) || !Array.isArray(latest.assets)) return;

    const version = latest.tag_name.slice("enhanced-".length);
    const complete = ARCHITECTURES.every((architecture) =>
      PACKAGE_TYPES.every((type) =>
        latest.assets.some(
          (asset) =>
            asset.name === `SumatraPDF-Enhanced-${version}-${architecture}-${type}` &&
            typeof asset.browser_download_url === "string" &&
            asset.browser_download_url.startsWith(`${RELEASES_URL}/download/`),
        ),
      ),
    );
    if (!complete) return;

    release = { version, tag: latest.tag_name, assets: latest.assets };
    updateDownloads();
    const status = document.getElementById("release-status");
    status.replaceChildren(document.createTextNode(`Latest release: ${version}. `));
    const notes = document.createElement("a");
    notes.href = `${RELEASES_URL}/tag/${latest.tag_name}`;
    notes.textContent = "Read the release notes";
    status.append(notes, document.createTextNode("."));
  } catch {
    // Keep the verified release links available when GitHub cannot be reached.
  }
}

const stage = document.querySelector(".document-stage");
const demoStatus = document.getElementById("demo-status");
const saveButton = document.getElementById("save-word");
const modeDescriptions = {
  read: "A quiet space for your next good read.",
  highlight: "Example: mark a word with your highlighter.",
  dictionary: "Example: an offline definition, beside your reading.",
};

document.querySelectorAll("[data-mode][aria-pressed]").forEach((button) => {
  button.addEventListener("click", () => {
    const mode = button.dataset.mode;
    stage.dataset.mode = mode;
    document.querySelector(".definition").hidden = mode !== "dictionary";
    document.querySelector(".pen-palette").hidden = mode !== "highlight";
    document.querySelectorAll("[data-mode][aria-pressed]").forEach((item) => {
      item.setAttribute("aria-pressed", String(item === button));
    });
    demoStatus.textContent = modeDescriptions[mode];
  });
});

saveButton.addEventListener("click", () => {
  const saved = saveButton.getAttribute("aria-pressed") !== "true";
  saveButton.setAttribute("aria-pressed", String(saved));
  saveButton.textContent = saved ? "✓ Saved in this demo" : "+ Save to vocabulary";
  demoStatus.textContent = saved ? "Example word saved for this preview session." : modeDescriptions.dictionary;
});

document.querySelectorAll('input[name="architecture"]').forEach((input) => {
  input.addEventListener("change", updateDownloads);
});

updateDownloads();
loadRelease();
