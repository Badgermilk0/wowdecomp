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
// waterSplashes: droplets (small spheres of water: the world behind them turned over, the sky at their rim, a sun
//   glint, streaked by their motion), torn sheets of the crown and mist thrown up when a unit jumps or falls into the water (more when it falls), walks in or
//   out, and as spray ahead of a unit moving through it; each droplet that falls back makes a small ring. The field also
//   keeps foam where the water was disturbed, fading over a few seconds. The amount (0 = none).
// waterDebug: 1 normals, 2 depth under the surface, 3 the height field, 4 reflection hits (green world, blue sky),
//   5 magma's heat (red) and how stirred a thick liquid is (green).
//
// liquidModern: magma and slime too (the stock client draws them unlit, the texture as it is). Each liquid type is
//   classed by its LiquidType.dbc row (AwesomeWaterNoteLiquid): water, magma (material 2, or a lava or magma texture),
//   slime (named so: Slime, Naxxramas - Slime, Orange Slime). Magma is crust and melt: dark crust lit like rock,
//   glowing veins (liquidGlow; past 1 in the HDR scene, so they bloom), hotter where stirred, cooling at the shore,
//   churning. Slime is thick and glossy: lit, keeping some of its stock glow, wet reflections, froth where stirred.
//   Both move on a height field of their own, slow and heavily damped, so a wake oozes shut instead of ringing; they
//   bubble (liquidBubbles), and a unit splashing in throws heavy globs and rising embers (magma) or gobs and strands
//   (slime), which fall back as dents. The liquid renderer's opaque pass, which draws them, waits for the opaque world
//   (AwesomeWaterOpaqueLiquids) so the scene can be copied for them.

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/GxWater.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/CAaSphere.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/LiquidTypeRec.h"
#include "WoW/Source/LiquidSystem/Liquid.h"
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
static AwesomeCVar s_cvSplashes = AwesomeCVar::Float("waterSplashes", 1.0f, 0.0f, 2.0f);
static AwesomeCVar s_cvDebug = AwesomeCVar::Int("waterDebug", 0, 0, 5);
static AwesomeCVar s_cvLiquids = AwesomeCVar::Int("liquidModern", 1, 0, 1);
static AwesomeCVar s_cvGlow = AwesomeCVar::Float("liquidGlow", 1.0f, 0.0f, 3.0f);
static AwesomeCVar s_cvBubbles = AwesomeCVar::Float("liquidBubbles", 1.0f, 0.0f, 4.0f);

static AwesomeCVar* const s_cvars[] = {
    &s_cvModern, &s_cvReflection, &s_cvReflectionStrength, &s_cvRefraction, &s_cvClarity, &s_cvDetail, &s_cvFoam,
    &s_cvRipples, &s_cvRippleStrength, &s_cvTessellation, &s_cvWaveHeight, &s_cvInteraction, &s_cvRain, &s_cvStockRipples, &s_cvSplashes, &s_cvDebug,
    &s_cvLiquids, &s_cvGlow, &s_cvBubbles,
};

// what a liquid is, and the colour of what it throws (linear)
struct LiquidInfo {
    uint32_t m_class = GxLiquid_Water;
    float m_color[3] = { 0.0f, 0.0f, 0.0f };
};

static bool s_inWorld = false;
static GxWaterParams s_params;
static std::vector<GxWaterImpulse> s_pending;
static std::vector<GxWaterSplash> s_splashes;
// the spray each moving unit owes, in drops (a fraction carries to the next frame)
static std::unordered_map<uintptr_t, float> s_spray;
// the class of each liquid's material settings (Liquid::CSettingsBank keeps one a liquid type), and of each liquid type
static std::unordered_map<const void*, LiquidInfo> s_settingsInfo;
static std::unordered_map<uint32_t, LiquidInfo> s_typeInfo;
static uint32_t s_lastMs = 0;
static float s_frameDt = 0.0f;
static float s_frameTime = 0.0f;
static uint32_t s_statsUnits = 0;
static uint32_t s_statsUnitsLast = 0;

// the thick liquids near the player: the last time one was drawn or disturbed there, and the nearest one's class
static uint32_t s_thickSeenMs = 0;
static uint32_t s_thickClass = GxLiquid_Magma;
static uint32_t s_thickClassFrame = GxLiquids_Last;
static float s_thickNearest = 1.0e9f;
// the liquid renderer's opaque pass waits for the opaque world this frame (AwesomeWaterOpaqueLiquids)
static bool s_opaqueDeferred = false;

