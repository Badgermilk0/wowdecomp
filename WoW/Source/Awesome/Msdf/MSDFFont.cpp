#include "MSDFFont.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ranges>
#include <span>

#include <windows.h>
#include <d3d9.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gxu/GxuFontFallback.h"
#include "MSDFCache.h"
#include "MSDFUtils.h"
#include "MSDFValidator.h"

MSDFFont::AtlasPage::~AtlasPage() {
    if (texture && g_theGxDevicePtr) {
        GxTexDestroy(texture);
    }

    texture = nullptr;
}

const void* MSDFFont::AtlasPage::pixels(uint32_t mip, uint32_t& pitch) {
    if (mip != 0) {
        return nullptr;
    }

    if (upload != nullptr) {
        const CiRect& dirty = texture->m_updateRect;

        if (dirty.minX != upload_rect.x0 || dirty.minY != upload_rect.y0 || dirty.maxX != upload_rect.x1 || dirty.maxY != upload_rect.y1) {
            return nullptr;
        }

        pitch = upload_pitch;
        return upload;
    }

    // anything else (a whole-texture refresh) clears: a zero row repeated with pitch 0
    pitch = 0;
    return kZeroRow->data();
}

void MSDFFont::AtlasPage::TextureCallback(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t face, uint32_t level, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    AtlasPage* page = static_cast<AtlasPage*>(userArg);

    if (!page) {
        return;
    }

    switch (command) {
        case GxTex_Latch:
            texels = page->pixels(level, texelStrideInBytes);
            break;

        // the device dropped the texture (reset); it comes back empty, so its glyphs go
        case GxTex_Recreate:
            page->owner->evictPage(*page);
            break;

        default:
            break;
    }
}

void MSDFFont::AtlasPage::markDirty(std::initializer_list<Region> regions, bool send) {
    Region box{.x0 = INT32_MAX, .y0 = INT32_MAX, .x1 = INT32_MIN, .y1 = INT32_MIN};

    const auto add = [&box](const Region& region) {
        if (region.x0 >= region.x1 || region.y0 >= region.y1) {
            return;
        }

        box = {
            .x0 = std::min(box.x0, region.x0),
            .y0 = std::min(box.y0, region.y0),
            .x1 = std::max(box.x1, region.x1),
            .y1 = std::max(box.y1, region.y1)
        };
    };

    for (const Region& region : regions) {
        add(region);
    }

    if (regions.size() != 0) {
        if (box.x0 >= box.x1) {
            return;
        }

        if (texture->m_needsUpdate) {
            add({.x0 = texture->m_updateRect.minX, .y0 = texture->m_updateRect.minY, .x1 = texture->m_updateRect.maxX, .y1 = texture->m_updateRect.maxY});
        }
    } else {
        // an empty rect dirties the whole texture
        box = {};
    }

    CiRect rect;
    rect.minY = box.y0;
    rect.minX = box.x0;
    rect.maxY = box.y1;
    rect.maxX = box.x1;
    GxTexUpdate(texture, rect, send ? 1 : 0);
}

void* MSDFFont::AtlasPage::realize() {
    CGxDevice* device = g_theGxDevicePtr;

    if (!device || !texture) {
        return nullptr;
    }

    device->ITexMarkAsUpdated(texture);
    return texture->m_needsCreation ? nullptr : texture->m_apiSpecificData;
}

bool MSDFFont::AtlasPage::needsCreate() const {
    return texture && texture->m_needsCreation;
}

MSDFFont::MSDFFont(FT_Face face, const FT_Byte* font_data, FT_Long data_size)
    : face_(face), font_data_(font_data), data_size_(data_size) {
    if (face == nullptr) { return; }

    const char* family = face->family_name != nullptr ? face->family_name : "Unknown";
    const char* style = face->style_name != nullptr ? face->style_name : "";
    if (MSDFCache::isFontBlacklisted(family, style, font_data, static_cast<size_t>(data_size))) { return; }

    msdf_font_ = createMsdfHandle(font_data, data_size);
    if (msdf_font_ == nullptr) { return; }

    msdfgen::FontMetrics font_metrics{};
    if (!msdfgen::getFontMetrics(font_metrics, msdf_font_) || !(font_metrics.emSize > 0.0)) { return; }
    em_size_ = font_metrics.emSize;

    cache_ = std::make_unique<MSDFCache>(font_data, data_size, family, style, msdf::kSdfRenderSize, msdf::kSdfSpread);

    MSDFCache::FaceRecord record = cache_->getFaceRecord();
    if (record.verdict == MSDFCache::FaceVerdict::eUnknown) {
        record = evaluateFace(msdf_font_);
        cache_->setFaceRecord(record);
    }
    face_metrics_ = record.metrics;

    is_valid_ = (msdf::allow_unsafe_fonts || record.verdict == MSDFCache::FaceVerdict::eCompatible) &&
        (!msdf::is_cjk || cache_->getFaceRecord().pregen_complete);
    if (is_valid_) { glyph_pool_.reserve(kGlyphPoolReserve); }
}

