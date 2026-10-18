#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gxu/GxuFontFont.h"
#include "Engine/Source/Gxu/GxuFontMiscClasses.h"
#include "Engine/Source/Gxu/GxuFontUtil.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Storm/H/storm.h"

// MODIFIED: font rendering. The stock client kept every font's glyphs on 256 x 256 ARGB4444 pages (a 16-level alpha)
// and capped the raster at 32 px so a cell always fit; the paste routines walked a fixed 256-texel stride. Pages are
// now ARGB8888 and grow with the font's cell height (PageSizeFor), the scratch buffers take the cell's own stride, and
// an update re-pastes and sends only the rectangle the device latches instead of the whole page.

// STATIC: 0x00C7D328 (256 x 256 ARGB4444 in the stock client)
static std::vector<uint32_t> s_textureData;

// the paste routines' working cells, a texel of border around the glyph cell
static std::vector<uint16_t> s_cellScratch[3];

static uint16_t* CellScratch(uint32_t index, uint32_t stride, uint32_t rows) {
    std::vector<uint16_t>& scratch = s_cellScratch[index];
    size_t size = static_cast<size_t>(stride) * rows;

    if (scratch.size() < size) {
        scratch.resize(size);
    }

    memset(scratch.data(), 0, size * sizeof(uint16_t));
    return scratch.data();
}

static uint32_t Grey(uint32_t alpha, uint32_t grey) {
    return (alpha << 24) | (grey << 16) | (grey << 8) | grey;
}

// VERIFY: 7a9fdc75 manual
// OFFSET: 0x006C8E70
void TEXTURECACHE::PasteGlyphOutlinedMonochrome(const GLYPHBITMAPDATA& data, uint32_t* dest) {
    const uint8_t* source = static_cast<const uint8_t*>(data.m_data);
    uint32_t thick = this->m_theFace->m_fontFlags & 8;
    uint32_t cellHeight = this->m_theFace->m_cellHeight;
    uint32_t cellWidth = data.m_glyphCellWidth;
    const uint32_t S = cellWidth + 2;

    uint16_t* buffer = CellScratch(0, S, cellHeight + 2);

    uint32_t yStart = data.m_yStart;
    uint16_t* line = &buffer[S * yStart + S + 1];

    if (thick) {
        line++;
    }

    for (uint32_t y = data.m_glyphHeight; y; y--) {
        for (uint32_t x = 0; x < data.m_glyphWidth; x++) {
            line[x] = (source[x >> 3] >> (7 - (x & 7))) & 1;
        }

        source += data.m_glyphPitch;
        line += S;
    }

    uint32_t passes = (thick != 0) + 1;

    for (uint32_t pass = 0; pass < passes; pass++) {
        uint32_t y = yStart;

        if (pass == 1 && yStart) {
            y = yStart - 1;
        }

        for (; y < cellHeight; y++) {
            uint32_t row = y * S;

            for (uint32_t x = 0; x < cellWidth; x++) {
                uint16_t mask = 0;
                uint16_t value = 0;

                if (pass == 1) {
                    if (buffer[row + x]) {
                        continue;
                    }

                    mask = 4;
                    value = 2;
                } else if (pass == 0) {
                    mask = 1;
                    value = 4;
                }

                uint16_t* cell = &buffer[row + x];
                int32_t found;

                if (y == 0) {
                    if (x == 0) {
                        found = (cell[1] & mask) || (cell[S + 1] & mask) || (cell[S] & mask);
                    } else if (x == cellWidth - 1) {
                        found = (cell[-1] & mask) || (cell[S - 1] & mask) || (cell[S] & mask);
                    } else {
                        found = (cell[-1] & mask) || (cell[1] & mask) || (cell[S - 1] & mask) || (cell[S] & mask) || (cell[S + 1] & mask);
                    }
                } else if (y == cellHeight - 1) {
                    if (x == 0) {
                        found = (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S - 1)] & mask) || (cell[1] & mask);
                    } else if (x == cellWidth - 1) {
                        found = (cell[-1] & mask) || (cell[-static_cast<ptrdiff_t>(S + 1)] & mask) || (cell[-static_cast<ptrdiff_t>(S)] & mask);
                    } else {
                        found = (cell[-static_cast<ptrdiff_t>(S + 1)] & mask) || (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S - 1)] & mask) || (cell[-1] & mask) || (cell[1] & mask);
                    }
                } else if (x == 0) {
                    found = (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S - 1)] & mask) || (cell[1] & mask) || (cell[S + 1] & mask) || (cell[S] & mask);
                } else if (x == cellWidth - 1) {
                    found = (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S + 1)] & mask) || (cell[-1] & mask) || (cell[S - 1] & mask) || (cell[S] & mask);
                } else {
                    found = (cell[-static_cast<ptrdiff_t>(S + 1)] & mask) || (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S - 1)] & mask) || (cell[-1] & mask) || (cell[1] & mask) || (cell[S - 1] & mask) || (cell[S] & mask) || (cell[S + 1] & mask);
                }

                if (found && *cell != 1) {
                    *cell = value;
                }
            }
        }
    }

    uint16_t* bufferRow = buffer;
    const uint32_t D = this->m_pageSize;

    for (uint32_t y = 0; y < cellHeight; y++) {
        for (uint32_t x = 0; x < cellWidth; x++) {
            uint16_t value = bufferRow[x];

            if (value == 1) {
                dest[x] = 0xFFFFFFFF;
            } else if (value == 2) {
                dest[x] = 0x77000000;
            } else {
                dest[x] = value != 4 ? 0 : 0xFF000000;
            }
        }

        bufferRow += S;
        dest += D;
    }
}

