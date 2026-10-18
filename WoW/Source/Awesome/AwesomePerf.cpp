// Frame time breakdown (not from awesome_wotlk): GetFrameTimings() and /frametimes, from the device's GxFrameStats.
// cpu: the game's own work per frame (Lua, UI, world, draw submission); gpuWait: gxFixLag waiting for the GPU;
// capWait: the maxFPS limiter; present: Present() (vsync and a full swap chain block here); frame: the whole frame.
// cpu near frame means the main thread is the limit; gpuWait or present large means it waits on the GPU or display.
//
// Multithreaded scene (not from awesome_wotlk): the sceneThreads CVar sizes the engine's scene worker pool
// (ParallelFor.h), and GetSceneTimings() breaks the world part of cpu down by section (FrameProfile.h).

#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "Engine/Source/Base/FrameProfile.h"
#include "Engine/Source/Base/ParallelFor.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/GxFrameLatency.h"

static void OnSceneThreads(AwesomeCVar* cvar, bool changed);

// threads for parallel scene work, the main thread included: 0 = auto (logical processors - 1, at most 8), 1 = off
// (the stock path: M2UseThreads' two-way split)
static AwesomeCVar s_cvSceneThreads = AwesomeCVar::Int("sceneThreads", 0, 0, 16, OnSceneThreads);

static void OnSceneThreads(AwesomeCVar* cvar, bool changed) {
    ParallelSetThreads(static_cast<uint32_t>(cvar->m_intValue));
}

static int32_t Script_GetFrameTimings(lua_State* L) {
    lua_pushnumber(L, g_gxFrameStats.m_cpu);
    lua_pushnumber(L, g_gxFrameStats.m_gpuWait);
    lua_pushnumber(L, g_gxFrameStats.m_capWait);
    lua_pushnumber(L, g_gxFrameStats.m_present);
    lua_pushnumber(L, g_gxFrameStats.m_frame);
    return 5;
}

// unitUpdate, worldUpdate, cull, animParallel, animSerial, animBatch, draw, post (ms), models, threads
static int32_t Script_GetSceneTimings(lua_State* L) {
    for (uint32_t i = 0; i < FRAMEPROFILE_COUNT; i++) {
        lua_pushnumber(L, g_frameProfile.m_ms[i]);
    }

    lua_pushnumber(L, g_frameProfile.m_counters[FRAMEPROFILE_MODELS]);
    lua_pushnumber(L, ParallelThreadCount());
    return FRAMEPROFILE_COUNT + 2;
}

static const char* s_slashCommand = R"lua(
SLASH_FRAMETIMES1 = "/frametimes"
SlashCmdList.FRAMETIMES = function()
    local cpu, gpuWait, capWait, present, frame = GetFrameTimings()
    print(("|cff33ff99Frame|r %.2f ms (%.0f fps): cpu %.2f, gpu wait %.2f, fps cap %.2f, present %.2f"):format(
        frame, frame > 0 and 1000 / frame or 0, cpu, gpuWait, capWait, present))
    local units, update, cull, animMT, animST, batch, draw, post, models, threads = GetSceneTimings()
    print(("|cff33ff99World|r units %.2f, update %.2f, cull %.2f, animate %.2f (%d threads) + %.2f serial + %.2f batch, draw %.2f, post %.2f; %.0f models"):format(
        units, update, cull, animMT, threads, animST, batch, draw, post, models))
end
)lua";

void AwesomePerfRegisterCVars() {
    s_cvSceneThreads.Register();
}

void AwesomePerfRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetFrameTimings", Script_GetFrameTimings },
        { "GetSceneTimings", Script_GetSceneTimings },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}

void AwesomePerfOnEnterWorld() {
    FrameScript_Execute(s_slashCommand, "FrameTimes", nullptr);
}
