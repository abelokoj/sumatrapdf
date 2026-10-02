/* Copyright 2024 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/WinDynCalls.h"
#include "base/DbgHelpDyn.h"
#include "base/File.h"
#include "base/DirScan.h"
#include "base/Win.h"
#include "base/Crypto.h"

#include "gui/UIModels.h"

#include "SumatraConfig.h"
#include "Translations.h"
#include "Version.h"
#include "Installer.h"
#include "AppTools.h"

/* Returns true, if a Registry entry indicates that this executable has been
   created by an installer (and should be updated through an installer) */
static bool HasBeenInstalled() {
    // see GetDefaultInstallationDir() in Installer.cpp
    TempStr regPathUninst =
        str::JoinTemp(StrL("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"), StrL(kEnhancedAppName));
    TempStr installedPath = LoggedReadRegStr2Temp(regPathUninst, StrL("InstallLocation"));
    if (len(installedPath) == 0) {
        return false;
    }

    TempStr exePath = GetSelfExePathTemp();
    if (!str::EndsWithI(installedPath, StrL(".exe"))) {
        installedPath = path::JoinTemp(installedPath, path::GetBaseNameTemp(exePath));
    }
    return path::IsSame(installedPath, exePath);
}

static bool PathStripBaseNameInPlace(Str& path) {
    if (!path.s) {
        return false;
    }
    TempStr base = path::GetBaseNameTemp(path);
    if (base.s > path.s) {
        base.s[-1] = 0;
        path.len = (int)(base.s - path.s - 1);
        return true;
    }
    return false;
}

// return true if path is in a given dir, even if dir is a junction etc.
static bool IsPathInDirSmart(Str path, Str dir) {
    TempStr work = str::DupTemp(path);
    Str p = work;
    while (p) {
        if (path::IsSame(dir, p)) {
            return true;
        }
        if (!PathStripBaseNameInPlace(p)) {
            break;
        }
    }
    return false;
}

static bool IsExeInProgramFiles() {
    TempStr exePath = GetSelfExePathTemp();
    TempStr dir = GetSpecialFolderTemp(CSIDL_PROGRAM_FILES);
    if (IsPathInDirSmart(exePath, dir)) {
        return true;
    }
    dir = GetSpecialFolderTemp(CSIDL_PROGRAM_FILESX86);
    if (IsPathInDirSmart(exePath, dir)) {
        return true;
    }
    return false;
}

/* Return false if this program has been started from "Program Files" directory
   (which is an indicator that it has been installed) or from the last known
   location of a SumatraPDF installation: */
bool IsRunningInPortableMode() {
    // cache the result so that it will be consistent during the lifetime of the process
    static int sCacheIsPortable = -1; // -1 == uninitialized, 0 == installed, 1 == portable
    if (sCacheIsPortable != -1) {
        return sCacheIsPortable != 0;
    }

    sCacheIsPortable = 0;
    if (gIsStoreBuild) {
        return false;
    }

    if (HasBeenInstalled()) {
        return false;
    }

    if (!IsExeInProgramFiles()) {
        sCacheIsPortable = 1;
    }
    return sCacheIsPortable != 0;
}

bool IsDllBuild() {
    HRSRC resSrc = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), RT_RCDATA);
    return resSrc != nullptr;
}

// true if the executable name indicates installer or uninstaller mode
// (e.g. SumatraPDF-prerel-64-install.exe)
bool IsInstallerOrUninstallerExe() {
    TempStr exeName = path::GetBaseNameTemp(GetSelfExePathTemp());
    return str::ContainsI(exeName, StrL("uninstall")) || str::ContainsI(exeName, StrL("install"));
}

static Str gAppDataDir;
static Str gPendingDataDir;
static Str gDataStorageError;
static bool gDataFolderOverride = false;
static constexpr const char* kStorageBootstrap = "SumatraPDFEnhanced-storage.txt";

void DeleteAppTools() {
    gAppDataDir = {};
    gPendingDataDir = {};
    str::Free(gDataStorageError);
    gDataStorageError = {};
    gDataFolderOverride = false;
}

static TempStr StorageBootstrapTemp() {
    if (IsRunningInPortableMode()) {
        return path::JoinTemp(GetSelfExeDirTemp(), Str(kStorageBootstrap));
    }
    TempStr base = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA, true);
    return path::JoinTemp(base, StrL("SumatraPDF Enhanced-config"), Str(kStorageBootstrap));
}

