# XBE analysis — The Sims: Bustin' Out (USA), XDK 5558 bootstrap

Image: `assets/default.xbe` (3,211,264 bytes, sha1 `6feea04a12fb134253abe2b5662da634d0048232`),
extracted from `Sims, The - Bustin' Out (USA).xiso.iso`. Everything below was read from this
image on Linux with the host tools built from `sdk/cmake/XbeTools.cmake`
(`/tmp/xbetools-build/xbe_symbols`, `/tmp/xbetools-build/xbe_recomp`; no SDK tool source
needed a portability patch) plus `tools/xbe_info.py` and IDA on `out/game.exe`
(`python3 sdk/tools/symbols/xbe2pe.py config/game.toml out/game.exe`, 108 exports, guest
addresses 1:1).

## Header, sections, libraries

- Title `0x45410043` ("EA-67"), region north_america, retail, base `0x00010000`,
  entry `0x00100EA2`, image size 4,072,128, headers 4,096, built 2003-11-13,
  debug name `c:\eor\src2\games\sims\obj\sims_xbox_devsystem_release\Sims.exe`.
- Kernel thunk `0x002E1C00`, 160 imports.
- **XDK build 5558 for every library** — this is why the scanner originally failed:
  it picks `sdk/xdk/<build>/signatures.toml` by D3D8 build and the SDK only shipped 5849.

| library | version | qfe |
|---|---|---|
| XAPILIB | 1.0.5558 | 1 |
| XBOXKRNL | 1.0.5558 | 1 |
| LIBCPMT | 1.0.5558 | 1 |
| LIBCMT | 1.0.5558 | 1 |
| D3D8 | 1.0.5558 | 4 |
| XGRAPHC | 1.0.5558 | 1 |
| DSOUND | 1.0.5558 | 1 |
| XMV | 1.0.5558 | 1 |

| section | vaddr | vsize | raw | raw size | flags |
|---|---|---|---|---|---|
| .text | 0x11000 | 2,305,252 | 0x1000 | 2,305,252 | preload, executable |
| D3D | 0x243D00 | 144,436 | 0x234000 | 129,888 | writable, preload, executable |
| XGRPH | 0x267140 | 148,800 | 0x254000 | 145,604 | writable, preload, executable |
| DSOUND | 0x28B680 | 156,532 | 0x278000 | 155,908 | writable, preload, executable |
| XMV | 0x2B1A00 | 164,556 | 0x29F000 | 164,540 | writable, preload, executable |
| XPP | 0x2D9CE0 | 32,540 | 0x2C8000 | 32,540 | writable, preload, executable |
| .rdata | 0x2E1C00 | 157,580 | 0x2D0000 | 157,564 | preload |
| .data | 0x3083A0 | 900,256 | 0x2F7000 | 31,112 | writable, preload |
| DOLBY | 0x3E4040 | 29,056 | 0x2FF000 | 29,036 | preload |
| $$XTINFO | 0x3EB1C0 | 494 | 0x307000 | 494 | inserted file |
| $$XTIMAGE | 0x3EB3C0 | 18,432 | 0x308000 | 18,432 | inserted file |
| $$XSIMAGE | 0x3EFBC0 | 6,144 | 0x30D000 | 6,144 | inserted file |
| .XTLID | 0x3F13C0 | 3,832 | 0x30F000 | 3,832 | inserted file |

Code sections the scanner sweeps (its guess, same list as the Urbz config):
`.text, D3D, XGRPH, DSOUND, XMV, XPP`. `config/game.toml` header as written by the tool:
title `0x45410043`, entry `0x100EA2`, `xdk = 5558`, `partitions = 64`.

## .XTLID statistics

`.XTLID` holds 479 (export id, address) pairs of 8 bytes, all 479 ids unique, no zero
addresses. By address section: `.text` 189, `DSOUND` 168, `D3D` 64, `XGRPH` 26, `XMV` 17,
`XPP` 14, header 1 (id 0 → 0x15B6). By id range: `0xBxxxx` 168 (DirectSound),
`0x2xxxx` 128, `0x1xxxx` 75 (XAPI/input), `0x8xxxx` 64 (D3D), `0xDxxxx` 26, `0xExxxx` 17.

