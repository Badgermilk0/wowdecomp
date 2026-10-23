#ifndef ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11WATER_H
#define ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11WATER_H

// MODIFIED: modern water on the D3D11 device (GxWater.h is the interface the game drives). Three parts:
//
//  - the ripple height field: two R32G32_FLOAT textures (height now, height a step ago) of s_fieldSize texels, s_texel
//    yards each, centred on the player and scrolled by whole texels as the player moves. Each step is a full-screen
//    pass of the wave equation (a 5-point Laplacian, damped, absorbing at the border) that also adds the frame's
//    disturbances (Gaussian dips at units in water) and raindrops. Steps are fixed at 60 a second
//  - before the liquids: the opaque scene's colour is copied (resolved when multisampled) and its depth turned into
//    clip w (distance along the view axis; 0 is the sky) in an R32F texture, for refraction, thickness and the
//    screen-space reflections
//  - the GxWater constants (b5) and the copies (t18 colour, t19 depth, t20 height field, s15 a linear clamp sampler)
//    that a translated water pixel shader reads (s_hlslWater in D3d11Dx9Shader.cpp). Off, the constants are zero and
//    the shader keeps the stock colour

#include <cstdint>
#include <windows.h>
#include <d3d11_1.h>
#include "Engine/Source/Gx/GxWater.h"

class CGxDeviceD3d11;

namespace GxD3d11 {
// the passes (D3d11WaterHlsl.cpp): FullscreenVs, SimPs, DepthPs (GX_MSAA 0 or 1)
extern const char* const s_hlslWaterPasses;
// the tessellation stages WaterHs / WaterDs, after s_hlslCommon, s_hlslPatchPoint and s_hlslWaterConstants
extern const char* const s_hlslWaterTessellation;
// the splash particles EmitCs / UpdateCs / ParticleVs / ParticlePs, after s_hlslWaterConstants (D3d11WaterHlsl.cpp)
extern const char* const s_hlslWaterParticles;
}

class D3d11Water : public IGxWater {
    public:
    static const uint32_t s_fieldSize = 512;
    static constexpr float s_texel = 0.25f;
    static const uint32_t s_maxImpulses = 64;
    static const uint32_t s_maxParticles = 8192;
    static const uint32_t s_maxSplashes = 32;

    explicit D3d11Water(CGxDeviceD3d11* device);
    virtual ~D3d11Water();

    // IGxWater
    virtual void Update(const GxWaterParams& params, const GxWaterImpulse* impulses, uint32_t count, const GxWaterSplash* splashes, uint32_t splashCount);
    virtual void BeforeLiquids(const C3Vector& origin);
    virtual void AfterLiquids();
    virtual void EndWorld();
    virtual void GetStats(GxWaterStats& stats);

    // the device's side: before a draw with a water pixel shader
    void BindForDraw();
    // a draw from a triangle list with the water shaders may be tessellated now (its vertex shader then takes the
    // Variant_Tessellated variant); BindTessellation binds the hull and domain shaders for it
    bool TessellationReady() const;
    void BindTessellation();
    // the frame's size changed, or the device goes
    void ReleaseTargets();
    void ReleaseAll();

    private:
    enum EShader {
        Shader_FullscreenVs = 0,
        Shader_SimPs,
        Shader_DepthPs,
        Shader_DepthPsMs,
        Shaders_Last,
    };

    CGxDeviceD3d11* m_device;
    GxWaterParams m_params;
    bool m_failed = false;

    // the height field; m_current holds the latest step
    ID3D11Texture2D* m_field[2] = {};
    ID3D11RenderTargetView* m_fieldRtv[2] = {};
    ID3D11ShaderResourceView* m_fieldSrv[2] = {};
    uint32_t m_current = 0;
    bool m_fieldValid = false;
    int32_t m_originX = 0;       // the absolute texel index of the field's corner
    int32_t m_originY = 0;
    float m_stepTime = 0.0f;     // seconds not yet stepped
    uint32_t m_stepSeed = 0;

