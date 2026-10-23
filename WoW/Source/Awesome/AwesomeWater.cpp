// Modern water on the D3D11 device (Engine/Source/Gx/GxWater.h, CGxDeviceD3d11/D3d11Water.*): the CVars, the
// disturbances units make in the water, rain on it, and the calls around the world pass. With gxApi D3D9 none of it does
// anything and the stock water stays. No asset is added: the stock water textures and Light.dbc's colours are shaded in
// a modern way. /water [on|off].
//
// waterModern: the water surface shaded per pixel from copies of the opaque scene: what lies under it seen through it
//   (waterRefraction bends it with the surface) and fading into the water's own colour with depth (waterClarity: how
//   far one sees; the colour is Light.dbc's shallow and deep water colour through the stock depth texture), reflections
//   by Fresnel (waterReflectionStrength) of the world (screen-space, waterReflection) and of the sky (the sky as drawn
//   where it is on screen, else Light.dbc's sky bands), the sun's glint, and foam at the shore and on disturbed water
//   (waterFoam). The normal comes from the stock animated water texture's relief, at its scale and at a larger one
//   drifting across it (waterDetail).
// waterRipples: a wave-equation height field around the player (128 yards, a texel a quarter of a yard) that units
//   disturb: a moving unit leaves a wake, one treading water sends out rings, and one entering the water splashes
//   (waterInteraction); raindrops land on it in rain (waterRain). waterRippleStrength is how far it bends the surface.
// waterTessellation: near the eye the water's triangles are cut finer (down to 0.3 yards) and moved up and down by the
//   height field, so the waves are geometry (waterWaveHeight scales how far).
// waterStockRipples: the stock splash and wake decals on the water too (off: the height field replaces them).
// waterDebug: 1 normals, 2 depth under the surface, 3 the height field, 4 reflection hits (green world, blue sky).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/GxWater.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/MapWeather.h"
#include "WoW/Source/WorldClient/World.h"

static AwesomeCVar s_cvModern = AwesomeCVar::Int("waterModern", 1, 0, 1);
static AwesomeCVar s_cvReflection = AwesomeCVar::Int("waterReflection", 1, 0, 1);
static AwesomeCVar s_cvReflectionStrength = AwesomeCVar::Float("waterReflectionStrength", 1.0f, 0.0f, 2.0f);
static AwesomeCVar s_cvRefraction = AwesomeCVar::Float("waterRefraction", 1.0f, 0.0f, 2.0f);
static AwesomeCVar s_cvClarity = AwesomeCVar::Float("waterClarity", 1.0f, 0.1f, 4.0f);
static AwesomeCVar s_cvDetail = AwesomeCVar::Float("waterDetail", 1.0f, 0.0f, 2.0f);
static AwesomeCVar s_cvFoam = AwesomeCVar::Float("waterFoam", 1.0f, 0.0f, 2.0f);
static AwesomeCVar s_cvRipples = AwesomeCVar::Int("waterRipples", 1, 0, 1);
static AwesomeCVar s_cvRippleStrength = AwesomeCVar::Float("waterRippleStrength", 1.0f, 0.0f, 4.0f);
static AwesomeCVar s_cvTessellation = AwesomeCVar::Int("waterTessellation", 1, 0, 1);
static AwesomeCVar s_cvWaveHeight = AwesomeCVar::Float("waterWaveHeight", 0.5f, 0.0f, 4.0f);
static AwesomeCVar s_cvInteraction = AwesomeCVar::Float("waterInteraction", 1.0f, 0.0f, 4.0f);
static AwesomeCVar s_cvRain = AwesomeCVar::Float("waterRain", 1.0f, 0.0f, 4.0f);
static AwesomeCVar s_cvStockRipples = AwesomeCVar::Int("waterStockRipples", 0, 0, 1);
static AwesomeCVar s_cvDebug = AwesomeCVar::Int("waterDebug", 0, 0, 4);

static AwesomeCVar* const s_cvars[] = {
    &s_cvModern, &s_cvReflection, &s_cvReflectionStrength, &s_cvRefraction, &s_cvClarity, &s_cvDetail, &s_cvFoam,
    &s_cvRipples, &s_cvRippleStrength, &s_cvTessellation, &s_cvWaveHeight, &s_cvInteraction, &s_cvRain, &s_cvStockRipples, &s_cvDebug,
};

static bool s_inWorld = false;
static GxWaterParams s_params;
static std::vector<GxWaterImpulse> s_pending;
static uint32_t s_lastMs = 0;
static float s_frameDt = 0.0f;
static float s_frameTime = 0.0f;
static uint32_t s_statsUnits = 0;
static uint32_t s_statsUnitsLast = 0;

static C3Vector ColorOf(const CImVector& c) {
    return C3Vector(c.r * (1.0f / 255.0f), c.g * (1.0f / 255.0f), c.b * (1.0f / 255.0f));
}

