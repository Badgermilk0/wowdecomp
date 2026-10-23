#ifndef ENGINE_SOURCE_GX_GXWATER_H
#define ENGINE_SOURCE_GX_GXWATER_H

// MODIFIED: modern water. Not in the 3.3.5a binary. The game (WoW/Source/Awesome/AwesomeWater.cpp) describes each world
// frame: which effects are on, the zone's sky and sun, and where units disturbed the water. The D3D11 device draws it
// (CGxDeviceD3d11/D3d11Water.*) and sets g_gxWater; every other device leaves it null and the stock water stays.
//
// The calls, in CGWorldFrame::OnWorldRender:
//   Update         after BeginWorld (GxLighting.h), before CWorld::Render: the ripple height field steps (a wave
//                  equation on the GPU around the player), with this frame's disturbances
//   BeforeLiquids  in CWorldScene::RenderLiquidPass, before the transparent liquids: the opaque scene's colour and
//                  depth are copied for refraction, thickness and reflections; the water constants are written
//   AfterLiquids   after the transparent liquids: the splash droplets and mist
//   EndWorld       with the lighting's EndWorld: the water constants switch off
//
// The water pixel shaders (psLiquidWater*, translated in D3d11Dx9Shader.cpp, s_hlslWater) still compute the stock
// colour; while the constants are on, they shade the surface from it instead.

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Tempest/C3Vector.h"

enum EGxWaterFeature : uint32_t {
    GxWater_Shading = 0x1,       // refraction, absorption by thickness, Fresnel, the sun's highlight
    GxWater_Reflection = 0x2,    // screen-space reflections (without it: the sky alone)
    GxWater_Ripples = 0x4,       // the interactive height field
    GxWater_Foam = 0x8,          // at the shore and on disturbed water
    GxWater_Tessellation = 0x10, // the water's triangles cut finer near the eye and moved by the height field
    GxWater_Splashes = 0x20,     // droplets and mist thrown up, falling back as rings
};

// a disturbance of the water this frame, in world space (absolute)
struct GxWaterImpulse {
    float m_x;
    float m_y;
    float m_radius;      // yards
    float m_strength;    // yards the surface is pushed down (negative lifts it)
};

// droplets (and mist) thrown up this frame, in world space (absolute)
struct GxWaterSplash {
    float m_x;
    float m_y;
    float m_z;           // the surface
    uint32_t m_count;
    float m_up;          // yards per second, the fastest
    float m_out;         // yards per second outward, the fastest
    float m_radius;      // yards
    uint32_t m_kind;     // 0 droplets, 1 mist
};

struct GxWaterParams {
    uint32_t m_features = 0;

    float m_refraction = 1.0f;       // 0 to 2
    float m_clarity = 1.0f;          // absorption per yard of water, relative
    float m_reflection = 1.0f;       // 0 to 2
    float m_detail = 1.0f;           // normals from the stock water texture
    float m_foam = 1.0f;
    float m_rippleNormal = 1.0f;     // how far the height field bends the surface
    float m_waveHeight = 0.5f;       // how far it moves the surface (yards per yard of height)
    uint32_t m_reflectionSteps = 24; // screen-space ray steps

    // the height field
    float m_rippleSpeed = 3.0f;      // yards per second
    float m_rippleDamping = 0.9f;    // amplitude lost per second (e-folding rate)
    float m_rain = 0.0f;             // raindrops per square yard and second
    float m_rainStrength = 0.012f;   // yards a drop pushes the surface down
    float m_centerX = 0.0f;          // the field follows this point (absolute world)
    float m_centerY = 0.0f;

    // the world draws relative to this point (CWorldScene::s_activeWorldView)
    C3Vector m_origin;

    // the zone this moment (DayNight), 0 to 1 as the engine stores them
    C3Vector m_skyColors[6];         // top, middle, band 1, band 2, smog, horizon (fog)
    C3Vector m_sunColor;
    C3Vector m_sunDirection;         // world, the way the light travels
    C3Vector m_ambient;
    float m_skyVisible = 1.0f;       // 0 indoors: reflections fall back to the ambient, not the sky
    float m_time = 0.0f;             // seconds
    float m_deltaTime = 0.0f;        // seconds since the last frame
    uint32_t m_debug = 0;            // 1 normals, 2 thickness, 3 ripple height, 4 reflection hits
};

struct GxWaterStats {
    bool m_simActive;
    uint32_t m_simSteps;             // steps last frame
    uint32_t m_impulses;             // disturbances last frame
    bool m_captured;                 // the scene was copied for the liquids last frame
    uint32_t m_waterDraws;           // water draws that shaded with it last frame
    uint32_t m_particlesStarted;     // splash particles started last frame
    uint32_t m_fieldSize;            // texels
    float m_texel;                   // yards
};

class IGxWater {
    public:
    virtual void Update(const GxWaterParams& params, const GxWaterImpulse* impulses, uint32_t count, const GxWaterSplash* splashes, uint32_t splashCount) = 0;
    // origin: the point the world draws relative to this frame (CWorldScene::s_activeWorldView)
    virtual void BeforeLiquids(const C3Vector& origin) = 0;
    virtual void AfterLiquids() = 0;
    virtual void EndWorld() = 0;
    virtual void GetStats(GxWaterStats& stats) = 0;
};

extern IGxWater* g_gxWater;

#endif
