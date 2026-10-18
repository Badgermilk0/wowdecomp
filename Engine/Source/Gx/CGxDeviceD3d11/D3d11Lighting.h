#ifndef ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11LIGHTING_H
#define ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11LIGHTING_H

// MODIFIED: modern lighting on the D3D11 device (GxLighting.h is the interface the game drives). Two halves:
//
//  - in the translated shaders (D3d11Dx9Shader.cpp, s_hlslLighting): the M2, WMO and terrain vertex shaders hand their
//    view-space surface to the pixel shader, which lights it again per pixel with the stock formula and adds the
//    clustered point lights, the hemisphere ambient and specular. This file fills what they read: the GxLighting
//    constants (b3, vertex and pixel), the lights (t16) and the clusters (t17), built by a compute shader in SetupView
//  - full-screen passes: SSAO and height fog / scattering at the end of the opaque pass, and the HDR scene's bloom and
//    tonemap into the frame at the end of the world
//
// The HDR scene is a 16-bit float target that stands in for the frame while the world draws (CGxDeviceD3d11::
// IFrameRtv); its colours are what the 8-bit frame would hold, gamma encoded, only not clamped at 1. The new light
// terms are added in linear space, and the tonemap works in linear space.

#include <cstdint>
#include <vector>
#include <windows.h>
#include <d3d11_1.h>
#include "Engine/Source/Gx/GxLighting.h"

class CGxDeviceD3d11;

class D3d11Lighting : public IGxLighting {
    public:
    explicit D3d11Lighting(CGxDeviceD3d11* device);
    virtual ~D3d11Lighting();

    // IGxLighting
    virtual void BeginWorld(const GxLightingParams& params);
    virtual void SetupView(const GxLightingParams& params, const GxLightingLight* lights, uint32_t count);
    virtual void OpaqueDone();
    virtual void EndWorld();
    virtual void GetStats(GxLightingStats& stats);

    // the device's side
    bool HdrActive() const {
        return this->m_hdrActive;
    }

    ID3D11RenderTargetView* SceneRtv() const {
        return this->m_sceneRtv;
    }

    ID3D11Texture2D* SceneTexture() const {
        return this->m_scene;
    }

    // the scene, single-sampled (resolved when multisampled), for a copy of the frame while the world draws
    ID3D11ShaderResourceView* SceneSrv();
    ID3D11Texture2D* SceneResolved();

    // the single-sampled scene as last resolved
    ID3D11ShaderResourceView* SceneSrvNoResolve() const {
        return this->m_sceneSrv;
    }
    // the constants, lights and clusters every translated shader may read
    void BindForDraw();
    // the frame's size changed, or the device goes
    void ReleaseTargets();
    void ReleaseAll();

    private:
    struct Target {
        ID3D11Texture2D* m_texture = nullptr;
        ID3D11RenderTargetView* m_rtv = nullptr;
        ID3D11ShaderResourceView* m_srv = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
    };

    enum EShader {
        Shader_FullscreenVs = 0,
        Shader_AoPs,
        Shader_AoPsMs,
        Shader_BlurPs,
        Shader_AoCompositePs,
        Shader_AoCompositePsMs,
        Shader_FogPs,
        Shader_FogPsMs,
        Shader_BloomPrefilterPs,
        Shader_BloomDownPs,
        Shader_BloomUpPs,
        Shader_TonemapPs,
        Shader_ClusterCs,
        Shaders_Last,
    };

    static const uint32_t s_bloomLevels = 6;
    static const uint32_t s_maxLights = 256;
    static const uint32_t s_clustersX = 16;
    static const uint32_t s_clustersY = 9;
    static const uint32_t s_clustersZ = 24;
    static const uint32_t s_clusterStride = 32;

    CGxDeviceD3d11* m_device;
    GxLightingParams m_params;
    bool m_worldActive = false;
    bool m_hdrActive = false;
    bool m_lightsActive = false;
    bool m_failed = false;
    uint32_t m_lightCount = 0;
    uint32_t m_statsLights = 0;

    // the HDR scene
    ID3D11Texture2D* m_scene = nullptr;
    ID3D11RenderTargetView* m_sceneRtv = nullptr;
    ID3D11Texture2D* m_sceneResolved = nullptr;
    ID3D11ShaderResourceView* m_sceneSrv = nullptr;
    bool m_sceneResolvedValid = false;
    uint32_t m_sceneWidth = 0;
    uint32_t m_sceneHeight = 0;
    uint32_t m_sceneSamples = 0;

    // half-resolution AO (x occlusion, y depth), ping-pong for the blur; the bloom chain
    Target m_ao[2];
    Target m_bloom[s_bloomLevels];

    // the lights and the clusters
    ID3D11Buffer* m_lightBuffer = nullptr;
    ID3D11ShaderResourceView* m_lightSrv = nullptr;
    ID3D11Buffer* m_clusterBuffer = nullptr;
    ID3D11ShaderResourceView* m_clusterSrv = nullptr;
    ID3D11UnorderedAccessView* m_clusterUav = nullptr;
    ID3D11Buffer* m_constants = nullptr;        // GxLighting, b3
    ID3D11Buffer* m_postConstants = nullptr;    // the passes' own, b0

    // the shaders, the blend states the passes use
    ID3D11DeviceChild* m_shaders[Shaders_Last] = {};
    ID3D11BlendState* m_blendMultiply = nullptr;
    ID3D11BlendState* m_blendAlpha = nullptr;
    ID3D11BlendState* m_blendAdd = nullptr;
    ID3D11BlendState* m_blendOpaque = nullptr;

    // the world viewport and the matrices of the view, from SetupView
    D3D11_VIEWPORT m_viewport = {};
    float m_view[16] = {};
    float m_viewInv[16] = {};
    float m_proj[16] = {};
    float m_projInv[16] = {};

    bool ICreateShaders();
    bool ICreateBuffers();
    bool IEnsureScene();
    bool IEnsureTarget(Target& target, uint32_t width, uint32_t height, DXGI_FORMAT format);
    void IReleaseTarget(Target& target);
    void IWriteConstants(bool on);
    void IWritePostConstants(const D3D11_VIEWPORT& viewport, uint32_t targetWidth, uint32_t targetHeight, const float* source);
    void IDrawFullscreen(EShader shader, ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport, ID3D11BlendState* blend, ID3D11ShaderResourceView* const* srvs, uint32_t srvCount);
    void ICaptureView();
    void IRestoreDevice();
};

#endif
