/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include <commctrl.h>
#include <richedit.h>
#include "gui/Dpi.h"
#include "base/Win.h"

#include "Settings.h"
#include "AppSettings.h"
#include "Theme.h"
#include "OverlayScrollbar.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif

constexpr const WCHAR* kOverlayScrollbarClass = L"SUMATRA_OVERLAY_SCROLLBAR";

static bool gScrollbarClassRegistered = false;

bool gOverlayScrollbarSuppressThick = false;
// if true, draw small filled triangles instead of chevrons
static bool gThickArrows = true;

// all live overlay scrollbars, for global mouse tracking
static Vec<OverlayScrollbar*> gAllScrollbars;
static UINT_PTR gMouseTrackTimer = 0;
static Point gLastMousePos = {-1, -1};
static constexpr UINT_PTR kMouseTrackTimerID = 100;
static constexpr int kMouseTrackIntervalMs = 50;
static constexpr UINT_PTR kNativeScrollbarSubclass = 0x736272;
static constexpr UINT_PTR kNativeScrollbarTimer = 0x736273;
static constexpr WCHAR kNativeScrollbarProperty[] = L"SumatraAppScrollbar";
static constexpr WCHAR kNativeHScrollbarProperty[] = L"SumatraAppHScrollbar";
static void SyncNativeScrollbar(OverlayScrollbar* sb, bool force = false);

static Rect NativeScrollbarClip(HWND hwnd) {
    Rect clip = HwndWindowRect(hwnd);
    HWND parent = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) ? GetParent(hwnd) : nullptr;
    while (parent) {
        clip = clip.Intersect(HwndMapRectToWindow(HwndClientRect(parent), parent, nullptr));
        if (!(GetWindowLongPtrW(parent, GWL_STYLE) & WS_CHILD)) break;
        parent = GetParent(parent);
    }
    return clip;
}

// Derive scrollbar colors from current theme
static Color ThemeTrackColor() {
    if (ThemeUsesHighContrastColors()) return GetSysColor(COLOR_SCROLLBAR);
    Color bg = ThemeControlBackgroundColor();
    return bg;
}

static Color ThemeThumbColor() {
    if (ThemeUsesHighContrastColors()) return GetSysColor(COLOR_BTNTEXT);
    return MkRgb(139, 139, 139);
}

static Color ThemeThumbHoverColor() {
    if (ThemeUsesHighContrastColors()) return GetSysColor(COLOR_HIGHLIGHT);
    return MkRgb(105, 105, 105);
}

static constexpr int kMinThumbSize = 20;

using State = OverlayScrollbar::State;

static bool IsThick(OverlayScrollbar* sb) {
    return sb->state == State::SmartThick || sb->state == State::AlwaysThick;
}

static bool IsVisible(OverlayScrollbar* sb) {
    return sb->state == State::SmartThin || sb->state == State::SmartThick || sb->state == State::AlwaysThick;
}

// scrollbar is active: shown or auto-hidden but ready to appear
static bool IsActive(OverlayScrollbar* sb) {
    return sb->state != State::Hidden;
}

static int ScaledWidth(OverlayScrollbar* sb) {
    sb->thickWidth = GetAppScrollbarWidth(DpiGetForHwnd(sb->hwndOwner));
    sb->thinWidth = sb->thickWidth;
    return sb->thickWidth;
}

static bool IsVert(OverlayScrollbar* sb) {
    return sb->type == OverlayScrollbar::Type::Vert;
}

static Rect GetBarRect(OverlayScrollbar* sb) {
    Rect rc = HwndClientRect(sb->hwnd);
    if (sb->nativeAdapter) {
        int width = std::min(IsVert(sb) ? rc.dx : rc.dy, GetAppScrollbarWidth(DpiGetForHwnd(sb->hwndOwner)));
        if (IsVert(sb)) {
            rc.x += (rc.dx - width) / 2;
            rc.dx = width;
        } else {
            rc.y += (rc.dy - width) / 2;
            rc.dy = width;
        }
    }
    return rc;
}

// Get the track rect in client coords of the scrollbar window
static Rect GetTrackRect(OverlayScrollbar* sb) {
    Rect rc = GetBarRect(sb);
    int arrowSize = 0;
    int gap = 0;
    if (IsVisible(sb)) {
        arrowSize = IsVert(sb) ? rc.dx : rc.dy;
        gap = UiScalePxForDpi(DpiGetForHwnd(sb->hwndOwner), 2);
    }
    int total = std::min(arrowSize + gap, (IsVert(sb) ? rc.dy : rc.dx) / 2);
    if (IsVert(sb)) {
        return {rc.x, rc.y + total, rc.dx, rc.dy - (2 * total)};
    }
    return {rc.x + total, rc.y, rc.dx - (2 * total), rc.dy};
}

// Calculate thumb rect within the track
static Rect GetThumbRect(OverlayScrollbar* sb) {
    Rect track = GetTrackRect(sb);
    int range = sb->nMax - sb->nMin + 1;
    if (range <= 0 || (int)sb->nPage >= range) {
        return track;
    }

    int trackLen = IsVert(sb) ? track.dy : track.dx;
    int thumbLen = MulDiv(trackLen, (int)sb->nPage, range);
    thumbLen =
        setMinMax(thumbLen, std::min(trackLen, UiScalePxForDpi(DpiGetForHwnd(sb->hwndOwner), kMinThumbSize)), trackLen);

    int scrollableTrack = trackLen - thumbLen;
    int scrollableRange = range - (int)sb->nPage;
    int pos = sb->isDragging ? sb->nTrackPos : sb->nPos;
    int thumbOffset = 0;
    if (scrollableRange > 0) {
        thumbOffset = MulDiv(pos - sb->nMin, scrollableTrack, scrollableRange);
    }
    thumbOffset = setMinMax(thumbOffset, 0, scrollableTrack);

    if (IsVert(sb)) {
        return {track.x, track.y + thumbOffset, track.dx, thumbLen};
    }
    return {track.x + thumbOffset, track.y, thumbLen, track.dy};
}

static Rect GetArrowTopRect(OverlayScrollbar* sb) {
    Rect rc = GetBarRect(sb);
    int arrowSize = std::min(IsVert(sb) ? rc.dx : rc.dy, (IsVert(sb) ? rc.dy : rc.dx) / 2);
    if (IsVert(sb)) {
        return {rc.x, rc.y, rc.dx, arrowSize};
    }
    return {rc.x, rc.y, arrowSize, rc.dy};
}

static Rect GetArrowBottomRect(OverlayScrollbar* sb) {
    Rect rc = GetBarRect(sb);
    int arrowSize = std::min(IsVert(sb) ? rc.dx : rc.dy, (IsVert(sb) ? rc.dy : rc.dx) / 2);
    if (IsVert(sb)) {
        return {rc.x, rc.Bottom() - arrowSize, rc.dx, arrowSize};
    }
    return {rc.Right() - arrowSize, rc.y, arrowSize, rc.dy};
}

static void ScrollRichEditTo(HWND hwnd, int target) {
    SCROLLINFO info{sizeof(info), SIF_ALL};
    GetScrollInfo(hwnd, SB_VERT, &info);
    // Rich Edit lays out long documents lazily, so nMax can still describe
    // only the first part of the text. EM_LINESCROLL clamps at the last line.
    target = std::max(info.nMin, target);
    int current = (int)SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    int count = (int)SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0);
    int low = 0, high = std::max(0, count - 1);
    // EM_SETSCROLLPOS truncates coordinates to 16 bits. Locate the actual
    // wrapped line using 32-bit character positions, including mixed heights.
    while (low < high) {
        int middle = low + (high - low + 1) / 2;
        LRESULT character = SendMessageW(hwnd, EM_LINEINDEX, middle, 0);
        POINT pos{};
        SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&pos, character);
        if (pos.y + info.nPos <= target)
            low = middle;
        else
            high = middle - 1;
    }
    SendMessageW(hwnd, EM_LINESCROLL, 0, low - current);
}

