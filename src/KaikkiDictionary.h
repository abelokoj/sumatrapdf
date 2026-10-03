/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3 */

#ifndef SUMATRA_KAIKKI_DICTIONARY_H
#define SUMATRA_KAIKKI_DICTIONARY_H

struct KaikkiPack {
    Str id, title, url, sourceUrl, license;
    i64 downloadBytes, expandedBytes;
    bool compressed;
};

struct KaikkiProgress {
    i64 downloaded = 0, total = 0;
    bool indexing = false;
};

const KaikkiPack* FindKaikkiPack(Str id);
const KaikkiPack* GetKaikkiPacks(int& count);
bool KaikkiDownloadSize(Str id, i64& bytes, Str* error);
i64 KaikkiRequiredFreeSpace(Str id, i64 downloadBytes);
bool InstallKaikkiPack(Str id, Str folder, const Func1<KaikkiProgress*>& progress, const Func1<bool*>& cancelled,
                       Str* error, i64 approvedBytes = 0);
bool KaikkiPackInstalled(Str id, Str folder);
bool RemoveKaikkiPack(Str id, Str folder, Str* error);
bool LookupKaikkiRecords(Str id, Str folder, Str normalizedWord, StrVec& records, Str* error);
#if IS_DEBUG
void KaikkiDictionary_UnitTests();
#endif

#endif
