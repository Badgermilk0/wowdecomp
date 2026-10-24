#ifndef ENGINE_SOURCE_GX_CGXDEVICED3D11_CGXDEVICED3D11_H
#define ENGINE_SOURCE_GX_CGXDEVICED3D11_CGXDEVICED3D11_H

// MODIFIED: D3D11 device (gxApi "D3D11", or -d3d11 on the command line). Not in the 3.3.5a binary, whose enum only
// names the API. It stands in for CGxDeviceD3d9Ex behind the same CGxDevice interface and reproduces what the D3D9
// device does, rather than what D3D11 would do by default:
//
//  - shaders: the client's DX9 bytecode is translated to HLSL (D3d11Dx9Shader), the fixed-function pipeline is
//    generated from its state (D3d11FixedFunction); both compile through D3d11Compiler, cached on disk
//  - rasterisation: every vertex moves half a pixel so the pixels covered are D3D9's (texelCenterOnEdge as D3D9)
//  - the frame: an offscreen colour target (multisampled when the format asks) copied to a flip-model swap chain
//    at present. Full screen is a borderless window over the monitor; the gamma ramp is applied in that copy
//  - render targets, viewport and scissor (including the unflipped y of a texture target), buffer locking with
//    D3D9's discard / no-overwrite rules, texture uploads through the engine's texel callbacks, the D3D9 hardware
//    shadow map (a depth texture compared at the coordinate's z), occlusion queries, gxFixLag
//
// Not done here: stereo (NVAPI), DeviceReadDepths (zero, as in the D3D9 device), and awesome_wotlk's frame capture,
// which calls IDirect3DDevice9 itself and stays off on this device. (MSDF fonts bind their atlas pages through texture
// states 12-15 on this device.)

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Compiler.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Dx9Shader.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11FixedFunction.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11GpuTimer.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Lighting.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Water.h"
#include "Engine/Source/Gx/CGxTex.h"

class CGxDeviceD3d11 : public CGxDevice {
    public:
    // a compiled shader; one per distinct HLSL text, shared by every engine shader and fixed-function key that
    // writes it
    struct Program {
        ID3D11VertexShader* m_vs = nullptr;
        ID3D11PixelShader* m_ps = nullptr;
        ID3D11GeometryShader* m_gs = nullptr;
        std::vector<uint8_t> m_bytecode;     // vertex shaders keep theirs for the input layouts
        uint64_t m_id = 0;
        uint32_t m_constCount = 0;           // the C[] registers it reads (16 or more)
    };

    // CGxShader::m_apiSpecific
    struct ShaderEntry {
        GxD3d11::Dx9Shader m_dx9;
        std::vector<std::pair<uint32_t, Program*>> m_variants;
    };

    // CGxTex::m_apiSpecificData
    struct Texture {
        enum EConvert : uint8_t {
            Convert_None = 0,
            Convert_DecodeDxt = 1,     // block compressed, but mip 0 is not a multiple of 4 (D3D11 needs that)
            Convert_4444 = 2,          // 16-bit formats the device cannot sample, widened to 8888
            Convert_1555 = 3,
            Convert_565 = 4,
        };

        ID3D11Resource* m_resource = nullptr;
        ID3D11ShaderResourceView* m_srv = nullptr;
        ID3D11RenderTargetView* m_rtv[6] = {};
        ID3D11DepthStencilView* m_dsv = nullptr;
        DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_levels = 1;
        uint32_t m_baseMip = 0;
        bool m_cube = false;
        bool m_depth = false;
        bool m_autoMips = false;
        bool m_mipsDirty = false;
        uint8_t m_convert = Convert_None;
    };

    // CGxPool::m_apiSpecific
    struct Pool {
        ID3D11Buffer* m_buffer = nullptr;
        uint32_t m_size = 0;
        bool m_dynamic = false;
        bool m_discard = false;
    };

    // a depth buffer for colour texture targets of one size (D3D11 binds colour and depth of equal size only)
    struct AutoDepth {
        uint32_t m_width;
        uint32_t m_height;
        ID3D11Texture2D* m_texture;
        ID3D11DepthStencilView* m_dsv;
    };

    // the vertex layout of the draw, as D3D11 input elements
    struct VertexLayout {
        D3D11_INPUT_ELEMENT_DESC m_elements[GxVAs_Last];
        uint32_t m_count = 0;
        uint32_t m_attribMask = 0;
        uint32_t m_attribClass = 0;
        uint8_t m_texCoordSize[8] = {};
        CGxBuf* m_streams[GxVAs_Last] = {};
        uint32_t m_strides[GxVAs_Last] = {};
        uint32_t m_streamCount = 0;
        uint64_t m_hash = 0;
    };

