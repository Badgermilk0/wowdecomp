#include "Engine/Source/Base/ParallelFor.h"
#include <atomic>
#include <windows.h>
#include <immintrin.h>
#include "Engine/Source/Base/Prop.h"

// MODIFIED: performance, multithreaded scene; see ParallelFor.h

namespace {

const uint32_t MAX_THREADS = 16;
// auto: one thread per logical processor less one (the driver and the sound and file threads need somewhere to run),
// at most this many. A frame's model work stops scaling well before 16
const uint32_t AUTO_MAX_THREADS = 8;

struct Job {
    ParallelForFunc m_func;
    void* m_context;
    uint32_t m_count;
    uint32_t m_grain;
    HPROPCONTEXT m_prop;
    uint32_t m_csr;
    std::atomic<uint32_t> m_next;
};

Job s_job;
uint32_t s_requested;
uint32_t s_workerCount;
HANDLE s_start;
HANDLE s_done;
std::atomic<int32_t> s_pending;
std::atomic<bool> s_busy;
thread_local bool t_inJob;

void RunJob(Job& job) {
    t_inJob = true;

    for (;;) {
        uint32_t begin = job.m_next.fetch_add(job.m_grain, std::memory_order_relaxed);

        if (begin >= job.m_count) {
            break;
        }

        uint32_t end = job.m_count - begin < job.m_grain ? job.m_count : begin + job.m_grain;
        job.m_func(job.m_context, begin, end);
    }

    t_inJob = false;
}

DWORD WINAPI WorkerProc(void*) {
    for (;;) {
        WaitForSingleObject(s_start, INFINITE);

        // the caller's thread state: Prop (the Gx device and friends live there) and the SSE rounding/denormal modes
        PropSelectContext(s_job.m_prop);
        _mm_setcsr(s_job.m_csr);
        RunJob(s_job);

        if (s_pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            SetEvent(s_done);
        }
    }
}

uint32_t TargetThreads() {
    uint32_t threads = s_requested;

    if (!threads) {
        SYSTEM_INFO info = {};
        GetSystemInfo(&info);
        threads = info.dwNumberOfProcessors > 1 ? info.dwNumberOfProcessors - 1 : 1;

        if (threads > AUTO_MAX_THREADS) {
            threads = AUTO_MAX_THREADS;
        }
    }

    return threads < MAX_THREADS ? threads : MAX_THREADS;
}

void EnsureWorkers(uint32_t workers) {
    if (!s_start) {
        s_start = CreateSemaphoreA(nullptr, 0, MAX_THREADS, nullptr);
        s_done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    }

    typedef HRESULT(WINAPI * SetThreadDescriptionFunc)(HANDLE, PCWSTR);
    static SetThreadDescriptionFunc s_setDescription = reinterpret_cast<SetThreadDescriptionFunc>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription"));

    while (s_workerCount < workers) {
        HANDLE thread = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);

        if (!thread) {
            break;
        }

        if (s_setDescription) {
            s_setDescription(thread, L"Scene worker");
        }

        CloseHandle(thread);
        s_workerCount++;
    }
}

}

void ParallelSetThreads(uint32_t threads) {
    s_requested = threads;
}

uint32_t ParallelThreadCount() {
    return TargetThreads();
}

void ParallelFor(uint32_t count, uint32_t grain, ParallelForFunc func, void* context) {
    if (!count) {
        return;
    }

    uint32_t threads = TargetThreads();

    if (!grain) {
        grain = count / (threads * 4);
        grain = grain ? grain : 1;
    }

    uint32_t pieces = (count + grain - 1) / grain;
    uint32_t workers = (threads < pieces ? threads : pieces) - 1;

    if (!workers || t_inJob || s_busy.exchange(true, std::memory_order_acquire)) {
        func(context, 0, count);
        return;
    }

    EnsureWorkers(workers);

    if (workers > s_workerCount) {
        workers = s_workerCount;
    }

    if (!workers) {
        s_busy.store(false, std::memory_order_release);
        func(context, 0, count);
        return;
    }

    s_job.m_func = func;
    s_job.m_context = context;
    s_job.m_count = count;
    s_job.m_grain = grain;
    s_job.m_prop = PropGetSelectedContext();
    s_job.m_csr = _mm_getcsr();
    s_job.m_next.store(0, std::memory_order_relaxed);
    s_pending.store(static_cast<int32_t>(workers), std::memory_order_release);
    ReleaseSemaphore(s_start, static_cast<LONG>(workers), nullptr);

    RunJob(s_job);

    // the workers usually finish within microseconds of the caller; spin briefly before sleeping on the event. A
    // signal left over from a job whose end the spin saw only costs one more pass of the loop
    for (uint32_t i = 0; i < 2000 && s_pending.load(std::memory_order_acquire) > 0; i++) {
        _mm_pause();
    }

    while (s_pending.load(std::memory_order_acquire) > 0) {
        WaitForSingleObject(s_done, INFINITE);
    }

    s_busy.store(false, std::memory_order_release);
}
