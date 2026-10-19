# ISO Extraction Record

## Image

`Sims, The - Bustin' Out (USA).xiso.iso`, 2,104,754,176 bytes, sha1 `5f5b2a668e4ab766779abf5d3961fcae095ab54b`. Also symlinked at `assets/Sims, The - Bustin' Out (USA).xiso.iso`.

## Tool

`tools/xiso_extract.py` — a stdlib-only XDVDFS reader with subcommands `info`, `list`, and `extract`.

## Reproduce

```
python3 tools/xiso_extract.py info "Sims, The - Bustin' Out (USA).xiso.iso"
python3 tools/xiso_extract.py list "Sims, The - Bustin' Out (USA).xiso.iso"
python3 tools/xiso_extract.py extract "Sims, The - Bustin' Out (USA).xiso.iso" assets
```

## Volume geometry

Transcript of the `info` run from the repository root:

```text
image            Sims, The - Bustin' Out (USA).xiso.iso
image size       2104754176 bytes
partition base   0x00000000
root sector      264 (offset 0x84000)
root table size  1816 bytes
volume filetime  0x01C3AE716A7FE550
files            60
file bytes       2104138128 (2.0 GiB)
```

- Partition base `0x00000000` — the image is a bare game partition, not a full dual-layer dump.
- 60 files, 2,104,138,128 bytes of file data.

## Contents

Full `list` output (60 files, 2,104,138,128 bytes):

| Path | Size (bytes) |
|---|---|
| `default.xbe` | 3,211,264 |
| `bustinout.bin` | 2,960 |
| `system.cnf` | 732 |
| `index.ind` | 5,992 |
| `index1.ind` | 216,788 |
| `default.ngh` | 1,318,912 |
| `chall.ngh` | 622,592 |
| `story.ngh` | 925,696 |
| `animatio.arc` | 32,140,635 |
| `audiostr.arc` | 79,876 |
| `audiostr.xsb` | 1,872 |
| `audiostr.xwb` | 268,763,136 |
| `binaries.arc` | 323,909 |
| `characte.arc` | 595,041 |
| `datasets.arc` | 152,593,910 |
| `flashes.arc` | 5,768,999 |
| `fonts.arc` | 143,559 |
| `levels.arc` | 4,249 |
| `models.arc` | 43,339,764 |
| `movies.arc` | 18,319,360 |
| `particle.arc` | 41,866 |
| `quickdat.arc` | 18,399,338 |
| `rletextu.arc` | 12,810,630 |
| `samples.arc` | 395,418,728 |
| `scripts.arc` | 4 |
| `shaders.arc` | 454,154 |
| `sounds.arc` | 4 |
| `textures.arc` | 40,580,142 |
| `ea_games.xmv` | 704,512 |
| `ea_games_du.xmv` | 1,077,248 |
| `ea_games_fr.xmv` | 1,118,208 |
| `ea_games_ge.xmv` | 1,077,248 |
| `ea_games_it.xmv` | 1,077,248 |
| `ea_games_pal.xmv` | 688,128 |
| `ea_games_sp.xmv` | 1,077,248 |
| `eor_logo.xmv` | 1,654,784 |
| `eor_logo_pal.xmv` | 1,654,784 |
| `rew_fashion_ntsc.xmv` | 42,864,640 |
| `rew_fashion_pal.xmv` | 41,193,472 |
| `rew_gangster_ntsc.xmv` | 44,097,536 |
| `rew_gangster_pal.xmv` | 41,811,968 |
| `rew_hippy_ntsc.xmv` | 41,627,648 |
| `rew_hippy_pal.xmv` | 39,743,488 |
| `rew_jock_ntsc.xmv` | 44,384,256 |
| `rew_jock_pal.xmv` | 42,246,144 |
| `rew_maltese_ntsc.xmv` | 143,212,544 |
| `rew_maltese_pal.xmv` | 135,208,960 |
| `rew_military_ntsc.xmv` | 39,849,984 |
| `rew_military_pal.xmv` | 37,810,176 |
| `rew_movstart_ntsc.xmv` | 46,166,016 |
| `rew_movstart_pal.xmv` | 44,568,576 |
| `rew_scientist_ntsc.xmv` | 48,394,240 |
| `rew_scientist_pal.xmv` | 45,293,568 |
| `sims_attract.xmv` | 50,532,352 |
| `sims_attract_china.xmv` | 29,978,624 |
| `sims_attract_pal.xmv` | 46,727,168 |
| `sims_ending.xmv` | 24,494,080 |
| `sims_ending_pal.xmv` | 23,941,120 |
| `sims_intro.xmv` | 22,740,992 |
| `sims_intro_pal.xmv` | 21,037,056 |

Grouping notes:

- Engine data: eight `.arc` packfiles, the `.ind` index files, three `.ngh` files, `bustinout.bin`, `system.cnf`.
- Audio: `audiostr.xsb`/`audiostr.xwb` XACT sound bank/wave bank; the wave bank is 268,763,136 bytes.
- Video: 32 `.xmv` files — NTSC and PAL variants of the logo/attract/intro/ending movies plus per-career `rew_*` videos; `rew_maltese_ntsc.xmv` (143,212,544 bytes) is the largest.

