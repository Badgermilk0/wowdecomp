// Smoother sky (not from awesome_wotlk): the Light.dbc sky bands drawn without stripes, and light changes faded in.
//
// skySmooth: the stock dome (DayNight::DNSky) colours six rings of 24 vertices and Gouraud shading draws each band as a
//   straight ramp, kinked at every ring; the rings near the horizon are a few degrees apart and show as stripes. The
//   smooth dome takes the same 6 x 24 colours (DNSky::SetColors, sun highlight and lightning flash included) and draws
//   them on 96 columns and 51 rows, a monotone cubic across the rings and a periodic Catmull-Rom around them
//   (WorldClient/SkyModel.h). Works on every device.
// skyTransition: the zone's light (DayNight's CurrentLight: the sky, fog, ambient, sun, cloud and water colours and the
//   fog distances) eases toward each new value with this time constant in seconds, so a light boundary, a change of
//   area light or an override does not snap. Snaps on a map change, a jump of the camera (a teleport), entering or
//   leaving a liquid, and after a hitch of more than a second. 0 = stock.
// skyDither: the HDR tonemap adds a triangular noise of this many 8-bit steps before the frame quantizes, so smooth
//   gradients (the sky, fog) do not band. Needs hdr (Awesome/AwesomeLighting.cpp).

#include <cmath>
#include <cstring>
#include <vector>
#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/SkyModel.h"

static AwesomeCVar s_cvSmooth = AwesomeCVar::Int("skySmooth", 1, 0, 1);
static AwesomeCVar s_cvTransition = AwesomeCVar::Float("skyTransition", 1.5f, 0.0f, 10.0f, nullptr, "%.1f");
static AwesomeCVar s_cvDither = AwesomeCVar::Float("skyDither", 1.0f, 0.0f, 2.0f);

static AwesomeCVar* const s_cvars[] = {
    &s_cvSmooth, &s_cvTransition, &s_cvDither,
};

// --- the smooth dome ---------------------------------------------------------------------------------------------

static const uint32_t s_columns = 96;
static const uint32_t s_maxRows = 64;

static std::vector<C3Vector> s_domePositions;
static std::vector<CImVector> s_domeColors;
static std::vector<uint16_t> s_domeIndices;
static float s_domeRows[s_maxRows];
static uint32_t s_domeRowCount = 0;
static CImVector s_lastControl[122];
static bool s_lastControlValid = false;

static void BuildDome() {
    s_domeRowCount = SkyModel::DomeRows(s_domeRows, s_maxRows);
    uint32_t stride = s_columns + 1;
    s_domePositions.resize(s_domeRowCount * stride);
    s_domeColors.resize(s_domeRowCount * stride);
    s_domeIndices.clear();

    for (uint32_t row = 0; row < s_domeRowCount; row++) {
        for (uint32_t col = 0; col <= s_columns; col++) {
            float xyz[3];
            SkyModel::DomePoint(s_domeRows[row], static_cast<float>(col % s_columns) / s_columns, xyz);
            s_domePositions[row * stride + col] = C3Vector(xyz[0], xyz[1], xyz[2]);
        }
    }

    for (uint32_t row = 0; row + 1 < s_domeRowCount; row++) {
        for (uint32_t col = 0; col < s_columns; col++) {
            uint16_t a = static_cast<uint16_t>(row * stride + col);
            uint16_t b = static_cast<uint16_t>(a + 1);
            uint16_t c = static_cast<uint16_t>(a + stride);
            uint16_t d = static_cast<uint16_t>(c + 1);
            s_domeIndices.push_back(a);
            s_domeIndices.push_back(c);
            s_domeIndices.push_back(b);
            s_domeIndices.push_back(b);
            s_domeIndices.push_back(c);
            s_domeIndices.push_back(d);
        }
    }
}

static uint8_t ToByte(float value) {
    value = value < 0.0f ? 0.0f : (value > 255.0f ? 255.0f : value);
    return static_cast<uint8_t>(value + 0.5f);
}

