# PDF bookmark editing

SumatraPDF Enhanced can edit PDF bookmarks through an optional [Coherent PDF (cpdf)](https://github.com/coherentgraphics/cpdf-binaries) executable. The tool is downloaded separately and is not included in the installer or portable application.

Save any annotation changes before opening the bookmark editor. It reads a snapshot of the saved PDF and saves a new PDF copy. The original file stays open and unchanged.

1. Open a PDF and choose **Edit PDF bookmarks** from the View menu, bookmarks context menu or command palette.
2. Choose the verified download or select an existing cpdf executable you trust. The download prompt shows the source, size and license before downloading.
3. Select a bookmark and edit its title, page, hierarchy level or initial expanded state. Level **0** is a top-level bookmark; level **1** is its child. Choose **Apply** to keep your edits. Use **Add**, **Remove**, **Move up** and **Move down** to organize the outline.
4. Choose **Save PDF copy** and enter a new filename. Existing files are not replaced. Open the saved copy to read it with the updated bookmarks.

Title, level and expanded-state changes preserve the bookmark's original destination, styles and actions. Changing its page creates a Fit-page destination. Hierarchy and page checks prevent invalid outlines from being saved.

The verified Windows x64 download is **11,724,417 bytes (11.18 MiB)**, from upstream commit `38b2556dc111670acb427fd9789a94ada971b2df`. Its SHA-256 is `91f51d30c063233e0b8a23b8bb459e4acbbd577559a0da52fcdbec37d3237351`. A downloaded executable must match both the expected size and hash before it runs.

Upstream distributes cpdf under [AGPL v3](https://github.com/coherentgraphics/cpdf-binaries/blob/38b2556dc111670acb427fd9789a94ada971b2df/LICENSE.md), with a [commercial license](https://www.coherentpdf.com/licensing.html) also available. The official repository provides a Windows x64 executable; it does not list a native Windows ARM64 build. On ARM64, a compatible executable or Windows x64 emulation is required.

Downloading and PDF processing run in the background and can be cancelled. If a download, validation or save fails, the editor reports the problem and leaves the original PDF unchanged.
