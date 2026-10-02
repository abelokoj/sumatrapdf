/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "base/File.h"
#include "base/Crypto.h"

#include "RegistryPreview.h"
#include "RegistrySearchFilter.h"
#include "ShellProviderRegistry.h"
#include "SumatraLog.h"

#define kThumbnailProviderClsid "{e357fccd-a995-4576-b01f-234630154e96}"
#define kExtractImageClsid "{bb2e617c-0920-11d1-9a0b-00c04fc2d6c1}"
#define kPreviewHandlerClsid "{8895b1c6-b41f-4c1c-a562-0d564250836f}"
#define kAppIdPrevHostExe "{6d2b5079-2f0b-48dd-ab7f-97cec514d30b}"
#define kAppIdPrevHostExeWow64 "{534a1e02-d58f-44f0-b58b-36cbed287c7c}"

#define kRegKeyPreviewHandlers "Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers"

// clang-format off
static struct {
    Str clsid;
    Str ext;
    Str ext2;
    bool skip = false;
} gPreviewers[] = {
    {StrL(kPdfPreviewClsid), StrL(".pdf")},
    {StrL(kCbxPreviewClsid), StrL(".cbz"), StrL(".cbr")},
    {StrL(kCbxPreviewClsid), StrL(".cb7"), StrL(".cbt")},
    {StrL(kTgaPreviewClsid), StrL(".tga")},
    {StrL(kDjVuPreviewClsid), StrL(".djvu")},
    {StrL(kXpsPreviewClsid), StrL(".xps"), StrL(".oxps")},
    {StrL(kEpubPreviewClsid), StrL(".epub")},
    // FictionBook: plain .fb2 and common zip containers (.fb2z, .fbz, .zfb2,
    // .fb2.zip). Multi-dot .fb2.zip needs its own Classes key so Explorer does
    // not treat it as a generic .zip (issue #1677).
    {StrL(kFb2PreviewClsid), StrL(".fb2"), StrL(".fb2z")},
    {StrL(kFb2PreviewClsid), StrL(".fbz"), StrL(".zfb2")},
    {StrL(kFb2PreviewClsid), StrL(".fb2.zip")},
    {StrL(kMobiPreviewClsid), StrL(".mobi")},
};
// clang-format on

bool InstallPreviewDll(Str dllPath, bool allUsers) {
    HKEY hkey = allUsers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    bool ok;

    for (auto& prev : gPreviewers) {
        if (prev.skip) {
            continue;
        }
        Str clsid = prev.clsid;
        Str ext = prev.ext;
        Str ext2 = prev.ext2;
        ok = true;

        TempStr displayName = fmt("SumatraPDF Enhanced Preview (*%s)", ext);
        // register class
        TempStr key = fmt("Software\\Classes\\CLSID\\%s", clsid);
        if (!ShellClassAvailable(hkey, key)) return false;
        ok &= LoggedWriteRegStr(hkey, key, {}, displayName);
        ok &= LoggedWriteRegStr(hkey, key, StrL("EnhancedOwner"), StrL("SumatraPDF Enhanced"));
        ok &= LoggedWriteRegStr(hkey, key, StrL("AppId"),
                                IsRunningInWow64() ? StrL(kAppIdPrevHostExeWow64) : StrL(kAppIdPrevHostExe));
        ok &= LoggedWriteRegStr(hkey, key, StrL("DisplayName"), displayName);
        key = fmt("Software\\Classes\\CLSID\\%s\\InProcServer32", clsid);
        ok &= LoggedWriteRegStr(hkey, key, {}, dllPath);
        ok &= LoggedWriteRegStr(hkey, key, StrL("ThreadingModel"), StrL("Apartment"));
        // IThumbnailProvider
        key = fmt("Software\\Classes\\%s\\shellex\\" kThumbnailProviderClsid, ext);
        ok &= ClaimShellProvider(hkey, key, clsid, HKEY_CLASSES_ROOT, Str(key.s + 17, len(key) - 17));
        if (ext2) {
            key = fmt("Software\\Classes\\%s\\shellex\\" kThumbnailProviderClsid, ext2);
            ok &= ClaimShellProvider(hkey, key, clsid, HKEY_CLASSES_ROOT, Str(key.s + 17, len(key) - 17));
        }
        // IPreviewHandler
        key = fmt("Software\\Classes\\%s\\shellex\\" kPreviewHandlerClsid, ext);
        ok &= ClaimShellProvider(hkey, key, clsid, HKEY_CLASSES_ROOT, Str(key.s + 17, len(key) - 17));
        if (ext2) {
            key = fmt("Software\\Classes\\%s\\shellex\\" kPreviewHandlerClsid, ext2);
            ok &= ClaimShellProvider(hkey, key, clsid, HKEY_CLASSES_ROOT, Str(key.s + 17, len(key) - 17));
        }
        ok &= LoggedWriteRegStr(hkey, StrL(kRegKeyPreviewHandlers), clsid, displayName);
        if (!ok) {
            return false;
        }
    }

    return true;
}

