#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <sapi.h>
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "WoW/Source/Ui/Types.h"

// Text-to-speech through the Windows voices (SAPI): retail's C_VoiceChat / C_TTSSettings speech API.
//   C_VoiceChat.SpeakText(voiceID, text, destination, rate, volume), StopSpeakingText, GetTtsVoices, GetRemoteTtsVoices
//   C_TTSSettings.Get/SetSpeechRate, Get/SetSpeechVolume, GetSpeechVoiceID, Get/SetVoiceOption(ByName), ...
// Events: VOICE_CHAT_TTS_PLAYBACK_STARTED / _FINISHED / _FAILED, VOICE_CHAT_TTS_VOICES_UPDATE.

#ifndef SPFEI
#define SPFEI_FLAGCHECK ((1ui64 << SPEI_RESERVED1) | (1ui64 << SPEI_RESERVED2))
#define SPFEI(SPEI_ord) ((1ui64 << SPEI_ord) | SPFEI_FLAGCHECK)
#endif

enum TTSDestination {
    TTS_DESTINATION_LOCAL_PLAYBACK = 1,
    TTS_DESTINATION_QUEUED_LOCAL_PLAYBACK = 4,
};

struct TTSVoice {
    std::wstring m_name;
    ISpObjectToken* m_token;
};

struct TTSUtterance {
    int32_t m_id;
    int32_t m_destination;
    bool m_started;
};

struct TTSEvent {
    bool m_start;
    ULONG m_stream;
};

static int32_t VoiceCount();

static AwesomeCVar s_cvTtsVoice = AwesomeCVar::Int("ttsVoice", 1, 0, 0);
static AwesomeCVar s_cvTtsSpeed = AwesomeCVar::Int("ttsSpeed", 0, -10, 10);
static AwesomeCVar s_cvTtsVolume = AwesomeCVar::Int("ttsVolume", 100, 0, 100);

static std::atomic<ISpVoice*> s_voice(nullptr);
static std::atomic<int32_t> s_callbacksInFlight(0);
static bool s_comOwned;
static std::vector<TTSVoice> s_voices;
static std::map<ULONG, TTSUtterance> s_streams;
static int32_t s_nextUtteranceId = 1;

// filled by the SAPI notification, drained on the main thread
static std::mutex s_eventsLock;
static std::vector<TTSEvent> s_events;

static void ClearSpeechEvent(SPEVENT* event) {
    switch (event->elParamType) {
        case SPET_LPARAM_IS_POINTER:
        case SPET_LPARAM_IS_STRING:
            CoTaskMemFree(reinterpret_cast<void*>(event->lParam));
            break;

        case SPET_LPARAM_IS_TOKEN:
        case SPET_LPARAM_IS_OBJECT:
            reinterpret_cast<IUnknown*>(event->lParam)->Release();
            break;

        default:
            break;
    }

    memset(event, 0, sizeof(*event));
}

static std::vector<TTSVoice> EnumerateVoices() {
    std::vector<TTSVoice> voices;
    ISpObjectTokenCategory* category = nullptr;

    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory, reinterpret_cast<void**>(&category)))) {
        return voices;
    }

    IEnumSpObjectTokens* tokens = nullptr;

    if (SUCCEEDED(category->SetId(SPCAT_VOICES, FALSE)) && SUCCEEDED(category->EnumTokens(nullptr, nullptr, &tokens)) && tokens) {
        ISpObjectToken* token = nullptr;
        ULONG fetched = 0;

        while (tokens->Next(1, &token, &fetched) == S_OK && fetched) {
            // the token key's default value is the voice's display name
            WCHAR* description = nullptr;

            if (SUCCEEDED(token->GetStringValue(nullptr, &description)) && description) {
                TTSVoice voice;
                voice.m_name = description;
                voice.m_token = token;
                voices.push_back(voice);
                CoTaskMemFree(description);
            } else {
                token->Release();
            }
        }

        tokens->Release();
    }

    category->Release();
    return voices;
}

static void ReleaseVoices(std::vector<TTSVoice>& voices) {
    for (size_t i = 0; i < voices.size(); i++) {
        if (voices[i].m_token) {
            voices[i].m_token->Release();
        }
    }

    voices.clear();
}