static void SendScrollMsg(OverlayScrollbar* sb, UINT scrollMsg, WPARAM wp) {
    if (sb->nativeAdapter) {
        sb->sendingNative = true;
        defer {
            sb->sendingNative = false;
        };
        WCHAR klass[32]{};
        GetClassNameW(sb->hwndOwner, klass, dimofi(klass));
        bool track = LOWORD(wp) == SB_THUMBTRACK || LOWORD(wp) == SB_THUMBPOSITION;
        if (track && IsVert(sb) && _wcsicmp(klass, L"RICHEDIT50W") == 0) {
            ScrollRichEditTo(sb->hwndOwner, sb->nTrackPos);
        } else if (track && IsVert(sb) && _wcsicmp(klass, L"EDIT") == 0) {
            int first = (int)SendMessageW(sb->hwndOwner, EM_GETFIRSTVISIBLELINE, 0, 0);
            SendMessageW(sb->hwndOwner, EM_LINESCROLL, 0, sb->nTrackPos - first);
        } else if (track && IsVert(sb) && _wcsicmp(klass, L"LISTBOX") == 0) {
            SendMessageW(sb->hwndOwner, LB_SETTOPINDEX, sb->nTrackPos, 0);
        } else {
            SendMessageW(sb->hwndOwner, scrollMsg, wp, 0);
        }
        SyncNativeScrollbar(sb);
        return;
    }
    SendMessageW(sb->hwndOwner, scrollMsg, wp, 0);
}

static UINT ScrollMsgForType(OverlayScrollbar* sb) {
    return IsVert(sb) ? WM_VSCROLL : WM_HSCROLL;
}

// The band the scrollbar occupies when thick, in screen coordinates. The mouse
// being in it is what turns a smart scrollbar thick, so it is always the thick
// width, whatever width the scrollbar is drawn at right now
static Rect GetScrollbarScreenRect(OverlayScrollbar* sb) {
    Rect ownerRc = HwndWindowRect(sb->hwndOwner);
    if (sb->nativeAdapter) {
        ownerRc = HwndMapRectToWindow(HwndClientRect(sb->hwndOwner), sb->hwndOwner, nullptr);
        if (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & WS_VSCROLL) {
            ownerRc.dx += DpiGetSystemMetrics(SM_CXVSCROLL, DpiGetForHwnd(sb->hwndOwner));
        }
        if (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & WS_HSCROLL) {
            ownerRc.dy += DpiGetSystemMetrics(SM_CYHSCROLL, DpiGetForHwnd(sb->hwndOwner));
        }
    }
    int scrollW = ScaledWidth(sb);
    if (IsVert(sb)) {
        return {ownerRc.x + ownerRc.dx - scrollW, ownerRc.y, scrollW, ownerRc.dy};
    }
    return {ownerRc.x, ownerRc.y + ownerRc.dy - scrollW, ownerRc.dx, scrollW};
}

// Check if hwnd is the same as or an ancestor of child
static bool IsOrIsParentOf(HWND hwnd, HWND child) {
    while (child) {
        if (child == hwnd) {
            return true;
        }
        child = GetParent(child);
    }
    return false;
}

// Sit just above the owner frame, so the bar is seen but doesn't cover the
// windows floating over the canvas (the annotation property row, the command
// palette). SetWindowPos inserts *after* the window it is given, so that is
// the one directly above the frame, not the frame itself (issue #6093).
static HWND ScrollbarZOrderAfter(OverlayScrollbar* sb) {
    HWND root = sb->hwndOwner ? GetAncestor(sb->hwndOwner, GA_ROOT) : nullptr;
    if (!root) {
        return HWND_TOP;
    }
    HWND above = GetWindow(root, GW_HWNDPREV);
    // skip ourselves: we are what is directly above the frame once shown
    while (above == sb->hwnd) {
        above = GetWindow(above, GW_HWNDPREV);
    }
    return above ? above : HWND_TOP;
}

