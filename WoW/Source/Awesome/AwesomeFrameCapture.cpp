// Frame capture (awesome_wotlk MiscCaptureFrame.h): CaptureFrame(frame[, size]) and /fcapture render one frame tree
// offscreen, over black and over white, and save the unmatted result as a transparent PNG in Screenshots\.
// size is the longest side in pixels ("2048") or a scale of the window ("2x").

#include "WoW/Source/Awesome/AwesomeInternal.h"
#include "WoW/Source/Awesome/AwesomeD3d.h"
#include "WoW/Source/Awesome/AwesomeFrameCaptureLua.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Frame/CFrameStrataNode.h"
#include "Engine/Source/Frame/CRenderBatch.h"
#include "Engine/Source/Frame/CSimpleFrame.h"
#include "Engine/Source/Frame/CSimpleModel.h"
#include "Engine/Source/Frame/CSimpleScrollFrame.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Model2/M2Model.h"
#include "WoW/Source/Ui/WorldFrame.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {
// FRAMECAPTURE_* format strings, as Lua expressions
std::string LuaString(std::string_view text) {
    std::string literal = "\"";

    for (unsigned char c : text) {
        if (c == '\\' || c == '"') {
            literal += '\\';
            literal += static_cast<char>(c);
        } else if (c < 0x20) {
            literal += std::format("\\{:03}", c);
        } else {
            literal += static_cast<char>(c);
        }
    }

    return literal + '"';
}

std::string LuaNumber(double value) {
    return std::format("{}", value);
}

std::string LuaFormat(std::string_view global, std::initializer_list<std::string> args = {}) {
    std::string expr = std::format("({} or \"{}\"):format(", global, global);
    const char* separator = "";

    for (const std::string& arg : args) {
        expr += std::exchange(separator, ", ");
        expr += arg;
    }

    return expr + ')';
}

constexpr uint32_t kStripRows = 256;
constexpr int32_t kMaxSamples = 8;
// pixels of each full-size surface
constexpr uint64_t kPixelBudget = 32ull << 20;
// pixels x samples of the multisampled colour and depth targets
constexpr uint64_t kSampleBudget = 64ull << 20;
constexpr float kMinScale = 0.25f;

struct CaptureRequest {
    CSimpleFrame* m_frame = nullptr;
    float m_longestSide = 0.0f;
    float m_scale = 1.0f;
};

CaptureRequest s_pending;
// the tree being drawn while a capture renders
CSimpleFrame* s_root;
// Lua chunks run after the capture
std::vector<std::string> s_messages;

bool IsInCaptureTree(const CSimpleFrame* frame) {
    for (const CSimpleFrame* it = frame; it; it = it->m_parent) {
        if (it == s_root) {
            return true;
        }
    }

    return false;
}

bool IsAncestorOfCaptureRoot(const CSimpleFrame* frame) {
    for (const CSimpleFrame* it = s_root->m_parent; it; it = it->m_parent) {
        if (it == frame) {
            return true;
        }
    }

    return false;
}

void CapturePrint(const std::string& messageExpr) {
    s_messages.push_back(std::format("print(\"|cff33ff99FrameCapture|r: \" .. {})", messageExpr));
}

void CaptureFailed(std::string_view global, std::initializer_list<std::string> args = {}) {
    CapturePrint(LuaFormat("FRAMECAPTURE_FAILED", { LuaFormat(global, args) }));
}

void FlushCaptureMessages() {
    std::vector<std::string> batch;
    batch.swap(s_messages);

    for (const std::string& chunk : batch) {
        FrameScript_Execute(chunk.c_str(), "FrameCapture", nullptr);
    }
}

// every OnFrameRender override draws its regions through the base, so this isolates the captured tree in the strata
// batches and in the scroll children's own batches
bool FrameRenderFilter(CSimpleFrame* frame) {
    return !s_root || IsInCaptureTree(frame);
}