static C3Vector ColorOf(const CImVector& c) {
    return C3Vector(c.r * (1.0f / 255.0f), c.g * (1.0f / 255.0f), c.b * (1.0f / 255.0f));
}

static bool Contains(const char* text, const char* word) {
    if (!text) {
        return false;
    }

    size_t n = strlen(word);

    for (const char* p = text; *p; p++) {
        size_t i = 0;

        while (i < n && p[i] && tolower(static_cast<unsigned char>(p[i])) == word[i]) {
            i++;
        }

        if (i == n) {
            return true;
        }
    }

    return false;
}

// a LiquidType.dbc row's class. The live client's rows: water, ocean, rivers and WMO water are material 3 (procedural
// water) or 1; Magma, Slow / Fast / WMO Magma, Green Lava, CoA Black and Chamber Magma, and every Slime row, material 2;
// Orange Slime is material 1 with a lava texture
static LiquidInfo Classify(const LiquidTypeRec* rec) {
    LiquidInfo info;

    if (!rec) {
        return info;
    }

    const char* texture = rec->m_texture[0];
    bool lavaTexture = Contains(texture, "lava") || Contains(texture, "magma");

    if (Contains(rec->m_name, "slime")) {
        info.m_class = GxLiquid_Slime;
    } else if (rec->m_materialID == 2 || lavaTexture) {
        info.m_class = GxLiquid_Magma;
    }

    if (info.m_class == GxLiquid_Magma) {
        // the glow of what it throws: green for the fel lava (Green Lava), else orange
        bool green = Contains(texture, "green");
        info.m_color[0] = green ? 0.25f : 1.0f;
        info.m_color[1] = green ? 1.0f : 0.25f;
        info.m_color[2] = green ? 0.08f : 0.03f;
    } else if (info.m_class == GxLiquid_Slime) {
        bool orange = Contains(rec->m_name, "orange") || lavaTexture;
        info.m_color[0] = orange ? 0.85f : 0.22f;
        info.m_color[1] = orange ? 0.32f : 0.45f;
        info.m_color[2] = orange ? 0.04f : 0.04f;
    }

    return info;
}

static const LiquidInfo& InfoOfType(uint32_t liquidType) {
    auto it = s_typeInfo.find(liquidType);

    if (it != s_typeInfo.end()) {
        return it->second;
    }

    return s_typeInfo[liquidType] = Classify(g_liquidTypeDB.GetRecord(static_cast<int32_t>(liquidType)));
}

static bool LiquidsOn() {
    return s_cvModern.m_intValue && s_cvLiquids.m_intValue;
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
    features |= s_cvRipples.m_intValue && s_cvSplashes.m_floatValue > 0.0f ? GxWater_Splashes : 0;
    features |= s_cvLiquids.m_intValue ? GxWater_Liquids : 0;
    return features;
}

static void BuildParams(GxWaterParams& p, uint32_t now) {
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

    // the thick liquids' field, while one was drawn or disturbed near the player in the last two seconds. Its physics
    // follow the nearest: viscous (D3d11Water's SimPs), magma heavier and slower to settle than slime, so a wake oozes
    // shut instead of ringing
    if (s_thickClassFrame < GxLiquids_Last) {
        s_thickClass = s_thickClassFrame;
    }

    s_thickClassFrame = GxLiquids_Last;
    s_thickNearest = 1.0e9f;
    bool magma = s_thickClass == GxLiquid_Magma;
    p.m_thick = LiquidsOn() && s_thickSeenMs && now - s_thickSeenMs < 2000;
    p.m_thickSpeed = magma ? 0.6f : 1.0f;
    p.m_thickDamping = magma ? 10.0f : 6.0f;
    p.m_thickSettle = magma ? 0.5f : 0.9f;
    p.m_bubbles = (magma ? 0.05f : 0.08f) * s_cvBubbles.m_floatValue;
    p.m_bubbleStrength = magma ? 0.05f : 0.03f;
    p.m_bubbleRadius = magma ? 0.45f : 0.3f;
    p.m_glow = s_cvGlow.m_floatValue;
    p.m_selfLight = 0.65f;

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
    s_opaqueDeferred = false;
    s_statsUnitsLast = s_statsUnits;
    s_statsUnits = 0;

    if (!g_gxWater) {
        s_pending.clear();
        s_splashes.clear();
        return;
    }

    BuildParams(s_params, now);
    g_gxWater->Update(s_params, s_pending.data(), static_cast<uint32_t>(s_pending.size()), s_splashes.data(), static_cast<uint32_t>(s_splashes.size()));
    s_pending.clear();
    s_splashes.clear();

    // units that stopped spraying long ago
    if (s_spray.size() > 512) {
        s_spray.clear();
    }
    s_inWorld = true;
}