static uint32_t Features() {
    if (!s_cvModern.m_intValue) {
        return 0;
    }

    uint32_t features = GxWater_Shading;
    features |= s_cvReflection.m_intValue ? GxWater_Reflection : 0;
    features |= s_cvRipples.m_intValue ? GxWater_Ripples : 0;
    features |= s_cvFoam.m_floatValue > 0.0f ? GxWater_Foam : 0;
    features |= s_cvRipples.m_intValue && s_cvTessellation.m_intValue && s_cvWaveHeight.m_floatValue > 0.0f ? GxWater_Tessellation : 0;
    return features;
}

static void BuildParams(GxWaterParams& p) {
    p.m_features = Features();
    p.m_refraction = s_cvRefraction.m_floatValue;
    p.m_clarity = s_cvClarity.m_floatValue;
    p.m_reflection = s_cvReflectionStrength.m_floatValue;
    p.m_detail = s_cvDetail.m_floatValue;
    p.m_foam = s_cvFoam.m_floatValue;
    p.m_rippleNormal = s_cvRippleStrength.m_floatValue;
    p.m_waveHeight = s_cvWaveHeight.m_floatValue;
    p.m_rainStrength = 0.012f;
    p.m_reflectionSteps = 24;
    p.m_rippleSpeed = 3.0f;
    p.m_rippleDamping = 0.9f;
    p.m_time = s_frameTime;
    p.m_deltaTime = s_frameDt;
    p.m_debug = static_cast<uint32_t>(s_cvDebug.m_intValue);
    p.m_origin = CWorldScene::s_activeWorldView;
    p.m_rain = 0.0f;

    DayNight::DNInfo* info = DayNight::GetInfo();

    if (!info) {
        return;
    }

    const DayNight::CurrentLight& bands = info->m_bands;

    for (uint32_t i = 0; i < 6; i++) {
        p.m_skyColors[i] = ColorOf(bands.m_skyColors[i]);
    }

    p.m_sunColor = ColorOf(bands.m_diffuse);
    p.m_sunDirection = info->m_light0.m_dir;
    p.m_ambient = ColorOf(bands.m_ambient);
    float interior = info->m_interiorFogBlend < 0.0f ? 0.0f : (info->m_interiorFogBlend > 1.0f ? 1.0f : info->m_interiorFogBlend);
    p.m_skyVisible = 1.0f - interior;
    p.m_centerX = info->m_playerPos.x;
    p.m_centerY = info->m_playerPos.y;

    // rain on the water: up to three drops a square yard and second in a downpour, none indoors
    Weather* weather = World::s_weather;

    if (weather && weather->GetType() == 1) {
        float particles = AwesomeWeatherParticleIntensity(weather->m_intensity);
        p.m_rain = 3.0f * particles * s_cvRain.m_floatValue * p.m_skyVisible;
    }
}

void AwesomeWaterBeginWorld() {
    uint32_t now = OsGetAsyncTimeMs();
    s_frameDt = s_lastMs ? (now - s_lastMs) * 0.001f : 0.0f;
    s_lastMs = now;
    s_frameTime = static_cast<float>(now % 3600000u) * 0.001f;
    s_inWorld = false;
    s_statsUnitsLast = s_statsUnits;
    s_statsUnits = 0;

    if (!g_gxWater) {
        s_pending.clear();
        return;
    }

    BuildParams(s_params);
    g_gxWater->Update(s_params, s_pending.data(), static_cast<uint32_t>(s_pending.size()));
    s_pending.clear();
    s_inWorld = true;
}

void AwesomeWaterBeforeLiquids() {
    if (s_inWorld && g_gxWater) {
        g_gxWater->BeforeLiquids(CWorldScene::s_activeWorldView);
    }
}

void AwesomeWaterEndWorld() {
    if (s_inWorld && g_gxWater) {
        g_gxWater->EndWorld();
    }

    s_inWorld = false;
}

