/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3. */

#ifndef SUMATRA_VOCABULARY_DIALOG_H
#define SUMATRA_VOCABULARY_DIALOG_H

struct MainWindow;
void ShowDictionaryDialog(MainWindow*, Str word = {}, Str context = {}, Str source = {}, int page = 0);
void ShowVocabularyDialog(MainWindow*);
void RefreshVocabularyDialogs();
void CloseVocabularyDialogs(MainWindow*);

#endif
