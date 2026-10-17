#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <cmath>
#include "Engine/Source/Model2/M2Model.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapBaseObj.h"
#include "WoW/Source/WorldClient/MapLoad.h"
#include "WoW/Source/WorldClient/World.h"

// cameraFov: the world camera's field of view, 100 = the engine's pi / 2
// showPlayer: 0 hides the player's own model
// cameraIndirectVisibility: a doodad between the character and the camera fades to cameraIndirectAlpha instead of
// pulling the camera in

static void OnCameraFov(AwesomeCVar* cvar, bool changed);
static void OnShowPlayer(AwesomeCVar* cvar, bool changed);
static void OnCameraIndirectVisibility(AwesomeCVar* cvar, bool changed);

static AwesomeCVar s_cvShowPlayer = AwesomeCVar::Int("showPlayer", 1, 0, 1, OnShowPlayer);
static AwesomeCVar s_cvCameraFov = AwesomeCVar::Float("cameraFov", 100.0f, 90.0f, 150.0f, OnCameraFov);
static AwesomeCVar s_cvCameraIndirectAlpha = AwesomeCVar::Float("cameraIndirectAlpha", 0.6f, 0.6f, 1.0f);
static AwesomeCVar s_cvCameraIndirectVisibility = AwesomeCVar::Int("cameraIndirectVisibility", 0, 0, 1, OnCameraIndirectVisibility);

// the models the boom faded, linked through CM2Model::m_cameraFadePrev/Next (a model unlinks itself when destroyed)
static CM2Model* s_fadeList;
// how far the boom reached on the last frame that did not end on a faded model
static float s_actualHitDistance = 1.0f;

float AwesomeCameraFieldOfView() {
    return 3.14159265f / 200.0f * s_cvCameraFov.m_floatValue;
}

static void FadeStep(CM2Model* model, float target) {
    model->m_baseAlpha += (target - model->m_baseAlpha) * 0.25f;
}

static bool FadeRestore(CM2Model* model, bool step) {
    if (step) {
        FadeStep(model, model->m_cameraFadeAlpha);

        if (std::fabs(model->m_baseAlpha - model->m_cameraFadeAlpha) >= 0.01f) {
            return false;
        }
    }

    model->m_baseAlpha = model->m_cameraFadeAlpha;
    return true;
}

static void FadePush(CM2Model* model) {
    model->m_cameraFadePrev = &s_fadeList;
    model->m_cameraFadeNext = s_fadeList;

    if (s_fadeList) {
        s_fadeList->m_cameraFadePrev = &model->m_cameraFadeNext;
    }

    s_fadeList = model;
    model->m_cameraFadeAlpha = model->m_baseAlpha;
    model->m_cameraFadePhase = CAMERA_FADE_FADING;
}

static void FadeUnlink(CM2Model* model) {
    *model->m_cameraFadePrev = model->m_cameraFadeNext;

    if (model->m_cameraFadeNext) {
        model->m_cameraFadeNext->m_cameraFadePrev = model->m_cameraFadePrev;
    }

    model->m_cameraFadePrev = nullptr;
    model->m_cameraFadeNext = nullptr;
    model->m_cameraFadePhase = CAMERA_FADE_IDLE;
}

static void FadeClear() {
    while (s_fadeList) {
        CM2Model* model = s_fadeList;
        FadeRestore(model, false);
        FadeUnlink(model);
        model->m_cameraFadeGrace = 0;
    }

    s_actualHitDistance = 1.0f;
}

// the root model of the M2 doodad the last World::Intersect ended on
static CM2Model* HitDoodadModel() {
    CMapBaseObj* owner = CMap::s_lastHitModelOwner;

    if (!owner || !(owner->m_type & (0x20 | 0x40))) {
        return nullptr;
    }

    CM2Model* model = static_cast<CMapStaticEntity*>(owner)->m_model;

    while (model && model->m_attachParent) {
        model = model->m_attachParent;
    }

    return model;
}

bool AwesomeCameraIntersect(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* distance, uint32_t& flags) {
    if (!s_cvCameraIndirectVisibility.m_intValue) {
        return World::Intersect(start, end, hit, distance, flags, nullptr) != 0;
    }

    uint32_t boomFlags = flags | 0x1 | 0x8000000;
    flags &= ~0x1u;

    bool result = World::Intersect(start, end, hit, distance, boomFlags, nullptr) != 0;
    CM2Model* hitModel = nullptr;

    if (result) {
        hitModel = HitDoodadModel();

        if (hitModel) {
            if (hitModel->m_cameraFadePhase == CAMERA_FADE_IDLE) {
                FadePush(hitModel);
            }

            FadeStep(hitModel, s_cvCameraIndirectAlpha.m_floatValue);
            hitModel->m_cameraFadeGrace = 0;

            // the boom keeps the reach it had before it met the faded model
            if (s_actualHitDistance < 1.0f) {
                *distance = s_actualHitDistance;
            } else {
                result = false;
            }
        } else {
            s_actualHitDistance = *distance;
        }
    } else if (!s_fadeList) {
        return result;
    } else {
        s_actualHitDistance = 1.0f;
    }

    uint32_t count = 0;

    for (CM2Model* model = s_fadeList; model; model = model->m_cameraFadeNext) {
        count++;
    }

    // a model the boom stopped meeting fades back in once it has been missed for longer than the list is long
    uint32_t cleanup = count + 1;

    for (CM2Model* model = s_fadeList; model;) {
        CM2Model* next = model->m_cameraFadeNext;

        if (model->m_cameraFadeGrace > cleanup && FadeRestore(model, true)) {
            FadeUnlink(model);
        } else {
            if (!hitModel) {
                model->m_cameraFadePhase = CAMERA_FADE_FADING;
            } else if (model == hitModel) {
                model->m_cameraFadePhase = CAMERA_FADE_CURRENT;
            }

            model->m_cameraFadeGrace++;
        }

        model = next;
    }

    return result;
}

static void OnCameraIndirectVisibility(AwesomeCVar* cvar, bool changed) {
    if (!cvar->m_intValue) {
        FadeClear();
    }
}

static void OnCameraFov(AwesomeCVar* cvar, bool changed) {
    CGCamera* camera = CGWorldFrame::GetActiveCamera();

    if (camera) {
        camera->m_fov = AwesomeCameraFieldOfView();
    }
}

static void OnShowPlayer(AwesomeCVar* cvar, bool changed) {
    CGPlayer_C* player = static_cast<CGPlayer_C*>(AwesomeObjectPtr(ClntObjMgrGetActivePlayer(), TYPEMASK_PLAYER));

    if (player) {
        player->m_scale = cvar->m_intValue ? 1.0f : 0.0f;
    }
}

void AwesomeCameraRegisterCVars() {
    s_cvShowPlayer.Register();
    s_cvCameraFov.Register();
    s_cvCameraIndirectAlpha.Register();
    s_cvCameraIndirectVisibility.Register();
}

void AwesomeCameraOnEnterWorld() {
    s_cvShowPlayer.Refresh();
}

void AwesomeCameraOnLeaveWorld() {
    s_cvShowPlayer.Refresh();
}
