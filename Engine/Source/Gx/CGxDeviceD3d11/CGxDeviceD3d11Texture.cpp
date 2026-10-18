#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include <cstring>
#include "Engine/Source/Gx/Blit.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/D3d11Util.h"
#include "Engine/Source/Gx/Gx.h"

// MODIFIED: D3D11 device, see CGxDeviceD3d11.h. Textures, render targets, copies and read-back.

using namespace GxD3d11;

namespace {

bool IsDxt(EGxTexFormat format) {
    return format >= GxTex_Dxt1 && format <= GxTex_Dxt5;
}

// 16-bit texels the device cannot sample, widened to B8G8R8A8
void Widen1555(const uint8_t* in, uint32_t inPitch, uint8_t* out, uint32_t outPitch, uint32_t width, uint32_t height) {
    for (uint32_t y = 0; y < height; y++) {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(in + y * inPitch);
        uint8_t* dst = out + y * outPitch;

        for (uint32_t x = 0; x < width; x++) {
            uint16_t v = src[x];
            uint32_t r = (v >> 10) & 0x1F;
            uint32_t g = (v >> 5) & 0x1F;
            uint32_t b = v & 0x1F;
            dst[x * 4 + 0] = static_cast<uint8_t>((b << 3) | (b >> 2));
            dst[x * 4 + 1] = static_cast<uint8_t>((g << 3) | (g >> 2));
            dst[x * 4 + 2] = static_cast<uint8_t>((r << 3) | (r >> 2));
            dst[x * 4 + 3] = (v & 0x8000) ? 0xFF : 0x00;
        }
    }
}

void Widen565(const uint8_t* in, uint32_t inPitch, uint8_t* out, uint32_t outPitch, uint32_t width, uint32_t height) {
    for (uint32_t y = 0; y < height; y++) {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(in + y * inPitch);
        uint8_t* dst = out + y * outPitch;

        for (uint32_t x = 0; x < width; x++) {
            uint16_t v = src[x];
            uint32_t r = (v >> 11) & 0x1F;
            uint32_t g = (v >> 5) & 0x3F;
            uint32_t b = v & 0x1F;
            dst[x * 4 + 0] = static_cast<uint8_t>((b << 3) | (b >> 2));
            dst[x * 4 + 1] = static_cast<uint8_t>((g << 2) | (g >> 4));
            dst[x * 4 + 2] = static_cast<uint8_t>((r << 3) | (r >> 2));
            dst[x * 4 + 3] = 0xFF;
        }
    }
}

}

CGxDeviceD3d11::Texture* CGxDeviceD3d11::ITexGet(CGxTex* texId) {
    if (!texId || texId->m_needsCreation) {
        return nullptr;
    }

    return static_cast<Texture*>(texId->m_apiSpecificData);
}

