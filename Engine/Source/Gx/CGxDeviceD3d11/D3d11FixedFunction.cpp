#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11FixedFunction.h"
#include <cstdarg>
#include <cstdio>
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Dx9Shader.h"

// MODIFIED: D3D11 device, see D3d11FixedFunction.h

namespace GxD3d11 {

namespace {

const char* const s_ffConstants =
    "cbuffer GxFixed : register(b2) {\n"
    "    row_major float4x4 ffWorldViewProj;\n"
    "    row_major float4x4 ffWorldView;\n"
    "    row_major float4x4 ffWorld;\n"
    "    row_major float4x4 ffNormal;         // the inverse transpose of the world view matrix\n"
    "    row_major float4x4 ffTexture[8];     // D3DTS_TEXTURE#: the texture coordinate as a row vector times it\n"
    "    float4 ffMatDiffuse;\n"
    "    float4 ffMatAmbient;\n"
    "    float4 ffMatSpecular;\n"
    "    float4 ffMatEmissive;\n"
    "    float4 ffAmbient;                    // D3DRS_AMBIENT\n"
    "    float4 ffMatPower;                   // x the specular exponent\n"
    "    float4 ffLightPos[4];                // view space: the position of a point light, the way toward a directional one\n"
    "    float4 ffLightDiffuse[4];\n"
    "    float4 ffLightAmbient[4];\n"
    "    float4 ffLightSpecular[4];\n"
    "    float4 ffLightAtten[4];              // constant, linear, quadratic\n"
    "    float4 ffFog;                        // x start, y end, z 1 / (end - start)\n"
    "};\n"
    "\n";

const char* const s_attribNames[14] = { "POSITION0", "BLENDWEIGHT0", "BLENDINDICES0", "NORMAL0", "COLOR0", "COLOR1", "TEXCOORD0", "TEXCOORD1", "TEXCOORD2", "TEXCOORD3", "TEXCOORD4", "TEXCOORD5", "TEXCOORD6", "TEXCOORD7" };

void Appendf(std::string& out, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    int32_t length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length > 0) {
        out.append(buffer, static_cast<size_t>(length) < sizeof(buffer) ? static_cast<size_t>(length) : sizeof(buffer) - 1);
    }
}

// one texture stage operation, as CGxDeviceD3d9Ex's s_texOp / s_texArgs program it
std::string StageOp(uint32_t op, const char* tex, const char* cur, const char* curAlpha, const char* diffuseAlpha) {
    const char* arg1 = op == 4 ? cur : tex;
    const char* arg2 = op == 4 ? tex : cur;
    char buffer[256];

    switch (op) {
        case 1:
            snprintf(buffer, sizeof(buffer), "2.0 * %s * %s", arg1, arg2);
            break;
        case 2:
            snprintf(buffer, sizeof(buffer), "%s + %s", arg1, arg2);
            break;
        case 3:
            snprintf(buffer, sizeof(buffer), "%s", arg2);
            break;
        case 4:
            snprintf(buffer, sizeof(buffer), "%s * %s + %s * (1.0 - %s)", arg1, curAlpha, arg2, curAlpha);
            break;
        case 5:
            snprintf(buffer, sizeof(buffer), "%s * %s + %s * (1.0 - %s)", arg1, diffuseAlpha, arg2, diffuseAlpha);
            break;
        default:
            snprintf(buffer, sizeof(buffer), "%s * %s", arg1, arg2);
            break;
    }

    return buffer;
}

}

