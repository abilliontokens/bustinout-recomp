# Playtest verification

Recorded 2026-10-19 on this machine (3440x1440 KDE Plasma Wayland, AMD BC-250/RADV),
native Linux verbose build (`out/build/linux-verbose/SimsBustinOut`, no `NDEBUG`, so
`LOGI` survives) unless a run says otherwise. Every claim below names the screenshot
or log line that proves it; screenshots and logs live under `out/shots/` (gitignored,
kept on disk). `XBE_MUTE=1` on every automated run; it silences only the SDL output
gain, the guest's sound state is unchanged.

How input was driven, since it decides how much of this is real:

- **Scripted pad** — the shipped `SIMSBO_STEP=<file>` hook (`src/common/game_info.cpp`):
  a token written to the file is one burst of pulses on pad 0. It only ORs bits in,
  exactly as documented. Used for deterministic menu walks.
- **Synthetic keyboard** — there is no `ydotool`/`wtype`/`xdotool` on this box. Keys
  were injected with a throwaway Python helper over `/dev/uinput` (evdev `UInput`,
  a virtual keyboard named `simsbo-test-kbd`; this user has rw ACL on `/dev/uinput`).
  The game window holds focus when launched, so the keys land in the game: the SDL
  host sees them both as ImGui keys (F1/F2) and through `SDL_GetKeyboardState`
  (the keyboard-as-pad layer). The helper lived in `/tmp`, it is not in the repo.
- **Controller** — none is plugged in: `/dev/input/by-id/` holds only a keyboard and
  two mice, and no run log contains an `input: controller ... plugged in` line
  (`sdk/runtime/core/input.cpp` logs each SDL gamepad it opens). Controller support
  is therefore **not proven** here; the SDL gamepad path itself was reviewed, not
  exercised.

## 1. Past the intro: states reached

The intro movies are skippable and the whole front end works. Draw counts are the
`XBE_PERF=1` per-second `perf: per frame N draws` lines, the fastest way to tell a
movie (1 draw) from a menu (tens) from a 3-D scene (hundreds+).

| State | Evidence | Draws/frame |
| --- | --- | --- |
| EA/Maxis logos, intro XMV playing | `out/shots/regress1_14s.png`, boot log opens `d:\ea_games.xmv` then `d:\sims_intro.xmv` at 10.5 s | 1 |
| Title screen ("Please press START to begin" / logo + © 2003 screen) | `out/shots/autostart1_15s.png`, `out/shots/loop_s1.png`, `out/shots/ui3_after_enter.png` | 6–8 |
| Main menu, no save yet: **Bust Out / Credits** | `out/shots/step1_name0.png`, `out/shots/loop_mainmenu.png` | 55 |
| Main menu, with a save: **Continue / Bust Out / Free Play / Credits** | `out/shots/loop2_mainmenu_save.png`, `out/shots/ui2_menu0.png` | 55 |
| Select Save (four empty slots; "Sim 1" once a family exists) | `out/shots/autostart1_25s.png`, `out/shots/loop_saveselect.png`, `out/shots/ui2_kbd_accept.png` | 66–110 |
| "Enter your Sim's last name" (default **Newbie**) | `out/shots/loop_lastname.png` | 476 (CAS room behind) |
| "Enter your Sim's first name" (default **Pat**) | `out/shots/loop_firstname.png` | 476 |
| Create-a-Sim ("CREATE A SIM" / "Customize A Sim", Sim on the rug, five category icons, B BACK / A SELECT) | `out/shots/step3_afterA3.png`, `out/shots/loop_createasim.png` | 328–480 |
| "Are you sure you want to save and continue with this Sim?" | `out/shots/loop3_s2.png` | 66 |
| **Household scene loaded** — the furthest state; it renders into the host target but **presents black** (defect D2 below) | black: `out/shots/loop2_s6.png`; scene in target: `out/build/linux-verbose/cap_house/front_05700_host.bmp` | 2076–2178 |

Menu facts learned by driving them (pad tokens, one press per screenshot):
the main menu opens on its first item; **Bust Out → Select Save**; an empty slot →
last name → first name → Create-a-Sim. On the name keyboard the cursor starts on
**ENTER** (so one A accepts the default name); RIGHT from ENTER wraps to U and then
walks one cell per press (`U V W X Y Z …`) — verified cell-by-cell at
`out/shots/step4_cas_r1.png`…`step4_cas_r5.png` (that run also shows why fixed press
counts desync: one press in three separate runs was swallowed by a screen fade, so
every walk after the first was verified per-screen). In Create-a-Sim, RIGHT×3 from
the first category lands on the confirm (person-with-checkmark) icon
(`out/shots/loop2_s4.png`), A asks the save question, A again (YES) saves and loads
the household. Confirming a Sim writes the family: later boots offer **Continue**
and the save slot reads "Sim 1" (`out/build/linux-verbose/hdd/…/UDATA/45410043/`).

Two earlier sessions are the proof the front end is stable, not just reachable:
`SIMSBO_AUTOSTART` (the blind Start/Up/A 10 s cycle) went title → menus → name entry
on its own and stayed there typing into the name field for 89 s with **zero**
`error:` lines (`out/shots/autostart1.log`); the save above was written by the
interactive `loop1` session before it hit defect D1.

## 2. Defects found

### D1 — missing indirect-call entry point `0x66980` (FIXED, commit `1672ac7`)

Confirming a Sim aborted the game: `signal 6` at guest `0x13F8A7`,
`indirect call to 00066980 has no recompiled function`
(`out/shots/loop1.log:196.586`). `0x66980` is a real `__cdecl` callback —
int3-bounded body `0x66980..0x66E86` ending in a plain `ret`, stored as a function
pointer at `0x696A1` (`mov [edi+0x60], 0x66980`) and called at `0x13F8A5` with four
stack arguments the caller cleans. The recompiler never saw the indirect call —
the same class as the already-fixed `0x2283CD`. Added `"0x66980" = ""` to
`config/game.toml` `[functions]` (that section only) and rebuilt. With it, the
identical path (Continue → Sim 1 → customize → confirm) loads the household:
2076 draws/frame, no trap (`out/shots/loop2.log`).

Note on method: `tools/bringup_loop.sh 8 260` with `SIMSBO_AUTOSTART` reported
"no missing entry point, error lines: 0" in round 1 — a **false pass**. The
autostart cycle never confirms a Sim (it was still typing a name at 89 s), the
release HDD gained no save in 260 s, so the trap point was never reached. The
loop only proves something when the drive is known to pass the trap point;
the attended `SIMSBO_STEP` walk is what reached it.

### D2 — household scene renders into the front target but presents black (OPEN, renderer)

The loaded household (a club lot: dance floor, sofas, several Sims) submits
correctly and never reaches the screen; every menu and the CAS room present fine.
Localised with guest front-buffer captures — **this is the exact reproduction for
whoever takes the renderer next**:

1. Run (save already present in `out/build/linux-verbose/hdd`, so the path is
   short): `SIMSBO_STEP=/tmp/step.txt XBE_PERF=1 XBE_MUTE=1 XBE_CAPTURE_DIR=cap_house
   XBE_VK_CAPTURE_EVERY=300 ./out/build/linux-verbose/SimsBustinOut`
   (`out/shots/loop3.log`), drive pad tokens: START, START (title → main menu),
   A (Continue → Select Save), A (Sim 1 → Customize), RIGHT×3, A (checkmark),
   A (YES). ~30 s later the draws jump to ~2076/frame and
...[truncated 9119 chars]