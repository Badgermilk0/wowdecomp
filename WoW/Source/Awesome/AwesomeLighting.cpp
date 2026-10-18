// Modern lighting on the D3D11 device (Engine/Source/Gx/GxLighting.h, CGxDeviceD3d11/D3d11Lighting.*): the CVars, the
// lights of the world in view, the zone's sky and fog colours, and the calls that bracket the world pass. Every effect
// defaults to off; with gxApi D3D9 none of them does anything. /lighting [on|off|stats].
//
// hdr: the world draws into a 16-bit float target and is tonemapped into the frame before the UI (hdrTonemap: 0 clamp
//   as stock, 1 neutral, the stock colour below a shoulder, 2 ACES; hdrExposure). bloom: the HDR scene's bright part,
//   blurred and added back (the intensity; bloomThreshold in linear light). Both before FFX glow, which sees the
//   tonemapped frame.
// ssao: screen-space ambient occlusion at the end of the opaque pass (the intensity; ssaoRadius in yards, faded out
//   by ssaoDistance).
// lightPerPixel: M2 models, WMOs and the terrain lit per pixel with their own stock formulas.
// lightDynamic: the point lights of M2 models (torches, braziers: their animated colour and attenuation tracks) and
//   of WMOs (the MOLT lights the stock client bakes into vertex colours and never draws), per pixel, clustered, with a
//   flicker on warm ones (lightFlicker). lightDynamicM2 / lightDynamicWmo scale each kind, lightDynamicDistance is how
//   far they reach.
// lightSpecular: the WMO specular shaders per pixel (they need the stock specular 1, a restart), and a highlight on
//   sphere-mapped M2 batches (lightSpecularM2).
// lightSkyAmbient: the ambient tinted by the zone's sky colour from above and its ground from below (Light.dbc).
// fogHeight: exponential height fog (density per yard), thickest below the player (fogHeightOffset yards from the
//   player's feet, fogHeightFalloff); fogScattering: the fog tinted toward the sun's colour around the sun.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>
#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Frame/CSimpleModel.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxMatrixStack.h"
#include "Engine/Source/Gx/GxLighting.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2ModelLight.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapObj.h"
#include "WoW/Source/WorldClient/MapObjDef.h"
#include "WoW/Source/WorldClient/World.h"

