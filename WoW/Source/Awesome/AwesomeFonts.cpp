// Font rendering at high resolutions: the raster glyph size cap and the fallback-font chain (the engine side is in
// Engine/Source/Gxu: GxuFontTextureCache.cpp, GxuFontFallback.*).
//
// fontMaxPixelSize: the stock client rasterised every font at 32 px at most and stretched it to the size on screen, so
// at 1440p and above text came out soft; the cap is now a CVar and the glyph pages grow to fit. MSDF fonts
// (AwesomeMsdf.cpp) draw from distance fields, but still lay text out with these raster metrics, so their spacing
// improves too.
//
// fontFallback: a ';'-separated list of font files a missing glyph is taken from (game paths, or absolute paths with
// %NAME% environment variables expanded), searched in order; empty draws '?' as the stock client did.

#include "WoW/Source/Awesome/AwesomeInternal.h"

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gxu/GxuFont.h"
#include "Engine/Source/Gxu/GxuFontFallback.h"

namespace {
constexpr const char* kDefaultFallback =
    "Fonts\\ARIALN.TTF;Fonts\\FRIZQT__.TTF;"
    "%WINDIR%\\Fonts\\segoeui.ttf;%WINDIR%\\Fonts\\seguisym.ttf;%WINDIR%\\Fonts\\msyh.ttc;%WINDIR%\\Fonts\\malgun.ttf";

void OnMaxPixelSize(AwesomeCVar* cvar, bool changed) {
    g_gxuFontMaxPixelSize = static_cast<uint32_t>(cvar->m_intValue);

    // the fonts made before this re-rasterise; at registration none exist yet
    if (changed) {
        GxuFontRefreshSizes();
    }
}

AwesomeCVar s_cvMaxPixelSize = AwesomeCVar::Int("fontMaxPixelSize", 128, 32, 256, OnMaxPixelSize);

bool OnFallback(CVar* cvar, const char* oldValue, const char* newValue, void* arg) {
    GxuFontSetFallbackList(newValue);
    return true;
}
}  // namespace

void AwesomeFontsRegisterCVars() {
    s_cvMaxPixelSize.Register();

    CVar* fallback = CVar::Register("fontFallback", "Fonts a missing glyph is taken from, ';'-separated", 0, kDefaultFallback, &OnFallback, CATEGORY_DEBUG, false, nullptr, false);

    if (fallback && fallback->m_stringValue.m_str) {
        GxuFontSetFallbackList(fallback->m_stringValue.m_str);
    }
}
