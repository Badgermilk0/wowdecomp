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
| Loot sparkle tuning (not in awesome): emission rate, opacity and particle size of the loot sparkle on lootable corpses and highlighted objects, live; settings window World > Effects | `AwesomeMisc.cpp`; `CM2Model::m_lootSparkle` + `s_lootSparkle*` (set where `Corpse_C`, `GameObject_C` and `CEffect::AddLootEffect` create the effect, applied in `AnimateParticleST`); `CParticleEmitter2::m_sizeScale`, colour writes clamp `m_alphaScale` above 1 | `lootSparkleAmount` (1), `lootSparkleBrightness` (1), `lootSparkleSize` (1) | — |
| Per-session chat/combat log names | `AwesomeMisc.cpp` | `chatLogSessionKey` (1), `combatLogSessionKey` (1) | — |
| Window / clipboard | `AwesomeMisc.cpp`, `Os/Win64/OsClipboard.cpp` (UTF-8 clipboard) | — | `FlashWindow`, `IsWindowFocused`, `FocusWindow`, `CopyToClipboard` |
| Settings window and Flash (the AddOns awesome ships) | built in: see "Built-in AwesomeCVar and Flash" | `awesomeBuiltInAddOns` (1) | `/awesome`, Esc menu and minimap buttons |
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
| Live aspect changes | `Base/Coordinate.cpp`, `CSimpleTop::RelayoutForAspect`, `GxuFontAspectChanged`, `AwesomeDisplayApplyAspect` | `uiLiveResize` (**1**, read at startup) | A new window aspect lays the game UI out again instead of reloading it, and fires `DISPLAY_SIZE_CHANGED`. Stock DDC coordinates span the screen's diagonal, so a UI unit's DDC size depends on the aspect and every stored frame size goes stale (hence the reload); with this on, DDC height is fixed at 0.6, a UI unit is always 0.6 / 768 DDC, and only the screen's width follows the aspect (at 4:3 both are the stock values). The new aspect is applied in `CSimpleTop::OnDisplaySizeChanged` after the fonts take the new pixel size and before frames measure text, the screen-anchored frames are laid out again, and every font string's horizontal NDC values are rescaled (they are converted once, when the string is made). Two culls assumed DDC within [0, 1] (`IsRectOffscreen`, `CRenderBatch::QueueFontString`); they now test the screen's DDC size. **The glue screens still reload**, as stock: the HD GlueXML places itself for the aspect once, when it loads, and has no event to redo it. World text (damage numbers, Miss, Dodge, …) measured its block with the shadow offset converted to NDC but wraps with the raw DDC offset `CalculateNewColor` gives it; once DDC width passed 1 (aspects wider than 5:3) the block was a pixel short and the last glyph dropped ("Mis"). `WORLDTEXTSTRING::RecreateString` now measures with the larger of the two. |
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

### Built-in AwesomeCVar and Flash (branch `feature/builtin-awesome-ui`)

awesome_wotlk ships two AddOns next to its DLL: **AwesomeCVar**, the settings window (Esc menu button, minimap button,
`/awesome`), and **Flash**, which flashes the taskbar on entering combat or on a whisper while the game is not focused.
The client now carries both itself; nothing has to be installed in `Interface\AddOns`.

| Part | Where | CVars (**default**) | What it does |
|---|---|---|---|
| The files | `WoW/Source/Awesome/Interface/AwesomeCVar`, `.../Flash` | — | The AddOns' sources, from `addons/` of the fork [Badgermilk0/awesome_wotlk](https://github.com/Badgermilk0/awesome_wotlk) (AwesomeCVar `3.0-waystone`, with the Waystone tabs), plus `Embedded.lua`. Edit them here; each `.toc` still gives the load order. |
| Embedding | `Awesome/EmbedInterface.cmake`, `Awesome/CMakeLists.txt` | — | At build time every `.toc`/`.lua`/`.xml` under `Interface/` becomes a byte array in a generated `AwesomeEmbeddedUiFiles.cpp` (build folder), made again when a file changes. About 350 KB. |
| Loading | `Awesome/AwesomeEmbeddedUi.cpp`, called from `CGGameUI::InitializeFrameXML` before `LoadAddOns` | `awesomeBuiltInAddOns` (**1**) | Each AddOn runs as FrameXML does, untainted, so its game menu button is secure: its `.toc`'s Lua files in order, each with the arguments an AddOn file gets (the name and a table shared by its files), then `ADDON_LOADED` with its name. A UTF-8 byte order mark is skipped, as the FrameXML loader does; a Lua error is written to `Logs\FrameXML.log` and the next file still runs. `awesomeBuiltInAddOns 0` turns this off (after a UI reload or relog). |
| AddOns of the same name | `LoadAddOnInfo` (`UIUtil/AddOns.cpp`) | — | `Interface\AddOns\AwesomeCVar` and `\Flash` are neither listed nor loaded while the built-in ones are on, so an old copy cannot load a second settings window or menu button. |
| Settings | `Interface/AwesomeCVar/Embedded.lua` | `awesomeGameMenuButton` (**1**), `awesomeChatMessages` (**1**), `awesomeMinimapHide` (**0**), `awesomeMinimapPos` (**220**), `awesomeFrameWidth` / `awesomeFrameHeight` (**empty** = the default size) | An AddOn keeps these in SavedVariables (`AwesomeCVarDB`), which only AddOns get. Here `AwesomeCVarDB` is a table that reads and writes these CVars, so they are kept in `Config.wtf`. The old `AwesomeCVar.lua` SavedVariables are not read: these start at their defaults once. |

**The client's settings window.** Every option in it is a CVar, so a change takes effect at once (or after a restart
where the card says so) and is kept between sessions: in `Config.wtf`, or for the account settings (camera, Lua
errors) in the client's account data. Nothing has to be edited outside the game. Besides awesome's own tabs and the
Waystone tabs (Display, Lighting, Sound, Performance), it now holds the stock settings that the stock option panels
leave out or cap:

| Where | Settings |
|---|---|
| **World** tab | `horizonFarclipScale`, `objectFade`. Effects: `spellEffectLevel`, loot sparkle, `SkyCloudLOD`, footprints and footstep dust, `ffxNetherWorld`. View distance, ground clutter, environment detail, particles, weather, glow, death effect, the texture settings and the frame rate caps moved to the native Video options (see "Native Video options"). |
| Rendering tab, Camera | `cameraDistanceMaxFactor` (1-4; the Interface option stops at 2), turn, pitch and zoom speeds, `cameraWaterCollision`, `cameraPivot`, `cameraDive`, `cameraBobbing`. |
| Sound tab, More | `Sound_ZoneMusicNoDelay`, armour sounds for yourself and others, `Sound_ListenerAtCharacter`. |
| Other tab | `screenshotFormat` (JPEG or TGA) and `screenshotQuality`; Troubleshooting: `scriptErrors`, `taintLog`, and `awesomeBuiltInAddOns`. |

