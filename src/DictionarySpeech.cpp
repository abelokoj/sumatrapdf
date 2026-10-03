/* Copyright 2026 the SumatraPDF Enhanced contributors. License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"
#if !COMPILER_MINGW
#include <sapi.h>
#include <winhttp.h>
#include <mmsystem.h>
#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "winmm.lib")
#endif
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "DictionarySpeech.h"

constexpr int kPronunciationMaxWord = 512;
constexpr DWORD kPronunciationMaxAudio = 4 * 1024 * 1024;
constexpr ULONGLONG kPronunciationTimeout = 20000;

enum class SpeechCommand {
    None,
    Word,
    Recording,
    Stop
};

struct DictionarySpeechState {
    Mutex lock;
    HWND owner = nullptr;
    UINT notifyMessage = 0;
    HANDLE wake = nullptr;
    bool workerStarted = false;
    bool closing = false;
    int generation = 0;
    SpeechCommand command = SpeechCommand::None;
    Str word;
    Str url;
    int voiceIndex = 0;
    StrVec voiceNames;
    DictionarySpeechStatus status = DictionarySpeechStatus::Loading;
    Str message;

    ~DictionarySpeechState() {
        str::Free(word);
        str::Free(url);
        str::Free(message);
        if (wake) {
            CloseHandle(wake);
        }
    }
};

static void NotifySpeech(DictionarySpeechState* state) {
    if (state->owner && !state->closing) {
        PostMessageW(state->owner, state->notifyMessage, 0, 0);
    }
}

static bool CurrentSpeech(DictionarySpeechState* state, int generation) {
    AutoUnlockMutex guard(&state->lock);
    return !state->closing && state->generation == generation;
}

static void SpeechStatus(DictionarySpeechState* state, int generation, DictionarySpeechStatus status,
                         Str message = {}) {
    AutoUnlockMutex guard(&state->lock);
    if (state->closing || state->generation != generation) {
        return;
    }
    state->status = status;
    str::ReplaceWithCopy(&state->message, message);
    NotifySpeech(state);
}

static bool ValidSpeechWord(Str word) {
    if (len(word) == 0 || len(word) > kPronunciationMaxWord || memchr(word.s, 0, len(word))) {
        return false;
    }
    for (int i = 0; i < len(word); i++) {
        char c = word.s[i];
        if ((u8)c < 0x20 || (u8)c == 0x7f) {
            return false;
        }
    }
    return true;
}

static bool ValidAudioUrl(Str url) {
    if (len(url) < 9 || len(url) > 8192 || !str::StartsWithI(url, StrL("https://"))) {
        return false;
    }
    int hostEnd = 8;
    while (hostEnd < len(url) && url.s[hostEnd] != '/' && url.s[hostEnd] != '?') {
        hostEnd++;
    }
    if (hostEnd == 8) {
        return false;
    }
    for (int i = 0; i < len(url); i++) {
        u8 c = (u8)url.s[i];
        if (c <= 0x20 || c == 0x7f || c == '"' || c == '\\' || c == '#' || (i < hostEnd && c == '@')) {
            return false;
        }
    }
    return true;
}

static void QueueSpeech(DictionarySpeechState* state, SpeechCommand command, Str word = {}, Str url = {},
                        int voice = 0) {
    if (!state) {
        return;
    }
    AutoUnlockMutex guard(&state->lock);
    if (state->closing) {
        return;
    }
    if (!state->workerStarted) {
        return;
    }
    state->generation++;
    state->command = command;
    state->voiceIndex = voice;
    str::ReplaceWithCopy(&state->word, word);
    str::ReplaceWithCopy(&state->url, url);
    state->status = command == SpeechCommand::Stop ? DictionarySpeechStatus::Idle : DictionarySpeechStatus::Loading;
    str::ReplaceWithCopy(&state->message, StrL(""));
    NotifySpeech(state);
    SetEvent(state->wake);
}

#if !COMPILER_MINGW
struct SpeechBackend {
    ISpVoice* voice = nullptr;
    Vec<ISpObjectToken*> tokens;
    MCIDEVICEID audio = 0;
    Str audioPath;
    bool speaking = false;
    bool initialized = false;

    void Stop() {
        if (voice) {
            voice->Speak(nullptr, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
        }
        if (audio) {
            mciSendCommandW(audio, MCI_STOP, MCI_WAIT, 0);
            mciSendCommandW(audio, MCI_CLOSE, MCI_WAIT, 0);
            audio = 0;
        }
        if (len(audioPath)) {
            file::Delete(audioPath);
            str::Free(audioPath);
            audioPath = {};
        }
        speaking = false;
    }

    ~SpeechBackend() {
        Stop();
        for (auto* token : tokens) {
            if (token) {
                token->Release();
            }
        }
        if (voice) {
            voice->Release();
        }
        if (initialized) {
            CoUninitialize();
        }
    }
};

static void InitSpeech(SpeechBackend& backend, DictionarySpeechState* state) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    backend.initialized = SUCCEEDED(hr);
    if (!backend.initialized) {
        SpeechStatus(state, 0, DictionarySpeechStatus::Error, StrL("Windows speech initialization failed."));
        return;
    }
    hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_ISpVoice, (void**)&backend.voice);
    if (FAILED(hr) || !backend.voice) {
        SpeechStatus(state, 0, DictionarySpeechStatus::Error,
                     StrL("No Windows speech voice is available. Install a speech voice in Windows Settings."));
        return;
    }
    VecAppend(backend.tokens, (ISpObjectToken*)nullptr);
    StrVec names;
    names.Append(StrL("Default Windows voice"));
    ISpObjectTokenCategory* category = nullptr;
    IEnumSpObjectTokens* enumerator = nullptr;
    hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_INPROC_SERVER, IID_ISpObjectTokenCategory,
                          (void**)&category);
    if (SUCCEEDED(hr) && category) {
        hr = category->SetId(SPCAT_VOICES, FALSE);
        if (SUCCEEDED(hr)) {
            category->EnumTokens(nullptr, nullptr, &enumerator);
        }
        category->Release();
    }
    if (enumerator) {
        ISpObjectToken* token = nullptr;
        ULONG fetched = 0;
        while (len(backend.tokens) < 128 && enumerator->Next(1, &token, &fetched) == S_OK && fetched) {
            WCHAR* name = nullptr;
            if (SUCCEEDED(token->GetStringValue(nullptr, &name)) && name) {
                names.Append(ToUtf8Temp(name));
                VecAppend(backend.tokens, token);
            } else {
                token->Release();
            }
            CoTaskMemFree(name);
            token = nullptr;
        }
        enumerator->Release();
    }
    {
        AutoUnlockMutex guard(&state->lock);
        state->voiceNames = names;
        NotifySpeech(state);
    }
    SpeechStatus(state, 0, DictionarySpeechStatus::Idle);
}

struct AudioRequest {
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    Str path;

    ~AudioRequest() {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        if (request) {
            WinHttpCloseHandle(request);
        }
        if (connect) {
            WinHttpCloseHandle(connect);
        }
        if (session) {
            WinHttpCloseHandle(session);
        }
        if (len(path)) {
            file::Delete(path);
            str::Free(path);
        }
    }
};

// Recordings are fetched only by the explicit recording command, with bounded reads and no redirects.
static Str DownloadRecording(DictionarySpeechState* state, int generation, Str url) {
    if (!ValidAudioUrl(url)) {
        return {};
    }
    WCHAR* urlW = CWStrTemp(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength =
        parts.dwPasswordLength = (DWORD)-1;
    if (!WinHttpCrackUrl(urlW, 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength ||
        parts.dwUserNameLength || parts.dwPasswordLength) {
        return {};
    }
    AudioRequest net;
    net.session = WinHttpOpen(L"SumatraPDFEnhancedDictionary", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!net.session) {
        return {};
    }
    if (!WinHttpSetTimeouts(net.session, 5000, 5000, 5000, 5000)) {
        return {};
    }
    TempWStr host = str::DupTemp(WStr(parts.lpszHostName, (int)parts.dwHostNameLength));
    net.connect = WinHttpConnect(net.session, host.s, parts.nPort, 0);
    if (!net.connect) {
        return {};
    }
    str::Builder target;
    target.Append(ToUtf8Temp(WStr(parts.lpszUrlPath, (int)parts.dwUrlPathLength)));
    if (!len(target)) {
        target.Append(StrL("/"));
    }
    target.Append(ToUtf8Temp(WStr(parts.lpszExtraInfo, (int)parts.dwExtraInfoLength)));
    net.request = WinHttpOpenRequest(net.connect, L"GET", CWStrTemp(ToStr(target)), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!net.request) {
        return {};
    }
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(net.request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect))) {
        return {};
    }
    ULONGLONG started = GetTickCount64();
    if (!CurrentSpeech(state, generation) ||
        !WinHttpSendRequest(net.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(net.request, nullptr)) {
        return {};
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(net.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        return {};
    }
    DWORD advertised = 0;
    size = sizeof(advertised);
    if (WinHttpQueryHeaders(net.request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &advertised, &size, WINHTTP_NO_HEADER_INDEX) &&
        advertised > kPronunciationMaxAudio) {
        return {};
    }
    net.path = str::Dup(GetTempFilePathTemp(StrL("SPE")));
    if (!len(net.path)) {
        return {};
    }
    net.file = CreateFileW(CWStrTemp(net.path), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_TEMPORARY,
                           nullptr);
    if (net.file == INVALID_HANDLE_VALUE) {
        return {};
    }
    DWORD total = 0;
    for (;;) {
        if (!CurrentSpeech(state, generation) || GetTickCount64() - started >= kPronunciationTimeout) {
            return {};
        }
        u8 buffer[16384];
        DWORD received = 0;
        if (!WinHttpReadData(net.request, buffer, sizeof(buffer), &received)) {
            return {};
        }
        if (!received) {
            break;
        }
        if (received > kPronunciationMaxAudio - total) {
            return {};
        }
        DWORD written = 0;
        if (!WriteFile(net.file, buffer, received, &written, nullptr) || written != received) {
            return {};
        }
        total += received;
    }
    if (!total || !CurrentSpeech(state, generation)) {
        return {};
    }
    CloseHandle(net.file);
    net.file = INVALID_HANDLE_VALUE;
    Str result = net.path;
    net.path = {};
    return result;
}

static bool PlayRecordingFile(SpeechBackend& backend, Str path) {
    if (!len(path)) {
        return false;
    }
    u8 signature[12]{};
    int count = file::ReadN(path, signature, sizeof(signature));
    bool wav = count >= 12 && memcmp(signature, "RIFF", 4) == 0 && memcmp(signature + 8, "WAVE", 4) == 0;
    bool mp3 =
        count >= 3 && (memcmp(signature, "ID3", 3) == 0 || (signature[0] == 0xff && (signature[1] & 0xe0) == 0xe0));
    bool ogg = count >= 4 && memcmp(signature, "OggS", 4) == 0;
    if (!wav && !mp3 && !ogg) {
        return false;
    }
    MCI_OPEN_PARMSW params{};
    params.lpstrDeviceType = wav ? L"waveaudio" : L"mpegvideo";
    params.lpstrElementName = CWStrTemp(path);
    MCIERROR error = mciSendCommandW(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_WAIT, (DWORD_PTR)&params);
    if (error) {
        return false;
    }
    backend.audio = params.wDeviceID;
    MCI_PLAY_PARMS play{};
    error = mciSendCommandW(backend.audio, MCI_PLAY, 0, (DWORD_PTR)&play);
    if (error) {
        mciSendCommandW(backend.audio, MCI_CLOSE, MCI_WAIT, 0);
        backend.audio = 0;
        return false;
    }
    backend.audioPath = str::Dup(path);
    backend.speaking = true;
    return true;
}

static bool SpeakWord(SpeechBackend& backend, Str word, int index) {
    if (!backend.voice || !ValidSpeechWord(word)) {
        return false;
    }
    ISpObjectToken* token = index > 0 && index < len(backend.tokens) ? backend.tokens[index] : nullptr;
    HRESULT hr = backend.voice->SetVoice(token);
    if (FAILED(hr)) {
        return false;
    }
    hr = backend.voice->Speak(CWStrTemp(word), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
    backend.speaking = SUCCEEDED(hr);
    return backend.speaking;
}

static bool SpeechFinished(SpeechBackend& backend) {
    if (backend.audio) {
        MCI_STATUS_PARMS status{};
        status.dwItem = MCI_STATUS_MODE;
        if (mciSendCommandW(backend.audio, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT, (DWORD_PTR)&status)) {
            return true;
        }
        return status.dwReturn != MCI_MODE_PLAY;
    }
    SPVOICESTATUS status{};
    return !backend.voice || FAILED(backend.voice->GetStatus(&status, nullptr)) || status.dwRunningState == SPRS_DONE;
}
#endif

static void RunSpeech(DictionarySpeechState* state) {
#if !COMPILER_MINGW
    SpeechBackend backend;
    InitSpeech(backend, state);
#else
    SpeechStatus(state, 0, DictionarySpeechStatus::Error, StrL("Windows speech is unavailable in this build."));
#endif
    int activeGeneration = 0;
    for (;;) {
        SpeechCommand command = SpeechCommand::None;
        Str word;
        Str url;
        int voice = 0;
        int generation = 0;
        {
            AutoUnlockMutex guard(&state->lock);
            if (state->closing) {
                break;
            }
            command = state->command;
            state->command = SpeechCommand::None;
            generation = state->generation;
            voice = state->voiceIndex;
            if (command != SpeechCommand::None) {
                word = str::Dup(state->word);
                url = str::Dup(state->url);
            }
        }
        if (command != SpeechCommand::None) {
            activeGeneration = generation;
#if !COMPILER_MINGW
            backend.Stop();
            if (command == SpeechCommand::Stop) {
                SpeechStatus(state, generation, DictionarySpeechStatus::Idle);
            } else if (!ValidSpeechWord(word)) {
                SpeechStatus(state, generation, DictionarySpeechStatus::Error, StrL("Choose a word to pronounce."));
            } else {
                bool played = false;
                if (command == SpeechCommand::Recording) {
                    SpeechStatus(state, generation, DictionarySpeechStatus::Loading,
                                 StrL("Loading pronunciation recording..."));
                    Str path = DownloadRecording(state, generation, url);
                    if (CurrentSpeech(state, generation)) {
                        played = PlayRecordingFile(backend, path);
                    }
                    if (len(path)) {
                        if (!played) {
                            file::Delete(path);
                        }
                        str::Free(path);
                    }
                }
                if (CurrentSpeech(state, generation)) {
                    bool spoken = played || SpeakWord(backend, word, voice);
                    Str message = command == SpeechCommand::Recording && !played
                                      ? StrL("Recording unavailable. Using the selected Windows voice.")
                                      : Str{};
                    SpeechStatus(
                        state, generation, spoken ? DictionarySpeechStatus::Speaking : DictionarySpeechStatus::Error,
                        spoken ? message
                               : StrL("Pronunciation failed. Check the selected Windows voice and audio output."));
                }
            }
#else
            SpeechStatus(state, generation,
                         command == SpeechCommand::Stop ? DictionarySpeechStatus::Idle : DictionarySpeechStatus::Error,
                         StrL("Windows speech is unavailable in this build."));
#endif
            str::Free(word);
            str::Free(url);
        }
#if !COMPILER_MINGW
        if (backend.speaking && SpeechFinished(backend)) {
            backend.Stop();
            SpeechStatus(state, activeGeneration, DictionarySpeechStatus::Idle);
        }
#else
        (void)activeGeneration;
        (void)voice;
#endif
        ResetTempArena();
#if !COMPILER_MINGW
        WaitForSingleObject(state->wake, backend.speaking ? 50 : INFINITE);
#else
        WaitForSingleObject(state->wake, INFINITE);
#endif
    }
#if !COMPILER_MINGW
    backend.Stop();
#endif
    delete state;
}

DictionarySpeech::DictionarySpeech(HWND owner, UINT notifyMessage) {
    state = new DictionarySpeechState();
    state->owner = owner;
    state->notifyMessage = notifyMessage;
    state->voiceNames.Append(StrL("Default Windows voice"));
    state->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE worker = state->wake ? StartThread(MkFunc0(RunSpeech, state), StrL("dictionary-speech")) : nullptr;
    if (!worker) {
        state->status = DictionarySpeechStatus::Error;
        state->message = str::Dup(StrL("Cannot start pronunciation service."));
        NotifySpeech(state);
        return;
    }
    state->workerStarted = true;
    CloseHandle(worker);
}

DictionarySpeech::~DictionarySpeech() {
    if (!state) {
        return;
    }
    state->lock.Lock();
    state->owner = nullptr;
    state->closing = true;
    state->generation++;
    bool workerStarted = state->workerStarted;
    SetEvent(state->wake);
    state->lock.Unlock();
    // A failed worker owns nothing; otherwise it deletes its state after cancellation.
    if (!workerStarted) {
        delete state;
    }
    state = nullptr;
}

void DictionarySpeech::GetVoiceNames(StrVec& out) {
    AutoUnlockMutex guard(&state->lock);
    out = state->voiceNames;
}

void DictionarySpeech::Speak(Str word, int voiceIndex) {
    QueueSpeech(state, SpeechCommand::Word, word, {}, voiceIndex);
}

void DictionarySpeech::PlayRecording(Str httpsUrl, Str fallbackWord, int voiceIndex) {
    QueueSpeech(state, SpeechCommand::Recording, fallbackWord, httpsUrl, voiceIndex);
}

void DictionarySpeech::Stop() {
    QueueSpeech(state, SpeechCommand::Stop);
}

DictionarySpeechStatus DictionarySpeech::GetStatus(Str* message) {
    AutoUnlockMutex guard(&state->lock);
    if (message) {
        str::ReplaceWithCopy(message, state->message);
    }
    return state->status;
}

#if IS_DEBUG
void DictionarySpeech_UnitTests() {
    utassert(ValidSpeechWord(StrL("omnipotent")));
    utassert(ValidSpeechWord(StrL("well-being")));
    utassert(!ValidSpeechWord(StrL("")));
    utassert(!ValidSpeechWord(StrL("one\ntwo")));
    utassert(!ValidSpeechWord(Str("one\0two", 7)));
    utassert(ValidAudioUrl(StrL("https://api.dictionaryapi.dev/media/pronunciations/en/word-us.mp3")));
    utassert(ValidAudioUrl(StrL("https://example.org/audio.ogg?version=1")));
    utassert(!ValidAudioUrl(StrL("http://example.org/audio.mp3")));
    utassert(!ValidAudioUrl(StrL("https://user:password@example.org/audio.mp3")));
    utassert(!ValidAudioUrl(StrL("https:///audio.mp3")));
    utassert(!ValidAudioUrl(StrL("https://example.org/audio.mp3#fragment")));
    utassert(!ValidAudioUrl(StrL("https://example.org/\"audio.mp3")));
    DictionarySpeechState state;
    state.workerStarted = true;
    state.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    QueueSpeech(&state, SpeechCommand::Word, StrL("word"), {}, 2);
    utassert(state.command == SpeechCommand::Word && state.voiceIndex == 2);
    utassert(CurrentSpeech(&state, 1));
    QueueSpeech(&state, SpeechCommand::Stop);
    utassert(!CurrentSpeech(&state, 1));
    utassert(state.status == DictionarySpeechStatus::Idle && state.command == SpeechCommand::Stop);
    SpeechStatus(&state, 1, DictionarySpeechStatus::Error, StrL("stale"));
    utassert(state.status == DictionarySpeechStatus::Idle && len(state.message) == 0);
    state.closing = true;
    utassert(!CurrentSpeech(&state, 2));
}
#endif
