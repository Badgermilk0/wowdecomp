#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include <cmath>
#include <cstring>
#include <new>
#include <dxgi1_5.h>
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Util.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/GxFrameLatency.h"
#include "Engine/Source/Base/FrameProfile.h"
#include "Engine/Source/Os/Error.h"
#include "Engine/Source/Storm/H/storm.h"

// MODIFIED: D3D11 device, see CGxDeviceD3d11.h. The window, the device and swap chain, present, caps and the cursor.

using namespace GxD3d11;

CGxDevice* CreateCGxDeviceD3d11() {
    void* mem = SMemAlloc(sizeof(CGxDeviceD3d11), __FILE__, __LINE__, 0);

    if (!mem) {
        return nullptr;
    }

    return new (mem) CGxDeviceD3d11();
}

CGxDeviceD3d11::CGxDeviceD3d11() {
    this->m_api = GxApi_D3d11;
    this->m_caps.m_colorFormat = GxCF_argb;
    this->m_caps.unk_0018 = 1;
    this->DeviceCreatePools();
    this->DeviceCreateStreamBufs();
}

CGxDeviceD3d11::~CGxDeviceD3d11() {
}

static ATOM WindowClassCreateD3d11() {
    HINSTANCE instance = GetModuleHandleA(nullptr);

    WNDCLASSEXA windowClass;
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = CGxDeviceD3d11::WindowProcD3d11;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = "GxWindowClassD3d11";
    windowClass.hIcon = static_cast<HICON>(LoadImageA(instance, "BlizzardIcon.ico", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    windowClass.hCursor = LoadCursorA(instance, "BlizzardCursor.cur");

    if (!windowClass.hCursor) {
        windowClass.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    }

    return RegisterClassExA(&windowClass);
}

int32_t CGxDeviceD3d11::DeviceCreate(GxWindowProc windowProc, const CGxFormat& format) {
    this->m_ownhwnd = 1;

    HDC dc = GetDC(nullptr);

    if (GetDeviceGammaRamp(dc, &this->m_systemGammaRamp) && &this->m_gammaRamp != &this->m_systemGammaRamp) {
        this->m_gammaRamp = this->m_systemGammaRamp;
    }

    ReleaseDC(nullptr, dc);

    this->m_hwndClass = WindowClassCreateD3d11();

    if (!this->m_hwndClass) {
        char* error = OsGetLastErrorStr();
        CGxDevice::Log("CGxDeviceD3d11::DeviceCreate(): WindowClassCreate() failed: %s", error);
        OsFreeLastErrorStr(error);
        this->DeviceDestroy();
        return 0;
    }

    if (this->IInitD3d11() && CGxDevice::DeviceCreate(windowProc, format)) {
        return 1;
    }

    this->DeviceDestroy();
    return 0;
}

int32_t CGxDeviceD3d11::DeviceCreate(void* window, const CGxFormat& format) {
    this->m_ownhwnd = 0;
    CGxDevice::DeviceCreate(window, format);
    this->m_hwnd = static_cast<HWND>(window);

    if (this->IInitD3d11() && this->ICreateD3d11Device(format) && CGxDevice::DeviceCreate(window, format)) {
        return 1;
    }

    this->DeviceDestroy();
    return 0;
}

int32_t CGxDeviceD3d11::IInitD3d11() {
    if (this->m_d3dDevice) {
        return 1;
    }

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    char debug[8] = {};

    // WOW_D3D11_DEBUG=1 turns on the debug layer (needs the Graphics Tools optional feature)
    if (GetEnvironmentVariableA("WOW_D3D11_DEBUG", debug, sizeof(debug)) && debug[0] == '1') {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    static const D3D_FEATURE_LEVEL s_levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, s_levels, 2, D3D11_SDK_VERSION, &this->m_d3dDevice, &this->m_featureLevel, &this->m_ctx);

    if (hr == E_INVALIDARG) {
        // a runtime without 11.1
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, s_levels + 1, 1, D3D11_SDK_VERSION, &this->m_d3dDevice, &this->m_featureLevel, &this->m_ctx);
    }

    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, s_levels, 2, D3D11_SDK_VERSION, &this->m_d3dDevice, &this->m_featureLevel, &this->m_ctx);
    }

    if (FAILED(hr)) {
        CGxDevice::Log("CGxDeviceD3d11::IInitD3d11(): D3D11CreateDevice failed (%08X); a feature level 11_0 GPU is required", static_cast<uint32_t>(hr));
        this->m_d3dDevice = nullptr;
        this->m_ctx = nullptr;
        return 0;
    }

    this->m_d3dDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&this->m_d3dDevice1));
    this->m_ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&this->m_ctx1));

    D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};

    if (this->m_d3dDevice1 && this->m_ctx1 && SUCCEEDED(this->m_d3dDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)))) {
        this->m_cbOffsets = options.ConstantBufferOffsetting && options.MapNoOverwriteOnDynamicConstantBuffer;
    }

    UINT support = 0;
    this->m_formatSupport4444 = SUCCEEDED(this->m_d3dDevice->CheckFormatSupport(DXGI_FORMAT_B4G4R4A4_UNORM, &support)) && (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);
    support = 0;
    this->m_formatSupport1555 = SUCCEEDED(this->m_d3dDevice->CheckFormatSupport(DXGI_FORMAT_B5G5R5A1_UNORM, &support)) && (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);
    support = 0;
    this->m_formatSupport565 = SUCCEEDED(this->m_d3dDevice->CheckFormatSupport(DXGI_FORMAT_B5G6R5_UNORM, &support)) && (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);
    this->m_formatSupport565Rt = this->m_formatSupport565 && (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET);

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;

    if (SUCCEEDED(this->m_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
        adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&this->m_factory));

        DXGI_ADAPTER_DESC desc;

        if (SUCCEEDED(adapter->GetDesc(&desc))) {
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, this->m_adapterName, sizeof(this->m_adapterName) - 1, nullptr, nullptr);
        }
    }

    SafeRelease(adapter);
    SafeRelease(dxgiDevice);

    if (!this->m_factory) {
        CGxDevice::Log("CGxDeviceD3d11::IInitD3d11(): no DXGI 1.2 factory");
        return 0;
    }

    IDXGIFactory5* factory5 = nullptr;

    if (SUCCEEDED(this->m_factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5)))) {
        BOOL tearing = FALSE;

        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing)))) {
            this->m_allowTearing = tearing != FALSE;
        }

        factory5->Release();
    }

    CGxDevice::Log("CGxDeviceD3d11: %s, feature level %x, constant buffer offsets %d, tearing %d", this->m_adapterName, this->m_featureLevel, this->m_cbOffsets, this->m_allowTearing);
    return 1;
}

// MODIFIED: per-monitor DPI (WinMain.cpp). A monitor's effective DPI (Windows 8.1+), 0 when unknown
static UINT MonitorDpi(HMONITOR monitor) {
    typedef HRESULT(WINAPI * GetDpiForMonitorProc)(HMONITOR, int, UINT*, UINT*);
    static GetDpiForMonitorProc s_getDpiForMonitor = nullptr;
    static bool s_looked = false;

    if (!s_looked) {
        s_looked = true;
        HMODULE shcore = LoadLibraryA("shcore.dll");
        s_getDpiForMonitor = shcore ? reinterpret_cast<GetDpiForMonitorProc>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
    }

    UINT dpiX = 0;
    UINT dpiY = 0;

    // MDT_EFFECTIVE_DPI
    if (!monitor || !s_getDpiForMonitor || FAILED(s_getDpiForMonitor(monitor, 0, &dpiX, &dpiY))) {
        return 0;
    }

    return dpiY;
}

