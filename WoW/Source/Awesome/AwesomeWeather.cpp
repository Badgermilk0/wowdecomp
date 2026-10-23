// Weather overhaul (not from awesome_wotlk). The arithmetic lives in WorldClient/WeatherModel.*; this file holds the
// state, the CVars, the Lua API and /weather.
// - Stock fixes (weatherFixes): a sandstorm no longer restarts on every packet; rain without a texture of its own
//   goes back to the default drop.
// - Grade curves: weatherParticleStart (stock 0.25: nothing falls at or below it), weatherSkyCurve (0 stock: the sky
//   is fully stormy from grade 0.25; 1: graded for precipitation from weatherSkyMin), weatherDensityScale.
// - Lightning: flashes through DayNight's flash colour (sky, fog, ambient and sun light), restrikes in bursts,
//   thunder delayed by the speed of sound. weatherLightning 1: the stock thunderstorm row (Weather.dbc 86) and servers
//   that ask for it; 2: also heavy rain.
// - Thunder: built from layers (WeatherModel::ThunderParts): a crack, a pitched-down body and a delayed roll near,
//   a duller roll far away. Each layer plays through its own FMOD chain: two bass bands, a presence band on the crack,
//   a lowpass on the body and rolls, and a compressor with make-up gain (weatherThunderBass, weatherThunderBoost),
//   which the stock channel volume (capped at 1) could not reach.
// - Sound layer (weatherSoundLayer): the weather loop plays over the zone ambience (ducked) at a volume that follows
//   the particle intensity, muffled indoors, instead of replacing the zone ambience and going silent inside. A second
//   loop, the rumble bed, can run under storms.
// - Server extension: SMSG_WEATHER may carry a block after the stock fields (WeatherModel.h, ExtKey). Stock clients
//   skip it.
// - Override for testing: /weather (/wx) sets any weather locally until /weather off; the server's packets are kept
//   and applied again then. Lua: GetWeatherInfo, SetWeatherOverride, SetWeatherOverrideField, ClearWeatherOverride,
//   TriggerLightning, GetWeatherRows, GetWeatherFields.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "WoW/Source/Awesome/AwesomeThunderDsp.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/SoundEngine/SoundEngine.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/WeatherRec.h"
#include "WoW/Source/SoundInterface/SoundInterface2.h"
#include "WoW/Source/SoundInterface/SoundInterface2ZoneSounds.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/MapWeather.h"
#include "WoW/Source/WorldClient/WeatherModel.h"
#include "WoW/Source/WorldClient/World.h"

using namespace WeatherModel;

static void OnCurveSettings(AwesomeCVar* cvar, bool changed);
static void OnDensitySettings(AwesomeCVar* cvar, bool changed);
static void OnThunderSettings(AwesomeCVar* cvar, bool changed);

static AwesomeCVar s_cvFixes = AwesomeCVar::Int("weatherFixes", 1, 0, 1, OnCurveSettings);
static AwesomeCVar s_cvParticleStart = AwesomeCVar::Float("weatherParticleStart", 0.25f, 0.0f, 0.9f, OnCurveSettings);
static AwesomeCVar s_cvSkyCurve = AwesomeCVar::Int("weatherSkyCurve", 0, 0, 1, OnCurveSettings);
static AwesomeCVar s_cvSkyMin = AwesomeCVar::Float("weatherSkyMin", 0.5f, 0.0f, 1.0f, OnCurveSettings);
static AwesomeCVar s_cvDensityScale = AwesomeCVar::Float("weatherDensityScale", 1.0f, 0.1f, 2.0f, OnDensitySettings);
static AwesomeCVar s_cvLightning = AwesomeCVar::Int("weatherLightning", 1, 0, 2);
static AwesomeCVar s_cvLightningRate = AwesomeCVar::Float("weatherLightningRate", 6.0f, 0.0f, 30.0f);
static AwesomeCVar s_cvLightningGrade = AwesomeCVar::Float("weatherLightningGrade", 0.85f, 0.3f, 1.0f);
static AwesomeCVar s_cvLightningBrightness = AwesomeCVar::Float("weatherLightningBrightness", 0.8f, 0.0f, 1.5f);
static AwesomeCVar s_cvLightningDistance = AwesomeCVar::Float("weatherLightningDistance", 200.0f, 0.0f, LIGHTNING_MAX_DISTANCE, nullptr, "%.0f");
static AwesomeCVar s_cvLightningBurst = AwesomeCVar::Float("weatherLightningBurst", 0.15f, 0.0f, 1.0f);
static AwesomeCVar s_cvThunder = AwesomeCVar::Int("weatherThunder", 1, 0, 1);
static AwesomeCVar s_cvThunderVolume = AwesomeCVar::Float("weatherThunderVolume", 1.0f, 0.0f, 1.0f);
// Blasted Lands bolt (a crack) and Shaman_Thunder (a heavier roll), the best of the stock sounds by ear
static AwesomeCVar s_cvThunderNearKit = AwesomeCVar::Int("weatherThunderNearKit", 8439, 0, 1000000);
static AwesomeCVar s_cvThunderFarKit = AwesomeCVar::Int("weatherThunderFarKit", 13006, 0, 1000000);
static AwesomeCVar s_cvThunderLayers = AwesomeCVar::Int("weatherThunderLayers", 1, 0, 1);
// gain of the low bands (FMOD PARAMEQ, 1 = flat) and the compressor's make-up gain in dB. The defaults, chosen by ear in game,
// keep peaks under full scale (WeatherAudioTests); 3 / 10 crackled in the speakers. Limits 3 and 24
static AwesomeCVar s_cvThunderBass = AwesomeCVar::Float("weatherThunderBass", 2.2f, 1.0f, 3.0f, OnThunderSettings);
static AwesomeCVar s_cvThunderBoost = AwesomeCVar::Float("weatherThunderBoost", 8.0f, 0.0f, 24.0f, OnThunderSettings, "%.1f");
static AwesomeCVar s_cvSoundLayer = AwesomeCVar::Int("weatherSoundLayer", 1, 0, 1, OnCurveSettings);
static AwesomeCVar s_cvSoundIndoor = AwesomeCVar::Float("weatherSoundIndoor", 0.35f, 0.0f, 1.0f);
static AwesomeCVar s_cvAmbienceDuck = AwesomeCVar::Float("weatherAmbienceDuck", 0.6f, 0.0f, 1.0f);
// the distant thunder bed (DarkPortal, the thunderstorm row's own loop) under other rows while lightning runs
static AwesomeCVar s_cvRumbleKit = AwesomeCVar::Int("weatherRumbleKit", 4994, 0, 1000000);
static AwesomeCVar s_cvRumbleVolume = AwesomeCVar::Float("weatherRumbleVolume", 0.5f, 0.0f, 1.0f);

