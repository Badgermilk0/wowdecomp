// MODIFIED: weather overhaul. Unit tests for WoW/Source/WorldClient/WeatherModel (no engine, no game data):
//   cmake --build build/x64 --config Release --target WeatherTests
//   ctest --test-dir build/x64 -C Release -R Weather   (or run bin/Win/WeatherTests.exe)

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "WoW/Source/WorldClient/WeatherModel.h"

using namespace WeatherModel;

static int32_t s_failures;
static int32_t s_checks;

#define CHECK(condition) Check((condition), #condition, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) CheckNear((a), (b), (eps), #a, __FILE__, __LINE__)

static void Check(bool condition, const char* text, const char* file, int32_t line) {
    s_checks++;

    if (!condition) {
        s_failures++;
        printf("FAIL %s:%d: %s\n", file, line, text);
    }
}

static void CheckNear(float a, float b, float eps, const char* text, const char* file, int32_t line) {
    s_checks++;

    if (!(fabsf(a - b) <= eps)) {
        s_failures++;
        printf("FAIL %s:%d: %s = %f, expected %f\n", file, line, text, a, b);
    }
}

static void TestParticleCurve() {
    // the stock formula, Weather::SetStormIntensity (0x00784850)
    for (int32_t i = 0; i <= 100; i++) {
        float grade = i / 100.0f;
        float stock = (grade - 0.25f) * 1.3333334f;
        stock = stock < 0.0f ? 0.0f : stock;
        CHECK_NEAR(ParticleIntensity(grade, 0.25f), stock, 1e-5f);
    }

    CHECK_NEAR(ParticleIntensity(0.0f, 0.0f), 0.0f, 1e-6f);
    CHECK_NEAR(ParticleIntensity(0.5f, 0.0f), 0.5f, 1e-6f);
    CHECK_NEAR(ParticleIntensity(2.0f, 0.25f), 1.0f, 1e-6f);
    CHECK_NEAR(ParticleIntensity(-1.0f, 0.25f), 0.0f, 1e-6f);

    for (int32_t i = 1; i <= 20; i++) {
        float particles = i / 20.0f;

        for (float start : { 0.0f, 0.25f, 0.5f }) {
            CHECK_NEAR(ParticleIntensity(GradeForParticles(particles, start), start), particles, 1e-5f);
        }
    }

    CHECK_NEAR(GradeForParticles(0.0f, 0.25f), 0.0f, 1e-6f);
}

static void TestSkyCurve() {
    for (int32_t i = 0; i <= 100; i++) {
        float grade = i / 100.0f;
        float stock = grade * 4.0f > 1.0f ? 1.0f : grade * 4.0f;

        // curve 0 is stock for every type; curve 1 keeps sky-only rows stock
        for (uint32_t type = 0; type < 4; type++) {
            CHECK_NEAR(SkyBlend(grade, type, 0, 0.25f, 0.5f), stock, 1e-5f);
        }

        CHECK_NEAR(SkyBlend(grade, 0, 1, 0.25f, 0.5f), stock, 1e-5f);
    }

    // curve 1, rain: skyMin at the first drop, full at grade 1, never decreasing, never above 1
    CHECK_NEAR(SkyBlend(0.2501f, 1, 1, 0.25f, 0.5f), 0.5f, 0.01f);
    CHECK_NEAR(SkyBlend(1.0f, 1, 1, 0.25f, 0.5f), 1.0f, 1e-5f);
    CHECK_NEAR(SkyBlend(0.625f, 1, 1, 0.25f, 0.5f), 0.75f, 1e-4f);
    float previous = 0.0f;

    for (int32_t i = 0; i <= 200; i++) {
        float sky = SkyBlend(i / 200.0f, 1, 1, 0.25f, 0.5f);
        CHECK(sky + 1e-5f >= previous);
        CHECK(sky <= 1.0f && sky >= 0.0f);
        previous = sky;
    }

    // below the first drop the stock ramp, capped at skyMin
    CHECK_NEAR(SkyBlend(0.05f, 2, 1, 0.25f, 0.5f), 0.2f, 1e-5f);
    CHECK_NEAR(SkyBlend(0.2f, 2, 1, 0.25f, 0.5f), 0.5f, 1e-5f);
}

