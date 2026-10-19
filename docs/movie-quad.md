# How the intro movie reaches the screen

Static analysis of `out/game.exe` (guest addresses 1:1). Nothing here was built or
run. The decoded-frame comparison in section 5 reads an existing capture
(`out/build/linux-verbose/cap/front_00960.bmp`) and the disc's `sims_intro.xmv`;
nothing new was captured for it.

## Summary

| Question | Answer |
|---|---|
| Texture | `D3DFMT_LIN_A8R8G8B8` (0x12), **linear**, **allocated 640x448** (not a power of two), pitch 2560 bytes, 1,146,880 bytes of data; visible size = allocated size = 640x448 |
| Draw | `D3DDevice_DrawVerticesUP(D3DPT_QUADLIST, 4, verts, 24)` at `0x173407` |
| Vertex format | **Programmable vertex shader**, not `XYZRHW`. Declaration v0 `FLOAT3`, v3 `PBYTE4`, v9 `FLOAT2`; the program computes `oPos = dp4(v0, c[121..124])` |
| Corners | position (0,0),(0,1),(1,1),(1,0), z 0, in normalised picture units; uv (0,0),(0,448),(640,448),(640,0) in **texels** |
| Final `oPos` | x 1.0625..1281.0625, y 33.0625..929.0625: **2x supersampled pixel space** |
| Viewport | The title never calls `SetViewport`. D3D sets it internally on every `SetRenderTarget`, and the movie path does not touch it |
| Screen-space offset | Only `D3D_InitDefaultState` calls it, with (0,0). The title supplies its own offset through c[121..124] |

**What most likely magnifies the frame:** the title creates its device with
`MultiSampleType = 0x2222` (`D3DMULTISAMPLE_4_SAMPLES_SUPERSAMPLE_GAUSSIAN`) and scales
its vertex-program positions by 2 to suit that 1280x960 supersampled buffer. The host
builds a 640x480 back buffer and ignores the multisample type, so every position lands
2x too far from the origin. Section 5 shows the guest capture is the movie's top-left
corner at **exactly 2.0x**.

## 1. The texture

### Allocation

The movie object's vtable is `0x2EF974`. Its open method is `sub_175A60`: it picks
`d:\sims_intro.xmv` (string `0x2EFA74`, hash `0xB5968ED8`), creates the XMV decoder
`sub_2B1D3A`, then reads the video size with `sub_2B2135`
(`movie+0x14 = width`, `movie+0x18 = height`). The `sims_intro.xmv` header gives
640 and 448 at file offsets 0x14 and 0x18; `ffprobe` agrees (wmv2 640x448).

It then builds the engine's texture descriptor (`sub_26360` initialises it) and asks
for a texture:

```asm
0x175E5E  mov  cx, [edi]                 ; edi = movie+0x14 -> width 640
0x175E61  mov  dx, [esi+18h]             ; height 448
0x175E65  mov  [esp+50h+var_1C], cx      ; desc+0x10 = width
0x175E6F  mov  byte ptr [esp+50h+var_14], 1        ; desc+0x18 = 1
0x175E74  mov  byte ptr [esp+50h+var_14+2], 20h    ; desc+0x1A = 32
0x175E79  mov  [esp+50h+var_1A], dx      ; desc+0x12 = height
0x175E88  mov  [esp+50h+var_18], bp      ; desc+0x14 = 0
0x175E8D  mov  [esp+50h+var_24], 100h    ; desc+0x08 = 0x100 (byte +9 = 1)
0x175E95  call _sub_00128890             ; new texture object (vtable 0x2EF7B8)
0x175EA6  call dword ptr [edx+20h]       ; sub_1747F0, create from desc
```

`sub_1747F0` is the texture object's create method:

```asm
0x174807  test byte ptr [ebx+9], 1       ; desc+9 bit 0: render-surface usage
0x174812  call _sub_00128B90             ; returns 18 = 0x12
0x17481B  mov  [esi+48h], eax            ; format = D3DFMT_LIN_A8R8G8B8
...
0x1748BA  call _sub_002676E5             ; XGSetTextureHeader(640, 448, levels 1, 0, 0x12, 0, hdr, 0, pitch 0)
```

The XGRAPHICS header builder `sub_2674B9` takes its linear branch for 0x12. The
format table entry `byte_287BB8[0x12]` is `0xA2`: bit 0 is clear (not swizzled) and
`& 0x3C` gives 0x20 (32 bpp). That branch computes:

- pitch = `((640 * 32 / 8) + 63) & ~63` = **2560 bytes**
- Size register = `(640-1) | (448-1) << 12 | (2560/64 - 1) << 24`
- data size = 2560 * 448 = **1,146,880 bytes**, rounded up to 4096 and allocated at
  `0x1748E6` (`[esi+6Ch]`)

