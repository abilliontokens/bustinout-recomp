# Can the host simply tell the title the dashboard says widescreen?

Static analysis of `out/game.exe` (guest addresses 1:1) plus the SDK runtime
source. Nothing was built or run. Builds on `docs/guest-projection.md`,
`docs/picture-rect.md`, `docs/movie-quad.md`.

## Answer in brief

**Yes — and the host already does.** The widescreen bit comes from kernel
export 24, `ExQueryNonVolatileSetting`, index 8 (`XC_VIDEO_FLAGS`), which
`sdk/runtime/kernel/xboxkrnl.cpp` already implements. On any output 3:2 or
wider — the owner's 3440x1440 included — it already returns the widescreen
bit, so the guest byte at `[video+0x274]` should already be 1 and
`sub_001287C0` should already be returning 1.7777778. The 4:3 pillarbox the
owner sees is therefore a **host present-aspect problem, not a missing guest
flag**: the guest draws a 16:9 projection anamorphically squeezed into its
SD/480p buffer, and the host presents that buffer at 4:3.

Setting the flag costs **zero lines** (it is already set). It buys exactly
16:9 and no more: the getter hardcodes the float at `0x2ECB74`. For 21:9 the
host must change that one value (its only reader in the image is the getter)
or override the getter's return — after which the engine's own maths do true
Hor+ with no clamp anywhere in the path. What it would break, and the one
trade-off (anamorphic HUD stretch), are in §4.

---

## 1. Where the widescreen bit comes from

### Guest side

`sub_001293C0` (video-mode setup) at `0x12944F` calls `sub_0022AC85`:

```asm
0x22AC8A  push 0                 ; result length (out, unused)
0x22AC8C  push 4                 ; value length
0x22AC8E  lea  eax, [ebp+var_4]  ; value
0x22AC91  push eax
0x22AC92  lea  eax, [ebp+var_8]  ; type
0x22AC95  push eax
0x22AC96  push 8                 ; setting index 8
0x22AC98  call sub_0010A2B6
0x22AC9D  test eax, eax
0x22AC9F  jl   fail -> value = 0
0x22ACA4  shr  eax, 10h          ; HIWORD(value)
0x22ACA7  and  eax, 5Fh
```

`sub_0010A2B6` at `0x10A2B6` is a one-instruction thunk,
`jmp dword_2E1C7C` — a function-pointer slot filled from the kernel import
table. The 5-argument signature (index, type-out, value-out, length,
result-length-out), the NTSTATUS-style `< 0` failure test, and index 8's use
identify it as the XDK statically-linked call to the kernel export
**`ExQueryNonVolatileSetting`**, index 8 = `XC_VIDEO_FLAGS` [INFERENCE: the
name/index mapping is the standard Xbox one; the bit layout below matches
`XC_VIDEO_FLAGS` exactly and each bit's use in the mode selection confirms
it, as recorded in `docs/guest-projection.md`].

Back in `sub_001293C0`:

```asm
0x129456  (al & 1)  -> cl               ; widescreen
0x12945B  mov [esi+274h], cl            ; byte [video+0x274]
0x129459  test al, 8  -> mode [esi+270h] = 1 (480p)   @ 0x12946A
0x129474  test al, 2, and widescreen set (cmp cl,bl / jz @ 0x12947D)
              -> mode = 2 (720p)        @ 0x129481
```

Tail of `sub_0022AC85` (`0x22ACB3`–`0x22ACC5`): if `sub_0022C33B() != 1` or
`sub_0022C312() == 3`, the flags are ANDed with `0xFFFFFFF1`, stripping the
480p/720p bits. **The widescreen bit (bit 0) survives either way.**
`sub_0022C312` is a second `sub_0010A2B6` call with index 259 (`0x103`,
factory AV region), reading byte 1 of the result. `sub_0022C33B` returns
`*dword_2E1E1C`; that pointer slot has no writer in the image (its only xref
is the read at `0x22C33B`), so what it points at on this host could not be
determined statically — it does not affect the widescreen conclusion, only
whether the 480p bit survives (see §3).

### Host side — already implemented, already on

`sdk/runtime/kernel/xboxkrnl.cpp`:

```cpp
// widescreen (a 4:3 output plays the console's 4:3), 480p
static uint32_t VideoFlags() { return (app::Widescreen() ? 0x00010000 : 0) | 0x00080000; }

KERNEL_EXPORT(24, ExQueryNonVolatileSetting, 5) {
    ...
    case 8: v = VideoFlags(); break;          // XC_VIDEO_FLAGS
```

Kernel ordinal 24 is the export the guest's thunk resolves to. The same
`VideoFlags()` is also served on the whole-EEPROM path (index `0xFFFF`,
written at value+0xEC, `xboxkrnl.cpp:1246`) and through
`AvSendTVEncoderOption` (`xboxkrnl.cpp:1270`), and `app::Widescreen()` is
also mirrored into the D3D device display flags
(`sdk/runtime/xdk/d3d8.cpp:441`).

