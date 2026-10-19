# Windows target audit (read-only, no Windows compiler on this machine)

Scope: every SDK change since the fork point `30124a8` in `sdk/` (6 commits,
`30124a8..7f2e294`; `git -C sdk diff 30124a8..HEAD` = 37 files, +5516/−147), plus this
repo's `build.bat`, `release.bat`, `CMakePresets.json`, `CMakeLists.txt` and the game-side
sources in `src/` that a Windows build compiles for the first time.

Method: textual diff inspection, preprocessor-balance check, symbol cross-reference by
grep, and a tag-to-tag comparison of Dear ImGui v1.91.0 vs v1.91.9b cloned to `/tmp`.
Nothing was built or run — this machine has clang 23 but no Windows SDK/MSVC headers, so
**no claim below is "it compiles"; the strongest claim available is "the Windows text is
unchanged, or changed only in ways that are syntactically self-evident."**

Verdict up front: **I believe the Windows target would compile today**, with the caveats
prioritised at the end. The Windows branches were preserved with unusual care; the real
exposure is (a) one deprecated-but-retained ImGui DX12 entry point, and (b) shared
(non-guarded) code whose *behaviour* on Windows changed and has never run there.

---

## 1. The SDK diff against the fork point, hunk by hunk

Legend: **[pure]** = purely additive / re-scoped for Windows (Windows compiles the same
code as at `30124a8`); **[shared]** = not purely additive: code a Windows build compiles
changed in substance; **[win-new]** = new file a Windows build now compiles.

### cmake/

| File | Class | What changed |
| --- | --- | --- |
| `cmake/XbeRecomp.cmake` | [pure], one exception | Platform split; §3 below line by line. The one exception that touches Windows is the ImGui pin (§4). |
| `cmake/XbeTools.cmake` (new) | [pure] | Host-tools-only include (Zydis + toml++ only). Not included by this repo's `CMakeLists.txt` (which includes only `cmake/XbeRecomp.cmake`, `CMakeLists.txt:20`), so it does not participate in the Windows game build at all. |

### runtime/app/

| File | Class | Windows effect |
| --- | --- | --- |
| `runtime/app/main.cpp` | [pure] | Only change: `#if defined(_WIN32)` at line 1 and `#endif` at line 289 wrap the previously unconditional file. Zero removed lines (`git diff` shows no `-` lines). Every include, `wWinMain`, `WndProc`, `app::` definition is byte-identical. It is also removed from the non-Windows source list in CMake, so the guard is belt-and-braces. |
| `runtime/app/main_sdl.cpp` (new, 466 ln) | [pure] | Whole file under `#if !defined(_WIN32)` (lines 5, 466) **and** removed from the Windows source list (`XbeRecomp.cmake:79-83`). Defines the same `app::` symbols as `main.cpp` (§2). |
| `runtime/app/platform.h` (new) | **[shared — latent]** | Declares `app::platform::{NativeHandle, ClientSize, DesktopSize, HasFocus, IsVisible, IsMinimized, SetVisible, Raise, SetCursorVisible, CursorVisible, QueryHdr}` (lines 26-72) with **no Windows definition anywhere** — only `main_sdl.cpp` defines them. This is safe today only because no Windows-compiled TU references them (§2). It is a standing link trap: the first Windows-side caller of e.g. `app::platform::HasFocus()` gets an unresolved external, not a compile error. |
| `runtime/app/game_files.cpp` | **[shared]** | The largest behavioural change that Windows shares. See below. |
| `runtime/app/handover.cpp` | [pure] | Windows code untouched (no removed lines). Linux AF_UNIX counterpart added under `#else` of the `g_ready` HANDLE (line 32), `StartHandover` (line 71-ish) and `FinishHandover` branches. Windows still does `OpenEventW`/`SetEvent`/`CloseHandle`. |
| `runtime/app/settings.cpp` | [pure] for Windows | Windows branch of `Desktop()` (lines 137+) is the original `MonitorFromWindow`/`EnumDisplaySettingsW` code verbatim; SDL branch added under `#else`. `platform.h` is included only under `#if !defined(_WIN32)` (lines 22-24), so Windows never even sees the undefined `platform::` declarations from this TU. |
| `runtime/app/splash.cpp` | [pure] for Windows | `HWND g_splash` moved from file scope into the `#if defined(_WIN32)` block (line 50) — the only removed line in the file, and every Windows user of it (`OpenSplash` Win32 branch, `CloseSplash` lines 276-278) is inside the same guard. WIC `Read()` unchanged. Linux SDL branch is additive. |
| `runtime/app/system_info.cpp` | [pure] for Windows | All Windows code (`RegString`, `CpuName` via `__cpuid`, `GetNativeSystemInfo`, `EnumDisplayDevicesW` loop, `#pragma comment(lib, "advapi32.lib")` line ~35) retained inside `#if defined(_WIN32)`; Linux `OsVersion`/`CpuName`/`ExePath`/`DisplayStack` added under `#else`. No removed lines. |
| `runtime/app/image_decode.cpp` / `.h` (new) | [win-new, empty] | Whole TU under `#if !defined(_WIN32)` (`.cpp:11`, `.h:5`). **It is not in the Windows `REMOVE_ITEM` list**, so a Windows build compiles it to an empty translation unit. Harmless to compile/link (at worst an `LNK4221`-style "no public symbols" warning, which lld does not emit by default), but it is dead weight in the Windows link and the one place where the "stay out of the Windows link entirely" comment at `XbeRecomp.cmake:78` is not literally true. |