Ranges stay inside what each CVar's callback accepts, so a value set here is never refused (a refused value is
dropped from `Config.wtf`).

Left out of the upstream AddOn, as they belong to a separately released mod: the About tab (the mod, its version and
links; the window opens on World instead), and the version broadcast (an addon message to the group and guild comparing awesome_wotlk releases) with its "Updates" option.
A text CVar (`screenshotFormat`) is compared as text by the reset button (`UI.lua`, `UpdateResetButtonState`).

Changes to the AddOns' own files: the `.toc` lists `Embedded.lua` first and has no `SavedVariables`; `Constants.lua`
shows `ACVar.ADDON_VERSION` ("3.0-waystone (built in)") where `GetAddOnMetadata` has no version; `Core.lua` has no
version broadcast; `UI.lua` has no Updates option and compares text values as text. These files are now the
client's own copy. To take something newer from the fork's `addons/`, merge it into them rather than copying over.

Tested offline: the client builds and embeds 19 files. A Lua 5.1 run of the embedded files the way the loader runs them
(toc order, `(name, table)` per file, then `ADDON_LOADED`), on a permissive WoW stub, checked these:
- all 16 AwesomeCVar files and Flash run;
- the addon initialises from the CVars' defaults and writes its settings back to them;
- all ten tabs are built and `/awesome` is registered;
- every card's CVar is one the client registers, and every card has its text.

The client also starts and exits cleanly with an old `AwesomeCVar` folder in `Interface\AddOns`.
**Not seen in the world**, where the UI loads: the window, the Esc menu and minimap buttons, and Flash.

### Native Video options (branch `feature/native-video-options`)

The graphics this client adds were reachable only through the AwesomeCVar window. The ones a player chooses (on or off,
how much, which renderer) are now in the game's own Video options (Esc > Video), as stock options controls: nothing
applies until Okay or Apply, Cancel puts it back, Defaults resets it to the CVar's default, a renderer change asks for
a restart. The fine tuning of each feature (per-type light strengths, fog shape, water refraction and foam, SSAO
radius, the tonemap curve, ...) stays in the AwesomeCVar window, whose tabs now say where the rest went; nothing is in
both places.

| Category | What it holds |
|---|---|
| **Resolution** (stock) | UI Scale slider down to 0.36 (pixel-exact at 2160p; stock 0.64). New **Scale with Display** (`uiScaleDpi`) in the free slot beside Reduce Input Lag. |
| **Effects** (stock) | The sliders reach the client's limits: View Distance 177-5017 (stock 1277), Environment Detail 0.5-3 (1.5), Ground Clutter Density 16-256 (64), Ground Clutter Radius 70-600 (140). Every stock quality preset still sits on its slider's grid, so Low to Ultra read and apply as before. |
| **Lighting** (new, D3D11) | A *Lighting Quality* preset slider like the Effects panel's: Off (stock), Low, Medium, High (the `/lighting on` set), Ultra, Custom. Sliders: Exposure, Bloom, Sharpening, Ambient Occlusion, Sky Lighting, Valley Mist (`fogHeight`), Sun Scattering, Firelight, Light Distance; Shadow Resolution (1024-8192) and Shadow Softness. Switches: HDR Rendering, Per-Pixel Lighting, Dynamic Lights, Specular Highlights, Stable Shadows. |
| **Water and Sky** (new) | Water (D3D11): Modern Water, Interactive Ripples, Reflections, Real Waves, Lava and Slime (`liquidModern`), Water Murkiness (`waterClarity`), Wave Height, Splashes. Sky and Weather: Smooth Sky, Light Transitions, Gradient Dithering, Lightning (off / storms / heavy rain too), Lightning Brightness. |
| **Advanced** (new) | Graphics API (D3D9, D3D9Ex, D3D11; restart) with the renderer this session runs, Monitor (`gxMonitor`), Interface Upscaling (`uiUpscale`), Animated Portraits, Max Foreground / Background FPS, Texture Sharpness (`texLodBias`, D3D11), Text Resolution (`fontMaxPixelSize`). |

How it works:

- **Built-in FrameXML.** `Awesome/Interface/FrameXML` (`FrameXML.toc`: `VideoOptionsStrings.lua`,
  `VideoOptionsPanelsWaystone.lua`) is embedded like the AwesomeCVar files and run by `AwesomeLoadBuiltInFrameXML`
  right after the FrameXML files and before the built-in AddOns, untainted, always (not tied to
  `awesomeBuiltInAddOns`). The panels are made with the stock templates and registered with `VideoOptionsPanel_OnLoad`,
  so the stock code sets up, refreshes, applies, cancels and resets them. Strings are globals as in GlobalStrings
  (`HDR_RENDERING`, `OPTION_TOOLTIP_HDR_RENDERING`, ...), English only; none collides with the client's GlobalStrings.
- **Requirements.** A control that needs D3D11 is disabled on another renderer, and the panel's description says
  why; a control that depends on a switch (Bloom on HDR, Real Waves on Interactive Ripples, Gradient Dithering on HDR
  across panels) follows the switch as set on screen, before Okay.
- **The view distance slider.** `GetCVarMax("farclip")` still says 1277 (both the game and glue scripts), and the
  stock handler sets the slider's step to a tenth of the range. A hook on the Effects panel's
  `PLAYER_ENTERING_WORLD` widens it afterwards on the stock 110-yard grid. The glue screen's Video options keep the
  stock ranges.
- **Lua API** (`Ui/VideoOptions_Scripts.cpp`, game and glue): `GetGraphicsAPIs()` (the renderers offered, as `gxApi`
  names) and `GetCurrentGraphicsAPI()` (the one this session runs; a changed `gxApi` waits for a restart, as
  `gxRestart` keeps the device).

Not moved: tuning knobs, the performance internals (threads, streaming, caches), sound, camera and nameplates, which
stay in the AwesomeCVar window. The login screen's Video options (GlueXML) do not have the new panels.

