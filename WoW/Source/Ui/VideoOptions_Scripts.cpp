#include "VideoOptions.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include "Engine/Source/Base/CmdLine.h"
#include "Engine/Source/Base/Types.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/Types.h"
#include "Engine/Source/Gx/CGxFormat.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Os/System.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C2iVector.h"
#include "Engine/Source/Tempest/C3iVector.h"
#include "Engine/Source/Tempest/CRect.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Frame/CSimpleFrame.h"

int32_t Script_SetScreenResolution(lua_State* L);
int32_t Script_SetupFullscreenScale(lua_State* L);
int32_t Script_SetMultisampleFormat(lua_State* L);

// STATIC: 0x00A0C7C0
static const int32_t s_formatBits[CGxFormat::Formats_Last] = { 16, 24, 24, 30, 16, 24, 24, 32 };

// STATIC: 0x00BEA730
static TSGrowableArray<C2iVector> s_resolutions;

// STATIC: 0x00BEA740
static TSGrowableArray<C3iVector> s_multisampleFormats;

// VERIFY: 690d5583 auto
// OFFSET: 0x0054EA30
static int CompareRefreshRates(const void* a, const void* b) {
    int32_t left = *static_cast<const int32_t*>(a);
    int32_t right = *static_cast<const int32_t*>(b);

    if (left < right) {
        return -1;
    }

    return right < left;
}

// VERIFY: 1b209080 auto
// OFFSET: 0x0054EA60
static int32_t Script_GetGamma(lua_State* L) {
    CVar* gamma = CVar::Lookup("gamma");
    lua_pushnumber(L, 1.0 - gamma->m_floatValue);
    return 1;
}

// VERIFY: 535f4faf auto
// OFFSET: 0x0054EA90
static int32_t Script_SetGamma(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetGamma(value)");
        return 0;
    }

    CVar* gamma = CVar::Lookup("gamma");
    char value[16];
    SStrPrintf(value, sizeof(value), "%f", 1.0 - lua_tonumber(L, 1));
    gamma->Set(value, true, false, false, true);
    return 0;
}

// VERIFY: 0aa3137a auto
// OFFSET: 0x0054EB10
static int32_t Script_GetTerrainMip(lua_State* L) {
    CVar* shadowLevel = CVar::Lookup("shadowLevel");
    lua_pushnumber(L, 1.0 - shadowLevel->m_intValue);
    return 1;
}

// VERIFY: 4f98daeb auto
// OFFSET: 0x0054EB40
static int32_t Script_SetTerrainMip(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetTerrainMip(value)");
        return 0;
    }

    int32_t value = static_cast<int32_t>(lua_tonumber(L, 1));

    char buffer[16];
    SStrPrintf(buffer, sizeof(buffer), "%d", 1 - value);
    CVar::Lookup("shadowLevel")->Set(buffer, true, false, false, true);

    return 0;
}

// VERIFY: 9e434c59 auto
// OFFSET: 0x0054EBC0
static int32_t Script_IsPlayerResolutionAvailable(lua_State* L) {
    if (OsGetNumberOfProcessors() <= 1) {
        lua_pushnil(L);
    } else {
        lua_pushnumber(L, 1.0);
    }

    return 1;
}

// VERIFY: 30ca7104 auto
// OFFSET: 0x0054EE60
static int32_t Script_GetVideoCaps(lua_State* L) {
    CGxCaps& caps = g_theGxDevicePtr->Caps();

    if (caps.m_texFilterAnisotropic) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    if (caps.m_shaderTargets[GxSh_Pixel]) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    if (caps.m_shaderTargets[GxSh_Vertex]) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    if (caps.m_texFilterTrilinear) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    lua_pushnumber(L, caps.unk_0018);

    if (caps.m_maxTexAnisotropy) {
        lua_pushnumber(L, caps.m_maxTexAnisotropy);
    } else {
        lua_pushnil(L);
    }

    if (caps.m_hwCursor) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 7;
}

