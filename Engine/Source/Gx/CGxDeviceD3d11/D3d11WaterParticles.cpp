#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Water.h"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Compiler.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Util.h"

// MODIFIED: modern water, the splash particles (D3d11Water.h, s_hlslWaterParticles in D3d11WaterHlsl.cpp). Droplets
// and mist thrown up where units splash into the water or swim fast; globs and embers of magma, gobs and strands of
// slime; a drop that falls back through the surface becomes an impact on its liquid's height field, so a splash rains
// back as a scatter of small rings

using namespace GxD3d11;

namespace {

// WxParticle
const uint32_t s_particleStride = 64;

// the GxWaterParticles cbuffer, b6
struct ParticleConstants {
    float m_offset[4];          // xyz particle space to render space, w the step (seconds)
    float m_field[4];           // xy the field's corner (particle space), z 1 / texel, w its size (texels)
    float m_sim[4];             // x gravity, y droplet drag, z impact units a yard, w yards a droplet pushes
    uint32_t m_emit[4];         // x first slot, y capacity, z seed, w particles to start
    uint32_t m_count[4];        // x emitters, y 1 = the water's field is there, z 1 = the thick liquids'
    float m_emitters[D3d11Water::s_maxSplashes * 3][4];
};

// the longest a particle lives (EmitCs: drops 3 s, mist 1.5 s, embers 2.2 s)
const float s_longestLife = 3.0f;

}

bool D3d11Water::ICreateParticles() {
    if (this->m_emitCs) {
        return true;
    }

    if (this->m_particlesFailed) {
        return false;
    }

    ID3D11Device* device = this->m_device->m_d3dDevice;
    std::string source = std::string(s_hlslWaterConstants) + s_hlslWaterParticles;
    std::vector<uint8_t> emit;
    std::vector<uint8_t> update;
    std::vector<uint8_t> vs;
    std::vector<uint8_t> ps;
    std::string error;

    bool ok = CompileHlsl(source, "EmitCs", "cs_5_0", emit, error) && CompileHlsl(source, "UpdateCs", "cs_5_0", update, error) && CompileHlsl(source, "ParticleVs", "vs_5_0", vs, error) && CompileHlsl(source, "ParticlePs", "ps_5_0", ps, error);

    ok = ok && SUCCEEDED(device->CreateComputeShader(emit.data(), emit.size(), nullptr, &this->m_emitCs));
    ok = ok && SUCCEEDED(device->CreateComputeShader(update.data(), update.size(), nullptr, &this->m_updateCs));
    ok = ok && SUCCEEDED(device->CreateVertexShader(vs.data(), vs.size(), nullptr, &this->m_particleVs));
    ok = ok && SUCCEEDED(device->CreatePixelShader(ps.data(), ps.size(), nullptr, &this->m_particlePs));

    if (ok) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = s_particleStride * s_maxParticles;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = s_particleStride;
        ok = SUCCEEDED(device->CreateBuffer(&desc, nullptr, &this->m_particles)) && SUCCEEDED(device->CreateUnorderedAccessView(this->m_particles, nullptr, &this->m_particleUav)) && SUCCEEDED(device->CreateShaderResourceView(this->m_particles, nullptr, &this->m_particleSrv));
    }

    if (ok) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = s_fieldSize;
        desc.Height = s_fieldSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

        // one for each height field
        for (uint32_t f = 0; ok && f < s_fields; f++) {
            ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &this->m_impact[f])) && SUCCEEDED(device->CreateUnorderedAccessView(this->m_impact[f], nullptr, &this->m_impactUav[f])) && SUCCEEDED(device->CreateShaderResourceView(this->m_impact[f], nullptr, &this->m_impactSrv[f]));
        }
    }

    if (ok) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = (sizeof(ParticleConstants) + 15) & ~15u;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok = SUCCEEDED(device->CreateBuffer(&desc, nullptr, &this->m_particleConstants));
    }

    if (ok) {
        // src over dest by alpha, colour only: the frame's alpha is FFX glow's mask
        D3D11_BLEND_DESC blend = {};
        D3D11_RENDER_TARGET_BLEND_DESC& rt = blend.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ZERO;
        rt.DestBlendAlpha = D3D11_BLEND_ONE;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        ok = SUCCEEDED(device->CreateBlendState(&blend, &this->m_blendParticles));
    }

    if (!ok) {
        CGxDevice::Log("D3d11Water: no splash particles: %.600s", error.c_str());
        this->IReleaseParticles();
        this->m_particlesFailed = true;
        return false;
    }

    UINT zero[4] = {};
    this->m_device->m_ctx->ClearUnorderedAccessViewUint(this->m_particleUav, zero);

    for (ID3D11UnorderedAccessView* uav : this->m_impactUav) {
        this->m_device->m_ctx->ClearUnorderedAccessViewUint(uav, zero);
    }

    this->m_particlesValid = false;
    return true;
}

