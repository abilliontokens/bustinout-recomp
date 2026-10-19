# The Sims: Bustin' Out - Static Recompilation

An unofficial native PC port of the original Xbox release of *The Sims: Bustin' Out* (USA),
produced by statically recompiling the game's x86 XBE with the
[XboxXBERecomp SDK](https://github.com/TGP482/XboxXBERecomp) (vendored in `sdk/`).
Windows uses Direct3D 12 and XAudio2; Linux is a native Vulkan and SDL3 build — no Wine.

**Status: in progress — boots, renders, reaches playable gameplay.** The native Linux build
boots, plays the intro movie whole and correctly proportioned (pillarboxed at the game's own
4:3 on a wider display) with SDL3 audio, and drives title → main menu → save select → name
entry → Create-a-Sim → save → a loaded, playable household at 2181 draws/frame with 0 skipped
and 0 dropped draws (`out/shots/house3_house.png` shows the club, the Sims, the HUD and a
dialogue; `docs/status.md`, `docs/playtest.md`). Game speed is measured at 1.000 game-seconds per
real second at 60, 144 and uncapped fps. What is still untested: an allocation-ordering defect
in the Vulkan backend's `UploadAlloc`/`AllocDrawSet` (found by code reading, being fixed);
Hor+ ultrawide is not implemented (pillarboxed, not widened); no controller has ever been
plugged into the test machine (gamepad unexercised); saving/reloading through visible play is
unproven; the remaining cheat rows and the per-call frame-rate consumers listed in
`docs/frame-rate.md` are unproven in play; and the Windows build has never been
compiled — no MSVC or Windows SDK here, so every `_WIN32` path is untested. There is no
release to download. Full, evidence-cited state: `docs/status.md`.

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
section) and **F2** the frame-time graph. **F3** captures a frame when `XBE_CAPTURE_DIR` is
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
| `aspect_ratio` | `auto` (default), `4:3`, `16:9`, `16:10`, `21:9`, `32:9` |
| `ultrawide` | `true` (default) / `false` — on Vulkan today this is read and logged but does not widen the frame target yet; the picture is pillarboxed at the chosen aspect (see **Status**) |
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
| `SIMSBO_AUTOSTART=1` | scripted pad self-test on port 0 (blind Start/Up/A cycle; `src/common/game_info.cpp`) — it never finishes name entry, so it cannot reach a household unattended |
| `SIMSBO_STEP=<file>` | step mode: each token written to the file is one burst of pad-0 pulses (`src/common/game_info.cpp`) |

Unset is the normal path; the `SIMSBO_*` switch in `src/game/hooks.cpp` also treats `"0"` as off.

## Layout

```
assets/        your disc image and/or the files extracted from it (never committed)
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

   **Windows** (Direct3D 12 / XAudio2) — **untested: never compiled.** No one has built or
   run this target; this machine has no MSVC or Windows SDK (`docs/toolchain.md`), so treat
   every Windows instruction below as the intended path, not a verified one:
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
