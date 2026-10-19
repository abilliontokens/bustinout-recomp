# D3D section gap functions — The Sims: Bustin' Out (XDK 5558)

Enumeration and naming of every function in the `D3D` section of `assets/default.xbe`
(guest `0x243D00`–`0x267140`, 144,436 bytes), with the miniport risk register behind the
boot crash. All addresses are guest addresses; `out/game.exe` (xbe2pe flat PE) maps them 1:1.
Companion to `docs/boot-crash.md` (the crash itself and the landed fix).

## Summary

| | |
|---|---|
| Code in the section | `0x243D00`–`0x259763` (ends with `D3DDevice_EnableOverlay` + `int3` pad) |
| Rest of the section | `0x259768`–`0x267140` is data: format/mode tables at `0x259768`+, string/blob data (`Xbox…` at `0x2597B0`), and the zeroed device globals (`D3D_Device` `0x264950`, `D3D_Streams` `0x266DF0`, …). No code was found there: no `int3`-delimited entries, no rel32 call targets into it except data references. |
| IDA entries in code range | **760** |
| Already named in `config/game.toml` (`[symbols]`, code) | **105** (incl. `D3DDevice_Reset`, landed during this investigation) |
| Unnamed IDA entries | **655** |
| Hand-split functions | **724** = 760 entries − 37 continuation pieces merged into their parent (see Method) + the `SetTile` thunk split back out. 22 of the 724 are 1-byte `ret` stubs that may be shared epilogues rather than callable functions, so the true count is 702–724. |
| Functions named by this investigation | **1 proven** (`D3DDevice_Reset`, in config), **14 graded candidates** (Direct3D-object cluster + 3 device queries), **~35 descriptive internal names** (not config names). The other ~620 unnamed entries are library internals with no XDK API name in either signature set; they are enumerated and clustered below but honestly left unnamed. |
| Direct `[CDevice+0x1C28]` touchers (Tier 1) | **16** functions (6 named+host-implemented, 10 unnamed) |
| Miniport-thiscall methods (Tier 2) | **16** functions |
| Game-called D3D entries (direct `call` from `.text`) | **87**, of which **23 unnamed** (listed in §5) |

## Method

1. IDA (`out/game.exe`, idalib) function list for `0x243D00`–`0x267140`: 760 entries.
2. Hand split check against `objdump -d --start-address=0x243D00 --stop-address=0x267140 out/game.exe`
   (51,218 instructions parsed): 665 `int3` runs and 6,337 padding bytes separate the entries;
   only 4 `int3` runs fall *inside* an IDA entry (the `0x24BC40` fragment and single pad bytes in
   `0x2522C0`/`0x252310`/`0x252630`, all noted in the table). No rel32 `call` target in the code
   range is missing from the entry list (53 rel32 `jmp` targets are internal blocks, not entries).
3. Continuation merge: an IDA entry with **no `ret`** in its range whose successor is not a
   named/XTLID/call-target entry is a continuation block of its predecessor (compiler out-of-line
   blocks and `jmp`-over-`nop` alignment, e.g. `lea ecx,[ecx+0]` at `0x24F94D`). This is validated
   by the 5849 signature lengths: merging the pieces of `D3DDevice_CopyRects`, 
   `D3DDevice_SetPixelShaderConstant` and `D3D_InitDefaultState` reproduces their recorded
   `len` exactly (`0x323`, `0x2C7`, `0xC2`), as do 82 of the named functions directly. The remaining
   named length differences vs 5849 are genuine 5558/5849 build differences (`Swap` 0xE5 vs 0xE3,
   `SetRenderState_StencilEnable` 0x84 vs 0x98, `SetRenderState_StencilFail` 0x6D vs 0x6F,
   `D3D_BlockOnTime` 0x166 vs 0x16A including its non-IDA tail block at `0x24D441`) or the merged
   `D3DDevice_SetTile` thunk (`0xA` API thunk + internal impl, below).
4. Naming evidence, strongest first: body semantics read from IDA pseudocode/asm; the
   `len`/`ret`/callee-length clusters in `sdk/xdk/5849/signatures.toml`; the `.XTLID` table at
   `0x3F13C0` (64 entries point into this section, 61 into code); direct callers from game `.text`;
   neighbour order against urbz.
5. Urbz neighbour alignment is **weak and was not used as proof**: urbz (XDK 5849) names a subset
   of the names Sims already has (91 shared D3D names, none extra), and the shared-name order is
   only block-isomorphic — whole `.obj` clusters move between builds (29.9% pairwise inversions,
   e.g. the render-state cluster is intact internally but displaced as a block). Within a cluster
   the order does agree (e.g. both builds have an unnamed setter between
   `SetRenderState_Dxt1NoiseEnable` and `SetRenderState_LogicOp`), which bounds but does not name.

## 1. The crash chain (assignments 3a/3b), proven

Guest call chain at boot (every address read from `out/game.exe`; game callers from `.text`):

```
game .text 0x16B8E9  call D3DDevice_Reset (return 0x16B8EE — in the crash stack)
  0x245F90 D3DDevice_Reset
    -> D3D_KickOffAndWaitForIdle, miniport poll 0x251D40, release-surfaces 0x24FA10
    -> 0x245FBE call mode-set 0x24FB40        (return 0x245FC3 — in the crash stack)
         0x24FB40 (0x24FB40–0x250020, JUMPOUT tail at 0x250064)
           -> surface alloc 0x251100 / init 0x24ECB0 / pitch 0x245A00
           -> 0x24FEB7 call internal SetTile 0x247940   (also 0x24FB05, 0x25005F)
              0x247940 (0x247940–0x247A11, retn 8; stores tile at [dev+idx*24+0x1AC0])
                -> 0x2479C3 call miniport tile-program 0x2530DA (return 0x2479C8 — crash stack)
                     ecx = device+0x1C28 (crash ecx=edi=0x266578)
                     0x2530DA (0x2530DA–0x2532C8, retn 0x1C, thiscall)
                       esi = [ecx] = miniport field 0 = HW register base = NULL on host
                       -> 0x2530EC call hw poll 0x252D36 (return 0x2530F1 — crash stack)
                            read [NULL+0x3214/0x2400/0x3230…]  =>  FAULT
                       writes [esi+0x400750]/[esi+0x400754]… in write-verify loops
```

- **3a — fault container `0x2530DA`**: entry `0x2530DA`, len `0x1EE`, `retn 0x1C` (thiscall:
  `ecx`=miniport, 7 stack args). It is an **NV2A miniport internal tile-programming method, not a
  named XDK API**. It loads the miniport's hardware-window base from `[miniport]` and programs tile
  registers at `[hwbase+0x400750]` etc., spin-verifying each write (calling idle-wait `0x25745F`
  each iteration). Its only caller is the internal SetTile impl `0x247940`. It must never be given
  a host name; it has to become unreachable, which naming `D3DDevice_Reset` (and the already-host
  `Direct3D_CreateDevice`) achieves.
- **3b — caller `0x247940`** (contains `0x2479C8`): entry `0x247940`, len `0xD1`, `retn 8`
  (stdcall index, `D3DTILE*`). Stores the 6-dword tile at `[device+index*24+0x1AC0]`, then calls
  `0x2530DA` with `ecx=device+0x1C28` (or tile-clear `0x2532C8` when the tile pointer is null).
  Only caller chain: the named thunk **`D3DDevice_SetTile` `0x247A20`** (len `0xA`:
  `call D3D_KickOffAndWaitForIdle; jmp 0x247940`), the mode-set `0x24FB40`, and release-surfaces
  `0x24FA10`. `SetTile` itself is host-implemented (`HLE_STDCALL(D3DDevice_SetTile,2)` returns 0,
  `sdk/runtime/xdk/d3d8.cpp:1362`), so the internal impl is only reachable through guest
  device-init code — Reset/Create — both now host-implemented.
- **3b — caller containing `0x245FC3`** is `D3DDevice_Reset` itself (next section).
- The `0x24FE4B`/`0x24FEBC` stack words sit inside the mode-set body `0x24FB40`–`0x250020`, not
  inside `D3D_InitDefaultState` (`0x24F940`–`0x24FA02`, len `0xC2`, `retn 0`), despite the
  nearest-symbol attribution in the crash log.

## 2. Proven name (assignment 3c)

### `D3DDevice_Reset = 0x245F90` — PROVEN, landed in `config/game.toml:97`

Entry `0x245F90`, end `0x246038`, len `0xA8`, `retn 4` (1 stack arg = `D3DPRESENT_PARAMETERS*`).
Body, instruction by instruction (`out/game.exe:0x245F90:asm`):

1. `0x245F9A` call `D3D_KickOffAndWaitForIdle`;
2. `0x245F9F–0x245FAE` miniport poll loop: `lea ebp,[edi+1C28h]; mov ecx,ebp; call 0x251D40; test eax,eax; jnz` back (`edi` = `[D3D_pDevice]`);
3. `0x245FB2` call `0x24FA10` (release surfaces), `0x245FBE` call mode-set `0x24FB40(pp)` — return `0x245FC3` is the crash-stack word;
4. `0x245FD9` push `[edi+1A18h]` (front buffer) call `0x245A00` (surface pitch);
   `0x245FFF` call miniport mode-apply `0x24F204` (`ecx=ebp`=miniport, 7 args from the pp struct);
5. `0x246008–0x246016` `mov ecx,[edi+1A20h]; mov edx,[edi+1A14h]; call D3DDevice_SetRenderTarget` — i.e. `[CDevice+0x1A14]`/`[CDevice+0x1A20]` per `sdk/xdk/5558/layout.toml`;
6. `0x24601B–0x24602A` `D3DDevice_Clear(0,0,3,0,1.0f,0)`; `xor eax,eax; retn 4`.

This is exactly the guest body the SDK comment describes (`sdk/runtime/xdk/d3d8.cpp:454`, from the
sibling title at `0x271570`), and the host `HLE_STDCALL(D3DDevice_Reset,1)` at `d3d8.cpp:459` was
already implemented and waiting for the name. Game callers: `.text 0x16B8E9`, `0x16E060`,
`0x16EFBD`. Post-fix result (reported by BootCrashTriage, who owns the build): 40 s run, zero
error lines, clean exit — the whole chain in §1 no longer executes.

No other name was added to `config/game.toml` by this investigation. The candidates in §3 have no
host implementation in the SDK; naming them would only label the recompiled guest function.

## 3. Candidate XDK names (evidence-graded, not in config)

Grades: A = body + signature/ordinal agree; B = body decisive, one supporting datum missing or
conflicting; C = body + neighbour/ordinal agree but a sibling assignment is also plausible.
Prefix convention follows the one proven sibling in the same `.XTLID` cluster,
`Direct3D_CreateDevice` (`0x800C5`).

### 3.1 Direct3D-object cluster (`0x243D00`–`0x2443C0`, XTLID `0x800C1`–`0x800CD`)

| Entry | Len | retn | XTLID | Candidate | Evidence |
|---|---|---|---|---|---|
| 0x243D00 | 0x8 | 4 | — | `Direct3D_GetAdapterCount` (B) | `mov eax,1; ret 4` — constant 1 (single adapter). Game-called at `.text 0x1293D8`. Fills the otherwise-missing `0x800C7` slot in the C1–CD sequence; no XTLID entry of its own, hence B not A. |
| 0x243D10 | 0x29 | 12 | 0x800C9 | `Direct3D_GetAdapterIdentifier` (A−) | 3 args (adapter, flags, out*); rejects adapter≠0 with `0x8876086C`, then `qmemcpy(out, aXboxDriver, 0x42C)` — copies a fixed adapter-identifier blob. |
| 0x243D40 | 0x9F | 4 | 0x800CA | `Direct3D_GetAdapterModeCount` (B) | 1 arg; walks the mode table from `0x24F18B` counting entries matching the current mode flags from `0x24F16C`. Game-called at `0x129400` (+ `.text 0x128550` via a thunk). Return is `4*count` as read by IDA — the one datum that does not match the PC signature, hence B. |
| 0x243DE0 | 0x141 | 12 | 0x800C6 | `Direct3D_EnumAdapterModes` (A−) | 3 args (adapter, mode index, out*); indexes the same mode table by `index>>2`, fills width/height/refresh(50/60)/format + a fifth dword (30/17/28/18 by flags). Game-called at `0x12943B`. |
| 0x244120 | 0xB9 | 8 | 0x800C8 | `Direct3D_GetAdapterDisplayMode` (B+) | 2 args (adapter, out*); fills width/height from the front-buffer surface (`[dev+0x1A18]`), refresh from miniport query `0x243F70(dev+0x1C28)`, format via `0x24F12C([dev+0x1DDC])`. Also Tier 1 (reads the miniport object, but only its guest-memory fields — safe, see §4). |
| 0x243F40 | 0x2E | 12 | 0x800CB | `Direct3D_GetDeviceCaps` (B+) | 3 args (adapter, devtype must be 1, out*); body is only `sub_00244E60(out)` = `qmemcpy(out, dword_30E430, 212)` — copies a 212-byte caps template. Ordinal would alphabetically be `0x800CC`; body wins over ordinal, hence B+. |
| 0x244240 | 0x7E | 24 | 0x800C2 | `Direct3D_CheckDeviceFormat` (B+) | Only 6-arg check in the cluster (adapter, devtype=1, adapterFormat, usage byte, rtype, checkFormat); tests the format byte table at `0x259768` (bits `0x40`/`0x80`/`0x3C` by usage). 6 args matches CheckDeviceFormat uniquely among the check family. |
| 0x244340 | 0x52 | 20 | 0x800C1 | `Direct3D_CheckDepthStencilMatch` (C+) | 5 args; tests two format-table entries (`a4`, `a5&0x40`), adapter format unused. Alphabetical C1 + body; CheckDeviceType assignment also fits the body — ambiguity stated, not resolved. |
| 0x2442C0 | 0x7E | 20 | 0x800C3 | `Direct3D_CheckDeviceMultiSampleType` (C+) | 5 args; `windowed==1` → NOTAVAILABLE (Xbox is fullscreen-only), adapter-format table check, then multisample-value validation (`0`, `17`, nibble-pair ≤3 — the Xbox packed multisample encoding). Alphabetical C3 + body. |
| 0x2441E0 | 0x5E | 20 | 0x800C4 | `Direct3D_CheckDeviceType` (C+) | 5 args; `windowed==1` → NOTAVAILABLE, then adapter/back-buffer format-table checks. Alphabetical C4 + body; see the C+ ambiguity above. |
| 0x2443A0 | 0x16 | 8 | 0x800CD | `Direct3D_RegisterSoftwareDevice` (C) | 2 args; stores the pair to globals `0x2670E0/0x2670E4` and returns arg1. `Direct3D_CreateDevice` reads exactly those globals as its software-device defaults (`0x2443C0`: if zero, substitutes `0x80000`/`0x8000`), which is what a register-software-device entry is for. Game-called at `0x12857A`, `0x1293FA`. Weakest in the cluster — the stored pair could also be a private registration; kept at C. |

### 3.2 Device-level candidates outside the cluster

| Entry | Len | retn | Candidate | Evidence |
|---|---|---|---|---|
| 0x245450 | 0x13 | 4 | `D3DDevice_SetVerticalBlankCallback` (B) | Whole body: `mov ecx,[D3D_pDevice]; mov [ecx+1DB8h],eax; ret 4` — stores its only argument at `CDevice+0x1DB8`, the field `sdk/xdk/5558/layout.toml:34` proves is `CDevice_VBlankCallback` ("the callback setter 0x245450"). Game-called at `.text 0x171D0A`. Name follows the SDK's `D3DDevice_*` convention; the exact XDK spelling is the only uncertainty. |
| 0x247630 | 0x3C | 0* | `D3DDevice_GetDisplayFieldStatus` (C+) | Fills `out[0]` = field (1/2, or 3 when `[dev+0x1DDC]&0x1200000==0`) and `out[1]` = `[dev+0x1DE8]` vblank count — exactly the fields `layout.toml:36–38` attributes to "the display-field query 0x247630", and `sdk/xdk/5849/layout.toml:33` names the same reader `D3DDevice_GetDisplayFieldStatus` in the 5849 title. (*IDA merges its `ret`; raw entry has no own `ret` in range.) |
| 0x24ECE0 | 0x90 | 16 | `D3DDevice_CreateImageSurface` (C+) | 4 args (w, h, …, format); allocates a 24-byte surface object (type `0x81000001`) through the same surface allocator `0x251100` the mode-set uses for frame buffers. Game-called at `.text 0x174E81`, `0x174F5A`. Could still be the library-internal surface creator the game happens to call; graded C+. |

## 4. Miniport risk register (assignment 3d)

The host never builds the NV2A miniport: `CDevice+0x1C28` is the embedded miniport *object*
(guest memory, safe to read/write), but its **field 0 — the hardware register-window base — is
always NULL on the host**. A function is a future crash of the boot-crash shape only if it
dereferences that field-0 pointer (`mov r,[miniport]` then `[r+…]`), not merely if it touches
`+0x1C28`. Tier 1 is split on exactly that line.

### Tier 1 — functions containing a `0x1C28` operand (16, exhaustive by instruction scan)