struct WeatherSource {
    bool m_valid = false;
    uint32_t m_id = 0;
    float m_grade = 0.0f;
    uint8_t m_abrupt = 0;
    Ext m_ext;
};

// the server's last packet, the /weather override, and what is applied now (one of the two)
static WeatherSource s_server;
static WeatherSource s_override;
static bool s_overrideActive;
static WeatherSource s_active;
static uint32_t s_activeType;
static int32_t s_activeKit;
// inside Apply: SetType's sky comes from the packet grade, not the particle grade it is given
static bool s_applying;
// apply again on the next frame (after a reset or a curve CVar change)
static bool s_reapply;

// lightning
static LightningScheduler s_scheduler;
static Strike s_strike;
static bool s_strikeActive;
static uint32_t s_strikeStartMs;
static bool s_flashOwned;
static uint32_t s_strikeCount;
static float s_lastRate;

struct PendingThunder {
    uint32_t m_dueMs;
    int32_t m_kit;
    int32_t m_layer;
    float m_volume;
    float m_pitch;
};

static const uint32_t MAX_PENDING_THUNDER = 24;
static PendingThunder s_thunder[MAX_PENDING_THUNDER];
static uint32_t s_thunderCount;

struct PendingRestrike {
    uint32_t m_dueMs;
    float m_distance;
};

static const uint32_t MAX_PENDING_RESTRIKES = 6;
static PendingRestrike s_restrikes[MAX_PENDING_RESTRIKES];
static uint32_t s_restrikeCount;

// a looping layer: the weather loop, the rumble bed
struct WeatherLoop {
    SI2ZoneSoundPair m_pair;
    int32_t m_kit = 0;
    uint32_t m_retryMs = 0;
    uint32_t m_soundId = 0;
    float m_base = 1.0f;
    float m_volume = 0.0f;
};

static WeatherLoop s_loop;
static WeatherLoop s_rumble;

// the thunder layers' FMOD chains (AwesomeThunderDsp), made when the first thunder of a layer plays
static ThunderDspChain s_chains[THUNDER_LAYER_COUNT] = {
    { "AwesomeThunderCrack", 0.0f, 1.6f, 0.8f, -1.0f },
    { "AwesomeThunderBody", 2600.0f, 1.0f, 1.0f, 0.0f },
    { "AwesomeThunderRoll", 1400.0f, 1.0f, 1.0f, -2.0f },
    { "AwesomeThunderDistant", 650.0f, 1.0f, 1.0f, -1.0f },
};

static uint32_t s_lastUpdateMs;

static float Clamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static void OnCurveSettings(AwesomeCVar* cvar, bool changed) {
    if (changed) {
        s_reapply = true;
    }
}

static void OnDensitySettings(AwesomeCVar* cvar, bool changed) {
    Weather_RefreshDensity();
}

float AwesomeWeatherParticleStart() {
    return s_cvParticleStart.m_floatValue;
}

float AwesomeWeatherParticleIntensity(float grade) {
    return ParticleIntensity(grade, s_cvParticleStart.m_floatValue);
}

float AwesomeWeatherSkyTarget(uint32_t type, float grade, bool* stock) {
    bool hasParticles = s_applying && s_active.m_ext.Has(EXT_PARTICLES);
    bool hasSky = s_applying && s_active.m_ext.Has(EXT_SKY);

    // with the server's particle intensity, SetType is handed the grade that gives it; the sky follows the packet's
    if (hasParticles) {
        grade = Clamp01(s_active.m_grade);
    }

    *stock = s_cvSkyCurve.m_intValue == 0 && !hasParticles && !hasSky;

    float sky = hasSky ? s_active.m_ext.Get(EXT_SKY, 0.0f) : SkyBlend(grade, type, s_cvSkyCurve.m_intValue, s_cvParticleStart.m_floatValue, s_cvSkyMin.m_floatValue);
    return sky * 0.25f;
}

