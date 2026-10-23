#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Water.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Compiler.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Util.h"
#include "Engine/Source/Tempest/C44Matrix.h"

// MODIFIED: modern water, see D3d11Water.h

using namespace GxD3d11;

namespace {

// the GxWater cbuffer of s_hlslWater (D3d11Dx9Shader.cpp), b5
struct WaterConstants {
    uint32_t m_flags[4];        // x features (0 = the stock colour), y debug, z reflection steps, w 0
    float m_viewInv[16];        // view to world (relative to the render origin)
    float m_proj[16];
    float m_projInv[16];
    float m_viewport[4];        // the world viewport: left, top, width, height (pixels)
    float m_depthRange[4];      // min, max
    float m_target[4];          // the copies: 1 / width, 1 / height, width, height
    float m_sunDir[4];          // toward the sun (world); w how far above the horizon (0 to 1)
    float m_sunColor[4];        // linear
    float m_sky[6][4];          // linear: top, middle, band 1, band 2, smog, horizon
    float m_ambient[4];         // linear; w the sky's visibility
    float m_params0[4];         // refraction, clarity, reflection, detail
    float m_params1[4];         // foam, ripple normal, time, 0
    float m_field[4];           // the height field: xy its corner (world, relative), z 1 / its size (yards), w 1 / texels
    float m_fieldExtra[4];      // x texel (yards), y 1 = valid
    float m_view[16];           // world (relative to the render origin) to view
    float m_tess[4];            // x 1 = tessellate, y largest factor, z edge length near (yards), w wave height
};

// the passes' cbuffer (s_waterPassHlsl), b0
struct PassConstants {
    int32_t m_shift[4];         // xy the field's shift (texels), z a seed, w the field's size
    float m_wave[4];            // x (speed * dt / texel)^2, y damping per step, z raindrop chance per texel, w drop depth
    float m_impulses[D3d11Water::s_maxImpulses][4];  // xy texel position, z radius (texels), w depth (yards)
    int32_t m_count[4];         // x impulses, yz the field's corner (absolute texels)
    float m_invProj[16];
    float m_viewport[4];
    float m_depthRange[4];
    float m_projW[4];
};

struct ShaderDesc {
    const char* m_entry;
    const char* m_profile;
    bool m_msaa;
};

const ShaderDesc s_shaderDescs[] = {
    { "FullscreenVs", "vs_5_0", false },
    { "SimPs", "ps_5_0", false },
    { "DepthPs", "ps_5_0", false },
    { "DepthPs", "ps_5_0", true },
};

void CopyMatrix(float* out, const C44Matrix& m) {
    memcpy(out, &m, sizeof(float) * 16);
}

float Decode(float c) {
    return c > 0.0f ? powf(c, 2.2f) : 0.0f;
}

void Linear(const C3Vector& c, float* out) {
    out[0] = Decode(c.x);
    out[1] = Decode(c.y);
    out[2] = Decode(c.z);
}

int32_t FloorDiv(float value, float step) {
    return static_cast<int32_t>(floor(static_cast<double>(value) / step));
}

}

D3d11Water::D3d11Water(CGxDeviceD3d11* device) : m_device(device) {
}

D3d11Water::~D3d11Water() {
    this->ReleaseAll();
}

bool D3d11Water::ICreateShaders() {
    if (this->m_shaders[0]) {
        return true;
    }

    if (this->m_failed) {
        return false;
    }

    ID3D11Device* device = this->m_device->m_d3dDevice;

    for (uint32_t i = 0; i < Shaders_Last; i++) {
        const ShaderDesc& desc = s_shaderDescs[i];
        std::string source = desc.m_msaa ? "#define GX_MSAA 1\n" : "#define GX_MSAA 0\n";
        source += s_hlslWaterPasses;
        std::vector<uint8_t> code;
        std::string error;

        if (!CompileHlsl(source, desc.m_entry, desc.m_profile, code, error)) {
            CGxDevice::Log("D3d11Water: %s did not compile: %.600s", desc.m_entry, error.c_str());
            this->m_failed = true;
            return false;
        }

        HRESULT hr = desc.m_profile[0] == 'v' ? device->CreateVertexShader(code.data(), code.size(), nullptr, reinterpret_cast<ID3D11VertexShader**>(&this->m_shaders[i])) : device->CreatePixelShader(code.data(), code.size(), nullptr, reinterpret_cast<ID3D11PixelShader**>(&this->m_shaders[i]));

        if (FAILED(hr)) {
            CGxDevice::Log("D3d11Water: creating %s failed (%08X)", desc.m_entry, static_cast<uint32_t>(hr));
            this->m_failed = true;
            return false;
        }
    }

    return true;
}

