#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <cstring>
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Awesome/AwesomeEmbeddedUi.h"
#include "WoW/Source/Ui/Types.h"

// The AwesomeCVar settings window and the Flash helper, built into the client: their files live in
// WoW/Source/Awesome/Interface and are compiled in (EmbedInterface.cmake). They run as FrameXML does, untainted,
// after the FrameXML files and before the AddOns, each file with the arguments an AddOn's files get (its name and a
// table shared by its files), in the order its .toc lists them; then ADDON_LOADED fires with the name.
// awesomeBuiltInAddOns 0 turns them off, and AddOns of those names in Interface\AddOns load instead.
// AwesomeCVar's own settings, an AddOn's SavedVariables, are the CVars below (Interface/AwesomeCVar/Embedded.lua).

static const char* const s_embeddedAddOns[] = { "AwesomeCVar", "Flash" };

static AwesomeCVar s_cvBuiltInAddOns = AwesomeCVar::Int("awesomeBuiltInAddOns", 1, 0, 1);

struct PreferenceCVar {
    const char* m_name;
    const char* m_help;
    const char* m_default;
};

static const PreferenceCVar s_preferenceCVars[] = {
    { "awesomeGameMenuButton", "AwesomeCVar: its button in the game menu (Esc)", "1" },
    { "awesomeChatMessages", "AwesomeCVar: setting changes and the load message in chat", "1" },
    { "awesomeMinimapHide", "AwesomeCVar: hide its minimap button", "0" },
    { "awesomeMinimapPos", "AwesomeCVar: its minimap button's angle around the minimap", "220" },
    { "awesomeFrameWidth", "AwesomeCVar: its window's width (empty = the default)", "" },
    { "awesomeFrameHeight", "AwesomeCVar: its window's height (empty = the default)", "" },
};

static const AwesomeEmbeddedFile* FindFile(const char* path) {
    for (size_t i = 0; i < g_awesomeEmbeddedFileCount; i++) {
        if (!SStrCmpI(g_awesomeEmbeddedFiles[i].m_path, path, STORM_MAX_STR)) {
            return &g_awesomeEmbeddedFiles[i];
        }
    }

    return nullptr;
}

static void RunFile(lua_State* L, const char* addOn, int32_t ns, const char* entry, CStatus* status) {
    char path[260];
    SStrPrintf(path, sizeof(path), "%s\\%s", addOn, entry);
    const AwesomeEmbeddedFile* file = FindFile(path);

    if (!file) {
        status->Add(STATUS_WARNING, "Built-in %s: %s is not built in", addOn, entry);
        return;
    }

    size_t length = SStrLen(entry);

    if (length < 4 || SStrCmpI(entry + length - 4, ".lua", STORM_MAX_STR)) {
        status->Add(STATUS_WARNING, "Built-in %s: %s is not a Lua file; only Lua files are loaded", addOn, entry);
        return;
    }

    // the FrameXML loader skips a UTF-8 byte order mark; Lua does not
    const char* data = reinterpret_cast<const char*>(file->m_data);
    size_t size = file->m_size;

    if (size >= 3 && static_cast<uint8_t>(data[0]) == 0xEF && static_cast<uint8_t>(data[1]) == 0xBB && static_cast<uint8_t>(data[2]) == 0xBF) {
        data += 3;
        size -= 3;
    }

    char chunkName[300];
    SStrPrintf(chunkName, sizeof(chunkName), "@Interface\\AddOns\\%s", path);

    if (luaL_loadbuffer(L, data, size, chunkName) == 0) {
        lua_pushstring(L, addOn);
        lua_pushvalue(L, ns);

        if (lua_pcall(L, 2, 0, 0) == 0) {
            return;
        }
    }

    const char* message = lua_tostring(L, -1);
    status->Add(STATUS_ERROR, "Built-in %s: %s", addOn, message ? message : "error");
    lua_pop(L, 1);
}

static void LoadEmbeddedAddOn(const char* addOn, CStatus* status) {
    char tocPath[260];
    SStrPrintf(tocPath, sizeof(tocPath), "%s\\%s.toc", addOn, addOn);
    const AwesomeEmbeddedFile* toc = FindFile(tocPath);

    if (!toc) {
        status->Add(STATUS_WARNING, "Built-in %s: no %s", addOn, tocPath);
        return;
    }

    lua_State* L = FrameScript_GetContext();
    int32_t top = lua_gettop(L);

    // client code, as FrameXML: what it creates is untainted (its game menu button is secure)
    const char* savedTaint = lua_tainted;
    lua_tainted = nullptr;

    lua_createtable(L, 0, 0);
    int32_t ns = lua_gettop(L);

    const char* cursor = reinterpret_cast<const char*>(toc->m_data);

    while (*cursor) {
        char line[260];
        SStrTokenize(&cursor, line, sizeof(line), "\r\n", nullptr);

        char* entry = line;

        while (*entry == ' ' || *entry == '\t') {
            entry++;
        }

        // UTF-8 byte order mark on the first line
        if (static_cast<uint8_t>(entry[0]) == 0xEF && static_cast<uint8_t>(entry[1]) == 0xBB && static_cast<uint8_t>(entry[2]) == 0xBF) {
            entry += 3;
        }

        size_t length = SStrLen(entry);

        while (length && (entry[length - 1] == ' ' || entry[length - 1] == '\t')) {
            entry[--length] = '\0';
        }

        if (!length || entry[0] == '#') {
            continue;
        }

        RunFile(L, addOn, ns, entry, status);
    }

    lua_settop(L, top);
    lua_tainted = savedTaint;

    FrameScript_SignalEvent(SCRIPT_EVENT_ADDON_LOADED, "%s", addOn);
}

bool AwesomeIsBuiltInAddOn(const char* name) {
    if (!s_cvBuiltInAddOns.m_intValue || !name) {
        return false;
    }

    for (const char* addOn : s_embeddedAddOns) {
        if (!SStrCmpI(addOn, name, STORM_MAX_STR)) {
            return true;
        }
    }

    return false;
}

void AwesomeLoadBuiltInAddOns(CStatus* status) {
    if (!s_cvBuiltInAddOns.m_intValue) {
        return;
    }

    for (const char* addOn : s_embeddedAddOns) {
        LoadEmbeddedAddOn(addOn, status);
    }
}

void AwesomeEmbeddedUiRegisterCVars() {
    s_cvBuiltInAddOns.Register();

    for (const PreferenceCVar& preference : s_preferenceCVars) {
        CVar::Register(preference.m_name, preference.m_help, 0, preference.m_default, nullptr, CATEGORY_DEBUG, false, nullptr, false);
    }
}