// CM2Scene::Animate drains the scene's animate and draw lists, which only CSimpleModel::OnLayerUpdate refills, once a
// frame; the on-screen pass already used them up, so arm the model again the same way before each capture pass
void CaptureRenderModel(void* param) {
    CSimpleModel* modelFrame = static_cast<CSimpleModel*>(param);
    CM2Model* model = modelFrame->m_model;

    if (model && modelFrame->m_isVisible && modelFrame->m_pendingCameraIndex == 0xFFFFFFFF) {
        model->SetAnimating(1);
        // draw (0x8 / 0x80 with the parent) and particles (0x10000 / 0x20000 with the parent) this frame
        uint32_t queue = model->m_attachParent ? (0x80 | 0x20000) : (0x8 | 0x10000);
        model->m_fFlags |= queue;
    }

    CSimpleModel::RenderModel(modelFrame);
}

// render callbacks (models, cooldowns, the world, scroll children) of the captured tree, plus the scroll frames on the
// way down to it so a target inside a scroll child is still drawn through its clip
CRenderBatch::RenderCallback CallbackFilter(CRenderBatch::RenderCallback callback, void* param) {
    if (!s_root) {
        return callback;
    }

    CSimpleFrame* frame = static_cast<CSimpleFrame*>(param);
    bool scrollPath = callback == &CSimpleScrollFrame::RenderScrollChild && IsAncestorOfCaptureRoot(frame);

    if (!scrollPath && !IsInCaptureTree(frame)) {
        return nullptr;
    }

    if (callback == &CSimpleModel::RenderModel) {
        return &CaptureRenderModel;
    }

    return callback;
}

void MarkAllBatchesDirty(CSimpleTop* top) {
    for (CFrameStrata* strata : top->m_strata) {
        if (!strata) {
            continue;
        }

        strata->m_batchDirty = 1;

        for (uint32_t i = 0; i < strata->m_topLevel && i < strata->m_levels.m_count; i++) {
            if (CFrameStrataNode* node = strata->m_levels.m_data[i]) {
                node->m_batchDirty |= 0x1F;
            }
        }
    }

    for (CSimpleFrame* frame = top->m_frames.Head(); frame; frame = top->m_frames.Next(frame)) {
        // being scrolled: drawn by its scroll frame's own batches
        if (frame->m_frameFlags & 0x2000) {
            frame->m_batchDirty |= 0x1F;
        }
    }
}

struct ComRelease {
    void operator()(IUnknown* object) const {
        if (object) {
            object->Release();
        }
    }
};

using SurfacePtr = std::unique_ptr<IDirect3DSurface9, ComRelease>;

SurfacePtr CreateTarget(IDirect3DDevice9* d3d, uint32_t width, uint32_t height, D3DMULTISAMPLE_TYPE samples) {
    IDirect3DSurface9* surface = nullptr;

    if (FAILED(d3d->CreateRenderTarget(width, height, D3DFMT_A8R8G8B8, samples, 0, FALSE, &surface, nullptr))) {
        return nullptr;
    }

    return SurfacePtr{ surface };
}

bool HasStencil(D3DFORMAT format) {
    return format == D3DFMT_D24S8 || format == D3DFMT_D24X4S4 || format == D3DFMT_D15S1 || format == D3DFMT_D24FS8;
}

// copies rows [y0, y0 + rows) of a resolved pass through a strip-sized target, so the readback never needs a
// full-size system memory surface
struct StripReader {
    IDirect3DDevice9* m_d3d;
    IDirect3DSurface9* m_strip;
    IDirect3DSurface9* m_sys;
    uint32_t m_width;

