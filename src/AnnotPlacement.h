/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;
struct DisplayModel;
struct AnnotCreateArgs;
enum class AnnotationType;
enum class InkPenStyle;
struct InkPenProfile;

constexpr WORD kAnnotationPlacementCommandCode = 0x5341;

bool CommandUsesPlacementMode(int cmdId);
AnnotPlacementKind PlacementKindFromCommand(int cmdId);

bool IsPlacingAnnotation(MainWindow*);
bool IsPlacingPointAnnotation(MainWindow*);
bool IsPlacingLineAnnotation(MainWindow*);
bool IsPlacingPolyLineAnnotation(MainWindow*);
bool IsPlacingShapeAnnotation(MainWindow*);
bool IsPlacingInkAnnotation(MainWindow*);
bool IsPlacingHighlighterAnnotation(MainWindow*);
Point SnapLineEndpoint(Point start, Point end);

void StartAnnotationPlacement(MainWindow*, int cmdId);
bool CancelAnnotationPlacement(MainWindow*);
bool FinishAnnotationPlacement(MainWindow*);
bool FinishPolyLineAnnotationPlacement(MainWindow*);
bool FinishInkAnnotationPlacement(MainWindow*);
bool CloseAnnotationPlacementHint(MainWindow*);

bool AnnotationPlacementOnLeftDown(MainWindow*, Point, WPARAM);
bool AnnotationPlacementOnLeftUp(MainWindow*, Point, WPARAM);
bool AnnotationPlacementOnLeftDblClk(MainWindow*, Point);
bool AnnotationPlacementOnRightDown(MainWindow*);
bool AnnotationPlacementOnMouseMove(MainWindow*, Point, WPARAM);
bool AnnotationPlacementOnSetCursor(MainWindow*);
bool AnnotationPlacementOnKeyDown(MainWindow*, WPARAM);
bool AnnotationPlacementEraseAt(MainWindow*, Point);
bool HandlePenToolCommand(MainWindow*, int);
InkPenProfile& GetInkPenProfile(InkPenStyle);
InkPenProfile& GetInkPenProfile(MainWindow*);
Color InkPenColor(InkPenStyle);
Color InkPenColor(MainWindow*);
float InkPenWidth(InkPenStyle);
float InkPenWidth(MainWindow*);
int InkPenOpacity(InkPenStyle);
int InkPenOpacity(MainWindow*);
void SetInkPenColor(InkPenStyle, Color);
void SetInkPenColor(MainWindow*, Color);
void SetInkPenWidth(InkPenStyle, float);
void SetInkPenWidth(MainWindow*, float);
void SetInkPenOpacity(InkPenStyle, int);
void SetInkPenOpacity(MainWindow*, int);
bool SuppressTouchForPen(MainWindow*);
void AddInkPressure(MainWindow*, UINT32);
void AnnotationPlacementOnSelectionStop(MainWindow*);

void PaintAnnotationPlacement(MainWindow*, HDC, DisplayModel*);
bool AnnotationPlacementFillCreate(MainWindow*, AnnotationType, Point&, int&, PointF&, PointF&, AnnotCreateArgs&);
SizeF FreeTextPlacementPageSize(const AnnotCreateArgs&);

void DeleteAnnotationPlacementCursors();
TempStr AnnotationPlacementStateTemp(MainWindow*);
