// awesome_wotlk MSDF font rendering (MSDF.cpp), on the engine hooks in Engine/Source/Gxu/GxuFontMsdf.h.
//
// The engine still lays text out with its FreeType raster metrics; InitializeTextLine tags each glyph quad of an MSDF
// font with its code point and puts it on page 0. Before a batch draws, every pending quad is moved and re-mapped onto
// the glyph's MSDF atlas cell (the atlas page goes in the sign bits of u and v), and the font shaders turn the distance
// field into coverage, outline and shadow. Fonts the validator rejects, monochrome fonts and every non-D3D9 device keep
// the stock path.

#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "WoW/Source/Awesome/AwesomeD3d.h"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxVertexPCT.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gxu/GxuFont.h"
#include "Engine/Source/Gxu/GxuFontFace.h"
#include "Engine/Source/Gxu/GxuFontFont.h"
#include "Engine/Source/Gxu/GxuFontMiscClasses.h"
#include "Engine/Source/Gxu/GxuFontMsdf.h"
#include "Engine/Source/Gxu/GxuFontString.h"
#include "Engine/Source/Gxu/GxuFontUtil.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Services/Texture.h"

#include "MSDF.h"
#include "MSDFFaceSweep.h"
#include "MSDFFont.h"
#include "MSDFPregen.h"
#include "MSDFShaders.h"
#include "MSDFValidator.h"