    bool Read(IDirect3DSurface9* source, uint32_t y0, uint32_t rows, std::vector<uint32_t>& out) const {
        RECT sourceRect = { 0, static_cast<LONG>(y0), static_cast<LONG>(this->m_width), static_cast<LONG>(y0 + rows) };
        RECT destRect = { 0, 0, static_cast<LONG>(this->m_width), static_cast<LONG>(rows) };

        if (FAILED(this->m_d3d->StretchRect(source, &sourceRect, this->m_strip, &destRect, D3DTEXF_NONE))) {
            return false;
        }

        if (FAILED(this->m_d3d->GetRenderTargetData(this->m_strip, this->m_sys))) {
            return false;
        }

        D3DLOCKED_RECT locked = {};

        if (FAILED(this->m_sys->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            return false;
        }

        out.resize(static_cast<size_t>(this->m_width) * rows);
        size_t pitch = static_cast<size_t>(locked.Pitch);
        const uint8_t* source8 = static_cast<const uint8_t*>(locked.pBits);

        for (uint32_t y = 0; y < rows; y++) {
            memcpy(&out[y * this->m_width], source8 + y * pitch, static_cast<size_t>(this->m_width) * 4);
        }

        this->m_sys->UnlockRect();
        return true;
    }
};

// straight alpha from the same render over black and over white: white - black = 1 - alpha
int32_t MatteAlpha(uint32_t onBlack, uint32_t onWhite) {
    int32_t alphaSum = 0;

    for (int32_t c = 0; c < 3; c++) {
        int32_t black = static_cast<int32_t>((onBlack >> (8 * c)) & 0xFF);
        int32_t white = static_cast<int32_t>((onWhite >> (8 * c)) & 0xFF);
        alphaSum += 255 - std::clamp(white - black, 0, 255);
    }

    return (alphaSum + 1) / 3;
}

void WriteMattePixel(uint32_t onBlack, int32_t alpha, uint8_t* dest) {
    for (int32_t c = 0; c < 3; c++) {
        int32_t black = static_cast<int32_t>((onBlack >> (8 * c)) & 0xFF);
        dest[2 - c] = static_cast<uint8_t>(std::min(255, (black * 255 + alpha / 2) / alpha));
    }

    dest[3] = static_cast<uint8_t>(alpha);
}

enum class MatteResult {
    Saved,
    Empty,
    ReadFailed,
    OutOfMemory,
    WriteFailed,
};

struct Box {
    int32_t m_minX = 0;
    int32_t m_minY = 0;
    int32_t m_maxX = 0;
    int32_t m_maxY = 0;
};

// two sweeps over the strips: find the drawn box, then unmatte just that box into the PNG
MatteResult WriteCapturePng(const std::string& path, const StripReader& reader, IDirect3DSurface9* black, IDirect3DSurface9* white, uint32_t height, Box& box) {
    uint32_t width = reader.m_width;
    std::vector<uint32_t> blackRows;
    std::vector<uint32_t> whiteRows;
    uint32_t minX = width;
    uint32_t minY = height;
    uint32_t maxX = 0;
    uint32_t maxY = 0;

    for (uint32_t y0 = 0; y0 < height; y0 += kStripRows) {
        uint32_t rows = std::min(kStripRows, height - y0);

        if (!reader.Read(black, y0, rows, blackRows) || !reader.Read(white, y0, rows, whiteRows)) {
            return MatteResult::ReadFailed;
        }

        for (uint32_t y = 0; y < rows; y++) {
            for (uint32_t x = 0; x < width; x++) {
                size_t i = y * width + x;

                if (MatteAlpha(blackRows[i], whiteRows[i]) == 0) {
                    continue;
                }

                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y0 + y);
                maxY = std::max(maxY, y0 + y);
            }
        }
    }

    if (minX > maxX || minY > maxY) {
        return MatteResult::Empty;
    }

    uint32_t boxWidth = maxX - minX + 1;
    uint32_t boxHeight = maxY - minY + 1;
    std::vector<uint8_t> rgba;

    try {
        rgba.resize(static_cast<size_t>(boxWidth) * boxHeight * 4);
    } catch (const std::bad_alloc&) {
        return MatteResult::OutOfMemory;
    }

    for (uint32_t y0 = minY - (minY % kStripRows); y0 <= maxY; y0 += kStripRows) {
        uint32_t rows = std::min(kStripRows, height - y0);

        if (!reader.Read(black, y0, rows, blackRows) || !reader.Read(white, y0, rows, whiteRows)) {
            return MatteResult::ReadFailed;
        }

        for (uint32_t y = std::max(y0, minY); y < y0 + rows && y <= maxY; y++) {
            for (uint32_t x = minX; x <= maxX; x++) {
                size_t i = (y - y0) * width + x;
                int32_t alpha = MatteAlpha(blackRows[i], whiteRows[i]);

                if (alpha == 0) {
                    continue;
                }

                size_t offset = (static_cast<size_t>(y - minY) * boxWidth + (x - minX)) * 4;
                WriteMattePixel(blackRows[i], alpha, &rgba[offset]);
            }
        }
    }

    box = { static_cast<int32_t>(minX), static_cast<int32_t>(minY), static_cast<int32_t>(maxX + 1), static_cast<int32_t>(maxY + 1) };
    bool written = stbi_write_png(path.c_str(), static_cast<int>(boxWidth), static_cast<int>(boxHeight), 4, rgba.data(), static_cast<int>(boxWidth * 4)) != 0;
    return written ? MatteResult::Saved : MatteResult::WriteFailed;
}

