/* Copyright 2026 the SumatraPDF Enhanced authors. License: GPLv3 */

constexpr DWORD kWinPointerTouch = 2;
constexpr DWORD kWinPointerPen = 3;
constexpr DWORD kWinPointerMouse = 4;
constexpr UINT32 kWinPointerContact = 0x0004;
constexpr int kWinPointerButtonShift = 4;
constexpr UINT32 kPointerButtonFirst = 0x01;
constexpr UINT32 kPointerButtonSecond = 0x02;
constexpr UINT32 kPointerButtonThird = 0x04;
constexpr UINT32 kPointerButtonFourth = 0x08;
constexpr UINT32 kPointerButtonFifth = 0x10;
constexpr UINT32 kPointerButtonMask = 0x1f;
constexpr UINT32 kWinPenBarrel = 0x01;
constexpr UINT32 kWinPenInverted = 0x02;
constexpr UINT32 kWinPenEraser = 0x04;
constexpr UINT32 kWinPenPressureMask = 0x01;
constexpr UINT32 kWinPenRotationMask = 0x02;
constexpr UINT32 kWinPenTiltXMask = 0x04;
constexpr UINT32 kWinPenTiltYMask = 0x08;
constexpr UINT32 kWinPenPressureMax = 1024;
constexpr UINT32 kWinPenRotationMax = 359;
constexpr int kWinPenTiltMax = 90;
constexpr UINT32 kWinPenHistoryCapacity = 128;

enum class PointerDevice {
    Unknown,
    Mouse,
    Touch,
    Pen
};
struct PointerSample {
    PointerDevice device = PointerDevice::Unknown;
    UINT32 id = 0;
    Point screen;
    DWORD time = 0;
    UINT64 performanceTime = 0;
    float pressure = 0;
    int tiltX = 0;
    int tiltY = 0;
    UINT32 rotation = 0;
    UINT32 buttons = 0;
    bool contact = false;
    bool eraser = false;
    bool barrel = false;
    bool pressureValid = false;
    bool tiltXValid = false;
    bool tiltYValid = false;
    bool rotationValid = false;
};

// Win8 ABI declarations allow dynamic loading with the older Windows build target.
struct WinPointerInfo {
    DWORD pointerType;
    UINT32 pointerId, frameId, pointerFlags;
    HANDLE sourceDevice;
    HWND hwndTarget;
    POINT ptPixelLocation, ptHimetricLocation, ptPixelLocationRaw, ptHimetricLocationRaw;
    DWORD time;
    UINT32 historyCount;
    INT32 inputData;
    DWORD keyStates;
    UINT64 performanceCount;
    DWORD buttonChangeType;
};
struct WinPointerPenInfo {
    WinPointerInfo pointerInfo;
    UINT32 penFlags, penMask, pressure, rotation;
    INT32 tiltX, tiltY;
};

inline PointerSample NormalizePointer(const WinPointerInfo& info, const WinPointerPenInfo* pen = nullptr) {
    PointerSample s;
    s.device = info.pointerType == kWinPointerTouch   ? PointerDevice::Touch
               : info.pointerType == kWinPointerPen   ? PointerDevice::Pen
               : info.pointerType == kWinPointerMouse ? PointerDevice::Mouse
                                                      : PointerDevice::Unknown;
    s.id = info.pointerId;
    s.screen = {info.ptPixelLocation.x, info.ptPixelLocation.y};
    s.time = info.time;
    s.performanceTime = info.performanceCount;
    s.contact = (info.pointerFlags & kWinPointerContact) != 0;
    s.buttons = (info.pointerFlags >> kWinPointerButtonShift) & kPointerButtonMask;
    if (!pen || s.device != PointerDevice::Pen) return s;
    s.eraser = (pen->penFlags & (kWinPenInverted | kWinPenEraser)) != 0;
    s.barrel = (pen->penFlags & kWinPenBarrel) != 0;
    s.pressureValid = (pen->penMask & kWinPenPressureMask) != 0;
    s.rotationValid = (pen->penMask & kWinPenRotationMask) != 0;
    s.tiltXValid = (pen->penMask & kWinPenTiltXMask) != 0;
    s.tiltYValid = (pen->penMask & kWinPenTiltYMask) != 0;
    if (s.pressureValid) s.pressure = (float)std::min(pen->pressure, kWinPenPressureMax) / (float)kWinPenPressureMax;
    if (s.rotationValid) s.rotation = std::min(pen->rotation, kWinPenRotationMax);
    if (s.tiltXValid) s.tiltX = limitValue((int)pen->tiltX, -kWinPenTiltMax, kWinPenTiltMax);
    if (s.tiltYValid) s.tiltY = limitValue((int)pen->tiltY, -kWinPenTiltMax, kWinPenTiltMax);
    return s;
}

inline PointerSample MousePointerSample(Point screen, WPARAM buttons, DWORD time) {
    PointerSample s;
    s.device = PointerDevice::Mouse;
    s.screen = screen;
    s.time = time;
    s.contact = (buttons & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)) != 0;
    s.buttons =
        ((buttons & MK_LBUTTON) ? kPointerButtonFirst : 0) | ((buttons & MK_RBUTTON) ? kPointerButtonSecond : 0) |
        ((buttons & MK_MBUTTON) ? kPointerButtonThird : 0) | ((buttons & MK_XBUTTON1) ? kPointerButtonFourth : 0) |
        ((buttons & MK_XBUTTON2) ? kPointerButtonFifth : 0);
    return s;
}

