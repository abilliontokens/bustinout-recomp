# Boot crash: signal 11 at 0.226 s — fault log, fix, result

Game: `assets/default.xbe` (XDK 5558, sha1 `6feea04a12fb134253abe2b5662da634d0048232`).
All guest addresses below were read from `out/game.exe` (flat PE, guest addresses 1:1,
regenerated with `python3 sdk/tools/symbols/xbe2pe.py config/game.toml out/game.exe`)
with `objdump -d`; function boundaries are the `int3` padding gaps between bodies.

## Fault 1 — the only fault so far (fixed)

### Observed state (pre-fix log, `out/bringup/run_2.log` before this change)

```
[ 0.154] Vulkan adapter: AMD BC-250 (RADV GFX1013) (vendor 1002 device 13fe, driver 109060100)
[ 0.226] error: signal 11 on guest thread 18
[ 0.226] error:   eax D009FC74 ecx 00266578 edx 00000000 ebx 00000A00 esp D009FC58 ebp D009FC94 esi 00000000 edi 00266578
[ 0.226] error:   [esp+008] 002530F1
[ 0.226] error:   [esp+040] 002479C8
[ 0.226] error:   [esp+084] 0024FE4B
[ 0.226] error:   [esp+088] 00266374   (data word inside the D3D section range, not a return address)
[ 0.226] error:   [esp+08C] 0024FEBC
[ 0.226] error:   [esp+0F8] 00245FC3
[ 0.226] error:   [esp+110] 0016B8EE   (game code)
```

`LogGuestStack` (`sdk/runtime/cpu/dispatch.cpp`) prints only stack words inside the
guest code range, so the dump mixes real return addresses with data words that happen
to fall in the D3D section (`0x266578`, `0x266374`, `D3D_Device` itself). The real
return addresses are the ones that sit one instruction past a `call`.

### Localisation

- `ecx = edi = 0x266578 = D3D_Device (0x264950) + 0x1C28` — the address of the NV2A
  miniport object embedded in the static `CDevice`. `esi = 0`: the miniport's field 0
  (hardware register-window base) was loaded and is NULL; the host never builds it.
- `0x2530F1` is the return address after `call 0x252D36` at `0x2530EC`, inside the
  unnamed function at **`0x2530DA`** (`push ebp … ret 0x1C`, thiscall, 7 stack args).
  Its second instruction, `0x2530E2: mov esi,[ecx]`, loads that NULL base.
- The faulting instruction is the first dereference of the base inside the poll
  helper **`0x252D36`**: `mov esi,[edi]` (again 0), then
  `0x252D3C: testb $0x10,0x3214(%esi)` reads guest VA `0x00003214` → SIGSEGV.
  Registers match exactly (`esi=0`, `edi=ecx=0x266578`, `edx=0`).
- Caller `0x2479C8` is the return after `call 0x2530DA` at `0x2479C3`, inside the
  unnamed function at **`0x247940`** (`ret 8`): it stores a compression tile at
  `[device + index*24 + 0x1AC0]`, then `add ecx,0x1C28; call 0x2530DA`. This is the
  library-internal body of `D3DDevice_SetTile`; the named entry `0x247A20`
  (already in `[symbols]`, already host-implemented as a no-op) is its
  KickOff-and-wrap thunk. Per the sibling analysis, do not name `0x2530DA`/`0x247940`
  as host APIs — they are miniport/library internals that must simply never run.
- Callers `0x24FE4B` / `0x24FEBC` are inside the unnamed presentation-params helper
  at **`0x24FB40`** (returns after its internal surface-creation call at `0x24FE46`,
  and after its `call 0x247940` SetTile at `0x24FEB7` → return `0x24FEBC`).
- Caller `0x245FC3` is the return after `call 0x24FB40` at `0x245FBE`, inside the
  unnamed function at **`0x245F90`**. Game frame `0x16B8EE` is the return after its
  `call 0x245F90` at `0x16B8E9`.

### Identification: `0x245F90` is `D3DDevice_Reset` — hypothesis proven

Body `0x245F90–0x246035`, `ret 4`, one stack argument (presentation parameters):

| Address | Instruction | Meaning |
|---|---|---|
| `0x245F94` | `mov edi,[0x264948]` | `edi = D3D_pDevice` |
| `0x245F9A` | `call 0x24D610` | `D3D_KickOffAndWaitForIdle` |
| `0x245F9F–AE` | `lea ebp,[edi+0x1C28]; mov ecx,ebp; call 0x251D40; test eax,eax; jne` loop | miniport poll |
| `0x245FB2` | `call 0x24FA10` | release old surfaces |
| `0x245FBE` | `call 0x24FB40` | validate params / set mode (contains the SetTile calls above) |
| `0x245FFF` | `call 0x24F204` (ecx = miniport) | miniport mode set |
| `0x24600E/14` | `mov edx,[edi+0x1A14]; mov ecx,[edi+0x1A20]; push ecx; push edx` | back buffer / auto-depth slots per `sdk/xdk/5558/layout.toml` |
| `0x246016` | `call 0x246040` | `D3DDevice_SetRenderTarget` |
| `0x24601B–2A` | `push 0; push 0x3F800000; push 0; push 3; push 0; push 0; call 0x248570` | `D3DDevice_Clear(0,0,3,0,1.0f,0)` |
| `0x246035` | `ret 4` | stdcall, 1 arg |

