#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <cmath>
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Frame/CSimpleFrame.h"
#include "Engine/Source/Frame/CSimpleRender.h"
#include "Engine/Source/Frame/CSimpleTexture.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Services/Camera.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/CRect.h"

// UI rendering fixes (awesome_wotlk BugFixes.cpp), all on by default as in the original:
// uiHalfPixelFix: CameraSetupScreenProjection's D3D9 half-pixel offset moves the UI by half a pixel, not by a
//   quarter of the rect's DDC size in pixels (which blurred every 1:1 texture)
// uiTextureSampling: 0 = engine (one bilinear tap), 1 = box, 2 = tent. UI textures have no mips, so a minified quad
//   took one tap per pixel and thin lines in the art came and went with the sub-pixel phase; the UI pixel shaders
//   are replaced by ones that average the pixel's whole footprint
// uiPixelSnap: the UI vertex shader rounds quad edges to pixel edges, and quads between 0.25 and 1 pixel thick are
//   widened to a full pixel so they do not vanish
//
// Beyond awesome, for high resolutions:
// uiUpscale: 0 = engine, 1 = bicubic, 2 = edge-adaptive. FrameXML lays the UI out on a 768-unit-high screen, so at
//   2160p every stock texture is drawn about 2.8 times its size (one bilinear tap, or point-sampled for small
//   unmipped art) and the whole UI looks soft. A magnified texture is reconstructed from its 4x4 texel neighbourhood
//   instead: Catmull-Rom, or an edge-directed Lanczos after AMD FSR 1's EASU that follows the art's edges. Colour is
//   filtered premultiplied by alpha for the blends that weight it by alpha, so transparent texels add no dark fringe;
//   the result is held to the range of the four nearest texels (no ringing, no new colours, and nothing from beyond
//   the bilinear reach); taps stay inside the texture's cell of an atlas
// uiUpscaleEdges: 0 to 1. Where a magnified texture's alpha steps from transparent to opaque within a texel (button
//   rims, rings, borders, icon masks), the alpha is treated as a distance field: the edge is redrawn one screen pixel
//   wide at the 50% contour instead of spread over the texel's 2.8 pixels. Soft alpha (glows, shadows) is left alone

static void OnHalfPixelFix(AwesomeCVar* cvar, bool changed);
static void OnTextureSampling(AwesomeCVar* cvar, bool changed);
static void OnPixelSnap(AwesomeCVar* cvar, bool changed);

enum {
    SAMPLING_ENGINE = 0,
    SAMPLING_BOX = 1,
    SAMPLING_TENT = 2,
};

enum {
    UPSCALE_ENGINE = 0,
    UPSCALE_BICUBIC = 1,
    UPSCALE_EDGE = 2,
};

static AwesomeCVar s_cvHalfPixelFix = AwesomeCVar::Int("uiHalfPixelFix", 1, 0, 1, OnHalfPixelFix);
static AwesomeCVar s_cvTextureSampling = AwesomeCVar::Int("uiTextureSampling", SAMPLING_BOX, SAMPLING_ENGINE, SAMPLING_TENT, OnTextureSampling);
static AwesomeCVar s_cvPixelSnap = AwesomeCVar::Int("uiPixelSnap", 1, 0, 1, OnPixelSnap);
static AwesomeCVar s_cvUpscale = AwesomeCVar::Int("uiUpscale", UPSCALE_EDGE, UPSCALE_ENGINE, UPSCALE_EDGE, OnTextureSampling);
static AwesomeCVar s_cvUpscaleEdges = AwesomeCVar::Float("uiUpscaleEdges", 1.0f, 0.0f, 1.0f);

// pixel c222: the stage-0 texture's width, height, 1/width, 1/height (c220/c221 belong to MSDF)
static const uint32_t SAMPLING_CONSTANT = 222;
// pixel c223: atlas cells per axis (0: not an atlas), uiUpscaleEdges, 1 when the blend weights colour by alpha
static const uint32_t UPSCALE_CONSTANT = 223;
// the atlas's cells per axis (CTextureAtlas, TextureGetAtlasOffsetAndScale)
static const float ATLAS_CELLS = 8.0f;
// vertex c222: the viewport's width, height, 1/width, 1/height
static const uint32_t SNAP_CONSTANT = 222;
// a quad thinner than this many pixels is left alone
static const float MIN_SNAP_PIXELS = 0.25f;