struct WindowsPointerApi {
    typedef BOOL(WINAPI* GetType)(UINT32, DWORD*);
    typedef BOOL(WINAPI* GetInfo)(UINT32, WinPointerInfo*);
    typedef BOOL(WINAPI* GetPen)(UINT32, WinPointerPenInfo*);
    typedef BOOL(WINAPI* GetHistory)(UINT32, UINT32*, WinPointerPenInfo*);
    GetType type = nullptr;
    GetInfo info = nullptr;
    GetPen pen = nullptr;
    GetHistory history = nullptr;
    WindowsPointerApi() {
        HMODULE h = GetModuleHandleW(L"user32.dll");
        if (!h) return;
        type = (GetType)GetProcAddress(h, "GetPointerType");
        info = (GetInfo)GetProcAddress(h, "GetPointerInfo");
        pen = (GetPen)GetProcAddress(h, "GetPointerPenInfo");
        history = (GetHistory)GetProcAddress(h, "GetPointerPenInfoHistory");
    }
};
inline WindowsPointerApi& PointerApi() {
    static WindowsPointerApi api;
    return api;
}
inline void NormalizePenHistory(const WinPointerPenInfo* history, int count, Vec<PointerSample>& out) {
    // Windows returns newest first; drawing consumes chronological samples.
    for (int i = count - 1; i >= 0; i--) VecAppend(out, NormalizePointer(history[i].pointerInfo, &history[i]));
}
inline bool ReadPointerSample(UINT32 id, PointerSample& sample) {
    auto& api = PointerApi();
    DWORD type = 0;
    if (!api.type || !api.type(id, &type)) return false;
    WinPointerPenInfo pen{};
    if (type == kWinPointerPen && api.pen && api.pen(id, &pen)) {
        sample = NormalizePointer(pen.pointerInfo, &pen);
        return true;
    }
    WinPointerInfo info{};
    if (api.info && api.info(id, &info)) {
        sample = NormalizePointer(info);
        return true;
    }
    sample = {};
    sample.id = id;
    sample.device = type == kWinPointerTouch   ? PointerDevice::Touch
                    : type == kWinPointerPen   ? PointerDevice::Pen
                    : type == kWinPointerMouse ? PointerDevice::Mouse
                                               : PointerDevice::Unknown;
    return true;
}
inline void ReadPenHistory(UINT32 id, Vec<PointerSample>& out) {
    auto& api = PointerApi();
    if (!api.history) return;
    constexpr UINT32 capacity = kWinPenHistoryCapacity;
    WinPointerPenInfo history[capacity]{};
    UINT32 count = capacity;
    if (api.history(id, &count, history)) NormalizePenHistory(history, (int)std::min(count, capacity), out);
}

#if IS_DEBUG
inline bool PointerInputTests() {
    WinPointerPenInfo pen{};
    pen.pointerInfo.pointerType = kWinPointerPen;
    pen.pointerInfo.pointerId = 7;
    pen.pointerInfo.pointerFlags =
        kWinPointerContact | ((kPointerButtonFirst | kPointerButtonSecond) << kWinPointerButtonShift);
    pen.pointerInfo.time = 100;
    pen.pointerInfo.performanceCount = 123456;
    pen.pointerInfo.ptPixelLocation = {-10, 20};
    pen.penMask = kWinPenPressureMask | kWinPenRotationMask | kWinPenTiltXMask | kWinPenTiltYMask;
    pen.penFlags = kWinPenBarrel | kWinPenInverted | kWinPenEraser;
    pen.pressure = 2048;
    pen.tiltX = -100;
    pen.tiltY = 100;
    pen.rotation = 400;
    PointerSample s = NormalizePointer(pen.pointerInfo, &pen);
    bool ok = s.device == PointerDevice::Pen && s.id == 7 && s.screen == Point{-10, 20} && s.contact &&
              s.buttons == (kPointerButtonFirst | kPointerButtonSecond) && s.eraser && s.barrel;
    ok &= s.pressureValid && s.pressure == 1.f && s.tiltX == -kWinPenTiltMax && s.tiltY == kWinPenTiltMax &&
          s.rotation == kWinPenRotationMax && s.performanceTime == 123456;
    pen.penMask = 0;
    s = NormalizePointer(pen.pointerInfo, &pen);
    ok &= !s.pressureValid && !s.tiltXValid && !s.tiltYValid && !s.rotationValid && s.pressure == 0;
    pen.penMask = kWinPenPressureMask;
    pen.pressure = 512;
    s = NormalizePointer(pen.pointerInfo, &pen);
    ok &= s.pressure == 0.5f;
    pen.pressure = 0;
    s = NormalizePointer(pen.pointerInfo, &pen);
    ok &= s.pressureValid && s.pressure == 0;
    pen.pointerInfo.pointerType = kWinPointerTouch;
    s = NormalizePointer(pen.pointerInfo, &pen);
    ok &= s.device == PointerDevice::Touch && !s.pressureValid && !s.eraser;
    PointerSample mouse = MousePointerSample({1, 2}, MK_LBUTTON | MK_RBUTTON, MAXDWORD);
    ok &= mouse.device == PointerDevice::Mouse && mouse.contact &&
          mouse.buttons == (kPointerButtonFirst | kPointerButtonSecond) && !mouse.pressureValid &&
          mouse.time == MAXDWORD;
    WinPointerPenInfo history[2]{};
    history[0].pointerInfo.time = 20;
    history[1].pointerInfo.time = 10;
    Vec<PointerSample> samples;
    NormalizePenHistory(history, 2, samples);
    ok &= len(samples) == 2 && samples[0].time == 10 && samples[1].time == 20;
    return ok;
}
#endif
