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
#include "WoW/Source/WorldClient/DetailDoodad.h"

static void OnSceneThreads(AwesomeCVar* cvar, bool changed);

// threads for parallel scene work, the main thread included: 0 = auto (logical processors - 1, at most 8), 1 = off
// (the stock path: M2UseThreads' two-way split)
static AwesomeCVar s_cvSceneThreads = AwesomeCVar::Int("sceneThreads", 0, 0, 16, OnSceneThreads);

static void OnSceneThreads(AwesomeCVar* cvar, bool changed) {
    ParallelSetThreads(static_cast<uint32_t>(cvar->m_intValue));
}

static void OnGroundEffectBuffers(AwesomeCVar* cvar, bool changed);

// baked grass buffer pairs per pool (stock 128, the only pool). More pools of this size are added as the grass in view
// needs them (CDetailDoodad::GrowPools); each pair is up to 4096 vertices, about 150 KB of video memory
static AwesomeCVar s_cvGroundEffectBuffers = AwesomeCVar::Int("groundEffectBuffers", 128, 32, 1024, OnGroundEffectBuffers);

static void OnGroundEffectBuffers(AwesomeCVar* cvar, bool changed) {
    if (!changed) {
        return;
    }

    CDetailDoodad::s_bufferPairs = static_cast<uint32_t>(cvar->m_intValue);
    // rebuilt at the next world render (CDetailDoodad::UpdatePools): the chunks' grass is regenerated
    CDetailDoodad::s_bPoolsDirty = 1;
}

// nothing on a device that cannot time its GPU; else the frame's GPU ms, then ms per section as GetSceneTimings
static int32_t Script_GetGpuTimings(lua_State* L) {
    if (!g_frameProfile.m_gpuValid) {
        return 0;
    }

    lua_pushnumber(L, g_frameProfile.m_gpuFrame);

    for (uint32_t i = 0; i < FRAMEPROFILE_COUNT; i++) {
        lua_pushnumber(L, g_frameProfile.m_gpuMs[i]);
    }

    return FRAMEPROFILE_COUNT + 1;
}

static int32_t Script_GetGroundEffectBuffers(lua_State* L) {
    lua_pushnumber(L, CDetailDoodad::s_poolPairs);
    return 1;
}

static int32_t Script_GetFrameTimings(lua_State* L) {
    lua_pushnumber(L, g_gxFrameStats.m_cpu);
    lua_pushnumber(L, g_gxFrameStats.m_gpuWait);
    lua_pushnumber(L, g_gxFrameStats.m_capWait);
    lua_pushnumber(L, g_gxFrameStats.m_present);
    lua_pushnumber(L, g_gxFrameStats.m_frame);
    return 5;
}

// ms per FrameProfileSection in enum order (unitUpdate, worldUpdate, cull, animParallel, animSerial, animBatch,
// terrain, wmo, worldOther, nameplates, models, detail, liquid, effects, shadows, grassFill, grassLights), then the counters in
// enum order (models animated, draw calls, grass chunks, grass draws, grass fills, grass streamed), then threads
static int32_t Script_GetSceneTimings(lua_State* L) {
    for (uint32_t i = 0; i < FRAMEPROFILE_COUNT; i++) {
        lua_pushnumber(L, g_frameProfile.m_ms[i]);
    }

    for (uint32_t i = 0; i < FRAMEPROFILE_COUNTERS; i++) {
        lua_pushnumber(L, g_frameProfile.m_counters[i]);
    }

    lua_pushnumber(L, ParallelThreadCount());
    return FRAMEPROFILE_COUNT + FRAMEPROFILE_COUNTERS + 1;
}

static const char* s_slashCommand = R"lua(
SLASH_FRAMETIMES1 = "/frametimes"
SlashCmdList.FRAMETIMES = function()
    local cpu, gpuWait, capWait, present, frame = GetFrameTimings()
    print(("|cff33ff99Frame|r %.2f ms (%.0f fps): cpu %.2f, gpu wait %.2f, fps cap %.2f, present %.2f"):format(
        frame, frame > 0 and 1000 / frame or 0, cpu, gpuWait, capWait, present))
    local units, update, cull, animMT, animST, batch, terrain, wmo, other, plates, models, detail, liquid, effects,
        shadows, grassFill, grassLights, animated, draws, grassChunks, grassDraws, grassFills, grassStreamed,
        threads = GetSceneTimings()
    print(("|cff33ff99World|r units %.2f, update %.2f, cull %.2f, animate %.2f (%d threads) + %.2f serial + %.2f batch; %.0f models"):format(
        units, update, cull, animMT, threads, animST, batch, animated))
    print(("|cff33ff99Draw|r terrain %.2f, wmo %.2f, models %.2f, grass %.2f, liquid %.2f, effects %.2f, shadows %.2f, plates %.2f, other %.2f; %.0f draw calls"):format(
        terrain, wmo, models, detail, liquid, effects, shadows, plates, other, draws))
    print(("|cff33ff99Grass|r %.0f chunks, %.0f draws; rebuilt %.0f (%.2f ms), %.0f streamed; lights %.2f ms; %d buffers"):format(
        grassChunks, grassDraws, grassFills, grassFill, grassStreamed, grassLights, GetGroundEffectBuffers()))
    local gpuFrame, _, _, _, _, _, _, gTerrain, gWmo, gOther, gPlates, gModels, gDetail, gLiquid, gEffects, gShadows = GetGpuTimings()
    if gpuFrame then
        local world = gTerrain + gWmo + gOther + gPlates + gModels + gDetail + gLiquid + gEffects + gShadows
        print(("|cff33ff99GPU|r %.2f ms: shadows %.2f, terrain %.2f, wmo %.2f, models %.2f, grass %.2f, liquid %.2f, effects %.2f, other %.2f, ui and rest %.2f"):format(
            gpuFrame, gShadows, gTerrain, gWmo, gModels, gDetail, gLiquid, gEffects, gOther + gPlates, math.max(gpuFrame - world, 0)))
    end
end
)lua";

void AwesomePerfRegisterCVars() {
    s_cvSceneThreads.Register();
    s_cvGroundEffectBuffers.Register();
}

void AwesomePerfRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetFrameTimings", Script_GetFrameTimings },
        { "GetSceneTimings", Script_GetSceneTimings },
        { "GetGroundEffectBuffers", Script_GetGroundEffectBuffers },
        { "GetGpuTimings", Script_GetGpuTimings },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}

void AwesomePerfOnEnterWorld() {
    FrameScript_Execute(s_slashCommand, "FrameTimes", nullptr);
}