static void TestTransition() {
    CHECK_NEAR(TransitionSeconds(0.5f, -1.0f), 5.01f, 1e-4f);
    CHECK_NEAR(TransitionSeconds(-0.5f, -1.0f), 4.99f, 1e-4f);
    CHECK_NEAR(TransitionSeconds(0.5f, 30.0f), 30.0f, 1e-6f);
    CHECK(TransitionSeconds(0.5f, 0.0f) > 0.0f);
    CHECK(TransitionSeconds(0.5f, 0.0f) <= 0.001f);
}

static void TestExtension() {
    Ext ext;
    ext.Set(EXT_SKY, 0.8f);
    ext.Set(EXT_LIGHTNING, 12.0f);
    ext.Set(EXT_TRANSITION, 45.0f);
    uint8_t buffer[256];
    uint32_t size = WriteExt(ext, buffer, sizeof(buffer));
    CHECK(size == 5 + 3 * 5);
    CHECK(buffer[0] == 'W' && buffer[1] == 'V' && buffer[2] == 'X' && buffer[3] == '1');

    Ext parsed;
    CHECK(ParseExt(buffer, size, &parsed));
    CHECK(parsed.m_mask == ext.m_mask);
    CHECK_NEAR(parsed.Get(EXT_SKY, -1.0f), 0.8f, 1e-6f);
    CHECK_NEAR(parsed.Get(EXT_LIGHTNING, -1.0f), 12.0f, 1e-6f);
    CHECK_NEAR(parsed.Get(EXT_TRANSITION, -1.0f), 45.0f, 1e-6f);
    CHECK_NEAR(parsed.Get(EXT_DENSITY, -1.0f), -1.0f, 1e-6f);

    // trailing bytes after the block are allowed
    CHECK(ParseExt(buffer, size + 3, &parsed));

    // cut short, wrong magic, nothing at all: ignored as a whole
    CHECK(!ParseExt(buffer, size - 1, &parsed));
    CHECK(parsed.m_mask == 0);
    CHECK(!ParseExt(buffer, 4, &parsed));
    CHECK(!ParseExt(nullptr, 0, &parsed));
    uint8_t bad[sizeof(buffer)];
    memcpy(bad, buffer, size);
    bad[0] = 'X';
    CHECK(!ParseExt(bad, size, &parsed));

    // an entry count over the limit
    memcpy(bad, buffer, size);
    bad[4] = EXT_MAX_ENTRIES + 1;
    CHECK(!ParseExt(bad, sizeof(bad), &parsed));

    // unknown keys are skipped, out-of-range values clamped, NaN dropped
    uint8_t mixed[5 + 4 * 5] = { 'W', 'V', 'X', '1', 4 };
    float values[4] = { 0.5f, 7.0f, NAN, -3.0f };
    uint8_t keys[4] = { 99, EXT_DENSITY, EXT_SKY, EXT_VOLUME };

    for (int32_t i = 0; i < 4; i++) {
        mixed[5 + i * 5] = keys[i];
        memcpy(&mixed[6 + i * 5], &values[i], 4);
    }

    CHECK(ParseExt(mixed, sizeof(mixed), &parsed));
    CHECK(!parsed.Has(EXT_SKY));
    CHECK_NEAR(parsed.Get(EXT_DENSITY, -1.0f), 2.0f, 1e-6f);
    CHECK_NEAR(parsed.Get(EXT_VOLUME, -1.0f), 0.0f, 1e-6f);

    // an empty block is valid
    uint8_t empty[5] = { 'W', 'V', 'X', '1', 0 };
    CHECK(ParseExt(empty, sizeof(empty), &parsed));
    CHECK(parsed.m_mask == 0);

    CHECK(ExtKeyFromName("SKY") == EXT_SKY);
    CHECK(ExtKeyFromName("strikedistance") == EXT_LIGHTNING_DISTANCE);
    CHECK(ExtKeyFromName("wind") == 0);
    CHECK(ExtKeyFromName(nullptr) == 0);

    for (uint32_t key = 1; key < EXT_KEY_COUNT; key++) {
        CHECK(ExtKeyFromName(ExtKeyName(key)) == key);
    }

    ext.Clear(EXT_SKY);
    CHECK(!ext.Has(EXT_SKY));
    ext.Set(0, 1.0f);
    ext.Set(200, 1.0f);
    CHECK(!ext.Has(0));
}

