#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <cstdio>
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Awesome/AwesomeNamePlates.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/ObjectClient/UnitCombatLog_C.h"
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/Object/Unit.h"

// UnitIsControlled / UnitIsDisarmed / UnitIsSilenced / UnitOccupations / UnitOwner / UnitTokenFromGUID

static const uint32_t UNIT_FLAG_SILENCED = 0x00002000;
static const uint32_t UNIT_FLAG_PACIFIED = 0x00020000;
static const uint32_t UNIT_FLAG_STUNNED = 0x00040000;
static const uint32_t UNIT_FLAG_DISARMED = 0x00200000;
static const uint32_t UNIT_FLAG_CONFUSED = 0x00400000;
static const uint32_t UNIT_FLAG_FLEEING = 0x00800000;

static CGUnit_C* UnitFromToken(lua_State* L, const char* usage) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        luaL_error(L, "%s", usage);
        return nullptr;
    }

    return static_cast<CGUnit_C*>(AwesomeObjectPtr(AwesomeGuidFromUnitToken(lua_tolstring(L, 1, nullptr)), TYPEMASK_UNIT));
}

static int32_t UnitHasFlag(lua_State* L, uint32_t flags, const char* usage) {
    CGUnit_C* unit = UnitFromToken(L, usage);

    if (!unit || !(unit->m_unit->m_unitFieldFlags & flags)) {
        return 0;
    }

    lua_pushnumber(L, 1.0);
    return 1;
}

static int32_t Script_UnitIsControlled(lua_State* L) {
    return UnitHasFlag(L, UNIT_FLAG_FLEEING | UNIT_FLAG_CONFUSED | UNIT_FLAG_STUNNED | UNIT_FLAG_PACIFIED, "Usage: UnitIsControlled(unitID)");
}

static int32_t Script_UnitIsDisarmed(lua_State* L) {
    return UnitHasFlag(L, UNIT_FLAG_DISARMED, "Usage: UnitIsDisarmed(unitID)");
}

static int32_t Script_UnitIsSilenced(lua_State* L) {
    return UnitHasFlag(L, UNIT_FLAG_SILENCED, "Usage: UnitIsSilenced(unitID)");
}

static int32_t Script_UnitOccupations(lua_State* L) {
    CGUnit_C* unit = UnitFromToken(L, "Usage: UnitOccupations(unitID)");

    if (!unit) {
        return 0;
    }

    lua_pushnumber(L, unit->m_unit->m_unitNpcFlags);
    return 1;
}

static int32_t Script_UnitOwner(lua_State* L) {
    CGUnit_C* unit = UnitFromToken(L, "Usage: UnitOwner(unitID)");

    if (!unit) {
        return 0;
    }

    WOWGUID ownerGuid = unit->m_unit->m_unitFieldSummonedby ? unit->m_unit->m_unitFieldSummonedby : unit->m_unit->m_unitFieldCreatedby;
    CGUnit_C* owner = static_cast<CGUnit_C*>(AwesomeObjectPtr(ownerGuid, TYPEMASK_UNIT));

    if (!owner) {
        return 0;
    }

    const char* name = owner->GetObjectName();
    lua_pushstring(L, name ? name : "UNKNOWN");

    char guidString[24];
    SStrPrintf(guidString, sizeof(guidString), "0x%016llX", static_cast<unsigned long long>(ownerGuid.m_guid));
    lua_pushstring(L, guidString);
    return 2;
}

static bool PushTokenIfMatches(lua_State* L, const char* token, const WOWGUID& guid) {
    if (AwesomeGuidFromUnitToken(token) != guid) {
        return false;
    }

    lua_pushstring(L, token);
    return true;
}

static bool PushIndexedTokenIfMatches(lua_State* L, const char* base, int32_t first, int32_t last, const WOWGUID& guid) {
    char token[32];

    for (int32_t i = first; i <= last; i++) {
        SStrPrintf(token, sizeof(token), "%s%d", base, i);

        if (PushTokenIfMatches(L, token, guid)) {
            return true;
        }
    }

    return false;
}

static int32_t Script_UnitTokenFromGUID(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        luaL_error(L, "Usage: UnitTokenFromGUID(GUID)");
        return 0;
    }

    WOWGUID guid = StringToGuid(lua_tolstring(L, 1, nullptr));

    if (!guid || !AwesomeObjectPtr(guid, TYPEMASK_UNIT)) {
        return 0;
    }

    static const char* s_singleTokens[] = { "player", "vehicle", "pet", "target", "focus", "mouseover" };

    for (uint32_t i = 0; i < sizeof(s_singleTokens) / sizeof(s_singleTokens[0]); i++) {
        if (PushTokenIfMatches(L, s_singleTokens[i], guid)) {
            return 1;
        }
    }

    if (PushIndexedTokenIfMatches(L, "party", 1, 4, guid) || PushIndexedTokenIfMatches(L, "partypet", 1, 4, guid)
        || PushIndexedTokenIfMatches(L, "raid", 1, 40, guid) || PushIndexedTokenIfMatches(L, "raidpet", 1, 40, guid)
        || PushIndexedTokenIfMatches(L, "arena", 1, 5, guid) || PushIndexedTokenIfMatches(L, "arenapet", 1, 5, guid)
        || PushIndexedTokenIfMatches(L, "boss", 1, 5, guid)) {
        return 1;
    }

    int32_t plate = AwesomeNamePlateGetTokenId(guid);

    if (plate >= 0) {
        char token[32];
        SStrPrintf(token, sizeof(token), "nameplate%d", plate + 1);
        lua_pushstring(L, token);
        return 1;
    }

    return 0;
}

void AwesomeUnitRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "UnitIsControlled", &Script_UnitIsControlled },
        { "UnitIsDisarmed", &Script_UnitIsDisarmed },
        { "UnitIsSilenced", &Script_UnitIsSilenced },
        { "UnitOccupations", &Script_UnitOccupations },
        { "UnitOwner", &Script_UnitOwner },
        { "UnitTokenFromGUID", &Script_UnitTokenFromGUID },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}
