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

Requires clang/clang++ (LLVM) plus the MSVC environment (`vcvars`), D3D12 and XAudio2. Built with `build.bat` from the repository root. **This Linux box has no MSVC or Windows SDK, so the Windows target cannot be compiled here.**

## Linux target

Built with `./build.sh` from the repository root: clang + ninja, Vulkan rendering, SDL3 for windowing/input, DXC from `/opt/dxc` (`libdxcompiler.so`, `libdxil.so`) with `glslangValidator` as fallback for SPIR-V compilation.

## Headless harness

For GPU-less testing, `sdk/harness/build.sh` builds the harness (`null_backend.cpp`, `null_audio.cpp`, `reference_renderer.cpp`, `pad_script.cpp`) and `sdk/harness/run.sh` runs it. See `sdk/harness/README.md`.
