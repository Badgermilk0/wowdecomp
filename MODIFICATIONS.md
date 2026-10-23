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
| Raster fonts at high resolution: the 32 px glyph cap lifted, glyph pages grow with the font, 8-bit glyph alpha | `AwesomeFonts.cpp`, `Gxu/GxuFontTextureCache.cpp`, `GxuFontFont.cpp` (`UpdateDimensions`), `GxuFontMiscClasses.*`, `GxuFontString.cpp` | `fontMaxPixelSize` (**128**; 32 = stock; live) | — |
| Fallback-font chain: a glyph the font lacks comes from the first listed font that has it, on both the raster and the MSDF path | `AwesomeFonts.cpp`, `Gxu/GxuFontFallback.*`, `GxuFontFont.cpp` (`GetGlyphData`), `IGxuFontGlyph.cpp`, `Msdf/MSDFFont.cpp`, `Msdf/AwesomeMsdf.cpp` | `fontFallback` (game `Fonts\ARIALN.TTF`, `FRIZQT__.TTF`, then Windows' Segoe UI, Segoe UI Symbol, Microsoft YaHei, Malgun Gothic; empty = stock `?`) | — |
| Frame capture: one frame tree rendered offscreen to a transparent PNG in `Screenshots\` | `AwesomeFrameCapture.cpp`; filters in `Frame/CSimpleTop.*`, `CSimpleFrame.cpp`, `CRenderBatch.*`, `Ui/CGCooldown.cpp` | — | `CaptureFrame(frame[, size])`, `/fcapture [name] [2048\|2x]` (`/framecapture`) |

The HLSL shaders compile at load time with `d3dcompiler_47.dll`, which ships with Windows 10/11. They need
shader model 3 and a D3D9 or D3D11 device (D3D11 translates the same bytecode). Without SM3, or on OpenGL, the stock
`.bls` shaders load instead, and MSDF and frame capture stay off; frame capture also stays off on D3D11.

**Defaults that change the look:** `uiHalfPixelFix`, `uiTextureSampling`, `uiPixelSnap` and `MSDFMode` default to on,
as in awesome's current source. `uiUpscale` and `uiUpscaleEdges` (see "UI upscaling for high resolutions", beyond awesome) are on too. Set any of them to `0` for the stock look. The three UI fixes take effect live;
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

#### Font rendering at high resolution (beyond awesome)

- **The raster cap.** `CGxFont::UpdateDimensions` clamped every font to a 32 px raster, which the string then
  stretched to its size on screen: at 2160p a 40 px damage number came from a 32 px glyph and a crit (about 120 px)
  from the same. The cap is `fontMaxPixelSize` (`g_gxuFontMaxPixelSize`); a change re-rasterises every font, as a
  resolution change does. MSDF fonts lay out with these raster metrics (whole-pixel advances), so they space better too.
- **Glyph pages.** A font's glyphs sat on 256 x 256 ARGB4444 pages, sized for the 36 px cell of a 32 px outlined font.
  `TEXTURECACHE::PageSizeFor` now gives each font pages of at least seven cell rows (256 for every stock-sized font,
  up to 2048), in ARGB8888, so anti-aliased edges keep 256 alpha levels instead of 16 (the outline's own 10-step ramp
  is unchanged). The paste routines take the cell's own stride, and an upload re-pastes and sends only the rectangle
  of the cells placed since the last one; the stock client re-pasted the whole page into an uncleared buffer each time.
  3D strings' half-texel inset follows the page size.
- **Fallback chain.** `GxuFontFallback` holds the `fontFallback` list and opens each face the first time a lookup
  reaches it (game paths through `FontFaceGetHandle`, absolute paths read from disk, `%NAME%` expanded).
  `CGxFont::GetGlyphData` rasterises a missing glyph from the first face that maps it, on the font's own baseline; the
  layout still follows the font's own face (`IGxuFontGlyphRenderGlyph`'s `fontFace`). The MSDF atlas generates the same
  glyph into the font's own atlas from the fallback face's outline, if that face passed the validator
  (`MSDFFont::generateGlyphData`, main thread only, never the pregen workers); otherwise the MSDF pass draws `?`.
  Fallback glyphs are cached in the font's own `Cache_AwesomeWotLK\` file, so delete it after reordering the list.
- **MSDF on D3D11.** The atlas pages go through the engine's texture states 12-15 (`BindAtlasPagesD3d11`), which the
  device creates, uploads and binds like any texture, and are cleared after each batch; the D3D11 upload writes out a
  0-stride source (how the atlas clears a page) row by row. The four font shader permutations translate and compile
  (checked offline: `ps_3_0` with both `OUTLINE_PASS` values, `vs_3_0` `main` and `mainStereo`).

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
| `Packet_SMSG_GUILD_BANK_LIST` (0x5A7250) read each item's spell charges as a `uint32` where the packet has a `uint8`. **Fixed upstream** (`1ba4a32`), taken in by the merge of 2026-10-18 | `Ui/GuildBankFrame.cpp` | Every guild bank item after the first was read 3 bytes off: items landed in the wrong slots with wrong counts and most of a tab showed empty. |
| `Script_GetGuildBankTabInfo` (0x5A67F0) pushed the remaining item withdrawals unsigned; the original loads it signed (`fild dword [0xC1DC30]` at 0x5A69AE) | `Ui/GuildBankFrame_Scripts.cpp` | For a guild master (server sends `0xFFFFFFFF`, unlimited) the bank read "Remaining Daily Withdrawals: -2147483648 Stacks" instead of "Unlimited". Present in upstream too. |

### Diagnostics

| Option | Effect |
|---|---|
| `-DWOWCLIENT_CRASH_CONSOLE=ON` (off by default) | Upstream's crash logger (`Tools/WowClient/crash_log.cpp`, `WOWCLIENT_CRASH_LOG`) opened a console window at every start. It now only writes `WowClient_crash.log`; this option brings the live console back. |
| Thread names | `Storm/Source/Win64/SLock.cpp` (`SThread::Create`): the name each caller passes ("Disk Queue 2", "Model2", "NetThread 0", ...) is set with `SetThreadDescription` (looked up at run time, Windows 10 1607+), so debuggers and profilers show it. |
| `-DWOWDECOMP_SOUND_TRACE=ON` (off by default) | `Logs\SoundTrace.log`: every sound's request (`kit`, `glue music`), create, file open, reads near the start of a file and every failed read (position and first bytes), seeks, non-blocking load result, play (volume), stop, end and virtual kill, plus FMOD's channel count every 2 s. Also writes every byte FMOD reads for the first file over 1 MB to `Logs\SoundDump.bin`, to compare with the file in the MPQ. Build it in its own folder (`build/x64-trace`); it writes to the same `bin/Win`. |

### Performance (x64)

| Change | Where | Effect |
|---|---|---|
| CPU and GPU overlap with `gxFixLag` | `Gx/GxFrameLatency.*`, `ScenePresent` in `CGxDeviceD3d`/`CGxDeviceD3d9Ex`, CVar `gxMaxFrameLatency` (1; 0 = stock) | `gxFixLag` (default from `VideoHardware.dbc`, on for GPUs the 2010 client does not know) waited at every Present until the GPU had finished the frame just submitted, so CPU and GPU took turns and a frame cost both their times. It now waits for the frame `gxMaxFrameLatency` back: the CPU builds the next frame while the GPU draws the last, and the queue stays one frame deep so the cursor still does not trail. |
| Frame time breakdown | `Gx/GxFrameLatency.*`, `Awesome/AwesomePerf.cpp` | `GetFrameTimings()` / `/frametimes`: smoothed ms for the game's CPU work, the GPU wait, the frame cap and Present (vsync blocks there), to see what limits the frame rate. |
| Lua allocator: O(1) allocation and O(log n) free | `lua-5.1/src/lmemPool.*` | The stock pool scanned every 1024-block chunk on each allocation and each free, so Lua work and garbage collection slowed down as addons grew the Lua heap (the 3.3.5a "addon lag"). Chunks with a free block are now kept in a list, and chunks are sorted by address for the free. Offline benchmark of the stock and new pools under one workload (same results, checked against a shadow map): 1.6× faster at 1.5M live objects, 2.9× at 4M. |
| Multithreaded scene: model animation on every core | `Base/ParallelFor.*` (worker pool), `Model2/M2Scene.cpp` (`CM2Scene::Animate`), CVar `sceneThreads` (**0** = auto: logical processors − 1, at most 8; 1 = stock) | The stock client animates models (`CM2Model::AnimateMT`, the bone matrices) on two threads: every other model goes to its one helper, `Model2`. That split shows each root model and its attachments animate independently, so the roots are now spread over the whole pool, four at a time, and a crowd of player models no longer leaves one thread finishing the rest. Workers run with the main thread's Prop context and SSE control word; a job started from inside a job runs serially. `AnimateST`, `SetupLighting` and the draw-list build stay on the main thread (they link into scene lists and call game callbacks), as do culling and the unit updates. Branch `feature/mt-scene`; not yet measured in game. |
| Scene time breakdown | `Base/FrameProfile.*`, `Awesome/AwesomePerf.cpp`, section marks in `Ui/WorldFrame.cpp` and `WorldClient/CWorldSceneRender.cpp`, a draw counter in each D3D device's `Draw` | `GetSceneTimings()` and two more `/frametimes` lines, smoothed ms. World: unit updates (`UpdateDisplay`), the rest of the world update, culling, parallel animation, serial animation, element batching, models animated. Draw: terrain, WMOs, M2 models (`CM2Scene::Draw` and the opaque Gxu list), grass (`RenderDetailDoodads`), liquid and weather, effects (spell visuals, particles, transparent Gxu list), nameplates, the rest of the world render, and API draw calls per frame (UI included). Shows what stays serial and whether a scene is draw-call bound. A fourth line counts grass chunks and batches drawn, batches rebuilt on the CPU (and their ms), batches that found no baked buffer, and the ms spent choosing each grass chunk's lights. |
| GPU time per section (D3D11) | `Gx/CGxDeviceD3d11/D3d11GpuTimer.*`, `g_frameProfileGpuMark` in `Base/FrameProfile.*`, a `shadows` section around `CShadowQuery::Update` | `GetGpuTimings()` and a GPU line in `/frametimes`: a timestamp query at every section switch, bracketed by a disjoint query, read a few frames later without stalling. GPU ms for the frame, shadows, terrain, WMOs, models, grass, liquid, effects, other, and the rest (UI). Gaps include GPU idle time when the frame is CPU-bound. D3D9 devices report nothing. |
| Grass buffer pool grows on demand | `WorldClient/DetailDoodad.*` (`GrowPools`), CVar `groundEffectBuffers` (pairs per pool, 128 = stock) | Each terrain chunk's grass is baked once into one of 128 vertex/index buffer pairs, and the stock pool never grew: with more grass in view (the HD client raised the `groundEffectDist` cap), the remaining batches fell back to stream buffers and were rebuilt on the CPU every frame. Another pool is now added whenever the free list runs dry, up to 2048 pairs (about 150 KB of video memory each at full density). Measured at Crystal Lake (D3D11): 220 of 348 batches rebuilt per frame, grass 2.92 ms, 157 fps → none rebuilt, grass 0.06 ms, CPU 6.33 → 3.32 ms, **217 fps**. |
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

### Asset streaming (branch `feature/async-assets`, x64)

How the stock client streams the world: every M2, skin, `.anim`, ADT, WMO root, WMO group and BLP is read by the
async file loader (`Services/AsyncFile.cpp`). One "Disk Queue" thread pops the head of a priority-sorted list and
reads the whole file. Storm already decompresses a large file's MPQ sectors on several cores (`IOMpqUnit`), so the
read itself was never on the main thread. Everything after the read was: each file's post-load callback (BLP parse and
GPU upload, M2 relocation and setup, WMO and ADT chunk indexing, which in turn create textures) ran in the frame's poll
event for up to 100 ms per frame. Code that cannot wait blocks the main thread until its file is read and processed
(`AsyncFileReadWait`): unit, mount, game-object and missile models (`CM2Model::WaitForLoad`), liquid animation frames
and FFX textures (`TextureGetGxTex(..., 1)`), WMOs, and ADTs inside the view while `waitForLoad` is set. That wait sat
behind whatever the single reader was busy with, then behind up to 100 ms of other post-load work before it checked
again. Both are the familiar hitches: entering a city full of player models, crossing a tile border, the first look at
water.

| Change | Where | CVars (**default**) | Effect |
|---|---|---|---|
| Reader thread pool | `AsyncFile.cpp` (`AsyncFileReadSetDiskThreads`, `CAsyncThread::m_index`) | `asyncReadThreads` (**0** = auto: half the logical processors, 2 to 4; 1 = stock; up to 8) | The disk queue is served by several threads, each popping the head of the same sorted list under `g_queueLock`, so priorities and urgent reads still go first and a waited-on file starts as soon as any thread is free. Completion order no longer follows queue order; nothing depends on it, because a dependent file (a skin, a WMO group) is only queued by its parent's post-load callback. Concurrent reads were already routine (the main thread's synchronous reads and FMOD's loader run beside the disk thread). Threads are created on demand and never destroyed before shutdown; lowering the count idles the surplus ones. Takes effect live. |
| Prepare stage on the reader threads | `CAsyncObject::m_prepareCallback` / `m_prepared` (x64 fields after `m_link`), `AsyncFileReadThread`; `CM2Shared::PrepareLoad` | — | After a successful read the thread runs the object's prepare callback before posting it. For model files that is `M2Init`: checking every array against the file size and turning file offsets into pointers, a walk over every track of every bone, texture transform, particle and ribbon, which `CM2Shared::LoadSucceededCallback` did on the main thread. `s_sequenceIndex`/`s_sequenceData`/`s_sequenceSize` are `thread_local`, as the main thread may be relocating a low-priority sequence at the same time. A prepare callback may only touch the read buffer, which a `Cancel` keeps alive until its cleanup callback runs; `Cancel` clears a callback that has not started. |
| Post-load frame budget | `AsyncFileReadPollEvent` (the `EVENT_ID_POLL` handler), `AsyncFileReadProcessPostList` | `asyncPostloadBudget` (**8** ms; 0 = stock 100) | The frame's post-load pass stops after the callback that crosses the budget, so a burst of finished files spreads over frames instead of making one long frame. Loading screens and the map's own load loops still call `AsyncFileReadPollHandler`, which keeps the stock 100 ms. `asyncHandlerTimeout`, the stock knob, never took a `Config.wtf` value: it is read before the file is loaded. |
| Waits jump the post list | `AsyncFileReadProcessPostList`, `AsyncFileReadWait` | — | The object the main thread waits on is processed as soon as it is read, before older finished files, and the wait returns right after it; the others get the frame budget per pass instead of 100 ms before every check. |
| Texture load budget 4 → 64 MB, and files over the budget | `Services/Texture.cpp` (`TextureLoadFits`, `AsyncTextureHandler`, `CreateBlpAsync`) | `textureLoadBudget` (**64** MB; 4 = stock) | A BLP is read only while the texture bytes in flight (queued, being read, waiting for upload) fit the budget. 4 MB is three or four HD textures, so a city's textures trickled in. Worse, a file over 4 MB (a 2048 × 2048 DXT5 BLP is 5.3 MB) never fitted at all: it stayed queued until something drew it with `wait = 1` and loaded it synchronously. Now a file larger than the budget starts whenever nothing else is in flight. The stock code also relied on unsigned wrap-around: once a synchronous load pushed the bytes in flight past 4 MB, the "space left" became about 4 GB and everything queued started at once. |
| Unused models kept 10 → 120 s, capped by size | `Model2/M2Cache.cpp` (`GarbageCollect`, `g_m2CacheFreeBytes`), `CM2Shared::AddRef` / `Release` | `M2CacheTime` (**120** s; 10 = stock), `M2CacheSize` (**256** MB of model files on the free list; 0 = no cap, as stock) | A model nobody uses waits on the cache's free list and is reused as is. Stock deleted it after 10 s, so a spell effect cast every 15 s, a mount, or a ride back across a border read and parsed its models again. Above the size cap the oldest go first. Freed models keep their textures referenced, so this also keeps those textures. |
| Unused WMOs kept 10 → 120 s | `WorldClient/MapObj.cpp` (`CMapObj::s_flushTime`) | `wmoCacheTime` (**120** s; 10 = stock) | A WMO no longer placed in the world (its ADT purged) is freed after this long, so flying back over a town does not read its root and every group again. Group vertex buffers are still freed after 5 s unseen, as stock. |
| Load timings | `AsyncLoadStats` / `AsyncFileReadGetStats`, `Awesome/AwesomeStreaming.cpp`, two Load lines in `/frametimes` | — | `GetLoadTimings()`: main-thread ms per frame in post-load callbacks by kind (texture, model, skin, anim, ADT, WMO, WMO group, other; `ASYNC_KIND_*` set where each file is queued), the worst frame's post-load ms, ms per frame and the longest blocked in `AsyncFileReadWait`, waits and loads per 5 s, reader-thread prepare ms per second, MB read per second, files queued / being read / read but not processed, reader threads, then textures waiting for the budget and texture MB in flight. Peaks cover the current and the last 5 s window. |

Also fixed: `AsyncFileReadWait` returned early for an object that had already been processed without lowering its
re-entry guard, so the next wait would have ended in the fatal error "s_waiting != FALSE".

Not moved off the main thread, on purpose: the GPU upload of a texture (`GxTexUpdate` in `PumpBlpTextureAsync`), the
texture and shader-effect creation inside the M2/WMO/ADT callbacks, and `CM2Model::InitializeLoaded`. They go through
the device and the handle and hash tables, none of them thread-safe. BLP "decoding" is already free for DXT files: the
mip pointers are aimed into the read buffer. Only palettised BLPs are decoded on the CPU, and that is not moved yet;
the texture line of `/frametimes` shows whether it matters. Everything is x64 only; the Win32 layout build is unchanged.

The CVars are registered in `InitializeGlobal`, before the engine context creates the async queues, and the engine runs
a CVar's callback at registration only for a saved value. So `AwesomeStreamingRegisterCVars` applies each value once
itself, and `AsyncFileReadSetDiskThreads` remembers the count until `AsyncFileReadInitialize` creates the queue.

Tested offline (scratch install on the HD data copy, D3D11, login screen): `asyncReadThreads` 0 gives four named
"Disk Queue" threads on this 16-thread machine, 1 gives the stock one, 8 gives eight; the login scene's models and
textures load and render; the client exits cleanly (code 0, empty crash log). **Not yet run in the world**, where the
waits, the frame budget and the caches actually matter: compare `/frametimes` Load lines with the CVars at their stock
values (`asyncReadThreads 1`, `asyncPostloadBudget 0`, `textureLoadBudget 4`, `M2CacheTime 10`, `M2CacheSize 0`,
`wmoCacheTime 10`).

### Graphics (x64)

| Change | Where | CVars (**default**) | Effect |
|---|---|---|---|
| Shadow map size | `ShaderEffect/ShadowCache.*` (`CShadowCache::Init`), `Awesome/AwesomeShadows.cpp` | `shadowMapSize` (**4096**; 0 = stock, else rounded up to a power of two from 1024 to 8192) | Every shadow map is this size: the near 40-yard one and the three cascades (80, 320, 1280 yards across). Stock is 1024, or 2048 from `extShadowQuality` 2 and 4 up, so 4096 halves the size of a shadow texel. With PCF filtering only, the default on SM3 cards. The R32F path draws with the screen's depth buffer, which a larger map would overrun. Takes effect live. Memory is about nine maps of size² × 4 bytes: ~600 MB at 4096, ~2.4 GB at 8192. |
| Shadow softness | `CShadowCache::InitPcfOffsets` | `shadowSoftness` (**1**, 0.25–4) | Scales the eight PCF tap offsets. At 1 the kernel stays about two texels, so a bigger map also gives sharper edges. Raise it to soften them again. |
| Cascade reuse | `CShadowCache::GatherBands` (`CascadeDue`), `CShadowQuery::FrustumCallback` / `QueryMapObjDefs`, `Awesome/AwesomeShadows.cpp` | `shadowCascadeReuse` (**1**; 0 = stock) | With `extShadowQuality` 5 the stock client re-renders all three cascades every frame, each with every caster in view out to 1280 yards. Now the 320-yard cascade is re-rendered every 2nd frame and the 1280-yard one every 4th (on different frames), and at once when the camera turns more than about 2°, moves more than 2% of the cascade's radius, or the sun moves. The near map and the 80-yard cascade, where units move, stay per frame. A cascade that may be kept renders 5% wider than the view on each side and keeps the inner cascade's casters (stock cuts them out), so a kept map never leaves a gap; skipped cascades gather no casters. Distant shadows of moving things update at a quarter or half rate. Takes effect live. |
| Stable shadows | `CShadowCache::GatherBands` / `Update`, `SnapToShadowTexels` | `shadowStable` (**1**) | The cascades (`extShadowQuality` 5) and the near map are centred on whole texels of the sun's view plane, not on whole-yard world cells (2/4/16 yards) or 1/size-yard steps. The texel grid then stays put in the world, and shadow edges no longer crawl and flicker while you move. They still creep slowly as the sun moves. |
| Live 3D unit portraits | `UIUtil/PortraitButton.*` (`LivePortraitSet`, `UpdateLivePortraits`), called each frame from `Ui/WorldFrame.cpp` (`OnWorldUpdate`); `Frame/CSimpleTexture` `s_destroyCallback`; `Awesome/AwesomeMisc.cpp` | `portraitLive` (**1**; 0 = the stock snapshot), `/liveportraits [on\|off]` | `SetPortraitTexture` on a unit no longer takes one picture: the portrait keeps its own copy of the unit's model (as `PlayerModel:SetUnit` does) playing the idle animation, and redraws it into the portrait's render target every frame through the stock portrait camera, lighting and circular mask, at `portraitResolution`. So every frame that uses `SetPortraitTexture` animates (player, target, focus, both targets-of-target, pet, party, boss, arena, and addon unit frames) with no change to FrameXML. One copy per unit GUID, shared by every texture showing it; it is drawn only while one of those textures is visible, follows the unit's model on shapeshift, display or gear change, keeps animating after the unit leaves view, and is freed once no texture shows it. Up to 32 at once; beyond that, and for units with an icon portrait, party pets (no unit object) and the 64×64 readback path (no render-target alpha), the stock snapshot stays. Takes effect live. Cost per visible portrait is about that of one `PlayerModel` frame. Don't also run the `HDPortraits` addon: it lays `PlayerModel` frames over the same textures. |
| Indoor minimap resolution | `Ui/MinimapFrame.*` (`CreateIndoorTexture`, `MinimapSetIndoorResolution`), `Awesome/AwesomeMisc.cpp` | `minimapIndoorResolution` (**1024**, 256–2048, rounded up to a power of two) | Inside a WMO (caves, dungeons, buildings) the minimap is drawn into a square render target covering three times the visible radius, and only its middle third is shown. Stock 256 left about 85 texels across the minimap, 3.5× coarser than the WMO minimap textures (0.5 yd per texel) at the widest zoom. 1024 keeps their full detail at every zoom; 2048 adds nothing visible. Takes effect live. The outdoor minimap has no render target: it draws the `Textures\Minimap` tiles directly, so only higher-resolution tile files sharpen it (tile texture coordinates are fractions, so any power-of-two size displays correctly). |

### UI upscaling for high resolutions (branch `feature/ui-upscale`)

FrameXML lays the interface out on a screen 768 units high, so at 2160p every texture of the UI is drawn about 2.8
times its size (1440p: 1.9). The stock client magnifies it with one bilinear tap, or point-samples it (a texture under
256 px with no mips gets `GxTex_Nearest` in `CTexture` creation), and the whole UI looks soft. awesome's
`uiTextureSampling` only treats the opposite case (a texture drawn smaller than it is). Most of the art is the stock
size (64 px icons, 128 × 32 buttons, 128 × 16 borders); the HD client replaces a few files at 2 to 4 times.

This sharpens the existing art without new files and without addons doing anything: the reconstruction runs in the UI
pixel shader that awesome already replaces (`Awesome/AwesomeUiFixes.cpp`), for every `CSimpleTexture`, backdrop, status
bar and cooldown in FrameXML, GlueXML and addons. Fonts are already handled by MSDF and `fontMaxPixelSize`.

| Part | CVars (**default**) | How |
|---|---|---|
| Magnified textures | `uiUpscale` (**2**: 0 engine, 1 bicubic, 2 edge-adaptive; live) | When a texture is drawn larger than it is (under 0.98 texels per pixel), the pixel is rebuilt from its 4 × 4 texel neighbourhood: Catmull-Rom, or (2) an edge-directed Lanczos after AMD FSR 1's EASU, which finds the art's local edge direction and stretches the kernel along it, so diagonals stay clean instead of stepping. The result is held to the range of the four nearest texels: no ringing, no colours the art does not have, and nothing from beyond what bilinear already reaches (sub-images packed into one file do not bleed further than before). Taps are at texel centres, so a point-sampled texture is reconstructed too. At 1:1 the output is the stock tap, bit for bit. |
| Premultiplied filtering | — | For the blends that weight colour by alpha (`AlphaKey`, `Alpha`, `Add`), colour is filtered premultiplied, so the colour of transparent texels (often black or garbage) no longer seeps into edges as a dark fringe. Opaque and modulate blends filter the colour as it is. |
| Alpha edges | `uiUpscaleEdges` (**1**, 0–1; live) | Where a magnified texture's alpha steps from transparent to opaque within a texel (button rims, rings, check marks, icon masks), a smooth cubic fit of the 4 × 4 alpha (Mitchell-Netravali B 0.6, C 0.2) is read as a distance field and its 50% contour is drawn one screen pixel wide, measured along the edge's own direction (correct for stretched and rotated quads). The cubic is smooth enough to straighten the stairs of 1-bit (DXT1) alpha into the diagonal they stand for, and sharp enough that a lone opaque texel and a 1-texel diagonal line survive (the B-spline erases both). Soft alpha (glows, shadows: under 0.5 change between neighbouring texels) is left alone. |
| Atlas cells | — | Icons and other square textures up to 64 px share 512 px atlases (`uiFaster` bit 1, on by default). `TextureGxTexIsAtlas` tells the batch callback, and the taps are clamped to the drawn cell, so neighbouring icons never bleed in. The stock bilinear tap bled up to 82/255 along a cell's border; with the clamp an atlased icon renders exactly as the same icon on its own. |

Plumbing: `CSimpleRender::s_batchTextureCallback` now also receives the batch's blend mode; pixel constant c223 holds
the atlas cell count, `uiUpscaleEdges` and the premultiply flag (c222 stays the texture size). The shader permutations
are `FOOTPRINT` (uiTextureSampling on), `TENT` and `UPSCALE`; the stock `.bls` loads only when both `uiTextureSampling`
and `uiUpscale` are 0. The edge-adaptive permutation is about 625 ps_3_0 instruction slots (fine on any GPU with
D3D10-class hardware or later); D3D11 translates it like the other awesome shaders.

Tested offline: every permutation of both entry points through `D3DCompile` (ps_3_0), the D3D11 translator and SM5;
real UI textures from the HD data rendered through that chain at 2.81× (comparisons of stock, bicubic, edge-adaptive and
edges); synthetic alpha (lone texels, 1-texel lines, 1-bit and anti-aliased circles, a soft glow); the atlas clamp; the
three blend paths; and the login screen at 2560 × 1440 on D3D11 (modes 0, 1, 2) and on D3D9, with no shader failures.
**Not yet seen in the world UI** (action bars, unit frames, bags), which uses the same path.

Not done: a separate retained-mode UI layer (the engine's UI is already retained: `CSimpleTop` keeps the frame tree and
`CRenderBatch` rebuilds a batch only when a region changes), and 9-slice (`SetTextureSliceMargins`) or SVG textures,
which would sharpen only art written for them. Without a texture change, the outdoor minimap tiles (drawn by the
minimap's own pass) and the hardware cursor (fixed 32 px) are not covered.

### Display, windows and UI scale (branch `feature/display-modern`)

The stock client predates high-DPI Windows and multi-monitor desks: it declares no DPI awareness, puts full screen
and maximized windows on the primary monitor, takes the UI's aspect from `gxResolution` rather than the window, reloads
the whole UI when the aspect changes, and keeps `uiScale` at 0.64 or more. Window-mode changes are D3D11 only; the
rest applies to every device.

| Part | Where | CVars (**default**) / Lua | What it does |
|---|---|---|---|
| Per-monitor DPI | `Win64/WinMain.cpp` (`start`) | — | The process declares per-monitor DPI awareness (v2) before any window exists. Unaware, Windows reported a scaled-down desktop at display scaling above 100% and stretched a smaller frame over it (a 4K monitor at 150% got 2560 × 1440). A compatibility setting on the exe (Properties > Compatibility > high DPI) overrides this. |
| DPI changes | `CGxDeviceD3d11.cpp` (`WM_DPICHANGED`, `SetClientAdjust`) | — | Moved to a monitor with another scale, a window takes the size Windows suggests (same size on screen); the frame around it is sized for the monitor it is on (`AdjustWindowRectExForDpi`), so the client size is exact on mixed-DPI desks. |
| Monitor choice | `Gx/CGxDevice/Win64/CGxDevice.cpp` (`PickMonitor`, `MonitorRects`), `CGxDeviceD3d11::ICreateWindow` | `gxMonitor` (**0** = the one the window was last on, 1 = primary, 2.. = left to right); `gxMonitorName` (remembered) | Full screen and maximized (borderless) windows cover their monitor at its native size; stock always used the primary. A changed `gxMonitor` moves a full-screen window at once. `AdjustWindowRectToWorkArea` centres a new window on that monitor's work area (D3D9 too). |
| Remembered window | `ConsoleDeviceRememberWindow` (`ConsoleDevice_CVar.cpp`), `AwesomeDisplay.cpp` | `gxWindowPos` ("x y width height", remembered) | A window opens where it was last, kept inside the work area (stock centred it on the primary each time), and a resized window keeps its size (`gxResolution` is updated). A pending `gxResolution` set from the console is not overwritten: only a move or resize after the window was made is remembered. |
| Alt+Enter | `Os/Win64/Input.cpp` (`OsInputConsumeFullscreenToggle`), `ConsoleDeviceToggleFullscreen` | — | Switches between a window and the full-screen mode last used (`gxWindow 0`, or the borderless `gxMaximize 1` by default), without the system beep; the window comes back at its remembered place and size. Runs from `CGGameUI::Idle` / `CGlueMgr::Idle`, outside rendering. |
| Display changes | `CGxDeviceD3d11::IFitToMonitor` | — | A full-screen or maximized window covers its monitor again after the monitor's resolution changes or it is unplugged (stock took the primary's size from `WM_DISPLAYCHANGE`). |
| Real aspect | `Ui/Screen.cpp` | — | The UI and the world take their aspect from the window's client size, not `gxResolution`, which names only the requested size: a D3D11 full-screen or borderless window takes its monitor's size, and a snapped or resized window keeps its own, so the picture was stretched whenever the two differed. `widescreen 0` still forces 4:3. |
| Video options resolution | `VideoOptions_Scripts.cpp` (`SyncCurrentResolution`) | — | The Resolution dropdown shows the size the game has: the monitor's for full screen and maximized windows, and a window's own size (listed as an extra entry) when it is no display mode. Stock showed the first entry (800x600) for any size not in the list. |
| Free resizing | `ConsoleDevice_CVar.cpp` | `gxAspect` (**0**, stock 1) | A window no longer keeps the aspect it was created with while resized. |
| Live aspect changes | `Base/Coordinate.cpp`, `CSimpleTop::RelayoutForAspect`, `GxuFontAspectChanged`, `AwesomeDisplayApplyAspect` | `uiLiveResize` (**1**, read at startup) | A new window aspect lays the game UI out again instead of reloading it, and fires `DISPLAY_SIZE_CHANGED`. Stock DDC coordinates span the screen's diagonal, so a UI unit's DDC size depends on the aspect and every stored frame size goes stale (hence the reload); with this on, DDC height is fixed at 0.6, a UI unit is always 0.6 / 768 DDC, and only the screen's width follows the aspect (at 4:3 both are the stock values). The new aspect is applied in `CSimpleTop::OnDisplaySizeChanged` after the fonts take the new pixel size and before frames measure text, the screen-anchored frames are laid out again, and every font string's horizontal NDC values are rescaled (they are converted once, when the string is made). Two culls assumed DDC within [0, 1] (`IsRectOffscreen`, `CRenderBatch::QueueFontString`); they now test the screen's DDC size. **The glue screens still reload**, as stock: the HD GlueXML places itself for the aspect once, when it loads, and has no event to redo it. |
| UI scale floor | `CGGameUI::SetUIScale`, `HandleDisplaySizeChanged` | — | `uiScale` goes down to 0.1 (stock 0.64), so the UI can be pixel-exact at 4K (768 / 2160 = 0.36). The Interface options slider still stops at 0.64; set lower values with `/console uiScale`. |
| DPI-based UI scale | `AwesomeDisplayUiScale`, `AwesomeDisplayDefaultUiScale` | `uiScaleDpi` (**0**) | The UI scale follows the monitor's DPI: `uiScale` (or 1 with `useUiScale` off) then means "as at 1080p and 100% Windows scaling", so the UI keeps its size on screen across resolutions and monitors (4K at 150%: 0.75 × `uiScale`). It is reapplied when the window moves to a monitor with another scale. |
| Ultrawide UI area | `AwesomeDisplayOnUiScaled` | `uiMaxAspect` (**0** = off, e.g. 1.78) | On a screen wider than this, `UIParent` keeps to a centred area of this aspect; the world still fills the screen. |
| Ultrawide camera | `UIUtil/Camera.cpp` (`GetCameraDistance`), `Services/Camera.cpp` | — | The world was already Hor+ (fixed vertical field of view), but the camera-collision probe used a diagonal one, narrower than the view past 4:3, so walls at the screen's sides could cut into the near plane; it now matches the view. Model scenes (login screen, character models) keep their vertical view past 16:9 instead of zooming in. |
| Lua | `AwesomeDisplay.cpp` | `GetMonitorCount()`, `GetMonitorName(i)` ("1: 3840x2160 (primary)", width, height, isPrimary, device), `GetCurrentMonitor()`, `GetDisplayScale()` (DPI scale, UI scale) | For an options panel. |

Tested offline on a scratch install at the login screen (two monitors: 3840 × 2160 at 150% primary, 2560 × 1440 at
100% second): per-monitor awareness and an exact client size without the compatibility setting; resizing to 28:9 and
back with the glue re-laid out live (a test build only; text, culling and centred labels checked) and with the stock
reload (the default for glue); D3D11 window → second monitor (DPI change) → Alt+Enter → borderless on that monitor →
Alt+Enter back to the same place and size; `gxWindow 0` and `gxMaximize 1` on the remembered monitor and on
`gxMonitor 1`; the window's place and size kept across a restart; D3D9 still starts. **Not tested in the world**: the
in-game live re-layout (FrameXML, addons, chat), `uiScaleDpi`, `uiMaxAspect` and the camera probe need a realm login.

### Distance clarity (branch `feature/distance-clarity`, D3D11)

| Part | Where | CVars (**default**) | What it does |
|---|---|---|---|
| Texture LOD bias | `World::SetTexLodBias` (`WorldClient/World.cpp`), `CGxDevice::s_texLodBias`, `CGxDeviceD3d11::ISamplerState` | `texLodBias` (stock, **0**, -1 to 1; live) | The stock client registered the CVar and checked its range, but `World::SetTexLodBias` was empty in 12340, so it did nothing on any device. It is now the `MipLODBias` of every D3D11 sampler that uses mips (not shadow-map comparisons or level-0-only filters); a change makes the cached samplers again. Negative values pick sharper mip levels in the distance and at grazing angles, at some cost in shimmer on fine patterns. The D3D9 devices still ignore it. |
| Sharpening | `TonemapPs` / `GxSharpen` (`D3d11Lighting.cpp`), `GxLightingParams::m_sharpen`, `AwesomeLighting.cpp` | `sharpen` (**0**, 0 to 1; live; `/lighting on` sets 0.5) | AMD FidelityFX contrast-adaptive sharpening (CAS) of the world as it is tonemapped, before the UI is drawn, so the UI is never sharpened. A 4-tap cross with a negative weight that shrinks where the 3x3 neighbourhood already has contrast, so detail softened by mips and distance comes back without halos on edges. The weight is judged on the colour clamped to 1, so bright HDR pixels are left alone. Needs `hdr 1` (it lives in the HDR tonemap pass). |

Tested offline: the tonemap shader compiles (no errors in `gx.log`) and, on the login scene's model with
`lightingGlueTest 3`, `sharpen 1` roughly doubles edge energy against the pass off, with no visible halos.
**Not seen in the world**; the texLodBias change could not be isolated on the login screen.

### Audio engine upgrade (branch `feature/audio-engine`)

The client already mixes with FMOD Ex 4.9.7 in software. 3D positioning, the cone settings and the `Sound_EnableReverb`
zone reverb are stock. What it lacked: occlusion, reverb that changes smoothly or exists at all in most interiors, and
enough voices. The engine half of the change is in `SoundEngine/SoundEngine.*` (`SE2::s_audioSettings`,
`SE2::UpdateOcclusion`, the reverb crossfade). The game half, with the CVars and the world ray tests, is in
`Awesome/AwesomeAudio.cpp`, called from `SI2::SetListenerAttributes` every world frame and from
`SI2::ApplyReverbSettings`. None of it touches the render device, so the 32-bit DX9 build gets the same.

| Change | Where | CVars (**default**) | Effect |
|---|---|---|---|
| Occlusion | `SE2::UpdateOcclusion`, `AwesomeAudio.cpp` (`OcclusionTest`); `FMOD_INIT_SOFTWARE_OCCLUSION` in `SE2::Init` | `Sound_EnableOcclusion` (**1**), `Sound_OcclusionStrength` (**0.7**), `Sound_OcclusionTests` (**8** per frame), `Sound_OcclusionFade` (**0.25** s) | Each playing 3D sound between 3 and 100 yards from the listener is ray-tested from the character's head against terrain, WMOs and collidable game objects (`World::Intersect` flags `0x100110`; trees and other doodads are left out). The ray stops a yard short of the source, so a torch on a wall still hears its own wall. A source blocked low but open 2 yards higher (a fence, a hill crest) gets half the strength. FMOD turns the result into a volume cut plus a low-pass filter, and cuts the reverb send by 35% of that, so a fight behind a building is dull and quieter but still fills the room. New sources are tested first, then the oldest result, each again after 200 ms, and they ease between values over the fade time. A sound that starts behind a wall is muffled from its first test. The character's own sounds are never occluded, nor are the music and cinematic groups. A source the client blends toward 2D (an advanced emitter such as a waterfall, inside its inner 2D radius: pan level 0) is occluded only in proportion to its pan level, so a sound bed you stand in is not muffled by a wall that happens to sit between you and its centre (FMOD itself ignores the pan level for occlusion). |
| Reverb crossfade | `SE2::SetReverb`, `BlendReverb`, `ProcessReverbFade` (heartbeat) | `Sound_ReverbFade` (**1.5** s; 0 = stock switch) | A zone, WMO or underwater reverb change blends over the fade time: levels and decay times are interpolated (smoothstep, 20 steps a second), and the discrete fields come from the nearer end. Fading to or from no reverb fades the existing room's level, then switches the reverb off. Stock jumped at once, with an audible cut in the tail. |
| Fallback reverb | `SI2::ApplyReverbSettings` → `AwesomeAudioFallbackReverb` | `Sound_ReverbFallback` (**1**) | Only where `SoundProviderPreferences` gives the place nothing (most WMO interiors; water without its own setting). Underwater gets FMOD's underwater preset. Indoors, eight horizontal rays and one upward ray (walls and roofs only, 40 yards) are cast every 250 ms. A mean wall distance under 7 yd gives `ROOM`, under 16 yd `STONEROOM`, otherwise or without a roof within 20 yd `AUDITORIUM`, and a new size must show twice in a row before it is used. A DBC reverb always wins. Needs `Sound_EnableReverb 1` like the stock reverb. |
| More voices | `SE2::Init` clamp, `SoundInterface2_CVar.cpp`, `SI2::Init` | `Sound_NumChannels` (**64**, stock 32; up to **256**, stock 128) | Real (mixed) voices. 1024 channels in all (stock 512) for the virtual ones. Measured with this FMOD build offline: 256 voices, half of them filtered, mix in 2.3% of one core. |
| Inaudible sounds give up their voice | `FMOD_INIT_VOL0_BECOMES_VIRTUAL`, `vol0virtualvol` 0.001 (−60 dB) in `SE2::Init` | — | A sound past its max distance (the client's rolloff is silent there) or occluded below −60 dB stops using a voice, so a town raid's near sounds are not cut off by far ones. |
| Virtual loops resume | `KillVirtualSounds` | `Sound_KeepVirtualLoops` (**1**; 0 = stock) | Stock stopped every non-streaming sound that was virtual 400 ms after it started, loops included, so a fire or a fountain that lost its voice stayed silent until it was re-created. Loops now stay virtual and resume when they are audible again. Their position keeps running, so they come back in time. One-shots are still stopped. |
| Stats | `SE2::GetAudioStats`, `AwesomeAudio.cpp` | — | `GetSoundStats()` and `/soundstats`: voices playing / available / virtual, FMOD CPU, 3D sources tracked and occluded, rays this frame, the current reverb (zone / room size / underwater / none), its room level and decay, and how far a fade has got. |

Tested: offline against the client's own `fmodex.lib` (scratch harness, `FMOD_OUTPUTTYPE_NOSOUND_NRT` with a capture
DSP at the end of the chain). A blocked source plays at 24% of its open level, low-passed, and occlusion 0 restores it
exactly; without the init flag there is no filter. 24 loops past their max distance go virtual at once and 8 near
sounds get all 8 voices; a virtual loop that comes near is real again. 90 interpolated reverb steps give no NaN and
no level spike, and the hall preset leaves a tail. In the client: the login screen inits with the new flags
(`Logs\SESound.log`: "Occlusion filter and virtual voices ENABLED", 512 requested channels clamped to 256; sound muted for the run) and
shuts down cleanly. **Not yet heard in the world:** the occlusion rays, the room estimate and `/soundstats`
run only there.

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
| Shader cache | `Gx/CGxDeviceD3d11/D3d11Compiler.*` | `D3DCompile`, cached under `Cache\D3D11Shaders\` by a hash of the source. As the engine loads a shader file (well before it draws with it), every permutation is translated and queued to worker threads (half the cores, at most 8, below normal priority); a draw that needs a shader still queued compiles it itself, one being compiled waits for it. From an empty cache the login screen alone fills it with about 1,050 shaders; later sessions read them back. Deleting the folder is safe. |
| The frame | `CGxDeviceD3d11.cpp` | An offscreen colour target (multisampled when `gxMultisample` asks), copied to a flip-model swap chain at present, with tearing when `gxVSync` is off. Full screen is a borderless window over its monitor at that monitor's resolution (see "Display, windows and UI scale"), not an exclusive mode; the gamma ramp is applied in the copy there, as D3D9 set it on the display only in full screen. Resizing the window resizes the targets live. |
| Constants | `CGxDeviceD3d11Draw.cpp` | The constant registers are kept as D3D9's hardware would hold them and uploaded as slices of one ring buffer (D3D11.1 offsets; plain buffers without them). |
| awesome_wotlk | `Awesome/AwesomeShaders.cpp` | The UI sampling and pixel-snap shaders compile to the same bytecode and are translated, so `uiTextureSampling` and `uiPixelSnap` work on D3D11. MSDF fonts bind their atlas pages through texture states 12-15 here (see "Font rendering at high resolution"). Frame capture calls `IDirect3DDevice9` itself and stays off. |

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

### Modern lighting (branch `feature/modern-lighting`, D3D11 only)

Lighting and post effects that need no new art: the stock textures and models, lit in a modern way. Every effect has
its own CVar and **defaults to off**; with all of them off the frame is the stock one. On the D3D9 devices the CVars do
nothing. `/lighting on` sets a starting preset, `/lighting off` turns every effect off, `/lighting` alone prints the
lights found and the CVars (`GetLightingStats()`).

| Effect | CVars (**default**) | How |
|---|---|---|
| HDR and tonemap | `hdr` (**0**), `hdrExposure` (**1**), `hdrTonemap` (**1**: 0 clamp as stock, 1 neutral, 2 ACES) | The world draws into a 16-bit float target (multisampled like the frame) instead of the frame. Its colours are what the 8-bit frame would hold, only not clamped at 1 where light builds up: a draw is still clamped per layer as before, except the lit families below (up to 16), so additive glows stack past 1. At the end of the world pass it is decoded to linear light, exposed, tonemapped and written into the frame before the UI and before FFX glow. `hdrTonemap 1` leaves every colour below a shoulder (0.8 linear) as it was and rolls the brighter ones off toward white, so nothing clips; with `hdrTonemap 0` the result is the stock frame (checked: pixel-identical on still parts of the login scene). |
| Bloom | `bloom` (**0** = off, else the intensity; needs `hdr`), `bloomThreshold` (**1**, linear) | The scene's light above the threshold (soft knee, each pixel clamped against fireflies), down a 6-level chain (13-tap downsample) and back up (tent filter), added before the tonemap. |
| SSAO | `ssao` (**0** = off, else the intensity), `ssaoRadius` (**1.5** yd), `ssaoDistance` (**200** yd, fades out over its last 40%) | After the opaque models and the grass, before liquids and everything transparent: 12 samples on a spiral at half resolution from the depth buffer (view positions through the inverse projection, normals from the nearer neighbour on each axis), biased by the depth buffer's own step at that distance, a depth-aware blur both ways, and a depth-aware upsample multiplied into the scene. `lightingDebug 1` shows the term alone. |
| Per-pixel lighting | `lightPerPixel` (**0**) | The M2 (`Diffuse_*`, `CDiffuse_*`), WMO (`MapObj*`, `MapObjU*`) and terrain (`Terrain`) vertex shaders compute the sun, ambient and stock point lights per vertex. The translator now also has them hand their view-space position, normal and material to the pixel shader, and the pixel shader evaluates the same formula per pixel. Each vertex checks that the formula reproduces its own colour (within 1/255); a permutation it does not reproduce (an unlit one) is left as it was. Checked on the GPU with stream output, random vertices and plausible constants: of the HD client's 773 distinct M2 / WMO / terrain vertex shader records, all 638 lit ones reproduce their colour exactly, and every rebuilt position matches (the other 135: unlit M2s, left alone, and unlit WMOs, which take dynamic lights only). |
| Dynamic lights | `lightDynamic` (**0**), `lightDynamicIntensity` (**1**), `lightDynamicM2` (**0.6**), `lightDynamicWmo` (**0.5**), `lightDynamicDistance` (**100** yd), `lightFlicker` (**0.3**), `lightWrap` (**0.25**) | The point lights of M2 models (braziers, torches, lanterns: `CM2Scene`'s light grid, with the colour and the attenuation start / end the model animates; the stock client never animated the attenuation tracks and lit with a fixed falloff, four lights a model) and of WMOs (the MOLT lights, which the stock client only baked into vertex colours: transformed by each placed WMO's matrix), the nearest 256 within reach. A compute shader sorts them into 16 × 9 × 24 view clusters; the lit pixel shaders add the lights of their cluster in linear light (full up to the start, a smooth fall to the end, a wrap for soft terminators). Warm lights flicker. Surfaces without a normal (the unlit `MapObjU*` interiors) use their face normal. |
| Fires as lights | `lightFire` (**0** = off, else the intensity; needs `lightDynamic`) | Most fires in the world (bonfires, braziers, torches) are particle effects with no light record, so the dynamic lights found nothing near them. `CM2Model::AnimateST` now hands each animated model with particle emitters to `AwesomeLighting` (`s_particlesAnimatedCallback`); an emitter counts as a flame when its particles glow (additive, or a bright alpha blend), its mid-life colour is red over green over blue, and it is emitting. Held and spell attachments are left out. The emitters of one fire (within 2.5 yd) merge into one warm, flickering light at their centre, half a yard up, sized by the particles and their number (5 to 18 yd). No asset is changed. |
| Specular | `lightSpecular` (**0**), `lightSpecularM2` (**0.3**), `lightSpecularWmo` (**1**) | Only where the data asks for it: the WMO specular shaders (`MapObjSpecular_*`, which the stock `specular 1` turns on after a restart) get their specular per pixel plus the dynamic lights'; sphere-mapped M2 batches (`*_Env*`, the shiny ones) get the sun's highlight and the lights'. Everything else stays matte. |
| Sky ambient | `lightSkyAmbient` (**0** = off, else the strength) | The ambient term of lit surfaces tinted by the zone's sky colour (Light.dbc's sky top and middle) from above and its ground (fog lit by the sun) from below, both scaled to the stock ambient's luminance. |
| Height fog, scattering | `fogHeight` (**0** = off, else the density per yard), `fogHeightFalloff` (**0.05**), `fogHeightOffset` (**-5** yd from the player's feet), `fogHeightMax` (**0.85**); `fogScattering` (**0** = off, else the strength) | After the opaque pass: exponential fog integrated along each view ray, thickest below the player, in the zone's fog colour; and the stock distance fog (recomputed from the same start, end and density) tinted toward the sun's colour looking into the sun. |

How it fits in (all of it is D3D11 work; `Engine/Source/Gx/GxLighting.h` is the interface the game drives):

| Part | Where |
|---|---|
| CVars, the lights, the zone's colours (`DayNight::DNInfo`), `/lighting` | `Awesome/AwesomeLighting.cpp` |
| The world pass bracket: `BeginWorld` before `CWorld::Render`, `SetupView` in `CWorldScene::Render` once the camera, the models' lights and the shadow maps are ready, `OpaqueDone` after the grass, `EndWorld` before `FFX::Finish` | `Ui/WorldFrame.cpp`, `WorldClient/CWorldSceneRender.cpp` |
| The attenuation tracks animated with the light's colour | `Model2/M2Model.cpp` (`AnimateMT`) |
| The surface outputs and per-pixel lighting of the translated shaders (`s_hlslLighting`, the families by shader name, the self-check) | `Gx/CGxDeviceD3d11/D3d11Dx9Shader.*` |
| The HDR scene, the passes, the light clusters | `Gx/CGxDeviceD3d11/D3d11Lighting.*` |
| The frame target and copies of it follow the HDR scene while the world draws (`IFrameRtv`, `IFrameSrv`); the depth buffer is typeless so the passes read it; the lighting constants (b3), the vertex constants again for a lit pixel shader (b4), the lights (t16) and clusters (t17) | `Gx/CGxDeviceD3d11/CGxDeviceD3d11*.cpp` |

The translated shaders change text, so the first session after the update compiles them again (the HD client's whole
corpus takes about three times as long as before; `Cache\D3D11Shaders` keeps them).

**Tested offline only.** The world cannot be reached without a realm, so the login screen's model scene has a test
mode, `lightingGlueTest` (1; 2 adds three coloured test lights in front of the camera; 3 switches between on and off
every 4 seconds, to compare the two in one session, the scene being random), which runs every pass around it. Seen
there, windowed and with 4× multisampling: the HDR path, the tonemap, bloom, SSAO, the fog passes, per-pixel and
dynamic lighting on skinned and lit M2 models with the scenes' own model lights (radii from their attenuation tracks)
and the test lights; the D3D11 debug layer reports nothing new. **Not seen in the world:** the terrain and WMO families
(checked only by the GPU test above), the MOLT lights, liquids and transparent passes over the HDR scene, the sky, real
fog values, and how any of it looks. The strengths are starting points to tune in game.

## Upstream syncs

- **2026-10-23**, SatyPardus `main` at `20f9120e` (187 commits, PR #1 on Badgermilk0/wowdecomp). Upstream replaced
  many magic numbers with recovered enums; this tree's own code follows the new names: `TYPEMASK_*` → `TYPE_*`
  (`TypeMask` → `OBJECT_TYPE`), `GxTexGen_EyeLinear`/`Unk3` → `GxTexGen_World`/`View`, `Unk84` → `GxRs_BlendFactor`,
  `GxTex_3` → `GxTex_Recreate`. Upstream's own fixes (portrait cache entries start dirty, the font cache row scan,
  override sky sync, talent preview ranks, `StopAllSounds` deadlock, item swap initialisation) sit beside the
  changes here without overlap; the font row fix lands in this tree's page-size-aware `CreateNewDesc`. Upstream now
  names the client "Waystone" (`GameVersion.h`, generated `WaystoneVersion.h`): the D3D9 windows and the glue
  `GetBuildInfo` use it, while the D3D11 window keeps its "World of Warcraft" title.

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
