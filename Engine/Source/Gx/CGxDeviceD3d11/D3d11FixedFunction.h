#ifndef ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11FIXEDFUNCTION_H
#define ENGINE_SOURCE_GX_CGXDEVICED3D11_D3D11FIXEDFUNCTION_H

// MODIFIED: D3D11 device. D3D11 has no fixed-function pipeline, and the client draws much of its frame through
// D3D9's: the UI, the cursor, particles and ribbons, and every draw with no vertex or pixel shader bound. The state the
// D3D9 device would program (CGxDeviceD3d9Ex::IStateSyncLights / IStateSyncMaterial / IXformSetTex and the texture
// stage tables) is reduced to a key here, and each key is written out as an HLSL vertex or pixel shader, with the
// same interface as the translated shaders (s_hlslCommon) so either half pairs with a translated other half.

#include <cstdint>
#include <string>

namespace GxD3d11 {

enum EFfMaterialSource : uint8_t {
    FfSource_Material = 0,
    FfSource_Color0 = 1,
};

enum EFfLightType : uint8_t {
    FfLight_Off = 0,
    FfLight_Directional = 1,
    FfLight_Point = 2,
};

enum EFfTexGen : uint8_t {
    FfTexGen_None = 0,
    FfTexGen_Position = 1,     // D3DTSS_TCI_CAMERASPACEPOSITION
    FfTexGen_Reflection = 2,   // D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR
    FfTexGen_Normal = 3,       // D3DTSS_TCI_CAMERASPACENORMAL
};

// a vertex shader for the fixed-function transform, lighting, fog and texture coordinate generation
struct FfVertexKey {
    uint32_t m_attribMask;     // GxVA in the vertex layout
    uint32_t m_attribClass;    // 2 bits of EInputClass per GxVA
    uint8_t m_texCoordSize[8]; // components of each GxVA_TexCoord#
    uint8_t m_lighting;
    uint8_t m_lightTypes[4];   // EFfLightType
    uint8_t m_ambientSource;   // EFfMaterialSource
    uint8_t m_diffuseSource;
    uint8_t m_specularSource;
    uint8_t m_emissiveSource;
    uint8_t m_specular;        // D3DRS_SPECULARENABLE
    uint8_t m_normalize;       // D3DRS_NORMALIZENORMALS
    uint8_t m_fog;             // linear vertex fog
    uint8_t m_pointScale;      // D3DRS_POINTSCALEENABLE
    uint8_t m_stageMask;       // the TEXCOORD# written, one per texture stage
    uint8_t m_texGen[8];       // EFfTexGen
    uint8_t m_texCoord[8];     // the GxVA_TexCoord# a stage reads when not generated
    uint8_t m_texTransform[8]; // 0 none, else the coordinate goes through ffTexture[stage]
    uint8_t m_pad[2];
};

// a pixel shader for the fixed-function texture stages, specular add, fog blend and alpha test
struct FfPixelKey {
    uint8_t m_stageCount;      // stages that run; the cascade ends at the first without a texture
    uint8_t m_colorOp[8];      // EGxTexOp as the engine numbers it (0 Mod .. 5 BlendDiffuseAlpha)
    uint8_t m_alphaOp[8];
    uint8_t m_coord[8];        // TEXCOORD# a stage samples at
    uint8_t m_dim[8];          // 2 2d, 3 cube
    uint8_t m_projected[8];    // D3DTTFF_PROJECTED: divided by z
    uint8_t m_depth[8];        // a depth texture: compared at z
    uint8_t m_specular;        // add COLOR1 after the stages
    uint8_t m_pad[2];
};

// the fixed-function constants (cbuffer GxFixed, b2), as the device fills them
struct FfConstants {
    float m_worldViewProj[16];
    float m_worldView[16];
    float m_world[16];
    float m_normal[16];
    float m_texture[8][16];
    float m_matDiffuse[4];
    float m_matAmbient[4];
    float m_matSpecular[4];
    float m_matEmissive[4];
    float m_ambient[4];
    float m_matPower[4];
    float m_lightPos[4][4];
    float m_lightDiffuse[4][4];
    float m_lightAmbient[4][4];
    float m_lightSpecular[4][4];
    float m_lightAtten[4][4];
    float m_fog[4];
};

std::string FfVertexHlsl(const FfVertexKey& key);
std::string FfPixelHlsl(const FfPixelKey& key);
// the point list expanded into sprites: D3D9 draws a point as a square of its size in pixels, D3D11 as one pixel
std::string PointSpriteHlsl();
// the copy of the frame to the swap chain, with the gamma ramp of a full-screen window
std::string PresentHlsl();
// a textured quad into a render target, for TexStretch and copies between formats
std::string BlitHlsl();
// a clear of the viewport's rectangle: D3D9 clears the viewport (and the scissor), D3D11 whole views
std::string ClearHlsl();

}

#endif
