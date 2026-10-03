# Annotation and pointer-input architecture

This describes the current native Windows reader, including the existing Enhanced ink implementation. It separates implemented behavior from the remaining handwriting work.

## Input and coordinate ownership

`Canvas.cpp` receives mouse, Windows pointer and gesture messages. `PointerInput.h` dynamically loads Windows 8 pointer APIs while retaining the older build target. Its device-independent `PointerSample` records mouse, touch or pen identity, screen coordinates, millisecond and performance-counter timestamps, contact and button state, barrel button and inverted and eraser flags. Pressure is normalized to 0–1 only when the driver reports its validity mask; tilt and rotation have separate validity flags. Missing pressure is not invented. The legacy mouse adapter supplies position, timestamp and button state without pressure.

The canvas reads the normalized pen sample before routing the gesture to its existing mouse-based handlers. Touch samples continue through the long-press observer and default gesture handling; pen-only touch suppression remains in place. Ordinary mouse navigation retains its existing message path. Eraser/inverted pen contact invokes the existing annotation eraser. The barrel flag is exposed by the sample layer but does not acquire a new shortcut action.

During ink and laser contact, the sample layer reads a bounded history of up to 128 pen samples and returns it oldest first. The canvas consumes contact samples in that order and applies valid pressure to the existing ink accumulator. If no contact history is available, it uses the current event. Pointer-down/up diagnostics report pressure validity, tilt, buttons and timestamps through the existing logging route; they do not log document text. Hardware sampling rates and driver availability still affect the result.

`AnnotPlacement.cpp` owns placement state, page selection and conversion from canvas coordinates to PDF page coordinates. Points and stroke counts are recorded in page space, so document zoom is not saved as an annotation coordinate scale. The placement cannot become a cross-page ink object: each PDF annotation belongs to one page.

## Temporary ink and commit

`AnnotationPlacementOnMouseMove` extends the pending placement. `PaintAnnotationPlacement` and `PaintInkPlacement` draw its GDI+ overlay. Pending placement is distinct from persistent PDF objects. `FinishInkAnnotationPlacement` calls the common placement finalizer. `AnnotationPlacementFillCreate` supplies page coordinates, stroke counts, ink points, the pen profile, color, opacity and border width to the annotation-creation path.

Fountain and brush profiles derive one width from mean accumulated pressure through `InkStrokeWidth`. Their live preview and saved PDF ink use that width. This is not per-point pressure rendering: individual sample pressure, tilt and rotation are not retained in PDF ink geometry, and tilt currently does not change nib appearance. Ballpoint, pencil and highlighter profiles have different settings, but this foundation does not add textured brushes or a variable-width appearance stream.

`Annotation.cpp` creates the MuPDF annotation and writes the InkList, color, opacity, border width and Enhanced pen-style metadata. The page owns the resulting annotation; the engine updates its appearance and invalidates the affected rendering state. Existing free-text font handling uses PDF resources and permitted font embedding separately from the interface font.

## Rendering, save and reopen

Persistent annotation appearance is rendered through the document engine and reader render cache. A newly placed stroke has an overlay before commit; committed ink becomes a PDF annotation appearance. `EngineMupdfSaveUpdated` writes the modified PDF, updates saved undo position and handles the engine's supported incremental or full-save choices. Reopening reconstructs annotations from PDF dictionaries, including InkList and retained pen-style metadata. Pressure history and tilt are not part of that saved format.

Laser ink is separate canvas state. It is composed with the page buffer, retains complete temporary paths until the configured lifetime expires after lift, and is never sent to annotation creation or PDF save. Laser width is measured in DPI-scaled logical screen pixels, independent of document zoom and persistent pen width.

## Editing, erasing and undo

Canvas annotation selection and manipulation feed the annotation-edit toolbar and PDF edit operations. The engine uses MuPDF's journal for persistent undo and redo. `BeginPdfEditOperation` groups a gesture such as resize into an operation; setters and creation update journaled PDF objects. Pending placement is managed separately until committed. Undo history is session state, not a cross-device history embedded in the saved PDF.

`Annotation.cpp` reads InkList and tests segment distance in `InkStrokeHit`; the stroke eraser removes hit strokes and regenerates the remaining ink geometry. `AnnotationPlacementEraseAt` converts the eraser position to page space and handles pending and persistent targets. Highlight-only erasing restricts its annotation types. Existing geometric hit testing is not handwriting recognition, a semantic word eraser or a complete lasso selection system.

## Foundation tests and remaining work

`Canvas_UnitTestPointerInput` exercises normalized device classification, contact and buttons, pressure masks and bounds, zero and missing pressure, tilt and rotation clamping, timestamps, eraser and barrel exposure, the legacy mouse adapter and chronological history conversion. The test uses synthetic samples; it requires no pointer hardware or visible window.

Hardware acceptance must cover actual pen pressure, tilt availability, coalesced motion, hover, eraser inversion, touch suppression, mouse navigation and DPI transitions. This batch does not add a prediction algorithm, per-point nib rendering, textured brushes, persistent pressure and tilt payloads, cross-page strokes, handwriting cleanup, shape recognition, Easy Writing Pad or a full lasso workflow. Cancellation and pointer-capture transitions retain the existing canvas behavior and still need dedicated hardware acceptance.
