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
// shadowSun: the sun's shadow (the stock cascades, extShadowQuality 5) taken off the direct sunlight alone, per pixel
//   and as soft as the sun's disc makes it (shadowPenumbra), instead of the stock 30% off all of the light; grass and
//   unlit surfaces get their stock shadow deepened to match (shadowSunStrength). lightSunIntensity and
//   lightAmbientIntensity weigh the two lights on lit surfaces. lightCanopy: the ambient dimmed under leaves and roofs,
//   from the same maps. lightShafts: sunlight scattered in the air and shadowed by the cascades, so it falls in shafts
//   through gaps in the leaves (lightShaftsDensity per yard, gathered lightShaftsDistance yards along each view ray).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <vector>
#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Frame/CSimpleModel.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxMatrixStack.h"
#include "Engine/Source/Gx/GxLighting.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Model2/M2ModelLight.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/ShaderEffect/ShaderEffect.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"
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
// contrast-adaptive sharpening of the world (not the UI) as it is tonemapped: detail softened by mips and distance
// comes back; needs hdr 1 (0 off, to 1)
static AwesomeCVar s_cvSharpen = AwesomeCVar::Float("sharpen", 0.0f, 0.0f, 1.0f);
static AwesomeCVar s_cvSsao = AwesomeCVar::Float("ssao", 0.0f, 0.0f, 2.0f);
static AwesomeCVar s_cvSsaoRadius = AwesomeCVar::Float("ssaoRadius", 1.5f, 0.25f, 8.0f);
static AwesomeCVar s_cvSsaoDistance = AwesomeCVar::Float("ssaoDistance", 200.0f, 20.0f, 2000.0f, nullptr, "%.0f");
static AwesomeCVar s_cvPerPixel = AwesomeCVar::Int("lightPerPixel", 0, 0, 1);
static AwesomeCVar s_cvDynamic = AwesomeCVar::Int("lightDynamic", 0, 0, 1);
static AwesomeCVar s_cvDynamicIntensity = AwesomeCVar::Float("lightDynamicIntensity", 1.0f, 0.0f, 8.0f);
static AwesomeCVar s_cvDynamicM2 = AwesomeCVar::Float("lightDynamicM2", 0.6f, 0.0f, 8.0f);
static AwesomeCVar s_cvDynamicWmo = AwesomeCVar::Float("lightDynamicWmo", 0.5f, 0.0f, 8.0f);
static AwesomeCVar s_cvDynamicDistance = AwesomeCVar::Float("lightDynamicDistance", 100.0f, 20.0f, 500.0f, nullptr, "%.0f");
static AwesomeCVar s_cvFlicker = AwesomeCVar::Float("lightFlicker", 0.0f, 0.0f, 1.0f);
static AwesomeCVar s_cvWrap = AwesomeCVar::Float("lightWrap", 0.25f, 0.0f, 1.0f);
static AwesomeCVar s_cvSpecular = AwesomeCVar::Int("lightSpecular", 0, 0, 1);
static AwesomeCVar s_cvSpecularM2 = AwesomeCVar::Float("lightSpecularM2", 0.1f, 0.0f, 4.0f);
static AwesomeCVar s_cvSpecularWmo = AwesomeCVar::Float("lightSpecularWmo", 1.0f, 0.0f, 4.0f);
static AwesomeCVar s_cvSkyAmbient = AwesomeCVar::Float("lightSkyAmbient", 0.0f, 0.0f, 1.0f);
static AwesomeCVar s_cvHeightFog = AwesomeCVar::Float("fogHeight", 0.0f, 0.0f, 0.1f, nullptr, "%.4f");
static AwesomeCVar s_cvHeightFogFalloff = AwesomeCVar::Float("fogHeightFalloff", 0.05f, 0.001f, 1.0f, nullptr, "%.3f");
static AwesomeCVar s_cvHeightFogOffset = AwesomeCVar::Float("fogHeightOffset", -5.0f, -500.0f, 500.0f, nullptr, "%.0f");
static AwesomeCVar s_cvHeightFogMax = AwesomeCVar::Float("fogHeightMax", 0.85f, 0.0f, 1.0f);
static AwesomeCVar s_cvScattering = AwesomeCVar::Float("fogScattering", 0.0f, 0.0f, 1.0f);
// fires as lights: the warm additive particle emitters of the world's models (bonfires, braziers, torches), which carry
// no light of their own (0 off, else the intensity)
static AwesomeCVar s_cvFire = AwesomeCVar::Float("lightFire", 0.0f, 0.0f, 4.0f);
// sun shadows, the canopy and the shafts (all need extShadowQuality 5: the cascades)
static AwesomeCVar s_cvSunShadow = AwesomeCVar::Int("shadowSun", 0, 0, 1);
static AwesomeCVar s_cvSunShadowStrength = AwesomeCVar::Float("shadowSunStrength", 1.0f, 0.0f, 1.0f);
static AwesomeCVar s_cvPenumbra = AwesomeCVar::Float("shadowPenumbra", 1.5f, 0.0f, 6.0f);
// yards of depth bias on top of the stock one: raise it where flat ground shades itself (a darker square around the player)
static AwesomeCVar s_cvSunBias = AwesomeCVar::Float("shadowSunBias", 0.5f, 0.0f, 4.0f);
// the first cascade the sun shadows read: 1 the 80-yard one, 2 the 320-yard one, 3 the 1280-yard one (a test)
static AwesomeCVar s_cvSunFirstCascade = AwesomeCVar::Int("shadowSunFirstCascade", 1, 1, 3);
static AwesomeCVar s_cvSunIntensity = AwesomeCVar::Float("lightSunIntensity", 1.0f, 0.0f, 3.0f);
static AwesomeCVar s_cvAmbientIntensity = AwesomeCVar::Float("lightAmbientIntensity", 1.0f, 0.0f, 2.0f);
static AwesomeCVar s_cvCanopy = AwesomeCVar::Float("lightCanopy", 0.0f, 0.0f, 1.0f);
static AwesomeCVar s_cvShafts = AwesomeCVar::Float("lightShafts", 0.0f, 0.0f, 4.0f);
static AwesomeCVar s_cvShaftsDensity = AwesomeCVar::Float("lightShaftsDensity", 0.012f, 0.001f, 0.2f, nullptr, "%.3f");
static AwesomeCVar s_cvShaftsDistance = AwesomeCVar::Float("lightShaftsDistance", 120.0f, 20.0f, 300.0f, nullptr, "%.0f");
// the passes on the login screen's model, for a look without a realm: 1, 2 with three test lights, 3 as 2 but on and
// off by turns every 4 seconds
static AwesomeCVar s_cvGlueTest = AwesomeCVar::Int("lightingGlueTest", 0, 0, 3);
// 1: the SSAO term alone; 2: the lit surfaces by kind (green relit per pixel, red unlit, blue face normal); 3: the dynamic light alone;
// 6: the sunlight's shadow alone; 7: the canopy's dimming alone; 8: the sun shafts alone
static AwesomeCVar s_cvDebug = AwesomeCVar::Int("lightingDebug", 0, 0, 9);

