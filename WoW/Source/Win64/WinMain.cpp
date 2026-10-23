#if defined(WOWDECOMP_ARCH_64)

#include <windows.h>
#include <cstdlib>
#include <float.h>
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

// STATIC: 0x00B311E8
static LPVOID lpFiber;

static void InvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved) {
}

static bool ReturnFalse() {
    return false;
}

extern "C" int WinMainCRTStartup();

// MODIFIED: per-monitor DPI awareness (v2), before any window exists. Unaware, Windows reports a scaled-down desktop
// at display scaling above 100% and stretches a smaller frame over it: a 4K monitor at 150% got a 2560x1440 picture.
// Windows 10 1703+ takes the v2 context; older versions fall back to per-monitor, then system awareness. A
// compatibility setting on the exe (Properties > Compatibility > high DPI) overrides this
static void SetDpiAwareness() {
    HMODULE user32 = GetModuleHandleA("user32.dll");

    typedef BOOL(WINAPI * SetContextProc)(HANDLE);
    SetContextProc setContext = user32 ? reinterpret_cast<SetContextProc>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")) : nullptr;

    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    if (setContext && setContext(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)))) {
        return;
    }

    HMODULE shcore = LoadLibraryA("shcore.dll");

    if (shcore) {
        typedef HRESULT(WINAPI * SetAwarenessProc)(int);
        SetAwarenessProc setAwareness = reinterpret_cast<SetAwarenessProc>(GetProcAddress(shcore, "SetProcessDpiAwareness"));

        // PROCESS_PER_MONITOR_DPI_AWARE; E_ACCESSDENIED means it is already set, by a manifest or compatibility setting
        if (setAwareness) {
            HRESULT hr = setAwareness(2);

            if (SUCCEEDED(hr) || hr == E_ACCESSDENIED) {
                return;
            }
        }
    }

    SetProcessDPIAware();
}

// OFFSET: 0x00401000
extern "C" int start() {
    SetDpiAwareness();
    StormRtlInitialize();
    return WinMainCRTStartup();
}

// OFFSET: 0x00406D70
int main() {
    _set_invalid_parameter_handler(InvalidParameterHandler);
    _clearfp();
    _control87(0x9001F & ~(_MCW_PC | _MCW_IC), _MCW_DN | _MCW_EM | _MCW_RC);
    ReturnFalse();

    return CommonMain();
}

// OFFSET: 0x0040B7D0
static VOID CALLBACK MainFiberProc(LPVOID parameter) {
    *static_cast<int*>(parameter) = main();
    SwitchToFiber(lpFiber);
}

// OFFSET: 0x0040B7F0
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    lpFiber = ConvertThreadToFiber(nullptr);

    if (!lpFiber) {
        return -1;
    }

    int result = -1;
    LPVOID fiber = CreateFiberEx(0, 0x800000, 0, MainFiberProc, &result);

    if (!fiber) {
        return -1;
    }

    SwitchToFiber(fiber);
    DeleteFiber(fiber);

    return result;
}

#endif