void CGxDeviceD3d11::ITexCreate(CGxTex* texId) {
    if (!this->m_d3dDevice) {
        return;
    }

    if (texId->m_apiSpecificData) {
        this->ITexRelease(texId);
    }

    uint32_t width;
    uint32_t height;
    uint32_t baseMip;
    uint32_t mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    width = width ? width : 1;
    height = height ? height : 1;

    bool renderTarget = texId->m_flags.m_renderTarget != 0;
    Texture* texture = new Texture();
    texture->m_width = width;
    texture->m_height = height;
    texture->m_baseMip = baseMip;
    texture->m_cube = texId->m_target == GxTex_CubeMap;
    texture->m_depth = texId->m_format == GxTex_D24X8;
    texture->m_autoMips = renderTarget && texId->m_flags.m_generateMipMaps && !texture->m_depth;

    DXGI_FORMAT srvFormat;

    switch (texId->m_format) {
        case GxTex_Abgr8888:
            texture->m_format = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;

        case GxTex_Argb4444:
            if (this->m_formatSupport4444 && !renderTarget) {
                texture->m_format = DXGI_FORMAT_B4G4R4A4_UNORM;
            } else {
                texture->m_format = DXGI_FORMAT_R8G8B8A8_UNORM;
                texture->m_convert = Texture::Convert_4444;
            }

            break;

        case GxTex_Argb1555:
            if (this->m_formatSupport1555 && !renderTarget) {
                texture->m_format = DXGI_FORMAT_B5G5R5A1_UNORM;
            } else {
                texture->m_format = DXGI_FORMAT_B8G8R8A8_UNORM;
                texture->m_convert = Texture::Convert_1555;
            }

            break;

        case GxTex_Rgb565:
            if (renderTarget ? this->m_formatSupport565Rt : this->m_formatSupport565) {
                texture->m_format = DXGI_FORMAT_B5G6R5_UNORM;
            } else {
                texture->m_format = DXGI_FORMAT_B8G8R8A8_UNORM;
                texture->m_convert = Texture::Convert_565;
            }

            break;

        case GxTex_Dxt1:
        case GxTex_Dxt3:
        case GxTex_Dxt5:
            // D3D11 takes block compression only when mip 0 is whole blocks
            if ((width & 3) || (height & 3)) {
                texture->m_format = DXGI_FORMAT_B8G8R8A8_UNORM;
                texture->m_convert = Texture::Convert_DecodeDxt;
            } else {
                texture->m_format = texId->m_format == GxTex_Dxt1 ? DXGI_FORMAT_BC1_UNORM : (texId->m_format == GxTex_Dxt3 ? DXGI_FORMAT_BC2_UNORM : DXGI_FORMAT_BC3_UNORM);
            }

            break;

        case GxTex_Uv88:
            texture->m_format = DXGI_FORMAT_R8G8_SNORM;
            break;

        case GxTex_Gr1616F:
            texture->m_format = DXGI_FORMAT_R16G16_FLOAT;
            break;

        case GxTex_R32F:
            texture->m_format = DXGI_FORMAT_R32_FLOAT;
            break;

        case GxTex_D24X8:
            texture->m_format = DXGI_FORMAT_R24G8_TYPELESS;
            break;

        default:
            texture->m_format = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
    }

    srvFormat = texture->m_depth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : texture->m_format;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = texture->m_autoMips ? 0 : (mipCount > baseMip ? mipCount - baseMip : 1);
    desc.ArraySize = texture->m_cube ? 6 : 1;
    desc.Format = texture->m_format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    if (renderTarget) {
        desc.BindFlags |= texture->m_depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
    }

    desc.MiscFlags = (texture->m_cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0) | (texture->m_autoMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0);

    ID3D11Texture2D* resource = nullptr;
    HRESULT hr = this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &resource);

    if (FAILED(hr)) {
        CGxDevice::Log("CGxDeviceD3d11::ITexCreate(): %u x %u format %d failed (%08X)", width, height, texId->m_format, static_cast<uint32_t>(hr));
        delete texture;
        return;
    }

    resource->GetDesc(&desc);
    texture->m_resource = resource;
    texture->m_levels = desc.MipLevels;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = srvFormat;

    if (texture->m_cube) {
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MostDetailedMip = 0;
        srv.TextureCube.MipLevels = desc.MipLevels;
    } else {
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = desc.MipLevels;
    }

    this->m_d3dDevice->CreateShaderResourceView(resource, &srv, &texture->m_srv);

    if (renderTarget && texture->m_depth) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        this->m_d3dDevice->CreateDepthStencilView(resource, &dsv, &texture->m_dsv);
    } else if (renderTarget) {
        for (uint32_t face = 0; face < (texture->m_cube ? 6u : 1u); face++) {
            D3D11_RENDER_TARGET_VIEW_DESC rtv = {};
            rtv.Format = texture->m_format;

            if (texture->m_cube) {
                rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rtv.Texture2DArray.MipSlice = 0;
                rtv.Texture2DArray.FirstArraySlice = face;
                rtv.Texture2DArray.ArraySize = 1;
            } else {
                rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                rtv.Texture2D.MipSlice = 0;
            }

            this->m_d3dDevice->CreateRenderTargetView(resource, &rtv, &texture->m_rtv[face]);
        }
    }

    texId->m_apiSpecificData = texture;
    texId->m_needsCreation = 0;
}

void CGxDeviceD3d11::ITexMarkAsUpdated(CGxTex* texId) {
    if (!texId->m_needsUpdate || !this->m_context) {
        return;
    }

    if (texId->m_needsCreation || !texId->m_apiSpecificData) {
        this->ITexCreate(texId);
    }

    if (!texId->m_needsCreation && texId->m_apiSpecificData) {
        if (texId->m_userFunc) {
            this->ITexUpload(texId);
        }

        CGxDevice::ITexMarkAsUpdated(texId);
    }
}

