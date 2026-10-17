#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <windows.h>
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameXML/XMLTree.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "WoW/Source/DB/Rec/LockRec.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/Object/GameObject.h"
#include "WoW/Source/Object/ObjectClient/GameObject_C.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/Object/Unit.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/MinimapFrame.h"
#include "WoW/Source/Ui/UIBindings.h"
#include "WoW/Source/Ui/UIMacros.h"
#include "WoW/Source/UIUtil/PortraitButton.h"

// ChatFrame_Scripts.cpp: the chat ([0]) and combat ([1]) log file names
extern const char* s_chatLogNames[2];

// ---------------------------------------------------------------------------------------------------------------------
// Smart interaction: QueueInteract(), /interact and the "Interaction Button" keybinding loot, skin or use the nearest
// (or forward-facing) mob or object; interactionHighlight outlines the current candidate.

static const uint32_t UNIT_FLAG_SKINNABLE = 0x04000000;
static const uint32_t UNIT_DYNFLAG_LOOTABLE = 0x0001;
// added to a unit's combat reach for the interact range (CGUnit_C's friendly reach, 0x009E8D2C in 12340)
static const float INTERACT_REACH_BONUS = 4.0f;
static const float INTERACT_SCAN_RANGE_SQ = 400.0f;
static const DWORD INTERACT_SCAN_INTERVAL_MS = 100;

// CGObject_C::m_modelFlags: one bit per highlight type (locked target, mouseover, interaction) and the loot glow
static const uint32_t HIGHLIGHT_TYPE_INTERACT = 2;
static const uint32_t HIGHLIGHT_MASK_NATIVE = (1u << 24) | (1u << 25);
static const uint32_t HIGHLIGHT_MASK_PASSIVE_LOOT_GLOW = 1u << 22;

enum {
    GAMEOBJECT_TYPE_DOOR = 0,
    GAMEOBJECT_TYPE_BUTTON = 1,
    GAMEOBJECT_TYPE_QUESTGIVER = 2,
    GAMEOBJECT_TYPE_CHEST = 3,
    GAMEOBJECT_TYPE_BINDER = 4,
    GAMEOBJECT_TYPE_CHAIR = 7,
    GAMEOBJECT_TYPE_SPELL_FOCUS = 8,
    GAMEOBJECT_TYPE_GOOBER = 10,
    GAMEOBJECT_TYPE_FISHINGNODE = 17,
    GAMEOBJECT_TYPE_MAILBOX = 19,
    GAMEOBJECT_TYPE_MEETINGSTONE = 23,
    GAMEOBJECT_TYPE_GUILD_BANK = 34,
};

static const uint32_t LOCK_KEY_SKILL = 2;

static void OnInteractionHighlight(AwesomeCVar* cvar, bool changed);
static void OnPortraitResolution(AwesomeCVar* cvar, bool changed);
static void OnMinimapIndoorResolution(AwesomeCVar* cvar, bool changed);
static void OnObjectHighlightMode(AwesomeCVar* cvar, bool changed);
static void OnChatLogSessionKey(AwesomeCVar* cvar, bool changed);
static void OnCombatLogSessionKey(AwesomeCVar* cvar, bool changed);

static AwesomeCVar s_cvInteractionAngle = AwesomeCVar::Int("interactionAngle", 60, 15, 160);
static AwesomeCVar s_cvInteractionMode = AwesomeCVar::Int("interactionMode", 1, 0, 1);
static AwesomeCVar s_cvInteractionHighlight = AwesomeCVar::Int("interactionHighlight", 1, 0, 1, OnInteractionHighlight);
// MODIFIED: 256 by default, the resolution the replaced client's binary patch rendered at (awesome_wotlk: 64)
static AwesomeCVar s_cvPortraitResolution = AwesomeCVar::Int("portraitResolution", 256, 64, 2048, OnPortraitResolution);
// MODIFIED: indoor minimap render target (stock 256); 1024 keeps the WMO minimap textures' full detail at every zoom
static AwesomeCVar s_cvMinimapIndoorResolution = AwesomeCVar::Int("minimapIndoorResolution", 1024, 256, 2048, OnMinimapIndoorResolution);
static AwesomeCVar s_cvObjectHighlightMode = AwesomeCVar::Int("objectHighlightMode", 0, 0, 2, OnObjectHighlightMode);
static AwesomeCVar s_cvChatLogSessionKey = AwesomeCVar::Int("chatLogSessionKey", 1, 0, 1, OnChatLogSessionKey);
static AwesomeCVar s_cvCombatLogSessionKey = AwesomeCVar::Int("combatLogSessionKey", 1, 0, 1, OnCombatLogSessionKey);

