#ifndef WOW_SOURCE_AWESOME_AWESOME_H
#define WOW_SOURCE_AWESOME_AWESOME_H

// Client features ported from awesome_wotlk (https://github.com/noname08662/awesome_wotlk, v38). The original is a
// DLL that detours the 32-bit Wow.exe at runtime; here every hook is a call from the function it detoured. These are
// the entry points the engine calls into, each named after the hook it replaces.

#include <cstddef>
#include <cstdint>
#include "WoW/Source/Object/WGUID.h"

class C2Vector;
struct FMOD_REVERB_PROPERTIES;
class C3Vector;
class CGGameObject_C;
class CGNamePlateFrame;
class CGObject_C;
class CGUnit_C;
class CGWorldFrame;
struct CVar;

// Version reported to addons as the AwesomeWotlk global, matching the upstream release this port follows
#define AWESOME_WOTLK_VERSION 38

// Client.cpp, right after CVar::ArchiveCodeRegisteredOnly: registers every CVar the features own
void AwesomeRegisterCVars();

// LoadScriptFunctions, after the last engine library: the Lua API the features add to the game UI
void AwesomeRegisterScriptFunctions();

// CGGameUI::EnterWorld / LeaveWorld, before the engine's own handling
void AwesomeOnEnterWorld();
void AwesomeOnLeaveWorld();

// CSimpleTop's PaintScreen, once per rendered frame before the UI updates (glue and game)
void AwesomeOnPaint();

// CGlueMgr::Resume after GlueXML loaded, and CCharacterSelection::UpdateCharacterList after it signalled the list
void AwesomeOnGlueXMLLoaded();
void AwesomeOnCharacterListUpdated();

// Script_GetGUIDFromToken: resolves the unit tokens the features add ("nameplateN"), advancing token past the match
bool AwesomeGetGUIDFromToken(const char*& token, WOWGUID& guid);

// Script_GetTokenIDsFromGUID: appends those tokens for guid while count < capacity
void AwesomeGetTokenIDsFromGUID(const WOWGUID& guid, char** tokens, int32_t* count, int32_t capacity, uint32_t tokenSize);

// CGCamera's constructor: the world camera's field of view (cameraFov CVar)
float AwesomeCameraFieldOfView();
// CGCamera::CollideCameraWithWorld's boom ray, in place of World::Intersect. With cameraIndirectVisibility on, the
// doodad the boom hits fades to cameraIndirectAlpha and the ray passes through it; flags then loses bit 0 (doodad
// collision) for the near-plane probes that follow
bool AwesomeCameraIntersect(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* distance, uint32_t& flags);

// Script_SecureCmdOptionParse: true when the parsed target is [@cursor] or [@playerlocation], which the macro places
// itself; the parse then returns no unit target
bool AwesomeMacroConsumeTarget(const char* start, const char* end);
// CGWorldFrame::OnLayerTrackTerrain, before the engine: places a pending [@cursor] / [@playerlocation] AoE
void AwesomeOnTrackTerrain(const C3Vector& point);
// Spell_C_DestroySpellWorldObject: the AoE targeting ended
void AwesomeOnCancelAoeTargeting();

// Nameplates (C_NamePlate, stacking, clamping, mouse freeze, occlusion fading, nameplateN tokens):
// CGUnit_C::UnitNameplatesReposition is replaced by this per-frame layout pass
void AwesomeNamePlatesUpdate(CGWorldFrame* worldFrame);
// GetNamePlateScreenPos replaced: placement offset, half off-screen plates kept, edge clamping
bool AwesomeNamePlateScreenPosition(CGUnit_C* unit, CGWorldFrame* worldFrame, C3Vector* screen);
// CGUnit_C::SetNamePlateFocus replaced: click-through modes, frozen plates keep the cursor
void AwesomeNamePlateSetFocus(const C2Vector& position);
// CGNamePlateFrame::OnLayerUpdate: the plate's alpha (non-target alpha, occlusion, fading)
uint8_t AwesomeNamePlateAlpha(CGNamePlateFrame* plate, CGUnit_C* unit);
// how far nameplates show and tab targeting reaches, squared (nameplateDistance CVar, 41 yards by default)
float AwesomeNamePlateDistanceSquared();
// CGUnit_C::UpdateReaction, after the engine
void AwesomeNamePlateOnReactionChanged(CGUnit_C* unit);
// a target, focus or binding mode change: lay the plates out again next frame
void AwesomeNamePlatesMarkDirty();
// CGUnit_C::NamePlateClearAll / PostShutdown: the plates are about to be released
void AwesomeNamePlatesClearAll();

// HD client parity (AwesomeHdClient.cpp): CVars for the patched Wow.exe's behaviour changes, defaulting to its behaviour.
// CGUnit_C::UpdateSpellCastBars / CGGameUI::Target / ClearTarget: cast bars on every nameplate (nameplateAllCastBars)
bool AwesomeNamePlateAllCastBars();
// CGUnit_C::SetMaxAlpha: the camera may fade the character it is zoomed in on (cameraPlayerFade, off by default)
bool AwesomeCameraPlayerFade();
// Script_SendChatMessage: say, yell and emote allowed while dead (chatWhileDead)
bool AwesomeChatWhileDead();

// Audio engine upgrade (AwesomeAudio.cpp, not from awesome_wotlk). SI2::SetListenerAttributes, after the engine placed
// the listener each world frame: per-source occlusion and the indoor room estimate
void AwesomeAudioUpdate(const C3Vector& listener);
// SI2::ApplyReverbSettings when the DBC gives the place no reverb: a fallback (underwater, or the estimated room)
bool AwesomeAudioFallbackReverb(FMOD_REVERB_PROPERTIES* properties);

// CGGameObject_C::CheckForPassiveHighlight, after the engine (objectHighlightMode CVar)
void AwesomeOnPassiveHighlight(CGGameObject_C* object);
// CGObject_C::HideHighlightType, after the engine (interaction highlight)
void AwesomeOnHideHighlight(CGObject_C* object, int32_t type);

#endif