float AwesomeWeatherTransitionSeconds(float delta) {
    return TransitionSeconds(delta, s_active.m_valid && s_active.m_ext.Has(EXT_TRANSITION) ? s_active.m_ext.Get(EXT_TRANSITION, 0.0f) : -1.0f);
}

bool AwesomeWeatherFixes() {
    return s_cvFixes.m_intValue != 0;
}

float AwesomeWeatherDensityScale() {
    float server = s_active.m_valid ? s_active.m_ext.Get(EXT_DENSITY, 1.0f) : 1.0f;
    return s_cvDensityScale.m_floatValue * server;
}

static void Apply(const WeatherSource& source) {
    Weather* weather = World::s_weather;

    if (!weather) {
        return;
    }

    s_active = source;
    s_active.m_valid = true;

    float transition = 1.0f;
    uint32_t type = 0;
    int32_t soundKitId = 0;
    const WeatherRec* rec = g_weatherDB.GetRecord(source.m_id);

    if (rec) {
        type = rec->m_effectType;
        transition = rec->m_transitionSkyBox;
        soundKitId = rec->m_ambienceID;
    }

    if (source.m_ext.Has(EXT_SOUND_KIT)) {
        soundKitId = static_cast<int32_t>(source.m_ext.Get(EXT_SOUND_KIT, 0.0f));
    }

    s_activeType = type;
    s_activeKit = soundKitId > 0 ? soundKitId : 0;
    Weather_RefreshDensity();

    float grade = source.m_grade;

    if (type != 0 && source.m_ext.Has(EXT_PARTICLES)) {
        grade = GradeForParticles(source.m_ext.Get(EXT_PARTICLES, 0.0f), s_cvParticleStart.m_floatValue);
    }

    s_applying = true;
    weather->SetType(type, grade, rec, source.m_abrupt == 0, transition);
    s_applying = false;

    // the layer plays the loop itself (AwesomeWeatherUpdate); stock replaces the zone ambience with it
    SI2::SetWeatherKitID(s_cvSoundLayer.m_intValue || soundKitId <= 0 ? -1 : soundKitId);
    ConsolePrintf("Weather changed to %d, intensity %f\n", type, source.m_grade);
}

void AwesomeWeatherOnPacket(uint32_t weatherId, float grade, uint8_t abrupt, const uint8_t* extra, uint32_t extraSize) {
    s_server.m_valid = true;
    s_server.m_id = weatherId;
    s_server.m_grade = grade;
    s_server.m_abrupt = abrupt;
    ParseExt(extra, extraSize, &s_server.m_ext);

    if (!s_overrideActive) {
        Apply(s_server);
    }
}

static void StopFlash() {
    if (s_flashOwned) {
        DayNight::SetFlashColor(CImVector(0xFF, 0xFF, 0xFF, 0xFF), 0.0f);
        s_flashOwned = false;
    }

    s_strikeActive = false;
}

static void StopLoop(WeatherLoop& loop, bool immediate) {
    if (loop.m_kit) {
        loop.m_pair.Stop(immediate ? 1 : 0);
    }

    loop.m_kit = 0;
    loop.m_soundId = 0;
    loop.m_volume = 0.0f;
}

static void StopLoops(bool immediate) {
    StopLoop(s_loop, immediate);
    StopLoop(s_rumble, immediate);
    SI2::SetZoneAmbienceScale(1.0f);
}

void AwesomeWeatherOnReset() {
    s_server = WeatherSource();
    s_active = WeatherSource();
    s_activeType = 0;
    s_activeKit = 0;
    s_thunderCount = 0;
    s_restrikeCount = 0;
    StopFlash();
    StopLoops(false);
    // an override survives teleports; it is applied again once the world runs
    s_reapply = s_overrideActive;
}

// --- thunder -----------------------------------------------------------------------------------------------------

static void OnThunderSettings(AwesomeCVar* cvar, bool changed) {
    for (ThunderDspChain& chain : s_chains) {
        if (chain.m_group) {
            ThunderDspApply(chain, s_cvThunderBass.m_floatValue, s_cvThunderBoost.m_floatValue);
        }
    }
}

void AwesomeWeatherAttachDspChain(FMOD::Channel* channel, const char* name) {
    // "AwesomeThunderBody:600": the layer's chain and the playback rate in thousandths
    if (!channel || !name || strncmp(name, "AwesomeThunder", 14)) {
        return;
    }

    const char* colon = strchr(name, ':');
    size_t length = colon ? static_cast<size_t>(colon - name) : strlen(name);

    for (ThunderDspChain& chain : s_chains) {
        if (strlen(chain.m_name) == length && !strncmp(chain.m_name, name, length)) {
            ThunderDspAttach(chain, channel, colon ? atoi(colon + 1) * 0.001f : 1.0f, s_cvThunderBass.m_floatValue, s_cvThunderBoost.m_floatValue);
            return;
        }
    }
}

void AwesomeWeatherSoundShutdown() {
    for (ThunderDspChain& chain : s_chains) {
        ThunderDspRelease(chain);
    }
}