// VERIFY: 891087d8 manual
// OFFSET: 0x006C9330
void TEXTURECACHE::PasteGlyphNonOutlinedMonochrome(const GLYPHBITMAPDATA& data, uint32_t* dest) {
    const uint8_t* source = static_cast<const uint8_t*>(data.m_data);
    uint32_t pitch = data.m_glyphPitch;
    size_t size = sizeof(uint32_t) * data.m_glyphCellWidth;
    const uint32_t D = this->m_pageSize;

    for (uint32_t y = 0; y < data.m_yStart; y++) {
        memset(dest, 0, size);
        dest += D;
    }

    for (uint32_t y = 0; y < data.m_glyphHeight; y++) {
        for (uint32_t x = 0; x < data.m_glyphWidth; x++) {
            dest[x] = ((source[x >> 3] >> (7 - (x & 7))) & 1) ? 0xFFFFFFFF : 0;
        }

        source += pitch;
        dest += D;
    }

    int32_t remaining = this->m_theFace->m_cellHeight - data.m_yStart - data.m_glyphHeight;

    if (remaining > 0) {
        for (uint32_t y = 0; y < static_cast<uint32_t>(remaining); y++) {
            memset(dest, 0, size);
            dest += D;
        }
    }
}

// VERIFY: c5efdcbb manual
// OFFSET: 0x006C9420
void TEXTURECACHE::PasteGlyphOutlinedAA(const GLYPHBITMAPDATA& data, uint32_t* dest) {
    static const uint8_t s_outlineAlpha[] = { 0, 1, 1, 3, 5, 7, 9, 11, 13, 15 };

    const uint8_t* source = static_cast<const uint8_t*>(data.m_data);
    uint32_t thick = this->m_theFace->m_fontFlags & 8;
    uint32_t cellHeight = this->m_theFace->m_cellHeight;
    uint32_t cellWidth = data.m_glyphCellWidth;
    const uint32_t S = cellWidth + 2;

    uint16_t* alpha = CellScratch(0, S, cellHeight + 2);
    uint16_t* coverage = CellScratch(1, S, cellHeight + 2);
    uint16_t* state = CellScratch(2, S, cellHeight + 2);

    uint16_t* stateLine = &state[S * data.m_yStart + S + 1];

    if (thick) {
        stateLine++;
    }

    uint16_t* coverageLine = &coverage[stateLine - state];

    for (uint32_t y = data.m_glyphHeight; y; y--) {
        for (uint32_t x = 0; x < data.m_glyphWidth; x++) {
            uint8_t value = source[x];

            if (value) {
                stateLine[x] = 1;
                coverageLine[x] = value;
            }
        }

        coverageLine += S;
        source += data.m_glyphPitch;
        stateLine += S;
    }

    uint32_t passes = (thick != 0) + 1;

    for (uint32_t pass = 0; pass < passes; pass++) {
        uint16_t value = 2 * (pass != 0) + 2;
        uint16_t mask = 2 * (pass != 0) + 1;
        int32_t firstRow = static_cast<int32_t>(data.m_yStart) - 1;

        for (uint32_t y = firstRow < 0 ? 0 : firstRow; y < cellHeight; y++) {
            uint32_t row = y * S;

            for (uint32_t x = 0; x < cellWidth; x++) {
                uint16_t* cell = &state[row + x];

                if (*cell & mask) {
                    continue;
                }

                int32_t found;

                if (y == 0) {
                    if (x == 0) {
                        found = (cell[1] & mask) || (cell[S + 1] & mask) || (cell[S] & mask);
                    } else if (x == cellWidth - 1) {
                        found = (cell[-1] & mask) || (cell[S - 1] & mask) || (cell[S] & mask);
                    } else {
                        found = (cell[-1] & mask) || (cell[1] & mask) || (cell[S - 1] & mask) || (cell[S] & mask) || (cell[S + 1] & mask);
                    }
                } else if (y == cellHeight - 1) {
                    if (x == 0) {
                        found = (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S - 1)] & mask) || (cell[1] & mask);
                    } else if (x == cellWidth - 1) {
                        found = (cell[-1] & mask) || (cell[-static_cast<ptrdiff_t>(S + 1)] & mask) || (cell[-static_cast<ptrdiff_t>(S)] & mask);
                    } else {
                        found = (cell[-static_cast<ptrdiff_t>(S + 1)] & mask) || (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S - 1)] & mask) || (cell[-1] & mask) || (cell[1] & mask);
                    }
                } else if (x == 0) {
                    found = (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S - 1)] & mask) || (cell[1] & mask) || (cell[S + 1] & mask) || (cell[S] & mask);
                } else if (x == cellWidth - 1) {
                    found = (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S + 1)] & mask) || (cell[-1] & mask) || (cell[S - 1] & mask) || (cell[S] & mask);
                } else {
                    found = (cell[-static_cast<ptrdiff_t>(S + 1)] & mask) || (cell[-static_cast<ptrdiff_t>(S)] & mask) || (cell[-static_cast<ptrdiff_t>(S - 1)] & mask) || (cell[-1] & mask) || (cell[1] & mask) || (cell[S - 1] & mask) || (cell[S] & mask) || (cell[S + 1] & mask);
                }

                if (found) {
                    *cell = value;
                }
            }
        }
    }

    for (uint32_t y = 0; y < cellHeight; y++) {
        uint32_t row = y * S;

        for (uint32_t x = 0; x < cellWidth; x++) {
            uint16_t* cell = &state[row + x];
            int32_t count;

            if (y == 0) {
                if (x == 0) {
                    count = (cell[0] != 0) + (cell[1] != 0) + (cell[S + 1] != 0) + (cell[S] != 0);
                } else if (x == cellWidth - 1) {
                    count = (cell[0] != 0) + (cell[S] != 0) + (cell[S - 1] != 0) + (cell[-1] != 0);
                } else {
                    count = (cell[0] != 0) + (cell[1] != 0) + (cell[S + 1] != 0) + (cell[S] != 0) + (cell[S - 1] != 0) + (cell[-1] != 0);
                }
            } else if (y == cellHeight - 1) {
                if (x == 0) {
                    count = (cell[0] != 0) + (cell[1] != 0) + (cell[-static_cast<ptrdiff_t>(S - 1)] != 0) + (cell[-static_cast<ptrdiff_t>(S)] != 0);
                } else if (x == cellWidth - 1) {
                    count = (cell[-static_cast<ptrdiff_t>(S)] != 0) + (cell[-static_cast<ptrdiff_t>(S + 1)] != 0) + (cell[0] != 0) + (cell[-1] != 0);
                } else {
                    count = (cell[-static_cast<ptrdiff_t>(S)] != 0) + (cell[-static_cast<ptrdiff_t>(S + 1)] != 0) + (cell[-static_cast<ptrdiff_t>(S - 1)] != 0) + (cell[1] != 0) + (cell[0] != 0) + (cell[-1] != 0);
                }
            } else if (x == 0) {
                count = (cell[0] != 0) + (cell[S] != 0) + (cell[1] != 0) + (cell[S + 1] != 0) + (cell[-static_cast<ptrdiff_t>(S - 1)] != 0) + (cell[-static_cast<ptrdiff_t>(S)] != 0);
            } else if (x == cellWidth - 1) {
                count = (cell[S] != 0) + (cell[S - 1] != 0) + (cell[-static_cast<ptrdiff_t>(S)] != 0) + (cell[-static_cast<ptrdiff_t>(S + 1)] != 0) + (cell[0] != 0) + (cell[-1] != 0);
            } else {
                count = (cell[S] != 0) + (cell[S - 1] != 0) + (cell[-static_cast<ptrdiff_t>(S - 1)] != 0) + (cell[-static_cast<ptrdiff_t>(S)] != 0) + (cell[-static_cast<ptrdiff_t>(S + 1)] != 0) + (cell[S + 1] != 0) + (cell[1] != 0) + (cell[0] != 0) + (cell[-1] != 0);
            }

            alpha[row + x] = s_outlineAlpha[count];
        }
    }

    const uint32_t D = this->m_pageSize;

    for (uint32_t y = 0; y < cellHeight; y++) {
        uint32_t index = y * S;

        for (uint32_t x = 0; x < cellWidth; x++, index++) {
            // the outline's 4-bit alpha widened to 8 bits; the glyph's own coverage keeps all 8
            if (!state[index]) {
                dest[x] = 0;
            } else if (!coverage[index]) {
                dest[x] = Grey(alpha[index] * 17u, 0);
            } else {
                dest[x] = Grey(alpha[index] * 17u, coverage[index] & 0xFF);
            }
        }

        dest += D;
    }
}