The 5849 reference set has 240 `[symbols]` names (+ 3 anchors, 12 data = 255 records);
102 symbol records carry an `xtlid`. Of those 102 ids, **65 are exported by this XBE**
(the other 37 are the XNet/XOnline/socket set, ids `0x30001`–`0x4003B`, which this title
does not link — see unresolved below). Export ids are API identities and are stable
across XDK builds, so those 65 names are exact, not guesses.

## Bootstrap method

`tools/sig_bootstrap.py` (stdlib only) parses the XBE and `sdk/xdk/5849/signatures.toml`
and writes `sdk/xdk/5558/signatures.toml`: every reference record with its byte signature
(`len`/`hash`/`calls`/`count`/`nth`/`deep`) removed — 5558 bytes differ from 5849, so a
copied hash would hunt for foreign bytes — keeping only what survives a rebuild:
`xtlid`, `sec`, `k` (constant set), `ret`, `regs`. With no length a record is the
documented no-signature form: the byte matcher skips it and the name stays 0 unless
.XTLID or the constant matcher finds it. Anchors stay `{}`; `[data]` is verbatim.
Then the real scanner runs:

```
xbe_symbols assets/default.xbe sdk/xdk --out config/game.toml
  found 108 of 240 functions, 4 of 12 data
```

(That is the first bootstrap run. `config/game.toml` today holds **194 of 242**
`[symbols]` and **12 of 12** `[data]`, after later runs and additions such as the
D1 entry point below.)

No name was given an address by hand. Names left at 0 are trapping stubs, which is the
correct state for them.

## Resolved: 65 by .XTLID (exact)

`Direct3D_CreateDevice` 0x800C5→0x2443C0, `D3DResource_IsBusy` 0x8006F→0x244B10,
`D3DResource_BlockUntilNotBusy` 0x8006A→0x244BF0, `XLaunchNewImageA` 0x1005A→0x102564,
`XMountUtilityDrive` 0x10049→0x1035AF, `XInputOpen` 0x10036→0x2DAC65,
`XInputClose` 0x10037→0x2DACBB, `XInputGetState` 0x10038→0x2DAE9F,
`XInputSetState` 0x1003A→0x2DAF12, `XAudioDownloadEffectsImage` 0xB00A6→0x28E23D,
`DirectSoundCreate` 0xB0002→0x2915DA, `DirectSoundCreateBuffer` 0xB0003→0x291621,
`DirectSoundCreateStream` 0xB0004→0x291678, `DirectSoundDoWork` 0xB000F→0x28E214,
`DirectSoundUseFullHRTF` 0xB002F→0x28BB69, `DirectSoundUseLightHRTF` 0xB0031→0x28BB88,
and the `IDirectSound*`/`IDirectSoundBuffer*` set: `IDirectSound_Release` 0xB0045→0x28BAE1,
`IDirectSound_CommitDeferredSettings` 0xB0037→0x290CAF,
`IDirectSound_CommitEffectData` 0xB0038→0x28DDC4,
`IDirectSound_CreateSoundBuffer` 0xB003A→0x29128C,
`IDirectSound_DownloadEffectsImage` 0xB003C→0x28DD4C,
`IDirectSound_EnableHeadphones` 0xB003D→0x28DDDC,
`IDirectSound_GetEffectData` 0xB003F→0x28DD73,
`IDirectSound_GetOutputLevels` 0xB0040→0x28DE34,
`IDirectSound_GetSpeakerConfig` 0xB0041→0x28DD30,
`IDirectSound_SetDistanceFactor` 0xB0048→0x2912F4,
`IDirectSound_SetDopplerFactor` 0xB0049→0x291318,
`IDirectSound_SetEffectData` 0xB004A→0x28DD9A,
`IDirectSound_SetI3DL2Listener` 0xB004B→0x291414,
`IDirectSound_SetMixBinHeadroom` 0xB004C→0x28DDF8,
`IDirectSound_SetOrientation` 0xB004D→0x29133C, `IDirectSound_SetPosition` 0xB004E→0x291386,
`IDirectSound_SetRolloffFactor` 0xB004F→0x2913BB, `IDirectSound_SetVelocity` 0xB0050→0x2913DF,
`IDirectSound_SynchPlayback` 0xB0051→0x28DE54,
`IDirectSoundBuffer_GetCurrentPosition` 0xB0053→0x28E09E,
`IDirectSoundBuffer_GetStatus` 0xB0054→0x28E082, `IDirectSoundBuffer_Lock` 0xB0056→0x28E0DA,
`IDirectSoundBuffer_Pause` 0xB0057→0x28E022, `IDirectSoundBuffer_Play` 0xB0059→0x28DF9E,
`IDirectSoundBuffer_PlayEx` 0xB005A→0x28DFC2, `IDirectSoundBuffer_Release` 0xB005D→0x28BB17,
`IDirectSoundBuffer_SetAllParameters` 0xB005F→0x28FDEA,
`IDirectSoundBuffer_SetBufferData` 0xB0060→0x2907AE,
`IDirectSoundBuffer_SetCurrentPosition` 0xB0064→0x28E0BE,
`IDirectSoundBuffer_SetFilter` 0xB0068→0x28DF4A, `IDirectSoundBuffer_SetFormat` 0xB0069→0x290792,
`IDirectSoundBuffer_SetFrequency` 0xB006A→0x28FD96,
`IDirectSoundBuffer_SetHeadroom` 0xB006B→0x28DF66,
`IDirectSoundBuffer_SetI3DL2Source` 0xB006C→0x28FFE5,
`IDirectSoundBuffer_SetLoopRegion` 0xB006E→0x28E062,
`IDirectSoundBuffer_SetMaxDistance` 0xB006F→0x28FE83,
`IDirectSoundBuffer_SetMinDistance` 0xB0070→0x28FEA7,
`IDirectSoundBuffer_SetMixBinVolumes` 0xB0072→0x28DF82,
`IDirectSoundBuffer_SetMixBins` 0xB0071→0x28FDCE,
`IDirectSoundBuffer_SetOutputBuffer` 0xB0075→0x28FDB2,
`IDirectSoundBuffer_SetPlayRegion` 0xB0077→0x290005,
`IDirectSoundBuffer_SetPosition` 0xB0078→0x28FEEB,
`IDirectSoundBuffer_SetRolloffCurve` 0xB0079→0x28FFC1,
`IDirectSoundBuffer_SetVelocity` 0xB007B→0x28FF20,
`IDirectSoundBuffer_SetVolume` 0xB007C→0x28DEDA, `IDirectSoundBuffer_Stop` 0xB007D→0x28DFE6,
`IDirectSoundBuffer_StopEx` 0xB007E→0x28DFFE, `IDirectSoundBuffer_Unlock` 0xB007F→0x28BAF7,
`IDirectSoundStream_SetEG` 0xB008C→0x28E18D.
Data by .XTLID: `D3D_Device` 0x80001→0x264950, `D3D_pDevice` 0x80003→0x264948,
`XPP_GamepadType` 0x1007C→0x2D9D10, `XPP_MemoryUnitType` 0x1007E→0x2D9DC0 (4 of 12 data — that first run; `config/game.toml` now has 12 of 12).

