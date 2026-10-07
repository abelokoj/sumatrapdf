/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

extern void (*gUiInstallScrollbar)(HWND hwnd);
extern int (*gUiScrollbarWidth)(int dpi);
extern int (*gUiCornerRadius)(int dpi, int designRadius);

struct ScrollMenuItem {
    WStr label;
    UINT command = 0;
    HMENU submenu = nullptr;
    bool enabled = true;
    bool checked = false;
    bool separator = false;
    bool literal = false;
};

struct ScrollMenu {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    PlatformFont* font = nullptr;
    Vec<ScrollMenuItem> items;
    Color background = 0;
    Color foreground = 0;
    Color hotBackground = 0;
    HBRUSH brush = nullptr;
    int rowDy = 0;
    int padding = 0;
    int width = 0;
    int height = 0;
    int picked = -1;
    bool rtl = false;
    bool done = false;

    ~ScrollMenu() {
        if (hwnd) DestroyWindow(hwnd);
        for (auto& item : items) wstr::Free(item.label);
        if (brush) DeleteObject(brush);
    }

    void Pick(int index) {
        if (index < 0 || index >= len(items) || !items[index].enabled || items[index].separator) return;
        picked = index;
        done = true;
    }
};

static void MeasureScrollMenu(ScrollMenu& popup, HWND owner, HMENU menu, int visibleRows, int maxWidth) {
    HDC dc = GetDC(owner);
    HGDIOBJ oldFont = SelectObject(dc, popup.font->GetHFont());
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    popup.padding = std::max(4, (int)metrics.tmHeight / 3);
    popup.rowDy = metrics.tmHeight + popup.padding * 2;
    int longest = 0;
    for (int i = 0; i < GetMenuItemCount(menu); i++) {
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_STRING | MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_FTYPE;
        GetMenuItemInfoW(menu, i, TRUE, &info);
        auto label = (WCHAR*)AllocZero(info.cch + 1, sizeof(WCHAR));
        info.dwTypeData = label;
        info.cch++;
        GetMenuItemInfoW(menu, i, TRUE, &info);
        ScrollMenuItem item;
        item.label = WStr(label);
        item.command = info.wID;
        item.submenu = info.hSubMenu;
        item.enabled = !(info.fState & (MFS_DISABLED | MFS_GRAYED));
        item.checked = (info.fState & MFS_CHECKED) != 0;
        item.separator = (info.fType & MFT_SEPARATOR) != 0;
        item.literal = (info.fType & MFT_OWNERDRAW) != 0;
        VecAppend(popup.items, item);
        SIZE size{};
        GetTextExtentPoint32W(dc, label, len(item.label), &size);
        longest = std::max(longest, (int)size.cx);
    }
    SelectObject(dc, oldFont);
    ReleaseDC(owner, dc);
    int dpi = DpiGetForHwnd(owner);
    int scrollbarDx = gUiScrollbarWidth ? gUiScrollbarWidth(dpi) : DpiScaleByDpi(dpi, 17);
    int rows = std::min(len(popup.items), std::clamp(visibleRows, 1, 50));
    popup.width = std::min(maxWidth, longest + popup.rowDy * 2 + popup.padding * 2 + scrollbarDx + 4);
    popup.width = std::max(popup.width, popup.rowDy * 4);
    popup.height = rows * popup.rowDy + 4;
}

