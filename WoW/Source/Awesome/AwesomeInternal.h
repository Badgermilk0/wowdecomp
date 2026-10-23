#ifndef WOW_SOURCE_AWESOME_AWESOMEINTERNAL_H
#define WOW_SOURCE_AWESOME_AWESOMEINTERNAL_H

// Shared between the feature files; the engine only sees Awesome.h.

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Awesome/Awesome.h"
#include "WoW/Source/Awesome/AwesomeCVar.h"
#include "WoW/Source/Object/WGUID.h"

class CGObject_C;
class FrameScript_Object;

struct AwesomeLuaFunction {
    const char* m_name;
    lua_CFunction m_function;
};

// Lua helpers
void AwesomeLuaRegisterGlobals(lua_State* L, const AwesomeLuaFunction* functions, size_t count);
void AwesomeLuaRegisterTable(lua_State* L, const char* name, const AwesomeLuaFunction* functions, size_t count);
void AwesomeLuaPushObject(lua_State* L, FrameScript_Object* object);
// the frame stored in the table at index (its [0] userdata), without raising an error
FrameScript_Object* AwesomeLuaToObject(lua_State* L, int32_t index);
// SlashCmdList[key] = function, SLASH_<key>1 = command; leaves an existing command alone
void AwesomeLuaRegisterSlashCommand(lua_State* L, const char* key, const char* command, lua_CFunction function);
bool AwesomeLuaIsNoneOrNil(lua_State* L, int32_t index);

// UTF-8 <-> UTF-16 (the engine strings are UTF-8, Windows wants UTF-16)
size_t AwesomeUtf8ToWide(const char* text, wchar_t* buffer, size_t capacity);
size_t AwesomeWideToUtf8(const wchar_t* text, char* buffer, size_t capacity);
bool AwesomeCopyToClipboard(const char* text, void* window);
char* AwesomeGetClipboard(void* window);

// "YYYY-MM-DD-hh.mm.ss " of the client launch, prefixed to per-session log files
const char* AwesomeSessionStamp();

// Object helpers
CGObject_C* AwesomeObjectPtr(const WOWGUID& guid, uint32_t typeMask);
WOWGUID AwesomeGuidFromUnitToken(const char* token);
bool AwesomeInCombatLockdown();

// Shaders compiled from HLSL in place of a stock .bls (D3D9 only; the stock shader stands in otherwise)
class CGxShader;

struct AwesomeShaderDefine {
    const char* m_name;
    const char* m_value;
};

struct AwesomeShaderSource {
    EGxShTarget m_target;
    // the name the engine creates the shader under; never a stock shader's name
    const char* m_name;
    const char* m_hlsl;
    // one entry point per permutation
    const char* const* m_entries;
    uint32_t m_entryCount;
    // the stock shader (Shaders\<Vertex|Pixel>, name) that supplies the metadata and stands in
    const char* m_fallbackPath;
    const char* m_fallbackName;
    // true: load the stock shader as is
    bool (*m_useFallback)();
    // optional macro list, ended by a null name; each set compiles and caches separately
    const AwesomeShaderDefine* (*m_defines)();
};

// source must outlive the client (a static)
void AwesomeShaderRegister(const AwesomeShaderSource* source);
// reloads a created shader in place, e.g. after its defines or fallback switch changed
void AwesomeShaderReload(CGxShader* shader);

// Features
void AwesomeUnitRegisterScriptFunctions(lua_State* L);
void AwesomeItemRegisterScriptFunctions(lua_State* L);

void AwesomeUiFixesRegisterCVars();

// AwesomeFonts.cpp
void AwesomeFontsRegisterCVars();

// Msdf/AwesomeMsdf.cpp
void AwesomeMsdfRegisterCVars();
void AwesomeMsdfOnEnterWorld();

// AwesomeFrameCapture.cpp
void AwesomeFrameCaptureRegisterScriptFunctions(lua_State* L);
void AwesomeFrameCaptureOnEnterWorld();
void AwesomeFrameCaptureOnLeaveWorld();

// AwesomeHdClient.cpp
void AwesomeHdClientRegisterCVars();

// AwesomeShadows.cpp
void AwesomeShadowsRegisterCVars();

// AwesomeLighting.cpp
void AwesomeLightingRegisterCVars();
void AwesomeLightingRegisterScriptFunctions(lua_State* L);
void AwesomeLightingOnEnterWorld();

// AwesomeAudio.cpp
void AwesomeAudioRegisterCVars();
void AwesomeAudioRegisterScriptFunctions(lua_State* L);
void AwesomeAudioOnEnterWorld();

// AwesomeWeather.cpp
void AwesomeWeatherRegisterCVars();
void AwesomeWeatherRegisterScriptFunctions(lua_State* L);
void AwesomeWeatherOnEnterWorld();
void AwesomeWeatherOnLeaveWorld();

// AwesomeStreaming.cpp
void AwesomeStreamingRegisterCVars();
void AwesomeStreamingRegisterScriptFunctions(lua_State* L);

// AwesomePerf.cpp
void AwesomePerfRegisterCVars();
void AwesomePerfRegisterScriptFunctions(lua_State* L);
void AwesomePerfOnEnterWorld();

void AwesomeDisplayRegisterCVars();
void AwesomeEmbeddedUiRegisterCVars();
void AwesomeDisplayRegisterScriptFunctions(lua_State* L);

void AwesomeCameraRegisterCVars();
void AwesomeCameraOnEnterWorld();
void AwesomeCameraOnLeaveWorld();

void AwesomeMiscRegisterCVars();
void AwesomeMiscRegisterScriptFunctions(lua_State* L);
void AwesomeMiscOnEnterWorld();
void AwesomeMiscOnLeaveWorld();
void AwesomeMiscOnPaint();

void AwesomeCommandLineInitialize();
void AwesomeCommandLineOnGlueXMLLoaded();
void AwesomeCommandLineOnCharacterListUpdated();

void AwesomeVoiceChatRegisterCVars();
void AwesomeVoiceChatRegisterScriptFunctions(lua_State* L);
void AwesomeVoiceChatOnEnterWorld();
void AwesomeVoiceChatOnLeaveWorld();
void AwesomeVoiceChatOnPaint();

#endif