TempStr GetDefaultDataDirTemp() {
    if (IsRunningInPortableMode() && dir::HasWriteAccess(GetSelfExeDirTemp())) {
        return GetSelfExeDirTemp();
    }
    return path::JoinTemp(GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA, true), Str(kEnhancedDataDirName));
}

bool IsDataFolderOverridden() {
    return gDataFolderOverride;
}
TempStr GetPendingDataDirTemp() {
    return gPendingDataDir;
}
TempStr GetDataStorageErrorTemp() {
    return gDataStorageError;
}

static bool StorageError(Str& error, Str message) {
    str::ReplaceWithCopy(&error, message);
    return false;
}

static bool StoragePathContains(Str parent, Str child) {
    TempStr current = path::NormalizeTemp(child);
    while (current) {
        if (path::IsSame(parent, current)) {
            return true;
        }
        TempStr next = path::GetDirTemp(current);
        if (len(next) == 0 || len(next) >= len(current)) {
            break;
        }
        current = next;
    }
    return false;
}

static bool SafeStorageFolder(Str folder) {
    TempStr parent = path::GetDirTemp(folder);
    TempStr base = path::GetBaseNameTemp(folder);
    if (len(parent) == 0 || str::EqI(base, StrL("SumatraPDF"))) {
        return false;
    }
    int roots[] = {CSIDL_PROGRAM_FILES, CSIDL_PROGRAM_FILESX86, CSIDL_LOCAL_APPDATA,    CSIDL_APPDATA, CSIDL_WINDOWS,
                   CSIDL_SYSTEM,        CSIDL_PROFILE,          CSIDL_DESKTOPDIRECTORY, CSIDL_PERSONAL};
    for (int id : roots) {
        TempStr reserved = GetSpecialFolderTemp(id, false);
        if (len(reserved) == 0) {
            continue;
        }
        if (path::IsSame(folder, reserved) ||
            ((id == CSIDL_WINDOWS || id == CSIDL_SYSTEM) && StoragePathContains(reserved, folder))) {
            return false;
        }
    }
    Str officialKey = StrL("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SumatraPDF");
    HKEY keys[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (HKEY key : keys) {
        TempStr official = LoggedReadRegStrTemp(key, officialKey, StrL("InstallLocation"));
        if (str::EndsWithI(official, StrL(".exe"))) {
            official = path::GetDirTemp(official);
        }
        if (len(official) > 0 && (StoragePathContains(official, folder) || StoragePathContains(folder, official))) {
            return false;
        }
    }
    TempStr current = path::NormalizeTemp(folder);
    while (len(current) > 0) {
        DWORD attrs = GetFileAttributesW(CWStrTemp(current));
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return false;
        }
        TempStr next = path::GetDirTemp(current);
        if (len(next) == 0 || len(next) >= len(current)) {
            break;
        }
        current = next;
    }
    return !file::Exists(path::JoinTemp(folder, StrL("SumatraPDF.exe")));
}

static bool WritableStorageDir(Str folder, Str& error) {
    if (!path::IsAbsolute(folder) || str::Contains(folder, StrL("\n")) || str::Contains(folder, StrL("\r"))) {
        return StorageError(error, StrL("Choose an absolute folder path."));
    }
    if (!SafeStorageFolder(folder)) {
        return StorageError(error, StrL("Choose a separate data folder, outside the official SumatraPDF installation "
                                        "and protected system folders."));
    }
    DWORD attrs = GetFileAttributesW(CWStrTemp(folder));
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return StorageError(error, StrL("Choose a real folder, not a linked or redirected folder."));
    }
    if (!dir::CreateAll(folder)) {
        return StorageError(
            error,
            StrL("The data folder is unavailable or could not be created. Your current data remains unchanged."));
    }
    WCHAR probe[MAX_PATH];
    if (!GetTempFileNameW(CWStrTemp(folder), L"spe", 0, probe)) {
        return StorageError(error, StrL("The data folder is not writable. Your current data remains unchanged."));
    }
    DeleteFileW(probe);
    return true;
}

static bool StorageFileAllowed(Str relative) {
    TempStr first = relative;
    int separator = str::IndexOfChar(relative, '\\');
    if (separator >= 0) {
        first = Str(relative.s, separator);
    }
    if (str::EqI(first, StrL("dictionaries")) || str::EqI(first, StrL("sumatrapdfcache")) ||
        str::EqI(first, StrL("notes")) || str::EqI(first, StrL("vocabulary")) || str::EqI(first, StrL("Screenshots")))
        return true;
    return str::EqI(relative, StrL("SumatraPDFEnhanced-settings.txt")) ||
           str::EqI(relative, StrL("SumatraPDF-settings.txt")) ||
           str::EqI(relative, StrL("SumatraPDF-vocabulary.json"));
}

