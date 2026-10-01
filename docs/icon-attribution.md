# Native toolbar icon attribution

SumatraPDF Enhanced embeds a subset of [Lucide](https://github.com/lucide-icons/lucide) SVG icons in `src/EnhancedIcons.h`.

- Upstream revision: [`5a92b9ba262de5bf10e864219883267672c05db8`](https://github.com/lucide-icons/lucide/tree/5a92b9ba262de5bf10e864219883267672c05db8).
- Original assets: [`icons/`](https://github.com/lucide-icons/lucide/tree/5a92b9ba262de5bf10e864219883267672c05db8/icons).
- Licenses: ISC for Lucide; MIT for its Feather-derived assets. The complete upstream notice is preserved in [licenses/lucide-LICENSE.txt](licenses/lucide-LICENSE.txt) and must accompany redistributed builds containing these icons.
- Adaptation: monochrome `currentColor`, consistent 24 × 24 view box and rounded line joins/caps, stroke width reduced from 2 to 1.75 for the native toolbar. Geometry is otherwise unchanged.
- Rendering: the application's existing native SVG renderer. No JavaScript, browser, Flutter runtime, icon-font installation or new runtime dependency is introduced.

The official [Cupertino Icons package](https://github.com/flutter/packages/tree/main/third_party/packages/cupertino_icons) was considered. It supplies font assets intended for Flutter. This subset uses Lucide vectors for a consistent rounded outline style; it does not claim to contain Apple's SF Symbols or Cupertino glyphs.

| Embedded symbol suffix | Source asset |
| --- | --- |
| `Open` | `folder-open.svg` |
| `Search` | `search.svg` |
| `Previous` | `chevron-left.svg` |
| `Next` | `chevron-right.svg` |
| `ZoomMinus` | `zoom-out.svg` |
| `ZoomPlus` | `zoom-in.svg` |
| `FitWidth` | `move-horizontal.svg` |
| `Page` | `file.svg` |
| `Layout` | `columns-2.svg` |
| `RotateLeft` | `rotate-ccw.svg` |
| `RotateRight` | `rotate-cw.svg` |
| `Edit` | `file-pen-line.svg` |
| `Ink` | `pen-tool.svg` |
| `Highlight` | `highlighter.svg` |
| `Underline` | `underline.svg` |
| `StrikeOut` | `strikethrough.svg` |
| `Eraser` | `eraser.svg` |
| `Laser` | `wand-sparkles.svg` |
| `Bookmark` | `bookmark.svg` |
| `Star` | `star.svg` |
| `Command` | `command.svg` |
| `Print` | `printer.svg` |
| `Settings` | `settings.svg` |
| `Sun` | `sun.svg` |
| `Moon` | `moon.svg` |
| `Grid` | `layout-grid.svg` |
| `Invert` | `contrast.svg` |

The application logo and previously copied PrettySumatra visual assets keep their existing attribution; this notice covers only the new toolbar subset.