    // the per-draw state every generated shader reads (cbuffer GxDraw, b1)
    struct DrawConstants {
        float m_viewport[4];
        float m_clipPlanes[6][4];
        float m_alphaTest[4];
        float m_fog[4];
        float m_pointSize[4];
        float m_pointScale[4];
        uint32_t m_clipMask[4];
        // MODIFIED: modern lighting: x the largest colour a pixel shader writes into the HDR scene (0 = no clamp)
        float m_output[4];
        // MODIFIED: sun shadows (D3d11Lighting): x 1 = the draw samples the sun's shadow maps outdoors, so a relit
        // pixel shades the sun itself; y how much deeper the stock shadow taps go elsewhere (1 = stock)
        float m_shadow[4];
    };

    // the stage a constant buffer belongs to
    enum EStage {
        Stage_Vertex = 0,
        Stage_Geometry = 1,
        Stage_Pixel = 2,
        Stages_Last = 3,
    };

    // a range of a constant buffer bound to one slot
    struct ConstantSlice {
        ID3D11Buffer* m_buffer = nullptr;
        uint32_t m_first = 0;
        uint32_t m_count = 0;
    };

    // which constants an upload carries
    enum EConstants {
        Constants_Vertex = 0,
        Constants_Pixel = 1,
        Constants_Draw = 2,
        Constants_Fixed = 3,
        Constants_Last = 4,
    };

    struct PairHash {
        size_t operator()(const std::pair<uint64_t, uint64_t>& key) const {
            return static_cast<size_t>(key.first * 0x9E3779B97F4A7C15ull ^ key.second);
        }
    };

    struct Hash128Hash {
        size_t operator()(const GxD3d11::Hash128& key) const {
            return static_cast<size_t>(key.m_lo ^ (key.m_hi * 0x9E3779B97F4A7C15ull));
        }
    };

    // window
    HWND m_hwnd = nullptr;
    ATOM m_hwndClass = 0;
    int32_t m_ownhwnd = 0;

    // device
    ID3D11Device* m_d3dDevice = nullptr;
    ID3D11Device1* m_d3dDevice1 = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    ID3D11DeviceContext1* m_ctx1 = nullptr;
    IDXGIFactory2* m_factory = nullptr;
    IDXGISwapChain1* m_swapChain = nullptr;
    D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool m_allowTearing = false;
    bool m_cbOffsets = false;
    bool m_formatSupport4444 = false;
    bool m_formatSupport1555 = false;
    bool m_formatSupport565 = false;
    bool m_formatSupport565Rt = false;
    char m_adapterName[128] = {};

    // the frame
    ID3D11Texture2D* m_backBuffer = nullptr;
    ID3D11RenderTargetView* m_backBufferRtv = nullptr;
    ID3D11Texture2D* m_frameColor = nullptr;
    ID3D11RenderTargetView* m_frameRtv = nullptr;
    ID3D11Texture2D* m_frameResolved = nullptr;      // the single-sample frame (m_frameColor without multisampling)
    ID3D11ShaderResourceView* m_frameResolvedSrv = nullptr;
    bool m_frameResolvedValid = false;
    ID3D11Texture2D* m_frameDepth = nullptr;
    ID3D11DepthStencilView* m_frameDsv = nullptr;
    uint32_t m_frameWidth = 0;
    uint32_t m_frameHeight = 0;
    uint32_t m_samples = 1;
    uint32_t m_sampleQuality = 0;
    std::vector<AutoDepth> m_autoDepths;
    ID3D11Texture2D* m_staging = nullptr;
    uint32_t m_stagingWidth = 0;
    uint32_t m_stagingHeight = 0;
    ID3D11DepthStencilView* m_savedDsv = nullptr;
    ID3D11RenderTargetView* m_curRtv = nullptr;
    ID3D11DepthStencilView* m_curDsv = nullptr;
    uint32_t m_targetWidth = 0;
    uint32_t m_targetHeight = 0;
    CGxTex* m_mipsPending = nullptr;

