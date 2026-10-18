#ifndef ENGINE_SOURCE_GXU_IGXUFONTGLYPH_H
#define ENGINE_SOURCE_GXU_IGXUFONTGLYPH_H

#include <cstddef>
#include <cstdint>
#include <freetype/freetype.h>

struct GLYPHBITMAPDATA;

// MODIFIED: font rendering; fontFace is the font's own face when face is a fallback for the glyph
bool IGxuFontGlyphRenderGlyph(FT_Face face, uint32_t pixelSize, uint32_t code, uint32_t baseline, GLYPHBITMAPDATA* dataPtr, int32_t monochrome, uint32_t padding, FT_Face fontFace = nullptr);

#endif
