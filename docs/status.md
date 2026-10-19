# Bring-up status

Recorded 2026-10-19. Every number below was observed on this machine or read from a file in this
repository; commands are reproducible from the repository root. Where a claim has not been exercised
in play it is marked **not proven** — see the last section, which is the authoritative list.

## Where the port stands

| Stage | Result |
| --- | --- |
| Disc image read | 60 files, 2,104,138,128 bytes of file data, partition base `0x00000000` (`docs/iso-extract.md`, `python3 tools/xiso_extract.py info "Sims, The - Bustin' Out (USA).xiso.iso"`); the image itself is 2,104,754,176 bytes, sha1 `5f5b2a668e4ab766779abf5d3961fcae095ab54b` |
| XBE identified | `assets/default.xbe`, 3,211,264 bytes, sha1 `6feea04a12fb134253abe2b5662da634d0048232`, title `0x45410043` (EA-67), retail, base `0x00010000`, entry `0x00100EA2`, image size 4,072,128, built 2003-11-13, **XDK build 5558** for every library (`docs/xbe-analysis.md`, `docs/iso-extract.md`) |
| XDK signature set | SDK shipped only `sdk/xdk/5849`; `sdk/xdk/5558/` was bootstrapped for this title (see below) |
| `config/game.toml` | 194 of 242 `[symbols]` names at a non-zero address and 48 at `0`; 12 of 12 `[data]` symbols named (counted from the file itself; see "Gap analysis") |
| Kernel imports | 160 xboxkrnl ordinals imported; 160 covered, 0 uncovered, after this port added six exports (`docs/kernel-coverage.md`) |
| Recompilation | 27,495 functions, 895,135 instructions (`generated/game/x86_recomp_stats.txt`) |
| Native Linux build | Builds and links; verbose binary `out/build/linux-verbose/SimsBustinOut` (112,107,496 bytes in the recorded run, log line 3 of `out/build/linux-verbose/logs/simsbo_001.log`) |
| Launch (Linux) | Boots, loads the game, opens its window, presents swaps, plays DirectSound audio and opens the intro XMV (`out/build/linux-verbose/logs/simsbo_001.log`) |
| Picture on screen | **Not proven.** The owner reports "i dont see the movie playing, just colors flashing" — see "The picture" below |
| Gameplay | **Not proven.** No menu, gameplay, save or reload has been observed |

## The XDK 5558 bootstrap

The SDK shipped signatures only for XDK 5849; this title is XDK 5558 for every library
(`docs/xbe-analysis.md` §"Header, sections, libraries"), so the scanner originally had no set to
pick. `tools/sig_bootstrap.py` seeded `sdk/xdk/5558/signatures.toml` from the 5849 set by stripping
everything that does not survive a rebuild — byte signatures (`len`/`hash`/`calls`/`count`/`nth`/
`deep`) — and keeping only `xtlid`, `sec`, `k` (constant set), `ret` and `regs`. Export ids are API
identities and are stable across XDK builds: of the 102 `xtlid`-carrying records in the 5849 set,
65 are exported by this XBE's `.XTLID` section (479 unique (id, address) pairs), and those 65 names
are exact. The first scanner run then reported:

```
xbe_symbols assets/default.xbe sdk/xdk --out config/game.toml
  found 108 of 240 functions, 4 of 12 data
```

(65 by `.XTLID`, 43 by constants, none by bytes.) Neighbour-order alignment against the sibling
port and later signing passes (`config/found-d3d8.toml`, `config/found-dsound.toml`,
`config/found-crt-xapi.toml`, `config/found-data.toml`, and `docs/d3d-gap-functions.md`) raised
`config/game.toml` to its current state: **194 of 242 `[symbols]` named, 48 at `0`, and 12 of 12
`[data]` named** (recounted from the file). Of the 48 at `0`, 41 are Bink/socket/WSA/XNet/XOnline
names this XBE has no section for, and 7 are `crt_*` math entries — the full grouped census is in
`docs/gap-analysis.md`.

## Kernel import coverage

`default.xbe` imports 160 xboxkrnl ordinals (kernel thunk `0x002E1C00`); the SDK runtime registers
275 exports and all 160 are covered, 0 uncovered (`docs/kernel-coverage.md`,
`tools/kernel_coverage.py`). Six of those implementations were added for this port:
`IoQueryVolumeInformation` (ordinal 76, `sdk/runtime/kernel/files.cpp`),
`ObOpenObjectByName` (243, same file), and `NtCreateIoCompletion` (191), `NtSetIoCompletion`
(227), `NtRemoveIoCompletion` (223) and `NtUserIoApcDispatcher` (232) in
`sdk/runtime/kernel/iocp.cpp`. The verbose boot log agrees: `boot: kernel imports resolved,
0 missing`.

## What had to be written for Linux

The SDK was Windows-only (Direct3D 12 + XAudio2). The Linux backend is new code in `sdk/`; the
Windows path is untouched.