static bool EmptyStorageTarget(Str folder) {
    WIN32_FIND_DATAW item;
    HANDLE handle = FindFirstFileW(CWStrTemp(path::JoinTemp(folder, StrL("*"))), &item);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    bool empty = true;
    do {
        if (wcscmp(item.cFileName, L".") && wcscmp(item.cFileName, L"..")) {
            empty = false;
            break;
        }
    } while (FindNextFileW(handle, &item));
    FindClose(handle);
    return empty;
}

static bool CopyStorageTree(Str source, Str target, Str& error) {
    DWORD attrs = GetFileAttributesW(CWStrTemp(source));
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return StorageError(error,
                            StrL("The original data folder is unavailable or is a linked folder. No data was copied."));
    }
    WIN32_FIND_DATAW check;
    HANDLE readable = FindFirstFileW(CWStrTemp(path::JoinTemp(source, StrL("*"))), &check);
    if (readable == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) {
        return StorageError(error, StrL("The original data folder cannot be read. No data was copied."));
    }
    if (readable != INVALID_HANDLE_VALUE) FindClose(readable);
    if (StoragePathContains(source, target) || StoragePathContains(target, source)) {
        return StorageError(error, StrL("The source and destination folders must not overlap."));
    }
    if (!WritableStorageDir(target, error) || !EmptyStorageTarget(target)) {
        if (len(error) == 0) {
            StorageError(error,
                         StrL("Copying requires an empty destination folder. Existing files are never overwritten."));
        }
        return false;
    }
    DirIter files(source);
    files.recurse = true;
    files.includeDirs = true;
    StrVec copied;
    StrVec madeDirs;
    bool ok = true;
    for (DirIterEntry* entry : files) {
        Str relative(entry->filePath.s + len(source) + 1, len(entry->filePath) - len(source) - 1);
        if (entry->fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            entry->stopTraversal = true;
            if (StorageFileAllowed(relative)) {
                ok = false;
                break;
            }
            continue;
        }
        if (entry->isDir && StorageFileAllowed(relative)) {
            WIN32_FIND_DATAW item;
            HANDLE handle = FindFirstFileW(CWStrTemp(path::JoinTemp(entry->filePath, StrL("*"))), &item);
            if (handle == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) {
                ok = false;
                break;
            }
            if (handle != INVALID_HANDLE_VALUE) FindClose(handle);
        }
        if (!entry->isFile || !StorageFileAllowed(relative)) {
            continue;
        }
        TempStr name =
            str::EqI(relative, StrL("SumatraPDF-settings.txt")) ? StrL("SumatraPDFEnhanced-settings.txt") : relative;
        TempStr dest = path::JoinTemp(target, name);
        TempStr parent = path::GetDirTemp(dest);
        StrVec missing;
        for (TempStr ancestor = parent; ancestor && !dir::Exists(ancestor); ancestor = path::GetDirTemp(ancestor)) {
            missing.Append(ancestor);
        }
        for (int i = len(missing) - 1; i >= 0; i--) {
            if (CreateDirectoryW(CWStrTemp(missing[i]), nullptr)) madeDirs.Append(missing[i]);
        }
        if (!dir::Exists(parent) || !file::Copy(dest, entry->filePath, true)) {
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) file::Delete(dest);
            ok = false;
            break;
        }
        copied.Append(dest);
    }
    if (ok) {
        return true;
    }
    for (Str file : copied) {
        file::Delete(file);
    }
    for (int i = len(madeDirs) - 1; i >= 0; i--) {
        RemoveDirectoryW(CWStrTemp(madeDirs[i]));
    }
    return StorageError(
        error, StrL("The copy failed. Original data was kept, and no existing destination files were replaced."));
}

static bool WriteStorageBootstrap(Str file, Str folder, Str source, Str lastGood = {}) {
    if (!dir::CreateAll(path::GetDirTemp(file))) {
        return false;
    }
    TempStr temp = fmt("%s.%d.pending", file, GetCurrentProcessId());
    TempStr content = fmt("%s\n%s\n%s\n", folder, source, lastGood);
    if (!file::WriteFile(temp, content)) {
        return false;
    }
    bool ok = MoveFileExW(CWStrTemp(temp), CWStrTemp(file), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        file::Delete(temp);
    }
    return ok;
}

