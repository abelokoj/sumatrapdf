# SumatraPDF Enhanced

[![Windows builds](https://github.com/abelokoj/sumatrapdf/actions/workflows/build-windows.yml/badge.svg?branch=master)](https://github.com/abelokoj/sumatrapdf/actions/workflows/build-windows.yml)

A Windows document reader built on official SumatraPDF, with a Pretty-style native interface, richer pen tools and offline dictionary and vocabulary learning. Supports the upstream reader's PDF, EPUB, MOBI, CBZ, CBR, FB2, CHM, XPS and DjVu formats.

## Download

**[Latest release and notes](https://github.com/abelokoj/sumatrapdf/releases/latest)**

- [x64 portable (Intel and AMD computers)](https://github.com/abelokoj/sumatrapdf/releases/download/enhanced-v0.1.0/SumatraPDF-Enhanced-v0.1.0-x64-portable.zip)
- [ARM64 portable (ARM devices)](https://github.com/abelokoj/sumatrapdf/releases/download/enhanced-v0.1.0/SumatraPDF-Enhanced-v0.1.0-arm64-portable.zip)

- [x64 installer](https://github.com/abelokoj/sumatrapdf/releases/download/enhanced-v0.1.0/SumatraPDF-Enhanced-v0.1.0-x64-install.exe)
- [ARM64 installer](https://github.com/abelokoj/sumatrapdf/releases/download/enhanced-v0.1.0/SumatraPDF-Enhanced-v0.1.0-arm64-install.exe)

Run the installer for your device to install the application. For a portable copy, extract the entire ZIP archive and run **SumatraPDF.exe**, keeping the supplied `dictionaries` folder beside it. Both formats include offline dictionary data and required licenses.

## Features included in Enhanced

### Offline dictionary and learning

- Select a word and press **Shift+D** for an offline definition, or enter a word manually.
- The same original wmkeyboard vocabulary-pack definitions: eleven study packs and 3,890 unique meanings, with WordNet English as an offline fallback. Original data and attribution are bundled in this repository.
- Import supported WM JSON/gzip, TSV and StarDict dictionaries; explicitly download and update packs. Lookup and practice stay offline.
- Save vocabulary with meanings, reading context, source, page and learned status; organize custom lists and import and export backups.
- Study Word Smart, Barron's, Magoosh, Kaplan, Powerscore, SparkNotes, Manhattan and GregMat lists.
- A home learning hub with word of the day, due reviews, study ahead and SM-2/Leitner scheduling.
- Six activities: **flashcards, meaning quiz, word quiz, spelling, word scramble and matching pairs**.

### Pen and presentation tools

- Initial ballpoint, fountain, brush, pencil and highlighter profiles with settings that can be expanded or hidden.
- Pin favorite annotation presets, including different colors and widths of the same pen.
- Default pen thickness **0.1–16 pt in 0.1 pt steps**, with configurable bounds and increments.
- Native PDF ink preserves fractional widths and pen-profile metadata for editing and erasing after saving and reopening, including on another Enhanced installation.
- Stroke/highlighter-only erasing, touch suppression while writing, coalesced stylus input and bounded repainting for responsiveness.
- Temporary laser pointer with **solid trail, hollow trail or single dot**, and preset and custom colors.

### Interface and reading improvements

- Pretty-style welcome page, document search, Resume last and recent-document cards.
- Rounded toolbar groups, expandable controls, twelve Pretty theme presets and right-side day, night and document-inversion actions.
- Rounded Lucide core icons and bundled **Manrope, Pretendard Std and Public Sans**, alongside System font selection.
- Main Appearance controls for interface and sidebar text, icon size, thumbnail size, recent-document count and minimum tab width; scrolling tab overflow.
- Sharper enlarged recent thumbnails and theme-aware control and icon refresh.
- Inline custom zoom entry and 25-percentage-point default zoom steps above 100%.
- Reference hover previews built on upstream functionality, with configurable delay and document-cache fixes.

Dictionary and vocabulary learning, the temporary laser, pen profiles and pinned presets and bundled UI-font selection are Enhanced additions. Existing upstream features such as native annotations, text search, document inversion and reference previews are retained and extended; they are not presented as entirely new inventions.

## Upstream basis and development

**v0.1.0 is based on the original SumatraPDF 3.7 source snapshot**, commit [`a0d8bcaed0412ce803d9c213845e710fcbe3c7a5`](https://github.com/sumatrapdfreader/sumatrapdf/commit/a0d8bcaed0412ce803d9c213845e710fcbe3c7a5), dated **30 September 2026, 09:55:54 UTC**. This identifies the upstream source snapshot, not an official upstream stable-release date. Every Enhanced release records its upstream version, commit and date.

`master` contains the accepted Enhanced version. [GitHub-hosted Windows builds](https://github.com/abelokoj/sumatrapdf/actions/workflows/build-windows.yml) produce x64 and ARM64 installers and portable packages on pushes/pull requests and manual runs. Build locally through `bun cmd/build.ts -dbg`, `bun cmd/build.ts -rel` or `bun cmd/build.ts -rel -arm64`, following [agents.md](agents.md).

The native reader and interface do not require the Microsoft Edge browser. Optional upstream AI, manual and CHM browser integrations need the separate WebView2 runtime.

Reference previews require supported local destinations. Advanced handwriting recognition and cleanup are not available in this release. Laser behavior and pen responsiveness are still being refined.

## Credits and license

Based on [official SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf), with visual inspiration from [JaviLendi/PrettySumatraPDF](https://github.com/JaviLendi/PrettySumatraPDF) and learning-data/design references from [wmkeyboard](https://github.com/wasi-master/wmkeyboard).

The application follows the upstream (A)GPLv3/BSD licensing; see [COPYING](COPYING), [COPYING.BSD](COPYING.BSD) and [AUTHORS](AUTHORS). Bundled fonts, icons and dictionary and study data retain their separate licenses and attribution in [license notices](docs/licenses), [font attribution](docs/font-attribution.md), [icon attribution](docs/icon-attribution.md) and [vocabulary attribution](docs/vocabulary-attribution.md).

Upstream resources: [website](https://www.sumatrapdfreader.org/free-pdf-reader) · [manual](https://www.sumatrapdfreader.org/manual) · [contribution information](https://www.sumatrapdfreader.org/docs/Contribute-to-SumatraPDF).
