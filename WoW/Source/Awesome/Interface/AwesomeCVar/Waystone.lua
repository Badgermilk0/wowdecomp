-- Waystone: the client's settings window beyond awesome_wotlk's own. The window is built into the client
-- (Awesome/AwesomeEmbeddedUi.cpp), so every setting here is a CVar the client keeps in Config.wtf (or, for the
-- account settings, in its account data): changed here, it stays changed. Loaded after Constants.lua and before UI.lua;
-- its strings are in Locales/Waystone_enUS.lua. The tabs and sections below hold the settings that the stock options
-- panels leave out or cap (view distance, camera speeds, screenshots, ...), and the ones the Waystone client adds.
local _, ACVar = ...
local L = ACVar.L

local CVARS = ACVar.CVARS

-- five more tabs
ACVar.CONSTANTS.FRAME.MAIN_WIDTH = 980

local REQUIRES = {
    d3d11 = { reason = L.W_REASON_D3D11, test = function(get) return tostring(get("gxApi") or ""):lower() == "d3d11" end },
    hdr = { reason = L.W_REASON_HDR, test = function(get) return get("hdr") ~= 0 end },
    dynamic = { reason = L.W_REASON_DYNAMIC, test = function(get) return get("lightDynamic") ~= 0 end },
    specular = { reason = L.W_REASON_SPECULAR, test = function(get) return get("lightSpecular") ~= 0 end },
    ssao = { reason = L.W_REASON_SSAO, test = function(get) return (tonumber(get("ssao")) or 0) > 0 end },
    heightFog = { reason = L.W_REASON_HEIGHT_FOG, test = function(get) return (tonumber(get("fogHeight")) or 0) > 0 end },
    upscale = { reason = L.W_REASON_UPSCALE, test = function(get) return get("uiUpscale") ~= 0 end },
    soundOcclusion = { reason = L.W_REASON_SOUND_OCCLUSION, test = function(get) return get("Sound_EnableOcclusion") ~= 0 end },
}

-- gxMonitor's choices: the monitors connected when the UI loaded (GetMonitorName is the client's)
local monitorOptions = { [0] = L.W_MONITOR_LAST }

if GetMonitorCount and GetMonitorName then
    for i = 1, GetMonitorCount() do
        monitorOptions[i] = GetMonitorName(i)
    end
end