    // present
    ID3D11Texture1D* m_gammaTexture = nullptr;
    ID3D11ShaderResourceView* m_gammaSrv = nullptr;
    bool m_gammaDirty = true;
    ID3D11Buffer* m_utilityConstants = nullptr;
    Program* m_presentVs = nullptr;
    Program* m_presentPs = nullptr;
    Program* m_blitVs = nullptr;
    Program* m_blitPs = nullptr;
    Program* m_spriteGs = nullptr;
    Program* m_clearVs = nullptr;
    Program* m_clearPs = nullptr;
    ID3D11Query* m_frameQueries[4] = {};
    uint32_t m_frameQueryNext = 0;
    uint32_t m_frameQueryIssued = 0;
    // MODIFIED: performance diagnostics, GPU time per FrameProfile section (D3d11GpuTimer.h)
    D3d11GpuTimer m_gpuTimer;
    // MODIFIED: modern lighting (D3d11Lighting.h): the passes and the HDR scene; the frame's depth as a texture for
    // them; whether the lighting constants and buffers are bound since the last invalidation
    D3d11Lighting* m_lighting = nullptr;
    // MODIFIED: modern water (D3d11Water.h): the ripple height field, the copies for the liquids, the water constants
    D3d11Water* m_water = nullptr;
    // a water draw bound the hull and domain shaders (D3d11Water::BindTessellation); Draw unbinds them after it
    bool m_tessBound = false;
    ID3D11ShaderResourceView* m_frameDepthSrv = nullptr;
    bool m_lightingBound = false;
    // MODIFIED: sun shadows: the draw being prepared samples the sun's cascades (1) or the interior map (2), else 0
    uint32_t m_drawShadowKind = 0;

    // shaders and the state objects, by key
    std::unordered_map<GxD3d11::Hash128, Program*, Hash128Hash> m_programs;
    std::unordered_map<GxD3d11::Hash128, Program*, Hash128Hash> m_ffVertex;
    std::unordered_map<GxD3d11::Hash128, Program*, Hash128Hash> m_ffPixel;
    std::unordered_map<std::pair<uint64_t, uint64_t>, ID3D11InputLayout*, PairHash> m_inputLayouts;
    std::unordered_map<uint32_t, ID3D11BlendState*> m_blendStates;
    std::unordered_map<uint32_t, ID3D11DepthStencilState*> m_depthStates;
    std::unordered_map<uint64_t, ID3D11RasterizerState*> m_rasterStates;
    std::unordered_map<uint32_t, ID3D11SamplerState*> m_samplerStates;
    // MODIFIED: texLodBias, the bias the cached samplers were made with
    float m_samplerBias = 0.0f;
    uint64_t m_nextProgramId = 1;

    // constants: a ring the draws take slices of (D3D11.1 offsets), or one buffer per binding without them
    ID3D11Buffer* m_ring = nullptr;
    uint32_t m_ringSize = 0;
    uint32_t m_ringOffset = 0;
    bool m_ringDiscard = true;
    ID3D11Buffer* m_fallbackConstants[Constants_Last] = {};
    // the D3D9 constant registers as the hardware would hold them (CGxDevice::s_shadowConstants is only the dirty
    // window the engine wrote since the last flush)
    float m_vsConstants[256 * 4] = {};
    float m_psConstants[256 * 4] = {};
    bool m_vsConstantsDirty = true;
    bool m_psConstantsDirty = true;
    ConstantSlice m_vsSlice;
    ConstantSlice m_psSlice;
    ConstantSlice m_drawSlice;
    ConstantSlice m_ffSlice;
    DrawConstants m_drawConstants = {};
    bool m_drawConstantsValid = false;
    GxD3d11::FfConstants m_ffConstants = {};
    bool m_ffConstantsValid = false;
    bool m_pixelFixed = false;

    // what is bound, to skip redundant calls (nullptr / ~0 after IInvalidateBindings)
    ID3D11VertexShader* m_boundVs = nullptr;
    ID3D11PixelShader* m_boundPs = nullptr;
    ID3D11GeometryShader* m_boundGs = nullptr;
    ID3D11InputLayout* m_boundLayout = nullptr;
    ID3D11BlendState* m_boundBlend = nullptr;
    float m_boundBlendFactor = -1.0f;
    ID3D11DepthStencilState* m_boundDepth = nullptr;
    ID3D11RasterizerState* m_boundRaster = nullptr;
    ID3D11ShaderResourceView* m_boundSrv[16] = {};
    ID3D11SamplerState* m_boundSampler[16] = {};
    ID3D11Buffer* m_boundVb[GxVAs_Last] = {};
    uint32_t m_boundVbOffset[GxVAs_Last] = {};
    uint32_t m_boundVbStride[GxVAs_Last] = {};
    ID3D11Buffer* m_boundIb = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY m_boundTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    // MODIFIED: modern lighting: slots 0-4 (4: the vertex constants again, for a lit pixel shader)
    ID3D11Buffer* m_boundConstants[Stages_Last][5] = {};
    uint32_t m_boundConstantsFirst[Stages_Last][5] = {};
    uint32_t m_boundConstantsCount[Stages_Last][5] = {};
    D3D11_VIEWPORT m_boundViewport = {};
    D3D11_RECT m_boundScissor = {};
    bool m_bindingsValid = false;

