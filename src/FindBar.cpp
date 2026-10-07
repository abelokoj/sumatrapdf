/* Copyright 2024 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "base/WinDynCalls.h"
#include "base/Win.h"
#include "base/Pixmap.h"
#include "base/UITask.h"

#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/PlatformWindow.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"

#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "ProgressUpdateUI.h"
#include "TextSelection.h"
#include "TextSearch.h"
#include "DisplayModel.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "Commands.h"
#include "Accelerators.h"
#include "Menu.h"
#include "SvgIcons.h"
#include "EnhancedIcons.h"
#include "Toolbar.h"
#include "SearchAndDDE.h"
#include "FindWindow.h"
#include "Translations.h"
#include "Theme.h"
#include "DarkMode.h"
#include "FindBar.h"

// command ids for the bar's toolbar buttons; must not collide with real commands
constexpr int kFindBarCloseCmdId = (int)CmdLast + 50;
constexpr int kFindBarPinCmdId = (int)CmdLast + 52;
constexpr int kFindBarOptionsCmdId = (int)CmdLast + 53;
constexpr UINT_PTR kFindBarCollapseTimer = 0x201;

static int FindIconSize(int dpi) {
    return ToolbarIconSize(dpi);
}

void ApplyFindEditScale(DropDown* edit, int dpi) {
    if (!edit || !edit->hwnd) return;
    int start = 0, end = 0;
    CbEditGetSelection(edit, start, end);
    PlatformFont* font = GetAppFontForDpi(dpi);
    edit->SetFont(font);
    int lineHeight = PlatformFontLineHeight(font);
    int fieldHeight = lineHeight + UiScalePxForDpi(dpi, 2);
    CbSetItemHeight(edit->hwnd, -1, fieldHeight);
    CbSetItemHeight(edit->hwnd, 0, lineHeight + UiScalePxForDpi(dpi, 6));
    CbEditSelectText(edit, start, end);
}

struct FindFieldFit {
    int editDx = 0;
    bool compact = false;
};

static FindFieldFit FitFindField(int available, int fullFixed, int navigationFixed, int minEditDx) {
    bool compact = available < fullFixed + minEditDx;
    int editDx = std::max(1, available - (compact ? navigationFixed : fullFixed));
    return {editDx, compact};
}

#if IS_DEBUG
void FindBarLayout_UnitTests() {
    auto wide = FitFindField(400, 160, 70, 80);
    utassert(!wide.compact && wide.editDx == 240);
    auto narrow = FitFindField(200, 160, 70, 80);
    utassert(narrow.compact && narrow.editDx == 130);
    auto tight = FitFindField(80, 160, 70, 80);
    utassert(tight.compact && tight.editDx == 10);
    auto scaled = FitFindField(500, 400, 175, 200);
    utassert(scaled.compact && scaled.editDx == 325);

    Settings* savedSettings = gSettings;
    Settings* fixture = NewSettings({});
    utassert(fixture);
    if (!fixture) return;
    gSettings = fixture;
    for (int iconSize : {16, 28, 40}) {
        gSettings->toolbarSize = iconSize;
        for (int scale : {100, 150}) {
            gSettings->interfaceScale = scale;
            for (int dpi : {96, 144}) {
                utassert(FindIconSize(dpi) == RoundUp(UiScalePxForDpi(dpi, iconSize), 4));
            }
        }
    }
    gSettings->toolbarSize = 16;
    gSettings->interfaceScale = 100;
    HWND host = CreateWindowExW(0, L"STATIC", nullptr, WS_POPUP, 0, 0, 500, 500, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    utassert(host);
    if (host) {
        auto* edit = new DropDown();
        DropDown::CreateArgs args;
        args.parent = host;
        args.font = GetAppFontForDpi(96);
        args.isEditable = true;
        utassert(edit->Create(args));
        edit->SetText(StrL("extraordinary"));
        CbEditSelectText(edit, 3, 7);
        ApplyFindEditScale(edit, 96);
        int normal = (int)SendMessageW(edit->hwnd, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
        gSettings->toolbarSize = 40;
        ApplyFindEditScale(edit, 96);
        int large = (int)SendMessageW(edit->hwnd, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
        utassert(large == normal);
        utassert(HwndWindowRect(edit->hwnd).dy >= large);
        gSettings->interfaceScale = 150;
        ApplyFindEditScale(edit, 144);
        int scaledHeight = (int)SendMessageW(edit->hwnd, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
        utassert(scaledHeight == PlatformFontLineHeight(GetAppFontForDpi(144)) + UiScalePxForDpi(144, 2));
        utassert((HFONT)SendMessageW(CbEditHwnd(edit), WM_GETFONT, 0, 0) == GetAppFontForDpi(144)->GetHFont());
        int start = 0, end = 0;
        CbEditGetSelection(edit, start, end);
        utassert(start == 3 && end == 7);
        utassert(str::Eq(edit->GetTextTemp(), StrL("extraordinary")));
        gSettings->toolbarSize = 16;
        gSettings->interfaceScale = 100;
        ApplyFindEditScale(edit, 96);
        utassert((int)SendMessageW(edit->hwnd, CB_GETITEMHEIGHT, (WPARAM)-1, 0) == normal);
        delete edit;
        DestroyWindow(host);
    }
    gSettings = savedSettings;
    DeleteSettings(fixture);
}
#endif

namespace {

// min-width box: at least `dx`, wider if the child needs more
struct FindStatusBox : ILayout {
    ILayout* child = nullptr;
    int dx = 0;

    FindStatusBox(ILayout* c, int dxIn);
    ~FindStatusBox() override;

    Size Layout(Constraints bc) override;
    int MinIntrinsicHeight(int width) override;
    int MinIntrinsicWidth(int height) override;
    void SetBounds(Rect) override;
    int LayoutChildCount() override;
    ILayout* LayoutChildAt(int) override;
};

FindStatusBox::FindStatusBox(ILayout* c, int dxIn) {
    child = c;
    dx = dxIn;
}

FindStatusBox::~FindStatusBox() {
    delete child;
}

int FindStatusBox::LayoutChildCount() {
    return child ? 1 : 0;
}

ILayout* FindStatusBox::LayoutChildAt(int) {
    return child;
}

int FindStatusBox::MinIntrinsicWidth(int) {
    return dx;
}

int FindStatusBox::MinIntrinsicHeight(int width) {
    return child ? child->MinIntrinsicHeight(width) : 0;
}

Size FindStatusBox::Layout(const Constraints bc) {
    int w = MinIntrinsicWidth(0);
    if (bc.min.dx > w) {
        w = bc.min.dx;
    }
    if (bc.HasBoundedWidth() && bc.max.dx < w) {
        w = bc.max.dx;
    }
    Size s = child ? child->Layout(bc.TightenWidth(w)) : Size{};
    return {w, s.dy};
}

void FindStatusBox::SetBounds(Rect r) {
    lastBounds = r;
    if (child) {
        child->SetBounds(r);
    }
}

} // namespace

static int DecimalDigits(int n) {
    int digits = 1;
    while (n >= 10) {
        n /= 10;
        digits++;
    }
    return digits;
}

// width of the "n / m" status slot, wide enough for the largest count it will
// show. Shared with the floating find window so both size it the same way.
int FindStatusDx(PlatformFont* font, int totalHits, bool capped) {
    int digits = DecimalDigits(std::max(totalHits, 0));
    int nChars = (2 * digits) + 3; // N, " / ", M
    if (capped) {
        nChars++; // the trailing '+' in e.g. "999 / 999+"
    }
    return nChars * font->averageCharWidth;
}

struct FindBarWnd : WindowBase {
    MainWindow* win = nullptr;
    // the status text and the buttons are virtual controls; the search field is
    // the only HWND child. Owned by `layout` once BuildLayout() runs
    DropDown* edit = nullptr;
    VirtText* status = nullptr;
    FindStatusBox* statusBox = nullptr;
    // what statusBox->dx was sized for; the slot is fixed so the search field
    // next to it doesn't resize every time the count changes
    int statusTotalHits = 0;
    bool statusCapped = false;
    Spacer* gapAfterEdit = nullptr;
    Spacer* gapAfterStatus = nullptr;
    Padding* padLayout = nullptr;
    int layoutDpi = 96;
    // prev / next / match-case / match-whole-word / pop-out / close
    VirtIconButton* btns[7]{};

    int barDx = 0;
    int barDy = 0;
    // when set, programmatic edits to the text don't kick off a search
    // (used while restoring text during a theme-change recreate)
    bool suppressTextChanged = false;
    // set while Layout() runs so the WM_SIZE its own SetWindowPos generates
    // doesn't re-enter Layout()
    bool inLayout = false;
    bool inOptionsMenu = false;
    Func1List<MainWindow*> onWindowMoved;

    FindBarWnd() = default;
    ~FindBarWnd() override;

    bool Create(MainWindow* win);
    void CreateButtons();
    void UpdateButtonIcons(int dpi = 0);
    void BuildLayout();
    void UpdateDpi(int dpi);
    // forceBarDx > 0: fit the bar into exactly that window width, giving the
    // slack to the edit box. 0: the default edit width.
    void Layout(int forceBarDx = 0);
    int MinBarDx() const;

    void OnTextChanged();
    void OnHistoryCommitted();

    void OnSize(WindowBase::SizeEvent* ev);
    void OnGetMinMaxInfo(WindowBase::GetMinMaxInfoEvent* ev);
    void OnNcHitTest(WindowBase::NcHitTestEvent* ev);
    void OnDpiChanged(WindowBase::DpiChangedEvent* ev);
    void OnKeyDown(KeyEvent* ev);
    void OnCommand(WindowBase::CommandEvent* ev);
    void OnActivate(WindowBase::ActivateEvent* ev);
    void OnTimer(WindowBase::TimerEvent* ev);
    void OnPaint(WindowBase::PaintEvent* ev);

    bool UpdateStatusWidth(int totalHits, bool capped);
};

// tooltip text for the bar's toolbar buttons
// append a command's keyboard shortcut to its tooltip, e.g. "Find Next (F3)"
static TempStr AppendCmdAccel(Str base, int cmd) {
    TempStr accel = AppendAccelKeyToMenuStringTemp({}, cmd);
    if (len(accel) == 0) {
        return base;
    }
    return str::JoinTemp(base, fmt(" (%s)", Str(accel.s + 1, len(accel) - 1))); // +1 skips the leading \t
}

static TempStr FindBarButtonTooltip(int cmd) {
    switch (cmd) {
        case CmdFindPrev:
            return AppendCmdAccel(Tr("Find Previous"), cmd);
        case CmdFindNext:
            return AppendCmdAccel(Tr("Find Next"), cmd);
        case CmdFindToggleMatchCase:
            return AppendCmdAccel(Tr("Match Case"), cmd);
        case CmdFindToggleMatchWholeWord:
            return AppendCmdAccel(Tr("Match Whole Word"), cmd);
        case kFindBarPinCmdId:
            return Tr("Open in a window");
        case kFindBarCloseCmdId:
            return Tr("Close");
        case kFindBarOptionsCmdId:
            return Tr("Search options");
    }
    return {};
}

FindBarWnd::~FindBarWnd() {
    if (win) {
        win->UnregisterOnWindowMoved(&onWindowMoved);
    }
    // edit, status and buttons are owned by `layout` (deleted in ~WindowBase)
}

// the icons come from the shared cache, which renders them for the current
// theme and size
void FindBarWnd::UpdateButtonIcons(int dpi) {
    static const char* icons[7] = {gIconSearchPrev,     gIconSearchNext, gIconMatchCase,       gIconMatchWholeWord,
                                   gIconArrowsDiagonal, gIconClose,      kEnhancedIconSettings};
    if (dpi <= 0) {
        dpi = layoutDpi;
    }
    int isz = FindIconSize(dpi);
    for (int i = 0; i < dimof(btns); i++) {
        if (btns[i]) {
            btns[i]->pixmap = GetCachedPixmapForSvg(Str(icons[i]), isz, isz);
        }
    }
}

static void FindBarButtonClicked(FindBarWnd* bar, VirtMouseEvent* ev) {
    auto* btn = (VirtIconButton*)ev->target;
    WindowBase::CommandEvent ce;
    ce.w = bar;
    ce.wparam = (WPARAM)btn->id;
    bar->OnCommand(&ce);
}

void FindBarWnd::CreateButtons() {
    static const int cmds[7] = {
        CmdFindPrev,      CmdFindNext,        CmdFindToggleMatchCase, CmdFindToggleMatchWholeWord,
        kFindBarPinCmdId, kFindBarCloseCmdId, kFindBarOptionsCmdId};
    int pad = UiScalePxForDpi(layoutDpi, 2);
    for (int i = 0; i < dimof(btns); i++) {
        auto* b = new VirtIconButton();
        b->id = cmds[i];
        b->padding = Insets{pad, pad, pad, pad};
        b->SetTooltip(FindBarButtonTooltip(cmds[i]));
        b->onClick = MkFunc1(FindBarButtonClicked, this);
        b->SetFlag(vwfFocusable, true);
        b->cornerRadius = UiScalePxForDpi(layoutDpi, 6);
        b->SetColor(kColIconBtnBgHover, ThemeHotBackgroundColor());
        b->SetColor(kColIconBtnBgSelected, ThemeHotBackgroundColor());
        btns[i] = b;
    }
    UpdateButtonIcons();
}

bool FindBarWnd::Create(MainWindow* mainWin) {
    win = mainWin;
    layoutDpi = win->frameDpi > 0 ? win->frameDpi : DpiGetForHwnd(win->hwndFrame);
    // Layout() sizes the HWND to content; don't let WM_SIZE DoLayout first
    autoLayout = false;

    auto colBg = ThemeWindowControlBackgroundColor();
    auto colTxt = ThemeWindowTextColor();

    {
        CreateCustomArgs args;
        args.visible = false;
        args.style = WS_POPUP;
        // WS_EX_TOOLWINDOW keeps it off the taskbar. Not topmost: we make the
        // frame our owner instead (below) so the bar floats above the frame but
        // not above other apps.
        args.exStyle = WS_EX_TOOLWINDOW;
        args.isRtl = IsUIRtl();
        Rect frame = HwndWindowRect(win->hwndFrame);
        args.pos = {frame.x, frame.y, 1, 1};
        CreateCustom(args);
    }
    if (!hwnd) {
        return false;
    }
    // make the frame our owner: an owned window always renders above its owner
    // (so it stays visible when the user clicks into the document) yet drops
    // behind when another application is activated.
    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, (LONG_PTR)win->hwndFrame);
    SetColors(colTxt, colBg);

    {
        DropDown::CreateArgs args;
        args.parent = hwnd;
        args.font = GetAppFontForDpi(layoutDpi);
        args.isRtl = IsUIRtl();
        args.isEditable = true;
        edit = new DropDown();
        edit->SetColors(colTxt, colBg);
        edit->Create(args);
        ApplyFindEditScale(edit, layoutDpi);
        CbSetCueBanner(edit, Tr("Find"));
        edit->onTextChanged = MkMethod0<FindBarWnd, &FindBarWnd::OnTextChanged>(this);
        edit->onCloseUp = MkMethod0<FindBarWnd, &FindBarWnd::OnHistoryCommitted>(this);
        ApplyFindHistory(edit);
        if (!win->findEdit) {
            // don't steal it from the floating find window: this can run while
            // that one is up (RecreateFindBar on a theme change)
            win->findEdit = edit;
        }
    }

    // ellipsis: single line, vertically centered, so it lines up with the
    // (taller, bordered) edit box's text instead of sitting at the top
    status = NewVirtText({
        .font = GetAppFontForDpi(layoutDpi),
        .isRtl = IsUIRtl(),
        .ellipsis = true,
    });

    CreateButtons();

    BuildLayout();

    DarkModeApplyToPopupWindow(hwnd);
    Layout();

    onWindowMoved = MkFunc1Void(FindBarReposition);
    win->RegisterOnWindowMoved(&onWindowMoved);
    return true;
}

constexpr int kFindBarPadding = 2;
constexpr int kFindBarGap = 4;
constexpr int kFindBarDefaultEditDx = 220;
constexpr int kFindBarMinEditDx = 80;
// how wide the drag zone along the left edge is
constexpr int kFindBarResizeGripDx = 6;

void FindBarWnd::BuildLayout() {
    int p = UiScalePxForDpi(layoutDpi, kFindBarPadding);
    int gap = UiScalePxForDpi(layoutDpi, kFindBarGap);
    // cap preferred width at the min so HBox flex, not the typed text, sets the
    // edit's size (a long query would otherwise blow out the bar)
    int minEditDx = UiScalePxForDpi(layoutDpi, kFindBarMinEditDx);
    edit->idealDx = minEditDx;
    edit->maxDx = minEditDx;

    auto* row = new HBox();
    row->alignCross = CrossAxisAlign::CrossCenter;
    row->AddChild(edit, 1);
    gapAfterEdit = new Spacer(gap, 0);
    row->AddChild(gapAfterEdit);
    statusBox = new FindStatusBox(status, FindStatusDx(status->font, statusTotalHits, statusCapped));
    row->AddChild(statusBox);
    gapAfterStatus = new Spacer(gap, 0);
    row->AddChild(gapAfterStatus);
    for (VirtIconButton* b : btns) {
        row->AddChild(b);
    }
    for (int i : {2, 3}) btns[i]->SetVisibility(Visibility::Collapse);
    padLayout = new Padding(row, Insets{p, p, p, p});
    layout = padLayout;
}

int FindBarWnd::MinBarDx() const {
    if (!layout) {
        return 0;
    }
    int client = 2 * UiScalePxForDpi(layoutDpi, kFindBarPadding) + gapAfterEdit->dx +
                 UiScalePxForDpi(layoutDpi, kFindBarMinEditDx);
    for (int i : {0, 1, 4, 5}) client += btns[i]->MinIntrinsicWidth(0);
    Rect wr = HwndWindowRect(hwnd);
    Rect cr = HwndClientRect(hwnd);
    return client + (wr.dx - cr.dx);
}

void FindBarWnd::Layout(int forceBarDx) {
    // WM_SIZE can arrive from CreateCustom, before the tree exists
    if (!layout) {
        return;
    }
    for (VirtIconButton* b : {btns[4], btns[5], btns[6]}) b->SetVisibility(Visibility::Visible);
    status->SetVisibility(Visibility::Visible);
    statusBox->SetVisibility(Visibility::Visible);
    gapAfterStatus->SetVisibility(Visibility::Visible);
    edit->idealDx = edit->maxDx = UiScalePxForDpi(layoutDpi, kFindBarMinEditDx);
    if (forceBarDx > 0) {
        Rect wr = HwndWindowRect(hwnd);
        Rect cr = HwndClientRect(hwnd);
        int nonClientDx = wr.dx - cr.dx;
        int clientDx = std::max(1, forceBarDx - nonClientDx);
        int fullFixed = layout->MinIntrinsicWidth(0) - edit->idealDx;
        int navigationFixed = fullFixed - statusBox->dx - gapAfterStatus->dx - btns[6]->MinIntrinsicWidth(0);
        FindFieldFit fit = FitFindField(clientDx, fullFixed, navigationFixed, edit->idealDx);
        if (fit.compact) {
            btns[6]->SetVisibility(Visibility::Collapse);
            status->SetVisibility(Visibility::Collapse);
            statusBox->SetVisibility(Visibility::Collapse);
            gapAfterStatus->SetVisibility(Visibility::Collapse);
        }
        edit->idealDx = edit->maxDx = fit.editDx;
        inLayout = true;
        LayoutAndSizeToContent(layout, clientDx, 0, hwnd);
        DoLayout(HwndClientRect(hwnd).Size());
        inLayout = false;
    } else {
        int extra = UiScalePxForDpi(layoutDpi, kFindBarDefaultEditDx - kFindBarMinEditDx);
        int minDx = layout->MinIntrinsicWidth(0) + extra;
        inLayout = true;
        LayoutAndSizeToContent(layout, minDx, 0, hwnd);
        DoLayout(HwndClientRect(hwnd).Size());
        inLayout = false;
    }
    Rect wr = HwndWindowRect(hwnd);
    barDx = wr.dx;
    barDy = wr.dy;
    HwndInvalidate(hwnd);
}

void FindBarWnd::OnTextChanged() {
    if (suppressTextChanged) {
        return;
    }
    OnFindBarTextChanged(win);
}

namespace {
struct PickedTermData {
    MainWindow* win = nullptr;
    Str term;
    ~PickedTermData() { str::Free(term); }
};
} // namespace

static void StartPickedFindTask(PickedTermData* d) {
    AutoDelete del(d);
    MainWindow* win = d->win;
    if (!IsMainWindowValidAndNotClosing(win) || !win->findEdit) {
        return;
    }
    if (!str::Eq(win->findEdit->GetTextTemp(), d->term)) {
        win->findEdit->SetText(d->term);
    }
    CbEditSetModified(win->findEdit, false);
    FindTextOnThread(win, TextSearch::Direction::Forward, d->term, true, false);
}

// Run the search after the combo box is done closing its list. Starting it here
// would rebuild the history list from inside the combo's own notification, and
// the combo then finishes the close-up against a selection we just deleted -
// leaving the edit empty while the status still counted the old term.
void StartPickedFindTerm(MainWindow* win, Str term) {
    if (!win || len(term) == 0) {
        return;
    }
    auto* d = new PickedTermData;
    d->win = win;
    d->term = str::Dup(term);
    uitask::Post(MkFunc0<PickedTermData>(StartPickedFindTask, d), "TaskFindHistoryPick");
}

// picking an entry out of the open history list is a search request; walking
// the list with the arrow keys only fills the box, and waits for Enter
void FindBarWnd::OnHistoryCommitted() {
    if (suppressTextChanged || !edit) {
        return;
    }
    int idx = CbGetCurrentSelection(edit);
    if (idx < 0 || idx >= len(edit->items)) {
        return;
    }
    // Take the term from the list item, not the edit: a combo box has not
    // necessarily copied the picked item into its edit yet when it says the
    // list closed.
    StartPickedFindTerm(win, edit->items[idx]);
}

void FindBarWnd::OnSize(WindowBase::SizeEvent* ev) {
    if (ev->msg != WM_SIZE) {
        return;
    }
    // ignore the WM_SIZE our own SetWindowPos in Layout() generates
    if (!inLayout) {
        // window size (barDx), not client size from the size event
        Rect wr = HwndWindowRect(hwnd);
        Layout(wr.dx);
    }
}

void FindBarWnd::OnGetMinMaxInfo(WindowBase::GetMinMaxInfoEvent* ev) {
    if (barDy <= 0) {
        return;
    }
    // don't let the edit box be dragged away entirely, and keep the height
    // fixed: it comes from the font metrics, not from the user
    auto* mmi = ev->mmi;
    mmi->ptMinTrackSize.x = MinBarDx();
    mmi->ptMinTrackSize.y = barDy;
    mmi->ptMaxTrackSize.y = barDy;
}

// The bar is pinned to the right edge of the frame, so only its left edge
// can be dragged; report that edge as a sizing border and the default
// handling turns a drag there into a resize.
void FindBarWnd::OnNcHitTest(WindowBase::NcHitTestEvent* ev) {
    if (!ToolbarFindScreenRect(win).IsEmpty()) return;
    Rect wr = HwndWindowRect(hwnd);
    if (ev->screenPos.x < wr.x + UiScalePxForDpi(layoutDpi, kFindBarResizeGripDx)) {
        ev->result = HTLEFT;
        ev->didHandle = true;
    }
}

// Keep the HWND. RecreateFindBar on every WM_DPICHANGED double-freed the
// layout tree when a nested DPI change arrived during DestroyWindow
// (DameWare / RDP oscillating 96 vs 120).
void FindBarWnd::UpdateDpi(int dpi) {
    if (dpi <= 0) {
        dpi = GetDpi();
    }
    if (!layout || !edit) {
        return;
    }
    int prevDpi = layoutDpi > 0 ? layoutDpi : 96;
    PlatformFont* appFont = GetAppFontForDpi(dpi);
    ApplyFindEditScale(edit, dpi);
    if (status) {
        status->font = appFont;
    }
    int p = UiScalePxForDpi(dpi, kFindBarPadding);
    int gap = UiScalePxForDpi(dpi, kFindBarGap);
    int minEditDx = UiScalePxForDpi(dpi, kFindBarMinEditDx);
    edit->idealDx = minEditDx;
    edit->maxDx = minEditDx;
    if (gapAfterEdit) {
        gapAfterEdit->dx = gap;
    }
    if (gapAfterStatus) {
        gapAfterStatus->dx = gap;
    }
    if (padLayout) {
        padLayout->insets = Insets{p, p, p, p};
    }
    if (statusBox && status) {
        statusBox->dx = FindStatusDx(status->font, statusTotalHits, statusCapped);
    }
    int buttonPad = UiScalePxForDpi(dpi, 2);
    for (VirtIconButton* b : btns) {
        if (b) {
            b->padding = Insets{buttonPad, buttonPad, buttonPad, buttonPad};
            b->cornerRadius = UiScalePxForDpi(dpi, 6);
        }
    }
    int newBarDx = barDx > 0 ? MulDiv(barDx, dpi, prevDpi) : 0;
    layoutDpi = dpi;
    UpdateButtonIcons(dpi);
    if (newBarDx > 0) {
        Layout(newBarDx);
    } else {
        Layout();
    }
}

void FindBarWnd::OnDpiChanged(WindowBase::DpiChangedEvent* ev) {
    // Don't apply the suggested rect: we pin ourselves to the frame. A hidden
    // popup is parked on the primary and WM_DPICHANGED would move it (#5998).
    if (!layout) {
        ev->didHandle = true;
        return;
    }
    UpdateDpi((int)ev->dpiX);
    if (win) {
        FindBarReposition(win);
    }
    ev->didHandle = true;
}

void FindBarWnd::OnKeyDown(KeyEvent* ev) {
    if ((ev->vkey == VK_RETURN || ev->vkey == VK_SPACE) && vroot && vroot->focused && GetFocus() == hwnd) {
        WindowBase::CommandEvent command;
        command.wparam = vroot->focused->id;
        OnCommand(&command);
        ev->didHandle = true;
        return;
    }
    // the find edit lives in this owned popup, not as a child of the frame, so
    // the frame's edit accelerator table doesn't reach it; handle the find keys
    // here (Esc, Enter/Shift+Enter, F3/Shift+F3)
    switch (ev->vkey) {
        case 'F':
            if (ev->isCtrl && !ev->isAlt) {
                FocusFindEditSelectAll(win);
                ev->didHandle = true;
            }
            break;
        case VK_ESCAPE:
            if (CbIsDropped(edit))
                SendMessageW(edit->hwnd, CB_SHOWDROPDOWN, FALSE, 0);
            else
                HideFindBar(win);
            ev->didHandle = true;
            break;
        case VK_RETURN:
        case VK_F3:
            // Enter forces a pending debounced search to start now (find the
            // first match) instead of advancing to the next one (issue #4626)
            if (ev->vkey == VK_RETURN && FindFlushPendingSearch(win)) {
                ev->didHandle = true;
                break;
            }
            if (ev->isShift) {
                FindPrev(win);
            } else {
                FindNext(win);
            }
            ev->didHandle = true;
            break;
    }
}

void FindBarWnd::OnCommand(WindowBase::CommandEvent* ev) {
    int cmd = LOWORD(ev->wparam);
    switch (cmd) {
        case CmdFindPrev:
            FindPrev(win);
            break;
        case CmdFindNext:
            FindNext(win);
            break;
        case CmdFindToggleMatchCase:
            FindToggleMatchCase(win);
            break;
        case CmdFindToggleMatchWholeWord:
            FindToggleMatchWholeWord(win);
            break;
        case kFindBarPinCmdId:
            ToggleFloatingFindUI(win); // pop out into the floating window
            break;
        case kFindBarCloseCmdId:
            HideFindBar(win);
            break;
        case kFindBarOptionsCmdId: {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | (win->findMatchCase ? MF_CHECKED : 0), CmdFindToggleMatchCase,
                        CWStrTemp(ToWStrTemp(Tr("Match Case"))));
            AppendMenuW(menu, MF_STRING | (win->findMatchWholeWord ? MF_CHECKED : 0), CmdFindToggleMatchWholeWord,
                        CWStrTemp(ToWStrTemp(Tr("Match Whole Word"))));
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kFindBarPinCmdId, CWStrTemp(ToWStrTemp(Tr("Open in a window"))));
            Rect anchor = HwndMapLtrClientRectToScreen(hwnd, btns[6]->BoundsInWindow());
            inOptionsMenu = true;
            MarkMenuOwnerDraw(menu);
            int picked = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, anchor.x, anchor.Bottom(), 0,
                                        win->hwndFrame, nullptr);
            FreeMenuOwnerDrawInfoData(menu);
            DestroyMenu(menu);
            inOptionsMenu = false;
            if (picked) {
                WindowBase::CommandEvent command;
                command.wparam = picked;
                OnCommand(&command);
            }
            break;
        }
        default:
            return;
    }
    ev->didHandle = true;
}

void FindBarWnd::OnActivate(WindowBase::ActivateEvent* ev) {
    if (ev->state == WA_INACTIVE && !inOptionsMenu) SetTimer(hwnd, kFindBarCollapseTimer, 40, nullptr);
}

void FindBarWnd::OnTimer(WindowBase::TimerEvent* ev) {
    if (ev->timerId != kFindBarCollapseTimer) return;
    KillTimer(hwnd, kFindBarCollapseTimer);
    HWND focus = GetFocus();
    if (!inOptionsMenu && !CbIsDropped(edit) && focus != hwnd && !IsChild(hwnd, focus)) CollapseFindBar(win);
}

void FindBarWnd::OnPaint(WindowBase::PaintEvent* ev) {
    Rect rect = HwndClientRect(hwnd);
    Gfx* gfx = GfxCreateWithDoubleBuffer(this, ev->hdc);
    gfx->FillRect(rect, ThemeControlBackgroundColor());
    gfx->FillRoundedRect(rect, rect.dy / 2, ThemeWindowControlBackgroundColor(), ThemeEdgeColor());
    if (vroot) vroot->Paint(gfx, rect);
    delete gfx;
}

//--- public API

// Chrome-style floating search bar. Created hidden together with the toolbar;
// owns win->findEdit. Shown via Ctrl+F or the toolbar search icon.
FindBarWnd* CreateFindBar(MainWindow* win) {
    auto* bar = new FindBarWnd();
    bar->onCommand = MkMethod1<FindBarWnd, WindowBase::CommandEvent*, &FindBarWnd::OnCommand>(bar);
    bar->onSize = MkMethod1<FindBarWnd, WindowBase::SizeEvent*, &FindBarWnd::OnSize>(bar);
    bar->onGetMinMaxInfo = MkMethod1<FindBarWnd, WindowBase::GetMinMaxInfoEvent*, &FindBarWnd::OnGetMinMaxInfo>(bar);
    bar->onNcHitTest = MkMethod1<FindBarWnd, WindowBase::NcHitTestEvent*, &FindBarWnd::OnNcHitTest>(bar);
    bar->onDpiChanged = MkMethod1<FindBarWnd, WindowBase::DpiChangedEvent*, &FindBarWnd::OnDpiChanged>(bar);
    bar->onKeyDown = MkMethod1<FindBarWnd, KeyEvent*, &FindBarWnd::OnKeyDown>(bar);
    bar->onActivate = MkMethod1<FindBarWnd, WindowBase::ActivateEvent*, &FindBarWnd::OnActivate>(bar);
    bar->onTimer = MkMethod1<FindBarWnd, WindowBase::TimerEvent*, &FindBarWnd::OnTimer>(bar);
    bar->onPaint = MkMethod1<FindBarWnd, WindowBase::PaintEvent*, &FindBarWnd::OnPaint>(bar);
    if (!bar->Create(win)) {
        delete bar;
        return nullptr;
    }
    return bar;
}

void DeleteFindBar(MainWindow* win) {
    FindBarWnd* bar = win->findBar;
    if (!bar) {
        return;
    }
    // Null first: DestroyWindow can re-enter RecreateFindBar / DeleteFindBar
    // (nested WM_DPICHANGED) and would otherwise double-free the bar.
    win->findBar = nullptr;
    // only if this bar is the active find UI; the floating find window's edit
    // must survive us (see ShowFindWindow)
    if (win->findEdit == bar->edit) {
        win->findEdit = nullptr;
    }
    if (bar->hwnd) {
        ShowWindow(bar->hwnd, SW_HIDE);
    }
    delete bar;
}

void FindBarUpdateDpi(MainWindow* win) {
    if (!win || !win->findBar) {
        return;
    }
    int dpi = win->frameDpi > 0 ? win->frameDpi : DpiGet();
    win->findBar->UpdateDpi(dpi);
    FindBarReposition(win);
}

int FindBarFontHeight(MainWindow* win) {
    if (!win || !win->findBar || !win->findBar->edit) {
        return 0;
    }
    return PlatformFontLineHeight(win->findBar->edit->GetFont());
}

// Physical height of the compact bar, used by the focused DPI regression test.
int FindBarWindowHeight(MainWindow* win) {
    if (!win || !win->findBar || !win->findBar->hwnd) {
        return 0;
    }
    return HwndWindowRect(win->findBar->hwnd).dy;
}

// rebuild the bar so it picks up new theme colors / icons (called on theme change)
void RecreateFindBar(MainWindow* win) {
    if (!win->findBar) {
        return;
    }
    // stop any in-flight find/count that captured the old bar's state
    AbortFinding(win, true);
    FindBarWnd* oldBar = win->findBar;
    bool wasVisible = HwndIsVisible(oldBar->hwnd);
    bool wasActive = win->findEdit == oldBar->edit;
    TempStr text = wasActive ? str::DupTemp(oldBar->edit->GetTextTemp()) : TempStr();
    DeleteFindBar(win);
    win->findBar = CreateFindBar(win);
    if (!win->findBar) {
        return;
    }
    if (wasActive && win->findEdit == win->findBar->edit) {
        // Preserve the compact term even while the bar is hidden. F3 uses that
        // edit after a theme change, so dropping it silently changes searches.
        win->findBar->suppressTextChanged = true;
        win->findEdit->SetText(text);
        win->findBar->suppressTextChanged = false;
    }
    if (wasVisible) {
        ShowFindBar(win);
    }
}

// Position the bar at the right edge of the window so it doesn't cover the
// toolbar (issue #5739). The x is always the same as if the toolbar were hidden;
// only the y follows the toolbar: centered on the search icon when the toolbar
// is shown, else just below the frame top.
static void PositionFindBar(FindBarWnd* bar) {
    MainWindow* win = bar->win;
    Rect slot = ToolbarFindScreenRect(win);
    if (!slot.IsEmpty()) {
        if (bar->barDx != slot.dx) bar->Layout(slot.dx);
        int y = slot.y + (slot.dy - bar->barDy) / 2;
        SetWindowPos(bar->hwnd, HWND_TOP, slot.x, y, slot.dx, bar->barDy, SWP_NOACTIVATE);
        return;
    }
    Rect btn = GetToolbarButtonScreenRect(win, CmdFindFirst);
    Rect fr = HwndWindowRect(win->hwndFrame);
    // Align to the right edge of the client area, not the outer window rect:
    // HwndWindowRect includes the resize border (and sits off-screen when maximized),
    // which pushed the bar a few pixels too far right (#5762).
    Rect frClient = HwndMapLtrClientRectToScreen(win->hwndFrame, HwndClientRect(win->hwndFrame));
    Rect work = PlatformWindowWorkArea(win->hwndFrame);
    int available = std::max(1, std::min(frClient.dx, work.dx) - UiScalePxForDpi(bar->layoutDpi, 12));
    if (bar->barDx > available) bar->Layout(available);
    int cx = frClient.x + frClient.dx - bar->barDx;
    int cy;
    if (btn.IsEmpty()) {
        cy = fr.y + bar->barDy;
    } else {
        cy = btn.y + (btn.dy / 2) - (bar->barDy / 2);
    }
    Rect r{cx, cy, bar->barDx, bar->barDy};
    r = ShiftRectToWorkArea(r, win->hwndFrame, true);
    SetWindowPos(bar->hwnd, HWND_TOP, r.x, r.y, r.dx, r.dy, SWP_NOACTIVATE);
}

static void ShowCompactBar(MainWindow* win) {
    TempStr term = CurrentFindTermTemp(win);
    if (!win->findBar) {
        win->findBar = CreateFindBar(win);
    }
    if (!win->findBar) {
        return;
    }
    FindBarWnd* bar = win->findBar;
    win->findEdit = bar->edit;    // make this the active find edit
    win->findPagesEdit = nullptr; // page range is only on the floating window
    if (len(term) > 0 && CbGetTextLen(win->findEdit) == 0) {
        bar->suppressTextChanged = true;
        win->findEdit->SetText(term);
        bar->suppressTextChanged = false;
    }
    // reflect the current match-case / whole-word state on the toggle buttons
    FindBarSetMatchCaseChecked(win, win->findMatchCase);
    FindBarSetMatchWholeWordChecked(win, win->findMatchWholeWord);
    ToolbarSetFindExpanded(win, true, bar->MinBarDx(), UiScalePxForDpi(bar->layoutDpi, 360));
    PositionFindBar(bar);
    ShowWindow(bar->hwnd, SW_SHOW);
    win->findEdit->SetFocus();
    CbEditSelectAll(win->findEdit);
    // the restored term is only a starting point, not a search request: hitting
    // Ctrl+F must not re-run the last search behind the user's back
}

// "ShowFindBar" is the entry point used by FindFirst/Ctrl+F; it shows whichever
// find UI the user has chosen (compact overlay or floating window)
void ShowFindBar(MainWindow* win) {
    if (gSettings->searchUIFloating) {
        ShowFindWindow(win);
        return;
    }
    ShowCompactBar(win);
}

void HideFindBar(MainWindow* win) {
    win->searchStartMarked = false;
    // drop the cached results: they belong to this search/document and must not
    // be shown or navigated into after the find UI is reopened (e.g. on another
    // tab, which would carry the previous document's page/glyph coordinates)
    ClearFindMatches(win);
    if (win->ctrl) {
        // remove in-page find highlights in a chm / markdown webview
        // (no-op for other document types and the IE backend)
        win->ctrl->FindClear();
    }
    // drop the active TextSearch hit so closing find clears the highlight;
    // F3 still works (FindNext re-searches) and paints the new hit (#5802)
    if (DisplayModel* dm = win->AsFixed()) {
        if (dm->textSearch) {
            dm->textSearch->Reset();
        }
    }
    if (IsFindWindowVisible(win)) {
        HideFindWindow(win);
        return;
    }
    if (!win->findBar) {
        return;
    }
    AbortFinding(win, true);
    ShowWindow(win->findBar->hwnd, SW_HIDE);
    ToolbarSetFindExpanded(win, false);
    HwndSetFocus(win->hwndFrame);
    ScheduleRepaint(win, 0);
}

void CollapseFindBar(MainWindow* win) {
    if (!win || !win->findBar) return;
    auto* bar = win->findBar;
    bool hadFocus = GetFocus() == bar->hwnd || IsChild(bar->hwnd, GetFocus());
    KillTimer(bar->hwnd, kFindBarCollapseTimer);
    ShowWindow(bar->hwnd, SW_HIDE);
    ToolbarSetFindExpanded(win, false);
    if (hadFocus) HwndSetFocus(win->hwndFrame);
}

// note: the floating window is not anchored to the search icon, so "visible"
// here means specifically the compact bar (used to reposition it on move)
bool IsFindBarVisible(MainWindow* win) {
    return win->findBar && HwndIsVisible(win->findBar->hwnd);
}

// true if either the compact bar or the floating find window is visible
bool IsFindUIVisible(MainWindow* win) {
    return IsFindBarVisible(win) || IsFindWindowVisible(win);
}

// focus the find edit and select all text (Ctrl+F when find UI is already open)
void FocusFindEditSelectAll(MainWindow* win) {
    if (!win->findEdit) {
        return;
    }
    win->findEdit->SetFocus();
    CbEditSelectAll(win->findEdit);
}

void FindBarSyncHistory(MainWindow* win) {
    if (win && win->findBar && win->findBar->edit) {
        ApplyFindHistory(win->findBar->edit);
    }
}

// switch the find UI between the compact toolbar overlay and the floating
// window (persists the choice in gSettings->searchUIFloating)
void ToggleFloatingFindUI(MainWindow* win) {
    struct FindUiSwitchState {
        MainWindow* win = nullptr;
        Str text;
        Str pages;
        int selStart = 0;
        int selEnd = 0;
        bool hasText = false;
    };
    Vec<FindUiSwitchState> states;
    for (MainWindow* w : gWindows) {
        if (!IsFindUIVisible(w)) {
            continue;
        }
        FindUiSwitchState state;
        state.win = w;
        if (w->findEdit) {
            state.hasText = true;
            state.text = str::Dup(w->findEdit->GetTextTemp());
            CbEditGetSelection(w->findEdit, state.selStart, state.selEnd);
        }
        if (w->findPagesEdit) {
            state.pages = str::Dup(w->findPagesEdit->GetTextTemp());
        }
        VecAppend(states, state);
    }

    for (FindUiSwitchState& state : states) {
        HideFindBar(state.win); // dispatches: hides whichever find UI is currently visible
    }

    gSettings->searchUIFloating = !gSettings->searchUIFloating;
    ScheduleSaveSettings();

    auto restore = [](FindUiSwitchState& state) {
        MainWindow* w = state.win;
        ShowFindBar(w); // shows the now-active UI and repoints win->findEdit
        if (state.hasText && w->findEdit) {
            w->findEdit->SetText(state.text); // restore text (re-runs the search)
        }
        if (len(state.pages) > 0 && w->findPagesEdit) {
            w->findPagesEdit->SetText(state.pages);
        }
        if (w->findEdit) {
            w->findEdit->SetFocus();
            // restore the caret/selection last, after Show/SetText reset it
            CbEditSelectText(w->findEdit, state.selStart, state.selEnd);
        }
    };
    // Restore the initiating window last so switching another window does not
    // steal focus from it.
    for (FindUiSwitchState& state : states) {
        if (state.win != win) {
            restore(state);
        }
    }
    for (FindUiSwitchState& state : states) {
        if (state.win == win) {
            restore(state);
        }
        str::Free(state.text);
        str::Free(state.pages);
    }
}

// Exercise and report find-UI state for focused integration tests.
TempStr FindUiStateResultTemp(Str action, int* exitCodeOut) {
    str::Builder out;
    auto finish = [&](int code) -> Str {
        if (exitCodeOut) {
            *exitCodeOut = code;
        }
        return ToStrTemp(out);
    };
    if (len(gWindows) == 0) {
        out.Append(StrL("ERROR no-window\n"));
        return finish(1);
    }
    if (str::Eq(action, StrL("show-all"))) {
        for (MainWindow* w : gWindows) {
            ShowFindBar(w);
        }
    } else if (str::Eq(action, StrL("toggle-first"))) {
        ToggleFloatingFindUI(gWindows[0]);
    } else if (str::Eq(action, StrL("set-first-text"))) {
        if (gWindows[0]->findEdit) {
            gWindows[0]->findEdit->SetText(StrL("stale-term"));
        }
    } else if (str::Eq(action, StrL("clear-first"))) {
        if (gWindows[0]->findEdit) {
            gWindows[0]->findEdit->SetText(StrL(""));
        }
    } else if (str::Eq(action, StrL("hide-first"))) {
        HideFindBar(gWindows[0]);
    } else if (str::Eq(action, StrL("theme-recreate-first"))) {
        // RecreateFindBar is the compact bar's theme-change path.
        RecreateFindBar(gWindows[0]);
    } else if (!str::Eq(action, StrL("state"))) {
        out.Append(StrL("ERROR invalid action\n"));
        return finish(1);
    }
    int docs = 0;
    int compact = 0;
    int floating = 0;
    for (MainWindow* w : gWindows) {
        docs += w->IsDocLoaded() ? 1 : 0;
        compact += IsFindBarVisible(w) ? 1 : 0;
        floating += IsFindWindowVisible(w) ? 1 : 0;
    }
    int firstTextLen = gWindows[0]->findEdit ? CbGetTextLen(gWindows[0]->findEdit) : -1;
    // search state of the first window: highlighted matches, page of the
    // active hit (0: none) and whether a search is still running
    MainWindow* first = gWindows[0];
    int matches = len(first->findMatches);
    int hitPage = 0;
    DisplayModel* dm = first->AsFixed();
    if (dm && dm->textSearch && dm->textSearch->result.len > 0) {
        hitPage = dm->textSearch->result.pages[0];
    }
    bool busy = first->findThread || first->findCountThread || first->findDebouncePending;
    int page = first->ctrl ? first->ctrl->CurrentPageNo() : 0;
    out.Append(
        fmt("OK windows=%d docs=%d pref=%d compact=%d floating=%d firstTextLen=%d matches=%d hitPage=%d "
            "busy=%d page=%d\n",
            len(gWindows), docs, gSettings->searchUIFloating ? 1 : 0, compact, floating, firstTextLen, matches, hitPage,
            busy ? 1 : 0, page));
    if (first->findBar) {
        auto* bar = first->findBar;
        Rect slot = ToolbarFindScreenRect(first);
        out.Append(fmt("METRICS bar=%d slot=%d font=%d icon=%d widen=%d close=%d options=%d hwnd=%lld\n", bar->barDy,
                       slot.dy, FindBarFontHeight(first), FindIconSize(bar->layoutDpi),
                       bar->btns[4]->IsVisible() ? 1 : 0, bar->btns[5]->IsVisible() ? 1 : 0,
                       bar->btns[6]->IsVisible() ? 1 : 0, (i64)(LONG_PTR)bar->hwnd));
    }
    return finish(0);
}

// reposition over the search toolbar icon (no-op if not visible)
void FindBarReposition(MainWindow* win) {
    if (!IsFindBarVisible(win)) {
        return;
    }
    // the current document may not support find (e.g. switched to an
    // image-only doc / CHM); don't leave an orphaned, inert bar floating
    if (!NeedsFindUI(win)) {
        HideFindBar(win);
        return;
    }
    ToolbarSetFindExpanded(win, true, win->findBar->MinBarDx(), UiScalePxForDpi(win->findBar->layoutDpi, 360));
    PositionFindBar(win->findBar);
}

// Widen the status slot only when a bigger count needs it. Relaying out on
// every status change resized the search field, and a combo box re-sets its
// edit's text on WM_SIZE - selecting all of it under the typing user (#6068).
bool FindBarWnd::UpdateStatusWidth(int totalHits, bool capped) {
    if (!statusBox || totalHits < 0) {
        return false;
    }
    int dx = FindStatusDx(status->font, totalHits, capped);
    if (statusBox->dx == dx) {
        return false;
    }
    statusBox->dx = dx;
    statusTotalHits = totalHits;
    statusCapped = capped;
    return true;
}

// show n/m or "No matches" style status in the bar
void FindBarSetStatus(MainWindow* win, Str s, int totalHits) {
    if (gSettings->searchUIFloating) {
        FindWindowSetStatus(win, s, totalHits);
        return;
    }
    FindBarWnd* bar = win->findBar;
    if (!bar || !bar->status) {
        return;
    }
    Str text = s ? s : StrL("");
    bool capped = str::EndsWith(text, StrL("+"));
    bool widthChanged = bar->UpdateStatusWidth(totalHits, capped);
    bar->status->SetText(text);
    if (!widthChanged) {
        bar->status->Invalidate();
        return;
    }
    if (bar->barDx > 0) {
        bar->Layout(bar->barDx);
    } else {
        bar->Layout();
    }
    FindBarReposition(win);
}

// idx into FindBarWnd::btns
constexpr int kBtnMatchCase = 2;
constexpr int kBtnMatchWholeWord = 3;

static void FindBarSetBtnChecked(MainWindow* win, int idx, bool checked) {
    if (!win->findBar) {
        return;
    }
    VirtIconButton* b = win->findBar->btns[idx];
    if (!b || b->isSelected == checked) {
        return;
    }
    b->isSelected = checked;
    b->Invalidate();
}

// reflect match-case toggle state on the bar's button
void FindBarSetMatchCaseChecked(MainWindow* win, bool checked) {
    if (gSettings->searchUIFloating) {
        FindWindowSetMatchCaseChecked(win, checked);
        return;
    }
    FindBarSetBtnChecked(win, kBtnMatchCase, checked);
}

// reflect match-whole-word toggle state on the bar's button
void FindBarSetMatchWholeWordChecked(MainWindow* win, bool checked) {
    if (gSettings->searchUIFloating) {
        FindWindowSetMatchWholeWordChecked(win, checked);
        return;
    }
    FindBarSetBtnChecked(win, kBtnMatchWholeWord, checked);
}
