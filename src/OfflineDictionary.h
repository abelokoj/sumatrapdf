/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3 */

#ifndef SUMATRA_OFFLINE_DICTIONARY_H
#define SUMATRA_OFFLINE_DICTIONARY_H

struct KaikkiProgress;

struct OfflineMeaning {
    Str headword;
    Str definition;
    Str dictionary;
    Str dictionaryId;
    Str partOfSpeech;
    Str example;
    Str synonyms;
    Str antonyms;
    Str phonetic;
    Str phoneticUk;
    Str audioUrl;
    Str audioUrlUk;
    Str sourceUrl;
    Str license;
};

enum class DictionarySource {
    Offline,
    Wiktionary,
    WiktionaryRest,
    FreeDictionary,
    OnlineFallback
};

struct OfflineDictPack {
    Str id;
    Str title;
    Str language;
    Str license;
    Str sourceUrl;
    bool installed = false;
    bool bundled = false;
    i64 downloadBytes = 0;
    i64 expandedBytes = 0;
    bool compressed = false;
};

TempStr GetDictionaryDirTemp();
bool LookupOfflineWord(Str word, Vec<OfflineMeaning>& out, Str* error);
bool LookupDictionaryWord(Str word, DictionarySource source, Vec<OfflineMeaning>& out, Str* error);
TempStr DictionaryPlainText(Str definition);
TempStr DictionaryMeaningText(const Vec<OfflineMeaning>& meanings);
void FreeOfflineMeanings(Vec<OfflineMeaning>&);
void GetDictionaryCatalog(Vec<OfflineDictPack>&);
void FreeDictionaryCatalog(Vec<OfflineDictPack>&);
bool GetDictionaryDownloadSize(Str id, i64& bytes, Str* error);
bool DownloadDictionaryPack(Str id, Str* error, const Func1<KaikkiProgress*>& progress = {},
                            const Func1<bool*>& cancelled = {}, i64 approvedBytes = 0);
bool InstallDictionaryFile(Str sourcePath, Str* error);
bool RemoveDictionaryPack(Str id, Str* error);
void ResetOfflineDictionaryCache();
#if IS_DEBUG
bool OfflineDictionary_UnitTests();
#endif

#endif
