# Frame rate and game speed

Recorded 2026-10-19, from static analysis of `out/game.exe` only. **None of this has been
verified in play yet, because the port has not been run.** Every address below is a guest
address in that flat PE (guest addresses map 1:1), and every claim names the instruction
that proves it. The fix lives in `src/game/hooks.cpp`.

## The defect

Above 60 fps the whole game runs fast - at 144 fps it would run 2.4x fast, and uncapped
without bound. This is the same defect the sibling port (The Urbz, same EOR engine, one
year later) measured in play. The host pacer is not at fault: the engine measures real
time correctly and then quantises it to whole 1/60 s ticks with a floor of one tick, so
every frame shorter than 16.7 ms still advances the world by a whole 1/60 s.

## The clock

The engine clock object is built by `sub_001216A0`:

- it installs its vtable `0x2EC1C8` (`mov [esi], 0x2EC1C8` at `0x1216AA`), publishes
  itself at `[0x35D1A0]` (`0x1216B0`), and calls `QueryPerformanceFrequency` once with
  `&[0x35D198]` (`push 0x35D198` at `0x1216B8`, call at `0x1216C4`).

That XAPI pair is in this image, in the XAPILIB area:

- `QueryPerformanceCounter` at `0x22AC60`: `rdtsc` at `0x22AC64`, storing `eax` to its
  argument at `0x22AC66` and `edx` at `0x22AC68`, `retn 4` at `0x22AC6E`. Its xrefs
  include the clock elapsed vfuncs at `0x121717` and `0x121757`.
- `QueryPerformanceFrequency` at `0x22AC71`: it clears the argument's high dword
  (`and dword ptr [eax+4], 0` at `0x22AC75`) and writes the fixed **733,333,333**
  (`mov dword ptr [eax], 0x2BB5C755` at `0x22AC79`), `retn 4` at `0x22AC82`.

So the clock is the console CPU clock and its seconds are real. Elapsed time comes from
two identical vfuncs, `sub_00121710` (vtable+0x10) and `sub_00121750` (vtable+0x14):
both call the counter (`0x121717` / `0x121757`), load the frequency at `[0x35D198]`
(`fildll` at `0x121736` / `0x121776`) and divide (`fdivrp` at `0x12173C` / `0x12177C`).

## The quantiser and the frame tick

The frame tick is `sub_00123430` (`this` in `ecx`, no stack arguments), reached in play
through the game frame subclass's thunk `sub_00165E30` at vtable+0x18 of `0x2EED28`.
Per frame it:

1. stamps its `this+0x10` timestamp through clock vfunc+0xC (`push edx` at `0x123453`,
   call at `0x123457`);
2. takes elapsed seconds through vfunc+0x10 (`push edi` (`this+8`) at `0x123475`, call
   at `0x12347D`, `fstp` to a float at `0x123480`) and restamps `this+8` (call at
   `0x12348D`), accumulating the raw total as a double at `[0x35D238]` (`fadd` at
   `0x123494`, `fstp` at `0x1234A1`);
3. calls the quantiser `sub_001232F0` with that float (`push ecx` at `0x12349E`, call at
   `0x1234A7` - the quantiser's only call site in the image);
4. makes the engine's one frame delta and stores it at **`[0x30AAD4]`** (`movss` at
   `0x123542`), with its reciprocal at `[0x30AAD8]` (computed at `0x12354C`, stored at
   `0x12355D`).

The tick size is a writable float at **`[0x35D218]`**, set once by the frame object's
constructor `sub_00123A00`: 0.02 s from `[0x2E2428]` (`movss` at `0x123A5B`) when
`[0x2E3D78]` equals 1 (`cmp` at `0x123A70` - the 50 Hz case), otherwise 1/60 s from
`[0x2E8838]` (`movss` at `0x123A7E`, the float `0x3C888889`), stored at `0x123A8B`.

The quantiser `sub_001232F0(this in ecx, float elapsed, retn 4)` at `0x12338B` /
`0x1233A4` / `0x1233E7`:

- divides by `[0x35D218]` (`fdiv` at `0x123343` on its fixed-step path, `fdiv` at
  `0x1233AE` on the default path) and truncates to an int tick count with `fistp` to
  **`[0x30AAD0]`** (`0x123366` / `0x1233D1`) - note this title truncates; it does not add
  0.5 the way the sibling's quantiser does;
- on the fixed-step path only, clamps that count to 1..15 (`cmp eax, 1` at `0x123371`,
  `cmp eax, 0xF` at `0x12338E`);
- adds the count to the running total at **`[0x35D230]`** (`0x12337F` / `0x123398` /
  `0x1233E0`) and returns it in `eax`.

### The delta global, and the reader count that confirms it

