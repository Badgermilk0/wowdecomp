#ifndef WOW_SOURCE_WORLDCLIENT_WEATHERMODEL_H
#define WOW_SOURCE_WORLDCLIENT_WEATHERMODEL_H

// MODIFIED: weather overhaul. The arithmetic of the weather features, free of the engine so Tools/WeatherTests can
// run it: the grade curves, the SMSG_WEATHER extension block, the precipitation texture choice, and the lightning
// scheduler with its flash and thunder. Awesome/AwesomeWeather.cpp and MapWeather.cpp drive it.

#include <cstddef>
#include <cstdint>

namespace WeatherModel {

// --- SMSG_WEATHER extension -------------------------------------------------------------------------------------
// A stock packet is uint32 weather id, float grade, uint8 abrupt. A server may append, in the same packet:
//   uint32 magic 'WVX1' (bytes 57 56 58 31), uint8 count, count x { uint8 key, float value }
// The stock client's net layer skips unread bytes, so a stock client ignores the block. Unknown keys are skipped;
// a block that is cut short is ignored as a whole.
const uint32_t EXT_MAGIC = 0x31585657;
const uint32_t EXT_MAX_ENTRIES = 32;

enum ExtKey : uint8_t {
    EXT_SKY = 1,                // storm sky blend 0..1, in place of the one derived from the grade
    EXT_PARTICLES = 2,          // particle intensity 0..1, in place of the one derived from the grade
    EXT_DENSITY = 3,            // particle count multiplier 0..2 (on top of the weatherDensity CVars)
    EXT_LIGHTNING = 4,          // lightning strikes per minute 0..60 (0 = none, whatever the weather id)
    EXT_TRANSITION = 5,         // seconds to reach the new grade, sky and particles (0 = at once)
    EXT_VOLUME = 6,             // weather sound volume multiplier 0..1
    EXT_LIGHTNING_DISTANCE = 7, // nearest a strike may land, yards 0..3000
    EXT_STRIKE_MAX_DISTANCE = 8, // farthest a strike may land, yards 50..3000
    EXT_FLASH = 9,              // flash brightness multiplier 0..2
    EXT_THUNDER = 10,           // thunder volume multiplier 0..2 (above 1 lifts the quieter layers and far strikes)
    EXT_NEAR_CHANCE = 11,       // chance 0..1 that a strike lands within THUNDER_NEAR_DISTANCE (crack and bright flash)
    EXT_BURST = 12,             // chance 0..1 that a strike is followed by 1-3 restrikes within two seconds
    EXT_STORM = 13,             // "intensity", the storm 0..1: lightning rate, near chance, bursts and rumble when
                                // those are not sent (the grade stays the precipitation)
    EXT_SOUND_KIT = 14,         // the weather loop's sound kit in place of the row's (0 = silent)
    EXT_RUMBLE_KIT = 15,        // a second loop under the weather, e.g. 4994 (distant thunder bed); 0 = none
    EXT_RUMBLE = 16,            // its volume 0..1 (default: storm, or 1)
    EXT_THUNDER_NEAR_KIT = 17,  // the crack of a near strike (default weatherThunderNearKit)
    EXT_THUNDER_FAR_KIT = 18,   // the body and roll of every strike (default weatherThunderFarKit)
    EXT_DUCK = 19,              // how far the zone ambience is turned down under full weather 0..1
    EXT_INDOOR = 20,            // weather loop volume indoors 0..1
    EXT_FLASH_COLOR = 21,       // flash colour as 0xRRGGBB in a float (exact up to 2^24)
    EXT_KEY_COUNT = 22,
};

struct Ext {
    uint32_t m_mask = 0;
    float m_values[EXT_KEY_COUNT] = {};

