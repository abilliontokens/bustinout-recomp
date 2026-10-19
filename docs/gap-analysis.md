# Gap analysis

Recorded 2026-10-19. Everything below is counted or read from files in this repository; nothing
here is claimed to have been exercised in play. Companion to `docs/status.md`.

## 1. The remaining `[symbols]` names at `0`

`config/game.toml` today: **242 `[symbols]` names, 194 at a non-zero address, 48 at `0`, and
12 of 12 `[data]` symbols named** (recounted from the file). A name at `0` becomes a trapping
stub: the host traps only if guest code calls it. Grouped by why:

### a) Section absent from this XBE — 41 names (0 is correct, not a gap)

This XBE's sections are `.text, D3D, XGRPH, DSOUND, XMV, XPP, .rdata, .data, DOLBY, $$XTINFO,
$$XTIMAGE, $$XSIMAGE, .XTLID` (`docs/xbe-analysis.md`). There is no BINK, XNET or XONLINE section,
and their `.XTLID` ids (`0x30001`–`0x4003B`) are not among the 479 ids this XBE exports, so no
guest code calls them.

- **Bink (4)** — video is XMV here, as the `XMV` section and the 32 `.xmv` files on the disc
  show: `BinkDoFrame`, `BinkGetSummary`, `BinkNextFrame`, `BinkWait`.
- **Sockets / WSA / XNet / XOnline (37)**: `accept`, `bind`, `closesocket`, `connect`,
  `getsockname`, `getsockopt`, `ioctlsocket`, `listen`, `recv`, `recvfrom`, `select`, `send`,
  `sendto`, `setsockopt`, `shutdown`, `socket`, `WSACancelOverlappedIO`, `WSAGetLastError`,
  `WSAGetOverlappedResult`, `WSARecv`, `WSARecvFrom`, `WSASend`, `WSASendTo`, `WSAStartup`,
  `XNetCreateKey`, `XNetDnsLookup`, `XNetDnsRelease`, `XNetGetEthernetLinkStatus`,
  `XNetGetTitleXnAddr`, `XNetInAddrToXnAddr`, `XNetRandom`, `XNetRegisterKey`, `XNetStartup`,
  `XNetUnregisterKey`, `XNetXnAddrToInAddr`, `XOnlineCleanup`, `XOnlineStartup`.

### b) Genuinely unmatched — 7 names

`crt_atan`, `crt_cos`, `crt_log`, `crt_log10`, `crt_sin`, `crt_sqrt`, `crt_tan`. Unlike group
(a), the CRT **is** linked: LIBCMT code sits in `.text`, and its neighbours were found —
`crt_pow` at `0x220A24`, `crt_acos` at `0x21EAF1`, `crt_asin` at `0x220879` (with `_CIacos` /
`_CIasin`). These seven carry constant sets too small for the constant matcher (mostly float
masks), so they remain unnamed; the in-image copies serve guest callers, and naming them matters
only if host code ever needs to call them by name (the SDK computes the `crt_*` math on the host
to avoid the Xbox x87 result mismatch). Lead: they sit in the same link cluster in link order
(`docs/xbe-analysis.md`).

No name at `0` is in a third state: every one of the 48 is accounted for by (a) or (b). The D3D
section's remaining unnamed entries are a different census — 655 unnamed IDA entries of 760 in
`0x243D00`–`0x259763`, most of them library internals with no XDK API name — enumerated in
`docs/d3d-gap-functions.md`, including its miniport risk register: 16 functions touch
`[CDevice+0x1C28]` directly, and the still-unnamed ones (`0x244120`, `0x247670`, `0x248140`,
`0x248AD0`, `0x24CFE0` / `0x24D1D0`) are future same-shape crashes if ever reached.

## 2. `d3d: MakeSpace from <addr>` — inline push-buffer writers

The verbose log (`out/build/linux-verbose/logs/simsbo_001.log`) repeats, roughly every 0.63 s
from 4.160 s on:

```
d3d: MakeSpace from 00170CBD, some caller still pushes hardware commands
d3d: MakeSpace from 0016F815, some caller still pushes hardware commands
d3d: MakeSpace from 0016F20C, some caller still pushes hardware commands
d3d: MakeSpace from 00170B0E, some caller still pushes hardware commands
```

Four distinct guest `.text` return addresses: **`0016F20C`, `0016F815`, `00170B0E`, `00170CBD`**.
`D3DDevice_MakeSpace` itself is named (`0x24D5B0` in `config/game.toml`); the log line means a
caller obtained push-buffer space and is writing hardware commands into it inline, rather than
going through a named `D3DDevice_*` entry the host intercepts.

What the sibling port says this symptom means (`/home/work/Desktop/Projects/urbz-recomp/docs/
status.md` §"Input, sound and the menu screens" item 3, and `docs/input-and-draw-path.md` §3):
the engine writes its own push-buffer packets — in the sibling, vertex-shader constant packets
(`0x41EA4` head + `0x0B80` inline data) from its writer family — and if the host's push walker
skips those packets, or only drains the ring at a few points, the state they carry never reaches
the renderer: projection constants stayed zero, and draws snapshotted the previous drain's
matrices. The sibling's fix was to parse those packets into its constant store and drain the
ring at the top of every draw entry point. The four callers above are this title's equivalent
writer sites, not yet identified packet-by-packet; until they are, any state they push inline
(vertex constants, inline vertex data, texture switches in the sibling's census) is a candidate
explanation for a presented picture that is not the game's picture — but that connection is a
lead, **not proven**: no packet from these four callers has been decoded yet.

## 3. Unimplemented-opcode census

`generated/game/x86_recomp_stats.txt`: 27,495 functions, 895,135 instructions, and 389
unimplemented-opcode reports across 25 mnemonics:

```
aaa 26   aad 7    aam 16   aas 17   arpl 13  bound 69  cvttsd2si 1  daa 50
das 26   enter 10 femms 3  insb 3   int1 7   into 4    iretd 1      lds 13
les 101  lldt 1   lss 1    outsb 11 outsd 2  prefetch 1 salc 1      sldt 4
str 1
```

The sibling port's note (`urbz-recomp/docs/status.md`, "Known gaps" item 4) applies to the same
shape of list: these are typical of **data decoded as code** — `les` (101), `bound` (69),
`daa` (50), `aaa`/`aas`/`das` and friends are not instructions a compiler emits into real
functions, and if they sit only in data they are harmless; if any sits inside a real function it
needs an `[xbe] data_ranges` or `[[function]]` entry.

The one exception to call out: **`cvttsd2si` (1)** is a real SSE2 instruction (scalar
double-to-integer conversion), not BCD/string debris. A single occurrence could still be a data
word that happens to decode as it, but unlike the rest of the list it cannot be dismissed by
family; it needs its address checked against the function map before it is declared noise. The
same caution applies in miniature to `prefetch` (1) and `femms` (3, 3DNow!/EMMS-family).

## 4. Per-call frame-rate consumers — unproven leads

`docs/frame-rate.md` proves the quantiser defect and its fix statically, and records the hunt
for the sibling's five per-call subsystems (camera zoom rate, pad button auto-repeat, menu
held-direction repeat gate, idle camera auto-return acceleration, shot-mode camera glide).
**None of the five could be proven per-call in this image, so none was wrapped** — a wrong
wrapper is worse than a missing one. What is proven as negative evidence: the pad object's own
timers are already delta-driven (`sub_0012A270` counts down by `[0x30AAD4]` at `0x12A284` and
`0x12A2B1`), so scaling them would correct them twice.

The unproven leads, as listed there:

- **Pad button auto-repeat.** No per-call byte/word countdown exists in game `.text` (an image
  scan turns up only XAPILIB/DSOUND library functions). This title's input layer is action-based
  (`sub_00125220` rolls pressed/held history, `sub_00125670` sets binding state at node+28), so a
  repeat is likely expressed with the delta or tick counts — and no equivalent of the sibling's
  delay/rate byte pair was identified on this title's 240-byte pad object (`sub_00126920`,
  vtable `0x2EC698`).
- **Camera zoom, idle auto-return, shot-mode glide, menu held-repeat gate.** The camera here is
  game-side, not in the engine core holding the clock/pad/tick; the only camera-named strings
  are cheat/config names (`Cam_First_Per` `0x2E3798`, `Cam_Tilt` `0x2E37A8`) and a script
  property (`cameraIntensity` `0x2E373C`). No camera update function was found that applies a
  per-call rate constant without also reading `[0x30AAD4]`. These are the first things to
  re-hunt in play, where the camera object can be watched live; the `g_dt` published by the
  quantiser wrapper is already in place for whichever proves per-call.

Until a run measures game seconds against wall seconds (the sibling verified its equivalent fix
by summing the engine's own dt against the wall clock at 1.000 at a 60 fps cap, a raised cap and
uncapped), the frame-step fix and every consumer above remain **not proven**.