static AwesomeCVar s_cvHdr = AwesomeCVar::Int("hdr", 0, 0, 1);
static AwesomeCVar s_cvExposure = AwesomeCVar::Float("hdrExposure", 1.0f, 0.25f, 4.0f);
static AwesomeCVar s_cvTonemap = AwesomeCVar::Int("hdrTonemap", 1, 0, 2);
static AwesomeCVar s_cvBloom = AwesomeCVar::Float("bloom", 0.0f, 0.0f, 2.0f);
static AwesomeCVar s_cvBloomThreshold = AwesomeCVar::Float("bloomThreshold", 1.0f, 0.1f, 8.0f);
static AwesomeCVar s_cvSsao = AwesomeCVar::Float("ssao", 0.0f, 0.0f, 2.0f);
static AwesomeCVar s_cvSsaoRadius = AwesomeCVar::Float("ssaoRadius", 1.5f, 0.25f, 8.0f);
static AwesomeCVar s_cvSsaoDistance = AwesomeCVar::Float("ssaoDistance", 200.0f, 20.0f, 2000.0f, nullptr, "%.0f");
static AwesomeCVar s_cvPerPixel = AwesomeCVar::Int("lightPerPixel", 0, 0, 1);
static AwesomeCVar s_cvDynamic = AwesomeCVar::Int("lightDynamic", 0, 0, 1);
static AwesomeCVar s_cvDynamicIntensity = AwesomeCVar::Float("lightDynamicIntensity", 1.0f, 0.0f, 8.0f);
static AwesomeCVar s_cvDynamicM2 = AwesomeCVar::Float("lightDynamicM2", 0.6f, 0.0f, 8.0f);
static AwesomeCVar s_cvDynamicWmo = AwesomeCVar::Float("lightDynamicWmo", 0.5f, 0.0f, 8.0f);
static AwesomeCVar s_cvDynamicDistance = AwesomeCVar::Float("lightDynamicDistance", 100.0f, 20.0f, 500.0f, nullptr, "%.0f");
static AwesomeCVar s_cvFlicker = AwesomeCVar::Float("lightFlicker", 0.3f, 0.0f, 1.0f);
static AwesomeCVar s_cvWrap = AwesomeCVar::Float("lightWrap", 0.25f, 0.0f, 1.0f);
static AwesomeCVar s_cvSpecular = AwesomeCVar::Int("lightSpecular", 0, 0, 1);
static AwesomeCVar s_cvSpecularM2 = AwesomeCVar::Float("lightSpecularM2", 0.3f, 0.0f, 4.0f);
static AwesomeCVar s_cvSpecularWmo = AwesomeCVar::Float("lightSpecularWmo", 1.0f, 0.0f, 4.0f);
static AwesomeCVar s_cvSkyAmbient = AwesomeCVar::Float("lightSkyAmbient", 0.0f, 0.0f, 1.0f);
static AwesomeCVar s_cvHeightFog = AwesomeCVar::Float("fogHeight", 0.0f, 0.0f, 0.1f, nullptr, "%.4f");
static AwesomeCVar s_cvHeightFogFalloff = AwesomeCVar::Float("fogHeightFalloff", 0.05f, 0.001f, 1.0f, nullptr, "%.3f");
static AwesomeCVar s_cvHeightFogOffset = AwesomeCVar::Float("fogHeightOffset", -5.0f, -500.0f, 500.0f, nullptr, "%.0f");
static AwesomeCVar s_cvHeightFogMax = AwesomeCVar::Float("fogHeightMax", 0.85f, 0.0f, 1.0f);
static AwesomeCVar s_cvScattering = AwesomeCVar::Float("fogScattering", 0.0f, 0.0f, 1.0f);
// the passes on the login screen's model, for a look without a realm: 1, 2 with three test lights, 3 as 2 but on and
// off by turns every 4 seconds
static AwesomeCVar s_cvGlueTest = AwesomeCVar::Int("lightingGlueTest", 0, 0, 3);
// 1: the SSAO term alone over the world, to tune ssao / ssaoRadius
static AwesomeCVar s_cvDebug = AwesomeCVar::Int("lightingDebug", 0, 0, 1);

static AwesomeCVar* const s_cvars[] = {
    &s_cvGlueTest,
    &s_cvDebug,
    &s_cvHdr, &s_cvExposure, &s_cvTonemap, &s_cvBloom, &s_cvBloomThreshold, &s_cvSsao, &s_cvSsaoRadius, &s_cvSsaoDistance,
    &s_cvPerPixel, &s_cvDynamic, &s_cvDynamicIntensity, &s_cvDynamicM2, &s_cvDynamicWmo, &s_cvDynamicDistance, &s_cvFlicker,
    &s_cvWrap, &s_cvSpecular, &s_cvSpecularM2, &s_cvSpecularWmo, &s_cvSkyAmbient, &s_cvHeightFog, &s_cvHeightFogFalloff,
    &s_cvHeightFogOffset, &s_cvHeightFogMax, &s_cvScattering,
};

// the world pass of this frame, between BeginWorld and EndWorld
static bool s_inWorld = false;
static GxLightingParams s_params;
static std::vector<GxLightingLight> s_lights;

// what /lighting stats reports, from the last SetupView
static uint32_t s_statsM2 = 0;
static uint32_t s_statsWmo = 0;
static uint32_t s_statsKept = 0;

static uint32_t Features() {
    uint32_t features = 0;

    if (s_cvHdr.m_intValue) {
        features |= GxLighting_Hdr;

        if (s_cvBloom.m_floatValue > 0.0f) {
            features |= GxLighting_Bloom;
        }
    }

    if (s_cvSsao.m_floatValue > 0.0f) {
        features |= GxLighting_Ssao;
    }

    if (s_cvPerPixel.m_intValue) {
        features |= GxLighting_PerPixel;
    }

    if (s_cvDynamic.m_intValue) {
        features |= GxLighting_Dynamic;
    }

    if (s_cvSpecular.m_intValue) {
        features |= GxLighting_Specular;
    }

    if (s_cvSkyAmbient.m_floatValue > 0.0f) {
        features |= GxLighting_Hemisphere;
    }

    if (s_cvHeightFog.m_floatValue > 0.0f) {
        features |= GxLighting_HeightFog;
    }

    if (s_cvScattering.m_floatValue > 0.0f) {
        features |= GxLighting_Scattering;
    }

    return features;
}