void AwesomeWaterOnUnitInLiquid(uintptr_t id, const C3Vector& surface, float depth, float maxDepth, uint32_t motion, uint32_t event, float speed) {
    if (!g_gxWater || !s_cvModern.m_intValue || !s_cvRipples.m_intValue || s_cvInteraction.m_floatValue <= 0.0f) {
        return;
    }

    DayNight::DNInfo* info = DayNight::GetInfo();

    // beyond the height field: nothing to disturb
    if (info) {
        float dx = surface.x - info->m_playerPos.x;
        float dy = surface.y - info->m_playerPos.y;

        if (dx * dx + dy * dy > 70.0f * 70.0f) {
            return;
        }
    }

    // CGUnit_C::CreateRipple's depth: a unit deeper than half its reach disturbs the surface less, and not at all at the
    // reach (twice its collision height)
    float half = maxDepth * 0.5f;
    float depthFactor = depth > half ? (maxDepth - depth) / (maxDepth - half) * 0.5f + 0.5f : 1.0f;
    float bodyRadius = maxDepth * 0.5f * 0.3f;
    bodyRadius = bodyRadius < 0.35f ? 0.35f : (bodyRadius > 2.5f ? 2.5f : bodyRadius);
    float dt = s_frameDt > 0.1f ? 0.1f : s_frameDt;
    float interaction = s_cvInteraction.m_floatValue;

    GxWaterImpulse impulse;
    impulse.m_x = surface.x;
    impulse.m_y = surface.y;
    impulse.m_radius = bodyRadius;

    if (event == 201) {
        // in or out of the water: a splash
        impulse.m_radius = bodyRadius * 1.6f;
        impulse.m_strength = 0.12f * interaction;
    } else if (motion & 0xF) {
        // moving: pushes the water aside as it goes, a wake behind it
        float pace = speed < 0.0f ? 0.0f : (speed > 14.0f ? 14.0f : speed);
        impulse.m_strength = (0.25f + 0.5f * pace / 7.0f) * dt * interaction * depthFactor;
    } else if (motion & 0x200000) {
        // swimming in place, treading water: a slow bob, rings
        float phase = static_cast<float>((id >> 4) % 628u) * 0.01f;
        impulse.m_strength = 0.2f * sinf(s_frameTime * 4.5f + phase) * dt * interaction * depthFactor;
        impulse.m_radius = bodyRadius * 0.8f;
    } else {
        // standing in shallow water: still
        return;
    }

    if (s_pending.size() < 256) {
        s_pending.push_back(impulse);
        s_statsUnits++;
    }
}

static void Print(lua_State* L, const char* text) {
    lua_getfield(L, LUA_GLOBALSINDEX, "DEFAULT_CHAT_FRAME");

    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "AddMessage");

        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, -2);
            lua_pushstring(L, text);
            lua_pcall(L, 2, 0, 0);
        } else {
            lua_pop(L, 1);
        }
    }

    lua_pop(L, 1);
}

// /water [on|off]: the modern water and its ripples on or off, or what it did last frame
static int32_t Script_WaterCommand(lua_State* L) {
    const char* option = AwesomeLuaIsNoneOrNil(L, 1) ? "" : lua_tolstring(L, 1, nullptr);
    char word[16] = {};

    if (option) {
        while (*option == ' ') {
            option++;
        }

        for (size_t i = 0; i + 1 < sizeof(word) && option[i] && option[i] != ' '; i++) {
            word[i] = static_cast<char>(option[i] >= 'A' && option[i] <= 'Z' ? option[i] + 32 : option[i]);
        }
    }

    if (!strcmp(word, "on") || !strcmp(word, "off")) {
        bool on = word[1] == 'n';
        s_cvModern.Set(on ? "1" : "0");
        s_cvRipples.Set(on ? "1" : "0");
        Print(L, on ? "|cff33ff99Water|r modern water and ripples on" : "|cff33ff99Water|r stock water");
        return 0;
    }

    if (!g_gxWater) {
        Print(L, "|cff33ff99Water|r needs the D3D11 device: SET gxApi \"D3D11\" or -d3d11, and a restart");
        return 0;
    }

    GxWaterStats stats = {};
    g_gxWater->GetStats(stats);
    char line[384];
    snprintf(line, sizeof(line), "|cff33ff99Water|r features 0x%X; scene copied %s, %u water draws shaded; height field %s (%u x %u, %.2f yd), %u steps, %u disturbances (%u from units)",
        Features(), stats.m_captured ? "yes" : "no", stats.m_waterDraws, stats.m_simActive ? "on" : "off", stats.m_fieldSize, stats.m_fieldSize, stats.m_texel, stats.m_simSteps, stats.m_impulses, s_statsUnitsLast);
    Print(L, line);
    snprintf(line, sizeof(line), "  rain %.2f drops / yd2 / s, sky visible %.2f", s_params.m_rain, s_params.m_skyVisible);
    Print(L, line);
    Print(L, "  /water on | off; waterDebug 1 normals, 2 depth, 3 height field, 4 reflection hits");
    return 0;
}

bool AwesomeWaterHidesStockRipples() {
    return g_gxWater && s_cvModern.m_intValue && s_cvRipples.m_intValue && !s_cvStockRipples.m_intValue;
}

void AwesomeWaterRegisterCVars() {
    for (AwesomeCVar* cvar : s_cvars) {
        cvar->Register();
    }
}

void AwesomeWaterOnEnterWorld() {
    s_pending.clear();
    s_lastMs = 0;
    AwesomeLuaRegisterSlashCommand(FrameScript_GetContext(), "MODERNWATER", "/water", &Script_WaterCommand);
}
