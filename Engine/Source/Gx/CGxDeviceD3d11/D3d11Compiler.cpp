#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Compiler.h"
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
#include <d3dcompiler.h>

// MODIFIED: D3D11 device, see D3d11Compiler.h

namespace GxD3d11 {

namespace {

// bump when the generated HLSL or the compile flags change in a way the source text does not show
const char s_cacheVersion[] = "gxd3d11-1";
const char s_cacheDir[] = "Cache\\D3D11Shaders";
const uint32_t s_cacheMagic = 0x31314433;   // '3D11'

struct HashKey {
    size_t operator()(const Hash128& hash) const {
        return static_cast<size_t>(hash.m_lo ^ (hash.m_hi * 0x9E3779B97F4A7C15ull));
    }
};

struct Job {
    std::string m_source;
    std::string m_entry;
    std::string m_profile;
    Hash128 m_hash;
};

// the workers' state; made once and never freed, so nothing is torn down under a worker at exit
struct Workers {
    SRWLOCK m_lock = SRWLOCK_INIT;
    CONDITION_VARIABLE m_wake = CONDITION_VARIABLE_INIT;
    CONDITION_VARIABLE m_done = CONDITION_VARIABLE_INIT;
    std::deque<Job> m_queue;
    std::unordered_set<Hash128, HashKey> m_queued;
    std::unordered_set<Hash128, HashKey> m_running;
    std::unordered_map<Hash128, std::vector<uint8_t>, HashKey> m_results;
    bool m_started = false;
};

Workers* s_workers;
SRWLOCK s_dirLock = SRWLOCK_INIT;
bool s_dirMade;

uint64_t Fnv1a(const void* data, size_t size, uint64_t hash) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }

    return hash;
}

Hash128 KeyHash(const std::string& source, const char* entry, const char* profile) {
    std::string keyText = source;
    keyText += '\n';
    keyText += entry;
    keyText += '\n';
    keyText += profile;
    keyText += '\n';
    keyText += s_cacheVersion;
    return HashBytes(keyText.data(), keyText.size());
}

std::string CachePath(const Hash128& hash) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%016llx%016llx.dxbc", s_cacheDir, static_cast<unsigned long long>(hash.m_hi), static_cast<unsigned long long>(hash.m_lo));
    return path;
}

bool ReadCache(const Hash128& hash, std::vector<uint8_t>& bytecode) {
    FILE* file = fopen(CachePath(hash).c_str(), "rb");

    if (!file) {
        return false;
    }

    uint32_t header[2] = {};
    bool ok = fread(header, sizeof(header), 1, file) == 1 && header[0] == s_cacheMagic && header[1] > 0 && header[1] < 0x1000000;

    if (ok) {
        bytecode.resize(header[1]);
        ok = fread(bytecode.data(), 1, bytecode.size(), file) == bytecode.size();
    }

    fclose(file);
    return ok;
}

void WriteCache(const Hash128& hash, const std::vector<uint8_t>& bytecode) {
    AcquireSRWLockExclusive(&s_dirLock);

    if (!s_dirMade) {
        CreateDirectoryA("Cache", nullptr);
        CreateDirectoryA(s_cacheDir, nullptr);
        s_dirMade = true;
    }

    ReleaseSRWLockExclusive(&s_dirLock);

    // written whole under a name of this thread's, then renamed: a reader never sees half a shader
    std::string path = CachePath(hash);
    char suffix[32];
    snprintf(suffix, sizeof(suffix), ".%lu.tmp", GetCurrentThreadId());
    std::string temp = path + suffix;
    FILE* file = fopen(temp.c_str(), "wb");

    if (!file) {
        return;
    }

    uint32_t header[2] = { s_cacheMagic, static_cast<uint32_t>(bytecode.size()) };
    bool ok = fwrite(header, sizeof(header), 1, file) == 1 && fwrite(bytecode.data(), 1, bytecode.size(), file) == bytecode.size();
    fclose(file);

    if (!ok || !MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(temp.c_str());
    }
}

bool Compile(const std::string& source, const char* entry, const char* profile, const Hash128& hash, std::vector<uint8_t>& bytecode, std::string& error) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL2, 0, &blob, &errors);

    if (errors) {
        error.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        errors->Release();
    }

    if (FAILED(hr) || !blob) {
        if (blob) {
            blob->Release();
        }

        if (error.empty()) {
            error = "D3DCompile failed";
        }

        return false;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(blob->GetBufferPointer());
    bytecode.assign(bytes, bytes + blob->GetBufferSize());
    blob->Release();
    WriteCache(hash, bytecode);
    return true;
}

