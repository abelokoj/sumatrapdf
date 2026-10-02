/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"

#include "RegistrySearchFilter.h"
#include "ShellProviderRegistry.h"

bool InstallSearchFilter(Str dllPath, bool allUsers) {
    struct {
        const char* key;
        const char* value;
        Str data;
    } regVals[] = {
        {"Software\\Classes\\CLSID\\" kPdfFilterClsid, nullptr, StrL("SumatraPDF Enhanced IFilter")},
        {"Software\\Classes\\CLSID\\" kPdfFilterClsid "\\InProcServer32", nullptr, dllPath},
        {"Software\\Classes\\CLSID\\" kPdfFilterClsid "\\InProcServer32", "ThreadingModel", StrL("Both")},
        {"Software\\Classes\\CLSID\\" kPdfFilterHandler, nullptr,
         StrL("SumatraPDF Enhanced IFilter Persistent Handler")},
        {"Software\\Classes\\CLSID\\" kPdfFilterHandler "\\PersistentAddinsRegistered", nullptr, StrL("")},
        {"Software\\Classes\\CLSID"
         "\\" kPdfFilterHandler "\\PersistentAddinsRegistered\\{89BCB740-6119-101A-BCB7-00DD010655AF}",
         nullptr, StrL(kPdfFilterClsid)},
        {R"(Software\Classes\.pdf\PersistentHandler)", nullptr, StrL(kPdfFilterHandler)},
#ifdef BUILD_TEX_IFILTER
        {"Software\\Classes\\CLSID\\" kTexFilterClsid, nullptr, StrL("SumatraPDF Enhanced IFilter")},
        {"Software\\Classes\\CLSID\\" kTexFilterClsid "\\InProcServer32", nullptr, dllPath},
        {"Software\\Classes\\CLSID\\" kTexFilterClsid "\\InProcServer32", "ThreadingModel", StrL("Both")},
        {"Software\\Classes\\CLSID\\" kTexFilterHandler, nullptr,
         StrL("SumatraPDF Enhanced LaTeX IFilter Persistent Handler")},
        {"Software\\Classes\\CLSID\\" kTexFilterHandler "\\PersistentAddinsRegistered", nullptr, StrL("")},
        {"Software\\Classes\\CLSID"
         "\\" kTexFilterHandler "\\PersistentAddinsRegistered\\{89BCB740-6119-101A-BCB7-00DD010655AF}",
         nullptr, StrL(kTexFilterClsid)},
        {"Software\\Classes\\.tex\\PersistentHandler", nullptr, StrL(kTexFilterHandler)},
#endif
#ifdef BUILD_EPUB_IFILTER
        {"Software\\Classes\\CLSID\\" kEpubFilterClsid, nullptr, StrL("SumatraPDF Enhanced IFilter")},
        {"Software\\Classes\\CLSID\\" kEpubFilterClsid "\\InProcServer32", nullptr, dllPath},
        {"Software\\Classes\\CLSID\\" kEpubFilterClsid "\\InProcServer32", "ThreadingModel", StrL("Both")},
        {"Software\\Classes\\CLSID\\" kEpubFilterHandler, nullptr,
         StrL("SumatraPDF Enhanced EPUB IFilter Persistent Handler")},
        {"Software\\Classes\\CLSID\\" kEpubFilterHandler "\\PersistentAddinsRegistered", nullptr, StrL("")},
        {"Software\\Classes\\CLSID"
         "\\" kEpubFilterHandler "\\PersistentAddinsRegistered\\{89BCB740-6119-101A-BCB7-00DD010655AF}",
         nullptr, StrL(kEpubFilterClsid)},
        {"Software\\Classes\\.epub\\PersistentHandler", nullptr, StrL(kEpubFilterHandler)},
#endif
    };
    HKEY hkey = allUsers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    for (auto& regVal : regVals) {
        auto keyName = regVal.key;
        auto valName = regVal.value;
        auto value = regVal.data;
        Str key(keyName);
        bool classRoot =
            !valName && !str::Contains(key, StrL("Persistent")) && !str::Contains(key, StrL("InProcServer32"));
        if (classRoot && !ShellClassAvailable(hkey, key)) return false;
        bool association = str::Contains(key, StrL("\\.pdf\\")) || str::Contains(key, StrL("\\.tex\\")) ||
                           str::Contains(key, StrL("\\.epub\\"));
        bool ok = association ? ClaimShellProvider(hkey, key, value, HKEY_CLASSES_ROOT, Str(key.s + 17, len(key) - 17))
                              : LoggedWriteRegStr(hkey, key, valName ? Str(valName) : Str(), value);
        if (!association && !valName && str::Contains(key, StrL("\\CLSID\\")) &&
            !str::Contains(key, StrL("InProcServer32")) && !str::Contains(key, StrL("PersistentAddinsRegistered"))) {
            ok &= LoggedWriteRegStr(hkey, key, StrL("EnhancedOwner"), StrL("SumatraPDF Enhanced"));
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool UninstallSearchFilter(Str dllPath) {
    struct Filter {
        Str clsid;
        Str handler;
        Str ext;
    };
    Filter filters[] = {
        {StrL(kPdfFilterClsid), StrL(kPdfFilterHandler), StrL(".pdf")},
#ifdef BUILD_TEX_IFILTER
        {StrL(kTexFilterClsid), StrL(kTexFilterHandler), StrL(".tex")},
#endif
#ifdef BUILD_EPUB_IFILTER
        {StrL(kEpubFilterClsid), StrL(kEpubFilterHandler), StrL(".epub")},
#endif
    };
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (auto& filter : filters) {
            TempStr classKey = fmt("Software\\Classes\\CLSID\\%s", filter.clsid);
            if (!OwnShellClass(root, classKey, dllPath)) continue;
            RemoveShellProvider(root, fmt("Software\\Classes\\%s\\PersistentHandler", filter.ext), filter.handler);
            LoggedDeleteRegKey(root, classKey);
            TempStr handlerKey = fmt("Software\\Classes\\CLSID\\%s", filter.handler);
            if (OwnShellClass(root, handlerKey, {})) LoggedDeleteRegKey(root, handlerKey);
        }
    }
    return true;
}

bool IsSearchFilterInstalled() {
    Str key = StrL(".pdf\\PersistentHandler");
    TempStr iid = LoggedReadRegStrTemp(HKEY_CLASSES_ROOT, key, Str());
    bool isInstalled = str::EqI(iid, StrL(kPdfFilterHandler));
    logf("IsSearchFilterInstalled() isInstalled=%d\n", (int)isInstalled);
    return isInstalled;
}