bool UninstallPreviewDll(Str dllPath) {
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (auto& prev : gPreviewers) {
            if (prev.skip) continue;
            TempStr classKey = fmt("Software\\Classes\\CLSID\\%s", prev.clsid);
            if (!OwnShellClass(root, classKey, dllPath)) continue;
            Str exts[] = {prev.ext, prev.ext2};
            for (Str ext : exts) {
                if (len(ext) == 0) continue;
                RemoveShellProvider(root, fmt("Software\\Classes\\%s\\shellex\\" kThumbnailProviderClsid, ext),
                                    prev.clsid);
                RemoveShellProvider(root, fmt("Software\\Classes\\%s\\shellex\\" kPreviewHandlerClsid, ext),
                                    prev.clsid);
            }
            DeleteRegValue(root, StrL(kRegKeyPreviewHandlers), prev.clsid);
        }
        for (auto& prev : gPreviewers) {
            if (prev.skip) continue;
            TempStr classKey = fmt("Software\\Classes\\CLSID\\%s", prev.clsid);
            if (OwnShellClass(root, classKey, dllPath)) LoggedDeleteRegKey(root, classKey);
        }
    }
    return true;
}

// TODO: is anyone using this functionality?
void DisablePreviewInstallExts(Str cmdLine) {
    // allows installing only a subset of available preview handlers
    if (str::TrimPrefixI(cmdLine, StrL("exts:"))) {
        TempStr extsList = str::DupTemp(cmdLine);
        str::ToLowerInPlace(extsList);
        str::TransCharsInPlace(extsList, StrL(";. :"), StrL(",,,\0"));
        StrVec exts;
        Split(&exts, extsList, StrL(","), true);
        for (auto& p : gPreviewers) {
            Str extNoDot = Str(p.ext.s + 1, p.ext.len - 1);
            p.skip = !exts.Contains(extNoDot);
        }
    }
}

bool IsPreviewInstalled() {
    Str key = StrL(".pdf\\shellex\\{8895b1c6-b41f-4c1c-a562-0d564250836f}");
    TempStr iid = LoggedReadRegStrTemp(HKEY_CLASSES_ROOT, key, {});
    bool isInstalled = str::EqI(iid, StrL(kPdfPreviewClsid));
    logf("IsPreviewInstalled() isInstalled=%d\n", (int)isInstalled);
    return isInstalled;
}

// --- opt-in PdfPreview.dll file logging ---------------------------------------

#define kRegKeySumatra "Software\\SumatraPDF Enhanced"
#define kRegValLogPdfPreview "LogPdfPreview"

bool IsPdfPreviewLoggingEnabled() {
    DWORD val = 0;
    if (!ReadRegDWORD(HKEY_CURRENT_USER, StrL(kRegKeySumatra), StrL(kRegValLogPdfPreview), val)) {
        return false;
    }
    return val != 0;
}

void SetPdfPreviewLoggingEnabled(bool enable) {
    WriteRegDWORD(HKEY_CURRENT_USER, StrL(kRegKeySumatra), StrL(kRegValLogPdfPreview), enable ? 1 : 0);
    logf("SetPdfPreviewLoggingEnabled: %d\n", (int)enable);
}

// Per-build data dir, keyed on the sha1 of the SumatraPDF.exe sitting next to
// this module: in SumatraPDF.exe that's the running exe, in PdfPreview.dll it's
// the sibling exe -- either way it resolves to the same directory SumatraPDF.exe
// uses (see GetSumatraBuildSpecificDirTemp), so logs land next to its crashinfo/logs.
// per-build data dir, same one SumatraPDF.exe uses (...\SumatraPDF-data\<sha1>)
TempStr GetPdfPreviewLogDirTemp() {
    TempStr exeDir = GetSelfExeDirTemp();
    if (len(exeDir) == 0) {
        return {};
    }
    TempStr exePath = path::JoinTemp(exeDir, StrL("SumatraPDFEnhanced.exe"));
    Str d = file::ReadFile(exePath);
    if (len(d) == 0) {
        return {};
    }
    u8 sha1[20]{};
    CalcSHA1Digest(d, sha1);
    str::Free(d);
    char id[7];
    for (int i = 0; i < 3; i++) { // first 6 hex chars (3 bytes), matches GetSumatraBuildSpecificDirTemp
        sprintf_s(&id[(size_t)2 * i], 3, "%02x", sha1[i]);
    }
    TempStr local = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA, false);
    if (len(local) == 0) {
        return {};
    }
    TempStr dir = path::JoinTemp(local, StrL("SumatraPDF Enhanced-data"));
    return path::JoinTemp(dir, Str(id));
}