static void PlayThunder(const PendingThunder& thunder) {
    if (thunder.m_kit <= 0 || thunder.m_layer < 0 || thunder.m_layer >= THUNDER_LAYER_COUNT) {
        return;
    }

    SE2SoundKitProperties properties;
    properties.ResetToDefaults();
    // the ambience channel, so the ambience volume slider and Sound_EnableAmbience apply
    properties.m_group = 2;
    properties.m_reverb = 0;
    // the layer's own level instead of the kit's (often quiet) SoundEntries volume
    properties.m_volume = Clamp01(thunder.m_volume);
    properties.m_useDspChain = 1;
    snprintf(properties.m_dspChainName, sizeof(properties.m_dspChainName), "%s:%d", s_chains[thunder.m_layer].m_name, static_cast<int32_t>(thunder.m_pitch * 1000.0f + 0.5f));
    SI2::PlaySoundKit(thunder.m_kit, nullptr, nullptr, &properties, 0, nullptr, 0, 0);
}

// --- lightning ---------------------------------------------------------------------------------------------------

static float StormLevel() {
    return s_active.m_ext.Get(EXT_STORM, -1.0f);
}

static void FireStrike(float distance, bool restrike) {
    const Ext& ext = s_active.m_ext;
    float storm = StormLevel();
    float nearChance = ext.Get(EXT_NEAR_CHANCE, storm >= 0.0f ? 0.05f + 0.3f * storm : -1.0f);
    float minDistance = ext.Get(EXT_LIGHTNING_DISTANCE, s_cvLightningDistance.m_floatValue);
    float maxDistance = ext.Get(EXT_STRIKE_MAX_DISTANCE, LIGHTNING_MAX_DISTANCE);

    s_strike = s_scheduler.MakeStrike(minDistance, maxDistance, nearChance, distance);
    s_strikeActive = true;
    s_strikeStartMs = OsGetAsyncTimeMs();
    s_strikeCount++;

    if (!restrike) {
        float burst = ext.Get(EXT_BURST, storm >= 0.0f ? 0.35f * storm : s_cvLightningBurst.m_floatValue);
        float delays[3];
        int32_t count = s_scheduler.RollBurst(burst, delays, 3);

        for (int32_t i = 0; i < count && s_restrikeCount < MAX_PENDING_RESTRIKES; i++) {
            PendingRestrike& next = s_restrikes[s_restrikeCount++];
            next.m_dueMs = s_strikeStartMs + static_cast<uint32_t>(delays[i] * 1000.0f);
            next.m_distance = s_strike.m_distance * (0.85f + 0.3f * s_scheduler.Random());
        }
    }

    if (!s_cvThunder.m_intValue) {
        return;
    }

    ThunderPart parts[THUNDER_MAX_PARTS];
    int32_t count = ThunderParts(s_strike.m_distance, s_cvThunderLayers.m_intValue != 0, s_scheduler, parts);
    int32_t nearKit = static_cast<int32_t>(ext.Get(EXT_THUNDER_NEAR_KIT, static_cast<float>(s_cvThunderNearKit.m_intValue)));
    int32_t farKit = static_cast<int32_t>(ext.Get(EXT_THUNDER_FAR_KIT, static_cast<float>(s_cvThunderFarKit.m_intValue)));
    float volume = s_strike.m_thunderVolume * s_cvThunderVolume.m_floatValue * ext.Get(EXT_THUNDER, 1.0f);

    if (SI2::sm_bPlayerInside) {
        volume *= 0.6f;
    }

    for (int32_t i = 0; i < count && s_thunderCount < MAX_PENDING_THUNDER; i++) {
        PendingThunder& thunder = s_thunder[s_thunderCount++];
        thunder.m_dueMs = s_strikeStartMs + static_cast<uint32_t>((s_strike.m_thunderDelay + parts[i].m_delay) * 1000.0f);
        thunder.m_kit = parts[i].m_layer == THUNDER_CRACK ? nearKit : farKit;
        thunder.m_layer = parts[i].m_layer;
        thunder.m_volume = volume * parts[i].m_volume;
        thunder.m_pitch = parts[i].m_pitch;
    }
}

static float LightningRate(float particles) {
    if (!s_cvLightning.m_intValue || !s_active.m_valid) {
        return 0.0f;
    }

    const Ext& ext = s_active.m_ext;
    // a server's rate fades in and out with the weather
    float level = s_activeType == 0 ? 1.0f : Clamp01(particles * 4.0f);

    if (ext.Has(EXT_LIGHTNING)) {
        return ext.Get(EXT_LIGHTNING, 0.0f) * level;
    }

    if (ext.Has(EXT_STORM)) {
        return s_cvLightningRate.m_floatValue * ext.Get(EXT_STORM, 0.0f) * level;
    }

    return AutoLightningRate(s_cvLightning.m_intValue, s_active.m_id, s_activeType, World::s_weather->m_intensity, s_cvLightningGrade.m_floatValue, s_cvLightningRate.m_floatValue);
}

