# Picture rectangle: 640x448 at top offset 16, inside a 640x480 back buffer

Conclusion: `app::kGame.picture_top = 16`, `picture_height = 448` in
`src/common/game_info.cpp`. The title draws a 640x448 picture centred in the
640x480 back buffer it asks Direct3D for, so the presented rows are 16..463
and the 16 black rows above and below are dropped.

This has **not been verified on screen**: the port has not been run yet. All
evidence below is static, from `out/game.exe` (guest addresses map 1:1).

## 1. The UI reciprocals (picture size)

A whole-image scan for the plausible reciprocal float constants found exactly
one adjacent pair (`.rdata`):

| Address | Bytes | Float | Meaning |
|---|---|---|---|
| `0x2E88D4` (`dword_2E88D4`) | `CD CC CC 3A` (`0x3ACCCCCD`) | 0.0015625 | 1/640 |
| `0x2E88D8` (`dword_2E88D8`) | `25 49 12 3B` (`0x3B124925`) | 0.0022321429 | 1/448 |

Neither `1/480` (`0x3B088889`), `2/480`, `-2/480`, `1/416`, `1/432`, nor
`1/440` appears anywhere in the image; `2/448`/`-2/448` do not appear either
(the UI normalises by multiplication with `1/h`, not by a 2/h clip-space
scale). The only other size pair in `.rdata` is the inverse pair at
`0x2E6558`/`0x2E655C`: 640.0 (`0x44200000`) and 448.0 (`0x43E00000`).

Xrefs (IDA, `out/game.exe`):

- `dword_2E88D4` (1/640): 20 reads, including the tiny helper at `0xC92E4`
  (`fmul dword_2E88D4` at `0xC92E4`, function `0xC92E0`), `sub_C9650` at
  `0xC96C4`, `sub_CAE80` at `0xCAEEA`, and `sub_1B9F80` at `0x1BA747`.
- `dword_2E88D8` (1/448): 18 reads, at the matching sites: helper `0xC92F0`
  (`fmul` at `0xC92F4`), `sub_C9650` at `0xC96E1`, `sub_CAE80` at `0xCAF02`.

In `sub_CAE80` (the large 2D/UI draw routine, 2000+ instructions) the pair is
used exactly as a pixel-to-normalised scale on the same coordinate set:

```asm
0xCAEEA  movss xmm2, dword_2E88D4        ; 1/640
0xCAEF2  movss xmm0, [esi+108h]
0xCAEFA  subss xmm0, [esi+100h]          ; x extent
0xCAF02  movss xmm3, dword_2E88D8        ; 1/448
0xCAF15  mulss xmm0, xmm2                ; x extent * 1/640
0xCAF2D  mulss xmm0, xmm3                ; y coord  * 1/448 (from [esi+104h])
```

`sub_C9650` adds the same constants as half-texel-style offsets at
`0xC96C4`/`0xC96E1`. So the UI's own coordinate space is 640 wide by 448 tall.

## 2. The mode set (back-buffer size vs picture size)

`sub_1293C0` builds the presentation parameters and calls the device-create
wrapper `sub_173EC0` (call at `0x129579`, parameters passed from `esi+0x244`).
For the default (non-HDTV) mode the instructions are:

```asm
0x1294AE  mov dword ptr [eax], 280h      ; params+0x00 = 640  (eax = esi+0x244)
0x12953E  mov dword ptr [esi+248h], 1E0h ; params+0x04 = 480  (shared fall-through)
0x1294BE  mov dword ptr [esi+8], 280h    ; object+0x8 = 640   (separate picture width)
0x129548  mov dword ptr [esi+0Ch], 1C0h  ; object+0xC = 448   (separate picture height)
```

So in the same breath the title asks for a **640x480 back buffer** and stores
a distinct **640x448 picture size** on the renderer object. The other modes
show the same pattern of a picture smaller than the buffer on both axes,
which only makes sense centred:

- mode 1 (`0x129509` branch): buffer 720x480 (`0x129517`, `0x12953E`),
  picture 680x448 (`0x129527`, `0x129548`) — 40 spare columns, 32 spare rows.
- mode 2 / HDTV (`0x1294C7` branch): buffer 1280x720 (`0x1294DF`, `0x1294E5`),
  picture 1200x660 (`0x1294F9`, `0x129500`) — 80 spare columns, 60 spare rows.

## 3. Viewport cross-check

`D3DDevice_SetViewport` is XDK function `0x246B40` (it stores X/Y/Width/
Height/MinZ/MaxZ at device+`0xEE0` onward: `0x246CF0`, `0x246D04`,
`0x246CE8`, `0x246CFE`; see `sdk/xdk/5558/layout.toml`, `CDevice_Viewport =
0xEE0`). Its only callers in the image are D3D-library-internal:
`0x2490DE` (in `sub_2490C0`, the render-target save/restore path) and
`0x24AE08`. **No game (`.text`) code calls `SetViewport`, and no game
instruction stores a literal viewport of {0, 16, 640, 448}** — so the
viewport evidence neither confirms a literal Y=16 nor contradicts the crop:
the title never redefines the viewport away from the library default, and
the 448-row extent comes from its own UI/picture size, not from a viewport.

`D3DDevice_SetRenderTarget` (`0x246040`) is called by game code (e.g.
`0x1751E5` in `sub_1751D0`), but with surface pointers, not sizes; it carries
no height evidence either way.

Verdict on agreement: the UI reciprocals (448) and the mode set (480 buffer,
448 picture) agree that the picture is 448 of 480 rows. The viewport path is
silent — there is no explicit viewport instruction to agree or disagree.

## 4. Top offset

No guest instruction examined stores a literal 16 as a picture/viewport Y.
The 16 is arithmetic, from the two proven numbers: (480 − 448) / 2 = 16.
Centring (rather than top-aligning) is supported by the mode table above:
in modes 1 and 2 the picture is smaller than the buffer in **width** as
well, where a top/left-aligned picture would leave all the spare space on
one side; the engine keeps one centred picture rectangle per mode. The
default mode's spare width is 0, so only the vertical centring (16 rows) is
observable there.

## 5. What is set, and the risk

`src/common/game_info.cpp`: `.picture_top = 16`, `.picture_height = 448`,
with a comment citing `0xCAEEA`, `0xCAF02`, `0x1294AE`, `0x12953E`,
`0x1294BE`, `0x129548`.

Residual risk, stated plainly: if the title in fact drew from row 0 and left
the spare 32 rows at the bottom, this crop would cut 16 real rows off the
bottom. The centred reading is the one consistent with the multi-mode
picture/buffer table, but only a first on-screen run can settle it — and
that run has not happened yet.