// the dense colours from the stock 6 x 24 (DNSky::SetColors' layout: the zenith, rings 1 to 4, the horizon ring of fog,
// the pole below)
static void ColorDome(const CImVector* control) {
    const uint32_t rings = SkyModel::RING_COUNT;
    const uint32_t segments = SkyModel::RING_SEGMENTS;
    float x[rings + 1];
    float ringChannel[rings][segments];
    // each ring around at each column, per channel
    static float ringAt[s_columns + 1][rings + 1][4];

    for (uint32_t r = 0; r < rings; r++) {
        x[r] = SkyModel::s_ringTheta[r];
    }

    x[rings] = 1.0f;

    for (uint32_t channel = 0; channel < 4; channel++) {
        for (uint32_t r = 0; r < rings; r++) {
            for (uint32_t a = 0; a < segments; a++) {
                const CImVector* c = r == 0 ? &control[0] : (r < 5 ? &control[1 + (r - 1) * segments + a] : &control[97 + a]);
                ringChannel[r][a] = static_cast<float>(channel == 0 ? c->b : (channel == 1 ? c->g : (channel == 2 ? c->r : c->a)));
            }
        }

        const CImVector& bottom = control[121];
        float below = static_cast<float>(channel == 0 ? bottom.b : (channel == 1 ? bottom.g : (channel == 2 ? bottom.r : bottom.a)));

        for (uint32_t col = 0; col <= s_columns; col++) {
            float u = static_cast<float>(col % s_columns) * segments / s_columns;

            for (uint32_t r = 0; r < rings; r++) {
                ringAt[col][r][channel] = SkyModel::PeriodicCatmullRom(ringChannel[r], segments, u);
            }

            ringAt[col][rings][channel] = below;
        }
    }

    uint32_t stride = s_columns + 1;

    for (uint32_t row = 0; row < s_domeRowCount; row++) {
        float s = s_domeRows[row];

        for (uint32_t col = 0; col <= s_columns; col++) {
            float values[4];

            for (uint32_t channel = 0; channel < 4; channel++) {
                float y[rings + 1];

                for (uint32_t r = 0; r <= rings; r++) {
                    y[r] = ringAt[col][r][channel];
                }

                values[channel] = SkyModel::MonotoneCubic(x, y, rings + 1, s);
            }

            CImVector& out = s_domeColors[row * stride + col];
            out.b = ToByte(values[0]);
            out.g = ToByte(values[1]);
            out.r = ToByte(values[2]);
            out.a = ToByte(values[3]);
        }
    }
}

bool AwesomeSkyDraw(const DayNight::DNSky* sky) {
    // the stock dome's layout (DNSky::GenSphere): 1 + 5 x 24 + 1 vertices
    if (!s_cvSmooth.m_intValue || !sky || sky->m_nVerts != 122 || sky->m_sphThetaTess != 24 || sky->m_clrVerts.m_count < 122) {
        return false;
    }

    if (!s_domeRowCount) {
        BuildDome();
    }

    const CImVector* control = sky->m_clrVerts.m_data;

    // the colours change with the time of day, slowly: recoloured only when they do
    if (!s_lastControlValid || memcmp(s_lastControl, control, sizeof(s_lastControl))) {
        ColorDome(control);
        memcpy(s_lastControl, control, sizeof(s_lastControl));
        s_lastControlValid = true;
    }

    GxPrimVertexPtr(static_cast<uint32_t>(s_domePositions.size()), s_domePositions.data(), sizeof(C3Vector), nullptr, 0, s_domeColors.data(), sizeof(CImVector), nullptr, 0, nullptr, 0);
    GxPrimIndexPtr(static_cast<uint32_t>(s_domeIndices.size()), s_domeIndices.data());

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_count = static_cast<uint32_t>(s_domeIndices.size());
    batch.m_minIndex = 0;
    batch.m_maxIndex = static_cast<uint16_t>(s_domePositions.size() - 1);
    g_theGxDevicePtr->Draw(&batch, 1);
    return true;
}

// --- the light, eased --------------------------------------------------------------------------------------------

