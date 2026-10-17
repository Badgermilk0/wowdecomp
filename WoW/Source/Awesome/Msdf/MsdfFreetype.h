#pragma once

// msdfgen's font import (msdfgen-ext, ext/import-font.h) for the FreeType 2.0 this client links, so the MSDF code
// keeps the client's font library instead of bringing a second FreeType. Same names and defaults as msdfgen 1.13:
// the default coordinate scaling is FONT_SCALING_LEGACY (font units / 64). Only what the MSDF code uses.

#include <msdfgen.h>
#include <freetype/freetype.h>

namespace msdfgen {

#define MSDFGEN_LEGACY_FONT_COORDINATE_SCALE (1 / 64.)

typedef unsigned unicode_t;

class FreetypeHandle;
class FontHandle;

class GlyphIndex {
public:
    explicit GlyphIndex(unsigned index = 0);
    unsigned getIndex() const;

private:
    unsigned index;
};

/// Global metrics of a typeface (in font units).
struct FontMetrics {
    double emSize;
    double ascenderY, descenderY;
    double lineHeight;
    double underlineY, underlineThickness;
};

enum FontCoordinateScaling {
    FONT_SCALING_NONE,
    FONT_SCALING_EM_NORMALIZED,
    FONT_SCALING_LEGACY
};

/// A FreeType library of its own (FreeType objects are not shared between threads).
FreetypeHandle* initializeFreetype();
void deinitializeFreetype(FreetypeHandle* library);
/// A handle over a face the caller owns; destroyFont leaves the face alone.
FontHandle* adoptFreetypeFont(FT_Face ftFace);
FT_Error readFreetypeOutline(Shape& output, FT_Outline* outline, double scale = MSDFGEN_LEGACY_FONT_COORDINATE_SCALE);
/// A face over data the caller keeps alive until destroyFont.
FontHandle* loadFontData(FreetypeHandle* library, const byte* data, int length);
void destroyFont(FontHandle* font);
bool getFontMetrics(FontMetrics& metrics, FontHandle* font, FontCoordinateScaling coordinateScaling = FONT_SCALING_LEGACY);
bool getGlyphIndex(GlyphIndex& glyphIndex, FontHandle* font, unicode_t unicode);
bool loadGlyph(Shape& output, FontHandle* font, GlyphIndex glyphIndex, FontCoordinateScaling coordinateScaling, double* outAdvance = nullptr);
bool loadGlyph(Shape& output, FontHandle* font, GlyphIndex glyphIndex, double* outAdvance = nullptr);
/// The face behind a handle.
FT_Face getFreetypeFace(FontHandle* font);

}  // namespace msdfgen
