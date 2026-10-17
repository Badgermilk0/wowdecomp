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

### The HD client's Wow.exe patches

The "WoW 3.3.5a HD" client this build replaces ships a patched 32-bit `Wow.exe` (and the stock one as
`wow (original).exe` in its `reserved.zip`). A section-by-section diff of the two on 2026-10-17 found 45 changed runs.
Each is listed here with what became of it, so this build does everything that exe did. The CVars are in
`Awesome/AwesomeHdClient.cpp` and default to the HD behaviour; set one to `0` for stock.

| Patch (32-bit address) | Here | CVar |
|---|---|---|
| Far clip caps 791.7 / 1583.3 → 10000 (`World::ValidateFarClip`, 0x780770; constants at 0xA3E70C) | `WorldClient/World.cpp`; `farClipOverride` no longer matters | — |
| `groundEffectDist` max 140 → 3166.7 (0x78DB10; 0x9E8CFC). Stock rejects a larger value, and the line then vanishes from Config.wtf | `WorldClient/WorldParam_CVar.cpp` | — |
| `horizonFarclipScale` max 6 → 12 (0x78D7C0) | same | — |
| `environmentDetail` 0.5–1.5 clamp removed (0x78DC60) | same | — |
| Cast bars on every nameplate, not only the target's (`UpdateSpellCastBars` 0x720E73/82, `Target` 0x524DC6, `ClearTarget` 0x524276) | `Object_C/Unit_C.cpp`, `Ui/GameUI.cpp` | `nameplateAllCastBars` (1) |
| The camera does not fade the character it is zoomed in on (`SetMaxAlpha` 0x737441) | `Object_C/Unit_C.cpp` | `cameraPlayerFade` (0) |
| Say, yell and emote while dead (`Script_SendChatMessage` 0x50D641) | `Ui/ChatFrame_Scripts.cpp` | `chatWhileDead` (1) |
| Forced reactions kept through `LeaveWorld` (0x528D1E skips `CGReputationInfo::LeaveWorld`) | `Ui/ReputationInfo.cpp`: only the forced reactions are kept; `EnterWorld` rebuilds the faction table anyway | — |
| Pending item queries sent every pump, not every 30 s (`DBCache<ItemStats_C>::Idle` 0x6695FD) | `DB/DBClient/DBCache.h` | — |
| `CheckInbox` refresh throttle 60 s → 1 ms (0x56E49A) | `Ui/MailInfo_Scripts.cpp` | — |
| Area triggers checked every 50 ms, not 100 (`AreaTriggerCheck` 0x6DBE41) | `Object_C/Player_C.cpp` | — |
| Second moon drawn white instead of black (DayNight static init 0x9D07C0 sets `g_planets.m_moon2.m_color`) | `WorldClient/DayNight.cpp` | — |
| Patch archives `patch-*.MPQ` / `patch-<locale>-*.MPQ` instead of `?` (strings 0x9E2709/16) | `WowServices/PatchFiles.cpp` | — |
| Loose `Interface\GlueXML` / `FrameXML` never moved aside (`DeleteInterfaceFiles` 0x5F4DBF) | `UIUtil/AddOns.cpp`, with `WOWDECOMP_DISABLE_SIGNATURE_CHECKS` | — |
| FrameXML/.toc signature check off (0x816625–0x81675F) | already: `WOWDECOMP_DISABLE_SIGNATURE_CHECKS` | — |
| Windowed-mode gamma, melee swing on right-click, NPC turn animation, pre-cast animation (0x401A94, 0x6E2867, 0x73E3C9, 0x73ECD6) | already in the decomp (`WoWFix335` comments) | — |
| Mouselook keeps the cursor where it was (`CenterMouse` 0x869DB1, `OsWindowProc` 0x86A62C + cave 0x9296A2) | already in the decomp (upstream 5b41f72, `Os/Win64/Input.cpp`) | — |
| HD unit-frame portraits (0x616E0A, 0x6176A0, 0x6180E9, 0x619B73, 0x619FAF, section `.hdp`) | already: `portraitResolution` | — |
| Loads `AwesomeWotlkLib.dll` (`MainFiberProc` 0x40B7D0 → cave over `CGlueMgr::ScanDLLStart` 0x4E5CB0; `Script_ScanDLLStart` 0x4DCCF0 stubbed because that function is gone) | awesome is built in (above) | — |
| `FlashWindow()` Lua function written over `BNRemoveFriend` (0x534ED5, name at 0xA086E4) | awesome's `FlashWindow` (above); `BNRemoveFriend` stays | — |