struct InteractState {
    // one-shot interaction queued from Lua
    WOWGUID m_request;
    // nearest valid interaction target
    WOWGUID m_candidate;
    // candidate currently showing the interaction highlight
    WOWGUID m_highlighted;
};

static InteractState s_interact;

static bool IsInteractableGameObjectType(int32_t type) {
    switch (type) {
        case GAMEOBJECT_TYPE_DOOR:
        case GAMEOBJECT_TYPE_BUTTON:
        case GAMEOBJECT_TYPE_QUESTGIVER:
        case GAMEOBJECT_TYPE_CHEST:
        case GAMEOBJECT_TYPE_BINDER:
        case GAMEOBJECT_TYPE_CHAIR:
        case GAMEOBJECT_TYPE_SPELL_FOCUS:
        case GAMEOBJECT_TYPE_GOOBER:
        case GAMEOBJECT_TYPE_FISHINGNODE:
        case GAMEOBJECT_TYPE_MAILBOX:
        case GAMEOBJECT_TYPE_MEETINGSTONE:
        case GAMEOBJECT_TYPE_GUILD_BANK:
            return true;

        default:
            return false;
    }
}

static bool IsValidInteractObject(CGObject_C* object, CGUnit_C* player, float distanceSq) {
    if (object->m_typeId == ID_UNIT) {
        CGUnit_C* unit = static_cast<CGUnit_C*>(object);
        bool lootable = (unit->m_unit->m_unitDynamicFlags & UNIT_DYNFLAG_LOOTABLE) != 0;
        bool skinnable = (unit->m_unit->m_unitFieldFlags & UNIT_FLAG_SKINNABLE) != 0;
        bool assistable = player->CanAssist(unit, true);

        if (!lootable && !skinnable && !(assistable && unit->m_unit->m_unitNpcFlags)) {
            return false;
        }

        float reach = unit->m_unit->m_unitFieldCombatreach + INTERACT_REACH_BONUS;
        return distanceSq <= reach * reach;
    }

    if (object->m_typeId == ID_GAMEOBJECT) {
        CGGameObject_C* gameObject = static_cast<CGGameObject_C*>(object);
        return IsInteractableGameObjectType(gameObject->m_gameObject->m_typeID) && gameObject->CanUse() && gameObject->CanUseNow();
    }

    return false;
}

static CGObject_C* InteractObjectPtr(const WOWGUID& guid) {
    return AwesomeObjectPtr(guid, TYPEMASK_GAMEOBJECT | TYPEMASK_UNIT);
}

// the interaction highlight is drawn dimmer than the engine's target and mouseover highlights
static void DimInteractHighlight(CGObject_C* object) {
    if (object->m_modelFlags & HIGHLIGHT_MASK_NATIVE) {
        return;
    }

    CM2Model* model = object->GetObjectModel();

    if (model) {
        model->m_emissiveTintR *= 0.8f;
        model->m_emissiveTintG *= 0.8f;
        model->m_emissiveTintB *= 0.8f;
    }
}

struct InteractScan {
    CGPlayer_C* m_player;
    C3Vector m_position;
    float m_facing;
    bool m_inAngle;
    float m_minDot;
    WOWGUID m_candidate;
    float m_bestDistanceSq;
};

