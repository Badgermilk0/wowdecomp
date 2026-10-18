#ifndef ENGINE_SOURCE_GXU_GXUFONTFALLBACK_H
#define ENGINE_SOURCE_GXU_GXUFONTFALLBACK_H

// MODIFIED: font rendering. A glyph the font's own face lacks is taken from the first face of a fallback chain that
// has it; the stock client drew '?' in its place. The chain is a ';'-separated list of font files: game paths
// (Fonts\ARIALN.TTF) load through SFile and share the engine's face, absolute paths (%WINDIR%\Fonts\seguisym.ttf,
// with %NAME% expanded from the environment) load from disk. Each face loads the first time a lookup reaches it.
// WoW/Source/Awesome/AwesomeFonts.cpp sets the list from the fontFallback CVar.

#include <cstdint>
#include <freetype/freetype.h>

// replaces the chain; faces already loaded are released
void GxuFontSetFallbackList(const char* list);
// the first fallback face other than primary that maps code; load: open faces the lookup reaches (the raster path,
// main thread), otherwise only faces already open are searched (the MSDF generator)
FT_Face GxuFontFallbackFind(FT_Face primary, uint32_t code, bool load);
// GxuFontShutdown, while the FreeType library and the MSDF hooks still exist
void GxuFontFallbackShutdown();

#endif