bool AwesomeWaterDefersOpaqueLiquids() {
    // only while the device shades magma and slime: the stock order otherwise
    s_opaqueDeferred = s_inWorld && g_gxWater && LiquidsOn() && CWorldScene::s_liquidRenderer;
    return s_opaqueDeferred;
}

void AwesomeWaterOpaqueLiquids() {
    if (!s_opaqueDeferred) {
        return;
    }

    s_opaqueDeferred = false;

    if (!CWorldScene::s_liquidRenderer) {
        return;
    }

    // magma and slime are opaque: drawn with the stock state after the opaque world, the scene copied for them first
    CWorldScene::ApplyFogState();

    if (s_inWorld && g_gxWater) {
        g_gxWater->BeforeOpaqueLiquids(CWorldScene::s_activeWorldView);
    }

    // the materials leave blending and depth to the state they find; stock that was the terrain's (opaque, depth written),
    // here it is whatever the models and grass left
    g_theGxDevicePtr->RsPush();
    g_theGxDevicePtr->RsSet(GxRs_BlendingMode, GxBlend_Opaque);
    g_theGxDevicePtr->RsSet(GxRs_DepthTest, 1);
    g_theGxDevicePtr->RsSet(GxRs_DepthWrite, 1);
    CWorldScene::s_liquidRenderer->Flush(CWorldScene::s_activeWorldView, 0);
    g_theGxDevicePtr->RsPop();
}

void AwesomeWaterBeforeLiquids() {
    if (s_inWorld && g_gxWater) {
        g_gxWater->BeforeLiquids(CWorldScene::s_activeWorldView);
    }
}

void AwesomeWaterAfterLiquids() {
    if (s_inWorld && g_gxWater) {
        g_gxWater->AfterLiquids();
    }
}

void AwesomeWaterNoteLiquid(const void* settings, const LiquidTypeRec* rec) {
    if (settings) {
        s_settingsInfo[settings] = Classify(rec);
    }
}

void AwesomeWaterLiquidRender(const void* settings, const CAaSphere* bounds, const C3Vector& camera) {
    if (!g_gxWater) {
        return;
    }

    auto it = s_settingsInfo.find(settings);
    uint32_t liquid = it != s_settingsInfo.end() ? it->second.m_class : static_cast<uint32_t>(GxLiquid_Water);
    g_gxWater->SetLiquid(liquid);

    if (liquid == GxLiquid_Water || !bounds) {
        return;
    }

    // a thick liquid within reach of the height field: it runs, with the nearest one's physics
    float dx = bounds->c.x - camera.x;
    float dy = bounds->c.y - camera.y;
    float dz = bounds->c.z - camera.z;
    float distance = sqrtf(dx * dx + dy * dy + dz * dz) - bounds->r;

    if (distance < 150.0f) {
        s_thickSeenMs = OsGetAsyncTimeMs();

        if (distance < s_thickNearest) {
            s_thickNearest = distance;
            s_thickClassFrame = liquid;
        }
    }
}

static void AddSplash(const C3Vector& at, float count, float up, float out, float radius, uint32_t kind, uint32_t liquid, const float* color) {
    uint32_t n = static_cast<uint32_t>(count * s_cvSplashes.m_floatValue + 0.5f);

    if (!n || s_splashes.size() >= 32) {
        return;
    }

    GxWaterSplash splash;
    splash.m_x = at.x;
    splash.m_y = at.y;
    splash.m_z = at.z;
    splash.m_count = n;
    splash.m_up = up;
    splash.m_out = out;
    splash.m_radius = radius;
    splash.m_kind = kind;
    splash.m_liquid = liquid;
    splash.m_color[0] = color ? color[0] : 0.0f;
    splash.m_color[1] = color ? color[1] : 0.0f;
    splash.m_color[2] = color ? color[2] : 0.0f;
    s_splashes.push_back(splash);
}

void AwesomeWaterEndWorld() {
    // the opaque liquids were held back and nothing drew them (no model scene this frame): they draw now, or they would
    // stay queued and never draw again
    AwesomeWaterOpaqueLiquids();

    if (s_inWorld && g_gxWater) {
        g_gxWater->EndWorld();
    }

    s_inWorld = false;
}