// VERIFY: 8a982f74 auto
// OFFSET: 0x0054EF90
static int32_t Script_IsStereoVideoAvailable(lua_State* L) {
    if (g_theGxDevicePtr->Caps().m_stereoAvailable) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// VERIFY: 8b3f6200 manual
// OFFSET: 0x0054F050
static void SetupFormats() {
    if (s_resolutions.Count()) {
        return;
    }

    bool useDefaults = false;
    CVar* widescreen = CVar::Lookup("widescreen");

    if (widescreen && widescreen->m_intValue) {
        TSGrowableArray<CGxMonitorMode> modes;
        GxAdapterMonitorModes(modes);

        C2iVector last = { 0, 0 };

        for (uint32_t i = 0; i < modes.Count(); i++) {
            const C2iVector& size = modes[i].m_size;

            if (static_cast<float>(size.x) / size.y >= 1.248f && size.x > 640 && size.y > 480 && (size.x != last.x || size.y != last.y)) {
                last = size;
                s_resolutions.New(last);
            }
        }

        useDefaults = s_resolutions.Count() == 0;
    } else {
        useDefaults = true;
    }

    if (!useDefaults) {
        return;
    }

    C2iVector resolution;
    resolution.x = 800;
    resolution.y = 600;
    s_resolutions.New(resolution);
    resolution.x = 1024;
    resolution.y = 768;
    s_resolutions.New(resolution);
    resolution.x = 1280;
    resolution.y = 1024;
    s_resolutions.New(resolution);
    resolution.x = 1600;
    resolution.y = 1200;
    s_resolutions.New(resolution);
}

// VERIFY: 9112f98e manual
// OFFSET: 0x0054F1B0
static void SetupMultisampleFormats() {
    if (s_multisampleFormats.Count()) {
        return;
    }

    EGxApi api = GxDefaultApi();
    CVar* gxApi = CVar::Lookup("gxApi");

    if (gxApi) {
        const char* name = gxApi->m_stringValue.m_str;

        for (int32_t i = GxApi_OpenGl; i < GxApis_Last; i++) {
            if (GxApiSupported(static_cast<EGxApi>(i)) && !SStrCmpI(name, g_gxApiNames[i], STORM_MAX_STR)) {
                api = static_cast<EGxApi>(i);
                break;
            }
        }
    }

    if (CmdLineGetBool(CMD_OPENGL) && GxApiSupported(GxApi_OpenGl)) {
        api = GxApi_OpenGl;
    }

    if (CmdLineGetBool(CMD_D3D) && GxApiSupported(GxApi_D3d9)) {
        api = GxApi_D3d9;
    }

    if (CmdLineGetBool(CMD_D3D9EX) && GxApiSupported(GxApi_D3d9Ex)) {
        api = GxApi_D3d9Ex;
    }

    // MODIFIED: D3D11 device
    if (CmdLineGetBool(CMD_D3D11) && GxApiSupported(GxApi_D3d11)) {
        api = GxApi_D3d11;
    }

    int32_t colorBits = 0;
    int32_t depthBits = 0;

    CVar* var = CVar::Lookup("gxColorBits");

    if (var) {
        colorBits = var->m_intValue;
    }

    var = CVar::Lookup("gxDepthBits");

    if (var) {
        depthBits = var->m_intValue;
    }

    CVar::Lookup("gxMultisample");

    TSGrowableArray<CGxFormat> formats;
    CGxDevice::AdapterFormats(api, formats);

    for (uint32_t i = 0; i < formats.Count(); i++) {
        C3iVector format;
        format.x = s_formatBits[formats[i].m_colorFormat];
        format.y = s_formatBits[formats[i].m_depthFormat];
        format.z = formats[i].m_multisampleCount;

        if (format.x != colorBits || format.y != depthBits) {
            continue;
        }

        bool found = false;

        for (uint32_t j = 0; j < s_multisampleFormats.Count(); j++) {
            if (s_multisampleFormats[j].x == format.x && s_multisampleFormats[j].y == format.y && s_multisampleFormats[j].z == format.z) {
                found = true;
                break;
            }
        }

        if (!found) {
            s_multisampleFormats.New(format);
        }
    }
}

// MODIFIED: display management. The one entry that is not a display mode: the window's own size, kept in the list's
// order by area. Returns its index
static int32_t s_customResolution = -1;

static uint32_t SetCustomResolution(int32_t width, int32_t height) {
    if (s_customResolution >= 0) {
        for (uint32_t i = s_customResolution; i + 1 < s_resolutions.Count(); i++) {
            s_resolutions[i] = s_resolutions[i + 1];
        }

        s_resolutions.SetCount(s_resolutions.Count() - 1);
    }

    uint32_t index = 0;

    while (index < s_resolutions.Count() && s_resolutions[index].x * s_resolutions[index].y <= width * height) {
        index++;
    }

    s_resolutions.SetCount(s_resolutions.Count() + 1);

    for (uint32_t i = s_resolutions.Count() - 1; i > index; i--) {
        s_resolutions[i] = s_resolutions[i - 1];
    }

    s_resolutions[index].x = width;
    s_resolutions[index].y = height;
    s_customResolution = static_cast<int32_t>(index);
    return index;
}

// MODIFIED: display management. The size the game has: gxResolution, or the window's for full screen and maximized
// windows, which cover their monitor whatever gxResolution says (CGxDeviceD3d11::ICreateWindow). A size that is no
// display mode (a resized or remembered window) is listed, instead of the first entry being shown, which Okay would
// then apply
static void SyncCurrentResolution(int32_t& width, int32_t& height) {
    CVar* gxResolution = CVar::Lookup("gxResolution");
    char separator;
    width = 0;
    height = 0;

    if (gxResolution && gxResolution->m_stringValue.m_str) {
        sscanf(gxResolution->m_stringValue.m_str, "%d%c%d", &width, &separator, &height);
    }

    if (g_theGxDevicePtr && (!g_theGxDevicePtr->m_format.m_window || g_theGxDevicePtr->m_format.m_maximize == 1)) {
        CRect rect = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_theGxDevicePtr->CapsWindowSize(rect);

        if (rect.maxX - rect.minX >= 1.0f && rect.maxY - rect.minY >= 1.0f) {
            width = static_cast<int32_t>(rect.maxX - rect.minX);
            height = static_cast<int32_t>(rect.maxY - rect.minY);
        }
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    for (uint32_t i = 0; i < s_resolutions.Count(); i++) {
        if (width == s_resolutions[i].x && height == s_resolutions[i].y) {
            return;
        }
    }

    SetCustomResolution(width, height);
}

// VERIFY: eea3ee86 auto
// OFFSET: 0x0054F430
static int32_t Script_GetScreenResolutions(lua_State* L) {
    SetupFormats();

    // MODIFIED: display management. The game's own size is in the list whichever of this and GetCurrentResolution the
    // options panel asks first
    int32_t width;
    int32_t height;
    SyncCurrentResolution(width, height);
    lua_checkstack(L, s_resolutions.Count());

    for (uint32_t i = 0; i < s_resolutions.Count(); i++) {
        char resolution[64];
        SStrPrintf(resolution, sizeof(resolution), "%dx%d", s_resolutions[i].x, s_resolutions[i].y);
        lua_pushstring(L, resolution);
    }

    return s_resolutions.Count();
}

// VERIFY: a2c8da50 manual
// OFFSET: 0x0054F4A0
static int32_t Script_GetCurrentResolution(lua_State* L) {
    SetupFormats();

    CVar* gxResolution = CVar::Lookup("gxResolution");

    if (gxResolution && gxResolution->m_stringValue.m_str) {
        // MODIFIED: display management (SyncCurrentResolution)
        int32_t width = 0;
        int32_t height = 0;
        SyncCurrentResolution(width, height);

        for (uint32_t i = 0; i < s_resolutions.Count(); i++) {
            if (width == s_resolutions[i].x && height == s_resolutions[i].y) {
                lua_pushnumber(L, i + 1);
                return 1;
            }
        }
    }

    lua_pushnumber(L, 1.0);
    return 1;
}

// VERIFY: 75e0cfd3 manual
// OFFSET: 0x0054F690
static int32_t Script_GetRefreshRates(lua_State* L) {
    SetupFormats();

    uint32_t index = 0;

    if (lua_isnumber(L, 1)) {
        uint32_t count = s_resolutions.Count();
        index = count < static_cast<uint32_t>(lua_tonumber(L, 1)) - 1 ? s_resolutions.Count() : static_cast<uint32_t>(lua_tonumber(L, 1)) - 1;
    }

    if (index >= s_resolutions.Count()) {
        return 0;
    }

    TSGrowableArray<CGxMonitorMode> modes;
    GxAdapterMonitorModes(modes);

    int32_t* rates = static_cast<int32_t*>(alloca(sizeof(int32_t) * modes.Count()));
    uint32_t numRates = 0;

    for (uint32_t i = 0; i < modes.Count(); i++) {
        if (s_resolutions[index].x == modes[i].m_size.x && s_resolutions[index].y == modes[i].m_size.y) {
            rates[numRates++] = modes[i].m_refreshRate;
        }
    }

    if (numRates) {
        qsort(rates, numRates, sizeof(int32_t), CompareRefreshRates);
    } else {
        // MODIFIED: display management. A window's own size has no display mode: offer the current refresh rate
        CVar* refresh = CVar::Lookup("gxRefresh");
        rates = static_cast<int32_t*>(alloca(sizeof(int32_t)));
        rates[numRates++] = refresh && refresh->m_intValue > 0 ? refresh->m_intValue : 60;
    }

    int32_t results = 0;

    for (uint32_t i = 0; i < numRates; i++) {
        if (i && rates[i] == rates[i - 1]) {
            continue;
        }

        lua_pushnumber(L, rates[i]);
        results++;
    }

    return results;
}

// VERIFY: f54064fe auto
// OFFSET: 0x0054F820
static int32_t Script_GetMultisampleFormats(lua_State* L) {
    SetupMultisampleFormats();
    lua_checkstack(L, 3 * s_multisampleFormats.Count());

    for (uint32_t i = 0; i < s_multisampleFormats.Count(); i++) {
        lua_pushnumber(L, s_multisampleFormats[i].x);
        lua_pushnumber(L, s_multisampleFormats[i].y);
        lua_pushnumber(L, s_multisampleFormats[i].z);
    }

    return 3 * s_multisampleFormats.Count();
}

// VERIFY: ebf2464a auto
// OFFSET: 0x0054F8B0
static int32_t Script_GetCurrentMultisampleFormat(lua_State* L) {
    SetupMultisampleFormats();

    int32_t colorBits = 0;
    int32_t depthBits = 0;
    int32_t multisample = 0;

    CVar* var = CVar::Lookup("gxColorBits");

    if (var) {
        colorBits = var->m_intValue;
    }

    var = CVar::Lookup("gxDepthBits");

    if (var) {
        depthBits = var->m_intValue;
    }

    var = CVar::Lookup("gxMultisample");

    if (var) {
        multisample = var->m_intValue;
    }

    for (uint32_t i = 0; i < s_multisampleFormats.Count(); i++) {
        if (colorBits == s_multisampleFormats[i].x && depthBits == s_multisampleFormats[i].y && multisample == s_multisampleFormats[i].z) {
            lua_pushnumber(L, i + 1);
            return 1;
        }
    }

    lua_pushnumber(L, 1.0);
    return 1;
}

// VERIFY: 1162e2e5 manual
// OFFSET: 0x0054ED80
static int32_t Script_SetupFullscreenScale(lua_State* L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        luaL_error(L, "Usage: SetupFullscreenScale(frame)");
        return 0;
    }

    lua_rawgeti(L, 1, 0);
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(lua_touserdata(L, -1));
    lua_settop(L, -2);

    if (!frame) {
        luaL_error(L, "SetupFullscreenScale(): Couldn't find 'this' in frame object");
        return 0;
    }

    if (!frame->IsA(CSimpleFrame::GetObjectType())) {
        luaL_error(L, "SetupFullscreenScale(): Wrong object type, expected frame");
        return 0;
    }

    float compensation = CoordinateGetAspectCompensation();

    if (compensation >= 1.0f) {
        frame->SetFrameScale(1.0f, false);
    } else {
        frame->SetFrameScale(compensation, false);
    }

    return 0;
}

// VERIFY: de05e80f manual
// OFFSET: 0x0054F570
static int32_t Script_SetScreenResolution(lua_State* L) {
    SetupFormats();

    uint32_t index = 0;

    if (lua_isnumber(L, 1)) {
        uint32_t count = s_resolutions.Count();
        index = count < static_cast<uint32_t>(lua_tonumber(L, 1)) - 1 ? s_resolutions.Count() : static_cast<uint32_t>(lua_tonumber(L, 1)) - 1;
    }

    CVar* gxResolution = CVar::Lookup("gxResolution");

    if (gxResolution && gxResolution->m_stringValue.m_str) {
        int32_t width = 0;
        int32_t height = 0;
        char separator;
        sscanf(gxResolution->m_stringValue.m_str, "%d%c%d", &width, &separator, &height);

        if (width != s_resolutions[index].x || height != s_resolutions[index].y) {
            char value[32];
            SStrPrintf(value, sizeof(value), "%dx%d", s_resolutions[index].x, s_resolutions[index].y);
            gxResolution->Set(value, true, false, false, true);
        }
    }

    return 0;
}

// VERIFY: 64c2ee40 manual
// OFFSET: 0x0054F980
static int32_t Script_SetMultisampleFormat(lua_State* L) {
    SetupMultisampleFormats();

    uint32_t index = 0;

    if (lua_isnumber(L, 1)) {
        uint32_t count = s_multisampleFormats.Count();
        index = count < static_cast<uint32_t>(lua_tonumber(L, 1)) - 1 ? s_multisampleFormats.Count() : static_cast<uint32_t>(lua_tonumber(L, 1)) - 1;
    }

    CVar* colorBits = CVar::Lookup("gxColorBits");
    CVar* depthBits = CVar::Lookup("gxDepthBits");
    CVar* multisample = CVar::Lookup("gxMultisample");

    if (colorBits && depthBits && multisample) {
        C3iVector& format = s_multisampleFormats[index];

        if (colorBits->m_intValue != format.x || depthBits->m_intValue != format.y || multisample->m_intValue != format.z) {
            char value[32];
            SStrPrintf(value, sizeof(value), "%d", format.x);
            colorBits->Set(value, true, false, false, true);
            SStrPrintf(value, sizeof(value), "%d", format.y);
            depthBits->Set(value, true, false, false, true);
            SStrPrintf(value, sizeof(value), "%d", format.z);
            multisample->Set(value, true, false, false, true);
        }
    }

    return 0;
}

// MODIFIED: the Video options' Graphics API choice. The renderers the options offer, as gxApi names: D3D9, D3D9Ex
// and the D3D11 device. OpenGL, which GxApiSupported also accepts, is left out: untested in this build.
static int32_t Script_GetGraphicsAPIs(lua_State* L) {
    static const EGxApi s_offered[] = { GxApi_D3d9, GxApi_D3d9Ex, GxApi_D3d11 };
    int32_t count = 0;

    for (EGxApi api : s_offered) {
        if (GxApiSupported(api)) {
            lua_pushstring(L, g_gxApiNames[api]);
            count++;
        }
    }

    return count;
}

// MODIFIED: the renderer this session runs (gxApi or a command-line switch, chosen at startup; a changed gxApi waits
// for a restart), as its gxApi name; nothing before the device exists
static int32_t Script_GetCurrentGraphicsAPI(lua_State* L) {
    if (!g_theGxDevicePtr) {
        return 0;
    }

    lua_pushstring(L, g_gxApiNames[g_theGxDevicePtr->DeviceApi()]);
    return 1;
}

// STATIC: 0x00ACD320
static FrameScript_Method s_scriptFunctions[] = {
    { "GetScreenResolutions", &Script_GetScreenResolutions },
    { "GetCurrentResolution", &Script_GetCurrentResolution },
    { "SetScreenResolution", &Script_SetScreenResolution },
    { "GetRefreshRates", &Script_GetRefreshRates },
    { "SetupFullscreenScale", &Script_SetupFullscreenScale },
    { "GetMultisampleFormats", &Script_GetMultisampleFormats },
    { "GetCurrentMultisampleFormat", &Script_GetCurrentMultisampleFormat },
    { "SetMultisampleFormat", &Script_SetMultisampleFormat },
    { "GetVideoCaps", &Script_GetVideoCaps },
    { "GetGamma", &Script_GetGamma },
    { "SetGamma", &Script_SetGamma },
    { "GetTerrainMip", &Script_GetTerrainMip },
    { "SetTerrainMip", &Script_SetTerrainMip },
    { "IsStereoVideoAvailable", &Script_IsStereoVideoAvailable },
    { "IsPlayerResolutionAvailable", &Script_IsPlayerResolutionAvailable },
    // MODIFIED: the Video options' Graphics API choice
    { "GetGraphicsAPIs", &Script_GetGraphicsAPIs },
    { "GetCurrentGraphicsAPI", &Script_GetCurrentGraphicsAPI },
};

// VERIFY: 9fc618b7 auto
// OFFSET: 0x0054EC00
void CGVideoOptions::RegisterScriptFunctions() {
    for (uint32_t i = 0; i < sizeof(s_scriptFunctions) / sizeof(s_scriptFunctions[0]); i++) {
        FrameScript_RegisterFunction(s_scriptFunctions[i].m_name, s_scriptFunctions[i].m_method);
    }
}

// VERIFY: d9c16912 auto
// OFFSET: 0x0054EC30
void CGVideoOptions::UnregisterScriptFunctions() {
    for (uint32_t i = 0; i < sizeof(s_scriptFunctions) / sizeof(s_scriptFunctions[0]); i++) {
        FrameScript_UnregisterFunction(s_scriptFunctions[i].m_name);
    }
}