- **Vulkan renderer**: `sdk/runtime/gpu/vulkan.cpp` (1,783 lines) + `vulkan.h` implementing
  `gpu::Backend` from `sdk/runtime/gpu/backend.h`, mirroring `sdk/runtime/gpu/d3d12.cpp`
  (4,057 lines). Shaders go HLSL → SPIR-V via DXC (`libdxcompiler.so` from `/opt/dxc`) with a
  glslang fallback (`sdk/runtime/gpu/spirv.cpp`, binding shifts `-fvk-b-shift 0 -fvk-t-shift 2
  -fvk-s-shift 6 -fvk-u-shift 10`), cached under `shadercache/`.
- **SDL3 app layer and audio**: `sdk/runtime/app/main_sdl.cpp` (window, event loop, ImGui
  SDL3+Vulkan backends) and `sdk/runtime/audio/sdl_audio.cpp`, against the Windows XAudio2 path.
- **POSIX handover**: `sdk/runtime/app/handover.cpp` maps the named-event handover onto an
  abstract-namespace `AF_UNIX` socket.
- **PNG splash**: `sdk/runtime/app/image_decode.cpp` is a zlib-inflate PNG decoder, because SDL3
  alone loads BMP but not PNG and SDL3_image is unavailable; Windows uses WIC.
- **Winsock-on-POSIX**: the socket mapping was promoted out of the test harness into the runtime
  (`sdk/runtime/xdk/xnet.cpp`).
- **Portability fixes**: `DXGI_FORMAT` available off Windows (`sdk/runtime/gpu/dxgi_format.h`),
  and `pthread_setname_np` thread naming (`sdk/runtime/gpu/render_thread.cpp`,
  `sdk/runtime/kernel/threads.cpp`).
- **ImGui pin**: bumped from v1.91.0 to v1.91.9b in `sdk/cmake/XbeRecomp.cmake`, with the measured
  reason recorded in that file: on this box, 1.91.0's `imgui_impl_sdl3.cpp` gives 2 errors and
  1.91.9b gives 0.

Deliberate Vulkan differences from the D3D12 reference (recorded by its author): no game render
extension runs on Vulkan (`gpu/ext.h` is D3D12-typed; this title uses none); vertex strides are
read from the assembling draw because they are not in the `PsoKey`, so two draws sharing a key
with different strides share the first pipeline; partial `kClearTarget` channel masks execute as
full clears (the `0xF0` mask the titles pass is exact); depth bias `zbias / 2²⁴`; border colour
reduced to transparent-black/opaque-white; no HDR path (SDR `B8G8R8` present); F3 capture reduced
to `frame.txt` + BMP target dumps; and the per-frame perf reporting of `d3d12.cpp` was not
reproduced, so `XBE_PERF=1` prints no draw/frame/present counts on Linux.

## The boot sequence actually observed on Linux

From `out/build/linux-verbose/logs/simsbo_001.log` (RelWithDebInfo without `NDEBUG`, `XBE_PERF=1`),
verbatim:

```
[ 0.101] input: SDL 3.4.16
[ 0.101] memory: guest space at 0x100000000
[ 0.101] boot: title 45410043, entry 00100EA2, 13 sections
[ 0.106] dispatch: 27495 functions over 00011000..002E1BFC
[ 0.106] boot: kernel imports resolved, 0 missing
[ 0.131] d3d: CreateDevice 640x480 format 1E buffers 1 depth 1 format 2E interval 2 flags 130
[ 0.181] Vulkan adapter: AMD BC-250 (RADV GFX1013) (vendor 1002 device 13fe, driver 109060100)
[ 0.182] vulkan: 640x480 guest, 3440x1440 window, render scale 3.000, HLSL->SPIR-V by DXC (libdxcompiler) with glslang fallback
[ 0.182] d3d: Swap 1
[ 0.187] imgui: Vulkan backend ready (3 images)
[ 0.199] d3d: Swap 2  …  [0.367] d3d: Swap 8   (Swap is LOG_FIRST(8); swaps continue unlogged)
[ 0.278] d3d: Reset 640x480 format 1E buffers 1 multisample 2222 depth 1 format 2E flags 130 refresh 0 interval 1
[ 3.511] d3d: BlockOnResource on surface 8051FC00, stack 00250FD6 00244630 00174BA3 001744BE
[ 3.526] d3d: Reset 640x480 format 1E buffers 1 multisample 2222 depth 1 format 2E flags 130 refresh 0 interval 2
[ 3.528] d3d: vertex shader B0106281, 9 decl tokens, 11 instructions, type 2078
[ 3.569] render thread: the title waited 25.92 ms for a fence
[ 4.160] d3d: MakeSpace from 00170CBD, some caller still pushes hardware commands   (repeats ~every 0.63 s; callers 0016F20C / 0016F815 / 00170B0E / 00170CBD)
[ 4.527] open D:\MODELS.ARC      [ 4.527] open D:\RLETEXTU.ARC
[10.545] open d:\sims_intro.xmv
[10.547] dsound: stream #2 obj=B0106650 tag=1 ch=2 rate=48000 bits=16 align=4
[10.609] d3d: Reset 640x480 format 1E buffers 1 multisample 2222 depth 1 format 2E flags 130 refresh 0 interval 1
[45.220] dsound: mix renders=4500 voices=49 sounding=1 … peak=0.4922
[45.220] audio: sdl submitted=4500 rendered=4500 requested=4608 peak=0.4922
[49.995] Exit (0)
```

