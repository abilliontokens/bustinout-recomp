# The intro movie draw: why it was magnified, and the fix

The guest buffer now holds the whole video frame, and the frame matches `ffmpeg`'s decode of
`assets/sims_intro.xmv` at **1.0x**. The framing is the same at `render_percent = 100` and `34`.
Two defects put the wrong part of the video in the frame. They are fixed in `sdk` commits `11ca56f`,
`ab16850`, `dad5cf1` and `ef61641` (branch `bustinout-port`, on top of `8899f96`). All the
evidence below comes from `dad5cf1` binaries; `ef61641` after it changes a comment only.

1. **The title's supersampled back buffer was not modelled (the magnification).** The title
   creates its device with `MultiSampleType = 0x2222`
   (`D3DMULTISAMPLE_4_SAMPLES_SUPERSAMPLE_GAUSSIAN`). Its vertex programs write positions in that
   buffer's pixels, which are twice the 640x480 frame's. The host divided them by 640x480, so the
   quad covered twice the frame.
2. **The ultrawide Hor+ shift was added for a frame the Vulkan backend never widens (an offset).**
   On this 3440x1440 display the shift was +110 guest pixels, which put the quad's left edge at
   guest x 111.

The static side (which code draws the quad, its vertex format, its texture, where the 2x comes
from) is in `docs/movie-quad.md`, by `MovieQuadAnalysis`. This document covers the run-time side:
what the backend received, what it computed, what changed, and the evidence.

## 1. The draw, instrumented

`XBE_VK_DEBUG_DRAW=1` makes the Vulkan backend log every draw of every 60th frame in full; during the
movie that is one draw. That covers
the vertex shader and its declaration, viewport, scissors, screen space offset, multisample states,
target sizes, `surf`, every bound texture's `TextureDesc`, and every vertex register. Each corner
is also worked through the shader on the CPU: the vertex program microcode evaluated as
`VertexProgramHlsl` translates it, then the `kVsEpilogue` pixel-to-clip step. The log gives
`oPos`, clip position, host pixel, guest pixel and UV per corner. (`VulkanBackend::LogDraw` in
`sdk/runtime/gpu/vulkan.cpp`; it replaces the cruder `vk-draw` block that was there.)

During the movie there is exactly one draw per frame. The draw at present 960, **before** the fix
(`out/shots/before100.log`, verbose build, `render_percent = 100`), verbatim apart from the log
prefix (timestamp, level, thread):