CVARS.Display = {
    { name = "wHeaderWindow", label = L.W_SECTION_WINDOW, type = "header" },
    { name = "wNoteAltEnter", label = L.W_LABEL_ALT_ENTER, desc = L.W_DESC_ALT_ENTER, type = "description" },
    { name = "gxMonitor", label = L.W_LABEL_MONITOR, desc = L.W_DESC_MONITOR, perf = L.PERF_NONE, type = "dropdown", default = 0, options = monitorOptions },
    { name = "wHeaderUiScale", label = L.W_SECTION_UI_SCALE, type = "header" },
    { name = "uiScaleDpi", label = L.W_LABEL_UI_SCALE_DPI, desc = L.W_DESC_UI_SCALE_DPI, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 0 },
    { name = "uiMaxAspect", label = L.W_LABEL_UI_MAX_ASPECT, desc = L.W_DESC_UI_MAX_ASPECT, perf = L.PERF_NONE, type = "mode", default = 0, modes = {
        { value = 0, label = L.W_MODE_FULL_WIDTH },
        { value = 1.78, label = L.W_MODE_ASPECT_16_9 },
        { value = 2.37, label = L.W_MODE_ASPECT_21_9 },
    }},
    { name = "uiLiveResize", label = L.W_LABEL_UI_LIVE_RESIZE, desc = L.W_DESC_UI_LIVE_RESIZE, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "wNoteUiScale", label = L.W_LABEL_UI_SCALE_FLOOR, desc = L.W_DESC_UI_SCALE_FLOOR, type = "description" },
    { name = "wHeaderUiArt", label = L.W_SECTION_UI_ART, type = "header" },
    { name = "uiUpscale", label = L.W_LABEL_UI_UPSCALE, desc = L.W_DESC_UI_UPSCALE, perf = L.PERF_NEGLIGIBLE, type = "mode", default = 2, modes = {
        { value = 0, label = L.W_MODE_ENGINE },
        { value = 1, label = L.W_MODE_BICUBIC },
        { value = 2, label = L.W_MODE_EDGE_ADAPTIVE },
    }},
    { name = "uiUpscaleEdges", label = L.W_LABEL_UI_UPSCALE_EDGES, desc = L.W_DESC_UI_UPSCALE_EDGES, perf = L.PERF_NEGLIGIBLE, type = "slider", min = 0, max = 1, step = 0.05, default = 1,
        requires = REQUIRES.upscale },
    { name = "fontMaxPixelSize", label = L.W_LABEL_FONT_MAX_PIXEL, desc = L.W_DESC_FONT_MAX_PIXEL, perf = L.PERF_NEGLIGIBLE, type = "slider", min = 32, max = 256, step = 16, default = 128 },
    { name = "wNoteFontFallback", label = L.W_LABEL_FONT_FALLBACK, desc = L.W_DESC_FONT_FALLBACK, type = "description" },
    { name = "wHeaderPortraits", label = L.W_SECTION_PORTRAITS, type = "header" },
    { name = "portraitLive", label = L.W_LABEL_PORTRAIT_LIVE, desc = L.W_DESC_PORTRAIT_LIVE, perf = L.W_PERF_PORTRAIT_LIVE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "minimapIndoorResolution", label = L.W_LABEL_MINIMAP_INDOOR, desc = L.W_DESC_MINIMAP_INDOOR, perf = L.PERF_NEGLIGIBLE, type = "dropdown", default = 1024, options = {
        [256] = "256", [512] = "512", [1024] = "1024", [2048] = "2048",
    }},
    { name = "wHeaderCharacter", label = L.W_SECTION_CHARACTER, type = "header" },
    { name = "cameraPlayerFade", label = L.W_LABEL_PLAYER_FADE, desc = L.W_DESC_PLAYER_FADE, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 0 },
    { name = "chatWhileDead", label = L.W_LABEL_CHAT_WHILE_DEAD, desc = L.W_DESC_CHAT_WHILE_DEAD, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
}

CVARS.Lighting = {
    { name = "wNoteLighting", label = L.W_LABEL_LIGHTING_NOTE, desc = L.W_DESC_LIGHTING_NOTE, type = "description" },
    { name = "wHeaderHdr", label = L.W_SECTION_HDR, type = "header" },
    { name = "hdr", label = L.W_LABEL_HDR, desc = L.W_DESC_HDR, perf = L.W_PERF_LOW, type = "toggle", min = 0, max = 1, default = 0, requires = REQUIRES.d3d11 },
    { name = "hdrExposure", label = L.W_LABEL_EXPOSURE, desc = L.W_DESC_EXPOSURE, perf = L.PERF_NONE, type = "slider", min = 0.25, max = 4, step = 0.05, default = 1, requires = REQUIRES.hdr },
    { name = "hdrTonemap", label = L.W_LABEL_TONEMAP, desc = L.W_DESC_TONEMAP, perf = L.PERF_NONE, type = "mode", default = 1, requires = REQUIRES.hdr, modes = {
        { value = 0, label = L.W_MODE_TONEMAP_CLAMP },
        { value = 1, label = L.W_MODE_TONEMAP_NEUTRAL },
        { value = 2, label = L.W_MODE_TONEMAP_ACES },
    }},
    { name = "bloom", label = L.W_LABEL_BLOOM, desc = L.W_DESC_BLOOM, perf = L.W_PERF_LOW, type = "slider", min = 0, max = 2, step = 0.05, default = 0, requires = REQUIRES.hdr },
    { name = "bloomThreshold", label = L.W_LABEL_BLOOM_THRESHOLD, desc = L.W_DESC_BLOOM_THRESHOLD, perf = L.PERF_NONE, type = "slider", min = 0.1, max = 8, step = 0.1, default = 1, requires = REQUIRES.hdr },
    { name = "sharpen", label = L.W_LABEL_SHARPEN, desc = L.W_DESC_SHARPEN, perf = L.W_PERF_LOW, type = "slider", min = 0, max = 1, step = 0.05, default = 0, requires = REQUIRES.hdr },
    { name = "wHeaderLights", label = L.W_SECTION_LIGHTS, type = "header" },
    { name = "lightPerPixel", label = L.W_LABEL_PER_PIXEL, desc = L.W_DESC_PER_PIXEL, perf = L.W_PERF_LOW, type = "toggle", min = 0, max = 1, default = 0, requires = REQUIRES.d3d11 },
    { name = "lightWrap", label = L.W_LABEL_WRAP, desc = L.W_DESC_WRAP, perf = L.PERF_NONE, type = "slider", min = 0, max = 1, step = 0.05, default = 0.25, requires = REQUIRES.d3d11 },
    { name = "lightSkyAmbient", label = L.W_LABEL_SKY_AMBIENT, desc = L.W_DESC_SKY_AMBIENT, perf = L.PERF_NONE, type = "slider", min = 0, max = 1, step = 0.05, default = 0, requires = REQUIRES.d3d11 },
    { name = "lightDynamic", label = L.W_LABEL_DYNAMIC, desc = L.W_DESC_DYNAMIC, perf = L.W_PERF_MEDIUM, type = "toggle", min = 0, max = 1, default = 0, requires = REQUIRES.d3d11 },
    { name = "lightDynamicIntensity", label = L.W_LABEL_DYNAMIC_INTENSITY, desc = L.W_DESC_DYNAMIC_INTENSITY, perf = L.PERF_NONE, type = "slider", min = 0, max = 8, step = 0.1, default = 1, requires = REQUIRES.dynamic },
    { name = "lightDynamicM2", label = L.W_LABEL_DYNAMIC_M2, desc = L.W_DESC_DYNAMIC_M2, perf = L.PERF_NONE, type = "slider", min = 0, max = 8, step = 0.1, default = 0.6, requires = REQUIRES.dynamic },
    { name = "lightDynamicWmo", label = L.W_LABEL_DYNAMIC_WMO, desc = L.W_DESC_DYNAMIC_WMO, perf = L.PERF_NONE, type = "slider", min = 0, max = 8, step = 0.1, default = 0.5, requires = REQUIRES.dynamic },
    { name = "lightDynamicDistance", label = L.W_LABEL_DYNAMIC_DISTANCE, desc = L.W_DESC_DYNAMIC_DISTANCE, perf = L.W_PERF_DYNAMIC_DISTANCE, type = "slider", min = 20, max = 500, step = 10, default = 100, requires = REQUIRES.dynamic },
    { name = "lightFire", label = L.W_LABEL_FIRE, desc = L.W_DESC_FIRE, perf = L.W_PERF_LOW, type = "slider", min = 0, max = 4, step = 0.1, default = 0, requires = REQUIRES.dynamic },
    { name = "lightFlicker", label = L.W_LABEL_FLICKER, desc = L.W_DESC_FLICKER, perf = L.PERF_NONE, type = "slider", min = 0, max = 1, step = 0.05, default = 0, requires = REQUIRES.dynamic },
    { name = "lightSpecular", label = L.W_LABEL_SPECULAR, desc = L.W_DESC_SPECULAR, perf = L.W_PERF_LOW, type = "toggle", min = 0, max = 1, default = 0, requires = REQUIRES.d3d11 },
    { name = "lightSpecularM2", label = L.W_LABEL_SPECULAR_M2, desc = L.W_DESC_SPECULAR_M2, perf = L.PERF_NONE, type = "slider", min = 0, max = 4, step = 0.1, default = 0.3, requires = REQUIRES.specular },
    { name = "lightSpecularWmo", label = L.W_LABEL_SPECULAR_WMO, desc = L.W_DESC_SPECULAR_WMO, perf = L.PERF_NONE, type = "slider", min = 0, max = 4, step = 0.1, default = 1, requires = REQUIRES.specular },
    { name = "wHeaderSsao", label = L.W_SECTION_SSAO, type = "header" },
    { name = "ssao", label = L.W_LABEL_SSAO, desc = L.W_DESC_SSAO, perf = L.W_PERF_MEDIUM, type = "slider", min = 0, max = 2, step = 0.05, default = 0, requires = REQUIRES.d3d11 },
    { name = "ssaoRadius", label = L.W_LABEL_SSAO_RADIUS, desc = L.W_DESC_SSAO_RADIUS, perf = L.PERF_NONE, type = "slider", min = 0.25, max = 8, step = 0.25, default = 1.5, requires = REQUIRES.ssao },
    { name = "ssaoDistance", label = L.W_LABEL_SSAO_DISTANCE, desc = L.W_DESC_SSAO_DISTANCE, perf = L.PERF_NONE, type = "slider", min = 20, max = 2000, step = 10, default = 200, requires = REQUIRES.ssao },
    { name = "wHeaderFog", label = L.W_SECTION_FOG, type = "header" },
    { name = "fogHeight", label = L.W_LABEL_FOG_HEIGHT, desc = L.W_DESC_FOG_HEIGHT, perf = L.W_PERF_LOW, type = "slider", min = 0, max = 0.02, step = 0.0005, default = 0, requires = REQUIRES.d3d11 },
    { name = "fogHeightFalloff", label = L.W_LABEL_FOG_FALLOFF, desc = L.W_DESC_FOG_FALLOFF, perf = L.PERF_NONE, type = "slider", min = 0.005, max = 0.5, step = 0.005, default = 0.05, requires = REQUIRES.heightFog },
    { name = "fogHeightOffset", label = L.W_LABEL_FOG_OFFSET, desc = L.W_DESC_FOG_OFFSET, perf = L.PERF_NONE, type = "slider", min = -100, max = 100, step = 1, default = -5, requires = REQUIRES.heightFog },
    { name = "fogHeightMax", label = L.W_LABEL_FOG_MAX, desc = L.W_DESC_FOG_MAX, perf = L.PERF_NONE, type = "slider", min = 0, max = 1, step = 0.05, default = 0.85, requires = REQUIRES.heightFog },
    { name = "fogScattering", label = L.W_LABEL_FOG_SCATTERING, desc = L.W_DESC_FOG_SCATTERING, perf = L.PERF_NONE, type = "slider", min = 0, max = 1, step = 0.05, default = 0, requires = REQUIRES.d3d11 },
    { name = "wHeaderShadows", label = L.W_SECTION_SHADOWS, type = "header" },
    { name = "shadowMapSize", label = L.W_LABEL_SHADOW_MAP, desc = L.W_DESC_SHADOW_MAP, perf = L.W_PERF_SHADOW_MAP, type = "dropdown", default = 4096, options = {
        [0] = L.W_OPTION_STOCK, [1024] = "1024", [2048] = "2048", [4096] = "4096", [8192] = "8192",
    }},
    { name = "shadowSoftness", label = L.W_LABEL_SHADOW_SOFTNESS, desc = L.W_DESC_SHADOW_SOFTNESS, perf = L.PERF_NONE, type = "slider", min = 0.25, max = 4, step = 0.05, default = 1 },
    { name = "shadowStable", label = L.W_LABEL_SHADOW_STABLE, desc = L.W_DESC_SHADOW_STABLE, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "shadowCascadeReuse", label = L.W_LABEL_SHADOW_REUSE, desc = L.W_DESC_SHADOW_REUSE, perf = L.W_PERF_SAVES, type = "toggle", min = 0, max = 1, default = 1 },
}

-- The world as drawn: view distance and detail beyond the stock Video options' sliders (the client takes the HD
-- client's limits: farclip 10000, groundEffectDist 3166), effects, textures and the frame rate caps
CVARS.World = {
    { name = "wNoteWorld", label = L.W_LABEL_WORLD_NOTE, desc = L.W_DESC_WORLD_NOTE, type = "description" },
    { name = "wHeaderDistance", label = L.W_SECTION_DISTANCE, type = "header" },
    { name = "farclip", label = L.W_LABEL_FARCLIP, desc = L.W_DESC_FARCLIP, perf = L.W_PERF_FARCLIP, type = "slider", min = 177, max = 5000, step = 25, default = 350 },
    { name = "horizonFarclipScale", label = L.W_LABEL_HORIZON, desc = L.W_DESC_HORIZON, perf = L.W_PERF_LOW, type = "slider", min = 3, max = 12, step = 0.5, default = 4 },
    { name = "environmentDetail", label = L.W_LABEL_ENV_DETAIL, desc = L.W_DESC_ENV_DETAIL, perf = L.W_PERF_MEDIUM, type = "slider", min = 0.5, max = 3, step = 0.05, default = 1 },
    { name = "groundEffectDist", label = L.W_LABEL_GRASS_DIST, desc = L.W_DESC_GRASS_DIST, perf = L.W_PERF_MEDIUM, type = "slider", min = 0, max = 600, step = 10, default = 70 },
    { name = "groundEffectDensity", label = L.W_LABEL_GRASS_DENSITY, desc = L.W_DESC_GRASS_DENSITY, perf = L.W_PERF_MEDIUM, type = "slider", min = 16, max = 256, step = 8, default = 16 },
    { name = "objectFade", label = L.W_LABEL_OBJECT_FADE, desc = L.W_DESC_OBJECT_FADE, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "wHeaderEffects", label = L.W_SECTION_EFFECTS, type = "header" },
    { name = "spellEffectLevel", label = L.W_LABEL_SPELL_EFFECTS, desc = L.W_DESC_SPELL_EFFECTS, perf = L.W_PERF_LOW, type = "slider", min = 0, max = 9, step = 1, default = 9 },
    { name = "particleDensity", label = L.W_LABEL_PARTICLES, desc = L.W_DESC_PARTICLES, perf = L.W_PERF_LOW, type = "slider", min = 0.1, max = 1, step = 0.05, default = 1 },
    { name = "lootSparkleAmount", label = L.W_LABEL_SPARKLE_AMOUNT, desc = L.W_DESC_SPARKLE_AMOUNT, perf = L.PERF_NEGLIGIBLE, type = "slider", min = 0, max = 5, step = 0.1, default = 1 },
    { name = "lootSparkleBrightness", label = L.W_LABEL_SPARKLE_BRIGHTNESS, desc = L.W_DESC_SPARKLE_BRIGHTNESS, perf = L.PERF_NONE, type = "slider", min = 0, max = 3, step = 0.1, default = 1 },
    { name = "lootSparkleSize", label = L.W_LABEL_SPARKLE_SIZE, desc = L.W_DESC_SPARKLE_SIZE, perf = L.PERF_NONE, type = "slider", min = 0.25, max = 3, step = 0.05, default = 1 },
    { name = "weatherDensity", label = L.W_LABEL_WEATHER, desc = L.W_DESC_WEATHER, perf = L.W_PERF_LOW, type = "mode", default = 2, modes = {
        { value = 0, label = L.MODE_DISABLED },
        { value = 1, label = L.W_MODE_LOW },
        { value = 2, label = L.W_MODE_MEDIUM },
        { value = 3, label = L.W_MODE_HIGH },
    }},
    { name = "SkyCloudLOD", label = L.W_LABEL_CLOUDS, desc = L.W_DESC_CLOUDS, perf = L.W_PERF_LOW, type = "slider", min = 0, max = 3, step = 1, default = 0 },
    { name = "showfootprints", label = L.W_LABEL_FOOTPRINTS, desc = L.W_DESC_FOOTPRINTS, perf = L.PERF_NEGLIGIBLE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "showfootprintparticles", label = L.W_LABEL_FOOTPRINT_DUST, desc = L.W_DESC_FOOTPRINT_DUST, perf = L.PERF_NEGLIGIBLE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "ffxGlow", label = L.W_LABEL_FFX_GLOW, desc = L.W_DESC_FFX_GLOW, perf = L.W_PERF_LOW, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "ffxDeath", label = L.W_LABEL_FFX_DEATH, desc = L.W_DESC_FFX_DEATH, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "ffxNetherWorld", label = L.W_LABEL_FFX_NETHER, desc = L.W_DESC_FFX_NETHER, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "wHeaderTextures", label = L.W_SECTION_TEXTURES, type = "header" },
    { name = "textureFilteringMode", label = L.W_LABEL_FILTERING, desc = L.W_DESC_FILTERING, perf = L.W_PERF_LOW, type = "mode", default = 1, modes = {
        { value = 0, label = L.W_MODE_BILINEAR },
        { value = 1, label = L.W_MODE_TRILINEAR },
        { value = 2, label = "2x" },
        { value = 3, label = "4x" },
        { value = 4, label = "8x" },
        { value = 5, label = "16x" },
    }},
    { name = "texLodBias", label = L.W_LABEL_TEX_LOD_BIAS, desc = L.W_DESC_TEX_LOD_BIAS, perf = L.PERF_NEGLIGIBLE, type = "slider", min = -1, max = 1, step = 0.05, default = 0, requires = REQUIRES.d3d11 },
    { name = "baseMip", label = L.W_LABEL_BASE_MIP, desc = L.W_DESC_BASE_MIP, perf = L.W_PERF_MEMORY, type = "mode", default = 0, modes = {
        { value = 0, label = L.W_MODE_HIGH },
        { value = 1, label = L.W_MODE_LOW },
    }},
    { name = "componentTextureLevel", label = L.W_LABEL_CHAR_TEXTURES, desc = L.W_DESC_CHAR_TEXTURES, perf = L.W_PERF_MEMORY, type = "mode", default = 8, modes = {
        { value = 8, label = L.W_MODE_NORMAL },
        { value = 9, label = L.W_MODE_HIGH },
    }},
    { name = "projectedTextures", label = L.W_LABEL_PROJECTED, desc = L.W_DESC_PROJECTED, perf = L.W_PERF_LOW, type = "toggle", min = 0, max = 1, default = 0 },
    { name = "wHeaderFrameRate", label = L.W_SECTION_FRAME_RATE, type = "header" },
    { name = "maxFPS", label = L.W_LABEL_MAX_FPS, desc = L.W_DESC_MAX_FPS, perf = L.PERF_NONE, type = "slider", min = 0, max = 360, step = 5, default = 200 },
    { name = "maxFPSBk", label = L.W_LABEL_MAX_FPS_BK, desc = L.W_DESC_MAX_FPS_BK, perf = L.PERF_NONE, type = "slider", min = 0, max = 240, step = 5, default = 30 },
}

CVARS.Sound = {
    { name = "wHeaderOcclusion", label = L.W_SECTION_SOUND_OCCLUSION, type = "header" },
    { name = "Sound_EnableOcclusion", label = L.W_LABEL_SOUND_OCCLUSION, desc = L.W_DESC_SOUND_OCCLUSION, perf = L.W_PERF_LOW, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "Sound_OcclusionStrength", label = L.W_LABEL_SOUND_OCCLUSION_STRENGTH, desc = L.W_DESC_SOUND_OCCLUSION_STRENGTH, perf = L.PERF_NONE, type = "slider", min = 0, max = 1, step = 0.05, default = 0.7,
        requires = REQUIRES.soundOcclusion },
    { name = "Sound_OcclusionTests", label = L.W_LABEL_SOUND_OCCLUSION_TESTS, desc = L.W_DESC_SOUND_OCCLUSION_TESTS, perf = L.W_PERF_LOW, type = "slider", min = 1, max = 64, step = 1, default = 8,
        requires = REQUIRES.soundOcclusion },
    { name = "Sound_OcclusionFade", label = L.W_LABEL_SOUND_OCCLUSION_FADE, desc = L.W_DESC_SOUND_OCCLUSION_FADE, perf = L.PERF_NONE, type = "slider", min = 0, max = 2, step = 0.05, default = 0.25,
        requires = REQUIRES.soundOcclusion },
    { name = "wHeaderReverb", label = L.W_SECTION_REVERB, type = "header" },
    { name = "Sound_ReverbFade", label = L.W_LABEL_REVERB_FADE, desc = L.W_DESC_REVERB_FADE, perf = L.PERF_NONE, type = "slider", min = 0, max = 10, step = 0.1, default = 1.5 },
    { name = "Sound_ReverbFallback", label = L.W_LABEL_REVERB_FALLBACK, desc = L.W_DESC_REVERB_FALLBACK, perf = L.PERF_NEGLIGIBLE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "wHeaderVoices", label = L.W_SECTION_VOICES, type = "header" },
    { name = "Sound_NumChannels", label = L.W_LABEL_NUM_CHANNELS, desc = L.W_DESC_NUM_CHANNELS, perf = L.W_PERF_LOW, type = "slider", min = 32, max = 256, step = 8, default = 64 },
    { name = "Sound_KeepVirtualLoops", label = L.W_LABEL_VIRTUAL_LOOPS, desc = L.W_DESC_VIRTUAL_LOOPS, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "wHeaderSoundMore", label = L.W_SECTION_SOUND_MORE, type = "header" },
    { name = "Sound_ZoneMusicNoDelay", label = L.W_LABEL_MUSIC_NO_DELAY, desc = L.W_DESC_MUSIC_NO_DELAY, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 0 },
    { name = "Sound_EnableArmorFoleySoundForSelf", label = L.W_LABEL_FOLEY_SELF, desc = L.W_DESC_FOLEY, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "Sound_EnableArmorFoleySoundForOthers", label = L.W_LABEL_FOLEY_OTHERS, desc = L.W_DESC_FOLEY, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "Sound_ListenerAtCharacter", label = L.W_LABEL_LISTENER, desc = L.W_DESC_LISTENER, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
}

CVARS.Performance = {
    { name = "wNotePerformance", label = L.W_LABEL_PERF_NOTE, desc = L.W_DESC_PERF_NOTE, type = "description" },
    { name = "wHeaderCpu", label = L.W_SECTION_CPU, type = "header" },
    { name = "sceneThreads", label = L.W_LABEL_SCENE_THREADS, desc = L.W_DESC_SCENE_THREADS, perf = L.W_PERF_SAVES, type = "slider", min = 0, max = 16, step = 1, default = 0 },
    { name = "gxMaxFrameLatency", label = L.W_LABEL_FRAME_LATENCY, desc = L.W_DESC_FRAME_LATENCY, perf = L.W_PERF_SAVES, type = "slider", min = 0, max = 3, step = 1, default = 1 },
    { name = "groundEffectBuffers", label = L.W_LABEL_GRASS_BUFFERS, desc = L.W_DESC_GRASS_BUFFERS, perf = L.W_PERF_SAVES, type = "slider", min = 32, max = 1024, step = 32, default = 128 },
    { name = "wHeaderStreaming", label = L.W_SECTION_STREAMING, type = "header" },
    { name = "asyncReadThreads", label = L.W_LABEL_READ_THREADS, desc = L.W_DESC_READ_THREADS, perf = L.W_PERF_SAVES, type = "slider", min = 0, max = 8, step = 1, default = 0 },
    { name = "asyncPostloadBudget", label = L.W_LABEL_POSTLOAD_BUDGET, desc = L.W_DESC_POSTLOAD_BUDGET, perf = L.W_PERF_SAVES, type = "slider", min = 0, max = 100, step = 1, default = 8 },
    { name = "textureLoadBudget", label = L.W_LABEL_TEXTURE_BUDGET, desc = L.W_DESC_TEXTURE_BUDGET, perf = L.W_PERF_SAVES, type = "slider", min = 4, max = 1024, step = 4, default = 64 },
    { name = "wHeaderCaches", label = L.W_SECTION_CACHES, type = "header" },
    { name = "M2CacheTime", label = L.W_LABEL_M2_CACHE_TIME, desc = L.W_DESC_M2_CACHE_TIME, perf = L.W_PERF_MEMORY, type = "slider", min = 10, max = 3600, step = 10, default = 120 },
    { name = "M2CacheSize", label = L.W_LABEL_M2_CACHE_SIZE, desc = L.W_DESC_M2_CACHE_SIZE, perf = L.W_PERF_MEMORY, type = "slider", min = 0, max = 4096, step = 64, default = 256 },
    { name = "wmoCacheTime", label = L.W_LABEL_WMO_CACHE_TIME, desc = L.W_DESC_WMO_CACHE_TIME, perf = L.W_PERF_MEMORY, type = "slider", min = 10, max = 3600, step = 10, default = 120 },
}

-- Upstream entries whose defaults or choices differ in Waystone
for _, cvarDef in ipairs(CVARS.Rendering) do
    if cvarDef.name == "portraitResolution" then
        cvarDef.default = 256
    end
end

local CAMERA = {
    { name = "cameraDistanceMaxFactor", label = L.W_LABEL_CAMERA_FACTOR, desc = L.W_DESC_CAMERA_FACTOR, perf = L.PERF_NONE, type = "slider", min = 1, max = 4, step = 0.1, default = 1 },
    { name = "cameraYawMoveSpeed", label = L.W_LABEL_CAMERA_YAW, desc = L.W_DESC_CAMERA_YAW, perf = L.PERF_NONE, type = "slider", min = 30, max = 360, step = 5, default = 180 },
    { name = "cameraPitchMoveSpeed", label = L.W_LABEL_CAMERA_PITCH, desc = L.W_DESC_CAMERA_PITCH, perf = L.PERF_NONE, type = "slider", min = 30, max = 360, step = 5, default = 90 },
    { name = "cameraDistanceMoveSpeed", label = L.W_LABEL_CAMERA_ZOOM, desc = L.W_DESC_CAMERA_ZOOM, perf = L.PERF_NONE, type = "slider", min = 1, max = 50, step = 0.5, default = 8.33 },
    { name = "cameraWaterCollision", label = L.W_LABEL_CAMERA_WATER, desc = L.W_DESC_CAMERA_WATER, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "cameraPivot", label = L.W_LABEL_CAMERA_PIVOT, desc = L.W_DESC_CAMERA_PIVOT, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "cameraDive", label = L.W_LABEL_CAMERA_DIVE, desc = L.W_DESC_CAMERA_DIVE, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 },
    { name = "cameraBobbing", label = L.W_LABEL_CAMERA_BOBBING, desc = L.W_DESC_CAMERA_BOBBING, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 0 },
}

for i, cvarDef in ipairs(CVARS.Rendering) do
    if cvarDef.name == "showPlayer" then
        for j, camera in ipairs(CAMERA) do
            table.insert(CVARS.Rendering, i + j, camera)
        end

        break
    end
end

-- screenshots and troubleshooting, in Other
local OTHER_SCREENSHOTS = {
    { name = "screenshotFormat", label = L.W_LABEL_SCREENSHOT_FORMAT, desc = L.W_DESC_SCREENSHOT_FORMAT, perf = L.PERF_NONE, type = "mode", default = "jpeg", modes = {
        { value = "jpeg", label = "JPEG" },
        { value = "tga", label = L.W_MODE_TGA },
    }},
    { name = "screenshotQuality", label = L.W_LABEL_SCREENSHOT_QUALITY, desc = L.W_DESC_SCREENSHOT_QUALITY, perf = L.PERF_NONE, type = "slider", min = 1, max = 10, step = 1, default = 3 },
}

for i, cvarDef in ipairs(CVARS.Other) do
    if cvarDef.name == "headerScreenshots" then
        for j, entry in ipairs(OTHER_SCREENSHOTS) do
            table.insert(CVARS.Other, i + j, entry)
        end

        break
    end
end

table.insert(CVARS.Other, { name = "wHeaderTroubleshooting", label = L.W_SECTION_TROUBLESHOOTING, type = "header" })
table.insert(CVARS.Other, { name = "scriptErrors", label = L.W_LABEL_SCRIPT_ERRORS, desc = L.W_DESC_SCRIPT_ERRORS, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 0 })
table.insert(CVARS.Other, { name = "taintLog", label = L.W_LABEL_TAINT_LOG, desc = L.W_DESC_TAINT_LOG, perf = L.W_PERF_LOW, type = "mode", default = 0, modes = {
    { value = 0, label = L.MODE_DISABLED },
    { value = 1, label = L.W_MODE_TAINT_BLOCKED },
    { value = 2, label = L.W_MODE_TAINT_ALL },
}})
table.insert(CVARS.Other, { name = "awesomeBuiltInAddOns", label = L.W_LABEL_BUILT_IN, desc = L.W_DESC_BUILT_IN, perf = L.PERF_NONE, type = "toggle", min = 0, max = 1, default = 1 })

local nameplates = CVARS.Nameplates

for i, cvarDef in ipairs(nameplates) do
    if cvarDef.name == "nameplateAlphaSpeed" then
        table.insert(nameplates, i + 1, { name = "nameplateAllCastBars", label = L.W_LABEL_ALL_CAST_BARS, desc = L.W_DESC_ALL_CAST_BARS, perf = L.PERF_NEGLIGIBLE,
            type = "toggle", min = 0, max = 1, default = 1 })
        break
    end
end

-- Tabs: the new ones at the end. Upstream's About tab (the mod, its version and links) is left out: this window is
-- part of the client
CVARS.About = nil
ACVar.CATEGORY_NAMES.About = nil

local order = ACVar.CATEGORY_ORDER

for i = #order, 1, -1 do
    if order[i] == "About" then
        table.remove(order, i)
    end
end

table.insert(order, "World")
table.insert(order, "Display")
table.insert(order, "Lighting")
table.insert(order, "Sound")
table.insert(order, "Performance")

ACVar.DEFAULT_CATEGORY = "World"

ACVar.CATEGORY_NAMES.World = L.W_CATEGORY_WORLD
ACVar.CATEGORY_NAMES.Display = L.W_CATEGORY_DISPLAY
ACVar.CATEGORY_NAMES.Lighting = L.W_CATEGORY_LIGHTING
ACVar.CATEGORY_NAMES.Sound = L.W_CATEGORY_SOUND
ACVar.CATEGORY_NAMES.Performance = L.W_CATEGORY_PERFORMANCE

-- A setting the running client does not have is left out, so the addon also works on a client without them (stock
-- awesome_wotlk, or an older Waystone build); headers, notes and links stay
local WAYSTONE_TABS = { World = true, Display = true, Lighting = true, Sound = true, Performance = true }
-- texLodBias is a stock CVar that only Waystone applies, so its tab needs the client itself
local isWaystone = GetCVar("uiLiveResize") ~= nil

for i = #order, 1, -1 do
    local id = order[i]
    local list = CVARS[id]
    local settings = 0

    for j = #list, 1, -1 do
        local cvarDef = list[j]

        if not ACVar.IsReadOnly(cvarDef) then
            if GetCVar(cvarDef.name) == nil then
                table.remove(list, j)
            else
                settings = settings + 1
            end
        end
    end

    -- a Waystone tab is dropped on another client, or when none of its settings are in this one
    if WAYSTONE_TABS[id] and (settings == 0 or not isWaystone) then
        table.remove(order, i)
    end
end
