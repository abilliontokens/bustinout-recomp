# The Sims: Bustin' Out - Static Recompilation

An unofficial native PC port of the original Xbox release of *The Sims: Bustin' Out* (USA),
produced by statically recompiling the game's x86 XBE with the
[XboxXBERecomp SDK](https://github.com/abilliontokens/XboxXBERecomp) fork (vendored in `sdk/` as a submodule at SDK commit `c3d4335`).
Windows uses Direct3D 12 and XAudio2; Linux is a native Vulkan and SDL3 build — no Wine.

**Status: playable on Linux.** The native Linux build boots, plays the intro movie whole and
correctly proportioned with SDL3 audio, and drives title → main menu → save select → name
entry → Create-a-Sim → save → a loaded, playable household at 2176 draws/frame with 0 skipped
and 0 dropped draws on the final check of the committed build (`docs/black-scene.md`;
`docs/status.md`, `docs/playtest.md`). Game speed is measured at 1.000 game-seconds per real
second at 60, 144 and uncapped fps. The Vulkan allocation-ordering defect in
`UploadAlloc`/`AllocDrawSet` is fixed and proven (`stalealloc 0` over a full boot → household
run, SDK `e809cd4`; `docs/black-scene.md`).

`aspect_ratio = "auto"` presents the aspect the title is actually projecting — 16:9 on any
output 3:2 or wider, because the host answers the console's widescreen flag there, and 4:3 on
a narrower one — so a 16:9 display fills edge to edge with no black bars. A display wider
than 16:9 still gets that 16:9 picture pillarboxed: true Hor+ widening is written but gated
off behind `SIMSBO_HORPLUS` and is **not** safe to enable yet (`docs/ultrawide.md` lists
exactly what is still wrong with it).

What is still untested: no controller has ever been plugged into the test machine (the gamepad
path is unexercised); saving and reloading through visible play is unproven; and the remaining
cheat rows and the per-call frame-rate consumers in `docs/frame-rate.md` are unproven in play.
Full, evidence-cited state: `docs/status.md`.

This repository contains **no game code and no game assets**. It holds tooling, configuration and
host-side glue only. You must supply your own legally acquired copy of the disc.

## The game

| Field | Value |
| --- | --- |
| Title ID | `0x45410043` (EA-67) |
| Region | North America |
| XBE | `default.xbe`, 3,211,264 bytes, SHA-1 `6feea04a12fb134253abe2b5662da634d0048232` |
| Built | 2003-11-13 |
| XDK | 5558 (XAPILIB, XBOXKRNL, LIBCPMT, LIBCMT, D3D8, XGRAPHC, DSOUND, XMV) |
| Kernel imports | 160 |

The build is pinned to this exact XBE. A different region or revision needs its own analysis pass.

## Status

| Stage | State |
| --- | --- |
| Disc image read and extracted (`tools/xiso_extract.py`) | done — the XBE is in `assets/` |
| XBE identified and analysed (`tools/xbe_info.py`) | done — see the table above |
| XDK build matched against the SDK's signature set | done — `sdk/xdk/5558/` bootstrapped from 5849 (see `docs/status.md`) |
| Repo scaffolded (this repository, Windows + Linux build files) | done |
| `config/game.toml` written by `xbe_symbols` | done — 194 of 242 `[symbols]` named, 48 at `0`, 12 of 12 `[data]` (recounted from the file; the 48 are accounted for in `docs/gap-analysis.md`) |
| Recompilation and native build | done on Linux — 27,492 functions (`generated/game/x86_recomp_stats.txt`); Windows never compiled |
| Kernel import coverage (`tools/kernel_coverage.py`) | done — 160 of 160 (`docs/kernel-coverage.md`) |
| Boot, rendering, audio, saves | boot, intro, menus, Create-a-Sim, audio and a visible playable household proven on Linux (`docs/status.md`, `docs/playtest.md`, `out/shots/house3_house.png`); save/reload through visible play **not proven** |

## Controls

The game itself only ever reads an Xbox pad, so the host translates keyboard and mouse into one
on port 0, and controllers are picked up in the order they are plugged in. This is the host
layer's mapping as `src/common/input_kbm.cpp` implements it. In play, arrows, Space, Esc and
Enter are screenshot-proven (`docs/playtest.md`); **no controller has ever been plugged into
the test machine, so the gamepad path is unexercised.**

| Keyboard / mouse | Pad | In game |
| --- | --- | --- |
| `W` `A` `S` `D` | left stick | walk; menus that read the stick |
| arrow keys | D-pad | menu and dialog navigation |
| mouse, right button held | right stick | camera |
| `Q` / `E` | right stick left/right | rotate the camera |
| `R` / `F` | right stick up/down | zoom the camera in / out |
| mouse wheel | right stick up/down, in pulses | zoom the camera in / out |
| space | A | accept, interact |
| escape, backspace | B | cancel, back |
| `X` / `Y` | X / Y | |
| `Z` / `C` | left / right trigger | |
| `1` / `2` | black / white | |
| enter | Start | "press Start", pause menu |
| tab | Back | |
| shift / ctrl, middle mouse | left / right stick click | |

Keys are read only while the game window has focus. While the F1 settings panel is up it owns
the keyboard and the mouse and the game sees neither.

## Settings, F1 / F2 overlay

**F1** opens the host settings panel (display mode, resolution, aspect ratio, ultrawide,
vsync, frame cap, render scale, anisotropic filtering, HDR on Windows, and the **Cheats**
section) and **F2** the frame-time graph. In the F1 panel the **Resolution** control is
greyed out in borderless mode — borderless always follows the display it is on, so no
chosen resolution can apply there; it is editable in windowed and fullscreen, and the
stored value survives a mode switch (`src/common/overlay.cpp`, superproject `4fae9fc`).
**F3** captures a frame when `XBE_CAPTURE_DIR` is
set (see below). Settings are kept in `settings.toml` beside the executable
(`src/common/game_info.cpp`: `.settings = "settings.toml"`); the first run also creates
`logs/` and `hdd/` (where saves go) beside it. Keys read by
`sdk/runtime/app/settings.cpp` (defaults from `sdk/runtime/app/app.h`):

| Key | Values / default |
| --- | --- |
| `display_mode` | `fullscreen` / `borderless` / `windowed` (default `borderless`) |
| `width`, `height` | output size in pixels; `0` = desktop mode (default) |
| `render_percent` | render scale as a percent of the output height (default `100`) |
| `fps_cap` | frame-rate cap; `0` = unlimited (default `60`) |
| `vsync` | `true` / `false` (default `true`) |
| `anisotropic_filtering` | `0` = as the game asks (default) |
| `aspect_ratio` | `auto` (default), `4:3`, `16:9`, `16:10`, `21:9`, `32:9` — on Vulkan this sets only the aspect the finished picture is presented at (`PresentSrcAspect()` in `sdk/runtime/gpu/vulkan.cpp`); `auto` presents at the title's own 4:3 |
| `ultrawide` | `true` (default) / `false` — on Linux/Vulkan this has **no visible effect today**: the factor is computed in `sdk/runtime/app/settings.cpp` and read by the Vulkan backend for logging only, the frame target is never widened, and the picture stays pillarboxed at the presented aspect (see `docs/ultrawide.md`). The widening path exists only in the never-compiled D3D12 backend |
| `hdr`, `hdr_paper_white`, `hdr_peak` | Windows/D3D12 only: `off` (default) / `auto` / `on`; paper white default `200`, peak `0` = display maximum |
| `join_ip` | system-link co-op host address (empty by default) |

The F1 panel's **Cheats** section enters the game's own pad codes the game's own way
(`docs/cheats.md`); Give Money has been entered in play through the game's own setter
(log: `cheats: entered Give Money (code 1): on, the game's cheat mask is now 02`,
`out/shots/ui2.log`). The other rows are not yet exercised in play.

## Diagnostic environment variables

| Variable | Effect |
| --- | --- |
| `XBE_PERF=1` | per-second `perf:` log lines (fps, draws, upload, clears) mirroring the D3D12 backend (`sdk/runtime/core/diag.h`) |
| `XBE_VK_DEBUG=1` | Vulkan validation layer (`VK_LAYER_KHRONOS_validation`) + debug-utils messenger (`sdk/runtime/gpu/vulkan.cpp`) — **⚠ WARNING: do not use on this machine. It hard-froze the whole machine twice (journal ends 06:47:47 in run `fix1` and 12:50:29 in run `house5`), each less than 0.5 s after a periodic frame capture, with no kernel message (the boot command line has `nowatchdog`). No run without the validation layer has ever frozen. Use logging and the capture dumps (`XBE_PERF`, `XBE_CAPTURE_DIR`, `XBE_VK_CAPTURE_EVERY`, `XBE_VK_DEBUG_DRAW`) instead.** |
| `XBE_CAPTURE_DIR=<dir>` | where F3 frame captures and periodic front-buffer dumps are written (`sdk/runtime/core/diag.h`) |
| `XBE_VK_CAPTURE_EVERY=<n>` | dump the guest front buffer every *n* presents into `XBE_CAPTURE_DIR` (`sdk/runtime/gpu/vulkan.cpp`) |
| `XBE_MUTE=1` | silence SDL audio output for capture/benchmark runs (`sdk/runtime/audio/sdl_audio.cpp`) |
| `SIMSBO_NO_STEP=1` | disable the frame-step fix in `src/game/hooks.cpp`: the stock 1/60 s quantiser runs untouched, so game speed is tied to frame rate as on the console at high fps |
| `XBE_VK_DEBUG_DRAW=1` | full per-draw Vulkan dump (`sdk/runtime/gpu/vulkan.cpp`) |
| `XBE_CAPTURE_PROBE=<x>,<y>` | F3 capture probe point, as fractions of the presented picture's width and height: the capture lists clears/draws that changed that pixel (`sdk/runtime/core/diag.h`) |
| `XBE_NO_UVRECT=1` | disable the UV-rect clamp for 2-D sprite quads, for bisecting a texture seam (both backends: `sdk/runtime/gpu/vulkan.cpp`, `sdk/runtime/gpu/d3d12.cpp`) |
| `XBE_STATE_VERIFY=1` | render-thread state tracking compares every state piece each frame and logs changes no dirty mark covered (`sdk/runtime/gpu/render_thread.cpp`) |
| `XBE_NO_STATE_DIRTY=1` | render-thread state tracking compares every state piece each frame, as before the dirty marks (`sdk/runtime/gpu/render_thread.cpp`) |
| `SIMSBO_AUTOSTART=1` | scripted pad self-test on port 0 (blind Start/Up/A cycle; `src/common/game_info.cpp`) — it never finishes name entry, so it cannot reach a household unattended |
| `SIMSBO_STEP=<file>` | step mode: each token written to the file is one burst of pad-0 pulses (`src/common/game_info.cpp`) |

Unset is the normal path. Presence alone switches most of these on (any value, including
`"0"`); the exceptions are `XBE_MUTE` and the `SIMSBO_NO_*` switch in `src/game/hooks.cpp`,
which treat `"0"` as off (`sdk/runtime/audio/sdl_audio.cpp`, `src/game/hooks.cpp`).

## Layout

```
assets/        your disc image and/or the files extracted from it (never committed)
cmake/         windows-cross.cmake - clang-cl + xwin toolchain for building the Windows exe on Linux
config/        game.toml - the recompiler config written by xbe_symbols, then hand-corrected
docs/          analysis and bring-up notes
sdk/           XboxXBERecomp SDK checkout (recompiler, runtime, XDK HLE, renderers)
src/common/    host-side code shared by every game on this engine family
src/game/      hooks and overrides for recompiled functions (excluded from the host glob)
tools/         xiso_extract.py (XDVDFS reader), xbe_info.py (XBE reader),
               kernel_coverage.py (kernel import coverage), env.bat (toolchain PATH)
generated/     recompiler output (never committed)
```

## Build it from source

You supply the game: the USA Xbox release, in one of two supported shapes:

- the whole disc image (`.iso`) on its own in `assets/`, or
- the fully extracted files in `assets/` (`default.xbe` plus every other file from the disc).

> **Warning:** a folder holding only `default.xbe` is the one shape that does **not** work.
> The build needs `assets/default.xbe` there (to hash and recompile it) while the game needs
> its data files, so that folder alone leaves the title with nothing to load and it will
> report a dirty or damaged disc. Either extract the full image into `assets/` or leave the
> `.iso` itself in `assets/`.

1. Put your disc image in the repository root, or `default.xbe` plus the files extracted from
   it in `assets/`.
2. Extract it:
   ```
   python tools/xiso_extract.py info    "<your>.iso"
   python tools/xiso_extract.py extract "<your>.iso" assets
   ```
3. Check that the XBE is the one this configuration targets:
   ```
   python tools/xbe_info.py assets/default.xbe
   ```
   The SHA-1 must match the table above.
4. Build for your platform:

   **Windows** (Direct3D 12 / XAudio2) — **never run on Windows.** The exe has only been
   cross-compiled on Linux (`windows-cross` preset; commands in `docs/toolchain.md`) and run
   under Wine; the native route below has never been run:
   ```
   tools\env.bat
   build.bat
   ```
   `build.bat` configures the `release` preset, builds `xbe_symbols`, writes
   `config/game.toml` from the XBE and the SDK's XDK signatures, then recompiles the XBE and
   builds `SimsBustinOut.exe` into `out/build/release`. `release.bat` packs that exe with the
   README, the SDK's `THIRD_PARTY.txt` and an empty `assets/` folder into `SimsBustinOut.zip`.

   **Linux** (native Vulkan + SDL3, no Wine):
   ```
   ./build.sh
   ```
   `build.sh` does the same job with the `linux-release` preset and builds
   `SimsBustinOut` into `out/build/linux-release`. Pass a preset name to override it, e.g.
   `./build.sh linux-debug`. You need clang, cmake and ninja on `PATH`, plus SDL3 and the
   Vulkan headers and loader for your distribution. Run it from beside the binary with the
   `assets/` rule above satisfied, e.g. `XBE_PERF=1 ./out/build/linux-release/SimsBustinOut`.
   `./release.sh` packs that binary with the README, the SDK's `THIRD_PARTY.txt` and an
   empty `assets/` folder into `SimsBustinOut-linux.tar.gz`; it refuses to package anything
   from `assets/`, `generated/` or `out/` other than the executable.

   Either way, the first build is long: it compiles one C++ function per guest function
   (27,492 of them, `generated/game/x86_recomp_stats.txt`). `config/game.toml` is already
   written and committed for this XBE, so step 4 does not need to regenerate it.

## Working on the port

- Never edit anything in `generated/`; it is rewritten on every recompile.
- Behaviour changes go in `src/game/` as `X86_FUNC(sub_XXXXXXXX)` overrides (call
  `__imp__sub_XXXXXXXX` for the original) or as `[[hook]]` entries in `config/game.toml`.
- Analysis mistakes are corrected in `config/game.toml` with `[[function]]`, `[[switch]]` and
  `[xbe] data_ranges`.

## Legal

Bring your own disc. Do not commit or distribute the XBE, the disc image, extracted assets, or
any key material. `assets/`, `generated/`, `*.iso` and `out/` are ignored by git for that reason.
The SDK is MIT licensed; see `sdk/LICENSE` and `sdk/THIRD_PARTY.txt`.
