# Toolchain

## Linux host (this machine)

Observed versions, commands shown:

```
$ clang --version
clang version 23.1.3
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin

$ cmake --version
cmake version 4.4.4

$ ninja --version
1.13.2

$ python3 --version
Python 3.14.8

$ pkg-config --modversion sdl3
3.4.18

$ glslangValidator --version
Glslang Version: 11:16.6.0
ESSL Version: OpenGL ES GLSL 3.20 glslang Khronos. 16.6.0
GLSL Version: 4.60 glslang Khronos. 16.6.0

$ ls /opt/dxc/lib
libdxcompiler.so
libdxil.so
```

`vulkaninfo --summary` needs the desktop session's X authority; the agent shell does not inherit it. With it set it runs:

```
$ XAUTHORITY=/run/user/1000/xauth_LSSKQF vulkaninfo --summary
Vulkan Instance Version: 1.4.363
Instance Extensions: count = 26   (incl. VK_KHR_wayland_surface, VK_KHR_xcb_surface,
                                   VK_KHR_xlib_surface, VK_EXT_headless_surface)
```

The session is KDE Plasma on Wayland (`kwin_wayland --socket wayland-1`, Xwayland on `:0`), so a native
Linux build can present either through `VK_KHR_wayland_surface` (`WAYLAND_DISPLAY=wayland-1`) or through
Xwayland with `XAUTHORITY=/run/user/1000/xauth_LSSKQF`. The GPU is an AMD `Cyan Skillfish [BC-250]` at
`01:00.0` (`lspci`) on the RADV ICD (`/usr/share/vulkan/icd.d/radeon_icd.json`).

## Windows target

Natively: clang/clang++ (LLVM) plus the Visual Studio environment (`VsDevCmd.bat`), D3D12 and XAudio2, built with `build.bat` from the repository root. That route has never been run.

Cross-compiled on this Linux box, which is how the Windows exe has actually been built: `clang-cl` and `lld-link` (the LLVM 23.1.3 above) against Microsoft's MSVC CRT and Windows SDK as downloaded by [xwin](https://github.com/Jake-Shadle/xwin), through `cmake/windows-cross.cmake` and the `windows-cross` preset. Observed versions: xwin 0.10.0, MSVC CRT 14.44.17.14, Windows SDK 10.0.26100.

```
$ cargo install xwin --locked
$ xwin --accept-license --arch x86_64 --cache-dir ~/.cache/xwin \
      splat --use-winsysroot-style --preserve-ms-arch-notation --output ~/.cache/xwin/sysroot
$ cmake --preset host-tools && cmake --build --preset host-tools
$ cmake --preset windows-cross && cmake --build --preset windows-cross
```

`host-tools` builds `xbe_recomp` for Linux into `out/build/host-tools`; the cross build runs that binary to recompile the XBE (it is platform-neutral output, byte-identical to what the Linux build generates, so `generated/` is shared) instead of building it for Windows. `XWIN_DIR` (environment or cache) points the toolchain at another xwin output; it must be the `/winsysroot` layout above, and it holds only the release CRT, so Debug builds of the cross preset do not link. The exe lands in `out/build/windows-cross/SimsBustinOut.exe` (PE32+ GUI, x86-64), linked against the DLL CRT (`VCRUNTIME140.dll`, `MSVCP140.dll`, `api-ms-win-crt-*`) like the native build.

It has never run on Windows. It has run under Wine 11.20 here (`wine`, default prefix, Wine's own `d3dcompiler_47`, which is vkd3d-shader): left alone it plays both intro movies with sound through XAudio2, and with the synthetic pad from `SIMSBO_AUTOSTART=1` it walks the menus into Create-a-Sim with the 3-D Sim drawn. Wine's D3D12 is vkd3d over Vulkan, not Microsoft's runtime, so this proves the build and that the Win32/D3D12/XAudio2 code paths execute, not that Windows draws them correctly.

## Linux target

Built with `./build.sh` from the repository root: clang + ninja, Vulkan rendering, SDL3 for windowing/input, DXC from `/opt/dxc` (`libdxcompiler.so`, `libdxil.so`) with `glslangValidator` as fallback for SPIR-V compilation.

## Headless harness

For GPU-less testing, `sdk/harness/build.sh` builds the harness (`null_backend.cpp`, `null_audio.cpp`, `reference_renderer.cpp`, `pad_script.cpp`) and `sdk/harness/run.sh` runs it. See `sdk/harness/README.md`.
