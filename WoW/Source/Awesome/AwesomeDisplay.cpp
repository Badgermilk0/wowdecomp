#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <cmath>
#include <cstdio>
#include <windows.h>
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Frame/CLayoutFrame.h"
#include "Engine/Source/Frame/CSimpleFrame.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/Frame/Types.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxFormat.h"
#include "Engine/Source/Gxu/GxuFont.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/Tempest/CiRect.h"
#include "Engine/Source/Tempest/CRect.h"
#include "WoW/Source/ConsoleDevice/ConsoleDevice.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/Screen.h"

// Display management: the window, its monitor and the UI's scale on it.
// uiLiveResize: a new window aspect lays the UI out again instead of reloading it (fixed-height coordinates,
// Engine/Source/Base/Coordinate.cpp); read once at startup
// uiScaleDpi: the UI scale follows the monitor's DPI; uiScale (or 1 with useUiScale off) then means "as at 1080p and
// 100% Windows scaling", so the UI keeps its size on screen across resolutions and monitors
// uiMaxAspect: on a wider screen the UI is kept in a centred area of this aspect (0 = the whole screen); the world
// still fills the screen
// Alt+Enter switches between a window and full screen; the window's place is remembered (gxWindowPos, gxMonitorName,
// ConsoleDevice_CVar.cpp) and gxMonitor moves a full-screen window when it changes

static void OnUiScaleDpi(AwesomeCVar* cvar, bool changed);
static void OnUiMaxAspect(AwesomeCVar* cvar, bool changed);
static void OnUiLiveResize(AwesomeCVar* cvar, bool changed);

static AwesomeCVar s_cvUiLiveResize = AwesomeCVar::Int("uiLiveResize", 1, 0, 1, OnUiLiveResize);
static AwesomeCVar s_cvUiScaleDpi = AwesomeCVar::Int("uiScaleDpi", 0, 0, 1, OnUiScaleDpi);
static AwesomeCVar s_cvUiMaxAspect = AwesomeCVar::Float("uiMaxAspect", 0.0f, 0.0f, 4.0f, OnUiMaxAspect);

// stock clamps uiScale to 0.64 or more: 1200 UI units of height at most, about 1.8 pixels each at 2160p. 0.1 leaves
// room for a pixel-exact UI (768 / 2160 = 0.36) even at 8K
static const float UI_SCALE_MIN = 0.1f;
// uiScaleDpi's reference: uiScale 1 is the stock size at 1080p and 96 DPI
static const float UI_SCALE_DPI_HEIGHT = 1080.0f;
static const DWORD POLL_INTERVAL_MS = 250;

static bool s_boundsApplied;
static uint32_t s_lastDpi;
static int32_t s_lastMonitorIndex = -1;
static HWND s_lastWindow;
static RECT s_lastWindowRect;
static RECT s_lastClientRect;
static DWORD s_lastPoll;

static HWND DeviceWindow() {
    return g_theGxDevicePtr ? static_cast<HWND>(g_theGxDevicePtr->DeviceWindow()) : nullptr;
}