static void ShowScrollbarHwnd(OverlayScrollbar* sb) {
    SetWindowPos(sb->hwnd, ScrollbarZOrderAfter(sb), 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
}

// Cursor is over the owner canvas, not over another top-level window covering it
static bool MouseOverOwnerSurface(OverlayScrollbar* sb, Point pt) {
    Rect ownerRc = HwndWindowRect(sb->hwndOwner);
    if (!ownerRc.Contains(pt)) {
        return false;
    }
    POINT p{pt.x, pt.y};
    HWND hit = WindowFromPoint(p);
    if (!hit) {
        return false;
    }
    if (hit == sb->hwnd) {
        return true;
    }
    HWND ownerRoot = GetAncestor(sb->hwndOwner, GA_ROOT);
    HWND hitRoot = GetAncestor(hit, GA_ROOT);
    if (ownerRoot && hitRoot && hitRoot != ownerRoot) {
        return false;
    }
    return true;
}

// Update the layered window with the current appearance
static void PaintScrollbar(OverlayScrollbar* sb) {
    if (!sb->hwnd || !HwndIsVisible(sb->hwnd)) {
        return;
    }

    Rect wrc = HwndWindowRect(sb->hwnd);
    int w = wrc.dx;
    int h = wrc.dy;
    if (w <= 0 || h <= 0) {
        return;
    }

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp || !bits) {
        if (hbmp) {
            DeleteObject(hbmp);
        }
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }
    HBITMAP hbmpOld = (HBITMAP)SelectObject(hdcMem, hbmp);

    memset(bits, 0, (size_t)w * h * 4);

    u8 alpha = 255;

    auto fillRect = [&](Rect r, Color color) {
        DWORD pixel = PremultiplyPixel(color, alpha);
        DWORD* pixels = (DWORD*)bits;
        int x0 = std::max(r.x, 0);
        int y0 = std::max(r.y, 0);
        int x1 = std::min(r.x + r.dx, w);
        int y1 = std::min(r.y + r.dy, h);
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                pixels[(y * w) + x] = pixel;
            }
        }
    };

    if (IsVisible(sb)) {
        fillRect(Rect(0, 0, w, h), ThemeTrackColor());
    }

    {
        Gdiplus::Bitmap surface(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Gdiplus::Graphics gfx(&surface);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Rect thumbRc = GetThumbRect(sb);
        Color thumbCol = sb->mouseOverThumb ? ThemeThumbHoverColor() : ThemeThumbColor();

        int thumbInset = UiScalePxForDpi(DpiGetForHwnd(sb->hwndOwner), 2);
        if (IsVisible(sb)) {
            if (IsVert(sb))
                thumbRc.SubLR(std::min(thumbInset, thumbRc.dx / 3), std::min(thumbInset, thumbRc.dx / 3));
            else
                thumbRc.SubTB(std::min(thumbInset, thumbRc.dy / 3), std::min(thumbInset, thumbRc.dy / 3));
        }
        if (!thumbRc.IsEmpty()) {
            int diameter = std::min(thumbRc.dx, thumbRc.dy);
            Gdiplus::GraphicsPath path;
            path.AddArc(thumbRc.x, thumbRc.y, diameter, diameter, 180, 90);
            path.AddArc(thumbRc.Right() - diameter, thumbRc.y, diameter, diameter, 270, 90);
            path.AddArc(thumbRc.Right() - diameter, thumbRc.Bottom() - diameter, diameter, diameter, 0, 90);
            path.AddArc(thumbRc.x, thumbRc.Bottom() - diameter, diameter, diameter, 90, 90);
            path.CloseFigure();
            Gdiplus::SolidBrush thumbBrush(
                Gdiplus::Color(alpha, GetRValue(thumbCol), GetGValue(thumbCol), GetBValue(thumbCol)));
            gfx.FillPath(&thumbBrush, &path);
        }

        if (IsVisible(sb)) {
            Color arrowCol = ThemeThumbHoverColor();
            u8 ar = (u8)MulDiv(GetRValue(arrowCol), alpha, 255);
            u8 ag = (u8)MulDiv(GetGValue(arrowCol), alpha, 255);
            u8 ab = (u8)MulDiv(GetBValue(arrowCol), alpha, 255);
            Gdiplus::Color gdipArrowCol(alpha, ar, ag, ab);

            Rect arrowTop = GetArrowTopRect(sb);
            Rect arrowBot = GetArrowBottomRect(sb);

            if (gThickArrows) {
                // filled triangles (like Windows Terminal)
                Gdiplus::SolidBrush br(gdipArrowCol);
                if (IsVert(sb)) {
                    float sz = (float)arrowTop.dx / 3.0f;
                    // up triangle
                    float cx = (float)(arrowTop.x + (arrowTop.dx / 2));
                    float cy = (float)(arrowTop.y + (arrowTop.dy / 2));
                    Gdiplus::PointF upPts[3] = {
                        {cx, cy - (sz * 0.7f)},
                        {cx - sz, cy + (sz * 0.7f)},
                        {cx + sz, cy + (sz * 0.7f)},
                    };
                    gfx.FillPolygon(&br, upPts, 3);
                    // down triangle
                    cx = (float)(arrowBot.x + (arrowBot.dx / 2));
                    cy = (float)(arrowBot.y + (arrowBot.dy / 2));
                    Gdiplus::PointF downPts[3] = {
                        {cx - sz, cy - (sz * 0.7f)},
                        {cx + sz, cy - (sz * 0.7f)},
                        {cx, cy + (sz * 0.7f)},
                    };
                    gfx.FillPolygon(&br, downPts, 3);
                } else {
                    float sz = (float)arrowTop.dy / 3.0f;
                    // left triangle
                    float cx = (float)(arrowTop.x + (arrowTop.dx / 2));
                    float cy = (float)(arrowTop.y + (arrowTop.dy / 2));
                    Gdiplus::PointF leftPts[3] = {
                        {cx - (sz * 0.7f), cy},
                        {cx + (sz * 0.7f), cy - sz},
                        {cx + (sz * 0.7f), cy + sz},
                    };
                    gfx.FillPolygon(&br, leftPts, 3);
                    // right triangle
                    cx = (float)(arrowBot.x + (arrowBot.dx / 2));
                    cy = (float)(arrowBot.y + (arrowBot.dy / 2));
                    Gdiplus::PointF rightPts[3] = {
                        {cx - (sz * 0.7f), cy - sz},
                        {cx - (sz * 0.7f), cy + sz},
                        {cx + (sz * 0.7f), cy},
                    };
                    gfx.FillPolygon(&br, rightPts, 3);
                }
            } else {
                // chevron lines
                Gdiplus::Pen pen(gdipArrowCol, 1.5f);
                pen.SetStartCap(Gdiplus::LineCapRound);
                pen.SetEndCap(Gdiplus::LineCapRound);
                pen.SetLineJoin(Gdiplus::LineJoinRound);
                int inset = IsVert(sb) ? arrowTop.dx / 5 : arrowTop.dy / 5;

                if (IsVert(sb)) {
                    int sz = arrowTop.dx / 5;
                    float cx = (float)(arrowTop.x + (arrowTop.dx / 2));
                    float cy = (float)(arrowTop.y + (arrowTop.dy / 2)) + ((float)inset / 2);
                    Gdiplus::PointF upPts[3] = {
                        {cx - (float)sz, cy + ((float)sz / 2.0f)},
                        {cx, cy - ((float)sz / 2.0f)},
                        {cx + (float)sz, cy + ((float)sz / 2.0f)},
                    };
                    gfx.DrawLines(&pen, upPts, 3);

                    cx = (float)(arrowBot.x + (arrowBot.dx / 2));
                    cy = (float)(arrowBot.y + (arrowBot.dy / 2)) - ((float)inset / 2);
                    Gdiplus::PointF downPts[3] = {
                        {cx - (float)sz, cy - ((float)sz / 2.0f)},
                        {cx, cy + ((float)sz / 2.0f)},
                        {cx + (float)sz, cy - ((float)sz / 2.0f)},
                    };
                    gfx.DrawLines(&pen, downPts, 3);
                } else {
                    int sz = arrowTop.dy / 5;
                    float cx = (float)(arrowTop.x + (arrowTop.dx / 2)) + ((float)inset / 2);
                    float cy = (float)(arrowTop.y + (arrowTop.dy / 2));
                    Gdiplus::PointF leftPts[3] = {
                        {cx + ((float)sz / 2.0f), cy - (float)sz},
                        {cx - ((float)sz / 2.0f), cy},
                        {cx + ((float)sz / 2.0f), cy + (float)sz},
                    };
                    gfx.DrawLines(&pen, leftPts, 3);

                    cx = (float)(arrowBot.x + (arrowBot.dx / 2)) - ((float)inset / 2);
                    cy = (float)(arrowBot.y + (arrowBot.dy / 2));
                    Gdiplus::PointF rightPts[3] = {
                        {cx - ((float)sz / 2.0f), cy - (float)sz},
                        {cx + ((float)sz / 2.0f), cy},
                        {cx - ((float)sz / 2.0f), cy + (float)sz},
                    };
                    gfx.DrawLines(&pen, rightPts, 3);
                }
            }
        }
    }

    POINT ptSrc = {0, 0};
    SIZE szWnd = {w, h};
    POINT ptDst = {wrc.x, wrc.y};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(sb->hwnd, hdcScreen, &ptDst, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hbmpOld);
    DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

// Make the layered window fully transparent without hiding it.
// ShowWindow(SW_HIDE) can steal activation from other windows (e.g. command palette).
static void MakeLayeredWindowTransparent(HWND hwnd) {
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 0;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &blend, ULW_ALPHA);
}

static void SetState(OverlayScrollbar* sb, State newState) {
    if (sb->state == newState) {
        return;
    }
    bool wasVisible = IsVisible(sb);
    sb->state = newState;
    bool nowVisible = IsVisible(sb);

    OverlayScrollbarUpdatePos(sb);
    if (nowVisible) {
        if (!wasVisible) {
            ShowScrollbarHwnd(sb);
        }
        PaintScrollbar(sb);
    } else {
        // Make fully transparent instead of ShowWindow(SW_HIDE) because
        // SW_HIDE can trigger Z-order changes that hide other popups
        sb->mouseOverThumb = false;
        MakeLayeredWindowTransparent(sb->hwnd);
    }

    if (wasVisible != nowVisible) {
        for (auto* other : gAllScrollbars) {
            if (other == sb || other->hwndOwner != sb->hwndOwner || !IsVisible(other)) continue;
            OverlayScrollbarUpdatePos(other);
            PaintScrollbar(other);
        }
    }

    KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
    if (newState == State::SmartThin) {
        SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->showAfterScrollMs, nullptr);
    }
}