// VERIFY: 45fd06c3 manual
// OFFSET: 0x006C9C70
void TEXTURECACHE::PasteGlyphNonOutlinedAA(const GLYPHBITMAPDATA& data, uint32_t* dest) {
    const uint8_t* source = static_cast<const uint8_t*>(data.m_data);
    uint32_t pitch = data.m_glyphPitch;
    size_t size = sizeof(uint32_t) * data.m_glyphCellWidth;
    const uint32_t D = this->m_pageSize;

    for (uint32_t y = 0; y < data.m_yStart; y++) {
        memset(dest, 0, size);
        dest += D;
    }

    for (uint32_t y = 0; y < data.m_glyphHeight; y++) {
        for (uint32_t x = 0; x < data.m_glyphWidth; x++) {
            // white, the coverage as an 8-bit alpha (its top 4 bits in the stock client)
            dest[x] = (static_cast<uint32_t>(source[x]) << 24) | 0xFFFFFF;
        }

        source += pitch;
        dest += D;
    }

    int32_t remaining = this->m_theFace->m_cellHeight - data.m_yStart - data.m_glyphHeight;

    if (remaining > 0) {
        for (uint32_t y = 0; y < static_cast<uint32_t>(remaining); y++) {
            memset(dest, 0, size);
            dest += D;
        }
    }
}

