#include "Engine/Source/Gxu/GxuFontMsdf.h"
#include "Engine/Source/Gxu/GxuFontFace.h"
#include "Engine/Source/Gxu/GxuFontFont.h"

// MODIFIED: awesome_wotlk MSDF font rendering, see GxuFontMsdf.h

GxuFontMsdfHooks g_gxuFontMsdf;
void (*g_gxuFontInitializeCallback)();
uint32_t g_gxuFontIndexQuads = 512;

bool GxuFontIsMsdf(CGxFont* font) {
    if (!g_gxuFontMsdf.m_isMsdfFace || !font || !font->m_fontFaceHandle || (font->m_fontFlags & 2)) {
        return false;
    }

    return g_gxuFontMsdf.m_isMsdfFace(FontFaceGetFace(font->m_fontFaceHandle)) != 0;
}
