# The black household scene (former defects D2 and D3)

Recorded 2026-10-19 on this machine (3440x1440 KDE Plasma Wayland, AMD
BC-250/RADV), native Linux verbose build (`out/build/linux-verbose`, no
`NDEBUG`). Every number below names the log, capture or screenshot that
proves it; the capture evidence lives in `out/evidence/black-scene/`
(gitignored, kept on disk).

> ## ⚠ DO NOT LOAD THE VULKAN VALIDATION LAYER ON THIS MACHINE
>
> **This machine hard-froze twice, and both times the Vulkan validation
> layer was loaded.** The journal for boot −2 ends at **06:47:47** during
> run `fix1`, and the journal for boot −1 ends at **12:50:29** during run
> `house5`. Each freeze came less than 0.5 s after a periodic frame
> capture, with **no kernel message** either time (the boot command line
> has `nowatchdog`, so nothing reported or recovered). **No run without
> the validation layer has ever frozen.** Do not set
> `VK_LAYER_KHRONOS_validation` (or any `VK_LOADER_LAYERS_ENABLE` /
> validation feature) for a run on this box without the owner's explicit
> say-so. Diagnose with `XBE_PERF=1`, `XBE_CAPTURE_DIR` +
> `XBE_VK_CAPTURE_EVERY=N`, `XBE_VK_DEBUG_DRAW=1` and the capture dumps
> instead — that is how everything below was proven.

## The symptom

The loaded household — a nightclub lot with Sims — submitted correctly
(2076–2178 draws/frame) and rendered into the host render target, but the
screen stayed pure black. Menus, name entry and Create-a-Sim presented
fine through the identical present code, and the F2 ImGui panel was
visible *in the same swapchain pass* as the black scene
(`out/shots/loop3_f2.png`). Two defects were filed: **D2** (household
presents black) and **D3** (159–267 draws per frame dropped as
`nopipeline`, some Sims black silhouettes in the target). They turned out
to be one defect.

## The two reads of the same target that disagreed

The localisation instrument was a guest front-buffer capture that sampled
the *same* target image several ways inside one `Present`
(`XBE_CAPTURE_DIR` + `XBE_VK_CAPTURE_EVERY=300`): a guest-size shrink
sample (`front_NNNNN.bmp`, which blits through a descriptor set), a
second sample of the same image (`front_NNNNN_b.bmp`), and a
full-resolution copy (`front_NNNNN_host.bmp`, a plain image copy that
needs no descriptor set). Pixel means:

| Evidence | First shrink sample | Second sample | Full-resolution copy |
| --- | --- | --- | --- |
| `before_h5/` (present 9000) | **0.0 (exact black)** | 0.2468 | 0.2467 |
| `probe_h6/` (flush before the first sample) | 0.2462 | 0.2462 | 0.2462 |
| `after_house3/` (presents 4200, 4500) | 0.301 | 0.301 | 0.301 |

So the target held the scene all along (the full-resolution copy, and the
second sample, read it back correctly), while the *first* shrink sample
of the same image could read exact black — and forcing a flush before
the first sample made all three agree. Whatever the first sample needed,
something before it in the frame had run out; the flush reset it. That
ruled out the recorded prime suspects (image layout / DCC visibility,
gamma — `gamma=0` on every present line): a layout bug does not heal
because a command buffer was submitted.

## The cause: the per-frame descriptor pool held 2048 sets

The household submits ~2175 draws per frame; every draw, blit and depth
copy takes one descriptor set from the per-frame pool, which was sized
for **2048**. Part way through every household frame,
`vkAllocateDescriptorSets` began failing with
`VK_ERROR_OUT_OF_POOL_MEMORY` (VkResult −1000069000, in
`before_h5/house5.log`). The old perf counter blamed all of it on
`nopipeline`; splitting that counter three ways proved what was actually
failing in the household:

```
perf: per frame 2175 draws (2175 raw) 264 skipped 264 nopipeline (shader 0 pso 0 set 264)
```

**shader 0, pso 0, set 264**: no shader or pipeline ever failed to build —
264 draws were dropped because they got no descriptor set. 2175 − 264 =
1911, which is why the earlier F3 capture listed 1912 draws for the
frame. And the same exhaustion explained the black screen: the present
blit takes its set from the same pool, so once the pool was empty the
blit was skipped without a word and the swapchain received only the
black clear — plus the F2 ImGui panel, which has its own descriptor pool
and stayed visible, which is what made this look like a sampling or
layout bug. Menus and Create-a-Sim stay under 2048 sets and were always
correct, which is why only the household bit. D2 and D3 were one defect.

## The fix

SDK `4888bc1` (superproject `03bc027`):

- The pool is sized by `kSetsPerSegment = 8192` sets, with the matching
  descriptor counts (2 uniform + 4 combined-image-sampler + 4 sampler
  descriptors per set), so the set count is the only limit.
- A per-segment set counter (`Frame::sets`, reset with the pool in
  `EnsureRecording`) and `EnsureSetRoom()`: outside any render pass,
  before work that allocates a set, a segment that has used its pool is
  submitted — inside a pass it cannot be. Called at the top of `Draw`
  (before the constants, since a flush restarts the upload ring),
  in `PrepareBlitSource` (the present/copy blits allocate their set
  inside the destination pass), and in `DepthCopyDraw`.
- The dropped-draw counter stays split (`shader`/`pso`/`set`), and a
  failed allocation logs its VkResult and the segment's set count, so
  the cause cannot be mistaken for a pipeline failure again.