static void PaintScrollMenuItem(ScrollMenu& popup, DRAWITEMSTRUCT* draw) {
    if (draw->itemID >= (UINT)len(popup.items)) return;
    auto& item = popup.items[draw->itemID];
    bool hot = item.enabled && (draw->itemState & ODS_SELECTED);
    Color bg = hot ? popup.hotBackground : popup.background;
    HdcFillRect(draw->hDC, ToRect(draw->rcItem), bg);
    if (item.separator) {
        Rect line = ToRect(draw->rcItem);
        line.x += popup.padding;
        line.dx -= popup.padding * 2;
        line.y += line.dy / 2;
        line.dy = 1;
        HdcFillRect(draw->hDC, line, AccentColor(bg, 40));
        return;
    }
    HDC dc = draw->hDC;
    HGDIOBJ oldFont = SelectObject(dc, popup.font->GetHFont());
    int oldMode = SetBkMode(dc, TRANSPARENT);
    COLORREF oldColor = SetTextColor(dc, item.enabled ? popup.foreground : AccentColor(popup.background, 100));
    RECT text = draw->rcItem;
    text.left += popup.rowDy;
    text.right -= popup.rowDy;
    UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    if (item.literal) flags |= DT_NOPREFIX;
    flags |= popup.rtl ? DT_RIGHT | DT_RTLREADING : DT_LEFT;
    DrawTextW(dc, item.label.s, len(item.label), &text, flags);
    RECT glyph = draw->rcItem;
    if (popup.rtl)
        glyph.left = glyph.right - popup.rowDy;
    else
        glyph.right = glyph.left + popup.rowDy;
    if (item.checked) {
        SetTextColor(dc, IsLightColor(bg) ? RGB(0, 112, 64) : RGB(109, 233, 165));
        DrawTextW(dc, L"\u2713", 1, &glyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    if (item.submenu) {
        glyph = draw->rcItem;
        if (popup.rtl)
            glyph.right = glyph.left + popup.rowDy;
        else
            glyph.left = glyph.right - popup.rowDy;
        SetTextColor(dc, popup.foreground);
        DrawTextW(dc, popup.rtl ? L"\u2039" : L"\u203a", 1, &glyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    if (draw->itemState & ODS_FOCUS) DrawFocusRect(dc, &text);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    SelectObject(dc, oldFont);
}

static LRESULT CALLBACK ScrollMenuListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* popup = (ScrollMenu*)data;
    if (msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE || wp == (popup->rtl ? VK_RIGHT : VK_LEFT)) {
            popup->done = true;
            return 0;
        }
        if (wp == VK_RETURN || wp == (popup->rtl ? VK_LEFT : VK_RIGHT)) {
            popup->Pick((int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0));
            return 0;
        }
    }
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == WM_LBUTTONUP) {
        DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp);
        if (!HIWORD(hit)) popup->Pick(LOWORD(hit));
    }
    return result;
}

static LRESULT CALLBACK ScrollMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* popup = (ScrollMenu*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        popup = (ScrollMenu*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)popup);
    }
    if (!popup) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
        case WM_DRAWITEM:
            PaintScrollMenuItem(*popup, (DRAWITEMSTRUCT*)lp);
            return TRUE;
        case WM_CTLCOLORLISTBOX:
            SetBkColor((HDC)wp, popup->background);
            return (LRESULT)popup->brush;
        case WM_ERASEBKGND:
            HdcFillRect((HDC)wp, HwndClientRect(hwnd), AccentColor(popup->background, 35));
            return TRUE;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) popup->done = true;
            break;
        case WM_CLOSE:
            popup->done = true;
            return 0;
        case WM_DESTROY:
            popup->hwnd = nullptr;
            popup->done = true;
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool CreateScrollMenu(ScrollMenu& popup, HWND owner, Rect bounds) {
    const WCHAR* name = L"SumatraScrollableMenu";
    WNDCLASSW cls{};
    cls.lpfnWndProc = ScrollMenuProc;
    cls.hInstance = GetInstance();
    cls.lpszClassName = name;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    popup.brush = CreateSolidBrush(popup.background);
    popup.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, name, L"", WS_POPUP, bounds.x, bounds.y, bounds.dx, bounds.dy, owner,
                                 nullptr, GetInstance(), &popup);
    if (!popup.hwnd) return false;
    DWORD style =
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;
    popup.list = CreateWindowExW(popup.rtl ? WS_EX_RTLREADING : 0, L"LISTBOX", L"", style, 2, 2, bounds.dx - 4,
                                 bounds.dy - 4, popup.hwnd, (HMENU)1, GetInstance(), nullptr);
    if (!popup.list) return false;
    SendMessageW(popup.list, WM_SETFONT, (WPARAM)popup.font->GetHFont(), FALSE);
    SendMessageW(popup.list, LB_SETITEMHEIGHT, 0, popup.rowDy);
    for (auto& item : popup.items) SendMessageW(popup.list, LB_ADDSTRING, 0, (LPARAM)item.label.s);
    if (gUiInstallScrollbar) gUiInstallScrollbar(popup.list);
    SetWindowSubclass(popup.list, ScrollMenuListProc, 1, (DWORD_PTR)&popup);
    int radius = gUiCornerRadius ? gUiCornerRadius(DpiGetForHwnd(owner), 6) : DpiScaleByDpi(DpiGetForHwnd(owner), 6);
    HRGN region = CreateRoundRectRgn(0, 0, bounds.dx + 1, bounds.dy + 1, radius * 2, radius * 2);
    if (region && !SetWindowRgn(popup.hwnd, region, FALSE)) DeleteObject(region);
    for (int i = 0; i < len(popup.items); i++) {
        if (popup.items[i].enabled && !popup.items[i].separator) {
            SendMessageW(popup.list, LB_SETCURSEL, i, 0);
            break;
        }
    }
    return true;
}

