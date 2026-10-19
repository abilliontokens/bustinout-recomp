# Native Linux port — code review

Read-only review of the six areas in the assignment, against the tree as found. Nothing was
built, run, or launched; nothing under `out/` was touched. Line numbers are the lines in the
files at review time.

Severity: **P1** = crash, corruption, or a permanently wrong picture under a condition a
player can reach; **P2** = wrong behaviour under a plausible machine, timing, or
configuration; **P3** = edge case or hardening. **Defects** are wrong under a stated
reachable condition; **risks** work today and break when a stated assumption stops holding;
**notes** are clarity or minor robustness.

The three threads that matter throughout: the **title thread** (guest code, XDK calls), the
**render thread** (`sdk/runtime/gpu/render_thread.cpp` executing the recorded backend
commands), and the **window thread** (`main_sdl.cpp`, SDL events and ImGui's SDL side). The
SDL audio device thread and the audio watcher thread appear in §5.

---

## Defects

### D1 · P1 — A failed swapchain acquire desynchronises the frame slot from the open recording segment
`sdk/runtime/gpu/vulkan.cpp:2033-2037`, with `EnsureRecording` at `:626-633`

`Present` reaches the acquire with the frame's draws and its own uploads (`EnsureTarget`,
`UploadTarget`, `UploadGammaRamp`, `:2027-2030`) already recorded into
`frames_[frame_ % 3].cmd`, so `recording_` is true. Both failure exits then do
`frame_++` and return **without submitting or closing that segment**:

```cpp
if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { LOGW(...); presents_++; ...; frame_++; ...; return; }
if (imageIndex >= swapFbs_.size()) { LOGE(...); presents_++; ...; frame_++; return; }
```

From then on `EnsureRecording()` no-ops (it only checks `recording_`), while `Cur()` names
the *next* frame slot. Later `Cmd()` calls record into a command buffer that was never begun
(still in its post-submit state), and `Submit` (`:655-657`) calls `vkEndCommandBuffer` on it
and `vkResetFences` on a fence that may still be pending from three frames earlier. The
original slot's segment, and the per-pass framebuffers in its `garbage` list, are never
retired.

- **Trigger**: `vkAcquireNextImageKHR` returns `VK_ERROR_OUT_OF_DATE_KHR` and the retry after
  the forced resize at `:2034` *also* fails (resize storm, surface lost, compositor gone), or
  a driver returns an out-of-range image index — during ordinary gameplay, when a segment is
  open.
- **Impact**: validation errors at best, device loss or a driver crash at worst; the frame's
  uploads are silently dropped.
- **Fix**: close the open segment on the old slot before `frame_++` — `Submit(true);` while
  `frame_` still names it. On the out-of-range-index path the acquire semaphore is already
  signalled, so that submit must also consume it (`Submit(true, Cur().imageAvailable)`), or
  the next acquire of that slot re-signals a binary semaphore whose signal is still pending.

### D2 · P1 — `DeliverLate` reads `submitFences_` on the title thread while the render thread mutates it
`sdk/runtime/gpu/vulkan.cpp:2145-2146` (reader) against `:663-667` (writer);
contract at `sdk/runtime/gpu/backend.h:35-38`, split at `sdk/runtime/gpu/render_thread.cpp:240-249`

`backend.h` documents `DeliverLate` as "any thread", and the threaded wrapper calls it
directly on the **title thread** (`render_thread.cpp:241`) while every `Submit`, `Draw`,
`Present` and `RecordLate` runs on the **render thread** (`render_thread.cpp:517-523`).
`DeliverLate` does `submitFences_.find(l.fence[i])` on a plain `std::unordered_map`; `Submit`
concurrently inserts (`:664`) and erases (`:666-667`) in the same map. A `find` racing a
rehash or erase is undefined behaviour — a torn node read can crash the process or return a
wrong fence, after which `DeliverLate` maps `l.rbm[best]` (`:2153`) and copies a read-back
the GPU has not finished into guest memory.

- **Trigger**: any `D3DLOCK_NOOVERWRITE` lock (`sdk/runtime/xdk/d3d8.cpp:557`) overlapping
  any submit — routine during gameplay, not an edge case.
