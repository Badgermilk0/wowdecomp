#ifndef WOW_SOURCE_AWESOME_AWESOME_H
#define WOW_SOURCE_AWESOME_AWESOME_H

// Client features ported from awesome_wotlk (https://github.com/noname08662/awesome_wotlk, v38). The original is a
// DLL that detours the 32-bit Wow.exe at runtime; here every hook is a call from the function it detoured. These are
// the entry points the engine calls into, each named after the hook it replaces.

#include <cstddef>
#include <cstdint>
#include "WoW/Source/Object/WGUID.h"

namespace FMOD {
class Channel;
}

class C2Vector;
struct FMOD_REVERB_PROPERTIES;
class C3Vector;
class CGGameObject_C;
class CGNamePlateFrame;
class CGObject_C;
class CGUnit_C;
class CGWorldFrame;
class CSimpleTop;
class CStatus;
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

// AwesomeLighting.cpp, modern lighting on the D3D11 device (Engine/Source/Gx/GxLighting.h): CGWorldFrame::OnWorldRender
// before CWorld::Render; CWorldScene::Render before the terrain, once the camera, the models' lights and the shadows are
// ready; OnWorldRender after the opaque models and the grass, before liquids and the transparent passes; and before
// FFX::Finish (also on its early return)
void AwesomeLightingBeginWorld();
void AwesomeLightingSetupView(const C3Vector& camera);
void AwesomeLightingOpaqueDone();
void AwesomeLightingEndWorld();
// CSimpleModelFFX::Render around the login screen's model, for lightingGlueTest (off by default)
void AwesomeLightingGlueBegin();
void AwesomeLightingGlueEnd();

// AwesomeWeather.cpp, the weather overhaul (WorldClient/WeatherModel.h holds the arithmetic).
// CGGameUI::MessageHandler SMSG_WEATHER: the stock fields and the bytes after them (an optional extension block)
void AwesomeWeatherOnPacket(uint32_t weatherId, float grade, uint8_t abrupt, const uint8_t* extra, uint32_t extraSize);
// Weather::SetStormIntensity: the particle intensity 0..1 for the displayed grade (stock (grade - 0.25) / 0.75)
float AwesomeWeatherParticleIntensity(float grade);
// the grade at and below which nothing falls (weatherParticleStart, stock 0.25)
float AwesomeWeatherParticleStart();
// Weather::SetType: the fog target (storm sky / 4, 0..0.25) for a grade; *stock is set false when it differs from
// the stock min(grade, 0.25)
float AwesomeWeatherSkyTarget(uint32_t type, float grade, bool* stock);
// Weather::SetStormIntensity: seconds a change of delta takes (stock |delta| * 10)
float AwesomeWeatherTransitionSeconds(float delta);
// Weather::SetType: weatherFixes (the sandstorm restart and the sticky rain texture)
bool AwesomeWeatherFixes();
// Weather_RefreshDensity: weatherDensityScale times the server's density
float AwesomeWeatherDensityScale();
// end of Weather::Update, once per world frame: lightning, thunder, the weather sound layer
void AwesomeWeatherUpdate();
// Weather::Reset (world load, teleport, logout)
void AwesomeWeatherOnReset();
// SI2::DSP_AttachCustomDspChain for a chain SoundFilter.dbc does not have: the thunder layers ("AwesomeThunder...")
// get their filter chain and pitch here, as their channel is set up
void AwesomeWeatherAttachDspChain(FMOD::Channel* channel, const char* name);
// SI2::DSP_ShutDown: release the thunder chains with the sound system
void AwesomeWeatherSoundShutdown();

// AwesomeEmbeddedUi.cpp, the AwesomeCVar settings window and the Flash helper built into the client:
// CGGameUI::InitializeFrameXML, after the FrameXML files and before LoadAddOns, runs them as FrameXML
void AwesomeLoadBuiltInAddOns(CStatus* status);
// LoadAddOnInfo: true for an AddOn the client has built in (and awesomeBuiltInAddOns on), which is then not listed or
// loaded from Interface\AddOns
bool AwesomeIsBuiltInAddOn(const char* name);

// AwesomeDisplay.cpp, display management. CGGameUI::Idle and CGlueMgr::Idle, once per frame outside the frame's
// rendering: Alt+Enter, the remembered window, DPI and gxMonitor changes
void AwesomeOnIdle();
// CGGameUI::SetUIScale: the scale UIParent gets for a requested uiScale (floor AwesomeDisplayMinUiScale, uiScaleDpi)
float AwesomeDisplayUiScale(float scale);
float AwesomeDisplayMinUiScale();
// CGGameUI::ScaleToScreenSize (useUiScale off): the stock automatic scale, or the DPI-based one with uiScaleDpi
float AwesomeDisplayDefaultUiScale(float scale);
// after UIParent's scale or the aspect changed: uiMaxAspect's centred area
void AwesomeDisplayOnUiScaled();
// CGGameUI on a new window aspect: lays top's frames out for it and returns true (uiLiveResize), or returns false and
// the caller reloads the UI as stock did. The glue screens always reload
bool AwesomeDisplayApplyAspect(CSimpleTop* top, float aspect);

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