Tested offline by `Tools/VideoOptionsTests` (`python video_options_tests.py <stock FrameXML folder> [--png <dir>]`;
the stock files come from the client's MPQs, see its docstring). It loads the real stock options XML and Lua with a
small FrameXML loader under Lua 5.1 (lupa), runs the built-in files as the client does, and plays the options through
with the CVar defaults and clamps read from the C++ sources: labels and tooltips, Okay without changes changing nothing,
the stock presets on the widened Effects sliders, the Lighting presets against `/lighting on`, Cancel, the
requirements on D3D11 and D3D9, shadow resolution leaving the stock 0 alone until moved, the renderer change and its
restart popup, Defaults; then lays every panel out from its anchors (no control overlapping another or leaving the
panel) and draws them. 537 checks pass. **Not seen in the game.**

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
| Specular | `lightSpecular` (**0**), `lightSpecularM2` (**0.1**), `lightSpecularWmo` (**1**) | Only where the data asks for it: the WMO specular shaders (`MapObjSpecular_*`, which the stock `specular 1` turns on after a restart) get their specular per pixel plus the dynamic lights'; sphere-mapped M2 batches (`*_Env*`, the shiny ones) get the sun's highlight and the lights'. Everything else stays matte. |
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

### Sunlight, shade and shafts (branch `feature/sky-light-shadows`, D3D11 only)

A second pass over how the sun and the sky light the world. In the stock shaders a shadow is `colour × (0.7 + 0.3 ×
shadow)`: full shade still keeps 70% of all the light, ambient included. That is why a forest under a bright sun looked
flat, with no patches of sunlight on the ground. Here the shadow takes away the direct sunlight only. Every effect has
a CVar, all of them **default off**, and they need `extShadowQuality` 5: the three cascades are what they are made of.
`/lighting on` turns them on with the rest of the preset.

| Effect | CVars (**default**) | How |
|---|---|---|
| Sun shadows | `shadowSun` (**0**), `shadowSunStrength` (**1**), `shadowPenumbra` (**1.5**), `lightSunIntensity` (**1**), `lightAmbientIntensity` (**1**) | The lit families (M2, WMO, terrain, as in "Modern lighting") are lit again per pixel with the sun's term and the ambient apart. The sun's term is multiplied by the shadow, and the ambient is not. The shadow is read from the stock maps (the near map of the units, and the cascades 80, 320 and 1280 yards across, with the stock matrices and biases). Each map is read soft as the sun's disc makes it (PCSS): 8 gathers find the occluders around the point, and their distance gives the penumbra (the sun's angular radius × `shadowPenumbra` × the gap). Then 12 filtered compares are taken over that width, on a Vogel disc turned per pixel. A trunk's foot stays sharp, and light through a high canopy falls in soft patches. The first cascade that holds the point is used, blended into the next over its outer tenth; the near map is taken as the stock shaders take it (the darker of the two). In those draws the stock shadow taps read lit, so the shadow is not applied twice. WMO and terrain specular are shaded with the sun. The two intensities weigh the lights on lit surfaces; with HDR the lighting may reach 2. |
| Grass and unlit surfaces | (the same) | Grass (`DetailDoodad`) and surfaces that are not relit keep the stock formula. Their shadow taps go deeper instead (`1 − k × (1 − tap)`), so their full shade is the zone's shaded fraction: ambient over ambient plus 0.7 × sun, from Light.dbc's colours. Grass in shade is then about as dark as the ground around it. |
| Canopy shade | `lightCanopy` (**0** = off, to 1; needs `shadowSun`) | The ambient of lit surfaces dims under leaves and roofs. Eight points of a cone above the pixel (2 to 20 yards up, spreading by half their height) are looked up in the 320-yard cascade (the 1280-yard one past it) with a gather each. A point counts as open when nothing within 30 yards of it, toward the sun, is in the map; farther occluders are hills, whose shade is the sun's. Full cover dims the ambient to 0.3 at strength 1. |
| Sun shafts | `lightShafts` (**0** = off, else the intensity), `lightShaftsDensity` (**0.012** per yard), `lightShaftsDistance` (**120** yd) | After the opaque pass (before the height fog): at half resolution each view ray is marched in 32 jittered steps up to the geometry or the distance. Each step reads the cascades for the sun, and the in-scattered light is weighed by Henyey-Greenstein (g 0.55, capped at 4). A ray lit along most of its length (an open field) scatters a tenth as much, so the open air stays clear and light crossing shade stands out as shafts. Then the depth-aware blur both ways, as SSAO's, and an upsample by depth laid over the scene by alpha. The colour is the sun's at luminance 1.5 with a quarter of the fog's. It is dimmer when the sun is (dusk, the moon) and off inside WMOs. |

How it fits in:

| Part | Where |
|---|---|
| The maps, matrices and their depth scale, read from `CShadowCache` after the shadow pass (`GatherShadows`), the CVars, the zone's shaded fraction | `Awesome/AwesomeLighting.cpp` |
| `GxLightingShadows` and the new parameters | `Gx/GxLighting.h` |
| The maps at t21-t24 with a point sampler (s13) and a comparison sampler (s14), both bordered as lit, only between SetupView and EndWorld (the shadow pass writes them before SetupView); the sun constants in b3; the shafts pass | `Gx/CGxDeviceD3d11/D3d11Lighting.*` |
| Which draws sample a cascade (outdoors) or the interior map, per draw into `GxDraw.gxShadowDraw` | `Gx/CGxDeviceD3d11/CGxDeviceD3d11Draw.cpp` |
| `GxStockShadow` around every depth-compare tap, `GxSunShadow` / `GxCanopy` / `GxSunLit*` in `s_hlslLighting`, the sun path of `GxLightPixel` | `Gx/CGxDeviceD3d11/D3d11Dx9Shader.cpp` |
| Sun Shadows and Canopy Shade on the Lighting panel (and in its quality levels), Sun Shafts on the Water and Sky panel | `Awesome/Interface/FrameXML/VideoOptionsPanelsWaystone.lua`, `VideoOptionsStrings.lua` |

After the first in-game test (the strength changed with the camera, and the shadows flickered while moving):
the PCSS taps turn by a hash of the shadow map's texels instead of the screen's pixels, so the pattern stays on the
ground. With `shadowSun` on, the 320- and 1280-yard cascades keep casters down to 0.5 and 3 yards
(`CShadowCache::s_fineCasters`; stock 2 and 10), so a bush's shadow no longer vanishes at a cascade edge. The
cascade blend is wider (the outer quarter). The shafts take a new offset along the rays each frame and blend 88% of
the last frame's, reprojected through its view and projection (the origin moves with the camera) where the depths
agree. Their view dependence is lower: g 0.4, phase at most 2.5, open air damped to 35%, colour at luminance 1,
opacity at most 0.6.

Second test: the shadows on the ground still turned with the camera. The terrain's C0-C3 is world × view
(`CMapRenderChunk::InitializeVertexShaderConstants`), so the terrain vertex shaders' position is already in view
space; the surface outputs took it through the view matrix again. Fixed, which also corrects the terrain's dynamic
lights and its sky-ambient up direction from "Modern lighting" (both had turned with the camera on the terrain). The
canopy cone's axes now come from the world's, and `lightingDebug` 6-8 are no longer taken by the SSAO view.

Third test (stable now): leafy small trees flickered, and Darkshore's fog went white. The shadow lookup now sits a
little off the surface along its normal (0.03 yd plus 0.15% of the distance), and models take at least 2.5× the
least penumbra. The shafts scale with the sun's share of the zone's light, `(1 − shaded fraction) × 1.6`, so an
overcast, foggy zone shows none. Open air is damped to 15%, opacity is at most 0.45, and the density is 0.012. The
canopy's darkest is 0.45 of the ambient.

Fourth test: pale creatures looked glossy and sunlit scenes a little bright, Darkshore's contours washed out, and
in Orgrimmar a darker square followed the player.
- The lighting on lit surfaces is clamped at 1 as stock (the 2 allowed with HDR made pale fur shine). It goes
  higher only with `lightSunIntensity` above 1.