static int32_t ScanInteractCandidate(WOWGUID guid, void* param) {
    InteractScan* scan = static_cast<InteractScan*>(param);

    if (guid == scan->m_player->m_obj->m_guid) {
        return 1;
    }

    CGObject_C* object = InteractObjectPtr(guid);

    if (!object) {
        return 1;
    }

    C3Vector position = object->GetPosition();
    float dx = position.x - scan->m_position.x;
    float dy = position.y - scan->m_position.y;
    float dz = position.z - scan->m_position.z;
    float distanceSq = dx * dx + dy * dy + dz * dz;

    if (distanceSq == 0.0f || distanceSq > INTERACT_SCAN_RANGE_SQ || distanceSq > scan->m_bestDistanceSq) {
        return 1;
    }

    if (!IsValidInteractObject(object, scan->m_player, distanceSq)) {
        return 1;
    }

    if (scan->m_inAngle) {
        float lengthSq = dx * dx + dy * dy;

        if (lengthSq == 0.0f) {
            return 1;
        }

        float length = std::sqrt(lengthSq);

        if ((dx / length) * std::cos(scan->m_facing) + (dy / length) * std::sin(scan->m_facing) < scan->m_minDot) {
            return 1;
        }
    }

    scan->m_candidate = guid;
    scan->m_bestDistanceSq = distanceSq;
    return 1;
}

static void UpdateInteractCandidate() {
    if (!CGGameUI::s_inWorld) {
        return;
    }

    static DWORD s_lastScan;
    DWORD now = GetTickCount();

    if (now - s_lastScan < INTERACT_SCAN_INTERVAL_MS) {
        return;
    }

    s_lastScan = now;

    CGPlayer_C* player = static_cast<CGPlayer_C*>(AwesomeObjectPtr(ClntObjMgrGetActivePlayer(), TYPEMASK_PLAYER));

    if (!player) {
        return;
    }

    InteractScan scan;
    scan.m_player = player;
    scan.m_position = player->GetPosition();
    scan.m_facing = player->GetFacing();
    scan.m_inAngle = s_cvInteractionMode.m_intValue == 1;
    scan.m_minDot = std::cos(static_cast<float>(s_cvInteractionAngle.m_intValue) * 0.5f * 3.14159265f / 180.0f);
    scan.m_candidate = WOWGUID();
    scan.m_bestDistanceSq = INTERACT_SCAN_RANGE_SQ;
    ClntObjMgrEnumVisibleObjects(ScanInteractCandidate, &scan);

    s_interact.m_candidate = scan.m_candidate;

    WOWGUID highlighted = s_cvInteractionHighlight.m_intValue ? scan.m_candidate : WOWGUID();

    if (s_interact.m_highlighted == highlighted) {
        return;
    }

    if (CGObject_C* previous = InteractObjectPtr(s_interact.m_highlighted)) {
        previous->HideHighlightType(HIGHLIGHT_TYPE_INTERACT);
    }

    s_interact.m_highlighted = highlighted;

    if (CGObject_C* current = InteractObjectPtr(highlighted)) {
        current->ShowHighlightType(HIGHLIGHT_TYPE_INTERACT);
        DimInteractHighlight(current);
    }
}

static void ProcessQueuedInteraction() {
    if (!s_interact.m_request) {
        return;
    }

    if (CGObject_C* object = InteractObjectPtr(s_interact.m_request)) {
        object->OnRightClick();
    }

    s_interact.m_request = WOWGUID();
}

static void QueueInteract(const char* unit) {
    if (!CGGameUI::s_inWorld) {
        return;
    }

    if (!unit) {
        if (s_interact.m_candidate) {
            s_interact.m_request = s_interact.m_candidate;
        }

        return;
    }

    for (const char* c = unit; *c; c++) {
        if (!isalnum(static_cast<unsigned char>(*c))) {
            return;
        }
    }

    WOWGUID guid = AwesomeGuidFromUnitToken(unit);

    if (guid) {
        s_interact.m_request = guid;
    }
}

static int32_t Script_QueueInteract(lua_State* L) {
    QueueInteract(AwesomeLuaIsNoneOrNil(L, 1) ? nullptr : lua_tolstring(L, 1, nullptr));
    return 0;
}