MSDFFont::~MSDFFont() {
    glyph_pool_.clear();
    atlas_pages_.clear();
    cache_.reset();
    if (msdf_font_ != nullptr) {
        msdfgen::destroyFont(msdf_font_);
        msdf_font_ = nullptr;
    }
}

MSDFFont::AtlasPage* MSDFFont::getAtlasPage(size_t index) const {
    if (index < atlas_pages_.size()) { return atlas_pages_[index].get(); }
    return nullptr;
}

const GlyphMetrics* MSDFFont::findGlyph(uint32_t codepoint) const {
    const auto it = glyph_pool_.find(codepoint);
    return it != glyph_pool_.end() ? &it->second : nullptr;
}

const GlyphMetrics* MSDFFont::getGlyph(uint32_t codepoint) {
    const auto pit = glyph_pool_.find(codepoint);
    if (pit != glyph_pool_.end()) { return &pit->second; }

    auto [it, inserted] = glyph_pool_.try_emplace(codepoint);
    GlyphMetrics& metrics = it->second;

    if (cache_->tryLoadGlyph(codepoint, metrics)) {
        const GlyphMetrics* placed = uploadGlyphToAtlas(metrics, codepoint);
        if (placed == nullptr) { glyph_pool_.erase(codepoint); }
        return placed;
    }

    GlyphMetricsToStore storage;
    if (!generateGlyphData(codepoint, storage, true)) {
        glyph_pool_.erase(it);
        return nullptr;
    }

    metrics.width = storage.width;
    metrics.height = storage.height;
    metrics.bitmap_left = storage.bitmap_left;
    metrics.bitmap_top = storage.bitmap_top;

    const GlyphMetrics* placed = &metrics;
    if (storage.width > 0 && storage.height > 0) {
        metrics.pixel_data = storage.owned_pixel_data.data();
        placed = uploadGlyphToAtlas(metrics, codepoint);
        if (placed == nullptr) {
            glyph_pool_.erase(codepoint);
            return nullptr;
        }
    }

    cache_->storeGlyph(std::move(storage));

    return placed;
}

void MSDFFont::loadGlyphs(std::span<const uint32_t> codepoints) {
    for (size_t first = 0; first < codepoints.size();) {
        const std::span<const uint32_t> run =
            codepoints.subspan(first, cache_->prefetchBlock(codepoints.subspan(first)));
        for (const uint32_t codepoint : run) {
            getGlyph(codepoint);
        }
        first += run.size();
    }
}

MSDFFont* MSDFFont::get(FT_Face face) {
    const auto it = kFontHandles->find(face);
    if (it != kFontHandles->end() && it->second->isValid()) { return it->second.get(); }
    return nullptr;
}

void MSDFFont::registerFace(FT_Face face, const FT_Byte* data, FT_Long size) {
    if (kFontHandles->contains(face)) { return; }
    auto font = std::make_unique<MSDFFont>(face, data, size);
    if (font->msdf_font_ != nullptr) { kFontHandles[face] = std::move(font); }
}

void MSDFFont::unregisterFace(FT_Face face) { kFontHandles->erase(face); }

void MSDFFont::clearAllCache() {
    for (auto& handle : *kFontHandles | std::views::values) {
        if (handle) {
            handle->glyph_pool_.clear();
            handle->atlas_pages_.clear();
            handle->oldest_page_ = 0;
            handle->eviction_count_++;
        }
    }
}

void MSDFFont::shutdown() { kFontHandles->clear(); }

bool MSDFFont::createAtlasPage() {
    auto page = std::make_unique<AtlasPage>(this, msdf::kAtlasGutter);
    // linear filtering; m_bit15: the texture callback's texels start at the dirty rect, not the image base
    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 1);
    CGxTex* texture = nullptr;

    if (!g_theGxDevicePtr || !GxTexCreate(GxTex_2d, msdf::kAtlasSize, msdf::kAtlasSize, 0, GxTex_Argb8888, GxTex_Argb8888, flags, page.get(), &AtlasPage::TextureCallback, "AwesomeWotlk", texture) || !texture) {
        return false;
    }

    page->texture = texture;
    page->markDirty();
    atlas_pages_.push_back(std::move(page));
    return true;
}

void MSDFFont::evictPage(AtlasPage& page) {
    for (const uint32_t cp : page.codepoints) {
        glyph_pool_.erase(cp);
    }
    page.clear();
    eviction_count_++;
}

