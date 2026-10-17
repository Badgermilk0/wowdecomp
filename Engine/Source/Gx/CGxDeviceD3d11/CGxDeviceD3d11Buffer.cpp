#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include <cstring>
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Util.h"
#include "Engine/Source/Gx/CGxPool.h"

// MODIFIED: D3D11 device, see CGxDeviceD3d11.h. Vertex and index pools, occlusion queries, and the engine's shader
// objects.

using namespace GxD3d11;

void CGxDeviceD3d11::ICreatePool(CGxPool* pool) {
    if (!this->m_d3dDevice || pool->m_apiSpecific || pool->m_size <= 0) {
        return;
    }

    Pool* created = new Pool();
    created->m_size = static_cast<uint32_t>(pool->m_size);
    created->m_dynamic = pool->m_usage != GxPoolUsage_Static;
    created->m_discard = true;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (created->m_size + 15) & ~15u;
    desc.Usage = created->m_dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    desc.BindFlags = pool->m_target == GxPoolTarget_Index ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = created->m_dynamic ? D3D11_CPU_ACCESS_WRITE : 0;

    HRESULT hr = this->m_d3dDevice->CreateBuffer(&desc, nullptr, &created->m_buffer);

    if (FAILED(hr)) {
        CGxDevice::Log("CGxDeviceD3d11::ICreatePool(): %s (%d bytes) failed (%08X)", pool->m_name ? pool->m_name : "?", pool->m_size, static_cast<uint32_t>(hr));
        delete created;
        return;
    }

    pool->m_apiSpecific = created;
}

void CGxDeviceD3d11::IReleasePool(CGxPool* pool) {
    if (pool->m_usage == GxPoolUsage_Stream) {
        pool->m_offset = 0;
    }

    pool->Invalidate();

    Pool* released = static_cast<Pool*>(pool->m_apiSpecific);

    if (!released) {
        return;
    }

    if (this->m_ctx) {
        // the draws re-bind their buffers; nothing may point at a released one
        if (this->m_boundIb == released->m_buffer) {
            this->m_ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
            this->m_boundIb = nullptr;
        }

        for (uint32_t stream = 0; stream < GxVAs_Last; stream++) {
            if (this->m_boundVb[stream] == released->m_buffer) {
                ID3D11Buffer* none = nullptr;
                UINT zero = 0;
                this->m_ctx->IASetVertexBuffers(stream, 1, &none, &zero, &zero);
                this->m_boundVb[stream] = nullptr;
            }
        }
    }

    SafeRelease(released->m_buffer);
    delete released;
    pool->m_apiSpecific = nullptr;
}

void CGxDeviceD3d11::IReleasePools(int32_t freeAll) {
    for (CGxPool* pool = this->m_poolList.Head(); pool; pool = this->m_poolList.Next(pool)) {
        if (freeAll || pool->m_usage == GxPoolUsage_Stream || pool->m_usage == GxPoolUsage_Dynamic) {
            this->IReleasePool(pool);
        }
    }
}

void CGxDeviceD3d11::PoolSizeSet(CGxPool* pool, uint32_t size) {
    this->IReleasePool(pool);
    pool->m_size = size;
    this->ICreatePool(pool);
}

void CGxDeviceD3d11::PoolDestroy(CGxPool* pool) {
    if (pool) {
        this->IReleasePool(pool);
        CGxDevice::PoolDestroy(pool);
    }
}

char* CGxDeviceD3d11::BufLock(CGxBuf* buf) {
    CGxDevice::BufLock(buf);

    CGxPool* pool = buf->m_pool;

    if (!this->m_context) {
        return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
    }

    if (!pool->m_apiSpecific) {
        this->ICreatePool(pool);
    }

    Pool* created = static_cast<Pool*>(pool->m_apiSpecific);

    if (!created) {
        return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
    }

    // a stream pool is a ring: the buffer goes after the last one, or the pool is discarded and it starts over
    // (CGxDeviceD3d9Ex::IBufLock's D3DLOCK_DISCARD / D3DLOCK_NOOVERWRITE)
    if (pool->m_usage == GxPoolUsage_Stream) {
        uint32_t itemSize = buf->m_itemSize ? buf->m_itemSize : 1;
        uint32_t index = (itemSize + pool->m_offset - 1) - (itemSize + pool->m_offset - 1) % itemSize;

        if (index + buf->m_size > static_cast<uint32_t>(pool->m_size)) {
            pool->Discard();
            buf->m_index = 0;
            pool->m_offset = buf->m_size;
            created->m_discard = true;
        } else {
            buf->m_index = index;
            pool->m_offset = index + buf->m_size;
        }
    }

    if (buf->m_index + buf->m_size > created->m_size) {
        return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
    }

    // the engine writes into memory of ours; the unlock uploads it
    std::vector<uint8_t> bytes;

    if (!this->m_lockFree.empty()) {
        bytes = std::move(this->m_lockFree.back());
        this->m_lockFree.pop_back();
    }

    bytes.resize(buf->m_size ? buf->m_size : 1);
    std::vector<uint8_t>& locked = this->m_locks[buf];
    locked = std::move(bytes);
    return reinterpret_cast<char*>(locked.data());
}