| Entry | Name today | How it touches `+0x1C28` | Derefs HW base? | Reachability in the port |
|---|---|---|---|---|
| 0x244120 | — (cand. GetAdapterDisplayMode) | `lea ecx,[esi+1C28h]` → `0x243F70` reads miniport-object fields `[+0x1B4]`, `[+0x820]` | **No** (guest-memory fields only) | XTLID-only xref; no direct game `call` found. Guest-safe if run. |
| 0x244EC0 | `D3DDevice_SetGammaRamp` | `lea edx,[esi+1C28h]`, copies ramps to miniport-object `+0x1DC+idx*0x300`, sets `+0x7DC` pending | No | **Host-implemented** (`d3d8.cpp`). Guest body never runs. |
| 0x245620 | `D3DDevice_SetFlickerFilter` | `mov eax,[eax+1C28h]` loads HW base | Yes, in guest body | **Host-implemented** (returns 0). Never runs. |
| 0x245670 | `D3DDevice_SetSoftDisplayFilter` | `mov edx,[eax+1C28h]` loads HW base | Yes, in guest body | **Host-implemented** (returns 0). Never runs. |
| 0x245F90 | `D3DDevice_Reset` | `lea ebp,[edi+1C28h]`, passes miniport to `0x251D40`/`0x24F204` | Through callees | **Host-implemented since this fix.** Was the boot crash. |
| 0x247670 | — | `add ecx,1C28h` at `0x2476CD` → call `0x252EA2` at `0x2476D3` | Through `0x252EA2`'s else-branch (`[hwbase+0x9200]` store, only when its slot arg is −1) | No callers found in IDA xrefs (library-internal, possibly dead or called via a table). **Watch.** |
| 0x247940 | — internal SetTile impl | `add ecx,1C28h` → `0x2530DA`; `lea ecx,[ebx+1C28h]` → `0x2532C8` | **Yes** (both callees load `[miniport]`) | Behind host `SetTile` thunk for game calls; reachable only from guest mode-set/release-surfaces. **Dead once Reset/Create are host — verified by the clean run.** |
| 0x247FC0 | `D3DDevice_PersistDisplay` | `lea ebx,[ebp+1C28h]` passed onward | Through callees | **Host-implemented** (`d3d8.cpp:586`). Never runs. |
| 0x248140 | — | `mov edi,[eax+1C28h]` at `0x248146` (HW base), later `mov [edi+0x68084C],…` | **Yes — direct store to `[NULL+0x68084C]` if called** | No callers found in IDA xrefs. **Highest-risk dead/unknown entry in this tier if a table call exists.** |
| 0x248AD0 | — present/vblank helper | passes `this+1802` dwords (=miniport) to `0x252F31` | **Yes** (in `0x252F31`: `mov eax,[ecx]; mov [eax+0x680680],…`) | Called from `0x2495E9`, inside the unnamed function `0x2495B0` immediately before `D3DDevice_Swap` (host). No game caller found for `0x2495B0`. **Watch.** |
| 0x24CFE0 | — hw fence spin | `mov eax,[eax+1C28h]`; `or [eax+0x100410],0x10000` then spins on it | **Yes — direct RMW + spin on NULL** | Called from guest `D3D_BlockOnTime` (host-implemented at its named entry), from miniport-cluster `0x252468/0x252486`, and from `0x256F27`. Reachable only through guest library code. **Watch.** |
| 0x24D120 | `CDevice_KickOff` | `mov eax,[eax+1C28h]`; `mov ecx,[eax+0x100410]`… | Yes, in guest body | **Host-implemented** (`HLE_FASTCALL(CDevice_KickOff)` returns 0). Its tail blocks `0x24D1D0/0x24D210/0x24D230` (next row) are part of the same guest function and equally unreachable. |
| 0x24D1D0 | — (KickOff tail block) | `mov eax,[eax+1C28h]`; same `0x100410` fence spin | Yes | Not an entry: continuation block of `CDevice_KickOff`. Unreachable except through it (host). |
| 0x24FA10 | — CDevice release-surfaces | `mov eax,[esi+1C28h]` value passed to a kernel import; device/miniport-object fields otherwise | Only through its `0x247940` calls (tile present case) | Called by Reset (`0x245FB2/0x245FCB`, host now) and destroy body `0x2505B0` (`0x25061E`, behind host `Release`). **Dead in the port.** |
| 0x2500E0 | — CDevice create body | `lea esi,[ebx+1C28h]` at `0x250147` → miniport init `0x252F78` | Through miniport init callees | Only caller: guest `Direct3D_CreateDevice` wrapper `0x244419` — **host-implemented at the named entry**, so this body never runs in the port. |
| 0x2505B0 | — CDevice destroy body | passes miniport to poll `0x251D40` and teardown `0x25338A` | Through `0x25338A` | Only callers: guest `CreateDevice` failure path and guest `D3DDevice_Release` (`0x245384`) — both **host-implemented at their named entries**. Dead in the port. |

### Tier 2 — miniport-thiscall methods (16, proven at the call site)

Each entry below is called with `ecx` = miniport (device+0x1C28) at the cited call site, or
forwards that `this` unchanged. "HW deref" = loads `[this]` (the NULL HW base) and uses it.

| Entry | Len | retn | Descriptive name | HW deref? | Proven call site(s) |
|---|---|---|---|---|---|
| 0x243F70 | 0x41 | 0 | miniport refresh-rate query | No — reads miniport-object `[+0x820]` qword and `[+0x1B4]` mode dword, returns 50/60 | `0x2441AE` in `0x244120` |
| 0x251D40 | 0x11 | 0 | miniport status-dword poll | No — returns `this[3*(this[111]&1)+93]` (miniport-object field) | Reset `0x245FA7`; destroy `0x2505B0` loop |
| 0x252DA1 | 0x15 | 0 | miniport clock read | **Yes** — `return *[hwbase+0x9400]` (`0x252DA1` body is 3 instructions) | `0x24F204` (twice) |
| 0x252DD4 | 0xCE | 12 | miniport timer helper (callee of `0x24F204`/`0x252EA2`) | Not read in full; grouped by its two proven miniport callers | `0x24F204`, `0x252EA2` |
| 0x252EA2 | 0x8F | 16 | miniport timer set/clear | **Yes**, else-branch only: `mov [hwbase+0x9200],0` when slot is −1 | `0x2476D3` in `0x247670` (`add ecx,1C28h` at `0x2476CD`) |
| 0x252F78 | 0x162 | 0* | miniport init | Through callees `0x2529F6/0x252A21/0x252A62/0x252AE5`; its own body writes miniport-object fields and registers a DPC/callback via kernel imports | Create body `0x250147` (`lea esi,[ebx+1C28h]`) |
| 0x252F31 | 0x47 | 0 | miniport register toggle | **Yes** — `mov eax,[ecx]; mov [eax+0x680680],…; lea ecx,[eax+0x6806A0]` | `0x248AD0` (`a1+1802` dwords) |
| 0x2530DA | 0x1EE | 0x1C | miniport tile-program (3a) | **Yes** | `0x2479C3` |
| 0x2532C8 | 0xC2 | 8 | miniport tile-clear | **Yes** — same register windows as `0x2530DA`, zeroed | `0x2479EB` (`lea ecx,[ebx+1C28h]`) |
| 0x25338A | 0x1BC | — | miniport teardown | **Yes** — loads `[this]`, calls tile-clear/poll/idle callees below | Destroy body `0x2505B0` (`this+1802`) |
| 0x252D36 | 0x6B | — | miniport HW poll | **Yes** — spins reading `[hwbase+0x3214]`, `[+0x2400]`, `[+0x3230]`, `[+0x400100]` | `0x2530EC`, `0x2532C8` head |
| 0x25745F | 0x3F | — | miniport idle-wait | **Yes** — loops on `[hwbase+0x400700]` | every iteration of `0x2530DA`/`0x2532C8` |
| 0x252580 | 0xAB | — | miniport HW-state handler (callee of poll/idle) | **Pattern yes** — 11 operands at `[hwbase+0x400720]` etc. | `0x252D36`, `0x25745F`, `0x25338A` |
| 0x251FF0 | 0x10A | — | miniport HW-state handler (callee of poll/idle) | **Pattern yes** — `[hwbase+0x6013D4]` operands | `0x252D36`, `0x25745F`, `0x25338A` |
| 0x252630 | 0xB9* | — | miniport HW-state handler (callee of poll/teardown) | **Pattern yes** — `[hwbase+0x2100/0x3250/0x2500]` operands | `0x252D36`, `0x25338A` |
| 0x24F204 | 0x265 | 0x1C | miniport mode-apply (Reset step 4) | Object fields (`[+0x1B4]`, `[+0x820]`) itself; **HW through `0x252DA1`** | Reset `0x245FFF` (`mov ecx,ebp`); create body `0x2500E0` |

(*IDA `retn` merged/absent for these entries; see table notes.)

### Tier 3 — HW-offset pattern only (not proof)

These 19 functions contain a `(%ecx)` dereference **and** a memory operand displaced by
`0x400000`–`0x7FFFFF`, the shape of an NV2A register access, but no call site passing the
miniport was proven for them. Several are certainly *not* miniport code (the base register is a
surface, push context or resource in their other uses). They are listed so the pattern is on
record, not as crash predictions:
`0x24CEE0, 0x251324, 0x2513A6, 0x251DE0, 0x251F37, 0x252BD7, 0x257054, 0x2571BB, 0x2572B9, 0x2573BE, 0x25749E, 0x2577B9, 0x2577D0, 0x2577E2, 0x25792C, 0x257A71, 0x257CE1, 0x257D56, 0x258735`.
The `0x257xxx`–`0x258xxx` members cluster around the proven miniport idle-wait `0x25745F` and are
the most likely of this tier to be miniport methods; resolving them needs per-function
call-site analysis, which was not done (see Unresolved).

**Bottom line for the port:** after naming `D3DDevice_Reset`, every Tier 1/Tier 2 function with a
proven HW dereference is either host-implemented at its entry or reachable only through host-
implemented entries (`CreateDevice`, `Release`, `SetTile`, `BlockOnTime`, `KickOff`). The three
**Watch** rows (`0x247670`, `0x248140`, `0x248AD0`, plus `0x24CFE0` through guest library callers)
have no proven game/library caller that survives host replacement; if a future crash shows a
return address inside any of them, name its *caller chain entry* (not the miniport method) or
host-implement the API that reaches it.

## 5. Game-called unnamed entries (23 — the API surface the title actually uses)

These have a direct `call` from game `.text` and no name in `config/game.toml`. They all run as
recompiled guest code today; none was found to dereference the miniport HW base (the three
push-packet writers and the KickOff thunks only touch guest push-buffer/device memory, or tail-jump
to host-implemented entries). Descriptive names are from the body; only the §3 candidates among
them are proposed as XDK names.

`0x243D00` GetAdapterCount cand. · `0x243D40` GetAdapterModeCount cand. · `0x243DE0`
EnumAdapterModes cand. · `0x2443A0` RegisterSoftwareDevice cand. · `0x2445B0` thunk →
surface-desc `0x250C10` · `0x2445C0` surface/texture wrapper (via `0x250D40`+`0x24EC50`) ·
`0x244610` Lock2DSurface wrapper (0,args) · `0x2449F0` resource AddRef (low-word pair) ·
`0x244A30` resource Release (low-word pair) · `0x244BC0` resource data-offset adjust (×22 calls —
the most-called unnamed entry) · `0x2453B0` thunk → `D3D_KickOffAndWaitForIdle` · `0x2453D0`
KickOff wrapper · `0x245450` SetVerticalBlankCallback cand. · `0x24A130` render-state group
setter (states 77–81 from a scaled float + enables; in the `Dxt1NoiseEnable`→`LogicOp` neighbour
slot in both builds) · `0x24BC50` vertex-shader-constant setter (game entry; calls
`SetVertexShaderConstantNotInlineFast`) · `0x24E030/0x24E070/0x24E160` push-packet writers
(×12–16 calls each) · `0x24E300` pixel-shader select helper · `0x24ECE0` CreateImageSurface cand. ·
`0x24EE00` surface-desc wrapper · `0x24EE20` Lock2DSurface wrapper (0,0,args) · `0x24F100`
resource data/busy query.

## 6. Full enumeration

Columns: entry/end are the IDA entry range (end exclusive, padding excluded); `retn` is the stack
bytes popped (`— (tail jmp)` = no `ret` in range, the entry ends in a tail jump or is a
continuation); callees are direct rel32 `call` targets in first-use order with their IDA length
when unnamed; notes flag continuations, the risk tiers (§4), candidates (§3), descriptive names
(§5 and below) and direct game callers. Multi-piece functions appear as consecutive rows; every
continuation row says which entry it belongs to. Descriptive names used in notes for the device
core: `0x24FA10` CDevice release-surfaces, `0x24FB40` CDevice mode-set, `0x24F204` miniport
mode-apply, `0x2500E0` CDevice create body, `0x2505B0` CDevice destroy body, `0x245A00` surface
pitch/size.