void CGxDeviceD3d11::ITexUpload(CGxTex* texId) {
    Texture* texture = this->ITexGet(texId);

    if (!texture || !texId->m_userFunc) {
        return;
    }

    uint32_t texelStrideInBytes = 0;
    const void* texels = nullptr;
    texId->m_userFunc(GxTex_Lock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    uint32_t width;
    uint32_t height;
    uint32_t baseMip;
    uint32_t mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    bool compressed = IsDxt(texId->m_dataFormat);
    bool renderTarget = texId->m_flags.m_renderTarget != 0;
    uint32_t faceCount = texture->m_cube ? 6 : 1;

    for (uint32_t face = 0; face < faceCount; face++) {
        uint32_t mipWidth = width ? width : 1;
        uint32_t mipHeight = height ? height : 1;
        uint32_t level = 0;

        for (uint32_t mip = baseMip; mip != mipCount; mip++, level++) {
            texels = nullptr;
            texId->m_userFunc(GxTex_Latch, mipWidth, mipHeight, face, mip, texId->m_userArg, texelStrideInBytes, texels);

            if (texels && !renderTarget && level < texture->m_levels) {
                // the update rectangle at this level, whole blocks for block compression (CGxDeviceD3d9Ex::ITexUpload)
                int32_t left = texId->m_updateRect.minX >> mip;
                int32_t top = texId->m_updateRect.minY >> mip;
                int32_t right = texId->m_updateRect.maxX >> mip;
                int32_t bottom = texId->m_updateRect.maxY >> mip;
                right = right > left + 1 ? right : left + 1;
                bottom = bottom > top + 1 ? bottom : top + 1;

                if (compressed) {
                    left &= ~3;
                    top &= ~3;
                    right = (right + 3) & ~3;
                    bottom = (bottom + 3) & ~3;
                }

                int32_t limitX = compressed ? static_cast<int32_t>((mipWidth + 3) & ~3u) : static_cast<int32_t>(mipWidth);
                int32_t limitY = compressed ? static_cast<int32_t>((mipHeight + 3) & ~3u) : static_cast<int32_t>(mipHeight);
                right = right < limitX ? right : limitX;
                bottom = bottom < limitY ? bottom : limitY;

                if (left < right && top < bottom) {
                    const uint8_t* src = static_cast<const uint8_t*>(texels);

                    if (!texId->m_flags.m_bit15) {
                        if (compressed) {
                            src += s_texFormatBytesPerBlock[texId->m_dataFormat] * (left >> 2) + texelStrideInBytes * (top >> 2);
                        } else {
                            src += ((left * s_texFormatBitDepth[texId->m_dataFormat]) >> 3) + texelStrideInBytes * top;
                        }
                    }

                    // the rectangle's texels in the engine's format
                    C2iVector size;
                    size.x = right - left;
                    size.y = bottom - top;
                    uint32_t srcPitch = texelStrideInBytes;
                    std::vector<uint8_t> repeated;

                    // MODIFIED: a stride of 0 repeats one row (the MSDF atlas clears a page that way); D3D9's
                    // row-by-row copy takes it, UpdateSubresource does not promise to, so the rows are written out
                    if (srcPitch == 0 && size.y > 1 && !compressed) {
                        uint32_t rowBytes = (static_cast<uint32_t>(size.x) * s_texFormatBitDepth[texId->m_dataFormat]) >> 3;
                        repeated.resize(static_cast<size_t>(rowBytes) * size.y);

                        for (int32_t row = 0; row < size.y; row++) {
                            memcpy(repeated.data() + static_cast<size_t>(rowBytes) * row, src, rowBytes);
                        }

                        src = repeated.data();
                        srcPitch = rowBytes;
                    }

                    const uint8_t* data = src;
                    uint32_t pitch = srcPitch;

                    if (texId->m_dataFormat != texId->m_format) {
                        pitch = GxCalcTexelStrideInBytes(texId->m_format, size.x);
                        uint32_t rows = IsDxt(texId->m_format) ? (size.y + 3) / 4 : size.y;
                        this->m_texScratch.resize(static_cast<size_t>(pitch) * rows);
                        Blit(size, BlitAlpha_0, src, srcPitch, GxGetBlitFormat(texId->m_dataFormat), this->m_texScratch.data(), pitch, GxGetBlitFormat(texId->m_format));
                        data = this->m_texScratch.data();
                    }

                    // the box on the texture; past the texture's edge only for whole blocks
                    uint32_t boxRight = static_cast<uint32_t>(right) < mipWidth || !IsDxt(texId->m_format) ? static_cast<uint32_t>(right) : mipWidth;
                    uint32_t boxBottom = static_cast<uint32_t>(bottom) < mipHeight || !IsDxt(texId->m_format) ? static_cast<uint32_t>(bottom) : mipHeight;

                    // and in the texture's own format
                    switch (texture->m_convert) {
                        case Texture::Convert_DecodeDxt: {
                            // whole blocks (Blit_DxtUnaligned takes a 6:1 rectangle for cube faces); the upload
                            // box keeps the part inside the texture
                            uint32_t outPitch = size.x * 4;
                            this->m_texScratch2.resize(static_cast<size_t>(outPitch) * size.y);
                            Blit(size, BlitAlpha_0, data, pitch, GxGetBlitFormat(texId->m_format), this->m_texScratch2.data(), outPitch, BlitFormat_Argb8888);
                            data = this->m_texScratch2.data();
                            pitch = outPitch;
                            break;
                        }

                        case Texture::Convert_4444: {
                            uint32_t outPitch = size.x * 4;
                            this->m_texScratch2.resize(static_cast<size_t>(outPitch) * size.y);
                            Blit(size, BlitAlpha_0, data, pitch, BlitFormat_Argb4444, this->m_texScratch2.data(), outPitch, BlitFormat_Abgr8888);
                            data = this->m_texScratch2.data();
                            pitch = outPitch;
                            break;
                        }

                        case Texture::Convert_1555:
                        case Texture::Convert_565: {
                            uint32_t outPitch = size.x * 4;
                            this->m_texScratch2.resize(static_cast<size_t>(outPitch) * size.y);

                            if (texture->m_convert == Texture::Convert_1555) {
                                Widen1555(data, pitch, this->m_texScratch2.data(), outPitch, size.x, size.y);
                            } else {
                                Widen565(data, pitch, this->m_texScratch2.data(), outPitch, size.x, size.y);
                            }

                            data = this->m_texScratch2.data();
                            pitch = outPitch;
                            break;
                        }

                        default:
                            break;
                    }

                    D3D11_BOX box;
                    box.left = static_cast<UINT>(left);
                    box.top = static_cast<UINT>(top);
                    box.right = texture->m_convert == Texture::Convert_DecodeDxt ? boxRight : (IsDxt(texId->m_format) ? static_cast<UINT>(right) : boxRight);
                    box.bottom = texture->m_convert == Texture::Convert_DecodeDxt ? boxBottom : (IsDxt(texId->m_format) ? static_cast<UINT>(bottom) : boxBottom);
                    box.front = 0;
                    box.back = 1;

                    // a block-compressed level smaller than a block takes the whole subresource
                    bool whole = IsDxt(texId->m_format) && texture->m_convert != Texture::Convert_DecodeDxt && (mipWidth < 4 || mipHeight < 4);
                    UINT subresource = D3D11CalcSubresource(level, face, texture->m_levels);
                    this->m_ctx->UpdateSubresource(texture->m_resource, subresource, whole ? nullptr : &box, data, pitch, 0);
                }
            }

            mipWidth = mipWidth > 1 ? mipWidth >> 1 : 1;
            mipHeight = mipHeight > 1 ? mipHeight >> 1 : 1;
        }
    }

    texels = nullptr;
    texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);
}

