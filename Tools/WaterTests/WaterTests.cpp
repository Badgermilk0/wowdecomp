// MODIFIED: modern water and the smoother sky. Offline tests (no game, no realm):
//   - WoW/Source/WorldClient/SkyModel: the interpolation of the sky bands and the dome's geometry
//   - the ripple height field's pass (D3d11WaterHlsl.cpp) run on a WARP device: a disturbance spreads at the set
//     speed, stays bounded and dies away
//   - with a folder argument holding the stock vsLiquidWater.bls, psLiquidWater.bls and psLiquidWaterNoSpec.bls
//     (Shaders\Vertex\vs_3_0, Shaders\Pixel\ps_3_0 from the client's MPQs): every permutation translated with the
//     water family (D3d11Dx9Shader.cpp, s_hlslWater) and compiled to shader model 5
//   cmake --build build/x64 --config Release --target WaterTests
//   bin/Win/WaterTests.exe [folder with the .bls files]

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Dx9Shader.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Water.h"
#include "WoW/Source/WorldClient/SkyModel.h"

static int32_t s_failures;
static int32_t s_checks;

#define CHECK(condition) Check((condition), #condition, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) CheckNear((a), (b), (eps), #a, __FILE__, __LINE__)

static void Check(bool condition, const char* text, const char* file, int32_t line) {
    s_checks++;

    if (!condition) {
        s_failures++;
        printf("FAIL %s:%d: %s\n", file, line, text);
    }
}

static void CheckNear(float a, float b, float eps, const char* text, const char* file, int32_t line) {
    s_checks++;

    if (!(fabsf(a - b) <= eps)) {
        s_failures++;
        printf("FAIL %s:%d: %s = %f, expected %f\n", file, line, text, a, b);
    }
}

// --- the sky -----------------------------------------------------------------------------------------------------

static void TestMonotoneCubic() {
    const float x[6] = { 0.0f, 0.17f, 0.2f, 0.23f, 0.24f, 0.25f };
    const float y[6] = { 40.0f, 90.0f, 120.0f, 200.0f, 210.0f, 255.0f };

    for (uint32_t i = 0; i < 6; i++) {
        CHECK_NEAR(SkyModel::MonotoneCubic(x, y, 6, x[i]), y[i], 1e-3f);
    }

    // monotone data stays monotone, and inside its range
    float last = -1.0f;

    for (int32_t i = 0; i <= 1000; i++) {
        float t = 0.25f * i / 1000.0f;
        float v = SkyModel::MonotoneCubic(x, y, 6, t);
        CHECK(v >= last - 1e-3f);
        CHECK(v >= 40.0f - 1e-3f && v <= 255.0f + 1e-3f);
        last = v;
    }

    // a peak: no overshoot past it
    const float z[4] = { 10.0f, 200.0f, 30.0f, 30.0f };
    const float xs[4] = { 0.0f, 1.0f, 2.0f, 3.0f };

    for (int32_t i = 0; i <= 300; i++) {
        float v = SkyModel::MonotoneCubic(xs, z, 4, i / 100.0f);
        CHECK(v <= 200.0f + 1e-3f && v >= 10.0f - 1e-3f);
    }

    CHECK_NEAR(SkyModel::MonotoneCubic(x, y, 6, -1.0f), 40.0f, 1e-6f);
    CHECK_NEAR(SkyModel::MonotoneCubic(x, y, 6, 2.0f), 255.0f, 1e-6f);
}

static void TestCatmullRom() {
    float y[24];

    for (uint32_t i = 0; i < 24; i++) {
        y[i] = 128.0f + 100.0f * sinf(i * 6.2831853f / 24.0f);
    }

    for (uint32_t i = 0; i < 24; i++) {
        CHECK_NEAR(SkyModel::PeriodicCatmullRom(y, 24, static_cast<float>(i)), y[i], 1e-3f);
    }

    // periodic, and close to the smooth curve it samples
    CHECK_NEAR(SkyModel::PeriodicCatmullRom(y, 24, 24.5f), SkyModel::PeriodicCatmullRom(y, 24, 0.5f), 1e-3f);
    CHECK_NEAR(SkyModel::PeriodicCatmullRom(y, 24, -0.5f), SkyModel::PeriodicCatmullRom(y, 24, 23.5f), 1e-3f);

    for (int32_t i = 0; i < 240; i++) {
        float u = i / 10.0f;
        CHECK_NEAR(SkyModel::PeriodicCatmullRom(y, 24, u), 128.0f + 100.0f * sinf(u * 6.2831853f / 24.0f), 1.0f);
    }
}