std::string FfVertexHlsl(const FfVertexKey& key) {
    std::string out = s_hlslCommon;
    out += s_ffConstants;

    bool needNormal = key.m_lighting != 0;

    for (uint32_t s = 0; s < 8; s++) {
        if ((key.m_stageMask & (1 << s)) && (key.m_texGen[s] == FfTexGen_Reflection || key.m_texGen[s] == FfTexGen_Normal)) {
            needNormal = true;
        }
    }

    bool hasNormal = (key.m_attribMask & (1 << 3)) != 0;
    bool hasColor0 = (key.m_attribMask & (1 << 4)) != 0;
    bool hasColor1 = (key.m_attribMask & (1 << 5)) != 0;
    uint32_t texRead = 0;

    for (uint32_t s = 0; s < 8; s++) {
        if ((key.m_stageMask & (1 << s)) && key.m_texGen[s] == FfTexGen_None && key.m_texCoord[s] < 8 && (key.m_attribMask & (1 << (6 + key.m_texCoord[s])))) {
            texRead |= 1u << key.m_texCoord[s];
        }
    }

    // the attributes this shader reads, as the input layout hands them over
    uint32_t read = 1u | (needNormal && hasNormal ? 1u << 3 : 0) | (hasColor0 ? 1u << 4 : 0) | (hasColor1 ? 1u << 5 : 0) | (texRead << 6);
    out += "struct FfVsIn {\n";

    for (uint32_t a = 0; a < 14; a++) {
        if (!(read & (1u << a)) || !(key.m_attribMask & (1u << a))) {
            continue;
        }

        uint32_t inputClass = (key.m_attribClass >> (2 * a)) & 3;
        const char* type = inputClass == Input_Uint ? "uint4" : (inputClass == Input_Sint ? "int4" : "float4");
        Appendf(out, "    %s a%u : %s;\n", type, a, s_attribNames[a]);
    }

    out += "};\n\n";
    out += "GxVaryings main(FfVsIn vin) {\n";
    out += "    GxVaryings o = (GxVaryings)0;\n";
    out += "    float4 pos = float4((float3)vin.a0.xyz, 1.0);\n";
    out += "    o.pos = mul(pos, ffWorldViewProj);\n";
    out += "    float4 viewPos = mul(pos, ffWorldView);\n";

    if (needNormal) {
        if (hasNormal) {
            out += "    float3 n = mul((float3)vin.a3.xyz, (float3x3)ffNormal);\n";
        } else {
            out += "    float3 n = float3(0.0, 0.0, 0.0);\n";
        }

        if (key.m_normalize) {
            out += "    n = normalize(n);\n";
        }
    }

    out += hasColor0 ? "    float4 vertexColor0 = (float4)vin.a4;\n" : "    float4 vertexColor0 = float4(1.0, 1.0, 1.0, 1.0);\n";
    out += hasColor1 ? "    float4 vertexColor1 = (float4)vin.a5;\n" : "    float4 vertexColor1 = float4(0.0, 0.0, 0.0, 0.0);\n";

    if (key.m_lighting) {
        // D3D9's vertex lighting, in view space with a local viewer:
        //   colour0 = Me + Ma * (Ga + sum(La * att)) + Md * sum(Ld * max(N.L, 0) * att), alpha Md.a
        //   colour1 = Ms * sum(Ls * max(N.H, 0)^P * att)
        const auto source = [&](uint8_t which, const char* material) -> std::string {
            return which == FfSource_Color0 && hasColor0 ? std::string("vertexColor0") : std::string(material);
        };

        Appendf(out, "    float4 matAmbient = %s;\n", source(key.m_ambientSource, "ffMatAmbient").c_str());
        Appendf(out, "    float4 matDiffuse = %s;\n", source(key.m_diffuseSource, "ffMatDiffuse").c_str());
        Appendf(out, "    float4 matSpecular = %s;\n", source(key.m_specularSource, "ffMatSpecular").c_str());
        Appendf(out, "    float4 matEmissive = %s;\n", source(key.m_emissiveSource, "ffMatEmissive").c_str());
        out += "    float3 ambient = ffAmbient.rgb;\n";
        out += "    float3 diffuse = 0.0;\n";
        out += "    float3 specular = 0.0;\n";
        out += "    float3 toEye = normalize(-viewPos.xyz);\n";

        for (uint32_t k = 0; k < 4; k++) {
            if (key.m_lightTypes[k] == FfLight_Off) {
                continue;
            }

            out += "    {\n";

            if (key.m_lightTypes[k] == FfLight_Point) {
                Appendf(out, "        float3 toLight = ffLightPos[%u].xyz - viewPos.xyz;\n", k);
                out += "        float distance = length(toLight);\n";
                out += "        float3 l = toLight / max(distance, 1e-20);\n";
                Appendf(out, "        float att = 1.0 / max(ffLightAtten[%u].x + ffLightAtten[%u].y * distance + ffLightAtten[%u].z * distance * distance, 1e-20);\n", k, k, k);
            } else {
                Appendf(out, "        float3 l = ffLightPos[%u].xyz;\n", k);
                out += "        float att = 1.0;\n";
            }

            Appendf(out, "        ambient += ffLightAmbient[%u].rgb * att;\n", k);
            Appendf(out, "        diffuse += ffLightDiffuse[%u].rgb * (max(dot(n, l), 0.0) * att);\n", k);

            if (key.m_specular) {
                out += "        float3 h = normalize(l + toEye);\n";
                Appendf(out, "        specular += ffLightSpecular[%u].rgb * (Dx9Pow(max(dot(n, h), 0.0), ffMatPower.x) * att);\n", k);
            }

            out += "    }\n";
        }

        out += "    o.c0 = saturate(float4(matEmissive.rgb + matAmbient.rgb * ambient + matDiffuse.rgb * diffuse, matDiffuse.a));\n";
        out += key.m_specular ? "    o.c1 = saturate(float4(matSpecular.rgb * specular, 0.0));\n" : "    o.c1 = 0.0;\n";
    } else {
        out += "    o.c0 = vertexColor0;\n";
        out += "    o.c1 = vertexColor1;\n";
    }

    // linear vertex fog on the view-space depth
    out += key.m_fog ? "    o.fog = saturate((ffFog.y - abs(viewPos.z)) * ffFog.z);\n" : "    o.fog = 1.0;\n";

    for (uint32_t s = 0; s < 8; s++) {
        if (!(key.m_stageMask & (1 << s))) {
            continue;
        }

        Appendf(out, "    {\n");

        switch (key.m_texGen[s]) {
            case FfTexGen_Position:
                out += "        float4 tc = float4(viewPos.xyz, 1.0);\n";
                break;

            case FfTexGen_Reflection:
                out += "        float4 tc = float4(reflect(normalize(viewPos.xyz), n), 1.0);\n";
                break;

            case FfTexGen_Normal:
                out += "        float4 tc = float4(n, 1.0);\n";
                break;

            default: {
                uint32_t coord = key.m_texCoord[s];

                if (coord < 8 && (key.m_attribMask & (1u << (6 + coord)))) {
                    // a texture coordinate of fewer than four components, for the matrix: (u, v) becomes (u, v, 1, 0),
                    // the third row being the translation (CGxDeviceD3d9Ex::IXformSetTex moves it there)
                    switch (key.m_texCoordSize[coord]) {
                        case 1:
                            Appendf(out, "        float4 tc = float4((float)vin.a%u.x, 1.0, 0.0, 0.0);\n", 6 + coord);
                            break;
                        case 3:
                            Appendf(out, "        float4 tc = float4((float3)vin.a%u.xyz, 1.0);\n", 6 + coord);
                            break;
                        case 4:
                            Appendf(out, "        float4 tc = (float4)vin.a%u;\n", 6 + coord);
                            break;
                        default:
                            Appendf(out, "        float4 tc = float4((float2)vin.a%u.xy, 1.0, 0.0);\n", 6 + coord);
                            break;
                    }
                } else {
                    out += "        float4 tc = float4(0.0, 0.0, 1.0, 0.0);\n";
                }

                break;
            }
        }

        if (key.m_texTransform[s]) {
            Appendf(out, "        tc = mul(tc, ffTexture[%u]);\n", s);
        }

        Appendf(out, "        o.t%u = tc;\n", s);
        out += "    }\n";
    }

    if (key.m_pointScale) {
        out += "    float d = length(viewPos.xyz);\n";
        out += "    o.psize = gxPointSize.x * gxViewport.w * sqrt(1.0 / max(gxPointScale.x + gxPointScale.y * d + gxPointScale.z * d * d, 1e-20));\n";
    } else {
        out += "    o.psize = gxPointSize.x;\n";
    }

    // the fixed function's clip planes are in world space
    out += "    GxFinishVertex(o, mul(pos, ffWorld));\n";
    out += "    return o;\n";
    out += "}\n";
    return out;
}

