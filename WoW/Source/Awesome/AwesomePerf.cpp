// Frame time breakdown (not from awesome_wotlk): GetFrameTimings() and /frametimes, from the device's GxFrameStats.
// cpu: the game's own work per frame (Lua, UI, world, draw submission); gpuWait: gxFixLag waiting for the GPU;
// capWait: the maxFPS limiter; present: Present() (vsync and a full swap chain block here); frame: the whole frame.
// cpu near frame means the main thread is the limit; gpuWait or present large means it waits on the GPU or display.

#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/GxFrameLatency.h"

static int32_t Script_GetFrameTimings(lua_State* L) {
    lua_pushnumber(L, g_gxFrameStats.m_cpu);
    lua_pushnumber(L, g_gxFrameStats.m_gpuWait);
    lua_pushnumber(L, g_gxFrameStats.m_capWait);
    lua_pushnumber(L, g_gxFrameStats.m_present);
    lua_pushnumber(L, g_gxFrameStats.m_frame);
    return 5;
}

static const char* s_slashCommand = R"lua(
SLASH_FRAMETIMES1 = "/frametimes"
SlashCmdList.FRAMETIMES = function()
    local cpu, gpuWait, capWait, present, frame = GetFrameTimings()
    print(("|cff33ff99Frame|r %.2f ms (%.0f fps): cpu %.2f, gpu wait %.2f, fps cap %.2f, present %.2f"):format(
        frame, frame > 0 and 1000 / frame or 0, cpu, gpuWait, capWait, present))
end
)lua";

void AwesomePerfRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetFrameTimings", Script_GetFrameTimings },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}

void AwesomePerfOnEnterWorld() {
    FrameScript_Execute(s_slashCommand, "FrameTimes", nullptr);
}