GlyphMetrics* MSDFFont::uploadGlyphToAtlas(GlyphMetrics& metrics, uint32_t codepoint) {
    if (metrics.pixel_data == nullptr || metrics.width == 0 || metrics.height == 0) { return &metrics; }

    GlyphMetrics* placed = &metrics;
    int16_t page_index = -1;
    AtlasPage* target_page = nullptr;

    for (size_t i = 0; i < atlas_pages_.size(); ++i) {
        AtlasPage* page = atlas_pages_[i].get();
        if (page->next_x + metrics.width + msdf::kAtlasGutter <= msdf::kAtlasSize &&
            page->next_y + metrics.height + msdf::kAtlasGutter <= msdf::kAtlasSize) {
            page_index = static_cast<int16_t>(i);
            target_page = page;
            break;
        }
        const int next_y = static_cast<int>(page->next_y + page->row_height + msdf::kAtlasGutter);
        if (next_y + metrics.height + msdf::kAtlasGutter <= msdf::kAtlasSize) {
            page->next_x = msdf::kAtlasGutter;
            page->next_y = next_y;
            page->row_height = 0;
            page_index = static_cast<int16_t>(i);
            target_page = page;
            break;
        }
    }
    if (page_index == -1) {
        if (atlas_pages_.size() >= msdf::kMaxAtlasPages) {
            page_index = static_cast<int16_t>(oldest_page_);
            target_page = atlas_pages_[oldest_page_].get();

            evictPage(*target_page);
            placed = &glyph_pool_.find(codepoint)->second;
            target_page->markDirty();
            oldest_page_ = (oldest_page_ + 1) % msdf::kMaxAtlasPages;
        } else {
            if (!createAtlasPage()) { return nullptr; }
            page_index = static_cast<int16_t>(atlas_pages_.size() - 1);
            target_page = atlas_pages_.back().get();
        }
    }
    if (target_page->texture == nullptr) { return nullptr; }

    GlyphMetrics& glyph = *placed;
    if (target_page->needsCreate()) { target_page->markDirty(); }
    target_page->upload = glyph.pixel_data;
    target_page->upload_pitch = static_cast<uint32_t>(glyph.width) * 4;
    target_page->upload_rect = {
        .x0 = target_page->next_x,
        .y0 = target_page->next_y,
        .x1 = target_page->next_x + static_cast<int32_t>(glyph.width),
        .y1 = target_page->next_y + static_cast<int32_t>(glyph.height)
    };
    target_page->markDirty({target_page->upload_rect});
    target_page->upload = nullptr;

    constexpr auto kAtlasSize = static_cast<float>(msdf::kAtlasSize);
    glyph.u0 = static_cast<float>(target_page->next_x) / kAtlasSize;
    glyph.v0 = static_cast<float>(target_page->next_y) / kAtlasSize;
    glyph.u1 = static_cast<float>(target_page->next_x + glyph.width) / kAtlasSize;
    glyph.v1 = static_cast<float>(target_page->next_y + glyph.height) / kAtlasSize;
    glyph.atlas_page_index = page_index;

    target_page->next_x += static_cast<int>(glyph.width + msdf::kAtlasGutter);
    target_page->row_height = std::max(target_page->row_height, static_cast<int>(glyph.height));
    target_page->codepoints.push_back(codepoint);

    return placed;
}

void MSDFFont::generateMsdf(std::vector<uint8_t>& out_data, const msdfgen::Shape& shape,
    const msdfgen::Projection& projection, double scale, int sdf_w, int sdf_h) {
    const auto pixel_count = static_cast<size_t>(sdf_w) * sdf_h;
    auto msdf_buf = msdf_pool_.acquireSized(pixel_count * 3);
    auto sdf_buf = msdf_pool_.acquireSized(pixel_count);

    msdfgen::BitmapRef<float, 3> msdf_bitmap(msdf_buf.data(), sdf_w, sdf_h);
    msdfgen::BitmapRef sdf_bitmap(sdf_buf.data(), sdf_w, sdf_h);

    msdfgen::MSDFGeneratorConfig config;
    config.overlapSupport = true;

    const msdfgen::Range msdf_range(msdf::kSdfSpread / scale);
    msdfgen::generateMSDF(msdf_bitmap, shape, projection, msdf_range, config);
    const msdfgen::SDFTransformation msdf_transform(projection, msdf_range);
    msdfgen::distanceSignCorrection(msdf_bitmap, shape, msdf_transform, msdfgen::FillRule::FILL_NONZERO);

    const msdfgen::Range sdf_range(msdf::kSdfSpread / scale * 5.0);
    msdfgen::generateSDF(sdf_bitmap, shape, projection, sdf_range);
    const msdfgen::SDFTransformation sdf_transform(projection, sdf_range);
    msdfgen::distanceSignCorrection(sdf_bitmap, shape, sdf_transform, msdfgen::FillRule::FILL_NONZERO);

    out_data.resize(pixel_count * 4);
    const std::span dest(out_data);
    const std::span<const float> src_msdf(msdf_buf);
    const std::span<const float> src_sdf(sdf_buf);

    const auto to_byte = [](float v) { return static_cast<uint8_t>(std::clamp(v * 255.f, 0.f, 255.f)); };
    for (size_t i = 0; i < pixel_count; ++i) {
        dest[i * 4 + 0] = to_byte(src_msdf[i * 3 + 0]);
        dest[i * 4 + 1] = to_byte(src_msdf[i * 3 + 1]);
        dest[i * 4 + 2] = to_byte(src_msdf[i * 3 + 2]);
        dest[i * 4 + 3] = to_byte(src_sdf[i]);
    }
    msdf_pool_.release(std::move(msdf_buf));
    msdf_pool_.release(std::move(sdf_buf));
}