std::string FfPixelHlsl(const FfPixelKey& key) {
    std::string out = s_hlslCommon;
    uint32_t stages = key.m_stageCount < 8 ? key.m_stageCount : 8;

    for (uint32_t s = 0; s < stages; s++) {
        Appendf(out, "%s T%u : register(t%u);\n", key.m_dim[s] == 3 ? "TextureCube" : "Texture2D", s, s);
        Appendf(out, "%s S%u : register(s%u);\n", key.m_depth[s] ? "SamplerComparisonState" : "SamplerState", s, s);
    }

    out += "\nfloat4 main(GxVaryings i) : SV_Target0 {\n";
    out += "    float4 diffuse = saturate(i.c0);\n";
    out += "    float4 cur = diffuse;\n";

    for (uint32_t s = 0; s < stages; s++) {
        uint32_t coord = key.m_coord[s] < 10 ? key.m_coord[s] : s;
        out += "    {\n";
        Appendf(out, "        float4 tc = i.t%u;\n", coord);

        if (key.m_projected[s]) {
            out += "        tc.xy /= tc.z;\n";
        }

        if (key.m_depth[s]) {
            Appendf(out, "        float4 tex = T%u.SampleCmpLevelZero(S%u, tc.xy, tc.z).xxxx;\n", s, s);
        } else {
            Appendf(out, "        float4 tex = T%u.Sample(S%u, %s);\n", s, s, key.m_dim[s] == 3 ? "tc.xyz" : "tc.xy");
        }

        Appendf(out, "        float3 rgb = %s;\n", StageOp(key.m_colorOp[s], "tex.rgb", "cur.rgb", "cur.a", "diffuse.a").c_str());
        Appendf(out, "        float a = %s;\n", StageOp(key.m_alphaOp[s], "tex.a", "cur.a", "cur.a", "diffuse.a").c_str());
        out += "        cur = saturate(float4(rgb, a));\n";
        out += "    }\n";
    }

    if (key.m_specular) {
        out += "    cur.rgb = saturate(cur.rgb + saturate(i.c1.rgb));\n";
    }

    out += "    cur.rgb = GxFogBlend(cur.rgb, i.fog);\n";
    out += "    GxAlphaTest(cur.a);\n";
    out += "    return cur;\n";
    out += "}\n";
    return out;
}

