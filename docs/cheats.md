# Console cheats

The F1 panel's **Cheats** section enters the game's own pad codes the game's own way
(`src/game/cheats.cpp`, `src/common/cheats.h`). **None of this has been verified in play: the port
has not been run yet.** Everything below is static evidence from `out/game.exe` (guest addresses
map 1:1), cross-checked once against the publicly documented Xbox code list at the end.

## Recogniser

`sub_00011F20`, called at `0x12393` from the per-frame update `sub_00012280` (vtable slot at
`0x2E2234`) with `ecx` = the recogniser object at `[app+0x1E4]` and one stack argument = the
active pad. The update only reaches it while the byte at `0x3DCF70` is clear (`0x122CB`), the pad
manager `dword_35D3F0` is non-null (`0x122DE`), the pad reports a fresh press (`sub_001250C0`
returns the word at `[[pad+4]+24]`), and the recogniser object exists (`0x12372`).

The recogniser:

- reads the fresh press, insists it carry exactly one set bit (`sub_00011990` counts bits), and
  insists it intersect the combined mask at `0x310B9C` (`0x11F47: and eax, dword_310B9C`);
- appends it to a six-entry ring at `[this+0x10]`, eight bytes a press — the button word, then an
  expiry float: now × 1000.0 (`fmul dbl_2E2290` at `0x11F77`; `dbl_2E2290` = 1000.0) + 1500.0
  (`fadd dword_2E2288` at `0x11F8B`; the float at `0x2E2288` is 1500.0). `sub_000119D0` clears a
  press once now reaches its expiry, so a code must be completed within 1.5 s of its first press
  (in the timer's millisecond scale);
- matches the ring against eight table rows (`0x12069: cmp al, 8`): a row is eligible when the OR
  of the ring covers the row's combined mask at `0x310BFE + 2*row` (`0x11FC4`), and matches when
  its words at `0x310B9E + 12*row + 2*i` (`0x12005: mov cx, word ptr dword_310B9C+2[ecx*2]`) appear
  in the ring in order, 0 terminating the row;
- on a match calls the setter (`0x12048: mov ecx, 0x310B98`; `0x1204D: push row`; `0x1204E: call
  sub_00072010`) and empties the ring.

## Table builder

`sub_000720A0`, called at `0x730FA` from the options constructor `sub_00072A80` (itself called at
`0xF76C5` from `sub_000F76C0` with `ecx = 0x310B98`). It writes, into the options object at
`0x310B98`:

- the eight rows, six button words each, from `[ecx+6]` (`0x720E3: lea edx, [ecx+6]`), rows twelve
  bytes apart — i.e. the table at `0x310B9E`;
- each row's combined (OR) mask at `[ecx+0x66] + 2*row` (`0x720E0: lea eax, [ecx+66h]`; the OR loop
  at `0x721B0`–`0x721E6`) — i.e. at `0x310BFE`;
- the OR of all rows at `[ecx+4]` (`0x721EB: mov [ecx+4], bx`) — the combined press mask at
  `0x310B9C`.

The table is built in code at run time, not stored as bytes, which is why no byte search of the
XBE finds the sequences. Registers at entry: `eax = 0x4000` (`0x720A4`), `ebp = 8` (`0x720AB`),
`ebx = 4` (`0x720B0`), `esi = 2` (`0x720B5`), `edi = 0x8000` (`0x720BB`), `edi = 0` after
`0x720EE`.

## Pad layout

Built by `sub_00126270` from the XInput state at `[pad+0x98]` (`0x126270: mov dx, [ecx+98h]`).
Analog button bytes (`[ecx+0x9A]`…`[ecx+0xA1]`): A `0x40` (`0x1262C0`), B `0x20` (`0x1262E1`),
X `0x80` (`0x1262D6`), Y `0x10` (`0x1262CB`), Black `0x2` (`0x1262B5`), White `0x1` (`0x1262AA`),
L `0x4` (`0x126294`), R `0x8` (`0x12629F`). D-pad bits of `wButtons`: Up `0x1000` (`0x1262F0`),
Down `0x4000` (`0x1262E8`), Left `0x8000` (`0x1262F8`), Right `0x2000` (`0x126300`).

## The rows

Every row, in builder order (each word is one `mov [ecx+disp], reg/imm` in `sub_000720A0`; row 0:
`[ecx+6] = 8` at `0x72108`, `[ecx+8] = 4` at `0x720F0`, `[ecx+0xA] = 0x4000` at `0x720C0`,
`[ecx+0xC] = 2` at `0x7210B`, `[ecx+0xE] = 0x8000` at `0x720E6`, `[ecx+0x10] = 0x20` at `0x7210F`;
rows 1–7 likewise from `[ecx+0x12]` upward). "Bit" is the bit of the effect word the row toggles.

| Row | Masks (hex) | Xbox sequence | Bit | Effect |
| --- | --- | --- | --- | --- |
| 0 | 0008 0004 4000 0002 8000 0020 | R L Down Black Left B | 0 | Cheat gnome |
| 1 | 0004 0002 2000 0080 8000 | L Black Right X Left | 1 | Give Money (gnome) |
| 2 | 0008 8000 0020 4000 0001 | R Left B Down White | 2 | Fill Motives (gnome) |
| 3 | 0002 1000 0010 4000 0008 | Black Up Y Down R | 3 | Unlock All Objects |
| 4 | 0002 4000 0008 0004 4000 0010 | Black Down R L Down Y | 4 | Unlock All Locations |
| 5 | 0004 0008 0040 4000 0002 | L R A Down Black | 5 | Unlock All Socials |
| 6 | 0004 0020 0040 0002 4000 0010 | L B A Black Down Y | 6 | Unlock All Skins |
| 7 | 0008 0002 4000 | R Black Down | 7 | Team picture |