This is the textbook Reset shape, and it is also the crash path: title →
`Reset` → mode helper `0x24FB40` → SetTile body `0x247940` → miniport
`0x2530DA` → poll `0x252D36` → read `[NULL + 0x3214]`. Independently confirmed by
the `D3DGapSymbols` read-only analysis (same entry, same end `0x246038`).

### Fix

- `config/game.toml` `[symbols]`: `D3DDevice_Reset = 0x245F90`. This flips
  `HAS_D3DDevice_Reset` to 1, so the already-implemented host body
  `HLE_STDCALL(D3DDevice_Reset, 1)` in `sdk/runtime/xdk/d3d8.cpp` (~line 459)
  replaces the guest body: it flushes (the KickOff), re-runs `SetupPresentation`,
  and clears — the guest tail — without touching the miniport. The whole chain
  below (`0x24FB40`, `0x247940`, `0x2530DA`, `0x252D36`) becomes unreachable
  from Reset.
- `sdk/xdk/5558/signatures.toml`: `D3DDevice_Reset = {sec = "D3D", ret = 4, regs = []}`
  (documented no-signature seed form: no `len`/`hash`, so the byte matcher skips it;
  the record exists so the name survives a config regeneration). No `xtlid` is
  claimed: the image's `.XTLID` scan never exported this name, and no id was
  invented.

### Checks while in the area (no change needed)

- **Reset surface reuse**: `SetupPresentation` already reuses the front/back/depth
  triple when `(w, h, colour format, buffer count, auto-depth, depth format)` are
  unchanged (`sdk/runtime/xdk/d3d8.cpp` lines ~367–380, with the leak this prevents
  documented at lines ~352–360). Confirmed present; not modified.
- **All-black gamma ramp**: `VulkanBackend::SetGamma` (`sdk/runtime/gpu/vulkan.cpp`)
  already treats a fully zero ramp as identity. Confirmed; not modified.
- **Push-buffer callback**: `D3DDevice_InsertCallback = 0x24D030` is named, and
  `src/game/hooks.cpp` already replaces it with a host body that runs the callback
  immediately. Confirmed present. It was not implicated in this crash (the fault
  was at 0.133 s, before any frame), and the post-fix run shows no deadlock
  signature — see below.

## Observed result after the fix

`tools/bringup_loop.sh 10 40`, round 1 (`out/bringup/run_1.log`):

```
[ 0.037] Game: default.xbe in /home/work/Desktop/Projects/simsbustinout/out/build/linux-release/assets, sha1 6feea04a12fb134253abe2b5662da634d0048232
[ 0.133] Vulkan adapter: AMD BC-250 (RADV GFX1013) (vendor 1002 device 13fe, driver 109060100)
[39.996] Exit (0)
```

No `error:` lines, no missing-entry-point requests; the loop stopped after round 1.
Follow-up hand runs: 20 s and 12 s runs likewise end only at the `timeout` kill
(`Exit (0)`). At 8 s into the 12 s run the process was alive with 19 threads and
~15.8 s of user CPU — actively working, not idle-waiting (the InsertCallback
deadlock shape) and not spinning on a fault. `cmake --build --preset linux-release`
is green.

Caveat, stated honestly: release builds define `NDEBUG`, so `LOGI` perf/frame
lines are compiled out and no frame counter is visible in these logs; survival +
sustained CPU is the evidence here, not a photographed frame.

## Still failing / next lead

Nothing is failing: the boot crash is gone and no successive fault appeared in
40 s of play. Next lead, in order:

1. Prove a picture: run a verbose build (`-DCMAKE_BUILD_TYPE=RelWithDebInfo
   -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g"`, no `NDEBUG`) with `XBE_PERF=1`
   and confirm per-second draw/present counts, then `XBE_CAPTURE_DIR` + F3 for a
   frame dump.
2. Watch the first minutes for the known shapes from the sibling port: `d3d:
   MakeSpace from …, some caller still pushes hardware commands` in the verbose
   log (engine writing state inline into the push buffer), and any new
   small-offset-off-NULL crash — which would be another unnamed XDK function
   letting a console routine touch hardware; fix is always to name it, the same
   method as above.

Sibling register (D3DGapSymbols, read-only IDA analysis, not re-verified here):
16 D3D functions touch `[CDevice+0x1C28]` directly. Still unnamed and guest-running
— future same-shape crashes if ever reached: `0x244120` (xtlid 0x800C8,
adapter/display query), `0x247670`, `0x248140` (writes `[hwbase+0x68084C]`),
`0x248AD0`, `0x24CFE0` / `0x24D1D0` (spin on `[hwbase+0x100410]`). `0x24FA10`,
`0x2500E0`, `0x2505B0` are unreachable while Reset/CreateDevice/Release stay
host-implemented. If a later crash shows a small offset off NULL with
`ecx/edi = D3D_Device+0x1C28`, check these first.