- **Fix**: `DeliverLate` must not touch the map. Record the owning frame slot (or the
  resolved `VkFence`) in `Late` when `RecordLate` records the copy — it is already written
  under `lateLock_` on the render thread — and test that handle. If the map must stay, guard
  every access (including `RecordLate`'s at `:2172`) with one mutex. The D3D12 backend avoids
  the shared map entirely (`d3d12.cpp:3893`, `GetCompletedValue()`).

### D3 · P2 — A not-yet-submitted late copy is treated as finished, and then never delivered
`sdk/runtime/gpu/vulkan.cpp:2145-2146` and `:2186`

`RecordLate` records the copy into the open segment and only then stamps the slot with the
*future* submission number, `l.fence[slot] = submitCounter_ + 1`. Until the render thread's
next `Submit`, that number has no entry in `submitFences_` — and `DeliverLate` reads a
missing entry as `finished = true`. It then copies whatever the slot's buffer already holds
(the previous cycle's pixels, or uninitialised memory on the first cycle) into guest memory
and sets `l.delivered = l.fence[best]`, so when the real copy lands it is never delivered.

- **Trigger**: the title thread locks the same surface `NOOVERWRITE` again after
  `RecordLate` has run on the render thread but before that segment is submitted — the two
  threads run concurrently by design.
- **Fix**: a missing entry must mean "not known finished". Give each slot an explicit state
  (`recorded` → `submitted` once a `Submit` passes its number → `delivered`), set by the
  render thread, instead of inferring state from map membership.

### D4 · P1 — A truncated shader-cache file is accepted, and the failure is silent and permanent
`sdk/runtime/gpu/spirv.cpp:55-72`, consumed at `sdk/runtime/gpu/vulkan.cpp:1004-1010`

`WriteWords` truncates the `.spv` in place and never checks the result of the write.
`ReadWords` accepts any positive 4-byte-aligned file whose first word is the SPIR-V magic —
and the magic is the first word written, so every plausible partial write (crash, kill,
power loss, disk full during a first-run cache fill, two instances filling the same cache)
passes validation with truncated garbage behind it. `CompileHlslToSpirv` then returns
*true*, so `vkCreateShaderModule` is called on the garbage; its `VkResult` is unchecked
(`vulkan.cpp:1007`), the resulting null module is cached in `modules_` (`:1010`), and every
draw using that shader is skipped from then on. The compile-failure log at `:1008` never
fires, because compilation "succeeded".

- **Trigger**: one interrupted first-run cache write. The bad file persists across runs
  until the player deletes `shadercache/` by hand.
- **Impact**: missing geometry or a black scene with no diagnostic pointing at the cause.
- **Fix**: write to a per-process temporary name, check `f.good()` after the write, then
  `std::filesystem::rename` over the target (atomic in one directory). Validate more than
  the magic on read (at least the 5-word header and version word; ideally a stored word
  count/CRC). Check the `vkCreateShaderModule` result, log it with the shader hash, do not
  cache a null module for a *device* failure, and delete the on-disk entry that produced
  invalid SPIR-V so the next run recompiles.

### D5 · P1 — `FinishHandover` writes to a socket whose peer may be gone, with SIGPIPE at its default disposition
`sdk/runtime/app/handover.cpp:119-131`; the peer's deadline at `sdk/runtime/kernel/boot.cpp:361-372`, its exit at `:418`

The old instance waits 15 s for the handover byte, then `std::_Exit(0)` closes the accepted
connection. Nothing in the tree ignores SIGPIPE for the *new* instance's socket write (the
only silencing is around the old instance's release-pipe write, `boot.cpp:337-347`), so if
the new instance takes longer than 15 s to reach `FinishHandover` — first-run shader
compilation, a slow disc, a loaded machine, a debugger — or the old instance crashes, the
`write()` at `handover.cpp:123` raises SIGPIPE and kills the new instance at its first
frames. The `n <= 0` branch right below already handles `EPIPE` correctly; only the signal
is missing.

- **Fix**: use `send(g_handover_fd, p, left, MSG_NOSIGNAL)`, or ignore SIGPIPE once at
  process start in `main()` (`main_sdl.cpp`), which also covers every other socket write in
  the process.

### D6 · P2 — Vulkan memory-type and allocation failures fall through to null-pointer use
`sdk/runtime/gpu/vulkan.cpp:531-534` (`FindMemory`), `:543-559` (`CreateImage`),
`:731-740` (upload rings), `:1807-1821` (`ReadBackImage`), `:2173-2182` (`RecordLate`)

`FindMemory` returns type **0** when no memory type satisfies the request. Type 0 may not be
in the resource's `memoryTypeBits` at all (the bind at `:555`, `:1815`, `:2181` is unchecked
and then undefined), and for the `HOST_VISIBLE|HOST_COHERENT` requests a device-local type 0
makes `vkMapMemory` fail — also unchecked, so `ReadBackImage` memcpy's from a null pointer
at `:1820`. The same pattern at init is worse: if any of the three 64 MiB upload rings fails
to create, allocate, bind, or map (`:734-740`, all unchecked; 192 MiB of host-visible memory
in total), `Init` still returns true and `UploadAlloc` hands out `f.map + o` — pointer
arithmetic on null — and the first draw's `memcpy` (`:1429`, `:1673`) faults.

- **Trigger**: a device with no host-coherent memory type (the coherent query is the one
  with no guaranteed match), or host-visible heap pressure/fragmentation on an
  integrated GPU at startup. Exactly the "different GPU or driver" class that this machine
  cannot exhibit.
- **Fix**: `FindMemory` returns `UINT32_MAX` on no match and every caller bails cleanly
  (destroy the partial resource, return false/null). Check create/allocate/bind/map results
  in `CreateImage`, the init rings (fail `Init`), `ReadBackImage`, and `RecordLate`. For
  read-back/upload memory, fall back from `HOST_COHERENT` to `HOST_VISIBLE` plus explicit
  flush/invalidate rather than to an arbitrary type.

### D7 · P2 — A failed `vkQueueSubmit` leaves an unsignalled fence marked pending, and the next wait hangs forever
`sdk/runtime/gpu/vulkan.cpp:655-668`, waited on at `:651-653`

`Submit` ignores the results of `vkEndCommandBuffer`, `vkResetFences`, and `vkQueueSubmit`,
then unconditionally sets `fencePending = true` and registers the fence. If the submit
fails — `VK_ERROR_DEVICE_LOST`, out of memory — the fence was reset and never submitted, so
it will never be signalled, and the next `WaitFrame` blocks in
`vkWaitForFences(..., UINT64_MAX)`. A recoverable driver error becomes a hard hang with no
log line naming it.

- **Fix**: capture each result; on failure, log it, leave `fencePending` false, do not
  register the submission in `submitFences_`, and do not advance `submitCounter_`. The
  backend interface returns `void`, so at minimum the failure must be logged once, loudly,
  instead of converted into a silent infinite wait.

### D8 · P2 — Binding a file to a completion port races I/O completing on the same file
`sdk/runtime/kernel/files.cpp:591-600` (write) against `:483` + `:458-466` (read)

`NtSetInformationFile` case 30 assigns and resets `f->completion_port` (a `std::shared_ptr`)
and `f->completion_key` holding no lock. `Transfer` holds `f->io` from `:483` through its
`return Finish(...)`, and `Finish` reads `f.completion_port` at `:466`. One guest thread
binding or unbinding `FileCompletionInformation` while another guest thread's read or write
on the same handle completes is a data race on the `shared_ptr` itself: torn control-block
pointer or refcount skew, up to the port being destroyed while `File` still points at it.

- **Fix**: take `std::lock_guard<std::mutex> lock(f->io);` at the top of case 30, covering
  the reset, the assignment, and the key. That is the mutex the reader already holds, and
  the lock order (`f->io` → handle-table lock via `FromHandle`) matches the existing order
  in `Transfer`.

### D9 · P2 — The overlay reports a widened frame target that the Vulkan backend never builds
`src/common/overlay.cpp:195-199`, used at `:243-246` and `:471-475`;
the backend at `sdk/runtime/gpu/vulkan.cpp:920-927`

`FrameTarget` multiplies the guest width by `app::AspectWiden()` and its comment claims to
mirror how the target is built. On this backend `WidenedGuestWidth()` returns the
*unwidened* `guestW_` — Hor+ is deliberately not implemented, and `WidensFrame()` says so —
so with an ultrawide output and `ultrawide` on, the F1 Quality note and the F2 "render" row
state a target wider than the one that exists (at widen 4/3 and scale 3: 2560×1440 shown,
1920×1440 built). Wrong facts on exactly the axis this port keeps regressing on.

- **Fix**: drop the widen factor on the Vulkan path (or ask the active backend whether it
  widens before applying it), and correct the comments at `overlay.cpp:28-31` and `:194`.

### D10 · P2 — Linux `--instance N` is silently parsed as instance 0
`sdk/runtime/app/main_sdl.cpp:259-266`

```cpp
if (const char* p = std::strstr(g_argv[i], "--instance")) return std::max(0, std::min(8, std::atoi(p + 10)));
```

For `--instance 2` (separate argv entries) `p + 10` is the empty string; for
`--instance=2` it is `"=2"`; both `atoi` to 0. Only the glued `--instance2` works, where the
Windows side parses the documented spaced form. A second copy launched the documented way
runs as instance 0 and collides with the first on the log name, the `hdd/` folder, and the
xnet bind address.

- **Fix**: parse pairwise (`argv[i] == "--instance"` → `atoi(argv[i + 1])`), and accept
  `--instance=N` as well.

### D11 · P2 — `SDL_GetMouseState` is called from guest polling threads; SDL reserves it for the main thread
`src/common/input_kbm.cpp:258-263`, reached via `src/common/game_info.cpp:141-144`,
`sdk/runtime/core/input.cpp:110`, `sdk/runtime/xdk/xapi.cpp:97`

Linux `Collect` runs inside the pad hook, which `input::Poll` calls on whichever **guest
thread** is running `XInputGetState` — never the SDL window thread. `SDL_GetMouseState` is
documented main-thread-only (its cached position/button state is written by the window
thread's event pump), so every poll reads that state unsynchronised: a torn x/y pair or a
button word from a different instant than the anchor taken under `g_drag_lock`, i.e. camera
jumps and phantom stick clicks, plus undefined behaviour inside SDL in checked builds.
`SDL_GetKeyboardState` at `:237` is explicitly any-thread-safe and is fine.

- **Fix**: cache the mouse position and buttons on the window thread (the way
  `platform::ClientSize` is already cached there) and have `Collect` read those atomics;
  keep the drag-anchor logic unchanged.

### D12 · P3 — Partially off-surface `CopyRects` rectangles reach `vkCmdCopyImage` unclipped
`sdk/runtime/gpu/vulkan.cpp:1853-1863`

In the equal-size copy path the destination offset is checked only for negativity
(`:1860`), not for lying inside the destination: `dstT->w - uint32_t(region.dstOffset.x)`
at `:1862` underflows when the offset is past the edge, leaving an extent that runs off the
image. The source side has no remaining-extent clamp at all (`:1858` clamps against the
full width, not `srcT->w - srcOffset.x`), and a negative guest coordinate goes through
`int32_t(uint32_t(r.left) * srcScale)` (`:1854`), an out-of-range float→int conversion.
The result is an invalid `VkImageCopy` — validation error, dropped or partial copy,
driver-dependent.

- **Trigger**: a title `CopyRects` whose rectangle or destination point is partly outside
  either surface, which the D3D side clips.
- **Fix**: intersect each rectangle with both surfaces in guest coordinates *before*
  scaling, skip empty intersections, then scale the clipped result.

### D13 · P3 — Extreme guest timeouts overflow the deadline conversion and return `STATUS_TIMEOUT` at once
`sdk/runtime/kernel/iocp.cpp:124-126`, in `ToDeadline` at `sdk/runtime/kernel/objects.cpp:200-204`

For a negative (relative) timeout, `ToDeadline` computes `now + (-due) * 100` in signed
64-bit. `due == INT64_MIN` makes `-due` overflow outright; any relative timeout beyond
roughly 292 years overflows the `* 100`. Both wrap to a deadline in the past, so
`NtRemoveIoCompletion` returns `kTimeout` immediately for what the guest asked to wait
(essentially) forever on. The same helper backs the other kernel waits, so the exposure is
not IOCP-only.

- **Fix**: saturate in `ToDeadline`: clamp the relative span (e.g. anything beyond a day,
  and `INT64_MIN` in particular) to `INT64_MAX - now` instead of multiplying unchecked.

---

## Risks

### R1 · P2 — `Settings::aspect_ratio` is rewritten on the render thread while guest threads read it
`src/common/overlay.cpp:315-319` (write), `sdk/runtime/app/settings.cpp:44-49` and `:177-185` (reads)

Apply assigns `s.aspect_ratio` (a `std::string`) with no lock. `ChosenAspect()` compares
that string from guest threads (`Widescreen()`/`AspectWiden()` via the XDK display-flag
paths). The comment at `overlay.cpp:316-317` argues safety from short-string storage — the
buffer indeed never moves for these ≤4-char values — but the length/bytes store is still a
data race: a reader can observe the new bytes with the old length ("auto"→"4:3" read as
"4:3o"), miss every table entry, and answer "auto" for one query. Formally UB; practically
a one-frame wrong aspect answer at the moment Apply is clicked.

- **Fix**: publish the choice as an atomic index/enum that `ChosenAspect()` reads, or route
  both sides through the cached atomic `Shape` fields `Shaped()` already maintains.

### R2 · P2 — ImGui/Vulkan shutdown runs while the render thread may still be inside a frame
`sdk/runtime/app/main_sdl.cpp:463-474`; the render thread's ImGui use at
`sdk/runtime/gpu/vulkan.cpp:1943-1953`

After the event loop quits, `vkDeviceWaitIdle` + `ImGui_ImplVulkan_Shutdown()` run with no
`UiLock` and with nothing having stopped the guest/render threads (the process only dies
later, in `kernel::Shutdown`'s `_Exit`). `vkDeviceWaitIdle` drains *submitted* work; it
does not stop the render thread from beginning a new `RenderUi`, which takes `UiLock` and
builds ImGui draw data into backend command buffers. Closing the window mid-run can
therefore destroy ImGui's Vulkan objects under a frame that is building them —
intermittent exit-time crash or device-lost noise.

- **Fix**: quiesce first — set a stop flag the present path observes and wait for the
  render thread to leave the frame (or join it), then take `UiLock` around the shutdown
  calls.

### R3 · P2 — `renderFinished` semaphores are per frame slot, but the swapchain may hold more images than slots
`sdk/runtime/gpu/vulkan.cpp:723-724` (creation), `:854` (image count), `:2051-2054` (use)

The swapchain is created with `max(kFrames=3, caps.minImageCount)` images, but there are
only three `imageAvailable`/`renderFinished` pairs. Where `minImageCount` is 4 (common on
Wayland), a fourth present can still be queued in the presentation engine — its wait on
`renderFinished[i]` not yet consumed — when slot `i` comes round again and `Submit`
signals that semaphore a second time. Signalling a binary semaphore whose previous signal
has not been waited on is a spec violation; drivers differ in what it costs. The fence wait
at `:2070` does not cover this, because a fence signals when the *submission* completes,
which is before the present engine necessarily consumes the semaphore. A failed
`vkQueuePresentKHR` (`:2055-2057`) leaves the same signalled-but-unconsumed state behind.

- **Fix**: one `renderFinished` per swapchain image, created/destroyed with the swapchain
  and selected by `imageIndex` — an image cannot be re-acquired until its present consumed
  the semaphore, which makes the reuse race-free by construction. For the symmetric `imageAvailable` case, wait the slot before the acquire at `:2033` only
  when no segment is open (`if (!recording_) WaitFrame(Cur());`) — in frames where nothing
  was recorded, `EnsureRecording`'s fence wait never ran, and running garbage is safe
  precisely because no segment is open.

### R4 · P2 — Depth-attachment barriers wait only on the early fragment-test stage
`sdk/runtime/gpu/vulkan.cpp:580-585`

`stageAccess` maps `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` to `EARLY_FRAGMENT_TESTS` only.
Depth/stencil writes also complete in `LATE_FRAGMENT_TESTS`, so a transition *away* from a
depth attachment — sampling a depth target rendered earlier in the same segment
(`Feedback`, `:1195-1202`), or the transfer-source transition before a depth read-back
(`:2183-2185`) — can be ordered before the late writes land and read stale depth.

- **Fix**: `s = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT`
  for that layout, on both the source and destination sides.

### R5 · P3 — The shader cache ignores which compiler produced it
`sdk/runtime/gpu/spirv.cpp:208-209` (key), `:197-204` (folder), `sdk/runtime/gpu/vulkan.cpp:758-763` (device key)

The disk key is source + entry + stage, in a folder keyed by adapter and driver. Neither
DXC's version nor glslang's is mixed in, so updating, installing, or removing
`libdxcompiler.so` silently reuses the other compiler's SPIR-V — a fixed codegen bug stays
cached, and two instances with different compiler availability write different bytes to the
same file name (which is what makes D4's race reachable). The output is layout-compatible
between the two compilers, so this is staleness, not wrong bindings.

- **Fix**: mix a compiler identity/version string into the key seed or the folder name.

### R6 · P3 — Smaller SPIR-V fragilities
- `g_compiler` is a plain `std::string` written outside `g_lock` (`spirv.cpp:142`, `:190`)
  while `SpirvCompilerName()` reads it under the lock and returns a pointer into it
  (`:251-254`). No caller exists today, so this is unreachable — but the API invites one
  from another thread. Make it an atomic enum, or return the string by value under the lock.
- If the first `dlopen("libdxcompiler.so")` succeeds but `dlsym("DxcCreateInstance")` fails
  (stripped or wrong-architecture library), the cached static returns null forever and the
  `/opt/dxc` and `/usr/local/lib` candidates are never tried (`:78-86`); no `dlerror()` is
  recorded anywhere, so "why was DXC skipped" is undiagnosable. On `dlsym` failure,
  `dlclose` and try the next candidate; surface the `dlerror()` text through `*error` once.
- glslang is asked to validate (`options.validate = true`, `:186`) but given a null logger,
  so a validation failure leaves populated `words` with valid magic and reaches
  `vkCreateShaderModule` — the D4 silent-skip path again. Pass a `spv::SpvBuildLogger`
  and treat a non-empty log as failure.

### R7 · P3 — The swapchain render pass outlives a change of swapchain format
`sdk/runtime/gpu/vulkan.cpp:865-871`, `:898-905`, `:918-921`

`swapPass_` is created once (`if (!swapPass_)`) and deliberately survives swapchain
recreation, while `swapFormat_` is re-chosen on every `CreateSwapchain`. If the chosen
format ever differs after recreation (display move, driver change), the new framebuffers
pair a mismatched format with the old pass, and the `renderPass` already handed to ImGui at
init no longer describes the swapchain.

- **Fix**: rebuild the pass when the format changes (and re-initialise ImGui's Vulkan side
  with it), or assert/log that the format is stable across recreations.

### R8 · P3 — Several Vulkan creation failures are cached or go unchecked
`sdk/runtime/gpu/vulkan.cpp:812-837`, `:543-559`, `:1146-1162`, `:1026-1091`

- Device scoring never checks `VK_KHR_swapchain` support; the best-scoring device is
  chosen and `vkCreateDevice` then fails outright, where a lower-scored device that does
  support it would have worked. If no depth format is supported, `depthFormat_` silently
  keeps its `D24_UNORM_S8_UINT` default (`:833-837`). Query both during scoring.
- `CreateImage` treats an image as valid if the `VkImage` exists, even when view creation
  failed (`Valid()` tests only `image`), so a null view can reach a framebuffer or
  descriptor. `UploadTexImage` returns early on an allocation failure *after*
  `BindGuestTexture` has stored the new signature (`:1185`), leaving the image in
  `TRANSFER_DST_OPTIMAL` while its descriptor says `SHADER_READ_ONLY`, never retried until
  the content changes. Failed pipeline/shader-module creations are cached permanently
  (`:1010`, `:1091`), so one transient out-of-memory makes a shader or PSO dead until
  restart. Check the results, retry on the next bind, and only store the signature after a
  completed upload.

### R9 · P3 — Cheat entry validates the options object, then dereferences three other addresses unchecked
`src/game/cheats.cpp:112-116`, `:139-148`, `:170-175`

`GuestPtr` is (correctly) consulted for `kOptions`, but `GameMask`/`InGame` then read
`kLatch` (0x3DCF70, a different page) through `HostPtr` with no committed-page check, and
`EnterPending` additionally reads `kPadManager` and `R32(ctx.ecx + 0x1E4)` unvalidated.
All four are image statics committed with the XBE load, and `Toggle` only queues after
`InGame()` already read the latch successfully — so this works today; it is one decommit or
one earlier-than-expected F1 frame away from a host fault on the render or title thread.

- **Fix**: one `GuestPtr(kLatch, 1)` and `GuestPtr(kPadManager, 4)` beside the existing
  check, and validate `ctx.ecx` before the `+0x1E4` read in `EnterPending`.

### R10 · P3 — `SIMSBO_STEP`'s pad state is shared between polling threads without synchronisation
`src/common/game_info.cpp:34-45`, `:59-60`; the caller at `sdk/runtime/core/input.cpp:110`

`StepPad`'s `held` string and its two timestamps are plain statics, while `input::Poll`
invokes the pad hook outside `input::g_lock` and the title may poll from more than one
guest thread (`AutostartPad`, `:106-108`, correctly uses atomics). Dev-harness only
(`SIMSBO_STEP` set), but two concurrent polls race `held = token` against `held.rfind` —
UB in the test rig. Guard the three statics with a mutex; polls are ≥50 ms apart by design.

### R11 · P3 — The frame tick's fixed-step byte is not restored if the guest unwinds through the hook
`src/game/hooks.cpp:158-165`

`sub_00123430` writes `this+5 = 1`, calls the recompiled body, and restores the saved byte
on the next line. A guest exception unwinding through this host frame (the runtime does
implement guest throws as host unwinds) skips the restore, leaving the byte set outside the
tick, where the tick-count getter at `sub_001233F0` then returns the raw count instead of
1. Same thread, no race — just a stuck flag. Use a scope guard with a destructor.

### R12 · P3 — A guest-built queue object can never be used as a completion port
`sdk/runtime/kernel/objects.cpp:96-100` against `sdk/runtime/kernel/iocp.cpp:25-38` and `:82-84`

`FromPointer` rebuilds a type-4 guest queue header as a plain `Object(ObjType::Queue)`,
but `PortFromHandle` and `PostIoCompletion` require a `dynamic_pointer_cast<IoCompletion>`.
Today only `NtCreateIoCompletion` mints queue handles, so this is unreachable; the day a
guest-created queue gains a handle, `NtSetIoCompletion`/`NtRemoveIoCompletion` answer
`STATUS_OBJECT_TYPE_MISMATCH` and `PostIoCompletion` silently drops every packet. Have
`FromPointer` case 4 construct the packet-carrying type, or hold the deque on the shared
base. (Relatedly, `files.cpp:597` checks the `ObjType` tag where the post checks the
dynamic type — one test should be used in both places.)

### R13 · P3 — Hand-edited settings and the host XBE are read without bounds
`sdk/runtime/app/settings.cpp:71-85`; `sdk/runtime/app/game_files.cpp:136-141`

`Load` converts TOML integers straight into `uint32_t` fields: `width = -1920` wraps to
~4.29 billion and reaches window creation as a negative `int`; `fps_cap = 4000000000`
makes the pacing division yield 0, i.e. silently uncapped. `ReadHostFile` slurps the whole
of `assets/default.xbe` with no size check, so a wrong or multi-GB file there is read fully
into memory (and hashed) at boot. Both need a hostile *local* file, hence P3: validate
ranges in `Load` (dimensions 0 or ≤16384, `render_percent` ~10-400, `fps_cap` ≤1000,
`aniso` ≤16) and cap the XBE read (e.g. 256 MB) via `file_size` before reading.

### R14 · P3 — The audio callback under-fills when the device period exceeds 30 ms
`sdk/runtime/audio/sdl_audio.cpp:54-72`

`OnGet` supplies at most `kMaxChunksPerCallback` (3) 10 ms chunks per call, however large
`additional_amount` is; SDL pads the shortfall with silence and does not re-call within the
period. On a device whose effective period is a 2048-frame quantum (42.7 ms — seen on some
PipeWire/Pulse configurations) that is a ~10 ms gap of silence every period: rhythmic
crackle that the stall detector at `:167-173` cannot see, because pulls keep arriving. The
3-chunk cap mirrors XAudio2's *poll* lookahead, but this is a pull model where the stream's
own buffer already bounds latency.

- **Fix**: loop until `additional_amount` is satisfied (SDL's request is bounded by its
  own buffer), or raise the cap to cover the largest expected period.

---

## Notes

- **The init log claims DXC unconditionally.** `vulkan.cpp:785` logs "HLSL->SPIR-V by DXC
  (libdxcompiler) with glslang fallback" whether or not DXC exists, and
  `SpirvCompilerName()` (`spirv.cpp:251-254`) has no callers. On a DXC-less machine the log
  misattributes every shader. Log the real compiler after the first compile, or drop the
  dead accessor.
- **The shader cache never shrinks.** Old `shadercache/<adapter>-<driver>/` folders
  accumulate across driver updates with no pruning (`spirv.cpp:197-204`). The 64-bit key
  itself is fine (source+entry+stage, well mixed); a collision would be undetectable
  because no hash is stored inside the file, but at this shader count that is theoretical.
- **Audio watcher log spam.** With no playback device at all, `OpenStream`'s uncapped
  `LOGE` (`sdl_audio.cpp:94`) repeats every 500 ms forever (`:158-160`). Use `LOG_FIRST`
  or back off. The `!reopen` test at `:167` is dead (`reopen` is always false there).
  `Output` is leaked by design and `Watch` is a detached `[[noreturn]]` thread — fine for a
  process-lifetime mixer, worth a comment only.
- **`KQUEUE.CurrentCount` carries the packet count** (`iocp.cpp:45`, `:139`). On NT that
  field counts threads servicing the queue; the outstanding count already lives in
  `SignalState`. Nothing observed reads it, but guest code inspecting its own queue body
  would see the wrong value. Also, `NtQueryIoCompletion` has no implementation
  (`ordinals.cpp:54`) and would trap if a title imported it — this title does not.
- **Persisted-display corner cases** (`boot.cpp:58-70`, `:81`): if the pixel read fails
  after `AllocContiguous`, the ≤8 MB block is never freed, and `SavePersistedDisplay`
  writes `g_picture.size()` uncapped while the loader rejects anything over 1040 bytes —
  a long picture path silently disables the kept display. Across `Relaunch`, only the sync
  pipe is `O_CLOEXEC` (`boot.cpp:260`); other long-lived fds (log, ISO, SDL) are inherited
  by the new instance at `execv` and stay open in it. Mark long-lived opens `O_CLOEXEC`.
- **The handover listener accepts any first peer.** `boot.cpp:387-402` closes the listener
  after the first `accept4`, and closes a connection that EOFs without a byte — after
  which no legitimate byte can ever arrive, and the parent just waits out the deadline.
  The name embeds the child PID and is guessable by any same-user process. Low risk (same
  trust domain), but re-listening after a byteless EOF costs little.
- **Overlay cosmetics.** `AspectName` (`overlay.cpp:179-190`) names 64/27 as "21:9" but the
  app's own "21:9" is 21/9, so the F2 row shows "2.33" for the setting and "21:9" for a
  2560×1080 auto output. The Save button runs `app::Save()`'s file write inside the ImGui
  frame (`overlay.cpp` Save → `settings.cpp:123-126`), against the file's own "nothing here
  may block" rule — a few hundred bytes, but it is the one blocking call in the panel.
- **Frame-step bookkeeping.** `g_dt` (`hooks.cpp:12`, `:133`) is stored every frame and read
  nowhere in-tree; it is documented scaffolding for future wrappers — delete it if they
  never land. The running total at `[0x35D230]` now accumulates 1 ms ticks, so its 32-bit
  wrap arrives at ~49.7 days of continuous play instead of ~828; its only other reader
  (guest 0x129E49) is unidentified, so this is a horizon to note, not a bug.
- **PNG tolerance.** `image_decode.cpp` accepts over-length tRNS for colour types 0/2 and
  compares the full 16-bit key against 8-bit samples, so an out-of-range key simply never
  matches and the pixel stays opaque (`:139-141` area, `:264`). Memory-safe, spec-loose.
- **Folder completeness count.** `game_files.cpp:185-186` tests every path component
  including the final filename against the host-artefact names, so a game data file
  literally named `log` or `settings` is excluded and can mark a complete folder
  "incomplete"; a mid-walk I/O error (`:175`) ends the count silently. The count is
  advisory, so both are diagnostics-grade. `Save()` in `settings.cpp` likewise never
  checks its `ofstream`.

---

## What is clean

### 1 · Vulkan backend (`vulkan.cpp`, `vulkan.h`)
- **The descriptor-pool fix (sdk `4888bc1`/`e809cd4`) is complete.** The pool is sized at
  exactly `kSetsPerSegment` sets with per-type counts matching the one layout
  (`:727-729`); `AllocDrawSet` is the only allocator of draw sets, checks its result, and
  logs-and-drops rather than recording a bad set (`:1380-1382`); `EnsureSetRoom` submits a
  full segment *outside* any pass (`:649`) and is called before every in-pass allocation
  site — `Draw` `:1670`, every blit via `PrepareBlitSource` `:1769`, `DepthCopyDraw`
  `:1891`. Both allocators open the segment before handing anything out and are
  generation-checked (`:626-645`, `:673`, `:1378`). The only other
  `vkAllocateDescriptorSets` consumer is ImGui's own pool (`:731-736`,
  `FREE_DESCRIPTOR_SET_BIT`, separate lifetime). No allocation path remains that can
  silently drop the tail of a frame the way the original bug did.
- **Guest read-back bounds hold.** `Sync` and `DeliverLate` refuse low physical memory
  first (`LowPhysical`, `:215`, via `memory.h:9-13` = `[0x80000000, 0x80100000)`), then
  refuse any surface whose pitched size exceeds the declared size (`ReadBackFits`,
  `:205-213`, 64-bit throughout); the row loops write `(gh-1)·pitch + gw·4 ≤ pitch·gh ≤
  size` bytes (`:2132`, `:2155`). Nothing writes below `0x80100000` or past the surface's
  bytes; the diag-only capture paths write only to host files.
- **No layout transition is recorded inside a render pass.** Every `Transition` call site
  was traced: target transitions happen in `BeginTargetPass` before `vkCmdBeginRenderPass`
  (`:1396-1397`), blit sources are transitioned in `PrepareBlitSource` before the
  destination pass begins (`:1760-1772`, with the comment explaining the RADV failure it
  prevents), and draws do all uploads/transitions before `:1686`. The swapchain pass's
  external dependencies (`:857-871`) correctly order acquire → write → present.
- **Per-frame lifetime is sound apart from D1.** Deferred destruction runs when the owning
  slot's fence has completed (`:651-654`), in-order submission post-dates every older use,
  and the upload ring is only reset after its fence wait (`:626-631`). Swapchain recreation
  flushes and idles the device before destroying anything (`:906-919`).

### 2 · SPIR-V path (`spirv.cpp`)
Apart from D4/R5/R6: DXC COM lifetimes are correct on every path (utils/compiler/result/
messages/blob all released, short-circuit creation handled, `:96-144`); a missing
`libdxcompiler.so` degrades cleanly to glslang with no crash; the two compilers are asked
for identical binding shifts (b0, t+2, s+6, u+10) and the same Vulkan 1.2/SPIR-V 1.5
environment, so the fallback produces layout-compatible modules; glslang process init is
`call_once` and its objects are correctly scoped; failure caching (`g_cache[key] = {}`)
stops a bad shader being recompiled every draw, and genuine compile failures propagate a
combined DXC+glslang message that the caller logs with the full source (`vulkan.cpp:1008`).
The cache folder *is* keyed by vendor, device, driver version, and pipeline-cache UUID
(`vulkan.cpp:758-763`), so a cache cannot be read across adapters or across a driver update
— only across compiler changes (R5).

### 3 · App layer (`main_sdl.cpp`, `platform.h`, `settings.cpp`, `game_files.cpp`, `splash.cpp`, `handover.cpp`, `image_decode.cpp`, `system_info.cpp`, `boot.cpp`)
- **`image_decode.cpp` is clean — this was the file that had to be.** Signature check;
  every chunk length tested against the remaining file before use (`:90-93`); CRC verified
  before any payload is trusted (`:99-104`); IHDR fully validated (non-zero, ≤16384/side,
  ≤2²⁵ pixels, compression/filter/interlace/bit-depth/colour-type whitelists) before
  anything sized by it exists (`:115-145`); PLTE bounded to 768 bytes and a multiple of 3;
  the IDAT total is bounded by the 64 MB file cap; inflate must end at `Z_STREAM_END` with
  exactly `h·(w·channels+1)` bytes produced, rejecting truncated and overlong streams alike
  (`:186-193`); filter reconstruction uses the correct `bpp = channels` for 8-bit data with
  a null-guarded previous row and rejects unknown filters (`:204-229`); palette indices are
  bounds-checked with a hard failure (`:269-272`); unknown *critical* chunks are rejected
  rather than skipped. All size arithmetic that matters is 64-bit. No overflow or
  out-of-bounds path was found.
- **Handover, apart from D5**, is careful: abstract-socket name length-checked against
  `sun_path` (`handover.cpp:81-94`), "no peer" treated as an ordinary first launch, the fd
  closed on every path, `FinishHandover` once-only via `exchange` and opening the window
  *before* signalling so visibility never depends on the peer. In `boot.cpp` the parent
  binds and listens *before* releasing the child, the child between `fork` and `execv`
  calls only async-signal-safe functions, a dead child is reaped via pidfd + `waitpid` (no
  zombie), and every listener/connection/pidfd descriptor is closed.
- **`game_files.cpp`**: the walk does not follow symlinks (no cycles), uses `error_code`
  throughout with `skip_permission_denied`, and the extracted-folder-vs-ISO precedence and
  SHA-1 gate are sound; the ISO side streams through `kernel::Disc` rather than loading
  the image. **`splash.cpp`** (Linux) cleans up every object on every failure path.
  **`system_info.cpp`** is bounds-safe throughout. **`platform.h`** matches its
  implementations. `main_sdl.cpp` keeps all SDL calls on the window thread with atomic
  cross-thread state, apart from R2/D10.

### 4 · Kernel IOCP (`iocp.cpp`, `files.cpp`, `objects.cpp`, `kernel.h`)
Apart from D8/D13/R12, the integration is sound. Queue, `SignalState`, and the guest
`KQUEUE` mirror mutate together under `DispatchLock` in both push and dequeue
(`iocp.cpp:41-47`, `:134-147`); `SignalChanged` wakes the existing wait machinery, no lock
is held across `Wait`, and a lost wakeup is impossible because `WaitFor` re-checks
satisfaction under the lock before sleeping (`objects.cpp:213-226`). Timeout semantics are
right: `NULL` waits forever, `0` polls (the packet check happens under the lock before the
timeout is consulted), negative is relative, positive absolute (`iocp.cpp:124-156`,
`objects.cpp:200-204`). A handle of the wrong type is rejected with
`STATUS_OBJECT_TYPE_MISMATCH` by `PortFromHandle` (`iocp.cpp:25-38`) — including the
current-thread pseudo-handle — and cannot corrupt anything; packets are 16-byte PODs
copied by value; closing a port mid-wait is safe because waiters and `File` hold
`shared_ptr`s. `NtUserIoApcDispatcher` snapshots the status block before calling guest
code and maps status to a Win32 error the way `RtlNtStatusToDosError` does. The generic
wait machinery never consumes a queue (`Consume`'s default case), which is correct —
dequeue is manual.

### 5 · Audio (`sdl_audio.cpp`)
Apart from R14 and the log-spam note: the callback does no allocation, renders into a
fixed 960-float scratch, and its chunk arithmetic is exact (`kFrames·kChannels·4` =
`kChunkBytes`, `:33-35`, `:54-72`); all counters are atomics; the buffer the mixer renders
is fully initialised before use. Device loss is handled by the stall detector plus
reopen (`:155-182`), SDL migrates default-device changes itself, and `CloseStream`
detaches the callback under the stream lock before destroying anything, never from the
callback thread (`:120-131`). `StartOutput` is single-instance and `XBE_MUTE` only turns
the gain down — mixer timing is unaffected. The render callback does run the dsound mixer
(guest memory and kernel event state under its recursive lock) on SDL's device thread;
the lock order is uniform and the kernel never calls back into audio, so there is no
deadlock, only the inherent real-time-thread priority-inversion exposure of that design.

### 6 · Port code (`overlay.cpp`, `input_kbm.cpp`, `game_info.cpp`, `hooks.cpp`, `cheats.cpp`)
Apart from D9/D11 and the risks above: the frame-step hook's arithmetic is right — NaN and
negative elapsed both take the 1 ms step (`hooks.cpp:125-128`), the tick count clamps at
the engine's own 0.25 s ceiling, and writing `dt/ticks` back into `[0x35D218]` makes
`ticks × [0x35D218]` exactly the elapsed time; both wrappers keep their `SIMSBO_NO_STEP`
escape hatch. `InsertCallback` runs the guest callback on the guest thread with a null
check. Cheat entry is faithful: the ABI (`ecx` = options, one stack argument, `retn 4`),
the eight table rows and their bit meanings in `overlay.cpp:499-517` all match the game's
own construction, `EnterPending` re-checks the game's three conditions on the title thread
and restores the guest flags it borrowed, and `GuestPtr`'s range/overflow/page checks are
correct for the object it does validate. The overlay itself is bounded: display-size
guard, quarter-step style scaling, correct ring offsets, every `snprintf` target sized for
its worst case, HDR index and slider outputs constrained to renderer-valid values, and
`g_settings_up` is a proper atomic publish to the input layer. Input merging only ever
adds to a real controller's report (OR for buttons, max-deflection for axes), clamps to
the unit circle before the int16 conversion, bounds the virtual-key array, sign-extends
mouse coordinates correctly, caps a wheel spin at 450 ms, and releases held keys on focus
loss. `game_info.cpp`'s scripted pads clamp their pulse counts and only ever set bits.