void CGxDeviceD3d11::IUnbindTexture(const Texture* texture) {
    if (!texture) {
        return;
    }

    for (uint32_t slot = 0; slot < 16; slot++) {
        if (texture->m_srv && this->m_boundSrv[slot] == texture->m_srv) {
            ID3D11ShaderResourceView* none = nullptr;

            if (this->m_ctx) {
                this->m_ctx->PSSetShaderResources(slot, 1, &none);
            }

            this->m_boundSrv[slot] = nullptr;
        }
    }
}

void CGxDeviceD3d11::ITexRelease(CGxTex* texId) {
    Texture* texture = static_cast<Texture*>(texId->m_apiSpecificData);

    if (!texture) {
        return;
    }

    this->IUnbindTexture(texture);

    bool target = false;

    for (ID3D11RenderTargetView*& rtv : texture->m_rtv) {
        target = target || (rtv && rtv == this->m_curRtv);
        SafeRelease(rtv);
    }

    target = target || (texture->m_dsv && texture->m_dsv == this->m_curDsv);

    if (texture->m_dsv == this->m_savedDsv) {
        this->m_savedDsv = nullptr;
    }

    SafeRelease(texture->m_dsv);
    SafeRelease(texture->m_srv);
    SafeRelease(texture->m_resource);
    delete texture;
    texId->m_apiSpecificData = nullptr;

    if (this->m_mipsPending == texId) {
        this->m_mipsPending = nullptr;
    }

    for (uint32_t i = 0; i < GxBuffers_Last; i++) {
        if (this->m_textureTarget[i].m_texture == texId) {
            this->m_textureTarget[i].m_apiSpecific = nullptr;
            target = true;
        }
    }

    if (target) {
        this->IBindTargets();
    }
}

void CGxDeviceD3d11::TexDestroy(CGxTex* texId) {
    this->ITexRelease(texId);

    for (uint32_t i = 0; i < GxBuffers_Last; i++) {
        if (this->m_textureTarget[i].m_texture == texId) {
            this->m_textureTarget[i].m_texture = nullptr;
            this->m_textureTarget[i].m_apiSpecific = nullptr;
            this->IBindTargets();
        }
    }

    CGxDevice::TexDestroy(texId);
}

