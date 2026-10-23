#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Engine/Source/Gx/Buffer.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Util.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Base/FrameProfile.h"

// MODIFIED: D3D11 device, see CGxDeviceD3d11.h. Render state, shaders, constants and the draw.

using namespace GxD3d11;

namespace {

// CGxVertexAttrib::m_type, as CGxDeviceD3d9Ex::s_gxAttribToD3dAttribType names them: D3DCOLOR, UBYTE4, UBYTE4N,
// FLOAT2, FLOAT3, SHORT2, FLOAT1
const DXGI_FORMAT s_attribFormats[7] = { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R32_FLOAT };
const uint32_t s_attribClasses[7] = { Input_Float, Input_Uint, Input_Float, Input_Float, Input_Float, Input_Sint, Input_Float };
const uint8_t s_attribComponents[7] = { 4, 4, 4, 2, 3, 2, 1 };
const uint32_t s_attribSizes[7] = { 4, 4, 4, 8, 12, 4, 4 };
const char* const s_attribSemantics[GxVAs_Last] = { "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "COLOR", "COLOR", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD" };
const UINT s_attribSemanticIndices[GxVAs_Last] = { 0, 0, 0, 0, 0, 1, 0, 1, 2, 3, 4, 5, 6, 7 };

// EGxBlend, as CGxDeviceD3d9Ex::s_srcBlend / s_dstBlend
const D3D11_BLEND s_srcBlend[GxBlends_Last] = { D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_BLEND_FACTOR };
const D3D11_BLEND s_dstBlend[GxBlends_Last] = { D3D11_BLEND_ZERO, D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_INV_BLEND_FACTOR };

// GxRs_DepthFunc, as CGxDeviceD3d9Ex::s_cmpFunc
const D3D11_COMPARISON_FUNC s_cmpFunc[4] = { D3D11_COMPARISON_LESS_EQUAL, D3D11_COMPARISON_EQUAL, D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_LESS };

const D3D11_PRIMITIVE_TOPOLOGY s_topologies[GxPrims_Last] = { D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };

const uint32_t s_ringSize = 8 * 1024 * 1024;

// D3D11 takes no colour factors for alpha
D3D11_BLEND AlphaBlend(D3D11_BLEND blend) {
    switch (blend) {
        case D3D11_BLEND_SRC_COLOR:
            return D3D11_BLEND_SRC_ALPHA;
        case D3D11_BLEND_INV_SRC_COLOR:
            return D3D11_BLEND_INV_SRC_ALPHA;
        case D3D11_BLEND_DEST_COLOR:
            return D3D11_BLEND_DEST_ALPHA;
        case D3D11_BLEND_INV_DEST_COLOR:
            return D3D11_BLEND_INV_DEST_ALPHA;
        default:
            return blend;
    }
}

void CopyMatrix(float* out, const C44Matrix& m) {
    memcpy(out, &m, sizeof(float) * 16);
}

void TransformDirection(const C3Vector& v, const C44Matrix& m, float* out) {
    out[0] = v.x * m.a0 + v.y * m.b0 + v.z * m.c0;
    out[1] = v.x * m.a1 + v.y * m.b1 + v.z * m.c1;
    out[2] = v.x * m.a2 + v.y * m.b2 + v.z * m.c2;
}

uint32_t RoundConstants(uint32_t count) {
    count = count < 16 ? 16 : count;
    count = (count + 15) & ~15u;
    return count > 256 ? 256 : count;
}

}

void CGxDeviceD3d11::IInvalidateBindings() {
    // the next draw binds everything again
    this->m_bindingsValid = false;
    this->m_lightingBound = false;
    this->m_boundIb = nullptr;
    this->m_viewportDirty = 1;
    this->m_scissorRectDirty = 1;
}

void CGxDeviceD3d11::IRsSendToHw(EGxRenderState which) {
    // everything else is read from the render states when the draw is synced; the texture generation keeps the
    // matrix CGxDeviceD3d9Ex::ISetTexGen makes for it
    if (which >= GxRs_TexGen0 && which <= GxRs_TexGen7) {
        this->ISetTexGen(which - GxRs_TexGen0, this->m_appRenderStates[which].m_value.m_data.i[0]);
    }
}

void CGxDeviceD3d11::ISetTexGen(uint32_t tmu, int32_t texGen) {
    if (tmu >= 8) {
        return;
    }

    C44Matrix matrix;

    if (texGen == GxTexGen_ObjectLinear || texGen == GxTexGen_World) {
        CGxMatrixStack& view = this->m_xforms[GxXform_View];
        matrix = view.m_mtx[view.m_level];
        matrix = matrix.AffineInverse();

        if (texGen == GxTexGen_ObjectLinear) {
            CGxMatrixStack& world = this->m_xforms[GxXform_World];
            matrix = matrix * world.m_mtx[world.m_level].Inverse();
        }
    } else if (texGen == GxTexGen_SphereMap) {
        matrix.a0 = 0.5f;
        matrix.b1 = 0.5f;
        matrix.d0 = 0.5f;
        matrix.d1 = 0.5f;
    } else {
        this->m_texGen[tmu].Identity();
        return;
    }

    CGxMatrixStack& stack = this->m_texGen[tmu];
    stack.m_dirty = 1;
    stack.m_flags[stack.m_level] &= ~CGxMatrixStack::F_Identity;
    stack.m_mtx[stack.m_level] = matrix;
}

void CGxDeviceD3d11::IXformSetViewport() {
    // CGxDeviceD3d9Ex::IXformSetViewport's arithmetic, so the pixels match
    const CRect& window = this->DeviceCurWindow();
    uint32_t x = static_cast<uint32_t>(this->m_viewport.m_x.m_l * window.maxX + 0.5f);
    uint32_t y = static_cast<uint32_t>((1.0f - this->m_viewport.m_y.m_h) * window.maxY + 0.5f);
    uint32_t width = static_cast<uint32_t>(this->m_viewport.m_x.m_h * window.maxX - x + 0.5f);
    uint32_t height = static_cast<uint32_t>((1.0f - this->m_viewport.m_y.m_l) * window.maxY - y + 0.5f);

    if (this->m_textureTarget[0].m_apiSpecific || this->m_textureTarget[1].m_apiSpecific) {
        y = static_cast<uint32_t>(window.maxY * this->m_viewport.m_y.m_l);
    }

    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = static_cast<float>(x);
    viewport.TopLeftY = static_cast<float>(y);
    viewport.Width = static_cast<float>(width ? width : 1);
    viewport.Height = static_cast<float>(height ? height : 1);
    viewport.MinDepth = this->m_viewport.m_z.m_l;
    viewport.MaxDepth = this->m_viewport.m_z.m_h;

    if (this->m_ctx) {
        this->m_ctx->RSSetViewports(1, &viewport);
    }

    this->m_boundViewport = viewport;
    this->m_viewportDirty = 0;
}

void CGxDeviceD3d11::IStateSyncScissorRect() {
    if (!this->m_scissorRectDirty && this->m_bindingsValid) {
        return;
    }

    const CRect& window = this->DeviceCurWindow();
    D3D11_RECT rect;

    rect.left = static_cast<LONG>(this->m_scissorRect.minX * window.maxX + 0.5f);
    rect.right = static_cast<LONG>(this->m_scissorRect.maxX * window.maxX + 1.0f);

    if (this->m_textureTarget[0].m_apiSpecific || this->m_textureTarget[1].m_apiSpecific) {
        rect.top = static_cast<LONG>(this->m_scissorRect.minY * window.maxY + 0.5f);
        rect.bottom = static_cast<LONG>(1.0f + this->m_scissorRect.maxY * window.maxY);
    } else {
        rect.top = static_cast<LONG>((1.0f - this->m_scissorRect.maxY) * window.maxY + 0.5f);
        rect.bottom = static_cast<LONG>(1.0f + (1.0f - this->m_scissorRect.minY) * window.maxY);
    }

    if (rect.left < 0) {
        rect.left = 0;
    }

    if (rect.top < 0) {
        rect.top = 0;
    }

    if (rect.right > window.maxX) {
        rect.right = static_cast<LONG>(window.maxX);
    }

    if (rect.bottom > window.maxY) {
        rect.bottom = static_cast<LONG>(window.maxY);
    }

    if (this->m_ctx) {
        this->m_ctx->RSSetScissorRects(1, &rect);
    }

    this->m_boundScissor = rect;
    this->m_scissorRectDirty = 0;
}

