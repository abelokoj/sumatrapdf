# Changelog

## Unreleased

## v0.1.2

- Added editable HEX, RGB, CMYK, HSV and HSL color entry, with a synchronized preview and independent opacity.
- Reduced first-opening work in Settings and Learning Hub by creating offscreen dropdowns and practice controls when needed. Visible Settings dropdowns start at their final bounds without temporary list entries or a second resize.
- Kept all five pen types on one adaptive row. Pen palettes use pin/unpin and hide icons; Dictionary and Learning Hub use compact pronunciation, stop and recording controls with full action names on hover and keyboard focus.
- Shortened PDF wheel easing and combined continuous-view line updates, preserving wheel distance and direction changes.
- Matched dictionary dropdown and button heights across themes and interface scales. Image and bookmark editors now refresh selected fonts, colors and spacing without overflowing their controls.
- Removed duplicate borders on custom-painted controls and parent-framed text fields, and pale rounded header corners in Dictionary and Learning Hub.
- Added lasso rotation, duplication, recoloring and thickness commands with undo, and segment erasing that preserves the remaining ink. Selection edits refresh the visible page and annotation lists.
- Added background annotation recovery copies with atomic manifests, original-file checks and explicit recovery choices. Recovery copies preserve the original PDF and the active undo history; repaired PDFs that cannot be saved incrementally are excluded.
- Added source and package checksum manifests, x64 ASan/UI validation and disposable installation coexistence checks to build-on-push.

- Integrated upstream prerelease Build 22653 (`a2957304f`, October 5, 2026), retaining Enhanced features, custom settings and existing command IDs.
- Added JPEG XL decoding in PDFs, MuPDF 1.28.5, MuJS 1.3.10 and the updated CHM decoder.
- Added interchangeable bookmark/thumbnail/favorite sidebar panels, the Merge PDF page organizer, DPI choices for PDF-to-image conversion and cached DVI-to-PDF opening when a TeX converter is installed.
- Included upstream fixes for search reopening, EPUB images, fullscreen session restoration, annotation snapping, window geometry and render/font/print thread lifetimes.
- Corrected DVI cache validation and preserved keyboard traversal and shared-menu tooltip behavior in the combined Enhanced interface.

- Reduced Settings opening work: dropdown choices load on first use, text measurements are reused across reopenings, and the completed form is laid out once during creation.
- Added horizontal toolbar scrolling, left and right arrows and a scrollable command dropdown, keeping tools in a single row. Arrow and pinned-tool clicks remain usable when the toolbar shares the title bar.
- Added a hand tool for dragging the page and a lasso for selecting, moving, resizing and deleting PDF annotations, with undo support. Hand dragging stops on release or capture loss, and switching tools clears pending selections.
- Let completed laser strokes remain until their configured timeout when another tool is selected, and kept touch scrolling and zoom available with palm rejection enabled.
- Allowed PDF editing commands to be pinned to the main toolbar, alongside independent pen color and width presets. Right-click a tool to pin it, or a pinned button to remove it.
- Added independent text and page-background colors for the current document, with color pickers and a reset to theme colors.
- Replaced tab fades with clear separators and added a scrollable open-file dropdown beside the navigation arrows. Its visible row count defaults to ten and can be changed in Settings.
- Made all configured recent documents reachable through a right-hand scrollbar and preserved the portable app's last session during shutdown.
- Added adjustable scrollbar width for app-owned scrolling controls and rounded native popup menus.
- Distinguished completed deck installation from source-definition coverage, retaining green `✓` marks and explaining missing definitions. Practice actions and feedback share a row when space permits.
- Preserved explicit reference-preview destinations and added edge and corner resizing.

- Made Settings scrolling and resizing more responsive by reusing field measurements and unchanged clipping regions. Fine wheel movements are retained, and scrolling over a closed field scrolls the form without changing its value.
- Kept Settings presets and added custom numeric values, matching units and installed font names, with validation before saving.
- Used the green `✓` character for installed dictionaries and decks, learned words and successful practice feedback.
- Applied native rounded corners to app windows and dialogs where Windows supports them, with rounded fields, selectors, lists, buttons and learning panels.

- Added optional cpdf-backed PDF bookmark editing with title, page, hierarchy and order controls, preserving existing destinations and styles. First use offers a size-labeled, verified download or a local executable; saving creates a separate PDF copy.
- Compacted the home header with the green logo beside a bold title, measured action buttons and reduced vertical spacing above recent documents.
- Narrowed Settings to fit its form, added clear section headings and wrapping help text, and kept OK/Cancel visible while scrolling. Large text and narrow windows use stacked fields.

- Compacted Dictionary and Learning hub with shared labels, measured control sizing and themed selectors. Dictionary and deck management expand when needed; search remains beside Help / Start guide.
- Focused practice on the word, answers, pronunciation and progress, hid setup controls during sessions, emphasized Check answer/Next word and kept Back to library available before revealing flashcards.
- Added session undo for the last 20 removed vocabulary entries, restoring definitions, PDF context, deck memberships and review history. Failed remove or undo saves retain the previous data. Deck deletion explains that saved words and review history remain available.
- Added Activity, Review method and Voice labels, SM-2/Leitner help, and an explanation of Mark learned. Empty input widths now account for their cues.
- Made home captions, list rows and thumbnail spacing follow the selected font. Existing annotation toolbars and hover panels refresh after font, interface scale or monitor DPI changes, including while hidden.
- Applied interface scaling to learning and annotation control spacing and icons, keeping PDF annotation dimensions independent.

- Made the word and answer divider directly draggable, removed the three panel-size buttons, and retained arrow-key, Home and context-menu resizing.
- Placed the vocabulary selector, Save word, Mark learned and Open learning hub in one compact dictionary footer row, with wrapping when space is limited.

- Added optional Kaikki English and Simple English offline dictionaries, with download sizes shown before confirmation, progress and cancellation. Downloads are streamed to a disk index, retaining separate meanings and source attribution without loading the full dictionary into memory.

- Preserved separate dictionary meanings, parts of speech, examples, phonetics and related words, with numbered definitions, paragraph breaks, bold headings and italic examples.
- Added explicit online lookup through Wiktionary, Wiktionary REST and Free Dictionary API, with configurable source order. Offline lookup remains the default.
- Bundled all eleven WMKeyboard vocabulary archives for offline lookup in installer and portable builds, retaining their attribution notices.
- Added word pronunciation with installed Windows voices, voice selection, stop controls and optional online recordings.
- Fixed guide navigation, added adjustable word and answer panels, and placed search beside Help / Start guide in dictionary and learning windows.
- Added green installed-deck and learned-word marks, with partial-deck counts and accessible status labels.
- Scaled expanded reader search fields, icons and hit targets with the interface settings and monitor DPI.

- Stabilized dictionary and vocabulary scrolling with cached layouts, batched control movement, preserved reading positions and cached, wrapping answer rows.
- Added content-fitting dictionary, practice and deck-name fields, compact expanding page and chapter fields, and shared font-aware native input sizing.
- Added normalized Windows pointer samples with valid pressure, tilt and rotation, chronological pen history and pointer diagnostics; documented the annotation pipeline and remaining handwriting work.

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