static void ShowScrollbarWindow(OverlayScrollbar* sb, bool thick) {
    // Don't revert to thin while user is dragging the thumb
    if (sb->isDragging && !thick) {
        return;
    }
    if (sb->mode == OverlayScrollbar::Mode::Thick) {
        SetState(sb, State::AlwaysThick);
    } else {
        SetState(sb, thick ? State::SmartThick : State::SmartThin);
    }
}

static void HideScrollbarWindow(OverlayScrollbar* sb) {
    // Don't hide while user is dragging the thumb
    if (sb->isDragging) {
        return;
    }
    if (sb->mode == OverlayScrollbar::Mode::Thick) {
        return; // never hide in Thick mode
    }
    SetState(sb, State::SmartInvisible);
}

// Restart the thin-bar auto-hide countdown (showAfterScrollMs). SetState only
// arms the timer on a state transition, so continuous scroll while already
// SmartThin would otherwise let the earlier mouse-stop / first-reveal timer
// fire and hide the bar mid-scroll.
static void RestartSmartThinAutoHide(OverlayScrollbar* sb) {
    if (!sb->hwnd || sb->state != State::SmartThin) {
        return;
    }
    KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
    SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->showAfterScrollMs, nullptr);
}

// ---- Global mouse tracking ----

static void CALLBACK MouseTrackTimerProc(HWND /*hwnd*/, UINT /*msg*/, UINT_PTR /*idEvent*/, DWORD /*time*/) {
    // e.g. splitter drag uses SetCapture(); don't react to cursor proximity then
    if (GetCapture()) {
        return;
    }

    Point pt = GetCursorPosition();

    bool mouseMoved = (pt.x != gLastMousePos.x || pt.y != gLastMousePos.y);
    gLastMousePos = pt;

    HWND hwndForeground = GetForegroundWindow();

    for (auto* sb : gAllScrollbars) {
        if (!sb->hwnd || !sb->hwndOwner) {
            continue;
        }

        if (!IsActive(sb)) {
            continue;
        }

        // Only process scrollbars whose owner is in the active window hierarchy
        bool ownerActive = IsOrIsParentOf(hwndForeground, sb->hwndOwner);
        if (!ownerActive) {
            // If we were showing, hide
            if (IsVisible(sb)) {
                if (!sb->isDragging) {
                    HideScrollbarWindow(sb);
                }
            }
            continue;
        }

        // Check if mouse is over the owner window's client area, and not over a
        // different top-level window covering it (edit annotations, etc.)
        bool overOwner = MouseOverOwnerSurface(sb, pt);

        // Is the mouse over the band the thick scrollbar occupies? Being merely
        // near it isn't enough: the thick bar used to pop out while the mouse
        // was still over the page, which is distracting while reading
        Rect sbRect = GetScrollbarScreenRect(sb);
        bool overScrollbar = sbRect.Contains(pt) && overOwner;

        if (sb->isDragging) {
            // Don't change state while dragging
            continue;
        }

        if (gOverlayScrollbarSuppressThick) {
            continue;
        }

        if (overScrollbar) {
            // Mouse is over the scrollbar area - show thick
            if (!IsThick(sb)) {
                ShowScrollbarWindow(sb, true);
            }
            // Update thumb hover state
            Point clientPt = HwndScreenToClient(sb->hwnd, pt);
            Rect thumbRc = GetThumbRect(sb);
            bool wasOver = sb->mouseOverThumb;
            sb->mouseOverThumb = thumbRc.Contains(clientPt);
            if (wasOver != sb->mouseOverThumb) {
                PaintScrollbar(sb);
            }
        } else if (overOwner && mouseMoved) {
            // Mouse is over owner and moving, but not over the scrollbar - show thin
            // IsThick() means transitioning from thick to thin
            if (IsThick(sb) || sb->state != State::SmartThin) {
                ShowScrollbarWindow(sb, false);
            }
            // Reset the auto-hide timer since mouse is moving
            KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
            SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->hideAfterMouseStopMs, nullptr);
        } else if (IsThick(sb) && !overOwner) {
            // Mouse left the owner area while thick - transition to hidden
            HideScrollbarWindow(sb);
        }
        // If mouse is over owner but not moving, the existing auto-hide timer handles it
    }
}

static void StartMouseTracking() {
    if (gMouseTrackTimer) {
        return;
    }
    gLastMousePos = {-1, -1};
    gMouseTrackTimer = SetTimer(nullptr, kMouseTrackTimerID, kMouseTrackIntervalMs, MouseTrackTimerProc);
}

static void StopMouseTracking() {
    if (gMouseTrackTimer) {
        KillTimer(nullptr, gMouseTrackTimer);
        gMouseTrackTimer = 0;
    }
}

// ---- WndProc for scrollbar window (handles clicks, drag, wheel) ----