static void TestDome() {
    // the stock rings: DNSky::GenSphere's vertices
    for (uint32_t r = 0; r < SkyModel::RING_COUNT; r++) {
        float s = SkyModel::s_ringTheta[r];

        for (uint32_t i = 0; i < 24; i++) {
            float xyz[3];
            SkyModel::DomePoint(s, i / 24.0f, xyz);
            float theta = s * 3.1415927f;
            float angle = i * 0.041666668f * 6.2831855f;
            CHECK_NEAR(xyz[0], sinf(angle) * sinf(theta), 1e-5f);
            CHECK_NEAR(xyz[1], sinf(theta) * cosf(angle), 1e-5f);
            CHECK_NEAR(xyz[2], cosf(theta) - cosf(0.78539819f), 1e-5f);
        }
    }

    // the eye at the origin: straight up meets the zenith, level meets the horizon ring
    CHECK_NEAR(SkyModel::DomeThetaForDirection(0.0f, 0.0f, 1.0f), 0.0f, 1e-4f);
    CHECK_NEAR(SkyModel::DomeThetaForDirection(1.0f, 0.0f, 0.0f), 0.25f, 1e-4f);
    CHECK_NEAR(SkyModel::DomeThetaForDirection(0.0f, 3.0f, 0.0f), 0.25f, 1e-4f);

    // and the point a direction sees lies on that direction
    for (int32_t i = 1; i < 20; i++) {
        float s = 0.25f * i / 20.0f;
        float xyz[3];
        SkyModel::DomePoint(s, 0.3f, xyz);
        CHECK_NEAR(SkyModel::DomeThetaForDirection(xyz[0], xyz[1], xyz[2]), s, 1e-3f);
    }

    float rows[64];
    uint32_t count = SkyModel::DomeRows(rows, 64);
    CHECK(count > 40 && count <= 64);
    CHECK_NEAR(rows[0], 0.0f, 1e-6f);
    CHECK_NEAR(rows[count - 1], 1.0f, 1e-6f);

    for (uint32_t i = 1; i < count; i++) {
        CHECK(rows[i] > rows[i - 1]);
    }

    // every stock ring is a row, so the smooth dome passes through the stock colours
    for (uint32_t r = 0; r < SkyModel::RING_COUNT; r++) {
        bool found = false;

        for (uint32_t i = 0; i < count; i++) {
            found = found || fabsf(rows[i] - SkyModel::s_ringTheta[r]) < 1e-5f;
        }

        CHECK(found);
    }

    CHECK_NEAR(SkyModel::SmoothFactor(0.0f, 1.0f), 0.0f, 1e-6f);
    CHECK_NEAR(SkyModel::SmoothFactor(1.0f, 0.0f), 1.0f, 1e-6f);
    CHECK_NEAR(SkyModel::SmoothFactor(1.0f, 1.0f), 1.0f - expf(-1.0f), 1e-5f);
    // frame-rate independent: two half steps make one whole
    float half = SkyModel::SmoothFactor(0.5f, 1.5f);
    CHECK_NEAR(1.0f - (1.0f - half) * (1.0f - half), SkyModel::SmoothFactor(1.0f, 1.5f), 1e-5f);
}

// --- HLSL --------------------------------------------------------------------------------------------------------

static bool Compile(const std::string& source, const char* entry, const char* profile, std::vector<uint8_t>* code, std::string& error) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(source.data(), source.size(), "test", nullptr, nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
    error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : std::string();

    if (errors) {
        errors->Release();
    }

    if (blob) {
        if (code) {
            const uint8_t* bytes = static_cast<const uint8_t*>(blob->GetBufferPointer());
            code->assign(bytes, bytes + blob->GetBufferSize());
        }

        blob->Release();
    }

    return SUCCEEDED(hr);
}

static void TestPassesCompile() {
    const char* entries[][2] = { { "FullscreenVs", "vs_5_0" }, { "SimPs", "ps_5_0" }, { "DepthPs", "ps_5_0" } };

    for (int32_t msaa = 0; msaa < 2; msaa++) {
        for (auto& entry : entries) {
            std::string source = msaa ? "#define GX_MSAA 1\n" : "#define GX_MSAA 0\n";
            source += GxD3d11::s_hlslWaterPasses;
            std::string error;
            bool ok = Compile(source, entry[0], entry[1], nullptr, error);
            CHECK(ok);

            if (!ok) {
                printf("  %s: %s\n", entry[0], error.c_str());
            }
        }
    }

    // the tessellation stages, on the shared declarations
    std::string source = std::string(GxD3d11::s_hlslCommon) + GxD3d11::s_hlslPatchPoint + GxD3d11::s_hlslWaterConstants + GxD3d11::s_hlslWaterTessellation;
    const char* stages[][2] = { { "WaterHs", "hs_5_0" }, { "WaterDs", "ds_5_0" } };

    for (auto& stage : stages) {
        std::string error;
        bool ok = Compile(source, stage[0], stage[1], nullptr, error);
        CHECK(ok);
        printf("  %s: %s\n", stage[0], ok ? "compiled" : error.c_str());
    }
}

// the GxWaterPass cbuffer, as D3d11Water.cpp fills it
struct PassConstants {
    int32_t m_shift[4];
    float m_wave[4];
    float m_impulses[64][4];
    int32_t m_count[4];
    float m_invProj[16];
    float m_viewport[4];
    float m_depthRange[4];
    float m_projW[4];
    float m_foam[4];
};