static void UpdateLightning(uint32_t now, float dt, float particles) {
    float rate = LightningRate(particles);
    s_lastRate = rate;

    if (s_scheduler.Update(dt, rate)) {
        FireStrike(-1.0f, false);
    }

    for (uint32_t i = 0; i < s_restrikeCount;) {
        if (static_cast<int32_t>(now - s_restrikes[i].m_dueMs) < 0) {
            i++;
            continue;
        }

        float distance = s_restrikes[i].m_distance;
        s_restrikes[i] = s_restrikes[--s_restrikeCount];
        FireStrike(distance, true);
    }

    if (s_strikeActive) {
        float t = (now - s_strikeStartMs) * 0.001f;

        if (t >= s_strike.m_duration) {
            StopFlash();
        } else {
            float blend = FlashEnvelope(s_strike, t) * s_cvLightningBrightness.m_floatValue * s_active.m_ext.Get(EXT_FLASH, 1.0f);

            if (SI2::sm_bPlayerInside) {
                blend *= 0.3f;
            }

            // bluish white unless the server picks a colour
            uint8_t r = 0xC8;
            uint8_t g = 0xD7;
            uint8_t b = 0xFF;

            if (s_active.m_ext.Has(EXT_FLASH_COLOR)) {
                UnpackColor(s_active.m_ext.Get(EXT_FLASH_COLOR, 0.0f), &r, &g, &b);
            }

            DayNight::SetFlashColor(CImVector(b, g, r, 0xFF), Clamp01(blend));
            s_flashOwned = true;
        }
    }

    for (uint32_t i = 0; i < s_thunderCount;) {
        if (static_cast<int32_t>(now - s_thunder[i].m_dueMs) < 0) {
            i++;
            continue;
        }

        PlayThunder(s_thunder[i]);
        s_thunder[i] = s_thunder[--s_thunderCount];
    }
}

// --- loops -------------------------------------------------------------------------------------------------------

static bool AmbienceEnabled() {
    static CVar* s_enableAllSound = CVar::Lookup("Sound_EnableAllSound");
    static CVar* s_enableAmbience = CVar::Lookup("Sound_EnableAmbience");
    return !((s_enableAllSound && !s_enableAllSound->m_intValue) || (s_enableAmbience && !s_enableAmbience->m_intValue) || CGGameUI::m_inMovie);
}

// plays kit on loop (crossfading from the last one) at scale times the kit's own volume; kit 0 stops it
static void DriveLoop(WeatherLoop& loop, int32_t kit, float scale, uint32_t now) {
    if (!kit) {
        if (loop.m_kit) {
            StopLoop(loop, false);
        }

        return;
    }

    if (kit != loop.m_kit || (!loop.m_pair.IsPlaying() && static_cast<int32_t>(now - loop.m_retryMs) >= 0)) {
        SE2SoundKitProperties properties;
        properties.ResetToDefaults();
        properties.m_group = 2;
        properties.m_loopMode = 1;
        properties.m_priority = 100;
        properties.m_reverb = 0;
        properties.m_fadeInTime = 4.0f;
        loop.m_pair.Transition(kit, 8, &properties, 0, 4.0f);
        loop.m_kit = kit;
        // a kit that does not play (no file, group full) is tried again every 2 s, not every frame
        loop.m_retryMs = now + 2000;
    }

    SESound& sound = loop.m_pair.m_sounds[loop.m_pair.m_current].m_object.m_sound;
    SESoundInternal* internal = sound.m_internal;
    loop.m_volume = scale;

    if (!internal) {
        return;
    }

    if (internal->m_id != loop.m_soundId) {
        loop.m_soundId = internal->m_id;
        loop.m_base = internal->m_volume;
    }

    float volume = loop.m_base * scale;

    if (fabsf(internal->m_volume - volume) > 0.001f) {
        sound.SetVolume(volume);
    }
}

static void UpdateLoops(uint32_t now, float particles) {
    const Ext& ext = s_active.m_ext;
    bool enabled = s_cvSoundLayer.m_intValue && AmbienceEnabled();
    bool inside = SI2::sm_bPlayerInside != 0;
    float indoor = ext.Get(EXT_INDOOR, s_cvSoundIndoor.m_floatValue);

    float loopScale = LoopVolume(particles, s_activeType, inside, indoor) * ext.Get(EXT_VOLUME, 1.0f);
    DriveLoop(s_loop, enabled ? s_activeKit : 0, loopScale, now);

    // the rumble bed: the server's, or the distant thunder under any other row while lightning runs
    int32_t rumbleKit = 0;
    float rumble = 0.0f;
    float storm = StormLevel();

    if (ext.Has(EXT_RUMBLE_KIT)) {
        rumbleKit = static_cast<int32_t>(ext.Get(EXT_RUMBLE_KIT, 0.0f));
        rumble = ext.Get(EXT_RUMBLE, storm >= 0.0f ? storm : 1.0f);
    } else if (s_lastRate > 0.0f && s_cvRumbleVolume.m_floatValue > 0.0f) {
        rumbleKit = s_cvRumbleKit.m_intValue;
        float level = storm >= 0.0f ? storm : s_lastRate / (s_cvLightningRate.m_floatValue > 1.0f ? s_cvLightningRate.m_floatValue : 1.0f);
        rumble = ext.Get(EXT_RUMBLE, Clamp01(level)) * s_cvRumbleVolume.m_floatValue;
    }

    // never the weather loop twice (the thunderstorm row's loop is the rumble already)
    if (rumbleKit == s_activeKit || rumble <= 0.0f) {
        rumbleKit = 0;
    }

    DriveLoop(s_rumble, enabled ? rumbleKit : 0, Clamp01(rumble) * (inside ? indoor : 1.0f), now);

    // a sky-only row (fog) leaves the zone as it is
    float duck = ext.Get(EXT_DUCK, s_cvAmbienceDuck.m_floatValue);
    SI2::SetZoneAmbienceScale(enabled && s_activeType ? ZoneAmbienceDuck(particles, duck) : 1.0f);
}