// VERIFY: d9c18e4c auto
// OFFSET: 0x006C9D50
void TEXTURECACHE::UpdateDirty() {
    if (this->m_anyDirtyGlyphs && this->m_texture) {
        CGxTex* gxTex = TextureGetGxTex(this->m_texture, 1, nullptr);

        if (gxTex) {
            // MODIFIED: font rendering; only the cells placed since the last upload (the whole page in the stock
            // client), joined to an update the device has not made yet
            CiRect rect = this->m_dirtyRect;

            if (rect.minX >= rect.maxX || rect.minY >= rect.maxY) {
                rect = CiRect(0, 0, this->m_pageSize, this->m_pageSize);
            }

            if (gxTex->m_needsUpdate) {
                const CiRect& pending = gxTex->m_updateRect;
                rect.minX = pending.minX < rect.minX ? pending.minX : rect.minX;
                rect.minY = pending.minY < rect.minY ? pending.minY : rect.minY;
                rect.maxX = pending.maxX > rect.maxX ? pending.maxX : rect.maxX;
                rect.maxY = pending.maxY > rect.maxY ? pending.maxY : rect.maxY;
            }

            GxTexUpdate(gxTex, rect.minX, rect.minY, rect.maxX, rect.maxY, 1);
        }

        this->m_dirtyRect = CiRect();
        this->m_anyDirtyGlyphs = 0;
    }
}

