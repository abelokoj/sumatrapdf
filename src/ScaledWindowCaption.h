/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct ScaledWindowCaption {
    int extraHeight = 0;
    int hot = HTNOWHERE;
    int pressed = HTNOWHERE;
};

#if IS_DEBUG
static int appCaptionPaintCount = 0;
#endif

static int AppCaptionHeight(HWND hwnd) {
    int dpi = DpiGetForHwnd(hwnd);
    int textHeight = PlatformFontLineHeight(GetAppFontForDpi(dpi));
    return std::max(DpiGetSystemMetrics(SM_CYCAPTION, dpi), textHeight + UiScalePxForDpi(dpi, 12));
}

static int AppCaptionExtraHeight(HWND hwnd) {
    return AppCaptionHeight(hwnd) - DpiGetSystemMetrics(SM_CYCAPTION, DpiGetForHwnd(hwnd));
}

static Rect AppCaptionRect(HWND hwnd) {
    Rect window = HwndWindowRect(hwnd);
    RECT client{};
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, nullptr, (POINT*)&client, 2);
    int bottom = client.top - window.y;
    int height = AppCaptionHeight(hwnd);
    int border = std::max(1, (int)client.left - window.x);
    return {border, std::max(0, bottom - height), std::max(0, window.dx - 2 * border), height};
}

static Rect AppCaptionButton(HWND hwnd, int hit) {
    Rect caption = AppCaptionRect(hwnd);
    int width = caption.dy;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    bool rtl = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    int index = hit == HTCLOSE ? 0 : hit == HTMAXBUTTON ? 1 : 2;
    if (hit == HTMINBUTTON && !(style & WS_MAXIMIZEBOX)) index--;
    int x = rtl ? caption.x + index * width : caption.Right() - (index + 1) * width;
    return {x, caption.y, width, caption.dy};
}

static bool AppCaptionButtonEnabled(HWND hwnd, int hit) {
    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (!menu) return false;
    UINT command = hit == HTCLOSE       ? SC_CLOSE
                   : hit == HTMINBUTTON ? SC_MINIMIZE
                   : IsZoomed(hwnd)     ? SC_RESTORE
                                        : SC_MAXIMIZE;
    UINT state = GetMenuState(menu, command, MF_BYCOMMAND);
    return state != (UINT)-1 && !(state & (MF_DISABLED | MF_GRAYED));
}

static int AppCaptionHitTest(HWND hwnd, Point point) {
    if (!AppCaptionRect(hwnd).Contains(point)) return HTNOWHERE;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!(style & WS_SYSMENU)) return HTCAPTION;
    if (AppCaptionButton(hwnd, HTCLOSE).Contains(point)) return HTCLOSE;
    if ((style & WS_MAXIMIZEBOX) && AppCaptionButton(hwnd, HTMAXBUTTON).Contains(point)) return HTMAXBUTTON;
    if ((style & WS_MINIMIZEBOX) && AppCaptionButton(hwnd, HTMINBUTTON).Contains(point)) return HTMINBUTTON;
    Rect caption = AppCaptionRect(hwnd);
    bool rtl = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    Rect icon = {rtl ? caption.Right() - caption.dy : caption.x, caption.y, caption.dy, caption.dy};
    return icon.Contains(point) ? HTSYSMENU : HTCAPTION;
}