// a crossing of the surface (event 201), a unit moving through it, or one treading in place, by liquid. Water splashes
// up droplets, sheets and mist; magma is heavy: a few slow globs that cool in the air and a shower of rising embers, a
// deep dent; slime throws gobs and strands that hang, a dent that oozes shut
static void Splash(uint32_t liquid, const float* color, const C3Vector& surface, float bodyRadius, bool entering, bool falling) {
    if (liquid == GxLiquid_Magma) {
        if (entering && falling) {
            AddSplash(surface, 30.0f, 4.5f, 2.2f, bodyRadius * 0.8f, 0, liquid, color);
            AddSplash(surface, 40.0f, 3.0f, 1.5f, bodyRadius * 0.9f, 3, liquid, color);
        } else if (entering) {
            AddSplash(surface, 8.0f, 1.6f, 1.0f, bodyRadius * 0.7f, 0, liquid, color);
            AddSplash(surface, 12.0f, 1.5f, 0.8f, bodyRadius * 0.8f, 3, liquid, color);
        } else {
            // out of it: drips
            AddSplash(surface, 6.0f, 0.5f, 0.3f, bodyRadius * 0.5f, 0, liquid, color);
        }
    } else if (liquid == GxLiquid_Slime) {
        if (entering && falling) {
            AddSplash(surface, 24.0f, 4.0f, 2.4f, bodyRadius * 1.0f, 2, liquid, color);
            AddSplash(surface, 60.0f, 4.5f, 2.6f, bodyRadius * 0.9f, 0, liquid, color);
        } else if (entering) {
            AddSplash(surface, 6.0f, 1.6f, 1.2f, bodyRadius * 0.9f, 2, liquid, color);
            AddSplash(surface, 20.0f, 1.6f, 1.2f, bodyRadius * 0.8f, 0, liquid, color);
        } else {
            // out of it: it drips off in gobs and strings
            AddSplash(surface, 16.0f, 0.6f, 0.4f, bodyRadius * 0.6f, 0, liquid, color);
            AddSplash(surface, 6.0f, 0.4f, 0.3f, bodyRadius * 0.6f, 2, liquid, color);
        }
    } else if (entering && falling) {
        // the crown torn into sheets, a spray of fine droplets, a column from the middle, and mist over it
        AddSplash(surface, 36.0f, 6.0f, 3.0f, bodyRadius * 1.0f, 2, liquid, color);
        AddSplash(surface, 260.0f, 7.0f, 3.2f, bodyRadius * 0.9f, 0, liquid, color);
        AddSplash(surface, 70.0f, 10.0f, 0.8f, bodyRadius * 0.35f, 0, liquid, color);
        AddSplash(surface, 8.0f, 1.2f, 1.6f, bodyRadius * 0.8f, 1, liquid, color);
    } else if (entering) {
        AddSplash(surface, 10.0f, 2.0f, 1.5f, bodyRadius * 0.9f, 2, liquid, color);
        AddSplash(surface, 70.0f, 2.4f, 1.8f, bodyRadius * 0.8f, 0, liquid, color);
    } else {
        // out of the water: it runs off
        AddSplash(surface, 40.0f, 0.9f, 0.6f, bodyRadius * 0.6f, 0, liquid, color);
    }
}