void CGxDeviceD3d11::ITexForceRecreation(int32_t all) {
    // nothing is kept beside the GPU copy, so every texture is made again from its callback
    for (CGxTex* texId = this->m_textures.Head(); texId; texId = this->m_textures.Next(texId)) {
        this->ITexRelease(texId);

        // STATIC: as CGxDeviceD3d9Ex::ITexForceRecreation
        static CiRect s_updateRect;

        texId->m_needsCreation = 1;
        this->TexMarkForUpdate(texId, s_updateRect, 0);

        if (texId->m_userFunc) {
            uint32_t texelStrideInBytes;
            const void* texels;
            texId->m_userFunc(GxTex_3, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);
        }
    }

    if (this->m_hwnd && this->m_d3dDevice) {
        this->NotifyOnTextureRecreation();
    }
}

void CGxDeviceD3d11::TexSetCacheSize(uint32_t size) {
}

void CGxDeviceD3d11::IGenerateMips(CGxTex* texId) {
    Texture* texture = this->ITexGet(texId);

    if (!texture || !texture->m_autoMips || !texture->m_srv) {
        return;
    }

    this->m_ctx->GenerateMips(texture->m_srv);
    texture->m_mipsDirty = false;
}

ID3D11DepthStencilView* CGxDeviceD3d11::IAutoDepth(uint32_t width, uint32_t height) {
    for (const AutoDepth& depth : this->m_autoDepths) {
        if (depth.m_width == width && depth.m_height == height) {
            return depth.m_dsv;
        }
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    AutoDepth depth = { width, height, nullptr, nullptr };

    if (FAILED(this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &depth.m_texture)) || FAILED(this->m_d3dDevice->CreateDepthStencilView(depth.m_texture, nullptr, &depth.m_dsv))) {
        SafeRelease(depth.m_texture);
        return nullptr;
    }

    this->m_autoDepths.push_back(depth);
    return depth.m_dsv;
}

void CGxDeviceD3d11::IBindTargets() {
    if (!this->m_ctx) {
        return;
    }

    // MODIFIED: modern lighting: the HDR scene while the world draws into it (IFrameRtv)
    ID3D11RenderTargetView* frameRtv = this->IFrameRtv();
    ID3D11RenderTargetView* rtv = frameRtv;
    ID3D11DepthStencilView* dsv = this->m_frameDsv;
    uint32_t width = this->m_frameWidth;
    uint32_t height = this->m_frameHeight;
    bool explicitDepth = false;

    CGxTex* colorTex = this->m_textureTarget[GxBuffers_Color].m_texture;
    CGxTex* depthTex = this->m_textureTarget[GxBuffers_Depth].m_texture;
    Texture* color = this->ITexGet(colorTex);
    Texture* depth = this->ITexGet(depthTex);
    uint32_t plane = this->m_textureTarget[GxBuffers_Color].m_plane;

    if (color && color->m_rtv[plane < 6 ? plane : 0]) {
        rtv = color->m_rtv[plane < 6 ? plane : 0];
        width = color->m_width;
        height = color->m_height;
        dsv = nullptr;
    }

    uint32_t depthWidth = width;
    uint32_t depthHeight = height;

    if (depth && depth->m_dsv) {
        dsv = depth->m_dsv;
        depthWidth = depth->m_width;
        depthHeight = depth->m_height;
        explicitDepth = true;
    } else if (color && rtv != frameRtv) {
        // a colour texture with no depth texture draws with a depth buffer of its size (D3D9's default surface)
        dsv = this->IAutoDepth(width, height);
    }

    // D3D11 binds colour and depth of the same size and sample count only: with a depth texture of another size the
    // draw writes depth alone (the engine's depth passes turn colour writes off)
    if (rtv && dsv && explicitDepth) {
        bool frame = rtv == frameRtv;

        if (depthWidth != width || depthHeight != height || (frame && this->m_samples > 1)) {
            rtv = nullptr;
            width = depthWidth;
            height = depthHeight;
        }
    }

    this->m_ctx->OMSetRenderTargets(rtv ? 1 : 0, &rtv, dsv);
    this->m_curRtv = rtv;
    this->m_curDsv = dsv;
    this->m_targetWidth = width;
    this->m_targetHeight = height;

    // a texture bound as a target reads as nothing
    for (uint32_t slot = 0; slot < 16; slot++) {
        ID3D11ShaderResourceView* srv = this->m_boundSrv[slot];

        if (srv && ((color && srv == color->m_srv) || (depth && srv == depth->m_srv))) {
            ID3D11ShaderResourceView* none = nullptr;
            this->m_ctx->PSSetShaderResources(slot, 1, &none);
            this->m_boundSrv[slot] = nullptr;
        }
    }

    this->m_viewportDirty = 1;
    this->m_scissorRectDirty = 1;
}

