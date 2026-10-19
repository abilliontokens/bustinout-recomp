# Guest projection: 3-D perspective, 2-D screen mapping, and the native widescreen mode

Static analysis of `out/game.exe` (guest addresses 1:1), 2026-10-19. Nothing was
built or run. Prior findings in `docs/movie-quad.md`, `docs/picture-rect.md`,
`docs/cheats.md` and `docs/frame-rate.md` are built on, not redone.

## Answers in brief

| # | Question | Answer |
|---|---|---|
| 3 | Native widescreen? | **Yes.** The dashboard widescreen flag (video object `+0x274`) makes the aspect getter return 16:9 instead of buffer W/H, and every projection is built with that aspect. In SD it is anamorphic: the 640x480 buffer and 640x448 picture do not change, only the projection (and a present-parameter bit) does. |
| 1 | 3-D projection | Built by `sub_00130BC0` via `sub_001341F0`. `m11 = cot(FOV·(448/640)/2)`, `m00 = m11 / aspect`. Horizontal is derived from vertical by one aspect divisor — the exact place Hor+ belongs. FOV is 55–62° (before the 448/640 scaling) lerped by camera distance. |
| 2 | 2-D mapping | Separable. Menus/HUD/text go through one 640x448-normalised family (`sub_000CAE80` and helpers) and the screen program's c121..c124, built only from picture size, video mode and the supersample scale. The aspect/widescreen byte never enters it. Widening the projection does not move any 2-D element. |

---

## 3. The title already supports widescreen, and it changes the projection

### Where the flag comes from

The video/display object is the static object at `0x35D440` (pointer published
at `dword_35D438` by its constructor `sub_00128660`, which also clears the byte
below). Its vtable is `off_2ECA78`.