// MODIFIED: per-monitor DPI. s_clientAdjust for a window of style on a monitor at dpi: the frame's size follows the
// monitor's scale, and AdjustWindowRectEx only knows the primary's. AdjustWindowRectExForDpi is Windows 10 1607+
static void SetClientAdjust(DWORD style, UINT dpi) {
    typedef BOOL(WINAPI * AdjustForDpiProc)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static AdjustForDpiProc s_adjustForDpi = reinterpret_cast<AdjustForDpiProc>(GetProcAddress(GetModuleHandleA("user32.dll"), "AdjustWindowRectExForDpi"));

    RECT rect = { 0, 0, 100, 100 };

    if (!dpi || !s_adjustForDpi || !s_adjustForDpi(&rect, style, FALSE, 0, dpi)) {
        AdjustWindowRectEx(&rect, style, FALSE, 0);
    }

    CGxDevice::s_clientAdjust.x = rect.right - rect.left - 100;
    CGxDevice::s_clientAdjust.y = rect.bottom - rect.top - 100;
}

int32_t CGxDeviceD3d11::ICreateWindow(CGxFormat& format) {
    HINSTANCE instance = GetModuleHandleA(nullptr);
    DWORD style;

    if (!format.m_window) {
        style = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU;
    } else if (format.m_maximize == 1) {
        style = WS_POPUP | WS_VISIBLE;
    } else if (format.m_maximize == 2) {
        style = WS_POPUP;
    } else {
        style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }

    float aspectRatio = 0.0f;

    if (format.m_aspect && format.m_size.x && format.m_size.y) {
        aspectRatio = static_cast<float>(format.m_size.x) / format.m_size.y;
    }

    // MODIFIED: display management. The monitor the window goes on: gxMonitor, else the one it was last on
    CiRect monitorRect;
    CiRect workRect;

    if (!CGxDevice::PickMonitor(&monitorRect, &workRect)) {
        monitorRect.minX = 0;
        monitorRect.minY = 0;
        monitorRect.maxX = GetSystemMetrics(SM_CXSCREEN);
        monitorRect.maxY = GetSystemMetrics(SM_CYSCREEN);
        workRect = monitorRect;
    }

    // MODIFIED: per-monitor DPI. The frame at that monitor's scale (stock: AdjustWindowRectEx, the primary's)
    RECT pickedRect = { monitorRect.minX, monitorRect.minY, monitorRect.maxX, monitorRect.maxY };
    SetClientAdjust(style, MonitorDpi(MonitorFromRect(&pickedRect, MONITOR_DEFAULTTONEAREST)));

    if (!format.m_window || format.m_maximize == 1) {
        // full screen is a borderless window over its monitor, at that monitor's resolution: flip-model swap chains
        // present there without an exclusive mode, so alt-tab and other windows behave as on the desktop. A maximized
        // window (gxMaximize) is the same window without the cursor clip; stock put it at 0,0 with the primary's size
        format.m_pos.x = monitorRect.minX;
        format.m_pos.y = monitorRect.minY;
        format.m_size.x = monitorRect.maxX - monitorRect.minX;
        format.m_size.y = monitorRect.maxY - monitorRect.minY;

        if (!format.m_window) {
            CGxDevice::s_clientAdjust.x = 0;
            CGxDevice::s_clientAdjust.y = 0;
        }
    } else {
        RECT windowRect;
        windowRect.left = 0;
        windowRect.top = 0;
        windowRect.right = format.m_size.x;
        windowRect.bottom = format.m_size.y;

        if (CGxDevice::AdjustWindowRectToWorkArea(aspectRatio, &CGxDevice::s_clientAdjust, &windowRect)) {
            format.m_pos.x = windowRect.left;
            format.m_pos.y = windowRect.top;
            format.m_size.x = windowRect.right - windowRect.left;
            format.m_size.y = windowRect.bottom - windowRect.top;
        } else {
            format.m_pos.x = 0;
            format.m_pos.y = 0;
        }

        // MODIFIED: display management. Back where the window was last time (gxWindowPos), if that is still on a
        // monitor (on gxMonitor's, when one is set), kept inside its work area; stock always centred on the primary
        if (CGxDevice::s_windowPosValid) {
            int32_t outerWidth = format.m_size.x + CGxDevice::s_clientAdjust.x;
            int32_t outerHeight = format.m_size.y + CGxDevice::s_clientAdjust.y;
            RECT saved = { CGxDevice::s_windowPos.x, CGxDevice::s_windowPos.y, CGxDevice::s_windowPos.x + outerWidth, CGxDevice::s_windowPos.y + outerHeight };
            HMONITOR savedMonitor = MonitorFromRect(&saved, MONITOR_DEFAULTTONULL);
            MONITORINFO info;
            info.cbSize = sizeof(info);

            if (savedMonitor && GetMonitorInfoA(savedMonitor, &info) && (CGxDevice::s_monitorIndex < 1 || (info.rcMonitor.left == monitorRect.minX && info.rcMonitor.top == monitorRect.minY))) {
                // the frame at the scale of the monitor the window goes back to
                SetClientAdjust(style, MonitorDpi(savedMonitor));
                outerWidth = format.m_size.x + CGxDevice::s_clientAdjust.x;
                outerHeight = format.m_size.y + CGxDevice::s_clientAdjust.y;

                // the invisible resize borders may hang over the work area's edge, as a window snapped there does
                int32_t border = CGxDevice::s_clientAdjust.x / 2;
                int32_t x = saved.left;
                int32_t y = saved.top;
                x = x > info.rcWork.right - outerWidth + border ? info.rcWork.right - outerWidth + border : x;
                x = x < info.rcWork.left - border ? info.rcWork.left - border : x;
                y = y > info.rcWork.bottom - outerHeight + border ? info.rcWork.bottom - outerHeight + border : y;
                y = y < info.rcWork.top ? info.rcWork.top : y;
                format.m_pos.x = x;
                format.m_pos.y = y;
            }
        }
    }

    int32_t width = format.m_size.x ? format.m_size.x : CW_USEDEFAULT;
    int32_t height = format.m_size.y ? format.m_size.y : CW_USEDEFAULT;

    if (format.m_window && format.m_maximize != 1 && format.m_size.x && format.m_size.y) {
        width += CGxDevice::s_clientAdjust.x;
        height += CGxDevice::s_clientAdjust.y;
    }

    this->m_hwnd = CreateWindowExA(WS_EX_APPWINDOW, "GxWindowClassD3d11", "World of Warcraft", style, format.m_pos.x, format.m_pos.y, width, height, nullptr, nullptr, instance, this);

    if (this->m_hwnd && format.m_maximize != 2) {
        ShowWindow(this->m_hwnd, SW_SHOWNORMAL);
    }

    return this->m_hwnd != nullptr;
}

// MODIFIED: performance diagnostics. The device whose GPU FrameProfile's section switches time (D3d11GpuTimer.h)
static CGxDeviceD3d11* s_gpuTimedDevice;

static void GpuTimerMark(FrameProfileSection section) {
    CGxDeviceD3d11* device = s_gpuTimedDevice;

    if (device && device->m_context && device->m_d3dDevice && device->m_ctx) {
        device->m_gpuTimer.Mark(device->m_d3dDevice, device->m_ctx, section);
    }
}