std::string CapturePath(CSimpleFrame* frame) {
    std::string name;

    if (const char* raw = frame->GetName()) {
        for (char ch : std::string_view(raw)) {
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
                name += ch;
            }
        }
    }

    if (name.empty()) {
        name = "Anonymous";
    }

    SYSTEMTIME t = {};
    GetLocalTime(&t);
    CreateDirectoryA("Screenshots", nullptr);
    return std::format("Screenshots\\FrameCapture_{}_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}.png", name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
}

// window pixels spanned by the shown frames of the tree (regions hanging outside their frame aren't counted)
void CaptureTreeSize(CSimpleTop* top, float windowWidth, float windowHeight, float& width, float& height) {
    bool any = false;
    CRect bounds = { 0.0f, 0.0f, 0.0f, 0.0f };

    for (CSimpleFrame* frame = top->m_frames.Head(); frame; frame = top->m_frames.Next(frame)) {
        if (!frame->m_isVisible || !IsInCaptureTree(frame)) {
            continue;
        }

        const CRect& rect = frame->m_rect;

        if (rect.maxX <= rect.minX || rect.maxY <= rect.minY) {
            continue;
        }

        if (!any) {
            bounds = rect;
            any = true;
            continue;
        }

        bounds.minX = std::min(bounds.minX, rect.minX);
        bounds.maxX = std::max(bounds.maxX, rect.maxX);
        bounds.minY = std::min(bounds.minY, rect.minY);
        bounds.maxY = std::max(bounds.maxY, rect.maxY);
    }

    width = any ? DDCToNDCWidth(bounds.maxX - bounds.minX) * windowWidth : 0.0f;
    height = any ? DDCToNDCHeight(bounds.maxY - bounds.minY) * windowHeight : 0.0f;
}