void AwesomeWeatherUpdate() {
    Weather* weather = World::s_weather;
    uint32_t now = OsGetAsyncTimeMs();
    float dt = s_lastUpdateMs ? (now - s_lastUpdateMs) * 0.001f : 0.0f;
    s_lastUpdateMs = now;

    // a hitch is not a reason to fire a burst of strikes
    if (dt > 0.25f) {
        dt = 0.25f;
    }

    if (!weather) {
        return;
    }

    if (s_reapply) {
        s_reapply = false;

        if (s_overrideActive) {
            Apply(s_override);
        } else if (s_server.m_valid) {
            Apply(s_server);
        }
    }

    float particles = AwesomeWeatherParticleIntensity(weather->m_intensity);
    UpdateLightning(now, dt, particles);

    if (!s_cvSoundLayer.m_intValue && (s_loop.m_kit || s_rumble.m_kit)) {
        StopLoops(false);
        // back to the stock way: the zone ambience pair plays the weather kit
        SI2::SetWeatherKitID(s_activeKit > 0 ? s_activeKit : -1);
    } else if (s_cvSoundLayer.m_intValue) {
        UpdateLoops(now, particles);
    }
}

// --- Lua ---------------------------------------------------------------------------------------------------------

static void SetField(lua_State* L, const char* name, float value) {
    lua_pushnumber(L, value);
    lua_setfield(L, -2, name);
}

static void PushExt(lua_State* L, const Ext& ext) {
    lua_newtable(L);

    for (uint32_t key = 1; key < EXT_KEY_COUNT; key++) {
        if (ext.Has(key)) {
            SetField(L, ExtKeyName(key), ext.Get(key, 0.0f));
        }
    }
}

// GetWeatherInfo() -> table: what is applied, what the server sent, what the client is drawing
static int32_t Script_GetWeatherInfo(lua_State* L) {
    Weather* weather = World::s_weather;
    lua_newtable(L);

    SetField(L, "id", static_cast<float>(s_active.m_id));
    SetField(L, "grade", s_active.m_grade);
    SetField(L, "abrupt", s_active.m_abrupt);
    SetField(L, "type", static_cast<float>(s_activeType));
    SetField(L, "soundKit", static_cast<float>(s_activeKit));
    lua_pushboolean(L, s_active.m_valid);
    lua_setfield(L, -2, "valid");
    lua_pushboolean(L, s_overrideActive);
    lua_setfield(L, -2, "override");
    PushExt(L, s_active.m_ext);
    lua_setfield(L, -2, "ext");

    lua_pushboolean(L, s_server.m_valid);
    lua_setfield(L, -2, "serverValid");
    SetField(L, "serverId", static_cast<float>(s_server.m_id));
    SetField(L, "serverGrade", s_server.m_grade);
    PushExt(L, s_server.m_ext);
    lua_setfield(L, -2, "serverExt");

    if (weather) {
        SetField(L, "drawnType", static_cast<float>(weather->GetType()));
        SetField(L, "intensity", weather->m_intensity);
        SetField(L, "targetIntensity", weather->m_targetIntensity);
        SetField(L, "particles", AwesomeWeatherParticleIntensity(weather->m_intensity));
        lua_pushstring(L, weather->m_precipTexture);
        lua_setfield(L, -2, "texture");
    }

    SetField(L, "sky", DayNight::GetInfo()->m_stormBlend);
    SetField(L, "density", s_weatherDensity);
    SetField(L, "lightningRate", s_lastRate);
    SetField(L, "nextStrike", s_scheduler.NextIn());
    SetField(L, "strikes", static_cast<float>(s_strikeCount));
    SetField(L, "loopKit", static_cast<float>(s_loop.m_kit));
    SetField(L, "loopVolume", s_loop.m_volume);
    SetField(L, "rumbleKit", static_cast<float>(s_rumble.m_kit));
    SetField(L, "rumbleVolume", s_rumble.m_volume);
    lua_pushboolean(L, SI2::sm_bPlayerInside != 0);
    lua_setfield(L, -2, "inside");
    return 1;
}

// SetWeatherOverride(id, grade[, abrupt]): draw this Weather.dbc row locally until ClearWeatherOverride. Extension
// fields already set on the override are kept; set them with SetWeatherOverrideField
static int32_t Script_SetWeatherOverride(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetWeatherOverride(weatherId, grade[, abrupt])");
        return 0;
    }

    uint32_t id = static_cast<uint32_t>(lua_tonumber(L, 1));
    Ext ext = s_overrideActive ? s_override.m_ext : Ext();
    s_override = WeatherSource();
    s_override.m_valid = true;
    s_override.m_id = id;
    s_override.m_grade = Clamp01(lua_isnumber(L, 2) ? static_cast<float>(lua_tonumber(L, 2)) : 0.7f);
    s_override.m_abrupt = lua_toboolean(L, 3) ? 1 : 0;
    s_override.m_ext = ext;
    s_overrideActive = true;
    Apply(s_override);

    lua_pushboolean(L, g_weatherDB.GetRecord(id) != nullptr || id == 0);
    return 1;
}

