# The Sims: Bustin' Out - Static Recompilation

An unofficial native PC port of the original Xbox release of *The Sims: Bustin' Out* (USA),
produced by statically recompiling the game's x86 XBE with the
[XboxXBERecomp SDK](https://github.com/TGP482/XboxXBERecomp) (vendored in `sdk/`).
Windows uses Direct3D 12 and XAudio2; Linux is a native Vulkan and SDL3 build — no Wine.

**Status: in bring-up. The port has not booted yet.** There is no release to download and no
compatibility claim to make: the disc image has been extracted and the XBE identified, and the
repository and build scaffolding below are in place, but recompilation, rendering, audio and
saving have not been demonstrated. The immediate blocker is known — this game was built with
XDK 5558, while the SDK only ships signatures for XDK 5849 (`sdk/xdk/5849/`).

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
| XDK build matched against the SDK's signature set | **blocked — XDK 5558, SDK ships 5849 only** |
| Repo scaffolded (this repository, Windows + Linux build files) | done |
| `config/game.toml` written by `xbe_symbols` | not yet — needs 5558 signatures |
| Recompilation and native build | not yet |
| Kernel import coverage (`tools/kernel_coverage.py`) | not yet run against this XBE |
| Boot, rendering, audio, saves | not yet demonstrated |

## Controls

The game itself only ever reads an Xbox pad, so the host translates keyboard and mouse into one
on port 0, and controllers are picked up in the order they are plugged in. This is the host
layer's mapping; it has not been exercised in this game yet.

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

**F1** opens the host settings panel (display mode, resolution, aspect ratio, vsync, frame cap,
render scale and related options) and **F2** the frame-time graph. Settings are kept in
`settings.toml` beside the executable; the first run also creates `logs\` and `hdd\` (where
saves go) beside it. These are SDK host-runtime surfaces — present in the code the port builds
on, not yet proven in this game.

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

   **Windows** (Direct3D 12 / XAudio2):
   ```
   tools\env.bat
   build.bat
   ```
   `build.bat` configures the `release` preset, builds `xbe_symbols`, writes
   `config/game.toml` from the XBE and the SDK's XDK signatures, then recompiles the XBE and
   builds `SimsBustinOut.exe` into `out/build/release`. `release.bat` packs that exe with the
   README and third-party notices into `SimsBustinOut.zip`.

   **Linux** (native Vulkan + SDL3, no Wine):
   ```
   ./build.sh
   ```
   `build.sh` does the same job with the `linux-release` preset and builds
   `SimsBustinOut` into `out/build/linux-release`. Pass a preset name to override it, e.g.
   `./build.sh linux-debug`. You need clang, cmake and ninja on `PATH`, plus SDL3 and the
   Vulkan headers and loader for your distribution.

   Either way, the first build is long: it compiles one C++ function per guest function. And
   until XDK 5558 signatures exist, step 4 cannot get past writing `config/game.toml` — see
   **Status** above.

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
