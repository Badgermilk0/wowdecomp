#include "Screen.h"
#include <cstdint>
#include <cstdio>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Tempest/CRect.h"

// VERIFY: a937de73 auto
// OFFSET: 0x0050F7C0
float Screen::GetAspectRatio() {
    CVar* widescreen = CVar::Lookup("widescreen");
    CVar* resolution = CVar::Lookup("gxResolution");
    int32_t width = 4;
    int32_t height = 3;
    char separator;

    if (widescreen && widescreen->m_intValue && resolution) {
        // MODIFIED: the aspect of the window as it really is. gxResolution only names the requested size: a borderless
        // or D3D11 full-screen window takes its monitor's size, and a resized or snapped window keeps its own, so the
        // UI and the world were stretched whenever the two differed
        if (g_theGxDevicePtr) {
            CRect rect = { 0.0f, 0.0f, 0.0f, 0.0f };
            g_theGxDevicePtr->CapsWindowSize(rect);
            float w = rect.maxX - rect.minX;
            float h = rect.maxY - rect.minY;

            if (w >= 1.0f && h >= 1.0f) {
                return w / h;
            }
        }

        sscanf(resolution->m_stringValue.m_str, "%d%c%d", &width, &separator, &height);
    }

    return static_cast<float>(width) / height;
}
