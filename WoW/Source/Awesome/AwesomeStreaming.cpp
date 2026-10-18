// Asset streaming (not from awesome_wotlk): the async file loader's thread pool and frame budget, the x64 memory limits
// for textures, models and WMOs, and GetLoadTimings() / the Load line of /frametimes.
//
// asyncReadThreads: disk reader threads (Engine/Source/Services/AsyncFile.cpp). 0 = auto (half the logical processors,
//   2 to 4); 1 = stock, one "Disk Queue" thread. Each read model file is also relocated on its reader thread.
// asyncPostloadBudget: main-thread ms per frame for the post-load work of files that finished reading (BLP uploads,
//   model setup, WMO and ADT parsing); 0 = stock, which spent up to 100 ms of one frame (asyncHandlerTimeout, which
//   Config.wtf never reached: it is read before the file is loaded).
// textureLoadBudget: MB of texture files in flight (Services/Texture.cpp); 4 = stock.
// M2CacheTime / M2CacheSize: seconds and MB of model files an unused model is kept for reuse (Model2/M2Cache.cpp);
//   10 s and no size cap = stock.
// wmoCacheTime: seconds an unused WMO is kept (WorldClient/MapObj.cpp); 10 = stock.

#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <thread>
#include "Engine/Source/Model2/M2Cache.h"
#include "Engine/Source/Services/AsyncFile.h"
#include "Engine/Source/Services/Texture.h"
#include "WoW/Source/WorldClient/MapObj.h"

static void OnReadThreads(AwesomeCVar* cvar, bool changed);
static void OnPostloadBudget(AwesomeCVar* cvar, bool changed);
static void OnTextureLoadBudget(AwesomeCVar* cvar, bool changed);
static void OnM2Cache(AwesomeCVar* cvar, bool changed);
static void OnWmoCacheTime(AwesomeCVar* cvar, bool changed);

static AwesomeCVar s_cvReadThreads = AwesomeCVar::Int("asyncReadThreads", 0, 0, 8, OnReadThreads);
static AwesomeCVar s_cvPostloadBudget = AwesomeCVar::Int("asyncPostloadBudget", 8, 0, 100, OnPostloadBudget);
static AwesomeCVar s_cvTextureLoadBudget = AwesomeCVar::Int("textureLoadBudget", 64, 4, 1024, OnTextureLoadBudget);
static AwesomeCVar s_cvM2CacheTime = AwesomeCVar::Int("M2CacheTime", 120, 10, 3600, OnM2Cache);
static AwesomeCVar s_cvM2CacheSize = AwesomeCVar::Int("M2CacheSize", 256, 0, 4096, OnM2Cache);
static AwesomeCVar s_cvWmoCacheTime = AwesomeCVar::Int("wmoCacheTime", 120, 10, 3600, OnWmoCacheTime);

static uint32_t ReadThreadCount(int32_t value) {
    if (value > 0) {
        return static_cast<uint32_t>(value);
    }

    uint32_t threads = std::thread::hardware_concurrency() / 2;
    return threads < 2 ? 2 : threads > 4 ? 4 : threads;
}

static void OnReadThreads(AwesomeCVar* cvar, bool changed) {
#if defined(WOWDECOMP_ARCH_64)
    AsyncFileReadSetDiskThreads(ReadThreadCount(cvar->m_intValue));
#endif
}

static void OnPostloadBudget(AwesomeCVar* cvar, bool changed) {
#if defined(WOWDECOMP_ARCH_64)
    AsyncFileReadSetPostloadBudget(static_cast<uint32_t>(cvar->m_intValue));
#endif
}

static void OnTextureLoadBudget(AwesomeCVar* cvar, bool changed) {
#if defined(WOWDECOMP_ARCH_64)
    TextureSetLoadBudget(static_cast<uint32_t>(cvar->m_intValue) << 20);
#endif
}

static void OnM2Cache(AwesomeCVar* cvar, bool changed) {
#if defined(WOWDECOMP_ARCH_64)
    // one callback for both: each runs at its own registration, before the other may be registered
    if (s_cvM2CacheTime.m_applied) {
        g_m2CacheKeepMs = static_cast<uint32_t>(s_cvM2CacheTime.m_intValue) * 1000;
    }

    if (s_cvM2CacheSize.m_applied) {
        // 0: no size cap, the time alone (stock)
        g_m2CacheMaxBytes = s_cvM2CacheSize.m_intValue ? static_cast<uint64_t>(s_cvM2CacheSize.m_intValue) << 20 : UINT64_MAX;
    }
#endif
}

static void OnWmoCacheTime(AwesomeCVar* cvar, bool changed) {
#if defined(WOWDECOMP_ARCH_64)
    CMapObj::s_flushTime = static_cast<float>(cvar->m_intValue);
#endif
}

// main-thread ms per frame in post-load callbacks by kind (texture, model, skin, anim, adt, wmo, wmo group, other),
// the worst frame's post-load ms, ms per frame and the longest blocked in waits, waits and loads per 5 s, reader-thread
// prepare ms per second, MB read per second, files queued, being read and read but not yet processed, reader threads,
// then textures waiting for the load budget and texture MB in flight; nothing on a 32-bit build
static int32_t Script_GetLoadTimings(lua_State* L) {
#if defined(WOWDECOMP_ARCH_64)
    const AsyncLoadStats& stats = AsyncFileReadGetStats();

    for (uint32_t i = 0; i < ASYNC_KIND_COUNT; i++) {
        lua_pushnumber(L, stats.m_postMs[i]);
    }

    lua_pushnumber(L, stats.m_postPeak);
    lua_pushnumber(L, stats.m_waitMs);
    lua_pushnumber(L, stats.m_waitPeak);
    lua_pushnumber(L, stats.m_waits);
    lua_pushnumber(L, stats.m_loads);
    lua_pushnumber(L, stats.m_prepareMs);
    lua_pushnumber(L, stats.m_readMBps);
    lua_pushnumber(L, stats.m_queued);
    lua_pushnumber(L, stats.m_reading);
    lua_pushnumber(L, stats.m_posted);
    lua_pushnumber(L, stats.m_threads);

    uint32_t texturesPending;
    uint32_t textureBytes;
    TextureGetLoadStats(texturesPending, textureBytes);
    lua_pushnumber(L, texturesPending);
    lua_pushnumber(L, static_cast<double>(textureBytes) / (1024.0 * 1024.0));

    return ASYNC_KIND_COUNT + 13;
#else
    return 0;
#endif
}

void AwesomeStreamingRegisterCVars() {
    AwesomeCVar* cvars[] = { &s_cvReadThreads, &s_cvPostloadBudget, &s_cvTextureLoadBudget, &s_cvM2CacheTime, &s_cvM2CacheSize, &s_cvWmoCacheTime };

    // the engine runs the callback at registration only for a saved value, so a CVar left at its default would never
    // reach the engine (one disk thread and the stock 100 ms budget until set); apply each once, the callbacks are
    // idempotent
    for (AwesomeCVar* cvar : cvars) {
        cvar->Register();
        cvar->Refresh();
    }
}

void AwesomeStreamingRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetLoadTimings", Script_GetLoadTimings },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}