static LRESULT CALLBACK WndProcOverlayScrollbar(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayScrollbar* sb = (OverlayScrollbar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!sb) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    switch (msg) {
        case WM_TIMER:
            if (wp == OverlayScrollbar::kTimerAutoHide) {
                if (!sb->isDragging) {
                    HideScrollbarWindow(sb);
                }
                return 0;
            }
            if (wp == OverlayScrollbar::kTimerRepeatScroll) {
                if (sb->repeatScrollCode == 0) {
                    KillTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll);
                    return 0;
                }
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(sb->repeatScrollCode, 0));
                if (sb->repeatIsInitial) {
                    // switch from initial delay to repeat rate
                    sb->repeatIsInitial = false;
                    UINT repeatMs = 0;
                    SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &repeatMs, 0);
                    // SPI_GETKEYBOARDSPEED returns 0-31, map to ~33-500ms (same as OS key repeat)
                    repeatMs = 400 - (repeatMs * 12);
                    SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, repeatMs, nullptr);
                }
                return 0;
            }
            break;

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);

            if (sb->isDragging) {
                int ptInTrack = IsVert(sb) ? my : mx;
                Rect track = GetTrackRect(sb);
                int range = sb->nMax - sb->nMin + 1;
                Rect thumb = GetThumbRect(sb);
                int thumbLen = IsVert(sb) ? thumb.dy : thumb.dx;
                int trackLen = IsVert(sb) ? track.dy : track.dx;
                int scrollableTrack = trackLen - thumbLen;
                int scrollableRange = range - (int)sb->nPage;

                int dragDelta = ptInTrack - sb->dragStartY;
                int newPos = sb->dragStartPos;
                if (scrollableTrack > 0 && scrollableRange > 0) {
                    newPos = sb->dragStartPos + MulDiv(dragDelta, scrollableRange, scrollableTrack);
                }
                newPos = setMinMax(newPos, sb->nMin, sb->nMax - (int)sb->nPage + 1);
                sb->nTrackPos = newPos;
                PaintScrollbar(sb);
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBTRACK, newPos));
                return 0;
            }

            // Also update hover immediately when the cursor enters the bar.
            if (IsVisible(sb)) {
                Rect thumbRc = GetThumbRect(sb);
                bool wasOver = sb->mouseOverThumb;
                sb->mouseOverThumb = thumbRc.Contains(Point(mx, my));
                if (wasOver != sb->mouseOverThumb) {
                    PaintScrollbar(sb);
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            SetCapture(hwnd);

            if (IsVisible(sb)) {
                Rect arrowTop = GetArrowTopRect(sb);
                Rect arrowBot = GetArrowBottomRect(sb);
                Point pt(mx, my);

                if (arrowTop.Contains(pt)) {
                    // SB_LINEUP == SB_LINELEFT, but spell out which axis we mean
                    UINT code = IsVert(sb) ? SB_LINEUP : SB_LINELEFT; // NOLINT(bugprone-branch-clone)
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                    sb->repeatScrollCode = code;
                    sb->repeatIsInitial = true;
                    UINT delayMs = 0;
                    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delayMs, 0);
                    delayMs = 250 + (delayMs * 250); // 0-3 maps to 250-1000ms
                    SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, delayMs, nullptr);
                    return 0;
                }
                if (arrowBot.Contains(pt)) {
                    // SB_LINEDOWN == SB_LINERIGHT, but spell out which axis we mean
                    UINT code = IsVert(sb) ? SB_LINEDOWN : SB_LINERIGHT; // NOLINT(bugprone-branch-clone)
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                    sb->repeatScrollCode = code;
                    sb->repeatIsInitial = true;
                    UINT delayMs = 0;
                    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delayMs, 0);
                    delayMs = 250 + (delayMs * 250);
                    SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, delayMs, nullptr);
                    return 0;
                }
            }

            // Shift+click: jump thumb center to click position
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                Rect track = GetTrackRect(sb);
                int range = sb->nMax - sb->nMin + 1;
                int trackLen = IsVert(sb) ? track.dy : track.dx;
                Rect thumb = GetThumbRect(sb);
                int thumbLen = IsVert(sb) ? thumb.dy : thumb.dx;
                int scrollableTrack = trackLen - thumbLen;
                int scrollableRange = range - (int)sb->nPage;
                int clickInTrack = (IsVert(sb) ? my : mx) - (IsVert(sb) ? track.y : track.x);
                int thumbOffset = clickInTrack - (thumbLen / 2);
                thumbOffset = setMinMax(thumbOffset, 0, scrollableTrack);
                int newPos = sb->nMin;
                if (scrollableTrack > 0 && scrollableRange > 0) {
                    newPos = sb->nMin + MulDiv(thumbOffset, scrollableRange, scrollableTrack);
                }
                sb->nTrackPos = newPos;
                PaintScrollbar(sb);
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBTRACK, newPos));
                ReleaseCapture();
                return 0;
            }

            Rect thumbRc = GetThumbRect(sb);
            Point pt(mx, my);
            if (thumbRc.Contains(pt)) {
                sb->isDragging = true;
                sb->dragStartY = IsVert(sb) ? my : mx;
                sb->dragStartPos = sb->nPos;
                sb->nTrackPos = sb->nPos;
                return 0;
            }

            Rect track = GetTrackRect(sb);
            if (track.Contains(pt)) {
                int clickPos = IsVert(sb) ? my : mx;
                int thumbMid = IsVert(sb) ? (thumbRc.y + (thumbRc.dy / 2)) : (thumbRc.x + (thumbRc.dx / 2));
                UINT code;
                if (clickPos < thumbMid) {
                    // SB_PAGEUP == SB_PAGELEFT (same for DOWN/RIGHT); spell out the axis
                    code = IsVert(sb) ? SB_PAGEUP : SB_PAGELEFT; // NOLINT(bugprone-branch-clone)
                } else {
                    code = IsVert(sb) ? SB_PAGEDOWN : SB_PAGERIGHT; // NOLINT(bugprone-branch-clone)
                }
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                sb->repeatScrollCode = code;
                sb->repeatIsInitial = true;
                UINT delayMs = 0;
                SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delayMs, 0);
                delayMs = 250 + (delayMs * 250);
                SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, delayMs, nullptr);
                return 0;
            }
            ReleaseCapture();
            return 0;
        }

        case WM_LBUTTONUP:
            if (sb->repeatScrollCode != 0) {
                sb->repeatScrollCode = 0;
                KillTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll);
            }
            if (sb->isDragging) {
                sb->isDragging = false;
                sb->nPos = sb->nTrackPos;
                PaintScrollbar(sb);
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBPOSITION, sb->nPos));
            }
            ReleaseCapture();
            return 0;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            SendMessageW(sb->hwndOwner, msg, wp, lp);
            return 0;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_NCHITTEST: {
            // pass through rightmost 2px of vertical scrollbar for frame resize
            if (IsVert(sb)) {
                int x = GET_X_LPARAM(lp);
                Rect rc = HwndWindowRect(hwnd);
                if ((rc.x + rc.dx - x) <= 2) {
                    return HTTRANSPARENT;
                }
            }
            LRESULT def = DefWindowProcW(hwnd, msg, wp, lp);
            if (def == HTNOWHERE) {
                return HTCLIENT;
            }
            return def;
        }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterScrollbarClass() {
    if (gScrollbarClassRegistered) {
        return;
    }
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProcOverlayScrollbar;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = GetCachedCursor(IDC_ARROW);
    wcex.lpszClassName = kOverlayScrollbarClass;
    RegisterClassExW(&wcex);
    gScrollbarClassRegistered = true;
}

