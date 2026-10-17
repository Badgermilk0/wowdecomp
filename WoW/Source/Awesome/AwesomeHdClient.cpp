// HD client parity (not from awesome_wotlk): the behaviour changes the "WoW 3.3.5a HD" client's patched Wow.exe makes,
// which have a stock alternative worth keeping. Each CVar defaults to the HD client's behaviour; 0 restores stock.
// The view-distance limits it raises are in WorldParam_CVar.cpp and World.cpp; the list of every patch and where it
// went is in MODIFICATIONS.md.

#include "WoW/Source/Awesome/AwesomeInternal.h"

// cast bars on every unit's nameplate; stock shows them only on the target's (CGUnit_C::UpdateSpellCastBars)
static AwesomeCVar s_cvNamePlateAllCastBars = AwesomeCVar::Int("nameplateAllCastBars", 1, 0, 1);
// the character fading out as the camera zooms in on it (CGUnit_C::SetMaxAlpha); the HD client keeps it opaque
static AwesomeCVar s_cvCameraPlayerFade = AwesomeCVar::Int("cameraPlayerFade", 0, 0, 1);
// /say, /yell and emotes while dead or a ghost; stock refuses them with ERR_CHAT_WHILE_DEAD (Script_SendChatMessage)
static AwesomeCVar s_cvChatWhileDead = AwesomeCVar::Int("chatWhileDead", 1, 0, 1);

bool AwesomeNamePlateAllCastBars() {
    return s_cvNamePlateAllCastBars.m_intValue != 0;
}

bool AwesomeCameraPlayerFade() {
    return s_cvCameraPlayerFade.m_intValue != 0;
}

bool AwesomeChatWhileDead() {
    return s_cvChatWhileDead.m_intValue != 0;
}

void AwesomeHdClientRegisterCVars() {
    s_cvNamePlateAllCastBars.Register();
    s_cvCameraPlayerFade.Register();
    s_cvChatWhileDead.Register();
}