static AwesomeCVar* const s_cvars[] = {
    &s_cvGlueTest,
    &s_cvDebug,
    &s_cvHdr, &s_cvExposure, &s_cvTonemap, &s_cvBloom, &s_cvBloomThreshold, &s_cvSharpen, &s_cvSsao, &s_cvSsaoRadius, &s_cvSsaoDistance,
    &s_cvPerPixel, &s_cvDynamic, &s_cvDynamicIntensity, &s_cvDynamicM2, &s_cvDynamicWmo, &s_cvDynamicDistance, &s_cvFlicker,
    &s_cvWrap, &s_cvSpecular, &s_cvSpecularM2, &s_cvSpecularWmo, &s_cvSkyAmbient, &s_cvHeightFog, &s_cvHeightFogFalloff,
    &s_cvHeightFogOffset, &s_cvHeightFogMax, &s_cvScattering, &s_cvFire,
    &s_cvSunShadow, &s_cvSunShadowStrength, &s_cvPenumbra, &s_cvSunBias, &s_cvSunFirstCascade, &s_cvSunIntensity, &s_cvAmbientIntensity, &s_cvCanopy,
    &s_cvShafts, &s_cvShaftsDensity, &s_cvShaftsDistance,
};

// the world pass of this frame, between BeginWorld and EndWorld
static bool s_inWorld = false;
static GxLightingParams s_params;
static std::vector<GxLightingLight> s_lights;

// what /lighting stats reports, from the last SetupView
static uint32_t s_statsM2 = 0;
static uint32_t s_statsWmo = 0;
static uint32_t s_statsKept = 0;
static uint32_t s_statsViews = 0;
static uint32_t s_statsFires = 0;
static uint32_t s_statsFireEmitters = 0;
static C3Vector s_statsOrigin;
static GxLightingLight s_statsLight;

// where the light gathering dropped lights, for /lighting
struct GatherDiag {
    uint32_t m_m2Grid;        // 1 when the M2 scene has a light grid
    uint32_t m_m2Seen;
    uint32_t m_m2Invisible;
    uint32_t m_m2NotPoint;
    uint32_t m_m2Stale;
    uint32_t m_wmoDefs;
    uint32_t m_wmoWithLights;
    uint32_t m_wmoInReach;
    uint32_t m_moltSeen;
    uint32_t m_moltSkipped;   // directional / ambient, or no attenuation
    uint32_t m_rejected;      // too dark, too small or out of reach
    uint32_t m_rejectRadius;
    uint32_t m_rejectFar;
    uint32_t m_rejectDark;
    float m_nearestRejected;  // distance of the nearest light dropped as too far (0: none)
    C3Vector m_nearestPos;
    float m_nearestRadius;
    C3Vector m_nearestColor;  // the gamma colour handed in
    float m_nearestScale;
};

