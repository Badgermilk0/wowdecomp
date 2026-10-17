#ifndef ENGINE_SOURCE_GXU_GXUFONTMSDF_H
#define ENGINE_SOURCE_GXU_GXUFONTMSDF_H

// MODIFIED: awesome_wotlk MSDF font rendering (multi-channel signed distance fields), ported natively.
// WoW/Source/Awesome/Msdf fills these in from GxuFontInitialize's start callback; while a hook is null the font code
// behaves like the stock client.

#include <cstddef>
#include <cstdint>
#include <freetype/freetype.h>

struct BATCHEDRENDERFONTDESC;
struct CGxFont;
struct CGxString;

struct GxuFontMsdfHooks {
    // FontFaceGetHandle loaded a face; data stays valid until faceDestroyed
    void (*m_faceCreated)(FT_Face face, const void* data, uint32_t size);
    // ~FACEDATA, before the face and its data are freed
    void (*m_faceDestroyed)(FT_Face face);
    // the face draws through the MSDF atlas (monochrome fonts never do)
    int32_t (*m_isMsdfFace)(FT_Face face);
    // CGxString::CheckGeometry, first: drops the geometry when the MSDF atlas evicted glyphs since it was built
    void (*m_checkGeometry)(CGxString* string);
    // in place of BATCHEDRENDERFONTDESC::RenderBatch; nonzero when it drew the batch
    int32_t (*m_renderBatch)(BATCHEDRENDERFONTDESC* desc);
    // GxuFontShutdown, before the FreeType library goes
    void (*m_shutdown)();
};

extern GxuFontMsdfHooks g_gxuFontMsdf;
// called first thing in GxuFontInitialize, where the device exists: installs g_gxuFontMsdf
extern void (*g_gxuFontInitializeCallback)();
// quads the font index buffer holds; 512 in the stock client
extern uint32_t g_gxuFontIndexQuads;

// font is drawn through MSDF
bool GxuFontIsMsdf(CGxFont* font);

#endif