int32_t CGxDeviceD3d11::ICreateD3d11Device(const CGxFormat& format) {
    if (!this->m_d3dDevice || !this->m_hwnd) {
        return 0;
    }

    // multisampling: the format's count if the colour and depth formats take it, else the next lower that does
    this->m_samples = 1;
    this->m_sampleQuality = 0;

    for (uint32_t count = format.m_multisampleCount > 32 ? 32 : format.m_multisampleCount; count > 1; count--) {
        UINT colorLevels = 0;
        UINT depthLevels = 0;

        if (SUCCEEDED(this->m_d3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, count, &colorLevels)) && colorLevels && SUCCEEDED(this->m_d3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, count, &depthLevels)) && depthLevels) {
            this->m_samples = count;
            UINT levels = colorLevels < depthLevels ? colorLevels : depthLevels;
            float quality = format.m_multisampleQuality < 0.0f ? 0.0f : (format.m_multisampleQuality > 1.0f ? 1.0f : format.m_multisampleQuality);
            this->m_sampleQuality = static_cast<uint32_t>((levels - 1) * quality);
            break;
        }
    }

    RECT client;
    GetClientRect(this->m_hwnd, &client);
    uint32_t width = static_cast<uint32_t>(client.right - client.left);
    uint32_t height = static_cast<uint32_t>(client.bottom - client.top);

    if (!width || !height) {
        width = format.m_size.x > 0 ? format.m_size.x : 640;
        height = format.m_size.y > 0 ? format.m_size.y : 480;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = format.m_vsync && format.m_backBufferCount > 1 ? 3 : 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = this->m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    HRESULT hr = this->m_factory->CreateSwapChainForHwnd(this->m_d3dDevice, this->m_hwnd, &desc, nullptr, nullptr, &this->m_swapChain);

    if (FAILED(hr)) {
        CGxDevice::Log("CGxDeviceD3d11::ICreateD3d11Device(): CreateSwapChainForHwnd failed (%08X)", static_cast<uint32_t>(hr));
        this->m_swapChain = nullptr;
        return 0;
    }

    // the client toggles full screen itself
    this->m_factory->MakeWindowAssociation(this->m_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    this->m_frameWidth = width;
    this->m_frameHeight = height;

    if (!this->ICreateFrame()) {
        return 0;
    }

    this->m_context = 1;
    this->m_format = format;
    this->ISetCaps(format);
    this->Log(this->m_caps);
    CGxDevice::Log("\tD3D11: %u x %u, multisample %u (quality %u)", width, height, this->m_samples, this->m_sampleQuality);

    if (!this->m_presentVs) {
        std::string present = PresentHlsl();
        std::string blit = BlitHlsl();
        this->m_presentVs = this->IProgram(present, "PresentVs", "vs_5_0", 0);
        this->m_presentPs = this->IProgram(present, "PresentPs", "ps_5_0", 0);
        this->m_blitVs = this->IProgram(blit, "BlitVs", "vs_5_0", 0);
        this->m_blitPs = this->IProgram(blit, "BlitPs", "ps_5_0", 0);
        this->m_spriteGs = this->IProgram(PointSpriteHlsl(), "main", "gs_5_0", 0);
        std::string clear = ClearHlsl();
        this->m_clearVs = this->IProgram(clear, "ClearVs", "vs_5_0", 0);
        this->m_clearPs = this->IProgram(clear, "ClearPs", "ps_5_0", 0);
    }

    if (!this->m_presentVs || !this->m_presentPs) {
        CGxDevice::Log("CGxDeviceD3d11::ICreateD3d11Device(): the present shaders did not compile");
        return 0;
    }

    this->IStateSetDefaults();
    this->DeviceSetGamma(this->m_gammaRamp);
    this->ICursorCreate(format);

    if (!CGxDevice::s_uiVertexShader[0]) {
        g_theGxDevicePtr->ShaderCreate(CGxDevice::s_uiVertexShader, GxSh_Vertex, "Shaders\\Vertex", "UI", 2);
    }

    if (!CGxDevice::s_uiPixelShader) {
        g_theGxDevicePtr->ShaderCreate(&CGxDevice::s_uiPixelShader, GxSh_Pixel, "Shaders\\Pixel", "UI", 1);
    }

    // MODIFIED: performance diagnostics (D3d11GpuTimer.h)
    s_gpuTimedDevice = this;
    g_frameProfileGpuMark = GpuTimerMark;

    // MODIFIED: modern lighting (D3d11Lighting.h): the game drives it through g_gxLighting
    if (!this->m_lighting) {
        this->m_lighting = new D3d11Lighting(this);
    }

    g_gxLighting = this->m_lighting;

    return 1;
}

int32_t CGxDeviceD3d11::ICreateFrame() {
    uint32_t width = this->m_frameWidth;
    uint32_t height = this->m_frameHeight;

    if (FAILED(this->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&this->m_backBuffer))) || FAILED(this->m_d3dDevice->CreateRenderTargetView(this->m_backBuffer, nullptr, &this->m_backBufferRtv))) {
        CGxDevice::Log("CGxDeviceD3d11::ICreateFrame(): no back buffer");
        return 0;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = this->m_samples;
    desc.SampleDesc.Quality = this->m_sampleQuality;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | (this->m_samples == 1 ? D3D11_BIND_SHADER_RESOURCE : 0);

    if (FAILED(this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &this->m_frameColor)) || FAILED(this->m_d3dDevice->CreateRenderTargetView(this->m_frameColor, nullptr, &this->m_frameRtv))) {
        CGxDevice::Log("CGxDeviceD3d11::ICreateFrame(): no colour target (%u x %u, %u samples)", width, height, this->m_samples);
        return 0;
    }

    if (this->m_samples > 1) {
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &this->m_frameResolved))) {
            return 0;
        }
    } else {
        this->m_frameResolved = this->m_frameColor;
        this->m_frameResolved->AddRef();
    }

    if (FAILED(this->m_d3dDevice->CreateShaderResourceView(this->m_frameResolved, nullptr, &this->m_frameResolvedSrv))) {
        return 0;
    }

    // MODIFIED: modern lighting: typeless, so the SSAO and fog passes read it (D3d11Lighting)
    desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    desc.SampleDesc.Count = this->m_samples;
    desc.SampleDesc.Quality = this->m_sampleQuality;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = this->m_samples > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;

    if (FAILED(this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &this->m_frameDepth)) || FAILED(this->m_d3dDevice->CreateDepthStencilView(this->m_frameDepth, &dsvDesc, &this->m_frameDsv))) {
        CGxDevice::Log("CGxDeviceD3d11::ICreateFrame(): no depth buffer");
        return 0;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension = this->m_samples > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MipLevels = 1;

    if (FAILED(this->m_d3dDevice->CreateShaderResourceView(this->m_frameDepth, &depthSrvDesc, &this->m_frameDepthSrv))) {
        this->m_frameDepthSrv = nullptr;
    }

    if (this->m_lighting) {
        this->m_lighting->ReleaseTargets();
    }

    this->m_frameResolvedValid = false;
    this->IBindTargets();
    return 1;
}

void CGxDeviceD3d11::IReleaseFrame() {
    if (this->m_ctx) {
        this->m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* none[16] = {};
        this->m_ctx->PSSetShaderResources(0, 16, none);
    }

    this->m_curRtv = nullptr;
    this->m_curDsv = nullptr;
    this->m_bindingsValid = false;

    SafeRelease(this->m_frameResolvedSrv);
    SafeRelease(this->m_frameResolved);
    SafeRelease(this->m_frameRtv);
    SafeRelease(this->m_frameColor);
    SafeRelease(this->m_frameDepthSrv);
    SafeRelease(this->m_frameDsv);
    SafeRelease(this->m_frameDepth);

    // MODIFIED: modern lighting: the HDR scene and the passes' targets follow the frame's size
    if (this->m_lighting) {
        this->m_lighting->ReleaseTargets();
    }
    SafeRelease(this->m_backBufferRtv);
    SafeRelease(this->m_backBuffer);
    SafeRelease(this->m_staging);
    this->m_stagingWidth = 0;
    this->m_stagingHeight = 0;
}