```
vk-draw: present 960 rt 0283C000 prim 8 count 4 | 4 vertices, 6 indices, draw count 0, topology 2 strip 0
vk-draw:  vertex shader B0002331: program, pretransformed 0, quad 1, registers 0209, decode 0
vk-draw:  declaration 20000000 40320000 40440003 40220009 FFFFFFFF | program 6 instructions
vk-draw:  viewport (0,0 640x480 z 0.000..1.000) scissors 1 (0,16)-(640,464) | screen space offset (0.00000,0.00000) zscale 16777215 | multisample antialias 1 mask FFFFFFFF mode 0 rendertarget mode 0
vk-draw:  target guest 640x480 host 1920x1440 (render scale 3.000) | guest extent 640x480 dx 0.0 | surf (640.0, 480.0, 5.96e-08)
vk-draw:  stage 0 texture 803D8000 format 12 LIN_A8R8G8B8 640x448 depth 1 levels 1 pitch 2560 linear 1 cube 0, 1146880 bytes | target 0 kind 1 rect 1 | texsize (640, 448, 0.000000, 0.000000) uvrect (0.00078,0.00112)-(0.99922,0.99888)
vk-draw:  register 0: format 6, buffer 0 offset 0 stride 24
vk-draw:  register 3: format 28, buffer 0 offset 12 stride 24
vk-draw:  register 9: format 16, buffer 0 offset 16 stride 24
vk-draw:  vertex 0: v0 (0.0000, 0.0000, 0.0000, 1.0000) v3 (0.0000, 0.0000, 0.0000, 0.0000) v9 (0.0000, 0.0000, 0.0000, 1.0000)
vk-draw:  vertex 0: oPos (111.0625, 33.0625, 0.0000, 1.0000) -> clip (-0.65293, 0.86224, 0.00000) -> host pixel (333.19, 99.19) = guest pixel (111.063, 33.062) | oT0 (0.0000, 0.0000, 0.0000, 1.0000) -> uv (0.00000, 0.00000)
vk-draw:  vertex 1: v0 (0.0000, 1.0000, 0.0000, 1.0000) v3 (0.0000, 0.0000, 0.0000, 0.0000) v9 (0.0000, 448.0000, 0.0000, 1.0000)
vk-draw:  vertex 1: oPos (111.0625, 929.0625, 0.0000, 1.0000) -> clip (-0.65293, -2.87109, 0.00000) -> host pixel (333.19, 2787.19) = guest pixel (111.063, 929.062) | oT0 (0.0000, 448.0000, 0.0000, 1.0000) -> uv (0.00000, 1.00000)
vk-draw:  vertex 2: v0 (1.0000, 1.0000, 0.0000, 1.0000) v3 (0.0000, 0.0000, 0.0000, 0.0000) v9 (640.0000, 448.0000, 0.0000, 1.0000)
vk-draw:  vertex 2: oPos (1391.0625, 929.0625, 0.0000, 1.0000) -> clip (3.34707, -2.87109, 0.00000) -> host pixel (4173.19, 2787.19) = guest pixel (1391.062, 929.062) | oT0 (640.0000, 448.0000, 0.0000, 1.0000) -> uv (1.00000, 1.00000)
vk-draw:  vertex 3: v0 (1.0000, 0.0000, 0.0000, 1.0000) v3 (0.0000, 0.0000, 0.0000, 0.0000) v9 (640.0000, 0.0000, 0.0000, 1.0000)
vk-draw:  vertex 3: oPos (1391.0625, 33.0625, 0.0000, 1.0000) -> clip (3.34707, 0.86224, 0.00000) -> host pixel (4173.19, 99.19) = guest pixel (1391.062, 33.062) | oT0 (640.0000, 0.0000, 0.0000, 1.0000) -> uv (1.00000, 0.00000)
vk-draw:  c[121] (D3D c25) = (1280.0000, 0.0000, 0.0000, 111.0625)
vk-draw:  c[122] (D3D c26) = (0.0000, 896.0000, 0.0000, 33.0625)
vk-draw:  c[123] (D3D c27) = (0.0000, 0.0000, 16777215.0000, 0.0000)
vk-draw:  c[124] (D3D c28) = (0.0000, 0.0000, 0.0000, 1.0000)
```

What these numbers show:

- **Primitive and vertices.** `D3DPT_QUADLIST` (prim 8) with 4 vertices, assembled as 6 indices.
  Each vertex is 24 bytes: `v0` FLOAT3 position, `v3` D3DCOLOR, `v9` FLOAT2 texcoord.
- **Not pretransformed.** This is a vertex program (`B0002331`, 6 instructions), not
  `D3DFVF_XYZRHW`. `oPos = dp4(v0, c[121..124])`; `v0` runs over the unit square, so the
  positions are set entirely by `c121`/`c122`.
- **Texture.** `LIN_A8R8G8B8` at exactly 640x448, pitch 2560, 1,146,880 bytes (= 2560 x 448).
  Visible size equals allocated size. The UVs run 0..640 and 0..448 in texels (a linear texture
  takes unnormalised coordinates) and come out as 0..1.
- **Position scale.** `c121.x = 1280` and `c122.y = 896` put the quad 1280x896 wide: twice the
  640x448 picture. `surf` was 640x480, so the far corners landed at guest (1391, 929). That is
  clip x 3.35 and y -2.87, well past the frame.
- **Position offset.** `c121.w = 111.0625` is the title's `2 x 0.53125 = 1.0625` plus the HLE's
  Hor+ centre shift of 110. `c122.w = 33.0625` is `2 x 16.53125`.