- A shadow darkens at least as much as the stock one: at most `(0.7 + 0.3 × shadow)` of the unshadowed light on
  sun-facing surfaces. The grass's taps are never deepened less than stock (k ≥ 1).
- The near map is left out. It follows the player, and ground that is also a caster in it shaded itself. The
  cascades hold the units too.

Fifth test: the Orgrimmar box remained, with near-black roofs beside the player. Those WMO surfaces are not relit.
Their stock taps still read the near map, in which the city's ground shades itself, and the deepened taps made
that nearly black. With `shadowSun` on, every stock tap of the near map now reads lit: `ShadowKindOf` returns 3,
the draw's near-map slots go in `gxShadowDraw.z`, and `GxStockShadow` takes the slot. The M2 sphere-map highlight
(`lightSpecularM2`) defaults to 0.1, since 0.3 made fur look glossy.

Sixth test: a faintly darker square stayed around the player in Orgrimmar. `lightingDebug 2` showed the ground
relit, and `shadowSun 0` removed the square. The stock bias is least on the 80-yard cascade: 0.9 yd, about half of
it eaten by the map's own depth scale, `(z − 1) / 3999` written against `z / 4000` read. So flat ground shaded
itself there. `shadowSunBias` (yards, scaled by √(radius / 40) for the wider cascades) can add to it, but it changed nothing.
`lightingDebug 9` then showed the square was exactly the 80-yard cascade, and skipping that cascade removed it with
no other shadow lost. That cascade shades Orgrimmar's open ground where the others do not. So
`shadowSunFirstCascade` (**2**: the 320-yard cascade first, still ~4 cm a texel at 8192; 1 restores the 80-yard
one) is the default, and `shadowSunBias` defaults to **0**.
`lightingDebug 9` colours the cascade used: red 80 yd, green 320, blue 1280.

`lightingDebug` 6 shows the sunlight's shadow alone, 7 the canopy's dimming, and 8 the shafts. `/lighting` reports the
shadow quality and whether the maps were found. A model indoors samples only the near map or the interior map. It
keeps the stock shadow, because only a draw that samples a cascade counts as outdoors.

**Tested offline only:** the translated corpus and the post shaders compile, and the options panels pass their layout
test. None of it can be seen without the world. The strengths (penumbra, canopy floor and reach, shaft density, phase
and open-air damping) are starting points to tune in game.

### Weather overhaul (branch `feature/weather-overhaul`)

The stock weather (`WorldClient/MapWeather.cpp`, the SMSG_WEATHER case in `Ui/GameUI.cpp`) takes a Weather.dbc row
and a grade. Particles are `(grade - 0.25) / 0.75`, so nothing falls at or below 0.25. The storm sky is `4 * min(grade,
0.25)`, fully stormy from 0.25. A change takes 10 s per 1.0 grade. The row's sound loop *replaces* the zone ambience
and is silent indoors. There is no lightning. Two bugs: a sandstorm restarts its effect on every packet (it has no
default texture, so `SetType` always sees a texture change), and a rain row's own texture (red, black) stays on
later rain rows without one. Snow does not restart; `GetType` tests all three effects (checked against the 12340 exe
at `0x783B60`). This overhaul works with any server and changes nothing a stock server relies on unless a CVar says so.
It also gives a server finer control through an optional extension of the packet.

| Part | Where | CVars (**default**) | What it does |
|---|---|---|---|
| Model | `WorldClient/WeatherModel.*` | — | The arithmetic, free of the engine: grade curves, the extension parser, the texture choice, the lightning scheduler, the flash envelope and the thunder. Unit-tested by `Tools/WeatherTests`. |
| Stock fixes | `Weather::SetType` → `WeatherModel::ChoosePrecipTexture` | `weatherFixes` (**1**; 0 = stock logic) | A sandstorm keeps running across packets. Rain without its own texture goes back to the default drop. |
| Particle curve | `Weather::SetStormIntensity`, the fade in `Weather::Update` | `weatherParticleStart` (**0.25** = stock) | The grade at and below which nothing falls; particles rise linearly from there to 1. |
| Sky curve | `Weather::SetType` (the fog target is storm sky / 4) | `weatherSkyCurve` (**0** = stock), `weatherSkyMin` (**0.5**) | Curve 1 grades the sky under precipitation: `weatherSkyMin` at the first drop, full storm at grade 1. Sky-only rows (fog, id 0) keep the stock ramp, so a server's 0..0.25 sky states look the same. Left at stock by default because mod_weather_vibe is tuned to it; with curve 1 its overcast-to-light-rain step brightens the sky. |
| Density | `MapWeather_CVar.cpp` (`s_weatherDensityBase`, `Weather_RefreshDensity`) | `weatherDensityScale` (**1**, 0.1-2) | The particle rate is the `weatherDensity` level × this × the server's density. |
| Lightning | `AwesomeWeather.cpp` (`UpdateLightning`, `FireStrike`) through `DayNight::SetFlashColor` | `weatherLightning` (**1**: the thunderstorm row 86 and servers that ask; 2: also rain at `weatherLightningGrade` **0.85**+; 0 off), `weatherLightningRate` (**6**/min at grade 1), `weatherLightningBrightness` (**0.8**), `weatherLightningDistance` (**200** yd, nearest strike), `weatherLightningBurst` (**0.15**, chance of restrikes) | Strikes come as a Poisson process (at least 1.5 s apart), 200-3000 yd away, most of them far. Each is 1-4 return strokes, and a strike may be followed by up to three restrikes nearby within about two seconds. The flash uses the hook Eclipse uses, which tints the sky dome, fog, ambient and sun light toward bluish white, brighter when near and a third as strong indoors. |
| Thunder | same, `PlayThunder`; `WeatherModel::ThunderParts`; the chains in `Awesome/AwesomeThunderDsp.*`, attached from `SI2::DSP_AttachCustomDspChain` (a name SoundFilter.dbc does not have) and released in `SI2::DSP_ShutDown` | `weatherThunder` (**1**), `weatherThunderVolume` (**1**), `weatherThunderNearKit` (**8439** Blasted Lands bolt), `weatherThunderFarKit` (**13006** Shaman_Thunder), `weatherThunderLayers` (**1**; 0 = one sound a strike), `weatherThunderBass` (**2.2**, 1 = flat, 3 the most), `weatherThunderBoost` (**8** dB, up to 24; the crack takes 1 dB less) | Plays after the flash, delayed by distance / 375 yd/s. A strike is built from layers. Under 600 yd: the crack (near kit), a body (far kit pitched down to 0.55-0.67) at the same moment, and a roll (far kit at 0.75-0.85, at 70%) 0.45-0.95 s later. To 1500 yd: body and roll. Beyond: a distant roll at 0.62-0.77, low-passed at 650 Hz. Each layer plays at its own level rather than the kit's SoundEntries volume, through an FMOD channel group with two bass bands (50 and 110 Hz), a 3 kHz presence band on the crack, a lowpass on the others (2600 / 1400 / 650 Hz), and a compressor whose make-up gain goes past the channel volume cap of 1. Its threshold keeps peaks about 1 dB under full scale. The defaults were picked by ear in game: 1.8 / 6 first, then 2.2 / 8. A trial of 3 / 10 crackled in the speakers (short peaks at full scale on 0.1-0.7% of samples), so 2.2 / 8 stayed. With them a test clap comes out about 16 dB louder with about 20 dB more bass; only the crack touches full scale, on about one sample in 8,000 (`WeatherAudioTests`). It plays on the ambience channel, so the ambience slider and `Sound_EnableAmbience` apply. Thunder is quieter indoors. |
| Sound layer | `AwesomeWeather.cpp` (`UpdateLoops`, `DriveLoop`), `SI2::SetZoneAmbienceScale` in `SoundInterface2ZoneSounds.cpp` | `weatherSoundLayer` (**1**; 0 = stock replace), `weatherSoundIndoor` (**0.35**), `weatherAmbienceDuck` (**0.6**), `weatherRumbleKit` (**4994**, DarkPortal), `weatherRumbleVolume` (**0.5**) | The row's loop plays on its own pair over the zone ambience and crossfades over 4 s. Its volume runs from 45% at the first drop to 100% at full particles, muffled indoors instead of cut. The zone ambience is ducked by up to `weatherAmbienceDuck` as the particles rise. While lightning runs under any row other than the thunderstorm (whose loop is the rumble already), a second loop, the distant-thunder bed, plays at a level that follows the strike rate. |
| Server extension | `GameUI.cpp` SMSG_WEATHER → `AwesomeWeatherOnPacket`, `WeatherModel::ParseExt` | — | After the stock `uint32 id, float grade, uint8 abrupt`: `uint32 'WVX1'` (bytes `57 56 58 31`), `uint8 count` (at most 32), then `count × {uint8 key, float value}`. Out-of-range values are clamped, NaN and unknown keys are skipped, and a block that is cut short is ignored. Every packet replaces the previous block, so a server sends every field it wants each time. The net layer skips unread bytes, so a stock client ignores the block. The keys are listed below. |
| Override and tests | `AwesomeWeather.cpp` | — | `/weather` (`/wx`) draws any weather locally until `/weather off`. The server's packets keep arriving underneath and are applied again then. Lua: `GetWeatherInfo()` (a table), `SetWeatherOverride(id, grade[, abrupt])`, `SetWeatherOverrideField(name, value|nil)`, `ClearWeatherOverride()`, `TriggerLightning([yards])`, `GetWeatherRows()`, `GetWeatherFields()`. |

