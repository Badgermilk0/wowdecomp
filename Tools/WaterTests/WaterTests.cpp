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
    desc.Format = DXGI_FORMAT_R16G16_FLOAT;
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

    auto halfToFloat = [](uint16_t h) {
        uint32_t sign = (h >> 15) & 1;
        int32_t exp = (h >> 10) & 0x1F;
        uint32_t mant = h & 0x3FF;
        float value = exp == 0 ? mant / 1024.0f * powf(2.0f, -14.0f) : (exp == 31 ? INFINITY : (1.0f + mant / 1024.0f) * powf(2.0f, exp - 15.0f));
        return sign ? -value : value;
    };

    // the heights along the row through the centre, x the height now
    auto readRow = [&](std::vector<float>& row, bool& finite) {
        ctx->CopyResource(staging, field[current]);
        D3D11_MAPPED_SUBRESOURCE mapped;
        ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        row.assign(n, 0.0f);
        finite = true;

        for (uint32_t y = 0; y < n; y++) {
            const uint16_t* line = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);

            for (uint32_t x = 0; x < n; x++) {
                float h = halfToFloat(line[x * 2]);
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

    printf("  12 s after the splash: largest height %.5f yd\n", late);
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
    CHECK(rainMax > 0.0f && rainMax < 0.3f);

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
    CHECK(GxD3d11::FamilyForShader("vsLiquidProcWater", true) == GxD3d11::Family_None);
    CHECK(GxD3d11::FamilyForShader("Terrain", true) == GxD3d11::Family_Terrain);
}

int main(int argc, char** argv) {
    TestMonotoneCubic();
    TestCatmullRom();
    TestDome();
    TestPassesCompile();
    TestSimulation();

    if (argc > 1) {
        TestTranslated(argv[1]);
    }

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