So the guest runs, the disc reads, shaders translate, the device resets (three times in this log),
the XMV intro movie opens with a 48 kHz DirectSound stream, and audio is actually submitted to
SDL (4,500 buffers, peak 0.4922 — not silence).

## Defects found and fixed during bring-up

1. **Missing indirect-call entry point `0x2283CD`.** `config/game.toml` carries it as an extra
   entry point under `[functions]` (`"0x2283CD" = ""`); without it that guest code was unreachable
   to the dispatcher.
2. **Boot crash: null NV2A miniport via an unnamed `D3DDevice_Reset`** (`docs/boot-crash.md`).
   Signal 11 at 0.226 s: the chain title → guest `0x245F90` → mode helper `0x24FB40` → SetTile
   body `0x247940` → miniport `0x2530DA` → poll `0x252D36` read `[NULL + 0x3214]`, with
   `ecx = edi = 0x266578 = D3D_Device (0x264950) + 0x1C28`, the miniport object the host never
   builds. `0x245F90` was identified as `D3DDevice_Reset` (its body is quoted instruction by
   instruction in `docs/boot-crash.md`) and named in `config/game.toml`, handing it to the
   already-implemented host body. Post-fix, `out/bringup/run_1.log` runs the full 40 s to
   `Exit (0)` with no `error:` lines. Two sibling-port checks were confirmed already correct in
   the same pass: `VulkanBackend::SetGamma` treats a fully zero (all-black) ramp as identity, and
   `D3DDevice_InsertCallback = 0x24D030` is named with `src/game/hooks.cpp` running the callback
   immediately.
3. **"Dirty disc" from an incomplete `assets/` folder** (SDK fix, `docs/iso-extract.md`). A folder
   holding only `default.xbe` won over a complete disc image in `app::FindGame`, so the title
   mounted a folder with no data files and drew its dirty-disc error screen. The rule now, in
   `sdk/runtime/app/game_files.cpp`: a complete extracted folder (the XBE plus at least one other
   game data file) still wins; an incomplete folder loses to a disc image carrying the right XBE;
   if an incomplete folder is all there is it is used, but loudly (the exact `game: …` log lines
   are quoted in `docs/iso-extract.md`).

## The picture

The swap chain is presented every frame and audio plays, but **what reaches the screen has not
been proven to be the game's picture**. The machine's owner reports: "i dont see the movie
playing, just colors flashing". A full-screen screenshot taken while it ran shows the desktop with
the game window not compositing anything recognisable. This is being worked on; nothing in this
document claims otherwise. Following the sibling port's lesson ("screen-scraping lies… every
present clears the back buffer to black"), the useful signal will be the captured guest render
target (`XBE_CAPTURE_DIR` + F3) compared against the window, not a desktop screenshot. The Vulkan
log also cannot yet say whether draws reach the target, because the per-frame perf reporting was
not reproduced on Linux (see above).

## What is proven / what is not exercised

**Proven** (observed in the files cited): the disc, XBE and XDK 5558 identity; the 5558 signature
bootstrap and the current `config/game.toml` counts; 160/160 kernel imports; recompilation
(27,495 functions); the Linux build; the boot sequence, swaps, device resets, disc reads, XMV
open and SDL audio quoted above; the two bring-up fixes and the dirty-disc fix.

**Not proven:**

- **The picture on screen** — the owner reports flashing colours, not the movie (above).
- **The frame-step fix at high frame rates** — the 1 ms-tick quantiser and frame-tick wrappers in
  `src/game/hooks.cpp` are static analysis only (`docs/frame-rate.md`: "None of this has been
  verified in play"); game-seconds vs wall-seconds has not been measured at any cap.
- **The cheats** — the eight pad-code rows and their effects are read from the image
  (`docs/cheats.md`) and cross-checked against public lists, but no code has been entered in play.
- **The keyboard/pad bindings** — the mapping in `README.md` is the host layer's; it has not been
  exercised in this game.
- **The widescreen crop** — `picture_top = 16`, `picture_height = 448` in
  `src/common/game_info.cpp` rest on the static evidence in `docs/picture-rect.md`; only an
  on-screen run can settle centred vs top-aligned.
- **The Windows target** — Direct3D 12 + XAudio2; this Linux box has no MSVC or Windows SDK, so
  it cannot be compiled here (`docs/toolchain.md`).
- Menus, gameplay, saving and reloading — never reached observably.

## Commands

```
python3 tools/xiso_extract.py info "Sims, The - Bustin' Out (USA).xiso.iso"
python3 tools/xbe_info.py assets/default.xbe --sdk sdk
cmake --build --preset linux-release -j $(nproc)        # out/build/linux-release/SimsBustinOut
cmake --build out/build/linux-verbose -j $(nproc)       # verbose (logs survive)
XBE_PERF=1 timeout 40 ./out/build/linux-verbose/SimsBustinOut
```