`[0x30AAD4]` is the single delta the rest of the engine multiplies by. Counted in the
flat PE: **143 read xrefs, 126 of them in 70 recognised functions** (the other 17 sites
sit in code the analysis database has not split into functions), against **3 writes** -
the frame tick at `0x123542` and a scoped save/restore pair (`sub_001EA360` writes it at
`0x1EA69A` after saving the old value at `0x1EA692`; `sub_001E9190` restores it at
`0x1E9198`). That shape - one writer per frame, readers everywhere - is the
confirmation, and it is stronger than the sibling's (97 reads in 63 functions there).
The reciprocal at `[0x30AAD8]` has no reader besides its writer.

The tick count at `[0x30AAD0]` has three other readers: the tick itself at `0x1234B4`,
and the getter `sub_001233F0` (byte cmp at `0x1233F0`, count read at `0x1233F4`), which returns 1 whenever the fixed-step byte
below is clear; its only caller is `sub_001651A0` at `0x1651C8`. The running total at
`[0x35D230]` has one other reader, at `0x129E49`.

### Both caller paths floor a frame at one tick

Which delta the tick makes depends on the frame object's byte at `this+5`:

- **Fixed-step** (byte set, tested at `0x1234AC`): `dt = ticks * tick_size`
  (`cvtsi2ss [0x30AAD0]` at `0x1234B4`, `mulss [0x35D218]` at `0x1234BC`). But the
  quantiser's own fixed path (byte tested at `0x1232F1`) does not even use the caller's
  elapsed: it quantises the largest of four history floats at `[0x30AAE0]`,
  `[0x30AAE4]`, `[0x30AAEC]`, `[0x30AAF0]` (`movss` at `0x1232FB`, `0x123303`,
  `0x123313`, `0x12331B`), a history the tick's sibling method `sub_001235C0` shifts
  and resamples.
- **Default** (byte clear): the elapsed is clamped to `[tick_size, tick_size * 15]`
  (`movss [0x35D218]` at `0x1234C6`; `mulss` by the 15.0 at `[0x2E3A50]` at `0x1234E0`)
  and averaged with the two previous clamped values (`addss` at `0x123518` and
  `0x12351C`, `mulss` by the 1/3 at `[0x2E873C]` at `0x123520`, history stored at
  `0x123528` / `0x123530`). The floor is one tick, so a fast frame still becomes 1/60 s.

That byte is a fixed-step switch nobody flips in this image: the constructor clears it
(`mov [esi+5], bl` at `0x123A69`, with `bl` zeroed at `0x123A35`), the game's frame
subclass clears it again on init (`mov byte ptr [esi+5], 0` at `0x165ED5` in
`sub_00165EC0`), and no instruction in the image sets it. So in play the default path
runs, and at 144 fps the world would advance 144 x 1/60 = **2.4 s of game time per real
second** - the sibling's measured defect, reproduced exactly.

## The fix (`src/game/hooks.cpp`)

Two wrappers, both gated by the same switch:

- **`sub_001232F0` (the quantiser)** keeps the engine's measurement and replaces only
  the quantisation: ticks of **1 ms**, the engine's own ceiling kept (15 ticks of
  1/60 s = **0.25 s**, i.e. 250 of the new ticks), and `dt / ticks` written back into
  the tick-size float at `[0x35D218]`, so the caller's `ticks * [0x35D218]` reproduces
  the time that really passed. It stores the tick count at `[0x30AAD0]`, adds it to the
  running total at `[0x35D230]`, returns it in `eax`, publishes `dt` (seconds) into a
  file-scope `std::atomic<double> g_dt` for other wrappers, and pops its one stack
  argument (`ctx.esp += 8`, emulating its `retn 4`). An elapsed that is not positive
  (or is NaN) takes the smallest step, as the original clamp did.
