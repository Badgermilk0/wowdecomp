#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Compiler.h"
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <d3dcompiler.h>

// MODIFIED: D3D11 device, see D3d11Compiler.h

namespace GxD3d11 {

namespace {

// bump when the generated HLSL or the compile flags change in a way the source text does not show
const char s_cacheVersion[] = "gxd3d11-1";
const char s_cacheDir[] = "Cache\\D3D11Shaders";
const uint32_t s_cacheMagic = 0x31314433;   // '3D11'

uint64_t Fnv1a(const void* data, size_t size, uint64_t hash) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }

    return hash;
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
    static bool s_dirMade;

    if (!s_dirMade) {
        CreateDirectoryA("Cache", nullptr);
        CreateDirectoryA(s_cacheDir, nullptr);
        s_dirMade = true;
    }

    // written whole under a temporary name, so a crash never leaves half a shader behind
    std::string path = CachePath(hash);
    std::string temp = path + ".tmp";
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

}

Hash128 HashBytes(const void* data, size_t size, uint64_t seed) {
    Hash128 hash;
    hash.m_lo = Fnv1a(data, size, 0xCBF29CE484222325ull ^ seed);
    hash.m_hi = Fnv1a(data, size, 0x84222325CBF29CE4ull ^ (seed * 0x9E3779B97F4A7C15ull) ^ size);
    return hash;
}

bool CompileHlsl(const std::string& source, const char* entry, const char* profile, std::vector<uint8_t>& bytecode, std::string& error) {
    std::string keyText = source;
    keyText += '\n';
    keyText += entry;
    keyText += '\n';
    keyText += profile;
    keyText += '\n';
    keyText += s_cacheVersion;
    Hash128 hash = HashBytes(keyText.data(), keyText.size());

    if (ReadCache(hash, bytecode)) {
        return true;
    }

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

}