int32_t CGxDeviceD3d11::BufUnlock(CGxBuf* buf, uint32_t size) {
    CGxDevice::BufUnlock(buf, size);

    CGxPool* pool = buf->m_pool;
    EmergencyMem& emergencyMem = this->m_emergencyMem[pool->m_target];

    if (emergencyMem.m_lock) {
        emergencyMem.Unlock();
        buf->m_valid = 0;
        this->m_intF5C = 1;
        return 1;
    }

    auto found = this->m_locks.find(buf);

    if (found == this->m_locks.end()) {
        buf->m_valid = 0;
        return 1;
    }

    std::vector<uint8_t> bytes = std::move(found->second);
    this->m_locks.erase(found);

    Pool* created = static_cast<Pool*>(pool->m_apiSpecific);
    uint32_t length = buf->m_size;
    bool ok = false;

    if (created && created->m_buffer && length && buf->m_index + length <= created->m_size) {
        if (created->m_dynamic) {
            D3D11_MAPPED_SUBRESOURCE mapped;

            if (SUCCEEDED(this->m_ctx->Map(created->m_buffer, 0, created->m_discard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped))) {
                memcpy(static_cast<uint8_t*>(mapped.pData) + buf->m_index, bytes.data(), length);
                this->m_ctx->Unmap(created->m_buffer, 0);
                created->m_discard = false;
                ok = true;
            }
        } else {
            D3D11_BOX box = { buf->m_index, 0, 0, buf->m_index + length, 1, 1 };
            this->m_ctx->UpdateSubresource(created->m_buffer, 0, &box, bytes.data(), 0, 0);
            ok = true;
        }
    }

    buf->m_valid = ok;
    this->m_lockFree.push_back(std::move(bytes));
    return 1;
}

void CGxDeviceD3d11::BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset) {
    CGxDevice::BufData(buf, data, size, offset);

    char* dst = this->BufLock(buf);

    if (!dst) {
        return;
    }

    // only the bytes given: the rest of the buffer keeps what the GPU holds
    auto found = this->m_locks.find(buf);

    if (found == this->m_locks.end() || offset + size > found->second.size()) {
        memcpy(dst + offset, data, size);
        this->BufUnlock(buf, 0);
        return;
    }

    std::vector<uint8_t> bytes = std::move(found->second);
    this->m_locks.erase(found);
    CGxDevice::BufUnlock(buf, size);

    CGxPool* pool = buf->m_pool;
    Pool* created = static_cast<Pool*>(pool->m_apiSpecific);
    bool ok = false;

    if (created && created->m_buffer && size && buf->m_index + offset + size <= created->m_size) {
        if (created->m_dynamic) {
            D3D11_MAPPED_SUBRESOURCE mapped;

            if (SUCCEEDED(this->m_ctx->Map(created->m_buffer, 0, created->m_discard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped))) {
                memcpy(static_cast<uint8_t*>(mapped.pData) + buf->m_index + offset, data, size);
                this->m_ctx->Unmap(created->m_buffer, 0);
                created->m_discard = false;
                ok = true;
            }
        } else {
            D3D11_BOX box = { buf->m_index + offset, 0, 0, buf->m_index + offset + size, 1, 1 };
            this->m_ctx->UpdateSubresource(created->m_buffer, 0, &box, data, 0, 0);
            ok = true;
        }
    }

    buf->m_valid = ok;
    this->m_lockFree.push_back(std::move(bytes));
}

void CGxDeviceD3d11::QueryCreate(CGxQuery*& query, EGxQueryType type) {
    CGxDevice::QueryCreate(query, type);

    if (!query->m_apiSpecific && this->m_d3dDevice) {
        D3D11_QUERY_DESC desc = { D3D11_QUERY_OCCLUSION, 0 };
        ID3D11Query* created = nullptr;

        if (SUCCEEDED(this->m_d3dDevice->CreateQuery(&desc, &created))) {
            query->m_apiSpecific = created;
        }
    }
}

void CGxDeviceD3d11::QueryDestroy(CGxQuery*& query) {
    if (query->m_apiSpecific) {
        static_cast<ID3D11Query*>(query->m_apiSpecific)->Release();
    }

    query->m_apiSpecific = nullptr;
    CGxDevice::QueryDestroy(query);
}

