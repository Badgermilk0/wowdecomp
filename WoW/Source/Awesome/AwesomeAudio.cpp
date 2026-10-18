// Audio engine upgrade (not from awesome_wotlk): the game half of the SoundEngine changes marked "audio engine upgrade".
// - Occlusion: every playing 3D sound is ray-tested against the world (terrain, WMOs, collidable game objects; not
//   trees or other doodads) from the character's head. A blocked source is muffled and quieter; one blocked low but
//   open over the top (a fence, a hill crest) half as much. SE2::UpdateOcclusion picks the sources and eases the result.
// - Reverb: zone changes crossfade (Sound_ReverbFade). Where the DBC gives the place no reverb (most WMO interiors and
//   all water), Sound_ReverbFallback picks one: underwater, or a small / medium / large room from rays cast round the
//   character indoors.
// - Voices: Sound_NumChannels goes up to 256 (stock 128, default 64 instead of 32), inaudible sounds give up their
//   voice, and Sound_KeepVirtualLoops keeps a loop that lost its voice so it comes back (stock stopped it for good).
// GetSoundStats() and /soundstats report what the engine is doing.

#include <cmath>
#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/SoundEngine/SoundEngine.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/SoundInterface/SoundInterface2.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/WorldClient/World.h"

// World::Intersect flags: 0x10 WMOs, 0x100 terrain, 0x100000 collidable game objects (doors, gates). Doodads (0x1:
// trees, fences, crates) are left out: they barely stop sound and would flicker the occlusion as things move past.
static const uint32_t AUDIO_OCCLUSION_FLAGS = 0x100110;
// the room estimate only asks for walls and ceilings
static const uint32_t AUDIO_ROOM_FLAGS = 0x110;
static const float AUDIO_ROOM_RAY_LENGTH = 40.0f;

static void OnOcclusionSettings(AwesomeCVar* cvar, bool changed);
static void OnReverbSettings(AwesomeCVar* cvar, bool changed);

static AwesomeCVar s_cvOcclusion = AwesomeCVar::Int("Sound_EnableOcclusion", 1, 0, 1, OnOcclusionSettings);
// direct-path occlusion of a fully blocked source: 0.7 = 30% of the volume, low-passed to about 4 kHz
static AwesomeCVar s_cvOcclusionStrength = AwesomeCVar::Float("Sound_OcclusionStrength", 0.7f, 0.0f, 1.0f, OnOcclusionSettings);
static AwesomeCVar s_cvOcclusionTests = AwesomeCVar::Int("Sound_OcclusionTests", 8, 1, 64, OnOcclusionSettings);
static AwesomeCVar s_cvOcclusionFade = AwesomeCVar::Float("Sound_OcclusionFade", 0.25f, 0.0f, 2.0f, OnOcclusionSettings);
static AwesomeCVar s_cvKeepVirtualLoops = AwesomeCVar::Int("Sound_KeepVirtualLoops", 1, 0, 1, OnOcclusionSettings);
static AwesomeCVar s_cvReverbFade = AwesomeCVar::Float("Sound_ReverbFade", 1.5f, 0.0f, 10.0f, OnReverbSettings);
static AwesomeCVar s_cvReverbFallback = AwesomeCVar::Int("Sound_ReverbFallback", 1, 0, 1, OnReverbSettings);

// where the occlusion rays start this frame: the character's head, or the camera without a character
static C3Vector s_rayStart;
static uint32_t s_lastUpdate;

// the room estimate: -1 none, else an index into s_roomPresets; a new size must be seen twice before it is used
static int32_t s_roomSize = -1;
static int32_t s_roomCandidate = -1;
static uint32_t s_lastRoomCheck;
// what ApplyReverbSettings last chose from (fallback on, inside, underwater, room size), to re-apply on a change
static int32_t s_appliedKey = -2;

static const FMOD_REVERB_PROPERTIES s_roomPresets[] = {
    FMOD_PRESET_ROOM,
    FMOD_PRESET_STONEROOM,
    FMOD_PRESET_AUDITORIUM,
};

static const char* s_roomNames[] = { "small room", "stone room", "hall" };

static void OnOcclusionSettings(AwesomeCVar* cvar, bool changed) {
    SE2::s_audioSettings.m_occlusion = s_cvOcclusion.m_intValue != 0;
    SE2::s_audioSettings.m_occlusionTestsPerFrame = s_cvOcclusionTests.m_intValue;
    SE2::s_audioSettings.m_occlusionFade = s_cvOcclusionFade.m_floatValue;
    SE2::s_audioSettings.m_keepVirtualLoops = s_cvKeepVirtualLoops.m_intValue != 0;
}