static void TestSimulation() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;

    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx))) {
        printf("SKIP the simulation: no WARP device\n");
        return;
    }

    std::string source = std::string("#define GX_MSAA 0\n") + GxD3d11::s_hlslWaterPasses;
    std::vector<uint8_t> vsCode;
    std::vector<uint8_t> psCode;
    std::string error;
    CHECK(Compile(source, "FullscreenVs", "vs_5_0", &vsCode, error));
    CHECK(Compile(source, "SimPs", "ps_5_0", &psCode, error));
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    device->CreateVertexShader(vsCode.data(), vsCode.size(), nullptr, &vs);
    device->CreatePixelShader(psCode.data(), psCode.size(), nullptr, &ps);

    const uint32_t n = D3d11Water::s_fieldSize;
    const float texel = D3d11Water::s_texel;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = n;
    desc.Height = n;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* field[2] = {};
    ID3D11RenderTargetView* rtv[2] = {};
    ID3D11ShaderResourceView* srv[2] = {};

    for (int32_t i = 0; i < 2; i++) {
        device->CreateTexture2D(&desc, nullptr, &field[i]);
        device->CreateRenderTargetView(field[i], nullptr, &rtv[i]);
        device->CreateShaderResourceView(field[i], nullptr, &srv[i]);
        float zero[4] = {};
        ctx->ClearRenderTargetView(rtv[i], zero);
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = (sizeof(PassConstants) + 15) & ~15u;
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer* cb = nullptr;
    device->CreateBuffer(&cbDesc, nullptr, &cb);

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    device->CreateTexture2D(&desc, nullptr, &staging);

    // as D3d11Water::IStep: 3 yards a second, damped 0.6 a second, 60 steps a second
    const float speed = 3.0f;
    const float dt = 1.0f / 60.0f;
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(n), static_cast<float>(n), 0.0f, 1.0f };
    uint32_t current = 0;

    // the heights along the row through the centre, x the height now; the foam of the field's middle
    float middleFoam = 0.0f;

    auto readRow = [&](std::vector<float>& row, bool& finite) {
        ctx->CopyResource(staging, field[current]);
        D3D11_MAPPED_SUBRESOURCE mapped;
        ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        row.assign(n, 0.0f);
        finite = true;

        for (uint32_t y = 0; y < n; y++) {
            const float* line = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);

            for (uint32_t x = 0; x < n; x++) {
                float h = line[x * 4];

                if (y == n / 2 && x == n / 2) {
                    middleFoam = line[x * 4 + 2];
                }
                finite = finite && std::isfinite(h);

                if (y == n / 2) {
                    row[x] = h;
                }
            }
        }

        ctx->Unmap(staging, 0);
    };

    auto step = [&](bool impulse, float rain) {
        static uint32_t seed = 0;
        PassConstants c = {};
        c.m_shift[2] = static_cast<int32_t>(seed++);
        c.m_shift[3] = static_cast<int32_t>(n);
        float courant = speed * dt / texel;
        c.m_wave[0] = courant * courant;
        c.m_wave[1] = expf(-0.6f * dt);
        c.m_wave[2] = rain * texel * texel * dt;
        c.m_wave[3] = 0.012f;
        c.m_foam[0] = expf(-0.35f * dt);
        c.m_foam[1] = 10.0f;
        c.m_foam[2] = 60.0f;
        c.m_foam[3] = 1.0f / 1000000.0f;

        if (impulse) {
            c.m_impulses[0][0] = n / 2.0f;
            c.m_impulses[0][1] = n / 2.0f;
            c.m_impulses[0][2] = 0.6f / texel;
            c.m_impulses[0][3] = 0.12f;
            c.m_count[0] = 1;
        }

        ctx->UpdateSubresource(cb, 0, nullptr, &c, 0, 0);
        uint32_t next = current ^ 1u;
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(0, 1, &none);
        ctx->OMSetRenderTargets(1, &rtv[next], nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, &cb);
        ctx->PSSetShaderResources(0, 1, &srv[current]);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        current = next;
    };

    // a splash in the middle, then two seconds
    step(true, 0.0f);

    for (int32_t i = 1; i < 120; i++) {
        step(false, 0.0f);
    }

    std::vector<float> row;
    bool finite = false;
    readRow(row, finite);
    CHECK(finite);
    printf("  foam where it splashed, after 2 s: %.3f\n", middleFoam);
    CHECK(middleFoam > 0.05f && middleFoam <= 1.0f);

    // the ring: the largest displacement away from the centre, about speed * 2 s = 6 yards out (24 texels)
    uint32_t peak = 0;
    float peakValue = 0.0f;
    float maxAbs = 0.0f;

    for (uint32_t x = n / 2; x < n - 30; x++) {
        float a = fabsf(row[x]);
        maxAbs = a > maxAbs ? a : maxAbs;

        if (a > peakValue) {
            peakValue = a;
            peak = x;
        }
    }

    // the front: the farthest texel the disturbance has reached
    uint32_t front = n / 2;

    for (uint32_t x = n / 2; x < n - 30; x++) {
        if (fabsf(row[x]) > peakValue * 0.05f) {
            front = x;
        }
    }

    float frontYards = (front + 0.5f - n / 2.0f) * texel;
    printf("  the splash after 2 s: front at %.2f yd (3 yd/s: 6 expected)\n", frontYards);
    CHECK(frontYards > 5.0f && frontYards < 7.5f);
    float ringYards = (peak - n / 2.0f) * texel;
    printf("  the splash after 2 s: ring at %.2f yd, height %.4f yd\n", ringYards, peakValue);
    CHECK(ringYards > 1.0f && ringYards < frontYards);
    CHECK(peakValue > 0.0005f && peakValue < 0.12f);
    // symmetric about the centre
    // (the splash is centred between texels n / 2 - 1 and n / 2)
    CHECK_NEAR(row[n / 2 + 20], row[n / 2 - 21], 0.002f);

    // ten seconds later: died away, still finite
    for (int32_t i = 0; i < 600; i++) {
        step(false, 0.0f);
    }

    readRow(row, finite);
    float late = 0.0f;

    for (float h : row) {
        late = fabsf(h) > late ? fabsf(h) : late;
    }

    printf("  12 s after the splash: largest height %.5f yd, foam %.4f\n", late, middleFoam);
    CHECK(middleFoam < 0.05f);
    CHECK(finite);
    CHECK(late < peakValue * 0.2f);

    // a minute of heavy rain: bounded, never blowing up
    for (int32_t i = 0; i < 3600; i++) {
        step(false, 3.0f);
    }

    readRow(row, finite);
    float rainMax = 0.0f;

    for (float h : row) {
        rainMax = fabsf(h) > rainMax ? fabsf(h) : rainMax;
    }

    printf("  after a minute of heavy rain: largest height %.4f yd\n", rainMax);
    CHECK(finite);
    CHECK(rainMax > 0.0f && rainMax <= 0.3f);

    staging->Release();
    cb->Release();

    for (int32_t i = 0; i < 2; i++) {
        srv[i]->Release();
        rtv[i]->Release();
        field[i]->Release();
    }

    ps->Release();
    vs->Release();
    ctx->Release();
    device->Release();
}