bool D3d11Water::ICreateTessellation() {
    if (this->m_hullShader && this->m_domainShader) {
        return true;
    }

    if (this->m_tessFailed) {
        return false;
    }

    std::string source = s_hlslCommon;
    source += s_hlslPatchPoint;
    source += s_hlslWaterConstants;
    source += s_hlslWaterTessellation;
    std::vector<uint8_t> hs;
    std::vector<uint8_t> ds;
    std::string error;
    ID3D11Device* device = this->m_device->m_d3dDevice;

    if (!CompileHlsl(source, "WaterHs", "hs_5_0", hs, error) || !CompileHlsl(source, "WaterDs", "ds_5_0", ds, error) || FAILED(device->CreateHullShader(hs.data(), hs.size(), nullptr, &this->m_hullShader)) || FAILED(device->CreateDomainShader(ds.data(), ds.size(), nullptr, &this->m_domainShader))) {
        CGxDevice::Log("D3d11Water: no tessellation: %.600s", error.c_str());
        SafeRelease(this->m_hullShader);
        SafeRelease(this->m_domainShader);
        this->m_tessFailed = true;
        return false;
    }

    return true;
}

bool D3d11Water::ICreateResources() {
    if (this->m_constants) {
        return true;
    }

    ID3D11Device* device = this->m_device->m_d3dDevice;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (sizeof(WaterConstants) + 15) & ~15u;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(device->CreateBuffer(&desc, nullptr, &this->m_constants))) {
        return false;
    }

    // the stock colour: flags 0
    std::vector<uint8_t> zero(desc.ByteWidth, 0);
    D3D11_SUBRESOURCE_DATA data = { zero.data(), 0, 0 };
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.CPUAccessFlags = 0;

    if (FAILED(device->CreateBuffer(&desc, &data, &this->m_offConstants))) {
        return false;
    }

    desc.ByteWidth = (sizeof(PassConstants) + 15) & ~15u;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(device->CreateBuffer(&desc, nullptr, &this->m_simConstants))) {
        return false;
    }

    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;

    if (FAILED(device->CreateSamplerState(&sampler, &this->m_sampler))) {
        return false;
    }

    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    return SUCCEEDED(device->CreateBlendState(&blend, &this->m_blendOpaque));
}

bool D3d11Water::IEnsureField() {
    if (this->m_field[0]) {
        return true;
    }

    ID3D11Device* device = this->m_device->m_d3dDevice;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = s_fieldSize;
    desc.Height = s_fieldSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    // 32-bit: at 16 bits the difference of neighbouring heights came in steps, and the slopes drew contour lines
    desc.Format = DXGI_FORMAT_R32G32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    for (uint32_t i = 0; i < 2; i++) {
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &this->m_field[i])) || FAILED(device->CreateRenderTargetView(this->m_field[i], nullptr, &this->m_fieldRtv[i])) || FAILED(device->CreateShaderResourceView(this->m_field[i], nullptr, &this->m_fieldSrv[i]))) {
            CGxDevice::Log("D3d11Water: no height field (%u x %u)", s_fieldSize, s_fieldSize);
            this->IReleaseField();
            return false;
        }
    }

    this->m_fieldValid = false;
    return true;
}

void D3d11Water::IReleaseField() {
    for (uint32_t i = 0; i < 2; i++) {
        SafeRelease(this->m_fieldSrv[i]);
        SafeRelease(this->m_fieldRtv[i]);
        SafeRelease(this->m_field[i]);
    }

    this->m_fieldValid = false;
}

