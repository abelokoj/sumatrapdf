/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#define kPdfFilterClsid "{72DEBEAF-96BA-5A7E-9715-B492B79FA46A}"
#define kPdfFilterHandler "{65CD0569-BF87-5FE2-8A25-FCDEB6259682}"

#define kTexFilterClsid "{8508A2AF-D79E-5D26-B17F-4BDDE4FF82A1}"
#define kTexFilterHandler "{E9DAA356-21A7-5892-A87E-621127D0221B}"

#define kEpubFilterClsid "{52EA7ADF-EF73-5F2C-B4E0-2D3E6879B866}"
#define kEpubFilterHandler "{997980E6-1B62-5174-9D27-E2CD375CA1C3}"

bool InstallSearchFilter(Str dllPath, bool allUsers);
bool UninstallSearchFilter(Str dllPath = {});
bool IsSearchFilterInstalled();