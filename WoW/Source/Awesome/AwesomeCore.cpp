#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "WoW/Source/Awesome/AwesomeNamePlates.h"
#include <cstdio>
#include <cstring>
#include <windows.h>
#include "Engine/Source/Frame/CScriptObject.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/ScriptEvents.h"

void AwesomeLuaRegisterGlobals(lua_State* L, const AwesomeLuaFunction* functions, size_t count) {
    for (size_t i = 0; i < count; i++) {
        FrameScript_RegisterFunction(functions[i].m_name, functions[i].m_function);
    }
}

void AwesomeLuaRegisterTable(lua_State* L, const char* name, const AwesomeLuaFunction* functions, size_t count) {
    lua_createtable(L, 0, static_cast<int32_t>(count));

    for (size_t i = 0; i < count; i++) {
        lua_pushcclosure(L, functions[i].m_function, 0);
        lua_setfield(L, -2, functions[i].m_name);
    }

    lua_setfield(L, LUA_GLOBALSINDEX, name);
}

void AwesomeLuaPushObject(lua_State* L, FrameScript_Object* object) {
    if (!object) {
        lua_pushnil(L);
        return;
    }

    if (!object->m_luaRegistered) {
        object->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, object->m_luaObjectRef);
}

FrameScript_Object* AwesomeLuaToObject(lua_State* L, int32_t index) {
    if (lua_type(L, index) != LUA_TTABLE) {
        return nullptr;
    }

    lua_rawgeti(L, index, 0);
    FrameScript_Object* object = static_cast<FrameScript_Object*>(lua_touserdata(L, -1));
    lua_settop(L, -2);
    return object;
}

void AwesomeLuaRegisterSlashCommand(lua_State* L, const char* key, const char* command, lua_CFunction function) {
    if (!L || !key || !command) {
        return;
    }

    char global[64];
    SStrPrintf(global, sizeof(global), "SLASH_%s1", key);

    lua_getfield(L, LUA_GLOBALSINDEX, "SlashCmdList");

    if (lua_type(L, -1) == LUA_TTABLE) {
        lua_getfield(L, -1, key);
        bool exists = lua_type(L, -1) != LUA_TNIL;
        lua_settop(L, -2);

        if (exists) {
            lua_settop(L, -2);
            return;
        }

        lua_pushcclosure(L, function, 0);
        lua_setfield(L, -2, key);
    }

    lua_settop(L, -2);

    lua_getfield(L, LUA_GLOBALSINDEX, global);
    bool exists = lua_type(L, -1) != LUA_TNIL;
    lua_settop(L, -2);

    if (!exists) {
        lua_pushstring(L, command);
        lua_setfield(L, LUA_GLOBALSINDEX, global);
    }
}

bool AwesomeLuaIsNoneOrNil(lua_State* L, int32_t index) {
    int32_t type = lua_type(L, index);
    return type == LUA_TNONE || type == LUA_TNIL;
}

size_t AwesomeUtf8ToWide(const char* text, wchar_t* buffer, size_t capacity) {
    if (!text || !buffer || !capacity) {
        return 0;
    }

    int32_t written = MultiByteToWideChar(CP_UTF8, 0, text, -1, buffer, static_cast<int32_t>(capacity));

    if (written <= 0) {
        buffer[0] = L'\0';
        return 0;
    }

    buffer[capacity - 1] = L'\0';
    return static_cast<size_t>(written - 1);
}

size_t AwesomeWideToUtf8(const wchar_t* text, char* buffer, size_t capacity) {
    if (!text || !buffer || !capacity) {
        return 0;
    }

    int32_t written = WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, static_cast<int32_t>(capacity), nullptr, nullptr);

    if (written <= 0) {
        buffer[0] = '\0';
        return 0;
    }

    buffer[capacity - 1] = '\0';
    return static_cast<size_t>(written - 1);
}

bool AwesomeCopyToClipboard(const char* text, void* window) {
    HWND hwnd = static_cast<HWND>(window);

    if (!text || !*text) {
        if (!OpenClipboard(hwnd)) {
            return false;
        }

        bool emptied = EmptyClipboard() != 0;
        CloseClipboard();
        return emptied;
    }

    int32_t length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);

    if (length <= 0) {
        return false;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(wchar_t) * length);

    if (!memory) {
        return false;
    }

    wchar_t* data = static_cast<wchar_t*>(GlobalLock(memory));

    if (!data) {
        GlobalFree(memory);
        return false;
    }

    MultiByteToWideChar(CP_UTF8, 0, text, -1, data, length);
    GlobalUnlock(memory);

    if (!OpenClipboard(hwnd)) {
        GlobalFree(memory);
        return false;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        GlobalFree(memory);
        return false;
    }

    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    return true;
}