Extension keys. The name is the one `/weather <name> <value>` and `SetWeatherOverrideField` take. "Default" is what the
client does when the key is absent.

| Key | Name | Range | Meaning | Default |
|---|---|---|---|---|
| 1 | `sky` | 0-1 | storm sky blend | from the grade (`weatherSkyCurve`) |
| 2 | `particles` | 0-1 | particle intensity, whatever the grade | from the grade (`weatherParticleStart`) |
| 3 | `density` | 0-2 | particle count multiplier | 1 |
| 4 | `lightning` | 0-60 | strikes per minute at full weather (0 = none, even for row 86) | `intensity`, else automatic |
| 5 | `transition` | 0-600 | seconds to reach the new grade, sky and particles (0 = at once) | 10 s per 1.0 grade |
| 6 | `volume` | 0-1 | weather loop volume | 1 |
| 7 | `strikedistance` | 0-3000 | nearest strike, yards | `weatherLightningDistance` |
| 8 | `strikemaxdistance` | 50-3000 | farthest strike, yards | 3000 |
| 9 | `flash` | 0-2 | flash brightness multiplier | 1 |
| 10 | `thunder` | 0-2 | thunder volume multiplier (above 1 lifts the quieter layers and far strikes up to full) | 1 |
| 11 | `nearchance` | 0-1 | share of strikes within 600 yd (the crack, the bright flash) | from `intensity`, else about 1 in 8 |
| 12 | `burst` | 0-1 | chance of each of up to three restrikes | from `intensity`, else `weatherLightningBurst` |
| 13 | `intensity` | 0-1 | the storm: lightning rate (× `weatherLightningRate`), near chance (0.05 + 0.3 × it), bursts (0.35 × it) and rumble volume when those keys are absent; the grade stays the precipitation | none |
| 14 | `soundkit` | id | the weather loop in place of the row's (0 = silent) | the row's |
| 15 | `rumblekit` | id | a second loop under the weather (0 = none) | 4994 while lightning runs |
| 16 | `rumble` | 0-1 | its volume | `intensity`, else 1 |
| 17 | `nearkit` | id | the crack of near strikes | `weatherThunderNearKit` |
| 18 | `farkit` | id | body and roll of every strike | `weatherThunderFarKit` |
| 19 | `duck` | 0-1 | zone ambience turned down under full weather | `weatherAmbienceDuck` |
| 20 | `indoor` | 0-1 | weather loop volume indoors | `weatherSoundIndoor` |
| 21 | `flashcolor` | 0-0xFFFFFF | flash colour as `0xRRGGBB` in the float (exact up to 2^24) | `0xC8D7FF` |

A server wanting a thunderstorm on heavy rain sends, for example, id 5, grade 0.9 and `{13: 0.8}`. A dry lightning storm
under an overcast sky is id 1, grade 0.25 and `{4: 10, 11: 0.3}`.

`/weather` commands: no argument for the status; a preset (`clear fog overcast drizzle lightrain rain heavyrain storm
lightsnow snow heavysnow blizzard lightsand sand sandstorm blackrain redrain mist`) with an optional grade and `now`
(abrupt); `id <weatherId> [grade] [now]`; `grade <g>`; `<field> <value|off>` with any field above; `fields` (the list
with ranges); `strike [yards]`; `list` (every Weather.dbc row); `off`.

Tested: `WeatherTests` (curves against the stock formulas, the extension block round trip, malformed blocks and a block
with every key, the stock and fixed texture logic, ten simulated minutes of the scheduler, 2,000 strikes' envelopes,
strike placement by near chance, bursts, the thunder layers, flash colours) and `WeatherAudioTests`, which renders the
thunder chains through the client's own FMOD Ex offline (`WAVWRITER_NRT`) on a synthetic clap and measures the loudness
gain, the bass gain, the pitch-down's longer tail, a flat chain leaving the low end alone, and clipping. Both run with
`ctest -R Weather`. The client builds. **Not yet seen or heard in the world**, apart from the user's first listen to
the first version: the flashes and the row 86 rumble worked, and the single-sound thunder was too weak, which led to
the layers and chains. The flash's strength, the layered thunder's loudness, the loop and duck levels, and the
override commands are starting points to tune in game.

### Modern water and a smoother sky (branch `feature/water-sky`)

