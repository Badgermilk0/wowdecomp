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

static void OnHalfPixelFix(AwesomeCVar* cvar, bool changed);
static void OnTextureSampling(AwesomeCVar* cvar, bool changed);
static void OnPixelSnap(AwesomeCVar* cvar, bool changed);

enum {
    SAMPLING_ENGINE = 0,
    SAMPLING_BOX = 1,
    SAMPLING_TENT = 2,
};

static AwesomeCVar s_cvHalfPixelFix = AwesomeCVar::Int("uiHalfPixelFix", 1, 0, 1, OnHalfPixelFix);
static AwesomeCVar s_cvTextureSampling = AwesomeCVar::Int("uiTextureSampling", SAMPLING_BOX, SAMPLING_ENGINE, SAMPLING_TENT, OnTextureSampling);
static AwesomeCVar s_cvPixelSnap = AwesomeCVar::Int("uiPixelSnap", 1, 0, 1, OnPixelSnap);

// pixel c222: the stage-0 texture's width, height, 1/width, 1/height (c220/c221 belong to MSDF)
static const uint32_t SAMPLING_CONSTANT = 222;
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

    static const bool kTent = TENT;               // uiTextureSampling 2, one compile per mode

    static const float kMinTexelsPerPixel = 2.0;  // per axis; a 64px icon at ~35px (1.8:1) keeps the sharper single tap
    static const float kMaxTaps = 16.0;           // loop bound, per texture axis
    static const float kBoxBudget = 8.0;          // taps per axis the box footprint may use
    static const float kTentBudget = 16.0;        // the tent spans twice as far; 12 per axis still drops lines

    float4 sampleFootprint(float2 uv) {
        float2 du = ddx(uv);
        float2 dv = ddy(uv);
        float4 size = texelSize;

        float2 extent = (abs(du) + abs(dv)) * size.xy;  // pixel footprint bounding box, texels
        [branch] if (size.x <= 0.0 || max(extent.x, extent.y) <= kMinTexelsPerPixel) {
            return tex2Dgrad(uiTexture, uv, du, dv);
        }

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

    float4 mainNormal(float4 color : COLOR0, float2 uv : TEXCOORD0) : COLOR {
        return sampleFootprint(uv) * color;
    }

    float4 mainDesaturate(float4 color : COLOR0, float2 uv : TEXCOORD0) : COLOR {
        float4 s = sampleFootprint(uv);
        float gray = dot(s.rgb, float3(0.299, 0.587, 0.114));
        return float4(gray, gray, gray, s.a * color.a);
    }
)";

static bool SamplingOff() {
    return s_cvTextureSampling.m_intValue == SAMPLING_ENGINE;
}

static bool SnapOff() {
    return !s_cvPixelSnap.m_intValue;
}

static const AwesomeShaderDefine* SamplingDefines() {
    static const AwesomeShaderDefine s_box[] = { { "TENT", "0" }, { nullptr, nullptr } };
    static const AwesomeShaderDefine s_tent[] = { { "TENT", "1" }, { nullptr, nullptr } };
    return s_cvTextureSampling.m_intValue == SAMPLING_TENT ? s_tent : s_box;
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

static void OnBatchTexture(CGxTex* texture) {
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
}