static TempStr ResolveStorageTemp(Str bootstrap, Str fallback, Str& error) {
    Str contents = file::ReadFile(bootstrap);
    StrVec lines;
    Split(&lines, contents, StrL("\n"));
    if (len(lines) > 0 && len(lines[0]) > 0 && !path::IsAbsolute(lines[0])) {
        str::Free(contents);
        StorageError(error, StrL("The saved data-folder choice is invalid. The default folder is being used."));
        return fallback;
    }
    TempStr chosen = len(lines) > 0 && len(lines[0]) > 0 ? path::NormalizeTemp(lines[0]) : TempStr{};
    TempStr source = len(lines) > 1 && len(lines[1]) > 0 ? path::NormalizeTemp(lines[1]) : TempStr{};
    TempStr lastGood = len(lines) > 2 && len(lines[2]) > 0 ? path::NormalizeTemp(lines[2]) : TempStr{};
    str::Free(contents);
    if (len(chosen) == 0) {
        return fallback;
    }
    if (source) {
        if (!CopyStorageTree(source, chosen, error) || !WriteStorageBootstrap(bootstrap, chosen, {}, source)) {
            if (len(error) == 0) {
                StorageError(error,
                             StrL("The data-folder choice could not be saved. Continue using the original folder."));
            }
            return dir::Exists(source) ? source : fallback;
        }
        lastGood = source;
    }
    if (!dir::Exists(chosen)) {
        StorageError(error, StrL("The selected data folder is missing. The previous folder is being used; "
                                 "the selected folder has not been recreated."));
        return dir::Exists(lastGood) && SafeStorageFolder(lastGood) && dir::HasWriteAccess(lastGood) ? lastGood
                                                                                                     : fallback;
    }
    if (!WritableStorageDir(chosen, error)) {
        return dir::Exists(lastGood) && SafeStorageFolder(lastGood) && dir::HasWriteAccess(lastGood) ? lastGood
                                                                                                     : fallback;
    }
    return chosen;
}

void SetAppDataDir(Str folder) {
    folder = path::NormalizeTemp(folder);
    dir::CreateAll(folder);
    gAppDataDir = str::Dup(GetPermArena(), folder);
    gDataFolderOverride = true;
}

TempStr GetAppDataDirTemp() {
    if (gAppDataDir) {
        return gAppDataDir;
    }
    TempStr fallback = GetDefaultDataDirTemp();
    if (!IsRunningInPortableMode() &&
        !file::Exists(path::JoinTemp(fallback, StrL("SumatraPDFEnhanced-settings.txt")))) {
        TempStr old = path::JoinTemp(GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA), StrL(kEnhancedAppName));
        if (file::Exists(path::JoinTemp(old, StrL("SumatraPDFEnhanced-settings.txt")))) {
            fallback = old;
        }
    }
    TempStr folder = ResolveStorageTemp(StorageBootstrapTemp(), fallback, gDataStorageError);
    dir::CreateAll(folder);
    gAppDataDir = str::Dup(GetPermArena(), folder);
    return gAppDataDir;
}

bool RequestDataFolder(Str folder, DataFolderMode mode, Str& error) {
    if (gDataFolderOverride || gForTesting) {
        return StorageError(error, StrL("This session uses a command-line or testing data folder. Restart normally to "
                                        "change the persistent location."));
    }
    TempStr target = path::NormalizeTemp(folder);
    TempStr current = GetAppDataDirTemp();
    if (path::IsSame(target, current)) {
        return StorageError(error, StrL("This is already the current data folder."));
    }
    if (StoragePathContains(target, current) || StoragePathContains(current, target)) {
        return StorageError(error, StrL("The current and new data folders must not overlap."));
    }
    if (!WritableStorageDir(target, error)) {
        return false;
    }
    if (mode == DataFolderMode::CopyCurrent && !EmptyStorageTarget(target)) {
        return StorageError(
            error, StrL("Choose an empty folder when copying current data. Existing files are never overwritten."));
    }
    Str source = mode == DataFolderMode::CopyCurrent ? current : Str{};
    if (!WriteStorageBootstrap(StorageBootstrapTemp(), target, source, current)) {
        return StorageError(error,
                            StrL("The data-folder choice could not be saved. Your current data remains unchanged."));
    }
    gPendingDataDir = str::Dup(GetPermArena(), target);
    return true;
}

