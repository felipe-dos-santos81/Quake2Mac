# Quake II Dead-Code Cleanup Round 3 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the 128 proven-dead data objects plus the round-2 cosmetic leftovers enumerated in the round-3 spec, on a single big-bang branch, with **zero behavior change**, gated once at the end by a clean build + verify-load + a `+map base1` runtime smoke.

**Architecture:** Approach C (big-bang). One branch `cleanup/dead-code-round-3`; deletions grouped into per-category/per-unit commits so a failed final gate can be bisected by category. Every deletion task ends with an incremental `make build` sanity check — a dead-symbol deletion keeps the build clean, so an undefined-reference or new warning after a deletion means a live symbol was hit and the task must stop. The formal gate (`make clean && make build && make verify-load` + `+map base1` smoke) runs once in the final task.

**Tech Stack:** GPL Quake II 3.21 C sources, SDL3 port (`sdl/`), Apple clang, GNU make 3.81, macOS arm64.

**Spec:** `docs/superpowers/specs/2026-09-02-quake2-dead-code-round-3-design.md` (read it — the plan argues from it and the spec carries the full removal inventory).

## Global Constraints

1. **No behavior change** — paramount. Only remove symbols proven dead. If an incremental build after a deletion reports an undefined reference (or a new warning), a live symbol was hit — STOP and report, do not improvise.
2. **Write-only `cvar_t*` pointers:** delete the pointer variable + any orphaned `extern`, but KEEP the `Cvar_Get(...)` call as a bare statement so the user-visible cvar stays registered. Never delete that `Cvar_Get` call.
3. **Monster chains:** remove each `mframe_t` array together with its consuming `mmove_t`. Do NOT touch the end-function named in each initializer, EXCEPT `floater_dead`, which is removed with the `floater_move_death` chain (verified no other references).
4. **Do NOT touch the excluded items:** the `qgl*` dispatch table in `sdl/qgl_sdl.c`; the byte-swap cluster in `game/q_shared.c` (`_BigShort/_LittleShort/_BigLong/_LittleLong/_BigFloat/_LittleFloat`, `bigendien`, `Swap_Init`, the Swap/NoSwap helpers, and the `BigShort/LittleShort/LittleLong/LittleFloat` wrappers); `RW_IN_Activate_fp` in `sdl/vid_sdl.c`; the 7 protected `qgl*EXT` pointers.
5. **Preserve ada44e2:** `sdl/qgl_sdl.c` untouched; `R_Init()` in `ref_gl/gl_rmain.c` keeps its `return true;`.
6. **Never** `git add -A` / `git commit -a`. Stage explicit paths only. `baseq2/` holds user game data and must never enter a commit.
7. **Never push.**
8. Locate symbols by name (`grep -rn`); the line numbers below are approximate anchors from plan time and shift as you delete.
9. If a symbol is not where expected, already gone, or has more references than the spec lists, STOP and report — do not guess.

---

### Task 1: Branch and baseline gate

**Files:** none (git state + build only)

**Interfaces:**
- Consumes: `main` at the committed round-3 spec
- Produces: branch `cleanup/dead-code-round-3` with a verified-green baseline build; every later task starts here

- [ ] **Step 1: Create the branch and verify state**

```bash
cd /Users/felipe.dos.santos/code/theirs/Quake-2
git checkout -b cleanup/dead-code-round-3 main
git status --short          # expect: empty (or only untracked build/ + baseq2/ play data)
```

- [ ] **Step 2: Run the baseline gate**

```bash
make clean && make build && make verify-load
```

Expected: exit 0; `verify-load` prints its dlopen/entry-point checks. The benign linker warning "reducing alignment of section __DATA,__common" is pre-existing — ignore it.

If the gate fails here, STOP and report — the baseline is broken.

---

### Task 2: Cosmetic leftovers (round-2 deferrals, zero risk)

**Files:** `ref_gl/gl_model.c`, `ref_gl/gl_rmain.c`, `game/m_brain.c`, `client/snd_dma.c`, `client/cl_tent.c`, `client/snd_mem.c`, `game/p_client.c`

