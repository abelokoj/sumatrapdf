/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3. */

#include "base/Base.h"
#include "base/Win.h"
#include "base/File.h"
#include "base/UITask.h"
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/PlatformFont.h"

#include "gui/win/WinGui.h"
#include <commdlg.h>
#include <richedit.h>
#include "Settings.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "DarkMode.h"
#include "MainWindow.h"
#include "SumatraPDF.h"
#include "Theme.h"
#include "Translations.h"
#include "KaikkiDictionary.h"
#include "OfflineDictionary.h"
#include "Vocabulary.h"
#include "DictionarySpeech.h"
#include "VocabularyDialog.h"
#if IS_DEBUG
#include "EngineBase.h"
#include "RenderCache.h"
#include "base/tests/UtAssert.h"
#endif

constexpr WCHAR kLearningClass[] = L"SumatraPDFEnhancedLearning";
constexpr WCHAR kChoiceListClass[] = L"SumatraEnhancedChoices";
enum LearningControl {
    lcTitle = 101,
    lcQuery,
    lcLookup,
    lcPack,
    lcImportPack,
    lcDownload,
    lcRemovePack,
    lcDeck,
    lcInstallDeck,
    lcNewDeck,
    lcCreateDeck,
    lcDeleteDeck,
    lcActivity,
    lcScheduler,
    lcPractice,
    lcLibrary,
    lcDetails,
    lcAnswer,
    lcCheck,
    lcReveal,
    lcAgain,
    lcHard,
    lcGood,
    lcEasy,
    lcSave,
    lcLearned,
    lcDeleteWord,
    lcExport,
    lcImport,
    lcStatus,
    lcChoices,
    lcPairs,
    lcBack,
    lcOpenVocabulary,
    lcGuideStart,
    lcGuideText,
    lcGuidePrev,
    lcGuideNext,
    lcGuideSkip,
    lcGuideAction,
    lcFeedback,
    lcSplit,
    lcPronounce,
    lcStopVoice,
    lcVoice,
    lcRecording,
    lcRecordingUk,
    lcLookupSource,
    lcSourcesToggle,
    lcOnlineFirst,
    lcOnlineSecond,
    lcOnlineThird,
    lcSense,
    lcPackInfo,
    lcCancelDownload,
    lcManageToggle,
    lcUndo,
    lcQueryLabel,
    lcDeckLabel,
    lcActivityLabel,
    lcSchedulerLabel,
    lcVoiceLabel,
    lcSourceLabel,
    lcSenseLabel,
    lcReviewHelp,
    lcLearnedHint,
    lcSessionInfo,
    lcLast
};
enum class DetailStyle {
    Body,
    Heading,
    Section,
    Example,
    Muted,
    WordLink
};
struct DetailRun {
    int start, length;
    DetailStyle style;
    Str word;
};
struct LearningPlacement {
    int id;
    RECT bounds;
};
struct LearningWindow {
    HWND hwnd = nullptr;
    MainWindow* owner = nullptr;
    bool dictionary = false, practice = false, revealed = false, checked = false;
    bool busy = false, updating = false, ready = false, lookupBusy = false;
    bool opening = true;
    int scrollY = 0, contentHeight = 0, wheelRemainder = 0;
    bool layingOut = false, layoutPending = false;
    Vec<LearningPlacement> placements;
    RECT appliedBounds[lcLast]{};
    bool boundsValid[lcLast]{};
    bool guideVisible = false;
    bool sourcesVisible = false, managementVisible = false, splitting = false;
    int promptShare = 30, splitHeight = 0;
    int splitPointerY = 0, splitPromptHeight = 0, splitStartShare = 30;
    int onlineOrder[3]{1, 2, 3};
    Vec<bool> deckInstalled;
    Vec<DetailRun> detailRuns;
    DictionarySpeech* speech = nullptr;
    HANDLE cancelDownload = nullptr;
    int guideStep = 0, feedbackKind = 0;
    ULONGLONG feedbackStart = 0;
    HICON smallIcon = nullptr, largeIcon = nullptr;
    HWND hoverButton = nullptr;
    Tooltip* buttonTooltip = nullptr;
    HWND tooltipButton = nullptr;
    HFONT titleFont = nullptr;
    int serial = 0, ticket = 0, page = 0, position = 0, correct = 0;
    HWND controls[lcLast]{};
    HBRUSH background = nullptr, fieldBackground = nullptr;
    Str context, source;
    Vec<OfflineDictPack> packs;
    Vec<OfflineMeaning> meanings;
    StrVec wordIds, deckIds, session, pairWords, pairDefinitions;
    VocabularyQuestion* question = nullptr;
    ~LearningWindow() {
        if (cancelDownload) SetEvent(cancelDownload);
        str::Free(context);
        str::Free(source);
        FreeOfflineMeanings(meanings);
        FreeDictionaryCatalog(packs);
        delete question;
        delete speech;
        delete buttonTooltip;
        for (auto& run : detailRuns) str::Free(run.word);
        DeleteObject(titleFont);
        DestroyIcon(smallIcon);
        DestroyIcon(largeIcon);
        DeleteObject(background);
        DeleteObject(fieldBackground);
    }
};
static Vec<LearningWindow*> gLearningWindows;
static int gLearningSerial = 0;

static HWND Control(LearningWindow* w, int id) {
    return w->controls[id];
}

static void LayoutLearning(LearningWindow* w, bool keepAnchor = false);
static void EnsurePracticeControls(LearningWindow* w);
static void EnsureGuideControls(LearningWindow* w);
static void EnsureLearningTools(LearningWindow* w);
static void EnsureOnlineControls(LearningWindow* w);

constexpr int kGuideSteps = 9;
constexpr UINT_PTR kFeedbackTimer = 1;
constexpr int kFeedbackDuration = 480;
struct GuideStep {
    const char* title;
    const char* text;
    bool dictionary;
    int control;
    const char* action;
};
static const GuideStep kGuide[] = {
    {"Look up a word",
     "Select a word in your PDF and press Shift+D, or use Dictionary / meaning in the selection popup. You can also "
     "type a word here and choose Look up. Your PDF context is kept when saving a selected word.",
     true, lcQuery, "Go to lookup"},
    {"Choose a dictionary",
     "Choose a pack to inspect its language, source and license. WordNet is bundled. Download adds another pack only "
     "when you request it; Import accepts supported dictionary files. Offline lookup uses installed packs. For online "
     "lookup, choose a source; Online sources lets you order or disable providers. Nothing is sent as you type.",
     true, lcPack, "Choose pack"},
    {"Read the meanings",
     "Look up your word, then read the definitions and their sources. If no meaning is found, check the spelling or "
     "install a pack for that language. Meanings are numbered, with examples and related words when available. Choose "
     "a meaning to save. Pronounce uses a Windows voice offline; recording buttons play available online audio.",
     true, lcDetails, "Read results"},
    {"Save with context",
     "Choose a deck, then Save word to keep the definition, selected PDF context and source page. Mark learned saves "
     "it as already learned. Open learning hub takes you to your library.",
     true, lcSave, "Go to Save word"},
    {"Explore your library",
     "Search filters your saved words; the deck selector limits the library to one deck. Select a word to read its "
     "meaning and source. Mark learned toggles learned status; learned words are excluded from practice until marked "
     "unlearned.",
     false, lcQuery, "Open library"},
    {"Choose a study deck",
     "Select a built-in deck and choose Install deck to add its words. Create deck makes a personal deck. Import "
     "restores a vocabulary backup or imports a supported study pack. Read the deck source and license before "
     "installing.",
     false, lcDeck, "Choose deck"},
    {"Practice and grade",
     "Choose Flashcards, Meaning quiz, Word quiz, Spelling, Word scramble or Matching pairs, then Practice. Quizzes "
     "use Check answer and Next word. Flashcards use Show answer, then Again / Hard / Good / Easy. Matching removes "
     "correct pairs. Feedback explains the result without relying on color. Drag the divider between the word and "
     "answers or focus it and use Up / Down. Home resets the panels. Right-click the divider for sizing actions.",
     false, lcActivity, "Choose practice mode"},
    {"Review due words",
     "The library status shows how many words are due. Practice reviews due words first and also includes words due "
     "within the next day. Choose SM-2 or Leitner; your grades determine when words return. Again means the word needs "
     "another review.",
     false, lcPractice, "Go to Practice"},
    {"Keep a backup",
     "Export saves a JSON backup of words, decks, context and review progress. Import restores it or adds a supported "
     "vocabulary pack. Keep a copy somewhere safe before changing devices. Use Back to revisit a step, or Finish to "
     "close this guide; Start guide is always available.",
     false, lcExport, "Go to Export"}};
