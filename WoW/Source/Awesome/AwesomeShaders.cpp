#include "WoW/Source/Awesome/AwesomeInternal.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <windows.h>
#include <d3dcompiler.h>
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Storm/H/storm.h"

// Shaders the features own, compiled from HLSL when the engine loads them. The original hooks
// CGxDevice::IShaderLoad; here the engine asks CGxDevice::s_shaderLoadOverride first. A shader is found by the name
// the engine creates it under ("name", or "name:k" for permutation k when reloaded on its own). Its stock .bls
// fills the metadata and stands in whenever the HLSL is switched off or fails to compile; HLSL is only compiled for
// the D3D9 devices, capped at shader model 3.

static std::vector<const AwesomeShaderSource*> s_sources;
// compiled bytecode by name|defines|entry|profile; an empty entry is a cached failure
static std::map<std::string, std::vector<uint8_t>> s_bytecode;

static const AwesomeShaderSource* FindSource(EGxShTarget target, const char* name, uint32_t& first) {
    first = 0;

    if (!name) {
        return nullptr;
    }

    const char* colon = strrchr(name, ':');
    size_t length = colon ? static_cast<size_t>(colon - name) : strlen(name);

    if (colon) {
        const char* suffix = colon + 1;

        if (!*suffix || strlen(suffix) > 4) {
            return nullptr;
        }

        for (const char* c = suffix; *c; c++) {
            if (*c < '0' || *c > '9') {
                return nullptr;
            }

            first = first * 10 + static_cast<uint32_t>(*c - '0');
        }
    }

    for (const AwesomeShaderSource* source : s_sources) {
        if (source->m_target == target && strlen(source->m_name) == length && !strncmp(source->m_name, name, length)) {
            return source;
        }
    }

    first = 0;
    return nullptr;
}

static const char* ProfileName(EGxShTarget target, int32_t profile) {
    if (target == GxSh_Vertex && profile > 0 && profile < 12) {
        return CGxDevice::s_vertexShaderTargetNames[profile];
    }

    if (target == GxSh_Pixel && profile > 0 && profile < 14) {
        return CGxDevice::s_pixelShaderTargetNames[profile];
    }

    return nullptr;
}

// the step-down IShaderLoad takes when a profile's .bls is missing
static int32_t NextProfile(EGxShTarget target, int32_t profile) {
    if (target == GxSh_Vertex) {
        return profile == 2 ? 1 : (profile == 3 ? 2 : 0);
    }

    if (target == GxSh_Pixel) {
        switch (profile) {
            case 2:
                return 1;
            case 3:
                return 2;
            case 4:
                return 3;
            case 9:
            case 10:
                return 8;
            default:
                return 0;
        }
    }

    return 0;
}

static bool IsD3d9(CGxDevice* device) {
    return device && (device->m_api == GxApi_D3d9 || device->m_api == GxApi_D3d9Ex);
}

// path\<profile>\name.bls records first .. first + count into shaders, like IShaderLoad from record first on
static bool LoadBls(CGxDevice* device, const char* path, const char* name, EGxShTarget target, CGxShader** shaders, uint32_t count, uint32_t first) {
    if (!path || !name) {
        return false;
    }

    for (int32_t profile = device->m_caps.m_shaderTargets[target]; profile > 0; profile = NextProfile(target, profile)) {
        const char* profileName = ProfileName(target, profile);

        if (!profileName) {
            continue;
        }

        char filename[260];
        SStrPrintf(filename, sizeof(filename), "%s\\%s\\%s.bls", path, profileName, name);

        SFileImpl* file = nullptr;
        SFileOpenFile(filename, &file);

        if (!file) {
            continue;
        }

        uint32_t header[3] = {};
        bool holds = SFileReadFile(file, header, sizeof(header), nullptr, nullptr, 0) && header[0] == 'GXSH' && header[1] == 0x10003;

        for (uint32_t i = 0; holds && i < first + count; i++) {
            // the records before first are read into the first node and overwritten
            CGxShader* shader = shaders[i < first ? 0 : i - first];
            shader->Load(file);
            shader->m_loaded = 0;
            shader->unused_0034 = 0;
        }

        SFileCloseFile(file);

        if (holds) {
            return true;
        }
    }

    return false;
}