    bool Has(uint32_t key) const { return key < EXT_KEY_COUNT && (this->m_mask & (1u << key)); }
    float Get(uint32_t key, float fallback) const { return this->Has(key) ? this->m_values[key] : fallback; }
    // clamps to the key's range; an unknown key is ignored
    void Set(uint32_t key, float value);
    void Clear(uint32_t key) { if (key < EXT_KEY_COUNT) this->m_mask &= ~(1u << key); }
};

const char* ExtKeyName(uint32_t key);
// the key named name ("sky", "particles", ...), 0 if none
uint32_t ExtKeyFromName(const char* name);
float ClampExt(uint32_t key, float value);
// the range a key is clamped to; false for an unknown key
bool ExtKeyRange(uint32_t key, float* min, float* max);

// data / size: the bytes after the stock fields. False when there is no well-formed block (ext left empty)
bool ParseExt(const uint8_t* data, uint32_t size, Ext* ext);
// writes a block for ext into out; returns the byte count, 0 if cap is too small
uint32_t WriteExt(const Ext& ext, uint8_t* out, uint32_t cap);

// --- grade curves ---------------------------------------------------------------------------------------------
// stock: nothing falls at or below 0.25, full at 1
float ParticleIntensity(float grade, float start);
// the grade whose particle intensity is p (inverse of ParticleIntensity, p > 0)
float GradeForParticles(float particles, float start);
// the storm sky blend (DayNight m_stormBlend) for a grade. curve 0: stock, full at grade 0.25. curve 1: graded for
// precipitation, skyMin at the first drop up to 1 at full particles; sky-only rows (type 0) keep the stock ramp
float SkyBlend(float grade, uint32_t type, int32_t curve, float start, float skyMin);
// seconds the client takes from one grade to the next: stock 10 s per 1.0, or the server's
float TransitionSeconds(float delta, float extSeconds);

// volume of the weather loop for its particle intensity (sky-only rows play at full), times indoorScale inside
float LoopVolume(float particles, uint32_t type, bool inside, float indoorScale);
// how much of the zone ambience is left under weather of this particle intensity
float ZoneAmbienceDuck(float particles, float strength);

// --- precipitation texture ------------------------------------------------------------------------------------
const char* DefaultPrecipTexture(uint32_t type);
// The texture Weather::SetType keeps for a packet, written to out (capacity bytes). Returns true when the effect has
// to be re-created (type or texture changed). fixes false reproduces stock: a sandstorm (no default texture) restarts
// on every packet, and a rain row's own texture stays on later rain without one.
bool ChoosePrecipTexture(uint32_t type, const char* recTexture, const char* current, bool sameType, bool fixes, char* out, size_t capacity);

// --- lightning ------------------------------------------------------------------------------------------------
const float SPEED_OF_SOUND_YARDS = 375.0f;  // 343 m/s
const float LIGHTNING_MAX_DISTANCE = 3000.0f;
const int32_t FLASH_MAX_PULSES = 4;

struct Strike {
    float m_distance = 0.0f;       // yards
    float m_brightness = 0.0f;     // peak flash blend 0..1
    int32_t m_pulseCount = 0;
    float m_pulseStart[FLASH_MAX_PULSES] = {};  // seconds after the strike
    float m_pulseLevel[FLASH_MAX_PULSES] = {};  // fraction of the brightness
    float m_duration = 0.0f;       // the flash is over after this
    float m_thunderDelay = 0.0f;   // seconds from the flash to the thunder
    float m_thunderVolume = 0.0f;  // 0..1
    bool m_near = false;           // a crack rather than a distant roll
};

// strike distance to its flash peak and thunder volume
float FlashBrightness(float distance);
float ThunderVolume(float distance);
const float THUNDER_NEAR_DISTANCE = 600.0f;
const float THUNDER_MID_DISTANCE = 1500.0f;

class LightningScheduler;

// The layers a strike's thunder is built from. Near: the crack, a pitched-down body at once and a roll after it.
// Mid: body and roll. Far: the distant roll alone, duller still. Each layer goes through its own filter chain
// (AwesomeWeather: bass shelf, lowpass, compressor).
enum ThunderLayer : int32_t {
    THUNDER_CRACK = 0,
    THUNDER_BODY = 1,
    THUNDER_ROLL = 2,
    THUNDER_DISTANT = 3,
    THUNDER_LAYER_COUNT = 4,
};

struct ThunderPart {
    int32_t m_layer;
    float m_delay;   // seconds after the thunder arrives
    float m_volume;  // 0..1 of the strike's thunder volume
    float m_pitch;   // playback rate
};

const int32_t THUNDER_MAX_PARTS = 3;
// fills parts (capacity THUNDER_MAX_PARTS) for a strike at distance; layered false gives one part, the crack near
// and the roll otherwise
int32_t ThunderParts(float distance, bool layered, LightningScheduler& random, ThunderPart* parts);
// a packed 0xRRGGBB ext value to bytes
void UnpackColor(float packed, uint8_t* r, uint8_t* g, uint8_t* b);

// flash blend at t seconds after the strike, 0 before and after
float FlashEnvelope(const Strike& strike, float t);

class LightningScheduler {
    public:
    explicit LightningScheduler(uint32_t seed = 0x9E3779B9u);
    void Seed(uint32_t seed);
    // uniform [0, 1)
    float Random();
    // advances dt seconds at ratePerMinute; true when a strike is due (then the next one is scheduled)
    bool Update(float dt, float ratePerMinute);
    // seconds to the next strike at the current rate, negative when none is scheduled
    float NextIn() const { return this->m_nextIn; }
    // a strike at distance (yards), or a random one between minDistance and maxDistance when distance < 0. With
    // nearChance >= 0, that share lands within THUNDER_NEAR_DISTANCE (when the range allows), the rest beyond it;
    // otherwise most land far (u^0.6 over the range)
    Strike MakeStrike(float minDistance, float maxDistance, float nearChance, float distance);
    // restrikes after a strike: how many (0-3, each with chance burst), their delays in seconds into delays
    int32_t RollBurst(float burst, float* delays, int32_t capacity);

    private:
    void Schedule(float ratePerMinute);

    uint32_t m_state;
    float m_nextIn;
    float m_rate;
};

// the strike rate for a weather when the server sends none. mode 0: none. mode 1: the stock thunderstorm row only
// (Weather.dbc 86). mode 2: also heavy rain from heavyGrade. rate: strikes per minute at grade 1
float AutoLightningRate(int32_t mode, uint32_t weatherId, uint32_t type, float grade, float heavyGrade, float rate);
const uint32_t WEATHER_ID_THUNDERSTORM = 86;

}

#endif