static void PaintAppCaption(HWND hwnd, ScaledWindowCaption* state, HDC target = nullptr) {
    if (!gSettings) return;
    HDC dc = target ? target : GetWindowDC(hwnd);
    if (!dc) return;
    defer {
        if (!target) ReleaseDC(hwnd, dc);
    };
    Rect caption = AppCaptionRect(hwnd);
    if (caption.dx <= 0) return;
#if IS_DEBUG
    appCaptionPaintCount++;
#endif
    GfxHdc gfx(dc);
    int dpi = DpiGetForHwnd(hwnd);
    Rect window = HwndWindowRect(hwnd);
    RECT client{};
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, nullptr, (POINT*)&client, 2);
    OffsetRect(&client, -window.x, -window.y);
    int frameSaved = SaveDC(dc);
    ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
    gfx.FillRect({0, 0, window.dx, window.dy}, ThemeWindowBackgroundColor());
    gfx.FillRoundedRect({0, 0, window.dx, window.dy}, IsZoomed(hwnd) ? 0 : 2 * GetAppCornerRadius(dpi, 6),
                        kColorTransparent, ThemeEdgeColor());
    RestoreDC(dc, frameSaved);
    gfx.FillRect(caption, ThemeWindowBackgroundColor());
    PlatformFont* font = GetAppFontForDpi(dpi);
    int pad = UiScalePxForDpi(dpi, 6);
    int iconSize = PlatformFontLineHeight(font);
    bool rtl = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    HICON icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0);
    if (!icon) icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
    Rect title = caption;
    title.x += pad;
    title.dx -= 2 * pad;
    if (icon && (style & WS_SYSMENU)) {
        int x = rtl ? title.Right() - iconSize : title.x;
        DrawIconEx(dc, x, caption.y + (caption.dy - iconSize) / 2, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        title.dx -= iconSize + pad;
        if (!rtl) title.x += iconSize + pad;
    }
    int buttons = (style & WS_SYSMENU) ? 1 + !!(style & WS_MINIMIZEBOX) + !!(style & WS_MAXIMIZEBOX) : 0;
    title.dx = std::max(0, title.dx - buttons * caption.dy);
    if (rtl) title.x += buttons * caption.dy;
    u32 textFlags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
    if (rtl) textFlags |= DT_RTLREADING | DT_RIGHT;
    int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ThemeWindowTextColor() & 0x00ffffff);
    HdcDrawText(dc, HwndGetTextWTemp(hwnd), title, textFlags, font->GetHFont());
    RestoreDC(dc, saved);
    for (int hit : {HTCLOSE, HTMAXBUTTON, HTMINBUTTON}) {
        if (hit == HTCLOSE && !(style & WS_SYSMENU)) continue;
        if (hit == HTMAXBUTTON && !(style & WS_MAXIMIZEBOX)) continue;
        if (hit == HTMINBUTTON && !(style & WS_MINIMIZEBOX)) continue;
        Rect button = AppCaptionButton(hwnd, hit);
        bool closeHot = hit == HTCLOSE && hit == state->hot && AppCaptionButtonEnabled(hwnd, hit);
        if (closeHot) {
            int inset = UiScalePxForDpi(dpi, 4);
            gfx.FillEllipse({button.x + inset, button.y + inset, button.dx - inset * 2, button.dy - inset * 2},
                            gColsCloseBtn[kColCloseCircleHover]);
        } else if (hit == state->hot) {
            gfx.FillRoundedRect(button, GetAppCornerRadius(dpi, 4), ThemeHotBackgroundColor());
        }
        int glyph = std::max(UiScalePxForDpi(dpi, 10), iconSize / 2);
        int x = button.x + (button.dx - glyph) / 2;
        int y = button.y + (button.dy - glyph) / 2;
        int stroke = std::max(1, UiScalePxForDpi(dpi, 1));
        Color color = AppCaptionButtonEnabled(hwnd, hit) ? ThemeWindowTextColor() : ThemeWindowTextDisabledColor();
        if (closeHot) color = gColsCloseBtn[kColCloseXHover];
        if (hit == HTCLOSE) {
            gfx.DrawLineAA({x, y}, {x + glyph, y + glyph}, color, (float)stroke);
            gfx.DrawLineAA({x + glyph, y}, {x, y + glyph}, color, (float)stroke);
        } else if (hit == HTMINBUTTON) {
            gfx.DrawLineAA({x, y + glyph / 2}, {x + glyph, y + glyph / 2}, color, (float)stroke);
        } else {
            gfx.DrawRect({x, y, glyph, glyph}, color, stroke);
            if (IsZoomed(hwnd)) gfx.DrawRect({x + glyph / 4, y - glyph / 4, glyph, glyph}, color, stroke);
        }
    }
}