**Interfaces:**
- Consumes: Task 1's green baseline
- Produces: cosmetic deletions/spacing fixes; build still clean

- [ ] **Step 1: Remove the orphaned comment `// gl underwater warp`**

`ref_gl/gl_model.c` ~:734. Delete the single comment line (it introduces nothing; the block it annotated was removed in round 1). Leave the surrounding code and closing brace intact.

- [ ] **Step 2: Remove the orphaned stereo banner**

`ref_gl/gl_rmain.c` ~:1170-1172. Delete the comment block:
```c
	/*
	** draw our stereo patterns
	*/
```
It sits immediately before `GL_InitImages ();` in `R_Init`. Remove only the 3-line comment; leave `GL_InitImages ();` and everything else.

- [ ] **Step 3: Remove the orphaned walk2 comment in m_brain.c**

`game/m_brain.c`. Locate the `// walk2 is FUBAR` comment (it references the round-2-removed `brain_move_walk2`). Delete the comment line(s) that reference the removed symbol. Do not touch any code.

- [ ] **Step 4: Collapse the double blank line in snd_dma.c**

`client/snd_dma.c` ~:80. Round 2's `s_primary` removal left two consecutive blank lines between the `cvar_t` block (`s_mixahead`) and `int s_rawend;`. Collapse to a single blank line.

- [ ] **Step 5: Remove the commented-out `CL_Tracker_Explode` call**

`client/cl_tent.c` ~:1171. Delete the line:
```c
//		CL_Tracker_Explode (pos);
```
It names a function removed in round 2. Leave the surrounding `CL_ColorExplosionParticles(...)` and `S_StartSound(...)` lines.

- [ ] **Step 6: Remove the commented-out `DumpChunks` call**

`client/snd_mem.c` ~:275. Delete the line:
```c
// DumpChunks ();
```
It sits between `iff_data = data_p + 12;` and `FindChunk("fmt ");`. Leave those two lines.

- [ ] **Step 7: Restore the blank line after `PM_trace` in p_client.c**

`game/p_client.c`. Round 2 removed a `#if 0` block and left `PM_trace`'s closing brace immediately followed by the `ClientThink` banner with no separating blank line. Insert one blank line between `PM_trace`'s closing brace and the `/*` banner that follows. (Cosmetic only; if the blank line is already present, skip.)

- [ ] **Step 8: Incremental build sanity check**

```bash
make build 2>&1 | tail -3
```
Expected: clean build, no new warnings.

- [ ] **Step 9: Commit**

```bash
git add ref_gl/gl_model.c ref_gl/gl_rmain.c game/m_brain.c client/snd_dma.c client/cl_tent.c client/snd_mem.c game/p_client.c
git commit -m "chore: round-3 cosmetic leftovers (orphaned comments, spacing)"
```

---

### Task 3: game/ self-contained dead data (sound indices, counters, orphan frames, q_shared strays)

**Files:** `game/m_boss31.c`, `game/m_brain.c`, `game/m_chick.c`, `game/m_flipper.c`, `game/m_infantry.c`, `game/m_parasite.c`, `game/m_move.c`, `game/m_boss32.c`, `game/q_shared.c`

**Interfaces:**
- Consumes: Task 2's commit
- Produces: 21 self-contained game/ data objects removed; build clean

For each write-only sound index, delete BOTH the `static int` declaration AND the single `= gi.soundindex(...)` assignment line. These are never read, so both lines go.

- [ ] **Step 1: Remove 15 write-only sound indices**