void CaptureFrame(CSimpleTop* top, const CaptureRequest& request) {
    CGxDevice* device = g_theGxDevicePtr;
    IDirect3DDevice9* d3d = AwesomeD3dDevice();

    if (!d3d) {
        CaptureFailed("FRAMECAPTURE_NO_DEVICE");
        return;
    }

    float windowWidth = device->m_curWindowRect.maxX;
    float windowHeight = device->m_curWindowRect.maxY;

    if (windowWidth < 1.0f || windowHeight < 1.0f) {
        return;
    }

    s_root = request.m_frame;
    float treeWidth;
    float treeHeight;
    CaptureTreeSize(top, windowWidth, windowHeight, treeWidth, treeHeight);
    s_root = nullptr;

    float treeLongest = std::max(treeWidth, treeHeight);

    if (treeLongest < 1.0f) {
        CaptureFailed("FRAMECAPTURE_NO_SIZE");
        return;
    }

    D3DCAPS9 caps = {};
    d3d->GetDeviceCaps(&caps);

    // every UI viewport is normalized x the window, so scaling the window scales models, scroll children and the
    // minimap with everything else; clamp uniformly or the aspect would change
    float wanted = request.m_longestSide > 0.0f ? request.m_longestSide / treeLongest : request.m_scale;
    float maxScale = std::min({ static_cast<float>(caps.MaxTextureWidth) / windowWidth, static_cast<float>(caps.MaxTextureHeight) / windowHeight, std::sqrt(static_cast<float>(kPixelBudget) / (windowWidth * windowHeight)) });
    float scale = std::clamp(wanted, kMinScale, std::max(kMinScale, maxScale));
    uint32_t width = static_cast<uint32_t>(std::lround(windowWidth * scale));
    uint32_t height = static_cast<uint32_t>(std::lround(windowHeight * scale));

    D3DFORMAT depthFormat = D3DFMT_D24S8;
    IDirect3DSurface9* currentDepth = nullptr;

    if (SUCCEEDED(d3d->GetDepthStencilSurface(&currentDepth)) && currentDepth) {
        D3DSURFACE_DESC depthDesc = {};

        if (SUCCEEDED(currentDepth->GetDesc(&depthDesc))) {
            depthFormat = depthDesc.Format;
        }

        currentDepth->Release();
    }

    int32_t samples = kMaxSamples;

    while (samples > 1 && static_cast<uint64_t>(width) * height * samples > kSampleBudget) {
        samples /= 2;
    }

    SurfacePtr color;
    SurfacePtr depth;

    for (;; samples /= 2) {
        D3DMULTISAMPLE_TYPE multisample = samples > 1 ? static_cast<D3DMULTISAMPLE_TYPE>(samples) : D3DMULTISAMPLE_NONE;
        color = CreateTarget(d3d, width, height, multisample);
        IDirect3DSurface9* rawDepth = nullptr;

        if (color && SUCCEEDED(d3d->CreateDepthStencilSurface(width, height, depthFormat, multisample, 0, TRUE, &rawDepth, nullptr))) {
            depth.reset(rawDepth);
            break;
        }

        color.reset();

        if (samples <= 1) {
            CaptureFailed("FRAMECAPTURE_NO_RENDER_TARGET", { LuaNumber(width), LuaNumber(height) });
            return;
        }
    }

    SurfacePtr black = CreateTarget(d3d, width, height, D3DMULTISAMPLE_NONE);
    SurfacePtr white = CreateTarget(d3d, width, height, D3DMULTISAMPLE_NONE);
    SurfacePtr strip = CreateTarget(d3d, width, kStripRows, D3DMULTISAMPLE_NONE);
    IDirect3DSurface9* rawSys = nullptr;

    if (!black || !white || !strip || FAILED(d3d->CreateOffscreenPlainSurface(width, kStripRows, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &rawSys, nullptr))) {
        CaptureFailed("FRAMECAPTURE_NO_READBACK_SURFACES", { LuaNumber(width), LuaNumber(height) });
        return;
    }

    SurfacePtr sys{ rawSys };

    // bound straight through d3d: the engine keeps thinking it draws to the backbuffer, so its viewport and scissor math
    // keeps the backbuffer's Y orientation and nothing the engine tracks has to be restored
    IDirect3DSurface9* rawSavedColor = nullptr;
    IDirect3DSurface9* rawSavedDepth = nullptr;
    d3d->GetRenderTarget(0, &rawSavedColor);
    d3d->GetDepthStencilSurface(&rawSavedDepth);
    SurfacePtr savedColor{ rawSavedColor };
    SurfacePtr savedDepth{ rawSavedDepth };
    CRect savedWindow = device->m_curWindowRect;

    device->m_curWindowRect.maxX = static_cast<float>(width);
    device->m_curWindowRect.maxY = static_cast<float>(height);
    d3d->SetRenderTarget(0, color.get());
    d3d->SetDepthStencilSurface(depth.get());
    s_root = request.m_frame;
    CSimpleTop::s_frameRenderFilter = FrameRenderFilter;
    CRenderBatch::s_callbackFilter = CallbackFilter;
    MarkAllBatchesDirty(top);

    DWORD clearFlags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | (HasStencil(depthFormat) ? D3DCLEAR_STENCIL : 0);

    const auto renderPass = [&](D3DCOLOR clearColor, IDirect3DSurface9* resolved) {
        D3DVIEWPORT9 fullViewport = { 0, 0, width, height, 0.0f, 1.0f };
        RECT fullRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        d3d->SetViewport(&fullViewport);
        d3d->SetScissorRect(&fullRect);
        d3d->Clear(0, nullptr, clearFlags, clearColor, 1.0f, 0);
        device->m_viewportDirty = 1;
        device->m_scissorRectDirty = 1;
        top->OnLayerRender();
        return SUCCEEDED(d3d->StretchRect(color.get(), nullptr, resolved, nullptr, D3DTEXF_NONE));
    };

    bool rendered = renderPass(0xFF000000, black.get()) && renderPass(0xFFFFFFFF, white.get());

    s_root = nullptr;
    CSimpleTop::s_frameRenderFilter = nullptr;
    CRenderBatch::s_callbackFilter = nullptr;
    device->m_curWindowRect = savedWindow;
    d3d->SetRenderTarget(0, savedColor.get());
    d3d->SetDepthStencilSurface(savedDepth.get());
    device->m_viewportDirty = 1;
    device->m_scissorRectDirty = 1;
    MarkAllBatchesDirty(top);

    if (!rendered) {
        CaptureFailed("FRAMECAPTURE_RENDER_FAILED");
        return;
    }

    std::string path = CapturePath(request.m_frame);
    StripReader reader = { d3d, strip.get(), sys.get(), width };
    Box box;

    switch (WriteCapturePng(path, reader, black.get(), white.get(), height, box)) {
        case MatteResult::Saved:
            break;

        case MatteResult::Empty:
            CaptureFailed("FRAMECAPTURE_NOTHING_DRAWN");
            return;

        case MatteResult::ReadFailed:
            CaptureFailed("FRAMECAPTURE_READBACK_FAILED");
            return;

        case MatteResult::OutOfMemory:
            CaptureFailed("FRAMECAPTURE_OUT_OF_MEMORY");
            return;

        case MatteResult::WriteFailed:
            CaptureFailed("FRAMECAPTURE_WRITE_FAILED", { LuaString(path) });
            return;
    }

    std::string clamped = wanted > scale + 0.001f ? LuaFormat("FRAMECAPTURE_CLAMPED", { LuaNumber(wanted) }) : LuaString("");
    std::string msaa = samples > 1 ? LuaFormat("FRAMECAPTURE_MSAA", { LuaNumber(samples) }) : LuaFormat("FRAMECAPTURE_NO_MSAA");
    CapturePrint(LuaFormat("FRAMECAPTURE_SAVED", { LuaString(path), LuaNumber(box.m_maxX - box.m_minX), LuaNumber(box.m_maxY - box.m_minY), LuaNumber(scale), clamped, msaa }));
}

