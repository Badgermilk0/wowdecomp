#include <cmath>
#include "Engine/Source/Base/Coordinate.h"

// STATIC: 0x00AC0CB0
static float s_aspectRatio = 4.0f / 3.0f;

// STATIC: 0x00AC0CB4
static float s_ddcWidthScale = 0.8f;

// STATIC: 0x00AC0CB8
static float s_ddcHeightScale = 0.6f;

// STATIC: 0x00AC0CBC
static float s_aspectCompensation = 1.0f;

// MODIFIED: live aspect changes. Stock DDC spans the screen diagonal, so a UI unit's size in DDC depends on the aspect
// and every frame's stored size goes stale when it changes (stock reloads the UI). With a fixed height, a UI unit is
// always 0.6 / 768 DDC and only the screen's width in DDC follows the aspect; at 4:3 both are the stock values.
// Chosen once, before the first aspect is set: switching later would leave the frames already laid out on the old scale.
static bool s_fixedHeight = false;
static bool s_aspectSet = false;

void CoordinateSetFixedHeight(bool fixedHeight) {
    if (!s_aspectSet) {
        s_fixedHeight = fixedHeight;
    }
}

bool CoordinateIsFixedHeight() {
    return s_fixedHeight;
}

// VERIFY: a3f58d50 manual
// OFFSET: 0x0047BF90
void CoordinateSetAspectRatio(float aspect) {
    s_aspectSet = true;
    s_aspectRatio = aspect;
    s_aspectCompensation = aspect * 0.75f;

    if (s_fixedHeight) {
        s_ddcHeightScale = 0.6f;
        s_ddcWidthScale = aspect * 0.6f;
        return;
    }

    float scale = 1.0f / sqrt(aspect * aspect + 1.0f);
    s_ddcHeightScale = scale;
    s_ddcWidthScale = aspect * scale;
}

// VERIFY: 61e66992 auto
// OFFSET: 0x0047BFD0
float CoordinateGetAspectRatio() {
    return s_aspectRatio;
}

// VERIFY: 33d3d8ec auto
// OFFSET: 0x0047BFE0
float CoordinateGetAspectCompensation() {
    return s_aspectCompensation;
}

// VERIFY: 219e50ed auto
// OFFSET: 0x0047BFF0
void NDCToDDC(float ndcx, float ndcy, float* ddcx, float* ddcy) {
    if (ddcx) {
        *ddcx = s_ddcWidthScale * ndcx;
    }

    if (ddcy) {
        *ddcy = s_ddcHeightScale * ndcy;
    }
}

// VERIFY: 55a28a00 auto
// OFFSET: 0x0047C020
void DDCToNDC(float ddcx, float ddcy, float* ndcx, float* ndcy) {
    if (ndcx) {
        *ndcx = ddcx / s_ddcWidthScale;
    }

    if (ndcy) {
        *ndcy = ddcy / s_ddcHeightScale;
    }
}

// VERIFY: 7b40f308 auto
// OFFSET: 0x0047C050
float DDCToNDCWidth(float ddcx) {
    return ddcx / s_ddcWidthScale;
}

// VERIFY: aa52e55e auto
// OFFSET: 0x0047C060
float DDCToNDCHeight(float ddcy) {
    return ddcy / s_ddcHeightScale;
}

// VERIFY: e46d6d9e auto
// OFFSET: 0x0047C070
float NDCToDDCWidth(float ndcx) {
    return s_ddcWidthScale * ndcx;
}

// VERIFY: 65cc685c auto
// OFFSET: 0x0047C080
float NDCToDDCHeight(float ndcy) {
    return s_ddcHeightScale * ndcy;
}
