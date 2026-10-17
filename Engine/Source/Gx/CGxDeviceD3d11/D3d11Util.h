#ifndef ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11UTIL_H
#define ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11UTIL_H

// MODIFIED: D3D11 device, helpers its files share

#include <cstdint>

namespace GxD3d11 {

template <class T>
inline void SafeRelease(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

// a D3DCOLOR (ARGB in a uint32) as floats, rgba
inline void ColorToFloat4(uint32_t argb, float* out) {
    out[0] = static_cast<float>((argb >> 16) & 0xFF) * (1.0f / 255.0f);
    out[1] = static_cast<float>((argb >> 8) & 0xFF) * (1.0f / 255.0f);
    out[2] = static_cast<float>(argb & 0xFF) * (1.0f / 255.0f);
    out[3] = static_cast<float>((argb >> 24) & 0xFF) * (1.0f / 255.0f);
}

}

#endif