struct SmoothState {
    bool m_valid = false;
    float m_colors[18][4];
    float m_floats[14];
    uint32_t m_lastMs = 0;
    int32_t m_mapId = -1;
    int32_t m_liquid = 0;
    C3Vector m_camera;
};

static SmoothState s_smooth;

static CImVector* ColorField(DayNight::CurrentLight& bands, uint32_t i) {
    if (i == 0) {
        return &bands.m_ambient;
    }

    if (i == 1) {
        return &bands.m_diffuse;
    }

    if (i == 2) {
        return &bands.m_shadowOpacity;
    }

    if (i < 9) {
        return &bands.m_skyColors[i - 3];
    }

    if (i < 14) {
        return &bands.m_sunCloudColors[i - 9];
    }

    return &bands.m_waterColors[i - 14];
}

static float* FloatField(DayNight::CurrentLight& bands, uint32_t i) {
    float* fields[14] = {
        &bands.m_fogEnd, &bands.m_fogStartMul, &bands.m_fogRate, &bands.m_highlightSky, &bands.m_glow, &bands.m_floatBand2,
        &bands.m_cloudDensity, &bands.m_floatBand4, &bands.m_floatBand5, &bands.m_waterShallowAlpha, &bands.m_waterDeepAlpha,
        &bands.m_oceanShallowAlpha, &bands.m_oceanDeepAlpha, &bands.m_cloudWeight,
    };

    return fields[i];
}

void AwesomeSkySmoothBands(DayNight::CurrentLight* bands, int32_t liquidStatus) {
    uint32_t now = OsGetAsyncTimeMs();
    float dt = s_smooth.m_lastMs ? (now - s_smooth.m_lastMs) * 0.001f : 0.0f;
    s_smooth.m_lastMs = now;

    if (!bands) {
        return;
    }

    DayNight::DNInfo* info = DayNight::GetInfo();
    C3Vector camera = info ? info->m_cameraPos : C3Vector(0.0f, 0.0f, 0.0f);
    float dx = camera.x - s_smooth.m_camera.x;
    float dy = camera.y - s_smooth.m_camera.y;
    float dz = camera.z - s_smooth.m_camera.z;
    bool jumped = dx * dx + dy * dy + dz * dz > 80.0f * 80.0f;
    s_smooth.m_camera = camera;

    // the stock light: no lights on this map (DayNight fills 0xFF), or no easing asked for
    bool snap = !s_smooth.m_valid || s_cvTransition.m_floatValue <= 0.0f || dt > 1.0f || jumped || s_smooth.m_mapId != DayNight::s_mapId || s_smooth.m_liquid != liquidStatus || bands->m_fogEnd > 1.0e6f || s_smooth.m_floats[0] > 1.0e6f;
    s_smooth.m_mapId = DayNight::s_mapId;
    s_smooth.m_liquid = liquidStatus;
    float k = snap ? 1.0f : SkyModel::SmoothFactor(dt, s_cvTransition.m_floatValue);

    for (uint32_t i = 0; i < 18; i++) {
        CImVector* c = ColorField(*bands, i);
        float target[4] = { static_cast<float>(c->b), static_cast<float>(c->g), static_cast<float>(c->r), static_cast<float>(c->a) };
        float* state = s_smooth.m_colors[i];

        for (uint32_t j = 0; j < 4; j++) {
            state[j] += (target[j] - state[j]) * k;
        }

        c->b = ToByte(state[0]);
        c->g = ToByte(state[1]);
        c->r = ToByte(state[2]);
        c->a = ToByte(state[3]);
    }

    for (uint32_t i = 0; i < 14; i++) {
        float* f = FloatField(*bands, i);
        s_smooth.m_floats[i] += (*f - s_smooth.m_floats[i]) * k;
        *f = s_smooth.m_floats[i];
    }

    s_smooth.m_valid = true;
}

float AwesomeSkyDither() {
    return s_cvDither.m_floatValue;
}

void AwesomeSkyRegisterCVars() {
    for (AwesomeCVar* cvar : s_cvars) {
        cvar->Register();
    }
}

void AwesomeSkyOnEnterWorld() {
    s_smooth.m_valid = false;
    s_lastControlValid = false;
}