// SetWeatherOverrideField(name, value): an extension field on the override (GetWeatherFields lists them); nil
// clears it. Starts an override from the server's weather if none is active
static int32_t Script_SetWeatherOverrideField(lua_State* L) {
    uint32_t key = ExtKeyFromName(lua_tostring(L, 1));

    if (!key) {
        luaL_error(L, "Usage: SetWeatherOverrideField(name, value or nil); GetWeatherFields() lists the names");
        return 0;
    }

    if (!s_overrideActive) {
        s_override = s_server.m_valid ? s_server : WeatherSource();
        s_override.m_valid = true;
        s_overrideActive = true;
    }

    if (lua_isnumber(L, 2)) {
        s_override.m_ext.Set(key, static_cast<float>(lua_tonumber(L, 2)));
    } else {
        s_override.m_ext.Clear(key);
    }

    // a field is a change of the same weather, never abrupt
    s_override.m_abrupt = 0;
    Apply(s_override);
    return 0;
}

static int32_t Script_ClearWeatherOverride(lua_State* L) {
    if (!s_overrideActive) {
        return 0;
    }

    s_overrideActive = false;
    s_override = WeatherSource();

    if (s_server.m_valid) {
        Apply(s_server);
    } else {
        WeatherSource clear;
        Apply(clear);
    }

    return 0;
}

// TriggerLightning([yards]): one strike now, at that distance or a random one; returns the distance and the
// seconds until its thunder
static int32_t Script_TriggerLightning(lua_State* L) {
    float distance = lua_isnumber(L, 1) ? static_cast<float>(lua_tonumber(L, 1)) : -1.0f;
    FireStrike(distance, false);
    lua_pushnumber(L, s_strike.m_distance);
    lua_pushnumber(L, s_strike.m_thunderDelay);
    return 2;
}

// GetWeatherRows() -> { { id, type, soundKit, texture }, ... } from the client's Weather.dbc
static int32_t Script_GetWeatherRows(lua_State* L) {
    lua_newtable(L);
    int32_t index = 1;

    for (int32_t i = 0; i < static_cast<int32_t>(g_weatherDB.m_numRecords); i++) {
        const WeatherRec* rec = &g_weatherDB.m_records[i];
        lua_newtable(L);
        SetField(L, "id", static_cast<float>(rec->m_id));
        SetField(L, "type", static_cast<float>(rec->m_effectType));
        SetField(L, "soundKit", static_cast<float>(rec->m_ambienceID));
        lua_pushstring(L, rec->m_effectTexture ? rec->m_effectTexture : "");
        lua_setfield(L, -2, "texture");
        lua_rawseti(L, -2, index++);
    }

    return 1;
}

// GetWeatherFields() -> { { name, key, min, max }, ... }: the extension fields, in key order
static int32_t Script_GetWeatherFields(lua_State* L) {
    lua_newtable(L);
    int32_t index = 1;

    for (uint32_t key = 1; key < EXT_KEY_COUNT; key++) {
        float min = 0.0f;
        float max = 0.0f;
        ExtKeyRange(key, &min, &max);
        lua_newtable(L);
        lua_pushstring(L, ExtKeyName(key));
        lua_setfield(L, -2, "name");
        SetField(L, "key", static_cast<float>(key));
        SetField(L, "min", min);
        SetField(L, "max", max);
        lua_rawseti(L, -2, index++);
    }

    return 1;
}

