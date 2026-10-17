#ifndef ENGINE_SOURCE_SOUNDENGINE_SOUNDTRACE_H
#define ENGINE_SOURCE_SOUNDENGINE_SOUNDTRACE_H

// MODIFIED: diagnostics. With WOWDECOMP_SOUND_TRACE=ON the sound engine logs every sound's life (create, file open
// and read, non-blocking load, play, stop, virtual kill) to Logs\SoundTrace.log, from any thread, with the thread id
// and milliseconds since start. Off by default: SOUND_TRACE expands to nothing.

#if defined(WOWDECOMP_SOUND_TRACE)
void SoundTraceWrite(const char* format, ...);
#define SOUND_TRACE(...) SoundTraceWrite(__VA_ARGS__)
#else
#define SOUND_TRACE(...) ((void)0)
#endif

#endif