static const char* RAIN = "textures\\Weather\\RainDrop01.blp";
static const char* RED = "textures\\Weather\\RainDropRed01.blp";

static void TestTexture() {
    char out[260];

    for (bool fixes : { false, true }) {
        // first rain: a new effect
        CHECK(ChoosePrecipTexture(1, "", "", false, fixes, out, sizeof(out)));
        CHECK(!strcmp(out, RAIN));
        // the same rain again: kept
        CHECK(!ChoosePrecipTexture(1, "", RAIN, true, fixes, out, sizeof(out)));
        // a row with its own texture: restarts with it
        CHECK(ChoosePrecipTexture(1, RED, RAIN, true, fixes, out, sizeof(out)));
        CHECK(!strcmp(out, RED));
        // the same, by case and slash: kept
        CHECK(!ChoosePrecipTexture(1, "Textures/WEATHER/raindropred01.BLP", RED, true, fixes, out, sizeof(out)));
        // another effect type
        CHECK(ChoosePrecipTexture(2, "", RAIN, false, fixes, out, sizeof(out)));
        // snow twice
        CHECK(!ChoosePrecipTexture(2, "", "textures\\Weather\\SnowFlake01.blp", true, fixes, out, sizeof(out)));
    }

    // stock: every sandstorm packet restarts the effect; fixed: only the first
    CHECK(ChoosePrecipTexture(3, "", "", true, false, out, sizeof(out)));
    CHECK(ChoosePrecipTexture(3, "", "", false, true, out, sizeof(out)));
    CHECK(!ChoosePrecipTexture(3, "", "", true, true, out, sizeof(out)));

    // stock: red rain stays on a later plain rain row; fixed: back to the default drop
    CHECK(!ChoosePrecipTexture(1, "", RED, true, false, out, sizeof(out)));
    CHECK(!strcmp(out, RED));
    CHECK(ChoosePrecipTexture(1, "", RED, true, true, out, sizeof(out)));
    CHECK(!strcmp(out, RAIN));

    // a small buffer truncates instead of overflowing
    char small[8];
    ChoosePrecipTexture(1, "", "", false, true, small, sizeof(small));
    CHECK(strlen(small) == 7);
}

static void TestScheduler() {
    LightningScheduler scheduler(12345);

    for (int32_t i = 0; i < 6000; i++) {
        CHECK(!scheduler.Update(1.0f / 60.0f, 0.0f));
    }

    CHECK(scheduler.NextIn() < 0.0f);

    // ten simulated minutes at 6 per minute
    int32_t strikes = 0;
    float time = 0.0f;
    float last = -100.0f;
    float minGap = 1000.0f;

    for (int32_t i = 0; i < 60 * 60 * 10; i++) {
        time += 1.0f / 60.0f;

        if (scheduler.Update(1.0f / 60.0f, 6.0f)) {
            strikes++;
            minGap = time - last < minGap ? time - last : minGap;
            last = time;
        }
    }

    printf("  scheduler: %d strikes in 10 min at 6/min, shortest gap %.2f s\n", strikes, minGap);
    CHECK(strikes >= 35 && strikes <= 90);
    CHECK(minGap >= 1.5f - 1.0f / 60.0f);

    // the same seed gives the same storm
    LightningScheduler a(777);
    LightningScheduler b(777);
    bool same = true;

    for (int32_t i = 0; i < 20000; i++) {
        same = same && a.Update(0.05f, 10.0f) == b.Update(0.05f, 10.0f);
    }

    CHECK(same);

    // a rate change applies to the wait already running
    LightningScheduler c(99);
    c.Update(0.0f, 1.0f);
    float before = c.NextIn();
    c.Update(0.0f, 2.0f);
    CHECK_NEAR(c.NextIn(), before * 0.5f, 1e-3f);
}

