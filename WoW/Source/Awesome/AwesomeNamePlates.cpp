#include "WoW/Source/Awesome/AwesomeNamePlates.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Frame/CLayoutFrame.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C2Vector.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/ObjectClient/UnitCombatLog_C.h"
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/Object/Unit.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/NamePlateFrame.h"
#include "WoW/Source/Ui/Types.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/UIUtil/InputControl.h"
#include "WoW/Source/WorldClient/Map.h"

// Nameplates from awesome_wotlk (NamePlates.cpp, NamePlateManager.*, NamePlateEntry.*, NamePlateSettings.h):
// - C_NamePlate.GetNamePlates / GetNamePlateForUnit / GetNamePlateByGUID / GetNamePlateTokenByGUID, the
//   nameplateN unit tokens and the NAME_PLATE_CREATED / _UNIT_ADDED / _UNIT_REMOVED events
// - plate methods Set/GetStackingEnabled, Set/GetOcclusionEnabled
// - stacking (plates push each other up instead of overlapping), screen edge clamping, a mouse freeze that pins the
//   plate under the cursor, fading of non-target and occluded plates, nameplateDistance up to 200 yards
// The engine's own layout pass (UnitNameplatesReposition) is replaced by EntryManager::Update, which runs every
// frame; the motion model below is a direct port.

// ---------------------------------------------------------------------------------------------------------------------
// Small vector helpers (awesome's Vec2f / Rectf)

namespace {

struct Vec2 {
    float x;
    float y;

    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(float x, float y) : x(x), y(y) {}

    float& operator[](size_t index) { return index == 0 ? this->x : this->y; }
    float operator[](size_t index) const { return index == 0 ? this->x : this->y; }
    Vec2 operator+(const Vec2& v) const { return Vec2(this->x + v.x, this->y + v.y); }
    Vec2 operator-(const Vec2& v) const { return Vec2(this->x - v.x, this->y - v.y); }
    Vec2 operator*(const Vec2& v) const { return Vec2(this->x * v.x, this->y * v.y); }
    Vec2 operator*(float s) const { return Vec2(this->x * s, this->y * s); }
    Vec2 operator/(float s) const { return Vec2(this->x / s, this->y / s); }
};

// min_y, min_x, max_y, max_x like CRect
struct Rect {
    float minY;
    float minX;
    float maxY;
    float maxX;

    Vec2 Clamp(const Vec2& p) const {
        return Vec2(std::min(std::max(p.x, this->minX), this->maxX), std::min(std::max(p.y, this->minY), this->maxY));
    }
};

const float EPSILON = 1e-4f;
const float TUNED_FPS = 60.0f;
const uint32_t MAX_PLATES = 768;

// CLayoutFrame::m_layoutFlags
const uint32_t LAYOUT_FLAG_VALID_RECT = 0x100;

// CGInputControl::m_flags while a mouse button turns or steers the camera
const uint32_t INPUT_TURN_OR_ACTION = 0x1;
const uint32_t INPUT_CAMERA_OR_SELECT = 0x2;
const uint32_t INPUT_MOUSE_LOOK = 0x2000000;
const uint32_t INPUT_MOUSE_STEER = 0x4000000;
const uint32_t INPUT_MOUSE_BUSY = INPUT_TURN_OR_ACTION | INPUT_CAMERA_OR_SELECT | INPUT_MOUSE_LOOK | INPUT_MOUSE_STEER;

// the default plate size (CGNamePlateFrame::Initialize)
const float PLATE_DEFAULT_WIDTH = 0.1f;
const float PLATE_DEFAULT_HEIGHT = 0.025f;

// World::Intersect flags for the camera-to-plate occlusion ray (terrain, WMO, doodads)
const uint32_t WORLD_TRACE_HIT_FLAGS = 0x100111;

enum CreatureRank {
    RANK_NORMAL = 0,
    RANK_ELITE = 1,
    RANK_RARE_ELITE = 2,
    RANK_WORLDBOSS = 3,
    RANK_RARE = 4,
    RANK_TRIVIAL = 5,
};

enum StackingMode { STACKING_DISABLED = 0, STACKING_ALL = 1, STACKING_ENEMY = 2, STACKING_FRIENDLY = 3 };
enum ClampMode { CLAMP_DISABLED = 0, CLAMP_ALL = 1, CLAMP_BOSS = 2, CLAMP_ALL_EDGES = 3, CLAMP_BOSS_EDGES = 4 };
enum ClampFilter { CLAMP_FILTER_TARGET = 1, CLAMP_FILTER_COMBAT = 2 };
enum OcclusionMode { OCCLUSION_ALWAYS = 0, OCCLUSION_NOT_IN_COMBAT = 1 };
enum FreezeMode { FREEZE_DISABLED = 0, FREEZE_ALWAYS = 1, FREEZE_COMBAT = 2 };

enum MouseMode : uint32_t {
    MOUSE_CLICK_THRU_ENEMY = 1 << 0,
    MOUSE_CLICK_THRU_FRIEND = 1 << 1,
    MOUSE_OVER_ALWAYS = 1 << 2,
    MOUSE_OVER_COMBAT = 1 << 3,
};

const uint32_t MOUSE_MODE_MAP[] = {
    0,
    MOUSE_CLICK_THRU_ENEMY,
    MOUSE_CLICK_THRU_ENEMY | MOUSE_OVER_ALWAYS,
    MOUSE_CLICK_THRU_ENEMY | MOUSE_OVER_COMBAT,
    MOUSE_CLICK_THRU_FRIEND,
    MOUSE_CLICK_THRU_FRIEND | MOUSE_OVER_ALWAYS,
    MOUSE_CLICK_THRU_FRIEND | MOUSE_OVER_COMBAT,
    MOUSE_OVER_ALWAYS,
    MOUSE_OVER_COMBAT,
};

enum ClickLogic { CLICK_NONE, CLICK_THRU_ENEMY, CLICK_THRU_FRIEND };
enum MouseInput { MOUSE_IDLE, MOUSE_CLICK_PENDING, MOUSE_DRAGGING };

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Settings

static void OnMarkDirty(AwesomeCVar* cvar, bool changed);
static void OnDerived(AwesomeCVar* cvar, bool changed);
static void OnDistance(AwesomeCVar* cvar, bool changed);
static void OnClampMode(AwesomeCVar* cvar, bool changed);
static void OnStackingMode(AwesomeCVar* cvar, bool changed);

static AwesomeCVar s_cvDistance = AwesomeCVar::Float("nameplateDistance", 41.0f, 41.0f, 200.0f, OnDistance);
static AwesomeCVar s_cvPlacement = AwesomeCVar::Float("nameplatePlacement", 0.0f, -1.0f, 2.0f, OnMarkDirty, "%.4f");
static AwesomeCVar s_cvMouseMode = AwesomeCVar::Int("nameplateMouseMode", 0, 0, 8, OnDerived);
static AwesomeCVar s_cvBandX = AwesomeCVar::Float("nameplateBandX", 0.7f, 0.1f, 1.0f, OnMarkDirty);
static AwesomeCVar s_cvBandY = AwesomeCVar::Float("nameplateBandY", 1.0f, 0.1f, 1.5f, OnMarkDirty);
static AwesomeCVar s_cvHitboxAnchor = AwesomeCVar::Int("nameplateHitboxAnchor", 1, 0, 2, OnMarkDirty);
static AwesomeCVar s_cvHitboxWidthE = AwesomeCVar::Float("nameplateHitboxWidthE", 1.0f, 0.0f, 1.0f, OnMarkDirty);
static AwesomeCVar s_cvHitboxHeightE = AwesomeCVar::Float("nameplateHitboxHeightE", 1.0f, 0.0f, 1.0f, OnMarkDirty);
static AwesomeCVar s_cvHitboxWidthF = AwesomeCVar::Float("nameplateHitboxWidthF", 1.0f, 0.0f, 1.0f, OnMarkDirty);
static AwesomeCVar s_cvHitboxHeightF = AwesomeCVar::Float("nameplateHitboxHeightF", 1.0f, 0.0f, 1.0f, OnMarkDirty);
static AwesomeCVar s_cvRaiseSpeed = AwesomeCVar::Float("nameplateRaiseSpeed", 100.0f, 1.0f, 250.0f, OnDerived);
static AwesomeCVar s_cvLowerSpeed = AwesomeCVar::Float("nameplateLowerSpeed", 100.0f, 1.0f, 250.0f, OnDerived);
static AwesomeCVar s_cvPullSpeed = AwesomeCVar::Float("nameplatePullSpeed", 50.0f, 1.0f, 250.0f, OnDerived);
static AwesomeCVar s_cvRaiseDistance = AwesomeCVar::Float("nameplateRaiseDistance", 8.0f, 1.0f, 20.0f, OnMarkDirty);
static AwesomeCVar s_cvPullDistance = AwesomeCVar::Float("nameplatePullDistance", 0.25f, 0.0f, 0.75f, OnMarkDirty);
static AwesomeCVar s_cvOcclusionAlpha = AwesomeCVar::Float("nameplateOcclusionAlpha", 1.0f, -1.0f, 1.0f, OnDerived);
static AwesomeCVar s_cvOcclusionMode = AwesomeCVar::Int("nameplateOcclusionMode", 0, 0, 1, OnMarkDirty);
static AwesomeCVar s_cvNonTargetAlpha = AwesomeCVar::Float("nameplateNonTargetAlpha", 0.5f, 0.0f, 1.0f, OnDerived);
static AwesomeCVar s_cvAlphaSpeed = AwesomeCVar::Float("nameplateAlphaSpeed", 0.25f, 0.01f, 1.0f, OnMarkDirty, "%.3f");
static AwesomeCVar s_cvInertia = AwesomeCVar::Float("nameplateInertia", 1.0f, 0.0f, 20.0f, OnMarkDirty);
static AwesomeCVar s_cvHysteresisDecay = AwesomeCVar::Float("nameplateHysteresisDecay", 1.0f, 0.25f, 30.0f, OnMarkDirty);
static AwesomeCVar s_cvClampMode = AwesomeCVar::Int("nameplateClampMode", 0, 0, CLAMP_BOSS_EDGES, OnClampMode);
static AwesomeCVar s_cvClampVOffset = AwesomeCVar::Float("nameplateClampModeVOffset", 0.01f, 0.0f, 0.05f, OnMarkDirty);
static AwesomeCVar s_cvClampHOffset = AwesomeCVar::Float("nameplateClampModeHOffset", 0.01f, 0.0f, 0.1f, OnMarkDirty);
static AwesomeCVar s_cvClampFilter = AwesomeCVar::Int("nameplateClampModeFilter", 0, 0, CLAMP_FILTER_TARGET | CLAMP_FILTER_COMBAT, OnMarkDirty);
static AwesomeCVar s_cvMouseFreeze = AwesomeCVar::Int("nameplateMouseFreeze", 0, 0, FREEZE_COMBAT, OnMarkDirty);
static AwesomeCVar s_cvMouseFreezeGrace = AwesomeCVar::Float("nameplateMouseFreezeGrace", 0.15f, 0.0f, 1.0f, OnMarkDirty, "%.3f");
static AwesomeCVar s_cvMouseFreezeTime = AwesomeCVar::Float("nameplateMouseFreezeTime", 0.3f, 0.0f, 2.0f, OnMarkDirty, "%.3f");
static AwesomeCVar s_cvStacking = AwesomeCVar::Int("nameplateStacking", 0, -STACKING_FRIENDLY, STACKING_FRIENDLY, OnStackingMode);

static float s_distanceSquared = 41.0f * 41.0f;

static int32_t StackingSetting() {
    return std::abs(s_cvStacking.m_intValue);
}

static bool ClampAllEdges() {
    return s_cvClampMode.m_intValue == CLAMP_ALL_EDGES || s_cvClampMode.m_intValue == CLAMP_BOSS_EDGES;
}

static bool ClampsRank(uint32_t rank) {
    switch (s_cvClampMode.m_intValue) {
        case CLAMP_ALL:
        case CLAMP_ALL_EDGES:
            return true;

        case CLAMP_BOSS:
        case CLAMP_BOSS_EDGES:
            return rank == RANK_WORLDBOSS;

        default:
            return false;
    }
}

static bool ClampFilterPasses(const WOWGUID& guid, const WOWGUID& target, bool inCombat) {
    uint32_t filter = static_cast<uint32_t>(s_cvClampFilter.m_intValue);
    bool targetOk = !(filter & CLAMP_FILTER_TARGET) || (guid && guid == target);
    bool combatOk = !(filter & CLAMP_FILTER_COMBAT) || inCombat;
    return targetOk && combatOk;
}

static bool FreezeActive() {
    int32_t mode = s_cvMouseFreeze.m_intValue;
    return mode == FREEZE_ALWAYS || (mode == FREEZE_COMBAT && AwesomeInCombatLockdown());
}

static float PerFrame(float k, float delta) {
    return 1.0f - std::pow(1.0f - std::min(k, 0.99f), delta * TUNED_FPS);
}

// capped so frames below 50 fps slow the animations down instead of jumping
static float SceneDelta(const CGWorldFrame* worldFrame) {
    return std::min(0.02f, worldFrame->m_elapsedSec);
}

struct Derived {
    uint32_t m_mouseMode;
    uint8_t m_nonTargetAlpha;
    bool m_occlusionOn;
    // a negative nameplateOcclusionAlpha caps the plate alpha instead of scaling it
    bool m_occludeUp;
    uint8_t m_occlusionCap;
    float m_occlusionScale;
    float m_raiseGain;
    float m_lowerGain;
    float m_pullGain;
    // freshState's decay rate, before PerFrame
    float m_lowerSettle;
};

static Derived s_derived = { 0, 255, false, false, 255, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f };

static float SpeedGain(float speed) {
    return std::pow(std::min(std::max(speed / TUNED_FPS, 0.0f), 1.0f), 1.5f);
}

static void ApplyDerived() {
    float occlusion = std::abs(s_cvOcclusionAlpha.m_floatValue);
    int32_t mouseMode = s_cvMouseMode.m_intValue;

    s_derived.m_mouseMode = mouseMode >= 0 && mouseMode < static_cast<int32_t>(sizeof(MOUSE_MODE_MAP) / sizeof(MOUSE_MODE_MAP[0])) ? MOUSE_MODE_MAP[mouseMode] : 0;
    s_derived.m_nonTargetAlpha = static_cast<uint8_t>(255 * s_cvNonTargetAlpha.m_floatValue);
    s_derived.m_occlusionOn = occlusion < 1.0f;
    s_derived.m_occludeUp = s_cvOcclusionAlpha.m_floatValue < 0.0f;
    s_derived.m_occlusionCap = static_cast<uint8_t>(255 * occlusion);
    s_derived.m_occlusionScale = occlusion;
    s_derived.m_raiseGain = SpeedGain(s_cvRaiseSpeed.m_floatValue);
    s_derived.m_lowerGain = SpeedGain(s_cvLowerSpeed.m_floatValue);
    s_derived.m_pullGain = SpeedGain(s_cvPullSpeed.m_floatValue);
    s_derived.m_lowerSettle = std::min(std::max(s_cvLowerSpeed.m_floatValue / TUNED_FPS, 0.0f), 0.5f);
}

void AwesomeNamePlatesMarkDirty() {
    if (CGWorldFrame::s_currentWorldFrame) {
        CGWorldFrame::s_currentWorldFrame->m_namePlateFlags |= 0x1;
    }
}

static bool FocusHasPriority() {
    uint32_t mode = s_derived.m_mouseMode;
    return (mode & MOUSE_OVER_ALWAYS) || ((mode & MOUSE_OVER_COMBAT) && AwesomeInCombatLockdown());
}

struct RaiseFilter {
    bool m_friendly;
    bool m_enemy;