#if IS_DEBUG
bool AppTools_UnitTestsStorage() {
    WCHAR temp[MAX_PATH], unique[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"sdt", 0, unique)) return false;
    DeleteFileW(unique);
    Str base = str::Dup(ToUtf8Temp(unique));
    dir::CreateAll(base);
    Str source = str::Dup(path::JoinTemp(base, StrL("source")));
    Str target = str::Dup(path::JoinTemp(base, StrL("target")));
    Str bootstrap = str::Dup(path::JoinTemp(base, StrL("bootstrap.txt")));
    dir::CreateAll(source);
    file::WriteFile(path::JoinTemp(source, StrL("SumatraPDF-vocabulary.json")), StrL("{\"words\": []}"));
    file::WriteFile(path::JoinTemp(source, StrL("reader.exe")), StrL("not user data"));
    Str error;
    bool ok =
        SafeStorageFolder(path::JoinTemp(GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA, false), Str(kEnhancedDataDirName)));
    ok &= !SafeStorageFolder(GetSpecialFolderTemp(CSIDL_WINDOWS, false));
    ok &= !SafeStorageFolder(path::JoinTemp(GetSpecialFolderTemp(CSIDL_WINDOWS, false), StrL("data")));
    ok &= WriteStorageBootstrap(bootstrap, target, source);
    TempStr resolved = ResolveStorageTemp(bootstrap, source, error);
    ok &= path::IsSame(resolved, target) && len(error) == 0;
    ok &= file::Exists(path::JoinTemp(target, StrL("SumatraPDF-vocabulary.json")));
    ok &= !file::Exists(path::JoinTemp(target, StrL("reader.exe")));
    ok &= file::Exists(path::JoinTemp(source, StrL("SumatraPDF-vocabulary.json")));
    ok &= path::IsSame(ResolveStorageTemp(bootstrap, source, error), target);
    dir::RemoveAll(target);
    ok &= path::IsSame(ResolveStorageTemp(bootstrap, source, error), source) && len(error) > 0;
    ok &= !dir::Exists(target);
    str::Free(error);
    error = {};
    ok &= WriteStorageBootstrap(bootstrap, target, {}, source);
    ok &= path::IsSame(ResolveStorageTemp(bootstrap, source, error), source) && len(error) > 0;
    ok &= !dir::Exists(target);
    str::Free(error);
    error = {};
    Str blocked = str::Dup(path::JoinTemp(base, StrL("blocked")));
    dir::CreateAll(blocked);
    file::WriteFile(path::JoinTemp(blocked, StrL("keep.txt")), StrL("existing data"));
    ok &= !CopyStorageTree(source, blocked, error);
    str::Free(error);
    error = {};
    ok &= WriteStorageBootstrap(bootstrap, blocked, source);
    ok &= path::IsSame(ResolveStorageTemp(bootstrap, target, error), source) && len(error) > 0;
    ok &= file::Exists(path::JoinTemp(blocked, StrL("keep.txt")));
    ok &= file::Exists(path::JoinTemp(source, StrL("SumatraPDF-vocabulary.json")));
    str::Free(error);
    error = {};
    ok &= !CopyStorageTree(source, path::JoinTemp(source, StrL("nested")), error);
    dir::RemoveAll(base);
    str::Free(base);
    str::Free(source);
    str::Free(target);
    str::Free(bootstrap);
    str::Free(blocked);
    str::Free(error);
    return ok;
}
#endif

// Generate full path for a file or directory for storing data
TempStr GetPathInAppDataDirTemp(Str name) {
    if (len(name) == 0) {
        return {};
    }
    TempStr dir = GetAppDataDirTemp();
    return path::JoinTemp(dir, name);
}

// List of rules used to detect TeX editors.

#define kRegCurrentVer "Software\\Microsoft\\Windows\\CurrentVersion"