static void TestStrike() {
    LightningScheduler scheduler(4242);

    for (int32_t i = 0; i < 2000; i++) {
        Strike strike = scheduler.MakeStrike(300.0f, LIGHTNING_MAX_DISTANCE, -1.0f, -1.0f);
        CHECK(strike.m_distance >= 300.0f && strike.m_distance <= LIGHTNING_MAX_DISTANCE);
        CHECK_NEAR(strike.m_thunderDelay, strike.m_distance / SPEED_OF_SOUND_YARDS, 1e-4f);
        CHECK(strike.m_near == (strike.m_distance < THUNDER_NEAR_DISTANCE));
        CHECK(strike.m_pulseCount >= 1 && strike.m_pulseCount <= FLASH_MAX_PULSES);
        CHECK(strike.m_brightness > 0.0f && strike.m_brightness <= 0.75f);
        CHECK(strike.m_thunderVolume >= 0.15f && strike.m_thunderVolume <= 1.0f);

        float peak = 0.0f;

        for (float t = -0.1f; t < strike.m_duration + 0.2f; t += 0.005f) {
            float level = FlashEnvelope(strike, t);
            CHECK(level >= 0.0f && level <= strike.m_brightness + 1e-5f);
            peak = level > peak ? level : peak;

            if (t < 0.0f || t >= strike.m_duration) {
                CHECK(level == 0.0f);
            }
        }

        CHECK(peak > strike.m_brightness * 0.8f);
    }

    // a set distance; nearer is brighter and louder
    Strike near = scheduler.MakeStrike(0.0f, LIGHTNING_MAX_DISTANCE, -1.0f, 100.0f);
    Strike far = scheduler.MakeStrike(0.0f, LIGHTNING_MAX_DISTANCE, -1.0f, 2800.0f);
    CHECK_NEAR(near.m_distance, 100.0f, 1e-6f);
    CHECK(near.m_near && !far.m_near);
    CHECK(near.m_brightness > far.m_brightness);
    CHECK(near.m_thunderVolume > far.m_thunderVolume);
    CHECK_NEAR(far.m_thunderDelay, 2800.0f / 375.0f, 1e-4f);
}

static void TestAutoLightning() {
    CHECK(AutoLightningRate(0, 86, 1, 1.0f, 0.85f, 6.0f) == 0.0f);
    CHECK_NEAR(AutoLightningRate(1, 86, 1, 1.0f, 0.85f, 6.0f), 6.0f, 1e-5f);
    CHECK_NEAR(AutoLightningRate(1, 86, 1, 0.1f, 0.85f, 6.0f), 1.8f, 1e-5f);
    CHECK(AutoLightningRate(1, 5, 1, 1.0f, 0.85f, 6.0f) == 0.0f);
    CHECK(AutoLightningRate(2, 5, 1, 0.5f, 0.85f, 6.0f) == 0.0f);
    CHECK_NEAR(AutoLightningRate(2, 5, 1, 0.85f, 0.85f, 6.0f), 1.5f, 1e-5f);
    CHECK_NEAR(AutoLightningRate(2, 5, 1, 1.0f, 0.85f, 6.0f), 6.0f, 1e-5f);
    // never snow or sand
    CHECK(AutoLightningRate(2, 8, 2, 1.0f, 0.85f, 6.0f) == 0.0f);
    CHECK(AutoLightningRate(2, 22, 3, 1.0f, 0.85f, 6.0f) == 0.0f);
    CHECK(AutoLightningRate(2, 86, 1, 1.0f, 0.85f, 0.0f) == 0.0f);
}