// --- the translated water shaders ------------------------------------------------------------------------------------

static bool ReadFile(const std::string& path, std::vector<uint8_t>& data) {
    FILE* file = fopen(path.c_str(), "rb");

    if (!file) {
        return false;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data.resize(size > 0 ? size : 0);
    size_t read = fread(data.data(), 1, data.size(), file);
    fclose(file);
    return read == data.size();
}

static void TestTranslated(const std::string& folder) {
    struct Case {
        const char* m_file;
        const char* m_name;
        bool m_vertex;
    };

    const Case cases[] = {
        { "vsLiquidWater.bls", "vsLiquidWater", true },
        { "psLiquidWater.bls", "psLiquidWater", false },
        { "psLiquidWaterNoSpec.bls", "psLiquidWaterNoSpec", false },
        // the HD client's water (its LiquidType.dbc makes every world water material 3)
        { "vsLiquidProcWater.bls", "vsLiquidProcWater", true },
        { "psLiquidProcWater.bls", "psLiquidProcWater", false },
    };

    for (const Case& test : cases) {
        std::vector<uint8_t> data;

        if (!ReadFile(folder + "\\" + test.m_file, data)) {
            printf("SKIP %s: not in %s\n", test.m_file, folder.c_str());
            continue;
        }

        uint32_t count = 0;
        CHECK(data.size() >= 12);
        memcpy(&count, data.data() + 8, 4);
        size_t offset = 12;

        for (uint32_t record = 0; record < count && offset + 16 <= data.size(); record++) {
            uint32_t size = 0;
            memcpy(&size, data.data() + offset + 12, 4);
            offset += 16;

            GxD3d11::Dx9Shader shader;
            bool decoded = shader.Decode(data.data() + offset, size);
            CHECK(decoded);
            offset += (size + 3) & ~3u;

            if (!decoded) {
                printf("  %s:%u: %s\n", test.m_name, record, shader.m_error.c_str());
                continue;
            }

            char name[64];
            snprintf(name, sizeof(name), "%s:%u", test.m_name, record);
            shader.m_family = GxD3d11::FamilyForShader(name, test.m_vertex);
            CHECK(shader.m_family == (test.m_vertex ? GxD3d11::Family_WaterVertex : GxD3d11::Family_WaterPixel));

            std::string hlsl;
            std::string error;
            bool emitted = shader.Emit(0, hlsl, error);
            CHECK(emitted);
            CHECK(hlsl.find(test.m_vertex ? "o.gxP = float4((C[5]" : "GxWaterShade(i, o.c0)") != std::string::npos);

            std::string compileError;
            bool compiled = emitted && Compile(hlsl, "main", test.m_vertex ? "vs_5_0" : "ps_5_0", nullptr, compileError);

            if (test.m_vertex) {
                // the variant that feeds the tessellation stages
                std::string tessHlsl;
                bool tessEmitted = shader.Emit(GxD3d11::Variant_Tessellated, tessHlsl, error);
                CHECK(tessEmitted && tessHlsl.find("return GxToPatchPoint(o);") != std::string::npos);
                std::string tessError;
                bool tessCompiled = tessEmitted && Compile(tessHlsl, "main", "vs_5_0", nullptr, tessError);
                CHECK(tessCompiled);
                printf("  %s (tessellated): %s\n", name, tessCompiled ? "translated and compiled" : tessError.c_str());
            }
            CHECK(compiled);
            printf("  %s: %s\n", name, compiled ? "translated and compiled" : compileError.c_str());

            if (!compiled) {
                FILE* out = fopen((std::string(test.m_name) + "_failed.hlsl").c_str(), "wb");

                if (out) {
                    fwrite(hlsl.data(), 1, hlsl.size(), out);
                    fclose(out);
                }
            }
        }
    }

    // the other shaders are not touched
    CHECK(GxD3d11::FamilyForShader("psLiquidMagma", false) == GxD3d11::Family_None);
    CHECK(GxD3d11::FamilyForShader("vsLiquidProcWater", true) == GxD3d11::Family_WaterVertex);
    CHECK(GxD3d11::FamilyForShader("psLiquidProcWater", false) == GxD3d11::Family_WaterPixel);
    CHECK(GxD3d11::FamilyForShader("Terrain", true) == GxD3d11::Family_Terrain);
}

// the first record of a stock .bls, translated with the family its name gives
static bool TranslateFirst(const std::string& path, const char* name, bool vertex, uint32_t variant, std::vector<uint8_t>& code) {
    std::vector<uint8_t> data;

    if (!ReadFile(path, data) || data.size() < 28) {
        return false;
    }

    uint32_t size = 0;
    memcpy(&size, data.data() + 24, 4);
    GxD3d11::Dx9Shader shader;

    if (!shader.Decode(data.data() + 28, size)) {
        return false;
    }

    shader.m_family = GxD3d11::FamilyForShader(name, vertex);
    std::string hlsl;
    std::string error;
    return shader.Emit(variant, hlsl, error) && Compile(hlsl, "main", vertex ? "vs_5_0" : "ps_5_0", &code, error);
}

struct Signature {
    std::string m_semantic;
    uint32_t m_index;
    uint32_t m_register;
    uint32_t m_mask;
};

static std::vector<Signature> Reflect(const std::vector<uint8_t>& code, bool outputs) {
    std::vector<Signature> result;
    ID3D11ShaderReflection* reflection = nullptr;

    if (FAILED(D3DReflect(code.data(), code.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(&reflection)))) {
        return result;
    }

    D3D11_SHADER_DESC desc;
    reflection->GetDesc(&desc);
    uint32_t count = outputs ? desc.OutputParameters : desc.InputParameters;

    for (uint32_t i = 0; i < count; i++) {
        D3D11_SIGNATURE_PARAMETER_DESC param;

        if (outputs) {
            reflection->GetOutputParameterDesc(i, &param);
        } else {
            reflection->GetInputParameterDesc(i, &param);
        }

        result.push_back({ param.SemanticName, param.SemanticIndex, param.Register, param.Mask });
    }

    reflection->Release();
    return result;
}

// every element the reader takes is one the writer gives, in the same register
static bool Links(const std::vector<Signature>& writer, const std::vector<Signature>& reader, const char* what) {
    bool ok = !reader.empty();

    for (const Signature& in : reader) {
        if (in.m_semantic.compare(0, 3, "SV_") == 0 && in.m_semantic != "SV_Position" && in.m_semantic != "SV_POSITION") {
            continue;
        }

        bool found = false;

        for (const Signature& out : writer) {
            if (_stricmp(out.m_semantic.c_str(), in.m_semantic.c_str()) == 0 && out.m_index == in.m_index) {
                found = out.m_register == in.m_register && (out.m_mask & in.m_mask) == in.m_mask;
            }
        }

        if (!found) {
            printf("  %s: %s%u (register %u) not linked\n", what, in.m_semantic.c_str(), in.m_index, in.m_register);
            ok = false;
        }
    }

    return ok;
}

static void TestLinkage(const std::string& folder, const char* vsName, const char* psName) {
    std::vector<uint8_t> vs;
    std::vector<uint8_t> vsTess;
    std::vector<uint8_t> ps;
    std::string vsFile = folder + "\\" + vsName + ".bls";
    std::string psFile = folder + "\\" + psName + ".bls";
    std::string vsKey = std::string(vsName) + ":0";
    printf("  %s + %s\n", vsName, psName);

    if (!TranslateFirst(vsFile, vsKey.c_str(), true, 0, vs) || !TranslateFirst(vsFile, vsKey.c_str(), true, GxD3d11::Variant_Tessellated, vsTess) || !TranslateFirst(psFile, psName, false, 0, ps)) {
        printf("SKIP the linkage: the stock water shaders are not in %s\n", folder.c_str());
        return;
    }

    std::string source = std::string(GxD3d11::s_hlslCommon) + GxD3d11::s_hlslPatchPoint + GxD3d11::s_hlslWaterConstants + GxD3d11::s_hlslWaterTessellation;
    std::vector<uint8_t> hs;
    std::vector<uint8_t> ds;
    std::string error;
    CHECK(Compile(source, "WaterHs", "hs_5_0", &hs, error));
    CHECK(Compile(source, "WaterDs", "ds_5_0", &ds, error));

    bool plain = Links(Reflect(vs, true), Reflect(ps, false), "VS -> PS");
    bool intoHull = Links(Reflect(vsTess, true), Reflect(hs, false), "VS (tessellated) -> HS");
    bool intoDomain = Links(Reflect(hs, true), Reflect(ds, false), "HS -> DS");
    bool intoPixel = Links(Reflect(ds, true), Reflect(ps, false), "DS -> PS");
    CHECK(plain);
    CHECK(intoHull);
    CHECK(intoDomain);
    CHECK(intoPixel);
    printf("  linkage: VS -> PS %s, VS -> HS %s, HS -> DS %s, DS -> PS %s\n", plain ? "ok" : "FAIL", intoHull ? "ok" : "FAIL", intoDomain ? "ok" : "FAIL", intoPixel ? "ok" : "FAIL");
}

// the GxWater cbuffer, as D3d11Water.cpp fills it
struct WaterConstants {
    uint32_t m_flags[4];
    float m_viewInv[16];
    float m_proj[16];
    float m_projInv[16];
    float m_viewport[4];
    float m_depthRange[4];
    float m_target[4];
    float m_sunDir[4];
    float m_sunColor[4];
    float m_sky[6][4];
    float m_ambient[4];
    float m_params0[4];
    float m_params1[4];
    float m_field[4];
    float m_fieldExtra[4];
    float m_view[16];
    float m_tess[4];
};

// the tessellation run on WARP: a 4-yard water quad 5 yards ahead, a height field lifted 0.5 yards everywhere; the domain
// shader's points streamed out
static void TestTessellation() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;

    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx))) {
        printf("SKIP the tessellation: no WARP device\n");
        return;
    }

    std::string common = std::string(GxD3d11::s_hlslCommon) + GxD3d11::s_hlslPatchPoint + GxD3d11::s_hlslWaterConstants;
    std::string vsSource = common +
        "GxPatchPoint TestVs(float3 p : POSITION) {\n"
        "    GxVaryings o = (GxVaryings)0;\n"
        "    o.gxP = float4(p, 1.0);\n"
        "    o.pos = mul(float4(p, 1.0), gxWaterProj);\n"
        "    return GxToPatchPoint(o);\n"
        "}\n";
    std::string tessSource = common + GxD3d11::s_hlslWaterTessellation;
    std::vector<uint8_t> vsCode;
    std::vector<uint8_t> hsCode;
    std::vector<uint8_t> dsCode;
    std::string error;
    bool compiled = Compile(vsSource, "TestVs", "vs_5_0", &vsCode, error) && Compile(tessSource, "WaterHs", "hs_5_0", &hsCode, error) && Compile(tessSource, "WaterDs", "ds_5_0", &dsCode, error);
    CHECK(compiled);

    if (!compiled) {
        printf("  %s\n", error.c_str());
        return;
    }

    ID3D11VertexShader* vs = nullptr;
    ID3D11HullShader* hs = nullptr;
    ID3D11DomainShader* ds = nullptr;
    ID3D11GeometryShader* so = nullptr;
    device->CreateVertexShader(vsCode.data(), vsCode.size(), nullptr, &vs);
    device->CreateHullShader(hsCode.data(), hsCode.size(), nullptr, &hs);
    device->CreateDomainShader(dsCode.data(), dsCode.size(), nullptr, &ds);

    // gxP, the view-space position
    D3D11_SO_DECLARATION_ENTRY entry = { 0, "TEXCOORD", 11, 0, 4, 0 };
    UINT stride = 16;
    CHECK(SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(dsCode.data(), dsCode.size(), &entry, 1, &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &so)));

    D3D11_INPUT_ELEMENT_DESC element = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    ID3D11InputLayout* layout = nullptr;
    device->CreateInputLayout(&element, 1, vsCode.data(), vsCode.size(), &layout);

    // the view is the world here (identity), z up the field's height
    const float quad[6][3] = { { -2, -2, 5 }, { 2, -2, 5 }, { 2, 2, 5 }, { -2, -2, 5 }, { 2, 2, 5 }, { -2, 2, 5 } };
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(quad);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init = { quad, 0, 0 };
    ID3D11Buffer* vb = nullptr;
    device->CreateBuffer(&desc, &init, &vb);

    WaterConstants c = {};
    float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    float proj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, -0.1f, 0 };
    c.m_flags[0] = 0x15;
    memcpy(c.m_viewInv, identity, sizeof(identity));
    memcpy(c.m_view, identity, sizeof(identity));
    memcpy(c.m_proj, proj, sizeof(proj));
    c.m_target[0] = 1.0f / 1280.0f;
    c.m_target[1] = 1.0f / 720.0f;
    c.m_field[0] = -64.0f;
    c.m_field[1] = -64.0f;
    c.m_field[2] = 1.0f / 128.0f;
    c.m_field[3] = 1.0f / 512.0f;
    c.m_fieldExtra[0] = 0.25f;
    c.m_fieldExtra[1] = 1.0f;
    c.m_tess[0] = 1.0f;
    c.m_tess[1] = 32.0f;
    c.m_tess[2] = 0.3f;
    c.m_tess[3] = 1.0f;
    desc.ByteWidth = (sizeof(c) + 15) & ~15u;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    init.pSysMem = &c;
    ID3D11Buffer* cb = nullptr;
    CHECK(SUCCEEDED(device->CreateBuffer(&desc, &init, &cb)));

    // a height of 0.5 everywhere (0x3800 as a half)
    std::vector<uint16_t> texels(512 * 512 * 2, 0);

    for (size_t i = 0; i < texels.size(); i += 2) {
        texels[i] = 0x3800;
    }

    D3D11_TEXTURE2D_DESC tex = {};
    tex.Width = 512;
    tex.Height = 512;
    tex.MipLevels = 1;
    tex.ArraySize = 1;
    tex.Format = DXGI_FORMAT_R16G16_FLOAT;
    tex.SampleDesc.Count = 1;
    tex.Usage = D3D11_USAGE_DEFAULT;
    tex.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA texInit = { texels.data(), 512 * 4, 0 };
    ID3D11Texture2D* field = nullptr;
    ID3D11ShaderResourceView* fieldSrv = nullptr;
    device->CreateTexture2D(&tex, &texInit, &field);
    device->CreateShaderResourceView(field, nullptr, &fieldSrv);
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ID3D11SamplerState* sampler = nullptr;
    device->CreateSamplerState(&samplerDesc, &sampler);

    const uint32_t capacity = 65536;
    desc.ByteWidth = capacity * 16;
    desc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
    ID3D11Buffer* out = nullptr;
    device->CreateBuffer(&desc, nullptr, &out);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* staging = nullptr;
    device->CreateBuffer(&desc, nullptr, &staging);

    D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_SO_STATISTICS, 0 };
    ID3D11Query* query = nullptr;
    device->CreateQuery(&queryDesc, &query);

    UINT offset = 0;
    UINT vbStride = 12;
    ctx->IASetInputLayout(layout);
    ctx->IASetVertexBuffers(0, 1, &vb, &vbStride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->VSSetConstantBuffers(5, 1, &cb);
    ctx->HSSetShader(hs, nullptr, 0);
    ctx->HSSetConstantBuffers(5, 1, &cb);
    ctx->DSSetShader(ds, nullptr, 0);
    ctx->DSSetConstantBuffers(5, 1, &cb);
    ctx->DSSetShaderResources(20, 1, &fieldSrv);
    ctx->DSSetSamplers(15, 1, &sampler);
    ctx->GSSetShader(so, nullptr, 0);
    ctx->SOSetTargets(1, &out, &offset);
    ctx->Begin(query);
    ctx->Draw(6, 0);
    ctx->End(query);

    D3D11_QUERY_DATA_SO_STATISTICS stats = {};

    while (ctx->GetData(query, &stats, sizeof(stats), 0) == S_FALSE) {
    }

    ctx->CopyResource(staging, out);
    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    const float* points = static_cast<const float*>(mapped.pData);
    uint32_t written = static_cast<uint32_t>(stats.NumPrimitivesWritten < capacity / 3 ? stats.NumPrimitivesWritten : capacity / 3);
    float lo = 1e9f;
    float hi = -1e9f;

    for (uint32_t i = 0; i < written * 3; i++) {
        float z = points[i * 4 + 2];
        lo = z < lo ? z : lo;
        hi = z > hi ? z : hi;
    }

    ctx->Unmap(staging, 0);
    printf("  tessellation: 2 triangles became %u, every point at z %.3f to %.3f (5 + 0.5 expected)\n", written, lo, hi);
    // 4-yard edges at 5 yards with 0.3-yard targets: factor about 13 an edge, well over a hundred triangles
    CHECK(written > 100);
    CHECK_NEAR(lo, 5.5f, 0.01f);
    CHECK_NEAR(hi, 5.5f, 0.01f);

    query->Release();
    staging->Release();
    out->Release();
    sampler->Release();
    fieldSrv->Release();
    field->Release();
    cb->Release();
    vb->Release();
    layout->Release();
    so->Release();
    ds->Release();
    hs->Release();
    vs->Release();
    ctx->Release();
    device->Release();
}