Three runs are not ported because they are byte corruption, not features: they fall mid-instruction in code that has
nothing to do with any feature, and running them would crash or hang. Probably a patcher written for another exe.

| Address | What it breaks |
|---|---|
| 0x414D37 | the CRT's 64-bit unsigned divide (`mul [esp+0x18]; add edx,ecx` becomes `mul [esp+0x10]; daa; …`) |
| 0x4361BF | a `std::string` replace: its `call memmove` becomes `jmp` to itself minus 3 bytes |
| 0x5DE85D | `Script_GetTradeSkillTools`: the search flags passed to `FindTotemCategory` go from 0 (equipped + bags + backpack) to 0xEB, which drops the backpack (flag 0x4), so a tool there would show as missing |

### Port fixes (bugs in the decompilation itself)

| Bug | Where | Symptom |
|---|---|---|
| `SI2::PlaySoundFile` passed `Create2D` its last three arguments shifted: reverb 0, priority 128/100, decodeBufferSize `0xFFFFFFFF`. The original (call at 0x4C91BF) passes reverb 128/100, priority -1 and decodeBufferSize 0 | `SoundInterface/SoundInterface2ZoneSounds.cpp` | Every Lua `PlayMusic` / `PlaySoundFile` (login-screen music, addon music and sounds) failed: FMOD read the whole file trying to fill a 4 GB decode buffer and gave up after about 6 s with `FMOD_ERR_INVALID_PARAM`. FMOD loads every sound on one thread, so sound effects queued behind it played late or not at all, and quitting hung while that thread was busy. Found with the sound trace below; reproduced and confirmed with the client's FMOD library outside the game. Present in upstream too. |
| `Script_GetGossipOptions` (0x58A9E0) ended its walk over `CGGossipInfo::m_options[32]` at the address of `m_gossipText`, which only works because the 32-bit exe's linker placed that buffer right after the array. Now walks the array by its own length | `Ui/GossipInfo_Scripts.cpp` | Talking to a gossip NPC (innkeepers, etc.) crashed the x64 client: the walk ran into unrelated statics and passed a garbage `m_text` to `lua_pushstring`, an access violation in `strlen` (crash log 2026-10-17, `Current Addon function: GetGossipOptions`). Present in upstream too. |

### Diagnostics

| Option | Effect |
|---|---|
| `-DWOWCLIENT_CRASH_CONSOLE=ON` (off by default) | Upstream's crash logger (`Tools/WowClient/crash_log.cpp`, `WOWCLIENT_CRASH_LOG`) opened a console window at every start. It now only writes `WowClient_crash.log`; this option brings the live console back. |
| `-DWOWDECOMP_SOUND_TRACE=ON` (off by default) | `Logs\SoundTrace.log`: every sound's request (`kit`, `glue music`), create, file open, reads near the start of a file and every failed read (position and first bytes), seeks, non-blocking load result, play (volume), stop, end and virtual kill, plus FMOD's channel count every 2 s. Also writes every byte FMOD reads for the first file over 1 MB to `Logs\SoundDump.bin`, to compare with the file in the MPQ. Build it in its own folder (`build/x64-trace`); it writes to the same `bin/Win`. |

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

### Graphics (x64)