static void TestSound() {
    CHECK_NEAR(LoopVolume(0.0f, 1, false, 0.35f), 0.45f, 1e-6f);
    CHECK_NEAR(LoopVolume(1.0f, 1, false, 0.35f), 1.0f, 1e-6f);
    CHECK_NEAR(LoopVolume(1.0f, 1, true, 0.35f), 0.35f, 1e-6f);
    CHECK_NEAR(LoopVolume(0.0f, 0, false, 0.35f), 1.0f, 1e-6f);
    CHECK_NEAR(ZoneAmbienceDuck(0.0f, 0.6f), 1.0f, 1e-6f);
    CHECK_NEAR(ZoneAmbienceDuck(1.0f, 0.6f), 0.4f, 1e-6f);
    CHECK_NEAR(ZoneAmbienceDuck(5.0f, 5.0f), 0.0f, 1e-6f);
}

static void TestStrikePlacement() {
    LightningScheduler scheduler(2024);

    // a near chance splits the range at THUNDER_NEAR_DISTANCE
    for (float chance : { 0.0f, 0.3f, 1.0f }) {
        int32_t near = 0;
        const int32_t total = 4000;

        for (int32_t i = 0; i < total; i++) {
            Strike strike = scheduler.MakeStrike(100.0f, 2000.0f, chance, -1.0f);
            CHECK(strike.m_distance >= 100.0f && strike.m_distance <= 2000.0f);
            near += strike.m_near ? 1 : 0;
        }

        CHECK_NEAR(near / static_cast<float>(total), chance, 0.04f);
    }

    // the range limits hold without a near chance, and a range wholly near or far ignores it
    for (int32_t i = 0; i < 1000; i++) {
        Strike a = scheduler.MakeStrike(800.0f, 1200.0f, 1.0f, -1.0f);
        CHECK(a.m_distance >= 800.0f && a.m_distance <= 1200.0f);
        Strike b = scheduler.MakeStrike(50.0f, 300.0f, 0.0f, -1.0f);
        CHECK(b.m_distance >= 50.0f && b.m_distance <= 300.0f);
        Strike c = scheduler.MakeStrike(5000.0f, 99999.0f, -1.0f, -1.0f);
        CHECK(c.m_distance <= LIGHTNING_MAX_DISTANCE);
    }

    // bursts: none at 0, always three at 1, delays rising and within two and a half seconds
    float delays[3];
    CHECK(scheduler.RollBurst(0.0f, delays, 3) == 0);

    for (int32_t i = 0; i < 200; i++) {
        int32_t count = scheduler.RollBurst(1.0f, delays, 3);
        CHECK(count == 3);
        CHECK(delays[0] >= 0.25f && delays[0] < delays[1] && delays[1] < delays[2] && delays[2] <= 2.55f);
    }

    CHECK(scheduler.RollBurst(1.0f, delays, 1) == 1);
}