int32_t CGxDeviceD3d11::IResizeFrame(uint32_t width, uint32_t height) {
    if (!this->m_swapChain || !width || !height) {
        return 0;
    }

    if (width == this->m_frameWidth && height == this->m_frameHeight && this->m_frameColor) {
        return 1;
    }

    this->IReleaseFrame();

    if (this->m_ctx) {
        this->m_ctx->Flush();
    }

    HRESULT hr = this->m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, this->m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

    if (FAILED(hr)) {
        CGxDevice::Log("CGxDeviceD3d11::IResizeFrame(): ResizeBuffers failed (%08X)", static_cast<uint32_t>(hr));
        this->m_context = 0;
        return 0;
    }

    this->m_frameWidth = width;
    this->m_frameHeight = height;

    if (!this->ICreateFrame()) {
        this->m_context = 0;
        return 0;
    }

    return 1;
}

void CGxDeviceD3d11::IReleaseSwapChain() {
    this->IReleaseFrame();

    for (AutoDepth& depth : this->m_autoDepths) {
        SafeRelease(depth.m_dsv);
        SafeRelease(depth.m_texture);
    }

    this->m_autoDepths.clear();

    if (this->m_ctx) {
        this->m_ctx->ClearState();
        this->m_ctx->Flush();
    }

    SafeRelease(this->m_swapChain);
    this->IInvalidateBindings();
}

void CGxDeviceD3d11::ISetCaps(const CGxFormat& format) {
    CGxCaps& caps = this->m_caps;

    caps.m_numTmus = 8;
    caps.m_pixelCenterOnEdge = 0;
    caps.m_texelCenterOnEdge = 1;
    caps.m_texMaxSize[GxTex_2d] = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    caps.m_texMaxSize[GxTex_CubeMap] = D3D11_REQ_TEXTURECUBE_DIMENSION;
    caps.m_texMaxSize[GxTex_Rectangle] = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    caps.m_texMaxSize[GxTex_NonPow2] = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    caps.m_maxIndex = 0xFFFFFF;
    caps.m_texFilterTrilinear = 1;
    caps.m_texFilterAnisotropic = 1;
    caps.m_maxTexAnisotropy = 16;
    caps.m_depthBias = 1;
    caps.m_numStreams = 16;
    caps.m_streamOffset = 1;

    for (uint32_t i = 0; i < GxShTargets_Last; i++) {
        caps.m_shaderTargets[i] = 0;
        caps.m_shaderConstants[i] = 0;
    }

    // the client's own shaders: shader model 3, translated (D3d11Dx9Shader)
    caps.m_shaderTargets[GxSh_Pixel] = 4;
    caps.m_shaderTargets[GxSh_Vertex] = 3;
    caps.m_shaderConstants[GxSh_Vertex] = 256;

    if (format.m_fixedFunction[GxSh_Pixel] != -1 && format.m_fixedFunction[GxSh_Pixel] <= caps.m_shaderTargets[GxSh_Pixel]) {
        caps.m_shaderTargets[GxSh_Pixel] = format.m_fixedFunction[GxSh_Pixel];
    }

    if (format.m_fixedFunction[GxSh_Vertex] != -1 && format.m_fixedFunction[GxSh_Vertex] <= caps.m_shaderTargets[GxSh_Vertex]) {
        caps.m_shaderTargets[GxSh_Vertex] = format.m_fixedFunction[GxSh_Vertex];
    }

    if (caps.m_shaderTargets[GxSh_Pixel] == 4 && caps.m_shaderTargets[GxSh_Vertex] != 3) {
        caps.m_shaderTargets[GxSh_Pixel] = 3;
    }

    // every format samples: what the hardware lacks is converted on upload
    caps.m_texFmt[GxTex_Unknown] = 0;

    for (uint32_t i = 1; i < GxTexFormats_Last; i++) {
        caps.m_texFmt[i] = 1;
    }

    caps.m_generateMipMaps = 1;

    for (uint32_t i = 0; i < GxTexFormats_Last; i++) {
        caps.m_rttFormat[i] = 0;
    }

    caps.m_rttFormat[GxTex_Argb8888] = 1;
    caps.m_rttFormat[GxTex_Rgb565] = 1;
    caps.m_rttFormat[GxTex_Gr1616F] = 1;
    caps.m_rttFormat[GxTex_R32F] = 1;
    caps.m_rttFormat[GxTex_D24X8] = 1;

    caps.m_colorWrite = 1;
    caps.m_hwCursor = 1;
    caps.m_maxClipPlanes = 6;
    caps.m_texTarget[GxTex_2d] = 1;
    caps.m_texTarget[GxTex_CubeMap] = 1;
    caps.m_texTarget[GxTex_Rectangle] = 0;
    caps.m_texTarget[GxTex_NonPow2] = 1;
    // full non-power-of-two support: FFX sizes its screen textures exactly (the D3D9 device's NVIDIA path)
    caps.m_texNonPow2 = 0;
    caps.m_occlusionQuery = 1;
    caps.m_pointParameters = 1;
    caps.m_pointScaleMax = 256.0f;
    caps.m_pointSprite = 1;
    caps.m_blendFactor = 1;
    caps.m_shaderAlphaRef = 1;

    for (uint32_t i = 0; i < 5; i++) {
        caps.unused_0114[i] = 0;
    }

    caps.m_oglRtAlpha = 0;
    caps.m_stereoAvailable = 0;
    caps.unk_0134 = caps.m_shaderTargets[GxSh_Pixel] != 4;
    caps.unk_0138 = caps.m_shaderTargets[GxSh_Pixel] != 4;
}

void CGxDeviceD3d11::IStateSetDefaults() {
    this->IInvalidateBindings();
    this->IRsForceUpdate();
    this->IRsSync(0);
    this->m_primVertexDirty = 0xFFFFFFFF;
    this->m_primIndexDirty = 0;
    this->InitLights();
    this->ShaderConstantsClear();
    this->m_vsConstantsDirty = true;
    this->m_psConstantsDirty = true;
    this->m_drawConstantsValid = false;
    this->m_ffConstantsValid = false;
    this->m_viewportDirty = 1;
    this->m_scissorRectDirty = 1;
    this->IBindTargets();
}

void CGxDeviceD3d11::ISetWindowFocus(int32_t focus) {
    this->m_windowFocus = focus;

    if (focus) {
        this->m_hwCursorDirty = true;

        if (!this->m_format.m_window) {
            RECT rect;
            GetWindowRect(this->m_hwnd, &rect);
            ClipCursor(&rect);
        }
    }
}