| Change | Where | CVars (**default**) | Effect |
|---|---|---|---|
| Shadow map size | `ShaderEffect/ShadowCache.*` (`CShadowCache::Init`), `Awesome/AwesomeShadows.cpp` | `shadowMapSize` (**4096**; 0 = stock, else rounded up to a power of two from 1024 to 8192) | Every shadow map is this size: the near 40-yard one and the three cascades (80, 320, 1280 yards across). Stock is 1024, or 2048 from `extShadowQuality` 2 and 4 up, so 4096 halves the size of a shadow texel. With PCF filtering only, the default on SM3 cards. The R32F path draws with the screen's depth buffer, which a larger map would overrun. Takes effect live. Memory is about nine maps of size² × 4 bytes: ~600 MB at 4096, ~2.4 GB at 8192. |
| Shadow softness | `CShadowCache::InitPcfOffsets` | `shadowSoftness` (**1**, 0.25–4) | Scales the eight PCF tap offsets. At 1 the kernel stays about two texels, so a bigger map also gives sharper edges. Raise it to soften them again. |
| Stable shadows | `CShadowCache::GatherBands` / `Update`, `SnapToShadowTexels` | `shadowStable` (**1**) | The cascades (`extShadowQuality` 5) and the near map are centred on whole texels of the sun's view plane, not on whole-yard world cells (2/4/16 yards) or 1/size-yard steps. The texel grid then stays put in the world, and shadow edges no longer crawl and flicker while you move. They still creep slowly as the sun moves. |
| Live 3D unit portraits | `UIUtil/PortraitButton.*` (`LivePortraitSet`, `UpdateLivePortraits`), called each frame from `Ui/WorldFrame.cpp` (`OnWorldUpdate`); `Frame/CSimpleTexture` `s_destroyCallback`; `Awesome/AwesomeMisc.cpp` | `portraitLive` (**1**; 0 = the stock snapshot), `/liveportraits [on\|off]` | `SetPortraitTexture` on a unit no longer takes one picture: the portrait keeps its own copy of the unit's model (as `PlayerModel:SetUnit` does) playing the idle animation, and redraws it into the portrait's render target every frame through the stock portrait camera, lighting and circular mask, at `portraitResolution`. So every frame that uses `SetPortraitTexture` animates (player, target, focus, both targets-of-target, pet, party, boss, arena, and addon unit frames) with no change to FrameXML. One copy per unit GUID, shared by every texture showing it; it is drawn only while one of those textures is visible, follows the unit's model on shapeshift, display or gear change, keeps animating after the unit leaves view, and is freed once no texture shows it. Up to 32 at once; beyond that, and for units with an icon portrait, party pets (no unit object) and the 64×64 readback path (no render-target alpha), the stock snapshot stays. Takes effect live. Cost per visible portrait is about that of one `PlayerModel` frame. Don't also run the `HDPortraits` addon: it lays `PlayerModel` frames over the same textures. |
| Indoor minimap resolution | `Ui/MinimapFrame.*` (`CreateIndoorTexture`, `MinimapSetIndoorResolution`), `Awesome/AwesomeMisc.cpp` | `minimapIndoorResolution` (**1024**, 256–2048, rounded up to a power of two) | Inside a WMO (caves, dungeons, buildings) the minimap is drawn into a square render target covering three times the visible radius, and only its middle third is shown. Stock 256 left about 85 texels across the minimap, 3.5× coarser than the WMO minimap textures (0.5 yd per texel) at the widest zoom. 1024 keeps their full detail at every zoom; 2048 adds nothing visible. Takes effect live. The outdoor minimap has no render target: it draws the `Textures\Minimap` tiles directly, so only higher-resolution tile files sharpen it (tile texture coordinates are fractions, so any power-of-two size displays correctly). |

### D3D11 device

A Direct3D 11 device beside the D3D9 ones, behind the same `CGxDevice` interface: `SET gxApi "D3D11"` in
`Config.wtf`, or `-d3d11` on the command line. The 3.3.5a binary only names the API (`GxApi_D3d11`). D3D9 stays the
default and is unchanged. Needs a feature level 11_0 GPU and Windows 10 or 11 (`d3dcompiler_47.dll` is part of them).
The plan beyond this first step is `RENDER_BACKEND_PLAN.md`.