## Resolved: 43 by constants (score in the scanner report)

All in `D3D` except `crt_pow`: `CDevice_KickOff` 0x24D120 (1.00),
`D3DDevice_Clear` 0x248570 (1.00), `D3DDevice_CopyRects` 0x2467D0 (1.00),
`D3DDevice_CreateVertexShader` 0x24B8B0 (1.00), `D3DDevice_DrawIndexedVertices` 0x24DD30 (1.00),
`D3DDevice_DrawVertices` 0x24DC90 (1.00), `D3DDevice_EnableOverlay` 0x259700 (1.00),
`D3DDevice_GetVertexShaderFunction` 0x24C870 (1.00), `D3DDevice_LightEnable` 0x247030 (1.00),
`D3DDevice_PersistDisplay` 0x247FC0 (1.00), `D3DDevice_Release` 0x245370 (1.00),
`D3DDevice_SelectVertexShader` 0x24C090 (1.00), `D3DDevice_SetGammaRamp` 0x244EC0 (1.00),
`D3DDevice_SetIndices` 0x247430 (1.00), `D3DDevice_SetLight` 0x246D70 (1.00),
`D3DDevice_SetPixelShader` 0x24E740 (1.00), `D3DDevice_SetPixelShaderConstant` 0x24E940 (1.00),
`D3DDevice_SetRenderState_CullMode` 0x249EF0 (1.00),
`D3DDevice_SetRenderState_Dxt1NoiseEnable` 0x24A0B0 (1.00),
`D3DDevice_SetRenderState_FogColor` 0x249EA0 (1.00),
`D3DDevice_SetRenderState_LineWidth` 0x24A040 (1.00),
`D3DDevice_SetRenderState_StencilEnable` 0x24AF50 (0.80),
`D3DDevice_SetRenderState_StencilFail` 0x24AFE0 (1.00),
`D3DDevice_SetRenderState_TwoSidedLighting` 0x24A2C0 (1.00),
`D3DDevice_SetRenderState_ZEnable` 0x24AEC0 (1.00), `D3DDevice_SetRenderTarget` 0x246040 (1.00),
`D3DDevice_SetScissors` 0x247C40 (1.00), `D3DDevice_SetShaderConstantMode` 0x24C180 (0.96),
`D3DDevice_SetStreamSource` 0x24BE90 (1.00), `D3DDevice_SetTexture` 0x2471C0 (1.00),
`D3DDevice_SetTextureState_TexCoordIndex` 0x24A390 (1.00), `D3DDevice_SetTransform` 0x244FE0 (1.00),
`D3DDevice_SetVertexShader` 0x24C3F0 (1.00),
`D3DDevice_SetVertexShaderConstantNotInlineFast` 0x24BB70 (0.86),
`D3DDevice_SetViewport` 0x246B40 (1.00), `D3DDevice_Swap` 0x2495F0 (1.00),
`D3D_BlockOnTime` 0x24D300 (1.00), `D3D_InitDefaultState` 0x24F940 (1.00),
`D3D_InternalAddRef` 0x2449B0 (1.00), `D3D_InternalRelease` 0x244DE0 (1.00),
`D3D_MakeRequestedSpace` 0x24D470 (1.00), `D3D_SetFence` 0x24D240 (1.00),
`crt_pow` 0x220A24 (1.00). Nothing was resolved by bytes (the seed has no byte
signatures) and no convention (`[abi]`) differences were reported.