// Windows 10 1607+ GetDpiForWindow, else the system DPI
static uint32_t WindowDpi(HWND hwnd) {
    typedef UINT(WINAPI * GetDpiForWindowProc)(HWND);
    static GetDpiForWindowProc s_getDpiForWindow = reinterpret_cast<GetDpiForWindowProc>(GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow"));

    uint32_t dpi = 0;

    if (hwnd && s_getDpiForWindow) {
        dpi = s_getDpiForWindow(hwnd);
    }

    if (!dpi) {
        HDC dc = GetDC(nullptr);
        dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;

        if (dc) {
            ReleaseDC(nullptr, dc);
        }
    }

    return dpi ? dpi : 96;
}

static float WindowHeight() {
    if (!g_theGxDevicePtr) {
        return 0.0f;
    }

    CRect rect = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_theGxDevicePtr->CapsWindowSize(rect);
    return rect.maxY - rect.minY;
}

static bool IsNormalWindow() {
    return g_theGxDevicePtr && g_theGxDevicePtr->m_format.m_window && !g_theGxDevicePtr->m_format.m_maximize;
}

float AwesomeDisplayMinUiScale() {
    return UI_SCALE_MIN;
}

float AwesomeDisplayUiScale(float scale) {
    if (s_cvUiScaleDpi.m_intValue) {
        float height = WindowHeight();

        if (height >= 1.0f) {
            scale *= WindowDpi(DeviceWindow()) / 96.0f * UI_SCALE_DPI_HEIGHT / height;
        }

        // a UI wider than the screen, as stock caps it below 4:3
        float aspect = CoordinateGetAspectRatio();

        if (aspect * 3.0f < 4.0f && scale > aspect * 0.75f) {
            scale = aspect * 0.75f;
        }
    }

    return scale > UI_SCALE_MIN ? scale : UI_SCALE_MIN;
}

float AwesomeDisplayDefaultUiScale(float scale) {
    return s_cvUiScaleDpi.m_intValue ? AwesomeDisplayUiScale(1.0f) : scale;
}

bool AwesomeDisplayApplyAspect(CSimpleTop* top, float aspect) {
    if (!top || !s_cvUiLiveResize.m_intValue || !CoordinateIsFixedHeight()) {
        return false;
    }

    // already applied by OnDisplayAspect for this size event
    if (std::fabs(CoordinateGetAspectRatio() - aspect) < 0.0001f) {
        return true;
    }

    float oldWidth = NDCToDDCWidth(1.0f);
    CoordinateSetAspectRatio(aspect);
    GxuFontAspectChanged(oldWidth / NDCToDDCWidth(1.0f));
    top->RelayoutForAspect();
    return true;
}

// CSimpleTop::s_displayAspectCallback: the new aspect before the frames lay out for the new size. Pixel size and
// aspect must change together: text measured with one new and one old would be cut to the wrong width
// Only the game UI: the glue screens keep stock's reload, as their GlueXML (the HD client's) places itself for the
// aspect once, when it loads, and has no event to do it again
static void OnDisplayAspect(CSimpleTop* top) {
    if (top != CGGameUI::m_simpleTop) {
        return;
    }

    float aspect = Screen::GetAspectRatio();

    if (std::fabs(CoordinateGetAspectRatio() - aspect) >= 0.0001f) {
        AwesomeDisplayApplyAspect(top, aspect);
    }
}

void AwesomeDisplayOnUiScaled() {
    CSimpleFrame* parent = CGGameUI::m_UISimpleParent;
    CSimpleTop* top = CGGameUI::m_simpleTop;

    if (!parent || !top || parent->m_layoutScale <= 0.0f) {
        return;
    }

    float maxAspect = s_cvUiMaxAspect.m_floatValue;
    float inset = 0.0f;

    if (maxAspect >= 1.0f && CoordinateGetAspectRatio() > maxAspect) {
        inset = (NDCToDDCWidth(1.0f) - NDCToDDCHeight(1.0f) * maxAspect) * 0.5f;
    }

    // UIParent stays as FrameXML anchored it until the area is first narrowed
    if (inset <= 0.0f && !s_boundsApplied) {
        return;
    }

    s_boundsApplied = inset > 0.0f;

    // point offsets are in the frame's own scale (CFramePoint::X)
    float offset = inset / parent->m_layoutScale;
    parent->SetPoint(FRAMEPOINT_TOPLEFT, top, FRAMEPOINT_TOPLEFT, offset, 0.0f, 1);
    parent->SetPoint(FRAMEPOINT_BOTTOMRIGHT, top, FRAMEPOINT_BOTTOMRIGHT, -offset, 0.0f, 1);
    CLayoutFrame::ResizePending();
}

// the UI scale again, as if useUiScale were set: after a DPI or uiScaleDpi change
static void ReapplyUiScale() {
    if (!CGGameUI::m_UISimpleParent || !g_useUiScaleCVar) {
        return;
    }

    CVarHandler_useUiScale(g_useUiScaleCVar, nullptr, g_useUiScaleCVar->m_stringValue.m_str ? g_useUiScaleCVar->m_stringValue.m_str : "0", nullptr);
}

static void OnUiScaleDpi(AwesomeCVar* cvar, bool changed) {
    if (changed) {
        ReapplyUiScale();
    }
}

static void OnUiMaxAspect(AwesomeCVar* cvar, bool changed) {
    if (changed) {
        AwesomeDisplayOnUiScaled();
    }
}

static void OnUiLiveResize(AwesomeCVar* cvar, bool changed) {
    // takes effect only before the first UI exists (CoordinateSetFixedHeight ignores later calls)
    CoordinateSetFixedHeight(cvar->m_intValue != 0);
}

// The window's place, every POLL_INTERVAL_MS: a moved or resized window is remembered for the next session and the
// next switch back from full screen, the monitor it is on in every mode
static void RememberWindow(HWND hwnd) {
    if (IsIconic(hwnd)) {
        return;
    }

    int32_t index = CGxDevice::MonitorOfWindow(hwnd);
    char name[32];

    if (index && CGxDevice::MonitorRects(index, nullptr, nullptr, name, sizeof(name))) {
        ConsoleDeviceRememberMonitor(name);
    }

    RECT window;
    RECT client;

    if (!GetWindowRect(hwnd, &window) || !GetClientRect(hwnd, &client)) {
        return;
    }

    // a new window (a gxRestart) is only a baseline: a pending gxResolution must not be overwritten by the old size
    bool moved = hwnd == s_lastWindow && (window.left != s_lastWindowRect.left || window.top != s_lastWindowRect.top);
    bool resized = hwnd == s_lastWindow && (client.right != s_lastClientRect.right || client.bottom != s_lastClientRect.bottom);
    s_lastWindow = hwnd;
    s_lastWindowRect = window;
    s_lastClientRect = client;

    if ((moved || resized) && IsNormalWindow() && !IsZoomed(hwnd)) {
        ConsoleDeviceRememberWindow(window.left, window.top, client.right, client.bottom, resized);
    }
}

void AwesomeOnIdle() {
    if (OsInputConsumeFullscreenToggle() && g_theGxDevicePtr) {
        ConsoleDeviceToggleFullscreen();
        return;
    }

    DWORD now = GetTickCount();

    if (now - s_lastPoll < POLL_INTERVAL_MS) {
        return;
    }

    s_lastPoll = now;
    HWND hwnd = DeviceWindow();

    if (!hwnd) {
        return;
    }

    RememberWindow(hwnd);

    // gxMonitor set from the console or an addon moves a full-screen window there at once
    if (s_lastMonitorIndex >= 0 && CGxDevice::s_monitorIndex != s_lastMonitorIndex && !IsNormalWindow()) {
        s_lastMonitorIndex = CGxDevice::s_monitorIndex;
        CGxDevice::s_lastWindowRectValid = 0;
        CCGxRestart(nullptr, nullptr);
        return;
    }

    s_lastMonitorIndex = CGxDevice::s_monitorIndex;

    // moved to a monitor with another scale (WM_DPICHANGED resized the window; the size event rescaled the UI unless
    // the size stayed the same)
    uint32_t dpi = WindowDpi(hwnd);

    if (dpi != s_lastDpi) {
        bool first = s_lastDpi == 0;
        s_lastDpi = dpi;

        if (!first && s_cvUiScaleDpi.m_intValue) {
            ReapplyUiScale();
        }
    }
}

// GetMonitorCount(): the connected monitors
static int Script_GetMonitorCount(lua_State* L) {
    lua_pushnumber(L, CGxDevice::MonitorCount());
    return 1;
}

// GetMonitorName(index): "1: 3840x2160 (primary)", width, height, isPrimary, device name; nothing for a bad index.
// The index is gxMonitor's
static int Script_GetMonitorName(lua_State* L) {
    int32_t index = static_cast<int32_t>(luaL_checknumber(L, 1));
    CiRect monitor;
    CiRect work;
    char device[32];

    if (!CGxDevice::MonitorRects(index, &monitor, &work, device, sizeof(device))) {
        return 0;
    }

    int32_t width = monitor.maxX - monitor.minX;
    int32_t height = monitor.maxY - monitor.minY;
    bool primary = monitor.minX == 0 && monitor.minY == 0;
    char name[64];
    snprintf(name, sizeof(name), "%d: %dx%d%s", index, width, height, primary ? " (primary)" : "");

    lua_pushstring(L, name);
    lua_pushnumber(L, width);
    lua_pushnumber(L, height);
    lua_pushboolean(L, primary);
    lua_pushstring(L, device);
    return 5;
}

// GetCurrentMonitor(): the index of the monitor the game window is on
static int Script_GetCurrentMonitor(lua_State* L) {
    HWND hwnd = DeviceWindow();
    lua_pushnumber(L, hwnd ? CGxDevice::MonitorOfWindow(hwnd) : 0);
    return 1;
}

// GetDisplayScale(): the game window's monitor's DPI scale (1 = 100%) and the UI scale in effect
static int Script_GetDisplayScale(lua_State* L) {
    lua_pushnumber(L, WindowDpi(DeviceWindow()) / 96.0f);
    lua_pushnumber(L, CGGameUI::m_UISimpleParent ? CGGameUI::m_UISimpleParent->m_layoutScale : 1.0f);
    return 2;
}

void AwesomeDisplayRegisterCVars() {
    CSimpleTop::s_displayAspectCallback = OnDisplayAspect;
    s_cvUiLiveResize.Register();
    // the coordinates are chosen before any UI exists, also when the value is the default
    s_cvUiLiveResize.Refresh();
    s_cvUiScaleDpi.Register();
    s_cvUiMaxAspect.Register();
}

void AwesomeDisplayRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "GetMonitorCount", Script_GetMonitorCount },
        { "GetMonitorName", Script_GetMonitorName },
        { "GetCurrentMonitor", Script_GetCurrentMonitor },
        { "GetDisplayScale", Script_GetDisplayScale },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}