// the GxWaterParticles cbuffer, as D3d11WaterParticles.cpp fills it
struct ParticleConstants {
    float m_offset[4];
    float m_field[4];
    float m_sim[4];
    uint32_t m_emit[4];
    uint32_t m_count[4];
    float m_emitters[64][4];
};

struct Particle {
    float m_pos[3];
    float m_life;
    float m_vel[3];
    float m_size;
    float m_waterZ;
    float m_kind;
    float m_age;
    float m_maxLife;
};

// a splash of 100 droplets at the field's middle: they rise, fall back through the surface, and each leaves an impact
static void TestParticles() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;

    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx))) {
        printf("SKIP the particles: no WARP device\n");
        return;
    }

    std::string source = std::string(GxD3d11::s_hlslWaterConstants) + GxD3d11::s_hlslWaterParticles;
    std::vector<uint8_t> emitCode;
    std::vector<uint8_t> updateCode;
    std::string error;
    bool compiled = Compile(source, "EmitCs", "cs_5_0", &emitCode, error) && Compile(source, "UpdateCs", "cs_5_0", &updateCode, error) && Compile(source, "ParticleVs", "vs_5_0", nullptr, error) && Compile(source, "ParticlePs", "ps_5_0", nullptr, error);
    CHECK(compiled);

    if (!compiled) {
        printf("  %s\n", error.c_str());
        return;
    }

    ID3D11ComputeShader* emitCs = nullptr;
    ID3D11ComputeShader* updateCs = nullptr;
    device->CreateComputeShader(emitCode.data(), emitCode.size(), nullptr, &emitCs);
    device->CreateComputeShader(updateCode.data(), updateCode.size(), nullptr, &updateCs);

    const uint32_t capacity = 8192;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(Particle) * capacity;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(Particle);
    ID3D11Buffer* particles = nullptr;
    ID3D11UnorderedAccessView* particleUav = nullptr;
    device->CreateBuffer(&desc, nullptr, &particles);
    device->CreateUnorderedAccessView(particles, nullptr, &particleUav);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* particleStaging = nullptr;
    device->CreateBuffer(&desc, nullptr, &particleStaging);

    D3D11_TEXTURE2D_DESC tex = {};
    tex.Width = 512;
    tex.Height = 512;
    tex.MipLevels = 1;
    tex.ArraySize = 1;
    tex.Format = DXGI_FORMAT_R32_UINT;
    tex.SampleDesc.Count = 1;
    tex.Usage = D3D11_USAGE_DEFAULT;
    tex.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    ID3D11Texture2D* impact = nullptr;
    ID3D11UnorderedAccessView* impactUav = nullptr;
    device->CreateTexture2D(&tex, nullptr, &impact);
    device->CreateUnorderedAccessView(impact, nullptr, &impactUav);
    tex.Usage = D3D11_USAGE_STAGING;
    tex.BindFlags = 0;
    tex.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* impactStaging = nullptr;
    device->CreateTexture2D(&tex, nullptr, &impactStaging);

    UINT zero[4] = {};
    ctx->ClearUnorderedAccessViewUint(particleUav, zero);
    ctx->ClearUnorderedAccessViewUint(impactUav, zero);

    desc.ByteWidth = (sizeof(ParticleConstants) + 15) & ~15u;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = 0;
    desc.StructureByteStride = 0;
    ID3D11Buffer* cb = nullptr;
    device->CreateBuffer(&desc, nullptr, &cb);

    // particle space: the field's corner at (-64, -64), the water at z 0, the splash at the origin
    ParticleConstants c = {};
    c.m_field[0] = -64.0f;
    c.m_field[1] = -64.0f;
    c.m_field[2] = 4.0f;
    c.m_field[3] = 512.0f;
    c.m_sim[0] = 15.0f;
    c.m_sim[1] = 0.6f;
    c.m_sim[2] = 1000000.0f;
    c.m_sim[3] = 0.004f;
    c.m_emit[1] = capacity;
    c.m_emit[2] = 12345;
    c.m_emit[3] = 100;
    c.m_count[0] = 1;
    c.m_count[1] = 1;
    c.m_emitters[0][3] = 100.0f;
    c.m_emitters[1][0] = 4.5f;
    c.m_emitters[1][1] = 2.6f;
    c.m_emitters[1][2] = 0.5f;
    c.m_emitters[1][3] = 0.0f;
    c.m_offset[3] = 1.0f / 60.0f;

    ID3D11UnorderedAccessView* uavs[2] = { particleUav, impactUav };
    ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    ctx->CSSetConstantBuffers(6, 1, &cb);
    ctx->UpdateSubresource(cb, 0, nullptr, &c, 0, 0);
    ctx->CSSetShader(emitCs, nullptr, 0);
    ctx->Dispatch(2, 1, 1);

    auto readParticles = [&](uint32_t& alive, float& highest) {
        ctx->CopyResource(particleStaging, particles);
        D3D11_MAPPED_SUBRESOURCE mapped;
        ctx->Map(particleStaging, 0, D3D11_MAP_READ, 0, &mapped);
        const Particle* all = static_cast<const Particle*>(mapped.pData);
        alive = 0;
        highest = -1e9f;

        for (uint32_t i = 0; i < capacity; i++) {
            if (all[i].m_life > 0.0f) {
                alive++;
                highest = all[i].m_pos[2] > highest ? all[i].m_pos[2] : highest;
            }
        }

        ctx->Unmap(particleStaging, 0);
    };

    uint32_t alive = 0;
    float highest = 0.0f;
    readParticles(alive, highest);
    CHECK(alive == 100);

    // a quarter of a second: in the air, risen
    c.m_emit[3] = 0;
    ctx->UpdateSubresource(cb, 0, nullptr, &c, 0, 0);
    ctx->CSSetShader(updateCs, nullptr, 0);

    for (int32_t i = 0; i < 15; i++) {
        ctx->Dispatch(capacity / 64, 1, 1);
    }

    readParticles(alive, highest);
    printf("  splash after 0.25 s: %u droplets in the air, the highest %.2f yd up\n", alive, highest);
    CHECK(alive > 75);
    CHECK(highest > 0.4f && highest < 2.0f);

    // two seconds: all back in the water, each an impact
    for (int32_t i = 0; i < 120; i++) {
        ctx->Dispatch(capacity / 64, 1, 1);
    }

    readParticles(alive, highest);
    ctx->CopyResource(impactStaging, impact);
    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(impactStaging, 0, D3D11_MAP_READ, 0, &mapped);
    uint64_t total = 0;
    uint32_t texels = 0;

    for (uint32_t y = 0; y < 512; y++) {
        const uint32_t* line = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);

        for (uint32_t x = 0; x < 512; x++) {
            total += line[x];
            texels += line[x] ? 1 : 0;
        }
    }

    ctx->Unmap(impactStaging, 0);
    printf("  after 2.25 s: %u in the air, impacts on %u texels, %.3f yards of push in all\n", alive, texels, total / 1000000.0);
    CHECK(alive == 0);
    CHECK(texels > 20);
    CHECK(total > 100000 && total < 2000000);

    cb->Release();
    impactStaging->Release();
    impactUav->Release();
    impact->Release();
    particleStaging->Release();
    particleUav->Release();
    particles->Release();
    updateCs->Release();
    emitCs->Release();
    ctx->Release();
    device->Release();
}

int main(int argc, char** argv) {
    TestMonotoneCubic();
    TestCatmullRom();
    TestDome();
    TestPassesCompile();
    TestSimulation();
    TestTessellation();
    TestParticles();

    if (argc > 1) {
        TestTranslated(argv[1]);
        TestLinkage(argv[1], "vsLiquidWater", "psLiquidWater");
        TestLinkage(argv[1], "vsLiquidProcWater", "psLiquidProcWater");
    }

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