    // the last fixed-function keys and input layout: consecutive draws mostly share them
    GxD3d11::FfVertexKey m_lastFfVertexKey = {};
    Program* m_lastFfVertex = nullptr;
    GxD3d11::FfPixelKey m_lastFfPixelKey = {};
    Program* m_lastFfPixel = nullptr;
    uint64_t m_lastLayoutProgram = 0;
    uint64_t m_lastLayoutHash = 0;
    ID3D11InputLayout* m_lastLayout = nullptr;

    // the draw being prepared
    VertexLayout m_layout;
    ID3D11Buffer* m_fanIndices = nullptr;
    uint32_t m_fanIndexCount = 0;

    // texture uploads: scratch for converted texels
    std::vector<uint8_t> m_texScratch;
    std::vector<uint8_t> m_texScratch2;
    // buffer locks: the bytes the engine writes until the unlock uploads them
    std::unordered_map<CGxBuf*, std::vector<uint8_t>> m_locks;
    std::vector<std::vector<uint8_t>> m_lockFree;

    // cursor
    HCURSOR m_hwCursorHandle = nullptr;
    bool m_hwCursorDirty = true;

    // virtual member functions
    virtual void ITexMarkAsUpdated(CGxTex* texId);
    virtual void IRsSendToHw(EGxRenderState which);
    virtual void ICursorCreate(const CGxFormat& format);
    virtual void ICursorDestroy();
    virtual void ICursorDraw();
    virtual int32_t DeviceCreate(GxWindowProc windowProc, const CGxFormat& format);
    virtual int32_t DeviceCreate(void* window, const CGxFormat& format);
    virtual void DeviceDestroy();
    virtual void DeviceEvictResources();
    virtual int32_t DeviceSetFormat(const CGxFormat& format);
    virtual void DeviceSetBaseMipLevel(uint32_t level);
    virtual void DeviceSetGamma(float gamma);
    virtual void DeviceSetGamma(const CGxGammaRamp& ramp);
    virtual void* DeviceWindow();
    virtual void DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels);
    virtual void DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths);
    virtual int32_t DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2);
    virtual void DeviceSetRenderTarget(EGxBuffer buffer, CGxTex* texture, uint32_t plane);
    virtual void DeviceSaveDepthSurface();
    virtual void DeviceRestoreDepthSurface();
    virtual void DeviceResolveDepthBuffer(CGxTex* texture);
    virtual void DeviceCopyTex(CGxTex* source, uint32_t sourcePlane, CGxTex* dest, uint32_t destPlane);
    virtual void DeviceOverride(EGxOverride override, uint32_t value);
    virtual void CapsWindowSize(CRect& rect);
    virtual void CapsWindowSizeInScreenCoords(CRect& rect);
    virtual void LogCrashInfo(char* buffer, uint32_t size);
    virtual void ScenePresent();
    virtual void SceneClear(uint32_t mask, CImVector color);
    virtual void XformSetProjection(const C44Matrix& matrix);
    virtual void XformSetView(const C44Matrix& matrix);
    virtual void Draw(CGxBatch* batch, int32_t count);
    virtual void PoolSizeSet(CGxPool* pool, uint32_t size);
    virtual void PoolDestroy(CGxPool* pool);
    virtual char* BufLock(CGxBuf* buf);
    virtual int32_t BufUnlock(CGxBuf* buf, uint32_t size);
    virtual void BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset);
    virtual void TexDestroy(CGxTex* texId);
    virtual int32_t TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane);
    virtual int32_t TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face);
    virtual void TexSetCacheSize(uint32_t size);
    virtual void QueryCreate(CGxQuery*& query, EGxQueryType type);
    virtual void QueryDestroy(CGxQuery*& query);
    virtual bool QueryBegin(CGxQuery* query);
    virtual bool QueryEnd(CGxQuery* query);
    virtual bool QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data);
    virtual bool QueryGetData(CGxQuery* query, uint32_t& data);
    virtual void ShaderCreate(CGxShader** shaders, EGxShTarget target, const char* path, const char* name, uint32_t permutations);
    virtual void ShaderDestroy(CGxShader*& shader);
    virtual void IShaderReload(CGxShader* shader, const char* path, const char* name);
    virtual void IShaderCreate(CGxShader* shader);
    virtual void CursorSetVisible(int32_t visible);
    virtual void CursorUnlock(uint32_t x, uint32_t y);
    virtual void StereoSetConvergence(float convergence);
    virtual float StereoGetConvergence();
    virtual void StereoSetSeparation(float separation);
    virtual float StereoGetSeparation();
    virtual bool StereoEnabled();
    virtual void DeviceUnk81(uint32_t a, uint32_t b);
    virtual void DeviceUnk82();
    virtual void DeviceUnk83(uint32_t a, uint32_t b);

    // static functions
    static LRESULT CALLBACK WindowProcD3d11(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    // member functions
    CGxDeviceD3d11();
    virtual ~CGxDeviceD3d11();

    // device and window (CGxDeviceD3d11.cpp)
    int32_t IInitD3d11();
    int32_t ICreateWindow(CGxFormat& format);
    int32_t ICreateD3d11Device(const CGxFormat& format);
    void IReleaseSwapChain();
    int32_t ICreateFrame();
    void IReleaseFrame();
    int32_t IResizeFrame(uint32_t width, uint32_t height);
    void IDestroyD3d11();
    void IReleaseD3d11Resources(int32_t freeAll);
    void ISetCaps(const CGxFormat& format);
    void IStateSetDefaults();
    void ISetWindowFocus(int32_t focus);
    // MODIFIED: display management. Moves a full-screen or maximized window over the monitor it is on; 0 for a
    // normal window
    int32_t IFitToMonitor();
    void IUpdateGamma();
    void IPresentFrame();
    void IFrameLatency();
    void IResolveFrame();
    // MODIFIED: modern lighting: the colour target a null render target means (the HDR scene while the world draws
    // into it), and that target single-sampled
    ID3D11RenderTargetView* IFrameRtv() const;
    ID3D11ShaderResourceView* IFrameSrv();

    // render state and drawing (CGxDeviceD3d11Draw.cpp)
    bool IStateSync(EGxPrim primType);
    void IInvalidateBindings();
    void ISetTexGen(uint32_t tmu, int32_t texGen);
    void IXformSetViewport();
    void IStateSyncScissorRect();
    bool IBuildVertexLayout();
    Program* IProgram(const std::string& hlsl, const char* entry, const char* profile, uint32_t constCount);
    Program* ITranslatedProgram(CGxShader* shader, uint32_t variant);
    Program* IFixedVertexProgram(const GxD3d11::FfVertexKey& key);
    Program* IFixedPixelProgram(const GxD3d11::FfPixelKey& key);
    ID3D11InputLayout* IInputLayout(Program* program);
    ID3D11BlendState* IBlendState(uint32_t mode, uint32_t writeMask);
    ID3D11DepthStencilState* IDepthState(uint32_t func, uint32_t write);
    ID3D11RasterizerState* IRasterState(uint32_t cull, bool wireframe, bool scissor, int32_t depthBias, bool multisample);
    ID3D11SamplerState* ISamplerState(const CGxTexFlags& flags, bool compare);
    void IBuildFixedConstants(GxD3d11::FfConstants& constants, const uint8_t* texTransform);
    void IBuildDrawConstants(DrawConstants& constants);
    void IRingReserve(uint32_t bytes);
    bool IUploadConstants(EConstants which, const void* data, uint32_t size, ConstantSlice& slice);
    void IBindConstants(EStage stage, uint32_t slot, const ConstantSlice& slice);
    void IClearRect(uint32_t mask, CImVector color);
    void IReleasePrograms();
    void IReleaseStateObjects();

    // textures and targets (CGxDeviceD3d11Texture.cpp)
    void ITexCreate(CGxTex* texId);
    void ITexUpload(CGxTex* texId);
    void ITexRelease(CGxTex* texId);
    void ITexForceRecreation(int32_t all);
    Texture* ITexGet(CGxTex* texId);
    void IBindTargets();
    ID3D11DepthStencilView* IAutoDepth(uint32_t width, uint32_t height);
    void IUnbindTexture(const Texture* texture);
    void IGenerateMips(CGxTex* texId);
    bool IBlit(ID3D11ShaderResourceView* source, uint32_t sourceWidth, uint32_t sourceHeight, const RECT& sourceRect, ID3D11RenderTargetView* dest, uint32_t destWidth, uint32_t destHeight, const RECT& destRect, bool linear);

    // buffers, queries and shaders (CGxDeviceD3d11Buffer.cpp)
    void IPrecompile(CGxShader* shader);
    void ICreatePool(CGxPool* pool);
    void IReleasePool(CGxPool* pool);
    void IReleasePools(int32_t freeAll);
    void IReleaseQueries();
};

CGxDevice* CreateCGxDeviceD3d11();

#endif