// MODIFIED: font rendering; a glyph cell joins the rectangle the next UpdateDirty sends
void TEXTURECACHE::MarkDirty(const CHARCODEDESC* desc) {
    uint32_t cellHeight = this->m_theFace ? this->m_theFace->m_cellHeight : 0;
    CiRect cell(desc->m_rowNumber * cellHeight, desc->m_glyphStartPixel, (desc->m_rowNumber + 1) * cellHeight, desc->m_glyphEndPixel + 1);

    if (this->m_dirtyRect.minX >= this->m_dirtyRect.maxX || this->m_dirtyRect.minY >= this->m_dirtyRect.maxY) {
        this->m_dirtyRect = cell;
    } else {
        this->m_dirtyRect.minX = cell.minX < this->m_dirtyRect.minX ? cell.minX : this->m_dirtyRect.minX;
        this->m_dirtyRect.minY = cell.minY < this->m_dirtyRect.minY ? cell.minY : this->m_dirtyRect.minY;
        this->m_dirtyRect.maxX = cell.maxX > this->m_dirtyRect.maxX ? cell.maxX : this->m_dirtyRect.maxX;
        this->m_dirtyRect.maxY = cell.maxY > this->m_dirtyRect.maxY ? cell.maxY : this->m_dirtyRect.maxY;
    }

    this->m_anyDirtyGlyphs = 1;
}

// MODIFIED: font rendering; at least seven rows of cells to a page, as the stock 256 page holds seven 36 px rows of
// the largest stock cell, up to 2048
uint32_t TEXTURECACHE::PageSizeFor(uint32_t cellHeight) {
    uint32_t size = 256;

    while (size < cellHeight * 7 && size < 2048) {
        size <<= 1;
    }

    return size;
}

// VERIFY: 0d8fc085 manual
// OFFSET: 0x006C9D90
CHARCODEDESC* TEXTURECACHE::AllocateNewGlyph(GLYPHBITMAPDATA* data) {
    uint32_t count = this->m_textureRows.m_count;

    for (uint32_t i = 0; i < count; i++) {
        CHARCODEDESC* desc = this->m_textureRows.m_data[i].CreateNewDesc(data, i, this->m_theFace->m_cellHeight);

        if (desc) {
            return desc;
        }
    }

    return nullptr;
}