- **`sub_00123430` (the frame tick)** writes nothing. It sets the frame object's
  fixed-step byte at `this+5` for the duration of the original call and restores the
  saved value afterwards, so the tick makes its delta by its own product path
  (`0x1234B4` / `0x1234BC` from the host quantiser's count and written-back tick size)
  instead of the default path's tick-floored three-frame average. Without this, the
  quantiser replacement alone would leave that average in charge: it would cap a
  60 fps frame's delta at 15 of the new 1 ms ticks and floor a fast frame at one of
  them. Inside the image the byte's only readers are the quantiser's test at
  `0x1232F1` (whose host replacement does not consult it), this tick's test at
  `0x1234AC`, and the getter's byte cmp at `0x1233F0`, whose sole call site (`0x1651C8`) is
  outside the tick and sees the restored value.

The same file also replaces **`D3DDevice_InsertCallback` at `0x24D030`**, which the SDK
does not implement: on the console it writes a callback token (token word `0x00081D8C`
written at `0x24D04B`) into the push buffer for the GPU interrupt handler to run, and
here that token would land in a scratch buffer the renderer walks for draws only, so
the callback - and whatever semaphore it releases - would never fire. The host
replacement runs the callback immediately, because the host renderer has no GPU
timeline the guest can be behind. Its argument order was read from the body, not
assumed: the second stack argument is stored at token+4 (`0x24D043` loads it,
`0x24D051` stores it) and the third at token+8 (`0x24D047` / `0x24D05A`), the first
only selecting the longer token form (loaded at `0x24D054`, tested at `0x24D058`), and
the function is `retn 0xC` (`0x24D073` / `0x24D098`). Guest `.text` reaches it through
the thunk at `0x16E6B0`.

## Environment switches

| Variable | Effect when set (and not `"0"`) |
| --- | --- |
| `SIMSBO_NO_STEP` | Both frame-step wrappers run the original recompiled functions untouched: the stock 1/60 s quantiser, the stock default delta path, game speed tied to frame rate as on a fast display. |

## Per-call consumers: what was hunted, what was proven

The sibling found five subsystems that step per *call* rather than per delta (camera
zoom rate, pad button auto-repeat, the menu held-direction repeat gate, the idle camera
auto-return acceleration, and the shot-mode camera glide), each then scaling with the
frame rate. They were hunted here by the same route - the pad poll and manager, the pad
object's timers, a scan of the image for per-call byte countdowns, and the camera
strings. **No wrapper in this batch comes from this section: none of the five could be
proven per-call in this image, so none was written.** A wrong wrapper is worse than a
missing one - the sibling records one it had to revert (scaling the script-binding
executor's stick floats made walking ten times too slow, because that handler already
applied frame time).

What the hunt did prove, as negative evidence:

- **The pad object's own timers are already delta-driven.** The pad poll
  `sub_00126660` calls `XInputGetState` (call at `0x12667A`, the function's only guest
  caller besides its offset-table entry) and ends by calling the pad's vfunc+0x3C,
  `sub_0012A270`, which counts its two float timers at pad+0x78 and pad+0x7C down
  *by the frame delta*: `subss [0x30AAD4]` at `0x12A284` and at `0x12A2B1`. Scaling
  those would correct them twice.

Leads, unproven and deliberately not wrapped:

- **Pad button auto-repeat.** No per-call countdown was found in game code: an image
  scan for `dec`/`sub` on a byte or word in memory turns up only XAPILIB/DSOUND library
  functions (e.g. `sub_0029C6D2` at `0x29C6F0`, the `sub_002DF030` cluster), nothing in
  the engine's `.text`. This title's input layer is action-based (`sub_00125220` rolls
  the pressed/held history words, `sub_00125670` sets binding state words at node+28),
  so if a repeat exists it is likely expressed with the delta or with tick counts
  rather than a byte reload pair like the sibling's delay/rate bytes at pad+0x1D8 /
  pad+0x1D9 - and neither byte nor an equivalent field pair was identified on this
  title's 240-byte pad object (built by `sub_00126920`, vtable `0x2EC698`).
- **Camera zoom, idle auto-return, shot-mode glide, menu held-repeat gate.** The camera
  in this title is game-side, not in the engine core that holds the clock, pad and
  frame tick (guest `0x11xxxx`-`0x16xxxx`), and the only camera-named strings in the
  image are cheat/config names (`Cam_First_Per` at `0x2E3798`, `Cam_Tilt` at
  `0x2E37A8`, registered as bytes of a flags struct by `sub_00034360`) and a script
  property (`cameraIntensity` at `0x2E373C`). No camera update function, rotate/zoom
  handler pair, or held-direction gate was identified with the certainty a wrapper
  needs: no function was found that applies a per-call rate constant to a camera
  distance/angle field without also reading `[0x30AAD4]`. These remain the first
  things to re-hunt in play, where the camera object can be watched live; the
  `g_dt` published by the quantiser wrapper is already in place for whichever of
  them proves per-call, in the sibling's shape (scale the per-frame constant by
  `dt * 60` around the `__imp__` call, restore it afterwards, one `SIMSBO_NO_*`
  opt-out each).

## Status

Static analysis and a syntax-checked translation unit only. The port has not been run,
so the quantiser, the frame-tick byte, and the callback replacement are all **unverified
in play**: the first real run should check that game seconds track wall seconds at a
60 fps cap, at a raised cap, and uncapped (the sibling verified the equivalent fix by
summing the engine's own dt against the wall clock at 1.000 in all three), and that
menus, camera and walking move at console speed at each.
