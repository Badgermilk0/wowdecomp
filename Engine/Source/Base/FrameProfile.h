#ifndef ENGINE_SOURCE_BASE_FRAMEPROFILE_H
#define ENGINE_SOURCE_BASE_FRAMEPROFILE_H

// MODIFIED: performance diagnostics, multithreaded scene. Where the main thread's world work goes, per section,
// smoothed, in milliseconds (GetSceneTimings() and /frametimes in Lua). Sections add up over a frame, so a section that
// runs several times (one Animate per model scene: world, portraits, UI models) reports their sum. FrameProfileEndFrame
// runs once per Present.

#include <cstdint>

enum FrameProfileSection {
    FRAMEPROFILE_UNIT_UPDATE,   // CGUnit_C/CGGameObject_C::UpdateDisplay for every visible object (movement, effects)
    FRAMEPROFILE_WORLD_UPDATE,  // the rest of CGWorldFrame::OnWorldUpdate (camera, day/night, CWorld::Update)
    FRAMEPROFILE_CULL,          // CWorldScene::Render up to model animation: portal/frustum/occlusion culling
    FRAMEPROFILE_ANIM_PARALLEL, // CM2Model::AnimateMT, bone matrices, split across threads
    FRAMEPROFILE_ANIM_SERIAL,   // AnimateST (particles, ribbons, lights) and SetupLighting, main thread
    FRAMEPROFILE_ANIM_BATCH,    // CM2Scene::Animate's draw-list walk that builds the render elements
    FRAMEPROFILE_DRAW,          // CWorldScene::Render after animation: terrain, WMOs, models, liquid submitted
    FRAMEPROFILE_POST,          // after the world render: unit PostAnimate, nameplates
    FRAMEPROFILE_COUNT
};

enum FrameProfileCounter {
    FRAMEPROFILE_MODELS, // models animated (attached ones count with their root)
    FRAMEPROFILE_COUNTERS
};

struct FrameProfileStats {
    double m_ms[FRAMEPROFILE_COUNT];
    double m_counters[FRAMEPROFILE_COUNTERS];
};

extern FrameProfileStats g_frameProfile;

uint64_t FrameProfileNow();
void FrameProfileAdd(FrameProfileSection section, uint64_t start, uint64_t end);
void FrameProfileCount(FrameProfileCounter counter, uint32_t value);
void FrameProfileEndFrame();

// times its scope into a section
class FrameProfileScope {
    public:
    explicit FrameProfileScope(FrameProfileSection section) : m_section(section), m_start(FrameProfileNow()) {}
    ~FrameProfileScope() { FrameProfileAdd(this->m_section, this->m_start, FrameProfileNow()); }

    private:
    FrameProfileSection m_section;
    uint64_t m_start;
};

#endif
