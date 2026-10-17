#include "Engine/Source/SoundEngine/SoundTrace.h"

#if defined(WOWDECOMP_SOUND_TRACE)

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <windows.h>

// MODIFIED: diagnostics, see SoundTrace.h

void SoundTraceWrite(const char* format, ...) {
    static std::mutex s_mutex;
    static FILE* s_file;
    static LARGE_INTEGER s_start;
    static LARGE_INTEGER s_frequency;

    std::lock_guard<std::mutex> lock(s_mutex);

    if (!s_file) {
        CreateDirectoryA("Logs", nullptr);
        s_file = fopen("Logs\\SoundTrace.log", "w");

        if (!s_file) {
            return;
        }

        QueryPerformanceFrequency(&s_frequency);
        QueryPerformanceCounter(&s_start);
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double ms = static_cast<double>(now.QuadPart - s_start.QuadPart) * 1000.0 / static_cast<double>(s_frequency.QuadPart);

    fprintf(s_file, "%10.1f [%5lu] ", ms, GetCurrentThreadId());

    va_list args;
    va_start(args, format);
    vfprintf(s_file, format, args);
    va_end(args);

    fputc('\n', s_file);
    fflush(s_file);
}

#endif
