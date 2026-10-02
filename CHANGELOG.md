# Changelog

## Unreleased

- Added circular search that expands horizontally, single-row reader and annotation toolbars with an overflow menu, and keyboard access to toolbar commands.
- Persisted independent color, thickness and opacity for each pen type, including favorite-tool actions and restart restoration.
- Added one overall interface scale in Appearance, proportional home-page typography and larger sidebar close targets.
- Added theme-aware tab separators and interface-scaled tab geometry.
- Reduced repeated vocabulary work on the home page by deferring word-list allocation, caching daily-word selection and counting due words without sorting.
- Added selectable user-data storage with restart migration, source preservation and a restore-default action.
- Refined dictionary and vocabulary panels with rounded controls, theme accents, wrapping layouts and accessible installed-pack checkmarks.
- Added consistent vector icons for dictionary, learning, exports, presentation and laser modes.
- Moved the home feature toggle beside Dictionary, with narrow-window reflow and no collapsed-panel gap above recent documents.
- Applied the selected interface font and text size across app-owned native controls and compatible optional WebView content, with embedded bundled fonts and refresh support for open modeless windows.
- Added Enhanced-only update checks with optional daily checks disabled by default and verified x64 and ARM64 downloads.

- Added a collapsible home-page feature overview with actions for Enhanced tools.
- Added bundled and installed font choices for free-text annotations, with preview, an independent default and embedding-rights checks.
- Added highlight and note exports to Markdown, plain text, Typst, HTML, Word, JSON and CSV, with filters, preview and links to source pages.

- Separated Enhanced installation folders, executable names, shortcuts, uninstall records and document registrations from official SumatraPDF. Installation guards reject overlapping destinations.
- Replaced the yellow installer artwork with a green Enhanced banner and generated installer and file version details from the Enhanced release version.
- Separated Enhanced preferences, user data and Quick Look startup registration. The official updater is not used.
- Added opt-in Enhanced preview and search providers with independent COM identities, preserving existing handlers and removing only owned registrations.
- Added illustrated pen types and a separate laser width setting from 0.1 to 32 pixels.
- Added dictionary and vocabulary window icons, replayable learning guides and animated correct and incorrect feedback with reduced-motion support.

- Limited the zoom picker to 25%–600% with a compact layout; mouse and keyboard zoom retain the wider reader range.
- Made Settings checkbox text and spacing follow the interface font size.
- Added configurable laser duration from 0.1 to 120 seconds and composed temporary strokes with the page to avoid separate-screen repaint flicker.
- Added lettered, wrapping vocabulary answers and responsive learning controls while preserving answer grading.
- Added dictionary lookup to the text-selection popup and moved dictionary download URLs to this repository.

- Added a large, theme-aware title banner with the green logo, followed by release badges and navigation links.

- Reorganized the repository homepage with status badges, icon-led feature sections, download comparisons and a getting-started guide.

## v0.1.1

- Applied the green application logo across the app and its repository home page; archived alternate colors.
- Added standalone portable EXEs for x64 and ARM64, with the offline dictionary embedded.
- Added automatic GitHub builds, version tagging and publication of installers, portable executables and ZIP packages.

Based on the SumatraPDF 3.7 source snapshot at commit `a0d8bcaed0412ce803d9c213845e710fcbe3c7a5`, dated September 30, 2026, 09:55:54 UTC.

## v0.1.0

- Added a native themed interface, recent-document home page and configurable interface fonts and sizes.
- Added pen profiles, favorite annotation presets and a temporary laser pointer.
- Added offline dictionary lookup, vocabulary lists and learning activities.
- Added x64 and ARM64 installer and portable downloads, including offline dictionary data and license notices.

Based on the SumatraPDF 3.7 source snapshot at commit `a0d8bcaed0412ce803d9c213845e710fcbe3c7a5`, dated September 30, 2026, 09:55:54 UTC.
