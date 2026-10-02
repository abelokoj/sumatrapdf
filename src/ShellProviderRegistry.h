/* Copyright 2026 the SumatraPDF Enhanced authors. License: GPLv3 */

inline bool ShellProviderAvailable(HKEY root, Str key, Str clsid) {
    HKEY opened = nullptr;
    LONG err = RegOpenKeyExW(root, CWStrTemp(key), 0, KEY_QUERY_VALUE, &opened);
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
    if (err != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = 0;
    err = RegQueryValueExW(opened, nullptr, nullptr, &type, nullptr, &bytes);
    RegCloseKey(opened);
    if (err == ERROR_FILE_NOT_FOUND) return true;
    if (err != ERROR_SUCCESS || type != REG_SZ) return false;
    TempStr value = ReadRegStrTemp(root, key, {});
    return len(value) == 0 || str::EqI(value, clsid);
}

inline bool ClaimShellProvider(HKEY root, Str key, Str clsid, HKEY effectiveRoot, Str effectiveKey) {
    if (!ShellProviderAvailable(root, key, clsid) || !ShellProviderAvailable(effectiveRoot, effectiveKey, clsid)) {
        logf("Preserving existing shell provider at '%s'\n", key);
        return true;
    }
    return LoggedWriteRegStr(root, key, {}, clsid);
}

inline void RemoveShellProvider(HKEY root, Str key, Str clsid) {
    if (!str::EqI(ReadRegStrTemp(root, key, {}), clsid)) return;
    DeleteRegValue(root, key, {});
    // RegDeleteKey also deletes values: remove the key only when truly empty.
    HKEY opened = nullptr;
    if (RegOpenKeyExW(root, CWStrTemp(key), 0, KEY_QUERY_VALUE, &opened) != ERROR_SUCCESS) return;
    DWORD subkeys = 0, values = 0;
    LONG err = RegQueryInfoKeyW(opened, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr,
                                nullptr, nullptr, nullptr);
    RegCloseKey(opened);
    if (err == ERROR_SUCCESS && subkeys == 0 && values == 0) RegDeleteKeyW(root, CWStrTemp(key));
}

inline bool OwnShellClass(HKEY root, Str key, Str dllPath) {
    if (!str::EqI(ReadRegStrTemp(root, key, StrL("EnhancedOwner")), StrL("SumatraPDF Enhanced"))) return false;
    if (len(dllPath) == 0) return true;
    return str::EqI(ReadRegStrTemp(root, fmt("%s\\InProcServer32", key), {}), dllPath);
}

inline bool ShellClassAvailable(HKEY root, Str key) {
    HKEY opened = nullptr;
    LONG err = RegOpenKeyExW(root, CWStrTemp(key), 0, KEY_QUERY_VALUE, &opened);
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
    if (err != ERROR_SUCCESS) return false;
    RegCloseKey(opened);
    return OwnShellClass(root, key, {});
}