static GatherDiag s_diag;

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

    if (s_cvSunShadow.m_intValue) {
        features |= GxLighting_SunShadow;

        if (s_cvCanopy.m_floatValue > 0.0f) {
            features |= GxLighting_Canopy;
        }
    }

    if (s_cvShafts.m_floatValue > 0.0f) {
        features |= GxLighting_Shafts;
    }

    return features;
}

// the sun's shadow maps of this frame, as the stock shaders read them (CShadowCache::SetShadowMapGenericGlobal /
// SetShadowMapTerrain): only the cascades of extShadowQuality 5 with depth-compare filtering, and only once the shadow
// pass has drawn them (CShadowQuery::Update, before SetupView)
static void GatherShadows(GxLightingShadows& shadows) {
    shadows = GxLightingShadows();

    if (CShadowCache::s_shadowMode != SHADOW_VERY_HIGH || !CShaderEffect::s_usePcfFiltering || CShadowCache::s_recreateTextures) {
        return;
    }

    CShadowBand* bands = CShadowCache::s_extShadowBands;

    for (uint32_t i = 0; i < 3; i++) {
        if (!bands[i].m_textures[bands[i].m_current]) {
            return;
        }
    }

    if (CShadowCache::s_extDynShadow.m_textures[1]) {
        shadows.m_maps[0] = TextureGetGxTex(CShadowCache::s_extDynShadow.m_textures[1], 1, nullptr);
    }

    shadows.m_radius[0] = CShadowCache::s_extDynShadow.m_radius;

    for (uint32_t i = 0; i < 3; i++) {
        shadows.m_maps[1 + i] = TextureGetGxTex(bands[i].m_textures[bands[i].m_current], 1, nullptr);
        shadows.m_radius[1 + i] = bands[i].m_radius;
    }

    for (uint32_t i = 0; i < 12; i++) {
        const C4Vector& row = CShadowCache::s_shadowMatrices[i];
        shadows.m_rows[i][0] = row.x;
        shadows.m_rows[i][1] = row.y;
        shadows.m_rows[i][2] = row.z;
        shadows.m_rows[i][3] = row.w;
    }

    shadows.m_depthYards = CShadowCache::s_shadowScale > 0.0f ? 1.0f / CShadowCache::s_shadowScale : 4000.0f;
    shadows.m_interior = CShadowCache::s_intDynShadow ? TextureGetGxTex(CShadowCache::s_intDynShadow, 1, nullptr) : nullptr;
}

static float LuminanceOf(const C3Vector& gamma) {
    return powf(gamma.x, 2.2f) * 0.2126f + powf(gamma.y, 2.2f) * 0.7152f + powf(gamma.z, 2.2f) * 0.0722f;
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

// flame: a fire or a model's light (a torch), which may flicker; a WMO's lights are its rooms' steady light
static void AddLight(const C3Vector& position, const C3Vector& gamma, float scale, float start, float radius, const C3Vector& camera, float reach, bool flame) {
    float distance = sqrtf(DistanceSquared(position, camera));

    if (s_diag.m_nearestRejected == 0.0f || distance < s_diag.m_nearestRejected) {
        s_diag.m_nearestRejected = distance;
        s_diag.m_nearestPos = position;
        s_diag.m_nearestRadius = radius;
        s_diag.m_nearestColor = gamma;
        s_diag.m_nearestScale = scale;
    }

    if (radius <= 0.1f || scale <= 0.0f) {
        s_diag.m_rejected++;
        s_diag.m_rejectRadius++;
        return;
    }

    float limit = reach + radius;

    if (distance > limit) {
        s_diag.m_rejected++;
        s_diag.m_rejectFar++;
        return;
    }

    GxLightingLight light;
    light.m_position = position;
    light.m_radius = radius;
    light.m_start = start < radius ? (start > 0.0f ? start : 0.0f) : radius * 0.25f;
    // a model's light can be animated far past 1; a few times white is as bright as a light gets here
    light.m_color = C3Vector(fminf(Decode(gamma.x), 4.0f) * scale, fminf(Decode(gamma.y), 4.0f) * scale, fminf(Decode(gamma.z), 4.0f) * scale);
    // a large light flickers less: a room lit as a whole should not pulse
    light.m_flicker = flame ? FlickerFor(light.m_color) * fminf(1.0f, 8.0f / radius) : 0.0f;
    light.m_specular = 1.0f;

    if (light.m_color.x + light.m_color.y + light.m_color.z < 0.003f) {
        s_diag.m_rejected++;
        s_diag.m_rejectDark++;
        return;
    }

    s_lights.push_back(light);
}

// the point lights of the animated models: CM2Scene's light grid (20-yard cells, 64 x 64, wrapping)
static void GatherM2Lights(CM2Scene* scene, const C3Vector& camera, float reach) {
    if (!scene || !scene->m_lightGrid || s_cvDynamicM2.m_floatValue <= 0.0f) {
        return;
    }

    s_diag.m_m2Grid = 1;

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
                s_diag.m_m2Seen++;

                if (!light->m_visible) {
                    s_diag.m_m2Invisible++;
                    continue;
                }

                if (light->m_type != M2LIGHT_POINT) {
                    s_diag.m_m2NotPoint++;
                    continue;
                }

                if (light->m_scene && light->m_stamp != scene->m_frameStamp) {
                    s_diag.m_m2Stale++;
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

                AddLight(light->m_pos, light->m_dirColor, s_cvDynamicM2.m_floatValue, start, end, camera, reach, true);
            }
        }
    }

    s_statsM2 = static_cast<uint32_t>(s_lights.size() - before);
}