    // this frame's disturbances, kept until a step takes them (a frame may have none at a high frame rate)
    GxWaterImpulse m_impulses[s_maxImpulses] = {};
    uint32_t m_impulseCount = 0;

    // the copies of the opaque scene
    ID3D11Texture2D* m_scene = nullptr;
    ID3D11ShaderResourceView* m_sceneSrv = nullptr;
    DXGI_FORMAT m_sceneFormat = DXGI_FORMAT_UNKNOWN;
    ID3D11Texture2D* m_depth = nullptr;
    ID3D11RenderTargetView* m_depthRtv = nullptr;
    ID3D11ShaderResourceView* m_depthSrv = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    ID3D11Buffer* m_constants = nullptr;      // GxWater (b5), this frame's
    ID3D11Buffer* m_offConstants = nullptr;   // all zero: the stock colour
    ID3D11Buffer* m_simConstants = nullptr;   // the passes' own (b0)
    ID3D11SamplerState* m_sampler = nullptr;
    ID3D11BlendState* m_blendOpaque = nullptr;
    ID3D11DeviceChild* m_shaders[Shaders_Last] = {};
    ID3D11HullShader* m_hullShader = nullptr;
    ID3D11DomainShader* m_domainShader = nullptr;
    bool m_tessFailed = false;

    // the splash particles (D3d11WaterParticles.cpp): a ring buffer, and the impacts of droplets falling back, in fixed
    // point on the height field's texels, which the next step takes and clears
    ID3D11Buffer* m_particles = nullptr;
    ID3D11UnorderedAccessView* m_particleUav = nullptr;
    ID3D11ShaderResourceView* m_particleSrv = nullptr;
    ID3D11Texture2D* m_impact = nullptr;
    ID3D11UnorderedAccessView* m_impactUav = nullptr;
    ID3D11ShaderResourceView* m_impactSrv = nullptr;
    ID3D11Buffer* m_particleConstants = nullptr;
    ID3D11ComputeShader* m_emitCs = nullptr;
    ID3D11ComputeShader* m_updateCs = nullptr;
    ID3D11VertexShader* m_particleVs = nullptr;
    ID3D11PixelShader* m_particlePs = nullptr;
    ID3D11BlendState* m_blendParticles = nullptr;
    bool m_particlesFailed = false;
    bool m_particlesValid = false;
    double m_particleOrigin[3] = {};
    uint32_t m_nextSlot = 0;
    uint32_t m_particleSeed = 0;
    float m_particleQuiet = 1000.0f;   // seconds since the last splash: past the longest life, nothing is left to move
    GxWaterSplash m_splashes[s_maxSplashes] = {};
    uint32_t m_splashCount = 0;
    uint32_t m_statsParticles = 0;

    // this frame
    bool m_worldActive = false;
    bool m_captured = false;
    ID3D11RenderTargetView* m_capturedRtv = nullptr;
    uint32_t m_statsSteps = 0;
    uint32_t m_statsImpulses = 0;
    uint32_t m_statsDraws = 0;
    uint32_t m_drawsThisFrame = 0;
    bool m_lastCaptured = false;

    bool ICreateShaders();
    bool ICreateTessellation();
    bool ICreateParticles();
    void IReleaseParticles();
    // starts this frame's splashes and moves every particle a step; droplets falling back write impacts
    void IRunParticles(float dt);
    void IWriteParticleConstants(float dt, uint32_t emit, const float* emitters, uint32_t emitterCount);
    bool ICreateResources();
    bool IEnsureField();
    bool IEnsureCopies(DXGI_FORMAT format);
    void IReleaseField();
    void IStep(int32_t shiftX, int32_t shiftY, bool impulses);
    void IWriteConstants();
    void IDrawFullscreen(EShader shader, ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport, ID3D11ShaderResourceView* srv);
    void IRestoreDevice();
};

#endif
