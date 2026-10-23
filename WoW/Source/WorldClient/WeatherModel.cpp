#include "WoW/Source/WorldClient/WeatherModel.h"
#include <cmath>
#include <cstring>

namespace WeatherModel {

static float Clamp(float value, float min, float max) {
    if (!(value >= min)) {
        return min;
    }

    return value > max ? max : value;
}

static int32_t CompareNoCase(const char* a, const char* b) {
    for (;; a++, b++) {
        int32_t ca = static_cast<unsigned char>(*a);
        int32_t cb = static_cast<unsigned char>(*b);

        if (ca >= 'A' && ca <= 'Z') {
            ca += 'a' - 'A';
        }

        if (cb >= 'A' && cb <= 'Z') {
            cb += 'a' - 'A';
        }

        // the client's paths mix both slashes
        if (ca == '/') {
            ca = '\\';
        }

        if (cb == '/') {
            cb = '\\';
        }

        if (ca != cb || !ca) {
            return ca - cb;
        }
    }
}

static const struct {
    const char* m_name;
    float m_min;
    float m_max;
} s_extKeys[EXT_KEY_COUNT] = {
    { nullptr, 0.0f, 0.0f },
    { "sky", 0.0f, 1.0f },
    { "particles", 0.0f, 1.0f },
    { "density", 0.0f, 2.0f },
    { "lightning", 0.0f, 60.0f },
    { "transition", 0.0f, 600.0f },
    { "volume", 0.0f, 1.0f },
    { "strikedistance", 0.0f, LIGHTNING_MAX_DISTANCE },
    { "strikemaxdistance", 50.0f, LIGHTNING_MAX_DISTANCE },
    { "flash", 0.0f, 2.0f },
    { "thunder", 0.0f, 2.0f },
    { "nearchance", 0.0f, 1.0f },
    { "burst", 0.0f, 1.0f },
    { "intensity", 0.0f, 1.0f },
    { "soundkit", 0.0f, 1000000.0f },
    { "rumblekit", 0.0f, 1000000.0f },
    { "rumble", 0.0f, 1.0f },
    { "nearkit", 0.0f, 1000000.0f },
    { "farkit", 0.0f, 1000000.0f },
    { "duck", 0.0f, 1.0f },
    { "indoor", 0.0f, 1.0f },
    { "flashcolor", 0.0f, 16777215.0f },
};

static_assert(sizeof(s_extKeys) / sizeof(s_extKeys[0]) == EXT_KEY_COUNT, "a name and range for every key");
static_assert(EXT_KEY_COUNT <= 32, "Ext::m_mask holds one bit per key");

const char* ExtKeyName(uint32_t key) {
    return key < EXT_KEY_COUNT ? s_extKeys[key].m_name : nullptr;
}

uint32_t ExtKeyFromName(const char* name) {
    if (!name) {
        return 0;
    }

    for (uint32_t key = 1; key < EXT_KEY_COUNT; key++) {
        if (!CompareNoCase(name, s_extKeys[key].m_name)) {
            return key;
        }
    }

    return 0;
}

bool ExtKeyRange(uint32_t key, float* min, float* max) {
    if (key == 0 || key >= EXT_KEY_COUNT) {
        return false;
    }

    *min = s_extKeys[key].m_min;
    *max = s_extKeys[key].m_max;
    return true;
}

float ClampExt(uint32_t key, float value) {
    if (key == 0 || key >= EXT_KEY_COUNT) {
        return value;
    }

    return Clamp(value, s_extKeys[key].m_min, s_extKeys[key].m_max);
}

void Ext::Set(uint32_t key, float value) {
    if (key == 0 || key >= EXT_KEY_COUNT) {
        return;
    }

    this->m_values[key] = ClampExt(key, value);
    this->m_mask |= 1u << key;
}

static uint32_t ReadU32(const uint8_t* data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

bool ParseExt(const uint8_t* data, uint32_t size, Ext* ext) {
    *ext = Ext();

    if (!data || size < 5 || ReadU32(data) != EXT_MAGIC) {
        return false;
    }

    uint32_t count = data[4];

    if (count > EXT_MAX_ENTRIES || size < 5 + count * 5) {
        return false;
    }

    Ext parsed;
    const uint8_t* entry = data + 5;

    for (uint32_t i = 0; i < count; i++, entry += 5) {
        uint32_t bits = ReadU32(entry + 1);
        float value;
        memcpy(&value, &bits, sizeof(value));

        // a NaN or infinity is a broken sender, not a value
        if (!std::isfinite(value)) {
            continue;
        }

        parsed.Set(entry[0], value);
    }

    *ext = parsed;
    return true;
}

uint32_t WriteExt(const Ext& ext, uint8_t* out, uint32_t cap) {
    uint32_t count = 0;

    for (uint32_t key = 1; key < EXT_KEY_COUNT; key++) {
        count += ext.Has(key) ? 1 : 0;
    }

    uint32_t size = 5 + count * 5;

    if (!out || cap < size) {
        return 0;
    }

    uint32_t magic = EXT_MAGIC;

    for (uint32_t i = 0; i < 4; i++) {
        out[i] = static_cast<uint8_t>(magic >> (i * 8));
    }

    out[4] = static_cast<uint8_t>(count);
    uint8_t* entry = out + 5;

    for (uint32_t key = 1; key < EXT_KEY_COUNT; key++) {
        if (!ext.Has(key)) {
            continue;
        }

        uint32_t bits;
        memcpy(&bits, &ext.m_values[key], sizeof(bits));
        entry[0] = static_cast<uint8_t>(key);

        for (uint32_t i = 0; i < 4; i++) {
            entry[1 + i] = static_cast<uint8_t>(bits >> (i * 8));
        }

        entry += 5;
    }

    return size;
}

float ParticleIntensity(float grade, float start) {
    start = Clamp(start, 0.0f, 0.95f);
    return Clamp((grade - start) / (1.0f - start), 0.0f, 1.0f);
}

float GradeForParticles(float particles, float start) {
    start = Clamp(start, 0.0f, 0.95f);
    particles = Clamp(particles, 0.0f, 1.0f);

    if (particles <= 0.0f) {
        return 0.0f;
    }

    return start + particles * (1.0f - start);
}

float SkyBlend(float grade, uint32_t type, int32_t curve, float start, float skyMin) {
    float stock = Clamp(grade * 4.0f, 0.0f, 1.0f);

    if (curve == 0 || type == 0) {
        return stock;
    }

    float particles = ParticleIntensity(grade, start);

    // below the first drop the stock ramp still applies, so a sky-only build-up meets the precipitation curve
    if (particles <= 0.0f) {
        return stock < skyMin ? stock : Clamp(skyMin, 0.0f, 1.0f);
    }

    skyMin = Clamp(skyMin, 0.0f, 1.0f);
    return skyMin + (1.0f - skyMin) * particles;
}

float TransitionSeconds(float delta, float extSeconds) {
    if (extSeconds >= 0.0f) {
        return extSeconds > 0.001f ? extSeconds : 0.001f;
    }

    return fabsf(delta + 0.001f) * 10.0f;
}

float LoopVolume(float particles, uint32_t type, bool inside, float indoorScale) {
    float volume = type == 0 ? 1.0f : 0.45f + 0.55f * Clamp(particles, 0.0f, 1.0f);
    return inside ? volume * Clamp(indoorScale, 0.0f, 1.0f) : volume;
}

float ZoneAmbienceDuck(float particles, float strength) {
    return 1.0f - Clamp(strength, 0.0f, 1.0f) * Clamp(particles, 0.0f, 1.0f);
}

const char* DefaultPrecipTexture(uint32_t type) {
    if (type == 1) {
        return "textures\\Weather\\RainDrop01.blp";
    }

    if (type == 2) {
        return "textures\\Weather\\SnowFlake01.blp";
    }

    return "";
}

static void Copy(char* out, size_t capacity, const char* text) {
    if (!capacity) {
        return;
    }

    size_t length = strlen(text);

    if (length >= capacity) {
        length = capacity - 1;
    }

    memcpy(out, text, length);
    out[length] = '\0';
}

bool ChoosePrecipTexture(uint32_t type, const char* recTexture, const char* current, bool sameType, bool fixes, char* out, size_t capacity) {
    const char* rec = recTexture ? recTexture : "";

    if (fixes) {
        const char* desired = *rec ? rec : DefaultPrecipTexture(type);
        bool changed = !sameType || CompareNoCase(desired, current ? current : "") != 0;
        Copy(out, capacity, desired);
        return changed;
    }

    // stock Weather::SetType (0x007846A0)
    Copy(out, capacity, sameType && current ? current : "");
    bool changed = !sameType;
    bool copied = false;

    if (*rec && CompareNoCase(out, rec)) {
        changed = true;
        Copy(out, capacity, rec);
        copied = true;
    }

    if (!*out && !copied) {
        Copy(out, capacity, DefaultPrecipTexture(type));
        changed = true;
    }

    return changed;
}

float FlashBrightness(float distance) {
    return Clamp(0.8f - distance / 3200.0f, 0.12f, 0.75f);
}

float ThunderVolume(float distance) {
    return Clamp(1.1f - distance / 2800.0f, 0.15f, 1.0f);
}

float FlashEnvelope(const Strike& strike, float t) {
    if (t < 0.0f || t >= strike.m_duration) {
        return 0.0f;
    }

    // each pulse: 25 ms to its peak, then an exponential decay; the pulses of one strike overlap
    float level = 0.0f;

    for (int32_t i = 0; i < strike.m_pulseCount; i++) {
        float local = t - strike.m_pulseStart[i];

        if (local < 0.0f) {
            continue;
        }

        float pulse = local < 0.025f ? local / 0.025f : expf(-(local - 0.025f) * 9.0f);
        float value = pulse * strike.m_pulseLevel[i];

        if (value > level) {
            level = value;
        }
    }

    // fade the tail to zero by the end
    float tail = (strike.m_duration - t) / 0.1f;

    if (tail < 1.0f) {
        level *= tail;
    }

    return Clamp(level * strike.m_brightness, 0.0f, 1.0f);
}

LightningScheduler::LightningScheduler(uint32_t seed) : m_state(seed ? seed : 1), m_nextIn(-1.0f), m_rate(0.0f) {
}

void LightningScheduler::Seed(uint32_t seed) {
    this->m_state = seed ? seed : 1;
    this->m_nextIn = -1.0f;
    this->m_rate = 0.0f;
}

float LightningScheduler::Random() {
    // xorshift32
    uint32_t x = this->m_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    this->m_state = x;
    return (x >> 8) * (1.0f / 16777216.0f);
}

void LightningScheduler::Schedule(float ratePerMinute) {
    if (ratePerMinute <= 0.0f) {
        this->m_nextIn = -1.0f;
        return;
    }

    // exponential gaps (a Poisson process) with a floor, so strikes cluster a little but never machine-gun
    float mean = 60.0f / ratePerMinute;
    float u = this->Random();
    float gap = -logf(1.0f - u * 0.999f) * mean;
    this->m_nextIn = gap < 1.5f ? 1.5f : gap;
}

bool LightningScheduler::Update(float dt, float ratePerMinute) {
    if (ratePerMinute <= 0.0f) {
        this->m_nextIn = -1.0f;
        this->m_rate = 0.0f;
        return false;
    }

    if (this->m_nextIn < 0.0f) {
        this->m_rate = ratePerMinute;
        this->Schedule(ratePerMinute);
        return false;
    }

    // a changed rate rescales the wait already running, rather than waiting out the old one
    if (this->m_rate != ratePerMinute && this->m_rate > 0.0f) {
        this->m_nextIn *= this->m_rate / ratePerMinute;
    }

    this->m_rate = ratePerMinute;
    this->m_nextIn -= dt;

    if (this->m_nextIn > 0.0f) {
        return false;
    }

    this->Schedule(ratePerMinute);
    return true;
}

Strike LightningScheduler::MakeStrike(float minDistance, float maxDistance, float nearChance, float distance) {
    Strike strike;
    maxDistance = Clamp(maxDistance, 50.0f, LIGHTNING_MAX_DISTANCE);
    minDistance = Clamp(minDistance, 0.0f, maxDistance);

    if (distance < 0.0f) {
        bool split = nearChance >= 0.0f && minDistance < THUNDER_NEAR_DISTANCE && maxDistance > THUNDER_NEAR_DISTANCE;

        if (split && this->Random() < nearChance) {
            distance = minDistance + (THUNDER_NEAR_DISTANCE - minDistance) * this->Random();
        } else if (split) {
            distance = THUNDER_NEAR_DISTANCE + (maxDistance - THUNDER_NEAR_DISTANCE) * powf(this->Random(), 0.8f);
        } else {
            // most strikes are far away: u^0.6 leans toward the far end
            float u = powf(this->Random(), 0.6f);
            distance = minDistance + (maxDistance - minDistance) * u;
        }
    }

    strike.m_distance = Clamp(distance, 0.0f, LIGHTNING_MAX_DISTANCE);
    strike.m_brightness = FlashBrightness(strike.m_distance);
    strike.m_thunderDelay = strike.m_distance / SPEED_OF_SOUND_YARDS;
    strike.m_thunderVolume = ThunderVolume(strike.m_distance);
    strike.m_near = strike.m_distance < THUNDER_NEAR_DISTANCE;

    // 1-4 return strokes, 40-160 ms apart, the first usually the brightest
    strike.m_pulseCount = 1 + static_cast<int32_t>(this->Random() * FLASH_MAX_PULSES);

    if (strike.m_pulseCount > FLASH_MAX_PULSES) {
        strike.m_pulseCount = FLASH_MAX_PULSES;
    }

    float time = 0.0f;

    for (int32_t i = 0; i < strike.m_pulseCount; i++) {
        strike.m_pulseStart[i] = time;
        strike.m_pulseLevel[i] = i == 0 ? 1.0f : 0.45f + 0.5f * this->Random();
        time += 0.04f + 0.12f * this->Random();
    }

    strike.m_duration = strike.m_pulseStart[strike.m_pulseCount - 1] + 0.45f;
    return strike;
}

int32_t LightningScheduler::RollBurst(float burst, float* delays, int32_t capacity) {
    int32_t count = 0;
    float time = 0.0f;
    int32_t limit = capacity < 3 ? capacity : 3;

    while (count < limit && this->Random() < burst) {
        time += 0.25f + 0.6f * this->Random();
        delays[count++] = time;
    }

    return count;
}

int32_t ThunderParts(float distance, bool layered, LightningScheduler& random, ThunderPart* parts) {
    bool near = distance < THUNDER_NEAR_DISTANCE;
    bool far = distance >= THUNDER_MID_DISTANCE;

    if (!layered) {
        parts[0] = { near ? THUNDER_CRACK : (far ? THUNDER_DISTANT : THUNDER_ROLL), 0.0f, 1.0f, 1.0f };
        return 1;
    }

    if (near) {
        // the crack, the body under it, and the roll as the sound comes back off the land
        parts[0] = { THUNDER_CRACK, 0.0f, 1.0f, 0.92f + 0.14f * random.Random() };
        parts[1] = { THUNDER_BODY, 0.02f, 0.9f, 0.55f + 0.12f * random.Random() };
        parts[2] = { THUNDER_ROLL, 0.45f + 0.5f * random.Random(), 0.55f, 0.75f + 0.1f * random.Random() };
        return 3;
    }

    if (!far) {
        parts[0] = { THUNDER_BODY, 0.0f, 0.85f, 0.6f + 0.12f * random.Random() };
        parts[1] = { THUNDER_ROLL, 0.2f + 0.4f * random.Random(), 0.8f, 0.72f + 0.12f * random.Random() };
        return 2;
    }

    parts[0] = { THUNDER_DISTANT, 0.0f, 1.0f, 0.62f + 0.15f * random.Random() };
    return 1;
}

void UnpackColor(float packed, uint8_t* r, uint8_t* g, uint8_t* b) {
    // in double: a float above 2^23 has no room for the half, and + 0.5f would round up to the next integer
    uint32_t value = static_cast<uint32_t>(static_cast<double>(Clamp(packed, 0.0f, 16777215.0f)) + 0.5);
    *r = static_cast<uint8_t>(value >> 16);
    *g = static_cast<uint8_t>(value >> 8);
    *b = static_cast<uint8_t>(value);
}

float AutoLightningRate(int32_t mode, uint32_t weatherId, uint32_t type, float grade, float heavyGrade, float rate) {
    if (mode <= 0 || rate <= 0.0f) {
        return 0.0f;
    }

    float level = Clamp(grade, 0.0f, 1.0f);

    if (weatherId == WEATHER_ID_THUNDERSTORM) {
        // the thunderstorm row flashes even at a low grade
        return rate * (level > 0.3f ? level : 0.3f);
    }

    if (mode >= 2 && type == 1 && level >= heavyGrade) {
        // heavy rain: from a quarter of the rate at heavyGrade to all of it at 1
        float span = 1.0f - heavyGrade;
        float t = span > 0.001f ? (level - heavyGrade) / span : 1.0f;
        return rate * (0.25f + 0.75f * t);
    }

    return 0.0f;
}

}