No new assets: the stock water textures, the stock depth textures (which carry Light.dbc's shallow and deep water
colours) and the zone's sky colours, shaded in a modern way. The water is D3D11 only (the D3D9 devices keep the stock
water); the sky smoothing works on every device. `/water [on|off]` prints what the water did last frame.

What the stock client does (read from the source): every world liquid but magma and slime is material 1,
`CMaterialWater` (`vsLiquidWater` / `psLiquidWater`). The pixel shader is 11 instructions: the depth texture (colour and
alpha by the vertex's water depth) times the per-vertex sun and ambient, plus the animated water texture (`lake_a`,
`ocean_h`, `fast_a`), plus a per-vertex sun highlight, alpha blended, no depth write. The splash and wake decals
(`WaterRipple.cpp`, `XTextures\splash`) are textures projected on the water for a moment; nothing moves the surface.

| Part | Where | CVars (**default**) | What it does |
|---|---|---|---|
| Water shading | `D3d11Dx9Shader.cpp` (`Family_WaterVertex` / `Family_WaterPixel`, `s_hlslWater`), `D3d11Water.*` | `waterModern` (**1**), `waterRefraction` (**1**), `waterClarity` (**1**), `waterReflection` (**1**: screen-space; 0 the sky alone), `waterReflectionStrength` (**1**), `waterDetail` (**1**), `waterFoam` (**1**), `waterDebug` (**0**; 1 normals, 2 depth, 3 height field, 4 reflection hits) | The translated `vsLiquidWater*` hand their view-space position on; the translated `psLiquidWater*` still compute the stock colour, then, while the water constants (b5) are on, shade the surface from it. Before the transparent liquids (`CWorldScene::RenderLiquidPass`) the opaque scene's colour is copied (resolved under MSAA; the HDR scene with `hdr`) and its depth turned into clip w (R32F). From them: the water's thickness along the ray, what lies under it shifted by the bent surface (not where something above the water would show), absorbed with depth toward the body colour (the colours the body lacks go first), reflections by Schlick's Fresnel of the world (24 growing steps through the depth copy and a 5-step refinement, faded at the screen's edge), of the sky as drawn where the reflected direction meets it on screen (clouds included), else of Light.dbc's six sky bands on the dome's geometry (the ambient indoors); the sun's glint (sharp and broad lobes, faded at the horizon), foam at the shore and on disturbed water broken up by the stock texture, the stock fog on top. The normal comes from the stock animated texture's brightness taken as height, at its scale and turned and 2.7 times larger drifting across it, faded with distance, plus the height field. From below the surface: Snell's window. |
| Interactive ripples | `D3d11Water.*` (`IStep`, `s_hlslWaterPasses` in `D3d11WaterHlsl.cpp`), `Awesome/AwesomeWater.cpp`, `CGUnit_C::CreateRipple` | `waterRipples` (**1**), `waterRippleStrength` (**1**), `waterInteraction` (**1**), `waterRain` (**1**) | A height field of 512 × 512 R16G16F texels (now, a step ago), 0.25 yd each, 128 yd around the player, scrolled by whole texels as the player moves (absolute texel indices, so it stays put in the world). 60 steps a second of the damped wave equation (5-point Laplacian, 3 yd/s, amplitude e-folding 1.7 s, an absorbing border of 24 texels). `CGUnit_C::CreateRipple` runs every frame for every visible unit in liquid shallow enough to touch it; before the stock decal's rate limit it now reports the unit: a moving one pushes a Gaussian dip (its size from its collision height) as it goes and leaves a wake, one swimming in place bobs and sends out rings, crossing the surface (the stock splash event 201) splashes; units standing in shallow water do nothing. In rain (`Weather::GetType` 1) up to 3 drops a square yard and second by particle intensity, none indoors, each hashed from its world texel and step. The stock decals stay. |
| Real waves (tessellation) | `s_hlslWaterTessellation` (`D3d11WaterHlsl.cpp`), `s_hlslPatchPoint` and `Variant_Tessellated` (`D3d11Dx9Shader.*`), `D3d11Water::BindTessellation`, `CGxDeviceD3d11::IStateSync` / `Draw`, `WaterStripToList` in `WorldClient/LiquidGeomFactories.cpp` | `waterTessellation` (**1**), `waterWaveHeight` (**1**), `waterStockRipples` (**0**) | The first version only bent the shading by the height field, and the user saw no physical reaction. Now the surface moves. Hardware tessellation takes patch lists, and the liquid factories build strips (with degenerate joins). On this device they now build lists of the strips' real triangles, in place: the ADT chunk buffer already holds 6 indices a tile, and the WMO one is allocated 9 per vertex. A water draw from a list with both water shaders uses the vertex shader's `Variant_Tessellated` translation. It hands the varyings on as `GxPatchPoint`, without system values, because hull and domain inputs may not carry `SV_ClipDistance1`. The hull shader cuts each edge by its length over max(0.3 yd, 2% of its distance), up to 32, inside the height field and within 90 yd. An edge's factor depends only on its two ends, so neighbours cut it alike and no crack opens. The domain shader interpolates, raises the point by the field's height × `waterWaveHeight` and projects it again. The stock splash and wake decals are left out while the field runs (`waterStockRipples 1` brings them back). The impulses are stronger than in the first version (a splash 0.35 yd, a swimmer 0.8 + 2 × pace / 7 yd/s, a bob 0.6). The texture relief now weighs 1.2 and the field's slope 2.5, so the field carries the surface near the player. |
| Smooth sky | `Awesome/AwesomeSky.cpp`, `WorldClient/SkyModel.*`, `DayNight::DNSky::Render` | `skySmooth` (**1**) | The stock dome is 6 rings × 24 colours, Gouraud shaded: each band a straight ramp with a kink at every ring, and the rings near the horizon are a few degrees apart, so they show as stripes. The same colours (`DNSky::SetColors`: highlight, lightning flash) are drawn on 96 columns × 51 rows, a monotone cubic (Fritsch-Butland tangents, no overshoot) across the rings and a clamped periodic Catmull-Rom around them; recoloured only when the colours change. |
| Light transitions | `AwesomeSkySmoothBands`, from `DayNight::SetColors` before `SetLightColors` | `skyTransition` (**1.5** s; 0 = stock) | The zone's `CurrentLight` (18 colours: ambient, sun, shadow, sky, sun and cloud, water; the fog distances and rates, cloud density, water alphas) eases toward each new value with this time constant, so light boundaries, area lights and overrides do not snap. Snaps on a map change, a camera jump over 80 yd, entering or leaving liquid, a hitch over 1 s, and a map with no lights. The lightning flash is outside it and stays instant. |
| Dither | `TonemapPs` in `D3d11Lighting.cpp` (`gxExtra`) | `skyDither` (**1**, in 8-bit steps; needs `hdr`) | Triangular noise before the frame quantizes, so the sky and fog gradients of the 16-bit scene do not band in the 8-bit frame. |