bool MSDFFont::generateGlyphData(uint32_t codepoint, GlyphMetricsToStore& out_storage, bool fallback) const {
    out_storage.codepoint = codepoint;
    out_storage.width = 0;
    out_storage.height = 0;
    out_storage.bitmap_left = 0;
    out_storage.bitmap_top = 0;
    out_storage.owned_pixel_data.clear();
    out_storage.data_size = 0;

    msdfgen::GlyphIndex index;
    msdfgen::Shape shape;
    msdfgen::FontHandle* source = msdf_font_;
    double em_size = em_size_;
    if (em_size_ <= 0.0) { return false; }

    // wowdecomp: a glyph the face lacks comes from the first fallback face that has it and passed the validator, scaled
    // to its own em as the engine's raster path scales it; the engine opened that face when it laid the glyph out
    if (!msdfgen::getGlyphIndex(index, msdf_font_, codepoint)) {
        const FT_Face fallback_face = fallback ? GxuFontFallbackFind(face_, codepoint, false) : nullptr;
        const MSDFFont* fallback_font = fallback_face != nullptr ? MSDFFont::get(fallback_face) : nullptr;
        if (fallback_font == nullptr || fallback_font->em_size_ <= 0.0 ||
            !msdfgen::getGlyphIndex(index, fallback_font->msdf_font_, codepoint)) {
            return false;
        }
        source = fallback_font->msdf_font_;
        em_size = fallback_font->em_size_;
    }

    if (!msdfgen::loadGlyph(shape, source, index)) {
        return false;
    }
    if (shape.contours.empty()) { return true; }

    // wowdecomp: msdfgen is built without Skia, so there is no resolveShapeGeometry; overlapping contours are left to
    // the generator's overlapSupport and the validator keeps self-intersecting faces on the engine path
    msdfgen::edgeColoringInkTrap(shape, 3.0, 0);

    const double scale = msdf::kSdfRenderSize / em_size;
    const auto [l, b, r, t] = shape.getBounds();
    if (r <= l || t <= b) { return true; }

    const auto x_min = static_cast<int>(std::floor(l * scale));
    const auto y_min = static_cast<int>(std::floor(b * scale));
    const auto x_max = static_cast<int>(std::ceil(r * scale));
    const auto y_max = static_cast<int>(std::ceil(t * scale));
    out_storage.bitmap_left = x_min;
    out_storage.bitmap_top = y_max;

    constexpr auto kSpread = static_cast<int>(msdf::kSdfSpread);
    const int sdf_w = x_max - x_min + 2 * kSpread;
    const int sdf_h = y_max - y_min + 2 * kSpread;
    if (sdf_w > kMaxSdfDim || sdf_h > kMaxSdfDim) { return true; }

    // pixel = scale * (coord + translate) = scale * coord + spread - min
    const msdfgen::Projection projection(
        msdfgen::Vector2(scale, scale), msdfgen::Vector2((kSpread - x_min) / scale, (kSpread - y_min) / scale));

    generateMsdf(out_storage.owned_pixel_data, shape, projection, scale, sdf_w, sdf_h);
    out_storage.width = static_cast<uint16_t>(sdf_w);
    out_storage.height = static_cast<uint16_t>(sdf_h);
    out_storage.data_size = out_storage.owned_pixel_data.size();
    return true;
}

msdfgen::FontHandle* MSDFFont::createMsdfHandle(const FT_Byte* data, FT_Long size) {
    return msdf::msdf_freetype == nullptr ? nullptr : msdfgen::loadFontData(msdf::msdf_freetype, data, size);
}

MSDFCache::FaceRecord MSDFFont::evaluateFace(msdfgen::FontHandle* font) {
    return {
        .verdict = msdf_validator::isFontMsdfCompatible(font) ? MSDFCache::FaceVerdict::eCompatible
                                                              : MSDFCache::FaceVerdict::eIncompatible,
        .metrics = msdf_validator::measureFace(font)
    };
}