static void OnReverbSettings(AwesomeCVar* cvar, bool changed) {
    SE2::s_audioSettings.m_reverbFade = s_cvReverbFade.m_floatValue;

    if (changed && cvar == &s_cvReverbFallback) {
        s_appliedKey = -2;
    }
}

static bool RayBlocked(const C3Vector& start, const C3Vector& end, uint32_t flags, float* fraction) {
    C3Vector hit(0.0f, 0.0f, 0.0f);
    float distance = 1.0f;

    if (World::Intersect(start, end, &hit, &distance, flags, nullptr)) {
        if (fraction) {
            *fraction = distance;
        }

        return true;
    }

    if (fraction) {
        *fraction = 1.0f;
    }

    return false;
}

static float OcclusionTest(const C3Vector& position) {
    float strength = s_cvOcclusionStrength.m_floatValue;

    // the character's own sounds (footsteps, spells, a mount) sit under the head: never occluded. The engine's
    // distance check is from the listener, which stands behind and above the character
    float dx = position.x - s_rayStart.x;
    float dy = position.y - s_rayStart.y;

    if (dx * dx + dy * dy < 9.0f) {
        return 0.0f;
    }

    // stop a yard short of the source, so the wall a torch or a door sound sits on does not hide it
    C3Vector low(position.x, position.y, position.z + 0.5f);
    C3Vector delta = low - s_rayStart;
    float length = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);

    if (length < 1.5f) {
        return 0.0f;
    }

    C3Vector end = s_rayStart + delta * ((length - 1.0f) / length);

    if (!RayBlocked(s_rayStart, end, AUDIO_OCCLUSION_FLAGS, nullptr)) {
        return 0.0f;
    }

    // blocked low; heard over the top of a low wall or a crest
    C3Vector high(end.x, end.y, end.z + 2.0f);

    if (!RayBlocked(s_rayStart, high, AUDIO_OCCLUSION_FLAGS, nullptr)) {
        return strength * 0.5f;
    }

    return strength;
}

// eight walls and a ceiling: the mean distance to the walls sizes the room
static int32_t EstimateRoomSize() {
    float total = 0.0f;

    for (int32_t i = 0; i < 8; i++) {
        float angle = static_cast<float>(i) * 0.7853982f;
        C3Vector end(s_rayStart.x + cosf(angle) * AUDIO_ROOM_RAY_LENGTH, s_rayStart.y + sinf(angle) * AUDIO_ROOM_RAY_LENGTH, s_rayStart.z);
        float fraction = 1.0f;
        RayBlocked(s_rayStart, end, AUDIO_ROOM_FLAGS, &fraction);
        total += fraction * AUDIO_ROOM_RAY_LENGTH;
    }

    float mean = total / 8.0f;
    C3Vector up(s_rayStart.x, s_rayStart.y, s_rayStart.z + AUDIO_ROOM_RAY_LENGTH);
    float ceiling = 1.0f;
    bool roofed = RayBlocked(s_rayStart, up, AUDIO_ROOM_FLAGS, &ceiling);

    if (!roofed || mean >= 16.0f || ceiling * AUDIO_ROOM_RAY_LENGTH >= 20.0f) {
        return 2;
    }

    return mean < 7.0f ? 0 : 1;
}

static bool HasZoneReverb() {
    return g_soundProviderPreferencesDB.GetRecord(SI2::GetZoneReverb()) != nullptr;
}

static void UpdateReverbFallback(uint32_t now) {
    if (now - s_lastRoomCheck < 250) {
        return;
    }

    s_lastRoomCheck = now;

    bool fallback = s_cvReverbFallback.m_intValue != 0;
    bool inside = SI2::sm_bPlayerInside != 0;
    bool underwater = SI2::sm_bCameraIsUnderwater != 0;

    if (fallback && inside && !underwater && !HasZoneReverb()) {
        int32_t size = EstimateRoomSize();

        if (size == s_roomCandidate || s_roomSize < 0) {
            s_roomSize = size;
        }

        s_roomCandidate = size;
    } else {
        s_roomCandidate = -1;

        if (!inside) {
            s_roomSize = -1;
        }
    }

    int32_t key = !fallback ? -1 : (underwater ? 100 : (inside ? s_roomSize : 200));

    if (key != s_appliedKey) {
        s_appliedKey = key;
        SI2::ApplyReverbSettings();
    }
}