    bool Passes(bool friendly) const {
        return friendly ? this->m_friendly : this->m_enemy;
    }
};

static RaiseFilter MouseoverRaise() {
    RaiseFilter filter = { false, false };

    if (!FocusHasPriority()) {
        return filter;
    }

    filter.m_friendly = !(s_derived.m_mouseMode & MOUSE_CLICK_THRU_FRIEND);
    filter.m_enemy = !(s_derived.m_mouseMode & MOUSE_CLICK_THRU_ENEMY);
    return filter;
}

// ---------------------------------------------------------------------------------------------------------------------
// Engine helpers

static CGPlayer_C* ActivePlayer() {
    return static_cast<CGPlayer_C*>(AwesomeObjectPtr(ClntObjMgrGetActivePlayer(), TYPEMASK_PLAYER));
}

static CGUnit_C* UnitPtr(const WOWGUID& guid) {
    return static_cast<CGUnit_C*>(AwesomeObjectPtr(guid, TYPEMASK_UNIT));
}

// awesome_wotlk's isFriendly, kept numerically as its build runs: its reaction enum is 1-based while the engine's is
// 0-based, so a plate counts as friendly at honored and above, or friendly and not attackable (neutral mobs are enemies)
static bool IsFriendly(CGUnit_C* unit) {
    CGPlayer_C* player = ActivePlayer();

    if (!player) {
        return false;
    }

    int32_t reaction = player->UnitReaction(unit);
    return reaction >= 5 || (reaction == 4 && !player->CanAttackNow(unit));
}

static Vec2 PlatePosition(const CGNamePlateFrame* plate) {
    return Vec2(plate->m_position.x, plate->m_position.y);
}

static Vec2 AspectNormal() {
    return Vec2(NDCToDDCWidth(1.0f), NDCToDDCHeight(1.0f));
}

// ---------------------------------------------------------------------------------------------------------------------
// Entry: one plate's layout state

namespace {

struct MotionParams {
    Vec2 m_speed;
    // Derived step gain of speed
    Vec2 m_gain;
    float m_inertia;
    float m_delta;
    // fraction of the plate size
    Vec2 m_maxOffset;
    // clamp insets from the screen edges
    Vec2 m_ceil;
    bool m_allEdges;
    // ShouldClamp gated by the per-pass filter
    bool m_clamp;
};

struct Entry {
    enum State : uint32_t {
        STATE_NONE = 0,
        STATE_SHOULD_STACK = 0x1,
        STATE_SHOULD_CLAMP = 0x2,
        STATE_IS_FRIENDLY = 0x4,
        STATE_IS_TRANSIENT = 0x8,
        STATE_IS_FRESH = 0x10,
        STATE_IS_ACTIVE = 0x20,
        STATE_IS_FROZEN = 0x40,
        STATE_IS_SLIDING = 0x80,
        STATE_IS_RESYNC = 0x100,
    };

    CGNamePlateFrame* m_plate;
    WOWGUID m_guid;
    uint32_t m_classification;