static const char* s_vertexHlsl = R"(
    float4 transform[4] : register(c0);
    float4 viewport : register(c222);    // D3D viewport width, height, 1/width, 1/height; 0 skips the snap

    struct Output {
        float4 position : POSITION;
        float4 color : COLOR;
        float2 texcoord : TEXCOORD;
    };

    float4 project(float4 position) {
        return float4(dot(transform[0], position), dot(transform[1], position), dot(transform[2], position),
            dot(transform[3], position));
    }

    // d3d9 puts pixel centers on integer window coordinates, so pixel edges sit at k - 0.5
    float4 snap(float4 position) {
        [branch] if (viewport.x <= 0.0) { return position; }
        float2 window = float2(1.0 + position.x / position.w, 1.0 - position.y / position.w) * 0.5 * viewport.xy;
        window = floor(window + 1.0) - 0.5;
        float2 ndc = float2(window.x * viewport.z * 2.0 - 1.0, 1.0 - window.y * viewport.w * 2.0);
        return float4(ndc * position.w, position.zw);
    }

    Output mainSnap(float4 position : POSITION, float4 color : COLOR, float4 texcoord : TEXCOORD) {
        Output o;
        o.position = snap(project(position));
        o.color = color;
        o.texcoord = texcoord.xy;
        return o;
    }

    Output mainSnapStereo(float4 position : POSITION, float4 color : COLOR, float4 texcoord : TEXCOORD) {
        float4 r = project(position);
        Output o;
        o.position = snap(float4(r.xy * r.w, r.z, r.w));
        o.color = color;
        o.texcoord = texcoord.xy;
        return o;
    }
)";