- **Viewport, scissor, offset.** Viewport is the whole 640x480 frame; one scissor at
  (0,16)-(640,464); screen space offset 0.

### The hypotheses, as the numbers decide them

| # | Hypothesis | Verdict |
|---|---|---|
| 1 | Pre-transformed vertices not scaled by the render scale | **Refuted.** The draw is a vertex program, `pretransformed 0`. Its positions do not depend on `render_percent`: `oPos`, clip and guest pixel are identical at 100 and 34 (§2). The cause is the guest's own **supersample** scale of 2. |
| 2 | Texture allocated larger than its visible size | **Refuted.** Exactly 640x448, `TotalSize` 1,146,880 = pitch x height, UVs 0..1 of it. |
| 3 | Vertex attribute order | **Refuted.** Registers 0, 3 and 9 hold the expected data (`v0` the unit square, `v9` the texel corners), and after the fix the guest buffer matches the decoded frame at 52 dB (§5), which a swapped or missing attribute could not give. |
| 4 | Viewport or screen space offset applied twice / not scaled | **Partly.** Viewport and screen space offset are correct, but the HLE added the Hor+ shift (+110) for a frame the Vulkan backend does not widen: a second, independent defect. |

The cause is the one `MovieQuadAnalysis` found statically (`docs/movie-quad.md` §4). The device
asks for `MultiSampleType 0x2222`, and the run logs it:
`d3d: Reset 640x480 format 1E buffers 1 multisample 2222 depth 1 format 2E ...`. The title's
`sub_128BF0` returns a scale of 2.0 for every mode except 720p, so its vertex programs write
positions in a 1280x960 supersampled back buffer. The host built a 640x480 one and mapped `oPos`
by 640x480.

## 2. `render_percent` was never coupled to the zoom

The brief's strongest clue was that the frame was tighter at `render_percent = 100` than at `34`,
from `out/shots/verify_20s.png` against `out/shots/scale34_20s.png`. **The measurement says
otherwise, and that clue should not be chased again.**

- Before the fix, at 100 and at 34, the instrumented draw gives the same `oPos`, the same clip
  coordinates and the same guest pixel for every corner. Only the host pixel differs, because the
  host target differs: 1920x1440 at 100, 653x490 at 34. Logs: `out/shots/before100.log`,
  `out/shots/before34.log`.
- Matched against the decoded XMV (§5), both of the owner's screenshots show a 2.0x crop anchored
  at guest (111.06, 33.06). Only the frame differs: `verify_20s.png` is XMV frame 303 at
  PSNR 48.7 dB, `scale34_20s.png` is XMV frame 291 at 48.1 dB. That is the same magnification at
  two moments of the movie, 12 frames (0.4 s) apart, and the camera moves between them. Our own
  `before100_21s.png` and `before34_21s.png` match the same way (frames 333 and 340, 48.7 and
  48.3 dB).
- The guest buffers before the fix at 100 and 34 are the same crop:
  `out/shots/movie_guest_00960_before_100_vs_34.png`.

## 3. The fix

All of it is in the device and presentation layer that both backends share. The title's surfaces
stay the size it asked for (640x480). Supersampling is modelled as the XDK's own
**SuperSampleScale**: screen space positions are in render target pixels, `SuperSampleScale` of
them to each pixel of the frame, while the viewport, scissors, clear rects, copies and locks stay
in the frame's pixels. `MovieQuadAnalysis` showed that every surface access this title makes
(`CopyRects` from the back buffer, rect `Clear`s, the font blits through `Lock2DSurface`) is in
640x480 coordinates. It has no `XYZRHW` draws, and no program that reads `c58`/`c59`. A real
1280x960 buffer would have needed every one of those scaled.