`0x00010000` → `HIWORD = 1` → guest widescreen bit set.
`app::Widescreen()` is `Shaped().wide` (`sdk/runtime/app/settings.cpp:205`),
where `wide = output.width * 2 >= output.height * 3` (3:2 or wider,
`settings.cpp:190`). On 3440x1440 that is true. Two consequences worth
noting:

- **`ultrawide = false` does not clear the guest widescreen byte.** The
  `ultrawide` setting only gates `AspectWiden()` (`settings.cpp:195`);
  `wide` is computed from the output shape alone.
- If `settings.toml` sets an explicit `width`/`height`, `Output()`
  (`settings.cpp:161-165`) uses those instead of the desktop, so a 4:3
  configured output would report a 4:3 TV. With the owner's settings
  (fullscreen desktop), it reports widescreen.

So the "one line" the question hoped for already exists. What has not been
proven (read-only assignment; the running agent can check) is the value at
run time: the host logs the first 32 queries
(`ExQueryNonVolatileSetting %X -> %X`, `xboxkrnl.cpp:1257`), so the log line
for index 8 settles it, as does the guest log of the mode `sub_001293C0`
selected.

---

## 2. What else the flag changes

### Every reader of `[video+0x274]`

Whole-image scan for the `274h` displacement (other hits are stack slots and
unrelated structures): exactly three instructions touch the video object's
byte —

| Address | Instruction | Role |
|---|---|---|
| `0x12868B` | `mov [esi+274h], al` in `sub_00128660` | constructor clears it |
| `0x12945B` | `mov [esi+274h], cl` in `sub_001293C0` | set from dashboard flags |
| `0x1287C4` | `cmp byte ptr [esi+274h], 0` in `sub_001287C0` | the aspect getter |

There is no other reader. Nothing else in the engine branches on widescreen.

### Every reader of the getter's result

`sub_001287C0` is reached only virtually (its sole xref is the vtable slot at
`0x2ECB08`, video vtable `off_2ECA78 + 0x90`). The proven video-object call
sites all feed the projection builder as its aspect argument
(`docs/guest-projection.md` §1): the camera setters `sub_000153E0`
(`0x15482`), `sub_000162C0` (`0x164B0`), `sub_00016500` (`0x1671D`),
`sub_00016B80` (`0x16C90`), and the second-view builder in `sub_0001D870`
(two calls through `*dword_35D438 + 144`, i.e. +0x90). No HUD, menu, text,
movie or asset-selection code calls it. (Ninety-one `call [reg+90h]` sites
exist image-wide; the rest are slot +0x90 of other objects' vtables.)

### Side effects inside `sub_001293C0` — confirmed, with one refinement

The shared-context claim is **confirmed**: in SD the flag changes only the
aspect and one present-parameter bit. The bit:

```asm
0x12948F  or [esi+250h], edx      ; edx = 0x10, pre-mode (redundant)
0x12955B  or [esi+250h], edx      ; effective OR, after the mode table
```

`[video+0x250]` is field 3 of the parameter block at `video+0x244` that
`sub_001293C0` passes to the device-create wrapper `sub_00173EC0` (call at
`0x129579`). There, asm at `0x173F48`–`0x173F50` copies it to
present-parameters +0x28 — the `Flags` dword. On the console that is the
anamorphic/widescreen present flag for the TV encoder. **On this host it is
a no-op**: `Direct3D_CreateDevice` reads `pp+0x28` only to log it
(`sdk/runtime/xdk/d3d8.cpp:478-479`).

Specifically checked and **not** affected by the widescreen byte:

- **Picture size / buffer size**: unchanged by bit 0. Sizes come from the
  separate mode field `[video+0x270]`, driven by the 480p/720p bits
  (`0x129495` on; table in `docs/guest-projection.md` §3). In SD widescreen
  the buffer stays 640x480 and the picture 640x448.
- **Viewport**: the title never calls `SetViewport`
  (`docs/picture-rect.md` §3); nothing in the flag path touches one.
- **HUD layout**: no reader. All 2-D goes through the 640x448-normalised
  family (`sub_000CAE80` and helpers; `docs/guest-projection.md` §2), none
  of which calls the getter or reads the byte.
- **The 2-D matrix c121..c124**: built in `sub_00171A30` from
  `sub_00128BF0`, which reads only the mode (`[video+0x270]`, supersample
  scale 2.0, or 1.0 in mode 2) and the centre offsets
  (`[video+0x260]/[video+0x264]`) — asm `0x128BF0`–`0x128C58` contains no
  `274h` access and no aspect. The c97/c98 fold (`sub_0016F1C0`) likewise.
- **Asset selection**: no branch on the byte or the getter anywhere outside
  the projection path. There are no 16:9-specific UI assets or layout in the
  image; the UI is drawn in the same 640x448 space in both modes.

One honest consequence of the native design: because the buffer and picture
do not change, native widescreen is **anamorphic** — the 16:9 projection is
squeezed into the 640-wide picture and the display stretches it back out.
Everything in the picture, HUD included, is stretched horizontally by the
same factor when presented at 16:9. That is how the title behaved on a
widescreen SD TV; a host route that widens the render target instead would
keep HUD pixels square, at the cost of the centre-shift machinery in
`docs/ultrawide.md`.

---

## 3. Does anything clamp the aspect? The 720p path?

### No clamp, anywhere in the path

- **Getter** (`0x1287C0` asm): `cmp byte [esi+274h],0` at `0x1287C4`; set →
  return the float at `0x2ECB74` (1.7777778); clear → call vtable +0x94/+0x98
  (present width/height getters, `sub_00128820`/`sub_00128830`) and divide.
  No comparison, range check, table or assert on either branch.
- **Builder** `sub_00130BC0`: the aspect argument is loaded at `0x130C59`
  (`fld [esp+arg_4]`) and divided into the cotangent at `0x130C5D`
  (`fdivr`), stored as `m[0]` at `0x130C5F`. No validation; any non-zero
  float works (zero would divide by zero — the only failure mode).
- **Wrapper** `sub_001341F0`: stores the aspect verbatim to `view+0x25C` at
  `0x13422F` and passes it on. The only clamp in the function is on the near
  plane, against the 3.2 float at `0x2ECF10` (`0x134219`–`0x134252`).
- **Camera callers**: pass the getter's return through unchanged
  (`docs/guest-projection.md` §1).

So 2.3888889 (21:9) flows through every projection exactly as 1.7777778
does. The dashboard bits alone cannot produce it — the getter hardcodes
`0x2ECB74` — but that float has **exactly one reader in the image**, the
getter at `0x1287CD` (whole-image xref count: 1). Changing that one float in
guest memory, or overriding the getter's return, is sufficient; the engine
then does its own Hor+ maths (frustum extraction in `sub_00133D80` /
`sub_001316B0` works from the built matrix, so culling follows the widened
frustum).

### The 1280x720 (mode 2) path — different branch, not taken, do not take it

Mode 2 requires **both** the 720p bit and widescreen
(`test al,2` at `0x129474`; `cmp cl,bl / jz` at `0x12947D` skips it
otherwise). The host's `VideoFlags()` never sets the 720p bit
(`0x00020000`), so the branch is unreachable from the current host. If it
were ever taken it would interfere, not help:

- buffer 1280x720, picture 1200x660, centre (40, 30) — contradicting the
  host's `app::kGame` picture assumptions (640x448 at top 16);
- `sub_00128BF0`'s supersample scale drops 2.0 → 1.0 (mode check at
  `0x128BF6`–`0x128C07`), changing c121..c124 and every 2-D draw;