void CGxDeviceD3d11::DeviceSetRenderTarget(EGxBuffer buffer, CGxTex* texture, uint32_t plane) {
    if (!this->m_context) {
        return;
    }

    CGxDevice::TextureTarget& target = this->m_textureTarget[buffer];

    if (target.m_texture == texture && target.m_plane == plane) {
        return;
    }

    // a render target with mipmaps gets them made from what was drawn, when it is next read
    if (target.m_texture && target.m_texture != texture) {
        Texture* previous = this->ITexGet(target.m_texture);

        if (previous && previous->m_autoMips) {
            previous->m_mipsDirty = true;
        }
    }

    CGxDevice::DeviceSetRenderTarget(buffer, texture, plane);
    target.m_apiSpecific = nullptr;

    if (texture) {
        if (texture->m_needsCreation || !texture->m_apiSpecificData) {
            this->ITexCreate(texture);
        }

        Texture* created = this->ITexGet(texture);

        if (created) {
            // non-null marks a texture target, as the D3D9 device's surface does (viewport and scissor read it)
            target.m_apiSpecific = buffer == GxBuffers_Color ? static_cast<void*>(created->m_rtv[plane < 6 ? plane : 0]) : static_cast<void*>(created->m_dsv);
        }
    }

    this->IBindTargets();
    this->m_viewportDirty = 1;
    this->m_scissorRectDirty = 1;
}

void CGxDeviceD3d11::DeviceSaveDepthSurface() {
    if (this->m_context) {
        this->m_savedDsv = this->m_curDsv;
    }
}

void CGxDeviceD3d11::DeviceRestoreDepthSurface() {
    if (!this->m_context) {
        return;
    }

    this->m_ctx->OMSetRenderTargets(this->m_curRtv ? 1 : 0, &this->m_curRtv, this->m_savedDsv);
    this->m_curDsv = this->m_savedDsv;
}

void CGxDeviceD3d11::DeviceResolveDepthBuffer(CGxTex* texture) {
    if (!this->m_context || !texture) {
        return;
    }

    if (texture->m_needsCreation || !texture->m_apiSpecificData) {
        this->ITexCreate(texture);
    }

    Texture* created = this->ITexGet(texture);

    if (created && created->m_dsv) {
        this->m_ctx->OMSetRenderTargets(this->m_curRtv ? 1 : 0, &this->m_curRtv, created->m_dsv);
        this->m_curDsv = created->m_dsv;
    }
}

bool CGxDeviceD3d11::IBlit(ID3D11ShaderResourceView* source, uint32_t sourceWidth, uint32_t sourceHeight, const RECT& sourceRect, ID3D11RenderTargetView* dest, uint32_t destWidth, uint32_t destHeight, const RECT& destRect, bool linear) {
    if (!this->m_blitVs || !this->m_blitPs || !source || !dest || !sourceWidth || !sourceHeight || !destWidth || !destHeight) {
        return false;
    }

    float constants[16] = {};
    constants[0] = static_cast<float>(destRect.left) / destWidth * 2.0f - 1.0f;
    constants[1] = 1.0f - static_cast<float>(destRect.top) / destHeight * 2.0f;
    constants[2] = static_cast<float>(destRect.right) / destWidth * 2.0f - 1.0f;
    constants[3] = 1.0f - static_cast<float>(destRect.bottom) / destHeight * 2.0f;
    constants[4] = static_cast<float>(sourceRect.left) / sourceWidth;
    constants[5] = static_cast<float>(sourceRect.top) / sourceHeight;
    constants[6] = static_cast<float>(sourceRect.right) / sourceWidth;
    constants[7] = static_cast<float>(sourceRect.bottom) / sourceHeight;

    if (!this->m_utilityConstants) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 64;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        if (FAILED(this->m_d3dDevice->CreateBuffer(&desc, nullptr, &this->m_utilityConstants))) {
            return false;
        }
    }

    this->m_ctx->UpdateSubresource(this->m_utilityConstants, 0, nullptr, constants, 0, 0);

    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(destWidth), static_cast<float>(destHeight), 0.0f, 1.0f };
    ID3D11SamplerState* sampler = this->ISamplerState(CGxTexFlags(linear ? GxTex_Linear : GxTex_Nearest), false);
    ID3D11ShaderResourceView* none[16] = {};

    this->m_ctx->PSSetShaderResources(0, 16, none);
    this->m_ctx->OMSetRenderTargets(1, &dest, nullptr);
    this->m_ctx->OMSetBlendState(this->IBlendState(GxBlend_Opaque, 15), nullptr, 0xFFFFFFFF);
    this->m_ctx->OMSetDepthStencilState(this->IDepthState(D3D11_COMPARISON_ALWAYS, 0), 0);
    this->m_ctx->RSSetState(this->IRasterState(0, false, false, 0, false));
    this->m_ctx->RSSetViewports(1, &viewport);
    this->m_ctx->IASetInputLayout(nullptr);
    this->m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    this->m_ctx->VSSetShader(this->m_blitVs->m_vs, nullptr, 0);
    this->m_ctx->GSSetShader(nullptr, nullptr, 0);
    this->m_ctx->PSSetShader(this->m_blitPs->m_ps, nullptr, 0);
    this->m_ctx->VSSetConstantBuffers(0, 1, &this->m_utilityConstants);
    this->m_ctx->PSSetShaderResources(0, 1, &source);
    this->m_ctx->PSSetSamplers(0, 1, &sampler);
    this->m_ctx->Draw(4, 0);
    this->m_ctx->PSSetShaderResources(0, 1, none);

    for (ID3D11ShaderResourceView*& srv : this->m_boundSrv) {
        srv = nullptr;
    }

    this->IInvalidateBindings();
    this->IBindTargets();
    return true;
}