- **`sdk/runtime/xdk/d3d8.cpp`**
  - `SetMultiSampleType`: `Direct3D_CreateDevice` and `D3DDevice_Reset` now pass
    `D3DPRESENT_PARAMETERS::MultiSampleType` (`pp + 0x10`) to `SetupPresentation`. Only the
    supersample bit `0x2000` gives a scale, x samples from bits 4..7 and y from bits 0..3, so
    `0x2222` gives 2x2. `D3DMULTISAMPLE_NONE` (`0x11`), 0 and every multisampling type (`0x1xxx`)
    stay at 1, so no other title changes behaviour. `CreateDevice` now logs the type too.
  - `UpdateSuperSample`: runs after every render target change (`SetupPresentation`,
    `D3DDevice_SetRenderTarget`). The scale applies when the render target is the device's back
    buffer (`[device + 0x1A14]`, the test the library's render target setup makes at `0x24AAC0`)
    and is 1 for any other surface. It goes into `CDevice_SuperSampleX/Y` (`0x95C`/`0x960`, which
    the library's own `SetViewport`, `SetScissors`, `Clear` and viewport push read) and into
    `gpu::State::super_x/super_y` for the renderer. The library takes it from
    `BackBufferScale` (`0x24FB40` writes `0x96C`/`0x970`) and halves it per
    `D3DRS_MULTISAMPLEMODE`.
  - `UpdateViewportConstants`: `c-38`/`c-37` (hardware `c58`/`c59`) are composed as the library's
    viewport offset and scale helper composes them. That helper is at `0x249980`, unnamed in
    this image; it is the computation `d3d8.cpp` already called `D3D_GetViewportOffsetAndScale`:
    `offset = X * SSX + ScreenSpaceOffsetX + W * SSX / 2`, and `scale = W * SSX / 2`. The screen
    space offset is added after the scale, not scaled. With a scale of 1 the sum is the old
    expression's, term for term.
  - `FrameCentreShift`, the Hor+ shift added to `c59.x`, `c98.x` and `c121.w`: in render target
    pixels (`dx * super_x`), and 0 when the backend does not widen the frame
    (`g_frame_widened`, read once from `gpu::Backend::WidensFrame()` before the render thread
    wrapper takes the backend).
- **`sdk/runtime/gpu/state.h`**: `State::super_x`, `super_y` (default 1). They are set through
  `Set()`, so they are marked, sent and ordered with the draws like every other field.
- **`sdk/runtime/gpu/backend.h`**: `virtual bool WidensFrame() const { return true; }`. Direct3D 12
  keeps `true` and is unchanged; Vulkan returns `false`, because its frame target is the guest's
  width (`WidenedGuestWidth` returns `guestW_`).
- **`sdk/runtime/gpu/vulkan.cpp`** (`FillConstants`) and **`sdk/runtime/gpu/d3d12.cpp`**
  (`FillSurfaceConstants`, plus its one call site): for a vertex program,
  `surf.xy = guest extent x super`, so `kVsEpilogue` divides `oPos` by the render target's pixels.
  Fixed function and pretransformed draws keep the frame's `surf`. Their positions are frame
  pixels, as D3D defines them, and fixed function goes straight to clip space through `wvp`.
  **The `d3d12.cpp` change is written but has never been compiled.** This box has no MSVC and no
  Windows SDK. It is the same one-factor change as the Vulkan line, so a Windows build should be
  a compile away from correct.

On Direct3D 12 the frame stays widened, and the result is still right. At widen 1.344 the
extent is 860x480 (`dx` 110), `surf` becomes 1720x960 and the shift 220. The quad then spans
x (1.0625 + 220)..(1281.0625 + 220) / 1720 x 860 = guest 110.53..750.53: the console's 640 frame,
centred in the widened 860. This is arithmetic from the code; it has not been run on Windows.

After the fix, the same draw at `render_percent = 100` (`out/shots/final100.log`, `dad5cf1`):

```
vk-draw:  target guest 640x480 host 1920x1440 (render scale 3.000) | guest extent 640x480 dx 0.0 supersample 2x2 | surf (1280.0, 960.0, 5.96e-08)
vk-draw:  vertex 0: oPos (1.0625, 33.0625, 0.0000, 1.0000) -> clip (-0.99834, 0.93112, 0.00000) -> host pixel (1.59, 49.59) = guest pixel (0.531, 16.531) | oT0 (0.0000, 0.0000, 0.0000, 1.0000) -> uv (0.00000, 0.00000)
vk-draw:  vertex 1: oPos (1.0625, 929.0625, 0.0000, 1.0000) -> clip (-0.99834, -0.93555, 0.00000) -> host pixel (1.59, 1393.59) = guest pixel (0.531, 464.531) | oT0 (0.0000, 448.0000, 0.0000, 1.0000) -> uv (0.00000, 1.00000)
vk-draw:  vertex 2: oPos (1281.0625, 929.0625, 0.0000, 1.0000) -> clip (1.00166, -0.93555, 0.00000) -> host pixel (1921.59, 1393.59) = guest pixel (640.531, 464.531) | oT0 (640.0000, 448.0000, 0.0000, 1.0000) -> uv (1.00000, 1.00000)
vk-draw:  vertex 3: oPos (1281.0625, 33.0625, 0.0000, 1.0000) -> clip (1.00166, 0.93112, 0.00000) -> host pixel (1921.59, 49.59) = guest pixel (640.531, 16.531) | oT0 (640.0000, 0.0000, 0.0000, 1.0000) -> uv (1.00000, 1.00000)
vk-draw:  c[121] (D3D c25) = (1280.0000, 0.0000, 0.0000, 1.0625)
vk-draw:  c[122] (D3D c26) = (0.0000, 896.0000, 0.0000, 33.0625)
```

These are the expected values: `c121.w = 1.0625` (no Hor+ shift), and the quad covers guest
(0.53, 16.53)..(640.53, 464.53), the 640x448 picture at `picture_top = 16`. At
`render_percent = 34` (`out/shots/final34.log`) the guest pixels are identical; only the host
pixels differ, (0.54, 16.88)..(653.54, 474.21) in a 653x490 target.

## 4. Evidence: guest buffers

`XBE_CAPTURE_DIR=cap XBE_VK_CAPTURE_EVERY=240` writes `front_NNNNN.bmp` (the 640x480 guest
read-back) beside the exe. The first four rows are from `dad5cf1`; the last two are from before
the fix:

| File | Content |
|---|---|
| `out/build/linux-verbose/cap/front_00960.bmp` | **The whole video frame**, verbose build, `render_percent = 100` |
| `out/build/linux-verbose/cap34/front_00960.bmp` | the whole video frame, verbose, `render_percent = 34` |
| `out/build/linux-release/cap_movie100/front_00960.bmp` and `front_01200.bmp` | release, 100 |
| `out/build/linux-release/cap_movie34/front_00960.bmp` and `front_01200.bmp` | release, 34 |
| `out/build/linux-verbose/cap_before100/`, `cap_before34/` | before the fix (`8899f96` code plus the instrumentation only), 100 and 34 |
| `out/build/linux-verbose/cap_base8899/` | plain `8899f96`, from the validation baseline run |

Side by side, converted with `magick` (alpha flattened, because the dumps carry the target's
alpha):

- `out/shots/movie_guest_00960_before_after_xmv173.png`: the guest buffer before (lawn, mailbox,
  conifer filling the frame), after (the scooter scene, whole), and `ffmpeg`'s frame 173 placed
  at guest rows 16..464.
- `out/shots/movie_guest_00960_after_100_vs_34.png`: after the fix, 100 against 34, same framing.
- `out/shots/movie_guest_00960_before_100_vs_34.png`: before the fix, 100 against 34, the same
  crop.
- `out/shots/before_owner_front_00960.png`: the owner's original `front_00960.bmp`.

## 5. Evidence: against the decoded movie

`ffmpeg` decodes `assets/sims_intro.xmv` (WMV2, 640x448, 29.97 fps) to RGB. A guest buffer is
then compared with every decoded frame, under the mapping a quad with its top-left corner at
guest (ox, oy) and `scale` guest pixels per texel implies:

- guest pixel (x, y) shows texel ((x + 0.5 - ox) / scale - 0.5, (y + 0.5 - oy) / scale - 0.5),
  sampled bilinearly;
- the comparison covers guest rows 16..463 where that texel lies inside the frame;
- the result is the mean squared error over 0..255 per channel, at the best frame.

Results:

| Guest buffer | 1.0x at (0.53, 16.53) | 2.0x at (1.06, 33.06) | 2.0x at (111.06, 33.06) |
|---|---|---|---|
| after, verbose 100, `front_00960` | **frame 173, MSE 0.4 (52.0 dB)** | 11891 (7.4 dB) | 10901 (7.8 dB) |
| after, verbose 100, `front_01200` | **frame 295, MSE 0.6 (50.7 dB)** | 5819 | 6673 |
| after, release 100, `front_00960` | **frame 171, MSE 0.4 (52.1 dB)** | 11976 | 10887 |
| after, release 100, `front_01200` | **frame 292, MSE 0.6 (50.7 dB)** | 5717 | 6502 |
| after, verbose 34, `front_00960` | **frame 168, MSE 36.2 (32.5 dB)** | 11704 | 10854 |
| after, release 34, `front_00960` | **frame 167, MSE 36.1 (32.6 dB)** | 11756 | 10857 |
| after, release 34, `front_01200` | **frame 287, MSE 39.2 (32.2 dB)** | 5405 | 5442 |
| before, 100, `front_00960` | 18012 (5.6 dB) | | **frame 171, MSE 0.6 (50.6 dB)** |
| before, 34, `front_00960` | 16640 (5.9 dB) | | **frame 168, MSE 10.7 (37.9 dB)** |
| plain `8899f96`, `front_00960` | 16447 (6.0 dB) | | **frame 173, MSE 0.6 (50.5 dB)** |

The fixed buffers match at 1.0x, and the pre-fix ones at exactly 2.0x with the +110 shift. The
1.0x match at 34 is good but not near-exact (32.5 dB against 52 dB) because of resampling. At 34
the video is drawn into a 653-wide target, a render scale of 1.02, and filtered back to 640 for
the read-back. At 100 it is drawn at exactly 3x and box-filtered by 3. The framing is the same at
both; the vertex log in §3 shows it directly.

Inside the 640x448 picture, guest column 0 and row 16 are not covered (measured coverage 0.00;
column 1 and row 17 are at 0.97..1.02 of the video's brightness). The title adds its own +0.53125
offset, which puts the quad's edge just past those pixels' centres (0.5).
[INFERENCE: the console does the same. NV2A pixel centres are at +0.5, and the title's position
includes the same +0.53125; not observed on hardware.]

## 6. Evidence: on screen

`tools/shot_run.sh`, release build, at 17, 20 and 26 s. I measured each picture rectangle from
the screenshot itself (non-black columns and rows), then shrank the presented 1920x1440 region to
640x448 and matched it against the decode.

| Screenshot | Picture rectangle | 1.0x match |
|---|---|---|
| `out/shots/movie100_17s.png` (`render_percent = 100`) | x 762..2680, y 2..1410 | frame 199, 29.4 dB |
| `out/shots/movie34_17s.png` (`render_percent = 34`) | x 762..2680, y 1..1410 | frame 200, 27.7 dB |
| `out/shots/movie100_20s.png` | x 762..2680, y 2..1410 | frame 304, 27.1 dB |
| `out/shots/movie34_20s.png` | x 762..2680, y 1..1410 | frame 306, 26.5 dB |
| `out/shots/movie100_26s.png` | x 762..2680, y 2..1410 | frame 140 of the 12 s+ decode, 32.6 dB |
| `out/shots/movie34_26s.png` | x 762..2680, y 1..1410 | frame 143 of the 12 s+ decode, 32.1 dB |
| before: `verify_20s.png` / `scale34_20s.png` | x 1093 / 1091..2680, y 54..1410 | none at 1.0x; 2.0x at guest (111, 33) |

The framing is identical at 100 and 34: x 762..2680, the 1920x1440 picture at x 760..2680 minus
the black guest column 0. Side by side: `out/shots/movie_screen_20s_after_100_vs_34.png`, and
before the fix `out/shots/movie_screen_20s_before_100_vs_34.png`. In every screenshot the
desktop's panel covers rows 1410..1440 over the borderless window (`display_mode = "borderless"`),
so the comparison stops above it.

## 7. Builds and validation

- `cmake --build --preset linux-release` and `cmake --build out/build/linux-verbose` both build
  with no error and no warning at `ef61641`.
- `XBE_VK_DEBUG=1` alone **validates nothing on this box**. The validation layer package is not
  installed, and the log says `vulkan: XBE_VK_DEBUG set but VK_LAYER_KHRONOS_validation is not
  available`. A copy of the layer extracted at `/tmp/vklayer` loads with:
  `VK_ADD_LAYER_PATH=/tmp/vklayer/usr/share/vulkan/explicit_layer.d LD_LIBRARY_PATH=/tmp/vklayer/usr/lib XBE_VK_DEBUG=1`.
  The log then reads `vulkan: XBE_VK_DEBUG validation layer + debug utils enabled (messenger ...)`.
- With the layer loaded, the fixed build at 100 (`dad5cf1`, `out/shots/final100.log`) and plain `8899f96`
  (`out/shots/base8899_validation.log`) give the **same set of messages**, compared after
  stripping timestamps, thread ids and handles. There are 6 in each:
  - 5 warnings: `vkCreateGraphicsPipelines(): ... Output value declared at Location 8 ... but
    there is no corresponding Input declared in [VK_SHADER_STAGE_FRAGMENT_BIT]`.
  - 1 error, at about 0.17 s: `vkQueueSubmit(): ... expects VkImage ... to be in layout
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL--instead, current layout is
    VK_IMAGE_LAYOUT_UNDEFINED`.

  **No new validation errors.** The one error predates this work and is not touched here.

## 8. Still approximate

- **Multisampling types** (`0x1xxx`) are kept at a scale of 1, by decision, so no title asking for
  one changes behaviour. The library's own value is 1 for the 2x1 and 2x2 types, but 1.5 for 3x3
  (`0x1233`). A title that sets `D3DRS_MULTISAMPLEMODE` to 0 gets the whole `BackBufferScale`.
  Neither case is modelled.
- **`CDevice_BackBufferScale`** (`0x96C`/`0x970`) stays 1.0; the console holds 2.0 for `0x2222`.
  No reachable code reads it. Its readers `0x245CC0`, `0x248C30` and `0x2496E0` have no callers;
  `0x249130`/`0x2497F0` run only under the HLE'd `D3DDevice_Swap`; and `0x24AAC0` runs only from
  HLE'd functions (`SetRenderTarget`, the two multisample-mode render states).
- **No supersample filter.** The back buffer is drawn at the frame's size times the render scale,
  not at 2x and filtered down with the console's gaussian. [INFERENCE: this costs nothing at
  `render_percent = 100`, where the host target already has 3x the frame's pixels on each axis
  against the console's 2x; not compared with a console capture.]
- **Direct3D 12 is uncompiled.** It needs one Windows build to confirm it compiles and draws the
  movie whole; the widened-frame arithmetic in §3 is unverified on Windows.
- **No Hor+ on Vulkan.** The Vulkan backend still has no ultrawide Hor+. Before the fix the HLE
  moved the 2-D matrix (`c121.w`) and the world fold (`c98.x`) right by a widening the Vulkan
  target never had. With `WidensFrame()` false it moves neither, so by the code the world and the
  HUD sit at the console's centre and are pillarboxed like the movie.
- **Nothing past the intro was observed.** The fix changes every vertex program draw into the
  back buffer, not only the movie: this title doubles all of them (`sub_128BF0` returns 2.0 for
  every mode but 720p) and uses 1 for its render-to-texture views, the same split
  `UpdateSuperSample` makes. Every run for that fix ended inside the intro (about
  22 s), so menus and gameplay under the fix were not proven by those runs; they
  have been since — menus, Create-a-Sim and the loaded household under the
  supersample fix are proven in `docs/playtest.md` and `docs/status.md`.