// clang-format off
static TextEditor editorRules[] = {
    {
        StrL("Code.exe"),
        StrL(R"(--goto "%f:%l")"),
        RegType::BinaryPath,
        StrL(kRegCurrentVer "\\Uninstall\\{771FD6B0-FA20-440A-A002-3B3BAC16DC50}_is1"),
        StrL("DisplayIcon"),
    },
    {
        StrL("WinEdt.exe"),
         StrL("\"[Open(|%f|);SelPar(%l,8)]\""),
        RegType::BinaryPath,
        StrL(kRegCurrentVer "\\App Paths\\WinEdt.exe"),
        {}
    },
    {
        StrL("WinEdt.exe"),
        StrL("\"[Open(|%f|);SelPar(%l,8)]\""),
        RegType::BinaryDir,
        StrL("Software\\WinEdt"),
        StrL("Install Root"),
    },
    {
        StrL("notepad++.exe"),
        StrL("-n%l \"%f\""),
        RegType::BinaryPath,
        StrL(kRegCurrentVer "\\App Paths\\notepad++.exe"),
        {}
    },
    {
        StrL("notepad++.exe"),
        StrL("-n%l \"%f\""),
        RegType::BinaryDir,
        StrL("Software\\Notepad++"),
        {}
    },
    {
        StrL("notepad++.exe"),
        StrL("-n%l \"%f\""),
        RegType::BinaryPath,
        StrL(kRegCurrentVer "\\Uninstall\\Notepad++"),
        StrL("DisplayIcon"),
    },
    {
        StrL("sublime_text.exe"),
        StrL("\"%f:%l:%c\""),
       RegType:: BinaryDir,
        StrL(kRegCurrentVer "\\Uninstall\\Sublime Text 3_is1"),
        StrL("InstallLocation"),
    },
    {
        StrL("sublime_text.exe"),
        StrL("\"%f:%l:%c\""),
        RegType::BinaryPath,
        StrL(kRegCurrentVer "\\Uninstall\\Sublime Text 3_is1"),
        StrL("DisplayIcon"),
    },
    {
        StrL("sublime_text.exe"),
        StrL("\"%f:%l:%c\""),
        RegType::BinaryDir,
        StrL(kRegCurrentVer "\\Uninstall\\Sublime Text 2_is1"),
         StrL("InstallLocation"),
    },
    {
        StrL("sublime_text.exe"),
        StrL("\"%f:%l:%c\""),
        RegType::BinaryPath,
        StrL(kRegCurrentVer "\\Uninstall\\Sublime Text 2_is1"),
        StrL("DisplayIcon"),
    },
    {
        StrL("sublime_text.exe"),
        StrL("\"%f:%l:%c\""),
        RegType::BinaryPath,
        StrL(kRegCurrentVer "\\Uninstall\\Sublime Text_is1"),
        StrL("DisplayIcon"),
    },
    {
        StrL("TeXnicCenter.exe"),
        StrL("/ddecmd \"[goto('%f', '%l')]\""),
        RegType::BinaryDir,
        StrL("Software\\ToolsCenter\\TeXnicCenterNT"),
        StrL("AppPath"),
    },
    {
        StrL("TeXnicCenter.exe"),
        StrL("/ddecmd \"[goto('%f', '%l')]\""),
        RegType::BinaryDir,
        StrL(kRegCurrentVer "\\Uninstall\\TeXnicCenter_is1"),
        StrL("InstallLocation"),
    },
    {
        StrL("TeXnicCenter.exe"),
        StrL("/ddecmd \"[goto('%f', '%l')]\""),
        RegType::BinaryDir,
        StrL(kRegCurrentVer "\\Uninstall\\TeXnicCenter Alpha_is1"),
        StrL("InstallLocation"),
    },
    {
        StrL("TEXCNTR.exe"),
        StrL("/ddecmd \"[goto('%f', '%l')]\""),
        RegType::BinaryDir,
        StrL("Software\\ToolsCenter\\TeXnicCenter"),
        StrL("AppPath"),
    },
    {
        StrL("TEXCNTR.exe"),
        StrL("/ddecmd \"[goto('%f', '%l')]\""),
        RegType::BinaryDir,
        StrL(kRegCurrentVer "\\Uninstall\\TeXnicCenter_is1"),
        StrL("InstallLocation"),
    },
    {
        StrL("WinShell.exe"),
        StrL("-c \"%f\" -l %l"),
        RegType::BinaryDir,
        StrL(kRegCurrentVer "\\Uninstall\\WinShell_is1"),
        StrL("InstallLocation"),
    },
    {
        StrL("gvim.exe"),
        StrL("\"%f\" +%l"),
        RegType::BinaryPath,
        StrL("Software\\Vim\\Gvim"),
        StrL("path"),
    },
    {
        // TODO: add this rule only if the latex-suite for ViM is installed
        // (http://vim-latex.sourceforge.net/documentation/latex-suite.txt)
        StrL("gvim.exe"),
        StrL("-c \":RemoteOpen +%l %f\""),
        RegType::BinaryPath,
        StrL("Software\\Vim\\Gvim"),
        StrL("path"),
    },
    {
        StrL("texmaker.exe"),
        StrL("\"%f\" -line %l"),
        RegType::SiblingPath,
        StrL(kRegCurrentVer "\\Uninstall\\Texmaker"),
        StrL("UninstallString"),
    },
    {
        StrL("TeXworks.exe"),
        StrL("-p=%l \"%f\""),
        RegType::BinaryDir,
        StrL(kRegCurrentVer "\\Uninstall\\{41DA4817-4D2A-4D83-AD02-6A2D95DC8DCB}_is1"),
        StrL("InstallLocation"),
        // TODO: find a way to detect where emacs is installed
        // "emacsclientw.exe","+%l \"%f\"", BinaryPath, "???", "???",
    },
    {
        StrL("notepad.exe"),
        StrL("\"%f\""),
        RegType::BinaryDir,
        StrL(R"(Software\Microsoft\Windows NT\CurrentVersion)"),
        StrL("SystemRoot"),
    }
};