static int32_t VoiceCount() {
    std::vector<TTSVoice> voices = EnumerateVoices();
    int32_t count = static_cast<int32_t>(voices.size());
    ReleaseVoices(voices);
    return count;
}

static int32_t VoiceMax() {
    int32_t count = VoiceCount();

    // MODIFIED: no voice list yet (COM is not initialized when the CVar registers at startup), so the saved voice is
    // kept instead of being clamped to 0
    return count > 0 ? count - 1 : 0x7FFF;
}

static std::string VoiceName(int32_t voiceId) {
    if (voiceId < 0 || voiceId >= static_cast<int32_t>(s_voices.size())) {
        return std::string();
    }

    char name[512];
    AwesomeWideToUtf8(s_voices[voiceId].m_name.c_str(), name, sizeof(name));
    return name;
}

static void RefreshVoices() {
    std::vector<TTSVoice> voices = EnumerateVoices();
    bool changed = voices.size() != s_voices.size();

    for (size_t i = 0; !changed && i < voices.size(); i++) {
        changed = voices[i].m_name != s_voices[i].m_name;
    }

    if (!changed) {
        ReleaseVoices(voices);
        return;
    }

    ReleaseVoices(s_voices);
    s_voices = voices;
    FrameScript_SignalEvent(SCRIPT_EVENT_VOICE_CHAT_TTS_VOICES_UPDATE, nullptr);
}

static void __stdcall OnSpeechNotify(WPARAM, LPARAM) {
    s_callbacksInFlight++;
    ISpVoice* voice = s_voice.load();

    if (voice) {
        SPEVENT event = {};
        ULONG fetched = 0;

        while (SUCCEEDED(voice->GetEvents(1, &event, &fetched)) && fetched == 1) {
            if (event.eEventId == SPEI_START_INPUT_STREAM || event.eEventId == SPEI_END_INPUT_STREAM) {
                std::lock_guard<std::mutex> lock(s_eventsLock);
                TTSEvent queued = { event.eEventId == SPEI_START_INPUT_STREAM, event.ulStreamNum };
                s_events.push_back(queued);
            }

            ClearSpeechEvent(&event);
        }
    }

    s_callbacksInFlight--;
}

static void SpeakText(int32_t voiceId, const char* text, int32_t destination, int32_t rate, int32_t volume) {
    int32_t utteranceId = s_nextUtteranceId;
    s_nextUtteranceId = utteranceId == 0x7FFFFFFF ? 1 : utteranceId + 1;
    destination = destination == TTS_DESTINATION_QUEUED_LOCAL_PLAYBACK ? TTS_DESTINATION_QUEUED_LOCAL_PLAYBACK : TTS_DESTINATION_LOCAL_PLAYBACK;

    ISpVoice* voice = s_voice.load();
    ULONG stream = 0;

    if (voice && voiceId >= 0 && voiceId < static_cast<int32_t>(s_voices.size()) && s_voices[voiceId].m_token && SUCCEEDED(voice->SetVoice(s_voices[voiceId].m_token))) {
        voice->SetRate(rate < -10 ? -10 : (rate > 10 ? 10 : rate));
        voice->SetVolume(static_cast<USHORT>(volume < 0 ? 0 : (volume > 100 ? 100 : volume)));

        int32_t length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);

        if (length > 0) {
            std::vector<wchar_t> wide(static_cast<size_t>(length));
            MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);

            if (FAILED(voice->Speak(wide.data(), SPF_ASYNC, &stream))) {
                stream = 0;
            }
        }
    }

    if (!stream) {
        FrameScript_SignalEvent(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_FAILED, "%s%d%d", "InternalError", utteranceId, destination);
        return;
    }

    TTSUtterance utterance = { utteranceId, destination, false };
    s_streams[stream] = utterance;
}

static void StopSpeakingAsync() {
    ISpVoice* voice = s_voice.load();

    if (!voice) {
        return;
    }

    // purging blocks until the current audio buffer is released
    voice->AddRef();
    std::thread([voice] {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
        voice->Release();

        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }).detach();
}