**The HD client's water is procedural water.** Its `patch-w.mpq` replaces `LiquidType.dbc`: water, ocean, rivers and WMO water become material 3, `CMaterialProcWater` (`vsLiquidProcWater` / `psLiquidProcWater`: two cube reflection maps, the depth texture at s2, `ocean_h` at s3, two normal maps at s4 / s5, lit from pixel constants C6-C8; no vertex colour). The first two versions only knew `psLiquidWater` and shaded nothing in the live client (`/water` said 0 water draws). Both are water families now; the shading reads its inputs by kind (`WX_KIND` 1 or 2: for procedural water the normal from its own normal maps, the body colour from s2 lit by C6-C8, the foam pattern from s3). The vertex side is the same: both place the vertex with C5-C8.

**Tuned after the first working test** (the wake read as yard-high crests, the reflections had contour lines): the height field is R32G32_FLOAT (at 16 bits neighbouring heights differed in steps, and the slopes drew contours), slopes over two texels, heights clamped to 0.3 yd, damping 0.9 / s; a splash 0.12 yd, a swimmer 0.25 + 0.5 x pace / 7 yd/s, a bob 0.2; `waterWaveHeight` 0.5 by default; the field's slope weighs 1.2 in the normal.

**Splashes and lingering foam** (branch `feature/water-life`, `D3d11WaterParticles.cpp`, `s_hlslWaterParticles`; `waterSplashes` (**1**, the amount; 0 off)). GPU particles, no texture: a ring of 8,192 in a structured buffer. `EmitCs` starts the frame's splashes from up to 32 emitters, `UpdateCs` moves every particle (gravity 15 yd/s2, drag) and turns a droplet that falls back through the surface into an impact on the height field (`InterlockedAdd` into an R32_UINT texture of its texels, which the next step adds like a raindrop and clears), `ParticleVs` / `ParticlePs` draw them after the transparent liquids as camera-facing quads, droplets stretched along their motion and brightest at the rim, mist as soft growing puffs, lit by the sky and the sun, depth-tested against the scene. Particle space is the world less a fixed origin, moved only on a teleport. Emitters (`AwesomeWaterOnUnitInLiquid`, from `CGUnit_C::CreateRipple`): falling into the water (`MOVEFLAG_FALLING` / `FALLING_FAR` at the surface crossing, event 201) throws a crown of 70 droplets, a column of 25 and 12 mist puffs; walking in, 22; walking out, 14 running off; a unit moving at the surface sprays 4 + 14 x pace / 7 droplets a second ahead of it. Nothing runs when nothing is in the air. The height field became RGBA32F: z is foam, made by disturbances, droplet impacts and fast crests, spread to the neighbours and fading to a third in about 3 s; the water shader draws it as foam broken up by the stock texture. WaterTests: the foam where a splash fell (0.99 after 2 s, 0.04 after 12 s), and the particles on WARP (100 droplets up to 0.64 yd, all back within 2.25 s, their impacts on 48 texels).

**Droplets that look like water** (after the first in-game look: flat blue-grey coins). A droplet is drawn as a small sphere of water: the scene copy behind it turned over through its middle (a lens), the sky at its rim by Fresnel, a sun glint, a crisp edge; it streaks along its motion over about a 30th of a second with its light spread over the streak, and one smaller than a pixel is drawn a pixel wide and fainter. Most droplets are tiny (radius 0.01 + 0.03 k^2 yd). A third kind, sheets, are the torn crown of a jump: thin strips stretched along their flight that live a quarter to half a second. Mist is faint (alpha 0.04). A jump throws 36 sheets, 260 droplets, a column of 70 and 8 mist puffs, up to about 3 yards; walking in 10 sheets and 70 droplets; walking out 40; spray 12 + 40 x pace / 7 a second. The water itself: the sun's glint widens and dims where the normal changes within a pixel (no sparkle), and a reflection hit fades as the ray leaves the surface it met (no jagged lines at object edges). `WaterTests --render <rgba> <w> <h> <out> [seconds]` draws a jump's splash over a background with the real shaders on WARP, to look at.

The droplets' lens first sampled the copy taken before the liquids, which holds the lake bottom, so droplets over water showed dark. `AfterLiquids` copies the frame again (resolved under MSAA) before drawing them: they carry the water's own colour and reflections. Droplet radius 0.005 + 0.017 k^2 yd, sheets 0.035-0.07 yd.

Settings: the Lighting tab of the built-in settings window has Water and Sky sections.

Tested offline (`Tools/WaterTests`, `ctest -R WaterSky`; with the folder of the stock `vsLiquidWater.bls`,
`psLiquidWater.bls`, `psLiquidWaterNoSpec.bls` as argument it also translates them): the sky interpolation passes
through the stock colours and stays monotone and in range, the dome rows include every stock ring and match
`GenSphere`'s vertices, the easing is frame-rate independent; the passes compile (MSAA and not); the height field run
on a WARP device spreads a splash's front 6.1 yd in 2 s (6 expected at 3 yd/s), symmetric, decays to under a fifth in
12 s, and stays finite and bounded (0.1 yd) through a minute of heavy rain; all 4 `vsLiquidWater` and both water pixel
shader records of the HD client translate with the water family and compile, the vertex shaders also as their tessellated
variant; the stage signatures link (by reflection: VS → PS, VS → HS, HS → DS, DS → PS); and on WARP the tessellation
turns a 4-yard quad 5 yards ahead into 1,066 triangles, every point lifted by the field's 0.5 yards (streamed out from
the domain shader). The client builds and runs on the login screen. **Not seen in the
world yet**: how any of it looks, the impulse strengths, and the cost at 4K are starting points to tune in game.

### Every liquid: magma and slime (branch `feature/liquids`)

**The live client's liquids** (its `LiquidType.dbc` from `patch-enUS-w.mpq` / `patch-w.mpq`, read with the MPQ reader):