| Part | Where | What it does |
|---|---|---|
| Device | `Gx/CGxDeviceD3d11/CGxDeviceD3d11*.cpp` | `CGxDeviceD3d9Ex` rebuilt on D3D11. It reproduces D3D9's behaviour, not D3D11's defaults: the viewport and scissor arithmetic (including a texture target's unflipped y), buffer locks with D3D9's discard / no-overwrite ring for stream pools, texture uploads through the engine's texel callbacks, occlusion queries, `gxFixLag` (event queries, `gxMaxFrameLatency`), screenshots and `GxReadPixels`, and `GetFrameTimings()`. |
| Rasterisation | `GxFinishVertex` in `D3d11Dx9Shader.cpp` | Every vertex moves half a pixel, so the pixels a triangle covers are D3D9's (D3D9 samples a pixel at its whole coordinate, D3D11 at its centre). UI text and borders land on the same pixels as on D3D9. |
| The client's shaders | `Gx/CGxDeviceD3d11/D3d11Dx9Shader.*` | The DX9 bytecode (`vs_2_0`/`vs_3_0`, `ps_2_0`/`ps_3_0`, as the caps report shader model 3) is translated to shader model 5 HLSL: every SM2/SM3 instruction, relative addressing, loops, subroutines, predicates. Varyings meet by D3D9 semantic in one struct shared by every shader, so any pair links. All 1,578 SM2/SM3 records of the HD client translate and compile; the 7 SM1 `Terrain1w` ones (a legacy fallback) are refused, as upstream's Vulkan translator refuses them. |
| Hardware shadow maps | same | A sampler bound to a D24X8 texture compares the coordinate's z with it (D3D9's driver behaviour the `_pcf` shaders rely on): `SampleCmpLevelZero` with a filtered comparison sampler. The `shadowMapSize` maps stay on the PCF path; a colour target always gets a depth buffer of its own size, so the R32F path is not limited by the screen's either. |
| Fixed function | `Gx/CGxDeviceD3d11/D3d11FixedFunction.*` | D3D11 has none; the UI, cursor, particles and ribbons use D3D9's. Its state (lights and material sources as `IStateSyncLights` / `IStateSyncMaterial` set them, texture generation and the texture matrices of `IXformSetTex`, linear vertex fog, the six stage operations, the specular add, the fog blend, the alpha test) is reduced to a key and written out as HLSL. Points (the snow and sand weather) become sprites in a geometry shader. |
| Shader cache | `Gx/CGxDeviceD3d11/D3d11Compiler.*` | `D3DCompile`, cached under `Cache\D3D11Shaders\` by a hash of the source. A shader compiles when a draw first needs it, about 20 ms each, so a first session hitches where new effects appear; later sessions read the cache. Deleting the folder is safe. |
| The frame | `CGxDeviceD3d11.cpp` | An offscreen colour target (multisampled when `gxMultisample` asks), copied to a flip-model swap chain at present, with tearing when `gxVSync` is off. Full screen is a borderless window over the primary monitor at its own resolution, not an exclusive mode; the gamma ramp is applied in the copy there, as D3D9 set it on the display only in full screen. Resizing the window resizes the targets live. |
| Constants | `CGxDeviceD3d11Draw.cpp` | The constant registers are kept as D3D9's hardware would hold them and uploaded as slices of one ring buffer (D3D11.1 offsets; plain buffers without them). |
| awesome_wotlk | `Awesome/AwesomeShaders.cpp` | The UI sampling and pixel-snap shaders compile to the same bytecode and are translated, so `uiTextureSampling` and `uiPixelSnap` work on D3D11. MSDF fonts and frame capture call `IDirect3DDevice9` themselves and stay off. |

Not on the D3D11 device: stereo (NVAPI), `DeviceReadDepths` (zeros, as the D3D9 device returns), indexed triangle fans
(the client draws none; a draw would be skipped and logged), and full-screen exclusive mode.

Checked so far, against the HD client's own data: every shader record through the translator and the compiler, and the
login screen in five of its random scenes (skinned and lit M2 models, particles, additive glows, the UI), windowed,
resized live, and with 8× multisampling and `gxFixLag`. The UI matches D3D9's pixel for pixel. **Not yet seen in the
world:** terrain, WMOs, liquids, shadows, weather, FFX glow, portraits and the minimap all go through paths the login
screen does not reach.

Debugging: `WOW_D3D11_DEBUG=1` in the environment turns on the D3D11 debug layer (the Graphics Tools optional feature).
`Logs\gx.log` names the adapter and any shader that did not translate; a shader that does not compile leaves its HLSL
and the compiler's message in `Logs\d3d11_<hash>.hlsl`, and the draws that need it are skipped.

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
5. `SET gxApi "D3D11"` (or `-d3d11`) runs the D3D11 device, which needs no `d3d9.dll` at all (see "D3D11 device").