bool CGxDeviceD3d11::IBuildVertexLayout() {
    VertexLayout& layout = this->m_layout;
    layout.m_count = 0;
    layout.m_attribMask = 0;
    layout.m_attribClass = 0;
    layout.m_streamCount = 0;
    memset(layout.m_texCoordSize, 0, sizeof(layout.m_texCoordSize));

    bool known = this->m_primVertexFormat < GxVertexBufferFormats_Last;
    uint64_t hash = 0xCBF29CE484222325ull;

    for (uint32_t i = 0; i < GxVAs_Last; i++) {
        if (!(this->m_primVertexMask & (1u << i))) {
            continue;
        }

        CGxBuf* buf = known ? this->m_primVertexBuf : this->m_primVertexFormatBuf[i];
        const CGxVertexAttrib& attrib = this->m_primVertexFormatAttrib[i];

        if (!buf || !buf->m_pool || attrib.m_type >= 7) {
            return false;
        }

        uint32_t stream;

        for (stream = 0; stream < layout.m_streamCount; stream++) {
            if (layout.m_streams[stream] == buf) {
                break;
            }
        }

        if (stream == layout.m_streamCount) {
            layout.m_streams[stream] = buf;
            layout.m_strides[stream] = known ? this->m_primVertexSize : attrib.m_bufSize;
            layout.m_streamCount++;
        } else if (!known && !attrib.m_bufSize) {
            layout.m_strides[stream] += s_attribSizes[attrib.m_type];
        }

        if (!layout.m_strides[stream]) {
            layout.m_strides[stream] = s_attribSizes[attrib.m_type];
        }

        D3D11_INPUT_ELEMENT_DESC& element = layout.m_elements[layout.m_count++];
        element.SemanticName = s_attribSemantics[i];
        element.SemanticIndex = s_attribSemanticIndices[i];
        element.Format = s_attribFormats[attrib.m_type];
        element.InputSlot = stream;
        element.AlignedByteOffset = attrib.m_offset;
        element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        element.InstanceDataStepRate = 0;

        layout.m_attribMask |= 1u << i;
        layout.m_attribClass |= s_attribClasses[attrib.m_type] << (2 * i);

        if (i >= GxVA_TexCoord0) {
            layout.m_texCoordSize[i - GxVA_TexCoord0] = s_attribComponents[attrib.m_type];
        }

        uint64_t key[4] = { i, attrib.m_type, attrib.m_offset, stream };

        for (uint64_t k : key) {
            hash = (hash ^ k) * 0x100000001B3ull;
        }
    }

    layout.m_hash = hash;
    return (layout.m_attribMask & 1) != 0;
}

CGxDeviceD3d11::Program* CGxDeviceD3d11::IProgram(const std::string& hlsl, const char* entry, const char* profile, uint32_t constCount) {
    std::string keyText = hlsl;
    keyText += '\n';
    keyText += entry;
    keyText += '\n';
    keyText += profile;
    Hash128 key = HashBytes(keyText.data(), keyText.size());
    auto found = this->m_programs.find(key);

    if (found != this->m_programs.end()) {
        return found->second;
    }

    std::vector<uint8_t> code;
    std::string error;
    Program* program = nullptr;

    if (CompileHlsl(hlsl, entry, profile, code, error)) {
        program = new Program();
        HRESULT hr = E_FAIL;

        switch (profile[0]) {
            case 'v':
                hr = this->m_d3dDevice->CreateVertexShader(code.data(), code.size(), nullptr, &program->m_vs);
                program->m_bytecode = std::move(code);
                break;
            case 'p':
                hr = this->m_d3dDevice->CreatePixelShader(code.data(), code.size(), nullptr, &program->m_ps);
                break;
            case 'g':
                hr = this->m_d3dDevice->CreateGeometryShader(code.data(), code.size(), nullptr, &program->m_gs);
                break;
            default:
                break;
        }

        if (FAILED(hr)) {
            CGxDevice::Log("CGxDeviceD3d11: creating a %s shader failed (%08X)", profile, static_cast<uint32_t>(hr));
            delete program;
            program = nullptr;
        } else {
            program->m_id = this->m_nextProgramId++;
            program->m_constCount = RoundConstants(constCount);
        }
    } else {
        // the source goes to Logs\ for a look; the draws that need it are skipped
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "Logs\\d3d11_%016llx.hlsl", static_cast<unsigned long long>(key.m_lo));
        OsCreateDirectory("Logs", 0);
        FILE* file = fopen(path, "wb");

        if (file) {
            fwrite(hlsl.data(), 1, hlsl.size(), file);
            fprintf(file, "\n/* %s %s\n%s\n*/\n", entry, profile, error.c_str());
            fclose(file);
        }

        CGxDevice::Log("CGxDeviceD3d11: a %s shader did not compile, source in %s: %.600s", profile, path, error.c_str());
    }

    this->m_programs[key] = program;
    return program;
}

CGxDeviceD3d11::Program* CGxDeviceD3d11::ITranslatedProgram(CGxShader* shader, uint32_t variant) {
    ShaderEntry* entry = static_cast<ShaderEntry*>(shader->m_apiSpecific);

    if (!entry) {
        return nullptr;
    }

    for (const auto& known : entry->m_variants) {
        if (known.first == variant) {
            return known.second;
        }
    }

    std::string hlsl;
    std::string error;
    Program* program = nullptr;

    if (entry->m_dx9.Emit(variant, hlsl, error)) {
        program = this->IProgram(hlsl, "main", entry->m_dx9.m_vertex ? "vs_5_0" : "ps_5_0", entry->m_dx9.m_constCount);
    } else {
        CGxDevice::Log("CGxDeviceD3d11: a %s shader was not translated: %s", entry->m_dx9.m_vertex ? "vertex" : "pixel", error.c_str());
    }

    entry->m_variants.push_back({ variant, program });
    return program;
}

CGxDeviceD3d11::Program* CGxDeviceD3d11::IFixedVertexProgram(const FfVertexKey& key) {
    if (this->m_lastFfVertex && !memcmp(&key, &this->m_lastFfVertexKey, sizeof(key))) {
        return this->m_lastFfVertex;
    }

    Hash128 hash = HashBytes(&key, sizeof(key));
    auto found = this->m_ffVertex.find(hash);
    Program* program;

    if (found != this->m_ffVertex.end()) {
        program = found->second;
    } else {
        program = this->IProgram(FfVertexHlsl(key), "main", "vs_5_0", 0);
        this->m_ffVertex[hash] = program;
    }

    this->m_lastFfVertexKey = key;
    this->m_lastFfVertex = program;
    return program;
}

CGxDeviceD3d11::Program* CGxDeviceD3d11::IFixedPixelProgram(const FfPixelKey& key) {
    if (this->m_lastFfPixel && !memcmp(&key, &this->m_lastFfPixelKey, sizeof(key))) {
        return this->m_lastFfPixel;
    }

    Hash128 hash = HashBytes(&key, sizeof(key));
    auto found = this->m_ffPixel.find(hash);
    Program* program;

    if (found != this->m_ffPixel.end()) {
        program = found->second;
    } else {
        program = this->IProgram(FfPixelHlsl(key), "main", "ps_5_0", 0);
        this->m_ffPixel[hash] = program;
    }

    this->m_lastFfPixelKey = key;
    this->m_lastFfPixel = program;
    return program;
}