| Material | Shaders | Rows | Stock look |
|---|---|---|---|
| 3, procedural water | `vsLiquidProcWater` / `psLiquidProcWater` | Water, Ocean, Slow / Fast Water and Ocean, WMO Water / Ocean / Water - Interior, Lake Wintergrasp, Basic Procedural Water | transparent (the modern water above) |
| 1, water | `vsLiquidWater` / `psLiquidWater` | Coilfang Raid - Water, Hyjal Past - Water, **Orange Slime** (181, a `LavaOrange` texture) | transparent (the modern water above; Orange Slime was shaded as water) |
| 2, magma | `vsLiquidMagma` / `psLiquidMagma` | Magma, Slow / Fast / WMO Magma, Green Lava (`LAVAGREEN1`), CoA Black - Magma, Chamber Magma; Slime, Slow / Fast / WMO Slime, Naxxramas - Slime | **opaque and unlit**: `psLiquidMagma` is texture × vertex colour (white outdoors, the WMO group's liquid colour indoors) with fog; drawn by the liquid renderer's opaque pass inside `CWorldScene::Render`, before the models, so there was nothing to shade it from |

Units in magma and slime made no ripple at all: `CGUnit_C::CreateRipple` stops at LiquidType flag 1, which only the water rows have.

| Part | Where | CVars (**default**) | What it does |
|---|---|---|---|
| Classes | `AwesomeWater.cpp` (`Classify`, `AwesomeWaterNoteLiquid` from `Liquid::CSettingsBank::UpdateRecord`, `AwesomeWaterLiquidRender` from each shader material's `Render` → `IGxWater::SetLiquid`) | `liquidModern` (**1**; needs `waterModern`) | Each row is water, magma (material 2, or a lava or magma texture) or slime (named so, which takes in Orange Slime). The device binds the class's constants (one b5 buffer for each, `gxWaterFlags.w`) and its height field per draw. |
| Opaque liquids later | `CWorldScene::Render` (`AwesomeWaterDefersOpaqueLiquids`), `CGWorldFrame::OnWorldRender` after `AwesomeLightingOpaqueDone` (`AwesomeWaterOpaqueLiquids`, also from `AwesomeWaterEndWorld` if nothing drew them), `IGxWater::BeforeOpaqueLiquids` | — | While the D3D11 device shades them, the opaque pass waits for the opaque world (models, grass, SSAO and height fog done: their light is their own) and the scene is copied for it, as for the water (`ICapture`). Stock order otherwise. |
| Shading | `psLiquidMagma` → water family, `WX_KIND 3` (`FamilyForShader`, the translator's sampler test); `WxThickShade` in `s_hlslWater` (also for a `psLiquidWater` liquid of another class) | `liquidGlow` (**1**) | **Magma**: heat from the texture's brightness against its own mean (its smallest mip), so dull and bright textures both get crust and veins; slow pulses where the texture, larger and drifting, wells up; hotter where the thick field is stirred (wakes, bursting bubbles, globs falling back); cooler and crusted against the shore (by the depth copy). The crust is the texture darkened and lit like rock with a dull sheen and a glassy sky reflection at grazing angles; the melt is its own colour, paler as it heats (`WxHotter`: weaker channels raised, so orange goes yellow and fel green pale green), times 1.35 × `liquidGlow` into an 8-bit frame (scaled whole, `WxFit`, so it never clips to white) or 2.6 × into the HDR scene (it passes 1 and blooms). The surface folds over itself (two drifting samples warp the texture). Opaque in a yard; a thin edge shows the rock under it. **Slime**: lit by the sun and the sky but keeping 65% of its stock (unlit) colour as its own glow; smooth with broad slow swells; glossy (Fresnel, the world by the same ray march as water at up to 16 steps, the sky, a sharp glint and a broad wet sheen); froth where stirred; the bottom shows only where it is a few inches deep, tinted by it. No refraction for either. `waterDebug 5`: heat (red) and stir (green). |
| Thick height field | `D3d11Water` (`s_fields` = 2, `FieldOf`), `SimPs` viscous mode (`wsThick`) and `WsBubbles` in `s_hlslWaterPasses` | `liquidBubbles` (**1**) | A second 512 × 512 field for magma and slime, run only while one was drawn within 150 yd or disturbed in the last 2 s (it starts flat again). Viscous: its motion is damped hard (10 / s magma, 6 / s slime), the surface settles back to level (0.5 / s, 0.9 / s), waves crawl (0.6, 1 yd / s), and a disturbance, a bubble or a settling displaces it without a push, so a dent oozes shut where it fell instead of ringing (the water's update turns any one-step change into velocity and has a self-oscillation; on bubbles that rang them up about five times). Bubbles rise on a grid of one-yard cells: at most one a cell and step, somewhere in it, a few large, most small; each lifts the surface (5 cm magma, 3 cm slime, radius 0.45 / 0.3 yd) and stirs it as it bursts (magma glows there). Its foam (stir) fades in about 1.7 s. Physics follow the nearest thick liquid drawn. |
| Units | `CGUnit_C::CreateRipple` (a liquid without flag 1 now reports too, with the same depth rule), `AwesomeWaterOnUnitInLiquid` (`liquidType`) | the water's `waterInteraction`, `waterSplashes` | A splash dents a thick liquid 1.4 times as deep and 1.2 times as wide; wakes push 1.3 times as hard; treading bobs slower. **Magma** throws few slow globs (30 a jump, 8 walking in, 6 drips out, 2-7 a second moving) and showers of embers (40, 12, 1.5 × the globs moving). **Slime** throws gobs (60, 20, 16 out, 4-16 a second) and strands (24, 6, 6 out). |
| Particles | `s_hlslWaterParticles` (`WxParticle` now 64 bytes: colour and liquid), `D3d11WaterParticles.cpp` (three float4 an emitter, an impact texture for each field) | — | Magma globs (0.03-0.09 yd, little drag) are molten through the middle with a crust at the rim and cool over 1.6 s in the air; embers (tiny) rise on the heat, flutter, slow and dim over 0.8-2.2 s, a hot core in a halo past 1 in the HDR scene, and leave nothing on landing. Slime gobs are opaque, lit, wet (sky at the rim, a glint); strands hang (gravity 0.6, drag 1.2) and stretch. Their colour is the class's: magma orange, Green Lava fel green, slime green, Orange Slime orange. Drops of a thick liquid land on its own field, twice as heavy. |
| Settings | Water and Sky panel: **Lava and Slime** (`liquidModern`, needs Modern Water); AwesomeCVar tuning: Lava Glow, Lava and Slime Bubbles | | `/water` prints a third line: magma and slime draws shaded, whether the scene was copied for them, their field and its class, the bubble rate. |

Tested offline: `WaterTests` (3324 checks, `ctest -R WaterSky`): with the live client's `vsLiquidMagma.bls` / `psLiquidMagma.bls` (from `patch.mpq`, stock) both translate as the water family (`WX_KIND 3`), the vertex shader also as its tessellated variant, and every stage links; the thick field with magma's physics keeps a splash's 0.12-yd dent to 0.075 yd after 0.5 s and 0.023 yd after 2 s, within 1.4 yd of where it fell (water's front is at 6 yd), and a minute of bubbling stays bounded (largest 0.09 yd, rms 0.008, stirred in places); magma's embers are still rising after a second and leave no impact, its globs land on the thick field only. `WaterTests --surface <psLiquidMagma.bls> <256 x 256 RGBA> <magma|slime> <out> [seconds] [hdr]` draws a pool with the real translated shader over a rock shore (the HD client's `magma0`, `LAVAGREEN1` and `slime.1`, DXT1, decoded), and `--render ... [seconds] [water|magma|slime]` a splash: looked at, crust and veins, the shore crusting, green fel lava, slime near its stock brightness, cooling globs, rising embers. `Tools/VideoOptionsTests` (553 checks) lays the panel out. The client builds. **Not seen in the world**: how any of it looks there (Molten Core, Undercity's canals, Shadowmoon's fel lava, Naxxramas), the deferred opaque pass's interplay with everything around it, the glow and bubble strengths, the cost. Starting points to tune in game.

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