#### game_files.cpp in detail (the one real Windows behaviour change in app/)

Removed on all platforms, including Windows:

- `<cwctype>` include and the `std::wstring`/`std::towlower` extension comparison;
  replaced by ASCII `Fold()`/`FoldEqual()` on `path.extension().string()`
  (`game_files.cpp:44-50`, diff hunk @@ -67). On Windows `.string()` (not `.wstring()`)
  narrows through the ANSI code page — irrelevant for `.iso`, but a real change of
  mechanism.
- `FindGame`'s selection logic. At the fork: first copy with a matching XBE wins,
  extracted files before any image, unconditionally. Now: an extracted folder whose
  XBE is present but which holds **zero** other game data files (`CountGameFiles`,
  recursive, host-artefact-filtered) is *deferred* (`defer_match`), a disc image with
  the right XBE beats it, and the incomplete folder is only used as a last resort with
  three new log lines. This changes what a Windows player with a bare
  `assets/default.xbe` (the normal dev-tree state, and exactly what `build.bat:18`
  checks for) experiences if they also drop an `.iso` in `assets/`: previously the
  folder won, now the ISO wins. That is presumably the intent (it is well argued in
  the comments), but it is **not** purely additive and has never run on Windows.
- The two modal prompts were factored into `ShowMissingGame()` /
  `ConfirmDifferentRelease()`; the Windows branches inside them are the original
  `MessageBoxW` calls verbatim, including `MB_DEFBUTTON2` (diff hunk in
  `FindGame`'s tail). No behavioural change there.
- `FindAssets()` now also does a case-folded `directory_iterator` scan per level
  (no-op effect on case-insensitive Windows filesystems; extra syscalls only).
- New shared helper `WideToUtf8()` handles `wchar_t` as UTF-16 with surrogate joining
  under `if constexpr (sizeof(wchar_t) == 2)` — the Windows path of that helper is
  exercised only by `IsHostArtefactFile` (`kGame.splash`); low risk, but it is new
  code running on Windows that has only ever compiled under libstdc++.

### runtime/audio/

| File | Class | Windows effect |
| --- | --- | --- |
| `runtime/audio/sdl_audio.cpp` (new) | [pure] | `#if !defined(_WIN32)` whole-file (lines 2, 196) and removed from the Windows list (`XbeRecomp.cmake:79-83`). Defines `audio::StartOutput`, the counterpart of `xaudio2.cpp` (§2). |
| `runtime/audio/xaudio2.cpp` | unchanged | Not in the diff. Still the Windows `audio::StartOutput`. |

### runtime/gpu/

| File | Class | Windows effect |
| --- | --- | --- |
| `runtime/gpu/backend.h` | **[shared]** | New virtual `bool WidensFrame() const { return true; }` (line 44). `D3D12Backend` does not override it, so the D3D12 backend reports `true` — the pre-fork behaviour (the XDK layer always widened). `VulkanBackend` overrides it to `false` (`vulkan.cpp:303`). Default-true is exactly the choice that keeps Windows semantics unchanged. |
| `runtime/gpu/state.h` | **[shared]** | Two new floats `super_x = 1, super_y = 1` (line 190) in the POD `State`. Both backends and the render-thread recorder are in the same binary, so the layout shift is internally consistent on either platform; defaults are 1, so a state snapshot that never sets them behaves as before. |
| `runtime/gpu/d3d12.cpp` | **[shared]** | §5 below. |
| `runtime/gpu/vertex.cpp` | [pure]-ish | One line: `#include <dxgiformat.h>` → `#include "gpu/dxgi_format.h"`. On Windows the new header (`dxgi_format.h:6-7`) does `#include <dxgiformat.h>` itself, so the Windows TU sees the identical system enum; off Windows it supplies the same values by hand. The only Windows-visible difference is include *kind* (quoted, resolved via the `runtime` include dir that `add_xbe_image` already adds, `XbeRecomp.cmake:173`). |
| `runtime/gpu/render_thread.cpp` | [pure] | `windows.h` include moved under `#if defined(_WIN32)` with `<pthread.h>` in the `#else` (lines 5-14); `SetThreadDescription` call guarded, `pthread_setname_np` in the `#else` (lines 466+). Windows text otherwise untouched; no removed lines. |
| `runtime/gpu/vulkan.cpp`, `vulkan.h`, `spirv.cpp`, `spirv.h` (new) | [pure] | All `#if !defined(_WIN32)` whole-file and removed from the Windows source list. `spirv.h` would be safe anyway; `vulkan.cpp` provides the non-Windows `gpu::CreateBackend` (§2). |
| `runtime/gpu/dxgi_format.h` (new) | [pure] | See vertex.cpp above. |

### runtime/kernel/ — six new exports, all shared with Windows

New `KERNEL_EXPORT`s, all in code a Windows build compiles, none platform-guarded:

- `kernel/iocp.cpp` (new file): 191 `NtCreateIoCompletion`, 227 `NtSetIoCompletion`,
  223 `NtRemoveIoCompletion`, 232 `NtUserIoApcDispatcher` (lines 87, 112, 120, 162),
  plus the non-export `kernel::PostIoCompletion` declared in `kernel.h:189`.
- `kernel/files.cpp`: 76 `IoQueryVolumeInformation` (line 852), 243
  `ObOpenObjectByName` (line 915).

Windows-relevant inspection of these:

- `iocp.cpp` includes only `<chrono>` + `kernel/exports.h`, but everything it uses
  (`std::deque`, `INT64_MAX`, `std::mutex`, logging) arrives via
  `kernel/kernel.h:2-14`. `INT64_MAX` is already used by fork-era Windows-compiled
  code (`kernel/xboxkrnl.cpp:61`, `kernel/objects.cpp:210`), so its availability under
  the MSVC-flavoured headers is already proven by the fork's own Windows build history.
- `files.cpp` also changes existing behaviour on Windows: `NtSetInformationFile`
  gains `len = a[3]` and accepts class 30 (`FileCompletionInformation`) where the
  fork returned `kInvalidParameter` (`files.cpp` diff @@ -545), and `Finish()` now
  posts to a bound completion port. `IoQueryVolumeInformation` returns
  `kInvalidInfoClass` = `0xC0000003`, the new enumerator in `kernel.h` (diff @@ -32).
  All additive from the guest's point of view (previously-failing calls now succeed),
  but it is new shared logic that has only run on Linux.
- `kernel/objects.cpp`: `FromPointer` gains dispatcher type 4 → `ObjType::Queue`
  (diff @@ -92), and `FindNamed` gains a case-insensitive fallback scan (diff @@ -121)
  plus `#include <cctype>`. The fallback only runs when the exact lookup misses or
  the weak pointer is dead, so existing Windows lookups are unchanged; the scan holds
  `g_names_lock` while calling `weak.lock()`, same as the original exact-hit path's
  lock scope. Shared, additive-in-effect.
- `kernel/boot.cpp`: the Windows `Relaunch` body (`boot.cpp:187-214`, `CreateProcessW`
  / `CreateEventW` / `WaitForMultipleObjects(..., 15000)`) is **byte-identical** to the
  fork; only the `#else` (Linux) branch was replaced (fork's 3-line `execl` became the
  ~200-line fork/pipe/pidfd handover) and the Linux include block grew. The two
  removed lines in the whole file are both in the Linux branch.
- `kernel/memory.cpp`: one added include, `<unistd.h>`, inside the existing `#else`
  (diff @@ -13). Windows untouched.
- `kernel/kernel.h`: additive only (enumerator + `PostIoCompletion` declaration,
  noted above).

### runtime/xdk/

| File | Class | Windows effect |
| --- | --- | --- |
| `runtime/xdk/xnet.cpp` | [pure] | Winsock includes moved under `#if defined(_WIN32)` (lines 11-20); `#else` includes the new `xdk/winsock_compat.h`. Windows TU still gets real `<winsock2.h>`/`<ws2tcpip.h>`. No removed logic lines. |
| `runtime/xdk/winsock_compat.h` (new) | [pure] | Content is the *moved* body of the old `harness/compat/winsock2.h` (see harness/ below); only included off Windows and from the harness shims. Never included by a Windows TU (`xnet.cpp` includes it only in the `#else`). |
| `runtime/xdk/d3d8.cpp` | **[shared]** | The SuperSampleScale model. See below. |

`d3d8.cpp` changes that execute on Windows (all in shared code, no guards):

- New globals `g_frame_widened` (default `true`), `g_super_x/g_super_y` and functions
  `SetMultiSampleType()` / `UpdateSuperSample()` (diff @@ -222). On Windows
  `g_frame_widened` is set from `D3D12Backend::WidensFrame()` = the new default
  `true`, so `FrameCentreShift()` behaves as at the fork on 16:9 and widens on
  ultrawide exactly as before — **plus** its result is now multiplied by
  `g_state.super_x`.
- `SetupPresentation()` gains a `multisample` parameter fed from
  `R32(pp + 0x10)` at both `Direct3D_CreateDevice` and `D3DDevice_Reset`; the fork
  wrote constant `1.0f` into the device's SuperSample X/Y fields, now
  `UpdateSuperSample()` writes the modelled scale (2×2 for this title's
  `0x2222` supersample type) and mirrors it into `g_state.super_x/super_y`.
- `UpdateViewportConstants()` composes c-38/c-37 with the super scale (diff @@ -271).
- `D3DDevice_SetRenderTarget` now calls `UpdateSuperSample()`.

This is intended, title-driven behaviour (the same code the Linux/Vulkan side was
verified with), but on the D3D12 path it is **wholly unexercised**: the first Windows
run of this title will be the first time `surf[]`, c-38/c-37 and the centre shift are
computed by these formulas against the D3D12 backend. Any error here is a *visual*
regression (mis-scaled/mis-centred picture), not a build failure.

### harness/ (not part of the Windows game build)

`harness/compat/dxgiformat.h`, `winsock2.h`, `ws2tcpip.h` became one-line shims that
include the real headers now living in the runtime (`gpu/dxgi_format.h`,
`xdk/winsock_compat.h`). Verified the moved content is verbatim: the deleted bodies
match the added bodies of `dxgi_format.h` (non-Windows branch) and
`winsock_compat.h` line for line in the diff. The harness is built by
`sdk/harness/build.sh`, is referenced by nothing in `XbeRecomp.cmake`, `build.bat` or
the root `CMakeLists.txt`, and cannot affect the Windows game target. It is listed
only because it is in the fork diff.

### xdk/5558/ (data, not code)

`xdk/5558/layout.toml` and `signatures.toml` are new data files consumed by
`xbe_recomp`/`xbe_symbols` at codegen time (`config/game.toml:7` selects
`xdk = 5558`). Platform-neutral TOML; the Windows build consumes them through the
same `add_custom_command` as Linux (`XbeRecomp.cmake:148-156`). No Windows-specific
risk beyond "codegen output differs from the fork's 5849 assumption," which is the
point of the port and is already what the verified Linux build runs.

---

## 2. Guards: balance, completeness, duplicate/missing symbols

Preprocessor balance: every changed `.cpp`/`.h` in `sdk/runtime` and `src` has equal
`#if`-family and `#endif` counts (checked mechanically). Every platform split follows
the same pattern — `#if defined(_WIN32)` keeps the original Windows body, `#else`
holds the Linux body — with two deliberate exceptions noted below.

Named symbols, each defined exactly once per platform:

| Symbol | Windows definition | Non-Windows definition | Verdict |
| --- | --- | --- | --- |
| `gpu::CreateBackend` | `runtime/gpu/d3d12.cpp:4057` | `runtime/gpu/vulkan.cpp:2197` (whole file `#if !defined(_WIN32)`) | OK. Disjoint by *both* CMake list exclusion (`XbeRecomp.cmake:79-92`) and file guards. Declared once, `gpu/backend.h:57`. |
| `audio::StartOutput` | `runtime/audio/xaudio2.cpp:28` (unchanged by the diff) | `runtime/audio/sdl_audio.cpp:184` (`#if !defined(_WIN32)` whole file) | OK. Declared `audio/audio.h:10`; caller `xdk/dsound.cpp:368`. Windows list removes only `sdl_audio.cpp`, keeps `xaudio2.cpp`. |
| `app::` host functions (`ExeDir`, `Instance`, `Window`, `UiLock`, `ApplyDisplay`, `OpenGameWindow`, `Pace`, `wWinMain`/`main`) | `runtime/app/main.cpp:178-289`, whole file `#if defined(_WIN32)` | `runtime/app/main_sdl.cpp:236-466`, whole file `#if !defined(_WIN32)` | OK, with one asymmetry: `main_sdl.cpp` additionally defines `app::platform::*` and `app::Relaunched()`-equivalents that `main.cpp` does not (it inlines `GetCommandLineW` checks instead, e.g. `main.cpp:232`). Nothing Windows-side references the missing ones — checked: the only `platform::` references in the tree are `main_sdl.cpp`, `settings.cpp`'s `#else` branch, `gpu/vulkan.cpp`, and `input_kbm.cpp`'s `#else` branch. **Latent trap, not a defect today** (see §1, platform.h). |
| `kernel::Relaunch` | `runtime/kernel/boot.cpp:185`, Windows body under `#ifdef _WIN32` (line 187) | Same function, Linux body under `#else` | OK. One definition, internal split; declared `kernel.h:197` as `[[noreturn]]`, both branches end in `std::_Exit(0)`. Caller `kernel/xboxkrnl.cpp:1191`. |
| `app::platform::*` (11 functions) | **none** | `runtime/app/main_sdl.cpp` (in the `app` namespace block ending line 349; `NativeHandle`/`SdlWindow` at 307-309) | Declared-not-defined on Windows. Safe only because unreferenced there (above). |
| `kbm::Message` / `kbm::NoteWheel` | `Message` defined `src/common/input_kbm.cpp:273` under `#if defined(_WIN32)`; `NoteWheel` **not** defined on Windows | `NoteWheel` defined in the `#else` (line ~332); `Message` not defined | OK and correctly cross-guarded at the use sites: `game_info.cpp:164-168` wires `.message = &kbm::Message` only under `_WIN32`, and `overlay.cpp:629-633` calls `kbm::NoteWheel` only under `#if !defined(_WIN32)` with a comment saying exactly why. This is the one place where a guard slip would have been a link error, and it is right. |
| `app::DecodePng` | declared nowhere on Windows (`image_decode.h` is wholly `#if !defined(_WIN32)`) | `image_decode.cpp:48` | OK: the only caller is `splash.cpp`'s Linux branch, and `splash.cpp` includes the header only under `#if !defined(_WIN32)` (line ~30). Windows splash uses WIC. |

Completeness of the Windows branches (does the Windows side still have everything it
had at the fork?): confirmed by the removed-lines audit — across
`main.cpp`, `handover.cpp`, `settings.cpp`, `system_info.cpp`, `render_thread.cpp`,
`xnet.cpp`, `boot.cpp` the diff removes **zero** Windows lines; `splash.cpp` removes
one (`HWND g_splash`'s old position) which reappears inside the guard. The files
that did lose lines on all platforms are `game_files.cpp`, `kernel/files.cpp`,
`kernel/objects.cpp` and `gpu/d3d12.cpp`, each covered in §1/§5.

One guard-style inconsistency worth knowing: the older kernel files use `#ifdef _WIN32`
(`boot.cpp:9`, `memory.cpp:11`, `disc.cpp`, `threads.cpp`) while the new app/gpu code
uses `#if defined(_WIN32)`. Both are equivalent for a compiler-defined macro; no file
mixes the two for the same branch in a way that could diverge.

---

## 3. `sdk/cmake/XbeRecomp.cmake` vs `git show 30124a8:cmake/XbeRecomp.cmake`

Line by line, the Windows-relevant machinery is intact. Current file is 189 lines,
fork's was 105; every difference is the platform split plus the ImGui pin.

| Item | Fork (30124a8) | Now | Windows status |
| --- | --- | --- | --- |
| ImGui source list | unconditional: core + `imgui_impl_win32.cpp` + `imgui_impl_dx12.cpp` | `if(WIN32)` branch (lines 58-65) has the **identical six files**; `#else` has core + SDL3/Vulkan backends | Intact for Windows. |
| Runtime source list | `file(GLOB … runtime/*/*.cpp)`, everything linked | Same glob (line 76), then `if(WIN32)` removes exactly `app/main_sdl.cpp`, `gpu/vulkan.cpp`, `gpu/spirv.cpp`, `audio/sdl_audio.cpp` (lines 79-83) | Intact, with the §1 caveat: `app/image_decode.cpp` (new, empty on Windows) is *not* removed and is compiled. Everything the fork linked on Windows is still linked. |
| ImGui pin | `GIT_TAG v1.91.0` | `GIT_TAG v1.91.9b` (line 34) + explanatory comment | **Changed for Windows** — §4. |
| `WIN32` executable flag | `add_executable(${target} WIN32 …)` | Same, in the `if(WIN32)` branch (line 168); non-Windows branch omits it | Intact. |
| Link line | `tomlplusplus $<LINK_ONLY:SDL3::SDL3-static> d3d12 dxgi dxguid d3dcompiler xaudio2 ole32 windowscodecs dbghelp winmm dwmapi ws2_32` | Identical string, `XbeRecomp.cmake:178`, in the `if(WIN32)` branch | Intact, verbatim. |
| Generated-source custom command | `add_custom_command(OUTPUT codegen.stamp … COMMAND xbe_recomp config/${config} --xdk …)` | Lines 148-156, unchanged | Intact (quoted diff shows no change in this hunk). |
| SHA-1 embed | `file(SHA1 …)` + `file(CONFIGURE … kImageSha1 …)` | Lines 161-166, unchanged | Intact. |
| Per-source `-O2` options | `COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-Wno-unused-label;…"` on generated sources; `-O2` injected into `CMAKE_<LANG>_FLAGS_RELEASE/RELWITHDEBINFO` (lines 16-23 of both versions) | Identical (line 159; the foreach block is above the diff's first hunk and unchanged) | Intact. `NDEBUG` still deliberately undefined — same comment. |
| `-fuse-ld=lld` | `target_link_options(${target} PRIVATE -fuse-ld=lld -g)` | Line 188, **outside** both platform branches — now applies to the Linux target too | Windows intact; note it is no longer Windows-only, which matters for Linux, not for this audit. |
| `target_compile_options(-fno-strict-aliasing)`, `UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS`, include dirs | unconditional | Lines 173-176, still unconditional | Intact. |
| SDL3 header scoping for `core/input.cpp` | `set_source_files_properties(… INCLUDE_DIRECTORIES …)` | Line 135, unchanged, still unconditional | Intact. |

Quoted differences (the complete set that touches the Windows path):

1. Line 34: `GIT_TAG v1.91.0` → `v1.91.9b` (comment at lines 30-33 claims the win32/dx12
   pair "compile clean on both" — a claim made by the SDK author, re-checked in §4).
2. The `if(WIN32)/else()` wrappers themselves (lines 58, 77, 167, 177) — pure
   re-scoping; the Windows arms reproduce the fork's statements verbatim.
3. Nothing else. In particular no Windows library was dropped, no source was dropped
   from the Windows list except the four Linux counterparts, and the custom command,
   SHA-1 embed and `-O2` machinery are untouched.

One structural note: the Windows `REMOVE_ITEM` list uses absolute
`${XBE_SDK_DIR}/runtime/...` paths, and `file(GLOB)` returns absolute paths built
from the same `${XBE_SDK_DIR}`, so the removal matches on Windows exactly as on
Linux (CMake normalises both to forward slashes). This is the same mechanism the
fork already relied on implicitly; it is sound, but it means a *future* Linux-only
runtime file whose author forgets to add it to the list will silently enter the
Windows link — `image_decode.cpp` already demonstrates the failure mode (benign
there only because the file self-guards to empty).

---

## 4. The ImGui bump: v1.91.0 → v1.91.9b

Compared the two tags (cloned to `/tmp/imgui-1910`, `/tmp/imgui-1919b`; the fetched
copy at `out/build/linux-release/_deps/imgui-src` confirms `IMGUI_VERSION "1.91.9b"`).

### What changed in the Windows backends

`backends/imgui_impl_win32.h`: **no API change.** The diff is two comment lines.
In the `.cpp`, all changes are internal: helpers gained an explicit `ImGuiIO&`
parameter (multi-context support), `ImGui_ImplWin32_VirtualKeyToImGuiKey` became
`ImGui_ImplWin32_KeyEventToImGuiKey(WPARAM, LPARAM)`, and an additive
`ImGui_ImplWin32_WndProcHandlerEx(..., ImGuiIO&)` appeared alongside the original
`ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM)` — **same name, same
signature** as in 1.91.0. This port's uses: `main.cpp:30` (extern declaration,
matches), `main.cpp:127` (call in `WndProc`), `main.cpp:266`
(`ImGui_ImplWin32_Init(HWND)` — signature unchanged), `d3d12.cpp:3534`
(`ImGui_ImplWin32_NewFrame()` — unchanged). Safe.

`backends/imgui_impl_dx12.h`: **the initialisation API changed shape.** 1.91.0:

```cpp
bool ImGui_ImplDX12_Init(ID3D12Device* device, int num_frames_in_flight, DXGI_FORMAT rtv_format,
                         ID3D12DescriptorHeap* cbv_srv_heap,
                         D3D12_CPU_DESCRIPTOR_HANDLE font_srv_cpu_desc_handle,
                         D3D12_GPU_DESCRIPTOR_HANDLE font_srv_gpu_desc_handle);
```

1.91.9b makes `ImGui_ImplDX12_Init(ImGui_ImplDX12_InitInfo* info)` the primary API
(new struct with `Device`, `CommandQueue`, `NumFramesInFlight`, `RTVFormat`,
`DSVFormat`, `SrvDescriptorHeap`, alloc/free callbacks), and keeps the old
six-argument overload only as a legacy shim *"Obsoleted in 1.91.5"*, inside
`#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS`. This port calls the legacy overload at
`runtime/gpu/d3d12.cpp:903-904`. Inspection of the 1.91.9b `.cpp` (lines 787-803)
shows the shim is fully implemented: it fills an `InitInfo`, creates its own direct
command queue for texture uploads, and forwards. `imconfig.h` ships with
`IMGUI_DISABLE_OBSOLETE_FUNCTIONS` commented out (line 31, confirmed in both the
clone and the fetched `_deps` copy), and nothing in this repo defines it. **So the
call compiles and works under 1.91.9b defaults — but it is the single most
fragile point in the Windows build**: anyone defining
`IMGUI_DISABLE_OBSOLETE_FUNCTIONS` (a common "clean build" habit, and the direction
ImGui is pushing) turns it into a hard compile error, and the next ImGui bump past
the legacy-removal point will do the same. Migration is mechanical (fill the struct;
`CommandQueue` can be the backend's existing queue instead of the shim's private
one).

Also in the DX12 header: it now `#include <d3d12.h>` itself (1.91.0 forward-declared
the D3D12 types). `d3d12.cpp` already includes D3D12 headers before the backend
header, so include order is not a new problem; it does mean the backend header can
no longer be included in a TU that lacks the Windows SDK — no such TU exists in the
Windows build.

Other backend changes in the tag range that this port could trip on: none found.
`ImGui_ImplDX12_NewFrame`, `ImGui_ImplDX12_RenderDrawData(ImDrawData*,
ID3D12GraphicsCommandList*)` (`d3d12.cpp:3533, 3540`) and
`ImGui_ImplDX12_CreateDeviceObjects`/`InvalidateDeviceObjects` keep their names and
signatures (the latter two swapped declaration order only).

### The overlay's core-ImGui API surface

Extracted all 112 distinct `ImGui::`/`ImGui_*` identifiers used by
`src/common/overlay.cpp`, `runtime/app/main.cpp` and `runtime/gpu/d3d12.cpp` and
checked each against the 1.91.9b headers: **zero are missing.** Spot-checked the
ones with known churn in this range: `ImGui::SeparatorText` (overlay.cpp:540+),
`ImGuiStyle::ScaleAllSizes` (overlay.cpp:168), `ImGui::IsKeyPressed(ImGuiKey, bool)`
(overlay.cpp:611, 618 — still `bool repeat = true` default), `ImGuiKey_F1/F2`,
`BeginTable`/`TableSetupColumn`, `SliderInt(..., ImGuiSliderFlags_AlwaysClamp)`,
`SetNextWindowBgAlpha`, `PlotLines` — all present with compatible signatures in
1.91.9b. (The Linux build already compiles this same overlay against 1.91.9b with
the SDL3/Vulkan backends, which covers the core-API surface; only the win32/dx12
backend TUs are Windows-unique.)

Net: the bump is safe for Windows as it stands, conditional on the legacy DX12 init
overload remaining enabled. The comment added at `XbeRecomp.cmake:30-33` ("the
win32/dx12 pair compile clean on both") is consistent with what the tag diff shows
for the win32 backend and for the dx12 backend's *own* compilation; it does not
mention that the *call site* in `d3d12.cpp` now depends on a deprecated overload.

---

## 5. The `d3d12.cpp` change (SuperSampleScale in `FillSurfaceConstants`)

The diff is 10 lines, not one — a signature change plus its single call site:

- `d3d12.cpp:2219-2223`: `FillSurfaceConstants(const State&, GuestExtent, DrawConstants&)`
  becomes `FillSurfaceConstants(const State&, GuestExtent, bool vertex_program, DrawConstants&)`,
  and the first two constants become
  `float(size.w) * (vertex_program ? s.super_x : 1.0f)` and the `super_y` analogue.
- `d3d12.cpp:2393`: the sole caller, in `D3D12Backend::UploadConstants`, passes
  `sh.prog`.

Checked: (a) there is exactly one caller (grep for `FillSurfaceConstants` in the
file: definition + line 2393), so the signature change has no stale call sites;
(b) it is a file-local free function (not a member, not declared in any header), so
nothing outside this TU can break; (c) semantics match the Vulkan counterpart
token-for-token — `vulkan.cpp:1248` computes
`float(size.w) * (sh.prog ? s.super_x : 1.0f)` inside its own `FillConstants`, with
the same comment; `sh.prog` is the same `DrawShaders::prog` flag both backends
receive; (d) `s.super_x/super_y` are the new `State` fields (`state.h:190`,
default 1), so with no supersampled target the D3D12 output is numerically
identical to the fork; (e) nothing else in the 4000-line file changed (the diff for
`d3d12.cpp` is exactly this one hunk, `git diff 30124a8..HEAD -- runtime/gpu/d3d12.cpp`).

Syntactically plausible: yes — plain float arithmetic on members that exist,
bool parameter from an existing field. Behaviourally: on Windows this is the first
time the D3D12 backend will consume a non-1 `super_x` (this title's back buffer,
via the shared `d3d8.cpp` changes in §1); the formula is proven only on Vulkan.

---

## 6. `build.bat` / `release.bat` vs the sibling port (Urbz)

Byte diff against `/home/work/Desktop/Projects/urbz-recomp/`:

- `build.bat`: **exactly two lines differ** — line 3 `GAME_TITLE` and line 5
  `GAME_EXE` (`SimsBustinOut` vs `Urbz`). `GAME_XBE=default.xbe` is identical in
  both. Every mechanic is therefore identical by construction; enumerated to be
  sure they are present in this copy:
  - VS discovery via `vswhere.exe -requires …VC.Tools.x86.x64` (line 22);
  - toolchain offer-to-install with the three `VC.Llvm.Clang(Toolset)` /
    `VC.CMake.Project` components (line 36), `CI` env suppressing prompts (line 30);
  - **environment setup is `VsDevCmd.bat -arch=amd64 -host_arch=amd64`** (line 74)
    plus manual PATH prepends for VS CMake/Ninja/LLVM and a standalone
    `%ProgramFiles%\LLVM` `clang++.exe` (line 77). Note for the record: there is
    **no `vcvars64.bat` invocation in either port** — anyone following instructions
    that mention `vcvars64.bat` should be pointed at `VsDevCmd.bat` here;
  - `clang++`/`cmake`/`ninja`/`git` presence probe (lines 78-81);
  - first-build symbols step: `cmake --preset release`, skip if `config\game.toml`
    already has `[symbols]`, else build `xbe_symbols` and run it with the SDK path
    read back from `sdk_dir.txt` (lines 46-53) — present and unchanged;
  - `assets\default.xbe` gate (line 18), 30-90-minute first-build warning (line 58),
    Play-now prompt (lines 66-67).
- `release.bat`: **exactly one line differs** — line 2 `GAME_EXE`. The packaging
  mechanic is `7z a "%GAME_EXE%.zip" …` (line 14) in both, preceded by rebuilding
  via `build.bat` if the exe is missing and re-deriving `sdk_dir.txt` if absent.
  Recorded plainly: **neither port has a 7-Zip fallback** — if `7z` is not on PATH,
  line 14 fails and no zip is produced, in the sibling exactly as here. If a
  fallback was expected, it does not exist in either tree; this port is not missing
  anything the sibling has.
- Supporting files are consistent with the `.bat`s: `CMakePresets.json`' Windows
  presets (`debug`/`release`/`relwithdebinfo`, `hostSystemName == Windows`,
  Ninja, `clang`/`clang++`, `-march=x86-64-v2`) are identical to Urbz's; this repo
  only *adds* the `linux-*` presets, which are conditioned on
  `hostSystemName == Linux` and cannot shadow the Windows ones.
  `CMakeLists.txt` differs from Urbz's only in the title comment, `project()` name
  and the `add_xbe_image(SimsBustinOut default.xbe game.toml …)` target name —
  matching `GAME_EXE`/`GAME_XBE` in the `.bat` files. `config/game.toml:7` selects
  the new `xdk = 5558` data (§1), which `build.bat`'s symbols step will regenerate
  identically on Windows.

---

## 7. Verified by inspection vs genuinely untestable here

Verified by inspection (textual, reproducible from the artefacts named):

- All §3 CMake items: Windows source list, ImGui backend list, link line, `WIN32`
  flag, custom command, SHA-1 embed, `-O2` machinery, `-fuse-ld=lld`.
- Zero Windows lines removed in `main.cpp`, `handover.cpp`, `settings.cpp`,
  `system_info.cpp`, `render_thread.cpp`, `xnet.cpp`, `boot.cpp` (Relaunch);
  preprocessor balance of every changed file.
- Single-definition status of `CreateBackend`, `StartOutput`, `app::` host
  functions, `kernel::Relaunch`, and the `kbm::Message`/`NoteWheel` cross-guard.
- ImGui 1.91.0→1.91.9b: win32 backend API unchanged; DX12 legacy init overload
  retained and implemented in 1.91.9b under default config; all 112 ImGui
  identifiers used by this port present in 1.91.9b.
- `d3d12.cpp` diff is one self-contained hunk, one caller, matches `vulkan.cpp:1248`.
- `build.bat`/`release.bat` identical to the sibling port except name strings
  (2 lines and 1 line respectively).

Genuinely untestable on this machine (no Windows SDK, MSVC STL, or MSVC ABI clang):

- Whether clang-cl/clang with the MSVC STL accepts the new shared TUs. The code was
  only ever compiled with libstdc++/glibc. The classic failure class — relying on
  transitive includes that MSVC's STL does not provide — was audited by hand for
  the new files (`iocp.cpp` gets everything via `kernel.h`; `files.cpp`'s new code
  via `kernel.h`'s `<cstring>`/`<cstdint>`; `game_files.cpp` includes its own
  `<cctype>/<cstdint>/<string_view>`), and no instance was found, but "audited by
  hand" is not "compiled."
- FetchContent of SDL3 3.4.16 + ImGui 1.91.9b under the VS LLVM toolchain, and the
  `-march=x86-64-v2` / `-fuse-ld=lld` flag set under clang targeting MSVC. All
  pre-existing at the fork except the ImGui tag.
- The full recompile (`xbe_recomp` over this title, 64 partitions per
  `config/game.toml`) under a Windows-built `xbe_recomp`.
- All Windows *runtime* behaviour: D3D12 with the new SuperSampleScale path,
  `game_files.cpp`'s new FindGame selection, the six new kernel exports under the
  Windows-hosted runtime, XAudio2, the F1/F2 overlay via `imgui_impl_win32/dx12`,
  kbm via window messages. None of this has ever executed on Windows.

## 8. Prioritised: what the first Windows build/run is most likely to fail on

1. **[compile, medium] The deprecated ImGui DX12 init overload** —
   `d3d12.cpp:903` depends on 1.91.9b's legacy `ImGui_ImplDX12_Init(device, …)`
   shim. Present and functional under the shipped `imconfig.h`, but it is one
   `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` define or one more ImGui bump away from a
   hard error. Fix proactively by moving to `ImGui_ImplDX12_InitInfo` (§4).
2. **[compile, low-medium] MSVC-STL/clang-cl nits in the new shared code** —
   most exposed: `runtime/kernel/iocp.cpp` (new TU, minimal own includes),
   `runtime/kernel/files.cpp`'s new exports, and `runtime/app/game_files.cpp`'s
   rewritten `FindGame` (new `std::filesystem` recursion, `WideToUtf8` surrogate
   path that only the `sizeof(wchar_t)==2` branch exercises). No concrete defect
   found; this is the residual "never compiled with these headers" risk.
3. **[build hygiene, low] `image_decode.cpp` in the Windows link** — compiles to
   an empty TU; expect at most a warning, but it proves the Windows `REMOVE_ITEM`
   list is already incomplete. Add it (and adopt the rule: new Linux-only runtime
   files go in the list *and* self-guard).
4. **[runtime, medium] D3D12 picture geometry** — the shared SuperSampleScale
   model (`d3d8.cpp`, `state.h`, `d3d12.cpp` §5) runs on D3D12 for the first time.
   Failure mode is a mis-scaled or off-centre picture for this title's
   supersampled back buffer, not a crash. Compare against the verified Vulkan
   output on first run.
5. **[runtime, low-medium] `FindGame` selection change** — a Windows player/dev
   tree with a bare `assets/default.xbe` plus an `.iso` now boots the ISO (§1).
   Intentional, but it will look like a regression to anyone who only tested the
   fork's behaviour; also the first run of `CountGameFiles`' artefact filtering
   on Windows paths.
6. **[runtime, low] The six new kernel exports on Windows** — shared, unguarded,
   previously these calls failed; now they succeed. Only exercised when the title
   calls them; the Linux-verified logic is platform-neutral (guest memory and
   `std::filesystem`), so risk is low.

### Bottom line

The Windows *text* survived the port intact: the build definition is verbatim
where it matters (§3), the Windows branches are the fork's own code under new
guards (§1-2), and the one dependency bump has been checked symbol by symbol
(§4). The three riskiest findings are (1) the reliance on ImGui 1.91.9b's
deprecated DX12 init overload at `d3d12.cpp:903`, (2) shared code — chiefly
`game_files.cpp`'s new game-selection logic and the six new kernel exports —
that changes Windows behaviour without ever having run there, and (3) the
standing `app::platform::*` declaration-without-Windows-definition trap in
`platform.h`, safe today only because nothing on Windows references it yet.

**Would it compile today? My belief: yes** — most likely on the first attempt,
with the ImGui legacy-overload point the only identified item I would fix
*before* handing it to a Windows machine rather than after. Whether it *runs*
correctly on Windows (items 4-6 above) is a separate question that only a
Windows machine can answer.