bool D3d11Water::IEnsureCopies(DXGI_FORMAT format) {
    CGxDeviceD3d11* dev = this->m_device;
    uint32_t width = dev->m_frameWidth;
    uint32_t height = dev->m_frameHeight;

    if (this->m_scene && this->m_depth && this->m_width == width && this->m_height == height && this->m_sceneFormat == format) {
        return true;
    }

    this->ReleaseTargets();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(dev->m_d3dDevice->CreateTexture2D(&desc, nullptr, &this->m_scene)) || FAILED(dev->m_d3dDevice->CreateShaderResourceView(this->m_scene, nullptr, &this->m_sceneSrv))) {
        CGxDevice::Log("D3d11Water: no scene copy (%u x %u, format %u)", width, height, static_cast<uint32_t>(format));
        this->ReleaseTargets();
        return false;
    }

    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(dev->m_d3dDevice->CreateTexture2D(&desc, nullptr, &this->m_depth)) || FAILED(dev->m_d3dDevice->CreateRenderTargetView(this->m_depth, nullptr, &this->m_depthRtv)) || FAILED(dev->m_d3dDevice->CreateShaderResourceView(this->m_depth, nullptr, &this->m_depthSrv))) {
        CGxDevice::Log("D3d11Water: no depth copy (%u x %u)", width, height);
        this->ReleaseTargets();
        return false;
    }

    this->m_width = width;
    this->m_height = height;
    this->m_sceneFormat = format;
    return true;
}

void D3d11Water::ReleaseTargets() {
    this->m_captured = false;
    this->m_capturedRtv = nullptr;
    SafeRelease(this->m_sceneSrv);
    SafeRelease(this->m_scene);
    SafeRelease(this->m_depthSrv);
    SafeRelease(this->m_depthRtv);
    SafeRelease(this->m_depth);
    this->m_width = 0;
    this->m_height = 0;
    this->m_sceneFormat = DXGI_FORMAT_UNKNOWN;
}

void D3d11Water::ReleaseAll() {
    this->ReleaseTargets();
    this->IReleaseField();
    this->m_worldActive = false;

    for (ID3D11DeviceChild*& shader : this->m_shaders) {
        SafeRelease(shader);
    }

    SafeRelease(this->m_hullShader);
    SafeRelease(this->m_domainShader);
    this->m_tessFailed = false;

    SafeRelease(this->m_blendOpaque);
    SafeRelease(this->m_sampler);
    SafeRelease(this->m_simConstants);
    SafeRelease(this->m_offConstants);
    SafeRelease(this->m_constants);
    this->m_failed = false;
}