int32_t CGxDeviceD3d11::IFitToMonitor() {
    if (!this->m_hwnd || (this->m_format.m_window && this->m_format.m_maximize != 1)) {
        return 0;
    }

    MONITORINFO info;
    info.cbSize = sizeof(info);
    RECT window;

    if (GetMonitorInfoA(MonitorFromWindow(this->m_hwnd, MONITOR_DEFAULTTONEAREST), &info) && GetWindowRect(this->m_hwnd, &window) && !EqualRect(&window, &info.rcMonitor)) {
        SetWindowPos(this->m_hwnd, nullptr, info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // the full-screen cursor clip follows the window
    if (this->m_windowFocus) {
        this->ISetWindowFocus(1);
    }

    return 1;
}

int32_t CGxDeviceD3d11::DeviceSetFormat(const CGxFormat& format) {
    CGxDevice::Log("CGxDeviceD3d11::DeviceSetFormat():");
    this->Log(format);

    if (this->m_hwnd) {
        ShowWindow(this->m_hwnd, SW_HIDE);
    }

    // the device, and with it every texture, buffer and shader, outlives the format: only the window, the swap
    // chain and the frame are made again
    this->IReleaseSwapChain();

    // MODIFIED: display management. The new window opens on the monitor the old one was on (PickMonitor)
    RECT lastRect;

    if (this->m_hwnd && !IsIconic(this->m_hwnd) && GetWindowRect(this->m_hwnd, &lastRect)) {
        CGxDevice::s_lastWindowRect.minX = lastRect.left;
        CGxDevice::s_lastWindowRect.minY = lastRect.top;
        CGxDevice::s_lastWindowRect.maxX = lastRect.right;
        CGxDevice::s_lastWindowRect.maxY = lastRect.bottom;
        CGxDevice::s_lastWindowRectValid = 1;
    }

    if (this->m_hwnd && this->m_ownhwnd) {
        DestroyWindow(this->m_hwnd);
    }

    this->m_hwnd = nullptr;
    this->m_format = format;
    this->m_context = 0;

    CGxFormat createFormat = format;

    if (this->ICreateWindow(createFormat) && this->ICreateD3d11Device(createFormat) && CGxDevice::DeviceSetFormat(createFormat)) {
        this->ISetWindowFocus(1);
        return 1;
    }

    CGxDevice::Log("CGxDeviceD3d11::DeviceSetFormat(): unable to set format!");
    this->IReleaseSwapChain();

    if (this->m_hwnd) {
        DestroyWindow(this->m_hwnd);
    }

    this->m_hwnd = nullptr;
    this->m_context = 0;
    return 0;
}

void CGxDeviceD3d11::IReleaseD3d11Resources(int32_t freeAll) {
    this->ITexForceRecreation(freeAll);
    this->IReleasePools(freeAll);
    this->IReleaseQueries();

    // the shaders' variants point into m_programs
    for (uint32_t target = 0; target < GxShTargets_Last; target++) {
        TSHashTable<CGxShader, HASHKEY_STRI>& list = this->m_shaderList[target];

        for (CGxShader* shader = list.m_fulllist.Head(); shader; shader = list.m_fulllist.Next(shader)) {
            if (shader->m_apiSpecific) {
                delete static_cast<ShaderEntry*>(shader->m_apiSpecific);
                shader->m_apiSpecific = nullptr;
            }

            shader->m_loaded = 0;
        }
    }

    // MODIFIED: modern lighting: its shaders, buffers and targets; recreated on first use
    if (this->m_lighting) {
        this->m_lighting->ReleaseAll();
    }

    this->IReleasePrograms();
    this->IReleaseStateObjects();

    for (ID3D11Query*& query : this->m_frameQueries) {
        SafeRelease(query);
    }

    this->m_frameQueryIssued = 0;
    // MODIFIED: performance diagnostics (D3d11GpuTimer.h)
    this->m_gpuTimer.Release();
}

void CGxDeviceD3d11::IDestroyD3d11() {
    for (uint32_t i = 0; i < 2; i++) {
        if (CGxDevice::s_uiVertexShader[i]) {
            g_theGxDevicePtr->ShaderDestroy(CGxDevice::s_uiVertexShader[i]);
        }
    }

    if (CGxDevice::s_uiPixelShader) {
        g_theGxDevicePtr->ShaderDestroy(CGxDevice::s_uiPixelShader);
    }

    // MODIFIED: performance diagnostics (D3d11GpuTimer.h)
    if (s_gpuTimedDevice == this) {
        s_gpuTimedDevice = nullptr;
        g_frameProfileGpuMark = nullptr;
    }

    // MODIFIED: modern lighting
    if (this->m_lighting) {
        if (g_gxLighting == this->m_lighting) {
            g_gxLighting = nullptr;
        }

        delete this->m_lighting;
        this->m_lighting = nullptr;
    }

    this->ICursorDestroy();
    this->IReleaseSwapChain();
    this->IReleaseD3d11Resources(1);

    SafeRelease(this->m_gammaSrv);
    SafeRelease(this->m_gammaTexture);
    SafeRelease(this->m_utilityConstants);
    SafeRelease(this->m_ring);
    SafeRelease(this->m_fanIndices);

    for (ID3D11Buffer*& buffer : this->m_fallbackConstants) {
        SafeRelease(buffer);
    }

    if (this->m_ctx) {
        this->m_ctx->ClearState();
        this->m_ctx->Flush();
    }

    SafeRelease(this->m_factory);
    SafeRelease(this->m_ctx1);
    SafeRelease(this->m_ctx);
    SafeRelease(this->m_d3dDevice1);
    SafeRelease(this->m_d3dDevice);
    this->m_context = 0;
}

void CGxDeviceD3d11::DeviceDestroy() {
    CGxDevice::DeviceDestroy();

    if (this->m_hwnd && this->m_ownhwnd) {
        DestroyWindow(this->m_hwnd);
        this->m_hwnd = nullptr;
    }

    if (this->m_hwndClass) {
        UnregisterClassA(reinterpret_cast<const char*>(static_cast<uintptr_t>(this->m_hwndClass)), GetModuleHandleA(nullptr));
        this->m_hwndClass = 0;
    }

    this->IDestroyD3d11();
}

void CGxDeviceD3d11::DeviceEvictResources() {
}

void CGxDeviceD3d11::DeviceSetBaseMipLevel(uint32_t level) {
    this->ITexForceRecreation(1);
    CGxDevice::DeviceSetBaseMipLevel(level);
}

void CGxDeviceD3d11::DeviceSetGamma(float gamma) {
    CGxDevice::DeviceSetGamma(gamma);
    this->m_gammaDirty = true;
}

void CGxDeviceD3d11::DeviceSetGamma(const CGxGammaRamp& ramp) {
    CGxDevice::DeviceSetGamma(ramp);
    this->m_gammaDirty = true;
}

void* CGxDeviceD3d11::DeviceWindow() {
    return this->m_hwnd;
}

void CGxDeviceD3d11::DeviceOverride(EGxOverride override, uint32_t value) {
    CGxDevice::DeviceOverride(override, value);

    if (override == GxOverride_PixelShader) {
        this->m_caps.m_shaderTargets[GxSh_Pixel] = value;
    } else if (override == GxOverride_Unk7) {
        this->m_caps.m_texNonPow2 = value != 0;
    }
}

int32_t CGxDeviceD3d11::DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {
    switch (wm) {
        case GxWM_Size: {
            if (param2 == 1 || param2 == 2) {
                this->m_windowVisible = 0;
                break;
            }

            this->m_windowVisible = 1;
            const CRect& rect = *reinterpret_cast<const CRect*>(param1);
            this->DeviceSetDefWindow(rect);

            if (this->m_swapChain && this->m_context) {
                uint32_t width = static_cast<uint32_t>(rect.maxX - rect.minX);
                uint32_t height = static_cast<uint32_t>(rect.maxY - rect.minY);

                if (width && height && (width != this->m_frameWidth || height != this->m_frameHeight)) {
                    if (this->IResizeFrame(width, height)) {
                        this->ISetWindowFocus(1);
                        this->NotifyOnDeviceRestored();
                    }
                }
            }

            this->m_viewportDirty = 1;
            this->m_scissorRectDirty = 1;
            break;
        }

        case GxWM_DisplayChange:
            if (this->m_windowVisible) {
                this->DeviceSetDefWindow(*reinterpret_cast<const CRect*>(param1));
                this->m_viewportDirty = 1;
            }

            break;

        case GxWM_Destroy:
        case GxWM_KillFocus:
            this->m_windowFocus = 0;

            // full screen clips the cursor to the window on focus (ISetWindowFocus); a borderless window stays on the
            // desktop after alt-tab, so the clip, which is system-wide, goes with the focus
            if (!this->m_format.m_window) {
                ClipCursor(nullptr);
            }

            break;

        case GxWM_SetFocus:
            this->ISetWindowFocus(1);
            break;

        default:
            break;
    }

    return 0;
}

void CGxDeviceD3d11::CapsWindowSize(CRect& rect) {
    rect = this->DeviceCurWindow();
}

void CGxDeviceD3d11::CapsWindowSizeInScreenCoords(CRect& rect) {
    if (this->IDevIsWindowed()) {
        const CRect& window = this->DeviceCurWindow();
        POINT points[2];
        points[0].x = 0;
        points[0].y = 0;
        points[1].x = static_cast<LONG>(window.maxX);
        points[1].y = static_cast<LONG>(window.maxY);
        MapWindowPoints(this->m_hwnd, nullptr, points, 2);
        rect.minY = static_cast<float>(points[0].y);
        rect.minX = static_cast<float>(points[0].x);
        rect.maxY = static_cast<float>(points[1].y);
        rect.maxX = static_cast<float>(points[1].x);
    } else {
        rect = this->DeviceCurWindow();
    }
}

void CGxDeviceD3d11::LogCrashInfo(char* buffer, uint32_t size) {
    char line[260];
    SStrPrintf(line, sizeof(line), "Adapter: %s\r\n", this->m_adapterName);
    SStrPack(buffer, line, size);
    SStrPrintf(line, sizeof(line), "Feature level: %x, frame %u x %u, multisample %u\r\n", this->m_featureLevel, this->m_frameWidth, this->m_frameHeight, this->m_samples);
    SStrPack(buffer, line, size);
}

void CGxDeviceD3d11::IUpdateGamma() {
    if (!this->m_gammaTexture) {
        D3D11_TEXTURE1D_DESC desc = {};
        desc.Width = 256;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(this->m_d3dDevice->CreateTexture1D(&desc, nullptr, &this->m_gammaTexture)) || FAILED(this->m_d3dDevice->CreateShaderResourceView(this->m_gammaTexture, nullptr, &this->m_gammaSrv))) {
            SafeRelease(this->m_gammaTexture);
            return;
        }

        this->m_gammaDirty = true;
    }

    if (!this->m_gammaDirty) {
        return;
    }

    uint16_t texels[256 * 4];

    for (uint32_t i = 0; i < 256; i++) {
        texels[i * 4 + 0] = this->m_gammaRamp.m_red[i];
        texels[i * 4 + 1] = this->m_gammaRamp.m_green[i];
        texels[i * 4 + 2] = this->m_gammaRamp.m_blue[i];
        texels[i * 4 + 3] = 0xFFFF;
    }

    this->m_ctx->UpdateSubresource(this->m_gammaTexture, 0, nullptr, texels, sizeof(texels), sizeof(texels));
    this->m_gammaDirty = false;
}

void CGxDeviceD3d11::IResolveFrame() {
    if (this->m_frameResolvedValid || !this->m_frameColor) {
        return;
    }

    // MODIFIED: modern lighting: while the world draws into the HDR scene, that is the frame
    if (this->m_lighting && this->m_lighting->HdrActive()) {
        this->m_lighting->SceneSrv();
    } else if (this->m_samples > 1) {
        this->m_ctx->ResolveSubresource(this->m_frameResolved, 0, this->m_frameColor, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    }

    this->m_frameResolvedValid = true;
}

ID3D11RenderTargetView* CGxDeviceD3d11::IFrameRtv() const {
    if (this->m_lighting && this->m_lighting->HdrActive() && this->m_lighting->SceneRtv()) {
        return this->m_lighting->SceneRtv();
    }

    return this->m_frameRtv;
}

ID3D11ShaderResourceView* CGxDeviceD3d11::IFrameSrv() {
    this->IResolveFrame();

    if (this->m_lighting && this->m_lighting->HdrActive()) {
        return this->m_lighting->SceneSrvNoResolve();
    }

    return this->m_frameResolvedSrv;
}

void CGxDeviceD3d11::IPresentFrame() {
    if (!this->m_swapChain || !this->m_backBuffer) {
        return;
    }

    if (!this->m_format.m_window && this->m_presentVs && this->m_presentPs) {
        // full screen: the copy goes through the gamma ramp, as D3D9 set it on the display
        this->IUpdateGamma();
        this->IResolveFrame();

        float present[4] = { this->m_gammaSrv ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };

        if (!this->m_utilityConstants) {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = 64;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            this->m_d3dDevice->CreateBuffer(&desc, nullptr, &this->m_utilityConstants);
        }

        float constants[16] = {};
        memcpy(constants, present, sizeof(present));
        this->m_ctx->UpdateSubresource(this->m_utilityConstants, 0, nullptr, constants, 0, 0);

        D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(this->m_frameWidth), static_cast<float>(this->m_frameHeight), 0.0f, 1.0f };
        ID3D11ShaderResourceView* views[2] = { this->m_frameResolvedSrv, this->m_gammaSrv };
        ID3D11SamplerState* sampler = this->ISamplerState(CGxTexFlags(GxTex_Linear), false);

        this->m_ctx->OMSetRenderTargets(1, &this->m_backBufferRtv, nullptr);
        this->m_ctx->OMSetBlendState(this->IBlendState(0, 15), nullptr, 0xFFFFFFFF);
        this->m_ctx->OMSetDepthStencilState(this->IDepthState(D3D11_COMPARISON_ALWAYS, 0), 0);
        this->m_ctx->RSSetState(this->IRasterState(0, false, false, 0, false));
        this->m_ctx->RSSetViewports(1, &viewport);
        this->m_ctx->IASetInputLayout(nullptr);
        this->m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        this->m_ctx->VSSetShader(this->m_presentVs->m_vs, nullptr, 0);
        this->m_ctx->GSSetShader(nullptr, nullptr, 0);
        this->m_ctx->PSSetShader(this->m_presentPs->m_ps, nullptr, 0);
        this->m_ctx->PSSetShaderResources(0, 2, views);
        this->m_ctx->PSSetSamplers(0, 1, &sampler);
        this->m_ctx->PSSetConstantBuffers(0, 1, &this->m_utilityConstants);
        this->m_ctx->Draw(3, 0);

        ID3D11ShaderResourceView* none[2] = {};
        this->m_ctx->PSSetShaderResources(0, 2, none);
        this->IInvalidateBindings();
    } else if (this->m_samples > 1) {
        this->m_ctx->ResolveSubresource(this->m_backBuffer, 0, this->m_frameColor, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    } else {
        this->m_ctx->CopyResource(this->m_backBuffer, this->m_frameColor);
    }

    uint32_t sync = this->m_format.m_vsync > 4 ? 4 : this->m_format.m_vsync;
    UINT flags = !sync && this->m_allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT hr = this->m_swapChain->Present(sync, flags);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        HRESULT reason = this->m_d3dDevice->GetDeviceRemovedReason();
        CGxDevice::Log("CGxDeviceD3d11::IPresentFrame(): the device was removed (%08X, reason %08X)", static_cast<uint32_t>(hr), static_cast<uint32_t>(reason));
        this->m_context = 0;
    }

    // the flip model unbinds the back buffer; the frame's own targets stay
    this->IBindTargets();
}

void CGxDeviceD3d11::IFrameLatency() {
    // MODIFIED: performance, as CGxFrameLatency: wait for the frame gxMaxFrameLatency back, not the one just submitted
    const uint32_t ring = 4;
    uint32_t latency = g_gxMaxFrameLatency < ring ? g_gxMaxFrameLatency : ring - 1;
    uint32_t slot = this->m_frameQueryNext;

    if (!this->m_frameQueries[slot]) {
        D3D11_QUERY_DESC desc = { D3D11_QUERY_EVENT, 0 };

        if (FAILED(this->m_d3dDevice->CreateQuery(&desc, &this->m_frameQueries[slot]))) {
            this->m_frameQueries[slot] = nullptr;
            return;
        }
    }

    this->m_ctx->End(this->m_frameQueries[slot]);
    this->m_frameQueryIssued++;
    this->m_frameQueryNext = (slot + 1) % ring;

    if (this->m_frameQueryIssued <= latency) {
        return;
    }

    ID3D11Query* query = this->m_frameQueries[(slot + ring - latency) % ring];

    if (!query) {
        return;
    }

    BOOL done = FALSE;

    while (this->m_ctx->GetData(query, &done, sizeof(done), 0) == S_FALSE) {
        SwitchToThread();
    }
}

void CGxDeviceD3d11::ScenePresent() {
    if (this->m_context) {
        int32_t scrShotClick = this->m_scrShotClick;
        // MODIFIED: performance diagnostics (GxFrameStats)
        uint64_t frameStart = GxFrameStatsNow();

        CGxDevice::ScenePresent();
        this->ICursorDraw();
        // MODIFIED: performance diagnostics (D3d11GpuTimer.h)
        this->m_gpuTimer.EndFrame(this->m_d3dDevice, this->m_ctx);

        uint64_t gpuWaitStart = GxFrameStatsNow();

        if (this->m_format.m_fixLag) {
            this->IFrameLatency();
        }

        if (scrShotClick) {
            this->DeviceScreenShot();
        }

        uint64_t capStart = GxFrameStatsNow();
        this->WaitForFPSCap();
        uint64_t presentStart = GxFrameStatsNow();

        this->IPresentFrame();

        GxFrameStatsRecord(frameStart, gpuWaitStart, capStart, presentStart, GxFrameStatsNow());
    }

    // the next scene: D3D9's BeginScene, where the device forgets the constants the engine set
    this->ShaderConstantsClear();
    this->m_frameResolvedValid = false;
}

void CGxDeviceD3d11::SceneClear(uint32_t mask, CImVector color) {
    if (!this->m_context) {
        return;
    }

    if (this->m_viewportDirty) {
        this->IXformSetViewport();
    }

    // D3D9 clears the viewport's rectangle (and the scissor's, when the test is on); D3D11 clears whole views, so a
    // partial viewport is cleared by drawing
    const D3D11_VIEWPORT& viewport = this->m_boundViewport;
    bool partial = viewport.TopLeftX > 0.5f || viewport.TopLeftY > 0.5f || viewport.Width + viewport.TopLeftX < this->m_targetWidth - 0.5f || viewport.Height + viewport.TopLeftY < this->m_targetHeight - 0.5f;
    bool scissor = this->m_appRenderStates[GxRs_ScissorTest].m_value.m_data.i[0] != 0;

    if ((partial || scissor) && this->m_clearVs && this->m_clearPs) {
        this->IClearRect(mask, color);
        this->m_frameResolvedValid = false;
        return;
    }

    if ((mask & 0x1) && this->m_curRtv) {
        float rgba[4] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
        this->m_ctx->ClearRenderTargetView(this->m_curRtv, rgba);
    }

    if ((mask & 0x2) && this->m_curDsv) {
        this->m_ctx->ClearDepthStencilView(this->m_curDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    this->m_frameResolvedValid = false;
}

void CGxDeviceD3d11::XformSetProjection(const C44Matrix& matrix) {
    CGxDevice::XformSetProjection(matrix);

    // the D3D9 device's native projection: D3D11 shares D3D9's depth range
    C44Matrix projNative = matrix;

    if (!(fabs(projNative.c3 - 1.0f) < 0.00000023841858f) && !(fabs(projNative.c3) < 0.00000023841858f)) {
        projNative *= 1.0f / projNative.c3;
    }

    if (projNative.d3 == 0.0f) {
        float minZ = -(projNative.d2 / (projNative.c2 + 1.0f));
        float maxZ = -(projNative.d2 / (projNative.c2 - 1.0f));
        projNative.c2 = maxZ / (maxZ - minZ);
        projNative.d2 = maxZ * minZ / (minZ - maxZ);
    } else {
        float invC2 = 1.0f / projNative.c2;
        float minZ = (-1.0f - projNative.d2) * invC2;
        float maxZ = (1.0f - projNative.d2) * invC2;
        projNative.c2 = 1.0f / (maxZ - minZ);
        projNative.d2 = minZ / (minZ - maxZ);
    }

    if (!(this->m_appMasterEnables & (1 << GxMasterEnable_NormalProjection)) && projNative.d3 != 1.0f) {
        C44Matrix scale;
        scale.a0 = 0.2f;
        scale.b1 = 0.2f;
        scale.c2 = 0.2f;
        projNative = projNative * scale;
    }

    this->m_xforms[GxXform_Projection].m_dirty = 1;
    this->m_projNative = projNative;
}

void CGxDeviceD3d11::XformSetView(const C44Matrix& matrix) {
    CGxDevice::XformSetView(matrix);
}

void CGxDeviceD3d11::CursorSetVisible(int32_t visible) {
    CGxDevice::CursorSetVisible(visible);

    if (this->m_hwCursor && this->m_context) {
        POINT point;
        GetCursorPos(&point);
        ScreenToClient(this->m_hwnd, &point);

        RECT rect;
        GetClientRect(this->m_hwnd, &rect);

        if (point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom) {
            SetCursor(this->m_cursorVisible ? this->m_hwCursorHandle : nullptr);
        }
    }
}

void CGxDeviceD3d11::CursorUnlock(uint32_t x, uint32_t y) {
    CGxDevice::CursorUnlock(x, y);
    this->m_hwCursorDirty = true;
}

void CGxDeviceD3d11::ICursorCreate(const CGxFormat& format) {
    CGxDevice::ICursorCreate(format);
    this->m_hwCursorDirty = true;
}

void CGxDeviceD3d11::ICursorDestroy() {
    CGxDevice::ICursorDestroy();

    if (this->m_hwCursorHandle) {
        if (GetCursor() == this->m_hwCursorHandle) {
            SetCursor(nullptr);
        }

        DestroyCursor(this->m_hwCursorHandle);
        this->m_hwCursorHandle = nullptr;
    }
}

void CGxDeviceD3d11::ICursorDraw() {
    // the software cursor draws through the device like anything else
    CGxDevice::ICursorDraw();

    if (!this->m_hwCursor || !this->m_hwCursorDirty || !this->m_context) {
        return;
    }

    // the hardware cursor: D3D9's SetCursorProperties is a Windows cursor made from the 32 x 32 ARGB image
    BITMAPV5HEADER header = {};
    header.bV5Size = sizeof(header);
    header.bV5Width = 32;
    header.bV5Height = -32;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);

    if (!color || !bits) {
        if (color) {
            DeleteObject(color);
        }

        return;
    }

    memcpy(bits, this->m_cursor, 32 * 32 * sizeof(uint32_t));

    static const uint8_t s_mask[32 * 32 / 8] = {};
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, s_mask);

    ICONINFO info = {};
    info.fIcon = FALSE;
    info.xHotspot = this->m_cursorHotspotX;
    info.yHotspot = this->m_cursorHotspotY;
    info.hbmMask = mask;
    info.hbmColor = color;
    HCURSOR cursor = reinterpret_cast<HCURSOR>(CreateIconIndirect(&info));

    DeleteObject(color);
    DeleteObject(mask);

    if (!cursor) {
        return;
    }

    HCURSOR old = this->m_hwCursorHandle;
    this->m_hwCursorHandle = cursor;
    this->m_hwCursorDirty = false;

    POINT point;
    GetCursorPos(&point);

    if (WindowFromPoint(point) == this->m_hwnd) {
        RECT rect;
        GetClientRect(this->m_hwnd, &rect);
        ScreenToClient(this->m_hwnd, &point);

        if (point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom) {
            SetCursor(this->m_cursorVisible ? cursor : nullptr);
        }
    }

    if (old) {
        DestroyCursor(old);
    }
}

void CGxDeviceD3d11::StereoSetConvergence(float convergence) {
}

float CGxDeviceD3d11::StereoGetConvergence() {
    return 1.0f;
}

void CGxDeviceD3d11::StereoSetSeparation(float separation) {
}

float CGxDeviceD3d11::StereoGetSeparation() {
    return 25.0f;
}

bool CGxDeviceD3d11::StereoEnabled() {
    return false;
}

void CGxDeviceD3d11::DeviceUnk81(uint32_t a, uint32_t b) {
}

void CGxDeviceD3d11::DeviceUnk82() {
}

void CGxDeviceD3d11::DeviceUnk83(uint32_t a, uint32_t b) {
}

LRESULT CALLBACK CGxDeviceD3d11::WindowProcD3d11(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    CGxDeviceD3d11* device = reinterpret_cast<CGxDeviceD3d11*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_CREATE:
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTA*>(lparam)->lpCreateParams));
            return 0;

        case WM_DESTROY:
            if (device) {
                device->DeviceWM(GxWM_Destroy, 0, 0);
            }

            return 0;

        case WM_SIZE: {
            if (!device) {
                break;
            }

            CRect rect;
            rect.minY = 0.0f;
            rect.minX = 0.0f;
            rect.maxY = HIWORD(lparam);
            rect.maxX = LOWORD(lparam);

            uint32_t resizeType = 0;

            if (wparam == SIZE_MINIMIZED) {
                resizeType = 1;
            } else if (wparam == SIZE_MAXHIDE) {
                resizeType = 2;
            }

            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), resizeType);
            break;
        }

        case WM_SETFOCUS:
            if (device) {
                device->DeviceWM(GxWM_SetFocus, 0, 0);
            }

            return 0;

        case WM_KILLFOCUS:
            if (device) {
                device->DeviceWM(GxWM_KillFocus, 0, 0);
            }

            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_ERASEBKGND:
            return 0;

        case WM_SETCURSOR:
            if (!device) {
                return DefWindowProcA(hwnd, message, wparam, lparam);
            }

            if (device->m_d3dDevice && LOWORD(lparam) == HTCLIENT) {
                SetCursor(device->m_cursorVisible && device->m_hwCursor ? device->m_hwCursorHandle : nullptr);
                return 1;
            }

            break;

        case WM_DISPLAYCHANGE:
            // MODIFIED: display management. A full-screen or maximized window covers its monitor again after the
            // monitor's resolution changed or it was unplugged; the move resizes the frame through WM_SIZE. Stock
            // took the new size from lparam, which is the primary monitor's
            if (device && device->IFitToMonitor()) {
                break;
            }

            if (device && !device->IDevIsWindowed()) {
                CRect rect;
                rect.minY = 0.0f;
                rect.minX = 0.0f;
                rect.maxY = HIWORD(lparam);
                rect.maxX = LOWORD(lparam);
                device->DeviceWM(GxWM_DisplayChange, reinterpret_cast<uintptr_t>(&rect), 0);
            }

            break;

        // MODIFIED: per-monitor DPI (WinMain.cpp). Moved to a monitor with another scale, a window takes the size
        // Windows suggests, which keeps it the same size on screen; a full-screen or maximized one covers the new monitor
        case WM_DPICHANGED: {
            if (device && device->IFitToMonitor()) {
                return 0;
            }

            // the frame at the new scale, for the aspect-keeping resize (WM_SIZING)
            SetClientAdjust(static_cast<DWORD>(GetWindowLongA(hwnd, GWL_STYLE)), HIWORD(wparam));

            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_SYSCOMMAND:
            if (wparam == SC_SCREENSAVE || wparam == SC_MONITORPOWER) {
                return 0;
            }

            break;

        case WM_SIZING: {
            RECT* sizingRect = reinterpret_cast<RECT*>(lparam);

            CiRect rect;
            rect.minX = sizingRect->left;
            rect.minY = sizingRect->top;
            rect.maxX = sizingRect->right;
            rect.maxY = sizingRect->bottom;

            if (rect.maxX - rect.minX <= 0 || rect.maxY - rect.minY <= 0) {
                return 0;
            }

            switch (wparam) {
                case WMSZ_LEFT:
                    CGxDevice::ClampRectLeft(rect);
                    CGxDevice::ClampRectTopByAspect(rect);
                    break;

                case WMSZ_RIGHT:
                    CGxDevice::ClampRectRight(rect);
                    CGxDevice::ClampRectBottomByAspect(rect);
                    break;

                case WMSZ_TOP:
                    CGxDevice::ClampRectTop(rect);
                    CGxDevice::ClampRectLeftByAspect(rect);
                    break;

                case WMSZ_TOPLEFT:
                    CGxDevice::ClampRectTop(rect);
                    CGxDevice::ClampRectLeft(rect);
                    CGxDevice::ClampRectTopByAspect(rect);
                    CGxDevice::ClampRectLeftByAspect(rect);
                    break;

                case WMSZ_TOPRIGHT:
                    CGxDevice::ClampRectTop(rect);
                    CGxDevice::ClampRectRight(rect);
                    CGxDevice::ClampRectTopByAspect(rect);
                    CGxDevice::ClampRectRightByAspect(rect);
                    break;

                case WMSZ_BOTTOM:
                    CGxDevice::ClampRectBottom(rect);
                    CGxDevice::ClampRectRightByAspect(rect);
                    break;

                case WMSZ_BOTTOMLEFT:
                    CGxDevice::ClampRectBottom(rect);
                    CGxDevice::ClampRectLeft(rect);
                    CGxDevice::ClampRectLeftByAspect(rect);
                    CGxDevice::ClampRectBottomByAspect(rect);
                    break;

                case WMSZ_BOTTOMRIGHT:
                    CGxDevice::ClampRectBottom(rect);
                    CGxDevice::ClampRectRight(rect);
                    CGxDevice::ClampRectBottomByAspect(rect);
                    CGxDevice::ClampRectRightByAspect(rect);
                    break;

                default:
                    break;
            }

            sizingRect->left = rect.minX;
            sizingRect->bottom = rect.maxY;
            sizingRect->top = rect.minY;
            sizingRect->right = rect.maxX;

            return 1;
        }

        default:
            break;
    }

    if (device && device->m_windowProc) {
        return device->m_windowProc(hwnd, message, wparam, lparam);
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}
