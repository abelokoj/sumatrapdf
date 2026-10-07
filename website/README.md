# SumatraPDF Enhanced website

Static project website for GitHub Pages. The site has no build step and serves
local branding and licensed Manrope fonts. Feature illustrations are examples,
not application screenshots.

## Preview

From the repository root, run `python -m http.server 4173 --directory website`
and open `http://localhost:4173`.

## Publish

In the repository's **Settings → Pages**, select **GitHub Actions** as the
publishing source. Merge the website and `.github/workflows/website.yml` onto
`master`. The workflow deploys only `website/`; subsequent website changes
publish automatically. It can also be run manually from Actions.

Expected project URL: https://abelokoj.github.io/sumatrapdf-enhanced/

The download selector loads the latest GitHub release only when all six x64 and
ARM64 packages match the existing naming convention. If the API is unavailable,
it keeps the verified v0.1.1 links and labels them with that version. Update
`FALLBACK_VERSION` in `site.js` and the static download links in `index.html`
when replacing the fallback. All paths are relative for project Pages hosting.

Manrope is included under the license in `assets/Manrope-OFL.txt`. Application
logos retain the repository's existing licensing.
