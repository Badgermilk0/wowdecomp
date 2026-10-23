#include "WoW/Source/WorldClient/SkyModel.h"
#include <cmath>

// MODIFIED: smooth sky, see SkyModel.h

namespace SkyModel {

// DayNight.cpp s_skyThetaTable, without the pole below
const float s_ringTheta[RING_COUNT] = { 0.0f, 0.17f, 0.2f, 0.23f, 0.24f, 0.25f };

const float s_domeLower = 0.70710678f;

static const float s_pi = 3.14159265f;

float MonotoneCubic(const float* x, const float* y, uint32_t count, float t) {
    if (count == 0) {
        return 0.0f;
    }

    if (count == 1 || t <= x[0]) {
        return y[0];
    }

    if (t >= x[count - 1]) {
        return y[count - 1];
    }

    uint32_t k = 0;

    while (k + 2 < count && t > x[k + 1]) {
        k++;
    }

    // the secants around the interval, and the tangents Fritsch-Carlson allows at its ends
    auto secant = [&](uint32_t i) {
        float h = x[i + 1] - x[i];
        return h > 0.0f ? (y[i + 1] - y[i]) / h : 0.0f;
    };

    auto tangent = [&](uint32_t i) {
        if (i == 0) {
            return secant(0);
        }

        if (i == count - 1) {
            return secant(count - 2);
        }

        float a = secant(i - 1);
        float b = secant(i);

        // a local extremum or a flat side: flat here, so the curve does not overshoot
        if (a * b <= 0.0f) {
            return 0.0f;
        }

        // the weighted harmonic mean (Fritsch-Butland), monotone by construction
        float h0 = x[i] - x[i - 1];
        float h1 = x[i + 1] - x[i];
        float w0 = 2.0f * h1 + h0;
        float w1 = h1 + 2.0f * h0;
        return (w0 + w1) / (w0 / a + w1 / b);
    };

    float h = x[k + 1] - x[k];

    if (h <= 0.0f) {
        return y[k + 1];
    }

    float s = (t - x[k]) / h;
    float m0 = tangent(k) * h;
    float m1 = tangent(k + 1) * h;
    float s2 = s * s;
    float s3 = s2 * s;
    return (2.0f * s3 - 3.0f * s2 + 1.0f) * y[k] + (s3 - 2.0f * s2 + s) * m0 + (-2.0f * s3 + 3.0f * s2) * y[k + 1] + (s3 - s2) * m1;
}

float PeriodicCatmullRom(const float* y, uint32_t count, float u) {
    if (count == 0) {
        return 0.0f;
    }

    float wrapped = fmodf(u, static_cast<float>(count));

    if (wrapped < 0.0f) {
        wrapped += static_cast<float>(count);
    }

    uint32_t i = static_cast<uint32_t>(wrapped);
    i = i >= count ? count - 1 : i;
    float s = wrapped - static_cast<float>(i);
    float p0 = y[(i + count - 1) % count];
    float p1 = y[i];
    float p2 = y[(i + 1) % count];
    float p3 = y[(i + 2) % count];
    float s2 = s * s;
    float s3 = s2 * s;
    float value = 0.5f * (2.0f * p1 + (-p0 + p2) * s + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * s2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * s3);

    // a Catmull-Rom can overshoot its neighbours; a colour stays between the two it joins
    float lo = p1 < p2 ? p1 : p2;
    float hi = p1 < p2 ? p2 : p1;
    return value < lo ? lo : (value > hi ? hi : value);
}

float SmoothFactor(float dt, float tau) {
    if (tau <= 0.0f || dt <= 0.0f) {
        return tau <= 0.0f ? 1.0f : 0.0f;
    }

    return 1.0f - expf(-dt / tau);
}

void DomePoint(float s, float u, float* xyz) {
    float theta = s * s_pi;
    float angle = u * 2.0f * s_pi;
    float sinTheta = sinf(theta);
    // DNSky::GenSphere: x = sin(angle) sin(theta), y = sin(theta) cos(angle), z = cos(theta) - cos(45 degrees)
    xyz[0] = sinf(angle) * sinTheta;
    xyz[1] = sinTheta * cosf(angle);
    xyz[2] = cosf(theta) - s_domeLower;
}

float DomeThetaForDirection(float x, float y, float z) {
    float length = sqrtf(x * x + y * y + z * z);

    if (length <= 0.0f) {
        return 0.0f;
    }

    float dz = z / length;
    // the eye at the origin, the dome's centre at (0, 0, -lower), radius 1: where the ray leaves the sphere
    float dc = -s_domeLower * dz;
    float disc = dc * dc - s_domeLower * s_domeLower + 1.0f;
    float t = dc + sqrtf(disc > 0.0f ? disc : 0.0f);
    float c = t * dz + s_domeLower;
    c = c < -1.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
    return acosf(c) / s_pi;
}

uint32_t DomeRows(float* rows, uint32_t capacity) {
    uint32_t n = 0;

    auto add = [&](float s) {
        if (n < capacity) {
            rows[n++] = s;
        }
    };

    // the zenith to the first ring, then the bands at the horizon densely, then the ground below (one colour)
    for (uint32_t i = 0; i < 16; i++) {
        add(0.17f * i / 16.0f);
    }

    for (uint32_t i = 0; i < 32; i++) {
        add(0.17f + 0.08f * i / 32.0f);
    }

    add(0.25f);
    add(0.5f);
    add(1.0f);
    return n;
}

}