// VERIFY: 248ca280 manual
// OFFSET: 0x006C9F50
void TEXTURECACHE::TextureCallback(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t face, uint32_t level, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    if (command != GxTex_Latch) {
        return;
    }

    TEXTURECACHE* cache = static_cast<TEXTURECACHE*>(userArg);
    const uint32_t S = cache->m_pageSize;
    size_t texelCount = static_cast<size_t>(S) * S;

    if (s_textureData.size() < texelCount) {
        s_textureData.resize(texelCount);
    }

    // MODIFIED: font rendering; the rectangle the device is about to read, cleared, and the cells that touch it
    // (the stock client pasted every glyph of the page into a buffer it never cleared)
    CiRect rect(0, 0, S, S);
    CGxTex* gxTex = cache->m_texture ? cache->m_texture->m_gxTex : nullptr;

    if (gxTex && gxTex->m_updateRect.minX < gxTex->m_updateRect.maxX && gxTex->m_updateRect.minY < gxTex->m_updateRect.maxY) {
        rect = gxTex->m_updateRect;
        rect.minX = rect.minX < 0 ? 0 : rect.minX;
        rect.minY = rect.minY < 0 ? 0 : rect.minY;
        rect.maxX = rect.maxX > static_cast<int32_t>(S) ? static_cast<int32_t>(S) : rect.maxX;
        rect.maxY = rect.maxY > static_cast<int32_t>(S) ? static_cast<int32_t>(S) : rect.maxY;
    }

    for (int32_t y = rect.minY; y < rect.maxY; y++) {
        memset(&s_textureData[static_cast<size_t>(y) * S + rect.minX], 0, sizeof(uint32_t) * (rect.maxX - rect.minX));
    }

    for (uint32_t row = cache->m_textureRows.m_count; row > 0; row--) {
        TEXTURECACHEROW& cacheRow = cache->m_textureRows.m_data[row - 1];

        for (CHARCODEDESC* glyph = cacheRow.m_glyphlist.Head(); glyph; glyph = cacheRow.m_glyphlist.Next(glyph)) {
            if (!cache->m_texture || !cache->m_theFace || !cache->m_theFace->m_cellHeight) {
                continue;
            }

            CGxFont* font = cache->m_theFace;
            int32_t top = static_cast<int32_t>(glyph->m_rowNumber * font->m_cellHeight);
            int32_t bottom = top + static_cast<int32_t>(font->m_cellHeight);
            int32_t left = static_cast<int32_t>(glyph->m_glyphStartPixel);
            int32_t right = left + static_cast<int32_t>(glyph->m_bitmapData.m_glyphCellWidth);

            if (bottom <= rect.minY || top >= rect.maxY || right <= rect.minX || left >= rect.maxX) {
                continue;
            }

            uint32_t* dest = &s_textureData[glyph->m_glyphStartPixel + static_cast<size_t>(glyph->m_rowNumber * font->m_cellHeight) * S];

            if (font->m_fontFlags & 1) {
                if (font->m_fontFlags & 2) {
                    cache->PasteGlyphOutlinedMonochrome(glyph->m_bitmapData, dest);
                } else {
                    cache->PasteGlyphOutlinedAA(glyph->m_bitmapData, dest);
                }
            } else if (font->m_fontFlags & 2) {
                cache->PasteGlyphNonOutlinedMonochrome(glyph->m_bitmapData, dest);
            } else {
                cache->PasteGlyphNonOutlinedAA(glyph->m_bitmapData, dest);
            }
        }
    }

    texelStrideInBytes = S * sizeof(uint32_t);
    texels = s_textureData.data();
}

// VERIFY: 0f656a97 auto
// OFFSET: 0x006CA050
void TEXTURECACHE::CreateTexture(int32_t filter) {
    CGxTexFlags flags(static_cast<EGxTexFilter>(filter != 0), 0, 0, 0, 0, 0, 1, 0, 0, 0);
    // MODIFIED: font rendering; m_pageSize square ARGB8888 (256 x 256 ARGB4444 in the stock client)
    this->m_texture = TextureCreate(this->m_pageSize, this->m_pageSize, GxTex_Argb8888, GxTex_Argb8888, flags, this, TEXTURECACHE::TextureCallback, "GxuFont", 0);
}

// VERIFY: ba005b39 manual
// OFFSET: 0x006CA130
void TEXTURECACHE::Initialize(CGxFont* face, uint32_t cellHeight) {
    this->m_theFace = face;
    // MODIFIED: font rendering; the page grows with the cell (256 in the stock client)
    this->m_pageSize = TEXTURECACHE::PageSizeFor(cellHeight);
    this->m_dirtyRect = CiRect();
    uint32_t rows = this->m_pageSize / cellHeight;
    this->m_textureRows.SetCount(rows);

    for (uint32_t i = 0; i < rows; i++) {
        this->m_textureRows.m_data[i].m_widestFreeSlot = this->m_pageSize;
        this->m_textureRows.m_data[i].m_pageSize = this->m_pageSize;
    }
}

// INLINED
TEXTURECACHE::TEXTURECACHE() {
    this->m_texture = nullptr;
    this->m_theFace = nullptr;
    this->m_anyDirtyGlyphs = 0;
    this->m_pageSize = 256;
}

// INLINED
TEXTURECACHE::~TEXTURECACHE() {
    this->Destroy();
}

// INLINED
void TEXTURECACHE::Destroy() {
    if (this->m_texture) {
        HandleClose(reinterpret_cast<HOBJECT>(this->m_texture));
    }

    this->m_texture = nullptr;
    this->m_textureRows.Clear();
}