OverlayScrollbar* OverlayScrollbarCreate(HWND hwndOwner, OverlayScrollbar::Type type, OverlayScrollbar::Mode mode) {
    RegisterScrollbarClass();

    auto* sb = new OverlayScrollbar();
    sb->hwndOwner = hwndOwner;
    sb->type = type;
    sb->mode = mode;
    ScaledWidth(sb);
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    DWORD style = WS_POPUP;

    // use the top-level ancestor as owner so the scrollbar stays above its
    // own window but doesn't cover other application windows
    HWND hwndTopLevel = GetAncestor(hwndOwner, GA_ROOT);
    sb->hwnd = CreateWindowExW(exStyle, kOverlayScrollbarClass, nullptr, style, 0, 0, 1, 1, hwndTopLevel, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(sb->hwnd, GWLP_USERDATA, (LONG_PTR)sb);

    // Register for global mouse tracking
    VecAppend(gAllScrollbars, sb);
    StartMouseTracking();

    return sb;
}

void OverlayScrollbarDestroy(OverlayScrollbar* sb) {
    if (!sb) {
        return;
    }

    // Unregister from global mouse tracking
    VecRemove(gAllScrollbars, sb);
    if (len(gAllScrollbars) == 0) {
        StopMouseTracking();
    }

    if (sb->hwnd) {
        KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
        DestroyWindow(sb->hwnd);
    }
    delete sb;
}

// Same API as SetScrollInfo / GetScrollInfo
void OverlayScrollbarSetInfo(OverlayScrollbar* sb, const SCROLLINFO* si, bool redraw) {
    if (!sb) {
        return;
    }
    bool changed = false;
    if (si->fMask & SIF_RANGE) {
        if (sb->nMin != si->nMin || sb->nMax != si->nMax) {
            changed = true;
        }
        sb->nMin = si->nMin;
        sb->nMax = si->nMax;
    }
    if (si->fMask & SIF_PAGE) {
        if (sb->nPage != si->nPage) {
            changed = true;
        }
        sb->nPage = si->nPage;
    }
    if (si->fMask & SIF_POS) {
        if (sb->nPos != si->nPos) {
            changed = true;
        }
        sb->nPos = si->nPos;
    }

    if (redraw && changed) {
        // Scroll moved: re-reveal the thin smart bar if auto-hidden, repaint the
        // thumb, and keep the auto-hide timer alive while scrolling continues
        // (wheel / keyboard / smooth-scroll ticks — not only mouse motion).
        if (IsVisible(sb)) {
            PaintScrollbar(sb);
            if (sb->state == State::SmartThin) {
                RestartSmartThinAutoHide(sb);
            }
        } else {
            ShowScrollbarWindow(sb, false);
        }
    }
}

// Show the thin smart overlay after scroll activity (mouse wheel, keys, etc.).
// Unlike mouse-move tracking, this does not require cursor motion (#5859).
void OverlayScrollbarNotifyScroll(OverlayScrollbar* sb) {
    if (!sb || !IsActive(sb) || sb->isDragging) {
        return;
    }
    if (sb->mode == OverlayScrollbar::Mode::Thick) {
        return;
    }
    // Leave thick-from-proximity alone; only (re)show the thin indicator.
    if (IsThick(sb)) {
        return;
    }
    if (sb->state != State::SmartThin) {
        ShowScrollbarWindow(sb, false);
    } else {
        RestartSmartThinAutoHide(sb);
    }
}

void OverlayScrollbarGetInfo(OverlayScrollbar* sb, SCROLLINFO* si) {
    if (!sb) {
        return;
    }
    if (si->fMask & SIF_RANGE) {
        si->nMin = sb->nMin;
        si->nMax = sb->nMax;
    }
    if (si->fMask & SIF_PAGE) {
        si->nPage = sb->nPage;
    }
    if (si->fMask & SIF_POS) {
        si->nPos = sb->nPos;
    }
    if (si->fMask & SIF_TRACKPOS) {
        si->nTrackPos = sb->nTrackPos;
    }
}

// Call when owner window moves/resizes
void OverlayScrollbarUpdatePos(OverlayScrollbar* sb) {
    if (!sb || !sb->hwnd || !sb->hwndOwner) {
        return;
    }

    Rect ownerRc = HwndWindowRect(sb->hwndOwner);
    Rect ownerWindow = ownerRc;
    if (sb->nativeAdapter) {
        ownerRc = HwndMapRectToWindow(HwndClientRect(sb->hwndOwner), sb->hwndOwner, nullptr);
        if (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & WS_VSCROLL) {
            ownerRc.dx += DpiGetSystemMetrics(SM_CXVSCROLL, DpiGetForHwnd(sb->hwndOwner));
        }
        if (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & WS_HSCROLL) {
            ownerRc.dy += DpiGetSystemMetrics(SM_CYHSCROLL, DpiGetForHwnd(sb->hwndOwner));
        }
    }

    int scrollW = ScaledWidth(sb);
    int x, y, w, h;

    // Keep visible bars from covering each other's end arrows.
    bool siblingVisible = false;
    for (auto* other : gAllScrollbars) {
        if (other != sb && other->hwndOwner == sb->hwndOwner && IsVisible(other)) {
            siblingVisible = true;
            break;
        }
    }
    int siblingInset = 0;
    if (IsVisible(sb) && siblingVisible) {
        siblingInset = scrollW;
    }

    if (IsVert(sb)) {
        x = ownerRc.x + ownerRc.dx - scrollW;
        y = ownerRc.y;
        w = scrollW;
        h = ownerRc.dy - siblingInset;
    } else {
        x = ownerRc.x;
        y = ownerRc.y + ownerRc.dy - scrollW;
        w = ownerRc.dx - siblingInset;
        h = scrollW;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(sb->hwnd, GWL_EXSTYLE);
    if (IsVisible(sb)) {
        exStyle &= ~WS_EX_TRANSPARENT;
    } else {
        exStyle |= WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(sb->hwnd, GWL_EXSTYLE, exStyle);

    // SWP_NOOWNERZORDER: raising an owned popup otherwise raises the owner
    // frame over other top-level windows (command palette, annotations).
    UINT swpFlags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    HWND insertAfter = nullptr;
    // re-show the window if the state says it should be visible
    // (RelayoutFrame hides overlay scrollbar windows with SW_HIDE
    // to prevent them from appearing at stale positions)
    if (IsVisible(sb) && !HwndIsVisible(sb->hwnd)) {
        swpFlags |= SWP_SHOWWINDOW;
        insertAfter = ScrollbarZOrderAfter(sb);
    } else {
        swpFlags |= SWP_NOZORDER;
    }
    Rect previousBounds = HwndWindowRect(sb->hwnd);
    if (!sb->nativeAdapter || previousBounds != Rect{x, y, w, h} || (swpFlags & SWP_SHOWWINDOW)) {
        SetWindowPos(sb->hwnd, insertAfter, x, y, w, h, swpFlags);
    }
    if (sb->nativeAdapter) {
        Rect visible = ownerRc.Intersect(NativeScrollbarClip(sb->hwndOwner));
        Rect stripe{x, y, w, h};
        visible = visible.Intersect(stripe);
        HRGN region = CreateRectRgn(visible.x - x, visible.y - y, visible.Right() - x, visible.Bottom() - y);
        HRGN ownerRegion = CreateRectRgn(0, 0, 0, 0);
        if (GetWindowRgn(sb->hwndOwner, ownerRegion) != ERROR) {
            OffsetRgn(ownerRegion, ownerWindow.x - x, ownerWindow.y - y);
            CombineRgn(region, region, ownerRegion, RGN_AND);
        }
        DeleteObject(ownerRegion);
        HRGN previous = CreateRectRgn(0, 0, 0, 0);
        bool same = GetWindowRgn(sb->hwnd, previous) != ERROR && EqualRgn(previous, region);
        DeleteObject(previous);
        if (same || !SetWindowRgn(sb->hwnd, region, true)) DeleteObject(region);
    }
}

// Hide the scrollbar window without stealing activation from other windows.
// Uses SWP_HIDEWINDOW | SWP_NOACTIVATE instead of ShowWindow(SW_HIDE).
void OverlayScrollbarHide(OverlayScrollbar* sb) {
    if (!sb || !sb->hwnd) {
        return;
    }
    SetWindowPos(sb->hwnd, nullptr, 0, 0, 0, 0,
                 SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

// Show/hide
void OverlayScrollbarShow(OverlayScrollbar* sb, bool show) {
    if (!sb) {
        return;
    }
    if (!show) {
        if (!IsActive(sb)) {
            return;
        }
        SetState(sb, State::Hidden);
        return;
    }

    // Already painted thin/thick — nothing to do. SmartInvisible still has an
    // IsWindowVisible layered HWND (fully transparent), so do not treat that as
    // shown: keyboard scroll / UpdateScrollbars must re-reveal it (#5850).
    if (IsVisible(sb) && HwndIsVisible(sb->hwnd)) {
        return;
    }
    if (!IsActive(sb) || sb->state == State::SmartInvisible) {
        ShowScrollbarWindow(sb, false);
        return;
    }
    // re-show if window was temporarily hidden (e.g. during relayout)
    OverlayScrollbarUpdatePos(sb);
    ShowScrollbarHwnd(sb);
    PaintScrollbar(sb);
}

// Change the scrollbar mode (Smart vs Thick)
void OverlayScrollbarSetMode(OverlayScrollbar* sb, OverlayScrollbar::Mode mode) {
    if (!sb || sb->mode == mode) {
        return;
    }
    sb->mode = mode;
    if (!IsActive(sb)) {
        return;
    }
    // transition to the appropriate state for the new mode
    if (mode == OverlayScrollbar::Mode::Thick) {
        SetState(sb, State::AlwaysThick);
    } else {
        // Smart mode: start as thin, will auto-hide
        SetState(sb, State::SmartThin);
    }
}

// returns true if scrollbar is visible (thin, thick, or always thick)
bool IsOverlayScrollbarVisible(OverlayScrollbar* sb) {
    return sb && IsVisible(sb);
}

int AppScrollbarTrackPos(HWND hwnd, int fallback, int bar) {
    auto* sb = (OverlayScrollbar*)GetPropW(hwnd, bar == SB_HORZ ? kNativeHScrollbarProperty : kNativeScrollbarProperty);
    return sb && (sb->isDragging || sb->sendingNative) ? sb->nTrackPos : fallback;
}

int AppScrollbarInset(HWND hwnd) {
    if (!GetPropW(hwnd, kNativeScrollbarProperty)) return 0;
    int dpi = DpiGetForHwnd(hwnd);
    int native = DpiGetSystemMetrics(SM_CXVSCROLL, dpi);
    return std::max(0, GetAppScrollbarWidth(dpi) - native);
}

static void SyncNativeScrollbar(OverlayScrollbar* sb, bool force) {
    if (sb->syncingNative) return;
    sb->syncingNative = true;
    defer {
        sb->syncingNative = false;
    };
    SCROLLINFO info{sizeof(info), SIF_ALL};
    int bar = IsVert(sb) ? SB_VERT : SB_HORZ;
    LONG_PTR style = IsVert(sb) ? WS_VSCROLL : WS_HSCROLL;
    bool range = GetScrollInfo(sb->hwndOwner, bar, &info) && info.nMax - info.nMin + 1 > (int)info.nPage;
    bool show = range && HwndIsVisible(sb->hwndOwner) && (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & style);
    Rect bounds = HwndWindowRect(sb->hwndOwner);
    Rect clip = NativeScrollbarClip(sb->hwndOwner);
    int width = GetAppScrollbarWidth(DpiGetForHwnd(sb->hwndOwner));
    Color track = ThemeTrackColor(), thumb = ThemeThumbColor();
    const SCROLLINFO& before = sb->nativeInfo;
    if (!force && sb->nativeSyncValid && before.nMin == info.nMin && before.nMax == info.nMax &&
        before.nPage == info.nPage && before.nPos == info.nPos && sb->nativeBounds == bounds &&
        sb->nativeClip == clip && sb->nativeWidth == width && sb->nativeShown == show && sb->nativeTrack == track &&
        sb->nativeThumb == thumb) {
        return;
    }
    bool repaint = sb->nativeSyncValid && (sb->nativeTrack != track || sb->nativeThumb != thumb ||
                                           sb->nativeWidth != width || sb->nativeBounds.Size() != bounds.Size());
    sb->nativeSyncValid = true;
    sb->nativeInfo = info;
    sb->nativeBounds = bounds;
    sb->nativeClip = clip;
    sb->nativeWidth = width;
    sb->nativeShown = show;
    sb->nativeTrack = track;
    sb->nativeThumb = thumb;
    OverlayScrollbarSetInfo(sb, &info, show);
    OverlayScrollbarShow(sb, show);
    if (show) {
        OverlayScrollbarUpdatePos(sb);
        if (repaint) PaintScrollbar(sb);
    }
}

static void EraseNativeScrollEdges(HWND hwnd) {
    int dpi = DpiGetForHwnd(hwnd);
    int width = GetAppScrollbarWidth(dpi);
    int nativeV = DpiGetSystemMetrics(SM_CXVSCROLL, dpi);
    int nativeH = DpiGetSystemMetrics(SM_CYHSCROLL, dpi);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((!(style & WS_VSCROLL) || width >= nativeV) && (!(style & WS_HSCROLL) || width >= nativeH)) return;

    Rect client = HwndMapRectToWindow(HwndClientRect(hwnd), hwnd, nullptr);
    Rect window = HwndWindowRect(hwnd);
    client.Offset(-window.x, -window.y);
    HDC dc = GetWindowDC(hwnd);
    if (!dc) return;
    HBRUSH brush = CreateSolidBrush(ThemeTrackColor());
    if (style & WS_VSCROLL && width < nativeV) {
        RECT stripe = ToRECT(Rect{client.Right(), client.y, nativeV - width, client.dy});
        FillRect(dc, &stripe, brush);
    }
    if (style & WS_HSCROLL && width < nativeH) {
        RECT stripe = ToRECT(Rect{client.x, client.Bottom(), client.dx, nativeH - width});
        FillRect(dc, &stripe, brush);
    }
    DeleteObject(brush);
    ReleaseDC(hwnd, dc);
}

static void SyncNativeScrollbars(HWND hwnd, bool force = false) {
    auto* vertical = (OverlayScrollbar*)GetPropW(hwnd, kNativeScrollbarProperty);
    if (vertical) SyncNativeScrollbar(vertical, force);
    auto* horizontal = (OverlayScrollbar*)GetPropW(hwnd, kNativeHScrollbarProperty);
    if (!horizontal && (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_HSCROLL)) {
        horizontal = OverlayScrollbarCreate(hwnd, OverlayScrollbar::Type::Horz, OverlayScrollbar::Mode::Thick);
        horizontal->nativeAdapter = true;
        if (!horizontal->hwnd || !SetPropW(hwnd, kNativeHScrollbarProperty, horizontal)) {
            OverlayScrollbarDestroy(horizontal);
            horizontal = nullptr;
        }
    }
    if (horizontal) SyncNativeScrollbar(horizontal, force);
    if (vertical && horizontal) {
        OverlayScrollbar* bars[] = {vertical, horizontal};
        for (auto* sb : bars) {
            if (!IsVisible(sb)) continue;
            Size previous = HwndWindowRect(sb->hwnd).Size();
            OverlayScrollbarUpdatePos(sb);
            if (previous != HwndWindowRect(sb->hwnd).Size()) PaintScrollbar(sb);
        }
    }
}

static LRESULT CALLBACK NativeScrollbarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* sb = (OverlayScrollbar*)data;
    if (msg == WM_NCDESTROY) {
        KillTimer(hwnd, kNativeScrollbarTimer);
        RemovePropW(hwnd, kNativeScrollbarProperty);
        auto* horizontal = (OverlayScrollbar*)GetPropW(hwnd, kNativeHScrollbarProperty);
        RemovePropW(hwnd, kNativeHScrollbarProperty);
        RemoveWindowSubclass(hwnd, NativeScrollbarProc, id);
        OverlayScrollbarDestroy(sb);
        OverlayScrollbarDestroy(horizontal);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (msg == WM_TIMER && wp == kNativeScrollbarTimer) {
        SyncNativeScrollbars(hwnd);
        return 0;
    }
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    if ((OverlayScrollbar*)GetPropW(hwnd, kNativeScrollbarProperty) != sb) return result;
    if (msg == WM_SIZE || msg == WM_SETTEXT || msg == WM_VSCROLL || msg == WM_HSCROLL || msg == WM_MOUSEWHEEL ||
        msg == WM_MOUSEHWHEEL || msg == WM_KEYDOWN || msg == WM_WINDOWPOSCHANGED || msg == WM_SHOWWINDOW ||
        msg == WM_NCPAINT || msg == EM_SETSCROLLPOS || msg == LB_SETTOPINDEX || msg == EM_LINESCROLL ||
        msg == WM_STYLECHANGED) {
        SyncNativeScrollbars(hwnd, msg == WM_WINDOWPOSCHANGED);
        EraseNativeScrollEdges(hwnd);
    }
    return result;
}

void InstallAppScrollbar(HWND hwnd) {
    if (!hwnd || GetPropW(hwnd, kNativeScrollbarProperty)) return;
    auto* sb = OverlayScrollbarCreate(hwnd, OverlayScrollbar::Type::Vert, OverlayScrollbar::Mode::Thick);
    sb->nativeAdapter = true;
    if (!sb->hwnd || !SetPropW(hwnd, kNativeScrollbarProperty, sb) ||
        !SetWindowSubclass(hwnd, NativeScrollbarProc, kNativeScrollbarSubclass, (DWORD_PTR)sb)) {
        RemovePropW(hwnd, kNativeScrollbarProperty);
        OverlayScrollbarDestroy(sb);
        return;
    }
    SetTimer(hwnd, kNativeScrollbarTimer, 200, nullptr);
    SyncNativeScrollbars(hwnd);
    EraseNativeScrollEdges(hwnd);
}

void RemoveAppScrollbar(HWND hwnd) {
    auto* sb = (OverlayScrollbar*)GetPropW(hwnd, kNativeScrollbarProperty);
    if (!sb) return;
    KillTimer(hwnd, kNativeScrollbarTimer);
    RemovePropW(hwnd, kNativeScrollbarProperty);
    auto* horizontal = (OverlayScrollbar*)GetPropW(hwnd, kNativeHScrollbarProperty);
    RemovePropW(hwnd, kNativeHScrollbarProperty);
    RemoveWindowSubclass(hwnd, NativeScrollbarProc, kNativeScrollbarSubclass);
    OverlayScrollbarDestroy(sb);
    OverlayScrollbarDestroy(horizontal);
}

#if IS_DEBUG
bool OverlayScrollbar_UnitTestsNative() {
    Settings* saved = gSettings;
    gSettings = NewSettings({});
    gSettings->scrollbarWidth = 28;
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 300, 200, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 10, 10, 250,
                                100, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    bool ok = parent && edit;
    if (edit) {
        str::Builder text;
        for (int i = 0; i < 2000; i++) text.Append(StrL("Practice word\r\n"));
        SendMessageW(edit, EM_SETLIMITTEXT, 100000, 0);
        SetWindowTextW(edit, CWStrTemp(ToWStrTemp(ToStrTemp(text))));
        InstallAppScrollbar(edit);
        auto* sb = (OverlayScrollbar*)GetPropW(edit, kNativeScrollbarProperty);
        ok &= sb && sb->nativeAdapter && !HwndIsVisible(sb->hwnd);
        utassert(sb && sb->nativeAdapter && !HwndIsVisible(sb->hwnd));
        if (sb) {
            if (!ThemeUsesHighContrastColors()) {
                utassert(ThemeThumbColor() == MkRgb(139, 139, 139));
                utassert(ThemeThumbHoverColor() == MkRgb(105, 105, 105));
            }
            gSettings->scrollbarWidth = 8;
            utassert(ScaledWidth(sb) == GetAppScrollbarWidth(DpiGetForHwnd(edit)));
            SetWindowPos(sb->hwnd, nullptr, 0, 0, 8, 200, SWP_NOZORDER | SWP_NOACTIVATE);
            sb->state = State::SmartThin;
            utassert(ScaledWidth(sb) == GetAppScrollbarWidth(DpiGetForHwnd(edit)));
            Rect smartTrack = GetTrackRect(sb);
            sb->state = State::AlwaysThick;
            utassert(smartTrack == GetTrackRect(sb));
            sb->state = State::Hidden;
            gSettings->scrollbarWidth = 28;
            utassert(!GetPropW(edit, kNativeHScrollbarProperty));
            SetWindowLongPtrW(edit, GWL_STYLE, GetWindowLongPtrW(edit, GWL_STYLE) | WS_HSCROLL);
            SCROLLINFO horizontalInfo{sizeof(horizontalInfo), SIF_RANGE | SIF_PAGE | SIF_POS};
            horizontalInfo.nMax = 200000;
            horizontalInfo.nPage = 100;
            horizontalInfo.nPos = 100000;
            SetScrollInfo(edit, SB_HORZ, &horizontalInfo, false);
            SyncNativeScrollbars(edit);
            auto* horizontal = (OverlayScrollbar*)GetPropW(edit, kNativeHScrollbarProperty);
            utassert(horizontal && horizontal->type == OverlayScrollbar::Type::Horz);
            if (horizontal) {
                horizontal->sendingNative = true;
                horizontal->nTrackPos = 170000;
                utassert(AppScrollbarTrackPos(edit, 7, SB_HORZ) == 170000);
                utassert(AppScrollbarTrackPos(edit, 7) == 7);
                horizontal->sendingNative = false;
                utassert(AppScrollbarTrackPos(edit, 7, SB_HORZ) == 7);
                horizontal->state = State::AlwaysThick;
                SetWindowPos(horizontal->hwnd, nullptr, 0, 0, 200, GetAppScrollbarWidth(DpiGetForHwnd(edit)),
                             SWP_NOZORDER | SWP_NOACTIVATE);
                Rect track = GetTrackRect(horizontal);
                Rect thumb = GetThumbRect(horizontal);
                utassert(track.Intersect(thumb) == thumb);
                utassert(GetArrowTopRect(horizontal).Right() <= track.x);
                utassert(GetArrowBottomRect(horizontal).x >= track.Right());
                SetWindowPos(horizontal->hwnd, nullptr, 0, 0, 8, 20, SWP_NOZORDER | SWP_NOACTIVATE);
                track = GetTrackRect(horizontal);
                thumb = GetThumbRect(horizontal);
                utassert(track.dx >= 0 && thumb.dx >= 0 && thumb.dx <= track.dx);
                horizontal->state = State::Hidden;
            }
            sb->nTrackPos = 1200;
            SendScrollMsg(sb, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, sb->nTrackPos));
            ok &= SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0) == 1200;
            utassert(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0) == 1200);
            sb->sendingNative = true;
            sb->nTrackPos = 150000;
            ok &= AppScrollbarTrackPos(edit, 7) == 150000;
            sb->sendingNative = false;
            ok &= AppScrollbarTrackPos(edit, 7) == 7;
            ok &= AppScrollbarInset(edit) == std::max(0, GetAppScrollbarWidth(DpiGetForHwnd(edit)) -
                                                             DpiGetSystemMetrics(SM_CXVSCROLL, DpiGetForHwnd(edit)));
            MoveWindow(edit, 10, 170, 250, 100, false);
            OverlayScrollbarUpdatePos(sb);
            HRGN region = CreateRectRgn(0, 0, 0, 0);
            RECT clipped{};
            ok &= GetWindowRgn(sb->hwnd, region) != ERROR;
            utassert(GetWindowRgn(sb->hwnd, region) != ERROR);
            GetRgnBox(region, &clipped);
            ok &= clipped.bottom <= 30;
            utassert(clipped.bottom <= 30);
            DeleteObject(region);
        }
    }
    HMODULE richModule = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (richModule) {
        HWND rich = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 10,
                                    10, 250, 100, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        ok &= rich != nullptr;
        if (rich) {
            str::Builder text;
            for (int i = 0; i < 10000; i++) text.Append(StrL("Dictionary definition\r\n"));
            SendMessageW(rich, EM_EXLIMITTEXT, 0, 1000000);
            SetWindowTextW(rich, CWStrTemp(ToWStrTemp(ToStrTemp(text))));
            InstallAppScrollbar(rich);
            auto* sb = (OverlayScrollbar*)GetPropW(rich, kNativeScrollbarProperty);
            ok &= sb && !HwndIsVisible(sb->hwnd);
            if (sb) {
                sb->nTrackPos = 80000;
                SendScrollMsg(sb, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, sb->nTrackPos));
                int firstLine = (int)SendMessageW(rich, EM_GETFIRSTVISIBLELINE, 0, 0);
                ok &= firstLine > 1000;
                utassert(firstLine > 1000);
            }
        }
    }
    if (parent) DestroyWindow(parent);
    if (richModule) FreeLibrary(richModule);
    DeleteSettings(gSettings);
    gSettings = saved;
    return ok;
}
#endif
