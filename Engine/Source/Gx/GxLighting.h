#ifndef ENGINE_SOURCE_GX_GXLIGHTING_H
#define ENGINE_SOURCE_GX_GXLIGHTING_H

// MODIFIED: modern lighting. Not in the 3.3.5a binary. The game (WoW/Source/Awesome/AwesomeLighting.cpp) describes each
// world frame: which effects are on, the zone's sky and fog colours, the point lights in view. The D3D11 device draws
// it (CGxDeviceD3d11/D3d11Lighting.*) and sets g_gxLighting; every other device leaves it null and nothing changes.
//
// The calls bracket the world pass in CGWorldFrame::OnWorldRender:
//   BeginWorld   before CWorld::Render: the world draws into the HDR scene target (hdr on)
//   SetupView    in CWorldScene::Render once the camera, the models' lights and the shadows are ready, before the
//                terrain: the lights, the clusters and the lighting constants the translated shaders read
//   OpaqueDone   after the opaque models and the grass, before liquids and everything transparent: SSAO, sun shafts and
//                fog
//   EndWorld     before FFX::Finish: bloom and the tonemap into the frame; the lighting constants switch off

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Tempest/C3Vector.h"

enum EGxLightingFeature : uint32_t {
    GxLighting_PerPixel = 0x1,        // M2, WMO and terrain lit per pixel with their stock formulas
    GxLighting_Dynamic = 0x2,         // the point lights below
    GxLighting_Specular = 0x4,        // WMO specular shaders per pixel, a highlight on sphere-mapped M2s
    GxLighting_Hemisphere = 0x8,      // the ambient tinted by the sky above and the ground below
    GxLighting_Ssao = 0x10,
    GxLighting_HeightFog = 0x20,
    GxLighting_Scattering = 0x40,     // the fog tinted toward the sun's colour around the sun
    GxLighting_Hdr = 0x80,            // the world into a 16-bit float target, tonemapped
    GxLighting_Bloom = 0x100,         // needs Hdr
    GxLighting_SunShadow = 0x200,     // the sun's shadow on the direct sunlight only, per pixel, soft (needs the cascades)
    GxLighting_Canopy = 0x400,        // the ambient dimmed under foliage and roofs (needs SunShadow)
    GxLighting_Shafts = 0x800,        // sunlight scattered in the air, shadowed by the cascades: shafts through the leaves
};

class CGxTex;

// the stock sun shadow maps of this frame (CShadowCache, extShadowQuality 5), for GxLighting_SunShadow / _Shafts
struct GxLightingShadows {
    CGxTex* m_maps[4] = {};          // the near map (units, 40 yards across), then the cascades (80, 320, 1280 across)
    float m_radius[4] = {};          // half of each map's width in yards
    float m_rows[12][4] = {};        // view space to each map: x and y (-1 to 1 across it) and depth, 3 rows a map
    float m_depthYards = 4000.0f;    // yards per unit of depth
    CGxTex* m_interior = nullptr;    // the interior map: a draw that samples it is indoors and has no sun
};

// a point light, in world space
struct GxLightingLight {
    C3Vector m_position;
    float m_radius;       // no light from here on
    float m_start;        // full light up to here
    C3Vector m_color;     // linear, intensity included
    float m_flicker;      // 0 to 1
    float m_specular;     // 0 to 1
    uint32_t m_kind = 0;  // the game's own tag (1: a fire made from particles); the device ignores it
};

struct GxLightingParams {
    uint32_t m_features = 0;

    // HDR and tonemap
    float m_exposure = 1.0f;
    uint32_t m_tonemap = 1;          // 0 clamp (stock), 1 neutral (stock below a shoulder), 2 ACES
    float m_bloomIntensity = 0.0f;
    float m_bloomThreshold = 1.0f;   // linear
    float m_sharpen = 0.0f;          // 0 off, to 1: contrast-adaptive sharpening of the world in the tonemap (needs Hdr)
    float m_dither = 0.0f;           // 8-bit steps of triangular noise the tonemap adds, against banding (needs Hdr)

    // SSAO
    float m_ssaoIntensity = 0.0f;
    float m_ssaoRadius = 1.5f;       // yards
    float m_ssaoDistance = 200.0f;   // fades out to here

    // lights
    float m_lightWrap = 0.2f;
    float m_dynamicIntensity = 1.0f;
    float m_specularM2 = 0.1f;
    float m_specularWmo = 1.0f;
    float m_lightDistance = 120.0f;  // the clusters reach this far

    // sky and fog, as the engine has them this frame (DayNight)
    C3Vector m_skyColor;             // 0 to 1, gamma as the engine stores them
    C3Vector m_groundColor;
    float m_hemisphere = 0.0f;
    C3Vector m_fogColor;
    float m_fogStart = 0.0f;
    float m_fogEnd = 0.0f;
    float m_fogDensity = 1.0f;
    C3Vector m_sunColor;
    C3Vector m_sunDirection;         // world, the way the light travels
    float m_scattering = 0.0f;
    float m_heightFogDensity = 0.0f; // per yard at the base height
    float m_heightFogFalloff = 0.05f;
    float m_heightFogBase = 0.0f;    // world z
    float m_heightFogMax = 0.9f;
    C3Vector m_cameraPosition;       // world
    C3Vector m_origin;               // what the world draws relative to (it moves with the camera): the camera, the
                                     // lights and the view matrix are relative to it
    float m_time = 0.0f;             // seconds
    uint32_t m_debug = 0;            // 1 the SSAO term alone, 2 the lit surfaces by kind, 3 the dynamic light alone

    // the sun and its shadow (GxLighting_SunShadow, _Canopy, _Shafts)
    GxLightingShadows m_shadows;
    float m_sunShadowStrength = 1.0f; // 0 none, 1 the shadow takes all of the direct sunlight
    uint32_t m_sunFirstCascade = 2; // the first cascade the sun shadows read: 1 the 80-yard one, 2 the 320-yard one
    float m_sunBias = 0.0f;          // yards of depth bias on top of the stock one (80-yard cascade; wider ones more)
    float m_sunPenumbra = 1.5f;      // the sun's apparent size, times the real sun's: the softness of distant occluders
    float m_sunIntensity = 1.0f;     // the direct sunlight on lit surfaces
    float m_ambientIntensity = 1.0f; // their ambient light
    float m_canopy = 0.0f;           // 0 to 1: how much the ambient dims under cover
    float m_shadowedFraction = 0.4f; // a surface in full shade over one in full sun, from the zone's colours: what the
                                     // stock shadow of grass and unlit surfaces is deepened to
    float m_shafts = 0.0f;           // the intensity of the scattered sunlight
    float m_shaftsDensity = 0.012f;   // its density per yard
    float m_shaftsDistance = 120.0f; // how far along each view ray it is gathered
    float m_interior = 0.0f;         // 0 outdoors, 1 inside a WMO (no sun)
};

struct GxLightingStats {
    uint32_t m_lights;
    uint32_t m_clusterOverflows;     // clusters that had more lights than they hold (last read back)
    float m_gpuMs;                   // not measured yet: 0
    float m_viewport[6];             // the world viewport: left, top, width, height, depth min, max
};

class IGxLighting {
    public:
    virtual void BeginWorld(const GxLightingParams& params) = 0;
    virtual void SetupView(const GxLightingParams& params, const GxLightingLight* lights, uint32_t count) = 0;
    virtual void OpaqueDone() = 0;
    virtual void EndWorld() = 0;
    virtual void GetStats(GxLightingStats& stats) = 0;
};

extern IGxLighting* g_gxLighting;

#endif