Evidence: `after_house3/` — both shrink samples at 0.301, 2181
draws/frame, 0 skipped, 0 `nopipeline`
(`out/evidence/black-scene/after_house3/house3.log`), and
`out/shots/house3_house.png`: the club, the Sims, the HUD (6:21 PM
clock, simoleons) and an in-game dialogue card on screen.
`out/shots/regress_pool_20s.png` shows the intro movie still whole and
correctly pillarboxed after the change.

## The allocation-ordering defect (found by code reading, fixed, proven)

Corroborated by the pre-fix validation run (`house4`: "Couldn't find
VkDescriptorSet …" and "uses set 0 but that set is not bound" at
135.589 s, right after `BlockOnResource` read-backs — quoted here as a
record; see the warning above before ever reproducing it that way):

`UploadAlloc` and `AllocDrawSet` read `Cur().used` / `Cur().pool`
**without** calling `EnsureRecording()`. After any `Submit` — a
`ReadBackImage`, a `Sync` flush, or the upload-reserve `Submit` inside
`Draw` — `recording_` is false. If the next `Draw` recorded nothing
before them (`TransitionRaw` returns early when the layout already
matches), its constants and its descriptor set were allocated from the
**stale** upload offset and the **un-reset** pool. Then
`BeginTargetPass`'s `vkCmdBeginRenderPass(Cmd())` ran `EnsureRecording()`,
which does `vkResetDescriptorPool` and sets `used = 0`: the draw bound a
set that had just been freed, and its constants were overwritten by
later draws. A pre-existing "flush and retry" fallback in `Draw` could
never work for the same reason — a flush does not reset the pool, only
`EnsureRecording` does.

The fix (in `sdk/runtime/gpu/vulkan.cpp`): both allocators call
`EnsureRecording()` first, so an allocation is only ever handed out by
the segment that will record its use. The audit of every other reader
of `Cur().used` / `Cur().pool` / `Cur().cmd`:

- `Draw`'s upload-reserve check read `Cur().used` unguarded; the ring
  offset is only meaningful in the open segment, so it is now
  `recording_`-gated (SDK `e809cd4`).
- `Cmd()` already calls `EnsureRecording()`; `EnsureSetRoom()` is
  already `recording_`-gated; `DeferDestroyImage`/`BeginTargetPass`
  push to `Cur().garbage`, which only runs after that frame's fence —
  not an allocation-ordering hazard.
- No barrier can be reordered by the allocator calls:
  `EnsureRecording()` records nothing itself (it begins the command
  buffer), and inside a render pass it is a no-op because a pass can
  only have been begun on an open segment. `PrepareBlitSource`'s rule —
  layout transitions are recorded *before* the destination pass begins —
  is unchanged: it still transitions (and now makes set room) outside
  the pass, and `BlitToImage`'s allocations inside the pass find the
  segment already open.
- `EnsureRecording()` cannot recurse: it only waits the frame fence and
  runs deferred destroys, none of which allocate.
- The dead flush-and-retry is gone: pool exhaustion is prevented by
  `EnsureSetRoom()` submitting the segment *before* the allocation,
  outside any pass, instead of retrying after a flush that cannot help.

### Proof the stale case is gone

Each segment `EnsureRecording` opens is stamped with a generation
(`Frame::gen` / `segmentGen_`). `CheckAllocGeneration` runs in both
allocators immediately after `EnsureRecording` and counts, as
`stalealloc` in the `XBE_PERF` per-frame line (raw count per second),
any allocation handed out against a closed or superseded segment
(SDK `e809cd4`). Same household drive both times (`SIMSBO_STEP`:
START, START, A Continue, A Sim 1, RIGHT×3, A, A; verbose build;
evidence in `out/evidence/black-scene/alloc-ordering/`):

- **Before** (the two `EnsureRecording` calls temporarily removed,
  instrumentation kept — `allocbefore.log`): the counter fired **3
  times at 48.079 s**, immediately after the `BlockOnResource`
  read-backs at 48.066–48.071 s, during the household load at present
  2869: two `stale upload allocation` and one
  `stale descriptor set allocation` ("no open segment (frame gen 2870,
  segment gen 2870, present 2869)"). That is exactly the predicted
  sequence — a read-back `Submit` closed the segment, and the next
  draw allocated before anything re-opened it.
- **After** (calls restored — `allocafter.log`): `stalealloc 0` in all
  91 perf lines and zero `vulkan: stale` lines, over the full boot →
  intro → menu → household run.

## Regression check after the ordering fix

`tools/shot_run.sh`, verbose build, `XBE_PERF=1 XBE_MUTE=1`, **no
validation layer**:

- **Intro**: `out/shots/allocintro_20s.png` — the movie whole and
  pillarboxed; the picture spans x 762..2679 of 3440, *identical* to
  `out/shots/regress_pool_20s.png` (a different movie frame — the movie
  is timed video — at the same geometry: no crop, no stretch).
- **Household**: `out/shots/allocafter_92s.png` — the club, Sims, HUD
  (6:20 PM) and the "Peter" dialogue card, as in
  `out/shots/house3_house.png`. `XBE_PERF` in the household:
  **2178 draws/frame, 0 skipped, 0 `nopipeline` (shader 0 pso 0
  set 0), `stalealloc` 0**, 113k verts, 14.2 MB upload/frame
  (`out/shots/allocafter.log`, also copied into
  `out/evidence/black-scene/alloc-ordering/`).
- Builds green: `out/build/linux-verbose` and
  `cmake --build --preset linux-release`.

The capture probe that produced the two-sample table above (a second
sample of the same target inside `PeriodicCapture`, plus its extra
present-log fields) was temporary diagnostic scaffolding and has been
removed (SDK `e809cd4`); the BMPs and logs it produced remain in
`out/evidence/black-scene/`.