| Symbol | File | Decl | Assign |
|---|---|---|---|
| `sound_firegun` | `game/m_boss31.c` | ~:44 | ~:686 |
| `sound_death_hit` | `game/m_boss31.c` | ~:47 | ~:687 |
| `sound_tentacles_extend` | `game/m_brain.c` | ~:33 | ~:575 |
| `sound_idle1` | `game/m_brain.c` | ~:36 | ~:578 |
| `sound_idle2` | `game/m_brain.c` | ~:37 | ~:579 |
| `sound_missile_launch` | `game/m_chick.c` | ~:40 | ~:635 |
| `sound_melee_hit` | `game/m_chick.c` | ~:42 | ~:637 |
| `sound_fall_down` | `game/m_chick.c` | ~:46 | ~:641 |
| `sound_search` | `game/m_chick.c` | ~:53 | ~:648 |
| `sound_attack` | `game/m_flipper.c` | ~:33 | ~:373 |
| `sound_idle` | `game/m_flipper.c` | ~:37 | ~:374 |
| `sound_search` | `game/m_flipper.c` | ~:38 | ~:375 |
| `sound_gunshot` | `game/m_infantry.c` | ~:39 | ~:569 |
| `sound_search` | `game/m_infantry.c` | ~:44 | ~:575 |
| `sound_search` | `game/m_parasite.c` | ~:42 | ~:513 |

For each: locate the decl and the assignment by name, confirm the symbol has exactly these two occurrences in its file (`grep -n`), then delete both lines.

- [ ] **Step 2: Remove the `c_yes`/`c_no` debug counters**

`game/m_move.c`. The decl is one shared line ~:35 (`static int c_yes, c_no;` — verify exact form). Delete the decl line and the three increment statements: `c_yes++` (~:60, ~:95) and `c_no++` (~:64). Confirm no reads exist first (`grep -n "c_yes\|c_no" game/m_move.c` should show only decl + increments).

- [ ] **Step 3: Remove the orphan `makron_frames_walk`**

`game/m_boss32.c` ~:189. Delete the entire `mframe_t makron_frames_walk [] = { ... };` array definition. Confirm it is unreferenced (`grep -rn "makron_frames_walk" game/` → only the definition). Note: the alive `makron_move_walk` uses `makron_frames_run`, not this — do not touch `makron_move_walk` or `makron_frames_run`.

- [ ] **Step 4: Remove the `q_shared.c` file-scope strays**

`game/q_shared.c`. Delete ONLY the two file-scope lines ~:265-266:
```c
int		i;
vec3_t	corners[2];
```
(exact indentation may differ — locate them between `anglemod()` and `BoxOnPlaneSide()`). Do NOT touch the function-local `int i;` at ~:345/:411/:471/:791. Then remove `paged_total`: delete the file-scope decl ~:787 and the `paged_total += buffer[i];` line ~:794 inside `Com_PageInMemory`; KEEP the `Com_PageInMemory` function itself. Confirm `paged_total` has no other occurrences.

- [ ] **Step 5: Incremental build sanity check**

```bash
make build 2>&1 | tail -3
```
Expected: clean build, no new warnings. (A warning like "unused variable" should NOT appear — these were already dead. If a warning or error appears, STOP and report.)

- [ ] **Step 6: Commit**

```bash
git add game/m_boss31.c game/m_brain.c game/m_chick.c game/m_flipper.c game/m_infantry.c game/m_parasite.c game/m_move.c game/m_boss32.c game/q_shared.c
git commit -m "chore: remove game/ self-contained dead data (sound indices, counters, orphan frames, q_shared strays)"
```

---

### Task 4: game/ monster chains — group A (berserk, boss2, boss31, boss32, brain, float)

**Files:** `game/m_berserk.c`, `game/m_boss2.c`, `game/m_boss31.c`, `game/m_boss32.c`, `game/m_brain.c`, `game/m_float.c`

**Interfaces:**
- Consumes: Task 3's commit
- Produces: 9 chains removed (incl. the `floater_dead` function); build clean

For EACH chain: delete the `mframe_t <name>_frames_*` array definition AND the `mmove_t <name>_move_*` struct that consumes it. Before deleting, confirm the frames array is referenced ONLY by that dead move (`grep -rn "<frames_name>" game/` → exactly 2 hits: definition + the move). Do NOT touch the end-function in each initializer.

- [ ] **Step 1: Remove the 9 chains**