`sdk/xdk/5558/layout.toml` was re-proved from these functions in IDA: every D3D8 and
DSound offset matched its 5849 value at a cited 5558 instruction (device = 0x264950),
e.g. Textures `0xF88` at 0x2471D0, Palettes `0xF98` at 0x2473AD, SwapCount `0x2478` at
0x249650, GammaRamps `0x1E04`/GammaPending `0x2404` in SetGammaRamp 0x244EC0. Only the
three `XPP_*` fields could not be isolated in guest code and are flagged UNPROVEN there.

## Still at 0 (132 functions, 8 data) — with the best lead for each

Addresses below are leads proven while proving the layout, **not** config entries;
`config/game.toml` keeps them 0 until the scanner (or a `--sign` pass) names them.

- **Proven by hand in IDA, awaiting a signing pass:** `D3DDevice_SetPalette` → 0x2473A0
  (stores `[device + stage*4 + 0xF98]`, pushes `(stage<<6)+0x41B20`);
  `D3DDevice_GetBackBuffer2` → 0x2463E0 (index −1 → `[device+0x1A18]`, 0 → `[device+0x1A14]`);
  `D3DDevice_GetDepthStencilSurface2` → 0x246B20 (returns `[device+0x1A08]`);
  `D3DDevice_BlockUntilVerticalBlank` → 0x245490 (0x24 bytes, waits on device+0x1DBC);
  `D3DDevice_SetScreenSpaceOffset` → 0x2480F0 (stores at device+0xEF8/0xEFC);
  `D3DDevice_SetTextureStageStateNotInline` → 0x24A5A0 (`table[stage*32+type]`).
  Next step for all six: add them as known starts and run
  `xbe_symbols --sign` so 5558 gets real byte signatures.
- **D3D render-state / shader helpers (about 30, e.g. `D3DDevice_Begin`, `D3DDevice_End`,
  `D3DDevice_MakeSpace`, `D3DDevice_SetRenderState_*`, `D3DDevice_SetVertexShaderConstant*`,
  `D3DDevice_SetFlickerFilter`, `D3DDevice_SetSoftDisplayFilter`, `D3DDevice_CreatePixelShader`,
  `D3DDevice_DeletePixelShader`, `D3DDevice_DeleteVertexShader`, `D3DDevice_LoadVertexShader`,
  `D3DDevice_DrawVerticesUP`, `D3DDevice_BlockOnFence`, `D3DDevice_InsertFence`,
  `D3DDevice_IsFencePending`, `D3DDevice_FlushVertexCache`, `D3DDevice_SetMaterial`,
  `D3DDevice_SetTile`, `D3DDevice_SwitchTexture`, `D3DDevice_GetRenderTarget2`,
  `D3DDevice_SetRenderStateNotInline`, `D3DDevice_SetRenderState_Simple`,
  `D3DDevice_SetTextureState_BorderColor/BumpEnv/ColorKeyColor`, `Lock2DSurface`,
  `D3D_BlockOnResource`, `D3D_KickOffAndWaitForIdle`):** their 5849 constant sets have
  fewer than 3 constants (single push-buffer method words like `0x8038C`), so the
  constant matcher correctly refuses them. Lead: they are the small `MakeSpace`
  wrappers clustered around the found `D3DDevice_SetRenderState_CullMode` 0x249EF0 /
  `FogColor` 0x249EA0 / `LineWidth` 0x24A040 and the callers in
  `SetRenderStateNotInline` 0x24A5A0's dispatch; identify each by the method constant
  it pushes, then sign. `D3D_BlockOnResource` has no constants at all; lead: first
  callee of the .XTLID-named `D3DResource_BlockUntilNotBusy` 0x244BF0.