`sub_001293C0` (video-mode setup, `this` = that object) at `0x12944F` calls
`sub_0022AC85`, which reads a saved-settings dword (`sub_0010A2B6` with size 8
at `0x22AC96`), shifts it right 16 and masks `0x5F` (`0x22ACA4`/`0x22ACA7`) —
the Xbox video-flags word [INFERENCE: the bit layout below matches
`XC_VIDEO_FLAGS` exactly, and each bit's use in the mode selection confirms it].

| Bit after `>>16 & 0x5F` | Meaning [INFERENCE for the name] | Use in `sub_001293C0` |
|---|---|---|
| 1 (`&1` at `0x129456`) | widescreen | stored to byte `[this+0x274]` at `0x12945B`; also ORs `0x10` into the present-parameter dword at `[esi+0x250]` (`0x12948F`, `0x12955B`) |
| 8 (`test al,8` at `0x129459`) | 480p | mode `[this+0x270] = 1` at `0x12946A` |
| 2 (`test al,2` at `0x129474`) | 720p | mode = 2 at `0x129481`, **only if widescreen is also set** (`cmp cl,bl` / `jz` at `0x12947D`) |

The mode table that follows (`0x129495` on) is the one `docs/picture-rect.md`
already recorded:

| Mode | Back buffer | Picture | Centre offset (`+0x260`/`+0x264`, floats at `0x1294CF`–`0x129551`) |
|---|---|---|---|
| 0 (SD) | 640x480 (`0x1294AE`, `0x12953E`) | 640x448 (`0x1294BE`, `0x129548`) | (0, 16) |
| 1 (480p) | 720x480 (`0x129517`) | 680x448 (`0x129527`) | (20, 16) |
| 2 (720p) | 1280x720 (`0x1294DF`, `0x1294E5`) | 1200x660 (`0x1294F9`, `0x129500`) | (40, 30) |

So in SD widescreen the buffer and picture are **identical** to 4:3 SD. Nothing
letterboxes and nothing resizes. What changes is only the aspect below and the
`0x10` bit in the present parameters — the console's anamorphic path: a 16:9
projection squeezed into 640x480 for the display to stretch.

### The aspect getter

Video vtable slot `+0x90` (table entry at `0x2ECB08`) is `sub_001287C0`:

```asm
0x1287C4  cmp byte ptr [esi+274h], 0     ; widescreen byte
0x1287CB  jz  short ...                  ; clear: fall through
          ; set: return dword_2ECB74 = 1.7777778 (16:9)
          ; clear: return vfunc+0x94 / vfunc+0x98
```

The two fallback getters are `sub_00128820` (returns `[this+145]`, the present
width at `+0x244`) and `sub_00128830` (returns `[this+146]`, the present
height). So the aspect is:

- widescreen set: **1.7777778** (the float at `0x2ECB74`), regardless of mode;
- otherwise: **present width / present height** — 640/480 = 1.3333 in mode 0,
  720/480 = 1.5 in mode 1, 1280/720 = 1.7778 in mode 2 (which cannot be
  reached without widescreen anyway).

Every camera caller below passes this value into the projection builder as its
aspect argument. That is the whole widescreen mechanism, and it is a
projection change, not a letterbox.

### Not in the options object

The options object at `0x310B98` (`docs/cheats.md`) holds the cheat table and
effect word; no widescreen or aspect field was found in it or in its
constructor `sub_00072A80`. The widescreen byte at video `+0x274` has exactly
two writers in the image: cleared by the constructor `sub_00128660` and set
from the dashboard flags by `sub_001293C0` at `0x12945B`. There is no in-game
widescreen toggle; on the console it follows the dashboard setting.

**Host consequence:** the engine's own Hor+ is "aspect = 16:9 in the
projection, everything else (buffer, picture, 2-D) untouched". A host widening
that reproduces that — widen the aspect divisor in §1, present the guest
picture undistorted — is using the title's own mode, not inventing one.

---

## 1. The 3-D projection

### The builder

`sub_00130BC0(this = matrix, a0, aAspect, aNear, aFar)` builds a 16-float
perspective matrix into a caller buffer:

```asm
0x130BC0  fld   [esp+arg_0]
0x130BC7  fmul  flt_2ECDC8              ; 0.008726646 = pi/360
0x130BE5  fcos
0x130C51  fsin
0x130C57  fdivp st(1), st               ; cot(a0 * pi/360)
0x130C59  fld   [esp+arg_4]             ; aspect
0x130C5D  fdivr st, st(1)               ; cot / aspect
0x130C5F  fstp  dword ptr [eax]         ; m[0]  = cot / aspect
0x130C61  fstp  dword ptr [eax+14h]     ; m[5]  = cot
0x130BF5  ... mulss by arg_8/arg_C ...
0x130BFD  movss [eax+38h], ...          ; m[14] = near*far/(near-far) form
0x130C40  movss [eax+28h], ...          ; m[10] = far/(near-far) form
0x130C45  movss [eax+2Ch], dword_2E23F8 ; m[11] = -1.0
```

All other entries are zeroed (`0x130C0E`–`0x130C4A`). So:

- **vertical**: `m[5] = cot(half-angle)`, where the half-angle is
  `a0 · π/360`, i.e. `a0` is a full vertical FOV **in degrees**;
- **horizontal**: `m[0] = m[5] / aspect` — derived from the vertical by the
  single aspect divisor. Changing only that divisor widens/narrows the
  horizontal FOV and leaves the vertical one bit-identical. That is Hor+ by
  construction, and it is exactly what the widescreen flag in §3 does.

### The wrapper and the camera callers

`sub_001341F0(view, fovArg, aspect, near, far)` (retn 0x10) calls the builder
into a stack matrix (`0x13420E`), stores the arguments at
`view+0x24C` (fovArg), `view+0x25C` (aspect), `view+0x2B0` (near),
`view+0x2B4` (clamped against the 3.2 float at `0x2ECF10`), `view+0x2B8`
(far), copies the matrix to `view+0x160` (`sub_0012F070` at `0x134265`), and
finishes with `sub_00133D80` (frustum-plane extraction begins in
`sub_001316B0`, which reads the matrix and the video object's `+0x8C` getter).

It has 22 call sites. The camera setters are the informative ones —
`sub_00016500` (call at `0x16743`), `sub_000162C0` (`0x164D6`),
`sub_00016B80` (`0x16CB6`), `sub_000153E0` (`0x154CE`) — and all build the
arguments the same way (shown from `sub_00016500`'s asm, the cleanest):

```asm
0x16696..0x166EA  t = (dist - 8.0) / (35.0 - 8.0)     ; dist = [cam+0x3A0]
0x16700..0x16714  fovDeg = lerp(55.0, 62.0, t)        ; 0x308988 / 0x30898C
0x166DA..0x166EA  near   = lerp(2.0, 10.0, t)         ; 0x308994 / 0x308998
0x16708           far    = 200.0                      ; flt_308990
0x1671D           call  [video vtable+0x90]           ; aspect (see §3)
0x16726..0x1673C  arg0   = pictureHeight * fovDeg / pictureWidth
                  ; ebx = [video+0xC] = 448, ebp = [video+0x8] = 640
0x16743           call  sub_001341F0
```

Notes:

- `dist` (`cam+0x3A0`, byte offset 928) is the camera-to-target distance:
  `sub_00016500` computes it as the length of eye−target at `0x1663E`
  (`fsqrt`, stored `0x16640`). This is the same distance/FOV interaction the
  sibling port found — FOV widens from **55° to 62°** as distance grows from
  8 to 35, and the near plane moves from 2 to 10 with it. (The sibling's
  numbers were 55–65°; this title's are 55–62°.)
- The FOV actually fed to the cot is **not** 55–62°: it is first scaled by
  picture height/width (448/640 = 0.7 in SD), so the effective vertical FOV
  in the matrix is 38.5–43.4°. The 55–62 figure behaves like a
  horizontal-reference FOV converted by the linear H/W ratio (not by the
  tan relation). Either way, both the scale and the aspect divisor are
  mode-independent constants at the call site; only `aspect` carries the
  widescreen state.
- `sub_0001D870` also calls the builder directly (`sub_00130BC0` at
  `0x1DF17` region, decompiled call with the same aspect getter result) for a
  second view, so there is more than one view object but only one builder
  and one aspect source.

### Which vertex constants it reaches

The view matrix is submitted to the renderer as a matrix command; on the
renderer side (`sub_00170AA0`, the per-draw constant upload):

- renderer command 8 (`sub_0016F700`) copies its matrix to `renderer+0x630`
  and sets dirty flag 4; `sub_00170AA0` at `0x170B20`–`0x170B3F` then writes
  that matrix transposed (`sub_0022DEBE`) into vertex constants **c125..c128**
  (push-buffer packet `0x41EA4`, index `0x7D` at `0x170B20`);
- the combined matrix `multiply(renderer+0x6F0, renderer+0x7F0)` is written
  transposed to **c129..c132** (index `0x81` at `0x170B44`), and the
  `renderer+0x670` matrix to c133.. (index `0x85` at `0x170BB0`) when flag 2
  is set;
- the assembled FVF programs from the `sub_00184A70` cache compute `oPos`
  from c129..c132 (fragment `0x30BE70` instr 0–3) or c133..c136 (skinned
  fragment `0x30BF38` instr 15–18), and the renderer's own program
  `0x30B908` from c125..c128 (instr 2–5). All three then apply the c97/c98
  fold of §2.

I did not trace which of the renderer's matrix slots (`+0x630` vs the
`+0x6F0`/`+0x7F0` pair) the game-side view submission fills with the
projection as opposed to the view transform — the submission goes through a
command-builder vtable (`+0x78` call with the `sub_00130BC0` result in
`sub_0001D870`) whose slot-to-command mapping I did not decode. What is
proven is the builder, its aspect divisor, and the constant ranges the
resulting matrices occupy; the exact slot is not needed to widen, because
the widening point is the aspect argument, upstream of all of them.

---

## 2. The 2-D screen mapping is a separate path

### Two constant paths, decoded from the fragments

Vertex-program instructions are 4 dwords; the constant index is word 1,
bits 13–20 (the layout `sdk/runtime/gpu/vulkan.cpp`'s `EvalVertexProgram`
uses). Decoding every fragment the `sub_00184A70` cache can concatenate, plus
the two static renderer programs:

| Program/fragment | oPos source | c97/c98 fold? |
|---|---|---|
| `0x30B8A0` (renderer `+0x80`, `+0x84`) | dp4 with **c121..c124** (instr 2–5) | **no** |
| `0x30B908` (renderer `+0x88`) | dp4 with c125..c128 (instr 2–5) | yes (instr 6–7) |
| `0x30BE70` (FVF base fragment) | dp4 with c129..c132 (instr 0–3) | yes (instr 4–5) |
| `0x30BF38` (FVF skinned fragment) | c133..c136 (instr 15–18) | yes (instr 19, 21) |

The fold is: instr `mul` by **c97**, then `mad` by **c98** — a
post-projection scale+offset in guest pixel space. It is written by renderer
command 3, `sub_0016F1C0` (packet `0x41EA4`, index 97 at `0x16F210`, 8 floats
copied from a view-transform object at `a2+4`): on the back buffer those
floats are scaled by `sub_00128BF0`'s scale/offset (`0x16F249` call); for a
render-to-texture view (target slot non-zero at `0x16F765` in `sub_0016F740`'s
sibling path) scale 1, offset 0 is used instead.

c121..c124, by contrast, is built once in `sub_00171A30`
(`D3DDevice_SetVertexShaderConstant4(121, …)` at `0x171DED`, from
`renderer+0x6B0` × `sub_0022E296(sx, sy, 16777215)` where `16777215.0` is the
float at `0x2ECB78` returned by `sub_00128BC0`) and re-pushed by
`sub_0016F740` (packet index 121 at `0x16F81E`, matrix at `renderer+0x428`
dwords = `+0x6B0`). Its scale/offset come from `sub_00128BF0` on the video
object: scale 2.0 in every mode except mode 2 (1.0), offset
`(0.53125 + modeX, 0.53125 + modeY)` from the mode table in §3, and its base
size is the picture `dword_35D448`/`dword_35D44C` (640, 448), converted at
`0x171D32`/`0x171D40`.

**Neither c97/c98 nor c121..c124 ever reads the aspect getter or the
widescreen byte.** `sub_00128BF0` reads only the video mode (`[this+0x270]`,
for the supersample scale) and the mode offsets (`+0x260`/`+0x264`). So:

- widening the §1 aspect changes world draws only;
- the c97/c98 fold is where a host viewport-offset shift (dx) would have to
  be reflected for world draws, because it is a pixel-space offset applied
  after projection;
- c121..c124 must stay centred on the guest 640x448 picture, or the movie
  and every 2-D element move with it.

### Every 2-D element goes through the one family

All reads of the UI reciprocals in the image (whole-image xref count):

| Constant | Readers |
|---|---|
| `0x2E88D4` = 1/640 | `sub_000C92E0` (x·1/640 helper), `sub_000C9650`, `sub_000CAE80` (9 reads), `sub_000CC3F6` (8, the tail of the same routine), `sub_001B9F80` |
| `0x2E88D8` = 1/448 | `sub_000C92F0`, `sub_000C9650`, `sub_000CAE80` (8), `sub_000CC3F6` (8) |

`docs/picture-rect.md` already established there is no second reciprocal
pair anywhere in the image. The family is:

- `sub_000CAE80` (+ `sub_000CC3F6`): the large 2-D/UI draw routine. At
  `0xCAEEA`/`0xCAF02` it multiplies pixel extents by 1/640 and 1/448,
  i.e. it emits positions **normalised to 0..1 in 640x448 space**, which
  c121..c124 then maps to supersampled pixels. Its helpers are
  `sub_000C9650` (text with shadow: adds the reciprocals as half-texel
  offsets at `0xC96C4`/`0xC96E1` and calls the text primitive
  `sub_00049A60` twice) and the `sub_000C92E0`/`sub_000C92F0` scalers.
- `sub_001B9F80`: the name-entry/keyboard widget (its only caller is
  `sub_00094A00` at `0x94A22`); it adds `flt_2E88D4` as a half-texel offset
  at `0x1BA747` and draws text through the same `sub_00049A60`.
- Everything submits through the shared primitives `sub_00049A60` (text) and
  the `sub_0013A0xx` render-context calls; `sub_000CAE80` also calls
  `sub_000C9650` 8 times and `sub_00049A60` 12 times.

So menus, HUD, dialogue text and the keyboard/name-entry screens are all
drawn in the same 640x448-normalised space through the same primitives and
the same c121..c124 screen program. There is no second 2-D coordinate
system in the XBE to widen.

**What I could not determine:** I cannot name the mood-wheel (or any
specific widget) in the XBE — widget layout and Edith-driven content live in
the game's data archives, not the image. The claim above is therefore
about the drawing path, not about individual widgets: any element the
engine code draws is drawn through this family, because these are the only
2-D normalisation constants and primitives in the image. If a specific
on-screen element were ever found to move when the aspect is widened, it
would have to be a world draw (c97/c98 fold path), not a member of this
family.

Known non-UI exceptions on the c121 path, already documented in
`docs/movie-quad.md`: the intro-movie quad (`sub_00173220`,
`DrawVerticesUP` at `0x173407`, same `0x30B8A0` program, corners in
normalised picture units). It is a picture, not a UI element, and it too is
immune to the aspect — it will simply stay centred in the guest picture
while the world widens around it.

---

## Method notes and residual uncertainty

- Function boundaries in this IDA database are occasionally split
  (e.g. `sub_000CC3F6` is the tail of `sub_000CAE80`); where a decompiled
  argument list disagreed with the push sequence, the asm was used
  (notably `sub_00016500` at `0x16708`–`0x16743`, where the decompiler
  misattributes the scratch stack slots that carry the FOV lerp and the
  aspect result).
- The video-flags bit names in §3 are labelled [INFERENCE]: the function
  (`sub_0022AC85`) and every use of each bit are observed; the names are the
  standard Xbox layout that those uses match one-for-one.
- The renderer matrix-slot question in §1 (which slot receives the
  projection vs the view matrix) was left open deliberately: it does not
  affect where the widening goes, and guessing it would risk sending the
  implementer to the wrong constants. The widening point is the aspect
  argument to `sub_001341F0`/`sub_00130BC0`.
- Nothing here was run. The runtime values (dashboard flags on this host,
  the actual video mode selected) are for the running agent to confirm —
  e.g. by logging VC[121..124] and the aspect at the first `sub_001341F0`
  call, or the guest `m[0]`/`m[5]` pair, whose ratio is the aspect in force.