static void UpdateAppCaptionMetrics(HWND hwnd, ScaledWindowCaption* state) {
    int extra = AppCaptionExtraHeight(hwnd);
    if (extra == state->extraHeight) return;
    Rect window = HwndWindowRect(hwnd);
    int delta = extra - state->extraHeight;
    state->extraHeight = extra;
    SetWindowPos(hwnd, nullptr, 0, 0, window.dx, window.dy + (IsZoomed(hwnd) ? 0 : delta),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static LRESULT CALLBACK AppCaptionSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* state = (ScaledWindowCaption*)data;
    bool hotChanged = false;
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, AppCaptionSubclass, id);
        delete state;
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (msg == WM_NCCALCSIZE) {
        LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        RECT* client = wp ? &((NCCALCSIZE_PARAMS*)lp)->rgrc[0] : (RECT*)lp;
        client->top = std::min(client->bottom, client->top + state->extraHeight);
        return result;
    }
    if (msg == WM_NCHITTEST) {
        LRESULT native = DefSubclassProc(hwnd, msg, wp, lp);
        if (native >= HTLEFT && native <= HTBOTTOMRIGHT) return native;
        Rect window = HwndWindowRect(hwnd);
        Point point = {(short)LOWORD(lp) - window.x, (short)HIWORD(lp) - window.y};
        int hit = AppCaptionHitTest(hwnd, point);
        return hit == HTNOWHERE ? native : hit;
    }
    if (msg == WM_NCLBUTTONDOWN && (wp == HTCLOSE || wp == HTMINBUTTON || wp == HTMAXBUTTON)) {
        if (!AppCaptionButtonEnabled(hwnd, (int)wp)) return 0;
        state->pressed = (int)wp;
        state->hot = (int)wp;
        SetCapture(hwnd);
        PaintAppCaption(hwnd, state);
        return 0;
    }
    if ((msg == WM_MOUSEMOVE || msg == WM_LBUTTONUP) && state->pressed != HTNOWHERE) {
        POINT screen{(short)LOWORD(lp), (short)HIWORD(lp)};
        ClientToScreen(hwnd, &screen);
        Rect window = HwndWindowRect(hwnd);
        state->hot = AppCaptionHitTest(hwnd, {screen.x - window.x, screen.y - window.y});
        if (msg == WM_LBUTTONUP) {
            int pressed = state->pressed;
            int released = state->hot;
            state->pressed = HTNOWHERE;
            ReleaseCapture();
            if (released == pressed) {
                UINT command = pressed == HTCLOSE       ? SC_CLOSE
                               : pressed == HTMINBUTTON ? SC_MINIMIZE
                               : IsZoomed(hwnd)         ? SC_RESTORE
                                                        : SC_MAXIMIZE;
                PostMessageW(hwnd, WM_SYSCOMMAND, command, 0);
            }
        }
        PaintAppCaption(hwnd, state);
        return 0;
    }
    if (msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE) {
        state->pressed = HTNOWHERE;
        state->hot = HTNOWHERE;
        PaintAppCaption(hwnd, state);
    }
    if (msg == WM_NCMOUSEMOVE) {
        int hot = (int)wp;
        if (state->hot != hot) {
            state->hot = hot;
            hotChanged = true;
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
        TrackMouseEvent(&track);
    }
    if (msg == WM_NCMOUSELEAVE && state->hot != HTNOWHERE) {
        state->hot = HTNOWHERE;
        hotChanged = true;
    }
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == WM_DPICHANGED || msg == WM_SETFONT || msg == WM_SETTINGCHANGE || msg == WM_THEMECHANGED) {
        UpdateAppCaptionMetrics(hwnd, state);
    }
    if (msg == WM_PRINT && (lp & PRF_NONCLIENT)) PaintAppCaption(hwnd, state, (HDC)wp);
    if (hotChanged || msg == WM_NCPAINT || msg == WM_NCACTIVATE || msg == WM_SETTEXT || msg == WM_THEMECHANGED ||
        msg == WM_SETFONT || msg == WM_WINDOWPOSCHANGED) {
        PaintAppCaption(hwnd, state);
    }
    return result;
}

static void MakeAppCaptionOpaque(HWND hwnd) {
    // The scaled GDI caption owns this frame; DWM must not add its own buttons or glass.
    DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    BOOL allowPaint = FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_ALLOW_NCPAINT, &allowPaint, sizeof(allowPaint));
    DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    MARGINS margins{};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    RoundPopupMenu(hwnd);
}

static void ApplyScaledWindowCaption(HWND hwnd) {
    constexpr UINT_PTR kAppCaptionSubclass = 7;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!gSettings || (style & WS_CHILD) || (style & WS_CAPTION) != WS_CAPTION) return;
    WCHAR name[64]{};
    GetClassNameW(hwnd, name, dimof(name));
    if (!WindowBaseFromHwnd(hwnd) && wcscmp(name, L"#32770") != 0 && wcscmp(name, L"SumatraPDFEnhancedLearning") != 0)
        return;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & (WS_EX_LAYERED | WS_EX_TRANSPARENT)) return;
    DWORD_PTR existing = 0;
    if (GetWindowSubclass(hwnd, AppCaptionSubclass, kAppCaptionSubclass, &existing)) {
        MakeAppCaptionOpaque(hwnd);
        UpdateAppCaptionMetrics(hwnd, (ScaledWindowCaption*)existing);
        PaintAppCaption(hwnd, (ScaledWindowCaption*)existing);
        return;
    }
    auto* state = new ScaledWindowCaption();
    state->extraHeight = AppCaptionExtraHeight(hwnd);
    if (!SetWindowSubclass(hwnd, AppCaptionSubclass, kAppCaptionSubclass, (DWORD_PTR)state)) {
        delete state;
        return;
    }
    MakeAppCaptionOpaque(hwnd);
    Rect window = HwndWindowRect(hwnd);
    SetWindowPos(hwnd, nullptr, 0, 0, window.dx, window.dy + state->extraHeight,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