void D3d11Water::IDrawFullscreen(EShader shader, ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport, ID3D11ShaderResourceView* srv) {
    CGxDeviceD3d11* dev = this->m_device;
    ID3D11DeviceContext* ctx = dev->m_ctx;
    ID3D11ShaderResourceView* none[2] = {};

    // the target first: a texture bound for writing unbinds itself as a source
    ctx->PSSetShaderResources(0, 2, none);
    ctx->OMSetRenderTargets(1, &target, nullptr);
    ctx->OMSetBlendState(this->m_blendOpaque, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(dev->IDepthState(D3D11_COMPARISON_ALWAYS, 0), 0);
    ctx->RSSetState(dev->IRasterState(0, false, false, 0, false));
    ctx->RSSetViewports(1, &viewport);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(static_cast<ID3D11VertexShader*>(this->m_shaders[Shader_FullscreenVs]), nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(static_cast<ID3D11PixelShader*>(this->m_shaders[shader]), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &this->m_simConstants);

    // the field at t0, the depth at t1
    ID3D11ShaderResourceView* views[2] = { shader == Shader_SimPs ? srv : nullptr, shader == Shader_SimPs ? nullptr : srv };
    ctx->PSSetShaderResources(0, 2, views);
    ctx->Draw(3, 0);
    ctx->PSSetShaderResources(0, 2, none);
}

void D3d11Water::IRestoreDevice() {
    CGxDeviceD3d11* dev = this->m_device;

    for (ID3D11ShaderResourceView*& srv : dev->m_boundSrv) {
        srv = nullptr;
    }

    dev->m_lightingBound = false;
    dev->IInvalidateBindings();
    dev->IBindTargets();
}

void D3d11Water::IStep(int32_t shiftX, int32_t shiftY, bool impulses) {
    CGxDeviceD3d11* dev = this->m_device;
    ID3D11DeviceContext* ctx = dev->m_ctx;
    const GxWaterParams& p = this->m_params;
    const float dt = 1.0f / 60.0f;

    PassConstants c = {};
    c.m_shift[0] = shiftX;
    c.m_shift[1] = shiftY;
    c.m_shift[2] = static_cast<int32_t>(this->m_stepSeed++);
    c.m_shift[3] = static_cast<int32_t>(s_fieldSize);

    // stable while (speed * dt / texel)^2 stays under 0.5
    float courant = p.m_rippleSpeed * dt / s_texel;
    courant = courant > 0.7f ? 0.7f : courant;
    c.m_wave[0] = courant * courant;
    c.m_wave[1] = expf(-p.m_rippleDamping * dt);
    c.m_wave[2] = p.m_rain * s_texel * s_texel * dt;
    c.m_wave[3] = p.m_rainStrength;

    uint32_t count = 0;

    if (impulses) {
        for (uint32_t i = 0; i < this->m_impulseCount; i++) {
            const GxWaterImpulse& im = this->m_impulses[i];
            float x = (im.m_x - this->m_originX * s_texel) / s_texel;
            float y = (im.m_y - this->m_originY * s_texel) / s_texel;

            if (x < -8.0f || y < -8.0f || x > s_fieldSize + 8.0f || y > s_fieldSize + 8.0f) {
                continue;
            }

            float* out = c.m_impulses[count++];
            out[0] = x;
            out[1] = y;
            out[2] = im.m_radius / s_texel;
            out[3] = im.m_strength;
        }
    }

    c.m_count[0] = static_cast<int32_t>(count);
    c.m_count[1] = this->m_originX;
    c.m_count[2] = this->m_originY;

    D3D11_MAPPED_SUBRESOURCE mapped;

    if (SUCCEEDED(ctx->Map(this->m_simConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &c, sizeof(c));
        ctx->Unmap(this->m_simConstants, 0);
    }

    uint32_t next = this->m_current ^ 1u;
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(s_fieldSize), static_cast<float>(s_fieldSize), 0.0f, 1.0f };
    this->IDrawFullscreen(Shader_SimPs, this->m_fieldRtv[next], vp, this->m_fieldSrv[this->m_current]);
    this->m_current = next;
    this->m_statsImpulses += count;
}

void D3d11Water::Update(const GxWaterParams& params, const GxWaterImpulse* impulses, uint32_t count) {
    CGxDeviceD3d11* dev = this->m_device;
    this->m_params = params;
    this->m_worldActive = true;
    this->m_captured = false;
    this->m_capturedRtv = nullptr;
    this->m_statsDraws = this->m_drawsThisFrame;
    this->m_drawsThisFrame = 0;
    this->m_statsSteps = 0;

    if (!dev->m_ctx || !params.m_features || !this->ICreateResources() || !this->ICreateShaders()) {
        return;
    }

    // the disturbances wait for the next step; when more come than a step takes, the strongest stay
    for (uint32_t i = 0; i < count; i++) {
        if (this->m_impulseCount < s_maxImpulses) {
            this->m_impulses[this->m_impulseCount++] = impulses[i];
            continue;
        }

        uint32_t weakest = 0;

        for (uint32_t j = 1; j < s_maxImpulses; j++) {
            if (fabsf(this->m_impulses[j].m_strength) < fabsf(this->m_impulses[weakest].m_strength)) {
                weakest = j;
            }
        }

        if (fabsf(impulses[i].m_strength) > fabsf(this->m_impulses[weakest].m_strength)) {
            this->m_impulses[weakest] = impulses[i];
        }
    }

    if (!(params.m_features & GxWater_Ripples)) {
        this->m_fieldValid = false;
        this->m_impulseCount = 0;
        return;
    }

    if (!this->IEnsureField()) {
        return;
    }

    int32_t originX = FloorDiv(params.m_centerX, s_texel) - static_cast<int32_t>(s_fieldSize / 2);
    int32_t originY = FloorDiv(params.m_centerY, s_texel) - static_cast<int32_t>(s_fieldSize / 2);
    int32_t shiftX = originX - this->m_originX;
    int32_t shiftY = originY - this->m_originY;
    ID3D11ShaderResourceView* noViews[3] = {};
    dev->m_ctx->PSSetShaderResources(18, 3, noViews);

    // a new field after a teleport, a long pause or a first frame
    if (!this->m_fieldValid || abs(shiftX) >= static_cast<int32_t>(s_fieldSize) || abs(shiftY) >= static_cast<int32_t>(s_fieldSize) || params.m_deltaTime > 1.0f) {
        float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        dev->m_ctx->ClearRenderTargetView(this->m_fieldRtv[0], clear);
        dev->m_ctx->ClearRenderTargetView(this->m_fieldRtv[1], clear);
        this->m_current = 0;
        this->m_fieldValid = true;
        this->m_stepTime = 0.0f;
        shiftX = 0;
        shiftY = 0;
    }

    this->m_originX = originX;
    this->m_originY = originY;

    float dt = params.m_deltaTime < 0.0f ? 0.0f : (params.m_deltaTime > 0.1f ? 0.1f : params.m_deltaTime);
    this->m_stepTime += dt;
    uint32_t steps = static_cast<uint32_t>(this->m_stepTime * 60.0f);
    steps = steps > 4 ? 4 : steps;
    this->m_stepTime -= steps / 60.0f;
    this->m_stepTime = this->m_stepTime > 1.0f / 60.0f ? 1.0f / 60.0f : this->m_stepTime;

    // the field moves with the player now, even on a frame without a step
    if (!steps && (shiftX || shiftY)) {
        steps = 1;
    }

    this->m_statsImpulses = 0;

    for (uint32_t i = 0; i < steps; i++) {
        this->IStep(i == 0 ? shiftX : 0, i == 0 ? shiftY : 0, i == 0);
    }

    if (steps) {
        this->m_impulseCount = 0;
    }

    this->m_statsSteps = steps;

    if (steps) {
        this->IRestoreDevice();
    }
}

void D3d11Water::IWriteConstants() {
    CGxDeviceD3d11* dev = this->m_device;
    const GxWaterParams& p = this->m_params;
    WaterConstants c = {};

    c.m_flags[0] = p.m_features;
    c.m_flags[1] = p.m_debug;
    c.m_flags[2] = p.m_reflectionSteps;

    CGxMatrixStack& viewStack = dev->m_xforms[GxXform_View];
    const C44Matrix& view = viewStack.m_mtx[viewStack.m_level];
    CopyMatrix(c.m_viewInv, view.Inverse());
    CopyMatrix(c.m_proj, dev->m_projNative);
    CopyMatrix(c.m_projInv, dev->m_projNative.Inverse());

    const D3D11_VIEWPORT& vp = dev->m_boundViewport;
    c.m_viewport[0] = vp.TopLeftX;
    c.m_viewport[1] = vp.TopLeftY;
    c.m_viewport[2] = vp.Width > 0.0f ? vp.Width : 1.0f;
    c.m_viewport[3] = vp.Height > 0.0f ? vp.Height : 1.0f;
    c.m_depthRange[0] = vp.MinDepth;
    c.m_depthRange[1] = vp.MaxDepth;
    c.m_target[0] = this->m_width ? 1.0f / this->m_width : 0.0f;
    c.m_target[1] = this->m_height ? 1.0f / this->m_height : 0.0f;
    c.m_target[2] = static_cast<float>(this->m_width);
    c.m_target[3] = static_cast<float>(this->m_height);

    C3Vector toSun(-p.m_sunDirection.x, -p.m_sunDirection.y, -p.m_sunDirection.z);
    float length = sqrtf(toSun.x * toSun.x + toSun.y * toSun.y + toSun.z * toSun.z);

    if (length > 0.0f) {
        toSun.x /= length;
        toSun.y /= length;
        toSun.z /= length;
    }

    c.m_sunDir[0] = toSun.x;
    c.m_sunDir[1] = toSun.y;
    c.m_sunDir[2] = toSun.z;
    // the highlight fades as the sun reaches the horizon and is gone below it
    c.m_sunDir[3] = toSun.z > 0.0f ? (toSun.z > 0.15f ? 1.0f : toSun.z / 0.15f) : 0.0f;
    Linear(p.m_sunColor, c.m_sunColor);

    for (uint32_t i = 0; i < 6; i++) {
        Linear(p.m_skyColors[i], c.m_sky[i]);
    }

    Linear(p.m_ambient, c.m_ambient);
    c.m_ambient[3] = p.m_skyVisible;

    c.m_params0[0] = p.m_refraction;
    c.m_params0[1] = p.m_clarity;
    c.m_params0[2] = p.m_reflection;
    c.m_params0[3] = p.m_detail;
    c.m_params1[0] = (p.m_features & GxWater_Foam) ? p.m_foam : 0.0f;
    c.m_params1[1] = p.m_rippleNormal;
    c.m_params1[2] = p.m_time;

    bool field = (p.m_features & GxWater_Ripples) && this->m_fieldValid;

    if (field) {
        // the corner relative to the render origin, in double: the absolute coordinates reach 17,000 yards
        c.m_field[0] = static_cast<float>(static_cast<double>(this->m_originX) * s_texel - p.m_origin.x);
        c.m_field[1] = static_cast<float>(static_cast<double>(this->m_originY) * s_texel - p.m_origin.y);
        c.m_field[2] = 1.0f / (s_fieldSize * s_texel);
        c.m_field[3] = 1.0f / s_fieldSize;
        c.m_fieldExtra[0] = s_texel;
        c.m_fieldExtra[1] = 1.0f;
    }

    CopyMatrix(c.m_view, view);
    bool tess = field && (p.m_features & GxWater_Tessellation) && this->ICreateTessellation();
    c.m_tess[0] = tess ? 1.0f : 0.0f;
    c.m_tess[1] = 32.0f;
    c.m_tess[2] = 0.3f;
    c.m_tess[3] = p.m_waveHeight;

    D3D11_MAPPED_SUBRESOURCE mapped;

    if (SUCCEEDED(dev->m_ctx->Map(this->m_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &c, sizeof(c));
        dev->m_ctx->Unmap(this->m_constants, 0);
    }
}

void D3d11Water::BeforeLiquids(const C3Vector& origin) {
    CGxDeviceD3d11* dev = this->m_device;
    this->m_params.m_origin = origin;
    this->m_captured = false;
    this->m_capturedRtv = nullptr;

    if (!this->m_worldActive || !this->m_params.m_features || !dev->m_ctx || !dev->m_frameDepthSrv || !this->m_constants || !this->ICreateShaders()) {
        return;
    }

    ID3D11RenderTargetView* frame = dev->IFrameRtv();

    // the world is drawing into a texture of its own: the water there stays stock
    if (!frame || dev->m_curRtv != frame) {
        return;
    }

    bool hdr = dev->m_lighting && dev->m_lighting->HdrActive() && dev->m_lighting->SceneTexture();
    ID3D11Texture2D* source = hdr ? dev->m_lighting->SceneTexture() : dev->m_frameColor;
    DXGI_FORMAT format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;

    if (!source || !this->IEnsureCopies(format)) {
        return;
    }

    // the viewport the liquids draw with
    dev->IXformSetViewport();
    D3D11_VIEWPORT world = dev->m_boundViewport;

    ID3D11ShaderResourceView* noViews[3] = {};
    dev->m_ctx->PSSetShaderResources(18, 3, noViews);

    if (dev->m_samples > 1) {
        dev->m_ctx->ResolveSubresource(this->m_scene, 0, source, 0, format);
    } else {
        dev->m_ctx->CopyResource(this->m_scene, source);
    }

    // the depth as clip w, over the whole frame so its pixels are the frame's
    PassConstants c = {};
    CopyMatrix(c.m_invProj, dev->m_projNative.Inverse());
    c.m_viewport[0] = world.TopLeftX;
    c.m_viewport[1] = world.TopLeftY;
    c.m_viewport[2] = world.Width > 0.0f ? world.Width : 1.0f;
    c.m_viewport[3] = world.Height > 0.0f ? world.Height : 1.0f;
    c.m_depthRange[0] = world.MinDepth;
    c.m_depthRange[1] = world.MaxDepth;
    c.m_depthRange[2] = world.MaxDepth > world.MinDepth ? 1.0f / (world.MaxDepth - world.MinDepth) : 1.0f;
    const float* proj = reinterpret_cast<const float*>(&dev->m_projNative);
    c.m_projW[0] = proj[3];
    c.m_projW[1] = proj[7];
    c.m_projW[2] = proj[11];
    c.m_projW[3] = proj[15];

    D3D11_MAPPED_SUBRESOURCE mapped;

    if (SUCCEEDED(dev->m_ctx->Map(this->m_simConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &c, sizeof(c));
        dev->m_ctx->Unmap(this->m_simConstants, 0);
    }

    D3D11_VIEWPORT full = { 0.0f, 0.0f, static_cast<float>(this->m_width), static_cast<float>(this->m_height), 0.0f, 1.0f };
    this->IDrawFullscreen(dev->m_samples > 1 ? Shader_DepthPsMs : Shader_DepthPs, this->m_depthRtv, full, dev->m_frameDepthSrv);

    this->IWriteConstants();
    this->m_captured = true;
    this->m_capturedRtv = frame;
    this->IRestoreDevice();

    // the first few captures of a session, for Logs\gx.log
    static uint32_t s_logged = 0;

    if (s_logged < 3) {
        s_logged++;
        CGxDevice::Log("D3d11Water: capture %u: features 0x%X, hdr %u, %u samples, %ux%u, viewport %.0f,%.0f %.0fx%.0f, field %u", s_logged, this->m_params.m_features, hdr ? 1 : 0, dev->m_samples, this->m_width, this->m_height, world.TopLeftX, world.TopLeftY, world.Width, world.Height, this->m_fieldValid ? 1 : 0);
    }
}

void D3d11Water::BindForDraw() {
    CGxDeviceD3d11* dev = this->m_device;
    ID3D11DeviceContext* ctx = dev->m_ctx;

    if (!ctx || !this->m_offConstants) {
        return;
    }

    bool on = this->m_worldActive && this->m_captured && dev->m_curRtv == this->m_capturedRtv;
    ID3D11Buffer* constants = on ? this->m_constants : this->m_offConstants;
    ctx->PSSetConstantBuffers(5, 1, &constants);

    if (!on) {
        return;
    }

    ID3D11ShaderResourceView* views[3] = { this->m_sceneSrv, this->m_depthSrv, this->m_fieldValid ? this->m_fieldSrv[this->m_current] : nullptr };
    ctx->PSSetShaderResources(18, 3, views);
    // s15 is the device's slot too: its cache learns what is bound there now
    ctx->PSSetSamplers(15, 1, &this->m_sampler);
    dev->m_boundSampler[15] = this->m_sampler;
    this->m_drawsThisFrame++;
}

bool D3d11Water::TessellationReady() const {
    const CGxDeviceD3d11* dev = this->m_device;
    return dev->m_ctx && this->m_worldActive && this->m_captured && dev->m_curRtv == this->m_capturedRtv && this->m_fieldValid && (this->m_params.m_features & GxWater_Tessellation) && this->m_hullShader && this->m_domainShader;
}

void D3d11Water::BindTessellation() {
    ID3D11DeviceContext* ctx = this->m_device->m_ctx;

    ctx->HSSetShader(this->m_hullShader, nullptr, 0);
    ctx->DSSetShader(this->m_domainShader, nullptr, 0);
    ctx->HSSetConstantBuffers(5, 1, &this->m_constants);
    ctx->DSSetConstantBuffers(5, 1, &this->m_constants);
    ctx->DSSetShaderResources(20, 1, &this->m_fieldSrv[this->m_current]);
    ctx->DSSetSamplers(15, 1, &this->m_sampler);
}

void D3d11Water::EndWorld() {
    this->m_worldActive = false;
    this->m_lastCaptured = this->m_captured;
    this->m_captured = false;
    this->m_capturedRtv = nullptr;

    if (this->m_device->m_ctx) {
        ID3D11ShaderResourceView* noViews[3] = {};
        this->m_device->m_ctx->PSSetShaderResources(18, 3, noViews);
    }
}

void D3d11Water::GetStats(GxWaterStats& stats) {
    stats.m_simActive = this->m_fieldValid;
    stats.m_simSteps = this->m_statsSteps;
    stats.m_impulses = this->m_statsImpulses;
    stats.m_captured = this->m_lastCaptured;
    stats.m_waterDraws = this->m_statsDraws;
    stats.m_fieldSize = s_fieldSize;
    stats.m_texel = s_texel;
}