| Move (`mmove_t`, delete) | Frames array (delete) | End-fn (KEEP) | File |
|---|---|---|---|
| `berserk_move_attack_strike` ~:246 | `berserk_frames_attack_strike` ~:228 | `berserk_run` | `game/m_berserk.c` |
| `boss2_move_fidget` ~:213 | `boss2_frames_fidget` ~:180 | — | `game/m_boss2.c` |
| `jorg_move_start_walk` ~:191 | `jorg_frames_start_walk` ~:183 | — | `game/m_boss31.c` |
| `jorg_move_end_walk` ~:221 | `jorg_frames_end_walk` ~:212 | — | `game/m_boss31.c` |
| `makron_move_death3` ~:390 | `makron_frames_death3` ~:367 | — | `game/m_boss32.c` |
| `brain_move_defense` ~:201 | `brain_frames_defense` ~:189 | — | `game/m_brain.c` |
| `floater_move_activate` ~:238 | `floater_frames_activate` ~:205 | — | `game/m_float.c` |
| `floater_move_death` ~:344 | `floater_frames_death` ~:328 | `floater_dead` (REMOVE, see Step 2) | `game/m_float.c` |
| `floater_move_pain3` ~:386 | `floater_frames_pain3` ~:371 | — | `game/m_float.c` |

- [ ] **Step 2: Remove the orphaned `floater_dead` function**

`game/m_float.c`. After removing `floater_move_death`, the end-function `floater_dead` is unreferenced. Confirm (`grep -rn "floater_dead" game/` → only the forward decl ~:53 and the definition ~:593). Delete both the forward declaration (~:53) and the function definition (~:593 to its closing brace). Do NOT remove any other function.

- [ ] **Step 3: Verify no leftover references**

```bash
grep -rn "berserk_move_attack_strike\|boss2_move_fidget\|jorg_move_start_walk\|jorg_move_end_walk\|makron_move_death3\|brain_move_defense\|floater_move_activate\|floater_move_death\|floater_move_pain3\|floater_dead" game/
```
Expected: no matches (all removed).

- [ ] **Step 4: Incremental build sanity check**

```bash
make build 2>&1 | tail -3
```
Expected: clean build, no new warnings.

- [ ] **Step 5: Commit**

```bash
git add game/m_berserk.c game/m_boss2.c game/m_boss31.c game/m_boss32.c game/m_brain.c game/m_float.c
git commit -m "chore: remove game/ dead monster chains group A (berserk/boss2/boss31/boss32/brain/float)"
```

---

### Task 5: game/ monster chains — group B (flyer, gunner, hover)

**Files:** `game/m_flyer.c`, `game/m_gunner.c`, `game/m_hover.c`

**Interfaces:**
- Consumes: Task 4's commit
- Produces: 13 chains removed; build clean

Same rule as Task 4: delete each `mframe_t` array + its consuming `mmove_t`; confirm the frames array has exactly 2 occurrences first; do NOT touch end-functions.

- [ ] **Step 1: Remove the 13 chains**

| Move (`mmove_t`, delete) | Frames array (delete) | File |
|---|---|---|
| `flyer_move_start` ~:245 | `flyer_frames_start` ~:236 | `game/m_flyer.c` |
| `flyer_move_rollright` ~:272 | `flyer_frames_rollright` ~:260 | `game/m_flyer.c` |
| `flyer_move_rollleft` ~:286 | `flyer_frames_rollleft` ~:274 | `game/m_flyer.c` |
| `flyer_move_defense` ~:329 | `flyer_frames_defense` ~:320 | `game/m_flyer.c` |
| `flyer_move_bankright` ~:341 | `flyer_frames_bankright` ~:331 | `game/m_flyer.c` |
| `flyer_move_bankleft` ~:353 | `flyer_frames_bankleft` ~:343 | `game/m_flyer.c` |
| `gunner_move_runandshoot` ~:230 | `gunner_frames_runandshoot` ~:220 | `game/m_gunner.c` |
| `hover_move_stop1` ~:112 | `hover_frames_stop1` ~:100 | `game/m_hover.c` |
| `hover_move_stop2` ~:125 | `hover_frames_stop2` ~:114 | `game/m_hover.c` |
| `hover_move_takeoff` ~:160 | `hover_frames_takeoff` ~:127 | `game/m_hover.c` |
| `hover_move_land` ~:230 | `hover_frames_land` ~:226 | `game/m_hover.c` |
| `hover_move_forward` ~:270 | `hover_frames_forward` ~:232 | `game/m_hover.c` |
| `hover_move_backward` ~:395 | `hover_frames_backward` ~:368 | `game/m_hover.c` |