std::string PointSpriteHlsl() {
    std::string out = s_hlslCommon;
    out +=
        "// D3D9 draws a point as a square of its size in pixels (clamped to D3DRS_POINTSIZE_MIN / MAX); with\n"
        "// D3DRS_POINTSPRITEENABLE every texture coordinate runs (0, 0) to (1, 1) across it, top left to bottom right\n"
        "[maxvertexcount(4)]\n"
        "void main(point GxVaryings p[1], inout TriangleStream<GxVaryings> stream) {\n"
        "    GxVaryings v = p[0];\n"
        "    float size = clamp(v.psize, gxPointSize.y, gxPointSize.z);\n"
        "    float2 extent = size * gxViewport.xy * v.pos.w;\n"
        "    const float2 corners[4] = { float2(-1.0, 1.0), float2(1.0, 1.0), float2(-1.0, -1.0), float2(1.0, -1.0) };\n"
        "    const float2 uvs[4] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0), float2(1.0, 1.0) };\n"
        "    [unroll] for (uint k = 0; k < 4; k++) {\n"
        "        GxVaryings o = v;\n"
        "        o.pos.xy = v.pos.xy + corners[k] * extent;\n"
        "        if (gxPointSize.w > 0.0) {\n"
        "            float4 uv = float4(uvs[k], 0.0, 1.0);\n"
        "            o.t0 = uv; o.t1 = uv; o.t2 = uv; o.t3 = uv; o.t4 = uv;\n"
        "            o.t5 = uv; o.t6 = uv; o.t7 = uv; o.t8 = uv; o.t9 = uv;\n"
        "        }\n"
        "        stream.Append(o);\n"
        "    }\n"
        "}\n";
    return out;
}

