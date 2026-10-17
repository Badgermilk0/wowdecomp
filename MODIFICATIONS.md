# Modifications to this wowdecomp tree

This tree is the `SatyPardus/wowdecomp` commit that `CLIENT/wowdecomp` holds, plus the changes below.
`CLIENT/wowdecomp` stays the untouched reference. Every engine edit carries a `// MODIFIED:` comment, so
`git diff` and `grep -rn "MODIFIED"` list them all. Never push this tree: `origin` is the third-party upstream.

## What was added

### Earlier WoW.exe binary patches, now in source

| Patch | Where | Notes |
|---|---|---|
| HD unit-frame portraits (`CLIENT/hd-portrait-patch`, 256×256 instead of 64×64) | `WoW/Source/UIUtil/PortraitButton.*`, CVar `portraitResolution` | Default 256, range 64–2048, rounded up to a power of two. Takes effect live. Only the render-target path changes; the circular mask is still the stock 64 px texture, filtered. |
| Projected-texture depth range 2.0 → 50.0 (awesome's byte patch at `0x820809`) | `Engine/Source/Model2/CM2SceneRender.cpp`, `WoW/Source/WorldClient/shadow.cpp` | One constant in the exe feeds both functions; both changed. |

### awesome_wotlk (https://github.com/noname08662/awesome_wotlk), ported natively

`awesome_wotlk` is a DLL that detours the 32-bit `Wow.exe`. Here each detour is a direct call into
`WoW/Source/Awesome/` from the function it hooked. **`AwesomeWotlkLib.dll` and its loader are not needed and
must not be used with this exe.** Addons see `AwesomeWotlk = 38`, the same as upstream v38.

| Feature | Files | CVars (default) | Lua API |
|---|---|---|---|
| Unit API | `AwesomeUnit.cpp` | — | `UnitIsControlled`, `UnitIsDisarmed`, `UnitIsSilenced`, `UnitOccupations`, `UnitOwner`, `UnitTokenFromGUID` |
| Item / spell API | `AwesomeItem.cpp` | — | `GetItemInfoInstant` (now falls back to Item.dbc on a cache miss), `GetInventoryItemTransmog`, `GetSpellBaseCooldown` |
| Command-line login | `AwesomeCommandLine.cpp` | — | `-login`, `-password`, `-realmlist`, `-realmname`, `-character` |
| Accepted TOS/EULA | `Glue/CGlueMgr.cpp` | — | — |
| Camera | `AwesomeCamera.cpp`, `UIUtil/Camera.cpp` | `cameraFov` (100), `showPlayer` (1) | — |
| Camera indirect visibility: doodads between the character and the camera fade instead of pulling the camera in | `AwesomeCamera.cpp`; `CM2Model` fade fields; `CMap` ray skips; `CMap::s_lastHitModelOwner` | `cameraIndirectVisibility` (0), `cameraIndirectAlpha` (0.6) | — |
| Interaction key / `/interact` | `AwesomeMisc.cpp` | `interactionMode` (1), `interactionAngle` (60), `interactionHighlight` (1) | `QueueInteract`, binding `INTERACTIONKEYBIND` |
| `[@cursor]` / `[@playerlocation]` macro targets | `AwesomeMisc.cpp`, `Ui/UIMacros_Scripts.cpp`, `Ui/WorldFrame.cpp`, `Spell_C.cpp` | — | — |
| Object highlight | `AwesomeMisc.cpp` | `objectHighlightMode` (0) | — |
| Per-session chat/combat log names | `AwesomeMisc.cpp` | `chatLogSessionKey` (1), `combatLogSessionKey` (1) | — |
| Window / clipboard | `AwesomeMisc.cpp`, `Os/Win64/OsClipboard.cpp` (UTF-8 clipboard) | — | `FlashWindow`, `IsWindowFocused`, `FocusWindow`, `CopyToClipboard` |
| Text-to-speech (SAPI) | `AwesomeVoiceChat.cpp` | `ttsVoice`, `ttsSpeed` (0), `ttsVolume` (100) | `C_VoiceChat.*`, `C_TTSSettings.*`, `VOICE_CHAT_TTS_*` events |
| Nameplates: stacking, clamping, mouse freeze, occlusion, `nameplateN` tokens | `AwesomeNamePlates.*`, `Ui/NamePlateFrame.*`, `Object_C/Unit_C.cpp`, `Ui/GameUI.cpp` | the 29 `nameplate*` CVars (e.g. `nameplateDistance` 41, `nameplateStacking` 0, `nameplatePlacement` 0) | `C_NamePlate.*`, plate methods, `NAME_PLATE_*` events |
| UI half-pixel fix | `Services/Camera.cpp` | `uiHalfPixelFix` (**1**) | — |
| UI footprint texture sampling (HLSL pixel shaders) | `AwesomeUiFixes.cpp`, `AwesomeShaders.cpp`, `Frame/CSimpleRender.cpp`, `Gx/CGxDevice` | `uiTextureSampling` (**1**: 0 engine, 1 box, 2 tent) | — |
| UI pixel snap (HLSL vertex shader + sub-pixel quad widening) | same | `uiPixelSnap` (**1**) | — |
| MSDF font rendering: text drawn from multi-channel signed distance fields, sharp at any size, with SDF outlines | `Msdf/` (see below), `Gxu/GxuFontMsdf.*` and hooks in `Gxu/GxuFontString.cpp`, `GxuFontFace.cpp`, `IGxuFontGlyph.cpp`, `GxuFontMiscClasses.cpp`, `GxuFontMain.cpp` | `MSDFMode` (**1**: 0 off, 1 on, 2 also fonts that fail validation; read at startup), `MSDFOutlinePass` (1) | `/msdfpregen` |
| Frame capture: one frame tree rendered offscreen to a transparent PNG in `Screenshots\` | `AwesomeFrameCapture.cpp`; filters in `Frame/CSimpleTop.*`, `CSimpleFrame.cpp`, `CRenderBatch.*`, `Ui/CGCooldown.cpp` | — | `CaptureFrame(frame[, size])`, `/fcapture [name] [2048\|2x]` (`/framecapture`) |

The HLSL shaders compile at load time with `d3dcompiler_47.dll`, which ships with Windows 10/11. They need
shader model 3 and a D3D9 device. Without SM3, or on OpenGL, the stock `.bls` shaders load instead, and MSDF and
frame capture stay off.

**Defaults that change the look:** `uiHalfPixelFix`, `uiTextureSampling`, `uiPixelSnap` and `MSDFMode` default to on,
as in awesome's current source. Set any of them to `0` for the stock look. The three UI fixes take effect live;
`MSDFMode` needs a restart.

#### MSDF fonts, how this port differs from awesome

- **FreeType stays the client's own 2.0.** Upstream swaps in FreeType 2.14 for the whole client and builds msdfgen's
  FreeType import against it. Here the engine keeps FreeType 2.0 (its glyph flags already match what upstream forces),
  and `Msdf/MsdfFreetype.cpp` reimplements the part of msdfgen's font import the MSDF code uses on top of 2.0.
- **No Skia.** msdfgen is built core-only, so `resolveShapeGeometry` is gone; overlapping contours rely on the
  generator's `overlapSupport`, and the validator already keeps self-intersecting faces (`MSDFMode` 1) on the stock path.
- **Glyph tagging in source.** Upstream writes the code point into the raster glyph's atlas cell and forces every glyph
  onto atlas page 0, which disturbs the engine's glyph-row bookkeeping. Here `CGxString::InitializeTextLine` puts the
  code point into the quad and the quad on page 0 for MSDF fonts only, and the raster atlas is left alone.
- **Engine hooks instead of detours:** `Gxu/GxuFontMsdf.h` (`g_gxuFontMsdf`, filled by `Msdf/AwesomeMsdf.cpp` at
  `GxuFontInitialize`), two `CGxString` fields for the pending/atlas-token state (upstream packs them into
  `m_texturePagesUsed`), the font index buffer sized by `g_gxuFontIndexQuads`, and the font shaders named by
  `g_fontShaderName`.
- Unchanged from upstream: the glyph cache in `Cache_AwesomeWotLK\` (same binary format, so a cache the awesome DLL
  wrote is reused), the font blacklist folder `Fonts_AwesomeWotLK\`, the background scan of AddOn fonts, the
  `/msdfpregen` console for CJK locales, and the shaders (plus a stereo entry point for the second permutation).

The `Msdf/` files from awesome (`MSDFCache`, `MSDFManager`, `MSDFFaceSweep`, `MSDFPregen`, `MSDFUtils`, `MSDFValidator`,
`MSDFFont`, `MSDFShaders`) are copied with only the changes above and 64-bit fixes; `MsdfSupport.h` stands in for the
awesome tool headers they included. The Awesome library is compiled as C++23 with `/permissive` (the decomp headers
need it); the MSDF files that never include a decomp header are compiled `/permissive-`.

### Third-party code added

| Library | Where | License |
|---|---|---|
| msdfgen 1.13 (core only) | `ThirdParty/msdfgen` | MIT |
| unordered_dense 5.3.1 | `ThirdParty/unordered_dense` | MIT |
| stb_image_write 1.16 | `ThirdParty/stb` | MIT or public domain |

awesome_wotlk itself is **GPL-3.0**, and everything in `WoW/Source/Awesome/` is ported from it. That is no concern for
a build you use yourself; anyone given this exe or its source would get it under the GPL.

### Not ported

| awesome feature | Why not |
|---|---|
| **Scan.dll login bypass** | Not done on purpose; see below. |

### Performance (x64)

| Change | Where | Effect |
|---|---|---|
| CPU and GPU overlap with `gxFixLag` | `Gx/GxFrameLatency.*`, `ScenePresent` in `CGxDeviceD3d`/`CGxDeviceD3d9Ex`, CVar `gxMaxFrameLatency` (1; 0 = stock) | `gxFixLag` (default from `VideoHardware.dbc`, on for GPUs the 2010 client does not know) waited at every Present until the GPU had finished the frame just submitted, so CPU and GPU took turns and a frame cost both their times. It now waits for the frame `gxMaxFrameLatency` back: the CPU builds the next frame while the GPU draws the last, and the queue stays one frame deep so the cursor still does not trail. |
| Frame time breakdown | `Gx/GxFrameLatency.*`, `Awesome/AwesomePerf.cpp` | `GetFrameTimings()` / `/frametimes`: smoothed ms for the game's CPU work, the GPU wait, the frame cap and Present (vsync blocks there), to see what limits the frame rate. |
| Lua allocator: O(1) allocation and O(log n) free | `lua-5.1/src/lmemPool.*` | The stock pool scanned every 1024-block chunk on each allocation and each free, so Lua work and garbage collection slowed down as addons grew the Lua heap (the 3.3.5a "addon lag"). Chunks with a free block are now kept in a list, and chunks are sorted by address for the free. Offline benchmark of the stock and new pools under one workload (same results, checked against a shadow map): 1.6× faster at 1.5M live objects, 2.9× at 4M. |
| Lua GC pause 110 → 200 | `FrameScript/FrameScript.cpp` | Lua's own default: a new collection cycle once the heap has doubled, not at +10%. Much less collector work for a larger Lua heap; addon memory figures read higher. |
| Precise frame cap | `Gx/CGxDevice/CGxDevice.cpp` (`WaitForFPSCap`), `Os/OsWait.cpp` | The stock cap slept whole milliseconds with `Sleep()`, which the default 15.6 ms timer rounds up, so `maxfps`/`maxfpsbk` overslept and stuttered. Now paced on the high-resolution counter with a steady cadence, waiting on a high-resolution waitable timer and yielding for the last half millisecond. |
| 1 ms timer resolution | `Os/Win64/TimeManager.cpp` | `timeBeginPeriod(1)` for the process, so every `Sleep(1)` in the streaming and file code wakes after 1 ms instead of 15.6. |
| No startup timer calibration | `Os/Win64/TimeManager.cpp` | Skips a quarter-second busy wait at time-critical priority (and 512 core hops) checking that QueryPerformanceCounter agrees across CPUs; 64-bit Windows guarantees it. |
| Texture object pool 64 MB → 512 MB | `Services/Texture.cpp`, `textureCacheSize` default 32 → 256 | Released textures are kept for reuse instead of being destroyed and recreated as the camera moves; the 64 MB ceiling was set by the 32-bit address space. 512 MB needs 4 GB of RAM. |
| Sound cache 4 → 32 MB | `SoundEngine/SoundCache.cpp` (`SoundMemoryCache`) | Repeated sounds come from memory. Its purge list moved from the stack to the heap: the stock `alloca` capped the budget and overflowed when more chunks than its estimate were loaded. |
| Hash tables grow to 64K slots (stock 8K) | `Storm/H/stpl.h` | Large tables (objects, item and name caches, doodad definitions, textures) keep short chains. |
| Storm allocator without `_msize` | `Storm/Source/SMem.cpp` | Each allocation and free called `_msize` (taking the heap lock) for heap tracking that has no effect in Release. |
| Whole-program optimisation | `CMakeLists.txt` | `/GL` + `/LTCG`, `/Gy /Oi`, `/OPT:REF /OPT:ICF` for Release x64 (`WOWDECOMP_LTCG`, on). Opt-in: `WOWDECOMP_AVX2` (`/arch:AVX2`, needs a 2013+ CPU) and `WOWDECOMP_FAST_FLOAT` (`/fp:fast`, what the original used, but the decomp's float edge cases are unchecked). |

All of these keep the Win32 layout build as it was. Investigated and left alone: model animation is already on its own
thread (`M2UseThreads`, default on); spreading it over several cores needs the animation code made thread-safe, which
cannot be verified without running the client. The D3D9 device is already created pure, with hardware vertex
processing and no multithreading lock. `processAffinityMask` defaults to no restriction.

## Building

Requirements: Visual Studio 2026 (v18) with the C++ desktop workload, and CMake 4.x. Build x64 only.

```
cmake -S . -B build/x64 -G "Visual Studio 18 2026" -A x64 -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON
cmake --build build/x64 --config Release --target WowClient --parallel
```

`WOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON` lets modified MPQs (HD patches) and edited interface files load. Leave it
`OFF` for a stock data folder. The output is `bin/Win/WowClient64.exe`. `build/` is gitignored. Whole-program
optimisation makes a full Release build and its link noticeably slower; `-DWOWDECOMP_LTCG=OFF` for quick iterations.

## Running

1. Copy the client's `Data` folder to `bin/Win/Data`, or copy `WowClient64.exe` and `XvidDecoder.dll` into a copy of
   the client folder.
2. Use a **64-bit** `d3d9.dll` (DXVK's x64 build) or none at all; a 32-bit DXVK `d3d9.dll` will not load into a 64-bit exe.
   The same applies to any other DLL the 32-bit client loads from its folder.
3. Do not install `AwesomeWotlkLib.dll` or its patcher. Addons written for awesome (including its CVar settings
   addon) work unchanged.
4. `Data/<locale>/realmlist.wtf` or `-realmlist` picks the server.
