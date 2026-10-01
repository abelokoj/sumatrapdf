/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3 */

#ifndef SUMATRA_OFFLINE_DICTIONARY_H
#define SUMATRA_OFFLINE_DICTIONARY_H

struct OfflineMeaning {
    Str headword;
    Str definition;
    Str dictionary;
    Str dictionaryId;
};

struct OfflineDictPack {
    Str id;
    Str title;
    Str language;
    Str license;
    Str sourceUrl;
    bool installed = false;
    bool bundled = false;
};

TempStr GetDictionaryDirTemp();
bool LookupOfflineWord(Str word, Vec<OfflineMeaning>& out, Str* error);
void FreeOfflineMeanings(Vec<OfflineMeaning>&);
void GetDictionaryCatalog(Vec<OfflineDictPack>&);
void FreeDictionaryCatalog(Vec<OfflineDictPack>&);
bool DownloadDictionaryPack(Str id, Str* error);
bool InstallDictionaryFile(Str sourcePath, Str* error);
bool RemoveDictionaryPack(Str id, Str* error);
void ResetOfflineDictionaryCache();
#if IS_DEBUG
bool OfflineDictionary_UnitTests();
#endif

#endif