// fires: an emitter whose particles glow (additive, or a bright alpha one) in a flame's colours. CM2Model::AnimateST
// hands each model with emitters over once they have animated; held and spell attachments are left out
struct FireCandidate {
    CM2Scene* m_scene;
    uint32_t m_stamp;
    C3Vector m_position;      // world
    C3Vector m_color;         // 0 to 1, gamma
    float m_size;             // particle size in yards
};

static std::vector<FireCandidate> s_fires;

static void OnParticlesAnimated(CM2Model* model) {
    if (model->m_attachParent || !model->m_shared || !model->m_scene || !model->m_particles || !model->m_boneMatrices) {
        return;
    }

    // a portrait or a glue scene that never reaches SetupView must not grow this for ever
    if (s_fires.size() > 8192) {
        s_fires.clear();
    }

    M2Data* data = model->m_shared->m_data;
    CM2Scene* scene = model->m_scene;
    float modelScale = model->m_scaleSq > 0.0f ? sqrtf(model->m_scaleSq) : 1.0f;

    for (uint32_t i = 0; i < data->m_particles.m_count; i++) {
        const M2Particle& particle = data->m_particles[i];
        const M2ModelParticle& state = model->m_particles[i];

        // visible counts, whatever the emission rate this frame: many flames pulse theirs, and the fire would flicker with it
        if (!state.m_visibilityTrack.m_currentValue || state.m_emissionRateTrack.m_currentValue <= 0.0f) {
            continue;
        }

        // 2 alpha, 3 additive without alpha, 4 additive
        uint8_t blend = particle.m_blendMode;

        if (blend < 2 || blend > 4 || !particle.m_colorTrack.m_values.m_count || particle.m_boneIndex >= data->m_bones.m_count) {
            continue;
        }

        // the colour in the middle of a particle's life (0 to 255)
        const C3Vector& mid = particle.m_colorTrack.m_values[particle.m_colorTrack.m_values.m_count / 2];
        C3Vector color(mid.x / 255.0f, mid.y / 255.0f, mid.z / 255.0f);

        // a flame: red over green over blue, and bright; an alpha-blended one brighter still
        if (!(color.x > (blend == 2 ? 0.75f : 0.5f) && color.x >= color.y * 1.15f && color.y >= color.z * 1.1f)) {
            continue;
        }

        float size = 0.5f;

        if (particle.m_scaleTrack.m_values.m_count) {
            size = particle.m_scaleTrack.m_values[particle.m_scaleTrack.m_values.m_count / 2].x * modelScale;
        }

        FireCandidate fire;
        fire.m_scene = scene;
        fire.m_stamp = scene->m_frameStamp;
        // as the model's lights: the emitter through its bone, from the scene's view space to the world
        fire.m_position = Transform(Transform(particle.m_position, model->m_boneMatrices[particle.m_boneIndex]), scene->m_viewInv);
        fire.m_color = color;
        fire.m_size = size;
        s_fires.push_back(fire);
    }
}

// the emitters of one fire (a bonfire has several) merged into one light
static void GatherFireLights(CM2Scene* scene, const C3Vector& camera, float reach) {
    s_statsFires = 0;
    s_statsFireEmitters = 0;

    if (s_cvFire.m_floatValue <= 0.0f || !scene) {
        s_fires.clear();
        return;
    }

    struct Fire {
        C3Vector m_sum;
        C3Vector m_color;
        float m_size;
        uint32_t m_count;
    };

    std::vector<Fire> fires;

    for (const FireCandidate& candidate : s_fires) {
        if (candidate.m_scene != scene || candidate.m_stamp != scene->m_frameStamp) {
            continue;
        }

        s_statsFireEmitters++;
        Fire* into = nullptr;

        for (Fire& fire : fires) {
            C3Vector center = fire.m_sum * (1.0f / fire.m_count);

            if (DistanceSquared(center, candidate.m_position) < 2.5f * 2.5f) {
                into = &fire;
                break;
            }
        }

        if (!into) {
            fires.push_back(Fire { C3Vector(0.0f, 0.0f, 0.0f), C3Vector(0.0f, 0.0f, 0.0f), 0.0f, 0 });
            into = &fires.back();
        }

        into->m_sum = into->m_sum + candidate.m_position;
        into->m_color.x = fmaxf(into->m_color.x, candidate.m_color.x);
        into->m_color.y = fmaxf(into->m_color.y, candidate.m_color.y);
        into->m_color.z = fmaxf(into->m_color.z, candidate.m_color.z);
        into->m_size = fmaxf(into->m_size, candidate.m_size);
        into->m_count++;
    }

    s_fires.clear();

    for (const Fire& fire : fires) {
        // a torch lights a few yards, a bonfire with many emitters much more
        // a candle lights a yard or three, a brazier several, a bonfire up to 12
        float radius = 3.0f + fire.m_size * 2.0f + 0.75f * static_cast<float>(fire.m_count < 6 ? fire.m_count : 6);
        radius = radius > 12.0f ? 12.0f : radius;
        float strength = s_cvFire.m_floatValue * 0.3f * (0.7f + 0.1f * static_cast<float>(fire.m_count < 3 ? fire.m_count : 3));
        size_t before = s_lights.size();
        // the light sits a little above the base of the flames
        C3Vector position = fire.m_sum * (1.0f / fire.m_count);
        position.z += 0.5f;
        AddLight(position, fire.m_color, strength, radius * 0.15f, radius, camera, reach, true);

        if (s_lights.size() > before) {
            s_lights.back().m_kind = 1;
            s_statsFires++;
        }
    }
}