// clang-format on

static bool didFindTextEditors = false;
static void FindTextEditors() {
    if (didFindTextEditors) {
        return;
    }
    StrVec found;
    // all but last entry, which is notepad.exe
    int n = dimofi(editorRules) - 1;
    for (int i = 0; i < n; i++) {
        auto& rule = editorRules[i];
        Str regKey = rule.regKey;
        Str regValue = rule.regValue;
        TempStr path = LoggedReadRegStr2Temp(regKey, regValue);
        if (len(path) == 0) {
            continue;
        }

        TempStr exePath;
        Str binaryFileName = rule.binaryFilename;
        Str inverseSearchArgs = rule.inverseSearchArgs;
        if (rule.type == RegType::SiblingPath) {
            // remove file part
            TempStr dir = path::GetDirTemp(path);
            exePath = path::JoinTemp(dir, binaryFileName);
        } else if (rule.type == RegType::BinaryDir) {
            exePath = path::JoinTemp(path, binaryFileName);
        } else { // if (editor_rules[i].Type == BinaryPath)
            exePath = path;
        }
        // don't show duplicate entries
        if (found.FindI(exePath) != -1) {
            continue;
        }
        // don't show inexistent paths (and don't try again for them)
        if (!file::Exists(exePath)) {
            found.Append(exePath);
            continue;
        }

        rule.fullPath = str::Dup(exePath);
        rule.openFileCmd = str::Dup(fmt("\"%s\" %s", exePath, inverseSearchArgs));
        found.Append(exePath);
    }
    didFindTextEditors = true;
}

// Detect TeX editors installed on the system and construct the
// corresponding inverse search commands.
void DetectTextEditors(Vec<TextEditor*>& res) {
    FindTextEditors();
    int n = dimofi(editorRules);
    for (int i = 0; i < n; i++) {
        TextEditor* e = &editorRules[i];
        if (len(e->openFileCmd) == 0) {
            continue;
        }
        VecAppend(res, e);
    }
}

// Detected text-editor command lines plus the current setting, if any.
void CollectInverseSearchCommands(StrVec& out, Str cmdLine) {
    out.Reset();
    Vec<TextEditor*> textEditors;
    DetectTextEditors(textEditors);
    for (auto* e : textEditors) {
        AppendIfNotExists(&out, e->openFileCmd);
    }
    if (cmdLine) {
        AppendIfNotExists(&out, cmdLine);
    }
}

/* Default size for the window, happens to be american A4 size (I think) */
constexpr double kDefPageRatio = 612.0 / 792.0;

constexpr int kMinWinDx = 50;
constexpr int kMinWinDy = 50;

void EnsureAreaVisibility(Rect& r) {
    // adjust to the work-area of the current monitor (not necessarily the primary one)
    Rect work = GetWorkAreaRect(r, nullptr);

    // make sure that the window is neither too small nor bigger than the monitor
    if (r.dx < kMinWinDx || r.dx > work.dx) {
        r.dx = std::min((int)((double)work.dy * kDefPageRatio), work.dx);
    }
    if (r.dy < kMinWinDy || r.dy > work.dy) {
        r.dy = work.dy;
    }

    // check whether the lower half of the window's title bar is
    // inside a visible working area
    int captionDy = GetSystemMetrics(SM_CYCAPTION);
    Rect halfCaption(r.x, r.y + (captionDy / 2), r.dx, captionDy / 2);
    if (halfCaption.Intersect(work).IsEmpty()) {
        r = Rect(work.TL(), r.Size());
    }
}

Rect GetDefaultWindowPos() {
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    Rect work = ToRect(workArea);

    Rect r = work;
    r.dx = std::min((int)((double)r.dy * kDefPageRatio), work.dx);
    r.x = (work.dx - r.dx) / 2;

    return r;
}

// cache because calculating sha1 of the whole executable
// might be relatively expensive
// sha1 is 20 bytes => 40 hex chars + null terminator
static char gAppSha1[41]{};