- the multisample type changes (`sub_00128BD0`, `docs/movie-quad.md` §2).

Route around it: leave bit 1 clear. (The host does set the 480p bit,
`0x00080000`, unconditionally. Whether the guest keeps mode 1 (720x480 /
680x448) or the `sub_0022AC85` tail strips it back to mode 0 depends on the
undetermined `*dword_2E1E1C` above — but that is the existing baseline
either way, not something the widescreen work changes; the widescreen bit
survives both branches.)

---

## 4. What the host should do, and what it would break

1. **16:9 is free and probably already live in the guest.** The flag is
   already returned; if the owner's picture is pillarboxed at 4:3, the fix
   is on the present side: present the guest picture at the aspect the guest
   is actually projecting (16:9 when the byte is set) instead of
   `PresentSrcAspect()`'s auto = 4:3 (`sdk/runtime/gpu/vulkan.cpp:2075`).
   No guest-target widening, no c97/c98 shift, no read-back crop is needed
   for this step, because the guest's buffer geometry never changed.
2. **21:9 needs exactly one guest-visible value changed**: the float at
   `0x2ECB74` (or an equivalent getter override) set to the target aspect,
   plus presenting the unchanged guest buffer at that same aspect. The
   engine's projections, frustum and world rendering then do Hor+ unaided;
   2-D, movies, picture size, viewport and read-backs are untouched because
   none of them ever read the aspect.
3. **What it would break / trade off:**
   - HUD, menus, text and the movie are stretched horizontally by the
     anamorphic factor (16:9: ×1.33 relative to 4:3 presentation of the same
     pixels; 21:9: ×1.79). This is the title's own console behaviour, but
     it is a real visual difference from a widened-target route, and per
     project validation rules it must be judged in a loaded household, not
     menus.
   - Nothing else found: no clamp, no mode table, no asset switch, no
     second aspect consumer. The only hard failure mode is a zero aspect
     (divide-by-zero in `sub_00130BC0`).

## What could not be determined

- The run-time value of `*dword_2E1E1C` (read at `0x22C33B`; the pointer slot
  has no writer in the image — presumably filled from kernel/XDK data
  outside the image). It decides only whether the host's 480p bit survives
  into mode 1; the widescreen bit survives regardless. The running agent
  can settle it from the host's `ExQueryNonVolatileSetting 8 -> …` log line
  and the guest's selected mode.
- Whether the first `ExQueryNonVolatileSetting` call happens after SDL has
  a display/window to measure (affects only whether `app::Widescreen()`
  sees 3440x1440 on the very first query; the same log line settles it).