- [ ] **Step 2: Verify no leftover references**

```bash
grep -rn "flyer_move_start\|flyer_move_roll\|flyer_move_defense\|flyer_move_bank\|gunner_move_runandshoot\|hover_move_stop\|hover_move_takeoff\|hover_move_land\|hover_move_forward\|hover_move_backward" game/
```
Expected: no matches. (Note: `flyer_move_start_melee` is ALIVE and must remain — grep for the exact dead symbols above, not bare prefixes.)

- [ ] **Step 3: Incremental build sanity check**

```bash
make build 2>&1 | tail -3
```
Expected: clean build, no new warnings.

- [ ] **Step 4: Commit**

```bash
git add game/m_flyer.c game/m_gunner.c game/m_hover.c
git commit -m "chore: remove game/ dead monster chains group B (flyer/gunner/hover)"
```

---

### Task 6: game/ monster chains — group C (parasite, supertank, tank)

**Files:** `game/m_parasite.c`, `game/m_supertank.c`, `game/m_tank.c`

**Interfaces:**
- Consumes: Task 5's commit
- Produces: 10 chains removed; build clean

Same rule: delete each `mframe_t` array + its consuming `mmove_t`; confirm the frames array has exactly 2 occurrences first; do NOT touch end-functions.

- [ ] **Step 1: Remove the 10 chains**

| Move (`mmove_t`, delete) | Frames array (delete) | File |
|---|---|---|
| `parasite_move_stop_run` ~:188 | `parasite_frames_stop_run` ~:179 | `game/m_parasite.c` |
| `parasite_move_stop_walk` ~:235 | `parasite_frames_stop_walk` ~:226 | `game/m_parasite.c` |
| `supertank_move_turn_right` ~:224 | `supertank_frames_turn_right` ~:203 | `game/m_supertank.c` |
| `supertank_move_turn_left` ~:247 | `supertank_frames_turn_left` ~:226 | `game/m_supertank.c` |
| `supertank_move_backward` ~:327 | `supertank_frames_backward` ~:306 | `game/m_supertank.c` |
| `supertank_move_attack4` ~:338 | `supertank_frames_attack4` ~:329 | `game/m_supertank.c` |
| `supertank_move_attack3` ~:370 | `supertank_frames_attack3` ~:340 | `game/m_supertank.c` |
| `tank_move_start_walk` ~:134 | `tank_frames_start_walk` ~:127 | `game/m_tank.c` |
| `tank_move_stop_walk` ~:165 | `tank_frames_stop_walk` ~:157 | `game/m_tank.c` |
| `tank_move_stop_run` ~:217 | `tank_frames_stop_run` ~:209 | `game/m_tank.c` |

- [ ] **Step 2: Verify no leftover references**

```bash
grep -rn "parasite_move_stop\|supertank_move_turn\|supertank_move_backward\|supertank_move_attack3\|supertank_move_attack4\|tank_move_start_walk\|tank_move_stop_walk\|tank_move_stop_run" game/
```
Expected: no matches. (Alive moves like `tank_walk`, `supertank_run`, `parasite_*` live variants must remain — grep only the exact dead symbols above.)

- [ ] **Step 3: Incremental build sanity check**

```bash
make build 2>&1 | tail -3
```
Expected: clean build, no new warnings.

- [ ] **Step 4: Commit**

```bash
git add game/m_parasite.c game/m_supertank.c game/m_tank.c
git commit -m "chore: remove game/ dead monster chains group C (parasite/supertank/tank)"
```

---

### Task 7: exe link unit — unreferenced + write-only plain data (14)

**Files:** `client/cl_tent.c`, `client/menu.c`, `client/snd_dma.c`, `client/snd_mem.c`, `client/cl_ents.c`, `client/cl_scrn.c`, `qcommon/cmodel.c`, `client/snd_loc.h`

**Interfaces:**
- Consumes: Task 6's commit
- Produces: 14 exe-unit data objects removed; build clean