// /interact [conditions] [@unit]: the standard macro options pick the unit, without one it takes the candidate
static int32_t Script_InteractCommand(lua_State* L) {
    const char* options = AwesomeLuaIsNoneOrNil(L, 1) ? "" : lua_tolstring(L, 1, nullptr);

    if (!options) {
        return 0;
    }

    MacroSubstring result = { nullptr, nullptr };
    MacroSubstring target = { nullptr, nullptr };

    // MODIFIED: nothing happens when no option matched (awesome_wotlk then still interacted with the candidate)
    if (!EvaluateMacroOptions(options, options + SStrLen(options), &result, &target)) {
        return 0;
    }

    if (target.m_end == target.m_start) {
        QueueInteract(nullptr);
        return 0;
    }

    char unit[64];
    size_t length = static_cast<size_t>(target.m_end - target.m_start);

    if (length >= sizeof(unit)) {
        return 0;
    }

    memcpy(unit, target.m_start, length);
    unit[length] = '\0';
    QueueInteract(unit);
    return 0;
}

static void OnInteractionHighlight(AwesomeCVar* cvar, bool changed) {
    // the next scan shows or clears the highlight
}

void AwesomeOnHideHighlight(CGObject_C* object, int32_t type) {
    // hiding the target or mouseover highlight leaves the interaction highlight at their full brightness
    if (!s_cvInteractionHighlight.m_intValue || type == HIGHLIGHT_TYPE_INTERACT || !s_interact.m_highlighted || object->m_obj->m_guid != s_interact.m_highlighted) {
        return;
    }

    DimInteractHighlight(object);
}

static void RegisterInteractionBinding() {
    CGUIBindings* bindings = CGUIBindings::s_bindings;
    lua_State* L = FrameScript_GetContext();

    if (!bindings || !L || bindings->m_commands.Ptr("INTERACTIONKEYBIND") || bindings->m_commands.Ptr("HEADER_AWESOME_WOTLK_KEYBINDS")) {
        return;
    }

    lua_pushstring(L, "Awesome Wotlk Keybinds");
    lua_setfield(L, LUA_GLOBALSINDEX, "BINDING_HEADER_AWESOME_WOTLK_KEYBINDS");
    lua_pushstring(L, "Interaction Button");
    lua_setfield(L, LUA_GLOBALSINDEX, "BINDING_NAME_INTERACTIONKEYBIND");

    XMLNode node(nullptr, "Binding");
    node.SetAttribute("name", "INTERACTIONKEYBIND");
    node.SetAttribute("header", "AWESOME_WOTLK_KEYBINDS");
    node.m_body = SStrDupA("QueueInteract()", __FILE__, __LINE__);
    node.m_bodyLen = SStrLen(node.m_body);

    CStatus status;
    bindings->LoadBinding("AWESOME_KEYBIND", &node, &status);
}

// ---------------------------------------------------------------------------------------------------------------------
// Macro conditionals [@cursor] and [@playerlocation]: an AoE spell cast from such a macro lands at the cursor or at
// the player instead of waiting for a click.

enum PendingClick {
    PENDING_NONE,
    PENDING_CURSOR,
    PENDING_PLAYER_LOCATION,
    // the targeting was started some other way, leave it to the player
    PENDING_BLOCKED,
};

static PendingClick s_pendingClick = PENDING_NONE;

bool AwesomeMacroConsumeTarget(const char* start, const char* end) {
    if (!start || end == start) {
        return false;
    }

    if (!Spell_C_IsTargeting()) {
        s_pendingClick = PENDING_NONE;
    }

    if (s_pendingClick == PENDING_BLOCKED) {
        return false;
    }

    size_t length = static_cast<size_t>(end - start);

    if (length == 6 && !SStrCmpI(start, "cursor", 6)) {
        s_pendingClick = PENDING_CURSOR;
        return true;
    }

    if (length == 14 && !SStrCmpI(start, "playerlocation", 14)) {
        s_pendingClick = PENDING_PLAYER_LOCATION;
        return true;
    }

    return false;
}