static const std::vector<uint8_t>* Compile(const AwesomeShaderSource* source, CGxDevice* device, EGxShTarget target, uint32_t entry) {
    if (!source->m_hlsl || entry >= source->m_entryCount || !IsD3d9(device)) {
        return nullptr;
    }

    std::vector<D3D_SHADER_MACRO> macros;
    std::string defines;
    const AwesomeShaderDefine* list = source->m_defines ? source->m_defines() : nullptr;

    for (; list && list->m_name; list++) {
        macros.push_back({ list->m_name, list->m_value });
        defines.append(list->m_name).append(1, '=').append(list->m_value).append(1, ';');
    }

    macros.push_back({ nullptr, nullptr });

    // D3D9 ends at shader model 3: ps_3_0 / vs_3_0
    int32_t maxProfile = target == GxSh_Pixel ? 4 : 3;
    int32_t profile = device->m_caps.m_shaderTargets[target];

    for (profile = profile < maxProfile ? profile : maxProfile; profile > 0; profile--) {
        const char* profileName = ProfileName(target, profile);

        if (!profileName) {
            continue;
        }

        std::string key = std::string(source->m_name) + '|' + defines + '|' + std::to_string(entry) + '|' + profileName;
        auto found = s_bytecode.find(key);

        if (found == s_bytecode.end()) {
            std::vector<uint8_t> code;
            ID3DBlob* blob = nullptr;
            ID3DBlob* errors = nullptr;
            HRESULT result = D3DCompile(source->m_hlsl, strlen(source->m_hlsl), source->m_name, macros.data(), nullptr, source->m_entries[entry], profileName, 0, 0, &blob, &errors);

            if (errors) {
                errors->Release();
            }

            if (blob) {
                if (SUCCEEDED(result)) {
                    const uint8_t* bytes = static_cast<const uint8_t*>(blob->GetBufferPointer());
                    code.assign(bytes, bytes + blob->GetBufferSize());
                }

                blob->Release();
            }

            found = s_bytecode.emplace(key, std::move(code)).first;
        }

        if (!found->second.empty()) {
            return &found->second;
        }
    }

    return nullptr;
}

static int32_t ShaderLoadOverride(CGxDevice* device, CGxShader** shaders, EGxShTarget target, const char* path, const char* name, uint32_t permutations) {
    uint32_t first;
    const AwesomeShaderSource* source = FindSource(target, name, first);

    if (!source) {
        return 0;
    }

    bool hasMeta = LoadBls(device, source->m_fallbackPath, source->m_fallbackName, target, shaders, permutations, first);

    if (source->m_useFallback && source->m_useFallback()) {
        return 1;
    }

    std::vector<const std::vector<uint8_t>*> compiled(permutations);

    for (uint32_t i = 0; i < permutations; i++) {
        compiled[i] = Compile(source, device, target, first + i);

        // keep the stock shaders
        if (!compiled[i]) {
            return 1;
        }
    }

    for (uint32_t i = 0; i < permutations; i++) {
        CGxShader* shader = shaders[i];

        if (!hasMeta) {
            shader->unk_003C = 0;
            shader->unk_0040 = 0;
            shader->unk_0044 = 0;
            shader->unk_0046 = 0;
        }

        shader->m_code.SetCount(static_cast<uint32_t>(compiled[i]->size()));
        memcpy(shader->m_code.m_data, compiled[i]->data(), compiled[i]->size());
        shader->m_loaded = 0;
        shader->unused_0034 = 0;
    }

    return 1;
}

void AwesomeShaderRegister(const AwesomeShaderSource* source) {
    CGxDevice::s_shaderLoadOverride = ShaderLoadOverride;
    s_sources.push_back(source);
}

void AwesomeShaderReload(CGxShader* shader) {
    CGxDevice* device = g_theGxDevicePtr;

    if (!device || !shader || !shader->m_key.m_str) {
        return;
    }

    // IShaderReload only drops the API object of an unshared shader; the D3D9 one is always released here
    if (IsD3d9(device) && shader->m_apiSpecific) {
        static_cast<IUnknown*>(shader->m_apiSpecific)->Release();
        shader->m_apiSpecific = nullptr;
        shader->m_loaded = 0;
    }

    char name[256];
    SStrCopy(name, shader->m_key.m_str, sizeof(name));
    device->IShaderReload(shader, "", name);
}