| # | Entry | End | Len | retn | config/game.toml name | XTLID | Direct callees | Note |
|---|---|---|---|---|---|---|---|---|
| 1 | 0x243D00 | 0x243D08 | 0x8 | 4 |  |  | — | cand. Direct3D_GetAdapterCount (B); game-called ×1 |
| 2 | 0x243D10 | 0x243D39 | 0x29 | 12 |  | 0x800C9 | — | cand. Direct3D_GetAdapterIdentifier (A-) |
| 3 | 0x243D40 | 0x243DDF | 0x9F | 4 |  | 0x800CA | 0x24F18B(0x79), 0x24F16C(0x1F) | cand. Direct3D_GetAdapterModeCount (B); game-called ×1 |
| 4 | 0x243DE0 | 0x243F21 | 0x141 | 12 |  | 0x800C6 | 0x24F18B(0x79), 0x24F16C(0x1F), 0x24F12C(0x40) | cand. Direct3D_EnumAdapterModes (A-); game-called ×1 |
| 5 | 0x243F40 | 0x243F6E | 0x2E | 12 |  | 0x800CB | 0x244E60(0x17) | cand. Direct3D_GetDeviceCaps (B+) |
| 6 | 0x243F70 | 0x243FB1 | 0x41 | 0 |  |  | — | **T2** miniport-thiscall |
| 7 | 0x243FC0 | 0x243FCD | 0xD | 4 |  |  | — |  |
| 8 | 0x243FD0 | 0x243FDD | 0xD | 4 |  |  | — |  |
| 9 | 0x243FE0 | 0x243FED | 0xD | 4 |  |  | — |  |
| 10 | 0x243FF0 | 0x244003 | 0x13 | 4 |  |  | — |  |
| 11 | 0x244010 | 0x244021 | 0x11 | 4 |  |  | — |  |
| 12 | 0x244030 | 0x244041 | 0x11 | 4 |  |  | — |  |
| 13 | 0x244050 | 0x244060 | 0x10 | 4 |  |  | — |  |
| 14 | 0x244060 | 0x24406E | 0xE | 4 |  |  | — |  |
| 15 | 0x244070 | 0x244080 | 0x10 | 4 |  |  | — |  |
| 16 | 0x244080 | 0x244093 | 0x13 | 4 |  |  | — |  |
| 17 | 0x2440A0 | 0x2440C7 | 0x27 | 4 |  |  | — |  |
| 18 | 0x2440D0 | 0x2440F8 | 0x28 | 4 |  |  | — |  |
| 19 | 0x244100 | 0x244113 | 0x13 | 4 |  |  | — |  |
| 20 | 0x244120 | 0x2441D9 | 0xB9 | 8 |  | 0x800C8 | 0x243F70(0x41), 0x24F12C(0x40) | **T1** [dev+0x1C28]; cand. Direct3D_GetAdapterDisplayMode (B+) |
| 21 | 0x2441E0 | 0x24423E | 0x5E | 20 |  | 0x800C4 | — | cand. Direct3D_CheckDeviceType (C+) |
| 22 | 0x244240 | 0x2442BE | 0x7E | 24 |  | 0x800C2 | — | cand. Direct3D_CheckDeviceFormat (B+) |
| 23 | 0x2442C0 | 0x24433E | 0x7E | 20 |  | 0x800C3 | — | cand. Direct3D_CheckDeviceMultiSampleType (C+) |
| 24 | 0x244340 | 0x244392 | 0x52 | 20 |  | 0x800C1 | — | cand. Direct3D_CheckDepthStencilMatch (C+) |
| 25 | 0x2443A0 | 0x2443B6 | 0x16 | 8 |  | 0x800CD | — | cand. Direct3D_RegisterSoftwareDevice (C); game-called ×2 |
| 26 | 0x2443C0 | 0x244464 | 0xA4 | 24 | Direct3D_CreateDevice | 0x800C5 | 0x2500E0(0x4C5), 0x2505B0(0x1AC) | game-called ×1 |
| 27 | 0x244470 | 0x244473 | 0x3 | 8 |  |  | — |  |
| 28 | 0x244480 | 0x24448C | 0xC | 4 |  |  | — |  |
| 29 | 0x244490 | 0x2444C3 | 0x33 | 4 |  |  | 0x108E9E(ext) |  |
| 30 | 0x2444D0 | 0x2444DE | 0xE | 4 |  |  | — |  |
| 31 | 0x2444E0 | 0x2444E3 | 0x3 | 12 |  |  | — |  |
| 32 | 0x2444F0 | 0x2445A0 | 0xB0 | 28 |  |  | 0x251100(0x1F5), 0x108E9E(ext), 0x108F3E(ext) |  |
| 33 | 0x2445A0 | 0x2445AE | 0xE | 4 |  | 0x8000C | — |  |
| 34 | 0x2445B0 | 0x2445B5 | 0x5 | — (tail jmp) |  | 0x80088 | — | thunk -> 0x250C10; game-called ×2 |
| 35 | 0x2445C0 | 0x24460A | 0x4A | 8 |  | 0x8008A | 0x250D40(0x223), 0x24EC50(0x51) | game-called ×4 |
| 36 | 0x244610 | 0x244633 | 0x23 | 20 |  | 0x8008D | Lock2DSurface | Lock2DSurface wrapper (0,args); game-called ×4 |
| 37 | 0x244640 | 0x244645 | 0x5 | — (tail jmp) |  | 0x8001A | — |  |
| 38 | 0x244650 | 0x24469D | 0x4D | 12 |  | 0x80017 | 0x250D40(0x223), 0x24EC50(0x51) |  |
| 39 | 0x2446A0 | 0x2446A5 | 0x5 | — (tail jmp) |  | 0x8001E | — |  |
| 40 | 0x2446B0 | 0x2446B5 | 0x5 | — (tail jmp) |  | 0x800B5 | — |  |
| 41 | 0x2446C0 | 0x244705 | 0x45 | 8 |  | 0x800B8 | 0x250D40(0x223), 0x24EE40(0x4D) |  |
| 42 | 0x244710 | 0x244715 | 0x5 | — (tail jmp) |  | 0x800BA | — |  |
| 43 | 0x244720 | 0x24476B | 0x4B | 4 |  |  | 0x108F3E(ext) |  |
| 44 | 0x244770 | 0x24478B | 0x1B | 8 |  |  | — |  |
| 45 | 0x244790 | 0x2447AB | 0x1B | 8 |  |  | — |  |
| 46 | 0x2447B0 | 0x2447B5 | 0x5 | — (tail jmp) |  |  | — |  |
| 47 | 0x2447C0 | 0x2447C3 | 0x3 | 8 |  |  | — | continuation of 0x2447B0 |
| 48 | 0x2447D0 | 0x2447D3 | 0x3 | 8 |  |  | — |  |
| 49 | 0x2447E0 | 0x2447EC | 0xC | 4 |  |  | — |  |
| 50 | 0x2447F0 | 0x2447FC | 0xC | 0 |  |  | 0x108F3E(ext) |  |
| 51 | 0x244800 | 0x244818 | 0x18 | 4 |  |  | — |  |
| 52 | 0x244820 | 0x24483F | 0x1F | 4 |  |  | — |  |
| 53 | 0x244840 | 0x24484D | 0xD | 8 |  |  | — |  |
| 54 | 0x244850 | 0x244856 | 0x6 | 0 |  |  | — |  |
| 55 | 0x244860 | 0x24486A | 0xA | 4 |  |  | — |  |
| 56 | 0x244870 | 0x244889 | 0x19 | 4 |  |  | — |  |
| 57 | 0x244890 | 0x2448C6 | 0x36 | 8 |  |  | — |  |
| 58 | 0x2448D0 | 0x244961 | 0x91 | 4 |  |  | D3D_BlockOnResource, 0x244720(0x4B), 0x108F3E(ext) |  |
| 59 | 0x244970 | 0x2449A5 | 0x35 | 4 |  |  | — |  |
| 60 | 0x2449B0 | 0x2449E2 | 0x32 | 4 | D3D_InternalAddRef |  | — |  |
| 61 | 0x2449F0 | 0x244A27 | 0x37 | 4 |  | 0x80069 | 0x2449F0(0x37) | resource AddRef (low-word pair); game-called ×4 |
| 62 | 0x244A30 | 0x244A7E | 0x4E | 4 |  | 0x80072 | 0x244A30(0x4E), 0x2448D0(0x91) | resource Release (low-word pair); game-called ×8 |
| 63 | 0x244A80 | 0x244B0A | 0x8A | 4 |  | 0x8006E | — |  |
| 64 | 0x244B10 | 0x244B9A | 0x8A | 4 | D3DResource_IsBusy | 0x8006F | — |  |
| 65 | 0x244BA0 | 0x244BB4 | 0x14 | 8 |  | 0x8006C | 0x245350(0x15) |  |
| 66 | 0x244BC0 | 0x244BE8 | 0x28 | 8 |  | 0x80071 | — | resource data-offset adjust; game-called ×22 |
| 67 | 0x244BF0 | 0x244BF5 | 0x5 | — (tail jmp) | D3DResource_BlockUntilNotBusy | 0x8006A | — |  |
| 68 | 0x244C00 | 0x244CA8 | 0xA8 | 20 |  | 0x80073 | 0x244890(0x36), 0x244D40(0x64), 0x108E9E(ext) |  |
| 69 | 0x244CB0 | 0x244D31 | 0x81 | 16 |  | 0x8006D | 0x244890(0x36) |  |
| 70 | 0x244D40 | 0x244DA4 | 0x64 | 8 |  | 0x8006B | 0x108F3E(ext) |  |
| 71 | 0x244DB0 | 0x244DD1 | 0x21 | 8 |  |  | 0x2448D0(0x91) |  |
| 72 | 0x244DE0 | 0x244E42 | 0x62 | 4 | D3D_InternalRelease |  | 0x2448D0(0x91) |  |
| 73 | 0x244E50 | 0x244E5D | 0xD | 4 |  |  | — |  |
| 74 | 0x244E60 | 0x244E77 | 0x17 | 4 |  |  | — |  |
| 75 | 0x244E80 | 0x244EBE | 0x3E | 4 |  |  | — |  |
| 76 | 0x244EC0 | 0x244F43 | 0x83 | 8 | D3DDevice_SetGammaRamp |  | 0x2513A6(0x59) | **T1** [dev+0x1C28] |
| 77 | 0x244F50 | 0x244F7A | 0x2A | 4 |  |  | — |  |
| 78 | 0x244F80 | 0x244F9F | 0x1F | 4 |  |  | — |  |
| 79 | 0x244FA0 | 0x244FE0 | 0x40 | 8 |  |  | — |  |
| 80 | 0x244FE0 | 0x2450F8 | 0x118 | 8 | D3DDevice_SetTransform |  | 0x249AB0(0x140), 0x24C300(0xEF) |  |
| 81 | 0x245100 | 0x245125 | 0x25 | 8 |  |  | — |  |
| 82 | 0x245130 | 0x245184 | 0x54 | 8 |  |  | 0x251460(0xFA), D3DDevice_SetTransform |  |
| 83 | 0x245190 | 0x2451C3 | 0x33 | 4 |  |  | — |  |
| 84 | 0x2451D0 | 0x245200 | 0x30 | 4 | D3DDevice_SetMaterial |  | — |  |
| 85 | 0x245200 | 0x24521D | 0x1D | 4 |  |  | — |  |
| 86 | 0x245220 | 0x245250 | 0x30 | 4 |  |  | — |  |
| 87 | 0x245250 | 0x24526D | 0x1D | 4 |  |  | — |  |
| 88 | 0x245270 | 0x245273 | 0x3 | 4 |  |  | — |  |
| 89 | 0x245280 | 0x2452AA | 0x2A | 8 |  |  | — |  |
| 90 | 0x2452B0 | 0x2452E0 | 0x30 | 8 |  |  | — |  |
| 91 | 0x2452E0 | 0x2452F4 | 0x14 | 8 |  |  | — |  |
| 92 | 0x245300 | 0x245330 | 0x30 | 4 | D3DDevice_SwitchTexture |  | D3DDevice_MakeSpace |  |
| 93 | 0x245330 | 0x24534D | 0x1D | 12 |  |  | — |  |
| 94 | 0x245350 | 0x245365 | 0x15 | 0 |  |  | — |  |
| 95 | 0x245370 | 0x2453A7 | 0x37 | 0 | D3DDevice_Release |  | 0x2505B0(0x1AC) | game-called ×2 |
| 96 | 0x2453B0 | 0x2453B5 | 0x5 | — (tail jmp) |  |  | — | thunk -> D3D_KickOffAndWaitForIdle; game-called ×2 |
| 97 | 0x2453C0 | 0x2453CF | 0xF | 4 | D3DDevice_BlockOnFence |  | D3D_BlockOnTime | game-called ×2 |
| 98 | 0x2453D0 | 0x2453DB | 0xB | — (tail jmp) |  |  | — | KickOff wrapper (CDevice_KickOff(device)); game-called ×3 |
| 99 | 0x2453E0 | 0x245441 | 0x61 | 12 |  |  | D3D_SetFence | continuation of 0x2453D0 |
| 100 | 0x245450 | 0x245463 | 0x13 | 4 |  |  | — | cand. D3DDevice_SetVerticalBlankCallback (B); game-called ×1 |
| 101 | 0x245470 | 0x245483 | 0x13 | 4 |  |  | — |  |
| 102 | 0x245490 | 0x2454B4 | 0x24 | 0 | D3DDevice_BlockUntilVerticalBlank |  | — | game-called ×1 |
| 103 | 0x2454C0 | 0x2454DE | 0x1E | 4 |  |  | — |  |
| 104 | 0x2454E0 | 0x24550E | 0x2E | 4 |  |  | — |  |
| 105 | 0x245510 | 0x245561 | 0x51 | 12 |  |  | — |  |
| 106 | 0x245570 | 0x2455AD | 0x3D | 8 |  |  | — |  |
| 107 | 0x2455B0 | 0x245601 | 0x51 | 12 |  |  | — |  |
| 108 | 0x245610 | 0x245616 | 0x6 | — (tail jmp) |  |  | — |  |
| 109 | 0x245620 | 0x245662 | 0x42 | 4 | D3DDevice_SetFlickerFilter |  | — | **T1** [dev+0x1C28] |
| 110 | 0x245670 | 0x2456C1 | 0x51 | 4 | D3DDevice_SetSoftDisplayFilter |  | — | **T1** [dev+0x1C28] |
| 111 | 0x2456D0 | 0x24572F | 0x5F | 0 |  |  | — |  |
| 112 | 0x245730 | 0x245790 | 0x60 | 12 |  |  | — |  |
| 113 | 0x245790 | 0x245798 | 0x8 | 0 | D3DDevice_InsertFence |  | D3D_SetFence | game-called ×2 |
| 114 | 0x2457A0 | 0x2457CC | 0x2C | 8 |  |  | — |  |
| 115 | 0x2457D0 | 0x2457D6 | 0x6 | 0 |  |  | — |  |
| 116 | 0x2457E0 | 0x2457FD | 0x1D | 4 |  |  | — |  |
| 117 | 0x245800 | 0x245801 | 0x1 | 0 |  |  | — |  |
| 118 | 0x245810 | 0x245815 | 0x5 | 4 |  |  | — |  |
| 119 | 0x245820 | 0x245825 | 0x5 | — (tail jmp) |  |  | — |  |
| 120 | 0x245830 | 0x245834 | 0x4 | 0 |  |  | — | continuation of 0x245820 |
| 121 | 0x245840 | 0x245844 | 0x4 | 0 |  |  | — |  |
| 122 | 0x245850 | 0x245853 | 0x3 | 0 |  |  | — |  |
| 123 | 0x245860 | 0x245867 | 0x7 | 0 |  |  | — |  |
| 124 | 0x245870 | 0x245887 | 0x17 | 0 |  |  | — |  |
| 125 | 0x245890 | 0x2458A6 | 0x16 | 4 |  |  | 0x251CB0(0x9) |  |
| 126 | 0x2458B0 | 0x2458E3 | 0x33 | 4 |  |  | 0x108E9E(ext) |  |
| 127 | 0x2458F0 | 0x245901 | 0x11 | 4 |  |  | — |  |
| 128 | 0x245910 | 0x245924 | 0x14 | 4 |  |  | 0x250830(0x1A) |  |
| 129 | 0x245930 | 0x24593B | 0xB | 4 |  |  | — |  |
| 130 | 0x245940 | 0x245956 | 0x16 | 4 |  |  | — |  |
| 131 | 0x245960 | 0x245978 | 0x18 | 4 |  |  | — |  |
| 132 | 0x245980 | 0x245996 | 0x16 | 4 |  |  | — |  |
| 133 | 0x2459A0 | 0x2459B6 | 0x16 | 4 |  |  | — |  |
| 134 | 0x2459C0 | 0x2459D1 | 0x11 | — (tail jmp) |  |  | — |  |
| 135 | 0x2459E0 | 0x2459EE | 0xE | 4 |  |  | — | continuation of 0x2459C0 |
| 136 | 0x2459F0 | 0x2459FF | 0xF | 4 |  |  | — |  |
| 137 | 0x245A00 | 0x245A65 | 0x65 | 4 |  |  | — | surface pitch/size from header |
| 138 | 0x245A70 | 0x245AE9 | 0x79 | 4 |  |  | 0x250830(0x1A) |  |
| 139 | 0x245AF0 | 0x245AFC | 0xC | 0 |  |  | — |  |
| 140 | 0x245B00 | 0x245B15 | 0x15 | 4 |  |  | — |  |
| 141 | 0x245B20 | 0x245B2E | 0xE | 4 |  |  | — |  |
| 142 | 0x245B30 | 0x245B31 | 0x1 | 0 |  |  | — |  |
| 143 | 0x245B40 | 0x245B54 | 0x14 | 4 |  |  | — |  |
| 144 | 0x245B60 | 0x245B7B | 0x1B | 8 |  |  | — |  |
| 145 | 0x245B80 | 0x245BA2 | 0x22 | 12 |  |  | — |  |
| 146 | 0x245BB0 | 0x245BC7 | 0x17 | 8 |  |  | — |  |
| 147 | 0x245BD0 | 0x245BDF | 0xF | 0 |  |  | 0x24CB50(0x70) |  |
| 148 | 0x245BE0 | 0x245BF9 | 0x19 | 0 |  |  | — |  |
| 149 | 0x245C00 | 0x245C0C | 0xC | 0 |  |  | — |  |
| 150 | 0x245C10 | 0x245C25 | 0x15 | 4 |  |  | — |  |
| 151 | 0x245C30 | 0x245C3E | 0xE | 4 |  |  | — |  |
| 152 | 0x245C40 | 0x245C51 | 0x11 | 4 |  |  | — |  |
| 153 | 0x245C60 | 0x245C6D | 0xD | 0 |  |  | — |  |
| 154 | 0x245C70 | 0x245C75 | 0x5 | — (tail jmp) |  |  | — |  |
| 155 | 0x245C80 | 0x245C89 | 0x9 | 4 |  |  | — | continuation of 0x245C70 |
| 156 | 0x245C90 | 0x245C9D | 0xD | 4 |  |  | — |  |
| 157 | 0x245CA0 | 0x245CBD | 0x1D | 4 |  |  | — |  |
| 158 | 0x245CC0 | 0x245EC0 | 0x200 | 28 |  |  | 0x251CB0(0x9) |  |
| 159 | 0x245EC0 | 0x245F81 | 0xC1 | 4 |  |  | 0x24F12C(0x40) |  |
| 160 | 0x245F90 | 0x246038 | 0xA8 | 4 | D3DDevice_Reset |  | D3D_KickOffAndWaitForIdle, 0x251D40(0x11), 0x24FA10(0x12F), 0x24FB40(0x4E0), 0x245A00(0x65), 0x24F204(0x265), D3DDevice_SetRenderTarget, D3DDevice_Clear | **T1** [dev+0x1C28]; game-called ×2 |
| 161 | 0x246040 | 0x2462B4 | 0x274 | 8 | D3DDevice_SetRenderTarget |  | D3D_InternalAddRef, D3D_InternalRelease, 0x250A50(0x46), 0x245A00(0x65), D3DDevice_MakeSpace, 0x24A8F0(0x59), 0x250930(0xBB), 0x24AAC0(0x355), D3DDevice_SetRenderState_Dxt1NoiseEnable | game-called ×5 |
| 162 | 0x2462C0 | 0x2463E0 | 0x120 | 12 |  |  | D3D_InternalAddRef, D3D_InternalRelease, D3DDevice_MakeSpace |  |
| 163 | 0x2463E0 | 0x246423 | 0x43 | 4 | D3DDevice_GetBackBuffer2 |  | 0x2449F0(0x37) | game-called ×6 |
| 164 | 0x246430 | 0x24649B | 0x6B | 4 |  |  | — |  |
| 165 | 0x2464A0 | 0x246572 | 0xD2 | 12 |  |  | — |  |
| 166 | 0x246580 | 0x2466FB | 0x17B | 8 |  |  | D3DDevice_MakeSpace, 0x2464A0(0xD2) |  |
| 167 | 0x246720 | 0x2467C4 | 0xA4 | 12 |  |  | D3DDevice_MakeSpace |  |
| 168 | 0x2467D0 | 0x24694A | 0x17A | — (tail jmp) | D3DDevice_CopyRects |  | 0x245A00(0x65), 0x250830(0x1A), 0x245A70(0x79), D3DDevice_MakeSpace | game-called ×2 |
| 169 | 0x246950 | 0x246AF3 | 0x1A3 | 20 |  |  | D3DDevice_MakeSpace | continuation of 0x2467D0 |
| 170 | 0x246B00 | 0x246B1A | 0x1A | 0 | D3DDevice_GetRenderTarget2 |  | 0x2449F0(0x37) |  |
| 171 | 0x246B20 | 0x246B3E | 0x1E | 0 | D3DDevice_GetDepthStencilSurface2 |  | 0x2449F0(0x37) | game-called ×2 |
| 172 | 0x246B40 | 0x246D62 | 0x222 | 4 | D3DDevice_SetViewport |  | 0x251CB0(0x9), D3DDevice_SetScissors, 0x249AB0(0x140), D3DDevice_MakeSpace, 0x24A700(0x1E5) |  |
| 173 | 0x246D70 | 0x247023 | 0x2B3 | 8 | D3DDevice_SetLight |  | 0x108E9E(ext), 0x108F3E(ext), 0x251710(0x42), 0x251560(0xCE), 0x251CC0(0x9), 0x2516E0(0x2F) |  |
| 174 | 0x247030 | 0x247181 | 0x151 | 8 | D3DDevice_LightEnable |  | D3DDevice_SetLight |  |
| 175 | 0x247190 | 0x2471B4 | 0x24 | 4 |  |  | 0x2449F0(0x37) |  |
| 176 | 0x2471C0 | 0x247364 | 0x1A4 | 8 | D3DDevice_SetTexture |  | 0x2448D0(0x91), D3DDevice_MakeSpace | game-called ×6 |
| 177 | 0x247370 | 0x247394 | 0x24 | 4 |  |  | 0x2449F0(0x37) |  |
| 178 | 0x2473A0 | 0x24742D | 0x8D | 8 | D3DDevice_SetPalette |  | 0x2448D0(0x91), D3DDevice_MakeSpace | game-called ×7 |
| 179 | 0x247430 | 0x2474B4 | 0x84 | 8 | D3DDevice_SetIndices |  | 0x2448D0(0x91) |  |
| 180 | 0x2474C0 | 0x2474F5 | 0x35 | 4 |  |  | 0x2449F0(0x37) |  |
| 181 | 0x247500 | 0x24752F | 0x2F | 0 |  |  | — | context idle query ([dev+0x934]) |
| 182 | 0x247530 | 0x24755B | 0x2B | 0 |  |  | D3DDevice_MakeSpace |  |
| 183 | 0x247560 | 0x2475C6 | 0x66 | 4 |  |  | 0x108E9E(ext) | fence-slot alloc (device+0x7D4 array) |
| 184 | 0x2475D0 | 0x24762A | 0x5A | 4 |  |  | 0x247560(0x66), D3DDevice_MakeSpace | fence submit (uses 0x247560) |
| 185 | 0x247630 | 0x24766C | 0x3C | 4 |  |  | — | cand. D3DDevice_GetDisplayFieldStatus (C+) |
| 186 | 0x247670 | 0x2476E9 | 0x79 | 16 |  |  | 0x252EA2(0x8F) | **T1** [dev+0x1C28] |
| 187 | 0x2476F0 | 0x247717 | 0x27 | 0 | D3DDevice_FlushVertexCache |  | D3DDevice_MakeSpace |  |
| 188 | 0x247720 | 0x2477D5 | 0xB5 | 12 |  |  | D3DDevice_MakeSpace, 0x24CB50(0x70) |  |
| 189 | 0x2477E0 | 0x247905 | 0x125 | 16 |  |  | D3DDevice_BeginPush, 0x24CB50(0x70) |  |
| 190 | 0x247910 | 0x247937 | 0x27 | 0 |  |  | D3DDevice_MakeSpace |  |
| 191 | 0x247940 | 0x247A11 | 0xD1 | 8 |  |  | 0x2530DA(0x1EE), 0x2532C8(0xC2) | **T1** [dev+0x1C28]; internal SetTile impl (behind D3DDevice_SetTile thunk) |
| 192 | 0x247A20 | 0x247A2A | 0xA | — (tail jmp) | D3DDevice_SetTile |  | D3D_KickOffAndWaitForIdle |  |
| 193 | 0x247A30 | 0x247ABF | 0x8F | 16 |  |  | — |  |
| 194 | 0x247AC0 | 0x247B4F | 0x8F | 16 |  |  | — |  |
| 195 | 0x247B50 | 0x247C31 | 0xE1 | 8 |  |  | D3D_KickOffAndWaitForIdle |  |
| 196 | 0x247C40 | 0x247F40 | 0x300 | 12 | D3DDevice_SetScissors |  | D3DDevice_MakeSpace, 0x251CB0(0x9) | game-called ×2 |
| 197 | 0x247F40 | 0x247FB8 | 0x78 | 12 |  |  | 0x251100(0x1F5) |  |
| 198 | 0x247FC0 | 0x2480EE | 0x12E | 0 | D3DDevice_PersistDisplay |  | 0x251D40(0x11), 0x247F40(0x78), D3DDevice_CopyRects, D3D_KickOffAndWaitForIdle, 0x245A00(0x65), 0x250770(0x62), 0x252F31(0x47) | **T1** [dev+0x1C28] |
| 199 | 0x2480F0 | 0x248139 | 0x49 | 8 | D3DDevice_SetScreenSpaceOffset |  | 0x24C300(0xEF), D3DDevice_MakeSpace, 0x24A700(0x1E5) |  |
| 200 | 0x248140 | 0x24825A | 0x11A | 4 |  |  | 0x24F16C(0x1F), 0x21F454(ext) | **T1** [dev+0x1C28]; hw colour writer [hwbase+0x68084C] |
| 201 | 0x248260 | 0x2482AD | 0x4D | 4 |  |  | D3DDevice_MakeSpace |  |
| 202 | 0x2482B0 | 0x2484D9 | 0x229 | 0 |  |  | 0x24F120(0xC), 0x24F100(0x1E), 0x24D1D0(0x39) |  |
| 203 | 0x248500 | 0x248516 | 0x16 | 4 |  |  | 0x251CB0(0x9) |  |
| 204 | 0x248520 | 0x24852C | 0xC | 0 |  |  | — |  |
| 205 | 0x248530 | 0x24853E | 0xE | 4 |  |  | — |  |
| 206 | 0x248540 | 0x248541 | 0x1 | 0 |  |  | — |  |
| 207 | 0x248550 | 0x24855C | 0xC | 0 |  |  | — |  |
| 208 | 0x248560 | 0x24856E | 0xE | 4 |  |  | — |  |
| 209 | 0x248570 | 0x24890F | 0x39F | 24 | D3DDevice_Clear |  | 0x250930(0xBB), D3DDevice_MakeSpace, 0x251CB0(0x9) | game-called ×9 |
| 210 | 0x248960 | 0x248987 | 0x27 | — (tail jmp) |  |  | — |  |
| 211 | 0x248987 | 0x248A1A | 0x93 | — (tail jmp) |  |  | — | continuation of 0x248960 |
| 212 | 0x248A20 | 0x248A3F | 0x1F | 0 |  |  | — | continuation of 0x248960 |
| 213 | 0x248A40 | 0x248A78 | 0x38 | 0 |  |  | D3DDevice_SetRenderTarget, D3DDevice_SetTexture |  |
| 214 | 0x248A80 | 0x248AB5 | 0x35 | 0 |  |  | — |  |
| 215 | 0x248AC0 | 0x248AC8 | 0x8 | 0 |  |  | D3DDevice_SetRenderStateNotInline |  |
| 216 | 0x248AD0 | 0x248BEE | 0x11E | 0 |  |  | D3D_KickOffAndWaitForIdle, D3DDevice_BlockUntilVerticalBlank, 0x252F31(0x47), 0x108F58(ext), 0x108F3E(ext) | **T1** [dev+0x1C28]; present/vblank helper (calls miniport 0x252F31) |
| 217 | 0x248BF0 | 0x248BF9 | 0x9 | 0 |  |  | D3DDevice_SetTextureStageStateNotInline |  |
| 218 | 0x248C00 | 0x248C2B | 0x2B | 0 |  |  | D3D_SetFence, D3D_BlockOnTime |  |
| 219 | 0x248C30 | 0x248C50 | 0x20 | 8 |  |  | — |  |
| 220 | 0x248C50 | 0x248C55 | 0x5 | 4 |  |  | — |  |
| 221 | 0x248C60 | 0x248C76 | 0x16 | 4 |  |  | 0x251CB0(0x9) |  |
| 222 | 0x248C80 | 0x248C8C | 0xC | 0 |  |  | — |  |
| 223 | 0x248C90 | 0x248CA5 | 0x15 | 4 |  |  | — |  |
| 224 | 0x248CB0 | 0x248CC9 | 0x19 | 8 |  |  | — |  |
| 225 | 0x248CD0 | 0x248CD1 | 0x1 | 0 |  |  | — |  |
| 226 | 0x248CE0 | 0x248CEC | 0xC | 0 |  |  | — |  |
| 227 | 0x248CF0 | 0x248D05 | 0x15 | 4 |  |  | — |  |
| 228 | 0x248D10 | 0x248D29 | 0x19 | 8 |  |  | — |  |
| 229 | 0x248D30 | 0x248D70 | 0x40 | 4 |  |  | D3DDevice_SetRenderStateNotInline |  |
| 230 | 0x248D70 | 0x248D8B | 0x1B | 0 |  |  | D3DDevice_SetTextureStageStateNotInline |  |
| 231 | 0x248D90 | 0x248EA7 | 0x117 | 0 |  |  | D3DDevice_SetRenderStateNotInline, D3DDevice_SetTextureStageStateNotInline, D3DDevice_SetPixelShader, 0x2550B0(0x7E), 0x24C2A0(0x39), D3DDevice_MakeSpace |  |
| 232 | 0x248EB0 | 0x24902D | 0x17D | 4 |  |  | D3DDevice_SetPixelShader, D3DDevice_SetTextureStageStateNotInline, D3DDevice_SetRenderStateNotInline, 0x24C2A0(0x39), D3DDevice_MakeSpace, 0x24A700(0x1E5) |  |
| 233 | 0x249030 | 0x24906A | 0x3A | — (tail jmp) |  |  | 0x2449F0(0x37) |  |
| 234 | 0x249070 | 0x2490BA | 0x4A | 0 |  |  | 0x2449F0(0x37) | continuation of 0x249030 |
| 235 | 0x2490C0 | 0x249128 | 0x68 | 0 |  |  | D3DDevice_SetRenderTarget, D3DDevice_SetViewport, 0x244A30(0x4E), D3DDevice_SetTexture |  |
| 236 | 0x249130 | 0x2492AB | 0x17B | — (tail jmp) |  |  | D3DDevice_Begin, 0x24E030(0x37) |  |
| 237 | 0x2492B0 | 0x249495 | 0x1E5 | 4 |  |  | 0x249030(0x3A), D3DDevice_SetRenderTarget, D3DDevice_SetTexture, 0x248960(0x27), 0x248D90(0x117), D3DDevice_MakeSpace, 0x249130(0x17B), 0x248EB0(0x17D), 0x2490C0(0x68) |  |
| 238 | 0x2494A0 | 0x2495A4 | 0x104 | 0 |  |  | D3DDevice_MakeSpace |  |
| 239 | 0x2495B0 | 0x2495F0 | 0x40 | 0 |  |  | D3D_SetFence, 0x248AD0(0x11E) |  |
| 240 | 0x2495F0 | 0x2496D5 | 0xE5 | 4 | D3DDevice_Swap |  | 0x255460(0x8C), D3D_SetFence, D3D_BlockOnTime, 0x2492B0(0x1E5), 0x2494A0(0x104), 0x2495B0(0x40), 0x2558F0(0x564) | game-called ×7 |
| 241 | 0x2496E0 | 0x2497E4 | 0x104 | 8 |  |  | D3DDevice_MakeSpace, 0x24AAC0(0x355) |  |
| 242 | 0x2497F0 | 0x2498A7 | 0xB7 | 12 |  |  | 0x249030(0x3A), D3DDevice_SetRenderTarget, D3DDevice_SetTexture, 0x248960(0x27), 0x248D90(0x117), 0x249130(0x17B), 0x248EB0(0x17D), 0x2490C0(0x68) |  |
| 243 | 0x2498B0 | 0x2498D2 | 0x22 | 4 | D3DDevice_SetRenderState_PSTextureModes |  | — | game-called ×3 |
| 244 | 0x2498E0 | 0x249907 | 0x27 | 0 | D3DDevice_SetRenderState_Simple |  | D3DDevice_MakeSpace | game-called ×45 |
| 245 | 0x249910 | 0x24996D | 0x5D | 8 | D3DDevice_SetRenderStateNotInline |  | D3DDevice_SetRenderState_Simple |  |
| 246 | 0x249970 | 0x249978 | 0x8 | 0 |  |  | D3DDevice_SetRenderStateNotInline |  |
| 247 | 0x249980 | 0x249AA2 | 0x122 | 8 |  |  | — |  |
| 248 | 0x249AB0 | 0x249BF0 | 0x140 | 0 |  |  | 0x251460(0xFA) |  |
| 249 | 0x249BF0 | 0x249C38 | 0x48 | 12 |  |  | — |  |
| 250 | 0x249C40 | 0x249C47 | 0x7 | 4 |  |  | — |  |
| 251 | 0x249C50 | 0x249C57 | 0x7 | 4 |  |  | — |  |
| 252 | 0x249C60 | 0x249C76 | 0x16 | 4 |  |  | 0x251CB0(0x9) |  |
| 253 | 0x249C80 | 0x249C8C | 0xC | 0 |  |  | — |  |
| 254 | 0x249C90 | 0x249CA5 | 0x15 | 4 |  |  | — |  |
| 255 | 0x249CB0 | 0x249CCC | 0x1C | 8 |  |  | — |  |
| 256 | 0x249CD0 | 0x249CE9 | 0x19 | 8 |  |  | — |  |
| 257 | 0x249CF0 | 0x249D17 | 0x27 | 16 |  |  | — |  |
| 258 | 0x249D20 | 0x249D2E | 0xE | 4 |  |  | — |  |
| 259 | 0x249D30 | 0x249D31 | 0x1 | 0 |  |  | — |  |
| 260 | 0x249D40 | 0x249D4C | 0xC | 0 |  |  | — |  |
| 261 | 0x249D50 | 0x249D65 | 0x15 | 4 |  |  | — |  |
| 262 | 0x249D70 | 0x249D8C | 0x1C | 8 |  |  | — |  |
| 263 | 0x249D90 | 0x249DA9 | 0x19 | 8 |  |  | — |  |
| 264 | 0x249DB0 | 0x249DD7 | 0x27 | 16 |  |  | — |  |
| 265 | 0x249DE0 | 0x249DEE | 0xE | 4 |  |  | — |  |
| 266 | 0x249DF0 | 0x249E14 | 0x24 | 4 |  |  | — |  |
| 267 | 0x249E20 | 0x249E52 | 0x32 | 4 | D3DDevice_SetRenderState_EdgeAntiAlias |  | D3DDevice_MakeSpace | game-called ×3 |
| 268 | 0x249E60 | 0x249E95 | 0x35 | 4 | D3DDevice_SetRenderState_ShadowFunc |  | D3DDevice_MakeSpace | game-called ×3 |
| 269 | 0x249EA0 | 0x249EED | 0x4D | 4 | D3DDevice_SetRenderState_FogColor |  | D3DDevice_MakeSpace | game-called ×3 |
| 270 | 0x249EF0 | 0x249F58 | 0x68 | 4 | D3DDevice_SetRenderState_CullMode |  | D3DDevice_MakeSpace | game-called ×5 |
| 271 | 0x249F60 | 0x249F9A | 0x3A | — (tail jmp) | D3DDevice_SetRenderState_FrontFace |  | D3DDevice_MakeSpace | game-called ×3 |
| 272 | 0x249FA0 | 0x249FD9 | 0x39 | 4 | D3DDevice_SetRenderState_NormalizeNormals |  | D3DDevice_MakeSpace | game-called ×3 |
| 273 | 0x249FE0 | 0x24A031 | 0x51 | 4 | D3DDevice_SetRenderState_TextureFactor |  | D3DDevice_MakeSpace | game-called ×3 |
| 274 | 0x24A040 | 0x24A0A5 | 0x65 | 4 | D3DDevice_SetRenderState_LineWidth |  | 0x251CB0(0x9), D3DDevice_MakeSpace | game-called ×3 |
| 275 | 0x24A0B0 | 0x24A127 | 0x77 | 4 | D3DDevice_SetRenderState_Dxt1NoiseEnable |  | D3DDevice_MakeSpace | game-called ×3 |
| 276 | 0x24A130 | 0x24A1A4 | 0x74 | 4 |  |  | D3DDevice_SetRenderStateNotInline | game-called ×3 |
| 277 | 0x24A1B0 | 0x24A202 | 0x52 | 4 | D3DDevice_SetRenderState_LogicOp |  | D3DDevice_MakeSpace | game-called ×3 |
| 278 | 0x24A210 | 0x24A254 | 0x44 | 4 | D3DDevice_SetRenderState_FillMode |  | D3DDevice_MakeSpace | game-called ×5 |
| 279 | 0x24A260 | 0x24A2B1 | 0x51 | 4 | D3DDevice_SetRenderState_BackFillMode |  | D3DDevice_MakeSpace | game-called ×3 |
| 280 | 0x24A2C0 | 0x24A340 | 0x80 | 4 | D3DDevice_SetRenderState_TwoSidedLighting |  | D3DDevice_MakeSpace | game-called ×3 |
| 281 | 0x24A340 | 0x24A381 | 0x41 | 4 | D3DDevice_SetRenderState_VertexBlend |  | D3DDevice_MakeSpace | game-called ×3 |
| 282 | 0x24A390 | 0x24A495 | 0x105 | 8 | D3DDevice_SetTextureState_TexCoordIndex |  | D3DDevice_MakeSpace | game-called ×3 |
| 283 | 0x24A4A0 | 0x24A503 | 0x63 | 12 | D3DDevice_SetTextureState_BumpEnv |  | D3DDevice_MakeSpace | game-called ×3 |
| 284 | 0x24A510 | 0x24A54D | 0x3D | 8 | D3DDevice_SetTextureState_BorderColor |  | D3DDevice_MakeSpace | game-called ×3 |
| 285 | 0x24A550 | 0x24A589 | 0x39 | 8 | D3DDevice_SetTextureState_ColorKeyColor |  | D3DDevice_MakeSpace | game-called ×3 |
| 286 | 0x24A590 | 0x24A595 | 0x5 | 12 |  |  | — |  |
| 287 | 0x24A5A0 | 0x24A68C | 0xEC | 12 | D3DDevice_SetTextureStageStateNotInline |  | D3DDevice_SetTextureState_TexCoordIndex, D3DDevice_SetTextureState_BorderColor, D3DDevice_SetTextureState_ColorKeyColor |  |
| 288 | 0x24A690 | 0x24A6FA | 0x6A | 0 |  |  | — |  |
| 289 | 0x24A700 | 0x24A8E5 | 0x1E5 | 0 |  |  | 0x249980(0x122) |  |
| 290 | 0x24A8F0 | 0x24A949 | 0x59 | 0 |  |  | — |  |
| 291 | 0x24A950 | 0x24A995 | 0x45 | 0 |  |  | D3DDevice_MakeSpace |  |
| 292 | 0x24A9A0 | 0x24A9E1 | 0x41 | 0 |  |  | — |  |
| 293 | 0x24A9F0 | 0x24AABE | 0xCE | 0 |  |  | D3DDevice_MakeSpace |  |
| 294 | 0x24AAC0 | 0x24AE15 | 0x355 | 0 |  |  | 0x251CB0(0x9), D3DDevice_MakeSpace, 0x24C300(0xEF), D3DDevice_SetViewport |  |
| 295 | 0x24AE20 | 0x24AEAB | 0x8B | 12 |  |  | D3DDevice_MakeSpace, 0x24A700(0x1E5) |  |
| 296 | 0x24AEC0 | 0x24AF4F | 0x8F | 4 | D3DDevice_SetRenderState_ZEnable |  | D3DDevice_MakeSpace, 0x249AB0(0x140), 0x24C300(0xEF), 0x24A8F0(0x59), 0x24A700(0x1E5) | game-called ×6 |
| 297 | 0x24AF50 | 0x24AFD4 | 0x84 | 4 | D3DDevice_SetRenderState_StencilEnable |  | D3DDevice_MakeSpace | game-called ×4 |
| 298 | 0x24AFE0 | 0x24B04D | 0x6D | 4 | D3DDevice_SetRenderState_StencilFail |  | D3DDevice_MakeSpace | game-called ×4 |
| 299 | 0x24B050 | 0x24B07B | 0x2B | 4 | D3DDevice_SetRenderState_YuvEnable |  | D3DDevice_MakeSpace, 0x24A8F0(0x59) | game-called ×3 |
| 300 | 0x24B080 | 0x24B0E2 | 0x62 | 4 | D3DDevice_SetRenderState_OcclusionCullEnable |  | D3DDevice_MakeSpace | game-called ×3 |
| 301 | 0x24B0F0 | 0x24B152 | 0x62 | 4 | D3DDevice_SetRenderState_StencilCullEnable |  | D3DDevice_MakeSpace | game-called ×3 |
| 302 | 0x24B160 | 0x24B171 | 0x11 | 4 | D3DDevice_SetRenderState_RopZCmpAlwaysRead |  | 0x24A9F0(0xCE) | game-called ×3 |
| 303 | 0x24B180 | 0x24B191 | 0x11 | 4 | D3DDevice_SetRenderState_RopZRead |  | 0x24A9F0(0xCE) | game-called ×4 |
| 304 | 0x24B1A0 | 0x24B1B1 | 0x11 | 4 | D3DDevice_SetRenderState_DoNotCullUncompressed |  | 0x24A9F0(0xCE) | game-called ×3 |
| 305 | 0x24B1C0 | 0x24B1E7 | 0x27 | 4 | D3DDevice_SetRenderState_MultiSampleMode |  | 0x24AAC0(0x355) | game-called ×3 |
| 306 | 0x24B1F0 | 0x24B217 | 0x27 | 4 | D3DDevice_SetRenderState_MultiSampleRenderTargetMode |  | 0x24AAC0(0x355) | game-called ×3 |
| 307 | 0x24B220 | 0x24B292 | 0x72 | 4 | D3DDevice_SetRenderState_MultiSampleAntiAlias |  | 0x24C300(0xEF), D3DDevice_MakeSpace, 0x24A700(0x1E5) | game-called ×4 |
| 308 | 0x24B2A0 | 0x24B2F0 | 0x50 | 4 | D3DDevice_SetRenderState_MultiSampleMask |  | D3DDevice_MakeSpace | game-called ×3 |
| 309 | 0x24B2F0 | 0x24B33E | 0x4E | 4 | D3DDevice_SetRenderState_SampleAlpha |  | D3DDevice_MakeSpace | game-called ×3 |
| 310 | 0x24B340 | 0x24B479 | 0x139 | 8 |  |  | — |  |
| 311 | 0x24B480 | 0x24B483 | 0x3 | 4 |  |  | — |  |
| 312 | 0x24B490 | 0x24B53E | 0xAE | 8 |  |  | — |  |
| 313 | 0x24B540 | 0x24B5A8 | 0x68 | 12 |  |  | — |  |
| 314 | 0x24B5B0 | 0x24B5E7 | 0x37 | — (tail jmp) |  |  | — |  |
| 315 | 0x24B5F0 | 0x24B604 | 0x14 | 4 |  |  | — | continuation of 0x24B5B0 |
| 316 | 0x24B610 | 0x24B7A9 | 0x199 | 12 |  |  | — |  |
| 317 | 0x24B7B0 | 0x24B8A7 | 0xF7 | 8 |  |  | — |  |
| 318 | 0x24B8B0 | 0x24B9EE | 0x13E | 16 | D3DDevice_CreateVertexShader |  | 0x24B7B0(0xF7), 0x108E9E(ext), 0x24B610(0x199), 0x24B540(0x68) | game-called ×4 |
| 319 | 0x24B9F0 | 0x24BA04 | 0x14 | 4 |  |  | — |  |
| 320 | 0x24BA10 | 0x24BA6F | 0x5F | 0 | D3DDevice_SetVertexShaderConstant1 |  | D3DDevice_MakeSpace | game-called ×2 |
| 321 | 0x24BA70 | 0x24BAB9 | 0x49 | 0 | D3DDevice_SetVertexShaderConstant1Fast |  | D3DDevice_MakeSpace |  |
| 322 | 0x24BAC0 | 0x24BB62 | 0xA2 | 0 | D3DDevice_SetVertexShaderConstant4 |  | D3DDevice_MakeSpace | game-called ×2 |
| 323 | 0x24BB70 | 0x24BC40 | 0xD0 | 4 | D3DDevice_SetVertexShaderConstantNotInlineFast |  | D3DDevice_MakeSpace |  |
| 324 | 0x24BC40 | 0x24BC50 | 0x10 | — (tail jmp) |  |  | — | continuation of 0x24BB70; 2-byte `emms` dead tail fragment, not a callable function |
| 325 | 0x24BC50 | 0x24BC81 | 0x31 | 4 |  |  | D3DDevice_SetVertexShaderConstantNotInlineFast | vertex-shader-constant setter, game-called entry (calls NotInlineFast); game-called ×4 |
| 326 | 0x24BC90 | 0x24BCBD | 0x2D | 12 |  |  | — |  |
| 327 | 0x24BCC0 | 0x24BCF7 | 0x37 | — (tail jmp) |  |  | — |  |
| 328 | 0x24BD00 | 0x24BD61 | 0x61 | 12 |  |  | — | continuation of 0x24BCC0 |
| 329 | 0x24BD70 | 0x24BD7C | 0xC | 0 |  |  | — |  |
| 330 | 0x24BD80 | 0x24BD95 | 0x15 | 4 |  |  | — |  |
| 331 | 0x24BDA0 | 0x24BDC7 | 0x27 | 16 |  |  | — |  |
| 332 | 0x24BDD0 | 0x24BDDE | 0xE | 4 |  |  | — |  |
| 333 | 0x24BDE0 | 0x24BDE1 | 0x1 | 0 |  |  | — |  |
| 334 | 0x24BDF0 | 0x24BDF1 | 0x1 | 0 |  |  | — |  |
| 335 | 0x24BE00 | 0x24BE0F | 0xF | 0 |  |  | 0x24CB50(0x70) |  |
| 336 | 0x24BE10 | 0x24BE1C | 0xC | 0 |  |  | — |  |
| 337 | 0x24BE20 | 0x24BE35 | 0x15 | 4 |  |  | — |  |
| 338 | 0x24BE40 | 0x24BE67 | 0x27 | 16 |  |  | — |  |
| 339 | 0x24BE70 | 0x24BE7E | 0xE | 4 |  |  | — |  |
| 340 | 0x24BE80 | 0x24BE84 | 0x4 | 0 |  |  | — |  |
| 341 | 0x24BE90 | 0x24BF1C | 0x8C | 12 | D3DDevice_SetStreamSource |  | 0x2448D0(0x91) | game-called ×20 |
| 342 | 0x24BF20 | 0x24BF5C | 0x3C | 8 |  |  | 0x2449F0(0x37) |  |
| 343 | 0x24BF60 | 0x24BFB2 | 0x52 | 8 |  |  | D3DDevice_MakeSpace |  |
| 344 | 0x24BFC0 | 0x24C014 | 0x54 | 8 | D3DDevice_LoadVertexShader |  | 0x24B490(0xAE), D3DDevice_BeginPush |  |
| 345 | 0x24C020 | 0x24C084 | 0x64 | 8 |  |  | D3DDevice_BeginPush, 0x24B540(0x68) |  |
| 346 | 0x24C090 | 0x24C12C | 0x9C | 8 | D3DDevice_SelectVertexShader |  | D3DDevice_MakeSpace, 0x24A700(0x1E5) |  |
| 347 | 0x24C130 | 0x24C144 | 0x14 | 8 |  |  | — |  |
| 348 | 0x24C150 | 0x24C17A | 0x2A | 8 |  |  | — |  |
| 349 | 0x24C180 | 0x24C298 | 0x118 | 4 | D3DDevice_SetShaderConstantMode |  | D3DDevice_MakeSpace, 0x24CB50(0x70) |  |
| 350 | 0x24C2A0 | 0x24C2D9 | 0x39 | 8 |  |  | D3DDevice_MakeSpace, 0x24B540(0x68) |  |
| 351 | 0x24C2E0 | 0x24C2FB | 0x1B | 4 | D3DDevice_DeleteVertexShader |  | 0x108F3E(ext) | game-called ×7 |
| 352 | 0x24C300 | 0x24C3EF | 0xEF | 0 |  |  | D3DDevice_BeginPush, 0x24B540(0x68) |  |
| 353 | 0x24C3F0 | 0x24C553 | 0x163 | 4 | D3DDevice_SetVertexShader |  | D3DDevice_LoadVertexShader, D3DDevice_SelectVertexShader, 0x24B340(0x139), D3DDevice_MakeSpace, 0x24A700(0x1E5), 0x24C300(0xEF) | game-called ×22 |
| 354 | 0x24C560 | 0x24C589 | 0x29 | 12 |  |  | — |  |
| 355 | 0x24C590 | 0x24C5BA | 0x2A | — (tail jmp) |  |  | — |  |
| 356 | 0x24C5C0 | 0x24C861 | 0x2A1 | 12 |  |  | — | continuation of 0x24C590 |
| 357 | 0x24C870 | 0x24C92C | 0xBC | 12 | D3DDevice_GetVertexShaderFunction |  | — |  |
| 358 | 0x24C930 | 0x24CA7D | 0x14D | 12 |  |  | 0x2448D0(0x91), D3DDevice_MakeSpace |  |
| 359 | 0x24CA80 | 0x24CADE | 0x5E | 12 |  |  | — |  |
| 360 | 0x24CAE0 | 0x24CAF3 | 0x13 | 0 |  |  | — |  |
| 361 | 0x24CB00 | 0x24CB2F | 0x2F | 4 |  |  | — |  |
| 362 | 0x24CB30 | 0x24CB43 | 0x13 | 4 |  |  | — |  |
| 363 | 0x24CB50 | 0x24CBC0 | 0x70 | 12 |  |  | — |  |
| 364 | 0x24CBC0 | 0x24CBC4 | 0x4 | 0 |  |  | — |  |
| 365 | 0x24CBD0 | 0x24CBD4 | 0x4 | 0 |  |  | — |  |
| 366 | 0x24CBE0 | 0x24CC13 | 0x33 | 4 |  |  | 0x108E9E(ext) |  |
| 367 | 0x24CC20 | 0x24CC2C | 0xC | 0 |  |  | 0x108F3E(ext) |  |
| 368 | 0x24CC30 | 0x24CC3C | 0xC | 0 |  |  | — |  |
| 369 | 0x24CC40 | 0x24CC55 | 0x15 | 4 |  |  | — |  |
| 370 | 0x24CC60 | 0x24CC61 | 0x1 | 0 |  |  | — |  |
| 371 | 0x24CC70 | 0x24CC81 | 0x11 | 4 |  |  | — |  |
| 372 | 0x24CC90 | 0x24CC91 | 0x1 | 0 |  |  | — |  |
| 373 | 0x24CCA0 | 0x24CCAC | 0xC | 0 |  |  | — |  |
| 374 | 0x24CCB0 | 0x24CCC5 | 0x15 | 4 |  |  | — |  |
| 375 | 0x24CCD0 | 0x24CCE4 | 0x14 | 4 |  |  | — |  |
| 376 | 0x24CCF0 | 0x24CCFF | 0xF | 0 |  |  | — |  |
| 377 | 0x24CD00 | 0x24CD03 | 0x3 | 4 |  |  | — |  |
| 378 | 0x24CD10 | 0x24CDB0 | 0xA0 | 0 |  |  | 0x108E9E(ext) |  |
| 379 | 0x24CDB0 | 0x24CDD9 | 0x29 | 0 |  |  | 0x108F3E(ext) |  |
| 380 | 0x24CDE0 | 0x24CEDE | 0xFE | 0 |  |  | — |  |
| 381 | 0x24CEE0 | 0x24CF5A | 0x7A | 0 |  |  | — | T3 hw-offset pattern |
| 382 | 0x24CF60 | 0x24CF79 | 0x19 | — (tail jmp) |  |  | — |  |
| 383 | 0x24CF80 | 0x24CFDB | 0x5B | 8 |  |  | 0x24CDE0(0xFE) | continuation of 0x24CF60 |
| 384 | 0x24CFE0 | 0x24D00D | 0x2D | 0 |  |  | — | **T1** [dev+0x1C28]; hw fence spin [hwbase+0x100410] |
| 385 | 0x24D010 | 0x24D02E | 0x1E | 4 | D3DDevice_IsFencePending |  | — |  |
| 386 | 0x24D030 | 0x24D09B | 0x6B | 12 | D3DDevice_InsertCallback |  | D3DDevice_MakeSpace | game-called ×4 |
| 387 | 0x24D0A0 | 0x24D0BB | 0x1B | — (tail jmp) |  |  | 0x255130(0x191) |  |
| 388 | 0x24D0C0 | 0x24D0CF | 0xF | 4 | D3DDevice_EndPush |  | — |  |
| 389 | 0x24D0D0 | 0x24D114 | 0x44 | 4 |  |  | — |  |
| 390 | 0x24D120 | 0x24D1D0 | 0xB0 | 0 | CDevice_KickOff |  | 0x247500(0x2F) | **T1** [dev+0x1C28] |
| 391 | 0x24D1D0 | 0x24D209 | 0x39 | — (tail jmp) |  |  | — | **T1** [dev+0x1C28]; CDevice_KickOff tail block |
| 392 | 0x24D210 | 0x24D22D | 0x1D | — (tail jmp) |  |  | — | continuation of 0x24D1D0 |
| 393 | 0x24D230 | 0x24D23B | 0xB | 0 |  |  | 0x247500(0x2F) | continuation of 0x24D1D0 |
| 394 | 0x24D240 | 0x24D2F8 | 0xB8 | 4 | D3D_SetFence |  | D3DDevice_MakeSpace, CDevice_KickOff |  |
| 395 | 0x24D300 | 0x24D42D | 0x12D | — (tail jmp) | D3D_BlockOnTime |  | D3D_SetFence, 0x24CDE0(0xFE), 0x24CF60(0x19), 0x24CB00(0x2F), 0x24CFE0(0x2D) |  |
| 396 | 0x24D430 | 0x24D441 | 0x11 | 8 |  |  | — | continuation of 0x24D300 |
| 397 | 0x24D470 | 0x24D5A7 | 0x137 | 8 | D3D_MakeRequestedSpace |  | 0x24CEE0(0x7A), D3D_BlockOnTime, CDevice_KickOff, D3D_SetFence |  |
| 398 | 0x24D5B0 | 0x24D5BF | 0xF | 0 | D3DDevice_MakeSpace |  | D3D_MakeRequestedSpace | game-called ×8 |
| 399 | 0x24D5C0 | 0x24D604 | 0x44 | 4 | D3DDevice_BeginPush |  | D3D_MakeRequestedSpace | game-called ×3 |
| 400 | 0x24D610 | 0x24D621 | 0x11 | 0 | D3D_KickOffAndWaitForIdle |  | D3D_BlockOnTime |  |
| 401 | 0x24D630 | 0x24D6A8 | 0x78 | 4 | D3D_BlockOnResource |  | 0x244970(0x35), D3D_BlockOnTime |  |
| 402 | 0x24D6B0 | 0x24D6E6 | 0x36 | 4 |  |  | D3D_BlockOnTime |  |
| 403 | 0x24D6F0 | 0x24D718 | 0x28 | — (tail jmp) |  |  | — |  |
| 404 | 0x24D718 | 0x24D71A | 0x2 | — (tail jmp) |  |  | — | continuation of 0x24D6F0 |
| 405 | 0x24D720 | 0x24D780 | 0x60 | 12 |  |  | — | continuation of 0x24D6F0 |
| 406 | 0x24D780 | 0x24D7E3 | 0x63 | — (tail jmp) |  |  | — |  |
| 407 | 0x24D7E3 | 0x24D847 | 0x64 | 12 |  |  | — | continuation of 0x24D780 |
| 408 | 0x24D850 | 0x24D85C | 0xC | 0 |  |  | — |  |
| 409 | 0x24D860 | 0x24D875 | 0x15 | 4 |  |  | — |  |
| 410 | 0x24D880 | 0x24D899 | 0x19 | 8 |  |  | — |  |
| 411 | 0x24D8A0 | 0x24D8C7 | 0x27 | 16 |  |  | — |  |
| 412 | 0x24D8D0 | 0x24D8DE | 0xE | 4 |  |  | — |  |
| 413 | 0x24D8E0 | 0x24D8E1 | 0x1 | 0 |  |  | — |  |
| 414 | 0x24D8F0 | 0x24D8FC | 0xC | 0 |  |  | — |  |
| 415 | 0x24D900 | 0x24D915 | 0x15 | 4 |  |  | — |  |
| 416 | 0x24D920 | 0x24D939 | 0x19 | 8 |  |  | — |  |
| 417 | 0x24D940 | 0x24D967 | 0x27 | 16 |  |  | — |  |
| 418 | 0x24D970 | 0x24D97E | 0xE | 4 |  |  | — |  |
| 419 | 0x24D980 | 0x24D9A4 | 0x24 | 4 |  |  | — |  |
| 420 | 0x24D9B0 | 0x24D9B8 | 0x8 | 0 |  |  | — |  |
| 421 | 0x24D9C0 | 0x24D9DB | 0x1B | 0 |  |  | D3D_SetFence |  |
| 422 | 0x24D9E0 | 0x24DB45 | 0x165 | 16 | D3DDevice_DrawVerticesUP |  | 0x2552D0(0x13E), D3DDevice_MakeSpace, D3DDevice_BeginPush, 0x24D6F0(0x28), D3D_SetFence | game-called ×7 |
| 423 | 0x24DB50 | 0x24DC87 | 0x137 | 20 |  |  | 0x2552D0(0x13E), D3DDevice_MakeSpace, D3DDevice_BeginPush, D3D_SetFence |  |
| 424 | 0x24DC90 | 0x24DD24 | 0x94 | 12 | D3DDevice_DrawVertices |  | 0x255130(0x191), D3DDevice_BeginPush | game-called ×3 |
| 425 | 0x24DD30 | 0x24DF87 | 0x257 | 12 | D3DDevice_DrawIndexedVertices |  | 0x255130(0x191), D3DDevice_BeginPush, D3D_SetFence | game-called ×2 |
| 426 | 0x24DF90 | 0x24E02C | 0x9C | 8 |  |  | 0x255130(0x191), D3DDevice_MakeSpace |  |
| 427 | 0x24E030 | 0x24E067 | 0x37 | 12 |  |  | D3DDevice_MakeSpace | push-packet writer (3-dword pkt); game-called ×12 |
| 428 | 0x24E070 | 0x24E0C9 | 0x59 | 20 |  |  | D3DDevice_MakeSpace | push-packet writer (5-dword pkt); game-called ×16 |
| 429 | 0x24E0D0 | 0x24E10B | 0x3B | 12 |  |  | D3DDevice_MakeSpace |  |
| 430 | 0x24E110 | 0x24E15D | 0x4D | 20 |  |  | D3DDevice_MakeSpace |  |
| 431 | 0x24E160 | 0x24E1AA | 0x4A | 20 |  |  | D3DDevice_MakeSpace | push-packet writer (2-dword pkt); game-called ×15 |
| 432 | 0x24E1B0 | 0x24E1FC | 0x4C | 8 |  |  | D3DDevice_MakeSpace |  |
| 433 | 0x24E200 | 0x24E235 | 0x35 | 4 | D3DDevice_Begin |  | 0x2550B0(0x7E), D3DDevice_MakeSpace | game-called ×9 |
| 434 | 0x24E240 | 0x24E281 | 0x41 | 0 | D3DDevice_End |  | D3DDevice_MakeSpace, D3D_SetFence | game-called ×9 |
| 435 | 0x24E290 | 0x24E2D8 | 0x48 | 8 | D3DDevice_CreatePixelShader |  | 0x108E9E(ext) |  |
| 436 | 0x24E2E0 | 0x24E2FD | 0x1D | 4 | D3DDevice_DeletePixelShader |  | 0x108F3E(ext) |  |
| 437 | 0x24E300 | 0x24E33F | 0x3F | — (tail jmp) |  |  | — | pixel-shader select helper (embedded shader at device+0x924); game-called ×3 |
| 438 | 0x24E340 | 0x24E354 | 0x14 | 4 |  |  | — | continuation of 0x24E300 |
| 439 | 0x24E360 | 0x24E39A | 0x3A | — (tail jmp) |  |  | — |  |
| 440 | 0x24E3A0 | 0x24E6C8 | 0x328 | 12 |  |  | — | continuation of 0x24E360 |
| 441 | 0x24E6D0 | 0x24E6E9 | 0x19 | 8 |  |  | — |  |
| 442 | 0x24E6F0 | 0x24E6FC | 0xC | 0 |  |  | — |  |
| 443 | 0x24E700 | 0x24E70E | 0xE | 4 |  |  | — |  |
| 444 | 0x24E710 | 0x24E711 | 0x1 | 0 |  |  | — |  |
| 445 | 0x24E720 | 0x24E72C | 0xC | 0 |  |  | — |  |
| 446 | 0x24E730 | 0x24E73E | 0xE | 4 |  |  | — |  |
| 447 | 0x24E740 | 0x24E933 | 0x1F3 | 4 | D3DDevice_SetPixelShader |  | D3DDevice_SetRenderState_TextureFactor, D3DDevice_MakeSpace, 0x24A690(0x6A) |  |
| 448 | 0x24E940 | 0x24E977 | 0x37 | — (tail jmp) | D3DDevice_SetPixelShaderConstant |  | — | game-called ×3 |
| 449 | 0x24E977 | 0x24EC07 | 0x290 | 12 |  |  | D3DDevice_MakeSpace | continuation of 0x24E940 |
| 450 | 0x24EC10 | 0x24EC43 | 0x33 | 4 |  |  | 0x108E9E(ext) |  |
| 451 | 0x24EC50 | 0x24ECA1 | 0x51 | 16 |  |  | 0x108E9E(ext), 0x2449F0(0x37) |  |
| 452 | 0x24ECB0 | 0x24ECE0 | 0x30 | 16 |  |  | — |  |
| 453 | 0x24ECE0 | 0x24ED70 | 0x90 | 16 |  |  | 0x251100(0x1F5), 0x108E9E(ext), 0x108F3E(ext) | cand. D3DDevice_CreateImageSurface (C+); game-called ×2 |
| 454 | 0x24ED70 | 0x24EDDE | 0x6E | 12 |  |  | 0x251100(0x1F5), 0x108E9E(ext) |  |
| 455 | 0x24EDE0 | 0x24EDF8 | 0x18 | 4 |  | 0x80077 | 0x2449F0(0x37) |  |
| 456 | 0x24EE00 | 0x24EE14 | 0x14 | 8 |  | 0x80078 | 0x250C10(0xB1) | surface-desc wrapper -> 0x250C10; game-called ×3 |
| 457 | 0x24EE20 | 0x24EE40 | 0x20 | 16 |  | 0x8007D | Lock2DSurface | Lock2DSurface wrapper (0,0,args); game-called ×3 |
| 458 | 0x24EE40 | 0x24EE8D | 0x4D | 12 |  |  | 0x108E9E(ext), 0x2449F0(0x37) |  |
| 459 | 0x24EE90 | 0x24EEA4 | 0x14 | 8 |  | 0x800A5 | 0x250CD0(0x6C) |  |
| 460 | 0x24EEB0 | 0x24EECE | 0x1E | 16 |  | 0x800AA | 0x251060(0x9C) |  |
| 461 | 0x24EED0 | 0x24EEE8 | 0x18 | 4 |  | 0x800A4 | 0x2449F0(0x37) |  |
| 462 | 0x24EEF0 | 0x24EF24 | 0x34 | 4 |  |  | 0x108E9E(ext) |  |
| 463 | 0x24EF30 | 0x24EF35 | 0x5 | — (tail jmp) |  |  | — |  |
| 464 | 0x24EF40 | 0x24EF73 | 0x33 | 4 |  |  | 0x108E9E(ext) | continuation of 0x24EF30 |
| 465 | 0x24EF80 | 0x24EF8C | 0xC | 0 |  |  | — |  |
| 466 | 0x24EF90 | 0x24EF91 | 0x1 | 0 |  |  | — |  |
| 467 | 0x24EFA0 | 0x24EFAC | 0xC | 0 |  |  | — |  |
| 468 | 0x24EFB0 | 0x24EFBF | 0xF | 4 |  |  | — |  |
| 469 | 0x24EFC0 | 0x24F00B | 0x4B | 4 |  |  | 0x108E9E(ext), 0x108F3E(ext) |  |
| 470 | 0x24F010 | 0x24F05B | 0x4B | 8 |  | 0x8009B | D3DDevice_MakeSpace, 0x24D6B0(0x36) |  |
| 471 | 0x24F060 | 0x24F07C | 0x1C | 8 |  | 0x80096 | 0x244A80(0x8A) |  |
| 472 | 0x24F080 | 0x24F094 | 0x14 | 8 |  | 0x80035 | — |  |
| 473 | 0x24F0A0 | 0x24F0FE | 0x5E | 4 |  |  | 0x108E9E(ext), 0x108F3E(ext) |  |
| 474 | 0x24F100 | 0x24F11E | 0x1E | 8 |  | 0x80048 | 0x24D6B0(0x36) | resource data/busy query; game-called ×4 |
| 475 | 0x24F120 | 0x24F12C | 0xC | 4 |  | 0x80045 | — |  |
| 476 | 0x24F12C | 0x24F16C | 0x40 | 4 |  |  | — |  |
| 477 | 0x24F16C | 0x24F18B | 0x1F | 0 |  |  | — |  |
| 478 | 0x24F18B | 0x24F204 | 0x79 | 0 |  |  | 0x24F16C(0x1F) |  |
| 479 | 0x24F204 | 0x24F469 | 0x265 | 28 |  |  | 0x250770(0x62), 0x2508C0(0x23), 0x24F18B(0x79), 0x24F16C(0x1F), 0x252DA1(0x15), 0x252DD4(0xCE) | **T2** miniport-thiscall; miniport mode-apply |
| 480 | 0x24F470 | 0x24F477 | 0x7 | 0 |  |  | — |  |
| 481 | 0x24F480 | 0x24F507 | 0x87 | 4 |  |  | — |  |
| 482 | 0x24F510 | 0x24F524 | 0x14 | 8 |  |  | D3DDevice_SetTransform |  |
| 483 | 0x24F530 | 0x24F53F | 0xF | 4 |  |  | D3DDevice_SetShaderConstantMode |  |
| 484 | 0x24F540 | 0x24F544 | 0x4 | 0 |  |  | — |  |
| 485 | 0x24F550 | 0x24F556 | 0x6 | 0 |  |  | — |  |
| 486 | 0x24F560 | 0x24F571 | 0x11 | 4 |  |  | — |  |
| 487 | 0x24F580 | 0x24F5B3 | 0x33 | 4 |  |  | 0x108E9E(ext) |  |
| 488 | 0x24F5C0 | 0x24F5CC | 0xC | 0 |  |  | 0x108F3E(ext) |  |
| 489 | 0x24F5D0 | 0x24F5DC | 0xC | 0 |  |  | — |  |
| 490 | 0x24F5E0 | 0x24F5FC | 0x1C | 8 |  |  | — |  |
| 491 | 0x24F600 | 0x24F627 | 0x27 | 16 |  |  | — |  |
| 492 | 0x24F630 | 0x24F63E | 0xE | 4 |  |  | — |  |
| 493 | 0x24F640 | 0x24F641 | 0x1 | 0 |  |  | — |  |
| 494 | 0x24F650 | 0x24F664 | 0x14 | 4 |  |  | — |  |
| 495 | 0x24F670 | 0x24F68B | 0x1B | 8 |  |  | — |  |
| 496 | 0x24F690 | 0x24F6A7 | 0x17 | 8 |  |  | — |  |
| 497 | 0x24F6B0 | 0x24F6BC | 0xC | 0 |  |  | — |  |
| 498 | 0x24F6C0 | 0x24F6DC | 0x1C | 8 |  |  | — |  |
| 499 | 0x24F6E0 | 0x24F707 | 0x27 | 16 |  |  | — |  |
| 500 | 0x24F710 | 0x24F71E | 0xE | 4 |  |  | — |  |
| 501 | 0x24F720 | 0x24F737 | 0x17 | 0 |  |  | — |  |
| 502 | 0x24F830 | 0x24F938 | 0x108 | — (tail jmp) |  |  | D3DDevice_MakeSpace |  |
| 503 | 0x24F940 | 0x24F94D | 0xD | — (tail jmp) | D3D_InitDefaultState |  | — |  |
| 504 | 0x24F950 | 0x24F96D | 0x1D | — (tail jmp) |  |  | D3DDevice_SetTransform | continuation of 0x24F940 |
| 505 | 0x24F970 | 0x24FA02 | 0x92 | 0 |  |  | D3DDevice_SetRenderStateNotInline, D3DDevice_SetTextureStageStateNotInline, D3DDevice_SetTexture, D3DDevice_SetScreenSpaceOffset | continuation of 0x24F940 |
| 506 | 0x24FA10 | 0x24FB3F | 0x12F | 0 |  |  | D3D_InternalRelease, 0x244A30(0x4E), 0x247940(0xD1), 0x108F3E(ext) | **T1** [dev+0x1C28]; CDevice release-surfaces |
| 507 | 0x24FB40 | 0x250020 | 0x4E0 | 4 |  |  | 0x250770(0x62), 0x2508F0(0x3C), 0x251100(0x1F5), 0x108E9E(ext), 0x24ECB0(0x30), 0x245A00(0x65), 0x247940(0xD1) | CDevice mode-set (behind Reset/Create) |
| 508 | 0x250020 | 0x2500D6 | 0xB6 | 4 |  |  | 0x245A00(0x65), 0x247940(0xD1), 0x2449F0(0x37), D3DDevice_MakeSpace |  |
| 509 | 0x2500E0 | 0x2505A5 | 0x4C5 | 4 |  |  | 0x108E9E(ext), 0x24CD10(0xA0), 0x252F78(0x162), 0x252BD7(0xCF), 0x2528EA(0x63), 0x25294D(0x34), 0x252CA6(0x90), CDevice_KickOff, 0x24CAE0(0x13), 0x24F830(0x108), 0x24FB40(0x4E0), 0x245A00(0x65), 0x24F204(0x265), D3D_SetFence, 0x247500(0x2F), D3DDevice_SetVertexShader, D3DDevice_SetRenderTarget, D3D_InitDefaultState, D3DDevice_Clear, D3DDevice_SetFlickerFilter, D3DDevice_SetSoftDisplayFilter | **T1** [dev+0x1C28]; CDevice create body (behind Direct3D_CreateDevice) |
| 510 | 0x2505B0 | 0x25075C | 0x1AC | 0 |  |  | D3D_KickOffAndWaitForIdle, CDevice_KickOff, 0x24CAE0(0x13), 0x251D40(0x11), 0x24FA10(0x12F), 0x24CDB0(0x29), 0x2448D0(0x91), 0x108F3E(ext), 0x25338A(0x1BC) | **T1** [dev+0x1C28]; CDevice destroy body (behind D3DDevice_Release) |
| 511 | 0x250760 | 0x25076C | 0xC | 0 |  |  | — |  |
| 512 | 0x250770 | 0x2507D2 | 0x62 | 4 |  |  | — |  |
| 513 | 0x250830 | 0x25084A | 0x1A | 4 |  |  | — |  |
| 514 | 0x250850 | 0x250856 | 0x6 | 0 |  |  | — |  |
| 515 | 0x250860 | 0x25086A | 0xA | 4 |  |  | — |  |
| 516 | 0x250870 | 0x25088F | 0x1F | 4 |  |  | — |  |
| 517 | 0x250890 | 0x2508A5 | 0x15 | 8 |  |  | — |  |
| 518 | 0x2508B0 | 0x2508BD | 0xD | 4 |  |  | — |  |
| 519 | 0x2508C0 | 0x2508E3 | 0x23 | 4 |  |  | — |  |
| 520 | 0x2508F0 | 0x25092C | 0x3C | 8 |  |  | — |  |
| 521 | 0x250930 | 0x2509EB | 0xBB | 8 |  |  | — |  |
| 522 | 0x250A50 | 0x250A96 | 0x46 | 8 |  |  | — |  |
| 523 | 0x250AC0 | 0x250C10 | 0x150 | 28 |  |  | — |  |
| 524 | 0x250C10 | 0x250CC1 | 0xB1 | 12 |  |  | 0x244A80(0x8A), 0x250AC0(0x150) | surface desc fill |
| 525 | 0x250CD0 | 0x250D3C | 0x6C | 12 |  |  | 0x244A80(0x8A), 0x250AC0(0x150) |  |
| 526 | 0x250D40 | 0x250F63 | 0x223 | 32 |  |  | — |  |
| 527 | 0x250F70 | 0x250FB4 | 0x44 | 20 |  |  | D3D_BlockOnResource, 0x250D40(0x223) |  |
| 528 | 0x250FC0 | 0x25105B | 0x9B | 24 | Lock2DSurface |  | D3D_BlockOnResource, 0x250D40(0x223) |  |
| 529 | 0x251060 | 0x2510FC | 0x9C | 20 |  |  | D3D_BlockOnResource, 0x250D40(0x223) |  |
| 530 | 0x251100 | 0x2512F5 | 0x1F5 | 40 |  |  | 0x250760(0xC) |  |
| 531 | 0x251300 | 0x251304 | 0x4 | 0 |  |  | — |  |
| 532 | 0x251310 | 0x251314 | 0x4 | 0 |  |  | — |  |
| 533 | 0x251320 | 0x251324 | 0x4 | 0 |  |  | — |  |
| 534 | 0x251324 | 0x2513A6 | 0x82 | 4 |  |  | — | T3 hw-offset pattern |
| 535 | 0x2513A6 | 0x2513FF | 0x59 | 4 |  |  | — | T3 hw-offset pattern |
| 536 | 0x251400 | 0x251457 | 0x57 | 16 |  |  | — |  |
| 537 | 0x251460 | 0x25155A | 0xFA | 12 |  |  | — |  |
| 538 | 0x251560 | 0x25162E | 0xCE | 12 |  |  | 0x251CD0(0x52), 0x251D30(0xB), 0x251CB0(0x9) |  |
| 539 | 0x251630 | 0x251637 | 0x7 | 4 |  |  | — |  |
| 540 | 0x251640 | 0x25165F | 0x1F | 0 |  |  | — |  |
| 541 | 0x251660 | 0x2516B9 | 0x59 | 4 |  |  | — |  |
| 542 | 0x2516C0 | 0x2516D1 | 0x11 | 4 |  |  | 0x251660(0x59) |  |
| 543 | 0x2516E0 | 0x25170F | 0x2F | 4 |  |  | 0x251660(0x59) |  |
| 544 | 0x251710 | 0x251752 | 0x42 | 4 |  |  | 0x251660(0x59) |  |
| 545 | 0x251760 | 0x251CA4 | 0x544 | 12 |  |  | 0x251660(0x59) |  |
| 546 | 0x251CB0 | 0x251CB9 | 0x9 | 4 |  |  | — |  |
| 547 | 0x251CC0 | 0x251CC9 | 0x9 | 4 |  |  | — |  |
| 548 | 0x251CD0 | 0x251D22 | 0x52 | 4 |  |  | — |  |
| 549 | 0x251D30 | 0x251D3B | 0xB | 4 |  |  | — |  |
| 550 | 0x251D40 | 0x251D51 | 0x11 | 0 |  |  | — | **T2** miniport-thiscall; miniport status-dword poll |
| 551 | 0x251D60 | 0x251D63 | 0x3 | 0 |  |  | — |  |
| 552 | 0x251D70 | 0x251D74 | 0x4 | 0 |  |  | — |  |
| 553 | 0x251D80 | 0x251D84 | 0x4 | 0 |  |  | — |  |
| 554 | 0x251D90 | 0x251D94 | 0x4 | 0 |  |  | — |  |
| 555 | 0x251DA0 | 0x251DA4 | 0x4 | 0 |  |  | — |  |
| 556 | 0x251DB0 | 0x251DC3 | 0x13 | 4 |  |  | — |  |
| 557 | 0x251DD0 | 0x251DD3 | 0x3 | 0 |  |  | — |  |
| 558 | 0x251DE0 | 0x251DED | 0xD | 0 |  |  | — | T3 hw-offset pattern |
| 559 | 0x251DF0 | 0x251F07 | 0x117 | 8 |  |  | 0x251324(0x82) |  |
| 560 | 0x251F10 | 0x251F37 | 0x27 | — (tail jmp) |  |  | — |  |
| 561 | 0x251F37 | 0x251FDC | 0xA5 | 0 |  |  | 0x251324(0x82), 0x2513A6(0x59) | continuation of 0x251F10; T3 hw-offset pattern |
| 562 | 0x251FF0 | 0x2520FA | 0x10A | 0 |  |  | 0x251DD0(0x3), 0x251F10(0x27) | **T2** miniport-thiscall |
| 563 | 0x252100 | 0x252230 | 0x130 | 0 |  |  | 0x252DD4(0xCE), 0x251FF0(0x10A), 0x252DA1(0x15), 0x252DB6(0x1E) |  |
| 564 | 0x252230 | 0x25224A | 0x1A | 0 |  |  | — |  |
| 565 | 0x252250 | 0x2522B2 | 0x62 | 4 |  |  | 0x25338A(0x1BC) |  |
| 566 | 0x2522C0 | 0x252304 | 0x44 | — (tail jmp) |  |  | 0x255E60(0x33) |  |
| 567 | 0x252304 | 0x252308 | 0x4 | 16 |  |  | — | continuation of 0x2522C0 |
| 568 | 0x252310 | 0x25253B | 0x22B | 4 |  |  | 0x251DD0(0x3), 0x251F10(0x27), 0x255EA0(0x91), 0x24CFE0(0x2D), 0x255E60(0x33) |  |
| 569 | 0x25253B | 0x252543 | 0x8 | 4 |  |  | — |  |
| 570 | 0x252580 | 0x25262B | 0xAB | 0 |  |  | 0x258735(0x123), 0x252310(0x22B), 0x2522C0(0x44) | **T2** miniport-thiscall |
| 571 | 0x252630 | 0x2526E9 | 0xB9 | — (tail jmp) |  |  | 0x255E60(0x33) | **T2** miniport-thiscall |
| 572 | 0x2526E9 | 0x2527B3 | 0xCA | 0 |  |  | 0x252580(0xAB), 0x251FF0(0x10A) | continuation of 0x252630 |
| 573 | 0x2527C0 | 0x252827 | 0x67 | — (tail jmp) |  |  | 0x252580(0xAB), 0x252100(0x130), 0x251FF0(0x10A) |  |
| 574 | 0x252827 | 0x252829 | 0x2 | — (tail jmp) |  |  | — | continuation of 0x2527C0 |
| 575 | 0x252830 | 0x252897 | 0x67 | 16 |  |  | 0x252630(0xB9) | continuation of 0x2527C0 |
| 576 | 0x252897 | 0x2528AA | 0x13 | 4 |  |  | — |  |
| 577 | 0x2528AA | 0x2528EA | 0x40 | 4 |  |  | — |  |
| 578 | 0x2528EA | 0x25294D | 0x63 | 20 |  |  | 0x257B1B(0x18D) |  |
| 579 | 0x25294D | 0x252981 | 0x34 | 4 |  |  | 0x257CA8(0x39) |  |
| 580 | 0x252990 | 0x252994 | 0x4 | 0 |  |  | — |  |
| 581 | 0x2529A0 | 0x2529A4 | 0x4 | 0 |  |  | — |  |
| 582 | 0x2529B0 | 0x2529BE | 0xE | 0 |  |  | — |  |
| 583 | 0x2529C0 | 0x2529C1 | 0x1 | 0 |  |  | — |  |
| 584 | 0x2529D0 | 0x2529D3 | 0x3 | 8 |  |  | — |  |
| 585 | 0x2529E0 | 0x2529F6 | 0x16 | 4 |  |  | — |  |
| 586 | 0x2529F6 | 0x252A21 | 0x2B | 0 |  |  | — |  |
| 587 | 0x252A21 | 0x252A62 | 0x41 | 0 |  |  | — |  |
| 588 | 0x252A62 | 0x252AE5 | 0x83 | 0 |  |  | 0x257054(0x167), 0x2571BB(0xFE), 0x2572B9(0xCF), 0x2528AA(0x40), 0x257388(0x36), 0x2573BE(0x30), 0x2573EE(0x71) |  |
| 589 | 0x252AE5 | 0x252B7D | 0x98 | 0 |  |  | 0x2577D0(0x12), 0x2588D9(0x275), 0x258858(0x81) |  |
| 590 | 0x252B7D | 0x252BD7 | 0x5A | 16 |  |  | — |  |
| 591 | 0x252BD7 | 0x252CA6 | 0xCF | 20 |  |  | 0x252B7D(0x5A) | T3 hw-offset pattern |
| 592 | 0x252CA6 | 0x252D36 | 0x90 | 12 |  |  | 0x257D56(0x9DF), 0x257CE1(0x75), 0x25294D(0x34) |  |
| 593 | 0x252D36 | 0x252DA1 | 0x6B | 4 |  |  | 0x252630(0xB9), 0x252580(0xAB), 0x251FF0(0x10A) | **T2** miniport-thiscall; miniport hw poll (fault site) |
| 594 | 0x252DA1 | 0x252DB6 | 0x15 | 0 |  |  | — | **T2** miniport-thiscall; miniport clock read [hw+0x9400] |
| 595 | 0x252DB6 | 0x252DD4 | 0x1E | 4 |  |  | — |  |
| 596 | 0x252DD4 | 0x252EA2 | 0xCE | 12 |  |  | — | **T2** miniport-thiscall |
| 597 | 0x252EA2 | 0x252F31 | 0x8F | 16 |  |  | 0x21FCF0(ext), 0x21FFD0(ext), 0x252DD4(0xCE) | **T2** miniport-thiscall |
| 598 | 0x252F31 | 0x252F78 | 0x47 | 0 |  |  | — | **T2** miniport-thiscall; miniport reg toggle |
| 599 | 0x252F78 | 0x2530DA | 0x162 | 0 |  |  | 0x2529F6(0x2B), 0x252A21(0x41), 0x252A62(0x83), 0x252AE5(0x98) | **T2** miniport-thiscall; miniport init |
| 600 | 0x2530DA | 0x2532C8 | 0x1EE | 28 |  |  | 0x252D36(0x6B), 0x25745F(0x3F) | **T2** miniport-thiscall; miniport tile-program |
| 601 | 0x2532C8 | 0x25338A | 0xC2 | 8 |  |  | 0x252D36(0x6B), 0x25745F(0x3F) | **T2** miniport-thiscall; miniport tile-clear |
| 602 | 0x25338A | 0x253546 | 0x1BC | 0 |  |  | 0x2532C8(0xC2), 0x252630(0xB9), 0x252580(0xAB), 0x251FF0(0x10A), 0x2574D7(0x1FD), 0x258735(0x123) | **T2** miniport-thiscall; miniport teardown |
| 603 | 0x253550 | 0x253551 | 0x1 | 0 |  |  | — |  |
| 604 | 0x253560 | 0x25357E | 0x1E | 16 |  |  | — |  |
| 605 | 0x253580 | 0x2535C7 | 0x47 | 0 |  |  | — |  |
| 606 | 0x2535D0 | 0x253622 | 0x52 | 0 |  |  | — |  |
| 607 | 0x253630 | 0x25367E | 0x4E | 0 |  |  | — |  |
| 608 | 0x253680 | 0x2536D7 | 0x57 | 0 |  |  | — |  |
| 609 | 0x2536E0 | 0x25372D | 0x4D | 0 |  |  | — |  |
| 610 | 0x253730 | 0x253789 | 0x59 | 0 |  |  | — |  |
| 611 | 0x253790 | 0x2537E5 | 0x55 | 0 |  |  | — |  |
| 612 | 0x2537F0 | 0x25384F | 0x5F | 0 |  |  | — |  |
| 613 | 0x253850 | 0x2538E8 | 0x98 | 0 |  |  | — |  |
| 614 | 0x2538F0 | 0x2538F7 | 0x7 | 4 |  |  | — |  |
| 615 | 0x253900 | 0x25391F | 0x1F | 0 |  |  | — |  |
| 616 | 0x253920 | 0x253948 | 0x28 | 0 |  |  | 0x251660(0x59) |  |
| 617 | 0x253950 | 0x253966 | 0x16 | 4 |  |  | 0x251CB0(0x9) |  |
| 618 | 0x253970 | 0x25397C | 0xC | 0 |  |  | — |  |
| 619 | 0x253980 | 0x253995 | 0x15 | 4 |  |  | — |  |
| 620 | 0x2539A0 | 0x2539B2 | 0x12 | 4 |  |  | — |  |
| 621 | 0x2539C0 | 0x2539E0 | 0x20 | 12 |  |  | — |  |
| 622 | 0x2539E0 | 0x2539EE | 0xE | 4 |  |  | — |  |
| 623 | 0x2539F0 | 0x2539F1 | 0x1 | 0 |  |  | — |  |
| 624 | 0x253A00 | 0x253A01 | 0x1 | 0 |  |  | — |  |
| 625 | 0x253A10 | 0x253A25 | 0x15 | 4 |  |  | 0x24CB50(0x70) |  |
| 626 | 0x253A30 | 0x253A4D | 0x1D | 4 |  |  | — |  |
| 627 | 0x253A50 | 0x253A5C | 0xC | 0 |  |  | — |  |
| 628 | 0x253A60 | 0x253A72 | 0x12 | 4 |  |  | — |  |
| 629 | 0x253A80 | 0x253A95 | 0x15 | 4 |  |  | — |  |
| 630 | 0x253AA0 | 0x253AC0 | 0x20 | 12 |  |  | — |  |
| 631 | 0x253AC0 | 0x253ACE | 0xE | 4 |  |  | — |  |
| 632 | 0x253AD0 | 0x253ADA | 0xA | 4 |  |  | — |  |
| 633 | 0x253AE0 | 0x253AED | 0xD | 0 |  |  | — |  |
| 634 | 0x253AF0 | 0x253C55 | 0x165 | 4 |  |  | D3DDevice_MakeSpace, 0x251CB0(0x9) |  |
| 635 | 0x253C60 | 0x253FDC | 0x37C | 4 |  |  | D3DDevice_MakeSpace |  |
| 636 | 0x253FE0 | 0x2541C5 | 0x1E5 | 8 |  |  | 0x251CB0(0x9), D3DDevice_MakeSpace |  |
| 637 | 0x2541D0 | 0x2543AC | 0x1DC | 4 |  |  | 0x24C300(0xEF), D3DDevice_MakeSpace |  |
| 638 | 0x2543B0 | 0x254730 | 0x380 | 4 |  |  | D3DDevice_MakeSpace |  |
| 639 | 0x254770 | 0x25485E | 0xEE | 4 |  |  | — |  |
| 640 | 0x254860 | 0x254898 | 0x38 | — (tail jmp) |  |  | — |  |
| 641 | 0x254898 | 0x254A14 | 0x17C | 4 |  |  | — | continuation of 0x254860 |
| 642 | 0x254A20 | 0x254B58 | 0x138 | 0 |  |  | 0x251560(0xCE) |  |
| 643 | 0x254B60 | 0x254EAE | 0x34E | 8 |  |  | D3DDevice_MakeSpace, 0x254A20(0x138), 0x253850(0x98), 0x254860(0x38), 0x254770(0xEE), 0x251400(0x57), 0x251710(0x42), 0x251660(0x59) |  |
| 644 | 0x254EB0 | 0x2550A3 | 0x1F3 | 8 |  |  | D3DDevice_MakeSpace, 0x251460(0xFA), 0x24CB50(0x70), 0x251760(0x544) |  |
| 645 | 0x2550B0 | 0x25512E | 0x7E | 0 |  |  | 0x253AF0(0x165), 0x258F80(0x7A), 0x253C60(0x37C), 0x253FE0(0x1E5), 0x2541D0(0x1DC), 0x2543B0(0x380), 0x254B60(0x34E), 0x254EB0(0x1F3) |  |
| 646 | 0x255130 | 0x2552C1 | 0x191 | 4 |  |  | 0x2550B0(0x7E), D3DDevice_MakeSpace |  |
| 647 | 0x2552D0 | 0x25540E | 0x13E | 0 |  |  | 0x2550B0(0x7E), D3DDevice_MakeSpace |  |
| 648 | 0x255410 | 0x25545D | 0x4D | 24 |  |  | 0x251100(0x1F5) |  |
| 649 | 0x255460 | 0x2554EC | 0x8C | 0 |  |  | — |  |
| 650 | 0x255510 | 0x255513 | 0x3 | 0 |  |  | — |  |
| 651 | 0x255520 | 0x255553 | 0x33 | 4 |  |  | 0x108E9E(ext) |  |
| 652 | 0x255560 | 0x25556C | 0xC | 0 |  |  | 0x108F3E(ext) |  |
| 653 | 0x255570 | 0x2558EB | 0x37B | 0 |  |  | 0x10068C(ext), 0x255E60(0x33), 0x1006B8(ext), 0x22AC60(ext), D3DDevice_GetBackBuffer2, 0x255410(0x4D), 0x2497F0(0xB7), 0x244A30(0x4E), 0x2453B0(0x5), 0x22AC71(ext) |  |
| 654 | 0x2558F0 | 0x255E54 | 0x564 | 0 |  |  | 0x108F3E(ext), 0x22AC71(ext), 0x21FCF0(ext), 0x220040(ext), D3DDevice_GetBackBuffer2, 0x255410(0x4D), 0x245A00(0x65), 0x244A30(0x4E), 0x108E9E(ext), 0x103039(ext), 0x100F43(ext), 0x255E60(0x33), 0x107583(ext), 0x107405(ext), 0x1006B8(ext), 0x255570(0x37B), 0x21E8E2(ext), 0x10068C(ext) |  |
| 655 | 0x255E60 | 0x255E93 | 0x33 | 0 |  |  | 0x2323E3(ext), 0x231A30(ext) |  |
| 656 | 0x255EA0 | 0x255F31 | 0x91 | 4 |  |  | — |  |
| 657 | 0x255F40 | 0x255F52 | 0x12 | 0 |  |  | — |  |
| 658 | 0x255F60 | 0x255F7E | 0x1E | 4 |  |  | — |  |
| 659 | 0x255F80 | 0x255F81 | 0x1 | 0 |  |  | — |  |
| 660 | 0x255F90 | 0x255F9C | 0xC | 0 |  |  | — |  |
| 661 | 0x255FA0 | 0x255FB5 | 0x15 | 4 |  |  | — |  |
| 662 | 0x255FC0 | 0x255FCE | 0xE | 4 |  |  | — |  |
| 663 | 0x255FD0 | 0x255FDF | 0xF | 0 |  |  | 0x24CB50(0x70) |  |
| 664 | 0x255FE0 | 0x255FE4 | 0x4 | 0 |  |  | — |  |
| 665 | 0x255FF0 | 0x25600A | 0x1A | 4 |  |  | — |  |
| 666 | 0x256010 | 0x25605F | 0x4F | 12 |  |  | — |  |
| 667 | 0x256060 | 0x256085 | 0x25 | — (tail jmp) |  | 0x80067 | — |  |
| 668 | 0x256090 | 0x2560AE | 0x1E | 16 |  |  | — | continuation of 0x256060 |
| 669 | 0x2560B0 | 0x2560E5 | 0x35 | 16 |  |  | — |  |
| 670 | 0x2560F0 | 0x256133 | 0x43 | 4 |  |  | 0x108E9E(ext) |  |
| 671 | 0x256140 | 0x256143 | 0x3 | 8 |  |  | — |  |
| 672 | 0x256150 | 0x256183 | 0x33 | 4 |  |  | 0x108E9E(ext) |  |
| 673 | 0x256190 | 0x25619C | 0xC | 0 |  |  | — |  |
| 674 | 0x2561A0 | 0x2561AE | 0xE | 4 |  |  | — |  |
| 675 | 0x2561B0 | 0x2561B1 | 0x1 | 0 |  |  | — |  |
| 676 | 0x2561C0 | 0x2561C1 | 0x1 | 0 |  |  | — |  |
| 677 | 0x2561D0 | 0x2561DC | 0xC | 0 |  |  | — |  |
| 678 | 0x2561E0 | 0x2561EE | 0xE | 4 |  |  | — |  |
| 679 | 0x2561F0 | 0x2561F4 | 0x4 | 0 |  |  | — |  |
| 680 | 0x256200 | 0x25621A | 0x1A | — (tail jmp) |  |  | — |  |
| 681 | 0x256220 | 0x25622E | 0xE | 8 |  |  | — | continuation of 0x256200 |
| 682 | 0x256230 | 0x2564CC | 0x29C | 8 |  |  | 0x255130(0x191), D3DDevice_MakeSpace, 0x255EA0(0x91), D3D_SetFence, D3DDevice_BeginPush, 0x24D6F0(0x28) |  |
| 683 | 0x2564D0 | 0x25655E | 0x8E | 8 |  |  | 0x108E9E(ext), 0x108F3E(ext) |  |
| 684 | 0x256560 | 0x2565C5 | 0x65 | 4 |  |  | 0x255130(0x191), 0x24D6B0(0x36) |  |
| 685 | 0x2565D0 | 0x25665D | 0x8D | 0 |  |  | 0x255130(0x191), 0x256660(0xA3), 0x2448D0(0x91) |  |
| 686 | 0x256660 | 0x256703 | 0xA3 | 4 |  |  | D3DDevice_BeginPush |  |
| 687 | 0x256710 | 0x2567C8 | 0xB8 | 16 |  | 0x8005D | — |  |
| 688 | 0x2567D0 | 0x256869 | 0x99 | 20 |  | 0x8005E | 0x24CB50(0x70) |  |
| 689 | 0x256870 | 0x25696B | 0xFB | 24 |  | 0x80064 | 0x24CB50(0x70) |  |
| 690 | 0x256970 | 0x2569DD | 0x6D | — (tail jmp) |  | 0x80066 | — |  |
| 691 | 0x2569E0 | 0x256A96 | 0xB6 | 20 |  |  | — | continuation of 0x256970 |
| 692 | 0x256AA0 | 0x256BAF | 0x10F | 16 |  | 0x80062 | 0x245A00(0x65) |  |
| 693 | 0x256BB0 | 0x256C70 | 0xC0 | 16 |  | 0x80063 | — |  |
| 694 | 0x256C70 | 0x256CD2 | 0x62 | 16 |  | 0x8005F | — |  |
| 695 | 0x256CE0 | 0x256D31 | 0x51 | 12 |  | 0x80053 | 0x247560(0x66), 0x256010(0x4F) |  |
| 696 | 0x256D40 | 0x256DF6 | 0xB6 | 20 |  | 0x80065 | — |  |
| 697 | 0x256E00 | 0x256E60 | 0x60 | 16 |  | 0x80061 | — |  |
| 698 | 0x256E60 | 0x256EBD | 0x5D | 16 |  | 0x80051 | — |  |
| 699 | 0x256EC0 | 0x256F31 | 0x71 | 12 |  | 0x80059 | 0x24CFE0(0x2D) |  |
| 700 | 0x256F40 | 0x256F43 | 0x3 | 8 |  | 0x80068 | — |  |
| 701 | 0x256F50 | 0x256F67 | 0x17 | 4 |  | 0x80030 | 0x24D6B0(0x36) |  |
| 702 | 0x256F70 | 0x256FA0 | 0x30 | 12 |  | 0x8004F | 0x24D6B0(0x36) |  |
| 703 | 0x256FA0 | 0x256FDD | 0x3D | 4 |  | 0x80052 | — |  |
| 704 | 0x256FE0 | 0x256FF5 | 0x15 | 8 |  | 0x80029 | — |  |
| 705 | 0x257000 | 0x25701F | 0x1F | 8 |  | 0x8002A | — |  |
| 706 | 0x257020 | 0x257024 | 0x4 | 0 |  |  | — |  |
| 707 | 0x257030 | 0x257034 | 0x4 | 0 |  |  | — |  |
| 708 | 0x257040 | 0x257044 | 0x4 | 0 |  |  | — |  |
| 709 | 0x257050 | 0x257054 | 0x4 | 0 |  |  | — |  |
| 710 | 0x257054 | 0x2571BB | 0x167 | 0 |  |  | — | T3 hw-offset pattern |
| 711 | 0x2571BB | 0x2572B9 | 0xFE | 0 |  |  | 0x252897(0x13) | T3 hw-offset pattern |
| 712 | 0x2572B9 | 0x257388 | 0xCF | 0 |  |  | — | T3 hw-offset pattern |
| 713 | 0x257388 | 0x2573BE | 0x36 | 0 |  |  | — |  |
| 714 | 0x2573BE | 0x2573EE | 0x30 | 0 |  |  | — | T3 hw-offset pattern |
| 715 | 0x2573EE | 0x25745F | 0x71 | 0 |  |  | — |  |
| 716 | 0x25745F | 0x25749E | 0x3F | 0 |  |  | 0x252580(0xAB), 0x251FF0(0x10A) | **T2** miniport-thiscall; miniport idle-wait |
| 717 | 0x25749E | 0x2574D7 | 0x39 | 4 |  |  | 0x25745F(0x3F) | T3 hw-offset pattern |
| 718 | 0x2574D7 | 0x2576D4 | 0x1FD | 4 |  |  | — |  |
| 719 | 0x2576D4 | 0x2577B9 | 0xE5 | 0 |  |  | — |  |
| 720 | 0x2577B9 | 0x2577D0 | 0x17 | 0 |  |  | — | T3 hw-offset pattern |
| 721 | 0x2577D0 | 0x2577E2 | 0x12 | 0 |  |  | — | T3 hw-offset pattern |
| 722 | 0x2577E2 | 0x25792C | 0x14A | 0 |  |  | — | T3 hw-offset pattern |
| 723 | 0x25792C | 0x257A71 | 0x145 | 0 |  |  | — | T3 hw-offset pattern |
| 724 | 0x257A71 | 0x257B1B | 0xAA | 0 |  |  | — | T3 hw-offset pattern |
| 725 | 0x257B1B | 0x257CA8 | 0x18D | 16 |  |  | — |  |
| 726 | 0x257CA8 | 0x257CE1 | 0x39 | 20 |  |  | — |  |
| 727 | 0x257CE1 | 0x257D56 | 0x75 | 8 |  |  | — | T3 hw-offset pattern |
| 728 | 0x257D56 | 0x258735 | 0x9DF | 0 |  |  | — | T3 hw-offset pattern |
| 729 | 0x258735 | 0x258858 | 0x123 | 4 |  |  | 0x252580(0xAB), 0x25745F(0x3F), 0x25749E(0x39) | T3 hw-offset pattern |
| 730 | 0x258858 | 0x2588D9 | 0x81 | 0 |  |  | 0x2574D7(0x1FD) |  |
| 731 | 0x2588D9 | 0x258B4E | 0x275 | 0 |  |  | 0x25745F(0x3F), 0x258735(0x123) |  |
| 732 | 0x258B50 | 0x258B67 | 0x17 | 0 |  |  | — |  |
| 733 | 0x258B70 | 0x258BF1 | 0x81 | 0 |  |  | — |  |
| 734 | 0x258C10 | 0x258C20 | 0x10 | 0 |  |  | 0x258B70(0x81) |  |
| 735 | 0x258C20 | 0x258C2D | 0xD | 0 |  |  | 0x258B70(0x81) |  |
| 736 | 0x258C30 | 0x258C59 | 0x29 | 4 |  |  | 0x258B70(0x81) |  |
| 737 | 0x258C60 | 0x258C8C | 0x2C | 4 |  |  | 0x258B70(0x81) |  |
| 738 | 0x258C90 | 0x258CBC | 0x2C | 4 |  |  | 0x258B70(0x81) |  |
| 739 | 0x258CC0 | 0x258CFA | 0x3A | 4 |  |  | 0x258B70(0x81) |  |
| 740 | 0x258D00 | 0x258D4A | 0x4A | 8 |  |  | 0x258B70(0x81) |  |
| 741 | 0x258D50 | 0x258D8F | 0x3F | 4 |  |  | 0x258B70(0x81) |  |
| 742 | 0x258D90 | 0x258DCB | 0x3B | 0 |  |  | 0x258B70(0x81) |  |
| 743 | 0x258DD0 | 0x258E0F | 0x3F | 8 |  |  | 0x258B70(0x81) |  |
| 744 | 0x258E10 | 0x258E53 | 0x43 | 8 |  |  | 0x258B70(0x81) |  |
| 745 | 0x258E60 | 0x258E77 | 0x17 | 0 |  |  | 0x258B70(0x81) |  |
| 746 | 0x258E80 | 0x258EA9 | 0x29 | 4 |  |  | 0x258B70(0x81) |  |
| 747 | 0x258EB0 | 0x258EEE | 0x3E | 8 |  |  | 0x258B70(0x81) |  |
| 748 | 0x258EF0 | 0x258F3E | 0x4E | 12 |  |  | 0x258B70(0x81) |  |
| 749 | 0x258F40 | 0x258F4C | 0xC | 0 |  |  | — |  |
| 750 | 0x258F50 | 0x258F51 | 0x1 | 0 |  |  | — |  |
| 751 | 0x258F60 | 0x258F61 | 0x1 | 0 |  |  | — |  |
| 752 | 0x258F70 | 0x258F7C | 0xC | 0 |  |  | — |  |
| 753 | 0x258F80 | 0x258FFA | 0x7A | 8 |  |  | — |  |
| 754 | 0x259000 | 0x25902D | 0x2D | — (tail jmp) |  |  | — |  |
| 755 | 0x259030 | 0x2594D7 | 0x4A7 | 8 |  |  | 0x258B70(0x81), D3DDevice_MakeSpace | continuation of 0x259000 |
| 756 | 0x259540 | 0x25955D | 0x1D | 0 |  |  | — |  |
| 757 | 0x259560 | 0x259564 | 0x4 | 0 |  |  | — |  |
| 758 | 0x259570 | 0x259574 | 0x4 | 0 |  |  | — |  |
| 759 | 0x259580 | 0x2596F6 | 0x176 | 20 |  |  | 0x245A00(0x65) |  |
| 760 | 0x259700 | 0x259763 | 0x63 | 4 | D3DDevice_EnableOverlay |  | — |  |