static const char* s_slashCommand = R"lua(
local presets = {
    clear = { 0, 0 }, fine = { 0, 0 }, fog = { 1, 0.15 }, overcast = { 1, 0.25 },
    drizzle = { 3, 0.32 }, lightrain = { 3, 0.45 }, rain = { 4, 0.65 }, heavyrain = { 5, 0.9 },
    storm = { 86, 0.9 },
    lightsnow = { 6, 0.45 }, snow = { 7, 0.65 }, heavysnow = { 8, 0.9 }, blizzard = { 8, 1 },
    lightsand = { 41, 0.45 }, sand = { 42, 0.65 }, sandstorm = { 22, 0.95 },
    blackrain = { 90, 0.8 }, redrain = { 100, 0.8 }, mist = { 104, 0.6 },
}
local fields = {}
for _, f in ipairs(GetWeatherFields()) do fields[f.name] = f end
local typeNames = { [0] = "none", "rain", "snow", "sand" }
local function say(text) print("|cff33ff99Weather|r " .. text) end
local function fmt(v) return v and ("%.2f"):format(v) or "-" end
local function extText(ext)
    local parts = {}
    for k, v in pairs(ext or {}) do parts[#parts + 1] = ("%s=%s"):format(k, fmt(v)) end
    table.sort(parts)
    return #parts > 0 and table.concat(parts, " ") or "none"
end
local function status()
    local w = GetWeatherInfo()
    say(("%s: id %d grade %s (%s), type %s%s"):format(w.override and "|cffff8800OVERRIDE|r" or "server",
        w.id, fmt(w.grade), w.abrupt == 1 and "abrupt" or "smooth", typeNames[w.type] or w.type,
        w.valid and "" or " (nothing received yet)"))
    say(("drawn %s, intensity %s -> %s, particles %s, sky %s, density %s, texture %s"):format(
        typeNames[w.drawnType or 0] or "?", fmt(w.intensity), fmt(w.targetIntensity), fmt(w.particles), fmt(w.sky),
        fmt(w.density), (w.texture ~= "" and w.texture) or "-"))
    say(("lightning %s/min, next in %s s, %d strikes; loop kit %d at %s, rumble kit %d at %s%s"):format(
        fmt(w.lightningRate), w.nextStrike >= 0 and fmt(w.nextStrike) or "-", w.strikes, w.loopKit, fmt(w.loopVolume),
        w.rumbleKit, fmt(w.rumbleVolume), w.inside and " (inside)" or ""))
    say("extension: " .. extText(w.ext))
    if w.override then
        say(("server underneath: id %d grade %s, extension %s"):format(w.serverId, fmt(w.serverGrade), extText(w.serverExt)))
    end
end
local function help()
    say("/weather - status")
    say("/weather <preset> [grade] [now] - clear fog overcast drizzle lightrain rain heavyrain storm lightsnow snow heavysnow blizzard lightsand sand sandstorm blackrain redrain mist")
    say("/weather id <weatherId> [grade] [now] - any Weather.dbc row (/weather list)")
    say("/weather grade <0-1> - change the override's grade")
    say("/weather <field> <value|off> - extension fields (/weather fields)")
    say("/weather strike [yards] - one lightning strike now")
    say("/weather off - drop the override, back to the server's weather")
end
SLASH_WEATHEROVERHAUL1 = "/weather"
SLASH_WEATHEROVERHAUL2 = "/wx"
SlashCmdList.WEATHEROVERHAUL = function(msg)
    local args = {}
    for word in (msg or ""):lower():gmatch("%S+") do args[#args + 1] = word end
    local cmd = args[1]
    if not cmd or cmd == "status" then
        status()
    elseif cmd == "help" or cmd == "?" then
        help()
    elseif cmd == "off" or cmd == "server" or cmd == "reset" then
        ClearWeatherOverride()
        say("override off, server weather restored")
    elseif cmd == "list" then
        local names = {}
        for _, r in ipairs(GetWeatherRows()) do
            names[#names + 1] = ("%d:%s%s%s"):format(r.id, typeNames[r.type] or r.type,
                r.soundKit > 0 and "+snd" or "", r.texture ~= "" and "+tex" or "")
        end
        say(table.concat(names, " "))
    elseif cmd == "fields" then
        local names = {}
        for _, f in ipairs(GetWeatherFields()) do
            names[#names + 1] = ("%d %s (%g-%g)"):format(f.key, f.name, f.min, f.max)
        end
        say(table.concat(names, ", "))
    elseif cmd == "strike" then
        local distance, delay = TriggerLightning(tonumber(args[2]))
        say(("strike at %d yd, thunder in %.1f s"):format(distance, delay))
    elseif cmd == "grade" then
        local w = GetWeatherInfo()
        local grade = tonumber(args[2])
        if not grade then return help() end
        SetWeatherOverride(w.id, grade)
        status()
    elseif cmd == "id" then
        local id = tonumber(args[2])
        if not id then return help() end
        if not SetWeatherOverride(id, tonumber(args[3]) or 0.7, args[4] == "now" or args[3] == "now") then
            say(("id %d is not in Weather.dbc (drawn as clear)"):format(id))
        end
        status()
    elseif presets[cmd] then
        local p = presets[cmd]
        local now = args[2] == "now" or args[3] == "now"
        SetWeatherOverride(p[1], tonumber(args[2]) or p[2], now)
        status()
    elseif fields[cmd] then
        if args[2] == "off" or args[2] == "nil" then
            SetWeatherOverrideField(cmd, nil)
        elseif tonumber(args[2]) then
            SetWeatherOverrideField(cmd, tonumber(args[2]))
        else
            local f = fields[cmd]
            return say(("%s takes a number %g-%g, or off"):format(cmd, f.min, f.max))
        end
        status()
    else
        help()
    end
end
)lua";

void AwesomeWeatherRegisterCVars() {
    s_cvFixes.Register();
    s_cvParticleStart.Register();
    s_cvSkyCurve.Register();
    s_cvSkyMin.Register();
    s_cvDensityScale.Register();
    s_cvLightning.Register();
    s_cvLightningRate.Register();
    s_cvLightningGrade.Register();
    s_cvLightningBrightness.Register();
    s_cvLightningDistance.Register();
    s_cvLightningBurst.Register();
    s_cvThunder.Register();
    s_cvThunderVolume.Register();
    s_cvThunderNearKit.Register();
    s_cvThunderFarKit.Register();
    s_cvThunderLayers.Register();
    s_cvThunderBass.Register();
    s_cvThunderBoost.Register();
    s_cvSoundLayer.Register();
    s_cvSoundIndoor.Register();
    s_cvAmbienceDuck.Register();
    s_cvRumbleKit.Register();
    s_cvRumbleVolume.Register();
}

void AwesomeWeatherRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetWeatherInfo", Script_GetWeatherInfo },
        { "SetWeatherOverride", Script_SetWeatherOverride },
        { "SetWeatherOverrideField", Script_SetWeatherOverrideField },
        { "ClearWeatherOverride", Script_ClearWeatherOverride },
        { "TriggerLightning", Script_TriggerLightning },
        { "GetWeatherRows", Script_GetWeatherRows },
        { "GetWeatherFields", Script_GetWeatherFields },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}

void AwesomeWeatherOnEnterWorld() {
    s_scheduler.Seed(OsGetAsyncTimeMs() | 1);
    FrameScript_Execute(s_slashCommand, "Weather", nullptr);
}

void AwesomeWeatherOnLeaveWorld() {
    s_overrideActive = false;
    s_override = WeatherSource();
    s_thunderCount = 0;
    s_restrikeCount = 0;
    StopFlash();
    StopLoops(true);
}