void D3d11Water::IReleaseParticles() {
    SafeRelease(this->m_blendParticles);
    SafeRelease(this->m_particleConstants);
    for (uint32_t f = 0; f < s_fields; f++) {
        SafeRelease(this->m_impactSrv[f]);
        SafeRelease(this->m_impactUav[f]);
        SafeRelease(this->m_impact[f]);
    }

    SafeRelease(this->m_particleSrv);
    SafeRelease(this->m_particleUav);
    SafeRelease(this->m_particles);
    SafeRelease(this->m_particlePs);
    SafeRelease(this->m_particleVs);
    SafeRelease(this->m_updateCs);
    SafeRelease(this->m_emitCs);
    this->m_particlesFailed = false;
    this->m_particlesValid = false;
    this->m_particleQuiet = 1000.0f;
}

void D3d11Water::IWriteParticleConstants(float dt, uint32_t emit, const float* emitters, uint32_t emitterCount) {
    const GxWaterParams& p = this->m_params;
    ParticleConstants c = {};
    // particle space to render space
    c.m_offset[0] = static_cast<float>(this->m_particleOrigin[0] - p.m_origin.x);
    c.m_offset[1] = static_cast<float>(this->m_particleOrigin[1] - p.m_origin.y);
    c.m_offset[2] = static_cast<float>(this->m_particleOrigin[2] - p.m_origin.z);
    c.m_offset[3] = dt;
    c.m_field[0] = static_cast<float>(static_cast<double>(this->m_originX) * s_texel - this->m_particleOrigin[0]);
    c.m_field[1] = static_cast<float>(static_cast<double>(this->m_originY) * s_texel - this->m_particleOrigin[1]);
    c.m_field[2] = 1.0f / s_texel;
    c.m_field[3] = static_cast<float>(s_fieldSize);
    // the game's own gravity is 19.3 yards / s2; droplets look right a little lighter
    c.m_sim[0] = 15.0f;
    c.m_sim[1] = 0.6f;
    c.m_sim[2] = 1000000.0f;
    c.m_sim[3] = 0.004f;
    c.m_emit[0] = this->m_nextSlot % s_maxParticles;
    c.m_emit[1] = s_maxParticles;
    c.m_emit[2] = this->m_particleSeed++ * 0x9E3779B9u;
    c.m_emit[3] = emit;
    c.m_count[0] = emitterCount;
    c.m_count[1] = this->m_fieldValid[0] ? 1u : 0u;
    c.m_count[2] = this->m_fieldValid[1] ? 1u : 0u;

    if (emitters && emitterCount) {
        memcpy(c.m_emitters, emitters, sizeof(float) * 12 * emitterCount);
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11DeviceContext* ctx = this->m_device->m_ctx;

    if (SUCCEEDED(ctx->Map(this->m_particleConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &c, sizeof(c));
        ctx->Unmap(this->m_particleConstants, 0);
    }
}

void D3d11Water::IRunParticles(float dt) {
    uint32_t splashCount = this->m_splashCount;
    this->m_splashCount = 0;
    this->m_particleQuiet += dt;

    // nothing in the air and nothing to throw
    if (!splashCount && this->m_particleQuiet > s_longestLife + 0.5f) {
        return;
    }

    if (!this->ICreateParticles()) {
        return;
    }

    CGxDeviceD3d11* dev = this->m_device;
    ID3D11DeviceContext* ctx = dev->m_ctx;
    const GxWaterParams& p = this->m_params;

    // particle space follows the player in big jumps only (a teleport): what is in the air goes
    double dx = p.m_centerX - this->m_particleOrigin[0];
    double dy = p.m_centerY - this->m_particleOrigin[1];

    if (!this->m_particlesValid || dx * dx + dy * dy > 1000.0 * 1000.0) {
        this->m_particleOrigin[0] = p.m_centerX;
        this->m_particleOrigin[1] = p.m_centerY;
        this->m_particleOrigin[2] = splashCount ? this->m_splashes[0].m_z : p.m_origin.z;
        UINT zero[4] = {};
        ctx->ClearUnorderedAccessViewUint(this->m_particleUav, zero);
        this->m_particlesValid = true;
    }

    // the emitters, each with the end of its share of the particles
    float emitters[s_maxSplashes * 3][4];
    uint32_t emit = 0;
    uint32_t emitterCount = 0;

    for (uint32_t i = 0; i < splashCount; i++) {
        const GxWaterSplash& splash = this->m_splashes[i];
        uint32_t count = splash.m_count;

        // at most a quarter of the ring a frame, so a crowd does not wipe what is in the air
        if (emit + count > s_maxParticles / 4) {
            count = s_maxParticles / 4 - emit;
        }

        if (!count) {
            continue;
        }

        emit += count;
        float* a = emitters[emitterCount * 3];
        float* b = emitters[emitterCount * 3 + 1];
        float* e = emitters[emitterCount * 3 + 2];
        a[0] = static_cast<float>(splash.m_x - this->m_particleOrigin[0]);
        a[1] = static_cast<float>(splash.m_y - this->m_particleOrigin[1]);
        a[2] = static_cast<float>(splash.m_z - this->m_particleOrigin[2]);
        a[3] = static_cast<float>(emit);
        b[0] = splash.m_up;
        b[1] = splash.m_out;
        b[2] = splash.m_radius;
        b[3] = static_cast<float>(splash.m_kind);
        e[0] = splash.m_color[0];
        e[1] = splash.m_color[1];
        e[2] = splash.m_color[2];
        e[3] = static_cast<float>(splash.m_liquid);
        emitterCount++;
    }

    if (emit) {
        this->m_particleQuiet = 0.0f;
    }

    this->IWriteParticleConstants(dt, emit, emitters[0], emitterCount);

    ID3D11ShaderResourceView* noViews[3] = {};
    ctx->PSSetShaderResources(0, 3, noViews);
    ctx->VSSetShaderResources(21, 1, noViews);
    ID3D11UnorderedAccessView* uavs[3] = { this->m_particleUav, this->m_impactUav[0], this->m_impactUav[1] };
    ctx->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
    ctx->CSSetConstantBuffers(6, 1, &this->m_particleConstants);

    if (emit) {
        ctx->CSSetShader(this->m_emitCs, nullptr, 0);
        ctx->Dispatch((emit + 63) / 64, 1, 1);
        this->m_nextSlot = (this->m_nextSlot + emit) % s_maxParticles;
    }

    ctx->CSSetShader(this->m_updateCs, nullptr, 0);
    ctx->Dispatch((s_maxParticles + 63) / 64, 1, 1);

    ID3D11UnorderedAccessView* noUavs[3] = {};
    ctx->CSSetUnorderedAccessViews(0, 3, noUavs, nullptr);
    ctx->CSSetShader(nullptr, nullptr, 0);
    this->m_statsParticles += emit;
}

void D3d11Water::AfterLiquids() {
    CGxDeviceD3d11* dev = this->m_device;
    ID3D11DeviceContext* ctx = dev->m_ctx;

    // only into the frame the water was drawn into with the copies, and while something is in the air
    if (!ctx || !this->m_worldActive || !this->m_captured || dev->m_curRtv != this->m_capturedRtv || !this->m_particleVs || !this->m_particlesValid || this->m_particleQuiet > s_longestLife + 0.5f || !(this->m_params.m_features & GxWater_Splashes)) {
        return;
    }

    // the render origin of this frame
    this->IWriteParticleConstants(0.0f, 0, nullptr, 0);

    // the droplets show what is behind them: the frame as it is now, the water drawn (the copy taken before the liquids
    // holds the bottom of the lake, and droplets showed it dark)
    bool hdr = dev->m_lighting && dev->m_lighting->HdrActive() && dev->m_lighting->SceneTexture();
    ID3D11Texture2D* source = hdr ? dev->m_lighting->SceneTexture() : dev->m_frameColor;

    if (source && this->m_scene) {
        if (dev->m_samples > 1) {
            dev->m_ctx->ResolveSubresource(this->m_scene, 0, source, 0, this->m_sceneFormat);
        } else {
            dev->m_ctx->CopyResource(this->m_scene, source);
        }
    }

    dev->IXformSetViewport();
    D3D11_VIEWPORT viewport = dev->m_boundViewport;
    ID3D11RenderTargetView* rtv = dev->m_curRtv;
    ID3D11DepthStencilView* dsv = dev->m_curDsv;

    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->OMSetBlendState(this->m_blendParticles, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(dev->IDepthState(D3D11_COMPARISON_LESS_EQUAL, 0), 0);
    ctx->RSSetState(dev->IRasterState(0, false, false, 0, dev->m_samples > 1));
    ctx->RSSetViewports(1, &viewport);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(this->m_particleVs, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(this->m_particlePs, nullptr, 0);
    // the water's constants: the frame's view and light, which every class shares
    ID3D11Buffer* constants[2] = { this->m_constants[GxLiquid_Water], this->m_particleConstants };
    ctx->VSSetConstantBuffers(5, 2, constants);
    ctx->PSSetConstantBuffers(5, 2, constants);
    ctx->VSSetShaderResources(21, 1, &this->m_particleSrv);
    // the droplets show the scene behind them, turned over: its copy and the linear sampler
    ctx->PSSetShaderResources(18, 1, &this->m_sceneSrv);
    ctx->PSSetSamplers(15, 1, &this->m_sampler);
    ctx->Draw(s_maxParticles * 6, 0);

    ID3D11ShaderResourceView* none = nullptr;
    ctx->VSSetShaderResources(21, 1, &none);
    ctx->PSSetShaderResources(18, 1, &none);
    this->IRestoreDevice();
}