void CGxDeviceD3d11::DeviceCopyTex(CGxTex* source, uint32_t sourcePlane, CGxTex* dest, uint32_t destPlane) {
    if (!this->m_context || !source || !dest) {
        return;
    }

    if (source->m_needsCreation || !source->m_apiSpecificData) {
        this->ITexCreate(source);
    }

    if (dest->m_needsCreation || !dest->m_apiSpecificData) {
        this->ITexCreate(dest);
    }

    Texture* from = this->ITexGet(source);
    Texture* to = this->ITexGet(dest);

    if (!from || !to) {
        return;
    }

    UINT fromSub = D3D11CalcSubresource(0, from->m_cube ? sourcePlane : 0, from->m_levels);
    UINT toSub = D3D11CalcSubresource(0, to->m_cube ? destPlane : 0, to->m_levels);

    if (from->m_format == to->m_format && from->m_width == to->m_width && from->m_height == to->m_height) {
        this->m_ctx->CopySubresourceRegion(to->m_resource, toSub, 0, 0, 0, from->m_resource, fromSub, nullptr);
        return;
    }

    // StretchRect between formats or sizes: a draw
    ID3D11RenderTargetView* rtv = to->m_rtv[to->m_cube && destPlane < 6 ? destPlane : 0];

    if (rtv && from->m_srv && !from->m_cube) {
        RECT sourceRect = { 0, 0, static_cast<LONG>(from->m_width), static_cast<LONG>(from->m_height) };
        RECT destRect = { 0, 0, static_cast<LONG>(to->m_width), static_cast<LONG>(to->m_height) };
        this->IBlit(from->m_srv, from->m_width, from->m_height, sourceRect, rtv, to->m_width, to->m_height, destRect, false);
    }
}

int32_t CGxDeviceD3d11::TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane) {
    if (!this->m_context) {
        return 0;
    }

    if (dest->m_needsCreation || !dest->m_apiSpecificData) {
        this->ITexCreate(dest);
    }

    Texture* to = this->ITexGet(dest);

    if (!to || size.x <= 0 || size.y <= 0) {
        return 0;
    }

    // from the frame, as D3D9 copies from its default colour surface whatever target is bound
    this->IResolveFrame();

    D3D11_BOX box;
    box.left = sourcePos.x < 0 ? 0 : sourcePos.x;
    box.top = sourcePos.y < 0 ? 0 : sourcePos.y;
    box.right = box.left + size.x;
    box.bottom = box.top + size.y;
    box.front = 0;
    box.back = 1;
    box.right = box.right < this->m_frameWidth ? box.right : this->m_frameWidth;
    box.bottom = box.bottom < this->m_frameHeight ? box.bottom : this->m_frameHeight;

    if (box.right <= box.left || box.bottom <= box.top) {
        return 0;
    }

    uint32_t mipWidth = to->m_width >> level ? to->m_width >> level : 1;
    uint32_t mipHeight = to->m_height >> level ? to->m_height >> level : 1;

    if (static_cast<uint32_t>(destPos.x) + (box.right - box.left) > mipWidth) {
        box.right = box.left + (mipWidth - destPos.x);
    }

    if (static_cast<uint32_t>(destPos.y) + (box.bottom - box.top) > mipHeight) {
        box.bottom = box.top + (mipHeight - destPos.y);
    }

    // MODIFIED: modern lighting: from the HDR scene a copy converts, so it is drawn
    bool hdr = this->m_lighting && this->m_lighting->HdrActive();

    if (!hdr && to->m_format == DXGI_FORMAT_B8G8R8A8_UNORM && level < to->m_levels) {
        UINT sub = D3D11CalcSubresource(level, to->m_cube ? plane : 0, to->m_levels);
        this->m_ctx->CopySubresourceRegion(to->m_resource, sub, destPos.x, destPos.y, 0, this->m_frameResolved, 0, &box);
        return 1;
    }

    ID3D11RenderTargetView* rtv = to->m_rtv[to->m_cube && plane < 6 ? plane : 0];

    if (!rtv || level) {
        return 0;
    }

    RECT sourceRect = { static_cast<LONG>(box.left), static_cast<LONG>(box.top), static_cast<LONG>(box.right), static_cast<LONG>(box.bottom) };
    RECT destRect = { destPos.x, destPos.y, destPos.x + static_cast<LONG>(box.right - box.left), destPos.y + static_cast<LONG>(box.bottom - box.top) };
    return this->IBlit(this->IFrameSrv(), this->m_frameWidth, this->m_frameHeight, sourceRect, rtv, to->m_width, to->m_height, destRect, false) ? 1 : 0;
}

