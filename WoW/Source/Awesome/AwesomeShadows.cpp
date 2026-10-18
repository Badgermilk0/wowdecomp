#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"

// Shadow quality options on top of extShadowQuality (CShadowCache, Engine/Source/ShaderEffect/ShadowCache.cpp):
// shadowMapSize: texels per shadow map side, 0 = stock (1024, or 2048 from extShadowQuality 2 and 4 up). Every map
//   (the near 40-yard one and the three 80/320/1280-yard cascades) uses it; 4096 halves the size of a shadow texel.
//   Needs PCF filtering (the default on SM3 cards). Memory: about 9 maps of size x size x 4 bytes (4096: ~600 MB).
// shadowSoftness: scales the PCF filter kernel; 1 keeps the stock two-texel kernel, so a bigger map also gives
//   sharper edges, and a larger value softens them again.
// shadowStable: snaps every shadow map to whole texels in light space, so edges no longer crawl as you move.
// shadowCascadeReuse: with extShadowQuality 5, re-renders the 320-yard cascade every 2nd frame and the 1280-yard one
//   every 4th instead of all three every frame (sooner when the view turns or moves); 0 = stock (ShadowCache.h).

static void OnMapSize(AwesomeCVar* cvar, bool changed);
static void OnSoftness(AwesomeCVar* cvar, bool changed);
static void OnStable(AwesomeCVar* cvar, bool changed);
static void OnCascadeReuse(AwesomeCVar* cvar, bool changed);

static AwesomeCVar s_cvMapSize = AwesomeCVar::Int("shadowMapSize", 4096, 0, 8192, OnMapSize);
static AwesomeCVar s_cvSoftness = AwesomeCVar::Float("shadowSoftness", 1.0f, 0.25f, 4.0f, OnSoftness);
static AwesomeCVar s_cvStable = AwesomeCVar::Int("shadowStable", 1, 0, 1, OnStable);
static AwesomeCVar s_cvCascadeReuse = AwesomeCVar::Int("shadowCascadeReuse", 1, 0, 1, OnCascadeReuse);

// 0, or the power of two from 1024 to 8192 nearest the value (rounding up)
static uint32_t MapSize(int32_t value) {
    if (value <= 0) {
        return 0;
    }

    uint32_t size = 1024;

    while (size < static_cast<uint32_t>(value) && size < 8192) {
        size *= 2;
    }

    return size;
}

static void OnMapSize(AwesomeCVar* cvar, bool changed) {
    uint32_t size = MapSize(cvar->m_intValue);

    if (size == CShadowCache::s_sizeOverride) {
        return;
    }

    CShadowCache::s_sizeOverride = size;
    // CShadowCache::Update rebuilds the maps at their new size on the next frame
    CShadowCache::s_recreateTextures = 1;
}

static void OnSoftness(AwesomeCVar* cvar, bool changed) {
    CShadowCache::s_pcfScale = cvar->m_floatValue;

    if (CShadowCache::s_shadowSize) {
        CShadowCache::InitPcfOffsets();
    }
}

static void OnStable(AwesomeCVar* cvar, bool changed) {
    CShadowCache::s_stableBands = cvar->m_intValue;
}

static void OnCascadeReuse(AwesomeCVar* cvar, bool changed) {
    CShadowCache::s_cascadeReuse = cvar->m_intValue;
}

void AwesomeShadowsRegisterCVars() {
    s_cvMapSize.Register();
    s_cvSoftness.Register();
    s_cvStable.Register();
    s_cvCascadeReuse.Register();
}