// pdfpreview.log.<month>-<day>.<hour>-<minute>.<unique>.txt
static TempStr GetNewPdfPreviewLogFilePathTemp() {
    TempStr dir = GetPdfPreviewLogDirTemp();
    if (len(dir) == 0) {
        return {};
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    // unique part: pid plus low bits of tick, so concurrent preview hosts that
    // start in the same minute don't collide
    DWORD uniq = (GetCurrentProcessId() << 16) ^ (DWORD)(GetTickCount64() & 0xffff);
    TempStr name = fmt("%s%02d-%02d.%02d-%02d.%08x.txt", Str(kPdfPreviewLogPrefix), (int)st.wMonth, (int)st.wDay,
                       (int)st.wHour, (int)st.wMinute, uniq);
    return path::JoinTemp(dir, name);
}

// if logging is enabled, route this module's log to a fresh unique file
void StartPdfPreviewLoggingIfEnabled() {
    static bool started = false;
    if (started || !IsPdfPreviewLoggingEnabled()) {
        return;
    }
    started = true;
    TempStr path = GetNewPdfPreviewLogFilePathTemp();
    if (len(path) == 0) {
        return;
    }
    // WriteCurrentLogToFile creates the directory and flushes whatever we've
    // already buffered (e.g. DllMain); StartLogToFile appends subsequent lines.
    WriteCurrentLogToFile(path);
    StartLogToFile(path, false);
    logf("PdfPreview: logging to '%s'\n", path);
}

#if IS_DEBUG
bool RegistryProviders_UnitTests() {
    TempStr key = fmt("Software\\SumatraPDF Enhanced\\ProviderTests-%d-%d", GetCurrentProcessId(), (int)GetTickCount());
    HKEY test = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CWStrTemp(key), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &test, nullptr) !=
        ERROR_SUCCESS)
        return false;
    Str selected = StrL("Selected\\.pdf\\shellex");
    Str inherited = StrL("Inherited\\.pdf\\shellex");
    Str ours = StrL(kPdfPreviewClsid);
    Str official = StrL("{3D3B1846-CC43-42AE-BFF9-D914083C2BA3}");
    bool ok = !str::EqI(ours, official);
    ok &= !str::EqI(StrL(kPdfFilterClsid), StrL("{55808EA8-81FE-43c6-AAE8-1D8149F941D3}"));
    ok &= !str::EqI(StrL(kPdfFilterHandler), StrL("{26CA6565-F22A-4f5e-B688-0AD051D56E96}"));
    Str persistent = StrL("Selected\\.pdf\\PersistentHandler");
    ok &= WriteRegStr(test, persistent, {}, StrL("{26CA6565-F22A-4f5e-B688-0AD051D56E96}"));
    ok &= ClaimShellProvider(test, persistent, StrL(kPdfFilterHandler), test, persistent);
    RemoveShellProvider(test, persistent, StrL(kPdfFilterHandler));
    ok &= str::EqI(ReadRegStrTemp(test, persistent, {}), StrL("{26CA6565-F22A-4f5e-B688-0AD051D56E96}"));
    ok &= ClaimShellProvider(test, selected, ours, test, inherited);
    ok &= str::EqI(ReadRegStrTemp(test, selected, {}), ours);
    ok &= WriteRegStr(test, selected, StrL("Unrelated"), StrL("keep"));
    RemoveShellProvider(test, selected, official);
    ok &= str::EqI(ReadRegStrTemp(test, selected, {}), ours);
    RemoveShellProvider(test, selected, ours);
    ok &= len(ReadRegStrTemp(test, selected, {})) == 0;
    ok &= str::EqI(ReadRegStrTemp(test, selected, StrL("Unrelated")), StrL("keep"));
    ok &= WriteRegStr(test, inherited, {}, official);
    ok &= ClaimShellProvider(test, selected, ours, test, inherited);
    ok &= len(ReadRegStrTemp(test, selected, {})) == 0;
    ok &= WriteRegStr(test, selected, {}, official);
    ok &= ClaimShellProvider(test, selected, ours, test, selected);
    RemoveShellProvider(test, selected, ours);
    ok &= str::EqI(ReadRegStrTemp(test, selected, {}), official);
    DWORD val = 123;
    HKEY malformed = nullptr;
    RegCreateKeyExW(test, L"Malformed", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &malformed, nullptr);
    if (malformed) {
        RegSetValueExW(malformed, nullptr, 0, REG_DWORD, (BYTE*)&val, sizeof(val));
        RegCloseKey(malformed);
        ok &= !ShellProviderAvailable(test, StrL("Malformed"), ours);
    } else
        ok = false;
    Str cls = StrL("CLSID\\Test");
    ok &= ShellClassAvailable(test, cls);
    ok &= WriteRegStr(test, cls, StrL("EnhancedOwner"), StrL("SumatraPDF Enhanced"));
    ok &= WriteRegStr(test, StrL("CLSID\\Test\\InProcServer32"), {}, StrL("C:\\Enhanced\\PdfPreview.dll"));
    ok &= OwnShellClass(test, cls, StrL("C:\\Enhanced\\PdfPreview.dll"));
    ok &= !OwnShellClass(test, cls, StrL("C:\\Other\\PdfPreview.dll"));
    RegCloseKey(test);
    RegDeleteTreeW(HKEY_CURRENT_USER, CWStrTemp(key));
    return ok;
}
#endif