static void TestThunderParts() {
    LightningScheduler random(31337);
    ThunderPart parts[THUNDER_MAX_PARTS];

    // near: crack, body, roll; the body pitched down, the roll after the crack
    int32_t count = ThunderParts(200.0f, true, random, parts);
    CHECK(count == 3);
    CHECK(parts[0].m_layer == THUNDER_CRACK && parts[1].m_layer == THUNDER_BODY && parts[2].m_layer == THUNDER_ROLL);
    CHECK(parts[1].m_pitch < 0.7f && parts[1].m_pitch >= 0.55f);
    CHECK(parts[2].m_delay > parts[0].m_delay);

    count = ThunderParts(1000.0f, true, random, parts);
    CHECK(count == 2);
    CHECK(parts[0].m_layer == THUNDER_BODY && parts[1].m_layer == THUNDER_ROLL);

    count = ThunderParts(2500.0f, true, random, parts);
    CHECK(count == 1 && parts[0].m_layer == THUNDER_DISTANT);

    // unlayered: one part, as before
    CHECK(ThunderParts(200.0f, false, random, parts) == 1 && parts[0].m_layer == THUNDER_CRACK && parts[0].m_pitch == 1.0f);
    CHECK(ThunderParts(1000.0f, false, random, parts) == 1 && parts[0].m_layer == THUNDER_ROLL);
    CHECK(ThunderParts(2500.0f, false, random, parts) == 1 && parts[0].m_layer == THUNDER_DISTANT);

    for (int32_t i = 0; i < 500; i++) {
        float distance = random.Random() * LIGHTNING_MAX_DISTANCE;
        count = ThunderParts(distance, true, random, parts);

        for (int32_t j = 0; j < count; j++) {
            CHECK(parts[j].m_layer >= 0 && parts[j].m_layer < THUNDER_LAYER_COUNT);
            CHECK(parts[j].m_volume > 0.0f && parts[j].m_volume <= 1.0f);
            CHECK(parts[j].m_pitch > 0.5f && parts[j].m_pitch < 1.1f);
            CHECK(parts[j].m_delay >= 0.0f && parts[j].m_delay < 1.0f);
        }
    }

    uint8_t r;
    uint8_t g;
    uint8_t b;
    UnpackColor(static_cast<float>(0xC8D7FF), &r, &g, &b);
    CHECK(r == 0xC8 && g == 0xD7 && b == 0xFF);
    UnpackColor(16777215.0f, &r, &g, &b);
    CHECK(r == 0xFF && g == 0xFF && b == 0xFF);
    UnpackColor(-5.0f, &r, &g, &b);
    CHECK(r == 0 && g == 0 && b == 0);

    // a colour survives the packet
    Ext ext;
    ext.Set(EXT_FLASH_COLOR, static_cast<float>(0x123456));
    uint8_t buffer[64];
    Ext parsed;
    CHECK(ParseExt(buffer, WriteExt(ext, buffer, sizeof(buffer)), &parsed));
    UnpackColor(parsed.Get(EXT_FLASH_COLOR, 0.0f), &r, &g, &b);
    CHECK(r == 0x12 && g == 0x34 && b == 0x56);
}

static void TestExtKeys() {
    // every key has a name and a range, and a full block (all keys) fits the client's packet buffer
    Ext ext;

    for (uint32_t key = 1; key < EXT_KEY_COUNT; key++) {
        float min = -1.0f;
        float max = -1.0f;
        CHECK(ExtKeyName(key) != nullptr);
        CHECK(ExtKeyRange(key, &min, &max) && min < max);
        ext.Set(key, max + 1000.0f);
        CHECK_NEAR(ext.Get(key, 0.0f), max, 1e-3f);
    }

    float min;
    float max;
    CHECK(!ExtKeyRange(0, &min, &max));
    CHECK(!ExtKeyRange(EXT_KEY_COUNT, &min, &max));

    uint8_t buffer[5 + 5 * EXT_MAX_ENTRIES];
    uint32_t size = WriteExt(ext, buffer, sizeof(buffer));
    CHECK(size == 5 + 5 * (EXT_KEY_COUNT - 1));
    Ext parsed;
    CHECK(ParseExt(buffer, size, &parsed));
    CHECK(parsed.m_mask == ext.m_mask);
    CHECK(ExtKeyFromName("intensity") == EXT_STORM);
    CHECK(ExtKeyFromName("storm") == 0);
    CHECK(ExtKeyFromName("flashcolor") == EXT_FLASH_COLOR);
}

int main() {
    TestParticleCurve();
    TestSkyCurve();
    TestTransition();
    TestExtension();
    TestTexture();
    TestScheduler();
    TestStrike();
    TestAutoLightning();
    TestSound();
    TestStrikePlacement();
    TestThunderParts();
    TestExtKeys();

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
