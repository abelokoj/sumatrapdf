<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/enhanced-wordmark-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/images/enhanced-wordmark-light.svg">
    <img src="docs/images/enhanced-wordmark-light.svg" width="960" alt="SumatraPDF Enhanced">
  </picture><br>

<a href="https://github.com/abelokoj/sumatrapdf-enhanced/releases/latest"><img src="https://img.shields.io/github/v/release/abelokoj/sumatrapdf-enhanced?label=release&amp;color=168349" alt="Latest release"></a>
<a href="https://github.com/abelokoj/sumatrapdf-enhanced/releases"><img src="https://img.shields.io/github/downloads/abelokoj/sumatrapdf-enhanced/total?color=168349" alt="Total release downloads"></a>
<a href="https://github.com/abelokoj/sumatrapdf-enhanced/actions/workflows/release-windows.yml"><img src="https://github.com/abelokoj/sumatrapdf-enhanced/actions/workflows/release-windows.yml/badge.svg" alt="Release build status"></a>
<a href="https://github.com/abelokoj/sumatrapdf-enhanced/stargazers"><img src="https://img.shields.io/github/stars/abelokoj/sumatrapdf-enhanced?style=flat&amp;color=eab308" alt="GitHub stars"></a>
<a href="COPYING"><img src="https://img.shields.io/badge/license-GPL_v3-168349" alt="GPL v3 application license"></a>
<img src="https://img.shields.io/badge/platform-Windows_x64_%7C_ARM64-168349" alt="Windows x64 and ARM64">
</p>

<p align="center"><strong>Read, annotate and learn in one place.</strong><br>A native Windows document reader with customizable pen tools, offline dictionaries and vocabulary practice.<br><em>Built on <a href="https://github.com/sumatrapdfreader/sumatrapdf">SumatraPDF</a>, with visual inspiration from <a href="https://github.com/JaviLendi/PrettySumatraPDF">PrettySumatraPDF</a>.</em></p>

<p align="center"><a href="#why-i-built-this">Why I built this</a> · <a href="#overview">Overview</a> · <a href="#download">Download</a> · <a href="#features">Features</a> · <a href="#get-started">Get started</a> · <a href="#upstream">Upstream source</a> · <a href="#development">Development</a> · <a href="#support">Support</a></p>

---

<a id="why-i-built-this"></a>

## Why I built this

Reading a paper or textbook is more than moving through pages. It means marking an argument, looking up an unfamiliar word, keeping useful notes and returning to what you have learned. I wanted those tasks to fit into one reading environment, rather than require a separate application for each step.

SumatraPDF already provides the document-reading foundation. My aim is to extend it, not replace it: customizable pens for annotation, a temporary laser for presentation, and offline dictionaries with vocabulary practice that keeps words connected to their source documents. Appearance controls let readers adjust the interface to their needs.

That is why I am building SumatraPDF Enhanced: to make reading, annotation and learning work together in a native Windows application. Core dictionary lookup and vocabulary review work offline, and the source remains open for others to inspect, adapt and improve. The project builds on the work of SumatraPDF and other open-source contributors, whose contributions are credited below.

---

<a id="overview"></a>

## 🚀 Overview

Keep SumatraPDF's document-reading foundation and add tools for studying, writing and presentation. Open PDF, EPUB, MOBI, CBZ, CBR, FB2, CHM, XPS and DjVu documents, customize the reading interface, annotate PDFs and build your vocabulary as you read.

<table>
<tr>
<td width="33%" valign="top"><h3>📚 Read and organize</h3><p>Search recent documents, resume reading and choose themes, fonts and interface sizes.</p></td>
<td width="33%" valign="top"><h3>✍️ Write and present</h3><p>Choose pen profiles, pin annotation presets and use a temporary laser pointer.</p></td>
<td width="33%" valign="top"><h3>🧠 Learn offline</h3><p>Look up words with Shift+D, save vocabulary and practice with six learning activities.</p></td>
</tr>
</table>

### ✨ What Enhanced adds