- [ ] **Step 1: Remove 5 unreferenced objects (declaration only)**

| Symbol | File | Notes |
|---|---|---|
| `cl_mod_plasmaexplo` | `client/cl_tent.c` ~:109 | decl only |
| `cl_sfx_plasexp` | `client/cl_tent.c` ~:96 | decl only; a commented-out registration at ~:145 may also reference it — remove that comment line too if it exists |
| `keys_cursor` | `client/menu.c` ~:592 | decl only |
| `snd_initialized` | `client/snd_dma.c` ~:44 | decl only |
| `cache_full_cycle` | `client/snd_mem.c` ~:25 | decl only |

For each: confirm it has no other occurrences (`grep -rn`), then delete the declaration line.

- [ ] **Step 2: Remove 9 write-only plain objects (decl + all writes)**

| Symbol | File | Decl | Writes to remove |
|---|---|---|---|
| `bitcounts[32]` | `client/cl_ents.c` | ~:46 | `bitcounts[i]++` ~:73 |
| `scr_centertime_start` | `client/cl_scrn.c` | ~:184 | write ~:205 |
| `scr_erase_center` | `client/cl_scrn.c` | ~:187 | write ~:265 |
| `cl_mod_parasite_tip` | `client/cl_tent.c` | ~:104 | `RegisterModel` assignment ~:179 |
| `solidleaf` | `qcommon/cmodel.c` | ~:79 | write ~:316 |
| `numentitychars` | `qcommon/cmodel.c` | ~:94 | writes ~:532, ~:575 |
| `listener_forward` | `client/snd_dma.c` | ~:50 | `VectorCopy` ~:1031; also remove orphaned extern `client/snd_loc.h` ~:132 |
| `listener_up` | `client/snd_dma.c` | ~:52 | `VectorCopy` ~:1033; also remove orphaned extern `client/snd_loc.h` ~:134 |
| `m_game_cursor` (static) | `client/menu.c` | ~:1787 | write `= 1` ~:1930 |

**`solidleaf` special case:** it shares its declaration line with the LIVE `emptyleaf` (`int emptyleaf, solidleaf;`). Edit the line to `int	emptyleaf;` (drop `solidleaf` only). Remove the `solidleaf` write at ~:316. Do NOT touch `emptyleaf`.

- [ ] **Step 3: Verify no leftover references**

```bash
grep -rn "cl_mod_plasmaexplo\|cl_sfx_plasexp\|keys_cursor\|snd_initialized\|cache_full_cycle\|bitcounts\|scr_centertime_start\|scr_erase_center\|cl_mod_parasite_tip\|solidleaf\|numentitychars\|listener_forward\|listener_up\|m_game_cursor" client/ qcommon/ sdl/
```
Expected: no matches. (`emptyleaf` must still be present and used.)

- [ ] **Step 4: Incremental build sanity check**

```bash
make build 2>&1 | tail -3
```
Expected: clean build, no new warnings.

- [ ] **Step 5: Commit**

```bash
git add client/cl_tent.c client/menu.c client/snd_dma.c client/snd_mem.c client/cl_ents.c client/cl_scrn.c qcommon/cmodel.c client/snd_loc.h
git commit -m "chore: remove exe-unit unreferenced + write-only dead data"
```

---

### Task 8: exe + ref_gl write-only cvar_t* pointers (drop variable, keep registration) (24)

**Files:** `client/cl_main.c`, `client/cl_scrn.c`, `sdl/vid_sdl.c`, `client/client.h`, `ref_gl/gl_rmain.c`, `ref_gl/gl_local.h`

**Interfaces:**
- Consumes: Task 7's commit
- Produces: 24 dead cvar pointer variables + orphaned externs removed; `Cvar_Get` registrations preserved; build clean

**Rule for every item here:** delete the `cvar_t *NAME;` pointer declaration and any orphaned `extern cvar_t *NAME;`, and change the assignment `NAME = Cvar_Get(...)` to a bare `Cvar_Get(...);` (discard the return). The cvar stays registered. NEVER delete the `Cvar_Get` call.

- [ ] **Step 1: exe-unit cvar pointers (16)**