So the **allocated** texture is exactly 640x448 and the **visible** picture is the
whole 640x448. There is no 1024x512 texture and no picture tucked into a corner, so
hypothesis 2 is refuted.

On the upload: one frame is 1.09 MiB at 32 bpp, not 0.55 MB at 16 bpp. The video runs
at 29.97 fps against a 60 Hz loop, so the runtime's "0.5 tex uploads 0.54 MB" per
frame is one 1.09 MiB upload every other frame. That matches.

### Decode target

`sub_175A60` stores the texture object at `movie+0x24` and its surface
(`D3DTexture_GetSurfaceLevel` = `sub_2445C0`, `0x175EBE`) at `movie+0x28`. It locks
the surface (`sub_244610` → `Lock2DSurface`, `0x175EDE`) and clears
`pitch * height` bytes. Each frame, `sub_175930` calls the XMV decoder
`sub_2B27E2(decoder, movie+0x28 surface, …)` to write the decoded frame straight into
that surface.

## 2. The quad

### Draw call

`sub_173220` is the renderer's movie command handler. It is registered at `0x24284E`
(`dword_30BB5C`). It decodes a frame (`0x173230`), puts the movie texture in stage 0
(`[edi+0F4h] = movie+0x24`, then `sub_16F8C0(0)` → `D3DDevice_SetTexture`), and
selects the renderer's vertex shader:

```asm
0x173289  mov  eax, [esi+80h]
0x17329E  call D3DDevice_SetVertexShader
...
0x17336D  push 18h                     ; stride 24
0x173373  push edx                     ; vertices
0x173374  push 4                       ; vertex count
0x17337A  push 8                       ; D3DPT_QUADLIST
0x173407  call D3DDevice_DrawVerticesUP
```

There is no `Begin`/`End` and no vertex buffer. All of `D3DDevice_Begin`'s callers
(`0x16E745`, `0x170411`, …) are other paths.

### Vertex declaration and program

`renderer+0x80` is created in `sub_171A30`:

```asm
0x171C01..  D3DDevice_CreateVertexShader(decl 0x2EF6F8, function 0x30B8A0, renderer+0x80, 0)
```

The declaration at `0x2EF6F8` is:

| token | meaning |
|---|---|
| `0x20000000` | `D3DVSD_STREAM(0)` |
| `0x40320000` | `D3DVSD_REG(0, FLOAT3)`: position |
| `0x40440003` | `D3DVSD_REG(3, PBYTE4)`: 4-byte diffuse |
| `0x40220009` | `D3DVSD_REG(9, FLOAT2)`: texcoord 0 |
| `0xFFFFFFFF` | `D3DVSD_END` |

That is 12 + 4 + 8 = 24 bytes, which matches the stride.

The function at `0x30B8A0` has header `0x00062078`: a vertex program of 6
instructions. Decoded with the field layout from `sdk/runtime/gpu/shaders.cpp`
(`Src`, `VertexProgramHlsl`):

```
0  mov oT0.xy, v9
1  mov oD0,    v3
2  dp4 oPos.x, v0, c[121]      (c25 in the -96 shader numbering)
3  dp4 oPos.y, v0, c[122]
4  dp4 oPos.z, v0, c[123]
5  dp4 oPos.w, v0, c[124]      (last)
```

So positions are **model-space through a vertex program**, not pre-transformed
`XYZRHW`. `v0` is FLOAT3, so `w = 1`.

### Corner values (written at run time, `0x17337C..0x1733FE`)

Let `W, H` be the picture size the renderer reads from `[dword_35D438+8]` and `+0xC`
(the object at `0x35D440`: 640 and 448). Let `w, h` be the movie size (640, 448).
With the movie's position at `-1` (centred):

- `x0 = (W - w) / (2W) = 0`, `x1 = w/W + x0 = 1`
- `y0 = (H - h) / (2H) = 0`, `y1 = h/H + y0 = 1`

| vertex | x | y | z | diffuse | u | v |
|---|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 0x00000000 | 0 | 0 |
| 1 | 0 | 1 | 0 | 0x00000000 | 0 | 448 |
| 2 | 1 | 1 | 0 | 0x00000000 | 640 | 448 |
| 3 | 1 | 0 | 0 | 0x00000000 | 640 | 0 |

The texture coordinates are in **texels**, not 0..1. That is correct for an Xbox
linear (`LIN_`) texture, which the hardware addresses with unnormalised coordinates.

### What c[121..124] holds

The constants are loaded at device setup (`sub_171A30`):

