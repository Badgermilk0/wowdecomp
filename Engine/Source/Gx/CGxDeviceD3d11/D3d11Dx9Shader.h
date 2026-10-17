#ifndef ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11DX9SHADER_H
#define ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11DX9SHADER_H

// MODIFIED: D3D11 device. The client's shaders are DX9 token streams (CGxShader::m_code: vs_2_0 / vs_3_0, ps_2_0 /
// ps_3_0 out of the .bls containers, or what d3dcompiler made of awesome's HLSL). D3D11 runs none of them, so each is
// decoded here and written out again as shader model 5 HLSL, which CGxDeviceD3d11 compiles with D3DCompile.
//
// Every generated shader shares one interface (s_hlslCommon): the varyings are matched by semantic into one struct
// (GxVaryings), so any vertex shader, translated or fixed-function, pairs with any pixel shader. The constant registers
// are a cbuffer of float4 C[256] at b0, the device's per-draw state (alpha test, fog, clip planes, the half-pixel
// offset that reproduces D3D9's rasterisation, point sizes) a cbuffer at b1.
//
// Shader model 1 streams are refused: they carry no instruction lengths and only Terrain1w (ps_1_x) is one.

#include <cstdint>
#include <string>
#include <vector>

namespace GxD3d11 {

// the varyings of GxVaryings, by D3D9 semantic
enum EVarying : uint32_t {
    Var_Color0 = 0,
    Var_Color1 = 1,
    Var_Tex0 = 2,
    Var_TexLast = 11,
    Var_Normal0 = 12,
    Var_Tangent0 = 13,
    Var_Binormal0 = 14,
    Var_Fog = 15,
    Var_PSize = 16,
    Vars_Last = 17,
};

// how the input layout hands a vertex attribute to the shader; 2 bits per GxVA in a vertex shader variant
enum EInputClass : uint32_t {
    Input_Float = 0,
    Input_Uint = 1,
    Input_Sint = 2,
    Input_Missing = 3,
};

// the declarations and cbuffers every generated shader starts with
extern const char* const s_hlslCommon;

// D3D9 names a varying by usage and usage index; -1 when there is no slot for it
int32_t VaryingForUsage(uint32_t usage, uint32_t index);
// the HLSL member and semantic of a varying slot
const char* VaryingMember(uint32_t varying);

struct Dx9Operand {
    uint32_t m_type = 0;
    uint32_t m_index = 0;
    bool m_rel = false;
    uint32_t m_relType = 0;
    uint32_t m_relIndex = 0;
    uint32_t m_relComp = 0;
    // destination
    uint32_t m_mask = 0xF;
    bool m_sat = false;
    bool m_centroid = false;
    int32_t m_shift = 0;
    // source
    uint8_t m_swizzle[4] = { 0, 1, 2, 3 };
    uint32_t m_mod = 0;
};

struct Dx9Inst {
    uint32_t m_op = 0;
    uint32_t m_ctrl = 0;
    bool m_predicated = false;
    bool m_hasDst = false;
    Dx9Operand m_dst;
    Dx9Operand m_pred;
    Dx9Operand m_src[4];
    uint32_t m_srcCount = 0;
    uint32_t m_dclUsage = 0;
    uint32_t m_dclIndex = 0;
    uint32_t m_dclTexType = 0;
    uint32_t m_values[4] = {};
};

class Dx9Shader {
    public:
    // a vertex input register and the GxVA it reads
    struct Input {
        uint32_t m_reg;
        uint32_t m_attrib;
    };

    // a vs_3_0 output declaration or a ps_3_0 / ps_2_0 input declaration
    struct Link {
        uint32_t m_reg;
        int32_t m_varying;    // EVarying, or -1 for position (vs_3_0)
        uint32_t m_mask;
        bool m_texture;       // ps_2_0: t# rather than v#
    };

    bool m_vertex = false;
    uint32_t m_major = 0;
    uint32_t m_minor = 0;
    // vertex: GxVA bits read; varyings written. pixel: varyings read
    uint32_t m_inputMask = 0;
    uint32_t m_outputMask = 0;
    uint32_t m_readMask = 0;
    // pixel: samplers declared, and their D3DSAMPLER_TEXTURE_TYPE (2 2d, 3 cube, 4 volume)
    uint32_t m_samplerMask = 0;
    uint8_t m_samplerDim[16] = {};
    // constant registers the shader reads (256 with relative addressing)
    uint32_t m_constCount = 0;
    // pixel: writes oDepth, colour targets written, reads vPos / vFace
    bool m_writesDepth = false;
    uint32_t m_colorOutMask = 0;
    bool m_usesPos = false;
    bool m_usesFace = false;
    std::string m_error;

    // false (and m_error) when the stream is not one this translator writes out
    bool Decode(const void* code, uint32_t size);
    // vertex: variant = 2 bits of EInputClass per GxVA. pixel: variant = sampler slots bound to depth textures, which
    // D3D9 compares against the coordinate's z (the hardware shadow map) and D3D11 samples with SampleCmp
    bool Emit(uint32_t variant, std::string& hlsl, std::string& error) const;

    // the decoded program, for the writer
    std::vector<Dx9Inst> m_insts;
    std::vector<Input> m_inputs;
    std::vector<Link> m_links;
    uint32_t m_tempMask[2] = {};          // r0..r63
    uint32_t m_outRegMask = 0;            // vs: o# / oT# written; ps: oC# written
    bool m_usesA0 = false;
    bool m_usesP0 = false;
    bool m_usesFogOut = false;           // vs_2_0 oFog
    bool m_usesPtsOut = false;           // vs_2_0 oPts
    bool m_usesPosOut = false;           // vs_2_0 oPos
    uint32_t m_colorRegMask = 0;          // vs_2_0 oD#
    uint32_t m_texRegMask = 0;            // vs_2_0 oT#, ps_2_0 t#
    uint32_t m_inputRegMask = 0;          // v# declared

    private:
    bool Analyze();
};

}

#endif