void AwesomeVoiceChatOnEnterWorld() {
    if (s_voice.load()) {
        return;
    }

    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        s_comOwned = true;
    }

    ISpVoice* voice = nullptr;

    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&voice)))) {
        return;
    }

    voice->SetNotifyCallbackFunction(OnSpeechNotify, 0, 0);
    ULONGLONG interest = SPFEI(SPEI_START_INPUT_STREAM) | SPFEI(SPEI_END_INPUT_STREAM);
    voice->SetInterest(interest, interest);
    s_voice.store(voice);
    RefreshVoices();
}

void AwesomeVoiceChatOnLeaveWorld() {
    ISpVoice* voice = s_voice.exchange(nullptr);

    if (voice) {
        voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);

        while (s_callbacksInFlight.load()) {
            std::this_thread::yield();
        }

        voice->Release();
    }

    {
        std::lock_guard<std::mutex> lock(s_eventsLock);
        s_events.clear();
    }

    s_streams.clear();
    ReleaseVoices(s_voices);

    if (s_comOwned) {
        CoUninitialize();
        s_comOwned = false;
    }
}

void AwesomeVoiceChatOnPaint() {
    std::vector<TTSEvent> events;

    {
        std::lock_guard<std::mutex> lock(s_eventsLock);

        if (s_events.empty()) {
            return;
        }

        events.swap(s_events);
    }

    for (size_t i = 0; i < events.size(); i++) {
        std::map<ULONG, TTSUtterance>::iterator it = s_streams.find(events[i].m_stream);

        if (it == s_streams.end()) {
            continue;
        }

        TTSUtterance& utterance = it->second;

        if (events[i].m_start) {
            if (!utterance.m_started) {
                utterance.m_started = true;
                FrameScript_SignalEvent(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_STARTED, "%d%d%d%d", 1, utterance.m_id, 0, utterance.m_destination);
            }
        } else {
            FrameScript_SignalEvent(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_FINISHED, "%d%d%d", 1, utterance.m_id, utterance.m_destination);
            s_streams.erase(it);
        }
    }
}

static void PushVoiceList(lua_State* L, const std::vector<TTSVoice>& voices) {
    lua_createtable(L, 0, 0);

    for (size_t i = 0; i < voices.size(); i++) {
        lua_createtable(L, 0, 2);

        lua_pushnumber(L, static_cast<double>(i));
        lua_setfield(L, -2, "voiceID");

        char name[512];
        AwesomeWideToUtf8(voices[i].m_name.c_str(), name, sizeof(name));
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");

        lua_rawseti(L, -2, static_cast<int32_t>(i + 1));
    }
}

static int32_t Script_GetTtsVoices(lua_State* L) {
    RefreshVoices();
    PushVoiceList(L, s_voices);
    return 1;
}

static int32_t Script_GetRemoteTtsVoices(lua_State* L) {
    std::vector<TTSVoice> voices = EnumerateVoices();
    PushVoiceList(L, voices);
    ReleaseVoices(voices);
    return 1;
}

static int32_t Script_SpeakText(lua_State* L) {
    int32_t voiceId = static_cast<int32_t>(luaL_checknumber(L, 1));
    const char* text = luaL_checklstring(L, 2, nullptr);
    int32_t top = lua_gettop(L);
    int32_t destination = top >= 3 && lua_type(L, 3) != LUA_TNIL ? static_cast<int32_t>(luaL_checknumber(L, 3)) : TTS_DESTINATION_LOCAL_PLAYBACK;
    int32_t rate = top >= 4 && lua_type(L, 4) != LUA_TNIL ? static_cast<int32_t>(luaL_checknumber(L, 4)) : 0;
    int32_t volume = top >= 5 && lua_type(L, 5) != LUA_TNIL ? static_cast<int32_t>(luaL_checknumber(L, 5)) : 100;

    SpeakText(voiceId, text, destination, rate, volume);
    return 0;
}

static int32_t Script_StopSpeakingText(lua_State* L) {
    StopSpeakingAsync();
    return 0;
}

static int32_t Script_RefreshVoices(lua_State* L) {
    RefreshVoices();
    return 0;
}

static int32_t Script_SetDefaultSettings(lua_State* L) {
    s_cvTtsVoice.Set(VoiceCount() > 1 ? "1" : "0");
    s_cvTtsSpeed.Set("0");
    s_cvTtsVolume.Set("100");
    FrameScript_SignalEvent(SCRIPT_EVENT_VOICE_CHAT_TTS_VOICES_UPDATE, nullptr);
    return 0;
}

