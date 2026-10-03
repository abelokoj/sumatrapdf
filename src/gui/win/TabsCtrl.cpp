/* Copyright 2024 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "base/Base.h"
#include "base/Win.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"

#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "gui/win/TabsCtrl.h"

// Forward declaration - defined in MainWindow.cpp
struct MainWindow;
MainWindow* FindMainWindowByHwnd(HWND hwnd);

//--- Tabs
//
// TabsCtrl is a VirtCtrl hosted in its own child HWND so the frame can
// SetWindowPos it, and so the drag loop can capture the mouse and map to
// screen coordinates. Each tab is a TabCtrl child with a VirtCloseButton.

static Kind kindTabs = "tabs";
static Kind kindTabCtrl = "tabCtrl";

using Gdiplus::Bitmap;
using Gdiplus::CompositingQualityHighQuality;
using Gdiplus::Font;
using Gdiplus::Graphics;
using Gdiplus::SolidBrush;
using Gdiplus::StringAlignmentCenter;
using Gdiplus::StringFormat;
using Gdiplus::TextRenderingHintClearTypeGridFit;
using Gdiplus::UnitPixel;

static const WStr kTabsCtrlClassName = L"SumatraTabsCtrlClass";

struct TabListPopup {
    StrVec titles;
    Vec<TabInfo*> tabs;
    PlatformFont* font = nullptr;
    int rowDy = 0;
    int width = 0;
    int padding = 0;
    int selected = -1;
    bool isRtl = false;
    Color background = 0;
    Color selectedBackground = 0;
    Color textColor = 0;
    HBRUSH brush = nullptr;

    ~TabListPopup() {
        if (brush) {
            DeleteObject(brush);
        }
    }
};

static Rect TabSeparatorRect(Rect r, int thickness, bool isRtl) {
    int inset = r.dy / 4;
    return {isRtl ? r.x : r.Right() - thickness, r.y + inset, thickness, r.dy - inset * 2};
}

// hwnd is kept LTR (like the canvas); UI direction comes from the parent frame
static bool IsTabsRtl(HWND hwnd) {
    HWND parent = GetParent(hwnd);
    return parent && HwndIsRtl(parent);
}

TabInfo::~TabInfo() {
    str::Free(text);
    str::Free(tooltip);
    str::Free(pageText);
}

static Gdiplus::Color GdipCol(Color c) {
    return GdiRgbFromColor(c);
}

// the text stays readable on a tab that carries a color of its own
static Color TabTextColorForBackground(Color text, Color tabBg) {
    if (abs((int)GetLightness(text) - (int)GetLightness(tabBg)) >= 80) {
        return text;
    }
    return IsLightColor(tabBg) ? kColBlack : kColWhite;
}

//--- TabCtrl: one tab

// paints the tab (background, title, dirty dot) and hosts its ✕. It doesn't own
// its TabInfo: the control's `tabs` does
struct TabCtrl : VirtCtrl {
    TabsCtrl* tabsCtrl = nullptr;
    TabInfo* ti = nullptr;
    VirtCloseButton* closeBtn = nullptr;
    Size idealSize;
    // the ✕ glyph itself, inside the close button's larger hit area
    Rect rClose;

    TabCtrl();
    ~TabCtrl() override = default;

    int Idx();
    bool IsSelected();
    bool IsUnderMouse();
    bool CloseVisible();
    Color BgColor();

    Size GetIdealSize() override;
    void SetBounds(Rect) override;
    void Paint(VirtPaintCtx&) override;
    void OnMouseDown(VirtMouseEvent*);
    void OnMouseUp(VirtMouseEvent*);
    void OnGetTooltip(VirtTooltipEvent*);
    void OnCloseClick(VirtMouseEvent* ev = nullptr);
};

TabCtrl::TabCtrl() {
    kind = kindTabCtrl;
    colorDefaults = gColsTab;
    nColors = kColTabCount;
    onMouseDown = MkMethod1<TabCtrl, VirtMouseEvent*, &TabCtrl::OnMouseDown>(this);
    onMouseUp = MkMethod1<TabCtrl, VirtMouseEvent*, &TabCtrl::OnMouseUp>(this);
    onGetTooltip = MkMethod1<TabCtrl, VirtTooltipEvent*, &TabCtrl::OnGetTooltip>(this);
    closeBtn = new VirtCloseButton();
    closeBtn->onClick = MkMethod1<TabCtrl, VirtMouseEvent*, &TabCtrl::OnCloseClick>(this);
    closeBtn->visibility = Visibility::Collapse;
    AddChild(closeBtn);
}

int TabCtrl::Idx() {
    return tabsCtrl ? VecFind(tabsCtrl->tabs, ti) : -1;
}

bool TabCtrl::IsSelected() {
    int idx = Idx();
    if (idx < 0) {
        return false;
    }
    TabsCtrl* tc = tabsCtrl;
    if (tc->IsValidIdx(tc->tabForceShowSelected)) {
        return idx == tc->tabForceShowSelected;
    }
    return idx == tc->selectedIdx;
}

bool TabCtrl::IsUnderMouse() {
    return tabsCtrl && tabsCtrl->tabHighlighted == Idx();
}

Color TabCtrl::BgColor() {
    Color selected = GetColor(kColTabBg);
    Color inactive = GetColor(kColTabInactiveBg);
    bool isSelected = IsSelected();
    bool isUnderMouse = IsUnderMouse();
    // a tab with a color of its own keeps it, shaded when it isn't selected
    if (!IsSpecialColor(ti->tabColor)) {
        if (isSelected) {
            return ti->tabColor;
        }
        return AccentColor(ti->tabColor, isUnderMouse ? 35 : 25);
    }
    if (isSelected) {
        return selected;
    }
    return isUnderMouse ? AccentColor(inactive, 10) : inactive;
}

Size TabCtrl::GetIdealSize() {
    return idealSize;
}

// the ✕ is inset from the tab's edge, but its hit area is the whole gutter
// (~40 DIP, full height) so it stays easy to hit
void TabCtrl::SetBounds(Rect r) {
    VirtCtrl::SetBounds(r);
    HWND hwnd = GetHwnd();
    int dx = r.dx;
    int dy = r.dy;

    int closeDy = std::min(std::max(tabsCtrl->ScaleMetric(12), tabsCtrl->tabIconDx), dy);
    int closeDx = closeDy;

    // Padding between circle and tab edge; grow with the button.
    int closePad = std::max(tabsCtrl->ScaleMetric(6), closeDx / 2);
    // Keep the glyph inside the tab when tabs are very narrow.
    if (closeDx + closePad > dx && dx > 0) {
        closeDx = std::min(closeDx, std::max(tabsCtrl->ScaleMetric(12), dx - 2));
        closeDy = closeDx;
        closePad = std::max(1, (dx - closeDx) / 2);
    }
    int closeY = (dy - closeDy) / 2;

    // Hit target: at least ~40 DIP wide (touch-friendly), full tab height.
    // Cap at half the tab so title still has a drag/select zone.
    int minHitDx = tabsCtrl->ScaleMetric(40);
    int hitDx = std::max(closeDx + (2 * closePad), minHitDx);
    hitDx = std::min(hitDx, std::max(closeDx + closePad, dx / 2));
    hitDx = std::min(hitDx, dx);

    bool isRtl = IsTabsRtl(hwnd);
    Rect hit;
    if (isRtl) {
        hit = {r.x, r.y, hitDx, dy};
        rClose = {r.x + closePad, r.y + closeY, closeDx, closeDy};
    } else {
        hit = {r.x + dx - hitDx, r.y, hitDx, dy};
        rClose = {r.x + dx - closeDx - closePad, r.y + closeY, closeDx, closeDy};
    }
    // the glyph is painted in the button's content rect, so the padding is what
    // makes the hit area bigger than the ✕ itself
    closeBtn->padding.left = rClose.x - hit.x;
    closeBtn->padding.top = rClose.y - hit.y;
    closeBtn->padding.right = hit.Right() - rClose.Right();
    closeBtn->padding.bottom = hit.Bottom() - rClose.Bottom();
    closeBtn->SetBounds(hit);
}

// like Chrome: only the selected tab shows (and hit-tests) its ✕, so a click
// on a non-selected tab always selects it and can't accidentally close it
bool TabCtrl::CloseVisible() {
    return ti->canClose && IsSelected();
}

void TabCtrl::Paint(VirtPaintCtx& ctx) {
    Gfx* gfx = ctx.gfx;
    HWND hwnd = GetHwnd();
    Rect r = ctx.bounds;
    Color tabBgCol = BgColor();
    Color textColor = TabTextColorForBackground(GetColor(kColTabText), tabBgCol);
    if (ti->isError) {
        // a tab whose document failed to load shows its title in red, shaded
        // to stay readable on light and dark tab backgrounds
        textColor = IsLightColor(tabBgCol) ? MkRgb(0xC4, 0x1E, 0x1E) : MkRgb(0xFF, 0x6A, 0x6A);
    }

    gfx->FillRoundedRect(r, tabsCtrl->ScaleMetric(8), tabBgCol);

    bool isRtl = IsTabsRtl(hwnd);
    Rect separator = TabSeparatorRect(r, std::max(1, tabsCtrl->ScaleMetric(1)), isRtl);
    gfx->FillRect(separator, AccentColor(tabBgCol, 48));
    PlatformFont* font = tabsCtrl->GetFont();

    // draw text — inset from the close glyph (size varies with tab height),
    // or using the full tab width when the ✕ is hidden
    Rect rTxt = r;
    int textPad = tabsCtrl->ScaleMetric(8);
    int textGap = tabsCtrl->ScaleMetric(4);
    bool closeVisible = CloseVisible();
    if (isRtl) {
        // RTL: close on the left — text after the close circle
        int textLeft = closeVisible ? rClose.x + rClose.dx + textGap : r.x + textPad;
        rTxt.x = textLeft;
        rTxt.dx = std::max(0, (r.x + r.dx - textPad) - textLeft);
    } else {
        // LTR: close on the right — text before the close circle
        rTxt.x = r.x + textPad;
        int textRight = closeVisible ? rClose.x - textGap : r.x + r.dx - textPad;
        rTxt.dx = std::max(0, textRight - rTxt.x);
    }
    PlatformFont* pageFont = font;
    int pageDx = 0;
    if (len(ti->pageText) > 0) {
        PlatformFont* scaled = GetScaledPlatformFont(font, 85);
        if (scaled) {
            pageFont = scaled;
        }
        pageDx = gfx->MeasureText(ti->pageText, pageFont).dx;
        if (pageDx + tabsCtrl->ScaleMetric(12) >= rTxt.dx) {
            pageDx = 0;
        }
    }

    Rect rFile = rTxt;
    Rect rPage{};
    if (pageDx > 0) {
        if (isRtl) {
            rPage = {rTxt.x, rTxt.y, pageDx, rTxt.dy};
            rFile.x = rTxt.x + pageDx;
            rFile.dx = rTxt.dx - pageDx;
        } else {
            rFile.dx = rTxt.dx - pageDx;
            rPage = {rFile.Right(), rTxt.y, pageDx, rTxt.dy};
        }
    }

    u32 fmt = gfxTextEllipsis | gfxTextVCenter | (isRtl ? gfxTextRight : gfxTextLeft);
    gfx->DrawText(ti->text, rFile, fmt, font, textColor);
    if (pageDx > 0) {
        Color pageCol = AccentColor(textColor, 40);
        u32 pageFmt = gfxTextVCenter | gfxTextNoClip | (isRtl ? gfxTextRight : gfxTextLeft);
        gfx->DrawText(ti->pageText, rPage, pageFmt, pageFont, pageCol);
    }

    // draw red dot after tab text for dirty (unsaved) tabs
    if (ti->isDirty) {
        int dotRadius = tabsCtrl->ScaleMetric(3);
        // the text may have been ellipsized, so the dot goes after whichever is
        // narrower: the text or the room it had
        int textDx = std::min(gfx->MeasureText(ti->text, font).dx, rFile.dx);
        int textEnd = isRtl ? rFile.Right() : rFile.x + textDx;
        int maxX = rFile.Right() - (dotRadius * 2);
        int dotX = std::min(textEnd + dotRadius, maxX);
        int dotY = r.y + ((r.dy - (dotRadius * 2)) / 2);
        gfx->FillEllipse({dotX, dotY, dotRadius * 2, dotRadius * 2}, MkRgb(0xEE, 0x22, 0x22));
    }

    // the ✕ blends into the tab, so it takes the tab's background
    closeBtn->SetColor(kColCloseCircle, tabBgCol);
}

void TabCtrl::OnMouseDown(VirtMouseEvent* ev) {
    tabsCtrl->OnTabMouseDown(this, *ev);
    ev->didHandle = true;
}

void TabCtrl::OnMouseUp(VirtMouseEvent* ev) {
    // the drag / migration handling needs capture and screen coordinates, so it
    // stays in the host WndProc
    ev->didHandle = true;
}

// VirtRoot::UpdateTooltip shows this. A second TTF_SUBCLASS tooltip on the
// tab HWND used to appear as well, so the tip showed twice.
void TabCtrl::OnGetTooltip(VirtTooltipEvent* ev) {
    if (!tabsCtrl || !tabsCtrl->withToolTips || !ti) {
        return;
    }
    ev->tip = str::DupTemp(ti->tooltip);
}

void TabCtrl::OnCloseClick(VirtMouseEvent*) {
    tabsCtrl->CloseTab(Idx());
}

//--- TabsCtrl host window

static LRESULT CALLBACK TabsCtrlWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    TabsCtrl* tabs = (TabsCtrl*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lparam;
        tabs = (TabsCtrl*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)tabs);
        tabs->hwnd = hwnd;
    }
    if (tabs && tabs->hwnd == hwnd) {
        return tabs->WndProc(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void RegisterTabsCtrlClass() {
    static ATOM atom = 0;
    if (atom) {
        return;
    }
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = TabsCtrlWindowProc;
    wcex.hInstance = GetInstance();
    wcex.hCursor = GetCachedCursor(IDC_ARROW);
    wcex.lpszClassName = kTabsCtrlClassName.s;
    atom = RegisterClassExW(&wcex);
    ReportIf(!atom);
}

//--- TabsCtrl

TabsCtrl::TabsCtrl() {
    kind = kindTabs;
    colorDefaults = gColsTab;
    nColors = kColTabCount;
}

TabsCtrl::~TabsCtrl() {
    Destroy();
    DeleteVecMembers(tabs);
    // TabCtrl children are owned via VirtCtrl::children
}

void TabsCtrl::Destroy() {
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        HwndDestroyWindowSafe(&hwnd);
    }
    if (vroot) {
        // tops do not own us; clear root pointer before deleting
        VecReset(vroot->tops);
        delete vroot;
        vroot = nullptr;
    }
    SetRoot(nullptr);
}

void TabsCtrl::ScheduleRepaint() {
    if (hwnd) {
        HwndScheduleRepaint(hwnd);
    }
}

bool TabsCtrl::IsValidIdx(int idx) {
    return idx >= 0 && idx < TabCount();
}

int TabsCtrl::TabCount() {
    return len(tabs);
}

TabCtrl* TabsCtrl::TabCtrlAt(int idx) {
    if (!IsValidIdx(idx)) {
        return nullptr;
    }
    return tabCtrls[idx];
}

int TabsCtrl::ScaleMetric(int logicalPx) const {
    int dpi = hwnd ? DpiGetForHwnd(hwnd) : DpiGet();
    return DpiScaleByDpi(dpi, (int)lroundf(logicalPx * interfaceScale));
}

PlatformFont* TabsCtrl::GetFont() const {
    return font;
}

void TabsCtrl::SetFont(PlatformFont* fontIn) {
    font = fontIn;
    if (hwnd) {
        HwndSetFont(hwnd, fontIn ? fontIn->GetHFont() : nullptr);
    }
    ScheduleRepaint();
}

void TabsCtrl::SetIsVisible(bool show) {
    VirtCtrl::SetIsVisible(show);
    if (!hwnd) {
        return;
    }
    HwndSetWindowStyle(hwnd, WS_VISIBLE, show);
}

bool TabsCtrl::IsVisible() const {
    return VirtCtrl::IsVisible();
}

// the tab wnds are rebuilt whenever the tab list changes: there are few of them
// and it keeps the tree and the list impossible to get out of step
void TabsCtrl::RebuildTabCtrls() {
    RemoveAllChildren(true);
    VecReset(tabCtrls);
    int n = TabCount();
    for (int i = 0; i < n; i++) {
        auto* w = new TabCtrl();
        w->tabsCtrl = this;
        w->ti = tabs[i];
        VecAppend(tabCtrls, w);
        AddChild(w);
    }
}

// Calculates the size of a tab and lays the children out.
void TabsCtrl::LayoutTabs() {
    if (!hwnd || !vroot) {
        return;
    }
    Rect rect = HwndClientRect(hwnd);
    int dy = rect.dy;
    int nTabs = TabCount();
    if (nTabs == 0) {
        // Do not ScheduleRepaint here: an empty bar with a forced repaint can
        // re-enter layout/paint forever if something keeps calling LayoutTabs
        // (issue #5861). The parent hides the control when there are no tabs.
        return;
    }
    int dx;
    if (tabWidthFrozen && frozenTabDx > 0) {
        dx = frozenTabDx;
    } else {
        auto maxDx = (rect.dx - 5) / nTabs;
        dx = std::max(tabMinDx, std::min(tabDefaultDx, maxDx));
    }
    dx = std::max(tabMinDx, dx);
    hasOverflow = dx * nTabs > rect.dx;
    scrollButtonDx = std::max(ScaleMetric(32), tabIconDx + ScaleMetric(12));
    viewportDx = std::max(1, rect.dx - (hasOverflow ? scrollButtonDx * 3 : 0));
    scrollDx = limitValue(scrollDx, 0, std::max(0, dx * nTabs - viewportDx));
    tabSize = {dx, dy};
    if (IsRunningOnWine()) {
        logf("TabsCtrl::LayoutTabs: hwnd=%p client=(%d,%d) tabSize=(%d,%d) nTabs=%d\n", hwnd, rect.dx, rect.dy,
             tabSize.dx, tabSize.dy, nTabs);
    }

    for (int i = 0; i < nTabs; i++) {
        tabCtrls[i]->idealSize = tabSize;
    }

    // pack tabs left-to-right (LTR) or right-aligned with reversed order (RTL)
    bool isRtl = IsTabsRtl(hwnd);
    int totalW = nTabs * tabSize.dx;
    int x = -scrollDx;
    if (isRtl && totalW < viewportDx) {
        x = viewportDx - totalW;
    }
    vroot->SetBounds(rect);
    VirtCtrl::SetBounds(rect);

    for (int i = 0; i < nTabs; i++) {
        int idx = isRtl ? (nTabs - 1 - i) : i;
        TabCtrl* t = tabCtrls[idx];
        Rect r = {x, 0, tabSize.dx, tabSize.dy};
        // absolute client coords; TabCtrl::SetBounds rebases via parent origin
        t->SetBounds({rect.x + r.x, rect.y + r.y, r.dx, r.dy});
        x += tabSize.dx;
    }
}

void TabsCtrl::ScrollTabs(int direction) {
    if (!hasOverflow) {
        return;
    }
    scrollDx += direction * std::max(tabSize.dx, 1);
    LayoutTabs();
    ScheduleRepaint();
}

// Finds the index of the tab which contains the given point.
TabsCtrl::MouseState TabsCtrl::TabStateFromMousePosition(const Point& p) {
    TabsCtrl::MouseState res;
    if (p.x < 0 || p.y < 0 || (hasOverflow && p.x >= viewportDx) || !vroot) {
        return res;
    }
    Point ptLocal{0, 0};
    ILayout* el = ElementFromPoint(vroot, p, &ptLocal);
    VirtCtrl* hit = el ? el->AsVirtCtrl() : nullptr;
    // the only child of a tab is its ✕, so anything below a tab is the ✕
    bool overClose = hit && hit->parent && IsVirtCtrlOfKind(hit->parent, kindTabCtrl);
    TabCtrl* tab = nullptr;
    for (VirtCtrl* w = hit; w; w = w->parent) {
        if (IsVirtCtrlOfKind(w, kindTabCtrl)) {
            tab = (TabCtrl*)w;
            break;
        }
    }
    if (!tab) {
        return res;
    }
    res.tabIdx = tab->Idx();
    res.tabInfo = tab->ti;
    res.overClose = overClose && tab->CloseVisible();
    Rect r = tab->BoundsInWindow();
    Rect rightHalf = r;
    int halfDx = r.dx / 2;
    rightHalf.x = r.x + halfDx;
    rightHalf.dx = halfDx;
    res.inRightHalf = rightHalf.Contains(p);
    return res;
}

// which tab the mouse is on decides the highlight and which ✕ is shown
void TabsCtrl::UpdateHover(int tabUnderMouse) {
    bool changed = (tabHighlighted != tabUnderMouse);
    tabHighlighted = tabUnderMouse;
    int n = TabCount();
    for (int i = 0; i < n; i++) {
        TabCtrl* w = tabCtrls[i];
        auto vis = w->CloseVisible() ? Visibility::Visible : Visibility::Collapse;
        if (w->closeBtn->visibility != vis) {
            w->closeBtn->visibility = vis;
            changed = true;
        }
    }
    if (changed) {
        ScheduleRepaint();
    }
}

HBITMAP TabsCtrl::RenderForDragging(int idx) {
    TabInfo* ti = GetTab(idx);
    TabCtrl* tw = TabCtrlAt(idx);
    if (!ti || !tw) {
        return nullptr;
    }
    Rect r = tw->BoundsInWindow();
    Bitmap bitmap(r.dx, r.dy);
    Graphics* gfx = Graphics::FromImage(&bitmap);
    // DrawString() on a bitmap does not work with CompositingModeSourceCopy - obscure bug.
    gfx->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    gfx->SetCompositingQuality(CompositingQualityHighQuality);
    gfx->SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    gfx->SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gfx->SetPageUnit(UnitPixel);

    StringFormat sfFile(StringFormat::GenericDefault());
    sfFile.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    sfFile.SetLineAlignment(StringAlignmentCenter);
    sfFile.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    StringFormat sfPage(StringFormat::GenericDefault());
    sfPage.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    sfPage.SetLineAlignment(StringAlignmentCenter);
    sfPage.SetTrimming(Gdiplus::StringTrimmingNone);

    // the drag image is the tab as it looks while selected
    Color bgCol = GetColor(kColTabBg);
    Color textCol = TabTextColorForBackground(GetColor(kColTabText), bgCol);

    SolidBrush br(GdipCol(bgCol));
    Gdiplus::Rect gr(0, 0, r.dx, r.dy);
    gfx->FillRectangle(&br, gr);

    Gdiplus::Font* f = GetFont() ? GetFont()->GetGdiplusFont() : nullptr;
    bool ownedFont = false;
    if (!f && GetFont()) {
        HDC hdc = GetDC(hwnd);
        f = new Font(hdc, GetFont()->GetHFont());
        ReleaseDC(hwnd, hdc);
        ownedFont = true;
    }

    Gdiplus::RectF rTxt(0, 0, (float)r.dx, (float)r.dy);
    int textPad = ScaleMetric(8);
    rTxt.X += textPad;
    rTxt.Width -= textPad * 2;

    int pageDx = 0;
    if (len(ti->pageText) > 0 && GetFont()) {
        pageDx = PlatformFontMeasureText(GetFont(), ti->pageText).dx;
        if (pageDx + ScaleMetric(12) >= (int)rTxt.Width) {
            pageDx = 0;
        }
    }
    Gdiplus::RectF rFile = rTxt;
    Gdiplus::RectF rPage = rTxt;
    if (pageDx > 0) {
        rFile.Width = rTxt.Width - (float)pageDx;
        rPage.X = rFile.X + rFile.Width;
        rPage.Width = (float)pageDx;
    }

    br.SetColor(GdipCol(textCol));
    WCHAR* ws = CWStrTemp(ti->text);
    gfx->DrawString(ws, -1, f, rFile, &sfFile, &br);
    if (pageDx > 0) {
        br.SetColor(GdipCol(AccentColor(textCol, 40)));
        WCHAR* wsPage = CWStrTemp(ti->pageText);
        gfx->DrawString(wsPage, -1, f, rPage, &sfPage, &br);
    }
    if (ownedFont) {
        delete f;
    }

    HBITMAP ret;
    bitmap.GetHBITMAP(Gdiplus::Color(255, 255, 255), &ret);
    delete gfx;
    return ret;
}

// must be called after LayoutTabs()
static void TabsCtrlUpdateAfterChangingTabsCount(TabsCtrl* tabs) {
    HWND hwnd = tabs->hwnd;
    if (hwnd && GetCapture() == hwnd) {
        ReleaseCapture();
    }
    tabs->tabBeingClosed = -1;
    Point mousePos = HwndGetCursorPos(hwnd);
    auto tabState = tabs->TabStateFromMousePosition(mousePos);
    bool canClose = tabState.tabInfo && tabState.tabInfo->canClose;
    bool overClose = tabState.overClose && canClose;
    int tabUnderMouse = tabState.tabIdx;
    tabs->UpdateHover(tabUnderMouse);
    tabs->tabHighlightedClose = overClose ? tabUnderMouse : -1;
    if (tabs->draggingTab) {
        tabs->draggingTab = false;
        ImageList_EndDrag();
    }
}

static void TriggerSelectionChanged(TabsCtrl* tabs) {
    if (!tabs->onSelectionChanged.IsValid()) {
        return;
    }
    TabsCtrl::SelectionChangedEvent ev;
    ev.tabs = tabs;
    ev.tabIdx = tabs->selectedIdx;
    tabs->onSelectionChanged.Call(&ev);
}

static bool TriggerSelectionChanging(TabsCtrl* tabs) {
    if (!tabs->onSelectionChanging.IsValid()) {
        // allow changing
        return false;
    }

    TabsCtrl::SelectionChangingEvent ev;
    tabs->onSelectionChanging.Call(&ev);
    return ev.preventChanging;
}

static HMENU BuildTabListMenu(TabsCtrl* tc, TabListPopup& popup) {
    HWND hwnd = tc->hwnd;
    popup.font = tc->GetFont();
    popup.padding = tc->ScaleMetric(12);
    popup.isRtl = IsTabsRtl(hwnd);
    popup.selected = tc->selectedIdx;
    popup.background = tc->GetColor(kColTabInactiveBg);
    popup.selectedBackground = tc->GetColor(kColTabBg);
    popup.textColor = tc->GetColor(kColTabText);
    HDC dc = GetDC(hwnd);
    HGDIOBJ oldFont = SelectObject(dc, popup.font->GetHFont());
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    popup.rowDy = std::max(tc->ScaleMetric(32), (int)metrics.tmHeight + tc->ScaleMetric(12));
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        SelectObject(dc, oldFont);
        ReleaseDC(hwnd, dc);
        return nullptr;
    }
    for (int i = 0; i < tc->TabCount(); i++) {
        TabInfo* tab = tc->tabs[i];
        popup.titles.Append(tab->text);
        VecAppend(popup.tabs, tab);
        SIZE size{};
        WStr title = ToWStrTemp(tab->text);
        GetTextExtentPoint32W(dc, title.s, len(title), &size);
        popup.width = std::max(popup.width, (int)size.cx);
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STRING;
        item.fType = MFT_OWNERDRAW;
        item.wID = i + 1;
        item.dwItemData = i;
        item.dwTypeData = CWStrTemp(title);
        InsertMenuItemW(menu, i, TRUE, &item);
    }
    SelectObject(dc, oldFont);
    ReleaseDC(hwnd, dc);

    RECT work{};
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    work = monitor.rcWork;
    popup.width = std::min(std::max(tc->ScaleMetric(240), popup.width + popup.padding * 3 + popup.rowDy),
                           std::max(1, (int)(work.right - work.left) - tc->ScaleMetric(16)));
    int rows = std::min(tc->TabCount(), limitValue(tc->tabListVisibleItems, 1, 50));
    MENUINFO info{sizeof(info)};
    popup.brush = CreateSolidBrush(popup.background);
    info.fMask = MIM_MAXHEIGHT | MIM_BACKGROUND;
    info.hbrBack = popup.brush;
    info.cyMax = std::min(rows * popup.rowDy + GetSystemMetrics(SM_CYBORDER) * 2,
                          std::max(1, (int)(work.bottom - work.top) - tc->ScaleMetric(16)));
    SetMenuInfo(menu, &info);
    return menu;
}

void TabsCtrl::ShowTabList() {
    if (!hwnd || tabListPopup || TabCount() == 0) {
        return;
    }
    TabListPopup popup;
    HMENU menu = BuildTabListMenu(this, popup);
    if (!menu) {
        return;
    }

    HWND host = hwnd;
    tabListPopup = &popup;
    Rect rect = HwndClientRect(host);
    Point anchor = HwndMapWindowPoint(host, nullptr, {rect.Right(), rect.Bottom()});
    UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_TOPALIGN;
    int chosen = TrackPopupMenuEx(menu, flags, anchor.x, anchor.y, host, nullptr);
    DestroyMenu(menu);
    if (!IsWindow(host) || (TabsCtrl*)GetWindowLongPtrW(host, GWLP_USERDATA) != this) {
        return;
    }
    tabListPopup = nullptr;
    if (chosen <= 0 || chosen > len(popup.tabs)) {
        return;
    }
    int index = VecFind(tabs, popup.tabs[chosen - 1]);
    if (index < 0 || index == selectedIdx || TriggerSelectionChanging(this)) {
        return;
    }
    SetSelected(index);
    TriggerSelectionChanged(this);
}

static bool DrawTabListItem(TabListPopup* popup, DRAWITEMSTRUCT* item) {
    if (!popup || item->CtlType != ODT_MENU || item->itemData >= (ULONG_PTR)len(popup->titles)) {
        return false;
    }
    int index = (int)item->itemData;
    bool hot = (item->itemState & ODS_SELECTED) != 0;
    Color bg = hot ? popup->selectedBackground : popup->background;
    if (hot) {
        bg = AccentColor(bg, 18);
    }
    Color text = TabTextColorForBackground(popup->textColor, bg);
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    HdcFillRect(dc, ToRect(rect), bg);
    HGDIOBJ oldFont = SelectObject(dc, popup->font->GetHFont());
    int oldMode = SetBkMode(dc, TRANSPARENT);
    COLORREF oldColor = SetTextColor(dc, text);
    RECT glyph = rect;
    glyph.right = glyph.left + popup->rowDy;
    if (popup->isRtl) {
        glyph.left = rect.right - popup->rowDy;
        glyph.right = rect.right;
    }
    if (index == popup->selected) {
        SetTextColor(dc, IsLightColor(bg) ? RGB(0, 112, 64) : RGB(109, 233, 165));
        DrawTextW(dc, L"\u2713", 1, &glyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        SetTextColor(dc, text);
    }
    if (popup->isRtl) {
        rect.left += popup->padding;
        rect.right = glyph.left;
    } else {
        rect.left = glyph.right;
        rect.right -= popup->padding;
    }
    WStr title = ToWStrTemp(popup->titles[index]);
    UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
    format |= popup->isRtl ? DT_RIGHT | DT_RTLREADING : DT_LEFT;
    DrawTextW(dc, title.s, len(title), &rect, format);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    SelectObject(dc, oldFont);
    return true;
}

static void TriggerTabMigration(TabsCtrl* tabs, int tabIdx, Point p) {
    if (!tabs->onTabMigration.IsValid()) {
        return;
    }
    TabsCtrl::MigrationEvent ev;
    ev.tabs = tabs;
    ev.tabIdx = tabIdx;
    ev.releasePoint = p;
    tabs->onTabMigration.Call(&ev);
}

static void TriggerTabClosed(TabsCtrl* tabs, int tabIdx) {
    if ((tabIdx < 0) || !tabs->onTabClosed.IsValid()) {
        return;
    }
    TabsCtrl::ClosedEvent ev;
    ev.tabs = tabs;
    ev.tabIdx = tabIdx;
    tabs->onTabClosed.Call(&ev);
}

static void TriggerTabDragged(TabsCtrl* tabs, int tab1, int tab2) {
    if (!tabs->onTabDragged.IsValid()) {
        return;
    }
    TabsCtrl::DraggedEvent ev;
    ev.tabs = tabs;
    ev.tab1 = tab1;
    ev.tab2 = tab2;
    tabs->onTabDragged.Call(&ev);
}

static void UpdateAfterDrag(TabsCtrl* tabsCtrl, int tabIdxFrom, int tabIdxTo) {
    int nTabs = tabsCtrl->TabCount();
    bool badState =
        (tabIdxFrom == tabIdxTo) || (tabIdxFrom < 0) || (tabIdxTo < 0) || (tabIdxFrom >= nTabs) || (tabIdxTo > nTabs);
    if (badState) {
        logf("tabIdxFrom: %d, tabIdxTo: %d, nTabs: %d\n", tabIdxFrom, tabIdxTo, nTabs);
        ReportDebugIf(true);
        return;
    }

    auto&& tabs = tabsCtrl->tabs;
    TabInfo* moved = tabs[tabIdxFrom];
    VecRemoveAt(tabs, tabIdxFrom);
    if (tabIdxFrom < tabIdxTo) {
        // we moved from left to right e.g. from 1 to 3
        // after removing 1 we insert not at 3 but 2
        tabIdxTo -= 1;
    }
    VecInsertAt(tabs, tabIdxTo, moved);
    tabsCtrl->RebuildTabCtrls();
    tabsCtrl->SetSelected(tabIdxTo);
    tabsCtrl->LayoutTabs();
    TabsCtrlUpdateAfterChangingTabsCount(tabsCtrl);
}

// clicking a tab selects it (and arms a possible drag)
void TabsCtrl::OnTabMouseDown(TabCtrl* tab, VirtMouseEvent& ev) {
    int idx = tab->Idx();
    UpdateHover(idx);
    if (idx != selectedIdx) {
        if (TriggerSelectionChanging(this)) {
            return;
        }
        SetSelected(idx);
        TriggerSelectionChanged(this);
        // LoadModelIntoTab() can pump messages; ensure tabs are fully painted.
        HwndRepaintNow(hwnd);
    }
    // SetSelected/TriggerSelectionChanged above can pump messages
    // (LoadModelIntoTab), which may remove tabs and leave idx stale
    TabInfo* ti = GetTab(idx);
    if (!ti || ti->isPinned) {
        return;
    }
    Rect r = tab->BoundsInWindow();
    grabLocation.x = ev.ptWindow.x - r.x;
    grabLocation.y = ev.ptWindow.y - r.y;
    SetCapture(hwnd);
}

void TabsCtrl::CloseTab(int idx) {
    if (!IsValidIdx(idx)) {
        return;
    }
    // freeze tab widths so next close button stays under cursor;
    // unfreezes when mouse leaves the tab control
    frozenTabDx = tabSize.dx;
    tabWidthFrozen = true;
    TriggerTabClosed(this, idx);
    // TriggerTabClosed() might have destroyed the window and this TabsCtrl
    if (!FindMainWindowByHwnd(hwnd)) {
        return;
    }
    ScheduleRepaint();
    tabBeingClosed = -1;
}

static bool CanDragTab(TabInfo* tab) {
    if (tab->isPinned) {
        return false;
    }
    return true;
}

void TabsCtrl::Paint(VirtPaintCtx&) {
    // children paint themselves; host WM_PAINT fills the background
}

LRESULT TabsCtrl::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEACTIVATE) {
        HWND frame = GetAncestor(hwnd, GA_ROOT);
        if (frame && GetForegroundWindow() == frame) {
            return MA_NOACTIVATE;
        }
    }
    Point mousePos = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (WM_MOUSELEAVE == msg) {
        mousePos = HwndGetCursorPos(hwnd);
    }

    if (hasOverflow && (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL)) {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        ScrollTabs(msg == WM_MOUSEHWHEEL ? (delta > 0 ? 1 : -1) : (delta > 0 ? -1 : 1));
        return 0;
    }
    if (hasOverflow && mousePos.x >= viewportDx && msg == WM_LBUTTONDOWN) {
        int button = (mousePos.x - viewportDx) / scrollButtonDx;
        if (button >= 2) {
            ShowTabList();
        } else {
            ScrollTabs(button == 0 ? -1 : 1);
        }
        return 0;
    }
    if (hasOverflow && mousePos.x >= viewportDx && msg == WM_LBUTTONUP) {
        return 0;
    }

    TabsCtrl::MouseState tabState;

    bool overClose = false;
    bool canClose = true;
    int tabUnderMouse = -1;

    if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg == WM_MOUSELEAVE)) {
        tabState = TabStateFromMousePosition(mousePos);
        tabUnderMouse = tabState.tabIdx;
        canClose = tabState.tabInfo && tabState.tabInfo->canClose;
        overClose = tabState.overClose && canClose;
        lastMousePos = mousePos;
    }

    if (draggingTab && msg == WM_MOUSEMOVE) {
        Point p = HwndMapWindowPoint(hwnd, nullptr, mousePos);
        ImageList_DragMove(p.x, p.y);
        return 0;
    }

    // Check if mouse has moved beyond system drag threshold
    bool beyondDragThreshold = false;
    if (msg == WM_MOUSEMOVE && GetCapture() == hwnd && !draggingTab) {
        TabCtrl* hlCtrl = TabCtrlAt(tabHighlighted);
        if (hlCtrl) {
            int cxDrag = GetSystemMetrics(SM_CXDRAG);
            int cyDrag = GetSystemMetrics(SM_CYDRAG);
            Rect r = hlCtrl->BoundsInWindow();
            beyondDragThreshold =
                (abs(mousePos.x - grabLocation.x - r.x) > cxDrag) || (abs(mousePos.y - grabLocation.y - r.y) > cyDrag);
        }
    }

    switch (msg) {
        case WM_MEASUREITEM: {
            auto item = (MEASUREITEMSTRUCT*)lp;
            if (tabListPopup && item->CtlType == ODT_MENU) {
                item->itemWidth = tabListPopup->width;
                item->itemHeight = tabListPopup->rowDy;
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
            if (DrawTabListItem(tabListPopup, (DRAWITEMSTRUCT*)lp)) {
                return TRUE;
            }
            break;
        case WM_NCHITTEST: {
            // parts that are HTTRANSPARENT are used to move the window
            if (!inTitleBar || hwnd == GetCapture()) {
                return HTCLIENT;
            }
            mousePos = HwndScreenToClient(hwnd, mousePos);
            tabState = TabStateFromMousePosition(mousePos);
            if (tabState.tabIdx >= 0 || (hasOverflow && mousePos.x >= viewportDx)) {
                return HTCLIENT;
            }
            return HTTRANSPARENT;
        }

        case WM_SIZE:
            LayoutTabs();
            break;

        case WM_MOUSELEAVE: {
            if (tabWidthFrozen) {
                tabWidthFrozen = false;
                LayoutTabs();
            }
            LRESULT res = 0;
            if (vroot) {
                vroot->OnMessage(msg, wp, lp, res);
            }
            tabHighlightedClose = -1;
            UpdateHover(-1);
            break;
        }

        case WM_MOUSEMOVE: {
            TrackMouseLeave(hwnd);
            bool isDragging = (GetCapture() == hwnd);
            int hl = tabHighlighted;
            if (isDragging && beyondDragThreshold) {
                if (hl < 0) {
                    return 0;
                }
                // move the tab out: draw it as a image and drag around the screen
                draggingTab = true;
                TabCtrl* hlCtrl = TabCtrlAt(hl);
                HBITMAP hbmp = RenderForDragging(hl);
                if (!hbmp || !hlCtrl) {
                    logf("TabsCtrl::WndProc: RenderForDragging failed for tab %d\n", hl);
                    return 0;
                }
                Rect r = hlCtrl->BoundsInWindow();
                HIMAGELIST himl = ImageList_Create(r.dx, r.dy, 0, 1, 0);
                ImageList_Add(himl, hbmp, nullptr);
                ImageList_BeginDrag(himl, 0, grabLocation.x, grabLocation.y);
                DeleteObject(hbmp);
                DeleteObject(himl);
                Point p = HwndMapWindowPoint(hwnd, nullptr, mousePos);
                ImageList_DragEnter(nullptr, p.x, p.y);
                return 0;
            }

            LRESULT res = 0;
            if (vroot) {
                vroot->OnMessage(msg, wp, lp, res);
            }

            if (hl != tabUnderMouse) {
                // note: hl == -1 possible repro: we start drag, a file gets loaded via DDE etc.
                // which re-layouts tabs and mouse is no longer over a tab
                if (isDragging && hl != -1 && tabUnderMouse != -1) {
                    // send notification if the highlighted tab is dragged over another
                    if (!CanDragTab(GetTab(tabUnderMouse))) {
                        TriggerTabDragged(this, hl, tabUnderMouse);
                        UpdateAfterDrag(this, hl, tabUnderMouse);
                        return 0;
                    }
                }
                UpdateHover(tabUnderMouse);
                return 0;
            }
            int xHl = -1;
            if (overClose && !isDragging) {
                xHl = hl;
            }
            if (tabHighlightedClose != xHl) {
                tabHighlightedClose = xHl;
                ScheduleRepaint();
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (overClose) {
                tabBeingClosed = tabUnderMouse;
            }
            LRESULT res = 0;
            if (vroot) {
                vroot->OnMessage(msg, wp, lp, res);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            bool isDragging = (GetCapture() == hwnd);
            if (isDragging) {
                ReleaseCapture();
            }
            if (tabBeingClosed != -1 && tabUnderMouse == tabBeingClosed && overClose) {
                // the ✕ is a control of its own: it runs CloseTab() from here
                LRESULT res = 0;
                if (vroot) {
                    vroot->OnMessage(msg, wp, lp, res);
                }
                return 0;
            }
            // we don't always get WM_MOUSEMOVE before WM_LBUTTONUP so
            // update the hover state
            UpdateHover(tabUnderMouse);

            if (!draggingTab) {
                LRESULT res = 0;
                if (vroot) {
                    vroot->OnMessage(msg, wp, lp, res);
                }
                return 0;
            }
            draggingTab = false;
            ImageList_EndDrag();
            int selectedTab = selectedIdx;
            if (tabUnderMouse < 0) {
                // migrate to new/different window
                Point scPoint = HwndClientToScreen(hwnd, mousePos);
                TriggerTabMigration(this, selectedTab, scPoint);
                return 0;
            }
            int dstIdx = tabUnderMouse;
            if (tabState.inRightHalf) {
                dstIdx++;
            }
            if (dstIdx == selectedTab) {
                return 0;
            }
            if ((dstIdx < TabCount()) && GetTab(dstIdx)->isPinned) {
                return 0;
            }
            TriggerTabDragged(this, selectedTab, dstIdx);
            UpdateAfterDrag(this, selectedTab, dstIdx);
            ScheduleRepaint();
            return 0;
        }

        case WM_MBUTTONDOWN: {
            // middle-clicking unconditionally closes the tab
            tabBeingClosed = tabUnderMouse;
            if (tabBeingClosed < 0 || !canClose) {
                return 0;
            }
            CloseTab(tabBeingClosed);
            return 0;
        }

        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            LRESULT res = 0;
            if (VirtTreeOnMessage(hwnd, vroot, msg, wp, lp, res)) {
                return res;
            }
            break;
        }

        case WM_SETCURSOR: {
            LRESULT res = 0;
            if (VirtTreeOnMessage(hwnd, vroot, msg, wp, lp, res)) {
                return res;
            }
            break;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            // BeginPaint / EndPaint only to consume the update region: ValidateRect
            // doesn't clear it on a hidden control, so WM_PAINT was regenerated
            // forever (fullscreen hides the tab bar), starving the low-priority
            // WM_TIMER that drives smooth wheel scrolling (issue #5865).
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            defer {
                EndPaint(hwnd, &ps);
            };
            if (!IsWindowVisible(hwnd)) {
                return 0;
            }
            Rect clientRc = HwndClientRect(hwnd);
            if (clientRc.IsEmpty()) {
                return 0;
            }
            HDC hdc = GetDC(hwnd);
            Color bgCol = GetColor(kColTabBg);
            if (vroot) {
                Rect tabClip = clientRc;
                if (hasOverflow) {
                    tabClip.dx = viewportDx;
                }
                PaintVirtTree(vroot, hdc, tabClip, bgCol);
            } else {
                // no tabs: nothing but the background
                HdcFillRect(hdc, clientRc, bgCol);
            }
            if (hasOverflow) {
                Rect buttons{viewportDx, 0, clientRc.dx - viewportDx, clientRc.dy};
                HdcFillRect(hdc, buttons, bgCol);
                PlatformFont* arrowFont = GetUserGuiFont(GetFont()->GetName(), tabIconDx);
                HGDIOBJ oldFont = SelectObject(hdc, arrowFont->GetHFont());
                int oldMode = SetBkMode(hdc, TRANSPARENT);
                Color oldColor = SetTextColor(hdc, GetColor(kColTabText));
                RECT left{viewportDx, 0, viewportDx + scrollButtonDx, clientRc.dy};
                RECT right{left.right, 0, left.right + scrollButtonDx, clientRc.dy};
                RECT list{right.right, 0, clientRc.dx, clientRc.dy};
                DrawTextW(hdc, L"\u2039", 1, &left, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                DrawTextW(hdc, L"\u203a", 1, &right, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                DrawTextW(hdc, L"\u2304", 1, &list, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SetTextColor(hdc, oldColor);
                SetBkMode(hdc, oldMode);
                SelectObject(hdc, oldFont);
            }
            ReleaseDC(hwnd, hdc);
            return 0;
        }

        case WM_NCDESTROY:
            // HWND is going away; detach without DestroyWindow
            if (this->hwnd == hwnd) {
                this->hwnd = nullptr;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND TabsCtrl::Create(TabsCtrl::CreateArgs& args) {
    RegisterTabsCtrlClass();
    withToolTips = args.withToolTips;
    tabDefaultDx = args.tabDefaultDx;
    font = args.font;
    ctrlID = args.ctrlID;

    DWORD style = WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE;
    DWORD exStyle = 0;
    if (args.isRtl) {
        exStyle |= WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT;
    }

    HWND created = CreateWindowExW(exStyle, kTabsCtrlClassName.s, L"", style, 0, 0, 0, 0, args.parent,
                                   (HMENU)(INT_PTR)ctrlID, GetInstance(), this);
    ReportIf(!created || created != hwnd);
    if (!hwnd) {
        return nullptr;
    }
    if (font) {
        HwndSetFont(hwnd, font->GetHFont());
    }

    vroot = new VirtRoot(hwnd);
    // non-owning top: MainWindow owns TabsCtrl, TabsCtrl owns vroot
    Vec<VirtCtrl*> tops;
    VecAppend(tops, this);
    vroot->SetTops(tops);
    return hwnd;
}

Size TabsCtrl::GetIdealSize() {
    Size sz{32, 128};
    return sz;
}

// takes ownership of tab
int TabsCtrl::InsertTab(int idx, TabInfo* tab, bool update) {
    ReportIf(idx < 0);
    VecInsertAt(tabs, idx, tab);
    RebuildTabCtrls();
    if (update) {
        // LayoutTabs() must be before SetSelected() because SetSelected()
        // triggers sync repaint which paints tab texts in wrong positions
        // because we didn't position them yet in layout.
        LayoutTabs();
        SetSelected(idx);
        TabsCtrlUpdateAfterChangingTabsCount(this);
    }
    return idx;
}

void TabsCtrl::SetPageText(int idx, Str page) {
    TabInfo* tab = GetTab(idx);
    if (!tab) {
        return;
    }
    if (tab->pageText && page && str::Eq(tab->pageText, page)) {
        return;
    }
    str::ReplaceWithCopy(&tab->pageText, page);
    ScheduleRepaint();
}

void TabsCtrl::SetTextAndTooltip(int idx, Str text, Str tooltip2) {
    TabInfo* tab = GetTab(idx);
    if (!tab) {
        return;
    }
    str::ReplaceWithCopy(&tab->text, text);
    str::ReplaceWithCopy(&tab->tooltip, tooltip2);
    LayoutTabs();
    // Immediate paint so F2 rename / path changes show without waiting for
    // hover or the next idle paint (tabs-in-titlebar caption especially).
    HwndRepaintNow(hwnd);
}

void TabsCtrl::SetTabDirty(int idx, bool dirty) {
    TabInfo* tab = GetTab(idx);
    if (tab && tab->isDirty != dirty) {
        tab->isDirty = dirty;
        LayoutTabs();
        // LayoutTabs only schedules a repaint; force it so the dirty (red dot)
        // indicator updates immediately (e.g. right after editing a form field)
        HwndRepaintNow(hwnd);
    }
}

// returns userData because it's not owned by TabsCtrl
UINT_PTR TabsCtrl::RemoveTab(int idx) {
    // GetTabIdx's "not found" is -1; a nested DDE CloseAllTabs / CloseWindow can
    // remove the tab before the outer CloseTab resumes. Do not index tabs[-1].
    if (idx < 0 || idx >= TabCount()) {
        if (idx < -1) {
            ReportIf(true);
        } else {
            logf("TabsCtrl::RemoveTab: out-of-range idx=%d (tabs=%d)\n", idx, TabCount());
        }
        return 0;
    }
    int selectedTab = selectedIdx;
    TabInfo* tab = tabs[idx];
    UINT_PTR userData = tab->userData;
    VecRemoveAt(tabs, idx);
    delete tab;
    RebuildTabCtrls();
    if (TabCount() > 0 && selectedTab >= 0) {
        if (idx < selectedTab) {
            selectedTab--;
        } else if (idx == selectedTab) {
            selectedTab = 0;
        }
        SetSelected(selectedTab);
    } else {
        selectedIdx = -1;
    }
    LayoutTabs();
    TabsCtrlUpdateAfterChangingTabsCount(this);
    return userData;
}

void TabsCtrl::SwapTabs(int idx1, int idx2) {
    TabInfo* tmp = tabs[idx1];
    tabs[idx1] = tabs[idx2];
    tabs[idx2] = tmp;
    RebuildTabCtrls();
}

// Note: the caller should take care of deleting userData
void TabsCtrl::RemoveAllTabs() {
    DeleteVecMembers(tabs);
    VecReset(tabs);
    selectedIdx = -1;
    RebuildTabCtrls();
    LayoutTabs();
    TabsCtrlUpdateAfterChangingTabsCount(this);
}

TabInfo* TabsCtrl::GetTab(int idx) {
    // This is the fail-safe accessor for tab indices that legitimately go out
    // of range: -1 ("no tab") and a stale index that can briefly occur during
    // teardown / DDE-triggered reload. Both are expected, so just bail to
    // nullptr (callers must null-check).
    if (idx < 0 || idx >= len(tabs)) {
        if (idx < -1) {
            // no sentinel ever produces an index below -1: treat as corruption
            ReportIf(true);
        } else if (idx != -1) {
            // idx >= tabs.Size() (idx >= 0): rare - a stale index during teardown
            // or a caller off-by-one. Log a breadcrumb so genuine caller bugs stay
            // diagnosable, without uploading a debug report. -1 ("no tab" sentinel)
            // is ubiquitous, so stay silent for it.
            logf("TabsCtrl::GetTab: out-of-range idx=%d (tabs=%d)\n", idx, len(tabs));
        }
        return nullptr;
    }
    return tabs[idx];
}

int TabsCtrl::GetSelected() {
    return selectedIdx;
}

int TabsCtrl::SetSelected(int idx) {
    int nTabs = TabCount();
    if (idx < 0 || idx >= nTabs) {
        logf("TabsCtrl::SetSelected(): idx: %d, TabsCount(): %d\n", idx, nTabs);
    }
    ReportIf(idx < 0 || idx >= nTabs);
    int prevSelectedIdx = selectedIdx;
    selectedIdx = idx;
    LayoutTabs();
    if (hasOverflow && IsValidIdx(idx)) {
        int visualIndex = IsTabsRtl(hwnd) ? TabCount() - 1 - idx : idx;
        int left = visualIndex * tabSize.dx;
        int right = left + tabSize.dx;
        if (left < scrollDx) {
            scrollDx = left;
        } else if (right > scrollDx + viewportDx) {
            scrollDx = right - viewportDx;
        }
        LayoutTabs();
    }
    UpdateHover(tabHighlighted);
    HwndRepaintNow(hwnd);
    return prevSelectedIdx;
}

void TabsCtrl::SetHighlighted(int idx) {
    int oldSelectedIdx = selectedIdx;
    if (IsValidIdx(tabForceShowSelected)) {
        oldSelectedIdx = tabForceShowSelected;
    }
    int newSelectedIdx = selectedIdx;
    if (IsValidIdx(idx)) {
        newSelectedIdx = idx;
    }
    if (tabForceShowSelected == idx) {
        return;
    }
    tabForceShowSelected = idx;
    if (oldSelectedIdx == newSelectedIdx) {
        return;
    }
    UpdateHover(tabHighlighted);
    HwndRepaintNow(hwnd);
}

#if IS_DEBUG
void TabsCtrl_UnitTests() {
    Rect left = TabSeparatorRect({10, 20, 100, 40}, 1, false);
    Rect right = TabSeparatorRect({10, 20, 100, 40}, 1, true);
    utassert(left.x == 109 && right.x == 10 && left.dx == 1 && right.dx == 1);
    utassert(left.y == 30 && left.dy == 20 && right.y == left.y && right.dy == left.dy);

    HWND parent =
        CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 800, 80, nullptr, nullptr, GetInstance(), nullptr);
    utassert(parent != nullptr);
    TabsCtrl tc;
    TabsCtrl::CreateArgs args;
    args.parent = parent;
    args.font = GetUserGuiFont(StrL("Segoe UI"), 17);
    utassert(tc.Create(args) != nullptr);
    SetWindowPos(tc.hwnd, nullptr, 0, 0, 800, 40, SWP_NOZORDER | SWP_NOACTIVATE);
    for (int i = 0; i < 25; i++) {
        auto tab = new TabInfo();
        tab->text = str::Dup(fmt("Document %d.pdf", i + 1));
        tc.InsertTab(i, tab, false);
    }
    tc.LayoutTabs();
    utassert(tc.hasOverflow);
    utassert(tc.viewportDx == 800 - tc.scrollButtonDx * 3);
    utassert(tc.TabStateFromMousePosition({tc.viewportDx, 10}).tabIdx == -1);
    tc.SetSelected(24);
    utassert(tc.scrollDx + tc.viewportDx >= tc.tabSize.dx * tc.TabCount());
    tc.tabListVisibleItems = 10;
    TabListPopup popup;
    HMENU menu = BuildTabListMenu(&tc, popup);
    utassert(menu != nullptr && GetMenuItemCount(menu) == 25);
    utassert(len(popup.titles) == 25 && len(popup.tabs) == 25);
    utassert(popup.selected == 24 && str::Eq(popup.titles[24], StrL("Document 25.pdf")));
    MENUINFO info{sizeof(info)};
    info.fMask = MIM_MAXHEIGHT;
    utassert(GetMenuInfo(menu, &info));
    utassert(info.cyMax <= (UINT)(popup.rowDy * 10 + GetSystemMetrics(SM_CYBORDER) * 2));
    WCHAR title[64]{};
    MENUITEMINFOW titleInfo{sizeof(titleInfo)};
    titleInfo.fMask = MIIM_STRING;
    titleInfo.dwTypeData = title;
    titleInfo.cch = dimof(title);
    utassert(GetMenuItemInfoW(menu, 24, TRUE, &titleInfo));
    utassert(titleInfo.cch > 0);
    utassert(str::Eq(ToUtf8Temp(WStr(title)), StrL("Document 25.pdf")));
    DestroyMenu(menu);

    HDC dc = GetDC(parent);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, 320, popup.rowDy);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    DRAWITEMSTRUCT item{};
    item.CtlType = ODT_MENU;
    item.itemData = 24;
    item.hDC = memory;
    item.rcItem = {0, 0, 320, popup.rowDy};
    utassert(DrawTabListItem(&popup, &item));
    int green = 0;
    for (int y = 0; y < popup.rowDy; y++) {
        for (int x = 0; x < popup.rowDy; x++) {
            COLORREF color = GetPixel(memory, x, y);
            if (GetGValue(color) > GetRValue(color) + 20 && GetGValue(color) > GetBValue(color) + 20) {
                green++;
            }
        }
    }
    utassert(green > 0);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(parent, dc);
    tc.Destroy();
    DestroyWindow(parent);
}
#endif