| Area            | Enhanced additions                                                           | Retained upstream foundation                |
| --------------- | ---------------------------------------------------------------------------- | ------------------------------------------- |
| 📖 Dictionary   | Embedded offline definitions, vocabulary lists and study packs               | Document reading, text selection and search |
| 🎓 Learning     | Flashcards, quizzes, spelling, word scramble, matching and review scheduling | Reading positions and document navigation   |
| 🖊️ Annotation   | Pen profiles, pinned color and width presets and fractional-width controls   | Native PDF annotations and saving           |
| 🔦 Presentation | Temporary laser with solid, hollow and dot styles                            | Page display, zoom and document inversion   |
| 🎨 Appearance   | Pretty themes, bundled UI fonts and expanded appearance controls             | Native Windows reader and settings          |

This comparison refers to the upstream source snapshot recorded below. Reference previews, native annotations and document inversion are upstream capabilities that Enhanced retains and extends.

---

<a id="download"></a>

## 📥 Download

**[Latest release and notes](https://github.com/abelokoj/sumatrapdf-enhanced/releases/latest)**

| Device              | Installer                                                                                                                              | Standalone portable                                                                                                                    | Portable ZIP                                                                                                                           |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| x64 (Intel and AMD) | [Installer EXE](https://github.com/abelokoj/sumatrapdf-enhanced/releases/download/enhanced-v0.1.1/SumatraPDF-Enhanced-v0.1.1-x64-install.exe)   | [Portable EXE](https://github.com/abelokoj/sumatrapdf-enhanced/releases/download/enhanced-v0.1.1/SumatraPDF-Enhanced-v0.1.1-x64-portable.exe)   | [Portable ZIP](https://github.com/abelokoj/sumatrapdf-enhanced/releases/download/enhanced-v0.1.1/SumatraPDF-Enhanced-v0.1.1-x64-portable.zip)   |
| ARM64               | [Installer EXE](https://github.com/abelokoj/sumatrapdf-enhanced/releases/download/enhanced-v0.1.1/SumatraPDF-Enhanced-v0.1.1-arm64-install.exe) | [Portable EXE](https://github.com/abelokoj/sumatrapdf-enhanced/releases/download/enhanced-v0.1.1/SumatraPDF-Enhanced-v0.1.1-arm64-portable.exe) | [Portable ZIP](https://github.com/abelokoj/sumatrapdf-enhanced/releases/download/enhanced-v0.1.1/SumatraPDF-Enhanced-v0.1.1-arm64-portable.zip) |

Run the installer to install the application, or run the standalone portable EXE directly. Both include the offline dictionary. The ZIP includes the reader, dictionary data, license notices and optional shell integration helpers; extract the entire folder before running **SumatraPDF.exe**.

---

<a id="features"></a>

## ✨ Features included in Enhanced

### 📖 Offline dictionary and learning

- Select a word and press **Shift+D** for an offline definition, or enter a word manually.
- The original WMKeyboard vocabulary definitions in eleven study packs, with WordNet English for broader offline coverage. Original data and attribution are bundled in this repository.
- Import supported WM JSON/gzip, TSV and StarDict dictionaries; explicitly download and update packs. Offline lookup is the default, and practice works offline.
- Download optional [Kaikki English](https://kaikki.org/dictionary/English/) and [Simple English](https://kaikki.org/simplewiktionary/) dictionaries. Current download sizes are shown before confirmation, with progress and cancellation. English is approximately 3.34 GB; Simple English is approximately 4.72 MB compressed and 37.90 MB expanded, plus an offline index. These packs are downloaded separately from the app.
- Read separate, numbered meanings with parts of speech, examples, pronunciation and related words when the dictionary provides them.
- Choose optional online lookup through Wiktionary, Wiktionary REST or Free Dictionary API, and set the source order.
- Hear words with installed Windows voices offline, or explicitly play available US and UK pronunciation recordings.
- Save vocabulary with meanings, reading context, source, page and learned status; organize custom lists and import and export backups.
- Study Word Smart, Barron's, Magoosh, Kaplan, Powerscore, SparkNotes, Manhattan and GregMat lists.
- A home learning hub with word of the day, due reviews, study ahead and SM-2/Leitner scheduling.
- Six activities: **flashcards, meaning quiz, word quiz, spelling, word scramble and matching pairs**.
- Resize word and answer panels with a divider or keyboard controls, and replay the guide whenever needed.
- Compact dictionary and library controls, with dictionary and deck tools that expand when needed. Practice keeps setup out of the way and emphasizes checking or advancing your answer.
- Undo the last 20 word removals during the session, preserving definitions, PDF context, deck memberships and review history.
- Persistent activity, review-method and voice labels, with help explaining SM-2, Leitner and learned status.
- Green `✓` marks for completed deck installation, with separate counts for words whose source definitions are unavailable. Check/Next, Back and answer feedback share a compact row where space permits.

### ✍️ Pen and presentation tools

- Initial ballpoint, fountain, brush, pencil and highlighter profiles with settings that can be expanded or hidden.
- Pin favorite annotation presets, including different colors and widths of the same pen.
- Pin PDF editing tools to the main toolbar; right-click a tool to pin it and a pinned button to remove it. Pinned pens show their own profile icon and preset color.
- Select annotations with a freehand lasso, then drag to move or resize them, use arrow keys to adjust their position, or press Delete. Undo and redo retain the PDF annotation data.
- Use the hand tool to drag the page while keeping drawing tools inactive.
- Default pen thickness **0.1–16 pt in 0.1 pt steps**, with configurable bounds and increments.
- Native PDF ink preserves fractional widths and pen-profile metadata for editing and erasing after saving and reopening, including on another Enhanced installation.
- Stroke/highlighter-only erasing, touch rejection for annotation input, coalesced stylus input and bounded repainting for responsiveness. Touch scrolling and zoom remain available while a pen tool is selected.
- Temporary laser pointer with **solid trail, hollow trail or single dot**, and preset and custom colors.
- Completed laser strokes remain for the selected duration after switching tools.

### 🎨 Interface and reading improvements

- Green application logo across the reader, home page, dialogs, installer and PDF file associations.
- Pretty-style welcome page, document search, Resume last and recent-document cards.
- Compact home header with the green logo beside the bold app name and more space for recent files.
- Content-sized Settings with wrapping fields and help text, and an OK/Cancel footer that stays visible while scrolling.
- Settings presets also accept custom values. Choices load on first use, reopening reuses text measurements, and app-owned fields, panels and menus use consistent rounded corners.
- Independent text and page-background colors for the current PDF, with color pickers and a reset to the selected theme.
- Clear tab separators and a scrollable open-file dropdown with ten visible entries by default; its visible count and scrollbar width are adjustable.
- A right scrollbar for recent documents and portable session restoration after closing the app.
- [Edit PDF bookmarks](docs/cpdf-bookmarks.md) with an optional cpdf download, or use your own executable. Change titles, pages, hierarchy and order, then save a separate PDF copy. The tool is not bundled; the download prompt shows its size and license.
- Rounded toolbar groups, expandable controls, twelve Pretty theme presets and right-side day, night and document-inversion actions.
- Scroll crowded toolbars left or right with the wheel or navigation arrows, or choose a tool from the scrollable dropdown.
- Use **Show or hide toolbar items** in the toolbar dropdown to save your preferred visibility, independently of pinned presets.
- Compact, rounded pen and laser palettes wrap their controls and scroll when needed, keeping final actions and instructions reachable.
- Rounded Lucide core icons and bundled **Manrope, Pretendard Std and Public Sans**, alongside System font selection.
- Main Appearance controls for interface and sidebar text, icon size, thumbnail size, recent-document count and minimum tab width; scrolling tab overflow.
- Sharper enlarged recent thumbnails and theme-aware control and icon refresh.
- Inline custom zoom entry and 25-percentage-point default zoom steps above 100%.
- Reference hover previews built on upstream functionality, with configurable delay, document-cache fixes and draggable edges and corners for resizing. Previews follow document inversion and current-file foreground/background colors.

Dictionary and vocabulary learning, the temporary laser, pen profiles, pinned presets and bundled interface-font selection are Enhanced additions. Existing upstream features such as native annotations, text search, document inversion and reference previews are retained and extended; they are not presented as entirely new inventions.

---

<a id="get-started"></a>

## 🛠️ Get started

### 1. Choose your download

Use **x64** for most Intel and AMD computers, or **ARM64** for a device running Windows on ARM. Choose the installer for a normal installation, the standalone portable EXE to run directly, or the ZIP for the reader with optional shell helpers. The download table above links to **v0.1.1**; the latest-release link always opens the current release.

### 2. Set up your reading interface

Open **Settings** and use **Appearance** to choose System, Manrope, Pretendard Std or Public Sans. Adjust interface and sidebar text, icons, home thumbnails, the recent-document limit and minimum tab width. Use the toolbar's theme controls to change the reading appearance.

### 3. Look up and keep a word

1. Open a document and select a word.
2. Press **Shift+D** to open its offline definition. You can also enter a word manually.
3. Save the word to your vocabulary with its meaning and reading context.
4. Visit the home learning hub to review saved words or study a bundled pack.

The offline dictionary is included in both the installer and standalone portable EXE. Supported dictionary imports and explicit pack downloads are available for extending the dictionary collection.

### 4. Practice and annotate

Choose flashcards, meaning quiz, word quiz, spelling, word scramble or matching pairs in the learning hub. For PDF annotations, select a pen profile, color and width, then pin combinations you use often. Save the PDF to retain its annotations for later editing in Enhanced.

---

<a id="upstream"></a>

## 🌿 Upstream source

**v0.1.1 is based on the original SumatraPDF 3.7 source snapshot**, commit [`a0d8bcaed0412ce803d9c213845e710fcbe3c7a5`](https://github.com/sumatrapdfreader/sumatrapdf/commit/a0d8bcaed0412ce803d9c213845e710fcbe3c7a5), dated **September 30, 2026, 09:55:54 UTC**. This identifies the upstream source snapshot, not an official upstream stable-release date. Every Enhanced release records its upstream version, commit and date.

<a id="development"></a>

## ⚙️ Development and builds

`master` contains the current Enhanced source. [GitHub-hosted Windows builds](https://github.com/abelokoj/sumatrapdf-enhanced/actions/workflows/build-windows.yml) produce x64 and ARM64 installers, standalone portable executables and portable ZIP packages on pushes/pull requests and manual runs. Build locally through `bun cmd/build.ts -dbg`, `bun cmd/build.ts -rel` or `bun cmd/build.ts -rel -arm64`, following [agents.md](agents.md). Standalone builds use `bun cmd/build.ts -rel -static` and `bun cmd/build.ts -rel -arm64 -static`.

The native reader and interface do not require the Microsoft Edge browser. Optional upstream AI, manual and CHM browser integrations need the separate WebView2 runtime.

Reference previews require supported local destinations. Advanced handwriting recognition and cleanup are not available in this release. Laser behavior and pen responsiveness are still being refined.

### 🔄 Automatic releases

To publish a new version, update `enhanced-version.txt` (for example, `v0.1.2`), add a matching public entry to [CHANGELOG.md](CHANGELOG.md), and push both changes to `master`. GitHub builds and checks x64 and ARM64, creates the `enhanced-vX.Y.Z` tag at the source commit, and publishes four executables plus two portable ZIPs. The release notes include the matching changelog entry and the upstream version, commit and date.

A release can also be started through **Actions > Enhanced release > Run workflow** on `master`, using the version recorded in `enhanced-version.txt`. Failed builds or package checks block publication. An upload failure leaves any incomplete release as a draft. Existing published versions are preserved; choose a new version for subsequent changes. Release notes appear in the release description, and the downloads contain only application packages.

---

<a id="support"></a>

## 🤝 Support and feedback

Use [GitHub Issues](https://github.com/abelokoj/sumatrapdf-enhanced/issues) to report a problem or request a feature. Include the Enhanced version, Windows version, device architecture and steps to reproduce the problem. For pen issues, include the device and stylus model. Remove personal document content from any screenshots or sample files you share.

## 📜 Credits and license

Based on [official SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf), with visual inspiration from [JaviLendi/PrettySumatraPDF](https://github.com/JaviLendi/PrettySumatraPDF) and learning-data/design references from [wmkeyboard](https://github.com/wasi-master/wmkeyboard).

The application follows the upstream (A)GPLv3/BSD licensing; see [COPYING](COPYING), [COPYING.BSD](COPYING.BSD) and [AUTHORS](AUTHORS). Bundled fonts, icons and dictionary and study data retain their separate licenses and attribution in [license notices](docs/licenses), [font attribution](docs/font-attribution.md), [icon attribution](docs/icon-attribution.md) and [vocabulary attribution](docs/vocabulary-attribution.md).

Upstream resources: [website](https://www.sumatrapdfreader.org/free-pdf-reader) · [manual](https://www.sumatrapdfreader.org/manual) · [contribution information](https://www.sumatrapdfreader.org/docs/Contribute-to-SumatraPDF).
