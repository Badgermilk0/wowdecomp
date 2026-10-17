#ifndef WOW_SOURCE_AWESOME_AWESOMENAMEPLATES_H
#define WOW_SOURCE_AWESOME_AWESOMENAMEPLATES_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Object/WGUID.h"

// Index of the nameplateN token (N - 1) whose plate shows guid, -1 when it has none
int32_t AwesomeNamePlateGetTokenId(const WOWGUID& guid);
// Unit shown by token nameplate(index + 1)
WOWGUID AwesomeNamePlateGetTokenGUID(int32_t index);

void AwesomeNamePlatesRegisterCVars();
void AwesomeNamePlatesRegisterScriptFunctions(lua_State* L);

#endif