| Pointer var (delete) | File | Cvar_Get call (KEEP as bare call) | Orphaned extern to remove |
|---|---|---|---|
| `adr0`…`adr8` (9) | `client/cl_main.c` ~:26-34 | ~:1419-1427 | (check `client/client.h`) |
| `info_password` | `client/cl_main.c` ~:77 | nearby | (check `client/client.h`) |
| `info_spectator` | `client/cl_main.c` ~:78 | nearby | (check `client/client.h`) |
| `cl_autoskins` | `client/cl_main.c` ~:43 | ~:1442 | `client/client.h` ~:258 |
| `scr_showturtle` | `client/cl_scrn.c` ~:50 | nearby | (check for extern) |
| `scr_printspeed` | `client/cl_scrn.c` ~:52 | nearby | (check for extern) |
| `vid_xpos` | `sdl/vid_sdl.c` ~:18 | nearby | (check for extern) |
| `vid_ypos` | `sdl/vid_sdl.c` ~:19 | nearby | (check for extern) |

For each: `grep -rn NAME client/ sdl/` to find decl + extern + assignment. Delete decl + extern; convert the assignment to a bare `Cvar_Get(...);`. Verify the pointer is never dereferenced (no `NAME->` anywhere) before deleting.

- [ ] **Step 2: ref_gl cvar pointers (8)**

| Pointer var (delete) | File | Orphaned extern |
|---|---|---|
| `gl_allow_software` | `ref_gl/gl_rmain.c` ~:84 | `ref_gl/gl_local.h` |
| `gl_nosubimage` | `ref_gl/gl_rmain.c` ~:83 | `ref_gl/gl_local.h` |
| `gl_ext_multitexture` | `ref_gl/gl_rmain.c` ~:97 | `ref_gl/gl_local.h` |
| `gl_ext_compiled_vertex_array` | `ref_gl/gl_rmain.c` ~:99 | `ref_gl/gl_local.h` |
| `gl_ext_swapinterval` | `ref_gl/gl_rmain.c` ~:95 | `ref_gl/gl_local.h` |
| `gl_bitdepth` | `ref_gl/gl_rmain.c` ~:102 | `ref_gl/gl_local.h` |
| `gl_playermip` | `ref_gl/gl_rmain.c` ~:124 | `ref_gl/gl_local.h` |
| `gl_swapinterval` | `ref_gl/gl_rmain.c` ~:126 | `ref_gl/gl_local.h` |

For each: `grep -rn NAME ref_gl/` to find decl + extern + assignment. Delete decl + extern; convert the assignment to a bare `Cvar_Get(...);`. Verify the pointer is never dereferenced (no `NAME->` anywhere) before deleting.

- [ ] **Step 3: Verify no leftover dereferences**

```bash
grep -rnE "(adr[0-9]|info_password|info_spectator|cl_autoskins|scr_showturtle|scr_printspeed|vid_xpos|vid_ypos|gl_allow_software|gl_nosubimage|gl_ext_multitexture|gl_ext_compiled_vertex_array|gl_ext_swapinterval|gl_bitdepth|gl_playermip|gl_swapinterval)->" client/ sdl/ ref_gl/
```
Expected: no matches (no dereferences remain). The bare `Cvar_Get(...)` calls should still be present (grep for `Cvar_Get ("adr0"` etc. to confirm registration survives).

- [ ] **Step 4: Incremental build sanity check**

```bash
make build 2>&1 | tail -3
```
Expected: clean build, no new warnings.

- [ ] **Step 5: Commit**

```bash
git add client/cl_main.c client/cl_scrn.c sdl/vid_sdl.c client/client.h ref_gl/gl_rmain.c ref_gl/gl_local.h
git commit -m "chore: drop dead cvar pointer variables (keep Cvar_Get registrations)"
```

---

### Task 9: ref_gl unreferenced + write-only plain data (5)

**Files:** `ref_gl/gl_image.c`, `ref_gl/gl_rmain.c`, `ref_gl/gl_light.c`, `ref_gl/gl_warp.c`

**Interfaces:**
- Consumes: Task 8's commit
- Produces: 5 ref_gl data objects removed; build clean