static const char* s_pixelHlsl = R"(
    sampler2D uiTexture : register(s0);
    float4 texelSize : register(c222);  // width, height, 1/width, 1/height of the stage-0 texture; 0 if unknown
    float4 upscaleParams : register(c223);  // x: atlas cells per axis (0: none), y: edge strength, z: 1 = premultiply

    static const bool kFootprint = FOOTPRINT;     // uiTextureSampling 1 or 2; one compile per combination
    static const bool kTent = TENT;               // uiTextureSampling 2
    static const int kUpscale = UPSCALE;          // uiUpscale: 0 engine, 1 bicubic, 2 edge-adaptive

    static const float kMinTexelsPerPixel = 2.0;  // per axis; a 64px icon at ~35px (1.8:1) keeps the sharper single tap
    static const float kMaxTaps = 16.0;           // loop bound, per texture axis
    static const float kBoxBudget = 8.0;          // taps per axis the box footprint may use
    static const float kTentBudget = 16.0;        // the tent spans twice as far; 12 per axis still drops lines
    static const float kMagnified = 0.98;         // texels per pixel below which a texture is upscaled
    // the cubic the alpha edges are fitted with: smooth enough to straighten the stairs of 1-bit alpha, sharp enough
    // that a lone opaque texel (peak k(0)^2 = 0.64) and a 1-texel diagonal line stay above the 50% contour; the
    // B-spline (1, 0) erases both
    static const float kEdgeB = 0.6;
    static const float kEdgeC = 0.2;

    // ---- magnification ------------------------------------------------------------------------------------------

    // the texel whose centre is at centre (texel units), kept inside [lo, hi] (the atlas cell); premultiplied when
    // the blend weights the colour by alpha
    float4 texel(float2 centre, float2 lo, float2 hi) {
        float4 s = tex2Dlod(uiTexture, float4(clamp(centre, lo, hi) * texelSize.zw, 0.0, 0.0));
        return upscaleParams.z > 0.5 ? float4(s.rgb * s.a, s.a) : s;
    }

    // the weights of the taps at -1, 0, 1, 2 for a sample at t in [0, 1), and their derivatives in t, of the cubic
    // with Mitchell-Netravali parameters (b, c): (0, 0.5) Catmull-Rom, (1, 0) the B-spline
    float4 cubicWeights(float t, float b, float c, out float4 slope) {
        float4 x = float4(1.0 + t, t, 1.0 - t, 2.0 - t);
        float4 x2 = x * x;
        float4 near = ((12.0 - 9.0 * b - 6.0 * c) * x2 * x + (-18.0 + 12.0 * b + 6.0 * c) * x2 + (6.0 - 2.0 * b)) / 6.0;
        float4 far = ((-b - 6.0 * c) * x2 * x + (6.0 * b + 30.0 * c) * x2 + (-12.0 * b - 48.0 * c) * x + (8.0 * b + 24.0 * c)) / 6.0;
        float4 nearSlope = (3.0 * (12.0 - 9.0 * b - 6.0 * c) * x2 + 2.0 * (-18.0 + 12.0 * b + 6.0 * c) * x) / 6.0;
        float4 farSlope = (3.0 * (-b - 6.0 * c) * x2 + 2.0 * (6.0 * b + 30.0 * c) * x + (-12.0 * b - 48.0 * c)) / 6.0;
        // the outer taps are 1 to 2 texels away; d/dt of the distance is +1 for the taps left of t, -1 right of it
        slope = float4(farSlope.x, nearSlope.y, -nearSlope.z, -farSlope.w);
        return float4(far.x, near.y, near.z, far.w);
    }

    // EASU (AMD FidelityFX Super Resolution 1, MIT): the edge direction and how clearly the neighbourhood is an edge,
    // from the luma cross around one of the four bilinear texels, weighted by its bilinear weight w
    float easuLuma(float4 c) {
        return c.r * 0.5 + c.g + c.b * 0.5 + c.a;
    }

    void easuSet(inout float2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
        float dirX = lD - lB;
        float lenX = saturate(abs(dirX) / max(max(abs(lD - lC), abs(lC - lB)), 1e-5));
        dir.x += dirX * w;
        len += lenX * lenX * w;
        float dirY = lE - lA;
        float lenY = saturate(abs(dirY) / max(max(abs(lE - lC), abs(lC - lA)), 1e-5));
        dir.y += dirY * w;
        len += lenY * lenY * w;
    }

    // one tap of the windowed Lanczos-2 approximation, stretched along the edge and squeezed across it
    void easuTap(inout float4 sum, inout float weightSum, float2 offset, float2 dir, float2 len, float lob, float clp, float4 c) {
        float2 v = float2(dot(offset, dir), dot(offset, float2(-dir.y, dir.x))) * len;
        float d2 = min(dot(v, v), clp);
        float wB = 0.4 * d2 - 1.0;
        float wA = lob * d2 - 1.0;
        wB *= wB;
        wA *= wA;
        wB = 1.5625 * wB - 0.5625;
        float w = wB * wA;
        sum += c * w;
        weightSum += w;
    }

    // duT, dvT: the pixel's screen axes in texels
    float4 upscale(float2 uv, float2 duT, float2 dvT) {
        float4 size = texelSize;
        float2 lo = -1e6;
        float2 hi = 1e6;

        [branch] if (upscaleParams.x > 0.0) {
            float2 cell = size.xy / upscaleParams.x;
            lo = floor(uv * upscaleParams.x) * cell + 0.5;
            hi = lo + cell - 1.0;
        }

        // the 4x4 neighbourhood, by texel; f g j k are the bilinear four
        //   a b c d
        //   e f g h
        //   i j k l
        //   m n o q
        float2 p = uv * size.xy - 0.5;
        float2 c = floor(p) + 0.5;
        float2 pp = p - floor(p);
        float4 a = texel(c + float2(-1.0, -1.0), lo, hi);
        float4 b = texel(c + float2(0.0, -1.0), lo, hi);
        float4 cc = texel(c + float2(1.0, -1.0), lo, hi);
        float4 d = texel(c + float2(2.0, -1.0), lo, hi);
        float4 e = texel(c + float2(-1.0, 0.0), lo, hi);
        float4 f = texel(c, lo, hi);
        float4 g = texel(c + float2(1.0, 0.0), lo, hi);
        float4 h = texel(c + float2(2.0, 0.0), lo, hi);
        float4 i = texel(c + float2(-1.0, 1.0), lo, hi);
        float4 j = texel(c + float2(0.0, 1.0), lo, hi);
        float4 k = texel(c + float2(1.0, 1.0), lo, hi);
        float4 l = texel(c + float2(2.0, 1.0), lo, hi);
        float4 m = texel(c + float2(-1.0, 2.0), lo, hi);
        float4 n = texel(c + float2(0.0, 2.0), lo, hi);
        float4 o = texel(c + float2(1.0, 2.0), lo, hi);
        float4 q = texel(c + float2(2.0, 2.0), lo, hi);
        float4 lowest = min(min(f, g), min(j, k));
        float4 highest = max(max(f, g), max(j, k));
        float4 r;

        if (kUpscale == 1) {
            float4 unused;
            float4 wx = cubicWeights(pp.x, 0.0, 0.5, unused);
            float4 wy = cubicWeights(pp.y, 0.0, 0.5, unused);
            r = (a * wx.x + b * wx.y + cc * wx.z + d * wx.w) * wy.x + (e * wx.x + f * wx.y + g * wx.z + h * wx.w) * wy.y +
                (i * wx.x + j * wx.y + k * wx.z + l * wx.w) * wy.z + (m * wx.x + n * wx.y + o * wx.z + q * wx.w) * wy.w;
        } else {
            float bL = easuLuma(b);
            float cL = easuLuma(cc);
            float eL = easuLuma(e);
            float fL = easuLuma(f);
            float gL = easuLuma(g);
            float hL = easuLuma(h);
            float iL = easuLuma(i);
            float jL = easuLuma(j);
            float kL = easuLuma(k);
            float lL = easuLuma(l);
            float nL = easuLuma(n);
            float oL = easuLuma(o);

            float2 dir = 0.0;
            float len = 0.0;
            easuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
            easuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
            easuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
            easuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);

            float dirR = dot(dir, dir);
            dir = dirR < 1.0 / 32768.0 ? float2(1.0, 0.0) : dir * rsqrt(dirR);
            len = len * 0.5;
            len *= len;
            float stretch = 1.0 / max(abs(dir.x), abs(dir.y));
            float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
            float lob = 0.5 - 0.29 * len;
            float clp = 1.0 / lob;

            float4 sum = 0.0;
            float weightSum = 0.0;
            easuTap(sum, weightSum, float2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
            easuTap(sum, weightSum, float2(1.0, -1.0) - pp, dir, len2, lob, clp, cc);
            easuTap(sum, weightSum, float2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
            easuTap(sum, weightSum, float2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
            easuTap(sum, weightSum, float2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
            easuTap(sum, weightSum, float2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
            easuTap(sum, weightSum, float2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
            easuTap(sum, weightSum, float2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
            easuTap(sum, weightSum, float2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
            easuTap(sum, weightSum, float2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
            easuTap(sum, weightSum, float2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
            easuTap(sum, weightSum, float2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
            r = sum / max(weightSum, 1e-5);
        }

        r = clamp(r, lowest, highest);

        if (upscaleParams.z <= 0.5) {
            return r;
        }

        // back to straight colour; where r is (nearly) transparent the colour comes from the four nearest texels,
        // which an edge below may make visible
        float4 nearSum = f + g + j + k;
        float3 rgb = (r.rgb + nearSum.rgb * (1.0 / 256.0)) / max(r.a + nearSum.a * (1.0 / 256.0), 1e-6);
        float alpha = r.a;

        // alpha edges: where the alpha steps from transparent to opaque within one texel, a smooth cubic fit of the
        // 4x4 alpha is read as a distance field and its 50% contour drawn one pixel wide. The smooth fit keeps a
        // straight edge where it is and turns the stairs of 1-bit alpha into the diagonal they stand for
        float strength = upscaleParams.y * saturate((highest.a - lowest.a - 0.5) * 2.5);

        [branch] if (strength > 0.0) {
            float4 sx;
            float4 sy;
            float4 wx = cubicWeights(pp.x, kEdgeB, kEdgeC, sx);
            float4 wy = cubicWeights(pp.y, kEdgeB, kEdgeC, sy);
            float4x4 rows = float4x4(float4(a.a, b.a, cc.a, d.a), float4(e.a, f.a, g.a, h.a), float4(i.a, j.a, k.a, l.a), float4(m.a, n.a, o.a, q.a));
            float4 across = mul(rows, wx);
            float field = dot(wy, across);
            float2 grad = float2(dot(wy, mul(rows, sx)), dot(sy, across));
            float gradLength = length(grad);

            [branch] if (gradLength > 0.1) {
                float2 normal = grad / gradLength;
                float dist = (field - 0.5) / gradLength;                               // texels from the contour
                float width = length(float2(dot(normal, duT), dot(normal, dvT)));    // texels per pixel across it
                float sharp = saturate(dist / max(width, 1e-3) + 0.5);
                alpha = lerp(alpha, sharp, strength * saturate((1.0 - width) * 8.0));
            }
        }

        return float4(saturate(rgb), alpha);
    }

    // ---- minification (awesome) ---------------------------------------------------------------------------------

    // extent: the pixel footprint's bounding box, texels
    float4 sampleFootprint(float2 uv, float2 extent) {
        float4 size = texelSize;
        float2 half_span = kTent ? extent : extent * 0.5;
        float2 center = uv * size.xy;
        float2 first = ceil(center - half_span - 0.5);
        float2 last = floor(center + half_span - 0.5);
        float2 count = extent > kMinTexelsPerPixel ? max(last - first + 1.0, 0.0) : 0.0;
        float2 stride = max(ceil(count / (kTent ? kTentBudget : kBoxBudget)), 1.0);
        float nx = count.x >= 1.0 ? ceil(count.x / stride.x) : 1.0;
        float ny = count.y >= 1.0 ? ceil(count.y / stride.y) : 1.0;

        float3 weighted = 0.0;
        float3 plain = 0.0;
        float weight_sum = 0.0;
        float alpha_sum = 0.0;
        float alpha_max = 0.0;
        [loop] for (float i = 0.0; i < kMaxTaps; i += 1.0) {
            if (i >= nx) { break; }
            float tx = count.x >= 1.0 ? first.x + i * stride.x + 0.5 : center.x;
            float wx = kTent && count.x >= 1.0 ? saturate(2.0 - 2.0 * abs(tx - center.x) / extent.x) : 1.0;
            [loop] for (float j = 0.0; j < kMaxTaps; j += 1.0) {
                if (j >= ny) { break; }
                float ty = count.y >= 1.0 ? first.y + j * stride.y + 0.5 : center.y;
                float wy = kTent && count.y >= 1.0 ? saturate(2.0 - 2.0 * abs(ty - center.y) / extent.y) : 1.0;
                float w = wx * wy;
                float4 s = tex2Dlod(uiTexture, float4(float2(tx, ty) * size.zw, 0.0, 0.0));
                weighted += s.rgb * s.a * w;
                plain += s.rgb * w;
                weight_sum += w;
                alpha_sum += s.a * w;
                alpha_max = max(alpha_max, s.a * w);
            }
        }
        float3 rgb = alpha_sum > 1e-5 ? weighted / alpha_sum : plain / max(weight_sum, 1e-5);
        return float4(rgb, alpha_max);
    }

    float4 sampleUi(float2 uv) {
        float2 du = ddx(uv);
        float2 dv = ddy(uv);
        float4 size = texelSize;

        [branch] if (size.x > 0.0) {
            float2 duT = du * size.xy;
            float2 dvT = dv * size.xy;

            [branch] if (kUpscale != 0 && max(length(duT), length(dvT)) < kMagnified) {
                return upscale(uv, duT, dvT);
            }

            float2 extent = abs(duT) + abs(dvT);

            [branch] if (kFootprint && max(extent.x, extent.y) > kMinTexelsPerPixel) {
                return sampleFootprint(uv, extent);
            }
        }

        return tex2Dgrad(uiTexture, uv, du, dv);
    }

    float4 mainNormal(float4 color : COLOR0, float2 uv : TEXCOORD0) : COLOR {
        return sampleUi(uv) * color;
    }

    float4 mainDesaturate(float4 color : COLOR0, float2 uv : TEXCOORD0) : COLOR {
        float4 s = sampleUi(uv);
        float gray = dot(s.rgb, float3(0.299, 0.587, 0.114));
        return float4(gray, gray, gray, s.a * color.a);
    }
)";

// both the footprint and the upscale off: the stock UI shaders
static bool SamplingOff() {
    return s_cvTextureSampling.m_intValue == SAMPLING_ENGINE && s_cvUpscale.m_intValue == UPSCALE_ENGINE;
}

static bool SnapOff() {
    return !s_cvPixelSnap.m_intValue;
}

static const AwesomeShaderDefine* SamplingDefines() {
    static const char* const s_digits[] = { "0", "1", "2" };
    static AwesomeShaderDefine s_defines[] = { { "FOOTPRINT", "0" }, { "TENT", "0" }, { "UPSCALE", "0" }, { nullptr, nullptr } };
    s_defines[0].m_value = s_digits[s_cvTextureSampling.m_intValue != SAMPLING_ENGINE];
    s_defines[1].m_value = s_digits[s_cvTextureSampling.m_intValue == SAMPLING_TENT];
    s_defines[2].m_value = s_digits[s_cvUpscale.m_intValue];
    return s_defines;
}

static const char* const s_normalEntries[] = { "mainNormal" };
static const char* const s_desaturateEntries[] = { "mainDesaturate" };
static const char* const s_snapEntries[] = { "mainSnap", "mainSnapStereo" };

// CSimpleTexture::s_imageModePixelShaderNames is { "UI", "Desaturate" } in the stock client
static const AwesomeShaderSource s_samplingShaders[2] = {
    { GxSh_Pixel, "AwesomeWotlk_UISampling", s_pixelHlsl, s_normalEntries, 1, "Shaders\\Pixel", "UI", SamplingOff, SamplingDefines },
    { GxSh_Pixel, "AwesomeWotlk_UISamplingDesaturate", s_pixelHlsl, s_desaturateEntries, 1, "Shaders\\Pixel", "Desaturate", SamplingOff, SamplingDefines },
};

static const AwesomeShaderSource s_snapShader = {
    GxSh_Vertex, "AwesomeWotlk_UISnap", s_vertexHlsl, s_snapEntries, 2, "Shaders\\Vertex", "UI", SnapOff, nullptr,
};

// the D3D viewport in pixels, rounded as CGxDeviceD3d sets it
static void ViewportPixels(CGxDevice* device, float& width, float& height) {
    double windowWidth = device->m_curWindowRect.maxX;
    double windowHeight = device->m_curWindowRect.maxY;
    long x = lround(device->m_viewport.m_x.m_l * windowWidth);
    long y = lround((1.0 - device->m_viewport.m_y.m_h) * windowHeight);
    width = static_cast<float>(lround(device->m_viewport.m_x.m_h * windowWidth - static_cast<double>(x)));
    height = static_cast<float>(lround((1.0 - device->m_viewport.m_y.m_l) * windowHeight - static_cast<double>(y)));
}

// the blends that multiply the source colour by its alpha, so a transparent texel's colour never shows
static bool WeightsByAlpha(int32_t alphaMode) {
    return alphaMode == GxBlend_AlphaKey || alphaMode == GxBlend_Alpha || alphaMode == GxBlend_Add;
}

static void OnBatchTexture(CGxTex* texture, int32_t alphaMode) {
    CGxDevice* device = g_theGxDevicePtr;

    if (!device) {
        return;
    }

    if (!SamplingOff()) {
        float width = texture ? static_cast<float>(texture->m_width) : 0.0f;
        float height = texture ? static_cast<float>(texture->m_height) : 0.0f;
        float constants[4] = { width, height, width > 0.0f ? 1.0f / width : 0.0f, height > 0.0f ? 1.0f / height : 0.0f };
        device->ShaderConstantsSet(GxSh_Pixel, SAMPLING_CONSTANT, constants, 1);
    }

    if (s_cvUpscale.m_intValue != UPSCALE_ENGINE) {
        float constants[4] = {
            TextureGxTexIsAtlas(texture) ? ATLAS_CELLS : 0.0f,
            s_cvUpscaleEdges.m_floatValue,
            WeightsByAlpha(alphaMode) ? 1.0f : 0.0f,
            0.0f,
        };
        device->ShaderConstantsSet(GxSh_Pixel, UPSCALE_CONSTANT, constants, 1);
    }

    if (!SnapOff()) {
        float width;
        float height;
        ViewportPixels(device, width, height);
        float constants[4] = { width, height, width > 0.0f ? 1.0f / width : 0.0f, height > 0.0f ? 1.0f / height : 0.0f };
        device->ShaderConstantsSet(GxSh_Vertex, SNAP_CONSTANT, constants, 1);
    }
}

// hi, or lo moved one pixel towards hi when the span is between MIN_SNAP_PIXELS and one pixel
static float MinPixelSpan(float ddcPerPixel, float lo, float hi) {
    float span = hi - lo;
    float length = std::fabs(span);
    return length >= ddcPerPixel * MIN_SNAP_PIXELS && length < ddcPerPixel ? lo + std::copysign(ddcPerPixel, span) : hi;
}

// corners: 0 = (left, top), 1 = (left, bottom), 2 = (right, top), 3 = (right, bottom)
static void OnTexturePosition(const CRect& rect, C3Vector* position) {
    CGxDevice* device = g_theGxDevicePtr;

    if (!device) {
        return;
    }

    float width;
    float height;
    ViewportPixels(device, width, height);

    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    float right = MinPixelSpan(NDCToDDCWidth(1.0f) / width, rect.minX, rect.maxX);
    float bottom = MinPixelSpan(NDCToDDCHeight(1.0f) / height, rect.maxY, rect.minY);
    position[2].x = right;
    position[3].x = right;
    position[1].y = bottom;
    position[3].y = bottom;
}

static void UpdateBatchTextureCallback() {
    CSimpleRender::s_batchTextureCallback = (SamplingOff() && SnapOff()) ? nullptr : OnBatchTexture;
}

// rebuilds every texture's quad from its rect
static void RefreshTextureQuads() {
    CSimpleTop* top = CSimpleTop::s_instance;

    if (!top) {
        return;
    }

    for (CSimpleFrame* frame = top->m_frames.Head(); frame; frame = top->m_frames.Next(frame)) {
        for (CSimpleRegion* region = frame->m_regions.Head(); region; region = frame->m_regions.Next(region)) {
            if (region->IsA(CSimpleTexture::GetObjectType())) {
                CSimpleTexture* texture = static_cast<CSimpleTexture*>(region);
                texture->OnFrameSizeChanged(texture->m_rect);
            }
        }
    }
}

static void OnHalfPixelFix(AwesomeCVar* cvar, bool changed) {
    g_cameraHalfPixelFix = cvar->m_intValue;
}

static void OnTextureSampling(AwesomeCVar* cvar, bool changed) {
    UpdateBatchTextureCallback();

    if (!changed) {
        return;
    }

    for (CGxShader* shader : CSimpleTexture::s_imageModePixelShaders) {
        if (shader) {
            AwesomeShaderReload(shader);
        }
    }
}

static void OnPixelSnap(AwesomeCVar* cvar, bool changed) {
    CSimpleTexture::s_positionCallback = cvar->m_intValue ? OnTexturePosition : nullptr;
    UpdateBatchTextureCallback();

    if (!changed) {
        return;
    }

    for (CGxShader* shader : CSimpleRender::s_vertexShader) {
        if (shader) {
            AwesomeShaderReload(shader);
        }
    }

    RefreshTextureQuads();
}

void AwesomeUiFixesRegisterCVars() {
    for (const AwesomeShaderSource& source : s_samplingShaders) {
        AwesomeShaderRegister(&source);
    }

    AwesomeShaderRegister(&s_snapShader);

    // the UI is created later (CSimpleTop), under the owned names
    CSimpleTexture::s_imageModePixelShaderNames[0] = s_samplingShaders[0].m_name;
    CSimpleTexture::s_imageModePixelShaderNames[1] = s_samplingShaders[1].m_name;
    CSimpleRender::s_vertexShaderName = s_snapShader.m_name;

    s_cvHalfPixelFix.Register();
    s_cvTextureSampling.Register();
    s_cvPixelSnap.Register();
    s_cvUpscale.Register();
    s_cvUpscaleEdges.Register();
}