void AwesomeWaterOnUnitInLiquid(uintptr_t id, const C3Vector& surface, float depth, float maxDepth, uint32_t motion, uint32_t event, float speed, float facing, uint32_t liquidType) {
    if (!g_gxWater || !s_cvModern.m_intValue || !s_cvRipples.m_intValue || s_cvInteraction.m_floatValue <= 0.0f) {
        return;
    }

    const LiquidInfo& info = InfoOfType(liquidType);
    uint32_t liquid = info.m_class;

    if (liquid != GxLiquid_Water && !s_cvLiquids.m_intValue) {
        return;
    }

    DayNight::DNInfo* dn = DayNight::GetInfo();

    // beyond the height field: nothing to disturb
    if (dn) {
        float dx = surface.x - dn->m_playerPos.x;
        float dy = surface.y - dn->m_playerPos.y;

        if (dx * dx + dy * dy > 70.0f * 70.0f) {
            return;
        }
    }

    // a thick liquid disturbed near the player: its field runs
    if (liquid != GxLiquid_Water) {
        s_thickSeenMs = OsGetAsyncTimeMs();
    }

    // CGUnit_C::CreateRipple's depth: a unit deeper than half its reach disturbs the surface less, and not at all at the
    // reach (twice its collision height)
    float half = maxDepth * 0.5f;
    float depthFactor = depth > half ? (maxDepth - depth) / (maxDepth - half) * 0.5f + 0.5f : 1.0f;
    float bodyRadius = maxDepth * 0.5f * 0.3f;
    bodyRadius = bodyRadius < 0.35f ? 0.35f : (bodyRadius > 2.5f ? 2.5f : bodyRadius);
    float dt = s_frameDt > 0.1f ? 0.1f : s_frameDt;
    float interaction = s_cvInteraction.m_floatValue;
    bool thick = liquid != GxLiquid_Water;

    GxWaterImpulse impulse;
    impulse.m_x = surface.x;
    impulse.m_y = surface.y;
    impulse.m_radius = bodyRadius;
    impulse.m_liquid = liquid;

    bool splashes = s_cvSplashes.m_floatValue > 0.0f;
    // falling (MOVEFLAG_FALLING / FALLING_FAR): a jump or a drop into the liquid
    bool falling = (motion & 0x3000) != 0;

    if (event == 201) {
        // in or out (CreateRipple's crossing of 0.4 of the collision height, a fifth of maxDepth): a splash. A thick
        // liquid takes a deeper, wider dent that it is slow to fill
        bool entering = depth >= maxDepth * 0.2f;
        impulse.m_radius = bodyRadius * (falling ? 2.0f : 1.6f) * (thick ? 1.2f : 1.0f);
        impulse.m_strength = (entering && falling ? 0.2f : 0.12f) * (thick ? 1.4f : 1.0f) * interaction;

        if (splashes) {
            Splash(liquid, info.m_color, surface, bodyRadius, entering, falling);
        }
    } else if (motion & 0xF) {
        // moving: pushes the liquid aside as it goes, a wake behind it (heavier through magma and slime)
        float pace = speed < 0.0f ? 0.0f : (speed > 14.0f ? 14.0f : speed);
        impulse.m_strength = (0.25f + 0.5f * pace / 7.0f) * dt * interaction * depthFactor * (thick ? 1.3f : 1.0f);

        if (splashes && depthFactor > 0.7f) {
            // spray thrown off ahead of it, more the faster it goes; magma throws few and slow globs (and embers), slime
            // gobs
            float rate = liquid == GxLiquid_Magma ? 2.0f + 5.0f * pace / 7.0f : (liquid == GxLiquid_Slime ? 4.0f + 12.0f * pace / 7.0f : 12.0f + 40.0f * pace / 7.0f);
            float& owed = s_spray[id];
            owed += rate * dt;

            if (owed >= 1.0f) {
                float ahead = bodyRadius * 0.9f;
                C3Vector front(surface.x + cosf(facing) * ahead, surface.y + sinf(facing) * ahead, surface.z);
                float up = thick ? 1.0f + 0.1f * pace : 1.4f + 0.15f * pace;
                AddSplash(front, floorf(owed), up, thick ? 0.8f : 1.1f, bodyRadius * 0.6f, 0, liquid, info.m_color);

                if (liquid == GxLiquid_Magma) {
                    AddSplash(front, floorf(owed) * 1.5f, 1.4f, 0.6f, bodyRadius * 0.7f, 3, liquid, info.m_color);
                }

                owed -= floorf(owed);
            }
        }
    } else if (motion & 0x200000) {
        // swimming in place, treading: a slow bob, rings (slower and smaller in a thick liquid)
        float phase = static_cast<float>((id >> 4) % 628u) * 0.01f;
        float rate = liquid == GxLiquid_Magma ? 2.0f : (liquid == GxLiquid_Slime ? 3.0f : 4.5f);
        float bob = liquid == GxLiquid_Water ? 0.2f : 0.13f;
        impulse.m_strength = bob * sinf(s_frameTime * rate + phase) * dt * interaction * depthFactor;
        impulse.m_radius = bodyRadius * 0.8f;
    } else {
        // standing in a shallow pool: still
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
    snprintf(line, sizeof(line), "  rain %.2f drops / yd2 / s, sky visible %.2f, %u splash particles started", s_params.m_rain, s_params.m_skyVisible, stats.m_particlesStarted);
    Print(L, line);
    snprintf(line, sizeof(line), "  magma and slime %s: %u draws shaded, scene copied for them %s; their height field %s (%s), %.2f bubbles / yd2 / s",
        LiquidsOn() ? "on" : "off", stats.m_thickDraws, stats.m_opaqueCaptured ? "yes" : "no", stats.m_thickActive ? "on" : "off", s_thickClass == GxLiquid_Slime ? "slime" : "magma", s_params.m_bubbles);
    Print(L, line);
    Print(L, "  /water on | off; waterDebug 1 normals, 2 depth, 3 height field, 4 reflection hits, 5 magma heat");
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
    s_thickSeenMs = 0;
    AwesomeLuaRegisterSlashCommand(FrameScript_GetContext(), "MODERNWATER", "/water", &Script_WaterCommand);
}