## Executable

`python3 tools/xbe_info.py assets/default.xbe --sdk sdk`:

- `assets/default.xbe`: 3,211,264 bytes, sha1 `6feea04a12fb134253abe2b5662da634d0048232`.
- Title `0x45410043` (EA-67), region north_america, media dvd_x2, retail build.
- Base `0x00010000`, entry `0x00100EA2`, image size 4,072,128 bytes, headers 4096.
- Built 2003-11-13T10:18:01+00:00 (cert 2003-11-19T19:29:19+00:00).
- Debug name `c:\eor\src2\games\sims\obj\sims_xbox_devsystem_release\Sims.exe`.
- Kernel thunk `0x002E1C00`, 160 imports (full list and coverage in `docs/kernel-coverage.md`).
- XDK build 5558 for every library:

| Library | Version | Notes |
|---|---|---|
| XAPILIB | 1.0.5558 | qfe=1, approved |
| XBOXKRNL | 1.0.5558 | qfe=1, approved |
| LIBCPMT | 1.0.5558 | qfe=1, approved |
| LIBCMT | 1.0.5558 | qfe=1, approved |
| D3D8 | 1.0.5558 | qfe=4, approved |
| XGRAPHC | 1.0.5558 | qfe=1, approved |
| DSOUND | 1.0.5558 | qfe=1, approved |
| XMV | 1.0.5558 | qfe=1, approved |

Sections:

| Name | Virt addr | Virt size | Raw size | Flags |
|---|---|---|---|---|
| `.text` | 0x00011000 | 2,305,252 | 2,305,252 | preload, executable, head_page_read_only |
| `D3D` | 0x00243D00 | 144,436 | 129,888 | writable, preload, executable |
| `XGRPH` | 0x00267140 | 148,800 | 145,604 | writable, preload, executable |
| `DSOUND` | 0x0028B680 | 156,532 | 155,908 | writable, preload, executable |
| `XMV` | 0x002B1A00 | 164,556 | 164,540 | writable, preload, executable |
| `XPP` | 0x002D9CE0 | 32,540 | 32,540 | writable, preload, executable |
| `.rdata` | 0x002E1C00 | 157,580 | 157,564 | preload, executable |
| `.data` | 0x003083A0 | 900,256 | 31,112 | writable, preload, executable |
| `DOLBY` | 0x003E4040 | 29,056 | 29,036 | preload, executable, tail_page_read_only |
| `$$XTINFO` | 0x003EB1C0 | 494 | 494 | inserted_file, head/tail_page_read_only |
| `$$XTIMAGE` | 0x003EB3C0 | 18,432 | 18,432 | inserted_file, head/tail_page_read_only |
| `$$XSIMAGE` | 0x003EFBC0 | 6,144 | 6,144 | inserted_file, head/tail_page_read_only |
| `.XTLID` | 0x003F13C0 | 3,832 | 3,832 | inserted_file, head/tail_page_read_only |

## Notes

- The extract command writes the tree into `assets/`; the SDK can also read the ISO image directly from `assets/` without extracting.
- `assets/`, `*.iso`, and `*.xbe` are the user's own game data and are **never committed**.

## Why the game reported a dirty disc

The title's "there's a problem with the disc you're using, it may be dirty" screen also appears
when the disc is fine but the runtime mounted the wrong copy of the game. `assets/` must contain
`default.xbe` for the build (to hash and recompile it), so a development tree normally held only
that file plus the `.iso`, and `app::FindGame` preferred the extracted files over any image
without checking that the folder held anything else. It therefore mounted that folder as `D:`,
every game data file open failed, and the title drew its dirty-disc screen — with no log line
saying the chosen folder had the XBE and nothing else.

The rule now (`sdk/runtime/app/game_files.cpp`): a **complete** extracted folder — the XBE plus
at least one other regular file that is not a disc image or a host artefact (logs, settings,
shadercache, `hdd`, `CrashDumps`, splash) — still wins over any image; an **incomplete** folder
loses to a disc image carrying the right XBE; and if an incomplete folder is all there is it is
still used, but loudly. The new log lines (all `LOGR`, the level that survives a release
`NDEBUG` build — `LOGW` does not) are, verbatim in format:

- `game: <folder> is incomplete: it holds <n> game data file(s) besides <xbe> (disc images and host files not counted), so disc image <image> is being used instead`
- `game: using incomplete extracted folder <folder> anyway: it holds <n> game data file(s) besides <xbe> (disc images and host files not counted) and no disc image with this <xbe> was found`
- `game: the game's data files are missing from <folder>, so the title will report a disc error (dirty or damaged disc)`
- `game: fix: run python3 tools/xiso_extract.py extract <image> assets, or put the .iso in assets/ and leave it there`
- `game: <image> holds <n> file(s)` — logged for every image opened, so a truncated image is distinguishable from a complete one (this image holds 60).