// return hex version of sha1 of app's executable (pointer to cached value)
// nullptr if there was an error
Str Sha1OfAppExe() {
    if (gAppSha1[0]) {
        return Str(gAppSha1);
    }

    TempStr appPath = GetSelfExePathTemp();
    if (len(appPath) == 0) {
        return {};
    }
    Str d = file::ReadFile(appPath);
    if (len(d) == 0) {
        return {};
    }

    u8 sha1[20]{};
    CalcSHA1Digest(d, sha1);
    str::Free(d);

    for (size_t i = 0; i < 20; i++) {
        sprintf_s(&gAppSha1[2 * i], 3, "%02x", sha1[i]);
    }
    return Str(gAppSha1);
}

TempStr GetWebViewDataDirTemp() {
    TempStr dir = GetSpecialFolderTemp(CSIDL_LOCAL_APPDATA, false);
    if (len(dir) == 0) {
        return {};
    }
    dir = path::JoinTemp(dir, StrL("SumatraPDF-data"));
    char id[7] = "000000";
    Str sha1 = Sha1OfAppExe();
    if (sha1) {
        str::BufSet(Str(id, dimof(id)), sha1);
    }
    dir = path::JoinTemp(dir, Str(id));
    return path::JoinTemp(dir, fmt("webview-%d", (int)GetCurrentProcessId()));
}

// Format the file size in a short form that rounds to the largest size unit
// e.g. "3.48 GB", "12.38 MB", "23 KB"
TempStr FormatFileSizeShortTransTemp(i64 size) {
    Str units[3] = {Tr("GB"), Tr("MB"), Tr("KB")};
    return str::FormatSizeShortTemp(size, units);
}

// format file size in a readable way e.g. 1348258 is shown
// as "1.29 MB (1,348,258 Bytes)"
TempStr FormatFileSizeTransTemp(i64 size) {
    if (size <= 0) {
        return fmt("%d", size);
    }
    TempStr n1 = FormatFileSizeShortTransTemp(size);
    TempStr n2 = str::FormatNumWithThousandSepTemp(size);
    return fmt("%s (%s %s)", n1, n2, Tr("Bytes"));
}

// returns true if file exists
bool LaunchFileIfExists(Str path) {
    if (len(path) == 0) {
        return false;
    }
    if (!file::Exists(path)) {
        logf("LaunchFileIfExists: !file::Exists('%s')\n", path);
        return false;
    }
    if (gIsStoreBuild) {
        path = path::GetNonVirtualTemp(path);
        logf("LaunchFileIfExists: gIsStoreBuild, path='%s'\n", path);
    }
    LaunchFileShell(path, {}, StrL("open"));
    return true;
}

// Updates the drive letter for a path that could have been on a removable drive,
// if that same path can be found on a different removable drive
// returns true if the path has been changed
bool AdjustVariableDriveLetter(Str& path) {
    // Don't bother if the file path is still valid
    if (file::Exists(path)) {
        return false;
    }
    // only check absolute path on drives i.e. those that start with "d:\"
    if (len(path) < 4 || path.s[1] != ':') {
        return false;
    }

    // Iterate through all (other) removable drives and try to find the file there
    char szDrive[] = "A:\\";
    char origDrive = path.s[0];
    for (DWORD driveMask = GetLogicalDrives(); driveMask; driveMask >>= 1) {
        if ((driveMask & 1) && szDrive[0] != origDrive && path::HasVariableDriveLetter(Str(szDrive))) {
            path.s[0] = szDrive[0];
            if (file::Exists(path)) {
                return true;
            }
        }
        szDrive[0]++;
    }
    path.s[0] = origDrive;
    return false;
}

// files are considered untrusted, if they're either loaded from a
// non-file URL in plugin mode, or if they're marked as being from
// an untrusted zone (e.g. by the browser that's downloaded them)
bool IsUntrustedFile(Str filePath, Str fileURL) {
    TempStr protocol;
    if (fileURL && !str::IsNull(str::Parse(fileURL, "%S:", &protocol))) {
        if (len(protocol) > 1 && !str::EqI(protocol, StrL("file"))) {
            return true;
        }
    }

    if (file::GetZoneIdentifier(filePath) >= URLZONE_INTERNET) {
        return true;
    }

    // check all parents of embedded files and ADSs as well
    TempStr path = str::DupTemp(filePath);
    while (len(path) > 2 && str::ContainsChar(Str(path.s + 2, path.len - 2), ':')) {
        Str lastColon = str::SliceFromCharLast(path, ':');
        *lastColon.s = '\0';
        if (file::GetZoneIdentifier(path) >= URLZONE_INTERNET) {
            return true;
        }
    }

    return false;
}