ID3D11InputLayout* CGxDeviceD3d11::IInputLayout(Program* program) {
    if (this->m_lastLayout && this->m_lastLayoutProgram == program->m_id && this->m_lastLayoutHash == this->m_layout.m_hash) {
        return this->m_lastLayout;
    }

    std::pair<uint64_t, uint64_t> key(program->m_id, this->m_layout.m_hash);
    auto found = this->m_inputLayouts.find(key);

    if (found != this->m_inputLayouts.end()) {
        this->m_lastLayoutProgram = program->m_id;
        this->m_lastLayoutHash = this->m_layout.m_hash;
        this->m_lastLayout = found->second;
        return found->second;
    }

    ID3D11InputLayout* layout = nullptr;
    HRESULT hr = this->m_d3dDevice->CreateInputLayout(this->m_layout.m_elements, this->m_layout.m_count, program->m_bytecode.data(), program->m_bytecode.size(), &layout);

    if (FAILED(hr)) {
        CGxDevice::Log("CGxDeviceD3d11: no input layout for vertex mask %04X (%08X)", this->m_layout.m_attribMask, static_cast<uint32_t>(hr));
        layout = nullptr;
    }

    this->m_inputLayouts[key] = layout;
    this->m_lastLayoutProgram = program->m_id;
    this->m_lastLayoutHash = this->m_layout.m_hash;
    this->m_lastLayout = layout;
    return layout;
}

ID3D11BlendState* CGxDeviceD3d11::IBlendState(uint32_t mode, uint32_t writeMask) {
    if (mode >= GxBlends_Last) {
        mode = GxBlend_Opaque;
    }

    uint32_t key = mode | (writeMask << 8);
    auto found = this->m_blendStates.find(key);

    if (found != this->m_blendStates.end()) {
        return found->second;
    }

    D3D11_BLEND_DESC desc = {};
    D3D11_RENDER_TARGET_BLEND_DESC& target = desc.RenderTarget[0];
    target.BlendEnable = mode >= GxBlend_Alpha;
    target.SrcBlend = s_srcBlend[mode];
    target.DestBlend = s_dstBlend[mode];
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = AlphaBlend(s_srcBlend[mode]);
    target.DestBlendAlpha = AlphaBlend(s_dstBlend[mode]);
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;

    // the engine's mask is 1 red, 2 blue, 4 green, 8 alpha (CGxDeviceD3d9Ex::DsSet, Ds_ColorWriteEnable)
    uint8_t mask = 0;
    mask |= (writeMask & 0x1) ? D3D11_COLOR_WRITE_ENABLE_RED : 0;
    mask |= (writeMask & 0x4) ? D3D11_COLOR_WRITE_ENABLE_GREEN : 0;
    mask |= (writeMask & 0x2) ? D3D11_COLOR_WRITE_ENABLE_BLUE : 0;
    mask |= (writeMask & 0x8) ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0;
    target.RenderTargetWriteMask = mask;

    ID3D11BlendState* state = nullptr;
    this->m_d3dDevice->CreateBlendState(&desc, &state);
    this->m_blendStates[key] = state;
    return state;
}

ID3D11DepthStencilState* CGxDeviceD3d11::IDepthState(uint32_t func, uint32_t write) {
    uint32_t key = func | (write << 8);
    auto found = this->m_depthStates.find(key);

    if (found != this->m_depthStates.end()) {
        return found->second;
    }

    D3D11_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = !(func == D3D11_COMPARISON_ALWAYS && !write);
    desc.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(func);
    desc.StencilEnable = FALSE;

    ID3D11DepthStencilState* state = nullptr;
    this->m_d3dDevice->CreateDepthStencilState(&desc, &state);
    this->m_depthStates[key] = state;
    return state;
}

ID3D11RasterizerState* CGxDeviceD3d11::IRasterState(uint32_t cull, bool wireframe, bool scissor, int32_t depthBias, bool multisample) {
    uint64_t key = cull | (wireframe ? 4u : 0u) | (scissor ? 8u : 0u) | (multisample ? 16u : 0u) | (static_cast<uint64_t>(static_cast<uint32_t>(depthBias)) << 32);
    auto found = this->m_rasterStates.find(key);

    if (found != this->m_rasterStates.end()) {
        return found->second;
    }

    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    // D3DCULL_CW culls clockwise triangles, and clockwise is the front here as in D3D9
    desc.CullMode = cull == 0 ? D3D11_CULL_NONE : (cull == 1 ? D3D11_CULL_FRONT : D3D11_CULL_BACK);
    desc.FrontCounterClockwise = FALSE;
    desc.DepthBias = depthBias;
    desc.DepthBiasClamp = 0.0f;
    desc.SlopeScaledDepthBias = 0.0f;
    desc.DepthClipEnable = TRUE;
    desc.ScissorEnable = scissor;
    desc.MultisampleEnable = multisample;
    desc.AntialiasedLineEnable = FALSE;

    ID3D11RasterizerState* state = nullptr;
    this->m_d3dDevice->CreateRasterizerState(&desc, &state);
    this->m_rasterStates[key] = state;
    return state;
}

ID3D11SamplerState* CGxDeviceD3d11::ISamplerState(const CGxTexFlags& flags, bool compare) {
    uint32_t filter = flags.m_filter < GxTexFilters_Last ? flags.m_filter : GxTex_Linear;
    uint32_t anisotropy = flags.m_maxAnisotropy < 1 ? 1 : (flags.m_maxAnisotropy > 16 ? 16 : flags.m_maxAnisotropy);
    uint32_t key = filter | (flags.m_wrapU << 3) | (flags.m_wrapV << 4) | (anisotropy << 5) | (compare ? 1u << 10 : 0);
    auto found = this->m_samplerStates.find(key);

    if (found != this->m_samplerStates.end()) {
        return found->second;
    }

    D3D11_SAMPLER_DESC desc = {};
    desc.MaxLOD = D3D11_FLOAT32_MAX;

    // CGxDeviceD3d9Ex::s_filterModes: min, mag, mip (mip NONE samples level 0 only)
    switch (filter) {
        case GxTex_Nearest:
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            desc.MaxLOD = 0.0f;
            break;
        case GxTex_Linear:
            desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            desc.MaxLOD = 0.0f;
            break;
        case GxTex_NearestMipNearest:
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            break;
        case GxTex_LinearMipNearest:
            desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            break;
        case GxTex_LinearMipLinear:
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            break;
        default:
            desc.Filter = anisotropy > 1 ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            break;
    }

    if (compare) {
        // the D3D9 hardware shadow map: 1 where the coordinate's z is at or before the texel, filtered
        bool point = filter == GxTex_Nearest || filter == GxTex_NearestMipNearest;
        desc.Filter = point ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        desc.MaxLOD = 0.0f;
    } else {
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    }

    desc.AddressU = flags.m_wrapU ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = flags.m_wrapV ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MaxAnisotropy = anisotropy;
    desc.MipLODBias = 0.0f;
    desc.MinLOD = 0.0f;

    ID3D11SamplerState* state = nullptr;
    this->m_d3dDevice->CreateSamplerState(&desc, &state);
    this->m_samplerStates[key] = state;
    return state;
}

void CGxDeviceD3d11::IReleasePrograms() {
    for (auto& entry : this->m_programs) {
        Program* program = entry.second;

        if (!program) {
            continue;
        }

        SafeRelease(program->m_vs);
        SafeRelease(program->m_ps);
        SafeRelease(program->m_gs);
        delete program;
    }

    this->m_programs.clear();
    this->m_ffVertex.clear();
    this->m_ffPixel.clear();
    this->m_lastFfVertex = nullptr;
    this->m_lastFfPixel = nullptr;

    for (auto& entry : this->m_inputLayouts) {
        SafeRelease(entry.second);
    }

    this->m_inputLayouts.clear();
    this->m_lastLayout = nullptr;
    this->m_presentVs = nullptr;
    this->m_presentPs = nullptr;
    this->m_blitVs = nullptr;
    this->m_blitPs = nullptr;
    this->m_spriteGs = nullptr;
    this->m_clearVs = nullptr;
    this->m_clearPs = nullptr;
}

void CGxDeviceD3d11::IReleaseStateObjects() {
    for (auto& entry : this->m_blendStates) {
        SafeRelease(entry.second);
    }

    for (auto& entry : this->m_depthStates) {
        SafeRelease(entry.second);
    }

    for (auto& entry : this->m_rasterStates) {
        SafeRelease(entry.second);
    }

    for (auto& entry : this->m_samplerStates) {
        SafeRelease(entry.second);
    }

    this->m_blendStates.clear();
    this->m_depthStates.clear();
    this->m_rasterStates.clear();
    this->m_samplerStates.clear();
}