static int gGuideProgress[2]{};
static bool gGuideLoaded = false;
static TempStr GuidePath() {
    return path::JoinTemp(path::GetDirTemp(VocabularyStorePathTemp()), StrL("SumatraPDF-learning-guide.txt"));
}
static void LoadGuideProgress() {
    if (gGuideLoaded) return;
    gGuideLoaded = true;
    if (!CanAccessDisk() || gDontSaveSettings) return;
    Str bytes = file::ReadFile(GuidePath());
    defer {
        str::Free(bytes);
    };
    if (len(bytes) == 2 && bytes.s[0] >= '0' && bytes.s[0] <= '8' && bytes.s[1] >= '0' && bytes.s[1] <= '8') {
        gGuideProgress[0] = bytes.s[0] - '0';
        gGuideProgress[1] = bytes.s[1] - '0';
    }
}
static void SaveGuideProgress(LearningWindow* w) {
    gGuideProgress[w->dictionary ? 0 : 1] = w->guideStep;
    if (!HasPermission(Perm::SavePreferences) || !CanAccessDisk() || gDontSaveSettings) return;
    char bytes[2] = {(char)('0' + gGuideProgress[0]), (char)('0' + gGuideProgress[1])};
    TempStr temp = fmt("%s.tmp", GuidePath());
    if (file::WriteFile(temp, Str(bytes, 2))) {
        if (!MoveFileExW(CWStrTemp(temp), CWStrTemp(GuidePath()), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            DeleteFileW(CWStrTemp(temp));
    }
}
static void UpdateGuide(LearningWindow* w);
static void UpdateLearningChrome(LearningWindow* w);
static void Feedback(LearningWindow* w, bool correct, Str message);
static void SetLearningIcons(LearningWindow* w);
static int LearningIconSize();
static int LearningRowHeight() {
    int content = std::max(PlatformFontLineHeight(GetAppFontForDpi(DpiGet())), LearningIconSize());
    return std::max(UiScalePx(32), content + UiScalePx(16));
}
static bool HasLearningGlyph(int id);
static void FitPackDropdown(LearningWindow* w);
static void DrawGreenCheck(HDC dc, RECT rc, Color background);
static void FormatDetails(LearningWindow* w);
static void ShowMeanings(LearningWindow* w);
static void LookupWord(LearningWindow* w);
static void RefreshVoices(LearningWindow* w);
constexpr UINT kSpeechMessage = WM_APP + 0x541;
static TempStr Read(LearningWindow* w, int id);
static int Selected(LearningWindow* w, int id);
static void AddChoice(LearningWindow* w, int id, Str text);
static VocabularyWord* SelectedWord(LearningWindow* w);

static bool IsRichDetails(HWND child) {
    WCHAR klass[32]{};
    GetClassNameW(child, klass, dimof(klass));
    return _wcsicmp(klass, L"RICHEDIT50W") == 0;
}
static TempStr LearningPrefsPath() {
    return path::JoinTemp(path::GetDirTemp(VocabularyStorePathTemp()), StrL("SumatraPDF-learning-layout.txt"));
}
static void LoadLearningPrefs(LearningWindow* w) {
    if (!CanAccessDisk() || gDontSaveSettings) return;
    Str bytes = file::ReadFile(LearningPrefsPath());
    defer {
        str::Free(bytes);
    };
    int share, first, second, third;
    if (len(bytes) > 0 && len(bytes) <= 64 &&
        sscanf_s(CStrTemp(bytes), "%d %d %d %d", &share, &first, &second, &third) == 4 && share >= 10 && share <= 80 &&
        first >= 0 && first <= 3 && second >= 0 && second <= 3 && third >= 0 && third <= 3) {
        w->promptShare = share;
        w->onlineOrder[0] = first;
        w->onlineOrder[1] = second;
        w->onlineOrder[2] = third;
    }
}
static void SaveLearningPrefs(LearningWindow* w) {
    if (!HasPermission(Perm::SavePreferences) || !CanAccessDisk() || gDontSaveSettings) return;
    TempStr pending = fmt("%s.tmp", LearningPrefsPath());
    if (file::WriteFile(pending,
                        fmt("%d %d %d %d", w->promptShare, w->onlineOrder[0], w->onlineOrder[1], w->onlineOrder[2]))) {
        if (!MoveFileExW(CWStrTemp(pending), CWStrTemp(LearningPrefsPath()),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            DeleteFileW(CWStrTemp(pending));
    }
}
static void RefreshVoices(LearningWindow* w) {
    if (!w->speech) return;
    int selected = Selected(w, lcVoice);
    StrVec names;
    w->speech->GetVoiceNames(names);
    bool updating = w->updating;
    w->updating = true;
    SendMessageW(Control(w, lcVoice), CB_RESETCONTENT, 0, 0);
    for (Str name : names) AddChoice(w, lcVoice, name);
    SendMessageW(Control(w, lcVoice), CB_SETCURSEL, std::clamp(selected, 0, std::max(0, len(names) - 1)), 0);
    w->updating = updating;
}
static Str PronunciationWord(LearningWindow* w) {
    if (w->dictionary) return Read(w, lcQuery);
    if (w->practice && w->question) {
        VocabularyWord* word = VocabularyFind(w->question->wordId);
        return word ? word->word : Str{};
    }
    VocabularyWord* word = SelectedWord(w);
    return word ? word->word : Str{};
}

static void Text(LearningWindow* w, int id, Str s) {
    if (str::Eq(HwndGetTextTemp(Control(w, id)), s)) return;
    SetWindowTextW(Control(w, id), CWStrTemp(s));
    if (id == lcDetails && IsRichDetails(Control(w, id))) {
        for (auto& run : w->detailRuns) str::Free(run.word);
        VecReset(w->detailRuns);
        FormatDetails(w);
    }
    if (w->ready && !w->updating && id != lcDetails && id != lcQuery && id != lcAnswer && id != lcNewDeck) {
        LayoutLearning(w);
    }
}

static TempStr Read(LearningWindow* w, int id) {
    return HwndGetTextTemp(Control(w, id));
}

static int Selected(LearningWindow* w, int id) {
    return (int)SendMessageW(Control(w, id), CB_GETCURSEL, 0, 0);
}

static void AddChoice(LearningWindow* w, int id, Str text) {
    SendMessageW(Control(w, id), CB_ADDSTRING, 0, (LPARAM)CWStrTemp(text));
}

static void Status(LearningWindow* w, Str text) {
    Text(w, lcStatus, text);
}

static TempStr RichText(Str value) {
    str::Builder text;
    for (int i = 0; i < len(value); i++) {
        char c = value.s[i];
        if (c == '\r' && i + 1 < len(value) && value.s[i + 1] == '\n') i++;
        text.AppendChar(c == '\n' ? '\r' : c);
    }
    return ToStrTemp(text);
}
static void FormatDetails(LearningWindow* w) {
    HWND child = Control(w, lcDetails);
    if (!IsRichDetails(child)) return;
    CHARRANGE selection{};
    SendMessageW(child, EM_EXGETSEL, 0, (LPARAM)&selection);
    POINT scroll{};
    SendMessageW(child, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
    LOGFONTW font{};
    GetObjectW(GetAppFontForDpi(DpiGet())->GetHFont(), sizeof(font), &font);
    CHARFORMAT2W base{};
    base.cbSize = sizeof(base);
    base.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_LINK;
    base.yHeight = MulDiv(abs(font.lfHeight), 1440, DpiGet());
    base.crTextColor = ThemeWindowTextColor();
    wcscpy_s(base.szFaceName, font.lfFaceName);
    SendMessageW(child, WM_SETREDRAW, FALSE, 0);
    SendMessageW(child, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&base);
    SendMessageW(child, EM_SETBKGNDCOLOR, 0, ThemeControlBackgroundColor());
    PARAFORMAT2 paragraph{};
    paragraph.cbSize = sizeof(paragraph);
    paragraph.dwMask = PFM_SPACEAFTER | PFM_LINESPACING;
    paragraph.dySpaceAfter = base.yHeight / 5;
    paragraph.bLineSpacingRule = 5;
    paragraph.dyLineSpacing = 23;
    CHARRANGE all{0, -1};
    SendMessageW(child, EM_EXSETSEL, 0, (LPARAM)&all);
    SendMessageW(child, EM_SETPARAFORMAT, 0, (LPARAM)&paragraph);
    auto apply = [&](int start, int length, DetailStyle style) {
        CHARRANGE range{start, start + length};
        SendMessageW(child, EM_EXSETSEL, 0, (LPARAM)&range);
        CHARFORMAT2W format = base;
        if (style == DetailStyle::Heading || style == DetailStyle::Section) format.dwEffects |= CFE_BOLD;
        if (style == DetailStyle::Heading) format.yHeight = base.yHeight * 6 / 5;
        if (style == DetailStyle::Example) format.dwEffects |= CFE_ITALIC;
        if (style == DetailStyle::Muted) format.crTextColor = ThemeWindowDarkerTextColor();
        if (style == DetailStyle::WordLink) format.dwEffects |= CFE_LINK;
        SendMessageW(child, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
    };
    if (!len(w->detailRuns)) {
        WStr value = ToWStrTemp(RichText(Read(w, lcDetails)));
        int first = 0;
        while (first < len(value) && value.s[first] != '\r') first++;
        if (first < len(value)) apply(0, first, DetailStyle::Heading);
    } else {
        for (const auto& run : w->detailRuns) apply(run.start, run.length, run.style);
    }
    SendMessageW(child, EM_EXSETSEL, 0, (LPARAM)&selection);
    SendMessageW(child, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(child, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(child, nullptr, false);
}
struct DetailBuilder {
    LearningWindow* window;
    str::Builder text;
    int characters = 0;
    explicit DetailBuilder(LearningWindow* w) : window(w) {
        for (auto& run : w->detailRuns) str::Free(run.word);
        VecReset(w->detailRuns);
    }
    void Add(Str value, DetailStyle style = DetailStyle::Body, Str word = {}) {
        Str normalized = RichText(value);
        int length = len(ToWStrTemp(normalized));
        if (length) VecAppend(window->detailRuns, DetailRun{characters, length, style, str::Dup(word)});
        text.Append(normalized);
        characters += length;
    }
    void Line(Str value, DetailStyle style = DetailStyle::Body) {
        Add(value, style);
        Add(StrL("\r"));
    }
    void Links(Str label, Str words) {
        if (!len(words)) return;
        Add(fmt("%s: ", label), DetailStyle::Section);
        StrVec values;
        Split(&values, words, StrL("\n"), true);
        for (int i = 0; i < len(values); i++) {
            if (i) Add(StrL(", "));
            Add(values[i], DetailStyle::WordLink, values[i]);
        }
        Add(StrL("\r"));
    }
    void Finish() {
        SetWindowTextW(Control(window, lcDetails), CWStrTemp(ToStrTemp(text)));
        FormatDetails(window);
        SendMessageW(Control(window, lcDetails), EM_SETSEL, 0, 0);
        SendMessageW(Control(window, lcDetails), EM_SCROLL, SB_TOP, 0);
    }
};
static void ShowMeanings(LearningWindow* w) {
    if (!len(w->meanings)) return;
    DetailBuilder text(w);
    text.Line(w->meanings[0].headword, DetailStyle::Heading);
    Str dictionary, part;
    int number = 0;
    for (const auto& meaning : w->meanings) {
        if (!str::Eq(dictionary, meaning.dictionaryId)) {
            text.Line({});
            text.Line(meaning.dictionary, DetailStyle::Section);
            if (len(meaning.phonetic)) text.Line(meaning.phonetic, DetailStyle::Example);
            if (len(meaning.phoneticUk) && !str::Eq(meaning.phoneticUk, meaning.phonetic))
                text.Line(fmt("UK: %s", meaning.phoneticUk), DetailStyle::Example);
            if (len(meaning.sourceUrl)) text.Line(fmt("%s: %s", Tr("Source"), meaning.sourceUrl), DetailStyle::Muted);
            if (len(meaning.license)) text.Line(meaning.license, DetailStyle::Muted);
            dictionary = meaning.dictionaryId;
            part = {};
            number = 0;
        }
        if (!number || !str::Eq(part, meaning.partOfSpeech)) {
            text.Line(len(meaning.partOfSpeech) ? meaning.partOfSpeech : Tr("Meanings"), DetailStyle::Section);
            part = meaning.partOfSpeech;
        }
        text.Line(fmt("%d. %s", ++number, meaning.definition));
        if (len(meaning.example)) text.Line(fmt("%s: %s", Tr("Example"), meaning.example), DetailStyle::Example);
        text.Links(Tr("Synonyms"), meaning.synonyms);
        text.Links(Tr("Antonyms"), meaning.antonyms);
        text.Line({});
    }
    if (len(w->context)) {
        text.Line(Tr("PDF context"), DetailStyle::Section);
        text.Line(w->context, DetailStyle::Example);
    }
    if (len(w->source))
        text.Line(fmt("%s: %s · %s %d", Tr("Document"), w->source, Tr("page"), w->page), DetailStyle::Muted);
    text.Finish();
}
static void StoredDefinition(DetailBuilder& text, Str definition) {
    StrVec lines;
    Split(&lines, DictionaryPlainText(definition), StrL("\n"), true);
    for (Str line : lines) {
        DetailStyle style = DetailStyle::Body;
        if (str::StartsWithI(line, StrL("Example:"))) style = DetailStyle::Example;
        if (str::EqI(line, StrL("noun")) || str::EqI(line, StrL("verb")) || str::EqI(line, StrL("adjective")) ||
            str::EqI(line, StrL("adverb")))
            style = DetailStyle::Section;
        text.Line(line, style);
    }
}

static void UpdateGuide(LearningWindow* w) {
    bool updating = w->updating;
    w->updating = true;
    if (w->guideVisible) EnsureGuideControls(w);
    w->guideStep = std::clamp(w->guideStep, 0, kGuideSteps - 1);
    const GuideStep& step = kGuide[w->guideStep];
    Text(w, lcGuideText, fmt("Step %d of %d: %s\r\n%s", w->guideStep + 1, kGuideSteps, Tr(step.title), Tr(step.text)));
    Text(w, lcGuideNext, w->guideStep == kGuideSteps - 1 ? Tr("Finish") : Tr("Next"));
    Text(w, lcGuideAction, Tr(step.action));
    Text(w, lcGuideStart, w->guideVisible ? Tr("Hide guide") : Tr("Help / Start guide"));
    for (int id : {lcGuideText, lcGuidePrev, lcGuideNext, lcGuideSkip, lcGuideAction}) {
        ShowWindow(Control(w, id), w->guideVisible ? SW_SHOW : SW_HIDE);
    }
    EnableWindow(Control(w, lcGuidePrev), w->guideStep > 0);
    SaveGuideProgress(w);
    w->updating = updating;
    LayoutLearning(w);
}
static void Feedback(LearningWindow* w, bool correct, Str message) {
    KillTimer(w->hwnd, kFeedbackTimer);
    w->feedbackKind = correct ? 1 : -1;
    w->feedbackStart = GetTickCount64();
    Text(w, lcFeedback, fmt("%s  %s", correct ? StrL("✓") : StrL("✕"), message));
    ShowWindow(Control(w, lcFeedback), SW_SHOW);
    BOOL animate = FALSE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
    if (animate)
        SetTimer(w->hwnd, kFeedbackTimer, 40, nullptr);
    else
        w->feedbackStart = 0;
    LayoutLearning(w);
    InvalidateRect(Control(w, lcFeedback), nullptr, true);
}

static void Place(LearningWindow* w, int id, int x, int y, int dx, int dy) {
    if (Control(w, id)) {
        VecAppend(w->placements, LearningPlacement{id, {x, y, x + std::max(dx, 1), y + std::max(dy, 1)}});
    }
}
static void ApplyLearningPositions(LearningWindow* w) {
    bool changed[lcLast]{}, resized[lcLast]{};
    RECT next[lcLast]{};
    int count = 0;
    for (const auto& item : w->placements) {
        int id = item.id;
        next[id] = item.bounds;
        OffsetRect(&next[id], 0, -w->scrollY);
        if (w->boundsValid[id] && EqualRect(&next[id], &w->appliedBounds[id])) continue;
        changed[id] = true;
        resized[id] = !w->boundsValid[id] ||
                      next[id].right - next[id].left != w->appliedBounds[id].right - w->appliedBounds[id].left ||
                      next[id].bottom - next[id].top != w->appliedBounds[id].bottom - w->appliedBounds[id].top;
        count++;
    }
    if (!count) return;
    HDWP batch = BeginDeferWindowPos(count);
    constexpr UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS;
    for (int id = 0; batch && id < lcLast; id++) {
        if (!changed[id]) continue;
        RECT rc = next[id];
        batch = DeferWindowPos(batch, Control(w, id), nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                               flags | (resized[id] ? 0 : SWP_NOSIZE));
    }
    if (!batch || !EndDeferWindowPos(batch)) {
        for (int id = 0; id < lcLast; id++) {
            if (!changed[id]) continue;
            RECT rc = next[id];
            SetWindowPos(Control(w, id), nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                         flags | (resized[id] ? 0 : SWP_NOSIZE));
        }
    }
    for (int id = 0; id < lcLast; id++) {
        if (!changed[id]) continue;
        w->appliedBounds[id] = next[id];
        w->boundsValid[id] = true;
        if (!resized[id]) continue;
        HWND child = Control(w, id);
        WCHAR klass[32]{};
        GetClassNameW(child, klass, dimof(klass));
        bool edit = _wcsicmp(klass, L"EDIT") == 0 || IsRichDetails(child);
        if (!edit && _wcsicmp(klass, kChoiceListClass) != 0 && id != lcFeedback) continue;
        int dx = next[id].right - next[id].left, dy = next[id].bottom - next[id].top;
        int diameter = std::min(2 * GetAppCornerRadius(DpiGetForHwnd(child), 6), std::min(dx, dy));
        HRGN region = CreateRoundRectRgn(0, 0, dx + 1, dy + 1, diameter, diameter);
        if (!SetWindowRgn(child, region, FALSE)) DeleteObject(region);
        if (edit && (GetWindowLongPtrW(child, GWL_STYLE) & ES_MULTILINE)) {
            int line = (int)SendMessageW(child, EM_GETFIRSTVISIBLELINE, 0, 0);
            int anchor = (int)SendMessageW(child, EM_LINEINDEX, line, 0);
            RECT text{UiScalePx(10), UiScalePx(8), dx - UiScalePx(10) - AppScrollbarInset(child), dy - UiScalePx(8)};
            SendMessageW(child, EM_SETRECTNP, 0, (LPARAM)&text);
            int target = (int)SendMessageW(child, EM_LINEFROMCHAR, std::max(anchor, 0), 0);
            int current = (int)SendMessageW(child, EM_GETFIRSTVISIBLELINE, 0, 0);
            SendMessageW(child, EM_LINESCROLL, 0, target - current);
        }
    }
    RedrawWindow(w->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
static int WheelDistance(WPARAM wp, int distance, int& remainder) {
    int delta = GET_WHEEL_DELTA_WPARAM(wp);
    LONGLONG amount = (LONGLONG)delta * distance + remainder;
    remainder = (int)(amount % WHEEL_DELTA);
    return (int)(amount / WHEEL_DELTA);
}
static int WheelStep(int viewport) {
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    return lines == WHEEL_PAGESCROLL ? viewport : UiScalePx(32) * (int)std::min(lines, (UINT)100);
}
static bool LearningIconOnlyButton(int id) {
    return id == lcPronounce || id == lcStopVoice;
}
static Str LearningButtonCaption(HWND control, int id) {
    if (LearningIconOnlyButton(id)) return {};
    if (id == lcRecording) return StrL("US");
    if (id == lcRecordingUk) return StrL("UK");
    return HwndGetTextTemp(control);
}
static int LearningTextWidth(HWND control) {
    HDC dc = PlatformFontMeasurementDC();
    HFONT font = (HFONT)SendMessageW(control, WM_GETFONT, 0, 0);
    HGDIOBJ old = SelectObject(dc, font ? font : GetAppFont()->GetHFont());
    WStr text = ToWStrTemp(LearningButtonCaption(control, GetDlgCtrlID(control)));
    SIZE size{};
    GetTextExtentPoint32W(dc, CWStrTemp(text), len(text), &size);
    SelectObject(dc, old);
    return size.cx;
}
static int LearningInputWidth(LearningWindow* w, int id, int available, int row) {
    Str value = Read(w, id);
    if (!len(value)) {
        WCHAR cue[256]{};
        if (SendMessageW(Control(w, id), EM_GETCUEBANNER, (WPARAM)cue, dimof(cue))) value = ToUtf8Temp(cue);
    }
    return std::min(available, EditPreferredWidth(Control(w, id), value, row * 3, available) + UiScalePx(12));
}
static void FitLearningInput(LearningWindow* w, int id) {
    if (!w->ready || w->layingOut || (id != lcQuery && id != lcAnswer && id != lcNewDeck)) return;
    LearningPlacement* input = nullptr;
    for (auto& item : w->placements) {
        if (item.id == id) input = &item;
    }
    if (!input) return;
    RECT client;
    GetClientRect(w->hwnd, &client);
    int available = client.right - UiScalePx(16) - input->bounds.left;
    for (const auto& item : w->placements) {
        if (item.id != id && item.bounds.top == input->bounds.top && item.bounds.left > input->bounds.left) {
            available -= item.bounds.right - item.bounds.left + UiScalePx(10);
        }
    }
    int row = std::max(UiScalePx(32), GetAppFontSize() + UiScalePx(16));
    int oldWidth = input->bounds.right - input->bounds.left;
    int width = LearningInputWidth(w, id, std::max(available, 1), row);
    int shift = width - oldWidth;
    if (!shift) return;
    for (auto& item : w->placements) {
        if (item.id != id && item.bounds.top == input->bounds.top && item.bounds.left > input->bounds.left) {
            OffsetRect(&item.bounds, shift, 0);
        }
    }
    input->bounds.right = input->bounds.left + width;
    ApplyLearningPositions(w);
}
static void ScrollLearning(LearningWindow* w, int requested) {
    RECT client;
    GetClientRect(w->hwnd, &client);
    int offset = std::clamp(requested, 0, std::max(0, w->contentHeight - (int)client.bottom));
    if (offset == w->scrollY) return;
    w->scrollY = offset;
    SCROLLINFO scroll{sizeof(scroll), SIF_POS};
    scroll.nPos = offset;
    SetScrollInfo(w->hwnd, SB_VERT, &scroll, true);
    ApplyLearningPositions(w);
}

static void Visible(LearningWindow* w, int id, bool value) {
    ShowWindow(Control(w, id), value ? SW_SHOW : SW_HIDE);
}
static Str CurrentDeck(LearningWindow* w) {
    int i = Selected(w, lcDeck);
    return i >= 0 && i < len(w->deckIds) ? w->deckIds[i] : Str{};
}
static VocabularyWord* SelectedWord(LearningWindow* w) {
    int i = (int)SendMessageW(Control(w, lcLibrary), LB_GETCURSEL, 0, 0);
    return i >= 0 && i < len(w->wordIds) ? VocabularyFind(w->wordIds[i]) : nullptr;
}
static void RefreshDecks(LearningWindow* w) {
    Str previous = str::Dup(CurrentDeck(w));
    w->updating = true;
    SendMessageW(Control(w, lcDeck), CB_RESETCONTENT, 0, 0);
    w->deckIds.Reset();
    VecReset(w->deckInstalled);
    AddChoice(w, lcDeck, Tr("All vocabulary"));
    w->deckIds.Append({});
    VecAppend(w->deckInstalled, false);
    for (VocabularyDeck* deck : VocabularyDecks()) {
        int count = 0, total = 0;
        bool installed = VocabularyDeckInstalled(deck->id, &count, &total);
        Str caption = count > 0 && total > count ? fmt("%s (%d/%d)", deck->name, count, total) : deck->name;
        AddChoice(w, lcDeck, installed ? fmt("%s · %s", caption, Tr("installed")) : caption);
        w->deckIds.Append(deck->id);
        VecAppend(w->deckInstalled, installed);
    }
    int i = w->deckIds.Find(previous);
    SendMessageW(Control(w, lcDeck), CB_SETCURSEL, std::max(i, 0), 0);
    str::Free(previous);
    w->updating = false;
    FitPackDropdown(w);
}
static TempStr DictionarySize(i64 bytes) {
    if (bytes >= 1000000000) return fmt("%.2f GB", (double)bytes / 1000000000);
    if (bytes >= 1000000) return fmt("%.2f MB", (double)bytes / 1000000);
    if (bytes >= 1000) return fmt("%.2f kB", (double)bytes / 1000);
    return fmt("%lld bytes", bytes);
}
static void PackDownloadInfo(LearningWindow* w) {
    int i = Selected(w, lcPack);
    bool sized = i >= 0 && i < len(w->packs) && w->packs[i].downloadBytes > 0;
    if (sized) {
        const auto& p = w->packs[i];
        Text(w, lcPackInfo,
             fmt("%s: approximately %s%s to download; %s dictionary data plus a disk index. "
                 "The current download size is confirmed before downloading.",
                 p.title, DictionarySize(p.downloadBytes), p.compressed ? StrL(" compressed") : Str(),
                 DictionarySize(p.expandedBytes)));
    }
    Visible(w, lcPackInfo, sized && w->managementVisible);
    if (w->ready) LayoutLearning(w, true);
}
static void RefreshPacks(LearningWindow* w) {
    if (!Control(w, lcPack)) return;
    int selected = Selected(w, lcPack);
    Str previous = selected >= 0 && selected < len(w->packs) ? str::Dup(w->packs[selected].id) : Str();
    GetDictionaryCatalog(w->packs);
    bool updating = w->updating;
    w->updating = true;
    SendMessageW(Control(w, lcPack), CB_RESETCONTENT, 0, 0);
    int next = 0;
    for (int i = 0; i < len(w->packs); i++) {
        const auto& pack = w->packs[i];
        AddChoice(w, lcPack, fmt("%s · %s", pack.title, pack.installed ? Tr("installed") : Tr("not installed")));
        if (str::Eq(pack.id, previous)) next = i;
    }
    SendMessageW(Control(w, lcPack), CB_SETCURSEL, next, 0);
    w->updating = updating;
    str::Free(previous);
    FitPackDropdown(w);
    PackDownloadInfo(w);
}
static void WordDetails(LearningWindow* w) {
    VocabularyWord* word = SelectedWord(w);
    EnableWindow(Control(w, lcLearned), word != nullptr);
    EnableWindow(Control(w, lcDeleteWord), word != nullptr);
    if (!word) {
        Text(w, lcDetails,
             Tr("No saved words here yet. Select a word in a document and press Shift+D to look it up and save it."));
        return;
    }
    DetailBuilder text(w);
    text.Line(word->word, DetailStyle::Heading);
    text.Line({});
    StoredDefinition(text, word->definition);
    if (len(word->context)) {
        text.Line({});
        text.Line(Tr("PDF context"), DetailStyle::Section);
        text.Line(word->context, DetailStyle::Example);
    }
    if (len(word->sourcePath)) {
        text.Line(fmt("%s: %s · %s %d", Tr("Document"), word->sourcePath, Tr("page"), word->page), DetailStyle::Muted);
    }
    text.Line(fmt("Reviews: %d · lapses: %d · interval: %d days", word->reviews, word->lapses, word->intervalDays),
              DetailStyle::Muted);
    text.Finish();
    Text(w, lcLearned, word->learned ? Tr("Mark unlearned") : Tr("Mark learned"));
}
enum class LibraryRefresh {
    Data,
    Query
};
static bool SetLibraryRows(LearningWindow* w, const Vec<VocabularyWord*>& words, Str previous);
static void ClearLibraryHeights(HWND control);
static void RefreshLibrary(LearningWindow* w, LibraryRefresh refresh = LibraryRefresh::Data) {
    w->updating = true;
    Vec<VocabularyWord*> words;
    VocabularySearch(Read(w, lcQuery), CurrentDeck(w), false, words);
    Str previous;
    if (auto* word = SelectedWord(w)) previous = str::Dup(word->id);
    defer {
        str::Free(previous);
    };
    if (refresh == LibraryRefresh::Data) ClearLibraryHeights(Control(w, lcLibrary));
    bool changed = SetLibraryRows(w, words, previous);
    int due = VocabularyDueCount(CurrentDeck(w));
    Status(w,
           len(words)
               ? fmt("%d saved words · %d due for review", len(words), due)
               : Tr("Start by selecting a built-in deck and Install deck, or save a word while reading with Shift+D."));
    VocabularyWord* selected = SelectedWord(w);
    if (!w->practice && (refresh == LibraryRefresh::Data || !str::Eq(previous, selected ? selected->id : Str{}))) {
        WordDetails(w);
    }
    if (refresh == LibraryRefresh::Data) UpdateLearningChrome(w);
    w->updating = false;
    if (refresh == LibraryRefresh::Data || changed) LayoutLearning(w);
}
static TempStr ChoiceLabel(int index) {
    char letters[16]{};
    int pos = sizeof(letters) - 1;
    do {
        letters[--pos] = (char)('A' + index % 26);
        index = index / 26 - 1;
    } while (index >= 0 && pos > 0);
    return fmt("(%s)", Str(letters + pos));
}
static int WrappedHeight(HWND control, WStr text, int width) {
    HDC dc = PlatformFontMeasurementDC();
    HFONT font = (HFONT)SendMessageW(control, WM_GETFONT, 0, 0);
    HGDIOBJ old = SelectObject(dc, font ? font : GetAppFontForDpi(DpiGet())->GetHFont());
    RECT rc{0, 0, std::max(width, 1), 0};
    DrawTextW(dc, CWStrTemp(text), len(text), &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(dc, old);
    return std::max((int)rc.bottom, GetAppFontSizeForDpi(DpiGet()));
}
static int LabelWidth(HWND control, int index) {
    HDC dc = PlatformFontMeasurementDC();
    HFONT font = (HFONT)SendMessageW(control, WM_GETFONT, 0, 0);
    HGDIOBJ old = SelectObject(dc, font ? font : GetAppFontForDpi(DpiGet())->GetHFont());
    WStr label = ToWStrTemp(ChoiceLabel(index));
    SIZE size{};
    GetTextExtentPoint32W(dc, CWStrTemp(label), len(label), &size);
    SelectObject(dc, old);
    return size.cx + UiScalePx(12);
}
static void WrapChoices(HWND control);
static void MeasureLearning(LearningWindow* w) {
    if (!w->ready) {
        return;
    }
    RECT client;
    GetClientRect(w->hwnd, &client);
    VecReset(w->placements);
    int pad = UiScalePx(16), gap = UiScalePx(8);
    int row = LearningRowHeight();
    int width = std::max((int)client.right - pad * 2, row * 3), y = pad;
    bool measuring = false;
    // Stack overflowing groups instead of shrinking their text or hit targets.
    auto group = [&](std::initializer_list<int> ids) {
        int x = pad, height = row;
        bool found = false;
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            int id = *it;
            HWND child = Control(w, id);
            if (!child || !(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) {
                continue;
            }
            found = true;
            WCHAR klass[32]{};
            GetClassNameW(child, klass, dimof(klass));
            bool combo = _wcsicmp(klass, L"COMBOBOX") == 0;
            bool edit = _wcsicmp(klass, L"EDIT") == 0;
            bool label = id == lcQueryLabel || id == lcDeckLabel || id == lcActivityLabel || id == lcSchedulerLabel ||
                         id == lcVoiceLabel || id == lcSourceLabel || id == lcSenseLabel;
            bool footer = w->dictionary && (id == lcDeck || id == lcSave || id == lcLearned || id == lcOpenVocabulary);
            int inset = footer ? gap : pad;
            int size = combo ? std::max(row * 3, LearningTextWidth(child) + row + gap * 2) : row * 3;
            if (combo && id == lcDeck && !w->dictionary && w->managementVisible) size += LearningIconSize() + gap;
            if (footer && combo) {
                int reserved = 0;
                for (int action : {lcSave, lcLearned, lcOpenVocabulary}) {
                    HWND button = Control(w, action);
                    if (!button || !(GetWindowLongPtrW(button, GWL_STYLE) & WS_VISIBLE)) continue;
                    reserved +=
                        LearningTextWidth(button) + gap * 3 + (HasLearningGlyph(action) ? LearningIconSize() + gap : 0);
                }
                size = std::max(row * 3, LearningTextWidth(child) + row + gap * 2);
                if (width - reserved >= row * 3) size = std::min(size, width - reserved);
            }
            if (!combo && !edit) {
                HDC dc = PlatformFontMeasurementDC();
                HGDIOBJ old = SelectObject(dc, GetAppFontForDpi(DpiGet())->GetHFont());
                WStr text = ToWStrTemp(LearningButtonCaption(child, id));
                SIZE extent{};
                GetTextExtentPoint32W(dc, CWStrTemp(text), len(text), &extent);
                SelectObject(dc, old);
                size =
                    std::min(width, (int)extent.cx + inset * 2 + (HasLearningGlyph(id) ? LearningIconSize() + gap : 0));
            }
            if (label) size = LearningTextWidth(child) + UiScalePx(4);
            if (LearningIconOnlyButton(id)) size = row;
            if (id == lcLearnedHint || id == lcSessionInfo) size = width;
            if (id == lcFeedback) {
                int remaining = pad + width - x;
                size = remaining >= row * 3 ? remaining : width;
            }
            if (edit && (id == lcQuery || id == lcAnswer || id == lcNewDeck)) {
                int action = id == lcQuery ? lcLookup : id == lcAnswer ? lcCheck : lcCreateDeck;
                int reserved = Control(w, action) ? LearningTextWidth(Control(w, action)) + pad * 2 + gap : 0;
                if (Control(w, action) && HasLearningGlyph(action)) reserved += LearningIconSize() + gap;
                size = LearningInputWidth(w, id, std::max(row * 3, width - reserved), row);
            }
            size = std::min(width, size);
            if (combo && it != ids.begin()) {
                int previous = *(it - 1);
                bool named = previous == lcDeckLabel || previous == lcActivityLabel || previous == lcSchedulerLabel ||
                             previous == lcVoiceLabel || previous == lcSourceLabel || previous == lcSenseLabel;
                if (named && x > pad) size = std::min(size, pad + width - x);
            }
            int pairWidth = size;
            if (label && it + 1 != ids.end() && Control(w, *(it + 1))) {
                HWND next = Control(w, *(it + 1));
                pairWidth +=
                    gap + std::min(width - size - gap, std::max(row * 3, LearningTextWidth(next) + row + gap * 2));
            }
            if (x > pad && x + pairWidth > pad + width) {
                y += height + gap;
                x = pad;
                height = row;
            }
            int h =
                std::max(row, WrappedHeight(child, ToWStrTemp(LearningButtonCaption(child, id)),
                                            size - gap * 2 - (HasLearningGlyph(id) ? LearningIconSize() + gap : 0)) +
                                  (footer ? gap : gap * 2));
            if (label || LearningIconOnlyButton(id) || (edit && !(GetWindowLongPtrW(child, GWL_STYLE) & ES_MULTILINE)))
                h = row;
            if (!measuring) Place(w, id, x, y, size, combo ? row * 10 : h);
            height = std::max(height, combo ? row : h);
            x += size + gap;
        }
        if (found) y += height + gap;
    };
    int title = WrappedHeight(Control(w, lcTitle), ToWStrTemp(Read(w, lcTitle)), width - LearningIconSize() - pad);
    Place(w, lcTitle, pad, y, width, title);
    y += title + gap;
    group({lcGuideStart, lcQueryLabel, lcQuery, lcLookup});
    group({lcSessionInfo});
    if (w->guideVisible) {
        int guideHeight =
            WrappedHeight(Control(w, lcGuideText), ToWStrTemp(Read(w, lcGuideText)), width - pad * 2) + pad * 2;
        Place(w, lcGuideText, pad, y, width, guideHeight);
        y += guideHeight + gap;
        group({lcGuidePrev, lcGuideNext, lcGuideSkip, lcGuideAction});
    }
    if (w->dictionary) {
        group({lcSourceLabel, lcLookupSource, lcManageToggle});
        group({lcSourcesToggle});
        if (w->sourcesVisible) {
            group({lcOnlineFirst, lcOnlineSecond, lcOnlineThird});
        }
        group({lcPack, lcImportPack, lcDownload, lcRemovePack, lcCancelDownload});
        if (Control(w, lcPackInfo) && (GetWindowLongPtrW(Control(w, lcPackInfo), GWL_STYLE) & WS_VISIBLE)) {
            int infoHeight = WrappedHeight(Control(w, lcPackInfo), ToWStrTemp(Read(w, lcPackInfo)), width);
            Place(w, lcPackInfo, pad, y, width, infoHeight);
            y += infoHeight + gap;
        }
    } else {
        group({lcDeckLabel, lcDeck, lcManageToggle});
        group({lcInstallDeck, lcDeleteDeck});
        group({lcNewDeck, lcCreateDeck, lcExport, lcImport});
        group({lcActivityLabel, lcActivity, lcSchedulerLabel, lcScheduler, lcPractice, lcReviewHelp});
    }
    int contentStart = y;
    measuring = true;
    y = 0;
    group({lcVoiceLabel, lcVoice, lcPronounce, lcStopVoice, lcRecording, lcRecordingUk});
    if (w->dictionary) {
        group({lcSenseLabel, lcSense});
        group({lcDeckLabel, lcDeck, lcSave, lcLearned, lcOpenVocabulary});
    } else {
        group({lcLearned, lcDeleteWord, lcUndo});
    }
    int footerHeight = y + row * 2 + pad;
    measuring = false;
    y = contentStart;
    int detailHeight = std::max(row * 4, (int)client.bottom - y - footerHeight);
    if (!w->dictionary && !w->practice) {
        if (width < row * 16) {
            Place(w, lcLibrary, pad, y, width, row * 4);
            y += row * 4 + gap;
            Place(w, lcDetails, pad, y, width, detailHeight);
        } else {
            int listWidth = width / 3;
            Place(w, lcLibrary, pad, y, listWidth, detailHeight);
            Place(w, lcDetails, pad + listWidth + gap, y, width - listWidth - gap, detailHeight);
        }
        y += detailHeight + gap;
        group({lcVoiceLabel, lcVoice, lcPronounce, lcStopVoice});
        group({lcLearned, lcDeleteWord, lcUndo});
        group({lcLearnedHint});
    } else if (w->dictionary) {
        Place(w, lcDetails, pad, y, width, detailHeight);
        y += detailHeight + gap;
        group({lcVoiceLabel, lcVoice, lcPronounce, lcStopVoice, lcRecording, lcRecordingUk});
        group({lcSenseLabel, lcSense});
        group({lcDeckLabel, lcDeck, lcSave, lcLearned, lcOpenVocabulary});
    } else {
        bool choices = (GetWindowLongPtrW(Control(w, lcChoices), GWL_STYLE) & WS_VISIBLE) != 0;
        bool matching = Selected(w, lcActivity) == (int)VocabActivity::MatchPairs;
        int start = y;
        measuring = true;
        y = 0;
        group({lcVoiceLabel, lcVoice, lcPronounce, lcStopVoice});
        group({lcAnswer});
        group({lcCheck, lcReveal, lcAgain, lcHard, lcGood, lcEasy, lcBack, lcFeedback});
        int tail = y + WrappedHeight(Control(w, lcStatus), ToWStrTemp(Read(w, lcStatus)), width) + pad;
        measuring = false;
        y = start;
        int listWidth = matching && width >= row * 16 ? (width - gap) / 2 : width;
        int stacks = matching && listWidth == width ? 2 : 1;
        int grip = std::max(UiScalePx(24), LearningIconSize());
        int minimumAnswers = row * 3 * stacks;
        int panels = std::max(row * 2 + minimumAnswers, (int)client.bottom - y - tail - grip - gap * (stacks + 2));
        int prompt = choices ? std::clamp(panels * w->promptShare / 100, row * 2, panels - minimumAnswers)
                             : std::max(row * 3, (int)client.bottom - y - tail - gap);
        w->splitHeight = panels;
        Place(w, lcDetails, pad, y, width, prompt);
        y += prompt + gap;
        if (choices) {
            Place(w, lcSplit, pad, y, width, grip);
            y += grip + gap;
            int listHeight = (panels - prompt) / stacks;
            Place(w, lcChoices, pad, y, listWidth, listHeight);
            if (matching) {
                if (stacks == 2) {
                    y += listHeight + gap;
                    Place(w, lcPairs, pad, y, width, listHeight);
                } else {
                    Place(w, lcPairs, pad + listWidth + gap, y, listWidth, listHeight);
                }
            }
            y += listHeight + gap;
        }
        group({lcVoiceLabel, lcVoice, lcPronounce, lcStopVoice});
        group({lcAnswer});
        group({lcCheck, lcReveal, lcAgain, lcHard, lcGood, lcEasy, lcBack, lcFeedback});
    }
    if (w->feedbackKind && !w->practice) {
        int h = WrappedHeight(Control(w, lcFeedback), ToWStrTemp(Read(w, lcFeedback)), width - pad * 2) + pad;
        Place(w, lcFeedback, pad, y, width, h);
        y += h + gap;
    }
    int status = WrappedHeight(Control(w, lcStatus), ToWStrTemp(Read(w, lcStatus)), width);
    Place(w, lcStatus, pad, y, width, status);
    w->contentHeight = y + status + pad;
}
static void LayoutLearning(LearningWindow* w, bool keepAnchor) {
    if (!w->ready) return;
    if (w->layingOut) {
        w->layoutPending = true;
        return;
    }
    int anchorId = 0, anchorOffset = 0;
    if (keepAnchor && w->scrollY > 0) {
        for (const auto& item : w->placements) {
            HWND child = Control(w, item.id);
            if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
            RECT bounds;
            GetWindowRect(child, &bounds);
            if (item.bounds.top + bounds.bottom - bounds.top > w->scrollY) {
                anchorId = item.id;
                anchorOffset = w->scrollY - item.bounds.top;
                break;
            }
        }
    }
    w->layingOut = true;
    defer {
        w->layingOut = false;
    };
    for (int pass = 0; pass < 2; pass++) {
        w->layoutPending = false;
        RECT client, after;
        GetClientRect(w->hwnd, &client);
        MeasureLearning(w);
        if (anchorId) {
            for (const auto& item : w->placements) {
                if (item.id == anchorId) {
                    w->scrollY = item.bounds.top + anchorOffset;
                    break;
                }
            }
        }
        w->scrollY = std::clamp(w->scrollY, 0, std::max(0, w->contentHeight - (int)client.bottom));
        SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
        scroll.nMax = std::max(0, w->contentHeight - 1);
        scroll.nPage = client.bottom;
        scroll.nPos = w->scrollY;
        SetScrollInfo(w->hwnd, SB_VERT, &scroll, true);
        GetClientRect(w->hwnd, &after);
        if (client.right == after.right && client.bottom == after.bottom && !w->layoutPending) break;
    }
    ApplyLearningPositions(w);
    for (int id : {lcLibrary, lcChoices, lcPairs}) {
        if (Control(w, id) && (GetWindowLongPtrW(Control(w, id), GWL_STYLE) & WS_VISIBLE)) {
            WrapChoices(Control(w, id));
        }
    }
    InvalidateRect(w->hwnd, nullptr, false);
}
static void RevealFocusedControl(LearningWindow* w, HWND child) {
    if (!child || !w->ready) {
        return;
    }
    RECT rc, client;
    GetWindowRect(child, &rc);
    GetClientRect(w->hwnd, &client);
    MapWindowPoints(nullptr, w->hwnd, (POINT*)&rc, 2);
    int margin = UiScalePx(12), offset = w->scrollY;
    if (rc.top < margin || rc.bottom - rc.top > client.bottom - margin * 2) {
        offset += rc.top - margin;
    } else if (rc.bottom > client.bottom - margin) {
        offset += rc.bottom - client.bottom + margin;
    } else {
        return;
    }
    ScrollLearning(w, offset);
}

static void UpdateLearningChrome(LearningWindow* w) {
    bool setup = !w->practice;
    if (w->managementVisible && (setup || w->dictionary)) EnsureLearningTools(w);
    if (w->dictionary && w->managementVisible && w->sourcesVisible) EnsureOnlineControls(w);
    for (int id : {lcQueryLabel, lcQuery, lcLookup, lcManageToggle}) Visible(w, id, setup);
    if (w->dictionary) {
        for (int id : {lcPack, lcImportPack, lcDownload, lcRemovePack, lcSourcesToggle})
            Visible(w, id, w->managementVisible);
        for (int id : {lcOnlineFirst, lcOnlineSecond, lcOnlineThird})
            Visible(w, id, w->managementVisible && w->sourcesVisible);
        Visible(w, lcPackInfo, w->managementVisible && len(Read(w, lcPackInfo)) > 0);
    } else {
        for (int id : {lcDeckLabel, lcDeck, lcActivityLabel, lcActivity, lcSchedulerLabel, lcScheduler, lcPractice,
                       lcReviewHelp, lcLearnedHint})
            Visible(w, id, setup);
        for (int id : {lcInstallDeck, lcDeleteDeck, lcNewDeck, lcCreateDeck, lcExport, lcImport})
            Visible(w, id, setup && w->managementVisible);
        Visible(w, lcUndo, setup && VocabularyCanUndoRemove());
        Visible(w, lcSessionInfo, w->practice);
        if (w->practice) Text(w, lcSessionInfo, fmt("%s (%s)", Read(w, lcActivity), Read(w, lcScheduler)));
    }
    Text(w, lcManageToggle,
         w->managementVisible ? Tr("Hide tools")
         : w->dictionary      ? Tr("Dictionaries…")
                              : Tr("Deck tools…"));
}
static bool LearningPrimaryAction(LearningWindow* w, int id) {
    if (w && w->practice) return id == lcCheck || id == lcReveal;
    return w && w->dictionary ? id == lcLookup : id == lcPractice;
}
static void PracticeControls(LearningWindow* w) {
    bool active = w->practice;
    if (active) EnsurePracticeControls(w);
    w->scrollY = 0;
    UpdateLearningChrome(w);
    int library[] = {lcLibrary, lcLearned, lcDeleteWord};
    for (int id : library) {
        Visible(w, id, !active);
    }
    int mode = Selected(w, lcActivity);
    bool cards = mode == (int)VocabActivity::Flashcards;
    bool choices = mode == (int)VocabActivity::MeaningChoice || mode == (int)VocabActivity::WordChoice;
    bool pairs = mode == (int)VocabActivity::MatchPairs;
    Visible(w, lcAnswer, active && !cards && !choices && !pairs);
    Visible(w, lcChoices, active && (choices || pairs));
    Visible(w, lcPairs, active && pairs);
    Visible(w, lcSplit, active && (choices || pairs));
    Visible(w, lcCheck, active && !cards && !pairs);
    Visible(w, lcReveal, active && cards && !w->revealed);
    int grades[] = {lcAgain, lcHard, lcGood, lcEasy};
    for (int id : grades) {
        Visible(w, id, active && cards && w->revealed);
    }
    Visible(w, lcBack, active);
    LayoutLearning(w);
}

static VocabScheduler Scheduler(LearningWindow* w) {
    return Selected(w, lcScheduler) == 0 ? VocabScheduler::Sm2 : VocabScheduler::Leitner;
}
static void NextQuestion(LearningWindow* w) {
    EnsurePracticeControls(w);
    delete w->question;
    w->question = nullptr;
    if (w->position >= len(w->session)) {
        w->practice = false;
        PracticeControls(w);
        RefreshLibrary(w);
        Status(w, fmt("Session complete: %d of %d correct", w->correct, len(w->session)));
        return;
    }
    w->question = new VocabularyQuestion();
    if (!VocabularyMakeQuestion(w->session[w->position], (VocabActivity)Selected(w, lcActivity), *w->question)) {
        w->practice = false;
        PracticeControls(w);
        Status(w, Tr("Save more defined words before starting this activity."));
        return;
    }
    w->revealed = false;
    w->checked = false;
    Text(w, lcDetails, DictionaryPlainText(w->question->prompt));
    Text(w, lcAnswer, {});
    Text(w, lcCheck, Tr("Check answer"));
    SendMessageW(Control(w, lcChoices), LB_RESETCONTENT, 0, 0);
    for (Str choice : w->question->choices) {
        SendMessageW(Control(w, lcChoices), LB_ADDSTRING, 0, (LPARAM)CWStrTemp(DictionaryPlainText(choice)));
    }
    Status(w, fmt("Review %d of %d · %d correct", w->position + 1, len(w->session), w->correct));
    PracticeControls(w);
    SetFocus(Control(w, len(w->question->choices) ? lcChoices : Selected(w, lcActivity) == 0 ? lcReveal : lcAnswer));
}
static void StartPractice(LearningWindow* w) {
    Vec<VocabularyWord*> due;
    VocabularyDue(CurrentDeck(w), due, 0, true);
    w->session.Reset();
    int newWords = 0;
    for (VocabularyWord* word : due) {
        if (word->learned || (!word->reviews && newWords >= 10)) {
            continue;
        }
        if (!word->reviews) {
            newWords++;
        }
        w->session.Append(word->id);
        if (len(w->session) >= 30) {
            break;
        }
    }
    if (!len(w->session)) {
        Status(w, Tr("No words to practice. Save a definition, install a deck, or mark a learned word unlearned."));
        return;
    }
    w->practice = true;
    EnsurePracticeControls(w);
    w->position = 0;
    w->correct = 0;
    if (Selected(w, lcActivity) != (int)VocabActivity::MatchPairs) {
        NextQuestion(w);
        return;
    }
    delete w->question;
    w->question = nullptr;
    w->pairWords.Reset();
    w->pairDefinitions.Reset();
    SendMessageW(Control(w, lcChoices), LB_RESETCONTENT, 0, 0);
    SendMessageW(Control(w, lcPairs), LB_RESETCONTENT, 0, 0);
    int count = std::min(len(w->session), 6);
    for (int i = 0; i < count; i++) {
        VocabularyWord* word = VocabularyFind(w->session[i]);
        w->pairWords.Append(word->id);
        SendMessageW(Control(w, lcChoices), LB_ADDSTRING, 0, (LPARAM)CWStrTemp(word->word));
    }
    int rotation = count > 1 ? 1 + (int)(GetTickCount64() % (count - 1)) : 0;
    for (int i = 0; i < count; i++) {
        VocabularyWord* word = VocabularyFind(w->session[(i + rotation) % count]);
        w->pairDefinitions.Append(word->id);
        SendMessageW(Control(w, lcPairs), LB_ADDSTRING, 0, (LPARAM)CWStrTemp(DictionaryPlainText(word->definition)));
    }
    Text(w, lcDetails,
         Tr("Match words to meanings. Select a word on the left and its definition on the right. Matching pairs "
            "disappear."));
    Status(w, fmt("%d pairs remaining", count));
    PracticeControls(w);
}
static void CheckPair(LearningWindow* w) {
    if (!HasPermission(Perm::SavePreferences)) {
        Status(w, Tr("Saving review progress is unavailable in restricted mode."));
        return;
    }
    int left = (int)SendMessageW(Control(w, lcChoices), LB_GETCURSEL, 0, 0);
    int right = (int)SendMessageW(Control(w, lcPairs), LB_GETCURSEL, 0, 0);
    if (left < 0 || right < 0 || left >= len(w->pairWords) || right >= len(w->pairDefinitions)) {
        return;
    }
    if (!str::Eq(w->pairWords[left], w->pairDefinitions[right])) {
        VocabularyReview(w->pairWords[left], VocabGrade::Again, Scheduler(w));
        Status(w, Tr("Not a match. Try another definition."));
        Feedback(w, false, Tr("Not a match. Choose another definition and try again."));
        SendMessageW(Control(w, lcPairs), LB_SETCURSEL, (WPARAM)-1, 0);
        return;
    }
    Feedback(w, true, Tr("Correct match. Continue with the remaining pairs."));
    VocabularyReview(w->pairWords[left], VocabGrade::Good, Scheduler(w));
    w->pairWords.RemoveAt(left);
    w->pairDefinitions.RemoveAt(right);
    SendMessageW(Control(w, lcChoices), LB_DELETESTRING, left, 0);
    SendMessageW(Control(w, lcPairs), LB_DELETESTRING, right, 0);
    SendMessageW(Control(w, lcChoices), LB_SETCURSEL, (WPARAM)-1, 0);
    SendMessageW(Control(w, lcPairs), LB_SETCURSEL, (WPARAM)-1, 0);
    if (!len(w->pairWords)) {
        w->practice = false;
        PracticeControls(w);
        RefreshLibrary(w);
        Status(w, Tr("All pairs matched. Review progress saved."));
    } else {
        Status(w, fmt("Correct · %d pairs remaining", len(w->pairWords)));
    }
}
static void CheckAnswer(LearningWindow* w) {
    if (!HasPermission(Perm::SavePreferences)) {
        Status(w, Tr("Saving review progress is unavailable in restricted mode."));
        return;
    }
    if (!w->question) {
        return;
    }
    if (w->checked) {
        w->position++;
        NextQuestion(w);
        return;
    }
    Str answer = Read(w, lcAnswer);
    if (len(w->question->choices)) {
        int i = (int)SendMessageW(Control(w, lcChoices), LB_GETCURSEL, 0, 0);
        if (i < 0 || i >= len(w->question->choices)) {
            Status(w, Tr("Choose an answer first."));
            return;
        }
        answer = w->question->choices[i];
    }
    bool correct = VocabularyCheckAnswer(*w->question, answer);
    VocabularyReview(w->question->wordId, correct ? VocabGrade::Good : VocabGrade::Again, Scheduler(w));
    w->correct += correct ? 1 : 0;
    w->checked = true;
    Feedback(w, correct,
             correct ? Tr("Correct. Review the answer, then choose Next word.")
                     : Tr("Not quite. Read the correct answer below, then choose Next word."));
    Text(w, lcDetails,
         fmt("%s\r\n\r\n%s\r\n\r\n%s", w->question->prompt, correct ? Tr("Correct") : Tr("Not quite"),
             w->question->answer));
    Text(w, lcCheck, Tr("Next word"));
    Status(w, Tr("Progress saved. Continue when ready."));
}
static TempStr ChooseLearningFile(HWND owner, bool save, bool dictionary) {
    WCHAR path[MAX_PATH]{};
    OPENFILENAMEW args{};
    args.lStructSize = sizeof(args);
    args.hwndOwner = owner;
    args.lpstrFile = path;
    args.nMaxFile = dimof(path);
    args.lpstrFilter = dictionary ? L"Offline dictionaries / WM "
                                    L"packs\0*.tsv;*.ifo;*.wmvocab.json;*.wmvocab.json.gz;*.json;*.gz\0All files\0*.*\0"
                                  : L"Vocabulary / WM packs\0*.json;*.gz\0All files\0*.*\0";
    args.lpstrDefExt = dictionary ? nullptr : L"json";
    args.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    bool picked = save ? GetSaveFileNameW(&args) : GetOpenFileNameW(&args);
    return picked ? ToUtf8Temp(path) : TempStr{};
}

enum class DictionaryJobKind {
    Lookup,
    DownloadInfo,
    Download,
    Import,
    Remove,
    WarmDeck
};
struct DictionaryJob {
    HWND hwnd;
    int serial, ticket;
    DictionaryJobKind kind;
    Str value, error;
    bool ok = false;
    i64 downloadBytes = 0;
    HANDLE cancel = nullptr;
    ULONGLONG lastProgress = 0;
    DictionarySource sources[3]{DictionarySource::Offline};
    int sourceCount = 1;
    Vec<OfflineMeaning> meanings;
    ~DictionaryJob() {
        if (cancel) CloseHandle(cancel);
        str::Free(value);
        str::Free(error);
        FreeOfflineMeanings(meanings);
    }
};
static void StartDictionaryJob(LearningWindow* w, DictionaryJobKind kind, Str value, i64 approvedBytes = 0);
struct DictionaryProgressMessage {
    HWND hwnd;
    int serial, ticket;
    Str text;
    ~DictionaryProgressMessage() { str::Free(text); }
};
static void ShowDictionaryProgress(DictionaryProgressMessage* message) {
    auto* w = IsWindow(message->hwnd) ? (LearningWindow*)GetWindowLongPtrW(message->hwnd, GWLP_USERDATA) : nullptr;
    if (w && w->serial == message->serial && w->ticket == message->ticket && w->busy) Status(w, message->text);
    delete message;
}
static void ReportDictionaryProgress(DictionaryJob* job, KaikkiProgress* progress) {
    ULONGLONG now = GetTickCount64();
    if (now - job->lastProgress < 300) return;
    job->lastProgress = now;
    auto* message = new DictionaryProgressMessage();
    message->hwnd = job->hwnd;
    message->serial = job->serial;
    message->ticket = job->ticket;
    message->text = str::Dup(progress->indexing
                                 ? fmt("Preparing offline index… %d%%. You can cancel or keep reading.",
                                       progress->total > 0 ? (int)(100.0 * progress->downloaded / progress->total) : 0)
                                 : fmt("Downloading… %s of %s. You can cancel or keep reading.",
                                       DictionarySize(progress->downloaded), DictionarySize(progress->total)));
    uitask::Post(MkFunc0(ShowDictionaryProgress, message), "Dictionary download progress");
}
static void DictionaryCancelled(DictionaryJob* job, bool* cancelled) {
    *cancelled = job->cancel && WaitForSingleObject(job->cancel, 0) == WAIT_OBJECT_0;
}
static TempStr KaikkiDownloadConfirmation(const KaikkiPack& pack, i64 bytes) {
    return fmt(
        "Download %s?\r\n\r\nDownload size: %s (%lld bytes)%s\r\n"
        "Dictionary data: approximately %s, plus a disk index.\r\n"
        "Free space needed before installation: at least %s, including temporary indexing space.\r\n\r\n"
        "Source: %s\r\nLicence: %s\r\nSave in: %s\r\n\r\n"
        "This optional pack works offline after installation. You can cancel the download; "
        "an existing installed copy stays available if the update fails or is cancelled.",
        pack.title, DictionarySize(bytes), bytes, pack.compressed ? StrL(" compressed") : Str(),
        DictionarySize(pack.expandedBytes), DictionarySize(KaikkiRequiredFreeSpace(pack.id, bytes)), pack.sourceUrl,
        pack.license, GetDictionaryDirTemp());
}
static void CompleteDictionaryJob(DictionaryJob* job) {
    auto* w = IsWindow(job->hwnd) ? (LearningWindow*)GetWindowLongPtrW(job->hwnd, GWLP_USERDATA) : nullptr;
    if (!w || w->serial != job->serial || w->ticket != job->ticket) {
        delete job;
        return;
    }
    w->busy = false;
    w->lookupBusy = false;
    w->cancelDownload = nullptr;
    Visible(w, lcCancelDownload, false);
    EnableWindow(Control(w, lcPack), true);
    int actions[] = {lcLookup, lcImportPack, lcDownload, lcRemovePack, lcInstallDeck};
    for (int id : actions) {
        EnableWindow(Control(w, id), true);
    }
    if (job->kind == DictionaryJobKind::DownloadInfo) {
        const KaikkiPack* pack = FindKaikkiPack(job->value);
        if (!job->ok || !pack) {
            Status(w, len(job->error) ? job->error : Tr("Could not confirm the dictionary download size. Try again."));
        } else {
            w->busy = true;
            int answer = MessageBoxW(w->hwnd, CWStrTemp(KaikkiDownloadConfirmation(*pack, job->downloadBytes)),
                                     L"Download offline dictionary", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2);
            // A modeless window can be closed while a modal message pumps messages.
            w = IsWindow(job->hwnd) ? (LearningWindow*)GetWindowLongPtrW(job->hwnd, GWLP_USERDATA) : nullptr;
            if (w && w->serial == job->serial) {
                w->busy = false;
                if (answer == IDYES)
                    StartDictionaryJob(w, DictionaryJobKind::Download, job->value, job->downloadBytes);
                else
                    Status(w, Tr("Download cancelled. No dictionary data was downloaded."));
            }
        }
    } else if (job->kind == DictionaryJobKind::Lookup) {
        FreeOfflineMeanings(w->meanings);
        for (OfflineMeaning& meaning : job->meanings) {
            VecAppend(w->meanings, meaning);
        }
        VecReset(job->meanings);
        SendMessageW(Control(w, lcSense), CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < len(w->meanings); i++) {
            const auto& meaning = w->meanings[i];
            AddChoice(w, lcSense, fmt("%d. %s", i + 1, meaning.definition));
        }
        SendMessageW(Control(w, lcSense), CB_SETCURSEL, 0, 0);
        if (!len(w->meanings)) {
            Text(w, lcDetails,
                 job->error ? job->error
                            : Tr("No offline definition found. Try another spelling or import a dictionary pack."));
        } else {
            ShowMeanings(w);
        }
        bool hasRecording = false, hasUk = false;
        for (const auto& meaning : w->meanings) {
            hasRecording |= len(meaning.audioUrl) > 0;
            hasUk |= len(meaning.audioUrlUk) > 0;
        }
        EnableWindow(Control(w, lcRecording), hasRecording);
        EnableWindow(Control(w, lcRecordingUk), hasUk);
        EnableWindow(Control(w, lcSense), len(w->meanings) > 0);
        EnableWindow(Control(w, lcSave), len(w->meanings) > 0);
        EnableWindow(Control(w, lcLearned), len(w->meanings) > 0);
        Status(w, len(w->meanings) ? fmt("%d meanings · choose a meaning to save", len(w->meanings))
                  : job->error     ? job->error
                                   : Tr("No definition found. Try another spelling or dictionary source."));
    } else if (job->kind == DictionaryJobKind::WarmDeck && job->ok) {
        int added = VocabularyInstallDeck(job->value);
        RefreshDecks(w);
        RefreshLibrary(w);
        int count = 0, total = 0;
        bool installed = VocabularyDeckInstalled(job->value, &count, &total);
        Status(w, added < 0 ? VocabularyLastError()
                  : installed && count < total
                      ? fmt("Installed: %d words added. %d/%d source words have definitions; %d have no definition "
                            "in the current offline dictionaries.",
                            added, count, total, total - count)
                      : fmt("%d words added to this deck", added));
    } else {
        if (w->dictionary) {
            RefreshPacks(w);
            if (!job->ok && len(job->error)) {
                Text(w, lcDetails, job->error);
            }
        }
        Status(w, job->ok      ? Tr("Dictionary packs updated. Look up a word to try the installed pack.")
                  : job->error ? job->error
                               : Tr("Could not complete this action. You can retry."));
    }
    delete job;
}
static void RunDictionaryJob(DictionaryJob* job) {
    switch (job->kind) {
        case DictionaryJobKind::Lookup:
            for (int i = 0; i < job->sourceCount; i++) {
                job->ok = LookupDictionaryWord(job->value, job->sources[i], job->meanings, &job->error);
                if (job->ok && len(job->meanings)) break;
            }
            break;
        case DictionaryJobKind::DownloadInfo:
            job->ok = GetDictionaryDownloadSize(job->value, job->downloadBytes, &job->error);
            break;
        case DictionaryJobKind::Download:
            job->ok = DownloadDictionaryPack(job->value, &job->error, MkFunc1(ReportDictionaryProgress, job),
                                             MkFunc1(DictionaryCancelled, job), job->downloadBytes);
            break;
        case DictionaryJobKind::Import:
            job->ok = InstallDictionaryFile(job->value, &job->error);
            break;
        case DictionaryJobKind::Remove:
            job->ok = RemoveDictionaryPack(job->value, &job->error);
            break;
        case DictionaryJobKind::WarmDeck:
            job->ok = true;
            break;
    }
    uitask::Post(MkFunc0(CompleteDictionaryJob, job), "Complete offline dictionary action");
}
static void StartDictionaryJob(LearningWindow* w, DictionaryJobKind kind, Str value, i64 approvedBytes) {
    if (!CanAccessDisk()) {
        Status(w, Tr("Dictionary storage is unavailable in restricted mode."));
        return;
    }
    if ((kind == DictionaryJobKind::Download || kind == DictionaryJobKind::DownloadInfo) &&
        !HasPermission(Perm::InternetAccess)) {
        Status(w, Tr("Downloads are unavailable in restricted mode. Installed dictionaries still work offline."));
        return;
    }
    if (kind != DictionaryJobKind::Lookup && !HasPermission(Perm::SavePreferences)) {
        Status(w, Tr("Saving is unavailable in restricted mode."));
        return;
    }
    if (w->busy) {
        Status(w, Tr("Please wait for the current dictionary action to finish."));
        return;
    }
    auto* job = new DictionaryJob();
    job->hwnd = w->hwnd;
    job->serial = w->serial;
    job->ticket = ++w->ticket;
    job->kind = kind;
    job->value = str::Dup(value);
    job->downloadBytes = approvedBytes;
    if (kind == DictionaryJobKind::Lookup && Selected(w, lcLookupSource) > 0) {
        if (!HasPermission(Perm::InternetAccess)) {
            delete job;
            Status(
                w,
                Tr("Online lookup is unavailable in restricted mode. Choose Offline to use installed dictionaries."));
            return;
        }
        int selected = Selected(w, lcLookupSource);
        if (selected == 1) {
            job->sourceCount = 0;
            bool enabled[4]{};
            for (int source : w->onlineOrder) {
                if (source <= 0 || source > 3 || enabled[source]) continue;
                enabled[source] = true;
                job->sources[job->sourceCount++] = (DictionarySource)source;
            }
        } else {
            job->sources[0] = (DictionarySource)(selected - 1);
        }
        if (!job->sourceCount) {
            delete job;
            Status(w, Tr("All online sources are disabled. Choose Sources to enable one, or use Offline."));
            return;
        }
    }
    if (kind == DictionaryJobKind::Download && FindKaikkiPack(value)) {
        job->cancel = CreateEventW(nullptr, true, false, nullptr);
        if (!job->cancel) {
            delete job;
            Status(w, Tr("Could not prepare the dictionary download. Try again."));
            return;
        }
        w->cancelDownload = job->cancel;
        Visible(w, lcCancelDownload, true);
        EnableWindow(Control(w, lcCancelDownload), true);
    }
    w->busy = true;
    EnableWindow(Control(w, lcPack), false);
    w->lookupBusy = kind == DictionaryJobKind::Lookup;
    int actions[] = {lcLookup, lcImportPack, lcDownload, lcRemovePack, lcInstallDeck};
    for (int id : actions) {
        EnableWindow(Control(w, id), false);
    }
    if (kind == DictionaryJobKind::Lookup) {
        EnableWindow(Control(w, lcSave), false);
        EnableWindow(Control(w, lcLearned), false);
    }
    Status(w, kind == DictionaryJobKind::DownloadInfo
                  ? Tr("Checking the current download size… No dictionary data is downloaded yet.")
              : kind == DictionaryJobKind::Download
                  ? Tr("Downloading and verifying dictionary pack… You can keep reading.")
              : kind == DictionaryJobKind::Lookup
                  ? job->sources[0] == DictionarySource::Offline
                        ? Tr("Looking up this word in installed offline dictionaries…")
                        : Tr("Looking up this word online… Only this word is sent to the selected provider.")
                  : Tr("Updating offline resources…"));
    RunAsync(MkFunc0(RunDictionaryJob, job), StrL("OfflineDictionaryAction"));
}
static void LookupWord(LearningWindow* w) {
    Str query = Read(w, lcQuery);
    if (!len(query)) {
        Status(w, Tr("Type a word or select one in the document first."));
        return;
    }
    StartDictionaryJob(w, DictionaryJobKind::Lookup, query);
}
static void SaveMeaning(LearningWindow* w, bool learned) {
    if (!len(w->meanings)) {
        return;
    }
    int selected = std::clamp(Selected(w, lcSense), 0, len(w->meanings) - 1);
    const OfflineMeaning& meaning = w->meanings[selected];
    Vec<OfflineMeaning> saved;
    VecAppend(saved, meaning);
    Str definition = DictionaryMeaningText(saved);
    VocabularyWord* word = VocabularyAdd(meaning.headword, definition, meaning.dictionaryId, w->context, w->source,
                                         w->page, CurrentDeck(w));
    if (word && learned) {
        VocabularySetLearned(word->id, true);
    }
    Status(w, word ? learned ? Tr("Saved and marked learned.")
                             : Tr("Saved to vocabulary. Review it from the learning hub.")
                   : VocabularyLastError());
    InvalidateRect(w->owner->hwndCanvas, nullptr, false);
}
static void LearningAction(LearningWindow* w, int id, int notification) {
    if (!w->ready || w->updating) {
        return;
    }
    if (id == lcManageToggle && notification == BN_CLICKED) {
        w->managementVisible = !w->managementVisible;
        UpdateLearningChrome(w);
        LayoutLearning(w, true);
        return;
    }
    if (id == lcReviewHelp && notification == BN_CLICKED) {
        MessageBoxW(w->hwnd,
                    CWStrTemp(Tr("SM-2 adjusts each word's next review using your grades and review history. "
                                 "Leitner moves words through boxes with progressively longer review intervals. "
                                 "Both keep difficult words due sooner. Mark learned removes a word from practice; "
                                 "Mark unlearned returns it to review.")),
                    CWStrTemp(Tr("Review methods")), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (id == lcSourcesToggle && notification == BN_CLICKED) {
        w->sourcesVisible = !w->sourcesVisible;
        if (w->sourcesVisible) EnsureOnlineControls(w);
        for (int setting : {lcOnlineFirst, lcOnlineSecond, lcOnlineThird}) Visible(w, setting, w->sourcesVisible);
        Text(w, lcSourcesToggle, w->sourcesVisible ? Tr("Hide sources") : Tr("Sources…"));
        LayoutLearning(w);
        return;
    }
    if ((id == lcOnlineFirst || id == lcOnlineSecond || id == lcOnlineThird) && notification == CBN_SELCHANGE) {
        int slot = id - lcOnlineFirst;
        w->onlineOrder[slot] = std::clamp(Selected(w, id), 0, 3);
        SaveLearningPrefs(w);
        Status(w, Tr("Online sources run in this order. Disabled sources are skipped; Offline never contacts them."));
        return;
    }
    if (id == lcLookupSource && notification == CBN_SELCHANGE) {
        Status(w, Selected(w, lcLookupSource) <= 0
                      ? Tr("Offline lookup uses installed dictionaries. No network request is made.")
                      : Tr("Online lookup sends only the typed word when you choose Look up. PDF context stays on this "
                           "device."));
        return;
    }
    if ((id == lcPronounce || id == lcRecording || id == lcRecordingUk || id == lcStopVoice) &&
        notification == BN_CLICKED) {
        if (!w->speech) w->speech = new DictionarySpeech(w->hwnd, kSpeechMessage);
        if (id == lcStopVoice) {
            w->speech->Stop();
            return;
        }
        Str word = PronunciationWord(w);
        if (!len(word)) {
            Status(w, Tr("Type or select a word to hear its pronunciation."));
            return;
        }
        int voice = std::max(Selected(w, lcVoice), 0);
        if (id == lcPronounce) {
            w->speech->Speak(word, voice);
        } else {
            if (!HasPermission(Perm::InternetAccess)) {
                Status(w, Tr("Recorded pronunciation needs internet access. Pronounce uses installed voices offline."));
                return;
            }
            Str audio;
            for (const auto& meaning : w->meanings) {
                audio = id == lcRecordingUk ? meaning.audioUrlUk : meaning.audioUrl;
                if (len(audio)) break;
            }
            if (!len(audio)) {
                Status(w, Tr("No recording is supplied for this word. Use Pronounce for an offline voice."));
                return;
            }
            w->speech->PlayRecording(audio, word, voice);
        }
        return;
    }
    if (id == lcGuideStart || id == lcGuidePrev || id == lcGuideNext || id == lcGuideSkip || id == lcGuideAction) {
        if (notification != BN_CLICKED) return;
        if (id == lcGuideAction) {
            const GuideStep& step = kGuide[w->guideStep];
            LearningWindow* target = w;
            if (step.dictionary != w->dictionary) {
                if (step.dictionary)
                    ShowDictionaryDialog(w->owner);
                else
                    ShowVocabularyDialog(w->owner);
                for (LearningWindow* other : gLearningWindows) {
                    if (other->owner == w->owner && other->dictionary == step.dictionary) target = other;
                }
                target->guideStep = w->guideStep;
                target->guideVisible = true;
                UpdateGuide(target);
            }
            if (!target->practice &&
                (step.control == lcPack || step.control == lcInstallDeck || step.control == lcExport)) {
                target->managementVisible = true;
                UpdateLearningChrome(target);
                LayoutLearning(target, true);
            }
            HWND child = Control(target, step.control);
            if (child && IsWindowVisible(child) && IsWindowEnabled(child)) {
                SetFocus(child);
                RevealFocusedControl(target, child);
            } else {
                Status(target, Tr("Complete the lookup or leave practice before using this step's action."));
            }
            return;
        }
        if (id == lcGuideStart) w->guideVisible = !w->guideVisible;
        if (id == lcGuidePrev) w->guideStep = std::max(0, w->guideStep - 1);
        if (id == lcGuideNext) {
            if (w->guideStep == kGuideSteps - 1) {
                w->guideVisible = false;
                w->guideStep = 0;
            } else
                w->guideStep++;
        }
        if (id == lcGuideSkip) w->guideVisible = false;
        UpdateGuide(w);
        return;
    }
    if (id == IDCANCEL) {
        DestroyWindow(w->hwnd);
        return;
    }
    if (id == IDOK) {
        if (w->dictionary) {
            LookupWord(w);
        } else if (w->practice) {
            if (Selected(w, lcActivity) == (int)VocabActivity::MatchPairs) {
                CheckPair(w);
            } else if (Selected(w, lcActivity) == (int)VocabActivity::Flashcards) {
                if (!w->revealed) {
                    LearningAction(w, lcReveal, BN_CLICKED);
                }
            } else {
                CheckAnswer(w);
            }
        } else {
            RefreshLibrary(w);
        }
        return;
    }
    if (id == lcPack && notification == CBN_SELCHANGE) {
        PackDownloadInfo(w);
        int i = Selected(w, lcPack);
        if (i >= 0 && i < len(w->packs)) {
            const auto& p = w->packs[i];
            Text(w, lcDetails,
                 fmt("%s\r\nLanguage: %s\r\nLicense: %s\r\nSource: %s\r\n\r\n%s", p.title, p.language, p.license,
                     p.sourceUrl,
                     p.installed ? Tr("Available offline. Lookup searches all installed dictionaries and shows the "
                                      "source for each result. Download refreshes the original pack.")
                                 : Tr("Download this pack, or import a dictionary file. Lookup remains offline.")));
            EnableWindow(Control(w, lcSave), false);
            EnableWindow(Control(w, lcLearned), false);
        }
        return;
    }
    if (id == lcQuery && notification == EN_CHANGE && !w->dictionary) {
        if (!w->practice) {
            RefreshLibrary(w, LibraryRefresh::Query);
        }
        return;
    }
    if (id == lcQuery && notification == EN_CHANGE && w->dictionary) {
        for (int action : {lcSave, lcLearned, lcRecording, lcRecordingUk}) EnableWindow(Control(w, action), false);
        return;
    }
    if (id == lcDeck && notification == CBN_SELCHANGE && !w->dictionary) {
        w->practice = false;
        PracticeControls(w);
        RefreshLibrary(w);
        int count = 0, total = 0;
        bool installed = VocabularyDeckInstalled(CurrentDeck(w), &count, &total);
        if (total > count) {
            Status(w, installed ? fmt("Installed: %d/%d source words have definitions. %d have no definition in the "
                                      "current offline dictionaries.",
                                      count, total, total - count)
                                : fmt("%d/%d source words have definitions. Choose Install deck to check and complete "
                                      "the available words; existing notes and reviews are kept.",
                                      count, total));
        }
        return;
    }
    if (id == lcActivity && notification == CBN_SELCHANGE && w->practice) {
        StartPractice(w);
        return;
    }
    if (id == lcLibrary && notification == LBN_SELCHANGE) {
        WordDetails(w);
        return;
    }
    if ((id == lcChoices || id == lcPairs) && notification == LBN_SELCHANGE && w->practice &&
        Selected(w, lcActivity) == (int)VocabActivity::MatchPairs) {
        CheckPair(w);
        return;
    }
    if (notification != BN_CLICKED) {
        return;
    }
    if (id != lcLookup && id != lcBack && id != lcReveal && id != lcOpenVocabulary &&
        !HasPermission(Perm::SavePreferences)) {
        Status(w, Tr("Saving vocabulary and progress is unavailable in restricted mode."));
        return;
    }
    switch (id) {
        case lcLookup:
            if (w->dictionary) {
                LookupWord(w);
            } else {
                RefreshLibrary(w);
            }
            break;
        case lcSave:
            SaveMeaning(w, false);
            break;
        case lcLearned: {
            if (w->dictionary) {
                SaveMeaning(w, true);
                break;
            }
            VocabularyWord* word = SelectedWord(w);
            if (word) {
                VocabularySetLearned(word->id, !word->learned);
                RefreshLibrary(w);
            }
            break;
        }
        case lcDeleteWord: {
            VocabularyWord* word = SelectedWord(w);
            if (word) {
                bool removed = VocabularyRemove(word->id);
                if (removed) RefreshLibrary(w);
                UpdateLearningChrome(w);
                Status(w, removed ? Tr("Word removed. Undo restores its definition, notes, decks and review history.")
                                  : VocabularyLastError());
                LayoutLearning(w, true);
            }
            break;
        }
        case lcUndo: {
            bool restored = VocabularyUndoRemove();
            for (LearningWindow* other : gLearningWindows) {
                if (!other->dictionary && !other->practice) {
                    RefreshDecks(other);
                    RefreshLibrary(other);
                    UpdateLearningChrome(other);
                    LayoutLearning(other, true);
                }
            }
            Status(w, restored ? Tr("Word restored with its notes and review history.") : VocabularyLastError());
            break;
        }
        case lcOpenVocabulary:
            ShowVocabularyDialog(w->owner);
            break;
        case lcPractice:
            StartPractice(w);
            break;
        case lcBack:
            w->practice = false;
            PracticeControls(w);
            RefreshLibrary(w);
            break;
        case lcReveal:
            if (w->question) {
                w->revealed = true;
                Text(w, lcDetails, fmt("%s\r\n\r\n%s", w->question->prompt, w->question->answer));
                PracticeControls(w);
            }
            break;
        case lcAgain:
        case lcHard:
        case lcGood:
        case lcEasy:
            if (w->question) {
                VocabularyReview(w->question->wordId, (VocabGrade)(id - lcAgain), Scheduler(w));
                w->correct += id == lcGood || id == lcEasy ? 1 : 0;
                w->position++;
                NextQuestion(w);
                Feedback(w, id == lcGood || id == lcEasy,
                         id == lcGood || id == lcEasy
                             ? Tr("Remembered. Your grade was saved; continue with the next card.")
                             : Tr("Needs more review. Your grade was saved; continue with the next card."));
            }
            break;
        case lcCheck:
            CheckAnswer(w);
            break;
        case lcCreateDeck: {
            VocabularyDeck* deck = VocabularyCreateDeck(Read(w, lcNewDeck));
            if (deck) {
                RefreshDecks(w);
                SendMessageW(Control(w, lcDeck), CB_SETCURSEL, w->deckIds.Find(deck->id), 0);
                Text(w, lcNewDeck, {});
                RefreshLibrary(w);
            } else {
                Status(w, VocabularyLastError());
            }
            break;
        }
        case lcDeleteDeck: {
            Str deck = str::Dup(CurrentDeck(w));
            defer {
                str::Free(deck);
            };
            if (!len(deck)) break;
            HWND receiver = w->hwnd;
            int serial = w->serial;
            int answer =
                MessageBoxW(receiver,
                            CWStrTemp(Tr("Delete this deck? Saved words, PDF notes and review history remain in All "
                                         "vocabulary. Only this deck and its word memberships are removed.")),
                            CWStrTemp(Tr("Delete deck")), MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION);
            // The reader owning this modeless window can close while the prompt pumps messages.
            w = IsWindow(receiver) ? (LearningWindow*)GetWindowLongPtrW(receiver, GWLP_USERDATA) : nullptr;
            if (!w || w->serial != serial) return;
            if (answer != IDYES) break;
            if (VocabularyRemoveDeck(deck)) {
                RefreshDecks(w);
                RefreshLibrary(w);
            } else {
                Status(w, VocabularyLastError());
            }
            break;
        }
        case lcInstallDeck:
            if (!len(CurrentDeck(w))) {
                Status(w, Tr("Select a built-in deck first."));
            } else {
                StartDictionaryJob(w, DictionaryJobKind::WarmDeck, CurrentDeck(w));
            }
            break;
        case lcImportPack: {
            if (!CanAccessDisk()) {
                Status(w, Tr("Import is unavailable in restricted mode."));
                break;
            }
            TempStr path = ChooseLearningFile(w->hwnd, false, true);
            if (len(path)) {
                StartDictionaryJob(w, DictionaryJobKind::Import, path);
            }
            break;
        }
        case lcCancelDownload:
            if (w->cancelDownload) {
                SetEvent(w->cancelDownload);
                EnableWindow(Control(w, lcCancelDownload), false);
                Status(w, Tr("Cancelling… Your existing offline dictionary stays available."));
            }
            break;
        case lcDownload:
        case lcRemovePack: {
            int i = Selected(w, lcPack);
            if (i >= 0 && i < len(w->packs)) {
                DictionaryJobKind kind = id == lcRemovePack               ? DictionaryJobKind::Remove
                                         : FindKaikkiPack(w->packs[i].id) ? DictionaryJobKind::DownloadInfo
                                                                          : DictionaryJobKind::Download;
                StartDictionaryJob(w, kind, w->packs[i].id);
            }
            break;
        }
        case lcExport:
        case lcImport: {
            if (!CanAccessDisk()) {
                Status(w, Tr("Import and export are unavailable in restricted mode."));
                break;
            }
            TempStr path = ChooseLearningFile(w->hwnd, id == lcExport, false);
            if (!len(path)) {
                break;
            }
            bool ok = id == lcExport ? VocabularyExport(path) : VocabularyImport(path);
            w->practice = false;
            RefreshDecks(w);
            PracticeControls(w);
            RefreshLibrary(w);
            Status(w, ok ? Tr("Vocabulary backup completed.") : VocabularyLastError());
            break;
        }
    }
    InvalidateRect(w->owner->hwndCanvas, nullptr, false);
}
static void LibraryContextMenu(LearningWindow* w, LPARAM point) {
    if (w->practice) {
        return;
    }
    POINT screen{GET_X_LPARAM(point), GET_Y_LPARAM(point)};
    if (screen.x == -1 && screen.y == -1) {
        RECT rc;
        GetWindowRect(Control(w, lcLibrary), &rc);
        screen = {rc.left + DpiScale(20), rc.top + DpiScale(20)};
    } else {
        POINT local = screen;
        ScreenToClient(Control(w, lcLibrary), &local);
        DWORD hit = (DWORD)SendMessageW(Control(w, lcLibrary), LB_ITEMFROMPOINT, 0, MAKELPARAM(local.x, local.y));
        if (!HIWORD(hit)) {
            SendMessageW(Control(w, lcLibrary), LB_SETCURSEL, LOWORD(hit), 0);
            WordDetails(w);
        }
    }
    VocabularyWord* word = SelectedWord(w);
    if (!word) {
        return;
    }
    constexpr int lookup = 10001;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, lookup, CWStrTemp(Tr("Look up offline")));
    AppendMenuW(menu, MF_STRING, lcLearned, CWStrTemp(word->learned ? Tr("Mark unlearned") : Tr("Mark learned")));
    AppendMenuW(menu, MF_STRING, lcDeleteWord, CWStrTemp(Tr("Remove word")));
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, w->hwnd, nullptr);
    DestroyMenu(menu);
    if (command == lookup) {
        ShowDictionaryDialog(w->owner, word->word, word->context, word->sourcePath, word->page);
    } else if (command) {
        LearningAction(w, command, BN_CLICKED);
    }
}

struct LibraryHeight {
    Str id, caption;
    int height;
};
struct ChoiceList {
    HWND hwnd = nullptr;
    StrVec strings;
    Vec<int> heights;
    int selected = -1, scroll = 0, hover = -1, wheelRemainder = 0;
    HFONT font = nullptr;
    Vec<int> tops;
    int measuredWidth = -1, measurePasses = 0, labelWidth = 0;
    bool wrapDirty = true, topsDirty = true, wrapping = false, partialWrap = false;
    Vec<int> measureRows;
    Vec<LibraryHeight> libraryHeights;
    void ClearHeights() {
        for (auto& row : libraryHeights) {
            str::Free(row.id);
            str::Free(row.caption);
        }
        VecReset(libraryHeights);
    }
    ~ChoiceList() { ClearHeights(); }
};
static void ClearLibraryHeights(HWND control) {
    auto* list = (ChoiceList*)GetWindowLongPtrW(control, GWLP_USERDATA);
    if (list) list->ClearHeights();
}
static void IndexChoiceRows(ChoiceList* list) {
    if (!list->topsDirty) return;
    VecReset(list->tops);
    int y = 0;
    for (int height : list->heights) {
        VecAppend(list->tops, y);
        y += height;
    }
    VecAppend(list->tops, y);
    list->topsDirty = false;
}
static int ChoiceTop(ChoiceList* list, int index) {
    IndexChoiceRows(list);
    return list->tops[std::clamp(index, 0, len(list->heights))];
}
static int ChoiceRowAt(ChoiceList* list, int y) {
    if (y < 0 || y >= ChoiceTop(list, len(list->heights))) return -1;
    int first = 0, last = len(list->heights);
    while (first + 1 < last) {
        int middle = (first + last) / 2;
        if (ChoiceTop(list, middle) <= y)
            first = middle;
        else
            last = middle;
    }
    return first;
}
static void InvalidateChoiceRow(ChoiceList* list, int index) {
    if (index < 0 || index >= len(list->heights)) return;
    RECT client;
    GetClientRect(list->hwnd, &client);
    int top = ChoiceTop(list, index) - list->scroll;
    RECT row{0, top, client.right, top + list->heights[index]};
    if (IntersectRect(&row, &row, &client)) InvalidateRect(list->hwnd, &row, false);
}
static void ScrollChoices(ChoiceList* list) {
    RECT rc;
    GetClientRect(list->hwnd, &rc);
    int total = ChoiceTop(list, len(list->heights));
    list->scroll = std::clamp(list->scroll, 0, std::max(0, total - (int)rc.bottom));
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    si.nMax = std::max(0, total - 1);
    si.nPage = rc.bottom;
    si.nPos = list->scroll;
    SetScrollInfo(list->hwnd, SB_VERT, &si, true);
    InvalidateRect(list->hwnd, nullptr, false);
}
static bool ScrollChoiceTo(ChoiceList* list, int64_t requested) {
    RECT client;
    GetClientRect(list->hwnd, &client);
    int total = ChoiceTop(list, len(list->heights));
    int offset = (int)std::clamp<int64_t>(requested, 0, std::max(0, total - (int)client.bottom));
    if (offset == list->scroll) return false;
    list->scroll = offset;
    SCROLLINFO si{sizeof(si), SIF_POS};
    si.nPos = offset;
    SetScrollInfo(list->hwnd, SB_VERT, &si, true);
    InvalidateRect(list->hwnd, nullptr, false);
    return true;
}
static void WrapChoices(HWND control) {
    if (!control) return;
    auto* list = (ChoiceList*)GetWindowLongPtrW(control, GWLP_USERDATA);
    if (!list || list->wrapping) return;
    RECT client;
    GetClientRect(control, &client);
    // Creation bounds have no usable text width; the final resize will measure rows.
    if (client.right <= AppScrollbarInset(control) + UiScalePx(24)) return;
    if (!list->wrapDirty && list->measuredWidth == client.right) return;
    if (list->measuredWidth != client.right) list->ClearHeights();
    int anchor = ChoiceRowAt(list, list->scroll);
    int within = anchor >= 0 ? list->scroll - ChoiceTop(list, anchor) : 0;
    list->wrapping = true;
    defer {
        list->wrapping = false;
    };
    int label = 0;
    if (GetDlgCtrlID(control) != lcLibrary) {
        for (int i = 0; i < len(list->strings); i++) label = std::max(label, LabelWidth(control, i));
    }
    list->labelWidth = label;
    HDC dc = PlatformFontMeasurementDC();
    HGDIOBJ old = SelectObject(dc, list->font ? list->font : GetAppFontForDpi(DpiGet())->GetHFont());
    for (int pass = 0; pass < 2; pass++) {
        GetClientRect(control, &client);
        bool all = !list->partialWrap || list->measuredWidth != client.right || pass > 0;
        int next = 0, measured = 0, i = 0;
        for (Str caption : list->strings) {
            int row = i++;
            if (!all && (next >= len(list->measureRows) || list->measureRows[next] != row)) continue;
            if (!all) next++;
            WStr text = ToWStrTemp(caption);
            RECT bounds{0, 0, std::max(1, (int)client.right - AppScrollbarInset(control) - label - UiScalePx(24)), 0};
            DrawTextW(dc, CWStrTemp(text), len(text), &bounds,
                      DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
            list->heights[row] = std::max((int)bounds.bottom, GetAppFontSizeForDpi(DpiGet())) + UiScalePx(20);
            measured++;
        }
        if (measured) list->measurePasses++;
        list->topsDirty = true;
        if (anchor >= 0 && anchor < len(list->heights)) {
            list->scroll = ChoiceTop(list, anchor) + std::min(within, list->heights[anchor] - 1);
        }
        ScrollChoices(list);
        RECT after;
        GetClientRect(control, &after);
        if (client.right != after.right) list->ClearHeights();
        if (client.right == after.right) break;
    }
    SelectObject(dc, old);
    list->measuredWidth = client.right;
    list->wrapDirty = list->partialWrap = false;
    VecReset(list->measureRows);
}
static void ChooseRow(ChoiceList* list, int index, bool notify) {
    if (index < -1 || index >= len(list->strings)) {
        return;
    }
    list->selected = index;
    if (index >= 0) {
        RECT rc;
        GetClientRect(list->hwnd, &rc);
        int top = ChoiceTop(list, index), bottom = top + list->heights[index];
        if (top < list->scroll || bottom - top > rc.bottom) {
            list->scroll = top;
        } else if (bottom > list->scroll + rc.bottom) {
            list->scroll = bottom - rc.bottom;
        }
    }
    ScrollChoices(list);
    if (notify) {
        SendMessageW(GetParent(list->hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(list->hwnd), LBN_SELCHANGE),
                     (LPARAM)list->hwnd);
    }
}
static bool SetLibraryRows(LearningWindow* w, const Vec<VocabularyWord*>& words, Str previous) {
    HWND control = Control(w, lcLibrary);
    auto* list = (ChoiceList*)GetWindowLongPtrW(control, GWLP_USERDATA);
    if (!list) return false;
    bool changed = len(words) != len(w->wordIds) || len(words) != len(list->strings);
    auto id = w->wordIds.begin();
    int index = 0;
    for (Str caption : list->strings) {
        if (changed) break;
        VocabularyWord* word = words[index++];
        changed = !str::Eq(word->id, *id++) ||
                  !str::Eq(caption, fmt("%s%s", word->word, word->learned ? StrL("  ✓") : Str{}));
    }
    if (!changed) return false;

    Vec<LibraryHeight> rows;
    id = w->wordIds.begin();
    index = 0;
    for (Str caption : list->strings) {
        if (index >= len(w->wordIds) || index >= len(list->heights)) break;
        VecAppend(rows, LibraryHeight{*id++, caption, list->heights[index++]});
    }
    VecSort(rows, [](const LibraryHeight* a, const LibraryHeight* b) { return str::Cmp(a->id, b->id); });
    RECT client;
    GetClientRect(control, &client);
    bool cached = !list->wrapDirty && list->measuredWidth == client.right;
    if (!cached) list->ClearHeights();
    int oldCount = len(list->libraryHeights);
    for (const auto& row : rows) {
        if (!cached) break;
        int first = 0, last = oldCount;
        while (first < last) {
            int middle = (first + last) / 2;
            if (str::Cmp(list->libraryHeights[middle].id, row.id) < 0)
                first = middle + 1;
            else
                last = middle;
        }
        if (first < oldCount && str::Eq(list->libraryHeights[first].id, row.id)) {
            auto& saved = list->libraryHeights[first];
            if (!str::Eq(saved.caption, row.caption)) str::ReplaceWithCopy(&saved.caption, row.caption);
            saved.height = row.height;
        } else {
            VecAppend(list->libraryHeights, LibraryHeight{str::Dup(row.id), str::Dup(row.caption), row.height});
        }
    }
    VecSort(list->libraryHeights,
            [](const LibraryHeight* a, const LibraryHeight* b) { return str::Cmp(a->id, b->id); });
    StrVec ids, captions;
    Vec<int> heights, measureRows;
    for (VocabularyWord* word : words) {
        Str caption = fmt("%s%s", word->word, word->learned ? StrL("  ✓") : Str{});
        int first = 0, last = len(list->libraryHeights);
        while (first < last) {
            int middle = (first + last) / 2;
            if (str::Cmp(list->libraryHeights[middle].id, word->id) < 0)
                first = middle + 1;
            else
                last = middle;
        }
        bool reuse = cached && first < len(list->libraryHeights) && str::Eq(list->libraryHeights[first].id, word->id) &&
                     str::Eq(list->libraryHeights[first].caption, caption);
        if (!reuse) VecAppend(measureRows, len(heights));
        VecAppend(heights, reuse ? list->libraryHeights[first].height : GetAppFontSizeForDpi(DpiGet()) + UiScalePx(20));
        ids.Append(word->id);
        captions.Append(caption);
    }
    w->wordIds = ids;
    list->strings = captions;
    list->heights = heights;
    list->measureRows = measureRows;
    list->wrapDirty = list->topsDirty = true;
    list->partialWrap = cached;
    list->selected = list->hover = -1;
    list->scroll = 0;
    WrapChoices(control);
    if (len(words)) ChooseRow(list, std::max(0, w->wordIds.Find(previous)), false);
    return true;
}
static LRESULT CALLBACK ChoiceWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DpiScope dpi(hwnd);
    auto* list = (ChoiceList*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        list = new ChoiceList();
        list->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)list);
    }
    if (!list) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    switch (msg) {
        case LB_ADDSTRING:
            list->partialWrap = false;
            list->strings.Append(ToUtf8Temp(WStr((WCHAR*)lp)));
            list->wrapDirty = list->topsDirty = true;
            VecAppend(list->heights, GetAppFontSizeForDpi(DpiGet()) + DpiScale(20));
            return len(list->strings) - 1;
        case LB_RESETCONTENT:
            list->partialWrap = false;
            list->strings.Reset();
            VecReset(list->heights);
            list->wrapDirty = list->topsDirty = true;
            list->selected = -1;
            list->scroll = 0;
            ScrollChoices(list);
            return 0;
        case LB_DELETESTRING:
            if ((int)wp < 0 || (int)wp >= len(list->strings)) {
                return LB_ERR;
            }
            list->partialWrap = false;
            list->strings.RemoveAt((int)wp);
            VecRemoveAt(list->heights, (int)wp);
            list->wrapDirty = list->topsDirty = true;
            list->selected = -1;
            ScrollChoices(list);
            return len(list->strings);
        case LB_ITEMFROMPOINT: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (x < 0 || x >= rc.right || y < 0 || y >= rc.bottom) {
                return MAKELONG(0, 1);
            }
            int row = ChoiceRowAt(list, y + list->scroll);
            return row >= 0 ? MAKELONG(row, 0) : MAKELONG(0, 1);
        }
        case LB_GETCOUNT:
            return len(list->strings);
        case LB_GETCURSEL:
            return list->selected;
        case LB_SETCURSEL:
            ChooseRow(list, (int)wp, false);
            return list->selected;
        case LB_GETTEXTLEN:
        case LB_GETTEXT: {
            if ((int)wp < 0 || (int)wp >= len(list->strings)) {
                return LB_ERR;
            }
            WStr text = ToWStrTemp(list->strings[(int)wp]);
            if (msg == LB_GETTEXT) {
                memcpy((void*)lp, CWStrTemp(text), (len(text) + 1) * sizeof(WCHAR));
            }
            return len(text);
        }
        case LB_SETITEMHEIGHT:
            if ((int)wp < 0 || (int)wp >= len(list->heights)) {
                return LB_ERR;
            }
            list->heights[(int)wp] = std::max(1, (int)lp);
            list->topsDirty = true;
            ScrollChoices(list);
            return 0;
        case WM_GETFONT:
            return (LRESULT)list->font;
        case WM_SETFONT:
            list->ClearHeights();
            list->partialWrap = false;
            list->font = (HFONT)wp;
            list->wrapDirty = true;
            WrapChoices(hwnd);
            return 0;
        case WM_SIZE:
            WrapChoices(hwnd);
            ScrollChoices(list);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(hwnd, nullptr, false);
            InvalidateRect(GetParent(hwnd), nullptr, false);
            if (msg == WM_SETFOCUS) {
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), LBN_SETFOCUS), (LPARAM)hwnd);
            }
            return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            int hover = ChoiceRowAt(list, GET_Y_LPARAM(lp) + list->scroll);
            if (hover != list->hover) {
                InvalidateChoiceRow(list, list->hover);
                list->hover = hover;
                InvalidateChoiceRow(list, hover);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            InvalidateChoiceRow(list, list->hover);
            list->hover = -1;
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            int row = ChoiceRowAt(list, GET_Y_LPARAM(lp) + list->scroll);
            if (row >= 0) ChooseRow(list, row, true);
            return 0;
        }
        case WM_KEYDOWN: {
            int index = list->selected;
            RECT rc;
            GetClientRect(hwnd, &rc);
            switch (wp) {
                case VK_UP:
                    index = std::max(0, index - 1);
                    break;
                case VK_DOWN:
                    index = std::min(len(list->strings) - 1, index + 1);
                    break;
                case VK_HOME:
                    index = 0;
                    break;
                case VK_END:
                    index = len(list->strings) - 1;
                    break;
                case VK_PRIOR:
                case VK_NEXT:
                    ScrollChoiceTo(list, (int64_t)list->scroll + (wp == VK_PRIOR ? -rc.bottom : rc.bottom));
                    return 0;
                default:
                    return DefWindowProcW(hwnd, msg, wp, lp);
            }
            ChooseRow(list, index, true);
            return 0;
        }
        case WM_CHAR:
            if (GetDlgCtrlID(hwnd) == lcLibrary) {
                char prefix[2]{(char)wp, 0};
                for (int step = 1; step <= len(list->strings); step++) {
                    int index = (std::max(list->selected, 0) + step) % len(list->strings);
                    if (str::StartsWithI(list->strings[index], Str(prefix))) {
                        ChooseRow(list, index, true);
                        break;
                    }
                }
                return 0;
            }
            if (wp >= 'a' && wp <= 'z') {
                ChooseRow(list, (int)wp - 'a', true);
            } else if (wp >= 'A' && wp <= 'Z') {
                ChooseRow(list, (int)wp - 'A', true);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            RECT client;
            GetClientRect(hwnd, &client);
            int movement = WheelDistance(wp, WheelStep(client.bottom), list->wheelRemainder);
            if (movement && !ScrollChoiceTo(list, (int64_t)list->scroll - movement)) {
                SendMessageW(GetParent(hwnd), msg, wp, lp);
            }
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof(si), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &si);
            int64_t offset = list->scroll;
            switch (LOWORD(wp)) {
                case SB_LINEUP:
                    offset -= UiScalePx(32);
                    break;
                case SB_LINEDOWN:
                    offset += UiScalePx(32);
                    break;
                case SB_PAGEUP:
                    offset -= si.nPage;
                    break;
                case SB_PAGEDOWN:
                    offset += si.nPage;
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    offset = AppScrollbarTrackPos(hwnd, si.nTrackPos);
                    break;
                case SB_TOP:
                    offset = 0;
                    break;
                case SB_BOTTOM:
                    offset = si.nMax;
                    break;
                default:
                    return 0;
            }
            ScrollChoiceTo(list, offset);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PRINTCLIENT:
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            bool printing = msg == WM_PRINTCLIENT;
            HDC target = printing ? (HDC)wp : BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (printing) {
                RECT clip = rc;
                if (GetClipBox(target, &clip) == ERROR) clip = rc;
                IntersectRect(&ps.rcPaint, &rc, &clip);
            }
            if (IsRectEmpty(&ps.rcPaint)) {
                if (!printing) EndPaint(hwnd, &ps);
                return 0;
            }
            DoubleBuffer buffer(hwnd, ToRect(ps.rcPaint));
            HDC dc = buffer.GetDC();
            HBRUSH bg = CreateSolidBrush(ThemeControlBackgroundColor());
            FillRect(dc, &rc, bg);
            DeleteObject(bg);
            int first = ChoiceRowAt(list, std::max(0, list->scroll + (int)ps.rcPaint.top));
            if (first < 0) first = len(list->strings);
            int y = ChoiceTop(list, first) - list->scroll;
            for (int i = first; i < len(list->strings) && y < ps.rcPaint.bottom; i++) {
                DRAWITEMSTRUCT item{};
                item.CtlType = ODT_LISTBOX;
                item.CtlID = GetDlgCtrlID(hwnd);
                item.itemID = i;
                item.hwndItem = hwnd;
                item.hDC = dc;
                item.rcItem = {0, y, rc.right - AppScrollbarInset(hwnd), y + list->heights[i]};
                item.itemState = i == list->selected ? ODS_SELECTED : 0;
                if (i == list->hover) item.itemState |= ODS_HOTLIGHT;
                if (i == list->selected && GetFocus() == hwnd) {
                    item.itemState |= ODS_FOCUS;
                }
                if (item.rcItem.bottom > ps.rcPaint.top && item.rcItem.top < ps.rcPaint.bottom) {
                    SendMessageW(GetParent(hwnd), WM_DRAWITEM, item.CtlID, (LPARAM)&item);
                }
                y += list->heights[i];
            }
            buffer.Flush(target);
            if (!printing) EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            KillTimer(hwnd, kFeedbackTimer);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete list;
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void DrawLearningChoice(DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    bool selected = (item->itemState & ODS_SELECTED) != 0;
    HBRUSH brush = CreateSolidBrush(ThemeControlBackgroundColor());
    FillRect(item->hDC, &rc, brush);
    DeleteObject(brush);
    if (selected || (item->itemState & ODS_HOTLIGHT)) {
        RECT panel = rc;
        InflateRect(&panel, -UiScalePx(4), -UiScalePx(3));
        brush = CreateSolidBrush(ThemeHotBackgroundColor());
        HGDIOBJ oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
        RoundRect(item->hDC, panel.left, panel.top, panel.right, panel.bottom, UiScalePx(10), UiScalePx(10));
        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);
        DeleteObject(brush);
        if (selected) {
            RECT stripe = panel;
            stripe.right = stripe.left + UiScalePx(3);
            InflateRect(&stripe, 0, -UiScalePx(5));
            brush = CreateSolidBrush(ThemeBrandColor());
            FillRect(item->hDC, &stripe, brush);
            DeleteObject(brush);
        }
    }
    if (item->itemID == (UINT)-1) {
        return;
    }
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, ThemeWindowTextColor());
    HFONT font = (HFONT)SendMessageW(item->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ old = SelectObject(item->hDC, font ? font : GetAppFontForDpi(DpiGet())->GetHFont());
    InflateRect(&rc, -UiScalePx(10), -UiScalePx(10));
    RECT label = rc;
    auto* list = (ChoiceList*)GetWindowLongPtrW(item->hwndItem, GWLP_USERDATA);
    int labelWidth = item->CtlID == lcLibrary || !list ? 0 : list->labelWidth;
    label.right = label.left + labelWidth;
    WStr letter = ToWStrTemp(ChoiceLabel(item->itemID));
    if (labelWidth) {
        DrawTextW(item->hDC, CWStrTemp(letter), len(letter), &label, DT_SINGLELINE | DT_NOPREFIX);
    }
    rc.left += labelWidth;
    WStr text = LbGetTextTemp(item->hwndItem, item->itemID);
    if (item->CtlID == lcLibrary && len(text) >= 3 && text.s[len(text) - 1] == L'✓') {
        text = WStr(text.s, len(text) - 3);
        int size = LearningIconSize();
        RECT check{rc.right - size, rc.top, rc.right, rc.top + size};
        DrawGreenCheck(item->hDC, check, selected ? ThemeHotBackgroundColor() : ThemeControlBackgroundColor());
        rc.right -= size + UiScalePx(8);
    }
    DrawTextW(item->hDC, CWStrTemp(text), len(text), &rc, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    if (item->itemState & ODS_FOCUS) {
        RECT focus = item->rcItem;
        InflateRect(&focus, -UiScalePx(2), -UiScalePx(2));
        DrawFocusRect(item->hDC, &focus);
    }
    SelectObject(item->hDC, old);
}
static void FitPackDropdown(LearningWindow* w) {
    for (int id : {lcPack, lcDeck, lcVoice, lcActivity, lcScheduler, lcLookupSource, lcOnlineFirst, lcOnlineSecond,
                   lcOnlineThird, lcSense}) {
        HWND combo = Control(w, id);
        if (!combo) continue;
        HDC dc = PlatformFontMeasurementDC();
        HGDIOBJ font = SelectObject(dc, GetAppFontForDpi(DpiGet())->GetHFont());
        int width = 0;
        int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < count; i++) {
            int length = (int)SendMessageW(combo, CB_GETLBTEXTLEN, i, 0);
            WCHAR* text = AllocArray<WCHAR>(GetTempArena(), std::max(0, length) + 1);
            SendMessageW(combo, CB_GETLBTEXT, i, (LPARAM)text);
            SIZE size{};
            GetTextExtentPoint32W(dc, text, std::max(length, 0), &size);
            width = std::max(width, (int)size.cx + LearningIconSize() + UiScalePx(40));
        }
        SelectObject(dc, font);
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            width = std::min(width, (int)(monitor.rcWork.right - monitor.rcWork.left) - UiScalePx(32));
        SendMessageW(combo, CB_SETDROPPEDWIDTH, std::max(width, UiScalePx(240)), 0);
        int height = LearningRowHeight();
        SendMessageW(combo, CB_SETITEMHEIGHT, 0, height);
        SendMessageW(combo, CB_SETITEMHEIGHT, (WPARAM)-1, height);
        int chrome = std::max(0, HwndWindowRect(combo).dy - height);
        SendMessageW(combo, CB_SETITEMHEIGHT, (WPARAM)-1, std::max(1, height - chrome));
    }
}
static void DrawGreenCheck(HDC dc, RECT rc, Color background) {
    int size = std::min((int)(rc.right - rc.left), (int)(rc.bottom - rc.top));
    bool dark = GetRValue(background) + GetGValue(background) + GetBValue(background) < 384;
    Color green = dark ? RGB(93, 230, 145) : RGB(17, 120, 58);
    auto* glyphFont = GetUserGuiFont(StrL("Segoe UI Symbol"), size);
    HGDIOBJ font = SelectObject(dc, glyphFont->GetHFont());
    Color ink = SetTextColor(dc, green);
    int mode = SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, L"\u2713", 1, &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SetBkMode(dc, mode);
    SetTextColor(dc, ink);
    SelectObject(dc, font);
}
static void DrawPackChoice(LearningWindow* w, DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    bool selected = (item->itemState & ODS_SELECTED) != 0;
    Color background = selected ? ThemeHotBackgroundColor() : ThemeControlBackgroundColor();
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(item->hDC, &rc, brush);
    DeleteObject(brush);
    bool packChoice = item->CtlID == lcPack;
    bool deckChoice = item->CtlID == lcDeck;
    if (item->itemID == (UINT)-1) return;
    if (packChoice && item->itemID >= (UINT)len(w->packs)) return;
    bool installed = packChoice
                         ? w->packs[item->itemID].installed
                         : deckChoice && item->itemID < (UINT)len(w->deckInstalled) && w->deckInstalled[item->itemID];
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, (item->itemState & ODS_DISABLED) ? ThemeWindowTextDisabledColor() : ThemeWindowTextColor());
    HGDIOBJ font = SelectObject(item->hDC, GetAppFontForDpi(DpiGet())->GetHFont());
    InflateRect(&rc, -UiScalePx(8), 0);
    if (installed) {
        int size = LearningIconSize();
        int top = rc.top + ((rc.bottom - rc.top) - size) / 2;
        RECT check{rc.left, top, rc.left + size, top + size};
        DrawGreenCheck(item->hDC, check, background);
        rc.left += size + UiScalePx(8);
    }
    Str caption;
    if (packChoice) {
        const auto& pack = w->packs[item->itemID];
        caption = pack.installed ? pack.title : fmt("%s · %s", pack.title, Tr("not installed"));
    } else {
        int count = (int)SendMessageW(item->hwndItem, CB_GETLBTEXTLEN, item->itemID, 0);
        WCHAR* value = AllocArray<WCHAR>(GetTempArena(), std::max(0, count) + 1);
        SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, (LPARAM)value);
        caption = ToUtf8Temp(WStr(value, std::max(count, 0)));
        if (installed) {
            Str suffix = fmt(" · %s", Tr("installed"));
            if (str::EndsWith(caption, suffix)) caption = Str(caption.s, len(caption) - len(suffix));
        }
    }
    WStr title = ToWStrTemp(caption);
    DrawTextW(item->hDC, CWStrTemp(title), len(title), &rc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
    SelectObject(item->hDC, font);
}
static int LearningIconSize() {
    return std::max(GetAppFontSizeForDpi(DpiGet()), UiScalePx(std::clamp(gSettings->toolbarSize, 16, 64)));
}
static bool HasLearningGlyph(int id) {
    return id == lcLookup || id == lcSave || id == lcPractice || id == lcGuideStart || id == lcOpenVocabulary ||
           id == lcDownload || id == lcInstallDeck || id == lcExport || id == lcImport || id == lcImportPack ||
           LearningIconOnlyButton(id) || id == lcRecording || id == lcRecordingUk;
}
static void DrawLearningGlyph(HDC dc, int id, RECT rc, Color ink) {
    int size = std::min((int)(rc.right - rc.left), (int)(rc.bottom - rc.top));
    int x = rc.left, y = rc.top;
    auto px = [&](int value) { return x + value * size / 24; };
    auto py = [&](int value) { return y + value * size / 24; };
    auto line = [&](int x1, int y1, int x2, int y2) {
        MoveToEx(dc, px(x1), py(y1), nullptr);
        LineTo(dc, px(x2), py(y2));
    };
    HPEN pen = CreatePen(PS_SOLID, std::max(1, size / 12), ink);
    HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (id == lcLookup) {
        Ellipse(dc, px(3), py(3), px(17), py(17));
        line(15, 15, 22, 22);
    } else if (id == lcPractice || id == lcRecording || id == lcRecordingUk) {
        POINT triangle[]{{px(7), py(3)}, {px(21), py(12)}, {px(7), py(21)}};
        Polygon(dc, triangle, dimof(triangle));
    } else if (id == lcStopVoice) {
        Rectangle(dc, px(5), py(5), px(19), py(19));
    } else if (id == lcPronounce) {
        POINT speaker[]{{px(2), py(9)},   {px(7), py(9)},  {px(13), py(4)},
                        {px(13), py(20)}, {px(7), py(15)}, {px(2), py(15)}};
        Polygon(dc, speaker, dimof(speaker));
        int direction = SetArcDirection(dc, AD_CLOCKWISE);
        Arc(dc, px(13), py(7), px(20), py(17), px(17), py(7), px(17), py(17));
        Arc(dc, px(12), py(3), px(24), py(21), px(19), py(3), px(19), py(21));
        SetArcDirection(dc, direction);
    } else if (id == lcDownload || id == lcInstallDeck || id == lcImport || id == lcImportPack || id == lcExport) {
        bool up = id == lcExport;
        line(12, up ? 18 : 3, 12, up ? 3 : 18);
        line(7, up ? 8 : 13, 12, up ? 3 : 18);
        line(17, up ? 8 : 13, 12, up ? 3 : 18);
        line(3, 18, 3, 22);
        line(3, 22, 21, 22);
        line(21, 22, 21, 18);
    } else if (id == lcSave) {
        POINT bookmark[]{{px(5), py(3)}, {px(19), py(3)}, {px(19), py(22)}, {px(12), py(17)}, {px(5), py(22)}};
        Polygon(dc, bookmark, dimof(bookmark));
    } else {
        RoundRect(dc, px(2), py(4), px(22), py(21), size / 6, size / 6);
        line(12, 4, 12, 21);
        line(5, 8, 9, 8);
        line(15, 8, 19, 8);
        line(5, 12, 9, 12);
        line(15, 12, 19, 12);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}
static void DrawLearningButton(DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    auto* w = (LearningWindow*)GetWindowLongPtrW(GetParent(item->hwndItem), GWLP_USERDATA);
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool hot = w && w->hoverButton == item->hwndItem && !disabled;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;
    bool primary = LearningPrimaryAction(w, item->CtlID);
    Color bg = disabled         ? ThemeControlBackgroundColor()
               : primary        ? ThemeBrandColor()
               : hot || pressed ? ThemeHotBackgroundColor()
                                : ThemeControlBackgroundColor();
    Color ink = disabled ? ThemeWindowTextDisabledColor() : primary ? ThemeBrandTextColor() : ThemeWindowTextColor();
    HBRUSH brush = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    FillRect(item->hDC, &rc, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush(bg);
    Color edge = disabled                                            ? ThemeDisabledEdgeColor()
                 : (hot || pressed || (item->itemState & ODS_FOCUS)) ? ThemeHotEdgeColor()
                 : primary                                           ? bg
                                                                     : ThemeEdgeColor();
    HPEN pen = CreatePen(PS_SOLID, UiScalePx(1), edge);
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rc.left, rc.top, rc.right, rc.bottom, UiScalePx(14), UiScalePx(14));
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, ink);
    HGDIOBJ oldFont = SelectObject(item->hDC, GetAppFontForDpi(DpiGet())->GetHFont());
    RECT textRect = rc;
    InflateRect(&textRect, -UiScalePx(8), 0);
    if (HasLearningGlyph(item->CtlID)) {
        int size = LearningIconSize();
        int left = LearningIconOnlyButton(item->CtlID) ? (rc.left + rc.right - size) / 2 : textRect.left;
        RECT icon{left, (rc.bottom + rc.top - size) / 2, left + size, (rc.bottom + rc.top + size) / 2};
        if (pressed) OffsetRect(&icon, 1, 1);
        DrawLearningGlyph(item->hDC, item->CtlID, icon, primary || disabled ? ink : ThemeBrandColor());
        textRect.left += size + UiScalePx(8);
    }
    Str text = LearningButtonCaption(item->hwndItem, item->CtlID);
    RECT measured = textRect;
    DrawTextW(item->hDC, CWStrTemp(text), -1, &measured, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    textRect.top += std::max(0, (int)((textRect.bottom - textRect.top) - (measured.bottom - measured.top)) / 2);
    if (pressed) OffsetRect(&textRect, 1, 1);
    DrawTextW(item->hDC, CWStrTemp(text), -1, &textRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    if (item->itemState & ODS_FOCUS) {
        RECT focus = rc;
        InflateRect(&focus, -UiScalePx(4), -UiScalePx(4));
        DrawFocusRect(item->hDC, &focus);
    }
    SelectObject(item->hDC, oldFont);
}
static LRESULT CALLBACK LearningSplitProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* w = (LearningWindow*)data;
    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
        return TRUE;
    }
    if (msg == WM_LBUTTONDOWN) {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        MapWindowPoints(hwnd, w->hwnd, &point, 1);
        w->splitPointerY = point.y + w->scrollY;
        w->splitStartShare = w->promptShare;
        RECT prompt;
        GetWindowRect(Control(w, lcDetails), &prompt);
        w->splitPromptHeight = prompt.bottom - prompt.top;
        w->splitting = true;
        SetFocus(hwnd);
        SetCapture(hwnd);
        return 0;
    }
    if (msg == WM_MOUSEMOVE && w->splitting) {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        MapWindowPoints(hwnd, w->hwnd, &point, 1);
        int delta = point.y + w->scrollY - w->splitPointerY;
        int share = delta ? MulDiv(w->splitPromptHeight + delta, 100, std::max(w->splitHeight, 1)) : w->splitStartShare;
        share = std::clamp(share, 10, 80);
        if (share != w->promptShare) {
            w->promptShare = share;
            LayoutLearning(w);
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED) {
        if (w->splitting) {
            w->splitting = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            SaveLearningPrefs(w);
        }
        return 0;
    }
    if (msg == WM_GETDLGCODE) return DLGC_WANTARROWS;
    if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME)) {
        w->promptShare = wp == VK_HOME ? 30 : std::clamp(w->promptShare + (wp == VK_DOWN ? 5 : -5), 10, 80);
        LayoutLearning(w);
        SaveLearningPrefs(w);
        return 0;
    }
    if (msg == WM_CONTEXTMENU) {
        SetFocus(hwnd);
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (point.x == -1 && point.y == -1) {
            RECT bounds;
            GetWindowRect(hwnd, &bounds);
            point = {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
        }
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, CWStrTemp(Tr("More word space")));
        AppendMenuW(menu, MF_STRING, 2, CWStrTemp(Tr("More answer space")));
        AppendMenuW(menu, MF_STRING, 3, CWStrTemp(Tr("Reset panels")));
        int action =
            TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, w->hwnd, nullptr);
        DestroyMenu(menu);
        if (action) {
            w->promptShare = action == 3 ? 30 : std::clamp(w->promptShare + (action == 1 ? 5 : -5), 10, 80);
            LayoutLearning(w);
            SaveLearningPrefs(w);
        }
        return 0;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) InvalidateRect(hwnd, nullptr, false);
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, LearningSplitProc, id);
    return DefSubclassProc(hwnd, msg, wp, lp);
}
static void ShowLearningButtonTooltip(LearningWindow* w, HWND hwnd, bool keyboard) {
    int id = GetDlgCtrlID(hwnd);
    if (!LearningIconOnlyButton(id) && id != lcRecording && id != lcRecordingUk) return;
    DpiScope dpi(hwnd);
    if (!w->buttonTooltip) {
        w->buttonTooltip = new Tooltip();
        w->buttonTooltip->Create({.parent = w->hwnd, .font = GetAppFontForDpi(DpiGet())});
    }
    w->buttonTooltip->SetFont(GetAppFontForDpi(DpiGet()));
    Rect bounds = HwndMapRectToWindow(HwndClientRect(hwnd), hwnd, w->hwnd);
    Str name = HwndGetTextTemp(hwnd);
    if (keyboard) {
        RECT screen;
        GetWindowRect(hwnd, &screen);
        w->buttonTooltip->SetSingleAt(name, bounds, {screen.left, screen.bottom + UiScalePx(4)}, false);
    } else {
        w->buttonTooltip->SetSingle(name, bounds, false);
    }
    w->tooltipButton = hwnd;
}
static LRESULT CALLBACK LearningButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* w = (LearningWindow*)data;
    if (msg == WM_MOUSEMOVE && IsWindowEnabled(hwnd)) {
        if (w->hoverButton != hwnd) {
            w->hoverButton = hwnd;
            InvalidateRect(hwnd, nullptr, false);
            ShowLearningButtonTooltip(w, hwnd, false);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
    } else if (msg == WM_MOUSELEAVE || msg == WM_ENABLE) {
        if (w->hoverButton == hwnd) w->hoverButton = nullptr;
        if (w->tooltipButton == hwnd && w->buttonTooltip) {
            w->buttonTooltip->Delete();
            w->tooltipButton = nullptr;
        }
        InvalidateRect(hwnd, nullptr, false);
    } else if (msg == WM_SETFOCUS && IsWindowEnabled(hwnd)) {
        ShowLearningButtonTooltip(w, hwnd, true);
    } else if (msg == WM_KILLFOCUS) {
        if (w->tooltipButton == hwnd && w->buttonTooltip) {
            w->buttonTooltip->Delete();
            w->tooltipButton = nullptr;
        }
    } else if (msg == WM_NCDESTROY) {
        if (w->hoverButton == hwnd) w->hoverButton = nullptr;
        if (w->tooltipButton == hwnd) w->tooltipButton = nullptr;
        RemoveWindowSubclass(hwnd, LearningButtonProc, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
static void DrawLearningPanel(LearningWindow* w, DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    HBRUSH background = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    FillRect(item->hDC, &rc, background);
    DeleteObject(background);
    bool title = item->CtlID == lcTitle;
    HBRUSH brush = CreateSolidBrush(title ? ThemeMainWindowBackgroundColor() : ThemeControlBackgroundColor());
    HPEN pen = CreatePen(PS_SOLID, UiScalePx(1), title ? ThemeMainWindowBackgroundColor() : ThemeEdgeColor());
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rc.left, rc.top, rc.right, rc.bottom, UiScalePx(16), UiScalePx(16));
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    if (title) {
        int size = LearningIconSize();
        RECT icon{rc.left, rc.top, rc.left + size, rc.top + size};
        DrawLearningGlyph(item->hDC, w->dictionary ? lcGuideStart : lcPractice, icon, ThemeBrandColor());
        rc.left += size + UiScalePx(12);
    } else
        InflateRect(&rc, -UiScalePx(16), -UiScalePx(16));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, ThemeWindowTextColor());
    HGDIOBJ font =
        SelectObject(item->hDC, title && w->titleFont ? w->titleFont : GetAppFontForDpi(DpiGet())->GetHFont());
    DrawTextW(item->hDC, CWStrTemp(Read(w, item->CtlID)), -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(item->hDC, font);
}
static void DrawLearningFrames(LearningWindow* w, HDC dc) {
    for (int id : {lcQuery, lcDetails, lcAnswer, lcNewDeck, lcLibrary, lcChoices, lcPairs}) {
        HWND child = Control(w, id);
        if (!child || !(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
        RECT rc;
        GetWindowRect(child, &rc);
        MapWindowPoints(nullptr, w->hwnd, (POINT*)&rc, 2);
        InflateRect(&rc, UiScalePx(2), UiScalePx(2));
        HBRUSH brush = CreateSolidBrush(ThemeControlBackgroundColor());
        HPEN pen = CreatePen(PS_SOLID, UiScalePx(1), GetFocus() == child ? ThemeBrandColor() : ThemeEdgeColor());
        HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, UiScalePx(14), UiScalePx(14));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    }
}
static HICON MakeLearningIcon(int size, bool dictionary, Color accent, Color text) {
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
    u32* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void**)&pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        DeleteDC(dc);
        return nullptr;
    }
    memset(pixels, 0, size * size * sizeof(u32));
    HGDIOBJ old = SelectObject(dc, bitmap);
    HBRUSH green = CreateSolidBrush(accent);
    HPEN border = CreatePen(PS_SOLID, std::max(1, size / 16), text);
    HGDIOBJ oldBrush = SelectObject(dc, green), oldPen = SelectObject(dc, border);
    int pad = std::max(2, size / 8);
    RoundRect(dc, pad, pad, size - pad, size - pad, size / 5, size / 5);
    MoveToEx(dc, size / 2, pad + 2, nullptr);
    LineTo(dc, size / 2, size - pad - 2);
    if (dictionary) {
        for (int y : {size / 3, size / 2, size * 2 / 3}) {
            MoveToEx(dc, pad + size / 12, y, nullptr);
            LineTo(dc, size / 2 - size / 12, y);
            MoveToEx(dc, size / 2 + size / 12, y, nullptr);
            LineTo(dc, size - pad - size / 12, y);
        }
    } else {
        MoveToEx(dc, size / 2 + size / 10, size / 2, nullptr);
        LineTo(dc, size * 2 / 3, size * 5 / 8);
        LineTo(dc, size * 4 / 5, size / 3);
        MoveToEx(dc, pad + size / 12, size / 3, nullptr);
        LineTo(dc, size / 2 - size / 12, size / 3);
    }
    GdiFlush();
    HRGN opaque = CreateRoundRectRgn(pad, pad, size - pad, size - pad, size / 5, size / 5);
    for (int i = 0; i < size * size; i++)
        if ((pixels[i] & 0xffffff) || PtInRegion(opaque, i % size, i / size)) pixels[i] |= 0xff000000;
    DeleteObject(opaque);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    SelectObject(dc, old);
    DeleteObject(green);
    DeleteObject(border);
    DeleteDC(dc);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO iconInfo{TRUE, 0, 0, mask, bitmap};
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(mask);
    DeleteObject(bitmap);
    return icon;
}
static void SetLearningIcons(LearningWindow* w) {
    HICON smallIcon = MakeLearningIcon(DpiScale(16), w->dictionary, ThemeBrandColor(), ThemeBrandTextColor());
    HICON largeIcon = MakeLearningIcon(DpiScale(32), w->dictionary, ThemeBrandColor(), ThemeBrandTextColor());
    SendMessageW(w->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    SendMessageW(w->hwnd, WM_SETICON, ICON_BIG, (LPARAM)largeIcon);
    DestroyIcon(w->smallIcon);
    DestroyIcon(w->largeIcon);
    w->smallIcon = smallIcon;
    w->largeIcon = largeIcon;
}
static void DrawFeedback(LearningWindow* w, DRAWITEMSTRUCT* item) {
    Color window = ThemeMainWindowBackgroundColor();
    bool dark = GetRValue(window) + GetGValue(window) + GetBValue(window) < 384;
    Color bg = w->feedbackKind > 0 ? (dark ? RGB(17, 62, 37) : RGB(224, 248, 232))
                                   : (dark ? RGB(77, 27, 32) : RGB(255, 231, 231));
    Color ink = w->feedbackKind > 0 ? (dark ? RGB(142, 237, 173) : RGB(20, 91, 43))
                                    : (dark ? RGB(255, 176, 181) : RGB(142, 27, 34));
    HBRUSH brush = CreateSolidBrush(window);
    FillRect(item->hDC, &item->rcItem, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush(bg);
    HGDIOBJ oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
    const RECT& bounds = item->rcItem;
    RoundRect(item->hDC, bounds.left, bounds.top, bounds.right, bounds.bottom, UiScalePx(10), UiScalePx(10));
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(brush);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, ink);
    HGDIOBJ font = SelectObject(item->hDC, GetAppFontForDpi(DpiGet())->GetHFont());
    RECT rc = item->rcItem;
    InflateRect(&rc, -UiScalePx(12), -UiScalePx(6));
    Str message = Read(w, lcFeedback);
    if (w->feedbackKind > 0 && str::StartsWith(message, StrL("✓"))) {
        int size = GetAppFontSizeForDpi(DpiGet());
        RECT check{rc.left, rc.top, rc.left + size, rc.top + size};
        DrawGreenCheck(item->hDC, check, bg);
        rc.left += size + UiScalePx(8);
        message = Str(message.s + len(StrL("✓")), len(message) - len(StrL("✓")));
        str::TrimWSInPlace(message, str::TrimOpt::Left);
    }
    DrawTextW(item->hDC, CWStrTemp(message), -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(item->hDC, font);
    RECT bar = item->rcItem;
    bar.top = bar.bottom - UiScalePx(3);
    ULONGLONG age = w->feedbackStart ? std::min(GetTickCount64() - w->feedbackStart, (ULONGLONG)kFeedbackDuration)
                                     : kFeedbackDuration;
    bar.right = bar.left + (int)((bar.right - bar.left) * age / kFeedbackDuration);
    brush = CreateSolidBrush(ink);
    FillRect(item->hDC, &bar, brush);
    DeleteObject(brush);
}

static void RefreshLearningStyle(LearningWindow* w) {
    SetLearningIcons(w);
    DeleteObject(w->titleFont);
    LOGFONTW title{};
    GetObjectW(GetAppFontForDpi(DpiGet())->GetHFont(), sizeof(title), &title);
    title.lfWeight = FW_BOLD;
    title.lfHeight = title.lfHeight * 6 / 5;
    w->titleFont = CreateFontIndirectW(&title);
    DeleteObject(w->background);
    DeleteObject(w->fieldBackground);
    w->background = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    w->fieldBackground = CreateSolidBrush(ThemeControlBackgroundColor());
    for (HWND child : w->controls) {
        if (child) {
            SendMessageW(child, WM_SETFONT,
                         (WPARAM)(child == Control(w, lcTitle) ? w->titleFont : GetAppFontForDpi(DpiGet())->GetHFont()),
                         true);
            WCHAR klass[32]{};
            GetClassNameW(child, klass, dimof(klass));
            if (_wcsicmp(klass, L"EDIT") == 0) EditSetDefaultMargins(child);
            InvalidateRect(child, nullptr, true);
        }
    }
    ZeroMemory(w->boundsValid, sizeof(w->boundsValid));
    FitPackDropdown(w);
    FormatDetails(w);
    WindowApplyScaledCaption(w->hwnd);
    LayoutLearning(w, true);
    RedrawWindow(w->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
static LRESULT CALLBACK LearningWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DpiScope dpi(hwnd);
    auto* w = (LearningWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        w = (LearningWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        w->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
    }
    if (!w) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    switch (msg) {
        case kSpeechMessage: {
            if (!w->speech) return 0;
            RefreshVoices(w);
            Str message;
            auto state = w->speech->GetStatus(&message);
            if (len(message)) Status(w, message);
            str::Free(message);
            EnableWindow(Control(w, lcStopVoice),
                         state == DictionarySpeechStatus::Loading || state == DictionarySpeechStatus::Speaking);
            return 0;
        }
        case WM_SIZE:
            LayoutLearning(w, true);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof(si), SIF_ALL};
            GetScrollInfo(hwnd, SB_VERT, &si);
            int offset = w->scrollY;
            switch (LOWORD(wp)) {
                case SB_LINEUP:
                    offset -= DpiScale(32);
                    break;
                case SB_LINEDOWN:
                    offset += DpiScale(32);
                    break;
                case SB_PAGEUP:
                    offset -= (int)si.nPage;
                    break;
                case SB_PAGEDOWN:
                    offset += (int)si.nPage;
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    offset = AppScrollbarTrackPos(hwnd, si.nTrackPos);
                    break;
                case SB_TOP:
                    offset = 0;
                    break;
                case SB_BOTTOM:
                    offset = si.nMax;
                    break;
            }
            ScrollLearning(w, offset);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            RECT client;
            GetClientRect(hwnd, &client);
            int movement = WheelDistance(wp, WheelStep(client.bottom), w->wheelRemainder);
            ScrollLearning(w, w->scrollY - movement);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = (MINMAXINFO*)lp;
            info->ptMinTrackSize = {DpiScale(540), DpiScale(440)};
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) {
                SetCurrentModelessDialog(hwnd);
                if (w->ready && !w->opening) {
                    RefreshDecks(w);
                    if (!w->dictionary && !w->practice) {
                        RefreshLibrary(w);
                    }
                }
            } else if (GetCurrentModelessDialog() == hwnd) {
                SetCurrentModelessDialog(nullptr);
            }
            break;
        case WM_CONTEXTMENU:
            if ((HWND)wp == Control(w, lcLibrary)) {
                LibraryContextMenu(w, lp);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* notice = (NMHDR*)lp;
            if (notice->hwndFrom != Control(w, lcDetails) || notice->code != EN_LINK) break;
            auto* link = (ENLINK*)lp;
            if (link->msg != WM_LBUTTONUP) break;
            for (const auto& run : w->detailRuns) {
                if (run.style != DetailStyle::WordLink || link->chrg.cpMin < run.start ||
                    link->chrg.cpMin >= run.start + run.length)
                    continue;
                Str word = str::DupTemp(run.word);
                if (w->dictionary) {
                    Text(w, lcQuery, word);
                    LookupWord(w);
                } else
                    ShowDictionaryDialog(w->owner, word);
                return 0;
            }
            int length = link->chrg.cpMax - link->chrg.cpMin;
            if (length <= 0 || length > 2048) return 0;
            WCHAR* url = AllocArray<WCHAR>(GetTempArena(), length + 1);
            TEXTRANGEW range{link->chrg, url};
            SendMessageW(Control(w, lcDetails), EM_GETTEXTRANGE, 0, (LPARAM)&range);
            Str value = ToUtf8Temp(WStr(url, length));
            if (str::StartsWithI(value, StrL("https://"))) SumatraLaunchBrowser(value);
            return 0;
        }
        case WM_COMMAND: {
            WCHAR klass[32]{};
            if (lp) {
                GetClassNameW((HWND)lp, klass, dimof(klass));
            }
            int notice = HIWORD(wp);
            if ((_wcsicmp(klass, L"EDIT") == 0 && notice == EN_SETFOCUS) ||
                (_wcsicmp(klass, L"BUTTON") == 0 && notice == BN_SETFOCUS) ||
                (_wcsicmp(klass, L"COMBOBOX") == 0 && notice == CBN_SETFOCUS) ||
                ((_wcsicmp(klass, L"LISTBOX") == 0 || _wcsicmp(klass, kChoiceListClass) == 0) &&
                 notice == LBN_SETFOCUS)) {
                InvalidateRect(hwnd, nullptr, false);
                RevealFocusedControl(w, (HWND)lp);
                return 0;
            }
            if (_wcsicmp(klass, L"EDIT") == 0 && notice == EN_CHANGE) FitLearningInput(w, LOWORD(wp));
            LearningAction(w, LOWORD(wp), HIWORD(wp));
            return 0;
        }
        case WM_MEASUREITEM: {
            auto* item = (MEASUREITEMSTRUCT*)lp;
            if (item->CtlType == ODT_COMBOBOX) {
                item->itemHeight = LearningRowHeight();
                return TRUE;
            }
            if (item->CtlType == ODT_LISTBOX) {
                item->itemHeight = GetAppFontSizeForDpi(DpiGet()) + DpiScale(20);
                return TRUE;
            }
            break;
        }
        case WM_TIMER:
            if (wp == kFeedbackTimer) {
                if (GetTickCount64() - w->feedbackStart >= kFeedbackDuration) KillTimer(hwnd, kFeedbackTimer);
                InvalidateRect(Control(w, lcFeedback), nullptr, false);
                return 0;
            }
            break;
        case WM_DRAWITEM:
            if (((DRAWITEMSTRUCT*)lp)->CtlID == lcSplit) {
                auto* item = (DRAWITEMSTRUCT*)lp;
                FillRect(item->hDC, &item->rcItem, w->background);
                RECT rc = item->rcItem;
                int x = (rc.left + rc.right) / 2, y = (rc.top + rc.bottom) / 2;
                HPEN pen = CreatePen(PS_SOLID, UiScalePx(2),
                                     GetFocus() == item->hwndItem ? ThemeBrandColor() : ThemeWindowDarkerTextColor());
                HGDIOBJ old = SelectObject(item->hDC, pen);
                MoveToEx(item->hDC, x - UiScalePx(32), y - UiScalePx(2), nullptr);
                LineTo(item->hDC, x + UiScalePx(32), y - UiScalePx(2));
                MoveToEx(item->hDC, x - UiScalePx(32), y + UiScalePx(2), nullptr);
                LineTo(item->hDC, x + UiScalePx(32), y + UiScalePx(2));
                SelectObject(item->hDC, old);
                DeleteObject(pen);
                if (GetFocus() == item->hwndItem) DrawFocusRect(item->hDC, &rc);
                return TRUE;
            }
            if (((DRAWITEMSTRUCT*)lp)->CtlType == ODT_COMBOBOX) {
                DrawPackChoice(w, (DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            if (((DRAWITEMSTRUCT*)lp)->CtlID == lcTitle || ((DRAWITEMSTRUCT*)lp)->CtlID == lcGuideText) {
                DrawLearningPanel(w, (DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            if (((DRAWITEMSTRUCT*)lp)->CtlID == lcFeedback) {
                DrawFeedback(w, (DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            if (((DRAWITEMSTRUCT*)lp)->CtlType == ODT_LISTBOX) {
                DrawLearningChoice((DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            if (((DRAWITEMSTRUCT*)lp)->CtlType == ODT_BUTTON) {
                DrawLearningButton((DRAWITEMSTRUCT*)lp);
                return TRUE;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            bool field = (HWND)lp == Control(w, lcDetails) || msg != WM_CTLCOLORSTATIC;
            SetTextColor((HDC)wp,
                         (HWND)lp == Control(w, lcStatus) ? ThemeWindowDarkerTextColor() : ThemeWindowTextColor());
            SetBkColor((HDC)wp, field ? ThemeControlBackgroundColor() : ThemeMainWindowBackgroundColor());
            return (LRESULT)(field ? w->fieldBackground : w->background);
        }
        case WM_PRINTCLIENT: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, w->background);
            DrawLearningFrames(w, (HDC)wp);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC target = BeginPaint(hwnd, &ps);
            if (IsRectEmpty(&ps.rcPaint)) {
                EndPaint(hwnd, &ps);
                return 0;
            }
            RECT client;
            GetClientRect(hwnd, &client);
            DoubleBuffer buffer(hwnd, ToRect(ps.rcPaint));
            HDC dc = buffer.GetDC();
            FillRect(dc, &client, w->background);
            DrawLearningFrames(w, dc);
            buffer.Flush(target);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            if (msg == WM_SETTINGCHANGE) {
                BOOL animate = FALSE;
                SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
                if (!animate) {
                    KillTimer(hwnd, kFeedbackTimer);
                    w->feedbackStart = 0;
                }
            }
            RefreshLearningStyle(w);
            return 0;
        case WM_DPICHANGED: {
            RECT* rc = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshLearningStyle(w);
            return 0;
        }
        case WM_NCDESTROY:
            if (GetCurrentModelessDialog() == hwnd) {
                SetCurrentModelessDialog(nullptr);
            }
            VecRemove(gLearningWindows, w);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete w;
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static void PaintLearningCombo(HWND hwnd, HDC dc, LearningWindow* w) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    bool enabled = IsWindowEnabled(hwnd) != FALSE;
    Color bg = ThemeControlBackgroundColor();
    HBRUSH brush = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, UiScalePx(1)),
                         !enabled             ? ThemeDisabledEdgeColor()
                         : GetFocus() == hwnd ? ThemeBrandColor()
                                              : ThemeEdgeColor());
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, UiScalePx(10), UiScalePx(10));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    COMBOBOXINFO info{sizeof(info)};
    GetComboBoxInfo(hwnd, &info);
    int arrow = std::max((int)(info.rcButton.right - info.rcButton.left), UiScalePx(24));
    RECT text{rc.left + UiScalePx(2), rc.top + UiScalePx(2), rc.right - arrow, rc.bottom - UiScalePx(2)};
    DRAWITEMSTRUCT item{};
    item.CtlType = ODT_COMBOBOX;
    item.CtlID = GetDlgCtrlID(hwnd);
    item.itemID = (UINT)SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
    item.itemAction = ODA_DRAWENTIRE;
    item.itemState = enabled ? 0 : ODS_DISABLED;
    item.hwndItem = hwnd;
    item.hDC = dc;
    item.rcItem = text;
    DrawPackChoice(w, &item);
    int cx = rc.right - arrow / 2 - UiScalePx(2), cy = (rc.top + rc.bottom) / 2;
    int half = std::max(2, UiScalePx(4));
    pen = CreatePen(PS_SOLID, std::max(1, UiScalePx(2)),
                    enabled ? ThemeWindowTextColor() : ThemeWindowTextDisabledColor());
    oldPen = SelectObject(dc, pen);
    MoveToEx(dc, cx - half, cy - half / 2, nullptr);
    LineTo(dc, cx, cy + half / 2);
    LineTo(dc, cx + half, cy - half / 2);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}
static LRESULT CALLBACK LearningComboProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* w = (LearningWindow*)data;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintLearningCombo(hwnd, dc, w);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_PRINTCLIENT) {
        PaintLearningCombo(hwnd, (HDC)wp, w);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ENABLE || msg == CB_SETCURSEL || msg == CB_SHOWDROPDOWN)
        InvalidateRect(hwnd, nullptr, false);
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, LearningComboProc, id);
    return result;
}
static void PaintLearningEditTop(HWND hwnd, LearningWindow* w, HDC dc) {
    Rect bounds = HwndWindowRect(hwnd);
    POINT origin{};
    ClientToScreen(hwnd, &origin);
    RECT top{0, 0, bounds.dx, origin.y - bounds.y};
    if (top.bottom <= 0) return;
    HBRUSH brush = w->fieldBackground;
    if (!brush) brush = CreateSolidBrush(ThemeControlBackgroundColor());
    FillRect(dc, &top, brush);
    if (!w->fieldBackground) DeleteObject(brush);
}

static LRESULT CALLBACK LearningEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* w = (LearningWindow*)data;
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, LearningEditProc, id);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (msg == WM_NCCALCSIZE) {
        LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        RECT* client = wp ? &((NCCALCSIZE_PARAMS*)lp)->rgrc[0] : (RECT*)lp;
        HFONT handle = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        auto* font = handle ? GetPlatformFont(handle) : GetAppFontForDpi(DpiGetForHwnd(hwnd));
        int extra = (int)(client->bottom - client->top) - PlatformFontLineHeight(font);
        if (extra > 1) client->top += extra / 2;
        return result;
    }
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == WM_SETFONT) {
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    if (msg == WM_NCPAINT) {
        HDC dc = GetWindowDC(hwnd);
        if (dc) {
            PaintLearningEditTop(hwnd, w, dc);
            ReleaseDC(hwnd, dc);
        }
    }
    if (msg == WM_PRINT && (lp & PRF_NONCLIENT)) PaintLearningEditTop(hwnd, w, (HDC)wp);
    return result;
}

static HWND MakeControl(LearningWindow* w, int id, const WCHAR* klass, Str text, DWORD style = 0, bool visible = true) {
    if (id == lcDetails) {
        static HMODULE richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (richEdit) klass = MSFTEDIT_CLASS;
    }
    if (_wcsicmp(klass, L"COMBOBOX") == 0) style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
    if (_wcsicmp(klass, L"EDIT") == 0 || _wcsicmp(klass, MSFTEDIT_CLASS) == 0 || _wcsicmp(klass, kChoiceListClass) == 0)
        style &= ~WS_BORDER;
    HWND child =
        CreateWindowExW(0, klass, CWStrTemp(text),
                        WS_CHILD | (visible ? WS_VISIBLE : 0) | (_wcsicmp(klass, L"STATIC") ? WS_TABSTOP : 0) | style,
                        0, 0, 1, 1, w->hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    w->controls[id] = child;
    if (_wcsicmp(klass, L"EDIT") == 0 && !(style & ES_MULTILINE))
        SetWindowSubclass(child, LearningEditProc, 1, (DWORD_PTR)w);
    if (_wcsicmp(klass, L"COMBOBOX") == 0) {
        SetWindowSubclass(child, LearningComboProc, 1, (DWORD_PTR)w);
        RoundControlUseCustomPaint(child);
    }
    if (id == lcQuery || id == lcDetails || id == lcAnswer || id == lcNewDeck) RoundControlUseCustomPaint(child);
    if (IsRichDetails(child)) {
        SendMessageW(child, EM_SETEVENTMASK, 0, ENM_LINK);
        SendMessageW(child, EM_AUTOURLDETECT, TRUE, 0);
        SendMessageW(child, EM_SETBKGNDCOLOR, 0, ThemeControlBackgroundColor());
    }
    if (_wcsicmp(klass, L"EDIT") == 0)
        SendMessageW(child, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(DpiScale(10), DpiScale(10)));
    HFONT font = id == lcTitle && w->titleFont ? w->titleFont : GetAppFontForDpi(DpiGet())->GetHFont();
    SendMessageW(child, WM_SETFONT, (WPARAM)font, false);
    if (_wcsicmp(klass, L"EDIT") == 0) EditSetDefaultMargins(child);
    if ((style & WS_VSCROLL) && _wcsicmp(klass, L"COMBOBOX") != 0) InstallAppScrollbar(child);
    return child;
}

static void MakeButton(LearningWindow* w, int id, Str text, bool visible = true) {
    HWND child = MakeControl(w, id, L"BUTTON", text, BS_OWNERDRAW | BS_NOTIFY, visible);
    SetWindowSubclass(child, LearningButtonProc, 1, (DWORD_PTR)w);
}
static HWND MakeLearningSplit(LearningWindow* w, bool visible = true) {
    HWND split = MakeControl(w, lcSplit, L"STATIC", Tr("Resize word and answer panels"),
                             SS_OWNERDRAW | SS_NOTIFY | WS_TABSTOP, visible);
    SetWindowSubclass(split, LearningSplitProc, 1, (DWORD_PTR)w);
    return split;
}

static void OrderLearningGroup(LearningWindow* w, std::initializer_list<int> ids) {
    HWND previous = nullptr;
    for (int id : ids) {
        HWND child = Control(w, id);
        if (!child) continue;
        if (previous) SetWindowPos(child, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        previous = child;
    }
}
static void EnsureGuideControls(LearningWindow* w) {
    if (Control(w, lcGuideText)) return;
    DpiScope dpi(w->hwnd);
    MakeControl(w, lcGuideText, L"STATIC", {}, SS_OWNERDRAW | SS_NOPREFIX, false);
    MakeButton(w, lcGuidePrev, Tr("Back"), false);
    MakeButton(w, lcGuideNext, Tr("Next"), false);
    MakeButton(w, lcGuideSkip, Tr("Skip"), false);
    MakeButton(w, lcGuideAction, Tr("Go to lookup"), false);
    OrderLearningGroup(w, {lcGuideStart, lcGuideText, lcGuidePrev, lcGuideNext, lcGuideSkip, lcGuideAction});
}
static void EnsureLearningTools(LearningWindow* w) {
    if (Control(w, w->dictionary ? lcPack : lcNewDeck)) return;
    DpiScope dpi(w->hwnd);
    bool ready = w->ready, updating = w->updating;
    w->ready = false;
    w->updating = true;
    defer {
        w->ready = ready;
        w->updating = updating;
    };
    if (w->dictionary) {
        MakeButton(w, lcSourcesToggle, w->sourcesVisible ? Tr("Hide sources") : Tr("Sources…"), false);
        MakeControl(w, lcPack, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL, false);
        MakeButton(w, lcImportPack, Tr("Import…"), false);
        MakeButton(w, lcDownload, Tr("Download"), false);
        MakeButton(w, lcRemovePack, Tr("Remove"), false);
        MakeControl(w, lcPackInfo, L"STATIC", {}, SS_NOPREFIX, false);
        MakeButton(w, lcCancelDownload, Tr("Cancel download"), false);
        OrderLearningGroup(w, {lcLookupSource, lcSourcesToggle});
        OrderLearningGroup(
            w, {lcRecordingUk, lcPack, lcImportPack, lcDownload, lcRemovePack, lcPackInfo, lcCancelDownload});
        RefreshPacks(w);
        for (int id : {lcPack, lcImportPack, lcDownload, lcRemovePack}) EnableWindow(Control(w, id), !w->busy);
    } else {
        MakeButton(w, lcInstallDeck, Tr("Install deck"), false);
        MakeButton(w, lcDeleteDeck, Tr("Delete deck"), false);
        MakeControl(w, lcNewDeck, L"EDIT", {}, ES_AUTOHSCROLL | WS_BORDER, false);
        SendMessageW(Control(w, lcNewDeck), EM_SETCUEBANNER, true, (LPARAM)L"New deck name");
        MakeButton(w, lcCreateDeck, Tr("Create deck"), false);
        MakeButton(w, lcExport, Tr("Export…"), false);
        MakeButton(w, lcImport, Tr("Import…"), false);
        OrderLearningGroup(w, {lcVoice, lcInstallDeck, lcDeleteDeck, lcNewDeck, lcCreateDeck, lcExport, lcImport});
        EnableWindow(Control(w, lcInstallDeck), !w->busy);
    }
}
static void EnsureOnlineControls(LearningWindow* w) {
    if (!w->dictionary || Control(w, lcOnlineFirst)) return;
    DpiScope dpi(w->hwnd);
    bool updating = w->updating;
    w->updating = true;
    defer {
        w->updating = updating;
    };
    int position = 0;
    for (int setting : {lcOnlineFirst, lcOnlineSecond, lcOnlineThird}) {
        MakeControl(w, setting, L"COMBOBOX", {}, CBS_DROPDOWNLIST, false);
        for (Str source :
             {Tr("Disabled"), StrL("Wiktionary (Kaikki)"), StrL("Wiktionary REST"), StrL("Free Dictionary API")})
            AddChoice(w, setting, fmt("%d. %s", position + 1, source));
        SendMessageW(Control(w, setting), CB_SETCURSEL, w->onlineOrder[position++], 0);
    }
    OrderLearningGroup(w, {lcSourcesToggle, lcOnlineFirst, lcOnlineSecond, lcOnlineThird});
    FitPackDropdown(w);
}
static void EnsurePracticeControls(LearningWindow* w) {
    if (w->dictionary || Control(w, lcAnswer)) return;
    DpiScope dpi(w->hwnd);
    MakeControl(w, lcSessionInfo, L"STATIC", {}, SS_NOPREFIX, false);
    MakeControl(w, lcAnswer, L"EDIT", {}, ES_AUTOHSCROLL | WS_BORDER, false);
    MakeControl(w, lcChoices, kChoiceListClass, {}, WS_VSCROLL | WS_BORDER, false);
    MakeControl(w, lcPairs, kChoiceListClass, {}, WS_VSCROLL | WS_BORDER, false);
    MakeLearningSplit(w, false);
    MakeButton(w, lcCheck, Tr("Check answer"), false);
    MakeButton(w, lcReveal, Tr("Show answer"), false);
    MakeButton(w, lcBack, Tr("Back to library"), false);
    MakeButton(w, lcAgain, Tr("Again"), false);
    MakeButton(w, lcHard, Tr("Hard"), false);
    MakeButton(w, lcGood, Tr("Good"), false);
    MakeButton(w, lcEasy, Tr("Easy"), false);
}
static LearningWindow* OpenLearningWindow(MainWindow* owner, bool dictionary, bool activate = true) {
    for (LearningWindow* w : gLearningWindows) {
        if (w->owner == owner && w->dictionary == dictionary) {
            bool active = GetForegroundWindow() == w->hwnd;
            ShowWindow(w->hwnd, SW_RESTORE);
            SetForegroundWindow(w->hwnd);
            if (active) {
                RefreshDecks(w);
                if (!w->dictionary && !w->practice) RefreshLibrary(w);
            }
            return w;
        }
    }
    VocabularyLoad();
    LoadGuideProgress();
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpfnWndProc = LearningWndProc;
    klass.lpszClassName = kLearningClass;
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&klass);
    klass.lpfnWndProc = ChoiceWndProc;
    klass.lpszClassName = kChoiceListClass;
    RegisterClassExW(&klass);
    auto* w = new LearningWindow();
    w->owner = owner;
    w->dictionary = dictionary;
    w->guideStep = gGuideProgress[dictionary ? 0 : 1];
    w->serial = ++gLearningSerial;
    LoadLearningPrefs(w);
    w->background = CreateSolidBrush(ThemeMainWindowBackgroundColor());
    w->fieldBackground = CreateSolidBrush(ThemeControlBackgroundColor());
    DpiSetFromHwnd(owner ? owner->hwndFrame : nullptr);
    RECT parent{};
    if (owner) GetWindowRect(owner->hwndFrame, &parent);
    VecAppend(gLearningWindows, w);
    HWND hwnd = CreateWindowExW(
        WS_EX_CONTROLPARENT, kLearningClass, dictionary ? L"Dictionary" : L"Vocabulary learning",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL, parent.left + DpiScale(36), parent.top + DpiScale(36),
        DpiScale(860), DpiScale(720), owner ? owner->hwndFrame : nullptr, nullptr, GetModuleHandleW(nullptr), w);
    if (!hwnd) {
        if (VecRemove(gLearningWindows, w) >= 0) {
            delete w;
        }
        return nullptr;
    }
    DpiScope windowDpi(hwnd);
    InstallAppScrollbar(hwnd);
    RefreshLearningStyle(w);
    MakeControl(w, lcTitle, L"STATIC", dictionary ? Tr("Dictionary") : Tr("Learning hub"), SS_OWNERDRAW | SS_NOPREFIX);
    MakeButton(w, lcGuideStart, Tr("Help / Start guide"));
    MakeControl(w, lcFeedback, L"STATIC", {}, SS_OWNERDRAW | SS_NOPREFIX);
    ShowWindow(Control(w, lcFeedback), SW_HIDE);
    MakeControl(w, lcQueryLabel, L"STATIC", dictionary ? Tr("&Word") : Tr("&Find word"), SS_CENTERIMAGE);
    MakeControl(w, lcQuery, L"EDIT", {}, ES_AUTOHSCROLL | WS_BORDER);
    MakeButton(w, lcLookup, dictionary ? Tr("Look up") : Tr("Search"));
    MakeControl(w, lcDeckLabel, L"STATIC", Tr("&Deck"), SS_CENTERIMAGE);
    MakeControl(w, lcDeck, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
    MakeControl(w, lcDetails, L"EDIT", {}, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER);
    SendMessageW(Control(w, lcDetails), EM_SETLIMITTEXT, 1024 * 1024, 0);
    MakeControl(w, lcStatus, L"STATIC",
                dictionary ? Tr("Lookup is entirely offline. Downloads only start when requested.") : Str{},
                SS_NOPREFIX);
    MakeButton(w, lcLearned, Tr("Mark learned"));
    MakeButton(w, lcPronounce, Tr("Pronounce"));
    MakeButton(w, lcStopVoice, Tr("Stop"));
    MakeControl(w, lcVoiceLabel, L"STATIC", Tr("&Voice"), SS_CENTERIMAGE);
    MakeControl(w, lcVoice, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
    AddChoice(w, lcVoice, Tr("System default voice"));
    SendMessageW(Control(w, lcVoice), CB_SETCURSEL, 0, 0);
    EnableWindow(Control(w, lcStopVoice), false);
    if (activate) w->speech = new DictionarySpeech(hwnd, kSpeechMessage);
    if (dictionary) {
        MakeControl(w, lcSourceLabel, L"STATIC", Tr("&Source"), SS_CENTERIMAGE);
        MakeControl(w, lcLookupSource, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
        for (Str source :
             {Tr("Offline · installed dictionaries"), Tr("Online · chosen source order"),
              Tr("Online · Wiktionary (Kaikki)"), Tr("Online · Wiktionary REST"), Tr("Online · Free Dictionary API")})
            AddChoice(w, lcLookupSource, source);
        SendMessageW(Control(w, lcLookupSource), CB_SETCURSEL, 0, 0);
        MakeControl(w, lcSenseLabel, L"STATIC", Tr("&Meaning"), SS_CENTERIMAGE);
        MakeControl(w, lcSense, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
        AddChoice(w, lcSense, Tr("Meaning to save"));
        SendMessageW(Control(w, lcSense), CB_SETCURSEL, 0, 0);
        EnableWindow(Control(w, lcSense), false);
        MakeButton(w, lcRecording, Tr("US recording"));
        MakeButton(w, lcRecordingUk, Tr("UK recording"));
        EnableWindow(Control(w, lcRecording), false);
        EnableWindow(Control(w, lcRecordingUk), false);
        MakeButton(w, lcSave, Tr("Save word"));
        MakeButton(w, lcOpenVocabulary, Tr("Open learning hub"));
        EnableWindow(Control(w, lcSave), false);
        EnableWindow(Control(w, lcLearned), false);
    } else {
        MakeControl(w, lcActivityLabel, L"STATIC", Tr("&Activity"), SS_CENTERIMAGE);
        MakeControl(w, lcActivity, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
        MakeControl(w, lcSchedulerLabel, L"STATIC", Tr("&Review method"), SS_CENTERIMAGE);
        MakeControl(w, lcScheduler, L"COMBOBOX", {}, CBS_DROPDOWNLIST);
        MakeButton(w, lcReviewHelp, Tr("Review help"));
        MakeButton(w, lcUndo, Tr("Undo removal"));
        MakeControl(w, lcLearnedHint, L"STATIC",
                    Tr("Learned words stay in your library and leave practice. Mark unlearned to review them again."),
                    SS_NOPREFIX);
        MakeButton(w, lcPractice, Tr("Practice"));
        MakeControl(w, lcLibrary, kChoiceListClass, {}, WS_VSCROLL | WS_BORDER);
        MakeButton(w, lcDeleteWord, Tr("Remove word"));
        Str modes[] = {Tr("Flashcards"), Tr("Meaning quiz"),  Tr("Word quiz"),
                       Tr("Spelling"),   Tr("Word scramble"), Tr("Matching pairs")};
        for (Str mode : modes) {
            AddChoice(w, lcActivity, mode);
        }
        AddChoice(w, lcScheduler, StrL("SM-2"));
        AddChoice(w, lcScheduler, Tr("Leitner"));
        SendMessageW(Control(w, lcActivity), CB_SETCURSEL, 0, 0);
        SendMessageW(Control(w, lcScheduler), CB_SETCURSEL, 0, 0);
    }
    MakeButton(w, lcManageToggle, dictionary ? Tr("Dictionaries…") : Tr("Deck tools…"));
    RefreshDecks(w);
    UpdateLearningChrome(w);
    UpdateGuide(w);
    for (int id : {lcLibrary}) {
        if (Control(w, id)) {
            SendMessageW(Control(w, id), LB_SETHORIZONTALEXTENT, DpiScale(1600), 0);
        }
    }
    if (!dictionary) {
        PracticeControls(w);
        RefreshLibrary(w);
    }
    SendMessageW(Control(w, lcQuery), EM_SETCUEBANNER, true,
                 (LPARAM)(dictionary ? L"Type a word…" : L"Search your vocabulary…"));
    FormatDetails(w);
    // The hidden window stays unlaid out until its controls, data and visibility are final.
    w->ready = true;
    LayoutLearning(w);
    if (activate) {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(Control(w, lcQuery));
        SetCurrentModelessDialog(hwnd);
    }
    w->opening = false;
    return w;
}
void ShowDictionaryDialog(MainWindow* owner, Str word, Str context, Str source, int page) {
    LearningWindow* w = OpenLearningWindow(owner, true);
    if (!w) {
        return;
    }
    if (w->busy && len(word)) {
        if (!w->lookupBusy) {
            Status(w, Tr("Please wait for the pack update before looking up another word."));
            return;
        }
        w->ticket++;
        w->busy = false;
    }
    str::ReplaceWithCopy(&w->context, context);
    str::ReplaceWithCopy(&w->source, source);
    w->page = page;
    Text(w, lcQuery, word);
    if (len(word)) {
        LookupWord(w);
    }
}
void ShowVocabularyDialog(MainWindow* owner) {
    OpenLearningWindow(owner, false);
}
void RefreshVocabularyDialogs() {
    for (LearningWindow* w : gLearningWindows) {
        DpiScope dpi(w->hwnd);
        RefreshLearningStyle(w);
    }
}
void CloseVocabularyDialogs(MainWindow* owner) {
    for (int i = len(gLearningWindows) - 1; i >= 0; i--) {
        if (gLearningWindows[i]->owner == owner) {
            DestroyWindow(gLearningWindows[i]->hwnd);
        }
    }
}

#if IS_DEBUG
static LRESULT CALLBACK CountChoiceMeasure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    if (msg == LB_SETITEMHEIGHT || msg == EM_SETRECT || msg == EM_SETRECTNP) {
        (*(int*)data)++;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
static void CaptureLearningWindow(LearningWindow* w, Str path) {
    DpiScope dpi(w->hwnd);
    utassert(!IsWindowVisible(w->hwnd));
    RECT rc;
    GetClientRect(w->hwnd, &rc);
    int width = rc.right, height = rc.bottom;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    utassert(bitmap != nullptr && pixels != nullptr);
    if (!bitmap || !pixels) {
        DeleteDC(dc);
        return;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    SendMessageW(w->hwnd, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    for (HWND child : w->controls) {
        if (!child || !(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
        RECT bounds;
        GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, w->hwnd, (POINT*)&bounds, 2);
        int saved = SaveDC(dc);
        SetViewportOrgEx(dc, bounds.left, bounds.top, nullptr);
        IntersectClipRect(dc, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
        SendMessageW(child, WM_PRINT, (WPARAM)dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
        RestoreDC(dc, saved);
    }
    GdiFlush();
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + width * height * 4;
    str::Builder bytes;
    bytes.Append(Str((const char*)&header, sizeof(header)));
    bytes.Append(Str((const char*)&info.bmiHeader, sizeof(info.bmiHeader)));
    bytes.Append(Str((const char*)pixels, width * height * 4));
    utassert(file::WriteFile(path, ToStrTemp(bytes)));
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}
static void LearningSurfaceCaptures() {
    WCHAR folder[1024]{};
    DWORD length = GetEnvironmentVariableW(L"SUMATRA_LEARNING_SNAPSHOTS", folder, dimof(folder));
    if (!length || length >= dimof(folder)) return;
    RenderCache* originalCache = gRenderCache;
    if (!originalCache) gRenderCache = new RenderCache();
    defer {
        if (!originalCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    Str output = ToUtf8Temp(folder);
    utassert(dir::CreateAll(output));
    Str originalData = str::Dup(GetAppDataDirTemp());
    SetAppDataDir(path::JoinTemp(output, StrL("isolated-data")));
    defer {
        SetAppDataDir(originalData);
        str::Free(originalData);
    };
    int originalFont = gSettings->uIFontSize, originalScale = gSettings->interfaceScale;
    Str originalTheme = str::Dup(gSettings->theme);
    defer {
        gSettings->uIFontSize = originalFont;
        gSettings->interfaceScale = originalScale;
        str::ReplaceWithCopy(&gSettings->theme, originalTheme);
        str::Free(originalTheme);
        SetCurrentThemeFromSettings();
        RefreshUiFonts();
    };
    for (int variant = 0; variant < 5; variant++) {
        gSettings->uIFontSize = variant == 4 ? 28 : 18;
        gSettings->interfaceScale = variant == 4 ? 150 : 100;
        str::ReplaceWithCopy(&gSettings->theme, variant == 3 ? StrL("Sumatra Light") : StrL("Modern Green Dark"));
        SetCurrentThemeFromSettings();
        RefreshUiFonts();
        auto* w = OpenLearningWindow(nullptr, variant == 0 || variant == 3 || variant == 4, false);
        utassert(w != nullptr);
        if (!w) continue;
        DpiScope dpi(w->hwnd);
        MoveWindow(w->hwnd, 0, 0, variant == 4 ? 960 : 1280, variant == 4 ? 1000 : 880, false);
        Text(w, lcQuery, StrL("omnipotent"));
        if (w->dictionary) {
            OfflineMeaning first{}, second{};
            first.headword = str::Dup(StrL("omnipotent"));
            first.dictionary = str::Dup(StrL("English dictionary"));
            first.dictionaryId = str::Dup(StrL("example"));
            first.partOfSpeech = str::Dup(StrL("adjective"));
            first.definition = str::Dup(StrL("Having unlimited power, force or authority."));
            first.example = str::Dup(StrL("An omnipotent ruler."));
            second.headword = str::Dup(first.headword);
            second.dictionary = str::Dup(first.dictionary);
            second.dictionaryId = str::Dup(first.dictionaryId);
            second.partOfSpeech = str::Dup(first.partOfSpeech);
            second.definition = str::Dup(StrL("Capable of developing into any type of cell or tissue."));
            VecAppend(w->meanings, first);
            VecAppend(w->meanings, second);
            ShowMeanings(w);
            EnableWindow(Control(w, lcSave), true);
            EnableWindow(Control(w, lcLearned), true);
        } else if (variant == 2) {
            EnsurePracticeControls(w);
            SendMessageW(Control(w, lcActivity), CB_SETCURSEL, (int)VocabActivity::MeaningChoice, 0);
            w->practice = true;
            Text(w, lcDetails, StrL("omnipotent"));
            for (Str answer : {StrL("Having unlimited power, force or authority."), StrL("Lacking energy or strength."),
                               StrL("Able to see distant objects clearly."), StrL("A person who studies languages.")})
                SendMessageW(Control(w, lcChoices), LB_ADDSTRING, 0, (LPARAM)CWStrTemp(answer));
            Status(w, StrL("Review 3 of 14 · 1 correct"));
            PracticeControls(w);
        } else {
            for (Str word : {StrL("omnipotent"), StrL("abeyance"), StrL("lucid")})
                SendMessageW(Control(w, lcLibrary), LB_ADDSTRING, 0, (LPARAM)CWStrTemp(word));
            Text(w, lcDetails, StrL("omnipotent\r\n\r\nadjective\r\n1. Having unlimited power, force or authority."));
            Status(w, StrL("3 saved words · 2 due for review"));
        }
        LayoutLearning(w);
        // Every placed control remains within the viewport width, including large fonts and scaling.
        RECT client;
        GetClientRect(w->hwnd, &client);
        for (const auto& item : w->placements) {
            utassert(item.bounds.left >= 0 && item.bounds.right <= client.right);
        }
        CaptureLearningWindow(w, path::JoinTemp(output, fmt("learning-%d.bmp", variant)));
        DestroyWindow(w->hwnd);
    }
}
static void GreenCheckGlyphTests() {
    constexpr int size = 32;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size * 2, -size, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    utassert(bitmap && pixels);
    if (!bitmap || !pixels) {
        DeleteDC(dc);
        return;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    for (Color background : {RGB(255, 255, 255), RGB(24, 24, 24)}) {
        RECT full{0, 0, size * 2, size};
        HBRUSH brush = CreateSolidBrush(background);
        FillRect(dc, &full, brush);
        DeleteObject(brush);
        RECT check{0, 0, size, size};
        DrawGreenCheck(dc, check, background);
        HFONT font = GetUserGuiFont(StrL("Segoe UI Symbol"), size)->GetHFont();
        HGDIOBJ previous = SelectObject(dc, font);
        WORD glyph = 0xffff;
        GetGlyphIndicesW(dc, L"\u2713", 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS);
        utassert(glyph != 0xffff);
        SetTextColor(dc, background == RGB(255, 255, 255) ? RGB(17, 120, 58) : RGB(93, 230, 145));
        SetBkMode(dc, TRANSPARENT);
        RECT reference{size, 0, size * 2, size};
        DrawTextW(dc, L"\u2713", 1, &reference, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        GdiFlush();
        bool same = true;
        int greenPixels = 0;
        auto* colors = (DWORD*)pixels;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                DWORD pixel = colors[y * size * 2 + x] & 0xffffff;
                same &= pixel == (colors[y * size * 2 + x + size] & 0xffffff);
                int green = (pixel >> 8) & 255;
                if (green > (int)((pixel >> 16) & 255) && green > (int)(pixel & 255)) greenPixels++;
            }
        }
        utassert(same && greenPixels > 0);
        SelectObject(dc, previous);
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

static void LearningPanelPaintTest(LearningWindow* w) {
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 256, -64, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    utassert(bitmap && pixels);
    if (bitmap && pixels) {
        HGDIOBJ old = SelectObject(dc, bitmap);
        PatBlt(dc, 0, 0, 256, 64, WHITENESS);
        DRAWITEMSTRUCT item{};
        item.CtlID = lcTitle;
        item.hwndItem = Control(w, lcTitle);
        item.hDC = dc;
        item.rcItem = {0, 0, 256, 64};
        DrawLearningPanel(w, &item);
        utassert(GetPixel(dc, 255, 0) == (ThemeMainWindowBackgroundColor() & 0xffffff));
        SelectObject(dc, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
}

static void LearningFieldPaintTest(LearningWindow* w) {
    HWND field = Control(w, lcQuery);
    RoundControlCorners(field);
    Size size = HwndWindowRect(field).Size();
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size.dx, -size.dy, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    utassert(bitmap && pixels);
    if (bitmap && pixels) {
        HGDIOBJ old = SelectObject(dc, bitmap);
        PatBlt(dc, 0, 0, size.dx, size.dy, WHITENESS);
        SendMessageW(field, WM_PRINT, (WPARAM)dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
        utassert(GetPixel(dc, size.dx / 2, 0) == (ThemeControlBackgroundColor() & 0xffffff));
        SelectObject(dc, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
}

static void LibraryRowsTest(HWND parent) {
    HWND control = CreateWindowExW(0, kChoiceListClass, L"", WS_CHILD | WS_VSCROLL, 0, 0, 1, 1, parent,
                                   (HMENU)(INT_PTR)lcLibrary, GetModuleHandleW(nullptr), nullptr);
    utassert(control != nullptr);
    if (!control) return;
    defer {
        DestroyWindow(control);
    };
    LearningWindow window;
    window.hwnd = parent;
    window.controls[lcLibrary] = control;
    VocabularyWord first, second;
    first.id = str::Dup(StrL("first"));
    first.word = str::Dup(StrL("A long saved word that wraps across several lines"));
    second.id = str::Dup(StrL("second"));
    second.word = str::Dup(StrL("Second word"));
    Vec<VocabularyWord*> words;
    VecAppend(words, &first);
    VecAppend(words, &second);
    utassert(SetLibraryRows(&window, words, {}));
    auto* list = (ChoiceList*)GetWindowLongPtrW(control, GWLP_USERDATA);
    utassert(list->measurePasses == 0 && list->wrapDirty);
    MoveWindow(control, 0, 0, 150, 120, false);
    utassert(list->measurePasses == 1);
    int firstHeight = list->heights[0], secondHeight = list->heights[1];
    int measured = list->measurePasses;
    ChooseRow(list, 1, false);
    int scroll = list->scroll;
    ValidateRect(control, nullptr);
    utassert(!SetLibraryRows(&window, words, second.id));
    utassert(list->measurePasses == measured);
    utassert(list->selected == 1 && list->scroll == scroll);
    utassert(!GetUpdateRect(control, nullptr, FALSE));

    VecRemoveAt(words, 0);
    utassert(SetLibraryRows(&window, words, second.id));
    utassert(list->measurePasses == measured && list->heights[0] == secondHeight);
    utassert(list->selected == 0 && str::Eq(window.wordIds[0], second.id));
    VecAppend(words, &first);
    utassert(SetLibraryRows(&window, words, second.id));
    utassert(list->heights[0] == secondHeight && list->heights[1] == firstHeight);
    utassert(list->measurePasses == measured);
    utassert(list->selected == 0 && str::Eq(window.wordIds[1], first.id));

    SendMessageW(control, LB_SETITEMHEIGHT, 1, firstHeight + 37);
    second.learned = true;
    utassert(SetLibraryRows(&window, words, first.id));
    utassert(str::Eq(list->strings[0], StrL("Second word  ✓")));
    utassert(list->heights[1] == firstHeight + 37 && list->selected == 1);
    MoveWindow(control, 0, 0, 450, 120, false);
    utassert(list->heights[1] < firstHeight);
    int changed = list->measurePasses;
    VecReset(words);
    utassert(SetLibraryRows(&window, words, first.id));
    utassert(len(window.wordIds) == 0 && len(list->strings) == 0 && len(list->heights) == 0);
    utassert(list->selected == -1 && list->scroll == 0);
    VecAppend(words, &second);
    VecAppend(words, &first);
    utassert(SetLibraryRows(&window, words, first.id));
    utassert(list->measurePasses == changed);
    utassert(list->selected == 1 && str::Eq(window.wordIds[1], first.id));
    str::ReplaceWithCopy(&second.word, StrL("Edited word caption"));
    utassert(SetLibraryRows(&window, words, first.id));
    utassert(list->measurePasses == changed + 1);
    changed = list->measurePasses;
    HFONT font = GetUserGuiFont(StrL("Consolas"), 30)->GetHFont();
    SendMessageW(control, WM_SETFONT, (WPARAM)font, false);
    utassert(list->measurePasses > changed);
    changed = list->measurePasses;
    VecReset(words);
    utassert(SetLibraryRows(&window, words, {}));
    VecAppend(words, &second);
    VecAppend(words, &first);
    utassert(SetLibraryRows(&window, words, first.id));
    utassert(list->measurePasses == changed);
}
static void LearningRowTests(LearningWindow* w) {
    int size = gSettings->uIFontSize, scale = gSettings->interfaceScale;
    Str theme = str::Dup(gSettings->theme), family = str::Dup(gSettings->uIFontFamily);
    defer {
        gSettings->uIFontSize = size;
        gSettings->interfaceScale = scale;
        str::ReplaceWithCopy(&gSettings->theme, theme);
        str::ReplaceWithCopy(&gSettings->uIFontFamily, family);
        str::Free(theme);
        str::Free(family);
        SetCurrentThemeFromSettings();
        RefreshUiFonts();
        RefreshLearningStyle(w);
    };
    for (int i = 0; i < ThemeGetCount(); i++) {
        str::ReplaceWithCopy(&gSettings->theme, ThemeGetNameAt(i));
        SetCurrentThemeFromSettings();
        for (int percent : {100, 200}) {
            gSettings->interfaceScale = percent;
            gSettings->uIFontSize = percent == 100 ? 18 : 24;
            str::ReplaceWithCopy(&gSettings->uIFontFamily, i % 2 ? StrL("Consolas") : StrL("Manrope"));
            RefreshUiFonts();
            RefreshLearningStyle(w);
            LearningPanelPaintTest(w);
            LearningFieldPaintTest(w);
            HWND combo = Control(w, w->dictionary ? lcVoice : lcDeck);
            HWND button = Control(w, w->dictionary ? lcPronounce : lcPractice);
            utassert(HwndWindowRect(combo).dy == HwndWindowRect(button).dy);
            utassert(HwndWindowRect(combo).dy >= PlatformFontLineHeight(GetAppFontForDpi(DpiGet())));
            for (int id : {lcPronounce, lcStopVoice}) {
                HWND audio = Control(w, id);
                utassert(audio && HwndWindowRect(audio).dx == HwndWindowRect(audio).dy);
                utassert(len(HwndGetTextTemp(audio)) > 0);
                utassert((GetWindowLongPtrW(audio, GWL_STYLE) & WS_TABSTOP) != 0);
            }
            for (HWND child : w->controls) {
                if (!child || child == Control(w, lcTitle)) continue;
                if (IsRichDetails(child)) {
                    CHARFORMAT2W format{};
                    format.cbSize = sizeof(format);
                    SendMessageW(child, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM)&format);
                    LOGFONTW expected{};
                    GetObjectW(GetAppFontForDpi(DpiGet())->GetHFont(), sizeof(expected), &expected);
                    utassert(_wcsicmp(format.szFaceName, expected.lfFaceName) == 0);
                    utassert(format.yHeight == MulDiv(abs(expected.lfHeight), 1440, DpiGet()));
                } else {
                    utassert((HFONT)SendMessageW(child, WM_GETFONT, 0, 0) == GetAppFontForDpi(DpiGet())->GetHFont());
                }
            }
        }
    }
}

struct LearningFontProbe {
    HWND hwnd;
    int messages = 0;
};
static Vec<LearningFontProbe>* learningFontProbes;
static LRESULT CALLBACK LearningFontProbeProc(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && learningFontProbes) {
        auto* message = (CWPRETSTRUCT*)lp;
        if (message->message == WM_SETFONT) {
            auto& probes = *learningFontProbes;
            int index = 0;
            while (index < len(probes) && probes[index].hwnd != message->hwnd) index++;
            if (index == len(probes)) VecAppend(probes, {message->hwnd});
            probes[index].messages++;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
static int HiddenLearningControls(LearningWindow* w) {
    int count = 0;
    for (int id : {lcGuideText, lcGuidePrev, lcGuideNext, lcGuideSkip, lcGuideAction})
        count += Control(w, id) != nullptr;
    if (w->dictionary) {
        for (int id : {lcPack, lcImportPack, lcDownload, lcRemovePack, lcPackInfo, lcCancelDownload, lcSourcesToggle,
                       lcOnlineFirst, lcOnlineSecond, lcOnlineThird})
            count += Control(w, id) != nullptr;
    } else {
        for (int id : {lcInstallDeck, lcDeleteDeck, lcNewDeck, lcCreateDeck, lcExport, lcImport})
            count += Control(w, id) != nullptr;
    }
    return count;
}
static void LearningHiddenTests(LearningWindow* w) {
    SendMessageW(w->hwnd, WM_GETOBJECT, 0, OBJID_CLIENT);
    SendMessageW(w->hwnd, WM_GETOBJECT, 0, -25);
    ScrollLearning(w, 120);
    utassert(HiddenLearningControls(w) == 0);
    if (w->dictionary) utassert(len(w->packs) == 0);

    w->guideStep = 1;
    SendMessageW(Control(w, lcGuideStart), BM_CLICK, 0, 0);
    utassert(w->guideVisible);
    for (int id : {lcGuideText, lcGuidePrev, lcGuideNext, lcGuideSkip, lcGuideAction}) utassert(Control(w, id));
    HWND guide = Control(w, lcGuideText);
    utassert(str::StartsWith(Read(w, lcGuideText), StrL("Step 2 of 9:")));
    utassert(GetNextDlgTabItem(w->hwnd, Control(w, lcGuideStart), false) == Control(w, lcGuidePrev));
    utassert(GetNextDlgTabItem(w->hwnd, Control(w, lcGuideAction), false) == Control(w, lcQuery));
    SendMessageW(Control(w, lcGuideStart), BM_CLICK, 0, 0);
    SendMessageW(Control(w, lcGuideStart), BM_CLICK, 0, 0);
    utassert(Control(w, lcGuideText) == guide);
    SendMessageW(Control(w, lcGuideStart), BM_CLICK, 0, 0);

    // First expansion can happen while a lookup or deck job disables its actions.
    w->busy = true;
    SendMessageW(Control(w, lcManageToggle), BM_CLICK, 0, 0);
    utassert(w->managementVisible);
    if (w->dictionary) {
        for (int id : {lcPack, lcImportPack, lcDownload, lcRemovePack, lcPackInfo, lcCancelDownload, lcSourcesToggle})
            utassert(Control(w, id));
        for (int id : {lcPack, lcImportPack, lcDownload, lcRemovePack}) utassert(!IsWindowEnabled(Control(w, id)));
        utassert(len(w->packs) > 0 && Selected(w, lcPack) == 0);
        utassert(!Control(w, lcOnlineFirst) && !Control(w, lcOnlineSecond) && !Control(w, lcOnlineThird));
        w->onlineOrder[0] = 3;
        w->onlineOrder[1] = 0;
        w->onlineOrder[2] = 2;
        SendMessageW(Control(w, lcSourcesToggle), BM_CLICK, 0, 0);
        for (int slot = 0; slot < 3; slot++) {
            HWND control = Control(w, lcOnlineFirst + slot);
            utassert(control && Selected(w, lcOnlineFirst + slot) == w->onlineOrder[slot]);
            utassert((GetWindowLongPtrW(control, GWL_STYLE) & (WS_VISIBLE | WS_TABSTOP)) == (WS_VISIBLE | WS_TABSTOP));
            utassert((HFONT)SendMessageW(control, WM_GETFONT, 0, 0) == GetAppFontForDpi(DpiGet())->GetHFont());
        }
        utassert(GetNextDlgTabItem(w->hwnd, Control(w, lcLookupSource), false) == Control(w, lcSourcesToggle));
        utassert(GetNextDlgTabItem(w->hwnd, Control(w, lcSourcesToggle), false) == Control(w, lcOnlineFirst));
        utassert(GetNextDlgTabItem(w->hwnd, Control(w, lcOnlineFirst), false) == Control(w, lcOnlineSecond));
        utassert(GetNextDlgTabItem(w->hwnd, Control(w, lcOnlineSecond), false) == Control(w, lcOnlineThird));
        HWND source = Control(w, lcOnlineFirst), pack = Control(w, lcPack);
        SendMessageW(source, CB_SETCURSEL, 1, 0);
        SendMessageW(w->hwnd, WM_COMMAND, MAKEWPARAM(lcOnlineFirst, CBN_SELCHANGE), (LPARAM)source);
        utassert(w->onlineOrder[0] == 1);
        SendMessageW(Control(w, lcSourcesToggle), BM_CLICK, 0, 0);
        SendMessageW(Control(w, lcSourcesToggle), BM_CLICK, 0, 0);
        SendMessageW(pack, CB_SETCURSEL, 1, 0);
        SendMessageW(Control(w, lcManageToggle), BM_CLICK, 0, 0);
        SendMessageW(Control(w, lcManageToggle), BM_CLICK, 0, 0);
        utassert(Control(w, lcPack) == pack && Selected(w, lcPack) == 1);
        utassert(Control(w, lcOnlineFirst) == source && Selected(w, lcOnlineFirst) == 1);
    } else {
        for (int id : {lcInstallDeck, lcDeleteDeck, lcNewDeck, lcCreateDeck, lcExport, lcImport})
            utassert(Control(w, id));
        utassert(!IsWindowEnabled(Control(w, lcInstallDeck)));
        HWND name = Control(w, lcNewDeck);
        SendMessageW(name, WM_CHAR, 'A', 0);
        SendMessageW(name, WM_CHAR, 'B', 0);
        utassert(str::Eq(Read(w, lcNewDeck), StrL("AB")));
        utassert(GetNextDlgTabItem(w->hwnd, Control(w, lcVoice), false) == Control(w, lcDeleteDeck));
        utassert(GetNextDlgTabItem(w->hwnd, name, true) == Control(w, lcDeleteDeck));
        utassert(GetNextDlgTabItem(w->hwnd, Control(w, lcImport), false) == Control(w, lcActivity));
        SendMessageW(Control(w, lcManageToggle), BM_CLICK, 0, 0);
        SendMessageW(Control(w, lcManageToggle), BM_CLICK, 0, 0);
        utassert(Control(w, lcNewDeck) == name && str::Eq(Read(w, lcNewDeck), StrL("AB")));
    }
    w->busy = false;
    for (int id : {lcPack, lcImportPack, lcDownload, lcRemovePack, lcInstallDeck}) EnableWindow(Control(w, id), true);
    SendMessageW(Control(w, lcManageToggle), BM_CLICK, 0, 0);
}
static LearningWindow* OpenLearningForTest(bool dictionary) {
    Vec<LearningFontProbe> probes;
    learningFontProbes = &probes;
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROCRET, LearningFontProbeProc, nullptr, GetCurrentThreadId());
    utassert(hook);
    LearningWindow* w = OpenLearningWindow(nullptr, dictionary, false);
    if (hook) UnhookWindowsHookEx(hook);
    learningFontProbes = nullptr;
    if (!w) return nullptr;
    for (HWND child : w->controls) {
        if (!child || child == Control(w, lcTitle)) continue;
        int messages = 0;
        for (auto& probe : probes) {
            if (probe.hwnd == child) messages = probe.messages;
        }
        utassert(messages == 1);
    }
    return w;
}

void VocabularyDialog_UnitTests() {
    RenderCache* originalCache = gRenderCache;
    if (!originalCache) gRenderCache = new RenderCache();
    defer {
        if (!originalCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    Settings* savedSettings = gSettings;
    Settings* fixture = NewSettings({});
    utassert(fixture != nullptr);
    if (!fixture) {
        return;
    }
    GreenCheckGlyphTests();
    gSettings = fixture;
    if (!ThemeGetCount()) CreateThemeCommands();
    SetCurrentThemeFromSettings();
    bool savedNoSettings = gDontSaveSettings;
    gDontSaveSettings = true;
    defer {
        gSettings = savedSettings;
        if (savedSettings) SetCurrentThemeFromSettings();
        gDontSaveSettings = savedNoSettings;
        DeleteSettings(fixture);
    };
    {
        Str originalData = str::Dup(GetAppDataDirTemp());
        Str testData = str::Dup(GetTempFilePathTemp(StrL("cap")));
        file::Delete(testData);
        SetAppDataDir(testData);
        defer {
            SetAppDataDir(originalData);
            RemoveDirectoryW(CWStrTemp(testData));
            str::Free(originalData);
            str::Free(testData);
        };
        int originalScale = gSettings->interfaceScale;
        auto* learning = OpenLearningForTest(true);
        utassert(learning != nullptr);
        if (learning) {
            utassert(HwndWindowRect(Control(learning, lcVoice)).dy ==
                     HwndWindowRect(Control(learning, lcPronounce)).dy);
            LearningHiddenTests(learning);
            LearningRowTests(learning);
            HWND audio = Control(learning, lcPronounce);
            ShowLearningButtonTooltip(learning, audio, true);
            utassert(learning->buttonTooltip && learning->tooltipButton == audio);
            if (learning->buttonTooltip) {
                utassert(TooltipGetCount(learning->buttonTooltip->hwnd) == 1);
                utassert(str::Eq(learning->buttonTooltip->lastText, HwndGetTextTemp(audio)));
                SendMessageW(audio, WM_KILLFOCUS, 0, 0);
                utassert(TooltipGetCount(learning->buttonTooltip->hwnd) == 0);
                utassert(!learning->tooltipButton);
            }
            WindowApplyScaledCaption(learning->hwnd);
            int before = HwndWindowRect(learning->hwnd).dy - HwndClientRect(learning->hwnd).dy;
            gSettings->interfaceScale = originalScale + 50;
            RefreshUiFonts();
            RefreshLearningStyle(learning);
            int after = HwndWindowRect(learning->hwnd).dy - HwndClientRect(learning->hwnd).dy;
            utassert(after > before);
            DestroyWindow(learning->hwnd);
        }
        gSettings->interfaceScale = originalScale;
        RefreshUiFonts();
        auto* hub = OpenLearningForTest(false);
        utassert(hub != nullptr);
        if (hub) {
            utassert(Control(hub, lcLibrary));
            utassert(HwndWindowRect(Control(hub, lcDeck)).dy == HwndWindowRect(Control(hub, lcPractice)).dy);
            LearningHiddenTests(hub);
            LearningRowTests(hub);
            for (int id :
                 {lcAnswer, lcChoices, lcPairs, lcSplit, lcCheck, lcReveal, lcBack, lcAgain, lcHard, lcGood, lcEasy})
                utassert(!Control(hub, id));
            hub->practice = true;
            PracticeControls(hub);
            for (int id :
                 {lcAnswer, lcChoices, lcPairs, lcSplit, lcCheck, lcReveal, lcBack, lcAgain, lcHard, lcGood, lcEasy})
                utassert(Control(hub, id));
            HWND answer = Control(hub, lcAnswer);
            Text(hub, lcAnswer, StrL("Retained answer"));
            hub->practice = false;
            PracticeControls(hub);
            hub->practice = true;
            PracticeControls(hub);
            utassert(Control(hub, lcAnswer) == answer);
            utassert(str::Eq(Read(hub, lcAnswer), StrL("Retained answer")));
            DestroyWindow(hub->hwnd);
        }
    }
    utassert(str::Eq(DictionarySize(3335559018LL), StrL("3.34 GB")));
    utassert(str::Eq(DictionarySize(4719269), StrL("4.72 MB")));
    const KaikkiPack* simplePack = FindKaikkiPack(StrL("kaikki-simple-english"));
    utassert(simplePack != nullptr);
    if (simplePack) {
        Str confirmation = KaikkiDownloadConfirmation(*simplePack, 4719269);
        utassert(str::Contains(confirmation, StrL("4719269 bytes")));
        utassert(str::Contains(confirmation, StrL("compressed")));
        utassert(str::Contains(confirmation, simplePack->sourceUrl));
        utassert(str::Contains(confirmation, StrL("37.90 MB")));
    }
    utassert(dimof(kGuide) == kGuideSteps);
    utassert(kGuide[0].dictionary && kGuide[0].control == lcQuery);
    utassert(!kGuide[kGuideSteps - 1].dictionary && kGuide[kGuideSteps - 1].control == lcExport);
    HICON book = MakeLearningIcon(16, true, RGB(34, 197, 94), kColBlack);
    HICON vocabularyIcon = MakeLearningIcon(48, false, RGB(34, 197, 94), kColBlack);
    utassert(book != nullptr && vocabularyIcon != nullptr);
    DestroyIcon(book);
    DestroyIcon(vocabularyIcon);
    utassert(str::Eq(ChoiceLabel(0), StrL("(A)")));
    utassert(str::Eq(ChoiceLabel(4), StrL("(E)")));
    utassert(str::Eq(ChoiceLabel(25), StrL("(Z)")));
    utassert(str::Eq(ChoiceLabel(26), StrL("(AA)")));
    utassert(str::Eq(ChoiceLabel(701), StrL("(ZZ)")));
    utassert(str::Eq(ChoiceLabel(702), StrL("(AAA)")));
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpfnWndProc = ChoiceWndProc;
    klass.lpszClassName = kChoiceListClass;
    RegisterClassExW(&klass);
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VSCROLL, 0, 0, 500, 500, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    utassert(parent != nullptr);
    if (!parent) {
        return;
    }
    defer {
        DestroyWindow(parent);
    };
    LibraryRowsTest(parent);
    HWND control = CreateWindowExW(0, kChoiceListClass, L"", WS_CHILD | WS_VSCROLL, 0, 0, 150, 120, parent, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    utassert(control != nullptr);
    if (!control) {
        return;
    }
    str::Builder definition;
    for (int i = 0; i < 40; i++) {
        definition.Append(StrL("A long definition with several words that must remain readable. "));
    }
    Str raw = ToStrTemp(definition);
    SendMessageW(control, LB_ADDSTRING, 0, (LPARAM)CWStrTemp(raw));
    SendMessageW(control, LB_ADDSTRING, 0, (LPARAM)L"Second answer");
    WrapChoices(control);
    auto* list = (ChoiceList*)GetWindowLongPtrW(control, GWLP_USERDATA);
    int narrowHeight = list->heights[0];
    utassert(narrowHeight > 255);
    int measured = list->measurePasses;
    int repeatedMeasures = 0;
    SetWindowSubclass(control, CountChoiceMeasure, 1, (DWORD_PTR)&repeatedMeasures);
    for (int i = 0; i < 50; i++) {
        WrapChoices(control);
    }
    RemoveWindowSubclass(control, CountChoiceMeasure, 1);
    utassert(repeatedMeasures == 0);
    utassert(list->measurePasses == measured);
    MoveWindow(control, 0, 0, 150, 160, false);
    utassert(list->measurePasses == measured);
    utassert(ChoiceRowAt(list, -1) == -1);
    utassert(ChoiceRowAt(list, ChoiceTop(list, 1)) == 1);
    utassert(ChoiceRowAt(list, ChoiceTop(list, len(list->heights))) == -1);
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    ShowWindow(control, SW_SHOWNOACTIVATE);
    UpdateWindow(parent);
    UpdateWindow(control);
    utassert(IsWindowVisible(control));
    ValidateRect(control, nullptr);
    for (int i = 0; i < 50; i++) {
        SendMessageW(control, WM_VSCROLL, SB_TOP, 0);
        SendMessageW(control, WM_VSCROLL, SB_ENDSCROLL, 0);
        SendMessageW(control, WM_MOUSEWHEEL, 0, 0);
    }
    utassert(list->scroll == 0);
    utassert(!GetUpdateRect(control, nullptr, FALSE));
    SCROLLINFO beforeScroll{sizeof(beforeScroll), SIF_ALL}, afterScroll{sizeof(afterScroll), SIF_ALL};
    utassert(GetScrollInfo(control, SB_VERT, &beforeScroll));
    SendMessageW(control, WM_VSCROLL, SB_LINEDOWN, 0);
    utassert(GetScrollInfo(control, SB_VERT, &afterScroll));
    utassert(afterScroll.nMin == beforeScroll.nMin && afterScroll.nMax == beforeScroll.nMax);
    utassert(afterScroll.nPage == beforeScroll.nPage);
    utassert(afterScroll.nPos == list->scroll && list->scroll > 0);
    ScrollChoiceTo(list, ChoiceTop(list, 1) - 60);
    int secondTop = ChoiceTop(list, 1) - list->scroll;
    ValidateRect(control, nullptr);
    SendMessageW(control, WM_MOUSEMOVE, 0, MAKELPARAM(10, secondTop + 1));
    RECT hoverDirty{};
    utassert(list->hover == 1);
    utassert(GetUpdateRect(control, &hoverDirty, FALSE));
    utassert(hoverDirty.top == secondTop);
    utassert(hoverDirty.bottom <= secondTop + list->heights[1]);
    ValidateRect(control, nullptr);
    SendMessageW(control, WM_MOUSELEAVE, 0, 0);
    utassert(list->hover == -1);
    utassert(GetUpdateRect(control, &hoverDirty, FALSE));
    utassert(hoverDirty.top == secondTop);
    utassert(hoverDirty.bottom <= secondTop + list->heights[1]);
    SendMessageW(control, WM_VSCROLL, SB_TOP, 0);
    {
        HDC paint = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateMemoryBitmap({160, 160});
        utassert(paint != nullptr && bitmap != nullptr);
        if (paint && bitmap) {
            HGDIOBJ old = SelectObject(paint, bitmap);
            RECT full{0, 0, 160, 160};
            COLORREF sentinel = RGB(13, 101, 197);
            HBRUSH brush = CreateSolidBrush(sentinel);
            FillRect(paint, &full, brush);
            DeleteObject(brush);
            IntersectClipRect(paint, 32, 100, 92, 140);
            SendMessageW(control, WM_PRINTCLIENT, (WPARAM)paint, PRF_CLIENT);
            SelectClipRgn(paint, nullptr);
            utassert(GetPixel(paint, 50, 120) == (ThemeControlBackgroundColor() & 0xffffff));
            utassert(GetPixel(paint, 10, 10) == sentinel);
            utassert(GetPixel(paint, 100, 150) == sentinel);
            SelectObject(paint, old);
        }
        DeleteObject(bitmap);
        DeleteDC(paint);
    }
    ShowWindow(control, SW_HIDE);
    ShowWindow(parent, SW_HIDE);
    HFONT large = CreateFontW(36, 0, 0, 0, FW_NORMAL, false, false, false, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    utassert(large != nullptr);
    SendMessageW(control, WM_SETFONT, (WPARAM)large, false);
    utassert(list->measurePasses > measured);
    utassert(list->heights[0] > narrowHeight);
    SendMessageW(control, WM_SETFONT, (WPARAM)GetAppFont()->GetHFont(), false);
    DeleteObject(large);
    int remainder = 0, moved = 0;
    for (int i = 0; i < WHEEL_DELTA; i++) moved += WheelDistance(MAKEWPARAM(0, 1), 96, remainder);
    utassert(moved == 96 && remainder == 0);
    utassert(WheelDistance(MAKEWPARAM(0, (WORD)-WHEEL_DELTA), 96, remainder) == -96);

    utassert(str::Eq(ToUtf8Temp(LbGetTextTemp(control, 0)), raw));
    MoveWindow(control, 0, 0, 450, 120, false);
    WrapChoices(control);
    utassert(list->heights[0] < narrowHeight);
    SendMessageW(control, LB_SETCURSEL, 0, 0);
    SendMessageW(control, WM_KEYDOWN, VK_DOWN, 0);
    utassert(SendMessageW(control, LB_GETCURSEL, 0, 0) == 1);
    SendMessageW(control, WM_KEYDOWN, VK_HOME, 0);
    utassert(SendMessageW(control, LB_GETCURSEL, 0, 0) == 0);
    SendMessageW(control, WM_CHAR, 'B', 0);
    utassert(SendMessageW(control, LB_GETCURSEL, 0, 0) == 1);
    VocabularyQuestion question;
    question.answer = str::Dup(StrL("Second answer"));
    utassert(VocabularyCheckAnswer(question, ToUtf8Temp(LbGetTextTemp(control, list->selected))));
    SendMessageW(control, LB_DELETESTRING, 0, 0);
    utassert(str::Eq(ToUtf8Temp(LbGetTextTemp(control, 0)), StrL("Second answer")));
    utassert(SendMessageW(control, LB_GETCURSEL, 0, 0) == -1);
    SendMessageW(control, LB_RESETCONTENT, 0, 0);
    utassert(SendMessageW(control, LB_GETCOUNT, 0, 0) == 0);
    LearningWindow window;
    window.hwnd = parent;
    window.dictionary = true;
    MakeControl(&window, lcTitle, L"STATIC", StrL("Offline dictionary"));
    MakeControl(&window, lcQuery, L"EDIT", {}, ES_AUTOHSCROLL);
    MakeControl(&window, lcLookup, L"BUTTON", StrL("Look up"));
    MakeControl(&window, lcDetails, L"EDIT", {}, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
    MakeControl(&window, lcStatus, L"STATIC", StrL("Offline lookup"));
    str::Builder lines;
    for (int i = 0; i < 100; i++) lines.Append(StrL("A readable dictionary definition.\r\n"));
    Text(&window, lcDetails, ToStrTemp(lines));
    window.ready = true;
    LayoutLearning(&window);
    HWND queryHwnd = Control(&window, lcQuery);
    Rect queryBounds = ChildPosWithinParent(queryHwnd);
    Text(&window, lcQuery, StrL("computing"));
    SendMessageW(queryHwnd, EM_SETSEL, 1, 5);
    for (int height : {60, 90}) {
        SetWindowPos(queryHwnd, nullptr, 0, 0, 180, height, SWP_NOZORDER | SWP_NOACTIVATE);
        RECT bounds;
        GetWindowRect(queryHwnd, &bounds);
        POINT origin{};
        ClientToScreen(queryHwnd, &origin);
        int lineHeight = PlatformFontLineHeight(GetAppFontForDpi(DpiGetForHwnd(queryHwnd)));
        utassert(abs(origin.y - bounds.top - (height - lineHeight) / 2) <= 1);
        DWORD start = 0, end = 0;
        SendMessageW(queryHwnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        utassert(start == 1 && end == 5);
    }
    SetWindowPos(queryHwnd, nullptr, queryBounds.x, queryBounds.y, queryBounds.dx, queryBounds.dy,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    Text(&window, lcQuery, {});
    HWND details = Control(&window, lcDetails);
    Rect detailsRect = HwndWindowRect(details);
    int cornerDiameter =
        std::min(2 * GetAppCornerRadius(DpiGetForHwnd(details), 6), std::min(detailsRect.dx, detailsRect.dy));
    HRGN expectedCorners =
        CreateRoundRectRgn(0, 0, detailsRect.dx + 1, detailsRect.dy + 1, cornerDiameter, cornerDiameter);
    HRGN actualCorners = CreateRectRgn(0, 0, 0, 0);
    utassert(GetWindowRgn(details, actualCorners) != ERROR);
    utassert(EqualRgn(expectedCorners, actualCorners));
    DeleteObject(expectedCorners);
    DeleteObject(actualCorners);
    SendMessageW(Control(&window, lcQuery), EM_SETCUEBANNER, true, (LPARAM)L"Search your vocabulary…");
    int cueWidth = LearningInputWidth(&window, lcQuery, 1000, 32);
    utassert(cueWidth >= EditPreferredWidth(Control(&window, lcQuery), StrL("Search your vocabulary…"), 96, 1000));
    SendMessageW(Control(&window, lcQuery), EM_SETCUEBANNER, true, (LPARAM)L"");
    FitLearningInput(&window, lcQuery);
    RECT shortInput, longInput, action;
    GetWindowRect(Control(&window, lcQuery), &shortInput);
    Text(&window, lcQuery, StrL("pneumonoultramicroscopicsilicovolcanoconiosis"));
    FitLearningInput(&window, lcQuery);
    GetWindowRect(Control(&window, lcQuery), &longInput);
    GetWindowRect(Control(&window, lcLookup), &action);
    utassert(longInput.right - longInput.left > shortInput.right - shortInput.left);
    utassert(longInput.top == shortInput.top && action.left >= longInput.right);
    Text(&window, lcQuery, StrL("word"));
    FitLearningInput(&window, lcQuery);
    GetWindowRect(Control(&window, lcQuery), &longInput);
    utassert(longInput.right - longInput.left == shortInput.right - shortInput.left);

    SendMessageW(details, EM_LINESCROLL, 0, 20);
    int firstLine = (int)SendMessageW(details, EM_GETFIRSTVISIBLELINE, 0, 0);
    utassert(firstLine > 0);
    int contentHeight = window.contentHeight;
    int editFormats = 0;
    SetWindowSubclass(details, CountChoiceMeasure, 1, (DWORD_PTR)&editFormats);
    for (int i = 0; i < 50; i++) ScrollLearning(&window, i);
    LayoutLearning(&window);
    RemoveWindowSubclass(details, CountChoiceMeasure, 1);
    utassert(editFormats == 0);
    utassert((int)SendMessageW(details, EM_GETFIRSTVISIBLELINE, 0, 0) == firstLine);
    utassert(window.contentHeight == contentHeight);
    RECT before, after;
    GetClientRect(parent, &before);
    MoveWindow(parent, 0, 0, 500, 2000, false);
    LayoutLearning(&window, true);
    MoveWindow(parent, 0, 0, 500, 500, false);
    LayoutLearning(&window, true);
    GetClientRect(parent, &after);
    utassert(before.right == after.right);
    utassert(window.scrollY >= 0 && window.scrollY <= std::max(0, window.contentHeight - (int)after.bottom));

    MakeButton(&window, lcGuideStart, StrL("Help / Start guide"));
    MakeControl(&window, lcGuideText, L"STATIC", {}, SS_OWNERDRAW | SS_NOPREFIX);
    MakeButton(&window, lcGuidePrev, StrL("Back"));
    MakeButton(&window, lcGuideNext, StrL("Next"));
    MakeButton(&window, lcGuideSkip, StrL("Skip"));
    MakeButton(&window, lcGuideAction, StrL("Go to lookup"));
    LearningAction(&window, lcGuideStart, BN_CLICKED);
    utassert(window.guideVisible);
    LearningAction(&window, lcGuideStart, BN_KILLFOCUS);
    utassert(window.guideVisible);
    LearningAction(&window, lcGuideNext, BN_CLICKED);
    utassert(window.guideVisible && window.guideStep == 1);
    LearningAction(&window, lcGuideNext, BN_KILLFOCUS);
    utassert(window.guideStep == 1);
    window.guideVisible = false;

    MoveWindow(parent, 0, 0, 1400, 1000, false);
    Text(&window, lcQuery, StrL("word"));
    UpdateGuide(&window);
    RECT help, query, lookup;
    GetWindowRect(Control(&window, lcGuideStart), &help);
    GetWindowRect(Control(&window, lcQuery), &query);
    GetWindowRect(Control(&window, lcLookup), &lookup);
    utassert(help.top == query.top && query.top == lookup.top);
    utassert(query.right < lookup.left);
    MakeControl(&window, lcDeck, L"COMBOBOX", {}, CBS_DROPDOWNLIST | WS_VSCROLL);
    AddChoice(&window, lcDeck, StrL("All vocabulary"));
    SendMessageW(Control(&window, lcDeck), CB_SETCURSEL, 0, 0);
    MakeButton(&window, lcSave, StrL("Save word"));
    MakeButton(&window, lcLearned, StrL("Mark learned"));
    MakeButton(&window, lcOpenVocabulary, StrL("Open learning hub"));
    LayoutLearning(&window);
    RECT footer[4]{};
    int footerIds[] = {lcDeck, lcSave, lcLearned, lcOpenVocabulary};
    for (int i = 0; i < dimof(footerIds); i++) GetWindowRect(Control(&window, footerIds[i]), &footer[i]);
    for (int i = 1; i < dimof(footerIds); i++) {
        utassert(footer[i].top == footer[0].top);
        utassert(footer[i].left > footer[i - 1].right);
    }
    utassert(footer[0].right - footer[0].left < 1400 / 3);
    MoveWindow(parent, 0, 0, 500, 500, false);

    // A single native selector style and a practice view free of setup controls.
    HWND practiceParent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 960, 800, nullptr, nullptr,
                                          GetModuleHandleW(nullptr), nullptr);
    utassert(practiceParent != nullptr);
    if (practiceParent) {
        LearningWindow focused;
        focused.hwnd = practiceParent;
        MakeControl(&focused, lcTitle, L"STATIC", StrL("Learning hub"));
        MakeControl(&focused, lcDetails, L"EDIT", {}, ES_MULTILINE | ES_READONLY);
        MakeControl(&focused, lcStatus, L"STATIC", {});
        MakeControl(&focused, lcActivity, L"COMBOBOX", {}, CBS_DROPDOWNLIST);
        MakeControl(&focused, lcScheduler, L"COMBOBOX", {}, CBS_DROPDOWNLIST);
        MakeControl(&focused, lcVoice, L"COMBOBOX", {}, CBS_DROPDOWNLIST);
        AddChoice(&focused, lcActivity, StrL("Flashcards"));
        SendMessageW(Control(&focused, lcActivity), CB_SETCURSEL, 0, 0);
        for (int id : {lcQuery, lcLookup, lcDeck, lcInstallDeck, lcDeleteDeck, lcNewDeck, lcCreateDeck, lcExport,
                       lcImport, lcPractice, lcBack, lcReveal})
            MakeButton(&focused, id, StrL("Action"));
        focused.ready = focused.practice = true;
        utassert(LearningPrimaryAction(&focused, lcReveal));
        utassert(LearningPrimaryAction(&focused, lcCheck));
        utassert(!LearningPrimaryAction(&focused, lcPractice));
        PracticeControls(&focused);
        for (int id : {lcQuery, lcLookup, lcDeck, lcInstallDeck, lcDeleteDeck, lcNewDeck, lcCreateDeck, lcExport,
                       lcImport, lcPractice, lcActivity, lcScheduler})
            utassert((GetWindowLongPtrW(Control(&focused, id), GWL_STYLE) & WS_VISIBLE) == 0);
        utassert((GetWindowLongPtrW(Control(&focused, lcBack), GWL_STYLE) & WS_VISIBLE) != 0);
        for (int id : {lcActivity, lcScheduler, lcVoice})
            utassert((GetWindowLongPtrW(Control(&focused, id), GWL_STYLE) & CBS_OWNERDRAWFIXED) != 0);
        DestroyWindow(practiceParent);
    }

    HWND footerParent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1200, 800, nullptr, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
    utassert(footerParent != nullptr);
    if (footerParent) {
        LearningWindow footerWindow;
        footerWindow.hwnd = footerParent;
        MakeControl(&footerWindow, lcTitle, L"STATIC", StrL("Practice"));
        MakeControl(&footerWindow, lcDetails, L"EDIT", {}, ES_MULTILINE | ES_READONLY);
        MakeControl(&footerWindow, lcStatus, L"STATIC", StrL("Review 1 of 10"));
        MakeButton(&footerWindow, lcCheck, StrL("Next word"));
        MakeButton(&footerWindow, lcBack, StrL("Back to library"));
        MakeControl(&footerWindow, lcFeedback, L"STATIC", StrL("✓ Correct"), SS_OWNERDRAW | SS_NOPREFIX);
        footerWindow.ready = footerWindow.practice = true;
        footerWindow.feedbackKind = 1;
        LayoutLearning(&footerWindow);
        RECT next, back, feedback;
        GetWindowRect(Control(&footerWindow, lcCheck), &next);
        GetWindowRect(Control(&footerWindow, lcBack), &back);
        GetWindowRect(Control(&footerWindow, lcFeedback), &feedback);
        utassert(next.top == back.top && back.top == feedback.top);
        utassert(next.right <= back.left && back.right <= feedback.left);
        for (int width : {640, 360}) {
            MoveWindow(footerParent, 0, 0, width, 800, false);
            footerWindow.feedbackKind = -1;
            Text(&footerWindow, lcFeedback, StrL("✕ Not quite. Read the correct answer, then choose Next word."));
            LayoutLearning(&footerWindow);
            RECT viewport;
            GetClientRect(footerParent, &viewport);
            for (const auto& item : footerWindow.placements) {
                utassert(item.bounds.left >= 0 && item.bounds.right <= viewport.right);
            }
            GetWindowRect(Control(&footerWindow, lcCheck), &next);
            GetWindowRect(Control(&footerWindow, lcBack), &back);
            GetWindowRect(Control(&footerWindow, lcFeedback), &feedback);
            utassert(next.top == back.top ? next.right <= back.left : next.bottom <= back.top);
            utassert(back.top == feedback.top ? back.right <= feedback.left : back.bottom <= feedback.top);
        }
        DestroyWindow(footerParent);
    }

    OfflineMeaning first{}, second{};
    first.headword = str::Dup(StrL("omnipotent"));
    first.dictionary = str::Dup(StrL("Test dictionary"));
    first.dictionaryId = str::Dup(StrL("test"));
    first.partOfSpeech = str::Dup(StrL("adjective"));
    first.definition = str::Dup(StrL("Having unlimited power, force or authority."));
    first.example = str::Dup(StrL("An omnipotent ruler."));
    second.headword = str::Dup(first.headword);
    second.dictionary = str::Dup(first.dictionary);
    second.dictionaryId = str::Dup(first.dictionaryId);
    second.partOfSpeech = str::Dup(first.partOfSpeech);
    second.definition = str::Dup(StrL("Capable of developing into any type of cell."));
    VecAppend(window.meanings, first);
    VecAppend(window.meanings, second);
    ShowMeanings(&window);
    Str displayed = RichText(Read(&window, lcDetails));
    utassert(str::Contains(displayed, StrL("1. Having unlimited power, force or authority.\r")));
    utassert(str::Contains(displayed, StrL("2. Capable of developing into any type of cell.\r")));
    utassert(str::Contains(displayed, StrL("Example: An omnipotent ruler.\r")));
    utassert(IsRichDetails(details));
    for (const auto& run : window.detailRuns) {
        CHARRANGE range{run.start, run.start + run.length};
        SendMessageW(details, EM_EXSETSEL, 0, (LPARAM)&range);
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        SendMessageW(details, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
        if (run.style == DetailStyle::Heading || run.style == DetailStyle::Section)
            utassert((format.dwEffects & CFE_BOLD) != 0);
        if (run.style == DetailStyle::Example) utassert((format.dwEffects & CFE_ITALIC) != 0);
    }

    utassert((GetWindowLongPtrW(Control(&window, lcDeck), GWL_STYLE) & CBS_OWNERDRAWFIXED) != 0);
    MakeControl(&window, lcChoices, kChoiceListClass, {}, WS_VSCROLL);
    window.dictionary = false;
    window.practice = true;
    for (int id : {lcSave, lcLearned, lcOpenVocabulary}) Visible(&window, id, false);
    HWND splitter = MakeLearningSplit(&window);
    LayoutLearning(&window);
    RECT shortChoices, tallChoices;
    GetWindowRect(Control(&window, lcChoices), &shortChoices);
    MoveWindow(parent, 0, 0, 500, 1500, false);
    LayoutLearning(&window);
    GetWindowRect(Control(&window, lcChoices), &tallChoices);
    utassert(tallChoices.bottom - tallChoices.top > shortChoices.bottom - shortChoices.top);
    RECT splitRect;
    GetWindowRect(splitter, &splitRect);
    POINT splitCenter{(splitRect.left + splitRect.right) / 2, (splitRect.top + splitRect.bottom) / 2};
    utassert(SendMessageW(splitter, WM_NCHITTEST, 0, MAKELPARAM(splitCenter.x, splitCenter.y)) == HTCLIENT);
    int beforeDrag = window.promptShare;
    POINT local{10, (splitRect.bottom - splitRect.top) / 2};
    SendMessageW(splitter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(local.x, local.y));
    SendMessageW(splitter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(local.x, local.y));
    utassert(window.promptShare == beforeDrag);
    SendMessageW(splitter, WM_LBUTTONUP, 0, MAKELPARAM(local.x, local.y));
    SendMessageW(splitter, WM_KEYDOWN, VK_DOWN, 0);
    utassert(window.promptShare == beforeDrag + 5);
    SendMessageW(splitter, WM_KEYDOWN, VK_HOME, 0);
    utassert(window.promptShare == 30);
    SendMessageW(Control(&window, lcChoices), LB_ADDSTRING, 0, (LPARAM)L"Selected answer survives dragging");
    SendMessageW(Control(&window, lcChoices), LB_SETCURSEL, 0, 0);
    GetWindowRect(splitter, &splitRect);
    local = {10, (splitRect.bottom - splitRect.top) / 2};
    SendMessageW(splitter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(local.x, local.y));
    SendMessageW(splitter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(local.x, local.y + 100));
    utassert(window.promptShare > 30);
    POINT returnPoint{10, window.splitPointerY - window.scrollY};
    MapWindowPoints(parent, splitter, &returnPoint, 1);
    SendMessageW(splitter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(returnPoint.x, returnPoint.y));
    utassert(window.promptShare == 30);
    SendMessageW(splitter, WM_LBUTTONUP, 0, 0);
    utassert(!window.splitting);
    utassert(SendMessageW(Control(&window, lcChoices), LB_GETCURSEL, 0, 0) == 0);
    SendMessageW(splitter, WM_KEYDOWN, VK_HOME, 0);
    SendMessageW(Control(&window, lcChoices), LB_RESETCONTENT, 0, 0);
    window.promptShare = 60;
    LayoutLearning(&window);
    GetWindowRect(Control(&window, lcChoices), &shortChoices);
    utassert(shortChoices.bottom - shortChoices.top < tallChoices.bottom - tallChoices.top);
    SendMessageW(Control(&window, lcChoices), LB_ADDSTRING, 0, (LPARAM)L"First choice");
    SendMessageW(Control(&window, lcChoices), LB_SETCURSEL, 0, 0);
    window.promptShare = 30;
    LayoutLearning(&window);
    utassert(SendMessageW(Control(&window, lcChoices), LB_GETCURSEL, 0, 0) == 0);
    for (int id : {lcGuideStart, lcGuideText, lcGuidePrev, lcGuideNext, lcGuideSkip, lcGuideAction, lcDeck, lcChoices})
        DestroyWindow(Control(&window, id));
    for (int id : {lcSplit, lcSave, lcLearned, lcOpenVocabulary}) DestroyWindow(Control(&window, id));
    DestroyWindow(Control(&window, lcTitle));
    DestroyWindow(Control(&window, lcQuery));
    DestroyWindow(Control(&window, lcLookup));
    DestroyWindow(details);
    DestroyWindow(Control(&window, lcStatus));
    LearningSurfaceCaptures();
}
#endif