```asm
0x171D32  cvtsi2ss xmm0, dword_35D448        ; 640  (object 0x35D440 + 8)
0x171D40  cvtsi2ss xmm0, dword_35D44C        ; 448  (+0xC)
0x171D69  call _sub_0012F480                 ; S = diag(640, 448, 1, 1) at renderer+0x6B0
0x171CD4  call _sub_0022E296                 ; P = diag(sx, sy, 16777215, 1)
0x171CE9/0x171D04                            ; P row 3 = (sx*ox, sy*oy, 0, 1)
0x171DD2  call _sub_0022DDC0                 ; S * P
0x171DDF  call _sub_0022DEBE                 ; transpose
0x171DED  D3DDevice_SetVertexShaderConstant4(0x79 = 121, …)
```

They are re-pushed by `sub_16F740` (push-buffer constant load 121 at `0x16F81E`),
which `sub_1723F0` (view pop) and `sub_1725D0` call. The values `sx, sy, ox, oy` come
from `sub_128BF0` called on the object at `0x35D440` (`0x16F77E`; at boot
`0x173F03`):

```asm
0x128BF0  mov  eax, [ecx+270h]          ; video mode
0x128BF6  movss xmm0, 1.0
0x128BFE  sub  eax, 2
0x128C05  jz   …                        ; mode 2 (720p): scale 1.0
0x128C07  movss xmm0, 2.0               ; every other mode: scale 2.0
0x128C2D  ox = int[+10h] + [+260h] + 0.53125
0x128C42  oy = int[+14h] + [+264h] + 0.53125
```

The default 640x480 mode (`sub_1293C0`) sets `[+260h] = 0.0` and `[+264h] = 16.0`.
The base constructor `sub_165C60` zeroes `+10h/+14h`, and no absolute reference
writes them.

A render-to-texture view (the view's target slot at `+0x100` is non-zero,
`0x16F765`) takes `0x16F78A` instead: scale 1, offset 0.

The **scale of 2** exists because of the device's multisample type. `sub_173EC0`
fills the present parameters and sets
`MultiSampleType = sub_128BD0()` (`0x173F56`). That function returns `0x2222`
(`D3DMULTISAMPLE_4_SAMPLES_SUPERSAMPLE_GAUSSIAN`) for every mode except 720p, which
gets `0x11` (none). Xbox D3D does not apply the supersample scale to programmable
vertex shaders, so the title folds it into its own matrix.

The back-buffer draw therefore expects:

```
c[121] = (1280, 0,   0,        1.0625)
c[122] = (0,    896, 0,        33.0625)
c[123] = (0,    0,   16777215, 0)
c[124] = (0,    0,   0,        1)
```

and the movie's `oPos` corners are (1.0625, 33.0625) to (1281.0625, 929.0625). In
the 1280x960 supersampled buffer that is the 640x448 picture at rows 16..464 of
640x480, which agrees with `docs/picture-rect.md`.

## 3. Viewport and screen-space offset

- **`D3DDevice_SetViewport`** (`0x246B40`) has two callers, both inside the D3D
  library: `0x2490DE` (`sub_2490C0`) and `0x24AE08` (`sub_24AAC0`). `sub_24AAC0`
  runs from `D3DDevice_SetRenderTarget` (`0x246299`) and from the
  MultiSampleMode / MultiSampleRenderTargetMode setters. It recomputes the
  supersample scale (`CDevice+0x95C/+0x960`) and resets the viewport to the full
  render target. The movie path (`sub_173220`, `sub_170AA0`, `sub_1714B0`,
  `sub_16F8C0`) sets no viewport. The viewport in force is whatever the last
  `SetRenderTarget` left: the full back buffer, after `sub_1723F0`'s
  `SetRenderTarget` at `0x172510`. A programmable shader's `oPos` is not
  viewport-transformed on Xbox, which is why the title builds the scale and offset
  into c[121..124].
- **`D3DDevice_SetScreenSpaceOffset`** (`0x2480F0`) has one caller, `0x24F9F9`, in
  `sub_24F96D`, which `D3D_InitDefaultState` reaches. It sets (0.0, 0.0). The movie
  never calls it. The title's own half-pixel-plus offset (0.53125, 16.53125 before
  scaling) lives in the c[121..124] translation instead.

## 4. Why the host draws it magnified

`sdk/runtime/xdk/d3d8.cpp` `SetupPresentation` makes a back buffer of the
present-parameter width and height (640x480). The multisample type `pp+0x10` is only
logged (`d3d: Reset … multisample %X`). Both backends then map `oPos` with
`surf = SurfaceSize(back buffer)` = 640x480:
`q.x = p.x * 2 / surf.x - 1` (`shaders.cpp` epilogue, `vulkan.cpp`
`FillConstants`). Positions built for a 1280x960 supersampled buffer therefore cover
twice the screen in each axis, anchored at the top-left. The visible result is the
picture's top-left quarter, magnified 2x.