void AwesomeOnTrackTerrain(const C3Vector& point) {
    if (Spell_C_IsTargeting()) {
        if (s_pendingClick == PENDING_NONE) {
            s_pendingClick = PENDING_BLOCKED;
            return;
        }

        if (s_pendingClick == PENDING_BLOCKED) {
            return;
        }
    }

    CGPlayer_C* player = static_cast<CGPlayer_C*>(AwesomeObjectPtr(ClntObjMgrGetActivePlayer(), TYPEMASK_PLAYER));

    if (!player) {
        return;
    }

    PendingClick pending = s_pendingClick;
    s_pendingClick = PENDING_NONE;

    if (pending != PENDING_CURSOR && pending != PENDING_PLAYER_LOCATION) {
        return;
    }

    CTerrainClickEvent click;
    click.m_guid = WOWGUID();
    click.m_point = pending == PENDING_PLAYER_LOCATION ? player->GetPosition() : point;
    click.m_button = MOUSE_BUTTON_LEFT;
    CGGameUI::HandleTerrainClick(click);
}

void AwesomeOnCancelAoeTargeting() {
    s_pendingClick = PENDING_NONE;
}

// ---------------------------------------------------------------------------------------------------------------------
// objectHighlightMode: 1 gives every usable chest, goober and quest object the loot sparkle, 2 only the ones a quest
// or the player can loot (no gathering nodes, quest objects only while they have a quest marker).

enum ObjectHighlightMode {
    OBJECT_HIGHLIGHT_DISABLED = 0,
    OBJECT_HIGHLIGHT_ALWAYS = 1,
    OBJECT_HIGHLIGHT_TRACKED = 2,
};

void AwesomeOnPassiveHighlight(CGGameObject_C* object) {
    int32_t mode = s_cvObjectHighlightMode.m_intValue;

    if (mode == OBJECT_HIGHLIGHT_DISABLED) {
        return;
    }

    int32_t type = object->m_gameObject->m_typeID;

    if (!object->CanUse() || (type != GAMEOBJECT_TYPE_CHEST && type != GAMEOBJECT_TYPE_GOOBER && type != GAMEOBJECT_TYPE_QUESTGIVER)) {
        return;
    }

    if (mode == OBJECT_HIGHLIGHT_TRACKED) {
        if (type == GAMEOBJECT_TYPE_QUESTGIVER && !object->m_interactIcon) {
            return;
        }

        if (type == GAMEOBJECT_TYPE_CHEST) {
            LockRec* lock = object->GetLockRec();

            // a gathering node
            if (lock && lock->m_type[0] == static_cast<int32_t>(LOCK_KEY_SKILL)) {
                return;
            }
        }
    }

    object->m_modelFlags |= HIGHLIGHT_MASK_PASSIVE_LOOT_GLOW;
    object->ShowLootEffect();
}

static int32_t RefreshPassiveHighlight(WOWGUID guid, void* param) {
    if (guid.m_guid < 0x1000) {
        return 1;
    }

    CGGameObject_C* object = static_cast<CGGameObject_C*>(AwesomeObjectPtr(guid, TYPEMASK_GAMEOBJECT));

    if (object && object->m_typeId == ID_GAMEOBJECT) {
        object->CheckForPassiveHighlight();
    }

    return 1;
}

static void OnObjectHighlightMode(AwesomeCVar* cvar, bool changed) {
    if (!changed || !AwesomeObjectPtr(ClntObjMgrGetActivePlayer(), TYPEMASK_PLAYER)) {
        return;
    }

    ClntObjMgrEnumVisibleObjects(RefreshPassiveHighlight, nullptr);
}

// ---------------------------------------------------------------------------------------------------------------------
// Session logs: chat and combat logs go to a new "Logs\<date-time> WoWChatLog.txt" every client launch.

static void SetSessionLogName(int32_t index, bool session, const char* file, const char* defaultName) {
    static char s_names[2][MAX_PATH];

    if (session) {
        snprintf(s_names[index], sizeof(s_names[index]), "Logs\\%s%s", AwesomeSessionStamp(), file);
        s_chatLogNames[index] = s_names[index];
    } else {
        s_chatLogNames[index] = defaultName;
    }
}

static void OnChatLogSessionKey(AwesomeCVar* cvar, bool changed) {
    SetSessionLogName(0, cvar->m_intValue != 0, "WoWChatLog.txt", "Logs\\WoWChatLog.txt");
}

