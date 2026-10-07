/* Copyright 2026 the SumatraPDF Enhanced contributors.
   License: GPLv3. Bundled font assets carry their original SIL OFL 1.1
   licenses; see docs/font-attribution.md and the original OFL notices under docs/licenses. */

#ifndef SUMATRA_ENHANCED_UI_FONTS_H
#define SUMATRA_ENHANCED_UI_FONTS_H

// Header-only to keep existing generated project files unchanged. Memory fonts
// are private to this process and remain registered until Windows tears it down.
struct EnhancedUiFontState {
    INIT_ONCE once[3] = {INIT_ONCE_STATIC_INIT, INIT_ONCE_STATIC_INIT, INIT_ONCE_STATIC_INIT};
    HANDLE handles[6]{};
};

inline EnhancedUiFontState& EnhancedUiFonts() {
    static EnhancedUiFontState state;
    return state;
}

inline BOOL CALLBACK RegisterEnhancedUiFonts(PINIT_ONCE, PVOID parameter, PVOID*) {
    const WCHAR* names[] = {
        L"ENHANCED_FONT_MANROPE_REGULAR",     L"ENHANCED_FONT_MANROPE_SEMIBOLD",   L"ENHANCED_FONT_PRETENDARD_REGULAR",
        L"ENHANCED_FONT_PRETENDARD_SEMIBOLD", L"ENHANCED_FONT_PUBLICSANS_REGULAR", L"ENHANCED_FONT_PUBLICSANS_SEMIBOLD",
    };
    HMODULE module = GetModuleHandleW(nullptr);
    EnhancedUiFontState& state = EnhancedUiFonts();
    int first = (int)(INT_PTR)parameter;
    for (int i = first; i < first + 2; i++) {
        HRSRC resource = FindResourceW(module, names[i], RT_RCDATA);
        if (!resource) {
            continue;
        }
        DWORD bytes = SizeofResource(module, resource);
        HGLOBAL data = LoadResource(module, resource);
        void* font = data ? LockResource(data) : nullptr;
        if (font && bytes) {
            DWORD faces = 0;
            state.handles[i] = AddFontMemResourceEx(font, bytes, nullptr, &faces);
        }
    }
    return TRUE;
}

inline void LoadBundledUiFonts(int first) {
    EnhancedUiFontState& state = EnhancedUiFonts();
    InitOnceExecuteOnce(&state.once[first / 2], RegisterEnhancedUiFonts, (PVOID)(INT_PTR)first, nullptr);
}

// Empty means use the normal Windows system-font fallback. SemiBold selects
// each static face's actual GDI family name rather than synthesizing bold.
inline Str ResolveUiFontName(Str preference, bool semibold = false) {
    int first = -1;
    bool explicitBold = false;
    if (str::EqI(preference, StrL("Manrope")) || str::EqI(preference, StrL("Manrope Semibold"))) {
        first = 0;
        explicitBold = str::EqI(preference, StrL("Manrope Semibold"));
    } else if (str::EqI(preference, StrL("Pretendard Std")) || str::EqI(preference, StrL("Pretendard Std SemiBold"))) {
        first = 2;
        explicitBold = str::EqI(preference, StrL("Pretendard Std SemiBold"));
    } else if (str::EqI(preference, StrL("Public Sans")) || str::EqI(preference, StrL("Public Sans SemiBold"))) {
        first = 4;
        explicitBold = str::EqI(preference, StrL("Public Sans SemiBold"));
    }
    if (first < 0) {
        return str::EqI(preference, StrL("system")) ? Str{} : preference;
    }
    LoadBundledUiFonts(first);
    Str families[] = {StrL("Manrope"),        StrL("Manrope Semibold"),
                      StrL("Pretendard Std"), StrL("Pretendard Std SemiBold"),
                      StrL("Public Sans"),    StrL("Public Sans SemiBold")};
    EnhancedUiFontState& state = EnhancedUiFonts();
    int index = first + ((semibold || explicitBold) ? 1 : 0);
    if (state.handles[index]) {
        return families[index];
    }
    return state.handles[first] ? families[first] : Str{};
}

#endif
