#ifndef WOW_SOURCE_WORLDCLIENT_SKYMODEL_H
#define WOW_SOURCE_WORLDCLIENT_SKYMODEL_H

// MODIFIED: smooth sky (Awesome/AwesomeSky.cpp). The arithmetic, free of the engine, unit-tested by Tools/WaterTests.
//
// The stock sky dome (DayNight::DNSky) is a unit sphere lowered by cos 45 degrees, so that theta = pi / 4 is the
// horizon, with six rings of 24 vertex colours at theta / pi = 0, 0.17, 0.2, 0.23, 0.24, 0.25 and one more pole below.
// Gouraud shading draws each colour band as a straight ramp with a kink at every ring, and the dense bands near the
// horizon show as stripes. The smooth dome samples the same 6 x 24 colours with a monotone cubic across the rings (no
// overshoot, so no new colours) and a periodic Catmull-Rom around them, on a far denser mesh.

#include <cstddef>
#include <cstdint>

namespace SkyModel {

enum : uint32_t {
    RING_COUNT = 6,        // the rings above and at the horizon; ring 0 is the zenith, ring 5 the horizon
    RING_SEGMENTS = 24,    // colours around a ring
};

// theta / pi of each ring
extern const float s_ringTheta[RING_COUNT];

// how far the stock dome is lowered (cos 45 degrees)
extern const float s_domeLower;

// a monotone cubic (Fritsch-Carlson) through (x[i], y[i]), x increasing, at t; clamped to the ends
float MonotoneCubic(const float* x, const float* y, uint32_t count, float t);

// a periodic Catmull-Rom through y[0 .. count - 1] (y[count] is y[0]) at the fractional index u
float PeriodicCatmullRom(const float* y, uint32_t count, float u);

// the share of the way to a target an exponential approach covers in dt seconds with the time constant tau (1 when
// tau is 0)
float SmoothFactor(float dt, float tau);

// a point of the stock dome at theta / pi = s and azimuth fraction u (0 to 1, the stock vertex i at u = i / 24)
void DomePoint(float s, float u, float* xyz);

// theta / pi of the dome point a world direction sees (the dome's centre lies below the eye); d need not be normalized
float DomeThetaForDirection(float x, float y, float z);

// the rows of the smooth dome (theta / pi), from the zenith to the pole below; returns how many were written
uint32_t DomeRows(float* rows, uint32_t capacity);

}

#endif