- **`CDirectSoundStream_*` (21 methods):** each carries only `k = [0x80004005]`, below
  the 3-constant floor, and streams have no .XTLID entries. Lead: the stream vtables
  proven in the layout (constructor 0x29048D stores them at +0/+4; vtable addresses
  0x2F9038 and 0x2F9028) — walk the vtable slots in slot order, or sign from the
  `IDirectSoundBuffer_*` thunk cluster at 0x28DEDA–0x290005.
- **CRT (`_CIacos`, `_CIasin`, `crt_acos/asin/atan/cos/log/log10/sin/sqrt/tan`):**
  LIBCMT in `.text`, constant sets too small (mostly float masks). Lead: `crt_pow`
  was found at 0x220A24; the rest sit in the same link cluster in link order.
- **Network (`accept`, `bind`, `closesocket`, `connect`, `getsockname`, `getsockopt`,
  `ioctlsocket`, `listen`, `recv`, `recvfrom`, `select`, `send`, `sendto`, `setsockopt`,
  `shutdown`, `socket`, `WSA*`, `XNet*`, `XOnline*` — 37 names):** genuinely absent.
  Their .XTLID ids (`0x30001`–`0x4003B`) are not exported by this XBE and there is no
  XNET/XONLINE section; the title does not link them. 0 is the correct value, not a
  gap — the host traps only if called, and no guest code calls them. No lead needed.
- **Bink (`BinkDoFrame/GetSummary/NextFrame/Wait`):** no BINK section in this image
  (video is XMV here, as the section list shows). Same disposition as network: absent,
  leave 0.
- **`XInitDevices`:** XPP, 4 constants but no start scored above the accept/lead
  thresholds. Lead: the XPP descriptor table at 0x2D9CE4 and the device-type lookup
  0x2DAFD5 beside `XInputOpen` 0x2DAC65.
- **Data at 0 (8):** `D3D_DeferredTextureState` (lead: the stage table at 0x2644B0 in
  0x24A5A0), `D3D_DirtyFlags` (lead: 0x2644A8, or'd with 0x4800 at 0x247247),
  `D3D_Streams` (lead: 0x266DF0 in SetStreamSource 0x24BE90),
  `DSound_StreamVtable`/`DSound_StreamVtable2` (leads: 0x2F9038 / 0x2F9028 from the
  constructor 0x29048D), `D3D_IndexData`, `D3D_RenderState`,
  `D3D_SimpleRenderStateEncode` (no direct lead yet; all three resolve only once
  their referencing function is byte-signed, per the scanner's own guard — the four
  "not read from …, which the bytes did not find" lines in its report).

## Files in this bootstrap

- `sdk/cmake/XbeTools.cmake` — host-tools-only CMake (Zydis + toml++ only); builds
  clean on Linux with clang/ninja, no tool-source patches were needed.
- `tools/sig_bootstrap.py` — the seeder described above (rerunnable; deterministic).
- `sdk/xdk/5558/signatures.toml` — seeded set consumed by the scanner (240 symbols).
- `sdk/xdk/5558/layout.toml` — field offsets, each with its 5558 proof address;
  `XPP_Current/Changed/Previous` flagged UNPROVEN.
- `config/game.toml` — scanner output, unedited at bootstrap time: the run above
  found 108 of 240 functions, 4 of 12 data; the file now holds 194 of 242
  `[symbols]` and 12 of 12 `[data]` after later runs and hand additions
  (e.g. the D1 entry point).