int32_t CGxDeviceD3d11::TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face) {
    if (!this->m_context) {
        return 0;
    }

    if (dest->m_needsCreation || !dest->m_apiSpecificData) {
        this->ITexCreate(dest);
    }

    Texture* to = this->ITexGet(dest);

    if (!to) {
        return 0;
    }

    ID3D11RenderTargetView* rtv = to->m_rtv[to->m_cube && face < 6 ? face : 0];

    if (!rtv || level) {
        return 0;
    }

    this->IResolveFrame();

    RECT from = { 0, 0, static_cast<LONG>(this->m_frameWidth), static_cast<LONG>(this->m_frameHeight) };
    RECT into = { 0, 0, static_cast<LONG>(to->m_width), static_cast<LONG>(to->m_height) };

    if (sourceRect) {
        from = { sourceRect->minX, sourceRect->minY, sourceRect->maxX, sourceRect->maxY };
    }

    if (destRect) {
        into = { destRect->minX, destRect->minY, destRect->maxX, destRect->maxY };
    }

    // CGxDeviceD3d9Ex::TexStretch: filtered unless the two rectangles are the same size
    bool linear = !(destRect && sourceRect && into.right - into.left == from.right - from.left && into.bottom - into.top == from.bottom - from.top);
    // MODIFIED: modern lighting: the HDR scene while the world draws into it
    return this->IBlit(this->IFrameSrv(), this->m_frameWidth, this->m_frameHeight, from, rtv, to->m_width, to->m_height, into, linear) ? 1 : 0;
}

void CGxDeviceD3d11::DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels) {
    this->ClampRectToWindow(rect);

    uint32_t width = rect.maxX > rect.minX ? rect.maxX - rect.minX : 0;
    uint32_t height = rect.maxY > rect.minY ? rect.maxY - rect.minY : 0;
    uint32_t count = width * height;
    pixels.SetCount(count);

    if (!count || !this->m_context || !this->m_frameResolved) {
        return;
    }

    // from the frame, as D3D9 reads its back buffer
    this->IResolveFrame();

    if (!this->m_staging || this->m_stagingWidth < this->m_frameWidth || this->m_stagingHeight < this->m_frameHeight) {
        SafeRelease(this->m_staging);

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = this->m_frameWidth;
        desc.Height = this->m_frameHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        if (FAILED(this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &this->m_staging))) {
            this->m_staging = nullptr;
            memset(pixels.Ptr(), 0xFF, sizeof(CImVector) * count);
            return;
        }

        this->m_stagingWidth = this->m_frameWidth;
        this->m_stagingHeight = this->m_frameHeight;
    }

    uint32_t right = rect.minX + width < this->m_frameWidth ? rect.minX + width : this->m_frameWidth;
    uint32_t bottom = rect.minY + height < this->m_frameHeight ? rect.minY + height : this->m_frameHeight;
    D3D11_BOX box = { static_cast<UINT>(rect.minX), static_cast<UINT>(rect.minY), 0, right, bottom, 1 };
    this->m_ctx->CopySubresourceRegion(this->m_staging, 0, 0, 0, 0, this->m_frameResolved, 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped;

    if (FAILED(this->m_ctx->Map(this->m_staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        memset(pixels.Ptr(), 0xFF, sizeof(CImVector) * count);
        return;
    }

    const uint8_t* source = static_cast<const uint8_t*>(mapped.pData);
    CImVector* dest = pixels.Ptr();
    uint32_t copyWidth = right - rect.minX;

    for (uint32_t y = 0; y < height; y++) {
        if (rect.minY + y < bottom) {
            memcpy(dest, source, sizeof(CImVector) * copyWidth);
        }

        source += mapped.RowPitch;
        dest += width;
    }

    this->m_ctx->Unmap(this->m_staging, 0);
}

void CGxDeviceD3d11::DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths) {
    // as CGxDeviceD3d9Ex: zero
    this->ClampRectToWindow(rect);

    uint32_t count = (rect.maxY - rect.minY) * (rect.maxX - rect.minX);
    depths.SetCount(count);
    memset(depths.Ptr(), 0, sizeof(float) * count);
}