char* AwesomeGetClipboard(void* window) {
    if (!OpenClipboard(static_cast<HWND>(window))) {
        return nullptr;
    }

    HANDLE data = GetClipboardData(CF_UNICODETEXT);

    if (!data) {
        CloseClipboard();
        return nullptr;
    }

    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));

    if (!text) {
        CloseClipboard();
        return nullptr;
    }

    int32_t length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    char* string = nullptr;

    if (length > 0) {
        string = static_cast<char*>(SMemAlloc(length, __FILE__, __LINE__, 0));

        if (string) {
            WideCharToMultiByte(CP_UTF8, 0, text, -1, string, length, nullptr, nullptr);
        }
    }

    GlobalUnlock(data);
    CloseClipboard();
    return string;
}

const char* AwesomeSessionStamp() {
    static char s_stamp[40];

    if (!s_stamp[0]) {
        SYSTEMTIME time;
        GetLocalTime(&time);
        snprintf(s_stamp, sizeof(s_stamp), "%04d-%02d-%02d-%02d.%02d.%02d ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    }

    return s_stamp;
}

CGObject_C* AwesomeObjectPtr(const WOWGUID& guid, uint32_t typeMask) {
    if (!guid) {
        return nullptr;
    }

    return ClntObjMgrObjectPtr(guid, static_cast<OBJECT_TYPE>(typeMask), __FILE__, __LINE__);
}

WOWGUID AwesomeGuidFromUnitToken(const char* token) {
    return token ? GetGuidByKeyword(token) : WOWGUID();
}

bool AwesomeInCombatLockdown() {
    return CSimpleTop::s_instance && !CSimpleTop::s_instance->m_allowProtectedActions;
}

void AwesomeRegisterCVars() {
    // the per-frame callback lives in the engine's frame layer, which does not link against the game
    CSimpleTop::s_paintCallback = AwesomeOnPaint;

    AwesomeCommandLineInitialize();
    AwesomeUiFixesRegisterCVars();
    AwesomeFontsRegisterCVars();
    AwesomeMsdfRegisterCVars();
    AwesomeDisplayRegisterCVars();
    AwesomeEmbeddedUiRegisterCVars();
    AwesomeCameraRegisterCVars();
    AwesomeNamePlatesRegisterCVars();
    AwesomeMiscRegisterCVars();
    AwesomeVoiceChatRegisterCVars();
    AwesomeHdClientRegisterCVars();
    AwesomeShadowsRegisterCVars();
    AwesomeLightingRegisterCVars();
    AwesomePerfRegisterCVars();
    AwesomeStreamingRegisterCVars();
    AwesomeAudioRegisterCVars();
    AwesomeWeatherRegisterCVars();
}

void AwesomeRegisterScriptFunctions() {
    lua_State* L = FrameScript_GetContext();

    if (!L) {
        return;
    }

    lua_pushnumber(L, AWESOME_WOTLK_VERSION);
    lua_setfield(L, LUA_GLOBALSINDEX, "AwesomeWotlk");

    AwesomeUnitRegisterScriptFunctions(L);
    AwesomeItemRegisterScriptFunctions(L);
    AwesomeNamePlatesRegisterScriptFunctions(L);
    AwesomeMiscRegisterScriptFunctions(L);
    AwesomeVoiceChatRegisterScriptFunctions(L);
    AwesomeFrameCaptureRegisterScriptFunctions(L);
    AwesomePerfRegisterScriptFunctions(L);
    AwesomeDisplayRegisterScriptFunctions(L);
    AwesomeStreamingRegisterScriptFunctions(L);
    AwesomeAudioRegisterScriptFunctions(L);
    AwesomeWeatherRegisterScriptFunctions(L);
    AwesomeLightingRegisterScriptFunctions(L);
}

void AwesomeOnEnterWorld() {
    AwesomeCameraOnEnterWorld();
    AwesomeMiscOnEnterWorld();
    AwesomeVoiceChatOnEnterWorld();
    AwesomeMsdfOnEnterWorld();
    AwesomeFrameCaptureOnEnterWorld();
    AwesomePerfOnEnterWorld();
    AwesomeAudioOnEnterWorld();
    AwesomeWeatherOnEnterWorld();
    AwesomeLightingOnEnterWorld();
}

void AwesomeOnLeaveWorld() {
    AwesomeCameraOnLeaveWorld();
    AwesomeMiscOnLeaveWorld();
    AwesomeVoiceChatOnLeaveWorld();
    AwesomeFrameCaptureOnLeaveWorld();
    AwesomeWeatherOnLeaveWorld();
}

void AwesomeOnPaint() {
    AwesomeMiscOnPaint();
    AwesomeVoiceChatOnPaint();
}

void AwesomeOnGlueXMLLoaded() {
    AwesomeCommandLineOnGlueXMLLoaded();
}

void AwesomeOnCharacterListUpdated() {
    AwesomeCommandLineOnCharacterListUpdated();
}
