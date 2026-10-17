# Render backend plan (branch `feature/d3d11`)

Goal: replace the D3D9 fixed-function and shader path with a modern backend that can host real dynamic lighting,
shadow maps on every object (generalising `shadowMapSize` / `shadowStable`), SSAO, bloom and HDR tonemapping, with
better driver support on current GPUs.

## Decision: Direct3D 11, our own device

The backend is a D3D11 `CGxDevice` (`Engine/Source/Gx/CGxDeviceD3d11/`), chosen over Vulkan for this tree.
Upstream's Vulkan device (`SatyPardus/wowdecomp`, branch `vulkan`) is not used. D3D9 stays selectable and is the
default until D3D11 has been played on.

How it works, in one paragraph: the engine keeps talking to `CGxDevice` exactly as before. The device translates the
client's own DX9 shader bytecode to shader model 5 HLSL at run time (no offline step, nothing extracted from the
client), generates HLSL for every fixed-function state the engine uses, and reproduces D3D9's rasterisation and
render-target rules. Every effect in the later phases is then plain D3D11 work, in HLSL we own.

## Phase 0: a D3D11 device at D3D9 parity (done, needs play-testing)

| Part | State |
|---|---|
| Device, window, swap chain, resize, multisampling, `gxFixLag` | Built and run: login screen, live resize, 8× MSAA |
| DX9 → SM5 shader translation | All 1,578 SM2/SM3 records of the HD client translate and compile; SM1 `Terrain1w` refused |
| Fixed-function emulation (UI, cursor, particles) | Built; UI matches D3D9 pixel for pixel on the login screen |
| Hardware shadow maps (D24X8 compare) | Built; not yet seen in the world |
| Render targets, `TexCopy` / `TexStretch`, read-back | Built; not yet seen in the world (portraits, minimap, FFX, liquids use them) |
| awesome_wotlk UI shaders | Translated and running |
| MSDF fonts, frame capture | Off on D3D11 (they call `IDirect3DDevice9`); to port |

### Still to do in phase 0

1. **Play-test in the world** with `SET gxApi "D3D11"`, side by side with D3D9 at the same spot. Check each of:
   - terrain and its shadows (every `extShadowQuality`, `shadowMapSize`);
   - WMO interiors and the indoor minimap;
   - M2 models;
   - liquids;
   - weather (rain, snow, sand);
   - FFX glow and the death effect;
   - portraits (`portraitLive`);
   - spell effects;
   - the cursor;
   - full screen and alt-tab.

   Send `Logs\gx.log` and any `Logs\d3d11_*.hlsl` from a session that looks wrong.
2. **Per-draw CPU cost:** measure with `GetFrameTimings()` against D3D9 in a busy city. Consecutive draws already reuse
   the last fixed-function program and input layout. The next candidates are the fixed-function constants (rebuilt
   every fixed-function draw) and the 4 KB constant upload of skinned M2 draws.
3. **MSDF fonts and frame capture:** port their D3D9 calls (atlas textures, offscreen targets, read-back) to the
   device's own texture and target paths.

Done in phase 0 beyond parity: shaders compile on worker threads as the engine loads them, so a first session does not
stall where a new effect appears (about 1,050 of them are ready after the login screen alone).

## Phase 1: HDR scene target and a post-process hook

1. Render the world into an `R16G16B16A16_FLOAT` target with a sampleable depth buffer. The device already draws the
   frame offscreen and copies it at present, so this is a second target for the world pass.
2. Insert a post chain between the world and the UI. The hook is the end of the world pass in
   `CGWorldFrame::OnWorldRender` / `RenderWorld` (`WoW/Source/Ui/WorldFrame.cpp`), before the UI batches;
   `CWorldScene::Render` (`WoW/Source/WorldClient/CWorldSceneRender.cpp`) owns the pass order. The UI keeps drawing
   in LDR on top.
3. The last step is a tonemap and gamma resolve. With every effect off, the output must match today's exactly.

## Phase 2: cheap modern effects

- **Anisotropic filtering:** the sampler path already takes the texture's anisotropy.
- **Anti-aliasing:** FXAA or SMAA as a post pass.
- **Bloom:** a threshold, downsample and blur chain on the HDR target.
- **SSAO:** GTAO from depth with reconstructed normals, before the tonemap.

## Phase 3: own the main shaders

The translated shaders are 1:1 D3D9 programs and cannot read a shadow map or a light list. Hand-written HLSL
replacements are needed for the main families:
- terrain (`Terrain*`);
- WMO (`MapObj*`);
- M2 (`Combiners_*` / `Diffuse_*`);
- liquids.

Each replacement is keyed by engine shader name in the device's shader table. Any shader without one keeps its
translation, so the stock path remains the fallback family by family.

## Phase 4: shadows on every object

Sample the existing `CShadowCache` cascades (`shadowMapSize`, `shadowStable`) in the replacement shaders instead of the
projected blob pass, and add M2 and WMO casters to the cascades.

## Phase 5: dynamic lighting

Clustered forward shading with a light list in a structured buffer. The lights come from M2 light records, WMO
interior lights and spell visuals.

## Ground rules

- Every edit outside `CGxDeviceD3d11/` carries `// MODIFIED:`, and `MODIFICATIONS.md` has a section per phase.
- Every new effect gets a CVar that defaults to off until it has been seen in game. D3D9 stays selectable.
- Nothing is pushed to `origin` (Badgermilk0/wowdecomp) until the user says so.
