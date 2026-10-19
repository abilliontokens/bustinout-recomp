# Ultrawide and aspect ratio: what works today, what was reverted, and why the gated Hor+ is not safe yet

> **Do NOT enable `SIMSBO_HORPLUS` until every item in
> [Why the gated code is still not safe to enable](#why-the-gated-code-is-still-not-safe-to-enable)
> has been worked.** The Hor+ code in the tree today would show a broken
> picture in the household: a stretched world, a HUD a quarter too narrow,
> stretched 3-D insets and squeezed frame feedback.
>
> **That list comes from static analysis only.** It was produced on 2026-10-19 by
> reading the guest image (`out/game.exe`, in IDA) and the current source. Nothing
> was built or run for it, and **none of it has been observed at runtime**. Every
> item is a prediction from the code and has to be confirmed or refuted in game.

Recorded 2026-10-19 from the code and git history in this repository. Nothing was
built or run for this document. **Hor+ ultrawide is a known gap the owner has
asked for. It is not a design choice and it is not finished.**

## What Hor+ is

On a display wider than 16:9, Hor+ shows **more world at the sides** instead of
pillarbox bars. The vertical field of view stays the same, every object keeps its
height, and the HUD, menus and movie stay their authored size, centred. Only the
horizontal field of view grows.

## What the two settings do today (Linux / Vulkan, `SIMSBO_HORPLUS` unset)

The settings are `aspect_ratio` and `ultrawide` in `settings.toml`
(`sdk/runtime/app/settings.cpp`, defaults in `sdk/runtime/app/app.h`).
`aspect_ratio` defaults to `"auto"` and can be `auto`, `4:3`, `16:9`, `16:10`,
`21:9` or `32:9`. `ultrawide` defaults to `true`.

- **The title is projecting 16:9 already.** On any output 3:2 or wider, the
  kernel's `ExQueryNonVolatileSetting` index 8 answer reports a widescreen TV, so
  the title's own widescreen mode is on and its aspect getter `sub_001287C0`
  returns 1.7777778 (`docs/widescreen-flag.md`, `docs/guest-projection.md`). The
  640x448 picture inside the 640x480 buffer (`docs/picture-rect.md`) is an
  anamorphic 16:9 picture. A 640-column row spans 16:9 of picture width, so the
  columns are not square.
- **`aspect_ratio` changes only the aspect the finished picture is presented
  at.** `PresentSrcAspect()` in `sdk/runtime/gpu/vulkan.cpp` returns an explicit
  choice as given. For `auto` it returns what the title is actually projecting:
  `app::HostAspect()` if host widening is active, otherwise 16:9 on a widescreen
  output, otherwise 4:3 (SDK `c36cd6f`, `11e6475`; superproject `3e1e209`,
  `5151825`). `BlitFrame` fits the picture inside the window at that aspect,
  centred, never cropped and never stretched to the window's shape. The
  superproject commit `3e1e209` shows this filling a 1920x1080 window edge to edge
  in game (`out/shots/a1080_*.png`). On this machine's 3440x1440 display the code
  gives a 2560x1440 16:9 picture with bars at the sides. That is correct, but it
  does not show more world.
- **`ultrawide` has no visible effect on Linux unless `SIMSBO_HORPLUS` is set.**
  `app::HostAspect()` (`sdk/runtime/app/settings.cpp`) returns 0 unless the
  variable is set to something other than empty or `0`, `ultrawide` is on, and
  the target aspect is wider than 16:9. While it returns 0,
  `VulkanBackend::WidensFrame()` is false, `WidenedGuestWidth()` returns the
  guest width, `Target::dx` is 0, the XDK centre shift and fold scale
  (`sdk/runtime/xdk/d3d8.cpp`) are 0 and 1, and the getter override in
  `src/game/hooks.cpp` passes straight through. Every number is then the
  console's.
- The D3D12 backend in `sdk/runtime/gpu/d3d12.cpp` has its own widening path,
  driven by `app::AspectWiden()`, with no `SIMSBO_HORPLUS` gate. The Windows
  target has never been compiled on this machine (no MSVC or Windows SDK,
  `docs/toolchain.md`), so that path is unproven everywhere.

## The two reverted attempts

A partial Hor+ has shipped a visibly broken picture twice. Both times it was
taken back out.

### 1. The centre shift without the widened target (SDK `5bbdb3d`, switched off in `11ca56f`)

SDK `5bbdb3d` ("d3d/settings: aspect ratio and ultrawide Hor+") added the Hor+
centre shift to the XDK layer. On an ultrawide output it added +110 px (widen
1.344) to `c121.w` (2-D) and `c98.x` (world), but the Vulkan frame target was
never widened. Everything moved right by 110 px into a frame of the old width.
SDK `11ca56f` stopped this: `gpu::Backend::WidensFrame()` (default true, false on
Vulkan back then) means the shift is applied only when the backend really builds
a wider frame.

### 2. The staged getter override (superproject `4202a7a`, reverted in `b3b1762`)

Superproject commit `4202a7a` ("hor+: stage a - the title's aspect getter
returns the host aspect") landed the first stage on its own:

- `src/game/hooks.cpp` gained an override of the aspect getter `sub_001287C0`.
  It returned the host frame aspect whenever `ultrawide` was on or an aspect was
  chosen (`SIMSBO_NO_WIDE=1` bisected it off).
- The SDK pointer moved to a build whose Vulkan frame target was widened by the
  same factor.

The 2-D centre shift, the present aspect and the read-back centre crop were
deliberately left for later stages. That combination is broken on its own. The
world was projected and rendered into a widened target, but:

- 2-D placement still used the unwidened centre,
- the present still fitted the picture into the old 4:3 pillarbox, and
- captures and read-backs still assumed the old width.

The HUD sat left of centre and the widened picture was squeezed into 4:3. The
owner called it "really buggy".

Superproject commit `b3b1762` reverted the whole stage, both the `hooks.cpp`
override and the SDK pointer. It went back to the last state verified in game
(superproject `4fae9fc`, SDK `c3d4335`). Its commit message says Hor+ is not
abandoned: it must return **as one change, proven in the household before it is
committed**, not in stages that each ship a broken picture. The three pieces that
must land together are:

1. **The 2-D centre shift.** For world draws, the post-projection scale+offset
   fold (vertex constants c97/c98, `docs/guest-projection.md` §2) and the XDK
   viewport centre offset must move by half the gained width. The
   640x448-normalised 2-D path (c121..c124) must stay centred on the guest
   picture, or every menu, HUD element and the movie moves with it.
2. **The present aspect.** `BlitFrame`/`PresentSrcAspect()` must present the
   widened target at the widened aspect, not squeeze it back into 4:3.
3. **The read-back centre crop.** Front-buffer captures and read-backs (F3,
   `XBE_VK_CAPTURE_EVERY`) that assume the guest's 640-wide picture must take the
   centre 640 columns of the widened target (rows
   `picture_top..picture_top+picture_height`, as today). Diagnostics then keep
   showing the picture the console would have shown.

## What is in the tree now, behind `SIMSBO_HORPLUS`

All of the following is inert while `app::HostAspect()` returns 0.

- **Gate:** `app::HostAspect()` in `sdk/runtime/app/settings.cpp`. When the gate
  is on it returns the target aspect, for example about 2.389 at 3440x1440.
- **Frame target:** `VulkanBackend::WidenedGuestWidth()` and `EnsureTarget()` in
  `sdk/runtime/gpu/vulkan.cpp`. The frame target (`IsFrameTarget()`) is built
  `lround(guestH x HostAspect())` columns wide. `Target::dx` is half the gained
  columns. The whole image is cleared to black once, when the target is created.
- **Centre shift and fold scale:** `WidenedFrameWidth()` (same formula),
  `FrameCentreShift()`, `FrameFoldScale()`, `CentreVsConstant()` and
  `UpdateViewportConstants()` in `sdk/runtime/xdk/d3d8.cpp`. They shift `c98.x`
  and `c121.w` by dx and multiply `c97.x` by `wide_w / 640`. The fixed-function
  viewport constants c58/c59 get the same scale and shift.
- **Present:** `BlitFrame` presents a widened target (`t.dx != 0`) whole, at
  `(gw + 2dx) / gh`. With widening on, `PresentSrcAspect()` returns
  `HostAspect()`.
- **Read-back:** `PrepareCopySource` blits the centre `gw` columns of a widened
  target before a read-back. That is the centre crop from piece 3 above.
- **Projection aspect:** the `sub_001287C0` override in `src/game/hooks.cpp`.
  Only when the caller's return address is in `kProjectionReturns` does it
  replace the result on the x87 stack with
  `lround(height x aspect) / height`. Here `height` is the present height read
  from `[ecx + 0x248]`. `SIMSBO_NO_WIDE=1` bypasses the override.
- **Panel:** `BackendWiden()` in `src/common/overlay.cpp` reports the widening
  the active backend builds.

## Why the gated code is still not safe to enable

**Static analysis only.** This comes from the 2026-10-19 investigation (agent
`HorPlusWriteComplete`), which read `out/game.exe` through IDA and read the
current source. It wrote no code and executed nothing: no build, no game run, no
capture. The visual effects below (stretch factors, HUD width) are worked out
from the code and the numbers. None has been seen on screen.

### 1. The household camera is missing from the return-address gate

`kProjectionReturns` in `src/game/hooks.cpp` does not list the getter calls that
feed the household camera, or several other cameras. With the gate on, those
calls keep the title's 16:9. Meanwhile the frame target, the fold scale and the
present all widen. The world would then be stretched horizontally by about
2.389 / 1.778 = **1.34x** at 3440x1440.

Return addresses of getter calls that are missing from the list:

| Return address | Calling function | What it is |
| --- | --- | --- |
| `0x8E508` | `sub_0008E3C0` | household camera, reached from `sub_0008F6A0` ("start_storymode_house") |
| `0x8E577` | `sub_0008E3C0` | household camera, same path |
| `0x8E761` | `sub_0008E730` | household camera, reached from `sub_0008F6A0` |
| `0x8DE62` | `sub_0008DCE0` | camera |
| `0x8DF88` | `sub_0008DF20` | split-screen camera |
| `0x7812C` | `sub_00078000` | camera |
| `0xCF73C` | `sub_000CF710` | camera |
| `0xE761E` | (neighbourhood view) | neighbourhood camera |
| `0xD2201` | `sub_000D21A0` | camera |

### 2. Four entries in the list never reach the getter

`0x1E424`, `0x1E44B`, `0x1E5A4` and `0x1E5CB` are in `kProjectionReturns`, but
those calls are on a different object and take a stack argument. They never
reach `sub_001287C0`, so they are dead entries and should be removed. That
leaves six live entries: `0x15488`, `0x164B6`, `0x16723`, `0x16C96`, `0x1DBA4`
and `0x1DEEC`. The investigation did not dispute those six.

### 3. Four call sites must keep the title's own aspect

These getter calls must stay out of the gate and keep returning the authored
value:

- `0xA4EA8` and `0xD1678` scale the result for small 3-D previews inside menus.
- `0xAF18C` and `0xAF4F7` use it to lay out HUD objects.

Widening them would distort the previews and move or resize HUD objects. Because
the gate is a list of allowed addresses, these four must simply never be added to
it.

### 4. The HUD would come out about 25% narrower than with `ultrawide=false`

The Linux code and the D3D12 code widen the frame by different formulas:

| | Linux (`vulkan.cpp` / `d3d8.cpp` / `hooks.cpp`) | D3D12 (`d3d12.cpp`) |
| --- | --- | --- |
| Width formula | `round(480 x aspect)` | `640 x AspectWiden()` |
| At 3440x1440 (aspect 2.389, widen 1.344) | **1147** columns | **860** columns |
| Centre shift dx | (1147 - 640) / 2 = **253.5**, a half-pixel shift | (860 - 640) / 2 = **110** |
| Presented aspect | 1147 / 480 = 2.389 | 16/9 x 860 / 640 = 2.389 |

The Linux formula treats a guest column as square, but the guest's 640 columns
hold an anamorphic 16:9 picture. Presented across 3440 px:

- Linux puts the authored 640 columns, and with them the HUD, into
  640 / 1147 x 3440 ≈ 1919 px.
- `ultrawide=false` gives 2560 px (the 16:9 pillarbox).

1919 / 2560 ≈ 0.75, so the HUD is about 25% narrower than with the setting off.
The D3D12 width keeps a column the same width as in the 16:9 picture
(640 / 860 x 3440 = 2560 px), so the HUD keeps its authored size.

`BlitFrame`'s present aspect `(gw + 2dx) / gh` is correct only for the 480
formula. With the D3D12 width it must be `16/9 x (gw + 2dx) / gw`.

Three places compute the same `lround(height x aspect)` width today:
`WidenedGuestWidth()` (vulkan), `WidenedFrameWidth()` (d3d8) and the override's
returned aspect (hooks). Moving to the D3D12 width means changing all three
together, along with the `BlitFrame` aspect and the perf log line beside it. If
they disagree, the projection, target and present stop matching.

### 5. 3-D views embedded in the HUD would stretch

`FrameFoldScale()` multiplies `c97.x` (and the c58 viewport scale) by
`wide_w / 640` for every 3-D draw whose render target is the frame. It does not
look at the size of the viewport. Only views that span the whole frame (the main
view, split-screen) should widen. Smaller 3-D views drawn into the frame as part
of the HUD should keep their scale and only shift by dx with the HUD. Today they
would be stretched horizontally.

### 6. Remaining gaps in the Vulkan backend and XDK layer

- **Frame sampled as a texture.** When the guest samples the frame as a texture,
  `BindTargetTexture` and `Feedback` (`vulkan.cpp`) give it the whole widened
  image. Across the guest's 640 columns that comes back squeezed. It needs the
  centre 640 columns, the same crop `PrepareCopySource` already does for
  read-backs.
- **Depth copies.** `DepthCopyDraw` copies whole image to whole image between
  widened and unwidened targets. It does not account for dx.
- **Fixed-function viewport scale.** The fixed-function viewport in
  `FillConstants` widens the scale. `d3d12.cpp` does not: it moves the viewport
  offset and keeps the scale.
- **Side margins.** `EnsureTarget` clears the widened margins only when a target
  is created. They need clearing after every present, or whatever was last drawn
  there stays visible. Undefined margins are part of what made earlier attempts
  look broken.

### 7. Two implementation facts for whoever resumes

- **The getter returns its value in two places.** `sub_001287C0` returns its
  float in x87 `st(0)` and also leaves the same bits in `ecx`. An override must
  set both. The override in `hooks.cpp` writes only `X87_ST(0)`. It also reads
  the present height from `[ctx.ecx + 0x248]` after calling the original getter.
  Because of the first fact, `ecx` may hold the float bits by then rather than
  `this`, so `this` should be captured before the call. *(Inference from the
  first fact; not checked against the getter's code.)*
- **No "stage-(a) early return" is left in `d3d8.cpp`.** `CentreVsConstant()`
  already shifts the 2-D screen matrix (`c121.w`) by the same dx as the world
  fold. Older notes and tasks talk about removing an early return that skipped
  the 2-D case. There is nothing left to remove.

## The clean route: the title's own widescreen mode

`docs/guest-projection.md` and `docs/widescreen-flag.md` establish statically
that this title has a native widescreen mode, and that it is a projection change,
not a letterbox:

- The video object at `0x35D440` (vtable `off_2ECA78`) carries a dashboard
  widescreen byte at `+0x274`, set by `sub_001293C0` from the Xbox video flags. In
  SD widescreen the back buffer and picture are the same as 4:3 SD (640x480,
  640x448). Only the projection aspect and a present-parameter bit change: the
  console's anamorphic path.
- Every projection is built by `sub_00130BC0` (via `sub_001341F0`) as
  `m[0] = cot / aspect` with `m[5] = cot`. Changing only the aspect divisor widens
  the horizontal field of view and leaves the vertical one bit-identical, so it is
  Hor+ by construction.
- That aspect comes from a single getter, `sub_001287C0` (vtable `off_2ECA78`
  slot `+0x90`). It returns the float at `0x2ECB74` (1.7777778, 16:9) when the
  widescreen byte is set, and present width / height otherwise. Not every caller
  is a projection: some menu previews and HUD layout read it too (finding 3
  above). That is why the override has to be gated by return address, and why
  the gate has to list every camera (findings 1 and 2).
- **The 640x448 2-D family does not read it.** Menus, HUD, dialogue, name entry
  and the movie quad are drawn through one 640x448-normalised family
  (`sub_000CAE80` and helpers), via screen constants c121..c124. Those constants
  are built only from picture size, video mode and supersample scale. The two
  HUD-layout reads at `0xAF18C`/`0xAF4F7` are outside that family and must keep
  the authored value.

So the clean implementation is the one `b3b1762` prescribes, done at the guest's
own widening point. The native byte only knows 16:9, so for wider aspects the
host overrides the getter, in one change:

1. Hand the title the host's target aspect through `sub_001287C0`. Gate it by the
   complete list of camera return addresses, set both `st(0)` and `ecx`, and
   leave the four authored-aspect sites alone.
2. Widen the frame target by `640 x AspectWiden()`, as D3D12 does. Present it at
   `16/9 x (gw + 2dx) / gw`, and clear its margins every frame.
3. Shift c98/c121 and the viewport offset by dx. Widen the fold scale only for
   full-frame 3-D views.
4. Centre-crop read-backs, frame feedback and depth copies.

Then prove it in a loaded household (menus do not show HUD placement or picture
squeezing), at the display's real aspect, before it is committed. Check that:

- there are no bars,
- the HUD is the same size as with `ultrawide=false`,
- there is more world at the sides, and
- every object is the same height.

Until that lands, the honest state is the first section above: 16:9 pillarboxed
on an ultrawide, and `ultrawide` inert on Linux.