static C3Vector ColorOf(const CImVector& c) {
    return C3Vector(c.r * (1.0f / 255.0f), c.g * (1.0f / 255.0f), c.b * (1.0f / 255.0f));
}

static C3Vector Lerp(const C3Vector& a, const C3Vector& b, float t) {
    return C3Vector(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

static float Decode(float c) {
    return c > 0.0f ? powf(c, 2.2f) : 0.0f;
}

static C3Vector Transform(const C3Vector& p, const C44Matrix& m) {
    return C3Vector(p.x * m.a0 + p.y * m.b0 + p.z * m.c0 + m.d0, p.x * m.a1 + p.y * m.b1 + p.z * m.c1 + m.d1, p.x * m.a2 + p.y * m.b2 + p.z * m.c2 + m.d2);
}

static float DistanceSquared(const C3Vector& a, const C3Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// a warm light (a flame) flickers; a cold or white one (magic, a lamp) does not
static float FlickerFor(const C3Vector& linear) {
    return linear.x > linear.y * 1.1f && linear.y > linear.z ? s_cvFlicker.m_floatValue : 0.0f;
}

static void AddLight(const C3Vector& position, const C3Vector& gamma, float scale, float start, float radius, const C3Vector& camera, float reach) {
    if (radius <= 0.1f || scale <= 0.0f) {
        return;
    }

    float limit = reach + radius;

    if (DistanceSquared(position, camera) > limit * limit) {
        return;
    }

    GxLightingLight light;
    light.m_position = position;
    light.m_radius = radius;
    light.m_start = start < radius ? (start > 0.0f ? start : 0.0f) : radius * 0.25f;
    // a model's light can be animated far past 1; a few times white is as bright as a light gets here
    light.m_color = C3Vector(fminf(Decode(gamma.x), 4.0f) * scale, fminf(Decode(gamma.y), 4.0f) * scale, fminf(Decode(gamma.z), 4.0f) * scale);
    light.m_flicker = FlickerFor(light.m_color);
    light.m_specular = 1.0f;

    if (light.m_color.x + light.m_color.y + light.m_color.z < 0.003f) {
        return;
    }

    s_lights.push_back(light);
}

// the point lights of the animated models: CM2Scene's light grid (20-yard cells, 64 x 64, wrapping)
static void GatherM2Lights(CM2Scene* scene, const C3Vector& camera, float reach) {
    if (!scene || !scene->m_lightGrid || s_cvDynamicM2.m_floatValue <= 0.0f) {
        return;
    }

    const float cellScale = 0.05f;
    int32_t cells = static_cast<int32_t>(ceilf(reach * cellScale)) + 1;
    cells = cells > 30 ? 30 : cells;
    int32_t cx = static_cast<int32_t>(floorf(camera.x * cellScale));
    int32_t cy = static_cast<int32_t>(floorf(camera.y * cellScale));
    size_t before = s_lights.size();

    for (int32_t dy = -cells; dy <= cells; dy++) {
        for (int32_t dx = -cells; dx <= cells; dx++) {
            CM2Light* light = scene->m_lightGrid[((cy + dy) & 63) * 64 + ((cx + dx) & 63)];

            for (; light; light = light->m_lightNext) {
                // CM2Scene::SelectLights' test: a light its model did not animate this frame is gone
                if (!light->m_visible || light->m_type != M2LIGHT_POINT || (light->m_scene && light->m_stamp != scene->m_frameStamp)) {
                    continue;
                }

                // every light in the grid is a model's (M2ModelLight), so its attenuation tracks are beside it
                const M2ModelLight* owner = reinterpret_cast<const M2ModelLight*>(reinterpret_cast<const char*>(light) - offsetof(M2ModelLight, m_light));
                float end = owner->m_attenuationEndTrack.m_currentValue;
                float start = owner->m_attenuationStartTrack.m_currentValue;

                if (!(end > 0.25f && end < 80.0f)) {
                    end = 8.0f;
                    start = 1.0f;
                }

                AddLight(light->m_pos, light->m_dirColor, s_cvDynamicM2.m_floatValue, start, end, camera, reach);
            }
        }
    }

    s_statsM2 = static_cast<uint32_t>(s_lights.size() - before);
}

// the WMOs' MOLT lights: in each placed WMO's own space, through its placement matrix
static void GatherWmoLights(const C3Vector& camera, float reach) {
    if (s_cvDynamicWmo.m_floatValue <= 0.0f) {
        return;
    }

    size_t before = s_lights.size();

    for (CMapObjDef* def = CMap::mapObjDefHash.m_fulllist.Head(); def; def = CMap::mapObjDefHash.Next(def)) {
        CMapObj* owner = def->m_owner;

        if ((def->m_flags & 0x20) || !owner || !owner->m_lightList || owner->m_ligtsCount <= 0) {
            continue;
        }

        // the placement's bounds within reach of the camera
        const CAaBox& box = def->m_bbox;
        float nx = camera.x < box.b.x ? box.b.x : (camera.x > box.t.x ? box.t.x : camera.x);
        float ny = camera.y < box.b.y ? box.b.y : (camera.y > box.t.y ? box.t.y : camera.y);
        float nz = camera.z < box.b.z ? box.b.z : (camera.z > box.t.z ? box.t.z : camera.z);

        if (DistanceSquared(C3Vector(nx, ny, nz), camera) > (reach + 30.0f) * (reach + 30.0f)) {
            continue;
        }

        for (int32_t i = 0; i < owner->m_ligtsCount; i++) {
            const SMOLight& molt = owner->m_lightList[i];

            // omni and spot lights (a spot lights all round here); directional and ambient ones are the stock lighting
            if (molt.m_type > 1 || molt.m_attenEnd <= 0.25f) {
                continue;
            }

            C3Vector color = ColorOf(molt.m_color);
            float intensity = molt.m_intensity > 0.0f ? molt.m_intensity : 1.0f;
            AddLight(Transform(molt.m_position, def->m_mat), color, s_cvDynamicWmo.m_floatValue * intensity, molt.m_attenStart, molt.m_attenEnd, camera, reach);
        }
    }

    s_statsWmo = static_cast<uint32_t>(s_lights.size() - before);
}

static void BuildParams(GxLightingParams& p, const C3Vector& camera) {
    p.m_features = Features();
    p.m_exposure = s_cvExposure.m_floatValue;
    p.m_tonemap = static_cast<uint32_t>(s_cvTonemap.m_intValue);
    p.m_bloomIntensity = s_cvBloom.m_floatValue;
    p.m_bloomThreshold = s_cvBloomThreshold.m_floatValue;
    p.m_ssaoIntensity = s_cvSsao.m_floatValue;
    p.m_ssaoRadius = s_cvSsaoRadius.m_floatValue;
    p.m_ssaoDistance = s_cvSsaoDistance.m_floatValue;
    p.m_lightWrap = s_cvWrap.m_floatValue;
    p.m_dynamicIntensity = s_cvDynamicIntensity.m_floatValue;
    p.m_specularM2 = s_cvSpecularM2.m_floatValue;
    p.m_specularWmo = s_cvSpecularWmo.m_floatValue;
    p.m_lightDistance = s_cvDynamicDistance.m_floatValue;
    p.m_hemisphere = s_cvSkyAmbient.m_floatValue;
    p.m_scattering = s_cvScattering.m_floatValue;
    p.m_heightFogDensity = s_cvHeightFog.m_floatValue;
    p.m_heightFogFalloff = s_cvHeightFogFalloff.m_floatValue;
    p.m_heightFogMax = s_cvHeightFogMax.m_floatValue;
    p.m_cameraPosition = camera;
    p.m_time = static_cast<float>(OsGetAsyncTimeMs() % 3600000u) * 0.001f;
    p.m_debug = static_cast<uint32_t>(s_cvDebug.m_intValue);

    // the zone's colours this moment (Light.dbc through DayNight): the sky's top and middle, the ground lit by the sun
    DayNight::DNInfo* info = DayNight::GetInfo();

    if (!info) {
        return;
    }

    const DayNight::CurrentLight& bands = info->m_bands;
    p.m_skyColor = Lerp(ColorOf(bands.m_skyColors[0]), ColorOf(bands.m_skyColors[1]), 0.5f);
    float interior = info->m_interiorFogBlend < 0.0f ? 0.0f : (info->m_interiorFogBlend > 1.0f ? 1.0f : info->m_interiorFogBlend);
    C3Vector fog = Lerp(ColorOf(info->m_fog.m_color), ColorOf(info->m_fogInterior.m_color), interior);
    p.m_groundColor = Lerp(fog, ColorOf(bands.m_diffuse), 0.6f);
    p.m_fogColor = fog;
    p.m_fogStart = info->m_fog.m_start + (info->m_fogInterior.m_start - info->m_fog.m_start) * interior;
    p.m_fogEnd = info->m_fog.m_end + (info->m_fogInterior.m_end - info->m_fog.m_end) * interior;
    p.m_fogDensity = info->m_fog.m_density > 0.0f ? info->m_fog.m_density : 1.0f;
    p.m_sunColor = ColorOf(bands.m_diffuse);
    p.m_sunDirection = info->m_light0.m_dir;
    p.m_heightFogBase = info->m_playerPos.z + s_cvHeightFogOffset.m_floatValue;
}

void AwesomeLightingBeginWorld() {
    s_inWorld = false;

    if (!g_gxLighting) {
        return;
    }

    DayNight::DNInfo* info = DayNight::GetInfo();
    BuildParams(s_params, info ? info->m_cameraPos : C3Vector(0.0f, 0.0f, 0.0f));
    g_gxLighting->BeginWorld(s_params);
    s_inWorld = true;
}

static void SetupView(CM2Scene* scene, const C3Vector& camera, bool wmo, bool testLights) {
    BuildParams(s_params, camera);
    s_lights.clear();
    s_statsM2 = 0;
    s_statsWmo = 0;

    if (s_params.m_features & GxLighting_Dynamic) {
        float reach = s_cvDynamicDistance.m_floatValue;
        GatherM2Lights(scene, camera, reach);

        if (wmo) {
            GatherWmoLights(camera, reach);
        }

        if (testLights) {
            // lightingGlueTest 2: three coloured lights in front of the camera (view space: x right, y up, z ahead)
            CGxMatrixStack& viewStack = g_theGxDevicePtr->m_xforms[GxXform_View];
            C44Matrix toWorld = viewStack.m_mtx[viewStack.m_level].Inverse();
            static const float s_test[3][7] = { { -8.0f, 0.0f, 15.0f, 20.0f, 1.0f, 0.25f, 0.1f }, { 10.0f, 3.0f, 35.0f, 30.0f, 0.2f, 1.0f, 0.3f }, { 0.0f, -5.0f, 60.0f, 45.0f, 0.3f, 0.4f, 1.0f } };

            for (const float* t : s_test) {
                GxLightingLight light;
                light.m_position = Transform(C3Vector(t[0], t[1], t[2]), toWorld);
                light.m_radius = t[3];
                light.m_start = t[3] * 0.2f;
                light.m_color = C3Vector(t[4] * 3.0f, t[5] * 3.0f, t[6] * 3.0f);
                light.m_flicker = 0.0f;
                light.m_specular = 1.0f;
                s_lights.push_back(light);
            }
        }

        // the nearest ones, by the distance to their edge, when there are more than the device takes
        const size_t keep = 256;

        if (s_lights.size() > keep) {
            auto nearer = [&camera](const GxLightingLight& a, const GxLightingLight& b) {
                return sqrtf(DistanceSquared(a.m_position, camera)) - a.m_radius < sqrtf(DistanceSquared(b.m_position, camera)) - b.m_radius;
            };

            std::nth_element(s_lights.begin(), s_lights.begin() + keep, s_lights.end(), nearer);
            s_lights.resize(keep);
        }
    }

    s_statsKept = static_cast<uint32_t>(s_lights.size());
    g_gxLighting->SetupView(s_params, s_lights.data(), s_statsKept);
}

void AwesomeLightingSetupView(const C3Vector& camera) {
    if (!s_inWorld || !g_gxLighting) {
        return;
    }

    SetupView(World::s_m2Scene, camera, true, false);
}

// lightingGlueTest: the login screen's model scene goes through the same passes as the world (CSimpleModelFFX::Render
// around it, CSimpleModel::RenderModel's callback inside), so they can be seen without a realm. 2 adds test lights
static void GlueRenderCallback(CSimpleModel* model, uint32_t phase, const C3Vector& camera) {
    if (!s_inWorld || !g_gxLighting) {
        return;
    }

    if (phase == 0) {
        SetupView(model->GetScene(), camera, false, s_cvGlueTest.m_intValue >= 2);
    } else if (phase == 1) {
        g_gxLighting->OpaqueDone();
    }
}

void AwesomeLightingGlueBegin() {
    if (!s_cvGlueTest.m_intValue || !g_gxLighting) {
        return;
    }

    // 3: on and off by turns every 4 seconds, to compare the two in one session (the login scene is random)
    if (s_cvGlueTest.m_intValue == 3 && (OsGetAsyncTimeMs() / 4000) % 2) {
        return;
    }

    AwesomeLightingBeginWorld();
    CSimpleModel::s_renderCallback = GlueRenderCallback;
}

void AwesomeLightingGlueEnd() {
    if (CSimpleModel::s_renderCallback == GlueRenderCallback) {
        CSimpleModel::s_renderCallback = nullptr;
        AwesomeLightingEndWorld();
    }
}

void AwesomeLightingOpaqueDone() {
    if (s_inWorld && g_gxLighting) {
        g_gxLighting->OpaqueDone();
    }
}

void AwesomeLightingEndWorld() {
    if (s_inWorld && g_gxLighting) {
        g_gxLighting->EndWorld();
    }

    s_inWorld = false;
}

// GetLightingStats(): D3D11 lighting available, the feature mask, M2 and WMO lights found, lights drawn
static int32_t Script_GetLightingStats(lua_State* L) {
    GxLightingStats stats = {};

    if (g_gxLighting) {
        g_gxLighting->GetStats(stats);
    }

    lua_pushboolean(L, g_gxLighting != nullptr);
    lua_pushnumber(L, Features());
    lua_pushnumber(L, s_statsM2);
    lua_pushnumber(L, s_statsWmo);
    lua_pushnumber(L, stats.m_lights);
    return 5;
}

static const char* s_slashCommand = R"lua(
SLASH_MODERNLIGHTING1 = "/lighting"
SlashCmdList.MODERNLIGHTING = function(msg)
    local word = (msg or ""):match("^%s*(%S*)"):lower()
    local on = {
        hdr = "1", hdrTonemap = "1", bloom = "0.3", ssao = "0.8", lightPerPixel = "1", lightDynamic = "1",
        lightSpecular = "1", lightSkyAmbient = "0.5", fogHeight = "0.003", fogScattering = "0.4",
    }
    if word == "on" or word == "off" then
        for name, value in pairs(on) do
            SetCVar(name, word == "on" and value or (name == "hdrTonemap" and "1" or "0"))
        end
        print(("|cff33ff99Lighting|r %s"):format(word == "on" and "on (the WMO specular shaders also need specular 1 and a restart)" or "off"))
        return
    end
    local d3d11, features, m2, wmo, drawn = GetLightingStats()
    if not d3d11 then
        print("|cff33ff99Lighting|r needs the D3D11 device: SET gxApi \"D3D11\" (or -d3d11) and restart")
        return
    end
    print(("|cff33ff99Lighting|r features 0x%03X; lights: %d from models, %d from WMOs, %d drawn"):format(features, m2, wmo, drawn))
    print("  /lighting on | off; CVars: hdr hdrExposure hdrTonemap bloom bloomThreshold ssao ssaoRadius ssaoDistance")
    print("  lightPerPixel lightDynamic lightDynamicIntensity lightDynamicM2 lightDynamicWmo lightDynamicDistance lightFlicker")
    print("  lightWrap lightSpecular lightSpecularM2 lightSpecularWmo lightSkyAmbient fogHeight fogHeightFalloff")
    print("  fogHeightOffset fogHeightMax fogScattering")
end
)lua";

void AwesomeLightingRegisterCVars() {
    for (AwesomeCVar* cvar : s_cvars) {
        cvar->Register();
    }
}

void AwesomeLightingRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetLightingStats", Script_GetLightingStats },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
    FrameScript_Execute(s_slashCommand, "ModernLighting", nullptr);
}
