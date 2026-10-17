#if defined(WOWDECOMP_ARCH_64)

#include <cstdint>
#include <malloc.h>
#include <windows.h>
#include "Engine/Source/Base/ConvertUTF.h"
#include "Engine/Source/Os/OsClipboard.h"
#include "Engine/Source/Storm/H/storm.h"

// OFFSET: 0x008726F0
// MODIFIED: reads the clipboard as UTF-16 (CF_UNICODETEXT) instead of the ANSI code page, so text outside it no
// longer pastes as "???" (awesome_wotlk's clipboard fix)
char* IOsClipboardGetString(void* window) {
    if (!OpenClipboard(static_cast<HWND>(window))) {
        return nullptr;
    }

    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        CloseClipboard();
        return nullptr;
    }

    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
    if (!text) {
        CloseClipboard();
        return nullptr;
    }

    char* string = nullptr;
    int32_t size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

    if (size > 0) {
        string = static_cast<char*>(SMemAlloc(size, __FILE__, __LINE__, 0));
        WideCharToMultiByte(CP_UTF8, 0, text, -1, string, size, nullptr, nullptr);
    }

    GlobalUnlock(data);
    CloseClipboard();
    return string;
}

// OFFSET: 0x008727C0
void OsClipboardFreeString(char* string) {
    if (string) {
        SMemFree(string, __FILE__, __LINE__, 0);
    }
}

// OFFSET: 0x008727E0
// MODIFIED: writes UTF-16 (CF_UNICODETEXT) instead of the ANSI code page (awesome_wotlk's clipboard fix)
int32_t IOsClipboardPutString(const char* string, void* window) {
    int32_t length = MultiByteToWideChar(CP_UTF8, 0, string, -1, nullptr, 0);
    if (length <= 0) {
        return 0;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_DDESHARE, length * sizeof(wchar_t));
    if (!memory) {
        return 0;
    }

    wchar_t* data = static_cast<wchar_t*>(GlobalLock(memory));
    if (!data) {
        GlobalFree(memory);
        return 0;
    }

    MultiByteToWideChar(CP_UTF8, 0, string, -1, data, length);
    GlobalUnlock(memory);

    if (!OpenClipboard(static_cast<HWND>(window))) {
        GlobalFree(memory);
        return 0;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        GlobalFree(memory);
        return 0;
    }

    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    return 1;
}

#endif