static int32_t Script_SetSpeechRate(lua_State* L) {
    s_cvTtsSpeed.Set(luaL_checklstring(L, 1, nullptr));
    return 0;
}

static int32_t Script_SetSpeechVolume(lua_State* L) {
    s_cvTtsVolume.Set(luaL_checklstring(L, 1, nullptr));
    return 0;
}

static int32_t Script_SetVoiceOption(lua_State* L) {
    if (VoiceCount() > 0) {
        s_cvTtsVoice.Set(luaL_checklstring(L, 1, nullptr));
        FrameScript_SignalEvent(SCRIPT_EVENT_VOICE_CHAT_TTS_VOICES_UPDATE, nullptr);
    }

    return 0;
}

static int32_t Script_SetVoiceOptionByName(lua_State* L) {
    const char* name = luaL_checklstring(L, 1, nullptr);
    wchar_t wideName[512];
    AwesomeUtf8ToWide(name, wideName, sizeof(wideName) / sizeof(wideName[0]));

    std::vector<TTSVoice> voices = EnumerateVoices();
    int32_t found = -1;

    for (size_t i = 0; i < voices.size(); i++) {
        if (!_wcsicmp(voices[i].m_name.c_str(), wideName)) {
            found = static_cast<int32_t>(i);
            break;
        }
    }

    ReleaseVoices(voices);

    if (found >= 0) {
        char value[16];
        snprintf(value, sizeof(value), "%d", found);
        s_cvTtsVoice.Set(value);
        FrameScript_SignalEvent(SCRIPT_EVENT_VOICE_CHAT_TTS_VOICES_UPDATE, nullptr);
    }

    return 0;
}

static int32_t Script_GetSpeechRate(lua_State* L) {
    lua_pushnumber(L, s_cvTtsSpeed.m_intValue);
    return 1;
}

static int32_t Script_GetSpeechVolume(lua_State* L) {
    lua_pushnumber(L, s_cvTtsVolume.m_intValue);
    return 1;
}

static int32_t Script_GetSpeechVoiceID(lua_State* L) {
    lua_pushnumber(L, s_cvTtsVoice.m_intValue);
    return 1;
}

static int32_t Script_GetVoiceOptionName(lua_State* L) {
    lua_pushstring(L, VoiceName(s_cvTtsVoice.m_intValue).c_str());
    return 1;
}

void AwesomeVoiceChatRegisterCVars() {
    s_cvTtsVoice.m_dynamicMax = VoiceMax;
    s_cvTtsVoice.Register();
    s_cvTtsSpeed.Register();
    s_cvTtsVolume.Register();
}

void AwesomeVoiceChatRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_voiceChat[] = {
        { "GetTtsVoices", &Script_GetTtsVoices },
        { "GetRemoteTtsVoices", &Script_GetRemoteTtsVoices },
        { "SpeakText", &Script_SpeakText },
        { "StopSpeakingText", &Script_StopSpeakingText },
    };

    static const AwesomeLuaFunction s_ttsSettings[] = {
        { "GetSpeechRate", &Script_GetSpeechRate },
        { "GetSpeechVolume", &Script_GetSpeechVolume },
        { "GetSpeechVoiceID", &Script_GetSpeechVoiceID },
        { "GetVoiceOptionName", &Script_GetVoiceOptionName },
        { "SetDefaultSettings", &Script_SetDefaultSettings },
        { "SetSpeechRate", &Script_SetSpeechRate },
        { "SetSpeechVolume", &Script_SetSpeechVolume },
        { "SetVoiceOption", &Script_SetVoiceOption },
        { "SetVoiceOptionByName", &Script_SetVoiceOptionByName },
        { "RefreshVoices", &Script_RefreshVoices },
    };

    AwesomeLuaRegisterTable(L, "C_VoiceChat", s_voiceChat, sizeof(s_voiceChat) / sizeof(s_voiceChat[0]));
    AwesomeLuaRegisterTable(L, "C_TTSSettings", s_ttsSettings, sizeof(s_ttsSettings) / sizeof(s_ttsSettings[0]));
}