- [ ] **Step 1: Remove 2 unreferenced objects**

| Symbol | File | Notes |
|---|---|---|
| `base_textureid` | `ref_gl/gl_image.c` ~:25 | decl only; confirm no other occurrences |
| `r_base_world_matrix` | `ref_gl/gl_rmain.c` ~:62 | decl only; confirm no other occurrences |

- [ ] **Step 2: Remove 3 write-only plain objects (decl + all writes)**

| Symbol | File | Decl | Writes to remove |
|---|---|---|---|
| `scrap_uploads` | `ref_gl/gl_image.c` | ~:397 | `scrap_uploads++` ~:401 |
| `lightplane` | `ref_gl/gl_light.c` | ~:181 | write ~:227 |
| `c_sky` | `ref_gl/gl_warp.c` | ~:260 | `c_sky++` ~:305 |

For each: confirm decl + write(s) are the only occurrences (`grep -rn`), then delete them.

- [ ] **Step 3: Verify no leftover references**

```bash
grep -rn "base_textureid\|r_base_world_matrix\|scrap_uploads\|lightplane\|c_sky" ref_gl/
```
Expected: no matches.

- [ ] **Step 4: Incremental build sanity check**

```bash
make build 2>&1 | tail -3
```
Expected: clean build, no new warnings.

- [ ] **Step 5: Commit**

```bash
git add ref_gl/gl_image.c ref_gl/gl_rmain.c ref_gl/gl_light.c ref_gl/gl_warp.c
git commit -m "chore: remove ref_gl unreferenced + write-only dead data"
```

---

### Task 10: Final gate + runtime smoke

**Files:** none (verification only)

**Interfaces:**
- Consumes: Tasks 2–9 commits (all 128 objects + cosmetics removed)
- Produces: a verified-green branch ready for the user-approved merge

- [ ] **Step 1: Full clean gate**

```bash
make clean && make build && make verify-load
```
Expected: exit 0; `verify-load: OK`. Only the pre-existing benign linker warning allowed.

- [ ] **Step 2: Runtime smoke**

```bash
./build/quake2 +set developer 1 +map base1 > /tmp/q2-r3-smoke.log 2>&1 & P=$!
sleep 40; kill $P 2>/dev/null
grep -q "SpawnServer: base1" /tmp/q2-r3-smoke.log && echo "PASS: SpawnServer base1" || echo "FAIL: no SpawnServer"
grep -q "client_connect" /tmp/q2-r3-smoke.log && echo "PASS: client_connect" || echo "FAIL: no connect"
grep -qE "maps/base1.bsp" /tmp/q2-r3-smoke.log && echo "PASS: base1.bsp precache" || echo "FAIL: no bsp"
grep -qE "FATAL|ShutdownError" /tmp/q2-r3-smoke.log && echo "FAIL: fatal present" || echo "PASS: no fatal"
```
Expected: all four PASS.

- [ ] **Step 3: Confirm no excluded item was touched**

```bash
grep -c "qglAccum\|qglLockArraysEXT" sdl/qgl_sdl.c          # qgl table intact (count > 0)
grep -n "RW_IN_Activate_fp" sdl/vid_sdl.c                    # still present (3 lines)
grep -n "_LittleShort\|Swap_Init" game/q_shared.c | head      # byte-swap cluster intact
grep -n "return true;" ref_gl/gl_rmain.c | tail -1            # R_Init still returns true
```
Expected: qgl table intact; `RW_IN_Activate_fp` still present; byte-swap cluster intact; `R_Init` ends `return true;`.

- [ ] **Step 4: Confirm clean staging + branch state**

```bash
git status --short | grep -v "^??" || echo "no tracked changes pending"
git log --oneline main..HEAD
```
Expected: no pending tracked changes; the commit list shows the round-3 commits (Tasks 2–9). **Do NOT merge or push** — that is the controller's step after user sign-off.

---

## After the plan

The controller (not a task subagent) performs the merge once the user approves:
`git checkout main && git merge --no-ff cleanup/dead-code-round-3`, then push
only if the user explicitly asks.