static void OnCombatLogSessionKey(AwesomeCVar* cvar, bool changed) {
    SetSessionLogName(1, cvar->m_intValue != 0, "WoWCombatLog.txt", "Logs\\WoWCombatLog.txt");
}

// ---------------------------------------------------------------------------------------------------------------------
// portraitResolution: the live 3D unit portraits render at this many pixels, rounded up to a power of two

static void OnPortraitResolution(AwesomeCVar* cvar, bool changed) {
    uint32_t resolution = 64;

    while (resolution < static_cast<uint32_t>(cvar->m_intValue)) {
        resolution <<= 1;
    }

    PortraitSetResolution(resolution);
}

// ---------------------------------------------------------------------------------------------------------------------
// minimapIndoorResolution: the indoor minimap's render target is this many pixels square, rounded up to a power of two

static void OnMinimapIndoorResolution(AwesomeCVar* cvar, bool changed) {
    uint32_t resolution = 256;

    while (resolution < static_cast<uint32_t>(cvar->m_intValue)) {
        resolution <<= 1;
    }

    MinimapSetIndoorResolution(resolution);
}

// ---------------------------------------------------------------------------------------------------------------------
// FlashWindow / IsWindowFocused / FocusWindow / CopyToClipboard

static HWND GameWindow() {
    return static_cast<HWND>(OsGuiGetWindow(0));
}

static int32_t Script_FlashWindow(lua_State* L) {
    if (HWND window = GameWindow()) {
        FlashWindow(window, FALSE);
    }

    return 0;
}

static int32_t Script_IsWindowFocused(lua_State* L) {
    HWND window = GameWindow();

    if (!window || GetForegroundWindow() != window) {
        return 0;
    }

    lua_pushnumber(L, 1.0);
    return 1;
}

static int32_t Script_FocusWindow(lua_State* L) {
    if (HWND window = GameWindow()) {
        SetForegroundWindow(window);
    }

    return 0;
}

static int32_t Script_CopyToClipboard(lua_State* L) {
    const char* text = luaL_checklstring(L, 1, nullptr);

    if (text && *text) {
        AwesomeCopyToClipboard(text, nullptr);
    }

    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------

void AwesomeMiscRegisterCVars() {
    s_cvInteractionAngle.Register();
    s_cvInteractionMode.Register();
    s_cvInteractionHighlight.Register();
    s_cvPortraitResolution.Register();
    s_cvMinimapIndoorResolution.Register();
    s_cvObjectHighlightMode.Register();
    s_cvChatLogSessionKey.Register();
    s_cvCombatLogSessionKey.Register();
}

void AwesomeMiscRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "FlashWindow", &Script_FlashWindow },
        { "IsWindowFocused", &Script_IsWindowFocused },
        { "FocusWindow", &Script_FocusWindow },
        { "CopyToClipboard", &Script_CopyToClipboard },
        { "QueueInteract", &Script_QueueInteract },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}

void AwesomeMiscOnEnterWorld() {
    lua_State* L = FrameScript_GetContext();

    AwesomeLuaRegisterSlashCommand(L, "INTERACTCMD", "/interact", &Script_InteractCommand);
    RegisterInteractionBinding();

    if (L) {
        char message[MAX_PATH + 64];

        snprintf(message, sizeof(message), "Chat being logged to %s", s_chatLogNames[0] ? s_chatLogNames[0] : "");
        lua_pushstring(L, message);
        lua_setfield(L, LUA_GLOBALSINDEX, "CHATLOGENABLED");

        snprintf(message, sizeof(message), "Combat being logged to %s", s_chatLogNames[1] ? s_chatLogNames[1] : "");
        lua_pushstring(L, message);
        lua_setfield(L, LUA_GLOBALSINDEX, "COMBATLOGENABLED");
    }
}

void AwesomeMiscOnLeaveWorld() {
    s_interact = InteractState();
}

void AwesomeMiscOnPaint() {
    UpdateInteractCandidate();
    ProcessQueuedInteraction();
}