    Vec2 m_momentum;
    Vec2 m_commitTarget;
    Vec2 m_targetOffset;
    Vec2 m_smoothTarget;
    Vec2 m_stackOffset;
    // m_stackOffset after the screen clamp
    Vec2 m_shownOffset;
    float m_accumX;
    int32_t m_pushCount;

    // last position handed to SetPoint
    Vec2 m_drawn;
    Vec2 m_freezePos;
    // offset from the unit at release, the slide moves with the camera
    Vec2 m_slideFrom;
    uint64_t m_graceEnd;
    float m_slideTime;

    uint32_t m_state;
    std::array<uint32_t, MAX_PLATES / 32> m_activeCollisions;

    explicit Entry(CGNamePlateFrame* plate) : m_plate(plate), m_guid(WOWGUID()), m_classification(RANK_NORMAL), m_accumX(0.0f), m_pushCount(0), m_graceEnd(0), m_slideTime(0.0f), m_state(STATE_NONE) {
        this->m_activeCollisions.fill(0);
    }

    bool HasState(uint32_t flag) const { return (this->m_state & flag) != 0; }

    void SetState(uint32_t flag, bool on) {
        this->m_state = on ? (this->m_state | flag) : (this->m_state & ~flag);
    }

    void ClearState() {
        this->m_momentum = this->m_commitTarget = this->m_targetOffset = this->m_smoothTarget = this->m_stackOffset = this->m_shownOffset = Vec2();
        this->m_accumX = 0.0f;
        this->m_pushCount = 0;
        this->m_drawn = this->m_freezePos = this->m_slideFrom = Vec2();
        this->m_graceEnd = 0;
        this->m_slideTime = 0.0f;
        this->m_state = STATE_IS_FRESH;
    }

    void SetActiveCollision(int32_t id) { this->m_activeCollisions[id >> 5] |= 1u << (id & 31); }
    void SetInactiveCollision(int32_t id) { this->m_activeCollisions[id >> 5] &= ~(1u << (id & 31)); }

    bool IsTransparent() const { return this->m_plate->m_alpha == 0; }

    Vec2 Ndc() const { return PlatePosition(this->m_plate); }
    // projected position plus the settled / visible offset
    Vec2 Tar() const { return this->Ndc() + this->m_targetOffset; }
    Vec2 Vis() const { return this->Ndc() + this->m_shownOffset; }
    Vec2 Size() const { return Vec2(this->m_plate->m_width, this->m_plate->m_height) * this->m_plate->m_layoutScale; }

    float GetTopNdc(float perc = 1.0f) const { return this->Ndc().y + this->Size().y * 0.5f * perc; }
    float GetBotNdc(float perc = 1.0f) const { return this->Ndc().y - this->Size().y * 0.5f * perc; }
    float GetAvgWFor(const Entry* e, float perc = 1.0f) const { return (this->Size().x + e->Size().x) * 0.5f * perc; }
    float GetAvgHFor(const Entry* e, float perc = 1.0f) const { return (this->Size().y + e->Size().y) * 0.5f * perc; }

    // y offset e needs to sit right above this plate
    float GetReqYFor(const Entry* e, float perc = 1.0f) const { return this->Tar().y + this->GetAvgHFor(e, perc) - e->Ndc().y; }
    float GetReqXFor(const Entry* e) const { return this->Tar().x - e->Tar().x; }
    float GetReqDxFor(const Entry* e) const { return std::abs(this->GetReqXFor(e)); }

    float GetProximity(const Entry* e, float bx = 1.0f, float by = 1.0f) const {
        Vec2 gap = this->Tar() - e->Tar();
        float proximity = std::min(std::abs(gap.x) / this->GetAvgWFor(e, bx), std::abs(gap.y) / this->GetAvgHFor(e, by));
        return std::min(std::max(proximity, 0.0f), 1.0f);
    }

    int32_t GetRankWeight() const {
        switch (this->m_classification) {
            case RANK_TRIVIAL:
                return 0;
            case RANK_NORMAL:
                return 1;
            case RANK_RARE:
                return 2;
            case RANK_ELITE:
                return 3;
            case RANK_RARE_ELITE:
                return 4;
            case RANK_WORLDBOSS:
                return 5;
            default:
                return 0;
        }
    }

    bool ResolvePush(const Entry* e, float sep, float hyst) const {
        bool withinBand = (e->Tar().y > this->GetBotNdc(sep * (hyst + 1.0f)) + this->m_targetOffset.y) && (e->Tar().y < this->GetTopNdc(sep * (hyst + 1.0f)) + this->m_targetOffset.y);
        bool crossing = e->GetBotNdc() + e->m_stackOffset.y > this->GetTopNdc() + this->m_targetOffset.y && e->GetBotNdc() + e->m_targetOffset.y < this->GetTopNdc() + this->m_targetOffset.y;
        return withinBand || crossing;
    }

    bool IsAt(const Vec2& offset) const {
        return std::abs(this->m_stackOffset.x - offset.x) < EPSILON && std::abs(this->m_stackOffset.y - offset.y) < EPSILON;
    }

    // settle: per-frame decay of the previous offsets toward home
    void FreshState(float settle, float pullDistance) {
        if (this->HasState(STATE_IS_FROZEN)) {
            this->m_targetOffset = this->m_freezePos - this->Ndc();
            this->m_accumX = 0.0f;
            this->m_pushCount = 0;
            return;
        }

        float maxPull = this->Size().x * pullDistance;
        this->m_accumX += (0.0f - this->m_accumX) * settle;
        this->m_targetOffset.y += (0.0f - this->m_targetOffset.y) * settle;
        float pull = this->m_pushCount > 1 ? this->m_accumX / static_cast<float>(this->m_pushCount) : this->m_accumX;
        this->m_targetOffset.x = std::min(std::max(pull, -maxPull), maxPull);
        this->m_pushCount = 0;
    }

    void UpdateStacking() {
        if (this->HasState(STATE_IS_TRANSIENT)) {
            this->SetState(STATE_SHOULD_STACK, false);
            return;
        }

        int32_t mode = StackingSetting();
        bool shouldStack = mode != STACKING_DISABLED;

        if (mode == STACKING_FRIENDLY) {
            shouldStack = this->HasState(STATE_IS_FRIENDLY);
        } else if (mode == STACKING_ENEMY) {
            shouldStack = !this->HasState(STATE_IS_FRIENDLY);
        }

        this->SetState(STATE_SHOULD_STACK, shouldStack);
    }

    void UpdateClamping() {
        if (CGUnit_C* unit = UnitPtr(this->m_plate->m_guid)) {
            this->SetState(STATE_SHOULD_CLAMP, ClampsRank(unit->GetCreatureClassification()));
        }
    }

    void UpdateReaction() {
        if (CGUnit_C* unit = UnitPtr(this->m_plate->m_guid)) {
            this->SetState(STATE_IS_FRIENDLY, IsFriendly(unit));
            this->UpdateStacking();
        }
    }

    // sideways pull on e, weighted by horizontal overlap
    void Pull(Entry* e, float dx, float bandX, float scale) const {
        float reqX = this->GetReqXFor(e);

        if (this->m_pushCount == 0 || std::signbit(this->m_targetOffset.x) == std::signbit(reqX)) {
            float overlap = std::min(std::max(1.0f - (dx / this->GetAvgWFor(e, bandX)), 0.0f), 1.0f);
            e->m_accumX += scale * reqX * std::pow(overlap, 1.5f);
            e->m_pushCount++;
        }
    }

    void UpdVis(const MotionParams& p) {
        if (this->HasState(STATE_IS_FROZEN)) {
            this->m_momentum = Vec2();
            this->m_shownOffset = this->m_stackOffset = this->m_smoothTarget = this->m_commitTarget = this->m_targetOffset = this->m_freezePos - this->Ndc();
            return;
        }

        Vec2 max = this->Size() * p.m_maxOffset;
        Rect bounds = { 0.0f, -max.x, max.y, max.x };
        this->m_targetOffset = bounds.Clamp(this->m_targetOffset);

        if (this->HasState(STATE_IS_RESYNC)) {
            this->SetState(STATE_IS_RESYNC, false);
            this->m_momentum = Vec2();
            this->m_stackOffset = this->m_smoothTarget = this->m_commitTarget = this->m_targetOffset;
        }

        // the axes only couple through these two
        bool resting = this->IsAt(Vec2());
        bool settled = this->IsAt(this->m_targetOffset);

        for (size_t i = 0; i < 2; i++) {
            float gap = this->m_targetOffset[i] - this->m_commitTarget[i];
            float want = std::abs(gap) > EPSILON ? (gap > 0.0f ? 1.0f : -1.0f) : 0.0f;
            float& momentum = this->m_momentum[i];

            if (resting) {
                momentum = want;
            } else {
                float rate = (want * momentum < 0.0f) ? 1.0f : p.m_speed[i] * 0.025f;
                momentum += (want - momentum) * std::min(std::max(rate * p.m_inertia * p.m_delta, 0.0f), 1.0f);
            }

            if (!settled) {
                this->m_commitTarget[i] += (this->m_targetOffset[i] - this->m_commitTarget[i]) * std::min(std::max(10.0f * std::abs(momentum) * p.m_delta, 0.0f), 1.0f);
            } else {
                this->m_commitTarget[i] = this->m_targetOffset[i];
                momentum *= std::pow(0.1f, p.m_delta * TUNED_FPS);
            }

            this->m_smoothTarget[i] += (this->m_commitTarget[i] - this->m_smoothTarget[i]) * (1.0f - std::exp(-p.m_speed[i] * std::abs(momentum) * p.m_delta));

            float step = PerFrame(p.m_gain[i] * std::abs(momentum * momentum * momentum), p.m_delta);
            float d = this->m_smoothTarget[i] - this->m_stackOffset[i];
            this->m_stackOffset[i] = std::abs(d) > EPSILON ? this->m_stackOffset[i] + d * std::min(std::max(step, 0.0f), 1.0f) : this->m_smoothTarget[i];
        }

        // display only, keeps the clamp out of the animated state
        this->m_shownOffset = this->m_stackOffset;

        if (p.m_clamp) {
            // anchored at the top center, screen extents follow the aspect ratio
            Vec2 screen = AspectNormal();
            Vec2 dims = this->Size();
            Vec2 ndc = this->Ndc();
            Rect limit = { -10000.0f, -10000.0f, screen.y - p.m_ceil.y - ndc.y, 10000.0f };

            if (p.m_allEdges) {
                limit.minY = p.m_ceil.y + dims.y - ndc.y;
                limit.minX = p.m_ceil.x + dims.x * 0.5f - ndc.x;
                limit.maxX = screen.x - p.m_ceil.x - dims.x * 0.5f - ndc.x;
            }

            this->m_shownOffset = limit.Clamp(this->m_stackOffset);
        }
    }

