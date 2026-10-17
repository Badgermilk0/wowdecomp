#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <cstring>
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/DB/DBClient/DBCacheInstances.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ItemClassRec.h"
#include "WoW/Source/DB/Rec/ItemRec.h"
#include "WoW/Source/DB/Rec/ItemSubClassRec.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/Object/ItemStats.h"
#include "WoW/Source/Object/ObjectClient/Item_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/Player.h"
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/UIUtil/Tooltip.h"

// GetItemInfoInstant / GetInventoryItemTransmog / GetSpellBaseCooldown

static const char* GetItemClassName(uint32_t classId) {
    ItemClassRec* rec = g_itemClassDB.GetRecord(static_cast<int32_t>(classId));
    return rec && rec->m_className ? rec->m_className : "";
}

static const char* GetItemSubClassName(uint32_t classId, uint32_t subClassId) {
    for (int32_t i = 0; i < g_itemSubClassDB.m_numRecords; i++) {
        const ItemSubClassRec* rec = &g_itemSubClassDB.m_records[i];

        if (rec->m_classID == static_cast<int32_t>(classId) && rec->m_subClassID == static_cast<int32_t>(subClassId)) {
            if (rec->m_verboseName && *rec->m_verboseName) {
                return rec->m_verboseName;
            }

            return rec->m_displayName ? rec->m_displayName : "";
        }
    }

    return "";
}

static int32_t Script_GetItemInfoInstant(lua_State* L) {
    int32_t type = lua_type(L, 1);

    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        luaL_error(L, "Usage: GetItemInfoInstant(itemID|\"itemName\"|\"itemLink\")");
        return 0;
    }

    uint32_t itemId = 0;

    if (type == LUA_TNUMBER) {
        double number = lua_tonumber(L, 1);

        if (number <= 0.0 || number != static_cast<double>(static_cast<uint32_t>(number))) {
            return 0;
        }

        itemId = static_cast<uint32_t>(number);
    } else {
        const char* text = lua_tolstring(L, 1, nullptr);
        itemId = static_cast<uint32_t>(GetItemIDByName(text));

        if (!itemId && SStrStr(text, "item:")) {
            ItemLinkInfo linkInfo;
            linkInfo.Clear();
            itemId = static_cast<uint32_t>(linkInfo.GetItemIdFromLink(text));
        }
    }

    uint32_t itemClass;
    uint32_t subClass;
    uint32_t inventoryType;
    uint32_t displayId;
    ItemStats_C* stats = itemId ? g_itemDBCache.GetRecord(static_cast<int32_t>(itemId), WOWGUID(), nullptr, nullptr, false) : nullptr;

    if (stats) {
        itemClass = stats->m_class;
        subClass = stats->m_subClass;
        inventoryType = stats->m_inventoryType;
        displayId = stats->m_displayId;
    } else {
        // MODIFIED: awesome_wotlk only reads the item cache; Item.dbc carries the same four fields for items the
        // client has not seen yet
        ItemRec* rec = itemId ? g_itemDB.GetRecord(static_cast<int32_t>(itemId)) : nullptr;

        if (!rec) {
            luaL_error(L, "Invalid itemID");
            return 0;
        }

        itemClass = static_cast<uint32_t>(rec->m_classID);
        subClass = static_cast<uint32_t>(rec->m_subclassID);
        inventoryType = static_cast<uint32_t>(rec->m_inventoryType);
        displayId = static_cast<uint32_t>(rec->m_displayInfoID);
    }

    lua_pushnumber(L, itemId);
    lua_pushstring(L, GetItemClassName(itemClass));
    lua_pushstring(L, GetItemSubClassName(itemClass, subClass));
    lua_pushstring(L, inventoryType < 29 ? s_inventoryTypeTokens[inventoryType] : "");
    const char* art = CGItem_C::GetInventoryArt(static_cast<int32_t>(displayId));
    lua_pushstring(L, art ? art : "");
    lua_pushnumber(L, itemClass);
    lua_pushnumber(L, subClass);
    return 7;
}

static int32_t Script_GetInventoryItemTransmog(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TNUMBER) {
        luaL_error(L, "Usage: GetInventoryItemTransmog(unitID, slotID)");
        return 0;
    }

    double slot = lua_tonumber(L, 2);
    WOWGUID guid = AwesomeGuidFromUnitToken(lua_tolstring(L, 1, nullptr));

    if (slot != static_cast<double>(static_cast<int32_t>(slot)) || !guid) {
        luaL_error(L, "Usage: GetInventoryItemTransmog(unitID, slotID)");
        return 0;
    }

    CGPlayer_C* player = static_cast<CGPlayer_C*>(AwesomeObjectPtr(guid, TYPEMASK_PLAYER));

    if (!player || !player->m_player) {
        return 0;
    }

    int32_t index = static_cast<int32_t>(slot) - 1;

    if (index < 0 || index >= 19) {
        return 0;
    }

    const VisibleItem_C& item = player->m_player->m_playerVisibleItems[index];
    lua_pushnumber(L, item.m_itemId);
    lua_pushnumber(L, static_cast<int32_t>(item.m_enchantment[0] | (static_cast<uint32_t>(item.m_enchantment[1]) << 16)));
    return 2;
}

static int32_t Script_GetSpellBaseCooldown(lua_State* L) {
    if (lua_type(L, 1) != LUA_TNUMBER) {
        luaL_error(L, "Usage: GetSpellBaseCooldown(spellID)");
        return 0;
    }

    int32_t spellId = static_cast<int32_t>(lua_tonumber(L, 1));
    SpellRec spell;

    if (!g_spellDB.GetRecord(spellId, &spell)) {
        return 0;
    }

    int32_t cooldown = spell.m_recoveryTime ? spell.m_recoveryTime : spell.m_categoryRecoveryTime;
    int32_t globalCooldown = spell.m_startRecoveryTime;

    if (!cooldown) {
        // spells that only trigger another (e.g. most "use: cast X") take the triggered spell's cooldown
        for (uint32_t i = 0; i < 3; i++) {
            int32_t triggered = spell.m_effectTriggerSpell[i];
            SpellRec triggeredSpell;

            if (!triggered || triggered == spellId || !g_spellDB.GetRecord(triggered, &triggeredSpell)) {
                continue;
            }

            int32_t triggeredCooldown = triggeredSpell.m_recoveryTime ? triggeredSpell.m_recoveryTime : triggeredSpell.m_categoryRecoveryTime;

            if (triggeredCooldown > cooldown) {
                cooldown = triggeredCooldown;
            }

            if (triggeredSpell.m_startRecoveryTime > globalCooldown) {
                globalCooldown = triggeredSpell.m_startRecoveryTime;
            }
        }
    }

    lua_pushnumber(L, cooldown);
    lua_pushnumber(L, globalCooldown);
    return 2;
}

void AwesomeItemRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetItemInfoInstant", &Script_GetItemInfoInstant },
        { "GetInventoryItemTransmog", &Script_GetInventoryItemTransmog },
        { "GetSpellBaseCooldown", &Script_GetSpellBaseCooldown },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}
