#include "WoW/Source/Awesome/Msdf/MsdfFreetype.h"
#include <freetype/ftoutln.h>

// Adapted from msdfgen 1.13 ext/import-font.cpp (MIT, Viktor Chlumsky) for FreeType 2.0: its outline callbacks take
// non-const vectors, the kerning and variable-font parts are left out, and faces select the Unicode charmap like the
// client's FontFaceGetHandle does.

namespace msdfgen {

class FreetypeHandle {
public:
    FT_Library library;
};

class FontHandle {
public:
    FT_Face face;
    bool ownership;
};

struct FtContext {
    double scale;
    Point2 position;
    Shape* shape;
    Contour* contour;
};

static Point2 ftPoint2(const FT_Vector& vector, double scale) {
    return Point2(scale * vector.x, scale * vector.y);
}

static int ftMoveTo(FT_Vector* to, void* user) {
    FtContext* context = reinterpret_cast<FtContext*>(user);

    if (!(context->contour && context->contour->edges.empty())) {
        context->contour = &context->shape->addContour();
    }

    context->position = ftPoint2(*to, context->scale);
    return 0;
}

static int ftLineTo(FT_Vector* to, void* user) {
    FtContext* context = reinterpret_cast<FtContext*>(user);
    Point2 endpoint = ftPoint2(*to, context->scale);

    if (endpoint != context->position) {
        context->contour->addEdge(EdgeHolder(context->position, endpoint));
        context->position = endpoint;
    }

    return 0;
}

static int ftConicTo(FT_Vector* control, FT_Vector* to, void* user) {
    FtContext* context = reinterpret_cast<FtContext*>(user);
    Point2 endpoint = ftPoint2(*to, context->scale);

    if (endpoint != context->position) {
        context->contour->addEdge(EdgeHolder(context->position, ftPoint2(*control, context->scale), endpoint));
        context->position = endpoint;
    }

    return 0;
}

static int ftCubicTo(FT_Vector* control1, FT_Vector* control2, FT_Vector* to, void* user) {
    FtContext* context = reinterpret_cast<FtContext*>(user);
    Point2 endpoint = ftPoint2(*to, context->scale);

    if (endpoint != context->position || crossProduct(ftPoint2(*control1, context->scale) - endpoint, ftPoint2(*control2, context->scale) - endpoint)) {
        context->contour->addEdge(EdgeHolder(context->position, ftPoint2(*control1, context->scale), ftPoint2(*control2, context->scale), endpoint));
        context->position = endpoint;
    }

    return 0;
}

static double getFontCoordinateScale(const FT_Face& face, FontCoordinateScaling coordinateScaling) {
    switch (coordinateScaling) {
        case FONT_SCALING_NONE:
            return 1;
        case FONT_SCALING_EM_NORMALIZED:
            return 1. / (face->units_per_EM ? face->units_per_EM : 1);
        case FONT_SCALING_LEGACY:
            return MSDFGEN_LEGACY_FONT_COORDINATE_SCALE;
    }

    return 1;
}

GlyphIndex::GlyphIndex(unsigned index) : index(index) {
}

unsigned GlyphIndex::getIndex() const {
    return index;
}

FreetypeHandle* initializeFreetype() {
    FreetypeHandle* handle = new FreetypeHandle;

    if (FT_Init_FreeType(&handle->library)) {
        delete handle;
        return nullptr;
    }

    return handle;
}

void deinitializeFreetype(FreetypeHandle* library) {
    FT_Done_FreeType(library->library);
    delete library;
}

FontHandle* adoptFreetypeFont(FT_Face ftFace) {
    FontHandle* handle = new FontHandle;
    handle->face = ftFace;
    handle->ownership = false;
    return handle;
}

FT_Error readFreetypeOutline(Shape& output, FT_Outline* outline, double scale) {
    output.contours.clear();
    output.setYAxisOrientation(Y_UPWARD);

    FtContext context = {};
    context.scale = scale;
    context.shape = &output;

    FT_Outline_Funcs ftFunctions;
    ftFunctions.move_to = &ftMoveTo;
    ftFunctions.line_to = &ftLineTo;
    ftFunctions.conic_to = &ftConicTo;
    ftFunctions.cubic_to = &ftCubicTo;
    ftFunctions.shift = 0;
    ftFunctions.delta = 0;

    FT_Error error = FT_Outline_Decompose(outline, &ftFunctions, &context);

    if (!output.contours.empty() && output.contours.back().edges.empty()) {
        output.contours.pop_back();
    }

    return error;
}

FontHandle* loadFontData(FreetypeHandle* library, const byte* data, int length) {
    if (!library) {
        return nullptr;
    }

    FontHandle* handle = new FontHandle;

    if (FT_New_Memory_Face(library->library, const_cast<FT_Byte*>(data), length, 0, &handle->face)) {
        delete handle;
        return nullptr;
    }

    FT_Select_Charmap(handle->face, ft_encoding_unicode);
    handle->ownership = true;
    return handle;
}

void destroyFont(FontHandle* font) {
    if (font->ownership) {
        FT_Done_Face(font->face);
    }

    delete font;
}

bool getFontMetrics(FontMetrics& metrics, FontHandle* font, FontCoordinateScaling coordinateScaling) {
    double scale = getFontCoordinateScale(font->face, coordinateScaling);
    metrics.emSize = scale * font->face->units_per_EM;
    metrics.ascenderY = scale * font->face->ascender;
    metrics.descenderY = scale * font->face->descender;
    metrics.lineHeight = scale * font->face->height;
    metrics.underlineY = scale * font->face->underline_position;
    metrics.underlineThickness = scale * font->face->underline_thickness;
    return true;
}

bool getGlyphIndex(GlyphIndex& glyphIndex, FontHandle* font, unicode_t unicode) {
    glyphIndex = GlyphIndex(FT_Get_Char_Index(font->face, unicode));
    return glyphIndex.getIndex() != 0;
}

bool loadGlyph(Shape& output, FontHandle* font, GlyphIndex glyphIndex, FontCoordinateScaling coordinateScaling, double* outAdvance) {
    if (!font) {
        return false;
    }

    if (FT_Load_Glyph(font->face, glyphIndex.getIndex(), FT_LOAD_NO_SCALE)) {
        return false;
    }

    // embedded bitmaps have no outline to read
    if (font->face->glyph->format != ft_glyph_format_outline) {
        return false;
    }

    double scale = getFontCoordinateScale(font->face, coordinateScaling);

    if (outAdvance) {
        *outAdvance = scale * font->face->glyph->advance.x;
    }

    return !readFreetypeOutline(output, &font->face->glyph->outline, scale);
}

bool loadGlyph(Shape& output, FontHandle* font, GlyphIndex glyphIndex, double* outAdvance) {
    return loadGlyph(output, font, glyphIndex, FONT_SCALING_LEGACY, outAdvance);
}

FT_Face getFreetypeFace(FontHandle* font) {
    return font ? font->face : nullptr;
}

}  // namespace msdfgen