// the WMOs' MOLT lights: in each placed WMO's own space, through its placement matrix
static void GatherWmoLights(const C3Vector& camera, float reach) {
    if (s_cvDynamicWmo.m_floatValue <= 0.0f) {
        return;
    }

    size_t before = s_lights.size();

    for (CMapObjDef* def = CMap::mapObjDefHash.m_fulllist.Head(); def; def = CMap::mapObjDefHash.Next(def)) {
        CMapObj* owner = def->m_owner;
        s_diag.m_wmoDefs++;

        if ((def->m_flags & 0x20) || !owner || !owner->m_lightList || owner->m_ligtsCount <= 0) {
            continue;
        }

        s_diag.m_wmoWithLights++;

        // the placement's bounds within reach of the camera
        const CAaBox& box = def->m_bbox;
        float nx = camera.x < box.b.x ? box.b.x : (camera.x > box.t.x ? box.t.x : camera.x);
        float ny = camera.y < box.b.y ? box.b.y : (camera.y > box.t.y ? box.t.y : camera.y);
        float nz = camera.z < box.b.z ? box.b.z : (camera.z > box.t.z ? box.t.z : camera.z);

        if (DistanceSquared(C3Vector(nx, ny, nz), camera) > (reach + 30.0f) * (reach + 30.0f)) {
            continue;
        }

        s_diag.m_wmoInReach++;

        for (int32_t i = 0; i < owner->m_ligtsCount; i++) {
            const SMOLight& molt = owner->m_lightList[i];
            s_diag.m_moltSeen++;

            // omni and spot lights (a spot lights all round here); directional and ambient ones are the stock lighting
            if (molt.m_type > 1 || molt.m_attenEnd <= 0.25f) {
                s_diag.m_moltSkipped++;
                continue;
            }

            C3Vector color = ColorOf(molt.m_color);
            float intensity = molt.m_intensity > 0.0f ? molt.m_intensity : 1.0f;
            AddLight(Transform(molt.m_position, def->m_mat), color, s_cvDynamicWmo.m_floatValue * intensity, molt.m_attenStart, molt.m_attenEnd, camera, reach, false);
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
    p.m_sharpen = s_cvSharpen.m_floatValue;
    // MODIFIED: skyDither (AwesomeSky.cpp)
    p.m_dither = AwesomeSkyDither();
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
    p.m_sunShadowStrength = s_cvSunShadowStrength.m_floatValue;
    p.m_sunPenumbra = s_cvPenumbra.m_floatValue;
    p.m_sunBias = s_cvSunBias.m_floatValue;
    p.m_sunFirstCascade = static_cast<uint32_t>(s_cvSunFirstCascade.m_intValue);
    p.m_sunIntensity = s_cvSunIntensity.m_floatValue;
    p.m_ambientIntensity = s_cvAmbientIntensity.m_floatValue;
    p.m_canopy = s_cvCanopy.m_floatValue;
    p.m_shafts = s_cvShafts.m_floatValue;
    p.m_shaftsDensity = s_cvShaftsDensity.m_floatValue;
    p.m_shaftsDistance = s_cvShaftsDistance.m_floatValue;

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
    p.m_interior = interior;

    // a surface in full shade over one in full sun, the sun about 45 degrees up: what the stock shadow of grass and
    // unlit surfaces is deepened to
    float ambient = LuminanceOf(ColorOf(bands.m_ambient));
    float direct = LuminanceOf(ColorOf(bands.m_diffuse)) * 0.7f;
    float fraction = ambient + direct > 0.001f ? ambient / (ambient + direct) : 1.0f;
    p.m_shadowedFraction = fraction < 0.15f ? 0.15f : (fraction > 1.0f ? 1.0f : fraction);
}

void AwesomeLightingBeginWorld() {
    s_inWorld = false;

    // the outer cascades keep small casters while the sun shadows read them (before this frame's shadow pass)
    CShadowCache::s_fineCasters = g_gxLighting && s_cvSunShadow.m_intValue ? 1 : 0;

    if (!g_gxLighting) {
        return;
    }

    DayNight::DNInfo* info = DayNight::GetInfo();
    BuildParams(s_params, info ? info->m_cameraPos : C3Vector(0.0f, 0.0f, 0.0f));
    g_gxLighting->BeginWorld(s_params);
    s_inWorld = true;

    // fires as lights: the models hand over their emitters while the scene animates, before SetupView
    bool fires = s_cvFire.m_floatValue > 0.0f && (s_params.m_features & GxLighting_Dynamic);
    CM2Model::s_particlesAnimatedCallback = fires ? OnParticlesAnimated : nullptr;

    if (!fires) {
        s_fires.clear();
    }
}

// a light kept from frame to frame: the client animates a model (and so places its lights and flames) only on the frames
// it draws or updates it, so a light found one frame may be missing the next. Matched by position (world space), each
// fades in over 0.3 s and out over 0.6 s instead of switching
struct TrackedLight {
    GxLightingLight m_light;
    float m_weight;
    float m_unseen;       // seconds held off screen
    bool m_seen;
};

static std::vector<TrackedLight> s_tracked;
static uint32_t s_trackedTime = 0;

// true when a world position (relative to the render origin) is inside the view: a model there is drawn and animated,
// so a light of it that was not found this frame is really gone
static bool OnScreen(const C3Vector& relative) {
    CGxMatrixStack& viewStack = g_theGxDevicePtr->m_xforms[GxXform_View];
    const C44Matrix& view = viewStack.m_mtx[viewStack.m_level];
    const C44Matrix& proj = g_theGxDevicePtr->m_projNative;
    C3Vector v = Transform(relative, view);
    float x = v.x * proj.a0 + v.y * proj.b0 + v.z * proj.c0 + proj.d0;
    float y = v.x * proj.a1 + v.y * proj.b1 + v.z * proj.c1 + proj.d1;
    float w = v.x * proj.a3 + v.y * proj.b3 + v.z * proj.c3 + proj.d3;
    return w > 0.5f && fabsf(x) < w * 0.9f && fabsf(y) < w * 0.9f;
}

static void SmoothLights(const C3Vector& origin) {
    uint32_t now = OsGetAsyncTimeMs();
    float dt = s_trackedTime ? static_cast<float>(now - s_trackedTime) * 0.001f : 0.0f;
    dt = dt > 0.25f ? 0.25f : dt;
    s_trackedTime = now;

    for (TrackedLight& tracked : s_tracked) {
        tracked.m_seen = false;
    }

    // a found light's colour, radius and place eased toward over about a quarter second: a fire's emitters come and go,
    // and its light would jump with them
    float ease = 1.0f - expf(-dt / 0.25f);
    // a fire's emitters show and hide on their own; once found it settles over two seconds, so it does not pulse
    float easeFire = 1.0f - expf(-dt / 2.0f);

    for (const GxLightingLight& light : s_lights) {
        TrackedLight* match = nullptr;
        float best = 3.0f * 3.0f;

        for (TrackedLight& tracked : s_tracked) {
            float d = DistanceSquared(tracked.m_light.m_position, light.m_position);

            if (!tracked.m_seen && d < best) {
                best = d;
                match = &tracked;
            }
        }

        if (match) {
            GxLightingLight& kept = match->m_light;
            float rate = kept.m_kind == 1 && light.m_kind == 1 ? easeFire : ease;
            kept.m_position = kept.m_position + (light.m_position - kept.m_position) * rate;
            kept.m_color = kept.m_color + (light.m_color - kept.m_color) * rate;
            kept.m_radius += (light.m_radius - kept.m_radius) * rate;
            kept.m_start += (light.m_start - kept.m_start) * rate;
            kept.m_flicker = light.m_flicker;
            kept.m_specular = light.m_specular;
            match->m_seen = true;
        } else {
            s_tracked.push_back(TrackedLight { light, 0.0f, 0.0f, true });
        }
    }

    s_lights.clear();

    for (size_t i = 0; i < s_tracked.size();) {
        TrackedLight& tracked = s_tracked[i];
        if (tracked.m_seen) {
            tracked.m_weight += dt / 0.3f;
            tracked.m_unseen = 0.0f;
        } else if (!OnScreen(tracked.m_light.m_position - origin) && tracked.m_unseen < 20.0f) {
            // off screen its model is not animated, so it is not found: it still lights what is on screen. Kept up to
            // 20 s unseen
            tracked.m_unseen += dt;
        } else {
            tracked.m_weight -= dt / 0.6f;
        }

        tracked.m_weight = tracked.m_weight > 1.0f ? 1.0f : tracked.m_weight;

        if (tracked.m_weight <= 0.0f && !tracked.m_seen) {
            s_tracked[i] = s_tracked.back();
            s_tracked.pop_back();
            continue;
        }

        float w = tracked.m_weight > 0.0f ? tracked.m_weight : 0.0f;
        GxLightingLight light = tracked.m_light;
        float fade = w * w * (3.0f - 2.0f * w);
        light.m_color = light.m_color * fade;
        s_lights.push_back(light);
        i++;
    }
}

// origin: the world draws relative to it (CWorldScene::s_activeWorldView: every object's world matrix subtracts it, so
// the view matrix and the depth buffer are in that space); the lights, the camera and the fog's base height are moved
// into it
static void SetupView(CM2Scene* scene, const C3Vector& camera, const C3Vector& origin, bool wmo, bool testLights) {
    BuildParams(s_params, camera);

    // the world's shadow maps; the login screen's scene has none
    if (wmo && (s_params.m_features & (GxLighting_SunShadow | GxLighting_Shafts))) {
        GatherShadows(s_params.m_shadows);
    }

    s_params.m_cameraPosition = camera - origin;
    s_params.m_origin = origin;
    s_params.m_heightFogBase -= origin.z;
    s_lights.clear();
    s_statsM2 = 0;
    s_statsWmo = 0;
    s_diag = GatherDiag();

    if (s_params.m_features & GxLighting_Dynamic) {
        float reach = s_cvDynamicDistance.m_floatValue;
        GatherM2Lights(scene, camera, reach);
        GatherFireLights(scene, camera, reach);

        if (wmo) {
            GatherWmoLights(camera, reach);
        }

        SmoothLights(origin);

        for (GxLightingLight& light : s_lights) {
            light.m_position = light.m_position - origin;
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
            C3Vector eye = s_params.m_cameraPosition;
            auto nearer = [&eye](const GxLightingLight& a, const GxLightingLight& b) {
                return sqrtf(DistanceSquared(a.m_position, eye)) - a.m_radius < sqrtf(DistanceSquared(b.m_position, eye)) - b.m_radius;
            };

            std::nth_element(s_lights.begin(), s_lights.begin() + keep, s_lights.end(), nearer);
            s_lights.resize(keep);
        }
    }

    s_statsKept = static_cast<uint32_t>(s_lights.size());
    s_statsViews++;

    // now and then, for Logs\gx.log
    if (s_statsViews % 600 == 1 && s_statsViews < 6000) {
        CGxDevice::Log("AwesomeLighting: view %u: %u lights from models, %u from WMOs, %u fires from %u emitters, %u kept", s_statsViews, s_statsM2, s_statsWmo, s_statsFires, s_statsFireEmitters, s_statsKept);
    }
    s_statsOrigin = origin;

    if (s_statsKept) {
        // the nearest, for /lighting
        size_t nearest = 0;

        for (size_t i = 1; i < s_lights.size(); i++) {
            if (DistanceSquared(s_lights[i].m_position, s_params.m_cameraPosition) < DistanceSquared(s_lights[nearest].m_position, s_params.m_cameraPosition)) {
                nearest = i;
            }
        }

        s_statsLight = s_lights[nearest];
    }

    g_gxLighting->SetupView(s_params, s_lights.data(), s_statsKept);
}

void AwesomeLightingSetupView(const C3Vector& camera) {
    if (!s_inWorld || !g_gxLighting) {
        return;
    }

    SetupView(World::s_m2Scene, camera, CWorldScene::s_activeWorldView, true, false);
}

// lightingGlueTest: the login screen's model scene goes through the same passes as the world (CSimpleModelFFX::Render
// around it, CSimpleModel::RenderModel's callback inside), so they can be seen without a realm. 2 adds test lights
static void GlueRenderCallback(CSimpleModel* model, uint32_t phase, const C3Vector& camera) {
    if (!s_inWorld || !g_gxLighting) {
        return;
    }

    if (phase == 0) {
        SetupView(model->GetScene(), camera, C3Vector(0.0f, 0.0f, 0.0f), false, s_cvGlueTest.m_intValue >= 2);
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

// /lighting [on|off]: the preset, everything off, or what the lighting found last frame
static int32_t Script_LightingCommand(lua_State* L) {
    const char* option = AwesomeLuaIsNoneOrNil(L, 1) ? "" : lua_tolstring(L, 1, nullptr);
    char word[16] = {};

    if (option) {
        while (*option == ' ') {
            option++;
        }

        for (size_t i = 0; i + 1 < sizeof(word) && option[i] && option[i] != ' '; i++) {
            word[i] = static_cast<char>(tolower(static_cast<unsigned char>(option[i])));
        }
    }

    struct Preset {
        AwesomeCVar* m_cvar;
        const char* m_on;
    };

    static const Preset s_preset[] = {
        { &s_cvHdr, "1" }, { &s_cvBloom, "0.3" }, { &s_cvSsao, "0.8" }, { &s_cvPerPixel, "1" }, { &s_cvDynamic, "1" },
        { &s_cvSpecular, "1" }, { &s_cvSkyAmbient, "0.5" }, { &s_cvHeightFog, "0.003" }, { &s_cvScattering, "0.4" },
        { &s_cvFire, "1" }, { &s_cvSharpen, "0.5" }, { &s_cvSunShadow, "1" }, { &s_cvCanopy, "0.6" }, { &s_cvShafts, "1" },
    };

    if (!strcmp(word, "on") || !strcmp(word, "off")) {
        bool on = word[1] == 'n';

        for (const Preset& preset : s_preset) {
            preset.m_cvar->Set(on ? preset.m_on : "0");
        }

        Print(L, on ? "|cff33ff99Lighting|r on (WMO specular also needs the stock specular 1 and a restart; sun shadows, canopy and shafts need Shadow Quality 5)" : "|cff33ff99Lighting|r off");
        return 0;
    }

    if (!g_gxLighting) {
        Print(L, "|cff33ff99Lighting|r needs the D3D11 device: SET gxApi \"D3D11\" or -d3d11, and a restart");
        return 0;
    }

    GxLightingStats stats = {};
    g_gxLighting->GetStats(stats);
    char line[512];
    snprintf(line, sizeof(line), "|cff33ff99Lighting|r features 0x%03X, %u views; lights: %u from models, %u from WMOs, %u fires (%u emitters), %u kept, %u drawn", Features(), s_statsViews, s_statsM2, s_statsWmo, s_statsFires, s_statsFireEmitters, s_statsKept, stats.m_lights);
    Print(L, line);
    snprintf(line, sizeof(line), "  viewport %.0f,%.0f %.0fx%.0f, depth %.4f-%.4f", stats.m_viewport[0], stats.m_viewport[1], stats.m_viewport[2], stats.m_viewport[3], stats.m_viewport[4], stats.m_viewport[5]);
    Print(L, line);
    snprintf(line, sizeof(line), "  origin (%.1f %.1f %.1f), camera (%.1f %.1f %.1f) relative", s_statsOrigin.x, s_statsOrigin.y, s_statsOrigin.z, s_params.m_cameraPosition.x, s_params.m_cameraPosition.y, s_params.m_cameraPosition.z);
    Print(L, line);

    snprintf(line, sizeof(line), "  M2: grid %u, %u seen, %u invisible, %u not point, %u stale; WMO: %u placed, %u with lights, %u in reach, %u MOLT, %u skipped; %u rejected", s_diag.m_m2Grid, s_diag.m_m2Seen, s_diag.m_m2Invisible, s_diag.m_m2NotPoint, s_diag.m_m2Stale, s_diag.m_wmoDefs, s_diag.m_wmoWithLights, s_diag.m_wmoInReach, s_diag.m_moltSeen, s_diag.m_moltSkipped, s_diag.m_rejected);
    Print(L, line);

    snprintf(line, sizeof(line), "  rejected: %u no radius, %u too far, %u too dark; nearest candidate %.1f yd at (%.1f %.1f %.1f), radius %.1f, colour %.2f %.2f %.2f x %.2f", s_diag.m_rejectRadius, s_diag.m_rejectFar, s_diag.m_rejectDark, s_diag.m_nearestRejected, s_diag.m_nearestPos.x, s_diag.m_nearestPos.y, s_diag.m_nearestPos.z, s_diag.m_nearestRadius, s_diag.m_nearestColor.x, s_diag.m_nearestColor.y, s_diag.m_nearestColor.z, s_diag.m_nearestScale);
    Print(L, line);

    if (s_statsKept) {
        snprintf(line, sizeof(line), "  nearest light (%.1f %.1f %.1f) relative, radius %.1f, colour %.2f %.2f %.2f", s_statsLight.m_position.x, s_statsLight.m_position.y, s_statsLight.m_position.z, s_statsLight.m_radius, s_statsLight.m_color.x, s_statsLight.m_color.y, s_statsLight.m_color.z);
        Print(L, line);
    }

    GxLightingShadows shadows;
    GatherShadows(shadows);
    snprintf(line, sizeof(line), "  sun shadows: shadow quality %u (5 needed), maps %s, near map %s, %.0f yd per depth unit; zone shade %.2f", static_cast<uint32_t>(CShadowCache::s_shadowMode), shadows.m_maps[1] ? "found" : "missing", shadows.m_maps[0] ? "found" : "missing", shadows.m_depthYards, s_params.m_shadowedFraction);
    Print(L, line);
    Print(L, "  /lighting on | off; lightingDebug 1 SSAO, 2 lit surfaces (green per pixel, red unlit, blue face normal), 3 dynamic light alone, 4 SSAO inputs (red geometry, green distance / 200, blue occlusion), 6 sun shadow, 7 canopy, 8 shafts, 9 shadow cascade (red 80 yd, green 320, blue 1280)");
    return 0;
}

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
}

void AwesomeLightingOnEnterWorld() {
    // as /liveportraits: SlashCmdList exists once FrameXML has loaded
    AwesomeLuaRegisterSlashCommand(FrameScript_GetContext(), "MODERNLIGHTING", "/lighting", &Script_LightingCommand);
}