bool CGxDeviceD3d11::QueryBegin(CGxQuery* query) {
    if (!this->m_context) {
        return false;
    }

    if (!query->m_apiSpecific) {
        D3D11_QUERY_DESC desc = { D3D11_QUERY_OCCLUSION, 0 };
        ID3D11Query* created = nullptr;

        if (FAILED(this->m_d3dDevice->CreateQuery(&desc, &created))) {
            return false;
        }

        query->m_apiSpecific = created;
    }

    this->m_ctx->Begin(static_cast<ID3D11Query*>(query->m_apiSpecific));
    return true;
}

bool CGxDeviceD3d11::QueryEnd(CGxQuery* query) {
    if (!this->m_context || !query->m_apiSpecific) {
        return false;
    }

    this->m_ctx->End(static_cast<ID3D11Query*>(query->m_apiSpecific));
    return true;
}

bool CGxDeviceD3d11::QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data) {
    if (!this->m_context || !query->m_apiSpecific) {
        return false;
    }

    switch (param) {
        case GxQueryParam_Unk0:
            // the size of the result the engine reads (QueryGetData's 32 bits)
            data = sizeof(uint32_t);
            return true;

        case GxQueryParam_Unk1:
            // as CGxDeviceD3d9Ex: whether the result is in, returned false
            data = this->m_ctx->GetData(static_cast<ID3D11Query*>(query->m_apiSpecific), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            return false;

        default:
            return false;
    }
}

bool CGxDeviceD3d11::QueryGetData(CGxQuery* query, uint32_t& data) {
    if (!this->m_context || !query->m_apiSpecific) {
        return false;
    }

    ID3D11Query* d3dQuery = static_cast<ID3D11Query*>(query->m_apiSpecific);
    UINT64 samples = 0;
    HRESULT hr;

    do {
        hr = this->m_ctx->GetData(d3dQuery, &samples, sizeof(samples), 0);
    } while (hr == S_FALSE);

    data = samples > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(samples);
    return hr == S_OK;
}

void CGxDeviceD3d11::IReleaseQueries() {
    for (CGxQuery* query = this->m_queryList.Head(); query; query = this->m_queryList.Next(query)) {
        if (query->m_apiSpecific) {
            static_cast<ID3D11Query*>(query->m_apiSpecific)->Release();
        }

        query->m_apiSpecific = nullptr;
    }
}

void CGxDeviceD3d11::IShaderCreate(CGxShader* shader) {
    shader->m_valid = 0;

    if (!this->m_context) {
        return;
    }

    shader->m_loaded = 1;

    if (!shader->m_code.Count() || (shader->m_target != GxSh_Vertex && shader->m_target != GxSh_Pixel)) {
        return;
    }

    ShaderEntry* entry = static_cast<ShaderEntry*>(shader->m_apiSpecific);

    if (!entry) {
        entry = new ShaderEntry();
        shader->m_apiSpecific = entry;
    }

    entry->m_variants.clear();

    // the DX9 bytecode is translated per variant when a draw first needs it (ITranslatedProgram)
    if (!entry->m_dx9.Decode(shader->m_code.m_data, shader->m_code.Count())) {
        CGxDevice::Log("CGxDeviceD3d11: shader %s not translated: %s", shader->m_key.m_str ? shader->m_key.m_str : "?", entry->m_dx9.m_error.c_str());
        return;
    }

    if (entry->m_dx9.m_vertex != (shader->m_target == GxSh_Vertex)) {
        return;
    }

    shader->m_valid = 1;
}

void CGxDeviceD3d11::ShaderCreate(CGxShader** shaders, EGxShTarget target, const char* path, const char* name, uint32_t permutations) {
    CGxDevice::ShaderCreate(shaders, target, path, name, permutations);

    if (permutations == 1 && !shaders[0]->m_loaded) {
        this->IShaderCreate(shaders[0]);
    }
}

void CGxDeviceD3d11::ShaderDestroy(CGxShader*& shader) {
    if (shader->m_refCount == 1) {
        if (shader->m_apiSpecific) {
            delete static_cast<ShaderEntry*>(shader->m_apiSpecific);
            shader->m_apiSpecific = nullptr;
        }

        shader->m_loaded = 0;
    }

    CGxDevice::ShaderDestroy(shader);
}

void CGxDeviceD3d11::IShaderReload(CGxShader* shader, const char* path, const char* name) {
    if (shader->m_target != GxSh_Vertex && shader->m_target != GxSh_Pixel) {
        return;
    }

    if (shader->m_refCount == 1) {
        if (shader->m_apiSpecific) {
            delete static_cast<ShaderEntry*>(shader->m_apiSpecific);
            shader->m_apiSpecific = nullptr;
        }

        shader->m_loaded = 0;
    }

    this->IShaderLoad(&shader, static_cast<EGxShTarget>(shader->m_target), path, name, 1);
    this->IShaderCreate(shader);
}