namespace {
enum Mode : int32_t {
    MODE_DISABLED = 0,
    MODE_ENABLED = 1,
    MODE_ENABLED_UNSAFE = 2,
};

constexpr int32_t kMinStreamVerts = 0x800;
constexpr int32_t kMaxStreamVerts = 0xFFFC;
// 4 verts each, the last vertex index still fits the 16-bit index buffer
constexpr uint32_t kMaxFontQuads = 0x3FFF;
// the last d3d9 samplers, s12-s15 in the font pixel shader
constexpr uint32_t kFirstAtlasSampler = 16 - msdf::kMaxAtlasPages;
constexpr size_t kEngineAtlasPages = 8;
constexpr size_t kPrefetchReserve = 16383;
constexpr std::array<float, 4> kResetControl{};
constexpr msdf_validator::FaceMetrics kResetMetrics{};

constexpr const char* kFontShaderName = "AwesomeWotlk_MSDFFont";

void OnOutlinePass(AwesomeCVar* cvar, bool changed);

// read at font initialisation only: switching takes a restart
AwesomeCVar s_cvMode = AwesomeCVar::Int("MSDFMode", MODE_ENABLED, MODE_DISABLED, MODE_ENABLED_UNSAFE);
AwesomeCVar s_cvOutlinePass = AwesomeCVar::Int("MSDFOutlinePass", 1, 0, 1, OnOutlinePass);

bool s_enabled = false;
std::string s_locale;
std::vector<uint32_t> s_misses;

const AwesomeShaderDefine* PixelDefines() {
    static const AwesomeShaderDefine s_single[] = { { "OUTLINE_PASS", "0" }, { nullptr, nullptr } };
    static const AwesomeShaderDefine s_split[] = { { "OUTLINE_PASS", "1" }, { nullptr, nullptr } };
    return s_cvOutlinePass.m_intValue ? s_split : s_single;
}

const char* const s_pixelEntries[] = { "main" };
const char* const s_vertexEntries[] = { "main", "mainStereo" };

const AwesomeShaderSource s_pixelShader = {
    GxSh_Pixel, kFontShaderName, msdf_shaders::kPixelShaderHlsl, s_pixelEntries, 1, "Shaders\\Pixel", "UI", nullptr, PixelDefines,
};

const AwesomeShaderSource s_vertexShader = {
    GxSh_Vertex, kFontShaderName, msdf_shaders::kVertexShaderHlsl, s_vertexEntries, 2, "Shaders\\Vertex", "UI", nullptr, nullptr,
};

MSDFFont* MsdfFontOf(CGxFont* font) {
    if (!font || !font->m_fontFaceHandle || (font->m_fontFlags & 2)) {
        return nullptr;
    }

    return MSDFFont::get(FontFaceGetFace(font->m_fontFaceHandle));
}

// px an outlined glyph sits into its padded engine cell
int32_t OutlineMode(uint32_t fontFlags) {
    if (fontFlags & 8) {
        return 2;
    }

    return (fontFlags & 1) ? 1 : 0;
}

bool Is3d(const CGxString* string) {
    return (string->m_flags & 0x80) != 0;
}

uint8_t AtlasToken(size_t evictions) {
    return static_cast<uint8_t>((evictions & 0x7F) | 0x80);
}

bool SplitsOutlines(const CGxString* string, const MSDFFont* font, bool outlinePass) {
    return outlinePass && font && !Is3d(string);
}

uint32_t PageVertCount(CGxString* string, const MSDFFont* font, int32_t page, bool outlinePass) {
    uint32_t count = static_cast<uint32_t>(string->CalculateVertsNeeded(page));
    return SplitsOutlines(string, font, outlinePass) ? count * 2 : count;
}

// InitializeTextLine stores 1 + code point in the left edge's u of every MSDF glyph, 0 = not a glyph
uint32_t GlyphCodepoint(const VERT& vert) {
    return vert.m_tc.x > 1.0f ? static_cast<uint32_t>(vert.m_tc.x - 1.0f) : 0;
}

// verts the engine rebuilt since the last ProcessGeometry, u still holding the code points; all on page 0
std::span<VERT> PendingVerts(CGxString* string) {
    TEXTLINETEXTURE* line = string->m_textLines[0];

    if (!string->m_msdfPending || !line || line->m_verts.m_count < 4) {
        return {};
    }

    return { line->m_verts.m_data, line->m_verts.m_count };
}

// moves each pending glyph quad onto its MSDF cell; misses (not yet generated) are collected and left pending
void ProcessGeometry(CGxString* string, MSDFFont* font, std::vector<uint32_t>* misses) {
    std::span<VERT> verts = PendingVerts(string);

    if (!font || verts.empty()) {
        return;
    }

    CGxFont* gxfont = string->m_currentFace;
    bool is3d = Is3d(string);
    double fontHeight = string->m_currentFontHeight;
    // outlined glyphs sit 1 (2 thick) px into their padded engine cell
    double outlineOffset = is3d ? 0.0 : OutlineMode(gxfont->m_fontFlags);
    double baselineOffset = outlineOffset > 0.0 ? 1.0 : 0.0;
    double effectiveHeight = is3d ? fontHeight : ScreenToPixelHeight(0, string->m_currentFontHeight);
    double scale = effectiveHeight / msdf::kSdfRenderSize;
    double pad = msdf::kSdfSpread * scale;
    // the engine's raster is capped at 32 px and stretched to the effective height
    double rasterScale = gxfont->m_pixelSize > 0 ? effectiveHeight / gxfont->m_pixelSize : 1.0;

    size_t evictions = font->getAtlasEvictionCount();
    bool complete = true;

    for (size_t q = 0; q + 4 <= verts.size(); q += 4) {
        std::span<VERT> quad = verts.subspan(q, 4);
        uint32_t codepoint = GlyphCodepoint(quad[0]);

        if (codepoint == 0) {
            continue;
        }

        const GlyphMetrics* gm = misses ? font->findGlyph(codepoint) : font->getGlyph(codepoint);

        if (!gm) {
            if (misses) {
                misses->push_back(codepoint);
                complete = false;
            }

            continue;
        }

        CHARCODEDESC* entry = gxfont->NewCodeDesc(codepoint);

        if (!entry) {
            continue;
        }

        VERT& leftBottom = quad[0];
        VERT& leftTop = quad[1];
        VERT& rightBottom = quad[2];
        VERT& rightTop = quad[3];

        double column0 = std::floor(entry->m_bitmapData.m_glyphBearing) * rasterScale;
        double left = static_cast<double>(leftBottom.m_vc.x) + gm->bitmap_left * scale - column0 - pad + outlineOffset * rasterScale;
        double right = left + gm->width * scale;
        double top = static_cast<double>(leftTop.m_vc.y) + gm->bitmap_top * scale + pad - baselineOffset;
        double bottom = top - gm->height * scale;

        leftBottom.m_vc.x = static_cast<float>(left);
        leftBottom.m_vc.y = static_cast<float>(bottom);
        leftTop.m_vc.x = static_cast<float>(left);
        leftTop.m_vc.y = static_cast<float>(top);
        rightBottom.m_vc.x = static_cast<float>(right);
        rightBottom.m_vc.y = static_cast<float>(bottom);
        rightTop.m_vc.x = static_cast<float>(right);
        rightTop.m_vc.y = static_cast<float>(top);

        // the atlas page index in the sign bits, the mantissa kept bit for bit
        float uSign = (gm->atlas_page_index & 1) ? -1.0f : 1.0f;
        float vSign = (gm->atlas_page_index & 2) ? -1.0f : 1.0f;

        leftBottom.m_tc.x = gm->u0 * uSign;
        leftBottom.m_tc.y = gm->v0 * vSign;
        leftTop.m_tc.x = gm->u0 * uSign;
        leftTop.m_tc.y = gm->v1 * vSign;
        rightBottom.m_tc.x = gm->u1 * uSign;
        rightBottom.m_tc.y = gm->v0 * vSign;
        rightTop.m_tc.x = gm->u1 * uSign;
        rightTop.m_tc.y = gm->v1 * vSign;
    }

    // the geometry is rebuilt when the atlas evicts a page after this
    string->m_msdfAtlasToken = AtlasToken(evictions);

    if (complete) {
        string->m_msdfPending = 0;
    }
}

// splits the page into core and outline regions (plus their shadow copies) so the outlines draw as a separate pass
void WriteOutlinePasses(CGxString* string, std::span<CGxVertexPCT> out, int32_t page, uint32_t vertIndex) {
    struct Region {
        int32_t end;
        int32_t offset;
    };

    int32_t pageCount = string->CalculateVertsNeeded(page);
    std::array<Region, 4> regions{};
    size_t regionCount;

    if (string->m_flags & 1) {
        pageCount /= 2;
        regions = { {
            { pageCount, 0 },
            { 2 * pageCount, -pageCount },
            { 3 * pageCount, -pageCount },
            { 4 * pageCount, -2 * pageCount },
        } };
        regionCount = 4;
    } else {
        regions[0] = { pageCount, 0 };
        regions[1] = { 2 * pageCount, -pageCount };
        regionCount = 2;
    }

    size_t written = 0;
    int32_t current = static_cast<int32_t>(vertIndex);
    int32_t remaining = static_cast<int32_t>(out.size());

    for (size_t i = 0; i < regionCount && remaining > 0; i++) {
        Region region = regions[i];

        if (current >= region.end) {
            continue;
        }

        int32_t count = std::min(remaining, region.end - current);
        std::span<CGxVertexPCT> chunk = out.subspan(written, static_cast<size_t>(count));
        string->WriteGeometry(chunk.data(), page, current + region.offset, count);

        if ((i & 1) == 0) {
            // the core pass sentinel: the engine gives every UI vertex z 1.0
            for (CGxVertexPCT& vert : chunk) {
                vert.p.z = -vert.p.z;
            }
        }

        written += static_cast<size_t>(count);
        current += count;
        remaining -= count;
    }
}

void SetFontShaderState(const std::array<float, 4>& control, const msdf_validator::FaceMetrics& metrics) {
    CGxDevice* device = g_theGxDevicePtr;

    if (!device) {
        return;
    }

    device->ShaderConstantsSet(GxSh_Pixel, msdf::kSdfSamplerSlot, control.data(), 1);
    device->ShaderConstantsSet(GxSh_Vertex, msdf::kSdfSamplerSlot, control.data(), 1);

    constexpr float renderSize = static_cast<float>(msdf::kSdfRenderSize);
    float metricConstants[4] = { metrics.stem * renderSize, metrics.hairline * renderSize, metrics.counter * renderSize, renderSize };
    device->ShaderConstantsSet(GxSh_Pixel, msdf::kSdfMetricsSlot, metricConstants, 1);
}

void SetStringShaderState(const CGxString* string, const MSDFFont* font) {
    bool is3d = Is3d(string);
    float outlineMode = is3d ? 0.0f : static_cast<float>(OutlineMode(string->m_currentFace->m_fontFlags));
    SetFontShaderState({ is3d ? 1.0f : 0.0f, outlineMode, static_cast<float>(msdf::kSdfSpread), static_cast<float>(msdf::kAtlasSize) }, font->getFaceMetrics());
}

// the atlas pages only depend on the font, and a BATCHEDRENDERFONTDESC holds exactly one; the engine never uses the
// samplers past 11, so they are set on the device directly
void BindAtlasPages(const MSDFFont* font) {
    IDirect3DDevice9* d3d = AwesomeD3dDevice();

    if (!d3d) {
        return;
    }

    for (size_t index = 0; index < font->getAtlasPageCount(); index++) {
        auto* page = font->getAtlasPage(index);

        if (!page || !page->texture) {
            continue;
        }

        auto* texture = static_cast<IDirect3DBaseTexture9*>(page->realize());

        if (!texture) {
            continue;
        }

        DWORD sampler = static_cast<DWORD>(kFirstAtlasSampler + index);
        d3d->SetTexture(sampler, texture);
        d3d->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d3d->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        d3d->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        d3d->SetSamplerState(sampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d3d->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    }
}

// BATCHEDRENDERFONTDESC::RenderBatch: all geometry is resolved before anything streams
int32_t RenderBatch(BATCHEDRENDERFONTDESC* desc) {
    bool outlinePass = s_cvOutlinePass.m_intValue != 0;
    CGxFont* gxfont = desc->m_face;
    MSDFFont* font = MsdfFontOf(gxfont);

    for (CGxString* string = desc->m_strings.Head(); string; string = desc->m_strings.Next(string)) {
        string->CheckGeometry();
    }

    if (font) {
        // writes what is pooled and queues the rest
        for (CGxString* string = desc->m_strings.Head(); string; string = desc->m_strings.Next(string)) {
            ProcessGeometry(string, font, &s_misses);
        }

        if (!s_misses.empty()) {
            std::ranges::sort(s_misses);
            auto duplicates = std::ranges::unique(s_misses);
            s_misses.erase(duplicates.begin(), duplicates.end());

            size_t evictions = font->getAtlasEvictionCount();
            font->loadGlyphs(s_misses);
            s_misses.clear();

            if (font->getAtlasEvictionCount() != evictions) {
                for (CGxString* string = desc->m_strings.Head(); string; string = desc->m_strings.Next(string)) {
                    string->CheckGeometry();
                }
            }

            for (CGxString* string = desc->m_strings.Head(); string; string = desc->m_strings.Next(string)) {
                ProcessGeometry(string, font, nullptr);
            }
        }
    }

    for (uint32_t i = 0; i < kEngineAtlasPages; i++) {
        gxfont->m_textureCache[i].UpdateDirty();
    }

    std::array<CGxTex*, kEngineAtlasPages> pageTextures{};
    int32_t maxPageVerts = 0;

    for (size_t page = 0; page < kEngineAtlasPages; page++) {
        CTexture* texture = gxfont->m_textureCache[page].m_texture;

        if (!texture) {
            continue;
        }

        pageTextures[page] = TextureGetGxTex(texture, 1, nullptr);

        if (!pageTextures[page]) {
            continue;
        }

        int32_t pageVerts = 0;

        for (CGxString* string = desc->m_strings.Head(); string; string = desc->m_strings.Next(string)) {
            if (string->m_textLines[page]) {
                pageVerts += static_cast<int32_t>(PageVertCount(string, font, static_cast<int32_t>(page), outlinePass));
            }
        }

        maxPageVerts = std::max(maxPageVerts, pageVerts);
    }

    // a page past kMaxStreamVerts still goes out, across several draws
    uint32_t capacity = static_cast<uint32_t>(std::clamp(maxPageVerts, kMinStreamVerts, kMaxStreamVerts));
    CGxBuf* buf = GxBufStream(GxPoolTarget_Vertex, sizeof(CGxVertexPCT), capacity);
    std::span<CGxVertexPCT> stream(reinterpret_cast<CGxVertexPCT*>(GxBufLock(buf)), capacity);
    uint32_t written = 0;

    const auto flush = [&] {
        stream = { BATCHEDRENDERFONTDESC::UnlockVertexPtrAndRender(buf, static_cast<int32_t>(written)), capacity };
        written = 0;
    };

    if (!font) {
        SetFontShaderState(kResetControl, kResetMetrics);
    } else {
        BindAtlasPages(font);
    }

    for (size_t page = 0; page < kEngineAtlasPages; page++) {
        if (!pageTextures[page]) {
            continue;
        }

        g_theGxDevicePtr->RsSet(GxRs_Texture0, pageTextures[page]);
        int32_t pageIndex = static_cast<int32_t>(page);

        for (CGxString* string = desc->m_strings.Head(); string; string = desc->m_strings.Next(string)) {
            if (!string->m_textLines[page]) {
                continue;
            }

            uint32_t pageVerts = PageVertCount(string, font, pageIndex, outlinePass);

            if (pageVerts == 0) {
                continue;
            }

            if (font) {
                SetStringShaderState(string, font);
            }

            bool split = SplitsOutlines(string, font, outlinePass);

            for (uint32_t vertIndex = 0; vertIndex < pageVerts;) {
                uint32_t count = std::min(capacity - written, pageVerts - vertIndex);
                std::span<CGxVertexPCT> chunk = stream.subspan(written, count);

                if (split) {
                    WriteOutlinePasses(string, chunk, pageIndex, vertIndex);
                } else {
                    string->WriteGeometry(chunk.data(), pageIndex, static_cast<int32_t>(vertIndex), static_cast<int32_t>(count));
                }

                vertIndex += count;
                written += count;

                if (written == capacity) {
                    flush();
                }
            }
        }

        if (written != 0) {
            flush();
        }
    }

    // balances the relock of the last flush, or the first lock
    GxBufUnlock(buf, 0);
    return 1;
}

// CGxString::CheckGeometry, first: geometry placed against atlas pages that were since evicted is rebuilt
void CheckGeometry(CGxString* string) {
    MSDFFont* font = MsdfFontOf(string->m_currentFace);

    if (!font) {
        return;
    }

    uint8_t stored = string->m_msdfAtlasToken;

    if ((stored & 0x80) && stored != AtlasToken(font->getAtlasEvictionCount())) {
        string->ClearInstanceData();
        string->m_msdfAtlasToken = 0;
    }
}

void FaceCreated(FT_Face face, const void* data, uint32_t size) {
    MSDFFont::registerFace(face, static_cast<const FT_Byte*>(data), static_cast<FT_Long>(size));
}

void FaceDestroyed(FT_Face face) {
    MSDFFont::unregisterFace(face);
}

int32_t IsMsdfFace(FT_Face face) {
    return MSDFFont::get(face) != nullptr;
}

void Shutdown() {
    MSDFFaceSweep::stop();
    MSDFFont::shutdown();

    if (msdf::msdf_freetype) {
        msdfgen::deinitializeFreetype(msdf::msdf_freetype);
        msdf::msdf_freetype = nullptr;
    }

    g_gxuFontMsdf = {};
    s_enabled = false;
}

// GxuFontInitialize, first: the device exists, the font shaders are not created yet
void OnFontInitialize() {
    s_locale = CVar::Lookup("locale") && CVar::Lookup("locale")->m_stringValue.m_str ? CVar::Lookup("locale")->m_stringValue.m_str : "enUS";

    CGxDevice* device = g_theGxDevicePtr;

    // the font shaders need D3D9 and shader model 3
    if (s_cvMode.m_intValue == MODE_DISABLED || !device || (device->m_api != GxApi_D3d9 && device->m_api != GxApi_D3d9Ex) || device->m_caps.m_shaderTargets[GxSh_Pixel] < 4 || device->m_caps.m_shaderTargets[GxSh_Vertex] < 3) {
        return;
    }

    msdf::is_cjk = s_locale == "zhCN" || s_locale == "zhTW" || s_locale == "koKR";
    msdf::allow_unsafe_fonts = s_cvMode.m_intValue == MODE_ENABLED_UNSAFE;
    msdf::msdf_freetype = msdfgen::initializeFreetype();

    if (!msdf::msdf_freetype) {
        return;
    }

    s_misses.reserve(kPrefetchReserve);
    MSDFFaceSweep::start();

    g_fontShaderName = kFontShaderName;
    g_gxuFontIndexQuads = kMaxFontQuads;
    g_gxuFontMsdf.m_faceCreated = FaceCreated;
    g_gxuFontMsdf.m_faceDestroyed = FaceDestroyed;
    g_gxuFontMsdf.m_isMsdfFace = IsMsdfFace;
    g_gxuFontMsdf.m_checkGeometry = CheckGeometry;
    g_gxuFontMsdf.m_renderBatch = RenderBatch;
    g_gxuFontMsdf.m_shutdown = Shutdown;
    s_enabled = true;
}

void OnOutlinePass(AwesomeCVar* cvar, bool changed) {
    if (changed && s_enabled && g_fontPixelShader) {
        AwesomeShaderReload(g_fontPixelShader);
    }
}

int32_t Script_MsdfPregen(lua_State* L) {
    MSDFPregen::tryStartPreGen();
    return 0;
}
}  // namespace

namespace client {
const std::string& getGameLocale() {
    return s_locale;
}
}  // namespace client

void AwesomeMsdfRegisterCVars() {
    AwesomeShaderRegister(&s_pixelShader);
    AwesomeShaderRegister(&s_vertexShader);
    s_cvMode.Register();
    s_cvOutlinePass.Register();
    g_gxuFontInitializeCallback = OnFontInitialize;
}

void AwesomeMsdfOnEnterWorld() {
    if (s_enabled) {
        AwesomeLuaRegisterSlashCommand(FrameScript_GetContext(), "MSDFPREGENCMD", "/msdfpregen", Script_MsdfPregen);
    }
}