## 7. Unresolved and anomalies (honest list)

- **~620 unnamed entries remain unnamed.** They are not in the 5849 or 5558 signature sets (every
  name in those sets is already claimed in `config/game.toml` — verified by set difference), and
  urbz names no name Sims lacks. They are library internals: resource/surface helpers
  (`0x244xxx`–`0x245xxx`), push-buffer writers (`0x24Dxxx`–`0x24Exxx`), shader helpers
  (`0x24Bxxx`–`0x24Cxxx`, `0x24EDE0`–`0x24F120` XTLID cluster), the miniport cluster
  (`0x251D40`–`0x253546`), surface/swizzle (XG-style) code (`0x254xxx`–`0x256xxx`), and a second
  large internal cluster (`0x257xxx`–`0x259763`). The table gives every one of them an address,
  length, `retn`, callees and neighbours; assigning XDK names to them would require the 5558
  `d3d8.lib` export list, which is not in either repo.
- **XTLID clusters 3–4 were only sampled.** `0x24EDE0` (0x80077: returns AddRef'd parent resource),
  `0x24F080` (0x80035: writes `{0x65,7}` to an out-struct), `0x256060` (0x80067: copies 0x40 dwords
  into `0x266EB0` state then jumps to `0x256970`), `0x256710` (0x8005D: push-packet builder) and
  their siblings (0x80078, 0x8007D, 0x800A5, 0x800AA, 0x800A4, 0x8009B, 0x80096, 0x80048, 0x80045;
  0x8005D–0x80068, 0x80030, 0x8004F, 0x80052, 0x80029, 0x8002A in the `0x256060`–`0x257000` range)
  are exported and mostly game-called, but their bodies did not decisively match a single XDK API
  name in the time available. They are safe by inspection of the sampled ones (guest-memory
  objects only); a future naming pass should read each body the way §3 was done.
- **Tier 3 (§4) is a pattern, not a finding.** Resolving the `0x257xxx`–`0x258xxx` members needs
  call-site analysis per function (what object is in `ecx`), not done here.
- **IDA artefacts kept visible in the table:** the 2-byte `emms` dead tail at `0x24BC40`
  (fragment of `0x24BB70`, jumped over by every path); 22 one-byte `ret` entries (e.g. `0x245800`,
  `0x24CC60`) that may be shared epilogues; `D3D_BlockOnTime`'s tail block at `0x24D441` that IDA
  did not split out at all (its true extent is `0x24D300`–`0x24D466`); jump-table data embedded in
  `0x252310` (table at `0x252544`) inflating its IDA range; and the `0x24F740`–`0x24F823` code
  chunk with no IDA entry (a continuation block reached from `D3DDevice_SetPixelShader`'s tail
  jump at `0x24F933`). Hand-split count 702–724 (§Summary) reflects exactly these ambiguities.
- **`0x24A130`** (render-state group setter, states 77–81) is game-called and sits in a named
  neighbour slot in both builds, but the XDK name for that state group is not provable from
  anything in either repo (no render-state numbering table), so no name is proposed. Same for
  `0x244BC0` (resource data-offset adjust, ×22 game calls) and the Lock/GetDesc wrapper family
  (`0x2445B0/0x2445C0/0x244610/0x24EE00/0x24EE20` → `0x250C10/0x250D40`): their API family is
  clear, their exact per-wrapper names are not.
- **What was not done:** no build, run or debug (BootCrashTriage owned the build tree throughout);
  no edit to `config/game.toml` or any SDK file — this document is the only file written. The
  post-fix clean run in §2 is BootCrashTriage's reported result, not re-observed here.