static bool ScrollMenuContainsWindow(HWND popup, HWND target) {
    // Overlay scrollbars are owned top-level windows, not child controls.
    for (HWND window = target; window; window = GetWindow(window, GW_OWNER)) {
        if (window == popup || IsChild(popup, window)) return true;
    }
    return false;
}

static UINT TrackScrollMenu(HWND owner, HMENU menu, Point anchor, PlatformFont* font, int visibleRows, int maxWidth,
                            Color background, Color foreground, Color hotBackground, bool rtl,
                            bool rightAlign = false) {
    ScrollMenu popup;
    popup.font = font;
    popup.background = background;
    popup.foreground = foreground;
    popup.hotBackground = hotBackground;
    popup.rtl = rtl;
    MeasureScrollMenu(popup, owner, menu, visibleRows, maxWidth);
    if (len(popup.items) == 0) return 0;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint({anchor.x, anchor.y}, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT work = monitor.rcWork;
    int rows = std::max(1, ((int)(work.bottom - work.top) - 4) / popup.rowDy);
    popup.height = std::min(popup.height, rows * popup.rowDy + 4);
    popup.width = std::min(popup.width, (int)(work.right - work.left));
    int x = rightAlign ? anchor.x - popup.width : anchor.x;
    x = std::clamp(x, (int)work.left, (int)work.right - popup.width);
    int y = std::clamp(anchor.y, (int)work.top, (int)work.bottom - popup.height);
    if (!CreateScrollMenu(popup, owner, {x, y, popup.width, popup.height})) return 0;
    HWND oldFocus = GetFocus();
    ShowWindow(popup.hwnd, SW_SHOW);
    SetFocus(popup.list);
    MSG message{};
    bool quit = false;
    while (!popup.done && IsWindow(owner)) {
        int result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            quit = result == 0;
            break;
        }
        bool mouseDown = message.message == WM_LBUTTONDOWN || message.message == WM_RBUTTONDOWN ||
                         message.message == WM_MBUTTONDOWN || message.message == WM_NCLBUTTONDOWN;
        if (mouseDown && !ScrollMenuContainsWindow(popup.hwnd, message.hwnd)) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    POINT nextAnchor{x + popup.width, y};
    if (popup.picked >= 0) {
        RECT row{};
        SendMessageW(popup.list, LB_GETITEMRECT, popup.picked, (LPARAM)&row);
        nextAnchor.y += row.top;
    }
    DestroyWindow(popup.hwnd);
    popup.hwnd = nullptr;
    if (IsWindow(oldFocus)) SetFocus(oldFocus);
    if (quit) PostQuitMessage((int)message.wParam);
    if (popup.picked < 0 || !IsWindow(owner)) return 0;
    auto& item = popup.items[popup.picked];
    if (!item.submenu) return item.command;
    return TrackScrollMenu(owner, item.submenu, {rtl ? x : nextAnchor.x, nextAnchor.y}, font, visibleRows, maxWidth,
                           background, foreground, hotBackground, rtl, rtl);
}