bool AwesomeAudioFallbackReverb(FMOD_REVERB_PROPERTIES* properties) {
    if (!s_cvReverbFallback.m_intValue) {
        return false;
    }

    if (SI2::sm_bCameraIsUnderwater) {
        FMOD_REVERB_PROPERTIES underwater = FMOD_PRESET_UNDERWATER;
        *properties = underwater;
        return true;
    }

    if (SI2::sm_bPlayerInside && s_roomSize >= 0) {
        *properties = s_roomPresets[s_roomSize];
        return true;
    }

    return false;
}

void AwesomeAudioUpdate(const C3Vector& listener) {
    uint32_t now = OsGetAsyncTimeMs();
    float elapsed = s_lastUpdate ? static_cast<float>(now - s_lastUpdate) * 0.001f : 0.0f;
    s_lastUpdate = now;

    if (elapsed > 0.25f) {
        elapsed = 0.25f;
    }

    s_rayStart = listener;
    CGObject_C* focus = CGObject_C::GetFocusObject();

    if (focus && (focus->m_obj->m_type & TYPEMASK_UNIT)) {
        CGUnit_C* unit = static_cast<CGUnit_C*>(focus);
        s_rayStart = unit->GetPosition();
        float height = unit->m_movementData.m_collisionHeight;
        s_rayStart.z += height > 0.5f ? height * 0.9f : 1.8f;
    } else if (CGCamera* camera = CGWorldFrame::GetActiveCamera()) {
        s_rayStart = camera->m_position;
    }

    SE2::UpdateOcclusion(OcclusionTest, elapsed);
    UpdateReverbFallback(now);
}

static int32_t Script_GetSoundStats(lua_State* L) {
    SEAudioStats stats;
    SE2::GetAudioStats(&stats);

    lua_pushnumber(L, stats.m_softwareChannels);
    lua_pushnumber(L, stats.m_real);
    lua_pushnumber(L, stats.m_virtual);
    lua_pushnumber(L, stats.m_sounds);
    lua_pushnumber(L, stats.m_tracked3D);
    lua_pushnumber(L, stats.m_occluded);
    lua_pushnumber(L, stats.m_testsLastFrame);
    lua_pushnumber(L, stats.m_reverbRoom);
    lua_pushnumber(L, stats.m_reverbDecay);
    lua_pushnumber(L, stats.m_reverbBlend);
    lua_pushnumber(L, stats.m_cpuDsp);
    lua_pushnumber(L, stats.m_cpuTotal);

    if (SI2::sm_bPlayerInside && s_roomSize >= 0 && !HasZoneReverb() && s_cvReverbFallback.m_intValue) {
        lua_pushstring(L, s_roomNames[s_roomSize]);
    } else if (SI2::sm_bCameraIsUnderwater && !HasZoneReverb() && s_cvReverbFallback.m_intValue) {
        lua_pushstring(L, "underwater");
    } else {
        lua_pushstring(L, HasZoneReverb() ? "zone" : "none");
    }

    return 13;
}

static const char* s_slashCommand = R"lua(
SLASH_SOUNDSTATS1 = "/soundstats"
SlashCmdList.SOUNDSTATS = function()
    local voices, real, virtual, sounds, tracked, occluded, tests, room, decay, blend, dsp, total, source = GetSoundStats()
    print(("|cff33ff99Voices|r %d of %d playing, %d virtual, %d sounds; fmod cpu %.1f%% (dsp %.1f%%)"):format(
        real, voices, virtual, sounds, total, dsp))
    print(("|cff33ff99Occlusion|r %d 3D sources, %d occluded, %d tested last frame"):format(tracked, occluded, tests))
    if room > -10000 then
        print(("|cff33ff99Reverb|r %s: room %d mB, decay %.2f s%s"):format(source, room, decay,
            blend < 1 and (" (fading, %d%%)"):format(blend * 100) or ""))
    else
        print(("|cff33ff99Reverb|r off (%s)"):format(source))
    end
end
)lua";

void AwesomeAudioRegisterCVars() {
    s_cvOcclusion.Register();
    s_cvOcclusionStrength.Register();
    s_cvOcclusionTests.Register();
    s_cvOcclusionFade.Register();
    s_cvKeepVirtualLoops.Register();
    s_cvReverbFade.Register();
    s_cvReverbFallback.Register();
}

void AwesomeAudioRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetSoundStats", Script_GetSoundStats },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}

void AwesomeAudioOnEnterWorld() {
    s_appliedKey = -2;
    s_roomSize = -1;
    s_roomCandidate = -1;
    FrameScript_Execute(s_slashCommand, "SoundStats", nullptr);
}