void CGxDeviceD3d11::IRingReserve(uint32_t bytes) {
    if (!this->m_cbOffsets) {
        return;
    }

    if (this->m_ringOffset + bytes <= this->m_ringSize) {
        return;
    }

    // the ring wraps: its contents go, so every slice is uploaded again
    this->m_ringOffset = 0;
    this->m_ringDiscard = true;
    this->m_vsConstantsDirty = true;
    this->m_psConstantsDirty = true;
    this->m_drawConstantsValid = false;
    this->m_ffConstantsValid = false;
}

bool CGxDeviceD3d11::IUploadConstants(EConstants which, const void* data, uint32_t size, ConstantSlice& slice) {
    uint32_t aligned = (size + 255) & ~255u;

    if (this->m_cbOffsets) {
        if (!this->m_ring) {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = s_ringSize;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            if (FAILED(this->m_d3dDevice->CreateBuffer(&desc, nullptr, &this->m_ring))) {
                this->m_cbOffsets = false;
                return this->IUploadConstants(which, data, size, slice);
            }

            this->m_ringSize = s_ringSize;
            this->m_ringOffset = 0;
            this->m_ringDiscard = true;
        }

        if (this->m_ringOffset + aligned > this->m_ringSize) {
            this->m_ringOffset = 0;
            this->m_ringDiscard = true;
        }

        D3D11_MAPPED_SUBRESOURCE mapped;

        if (FAILED(this->m_ctx->Map(this->m_ring, 0, this->m_ringDiscard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped))) {
            return false;
        }

        memcpy(static_cast<uint8_t*>(mapped.pData) + this->m_ringOffset, data, size);
        this->m_ctx->Unmap(this->m_ring, 0);
        this->m_ringDiscard = false;

        slice.m_buffer = this->m_ring;
        slice.m_first = this->m_ringOffset / 16;
        slice.m_count = aligned / 16;
        this->m_ringOffset += aligned;
        return true;
    }

    static const uint32_t s_sizes[Constants_Last] = { 4096, 4096, (sizeof(DrawConstants) + 15) & ~15u, (sizeof(FfConstants) + 15) & ~15u };
    ID3D11Buffer*& buffer = this->m_fallbackConstants[which];

    if (!buffer) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = s_sizes[which];
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(this->m_d3dDevice->CreateBuffer(&desc, nullptr, &buffer))) {
            return false;
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped;

    if (FAILED(this->m_ctx->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }

    memcpy(mapped.pData, data, size < s_sizes[which] ? size : s_sizes[which]);
    this->m_ctx->Unmap(buffer, 0);

    slice.m_buffer = buffer;
    slice.m_first = 0;
    slice.m_count = s_sizes[which] / 16;
    return true;
}

void CGxDeviceD3d11::IBindConstants(EStage stage, uint32_t slot, const ConstantSlice& slice) {
    if (this->m_bindingsValid && this->m_boundConstants[stage][slot] == slice.m_buffer && this->m_boundConstantsFirst[stage][slot] == slice.m_first && this->m_boundConstantsCount[stage][slot] == slice.m_count) {
        return;
    }

    this->m_boundConstants[stage][slot] = slice.m_buffer;
    this->m_boundConstantsFirst[stage][slot] = slice.m_first;
    this->m_boundConstantsCount[stage][slot] = slice.m_count;
    ID3D11Buffer* buffer = slice.m_buffer;

    if (this->m_cbOffsets && buffer) {
        UINT first = slice.m_first;
        UINT count = slice.m_count;

        switch (stage) {
            case Stage_Vertex:
                this->m_ctx1->VSSetConstantBuffers1(slot, 1, &buffer, &first, &count);
                break;
            case Stage_Geometry:
                this->m_ctx1->GSSetConstantBuffers1(slot, 1, &buffer, &first, &count);
                break;
            default:
                this->m_ctx1->PSSetConstantBuffers1(slot, 1, &buffer, &first, &count);
                break;
        }

        return;
    }

    switch (stage) {
        case Stage_Vertex:
            this->m_ctx->VSSetConstantBuffers(slot, 1, &buffer);
            break;
        case Stage_Geometry:
            this->m_ctx->GSSetConstantBuffers(slot, 1, &buffer);
            break;
        default:
            this->m_ctx->PSSetConstantBuffers(slot, 1, &buffer);
            break;
    }
}

void CGxDeviceD3d11::IBuildDrawConstants(DrawConstants& c) {
    memset(&c, 0, sizeof(c));

    float width = this->m_boundViewport.Width > 0.0f ? this->m_boundViewport.Width : 1.0f;
    float height = this->m_boundViewport.Height > 0.0f ? this->m_boundViewport.Height : 1.0f;
    c.m_viewport[0] = 1.0f / width;
    c.m_viewport[1] = 1.0f / height;
    c.m_viewport[2] = width;
    c.m_viewport[3] = height;

    for (uint32_t i = 0; i < 6; i++) {
        c.m_clipPlanes[i][0] = this->m_clipPlane[i].n.x;
        c.m_clipPlanes[i][1] = this->m_clipPlane[i].n.y;
        c.m_clipPlanes[i][2] = this->m_clipPlane[i].n.z;
        c.m_clipPlanes[i][3] = this->m_clipPlane[i].d;
    }

    c.m_clipMask[0] = this->m_appRenderStates[GxRs_ClipPlaneMask].m_value.m_data.u[0] & 0x3F;

    int32_t alphaRef = this->m_appRenderStates[GxRs_AlphaRef].m_value.m_data.i[0];
    c.m_alphaTest[0] = static_cast<float>(alphaRef) / 255.0f;
    c.m_alphaTest[1] = alphaRef > 0 ? 1.0f : 0.0f;

    ColorToFloat4(this->m_appRenderStates[GxRs_FogColor].m_value.m_data.u[0], c.m_fog);
    c.m_fog[3] = (this->m_appMasterEnables & 0x2) && this->m_appRenderStates[GxRs_Fog].m_value.m_data.i[0] ? 1.0f : 0.0f;

    // D3DRS_POINTSIZE as CGxDeviceD3d9Ex::IRsSendToHw sets it
    const float* attenuation = this->m_appRenderStates[GxRs_PointScaleAttenuation].m_value.m_data.f;
    bool scaled = attenuation[0] != CGxDevice::s_pointScaleAttenuation.x || attenuation[1] != CGxDevice::s_pointScaleAttenuation.y || attenuation[2] != CGxDevice::s_pointScaleAttenuation.z;
    float pointSize = this->m_appRenderStates[GxRs_PointScale].m_value.m_data.f[0];

    if (scaled) {
        const CRect& window = this->DeviceCurWindow();
        float viewportHeight = (1.0f - this->m_viewport.m_y.m_l) * window.maxY - (1.0f - this->m_viewport.m_y.m_h) * window.maxY;

        if (viewportHeight != 0.0f) {
            pointSize /= viewportHeight;
        }
    }

    float maxSize = this->m_appRenderStates[GxRs_PointScaleMax].m_value.m_data.f[0];
    c.m_pointSize[0] = pointSize;
    c.m_pointSize[1] = this->m_appRenderStates[GxRs_PointScaleMin].m_value.m_data.f[0];
    c.m_pointSize[2] = maxSize < this->m_caps.m_pointScaleMax ? maxSize : this->m_caps.m_pointScaleMax;
    c.m_pointSize[3] = this->m_appRenderStates[GxRs_PointSprite].m_value.m_data.i[0] ? 1.0f : 0.0f;
    c.m_pointScale[0] = attenuation[0];
    c.m_pointScale[1] = attenuation[1];
    c.m_pointScale[2] = attenuation[2];
    c.m_pointScale[3] = scaled ? 1.0f : 0.0f;

    // MODIFIED: modern lighting: into the HDR scene a colour may pass 1 (an 8-bit target clamps it), except where the
    // blend multiplies the scene by it
    if (this->m_lighting && this->m_lighting->HdrActive() && this->m_curRtv && this->m_curRtv == this->m_lighting->SceneRtv()) {
        uint32_t blend = this->m_appRenderStates[GxRs_BlendingMode].m_value.m_data.u[0];
        c.m_output[0] = blend == GxBlend_Mod || blend == GxBlend_Mod2x ? 1.0f : 16.0f;
    }
}

void CGxDeviceD3d11::IBuildFixedConstants(FfConstants& c, const uint8_t* texTransform) {
    memset(&c, 0, sizeof(c));

    CGxMatrixStack& worldStack = this->m_xforms[GxXform_World];
    CGxMatrixStack& viewStack = this->m_xforms[GxXform_View];
    const C44Matrix& world = worldStack.m_mtx[worldStack.m_level];
    const C44Matrix& view = viewStack.m_mtx[viewStack.m_level];
    C44Matrix worldView = world * view;
    C44Matrix worldViewProj = worldView * this->m_projNative;
    C44Matrix normal = worldView.Inverse().Transpose();

    CopyMatrix(c.m_worldViewProj, worldViewProj);
    CopyMatrix(c.m_worldView, worldView);
    CopyMatrix(c.m_world, world);
    CopyMatrix(c.m_normal, normal);

    // the texture matrices, as CGxDeviceD3d9Ex::IXformSetTex makes D3DTS_TEXTURE#
    for (uint32_t s = 0; s < 8; s++) {
        if (!texTransform[s]) {
            continue;
        }

        CGxMatrixStack& texGen = this->m_texGen[s];
        CGxMatrixStack& xform = this->m_xforms[s];
        int32_t shader = this->m_appRenderStates[GxRs_TextureShader0 + s].m_value.m_data.i[0];
        C44Matrix matrix;

        if (shader == 0) {
            matrix = texGen.m_mtx[texGen.m_level];
        } else {
            C44Matrix product = texGen.m_mtx[texGen.m_level] * xform.m_mtx[xform.m_level];
            matrix = product;

            if (shader == 1 && !this->m_appRenderStates[GxRs_TexGen0 + s].m_value.m_data.i[0]) {
                // a two-component coordinate: the translation row moves to the third row
                matrix.c0 = product.d0;
                matrix.c1 = product.d1;
            }
        }

        CopyMatrix(c.m_texture[s], matrix);
    }

    // CGxDeviceD3d9Ex::ISetMaterial: ambient is the diffuse colour
    ColorToFloat4(this->m_appRenderStates[GxRs_MatDiffuse].m_value.m_data.u[0], c.m_matDiffuse);
    memcpy(c.m_matAmbient, c.m_matDiffuse, sizeof(c.m_matAmbient));
    ColorToFloat4(this->m_appRenderStates[GxRs_MatSpecular].m_value.m_data.u[0], c.m_matSpecular);
    ColorToFloat4(this->m_appRenderStates[GxRs_MatEmissive].m_value.m_data.u[0], c.m_matEmissive);
    c.m_matPower[0] = this->m_appRenderStates[GxRs_MatSpecularExp].m_value.m_data.f[0];

    // CGxDeviceD3d9Ex::IStateSyncLights: lighting off, no vertex colour and a material colour other than white turns on
    // "ambient only" lighting, white ambient and no lights, to give the draw its material colour
    bool lightingState = this->m_appRenderStates[GxRs_Lighting].m_value.m_data.i[0] != 0;
    bool ambientOnly = !lightingState && !(this->m_primVertexMask & (1 << GxVA_Color0)) && this->m_appRenderStates[GxRs_MatDiffuse].m_value.m_data.u[0] != 0xFFFFFFFF;
    float ambient = ambientOnly ? 1.0f : 0.0f;
    c.m_ambient[0] = ambient;
    c.m_ambient[1] = ambient;
    c.m_ambient[2] = ambient;
    c.m_ambient[3] = ambient;

    for (uint32_t i = 0; i < 4; i++) {
        const CGxApiLight& light = this->m_lights[i];
        C3Vector dir(light.m_dir.x, light.m_dir.y, light.m_dir.z);

        if (light.m_dir.w == 1.0f) {
            TransformDirection(dir, view, c.m_lightPos[i]);
            c.m_lightPos[i][0] += view.d0;
            c.m_lightPos[i][1] += view.d1;
            c.m_lightPos[i][2] += view.d2;
            c.m_lightPos[i][3] = 1.0f;
        } else {
            // the way toward the light: D3D9's direction is the way the light travels
            float d[3];
            TransformDirection(dir, view, d);
            float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            float scale = length > 0.0f ? -1.0f / length : 0.0f;
            c.m_lightPos[i][0] = d[0] * scale;
            c.m_lightPos[i][1] = d[1] * scale;
            c.m_lightPos[i][2] = d[2] * scale;
            c.m_lightPos[i][3] = 0.0f;
        }

        c.m_lightDiffuse[i][0] = light.m_dirColor.x;
        c.m_lightDiffuse[i][1] = light.m_dirColor.y;
        c.m_lightDiffuse[i][2] = light.m_dirColor.z;
        c.m_lightAmbient[i][0] = light.m_ambColor.x;
        c.m_lightAmbient[i][1] = light.m_ambColor.y;
        c.m_lightAmbient[i][2] = light.m_ambColor.z;
        c.m_lightSpecular[i][0] = light.m_specColor.x;
        c.m_lightSpecular[i][1] = light.m_specColor.y;
        c.m_lightSpecular[i][2] = light.m_specColor.z;
        c.m_lightAtten[i][0] = light.m_constantAttenuation;
        c.m_lightAtten[i][1] = light.m_linearAttenuation;
        c.m_lightAtten[i][2] = light.m_quadraticAttenuation;
    }

    float fogStart = this->m_appRenderStates[GxRs_FogStart].m_value.m_data.f[0];
    float fogEnd = this->m_appRenderStates[GxRs_FogEnd].m_value.m_data.f[0];
    c.m_fog[0] = fogStart;
    c.m_fog[1] = fogEnd;
    c.m_fog[2] = fogEnd != fogStart ? 1.0f / (fogEnd - fogStart) : 1.0e20f;
}

bool CGxDeviceD3d11::IStateSync(EGxPrim primType) {
    // the constants the engine wrote, into the registers they would be in on D3D9 (IShaderConstantsFlush)
    ShadowConstants& vsShadow = CGxDevice::s_shadowConstants[1];

    if (vsShadow.dirtyMin <= vsShadow.dirtyMax && vsShadow.dirtyMax < 256) {
        memcpy(&this->m_vsConstants[vsShadow.dirtyMin * 4], &vsShadow.constants[vsShadow.dirtyMin * 4], (vsShadow.dirtyMax - vsShadow.dirtyMin + 1) * 16);
        this->m_vsConstantsDirty = true;
    }

    vsShadow.dirtyMin = 255;
    vsShadow.dirtyMax = 0;

    ShadowConstants& psShadow = CGxDevice::s_shadowConstants[0];

    if (psShadow.dirtyMin <= psShadow.dirtyMax && psShadow.dirtyMax < 256) {
        memcpy(&this->m_psConstants[psShadow.dirtyMin * 4], &psShadow.constants[psShadow.dirtyMin * 4], (psShadow.dirtyMax - psShadow.dirtyMin + 1) * 16);
        this->m_psConstantsDirty = true;
    }

    psShadow.dirtyMin = 255;
    psShadow.dirtyMax = 0;

    this->IRsSync(0);

    bool rebind = !this->m_bindingsValid;

    if (!this->IBuildVertexLayout()) {
        return false;
    }

    // the textures, with their samplers; a texture that is a bound target reads as nothing
    ID3D11ShaderResourceView* srvs[16];
    ID3D11SamplerState* samplers[16];
    uint32_t depthMask = 0;
    uint32_t cubeMask = 0;
    uint32_t boundMask = 0;

    for (uint32_t slot = 0; slot < 16; slot++) {
        CGxTex* texId = static_cast<CGxTex*>(this->m_appRenderStates[GxRs_Texture0 + slot].m_value.m_data.p);
        srvs[slot] = nullptr;
        samplers[slot] = this->m_boundSampler[slot];

        if (!texId) {
            continue;
        }

        if (texId->m_needsUpdate || texId->m_needsCreation || !texId->m_apiSpecificData) {
            this->ITexMarkAsUpdated(texId);

            if (texId->m_needsCreation || !texId->m_apiSpecificData) {
                this->ITexCreate(texId);
            }
        }

        Texture* texture = this->ITexGet(texId);

        if (!texture || !texture->m_srv) {
            continue;
        }

        if (texture->m_mipsDirty) {
            this->IGenerateMips(texId);
        }

        bool isTarget = false;

        for (ID3D11RenderTargetView* rtv : texture->m_rtv) {
            if (rtv && rtv == this->m_curRtv) {
                isTarget = true;
            }
        }

        if (texture->m_dsv && texture->m_dsv == this->m_curDsv) {
            isTarget = true;
        }

        if (isTarget) {
            continue;
        }

        texId->m_lastUsedFrame = this->m_frameCount;
        srvs[slot] = texture->m_srv;
        samplers[slot] = this->ISamplerState(texId->m_flags, texture->m_depth);
        boundMask |= 1u << slot;
        depthMask |= texture->m_depth ? 1u << slot : 0;
        cubeMask |= texture->m_cube ? 1u << slot : 0;
    }

    // the shaders: the engine's when it bound valid ones, the fixed function's otherwise
    CGxShader* vertexShader = static_cast<CGxShader*>(this->m_appRenderStates[GxRs_VertexShader].m_value.m_data.p);
    CGxShader* pixelShader = static_cast<CGxShader*>(this->m_appRenderStates[GxRs_PixelShader].m_value.m_data.p);
    Program* vsProgram = nullptr;
    Program* psProgram = nullptr;
    ShaderEntry* vsEntry = nullptr;
    ShaderEntry* psEntry = nullptr;

    if (vertexShader) {
        if (!vertexShader->m_loaded) {
            this->IShaderCreate(vertexShader);
        }

        if (vertexShader->m_valid && vertexShader->m_apiSpecific) {
            vsEntry = static_cast<ShaderEntry*>(vertexShader->m_apiSpecific);
        }
    }

    if (pixelShader) {
        if (!pixelShader->m_loaded) {
            this->IShaderCreate(pixelShader);
        }

        if (pixelShader->m_valid && pixelShader->m_apiSpecific) {
            psEntry = static_cast<ShaderEntry*>(pixelShader->m_apiSpecific);
        }
    }

    const uint32_t master = this->m_appMasterEnables;
    bool specularEnable = this->m_appRenderStates[GxRs_MatSpecularExp].m_value.m_data.f[0] > 0.0f && this->m_appRenderStates[GxRs_MatSpecular].m_value.m_data.i[0];
    uint8_t texTransform[8] = {};
    bool fixedVertex = vsEntry == nullptr;

    if (psEntry) {
        psProgram = this->ITranslatedProgram(pixelShader, depthMask & psEntry->m_dx9.m_samplerMask);

        if (!psProgram) {
            return false;
        }
    } else {
        // the texture stages run up to the first without a texture
        FfPixelKey key;
        memset(&key, 0, sizeof(key));

        for (uint32_t s = 0; s < 8; s++) {
            if (!(boundMask & (1u << s))) {
                break;
            }

            key.m_stageCount = static_cast<uint8_t>(s + 1);
            key.m_colorOp[s] = static_cast<uint8_t>(this->m_appRenderStates[GxRs_ColorOp0 + s].m_value.m_data.u[0]);
            key.m_alphaOp[s] = static_cast<uint8_t>(this->m_appRenderStates[GxRs_AlphaOp0 + s].m_value.m_data.u[0]);
            key.m_coord[s] = static_cast<uint8_t>(fixedVertex ? s : (this->m_appRenderStates[GxRs_TextureCoord0 + s].m_value.m_data.u[0] & 7));
            key.m_dim[s] = (cubeMask & (1u << s)) ? 3 : 2;
            key.m_projected[s] = fixedVertex && this->m_appRenderStates[GxRs_TextureShader0 + s].m_value.m_data.i[0] == 2;
            key.m_depth[s] = (depthMask & (1u << s)) && !(cubeMask & (1u << s));
        }

        key.m_specular = specularEnable;
        psProgram = this->IFixedPixelProgram(key);

        if (!psProgram) {
            return false;
        }
    }

    if (vsEntry) {
        uint32_t variant = 0;
        uint32_t reads = vsEntry->m_dx9.m_inputMask;

        for (uint32_t a = 0; a < GxVAs_Last; a++) {
            if (!(reads & (1u << a))) {
                continue;
            }

            uint32_t inputClass = (this->m_layout.m_attribMask & (1u << a)) ? (this->m_layout.m_attribClass >> (2 * a)) & 3 : static_cast<uint32_t>(Input_Missing);
            variant |= inputClass << (2 * a);
        }

        vsProgram = this->ITranslatedProgram(vertexShader, variant);

        if (!vsProgram) {
            return false;
        }
    } else {
        FfVertexKey key;
        memset(&key, 0, sizeof(key));
        key.m_attribMask = this->m_layout.m_attribMask;
        key.m_attribClass = this->m_layout.m_attribClass;
        memcpy(key.m_texCoordSize, this->m_layout.m_texCoordSize, sizeof(key.m_texCoordSize));

        bool hasColor0 = (this->m_primVertexMask & (1 << GxVA_Color0)) != 0;
        bool lightingState = this->m_appRenderStates[GxRs_Lighting].m_value.m_data.i[0] != 0;

        if (lightingState) {
            key.m_lighting = (master & 0x1) != 0;

            for (uint32_t i = 0; i < 4 && key.m_lighting; i++) {
                if (this->m_lights[i].m_enable) {
                    key.m_lightTypes[i] = this->m_lights[i].m_dir.w == 1.0f ? FfLight_Point : FfLight_Directional;
                }
            }
        } else if (!hasColor0 && this->m_appRenderStates[GxRs_MatDiffuse].m_value.m_data.u[0] != 0xFFFFFFFF) {
            key.m_lighting = 1;
        }

        // CGxDeviceD3d9Ex::IStateSyncMaterial
        if (hasColor0) {
            int32_t colorMaterial = this->m_appRenderStates[GxRs_ColorMaterial].m_value.m_data.i[0];
            key.m_ambientSource = colorMaterial == 0 ? FfSource_Color0 : FfSource_Material;
            key.m_diffuseSource = colorMaterial == 0 ? FfSource_Color0 : FfSource_Material;
            key.m_specularSource = colorMaterial == 1 ? FfSource_Color0 : FfSource_Material;
            key.m_emissiveSource = colorMaterial == 2 ? FfSource_Color0 : FfSource_Material;
        }

        key.m_specular = key.m_lighting && specularEnable;
        key.m_normalize = this->m_appRenderStates[GxRs_NormalizeNormals].m_value.m_data.i[0] != 0;
        key.m_fog = (master & 0x2) && this->m_appRenderStates[GxRs_Fog].m_value.m_data.i[0];

        const float* attenuation = this->m_appRenderStates[GxRs_PointScaleAttenuation].m_value.m_data.f;
        key.m_pointScale = primType == GxPrim_Points && (attenuation[0] != CGxDevice::s_pointScaleAttenuation.x || attenuation[1] != CGxDevice::s_pointScaleAttenuation.y || attenuation[2] != CGxDevice::s_pointScaleAttenuation.z);

        // the coordinates: one per stage the pixel shader reads
        uint32_t stages = psEntry ? (psEntry->m_dx9.m_readMask >> Var_Tex0) & 0xFF : 0;

        for (uint32_t s = 0; s < 8; s++) {
            if (!psEntry && (boundMask & (1u << s))) {
                stages |= 1u << s;
            }
        }

        key.m_stageMask = static_cast<uint8_t>(stages);

        for (uint32_t s = 0; s < 8; s++) {
            if (!(stages & (1u << s))) {
                continue;
            }

            switch (this->m_appRenderStates[GxRs_TexGen0 + s].m_value.m_data.i[0]) {
                case GxTexGen_ObjectLinear:
                case GxTexGen_World:
                case GxTexGen_View:
                    key.m_texGen[s] = FfTexGen_Position;
                    break;
                case GxTexGen_ReflectionMap:
                case GxTexGen_SphereMap:
                    key.m_texGen[s] = FfTexGen_Reflection;
                    break;
                case GxTexGen_Normal:
                    key.m_texGen[s] = FfTexGen_Normal;
                    break;
                default:
                    key.m_texGen[s] = FfTexGen_None;
                    break;
            }

            key.m_texCoord[s] = static_cast<uint8_t>(this->m_appRenderStates[GxRs_TextureCoord0 + s].m_value.m_data.u[0] & 7);

            // IXformSetTex: shader 0 with no generation matrix passes the coordinate through
            int32_t shader = this->m_appRenderStates[GxRs_TextureShader0 + s].m_value.m_data.i[0];
            CGxMatrixStack& texGen = this->m_texGen[s];
            bool identity = (texGen.m_flags[texGen.m_level] & CGxMatrixStack::F_Identity) != 0;
            key.m_texTransform[s] = (shader == 0 && identity) || shader > 2 ? 0 : 1;
            texTransform[s] = key.m_texTransform[s];
        }

        vsProgram = this->IFixedVertexProgram(key);

        if (!vsProgram) {
            return false;
        }
    }

    ID3D11InputLayout* layout = this->IInputLayout(vsProgram);

    if (!layout) {
        return false;
    }

    bool points = primType == GxPrim_Points;
    Program* gsProgram = points ? this->m_spriteGs : nullptr;

    // the fixed state
    uint32_t blendMode = this->m_appRenderStates[GxRs_BlendingMode].m_value.m_data.u[0];
    uint32_t writeMask = (master & 0x10) ? this->m_appRenderStates[GxRs_ColorWrite].m_value.m_data.u[0] & 0xF : 0;
    ID3D11BlendState* blend = this->IBlendState(blendMode < 2 ? GxBlend_Opaque : blendMode, writeMask);
    float factor = floorf(this->m_appRenderStates[GxRs_BlendFactor].m_value.m_data.f[0] * 255.0f) / 255.0f;

    uint32_t depthFunc = D3D11_COMPARISON_ALWAYS;

    if ((master & 0x4) && this->m_appRenderStates[GxRs_DepthTest].m_value.m_data.i[0]) {
        uint32_t func = this->m_appRenderStates[GxRs_DepthFunc].m_value.m_data.u[0];
        depthFunc = s_cmpFunc[func < 4 ? func : 0];
    }

    uint32_t depthWrite = (master & 0x8) && this->m_appRenderStates[GxRs_DepthWrite].m_value.m_data.i[0] ? 1 : 0;
    ID3D11DepthStencilState* depth = this->IDepthState(depthFunc, depthWrite);

    uint32_t cull = 0;

    if ((master & 0x20) && !points) {
        cull = this->m_appRenderStates[GxRs_Culling].m_value.m_data.u[0];
        cull = cull > 2 ? 2 : cull;
    }

    // D3D9's DEPTHBIAS is in depth units; D3D11 counts 24-bit steps
    float bias = this->m_caps.m_depthBias ? -this->m_appRenderStates[GxRs_PolygonOffset].m_value.m_data.f[0] : 0.0f;
    double steps = static_cast<double>(bias) * 16777216.0;
    int32_t depthBias = steps > 2.0e9 ? 2000000000 : (steps < -2.0e9 ? -2000000000 : static_cast<int32_t>(steps >= 0.0 ? steps + 0.5 : steps - 0.5));
    bool wireframe = !(master & (1 << GxMasterEnable_PolygonFill));
    bool scissor = this->m_appRenderStates[GxRs_ScissorTest].m_value.m_data.i[0] != 0;
    bool multisample = this->m_samples > 1 && this->m_appRenderStates[GxRs_Multisample].m_value.m_data.i[0];
    ID3D11RasterizerState* raster = this->IRasterState(cull, wireframe, scissor, depthBias, multisample);

    if (this->m_viewportDirty || rebind) {
        this->IXformSetViewport();
    }

    if (scissor && (this->m_scissorRectDirty || rebind)) {
        this->m_scissorRectDirty = 1;
        this->IStateSyncScissorRect();
    }

    // the constants: the engine's registers, the draw's state, the fixed function's
    this->IRingReserve(2 * 4096 + sizeof(DrawConstants) + sizeof(FfConstants) + 1024);

    if (vsEntry) {
        uint32_t count = vsProgram->m_constCount;

        if (this->m_vsConstantsDirty || count > this->m_vsSlice.m_count || !this->m_vsSlice.m_buffer) {
            if (!this->IUploadConstants(Constants_Vertex, this->m_vsConstants, count * 16, this->m_vsSlice)) {
                return false;
            }

            this->m_vsConstantsDirty = false;
        }

        this->IBindConstants(Stage_Vertex, 0, this->m_vsSlice);
    } else {
        FfConstants constants;
        this->IBuildFixedConstants(constants, texTransform);

        if (!this->m_ffConstantsValid || memcmp(&constants, &this->m_ffConstants, sizeof(constants))) {
            if (!this->IUploadConstants(Constants_Fixed, &constants, sizeof(constants), this->m_ffSlice)) {
                return false;
            }

            this->m_ffConstants = constants;
            this->m_ffConstantsValid = true;
        }

        this->IBindConstants(Stage_Vertex, 2, this->m_ffSlice);
    }

    if (psEntry) {
        uint32_t count = psProgram->m_constCount;

        if (this->m_psConstantsDirty || count > this->m_psSlice.m_count || !this->m_psSlice.m_buffer) {
            if (!this->IUploadConstants(Constants_Pixel, this->m_psConstants, count * 16, this->m_psSlice)) {
                return false;
            }

            this->m_psConstantsDirty = false;
        }

        this->IBindConstants(Stage_Pixel, 0, this->m_psSlice);
    }

    DrawConstants drawConstants;
    this->IBuildDrawConstants(drawConstants);

    if (!this->m_drawConstantsValid || memcmp(&drawConstants, &this->m_drawConstants, sizeof(drawConstants))) {
        if (!this->IUploadConstants(Constants_Draw, &drawConstants, sizeof(drawConstants), this->m_drawSlice)) {
            return false;
        }

        this->m_drawConstants = drawConstants;
        this->m_drawConstantsValid = true;
    }

    this->IBindConstants(Stage_Vertex, 1, this->m_drawSlice);
    this->IBindConstants(Stage_Pixel, 1, this->m_drawSlice);

    if (gsProgram) {
        this->IBindConstants(Stage_Geometry, 1, this->m_drawSlice);
    }

    // MODIFIED: modern lighting (D3d11Lighting.h): the lighting constants (b3), the lights and clusters (t16-t17), and
    // a lit pixel shader reads the vertex shader's constants again (b4)
    if (this->m_lighting) {
        if (rebind || !this->m_lightingBound) {
            this->m_lighting->BindForDraw();
            this->m_lightingBound = true;
        }

        if (vsEntry && psEntry && psEntry->m_dx9.m_family == Family_LitPixel) {
            this->IBindConstants(Stage_Pixel, 4, this->m_vsSlice);
        }
    }

    // bind what changed
    if (rebind || this->m_boundVs != vsProgram->m_vs) {
        this->m_ctx->VSSetShader(vsProgram->m_vs, nullptr, 0);
        this->m_boundVs = vsProgram->m_vs;
    }

    ID3D11GeometryShader* gs = gsProgram ? gsProgram->m_gs : nullptr;

    if (rebind || this->m_boundGs != gs) {
        this->m_ctx->GSSetShader(gs, nullptr, 0);
        this->m_boundGs = gs;
    }

    if (rebind || this->m_boundPs != psProgram->m_ps) {
        this->m_ctx->PSSetShader(psProgram->m_ps, nullptr, 0);
        this->m_boundPs = psProgram->m_ps;
    }

    if (rebind || this->m_boundLayout != layout) {
        this->m_ctx->IASetInputLayout(layout);
        this->m_boundLayout = layout;
    }

    for (uint32_t stream = 0; stream < this->m_layout.m_streamCount; stream++) {
        CGxBuf* buf = this->m_layout.m_streams[stream];
        Pool* pool = static_cast<Pool*>(buf->m_pool->m_apiSpecific);

        if (!pool || !pool->m_buffer) {
            return false;
        }

        uint32_t offset = buf->m_index;
        uint32_t stride = this->m_layout.m_strides[stream];

        if (rebind || this->m_boundVb[stream] != pool->m_buffer || this->m_boundVbOffset[stream] != offset || this->m_boundVbStride[stream] != stride) {
            this->m_ctx->IASetVertexBuffers(stream, 1, &pool->m_buffer, &stride, &offset);
            this->m_boundVb[stream] = pool->m_buffer;
            this->m_boundVbOffset[stream] = offset;
            this->m_boundVbStride[stream] = stride;
        }
    }

    if (rebind || this->m_boundBlend != blend || this->m_boundBlendFactor != factor) {
        float factors[4] = { factor, factor, factor, factor };
        this->m_ctx->OMSetBlendState(blend, factors, 0xFFFFFFFF);
        this->m_boundBlend = blend;
        this->m_boundBlendFactor = factor;
    }

    if (rebind || this->m_boundDepth != depth) {
        this->m_ctx->OMSetDepthStencilState(depth, 0);
        this->m_boundDepth = depth;
    }

    if (rebind || this->m_boundRaster != raster) {
        this->m_ctx->RSSetState(raster);
        this->m_boundRaster = raster;
    }

    uint32_t first = 16;
    uint32_t last = 0;

    for (uint32_t slot = 0; slot < 16; slot++) {
        if (rebind || this->m_boundSrv[slot] != srvs[slot] || this->m_boundSampler[slot] != samplers[slot]) {
            first = slot < first ? slot : first;
            last = slot;
        }
    }

    if (first <= last) {
        this->m_ctx->PSSetShaderResources(first, last - first + 1, &srvs[first]);
        this->m_ctx->PSSetSamplers(first, last - first + 1, &samplers[first]);

        for (uint32_t slot = first; slot <= last; slot++) {
            this->m_boundSrv[slot] = srvs[slot];
            this->m_boundSampler[slot] = samplers[slot];
        }
    }

    D3D11_PRIMITIVE_TOPOLOGY topology = s_topologies[primType];

    if (rebind || this->m_boundTopology != topology) {
        this->m_ctx->IASetPrimitiveTopology(topology);
        this->m_boundTopology = topology;
    }

    this->m_pixelFixed = psEntry == nullptr;
    this->m_bindingsValid = true;
    return true;
}

void CGxDeviceD3d11::Draw(CGxBatch* batch, int32_t count) {
    if (!this->m_context || this->m_intF5C || !batch || batch->m_primType >= GxPrims_Last || !batch->m_count) {
        return;
    }

    // MODIFIED: performance diagnostics (FrameProfile.h)
    FrameProfileCount(FRAMEPROFILE_DRAW_CALLS, 1);

    if (!this->IStateSync(batch->m_primType)) {
        return;
    }

    if (count) {
        if (!this->m_primIndexBuf || !this->m_primIndexBuf->m_pool) {
            return;
        }

        Pool* pool = static_cast<Pool*>(this->m_primIndexBuf->m_pool->m_apiSpecific);

        if (!pool || !pool->m_buffer) {
            return;
        }

        if (this->m_boundIb != pool->m_buffer) {
            this->m_ctx->IASetIndexBuffer(pool->m_buffer, DXGI_FORMAT_R16_UINT, 0);
            this->m_boundIb = pool->m_buffer;
        }

        if (batch->m_primType == GxPrim_TriangleFan) {
            // no fans in D3D11, and an indexed fan would need its indices read back: never drawn by the client
            static bool s_logged;

            if (!s_logged) {
                CGxDevice::Log("CGxDeviceD3d11::Draw(): indexed triangle fan skipped");
                s_logged = true;
            }

            return;
        }

        this->m_ctx->DrawIndexed(batch->m_count, batch->m_start + (this->m_primIndexBuf->m_index >> 1), 0);
    } else if (batch->m_primType == GxPrim_TriangleFan) {
        // a fan as a list: 0, i, i + 1
        if (batch->m_count < 3) {
            return;
        }

        uint32_t triangles = batch->m_count - 2;
        uint32_t indexCount = triangles * 3;

        if (!this->m_fanIndices || this->m_fanIndexCount < indexCount) {
            SafeRelease(this->m_fanIndices);
            this->m_fanIndexCount = indexCount < 3 * 1024 ? 3 * 1024 : indexCount;

            std::vector<uint16_t> indices(this->m_fanIndexCount);

            for (uint32_t t = 0; t < this->m_fanIndexCount / 3; t++) {
                indices[t * 3 + 0] = 0;
                indices[t * 3 + 1] = static_cast<uint16_t>(t + 1);
                indices[t * 3 + 2] = static_cast<uint16_t>(t + 2);
            }

            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = this->m_fanIndexCount * sizeof(uint16_t);
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA data = { indices.data(), 0, 0 };

            if (FAILED(this->m_d3dDevice->CreateBuffer(&desc, &data, &this->m_fanIndices))) {
                this->m_fanIndices = nullptr;
                this->m_fanIndexCount = 0;
                return;
            }
        }

        this->m_ctx->IASetIndexBuffer(this->m_fanIndices, DXGI_FORMAT_R16_UINT, 0);
        this->m_boundIb = this->m_fanIndices;
        this->m_ctx->DrawIndexed(indexCount, 0, 0);
    } else {
        this->m_ctx->Draw(batch->m_count, 0);
    }

    this->m_frameResolvedValid = false;
}

void CGxDeviceD3d11::IClearRect(uint32_t mask, CImVector color) {
    // a triangle over the viewport, cut by the scissor when it is on: D3D9's Clear of the viewport
    float constants[16] = {};
    constants[0] = color.r / 255.0f;
    constants[1] = color.g / 255.0f;
    constants[2] = color.b / 255.0f;
    constants[3] = color.a / 255.0f;
    constants[4] = 1.0f;

    if (!this->m_utilityConstants) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 64;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        if (FAILED(this->m_d3dDevice->CreateBuffer(&desc, nullptr, &this->m_utilityConstants))) {
            return;
        }
    }

    this->m_ctx->UpdateSubresource(this->m_utilityConstants, 0, nullptr, constants, 0, 0);

    bool scissor = this->m_appRenderStates[GxRs_ScissorTest].m_value.m_data.i[0] != 0;

    if (scissor) {
        this->m_scissorRectDirty = 1;
        this->IStateSyncScissorRect();
    }

    this->m_ctx->OMSetBlendState(this->IBlendState(GxBlend_Opaque, (mask & 0x1) ? 15 : 0), nullptr, 0xFFFFFFFF);
    this->m_ctx->OMSetDepthStencilState(this->IDepthState(D3D11_COMPARISON_ALWAYS, (mask & 0x2) ? 1 : 0), 0);
    this->m_ctx->RSSetState(this->IRasterState(0, false, scissor, 0, false));

    // D3D9 clears depth to 1.0 whatever the viewport's depth range
    D3D11_VIEWPORT viewport = this->m_boundViewport;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    this->m_ctx->RSSetViewports(1, &viewport);
    this->m_ctx->IASetInputLayout(nullptr);
    this->m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    this->m_ctx->VSSetShader(this->m_clearVs->m_vs, nullptr, 0);
    this->m_ctx->GSSetShader(nullptr, nullptr, 0);
    this->m_ctx->PSSetShader(this->m_clearPs->m_ps, nullptr, 0);
    this->m_ctx->VSSetConstantBuffers(0, 1, &this->m_utilityConstants);
    this->m_ctx->PSSetConstantBuffers(0, 1, &this->m_utilityConstants);
    this->m_ctx->Draw(3, 0);

    this->IInvalidateBindings();
}