DWORD WINAPI Worker(void* param) {
    Workers* workers = static_cast<Workers*>(param);

    for (;;) {
        AcquireSRWLockExclusive(&workers->m_lock);

        while (workers->m_queue.empty()) {
            SleepConditionVariableSRW(&workers->m_wake, &workers->m_lock, INFINITE, 0);
        }

        Job job = std::move(workers->m_queue.front());
        workers->m_queue.pop_front();
        workers->m_queued.erase(job.m_hash);
        workers->m_running.insert(job.m_hash);
        ReleaseSRWLockExclusive(&workers->m_lock);

        // a shader the cache already has is read when drawn; only new ones are compiled and handed over
        std::vector<uint8_t> bytecode;
        std::string error;
        bool compiled = false;

        if (GetFileAttributesA(CachePath(job.m_hash).c_str()) == INVALID_FILE_ATTRIBUTES) {
            compiled = Compile(job.m_source, job.m_entry.c_str(), job.m_profile.c_str(), job.m_hash, bytecode, error);
        }

        AcquireSRWLockExclusive(&workers->m_lock);

        if (compiled) {
            workers->m_results[job.m_hash] = std::move(bytecode);
        }

        workers->m_running.erase(job.m_hash);
        WakeAllConditionVariable(&workers->m_done);
        ReleaseSRWLockExclusive(&workers->m_lock);
    }
}

void StartWorkers() {
    s_workers = new Workers();

    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uint32_t count = info.dwNumberOfProcessors / 2;
    count = count < 1 ? 1 : (count > 8 ? 8 : count);

    for (uint32_t i = 0; i < count; i++) {
        HANDLE thread = CreateThread(nullptr, 0, Worker, s_workers, 0, nullptr);

        if (thread) {
            SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
            CloseHandle(thread);
        }
    }

    s_workers->m_started = true;
}

}

Hash128 HashBytes(const void* data, size_t size, uint64_t seed) {
    Hash128 hash;
    hash.m_lo = Fnv1a(data, size, 0xCBF29CE484222325ull ^ seed);
    hash.m_hi = Fnv1a(data, size, 0x84222325CBF29CE4ull ^ (seed * 0x9E3779B97F4A7C15ull) ^ size);
    return hash;
}

void PrecompileHlsl(const std::string& source, const char* entry, const char* profile) {
    if (!s_workers) {
        StartWorkers();
    }

    Hash128 hash = KeyHash(source, entry, profile);
    Workers* workers = s_workers;

    AcquireSRWLockExclusive(&workers->m_lock);

    if (!workers->m_queued.count(hash) && !workers->m_running.count(hash) && !workers->m_results.count(hash)) {
        workers->m_queue.push_back({ source, entry, profile, hash });
        workers->m_queued.insert(hash);
        WakeConditionVariable(&workers->m_wake);
    }

    ReleaseSRWLockExclusive(&workers->m_lock);
}

bool CompileHlsl(const std::string& source, const char* entry, const char* profile, std::vector<uint8_t>& bytecode, std::string& error) {
    Hash128 hash = KeyHash(source, entry, profile);

    if (Workers* workers = s_workers) {
        AcquireSRWLockExclusive(&workers->m_lock);

        // still queued: this thread compiles it now rather than wait for a worker to get to it
        if (workers->m_queued.erase(hash)) {
            for (auto it = workers->m_queue.begin(); it != workers->m_queue.end(); ++it) {
                if (it->m_hash == hash) {
                    workers->m_queue.erase(it);
                    break;
                }
            }
        }

        // being compiled: wait for it
        while (workers->m_running.count(hash)) {
            SleepConditionVariableSRW(&workers->m_done, &workers->m_lock, INFINITE, 0);
        }

        auto found = workers->m_results.find(hash);

        if (found != workers->m_results.end()) {
            bytecode = std::move(found->second);
            workers->m_results.erase(found);
            ReleaseSRWLockExclusive(&workers->m_lock);
            return true;
        }

        ReleaseSRWLockExclusive(&workers->m_lock);
    }

    if (ReadCache(hash, bytecode)) {
        return true;
    }

    return Compile(source, entry, profile, hash, bytecode, error);
}

}