Every draw through `renderer+0x80`/`+0x84` (the same program, `0x30B8A0`) reads
c[121..124], so other screen-space 2D drawn through that program in an SD mode is
exposed in the same way. The other program, `0x30B908` (`renderer+0x88`), reads
c[125..128] instead.

## 5. Checking against the existing capture

`out/build/linux-verbose/cap/front_00960.bmp` (640x480 guest read-back):

- The quad's top edge is at y = 33 (2 × 16.53).
- Its bottom is clipped at y = 464.
- Its left edge is at x = 111 (2 × 0.53 + the backend's ~110-pixel widen `dx`).
- It runs off the right edge.

`ffmpeg` decoded `sims_intro.xmv` to 320x224 greyscale. I resized the capture region
[111, 640) × [33, 464) for several candidate magnifications and matched it against the
movie's top-left corner in every frame:

| magnification | best MSE (frame) |
|---|---|
| 1.0 | 5775 (597) |
| 1.5 | 5071 (188) |
| 1.9 | 1211 (173) |
| 1.95 | 447 (172) |
| **2.0** | **35.6 (172)** |
| 2.05 | 565 (172) |
| 2.1 | 1553 (172) |
| 2.5 | 5126 (172) |
| 3.0 | 4141 (369) |

The guest buffer is the movie's top-left corner at exactly 2.0x, as section 4
predicts.

## Not determined statically

- Why the crop changes with `render_percent`. That depends on host code
  (`BoundExtent`, the widen `dx`, render scale), not on the guest.
- The run-time values of `int[0x35D450]` and `int[0x35D454]` (assumed 0 from the
  constructor) and the run-time video mode. Logging `VC[121..124]` on the movie draw
  (already in `vulkan.cpp` around line 1468) checks both against the expected
  constants above.

## Appendix: back-buffer access and related questions

- **XYZRHW draws:** none. Every title `D3DDevice_SetVertexShader` call site (for
  example `0x17040A`, `0x172933`, `0x17329E`, `0x17370D`, `0x184ACD`) pushes a
  `CreateVertexShader` handle: the renderer's own programs or the `sub_184A70`
  FVF-keyed program cache. No title draw sets an FVF handle. The only
  `SetVertexShader(4)` is XDK-library device creation at `0x2504E5` (`sub_2500E0`).
- **Back-buffer reads/copies:** `D3DDevice_GetRenderTarget2` has no callers.
  `GetBackBuffer2` is used by the fatal-error screen `sub_16B810` (software font
  blits into the back buffer at (320,200) and (320,234)), by `sub_171660`
  (`CopyRects` from back buffer to a texture's level 0 at `0x171710`, rect from the
  render command, clamped to the destination surface), and by `sub_173EC0` (stored
  as the render target). All coordinates are 640x480; nothing uses 1280x960. A
  `BlockOnResource` raised from `Lock2DSurface` (`0x250FD1`) via `sub_244610` and
  texture method `sub_174B10` is a texture lock, not the back buffer.
- **Rect clears:** two, both in 640x480 coordinates: `sub_171520` at `0x17164D`
  (`Clear(1, {x0*640, y0*480, x1*640, y1*480}, flags 3)`) and `sub_16EC90` at
  `0x16ED33` (`Clear(1, {1,1,638,478}, flags 240)`). Other title clears pass count 0.
- **Viewport offset/scale:** no `D3DDevice_GetViewportOffsetAndScale` symbol exists
  in the image, and no game code reads `CDevice+0x95C` or `+0x960`; only D3D-library
  internals do (`sub_24AAC0` writes them at `0x24ACD3`/`0x24ACDD`). The title
  computes its scale in `sub_128BF0` from its own object at `0x35D440`.
- **Appended viewport constants:** no vertex program reads hardware c58/c59.
  Decoding the two static programs and all 22 assembler fragments the `sub_184A70`
  cache can concatenate shows every referenced constant is 96 or above: c96-119,
  c121-128, c129-156. The viewport-style pairs in use are c97/c98 and c121-124.
- **Multisample render state:** the title sets `MultiSampleAntiAlias = 1` once, at
  `0x171C56` in `sub_171A30`. It never sets `MultiSampleMode` or
  `MultiSampleRenderTargetMode` to a constant; the only other call sites are
  generic render-state dispatchers (`sub_16DD70`, `sub_16E140`, `sub_184D30`)
  forwarding a runtime value, with no caller passing states 154/155. Device
  multisampling comes only from the present parameter: 0x2222, or 0x11 for 720p.

Keeping the guest frame surfaces at 640x480 and treating supersampling purely as a
coordinate scale agrees with every surface access above. A real 1280x960 AA buffer
would disagree with the title's own rect clears, copies and locks unless the host
scales those too.
