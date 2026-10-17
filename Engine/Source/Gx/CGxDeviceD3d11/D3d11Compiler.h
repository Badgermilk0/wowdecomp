#ifndef ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11COMPILER_H
#define ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11COMPILER_H

// MODIFIED: D3D11 device. HLSL to shader model 5 bytecode with d3dcompiler_47 (part of Windows 10 and 11), cached on
// disk under Cache\D3D11Shaders\ by a hash of the source, entry point and profile: a first run compiles a shader when
// the engine first draws with it (about 20 ms each), later runs read it back.

#include <cstdint>
#include <string>
#include <vector>

namespace GxD3d11 {

// 128 bits of the text, for cache keys
struct Hash128 {
    uint64_t m_lo;
    uint64_t m_hi;

    bool operator==(const Hash128& other) const {
        return this->m_lo == other.m_lo && this->m_hi == other.m_hi;
    }
};

Hash128 HashBytes(const void* data, size_t size, uint64_t seed = 0);

bool CompileHlsl(const std::string& source, const char* entry, const char* profile, std::vector<uint8_t>& bytecode, std::string& error);

// compiles on worker threads ahead of use: the engine loads its shader files well before it first draws with them, and
// a compile that waits for the draw stalls the frame. CompileHlsl takes the result (or the queued job) when the draw
// comes
void PrecompileHlsl(const std::string& source, const char* entry, const char* profile);

}

#endif