std::string PresentHlsl() {
    return
        "// The frame to the swap chain. A full-screen window gets the gamma ramp D3D9 gave the display.\n"
        "Texture2D gxFrame : register(t0);\n"
        "Texture1D gxGamma : register(t1);\n"
        "SamplerState gxLinear : register(s0);\n"
        "cbuffer GxPresent : register(b0) {\n"
        "    float4 gxPresent;   // x 1 = the gamma ramp on\n"
        "};\n"
        "struct PresentVaryings {\n"
        "    float4 pos : SV_Position;\n"
        "};\n"
        "PresentVaryings PresentVs(uint id : SV_VertexID) {\n"
        "    PresentVaryings o;\n"
        "    float2 uv = float2((id << 1) & 2, id & 2);\n"
        "    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
        "    return o;\n"
        "}\n"
        "float4 PresentPs(PresentVaryings i) : SV_Target0 {\n"
        "    float4 c = gxFrame.Load(int3(i.pos.xy, 0));\n"
        "    if (gxPresent.x > 0.0) {\n"
        "        float3 at = saturate(c.rgb) * (255.0 / 256.0) + (0.5 / 256.0);\n"
        "        c.r = gxGamma.SampleLevel(gxLinear, at.r, 0).r;\n"
        "        c.g = gxGamma.SampleLevel(gxLinear, at.g, 0).g;\n"
        "        c.b = gxGamma.SampleLevel(gxLinear, at.b, 0).b;\n"
        "    }\n"
        "    return float4(c.rgb, 1.0);\n"
        "}\n";
}

std::string BlitHlsl() {
    return
        "// A rectangle of one texture into a render target (StretchRect, and copies between formats).\n"
        "Texture2D gxSource : register(t0);\n"
        "SamplerState gxSampler : register(s0);\n"
        "cbuffer GxBlit : register(b0) {\n"
        "    float4 gxBlitDest;     // the rectangle in clip space: x0, y0, x1, y1\n"
        "    float4 gxBlitSource;   // the rectangle in texture space: u0, v0, u1, v1\n"
        "};\n"
        "struct BlitVaryings {\n"
        "    float4 pos : SV_Position;\n"
        "    float2 uv : TEXCOORD0;\n"
        "};\n"
        "BlitVaryings BlitVs(uint id : SV_VertexID) {\n"
        "    BlitVaryings o;\n"
        "    float2 k = float2(id & 1, (id >> 1) & 1);\n"
        "    o.pos = float4(lerp(gxBlitDest.xy, gxBlitDest.zw, k), 0.0, 1.0);\n"
        "    o.uv = lerp(gxBlitSource.xy, gxBlitSource.zw, k);\n"
        "    return o;\n"
        "}\n"
        "float4 BlitPs(BlitVaryings i) : SV_Target0 {\n"
        "    return gxSource.SampleLevel(gxSampler, i.uv, 0);\n"
        "}\n";
}

std::string ClearHlsl() {
    return
        "// A clear of the viewport's rectangle (and the scissor's): a triangle over the viewport at the clear depth.\n"
        "cbuffer GxClear : register(b0) {\n"
        "    float4 gxClearColor;\n"
        "    float4 gxClearDepth;   // x the depth\n"
        "};\n"
        "float4 ClearVs(uint id : SV_VertexID) : SV_Position {\n"
        "    float2 uv = float2((id << 1) & 2, id & 2);\n"
        "    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), gxClearDepth.x, 1.0);\n"
        "}\n"
        "float4 ClearPs(float4 pos : SV_Position) : SV_Target0 {\n"
        "    return gxClearColor;\n"
        "}\n";
}

}