    void UpdateFreeze(bool hovered, bool releaseNow, uint64_t ms, float grace) {
        if (hovered && !this->HasState(STATE_IS_FRESH)) {
            if (!this->HasState(STATE_IS_FROZEN)) {
                // mid-slide too, picks up where it is
                this->m_freezePos = this->m_drawn;
                this->SetState(STATE_IS_FROZEN, true);
                this->SetState(STATE_IS_SLIDING, false);
            }

            this->m_graceEnd = ms + static_cast<uint64_t>(grace * 1000.0f);
            return;
        }

        if (!this->HasState(STATE_IS_FROZEN) || (!releaseNow && ms < this->m_graceEnd)) {
            return;
        }

        // stacking resumes from the pinned spot, restarting from home would lose its place in the stack
        this->m_momentum = Vec2();
        this->m_slideFrom = this->m_freezePos - this->Ndc();
        this->m_commitTarget = this->m_targetOffset = this->m_smoothTarget = this->m_stackOffset = this->m_slideFrom;
        this->m_accumX = 0.0f;
        this->m_pushCount = 0;
        this->m_slideTime = 0.0f;
        this->SetState(STATE_IS_FROZEN, false);
        this->SetState(STATE_IS_SLIDING, true);
        this->SetState(STATE_IS_RESYNC, true);
    }

