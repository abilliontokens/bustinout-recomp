# Ultrawide and aspect ratio: what works today, what was reverted, and the clean route

Recorded 2026-10-19 from the code and git history in this repository. Nothing was
built or run for this document. **Hor+ ultrawide is a known gap the owner has
asked for. It is not a design choice and it is not finished.**

## What the two settings do today (Linux / Vulkan)

The settings are `aspect_ratio` and `ultrawide` in `settings.toml`
(`sdk/runtime/app/settings.cpp`, defaults in `sdk/runtime/app/app.h`):
`aspect_ratio = "auto"` by default (one of `auto`, `4:3`, `16:9`, `16:10`,
`21:9`, `32:9`) and `ultrawide = true` by default.

- **`aspect_ratio` changes only the aspect the finished picture is presented
  at.** `PresentSrcAspect()` in `sdk/runtime/gpu/vulkan.cpp` returns the chosen
  ratio, or 4:3 for `auto` and for `4:3`, and `BlitFrame` fits the guest
  picture inside the window at that aspect, centred, never cropped and never
  stretched to the window's own shape. So on the default `auto` the picture is
  pillarboxed at the title's own 4:3: on this machine's 3440x1440 display that
  is a 1920x1440 picture with black bars at the sides
  (`docs/status.md`, "The picture"). Choosing `16:9` presents that same
  640x448 picture in a 16:9 box; it does not show more of the world.
- **`ultrawide` has no visible effect on Linux.** The shared settings code
  computes a widening factor from it (`Shaped()` in
  `sdk/runtime/app/settings.cpp`: on an output 3:2 or wider, with `ultrawide`
  on, `AspectWiden()` is the chosen aspect divided by 16:9, clamped to
  1–4, and `TargetAspect()` is the widened 16:9). The Vulkan backend reads
  that factor for logging only. `VulkanBackend::WidenedGuestWidth()` returns
  the unwidened guest width, the frame target is exactly the guest's size at
  render scale, and the backend tells the XDK layer it does not widen the
  frame, so the title's horizontal centre is left where the console has it —
  the comment at `WidenedGuestWidth()` in `sdk/runtime/gpu/vulkan.cpp` says
  exactly this. Turning `ultrawide` off and on changes no pixel on Linux.
- The D3D12 backend in `sdk/runtime/gpu/d3d12.cpp` does contain a widening
  path driven by the same factor, but the Windows target has never been
  compiled on this machine (no MSVC or Windows SDK, `docs/toolchain.md`), so
  that path is unproven anywhere.

In short: today the game always renders its 640x480 frame with its 640x448
picture (`docs/picture-rect.md`), and a wider display gets black bars, not
more world.

## The staged Hor+ attempt, and why it was reverted

Superproject commit `4202a7a` ("hor+: stage a - the title's aspect getter
returns the host aspect") landed the first stage on its own:

- `src/game/hooks.cpp` gained an override of the aspect getter
  `sub_001287C0` that returned the host frame aspect whenever `ultrawide` was
  on or an aspect was chosen (`SIMSBO_NO_WIDE=1` bisected it off), and
- the SDK pointer moved to a build whose Vulkan frame target was widened by
  the same factor.

The 2-D centre shift, the present aspect and the read-back centre crop were
deliberately left for later stages. That combination is broken on its own:
the world was projected and rendered into a widened target, but 2-D placement
still used the unwidened centre, the present still fitted the picture into
the old 4:3 pillarbox, and captures/read-backs still assumed the old width.
The HUD sat left of centre and the widened picture was squeezed into 4:3 —
the owner saw it as "really buggy".

Superproject commit `b3b1762` reverted the whole stage (the `hooks.cpp`
override and the SDK pointer) back to the last state verified in game
(superproject `4fae9fc`, SDK `c3d4335`): the F1 resolution control works, the
household renders, and the picture is pillarboxed at the title's own 4:3.
Its commit message is explicit that Hor+ is not abandoned: it must return
**as one change, proven in the household before it is committed**, not in
stages that each ship a broken picture. The three pieces that must land
together are:

1. **The 2-D centre shift** — for world draws, the post-projection
   scale+offset fold (vertex constants c97/c98, `docs/guest-projection.md`
   §2) and the XDK viewport centre offset must move by half the gained width;
   the 640x448-normalised 2-D path itself (c121..c124) must stay centred on
   the guest picture or every menu, HUD element and the movie move with it.
2. **The present aspect** — `BlitFrame`/`PresentSrcAspect()` must present the
   widened target at the widened aspect, not squeeze it back into 4:3.
3. **The read-back centre crop** — front-buffer captures and read-backs
   (F3, `XBE_VK_CAPTURE_EVERY`) that assume the guest's 640-wide picture must
   take the centre 640 columns (rows `picture_top..picture_top+picture_height`
   as today) of the widened target, so diagnostics keep showing the picture
   the console would have shown.

## The clean route: the title's own widescreen mode

`docs/guest-projection.md` (kept by the revert commit) establishes statically
that this title already has a native widescreen mode, and that it is a
projection change, not a letterbox:

- The video object at `0x35D440` (vtable `off_2ECA78`) carries a dashboard
  widescreen byte at `+0x274`, set by `sub_001293C0` from the Xbox video
  flags. In SD widescreen the back buffer and picture are identical to 4:3 SD
  (640x480, 640x448); only the projection aspect and a present-parameter bit
  change — the console's anamorphic path.
- Every projection is built by `sub_00130BC0` (via `sub_001341F0`) as
  `m[0] = cot / aspect` with `m[5] = cot`, so changing only the one aspect
  divisor widens the horizontal field of view and leaves the vertical one
  bit-identical: Hor+ by construction.
- That aspect comes from a single getter: `sub_001287C0`, vtable
  `off_2ECA78` slot `+0x90`. It returns the float at `0x2ECB74`
  (1.7777778, 16:9) when the widescreen byte is set, and present
  width/height otherwise. Every camera caller passes its result into the
  projection builder.
- **No 2-D path reads it.** Menus, HUD, dialogue, name entry and the movie
  quad are drawn in one 640x448-normalised family (`sub_000CAE80` and
  helpers) through screen constants c121..c124 built only from picture size,
  video mode and supersample scale. Widening the aspect cannot move them —
  which is why the reverted stage's HUD displacement came from the host
  widening the target without the centre shift, not from the guest.

So the clean implementation is the one `b3b1762` prescribes, done at the
guest's own widening point: hand the title the host's target aspect through
that one getter (the native byte only knows 16:9, so wider-than-16:9 aspects
must override the getter's return value), widen the Vulkan frame target by
the same factor, apply the c97/c98 and viewport centre shift for world draws
only, present at the widened aspect, and centre-crop read-backs — all in one
change. It must then be proven in a loaded household (menus do not expose HUD
placement or picture squeezing), at the display's real aspect, before it is
committed. Until that lands, the honest state is the first section above:
pillarboxed at the title's own 4:3, and `ultrawide` inert on Linux.