// CSimpleTop::OnLayerRender, after the UI drew: run a pending capture (set only while one is pending)
void OnLayerRender(CSimpleTop* top) {
    CSimpleTop::s_layerRenderCallback = nullptr;

    if (s_pending.m_frame) {
        CaptureFrame(top, std::exchange(s_pending, {}));
    }

    FlushCaptureMessages();
}

int32_t Script_CaptureFrame(lua_State* L) {
    CSimpleFrame* frame = nullptr;

    if (lua_type(L, 1) == LUA_TTABLE) {
        lua_rawgeti(L, 1, 0);
        frame = static_cast<CSimpleFrame*>(lua_touserdata(L, -1));
        lua_settop(L, -2);
    }

    if (!frame || !frame->IsA(CSimpleFrame::GetObjectType())) {
        return 0;
    }

    if (frame == reinterpret_cast<CSimpleFrame*>(CGWorldFrame::s_currentWorldFrame)) {
        CaptureFailed("FRAMECAPTURE_WORLD_FRAME");
        FlushCaptureMessages();
        return 0;
    }

    CaptureRequest request;
    request.m_frame = frame;

    if (lua_type(L, 2) == LUA_TNUMBER) {
        request.m_longestSide = static_cast<float>(lua_tonumber(L, 2));
    } else if (lua_type(L, 2) == LUA_TSTRING) {
        std::string_view text = lua_tolstring(L, 2, nullptr);
        bool isScale = !text.empty() && (text.back() == 'x' || text.back() == 'X');
        std::string_view number = isScale ? text.substr(0, text.size() - 1) : text;
        double value = 0.0;
        auto [ptr, ec] = std::from_chars(number.data(), number.data() + number.size(), value);

        if (ec != std::errc{} || ptr != number.data() + number.size() || !(value > 0.0)) {
            CaptureFailed("FRAMECAPTURE_INVALID_SIZE", { LuaString(text) });
            FlushCaptureMessages();
            return 0;
        }

        (isScale ? request.m_scale : request.m_longestSide) = static_cast<float>(value);
    }

    s_pending = request;
    CSimpleTop::s_layerRenderCallback = OnLayerRender;
    return 0;
}
}  // namespace

void AwesomeFrameCaptureRegisterScriptFunctions(lua_State* L) {
    static const AwesomeLuaFunction s_functions[] = {
        { "CaptureFrame", Script_CaptureFrame },
    };

    AwesomeLuaRegisterGlobals(L, s_functions, sizeof(s_functions) / sizeof(s_functions[0]));
}

void AwesomeFrameCaptureOnEnterWorld() {
    FrameScript_Execute(misc_capture_frame_lua::kFrameCaptureCommands, "FrameCapture", nullptr);
}

void AwesomeFrameCaptureOnLeaveWorld() {
    s_pending = {};
    CSimpleTop::s_layerRenderCallback = nullptr;
}