## Effect word and its readers

Setter `sub_00072010` (`ecx` = options `0x310B98`, row as its one stack argument, `retn 4` at
`0x72097`) flips the bit at `0x72028: xor [eax+76h], dx` — the effect word at **`0x310C0E`** — and
plays sound `0x350B962D` when the bit turns on (`push` at `0x72078`) or `0x4A7EF64D` when it turns
off (`push` at `0x7207F`), both through `sub_0006B050` (`0x72084`). `sub_000F9AC0`, a method in
the same front-end vtable cluster (slot at `0x2EADE4`), clears the word (`word_310C0E = 0` is
its first write).

Readers:

- The SimAntics global getter `sub_001E9680`: cases 46 (`0x1E969F`) and 47 (`0x1E96BD`) both call
  the mask getter `sub_00071FC0` (`mov ax, [ecx+76h]` at `0x71FCD`) and hand the word out, so
  Edith trees see row *n* as flag *n+1* of globals 46 and 47 (Edith flags count from 1). Rows
  0–2 have no native reader besides this getter: the gnome (whose unlock text
  `unlocked_gnome_message` sits at `0x2E90AC` among the unlock messages) appears and offers the
  money and motives entries while those flags are set. The gnome's trees live in the game's data
  archives, not the XBE, so the pie-menu wiring itself was not read here — only the getter that
  feeds it is in the image.
- Native bit tests through `sub_00071FE0` (`[ecx+76h] & (1 << row)` at `0x71FEE`–`0x71FFD`):
  row 3 in `sub_0009A910` (an item's locked byte, read from the build/buy code in `sub_0009C6B0`,
  `sub_0009EAB0`, `sub_000A1C00` — `locked_object_message`, `showBuildBuyItems`); row 4 in the
  sixteen-entry list `sub_00077440`/`sub_00076DD0` and in the primitive dispatcher below; row 5 in
  `sub_001CF3C0` cases 40 and 41 (entries 19–22 and 29–32 read as `0x7FFF` while set) and row 4 in
  its case 34 (entry 6 reads as `0x7FFF`); row 6 in the unlockables lock test `sub_0002BD90`
  (`"Bustin Out Unlockables"` at `0x2E334C`), called from the Create-a-Family item browsers
  `sub_0002D2D0`/`sub_0002D330`; row 7 in `sub_000F7CA0`, which mirrors the bit into bit 0 of the
  object it updates each frame.

## The latch

The byte at **`0x3DCF70`** gates everything: the frame update skips the recogniser while it is
set (`0x122CB`), the setter returns without flipping (`0x72010`–`0x72019`), the mask getter
returns 0 (`0x71FC0`–`0x71FCC`) and the bit test returns false (`0x71FE0`–`0x71FEB`). The state
entries `sub_000E5370` (`0xE5388`), `sub_000E74A0` (`0xE7506`), `sub_000F7630` (`0xF76AC`) and
`sub_000F8770` (`0xF87CC`) all clear it; no instruction in this image sets it (all eleven
references were read). What sets it on the console — if anything in this build — was not found.

## Flow gate used by the host entry

The host override of `sub_00012280` drains the panel's queue at the top of the frame, before the
original body, and re-checks the game's own conditions for recognising a code, in the game's own
form: latch clear (`0x122CB`), pad manager `dword_35D3F0 != 0` (`0x122DE`), and recogniser object
`[app+0x1E4] != 0` (`0x12372`, read from the update's `ecx`). The render-thread gate
(`cheats::InGame`) additionally requires the table to have been built (combined mask at
`0x310B9C` non-zero, written at `0x721EB`). This title has no `flowGoto`-style numbered flow
object like the sequel's; the per-frame update's own checks are the gate the game itself uses, so
they are the gate the host uses. No separate "credits" code exists here: the team picture is
ordinary row 7.

## Cross-check against the publicly documented list

The documented Xbox list (pause, then enter; e.g. XboxHome's and SuperCheats' Bustin' Out pages,
checked 2026-10-19) names: Cheat gnome `R L Down Black Left B`; Give Money `L Black Right X
Left`; Raise/Fill Motives `R Left B Down White`; All Objects `Black Up Y Down R`; All Locations
`Black Down R L Down Y`; All Socials `L R A Down Black`; Team picture `R Black Down`. All seven
match rows 0–5 and 7 of the binary table exactly, in the same order. Row 6 (`L B A Black Down Y`)
is absent from those two lists; its name here, Unlock All Skins, rests on its binary consumer
(the Create-a-Family/unlockables lock test) and on skins being one of the game's unlock
categories (`unlocked_skins_message` at `0x2E9048`; category 2 of the unlock display
`sub_000CFE30`) with no other row left to carry it. It is the one label not confirmed by a public
list.

Comparison with the sequel: seven of these eight sequences also appear in The Urbz's twelve-row
table, shifted by two rows (there they are rows 2–8, behind its gnome and money rows); the
meanings the sequel gives the shared sequences differ (its gnome is a separate row 0), so no
Urbz meaning was carried over — every label above is this title's.

## Not proved

- Nothing has been verified in play; the port has not been run.
- The gnome's Edith pie-menu wiring is in the data archives and was not read; rows 0–2 are proved
  as "read only through globals 46/47", and their names are the public list's.
- Row 6's name (above) is inferred from its consumer, not read from the image.
- What sets the latch byte at `0x3DCF70` was not found; in this image it is only ever cleared.