    Vec2 Place(float slideDuration, float delta) {
        Vec2 position = this->Vis();

        if (this->HasState(STATE_IS_SLIDING)) {
            this->m_slideTime += delta;
            float t = slideDuration > 0.0f ? std::min(this->m_slideTime / slideDuration, 1.0f) : 1.0f;

            if (t >= 1.0f) {
                this->SetState(STATE_IS_SLIDING, false);
            } else {
                position = this->Ndc() + this->m_slideFrom + (this->m_shownOffset - this->m_slideFrom) * (t * t * (3.0f - 2.0f * t));
            }
        }

        this->m_drawn = position;
        return position;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// Pairs: per pair of plates, the hysteresis that keeps two stacked plates from flickering apart

struct PairState {
    uint64_t m_timestamp;
    int32_t m_hystSteps;
    float m_hystDecay;
    float m_hysteresis;
    uint64_t m_proximate;
    const Entry* m_e1;
    const Entry* m_e2;

    PairState() : m_timestamp(0), m_hystSteps(0), m_hystDecay(0.0f), m_hysteresis(1.0f), m_proximate(0), m_e1(nullptr), m_e2(nullptr) {}

    bool IsStale(uint64_t ms) const { return this->m_timestamp < ms; }
    bool IsApart(uint64_t ms) const { return this->m_proximate < ms; }

    void Commit(uint64_t ms, float hyst) {
        if (this->m_hystDecay > 0.0f && this->m_timestamp == 0) {
            this->m_hystSteps++;
            this->m_hystDecay = 1.0f;
        } else if (this->m_timestamp == 0) {
            this->m_hystDecay = 1.0f;
        }

        this->m_timestamp = ms;
        this->m_hysteresis = hyst;
    }

    void Cooldown(float delta) {
        if (!this->m_e1 || !this->m_e2) {
            this->m_hystDecay = 0.0f;
            this->m_hystSteps = 0;
            return;
        }

        if (this->m_hystDecay > 0.0f) {
            this->m_hystDecay -= this->m_e1->GetProximity(this->m_e2) * delta;

            if (this->m_hystDecay <= 0.0f) {
                this->m_hystDecay = 0.0f;
                this->m_hystSteps = 0;
            }
        }
    }

    void Seed(uint64_t ms, const Entry* first, const Entry* second) {
        this->m_proximate = ms;

        if (!this->m_e1 || !this->m_e2) {
            this->m_e1 = first;
            this->m_e2 = second;
        }
    }

    void Reset(bool full) {
        this->m_hysteresis = 1.0f;
        this->m_timestamp = 0;

        if (full) {
            this->m_proximate = 0;
            this->m_hystDecay = 0.0f;
            this->m_hystSteps = 0;
            this->m_e1 = nullptr;
            this->m_e2 = nullptr;
        }
    }
};

// strict lower triangle, rows keyed by the larger id
class PairsManager {
    public:
    void Init(size_t count) { this->m_pairs.assign(count * (count - 1) / 2, PairState()); }
    void Wipe() { std::fill(this->m_pairs.begin(), this->m_pairs.end(), PairState()); }

    PairState* Get(int32_t id1, int32_t id2) {
        size_t lo = static_cast<size_t>(std::min(id1, id2));
        size_t hi = static_cast<size_t>(std::max(id1, id2));
        return &this->m_pairs[hi * (hi - 1) / 2 + lo];
    }

    private:
    std::vector<PairState> m_pairs;
};

// ---------------------------------------------------------------------------------------------------------------------
// EntryManager

class EntryManager {
    public:
    void Init(uint32_t count);
    void ClearAll();
    void Update(CGWorldFrame* worldFrame);
    void OnReactionChanged(CGUnit_C* unit);

    std::vector<Entry*>& Entries() { return this->m_entries; }

    int32_t TotalSize() const { return static_cast<int32_t>(this->m_byId.size()); }

    WOWGUID GetTokenGuid(int32_t index) const {
        return index >= 0 && index < this->TotalSize() ? this->m_byId[index].m_guid : WOWGUID();
    }

    int32_t GetTokenId(const WOWGUID& guid) const {
        if (!guid) {
            return -1;
        }

        for (int32_t i = 0; i < this->TotalSize(); i++) {
            if (this->m_byId[i].m_guid == guid) {
                return i;
            }
        }

        return -1;
    }

    // "none" is a valid unit token
    const char* GetToken(int32_t index) const {
        return index >= 0 && index < this->TotalSize() ? this->m_tokens[index].c_str() : "none";
    }

    Entry* GetEntry(int32_t index) {
        if (index < 0 || index >= this->TotalSize()) {
            return nullptr;
        }

        Entry& e = this->m_byId[index];
        return e.m_guid ? &e : nullptr;
    }

    CGNamePlateFrame* GetPlate(const WOWGUID& guid) {
        Entry* e = this->GetEntry(this->GetTokenId(guid));
        return e ? e->m_plate : nullptr;
    }

    // the plate frame passed as self (arg 1), if it is one of ours
    Entry* EntryArg(lua_State* L);

    void RefreshStacking() {
        for (size_t i = 0; i < this->m_entries.size(); i++) {
            this->m_entries[i]->UpdateStacking();
        }
    }

    void RefreshClamping() {
        for (size_t i = 0; i < this->m_entries.size(); i++) {
            this->m_entries[i]->UpdateClamping();
        }
    }

    CGNamePlateFrame* PickPlate(ClickLogic mode, const C2Vector& position) const;
    CGNamePlateFrame* HoverPlate(const CGNamePlateFrame* focus, const C2Vector& position) const;
    void SetHover(CGNamePlateFrame* plate) { this->m_hover = plate; }

    void Track(CGNamePlateFrame* plate);

    private:
    enum SortPass { SORT_STACK, SORT_DRAW };

    static bool IsUnderCursor(const Entry* e, const C2Vector& position);

    PairState* GetPair(const Entry* e1, const Entry* e2) {
        return this->m_pairs.Get(e1->m_plate->m_awesomePlateId, e2->m_plate->m_awesomePlateId);
    }

    void CommitPair(const Entry* e1, const Entry* e2, uint64_t ms, float delta, float by, float bx);
    void SeedPair(Entry* e1, Entry* e2, uint64_t ms);
    void ResolvePairs(Entry* e, uint64_t ms, float delta);
    void Sort(SortPass pass);
    void FlushAdded();
    void FlushRemoved();
    void AppendAdded(int32_t index);
    void ClearPending() { std::fill(this->m_pending.begin(), this->m_pending.end(), 0); }
    Entry* InitEntry(CGNamePlateFrame* plate);
    void MarkRemoved(Entry* e, int32_t id);
    MouseInput GetMouseInput(uint64_t ms);
    void Stack(uint64_t ms, float sceneTime, float pullScale);

    static void OnPlateCreated(CGNamePlateFrame* plate);

    PairsManager m_pairs;
    std::vector<Entry> m_byId;
    std::vector<Entry*> m_entries;
    std::vector<std::string> m_tokens;
    std::array<uint64_t, MAX_PLATES / 64> m_pending;
    CGNamePlateFrame* m_hover;
    CGNamePlateFrame* m_lastHover;
    uint64_t m_pressMs;
};

} // namespace

static EntryManager s_plates;

static bool IsNamedUnit(CGUnit_C* unit) {
    const char* name = unit->GetUnitName(nullptr, 1);

    if (!name) {
        return false;
    }

    const char* unknown = FrameScript_GetText("UNKNOWNOBJECT", -1, GENDER_NOT_APPLICABLE);
    return (!unknown || !*unknown || strcmp(name, unknown) != 0) && strcmp(name, "Unknown Being") != 0;
}

static FrameScript_Object* PlateObject(CGNamePlateFrame* plate) {
    return static_cast<CSimpleFrame*>(plate);
}

// ---------------------------------------------------------------------------------------------------------------------
// Plate methods

static int32_t Script_SetStackingEnabled(lua_State* L) {
    if (Entry* e = s_plates.EntryArg(L)) {
        e->SetState(Entry::STATE_IS_TRANSIENT, !lua_toboolean(L, 2));
        e->UpdateStacking();
    }

    return 0;
}

static int32_t Script_GetStackingEnabled(lua_State* L) {
    if (Entry* e = s_plates.EntryArg(L)) {
        lua_pushboolean(L, e->HasState(Entry::STATE_SHOULD_STACK));
        return 1;
    }

    return 0;
}

static int32_t Script_SetOcclusionEnabled(lua_State* L) {
    if (Entry* e = s_plates.EntryArg(L)) {
        e->m_plate->m_awesomeOpaque = lua_toboolean(L, 2) ? 0 : 1;
    }

    return 0;
}

static int32_t Script_GetOcclusionEnabled(lua_State* L) {
    if (Entry* e = s_plates.EntryArg(L)) {
        lua_pushboolean(L, !e->m_plate->m_awesomeOpaque);
        return 1;
    }

    return 0;
}

Entry* EntryManager::EntryArg(lua_State* L) {
    FrameScript_Object* object = AwesomeLuaToObject(L, 1);

    if (!object) {
        return nullptr;
    }

    for (int32_t i = 0; i < this->TotalSize(); i++) {
        if (PlateObject(this->m_byId[i].m_plate) == object) {
            return &this->m_byId[i];
        }
    }

    return nullptr;
}

void EntryManager::Init(uint32_t count) {
    this->m_tokens.reserve(count);

    for (uint32_t i = 1; i <= count; i++) {
        char token[32];
        snprintf(token, sizeof(token), "nameplate%u", i);
        this->m_tokens.push_back(token);
    }

    this->m_byId.reserve(count);
    this->m_pairs.Init(count);
    this->m_entries.reserve(count);
    this->m_pending.fill(0);
    this->m_hover = nullptr;
    this->m_lastHover = nullptr;
    this->m_pressMs = 0;
}

void EntryManager::ClearAll() {
    this->m_pairs.Wipe();
    this->m_byId.clear();
    this->m_entries.clear();
    this->m_pending.fill(0);
    this->m_hover = nullptr;
    this->m_lastHover = nullptr;
    this->m_pressMs = 0;
}

static void TrackPlate(CGNamePlateFrame* plate, void* param) {
    static_cast<EntryManager*>(param)->Track(plate);
}

void EntryManager::Update(CGWorldFrame* worldFrame) {
    // every plate the engine has built, hidden and free-listed ones included
    CGNamePlateFrame::EnumerateAll(TrackPlate, this);

    // bulk flush now so callbacks receive a complete gapless snapshot of the previous frame
    this->FlushRemoved();
    uint64_t ms = GetTickCount64();
    MouseInput input = this->GetMouseInput(ms);
    CGNamePlateFrame* hover = this->m_hover;
    this->m_hover = nullptr;

    if (!hover && input == MOUSE_CLICK_PENDING) {
        hover = this->m_lastHover;
    }

    this->m_lastHover = hover;
    // a camera drag or another plate under the cursor ends a pin outright
    bool releaseNow = hover || input == MOUSE_DRAGGING;

    // the engine stops updating focus while a mouse button is held, a camera drag would keep the highlight
    if (input == MOUSE_DRAGGING && CGUnit_C::s_namePlateFocus) {
        CGUnit_C::s_namePlateFocus->OnLoseFocus();
        CGUnit_C::s_namePlateFocus = nullptr;
    }

    if (this->m_entries.empty()) {
        this->ClearPending();
        CLayoutFrame::ResizePending();
        return;
    }

    int32_t level = static_cast<int32_t>(this->m_entries.size()) * 10;
    float sceneTime = SceneDelta(worldFrame);
    WOWGUID target = CGGameUI::m_lockedTarget;
    bool inCombat = AwesomeInCombatLockdown();
    bool freezeOn = FreezeActive();

    for (size_t i = 0; i < this->m_entries.size(); i++) {
        Entry* e = this->m_entries[i];
        e->UpdateFreeze(freezeOn && e->m_plate == hover, releaseNow, ms, s_cvMouseFreezeGrace.m_floatValue);
    }

    this->Sort(SORT_STACK);

    float settle = PerFrame(s_derived.m_lowerSettle, sceneTime);
    // accumX sums pulls across frames, rescaled so its steady state matches the tuned rate at any fps
    float pullScale = settle * (1.0f - s_derived.m_lowerSettle) / (s_derived.m_lowerSettle * (1.0f - settle));

    for (size_t i = 0; i < this->m_entries.size(); i++) {
        this->m_entries[i]->FreshState(settle, s_cvPullDistance.m_floatValue);
    }

    this->Stack(ms, sceneTime, pullScale);
    this->Sort(SORT_DRAW);

    for (size_t i = 0; i < this->m_entries.size(); i++) {
        Entry* e = this->m_entries[i];
        this->ResolvePairs(e, ms, sceneTime);
        bool raising = (e->m_commitTarget.y - e->m_stackOffset.y) > 0.0f;

        MotionParams params;
        params.m_speed = Vec2(s_cvPullSpeed.m_floatValue, raising ? s_cvRaiseSpeed.m_floatValue : s_cvLowerSpeed.m_floatValue);
        params.m_gain = Vec2(s_derived.m_pullGain, raising ? s_derived.m_raiseGain : s_derived.m_lowerGain);
        params.m_inertia = s_cvInertia.m_floatValue;
        params.m_delta = sceneTime;
        params.m_maxOffset = Vec2(s_cvPullDistance.m_floatValue, s_cvRaiseDistance.m_floatValue);
        params.m_ceil = Vec2(s_cvClampHOffset.m_floatValue, s_cvClampVOffset.m_floatValue);
        params.m_allEdges = ClampAllEdges();
        // the plate's own guid, e->m_guid is only assigned in FlushAdded after this pass
        params.m_clamp = e->HasState(Entry::STATE_SHOULD_CLAMP) && ClampFilterPasses(e->m_plate->m_guid, target, inCombat);
        e->UpdVis(params);
        e->SetState(Entry::STATE_IS_FRESH, false);

        Vec2 position = e->Place(s_cvMouseFreezeTime.m_floatValue, sceneTime) / e->m_plate->m_layoutScale;
        e->m_plate->SetPoint(FRAMEPOINT_TOP, worldFrame, FRAMEPOINT_BOTTOMLEFT, position.x, position.y, 1);
        e->m_plate->SetFrameDepth(e->m_plate->m_position.z - worldFrame->m_layoutDepth, true);
        e->m_plate->SetFrameLevel(level, 1);
        // addons buffer
        level -= 10;
    }

    // stays set: the plates animate every frame
    worldFrame->m_namePlateFlags |= 0x1;

    CLayoutFrame::ResizePending();
    // all set, fire the callbacks
    this->FlushAdded();
    this->ClearPending();
}

void EntryManager::OnReactionChanged(CGUnit_C* unit) {
    if (unit->m_obj->m_guid == ClntObjMgrGetActivePlayer()) {
        for (size_t i = 0; i < this->m_entries.size(); i++) {
            this->m_entries[i]->UpdateReaction();
        }
    } else if (CGNamePlateFrame* plate = unit->m_namePlateFrame) {
        if (Entry* e = this->GetEntry(plate->m_awesomePlateId)) {
            e->UpdateReaction();
        }
    }
}

void EntryManager::CommitPair(const Entry* e1, const Entry* e2, uint64_t ms, float delta, float by, float bx) {
    PairState* pair = this->GetPair(e1, e2);

    if ((e1->GetTopNdc() + e1->m_targetOffset.y + e1->GetAvgHFor(e2, by)) > (e2->GetBotNdc() + e2->m_targetOffset.y) && e1->GetReqDxFor(e2) < e1->GetAvgWFor(e2, bx)) {
        // still overlapping
        pair->Commit(ms, 1.25f + std::min(static_cast<float>(pair->m_hystSteps) * 0.15f, 0.75f));
    } else {
        // the boxes no longer overlap, decay scaled by separation
        pair->Commit(ms, std::max(1.0f, pair->m_hysteresis - (e1->GetProximity(e2) * 0.05f) * delta * s_cvHysteresisDecay.m_floatValue));
    }
}

void EntryManager::SeedPair(Entry* e1, Entry* e2, uint64_t ms) {
    int32_t id1 = e1->m_plate->m_awesomePlateId;
    int32_t id2 = e2->m_plate->m_awesomePlateId;
    this->m_pairs.Get(id1, id2)->Seed(ms, e1, e2);
    e1->SetActiveCollision(id2);
    e2->SetActiveCollision(id1);
}

void EntryManager::ResolvePairs(Entry* e, uint64_t ms, float delta) {
    int32_t id1 = e->m_plate->m_awesomePlateId;
    int32_t words = (this->TotalSize() + 31) >> 5;

    for (int32_t w = 0; w < words; w++) {
        uint32_t mask = e->m_activeCollisions[w];

        while (mask) {
            unsigned long bit;
            _BitScanForward(&bit, mask);
            int32_t id2 = (w << 5) | static_cast<int32_t>(bit);
            PairState* pair = this->m_pairs.Get(id1, id2);
            bool apart = pair->IsApart(ms);

            if (pair->IsStale(ms)) {
                pair->Reset(apart);
                pair->Cooldown(delta);
            }

            if (apart) {
                e->SetInactiveCollision(id2);
            }

            mask &= mask - 1;
        }
    }
}

void EntryManager::Sort(SortPass pass) {
    if (pass == SORT_STACK) {
        std::sort(this->m_entries.begin(), this->m_entries.end(), [](const Entry* a, const Entry* b) {
            float posA = a->Tar().y;
            float posB = b->Tar().y;

            if (std::abs(posA - posB) > EPSILON) {
                return posA < posB;
            }

            int32_t rankA = a->GetRankWeight();
            int32_t rankB = b->GetRankWeight();

            if (rankA != rankB) {
                return rankA > rankB;
            }

            return a->m_plate < b->m_plate;
        });

        return;
    }

    WOWGUID target = CGGameUI::m_lockedTarget;
    RaiseFilter raise = MouseoverRaise();
    CGNamePlateFrame* focus = CGUnit_C::s_namePlateFocus;

    if (focus) {
        const Entry* e = this->GetEntry(focus->m_awesomePlateId);

        if (!e || e->m_plate != focus || !raise.Passes(e->HasState(Entry::STATE_IS_FRIENDLY))) {
            focus = nullptr;
        }
    }

    // drawn top first: the mouseover raise, then the target, then the focus, then the nearest
    std::sort(this->m_entries.begin(), this->m_entries.end(), [target, focus, raise](const Entry* a, const Entry* b) {
        bool frozenA = a->HasState(Entry::STATE_IS_FROZEN) && raise.Passes(a->HasState(Entry::STATE_IS_FRIENDLY));
        bool frozenB = b->HasState(Entry::STATE_IS_FROZEN) && raise.Passes(b->HasState(Entry::STATE_IS_FRIENDLY));

        if (frozenA != frozenB) {
            return frozenA;
        }

        if (target) {
            bool targetA = a->m_guid == target;
            bool targetB = b->m_guid == target;

            if (targetA != targetB) {
                return targetA;
            }
        }

        if (focus) {
            bool focusA = a->m_plate == focus;
            bool focusB = b->m_plate == focus;

            if (focusA != focusB) {
                return focusA;
            }
        }

        return a->m_plate->m_position.z < b->m_plate->m_position.z;
    });
}

void EntryManager::FlushAdded() {
    lua_State* L = FrameScript_GetContext();
    int32_t words = (this->TotalSize() + 63) / 64;

    for (int32_t w = 0; w < words; w++) {
        uint64_t word = this->m_pending[w];

        while (word) {
            unsigned long bit;
            _BitScanForward64(&bit, word);
            int32_t index = w * 64 + static_cast<int32_t>(bit);
            Entry* e = &this->m_byId[index];
            CGUnit_C* unit = UnitPtr(e->m_plate->m_guid);

            if (unit && unit->m_namePlateFrame) {
                e->m_guid = unit->m_namePlateFrame->m_guid;

                if (L) {
                    AwesomeLuaPushObject(L, PlateObject(e->m_plate));
                    lua_pushstring(L, this->m_tokens[index].c_str());
                    lua_setfield(L, -2, "unit");
                    lua_settop(L, -2);
                }

                FrameScript_SignalEvent(SCRIPT_EVENT_NAME_PLATE_UNIT_ADDED, "%s", this->m_tokens[index].c_str());
            }

            word &= word - 1;
        }
    }
}

void EntryManager::FlushRemoved() {
    this->m_entries.erase(std::remove_if(this->m_entries.begin(), this->m_entries.end(), [](Entry* e) {
        if (!e->HasState(Entry::STATE_IS_ACTIVE)) {
            e->m_guid = WOWGUID();
            return true;
        }

        return false;
    }), this->m_entries.end());
}

void EntryManager::AppendAdded(int32_t index) {
    if (index < 0 || index >= this->TotalSize()) {
        return;
    }

    this->m_pending[index / 64] |= 1ull << (index % 64);
    Entry* e = &this->m_byId[index];

    if (!e->HasState(Entry::STATE_IS_ACTIVE)) {
        e->SetState(Entry::STATE_IS_ACTIVE, true);
        this->m_entries.push_back(e);
    }
}

Entry* EntryManager::InitEntry(CGNamePlateFrame* plate) {
    int32_t index = plate->m_awesomePlateId;

    if (index == -1) {
        // MODIFIED: the entries are addressed by pointer, the vector must never grow past what Init reserved
        if (this->TotalSize() >= static_cast<int32_t>(MAX_PLATES)) {
            return nullptr;
        }

        // plates live for one UI session, they are released right after NamePlateClearAll clears us
        this->m_byId.push_back(Entry(plate));
        plate->m_awesomePlateId = this->TotalSize() - 1;
        OnPlateCreated(plate);
        return &this->m_byId.back();
    }

    if (index < 0 || index >= this->TotalSize() || this->m_byId[index].m_plate != plate) {
        return nullptr;
    }

    return &this->m_byId[index];
}

void EntryManager::MarkRemoved(Entry* e, int32_t id) {
    FrameScript_SignalEvent(SCRIPT_EVENT_NAME_PLATE_UNIT_REMOVED, "%s", this->GetToken(id));
    e->SetState(Entry::STATE_IS_ACTIVE, false);
}

void EntryManager::Track(CGNamePlateFrame* plate) {
    int32_t id = plate->m_awesomePlateId;
    Entry* e = this->GetEntry(id);
    bool active = e && e->HasState(Entry::STATE_IS_ACTIVE);

    if (!plate->m_isShown) {
        if (active) {
            this->MarkRemoved(e, id);
        }

        return;
    }

    CGUnit_C* unit = UnitPtr(plate->m_guid);

    if (active) {
        if (!unit || !unit->m_namePlateFrame) {
            this->MarkRemoved(e, id);
        } else if (unit->m_namePlateFrame->m_guid != e->m_guid) {
            // a pinned plate would keep the cursor while showing another unit
            e->SetState(Entry::STATE_IS_FROZEN, false);
            e->SetState(Entry::STATE_IS_SLIDING, false);
            FrameScript_SignalEvent(SCRIPT_EVENT_NAME_PLATE_UNIT_REMOVED, "%s", this->GetToken(id));
            this->AppendAdded(id);
        }

        return;
    }

    if (!unit || !IsNamedUnit(unit)) {
        return;
    }

    e = this->InitEntry(plate);

    if (!e) {
        return;
    }

    e->ClearState();
    e->SetState(Entry::STATE_IS_FRIENDLY, IsFriendly(unit));
    e->m_classification = unit->GetCreatureClassification();

    e->UpdateStacking();
    e->UpdateClamping();
    this->ResolvePairs(e, ~0ull, 0.0f);
    this->AppendAdded(plate->m_awesomePlateId);

    // for occlusion
    plate->m_awesomeFresh = 1;
    plate->m_awesomeOpaque = 0;
}

// press timed here, the engine's own press time runs on the input event clock
MouseInput EntryManager::GetMouseInput(uint64_t ms) {
    CGInputControl* input = CGInputControl::GetActive();
    uint32_t flags = input ? input->m_flags : 0;

    if (!(flags & INPUT_MOUSE_BUSY)) {
        this->m_pressMs = 0;
        return MOUSE_IDLE;
    }

    if (!this->m_pressMs) {
        this->m_pressMs = ms;
    }

    // CGInputControl::IsMouseDrag, timed from our own press
    uint64_t held = ms - this->m_pressMs;
    bool drag = held >= 800 || ((input->m_dragAccumX >= 8.0f || input->m_dragAccumY >= 8.0f) && held >= 200);
    bool dragging = (flags & (INPUT_MOUSE_LOOK | INPUT_MOUSE_STEER)) || drag;
    return dragging ? MOUSE_DRAGGING : MOUSE_CLICK_PENDING;
}

void EntryManager::Stack(uint64_t ms, float sceneTime, float pullScale) {
    if (StackingSetting() == STACKING_DISABLED) {
        return;
    }

    float bandX = s_cvBandX.m_floatValue;
    float bandY = s_cvBandY.m_floatValue;
    size_t count = this->m_entries.size();

    for (size_t i = 0; i < count; i++) {
        Entry* e1 = this->m_entries[i];

        if (!e1->HasState(Entry::STATE_SHOULD_STACK) || e1->HasState(Entry::STATE_IS_FRESH) || e1->IsTransparent()) {
            continue;
        }

        bool frozen1 = e1->HasState(Entry::STATE_IS_FROZEN);
        bool freed = frozen1 || s_cvStacking.m_intValue > STACKING_DISABLED;

        for (size_t j = i + 1; j < count; j++) {
            Entry* e2 = this->m_entries[j];

            // skip fresh plates, NAME_PLATE_UNIT_ADDED handlers later might disable collisions
            if (!e2->HasState(Entry::STATE_SHOULD_STACK) || e2->HasState(Entry::STATE_IS_FRESH) || e2->IsTransparent()) {
                continue;
            }

            bool frozen2 = e2->HasState(Entry::STATE_IS_FROZEN);

            // both pinned, neither can yield
            if (frozen1 && frozen2) {
                continue;
            }

            this->SeedPair(e1, e2, ms);
            PairState* pair = this->GetPair(e1, e2);
            float dx = e1->GetReqDxFor(e2);
            float minSepX = e1->GetAvgWFor(e2, bandX * pair->m_hysteresis);

            if (!(dx <= minSepX - EPSILON)) {
                continue;
            }

            float reqY = e1->GetReqYFor(e2, bandY);

            if (reqY > e2->m_targetOffset.y) {
                if (frozen2) {
                    continue;
                }

                if (!frozen1 && e2->GetRankWeight() > e1->GetRankWeight()) {
                    // rank priority hot correction
                    reqY = e2->GetReqYFor(e1, bandY);

                    if (reqY > e1->m_targetOffset.y) {
                        e1->m_targetOffset.y = reqY;
                        this->CommitPair(e1, e2, ms, sceneTime, bandY, bandX);
                    }
                } else {
                    if (!freed && !e1->ResolvePush(e2, bandY, pair->m_hysteresis)) {
                        freed = true;
                        continue;
                    }

                    e2->m_targetOffset.y = reqY;
                    e1->Pull(e2, dx, bandX, pullScale);
                    this->CommitPair(e1, e2, ms, sceneTime, bandY, bandX);
                }
            } else if (!pair->IsStale(1) && (e1->GetTopNdc(pair->m_hysteresis) + e1->m_targetOffset.y + e1->GetAvgHFor(e2, bandY) > (e2->GetBotNdc(pair->m_hysteresis) + e2->m_targetOffset.y))) {
                // overlap at extended range, keep the commitment and pull up
                e1->Pull(e2, dx, bandX, pullScale);
                this->CommitPair(e1, e2, ms, sceneTime, bandY, bandX);
            }
        }
    }
}

void EntryManager::OnPlateCreated(CGNamePlateFrame* plate) {
    static const AwesomeLuaFunction s_methods[] = {
        { "SetStackingEnabled", &Script_SetStackingEnabled },
        { "GetStackingEnabled", &Script_GetStackingEnabled },
        { "SetOcclusionEnabled", &Script_SetOcclusionEnabled },
        { "GetOcclusionEnabled", &Script_GetOcclusionEnabled },
    };

    lua_State* L = FrameScript_GetContext();

    if (!L) {
        return;
    }

    lua_pushstring(L, "NAME_PLATE_CREATED");
    AwesomeLuaPushObject(L, PlateObject(plate));

    for (size_t i = 0; i < sizeof(s_methods) / sizeof(s_methods[0]); i++) {
        lua_pushcclosure(L, s_methods[i].m_function, 0);
        lua_setfield(L, -2, s_methods[i].m_name);
    }

    // the event name and the frame are the two arguments already on the stack
    FrameScript_SignalEvent(SCRIPT_EVENT_NAME_PLATE_CREATED, L, 2);
    lua_settop(L, -3);
}

// original logic, clamped search boundaries
bool EntryManager::IsUnderCursor(const Entry* e, const C2Vector& position) {
    const CGNamePlateFrame* plate = e->m_plate;

    if (!(plate->m_layoutFlags & LAYOUT_FLAG_VALID_RECT)) {
        return false;
    }

    bool friendly = e->HasState(Entry::STATE_IS_FRIENDLY);
    Vec2 hitBox = friendly ? Vec2(s_cvHitboxWidthF.m_floatValue, s_cvHitboxHeightF.m_floatValue) : Vec2(s_cvHitboxWidthE.m_floatValue, s_cvHitboxHeightE.m_floatValue);
    Vec2 size = Vec2(plate->m_width, plate->m_height) * plate->m_layoutScale;
    const CRect& rect = plate->m_rect;
    float minY;
    float maxY;

    switch (s_cvHitboxAnchor.m_intValue) {
        case 0:
            maxY = rect.maxY;
            minY = rect.maxY - size.y * hitBox.y;
            break;

        case 2:
            minY = rect.minY;
            maxY = rect.minY + size.y * hitBox.y;
            break;

        default: {
            float midY = (rect.minY + rect.maxY) * 0.5f;
            float halfHeight = size.y * hitBox.y * 0.5f;
            minY = midY - halfHeight;
            maxY = midY + halfHeight;
            break;
        }
    }

    float midX = (rect.minX + rect.maxX) * 0.5f;
    float halfWidth = (size.x * 0.5f) * hitBox.x;
    return position.x >= midX - halfWidth && position.x <= midX + halfWidth && position.y >= minY && position.y <= maxY;
}

CGNamePlateFrame* EntryManager::PickPlate(ClickLogic mode, const C2Vector& position) const {
    WOWGUID target = CGGameUI::m_lockedTarget;

    // a raised frozen plate keeps the cursor, the plates yielding to it slide through underneath;
    // unraised, it may sit under another plate and the click goes to what is visibly on top
    RaiseFilter raise = MouseoverRaise();

    if (raise.m_friendly || raise.m_enemy) {
        for (size_t i = 0; i < this->m_entries.size(); i++) {
            const Entry* e = this->m_entries[i];

            if (e->HasState(Entry::STATE_IS_FROZEN) && raise.Passes(e->HasState(Entry::STATE_IS_FRIENDLY)) && target != e->m_plate->m_guid && IsUnderCursor(e, position)) {
                return e->m_plate;
            }
        }
    }

    CGNamePlateFrame* priority = nullptr;

    for (size_t i = 0; i < this->m_entries.size(); i++) {
        const Entry* e = this->m_entries[i];

        if (target == e->m_plate->m_guid || !IsUnderCursor(e, position)) {
            continue;
        }

        bool friendly = e->HasState(Entry::STATE_IS_FRIENDLY);

        if (mode == CLICK_THRU_ENEMY) {
            if (friendly) {
                return e->m_plate;
            }
        } else if (mode == CLICK_THRU_FRIEND) {
            if (!friendly) {
                return e->m_plate;
            }
        } else {
            return e->m_plate;
        }

        if (!priority) {
            priority = e->m_plate;
        }
    }

    return priority;
}

// the one plate to freeze: the topmost of the focus and the target under the cursor. the engine never focuses the
// target, so with the target on top the focus is a plate underneath it
CGNamePlateFrame* EntryManager::HoverPlate(const CGNamePlateFrame* focus, const C2Vector& position) const {
    WOWGUID target = CGGameUI::m_lockedTarget;

    // still in the last draw order, topmost first
    for (size_t i = 0; i < this->m_entries.size(); i++) {
        const Entry* e = this->m_entries[i];

        if (e->m_plate == focus) {
            return e->m_plate;
        }

        if (target && e->m_plate->m_guid == target && IsUnderCursor(e, position)) {
            return e->m_plate;
        }
    }

    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Engine entry points

void AwesomeNamePlatesUpdate(CGWorldFrame* worldFrame) {
    s_plates.Update(worldFrame);
}

void AwesomeNamePlatesClearAll() {
    s_plates.ClearAll();
}

void AwesomeNamePlateOnReactionChanged(CGUnit_C* unit) {
    s_plates.OnReactionChanged(unit);
}

float AwesomeNamePlateDistanceSquared() {
    return s_distanceSquared;
}

// whether an off-screen plate of this unit is kept alive and clamped
static bool ClampsUnit(CGUnit_C* unit) {
    if (s_cvClampMode.m_intValue == CLAMP_DISABLED) {
        return false;
    }

    return ClampFilterPasses(unit->m_obj->m_guid, CGGameUI::m_lockedTarget, AwesomeInCombatLockdown()) && ClampsRank(unit->GetCreatureClassification());
}

bool AwesomeNamePlateScreenPosition(CGUnit_C* unit, CGWorldFrame* worldFrame, C3Vector* screen) {
    C3Vector position = unit->GetNamePosition();
    // MODIFIED: nameplatePlacement replaces the engine's fixed 0.6666667 lift (awesome_wotlk; 0 by default)
    position.z += s_cvPlacement.m_floatValue;

    uint32_t mask = 0;

    if (worldFrame->GetScreenCoordinates(position, screen, &mask)) {
        return true;
    }

    if (mask > 0) {
        Vec2 margin = Vec2(PLATE_DEFAULT_WIDTH * 0.5f, PLATE_DEFAULT_HEIGHT * 0.5f) * AspectNormal();
        float width = worldFrame->m_rect.maxX - worldFrame->m_rect.minX;
        float height = worldFrame->m_rect.maxY - worldFrame->m_rect.minY;

        // at least half out of the viewport but still visible: keep it, so names do not pop in
        if (screen->x >= -margin.x && screen->x <= width + margin.x && screen->y >= -margin.y && screen->y <= height + margin.y) {
            return true;
        }
    }

    if (!ClampsUnit(unit)) {
        return false;
    }

    switch (s_cvClampMode.m_intValue) {
        case CLAMP_ALL:
        case CLAMP_BOSS:
            return mask == 7;

        case CLAMP_ALL_EDGES:
        case CLAMP_BOSS_EDGES:
            return mask > 0;

        default:
            return false;
    }
}

void AwesomeNamePlateSetFocus(const C2Vector& position) {
    CGInputControl* input = CGInputControl::GetActive();

    if (!input || (input->m_flags & INPUT_MOUSE_BUSY)) {
        return;
    }

    uint32_t mode = s_derived.m_mouseMode;
    CGNamePlateFrame* priority;

    if (mode & MOUSE_CLICK_THRU_ENEMY) {
        priority = s_plates.PickPlate(CLICK_THRU_ENEMY, position);
    } else if (mode & MOUSE_CLICK_THRU_FRIEND) {
        priority = s_plates.PickPlate(CLICK_THRU_FRIEND, position);
    } else {
        priority = s_plates.PickPlate(CLICK_NONE, position);
    }

    if (priority != CGUnit_C::s_namePlateFocus) {
        if (CGUnit_C::s_namePlateFocus) {
            CGUnit_C::s_namePlateFocus->OnLoseFocus();
        }

        CGUnit_C::s_namePlateFocus = priority;

        if (priority) {
            priority->OnGainFocus();
        }

        AwesomeNamePlatesMarkDirty();
    }

    if (FreezeActive()) {
        s_plates.SetHover(s_plates.HoverPlate(priority, position));
    }
}

uint8_t AwesomeNamePlateAlpha(CGNamePlateFrame* plate, CGUnit_C* unit) {
    if (!unit || !unit->m_namePlateFrame) {
        return 255;
    }

    plate = unit->m_namePlateFrame;
    WOWGUID target = CGGameUI::m_lockedTarget;
    bool isTargetOrMouseover = target ? unit->m_obj->m_guid == target : plate == CGUnit_C::s_namePlateFocus;
    uint8_t targetAlpha = 255;

    if (target && !isTargetOrMouseover) {
        targetAlpha = s_derived.m_nonTargetAlpha;
    }

    if (!isTargetOrMouseover && s_derived.m_occlusionOn && (s_cvOcclusionMode.m_intValue == OCCLUSION_ALWAYS || !AwesomeInCombatLockdown()) && !plate->m_awesomeOpaque) {
        if (CGCamera* camera = CGWorldFrame::GetActiveCamera()) {
            C3Vector start = camera->m_position;
            C3Vector end = unit->GetPosition();
            end.z += unit->m_movementData.m_collisionHeight * 0.666f;

            C3Vector hit(0.0f, 0.0f, 0.0f);
            float distance = 1.0f;

            if (CMap::Intersect(&start, &end, &hit, &distance, WORLD_TRACE_HIT_FLAGS, nullptr)) {
                if (s_derived.m_occludeUp) {
                    targetAlpha = std::min(targetAlpha, s_derived.m_occlusionCap);
                } else {
                    targetAlpha = static_cast<uint8_t>(static_cast<float>(targetAlpha) * s_derived.m_occlusionScale);
                }
            }
        }
    }

    if (plate->m_awesomeFresh) {
        plate->m_awesomeFresh = 0;
        return targetAlpha;
    }

    float current = static_cast<float>(plate->m_alpha);
    float delta = static_cast<float>(targetAlpha) - current;

    if (std::abs(delta) < 0.5f) {
        return targetAlpha;
    }

    const CGWorldFrame* worldFrame = CGWorldFrame::s_currentWorldFrame;
    float step = delta * PerFrame(s_cvAlphaSpeed.m_floatValue, worldFrame ? SceneDelta(worldFrame) : 1.0f / TUNED_FPS);
    float next = current + (std::abs(step) < 1.0f ? std::copysign(1.0f, delta) : step);

    if (std::abs(static_cast<float>(targetAlpha) - next) < 1.0f) {
        return targetAlpha;
    }

    bool overshoot = (delta > 0.0f && next > targetAlpha) || (delta < 0.0f && next < targetAlpha);
    return overshoot ? targetAlpha : static_cast<uint8_t>(next);
}

// ---------------------------------------------------------------------------------------------------------------------
// Tokens and Lua

int32_t AwesomeNamePlateGetTokenId(const WOWGUID& guid) {
    return s_plates.GetTokenId(guid);
}

WOWGUID AwesomeNamePlateGetTokenGUID(int32_t index) {
    return s_plates.GetTokenGuid(index);
}

static WOWGUID GuidFromString(const char* text) {
    if (!text) {
        return WOWGUID();
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        return StringToGuid(text);
    }

    return AwesomeGuidFromUnitToken(text);
}

// a numeric GUID string at argument 1, 0 when missing
static WOWGUID GuidArg(lua_State* L) {
    const char* text = luaL_checklstring(L, 1, nullptr);
    WOWGUID guid;
    guid.m_guid = text ? _strtoui64(text, nullptr, 0) : 0;
    return guid;
}

static int32_t PushPlate(lua_State* L, const WOWGUID& guid) {
    CGNamePlateFrame* plate = s_plates.GetPlate(guid);

    if (!plate) {
        return 0;
    }

    AwesomeLuaPushObject(L, PlateObject(plate));
    return 1;
}

static int32_t Script_GetNamePlates(lua_State* L) {
    lua_createtable(L, 0, 0);
    std::vector<Entry*>& entries = s_plates.Entries();

    for (size_t i = 0; i < entries.size(); i++) {
        AwesomeLuaPushObject(L, PlateObject(entries[i]->m_plate));
        lua_rawseti(L, -2, static_cast<int32_t>(i + 1));
    }

    return 1;
}

static int32_t Script_GetNamePlateForUnit(lua_State* L) {
    const char* token = luaL_checklstring(L, 1, nullptr);
    return token ? PushPlate(L, GuidFromString(token)) : 0;
}

static int32_t Script_GetNamePlateByGUID(lua_State* L) {
    return PushPlate(L, GuidArg(L));
}

static int32_t Script_GetNamePlateTokenByGUID(lua_State* L) {
    WOWGUID guid = GuidArg(L);

    if (!guid) {
        return 0;
    }

    lua_pushstring(L, s_plates.GetToken(s_plates.GetTokenId(guid)));
    return 1;
}

static void OnMarkDirty(AwesomeCVar* cvar, bool changed) {
    AwesomeNamePlatesMarkDirty();
}

static void OnDerived(AwesomeCVar* cvar, bool changed) {
    ApplyDerived();
    AwesomeNamePlatesMarkDirty();
}

static void OnDistance(AwesomeCVar* cvar, bool changed) {
    if (!changed) {
        return;
    }

    s_distanceSquared = cvar->m_floatValue * cvar->m_floatValue;
    AwesomeNamePlatesMarkDirty();
}

static void OnClampMode(AwesomeCVar* cvar, bool changed) {
    if (!changed) {
        return;
    }

    s_plates.RefreshClamping();
    AwesomeNamePlatesMarkDirty();
}

static void OnStackingMode(AwesomeCVar* cvar, bool changed) {
    if (!changed) {
        return;
    }

    s_plates.RefreshStacking();
    AwesomeNamePlatesMarkDirty();
}

void AwesomeNamePlatesRegisterCVars() {
    s_plates.Init(MAX_PLATES);

    s_cvDistance.Register();
    s_cvPlacement.Register();
    s_cvMouseMode.Register();
    s_cvBandX.Register();
    s_cvBandY.Register();
    s_cvHitboxAnchor.Register();
    s_cvHitboxWidthE.Register();
    s_cvHitboxHeightE.Register();
    s_cvHitboxWidthF.Register();
    s_cvHitboxHeightF.Register();
    s_cvRaiseSpeed.Register();
    s_cvLowerSpeed.Register();
    s_cvPullSpeed.Register();
    s_cvRaiseDistance.Register();
    s_cvPullDistance.Register();
    s_cvOcclusionAlpha.Register();
    s_cvOcclusionMode.Register();
    s_cvNonTargetAlpha.Register();
    s_cvAlphaSpeed.Register();
    s_cvInertia.Register();
    s_cvHysteresisDecay.Register();
    s_cvClampMode.Register();
    s_cvClampVOffset.Register();
    s_cvClampHOffset.Register();
    s_cvClampFilter.Register();
    s_cvMouseFreeze.Register();
    s_cvMouseFreezeGrace.Register();
    s_cvMouseFreezeTime.Register();
    s_cvStacking.Register();

    ApplyDerived();
}

void AwesomeNamePlatesRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetNamePlates", &Script_GetNamePlates },
        { "GetNamePlateForUnit", &Script_GetNamePlateForUnit },
        { "GetNamePlateByGUID", &Script_GetNamePlateByGUID },
        { "GetNamePlateTokenByGUID", &Script_GetNamePlateTokenByGUID },
    };

    AwesomeLuaRegisterTable(L, "C_NamePlate", s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}

bool AwesomeGetGUIDFromToken(const char*& token, WOWGUID& guid) {
    if (SStrCmpI(token, "nameplate", 9)) {
        return false;
    }

    token += 9;
    uint32_t index = SStrToUnsigned(&token);
    guid = index > 0 ? s_plates.GetTokenGuid(static_cast<int32_t>(index - 1)) : WOWGUID();
    return true;
}

void AwesomeGetTokenIDsFromGUID(const WOWGUID& guid, char** tokens, int32_t* count, int32_t capacity, uint32_t tokenSize) {
    if (*count >= capacity) {
        return;
    }

    int32_t id = s_plates.GetTokenId(guid);

    if (id >= 0) {
        SStrPrintf(tokens[(*count)++], tokenSize, "nameplate%d", id + 1);
    }
}
