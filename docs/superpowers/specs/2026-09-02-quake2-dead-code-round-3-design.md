# Quake II — Dead-Code Cleanup Round 3 (Dead Data & Cosmetics) — Design

**Date:** 2026-09-02
**Status:** Draft (brainstorming; approach C — big-bang — chosen by user)
**Goal:** Remove the dead *data* objects and cosmetic leftovers that rounds 1–2
deliberately skipped, with **zero behavior change**. Rounds 1–2 removed dead
platform code, all `#if 0` blocks, and 24 dead *functions*; round 2's audit was
functions-only, so dead *data* remained. Round 3 is a full data audit + removal.

Companion specs (this work preserves them):
`2026-08-31-quake2-sdl3-apple-silicon-design.md` (port),
`2026-09-01-quake2-apple-silicon-cleanup-design.md` (round 1),
`2026-09-01-quake2-dead-code-round-2-design.md` (round 2).

---

## 1. Scope and constraints

- **In scope:** the 128 individually proven-dead data objects enumerated in §3,
  plus the round-2 cosmetic leftovers (§4). Big-bang: one branch, removals
  grouped into a few per-unit commits, gate + runtime smoke once at the end.
- **Out of scope (explicitly excluded):**
  - The **282 zero-call `qgl*` GL dispatch pointers** in `sdl/qgl_sdl.c` — the
    OpenGL loader table; low value, deliberate-complete by design. Never touch
    the 7 protected extension pointers (`qglLockArraysEXT`,
    `qglUnlockArraysEXT`, `qglPointParameterfEXT`, `qglPointParameterfvEXT`,
    `qglColorTableEXT`, `qglSelectTextureSGIS`, `qglMTexCoord2fSGIS`) — all
    have live call sites.
  - The **byte-swap cluster** in `game/q_shared.c` (`_BigShort`, `_LittleShort`,
    `_BigLong`, `_LittleLong`, `_BigFloat`, `_LittleFloat` :568-573, `bigendien`
    :564, `Swap_Init`, the Swap/NoSwap helpers, and the
    `BigShort/LittleShort/LittleLong/LittleFloat` wrappers :575-578). Dead in the
    game DLL only; the *same* `q_shared.c` compiles into the executable
    (`Swap_Init` called at `qcommon/common.c:1337`, wrappers ~80×) and ref_gl
    (`Swap_Init` at `ref_gl/gl_rmain.c:1501`), where it is alive. Removing it
    would break the exe and renderer without splitting the file.
  - **`RW_IN_Activate_fp`** (`sdl/vid_sdl.c:43`) — proven never called, but part
    of the `RW_IN_*` renderer-ABI contract that round 2 deliberately preserved,
    and `sdl/verify_load.c:32` still verifies the ref bundle exports
    `RW_IN_Activate`. Removal would require rewriting the dlsym validation
    condition at `sdl/vid_sdl.c:255` (a validation-behavior change). Excluded to
    honor the no-behavior-change rule and round-2 precedent.
  - Any behavioral change; gameplay/renderer/audio fixes; `baseq2/` contents.
- **Standing constraints (carried from rounds 1–2):**
  - **No behavior change** is the paramount rule. Every removal must be provably
    dead (defined, never read / never referenced).
  - Never `git add -A` / `git commit -a`; stage explicit paths only; `baseq2/`
    (user game data) never enters a commit.
  - Never push (unless the user explicitly asks).
  - The `make clean && make build && make verify-load` gate plus the
    `+map base1` runtime smoke must pass at the end.
  - Preserve `ada44e2`: `sdl/qgl_sdl.c` untouched; `R_Init()` in
    `ref_gl/gl_rmain.c` keeps `return true;`.

### Locked decisions
| Decision | Choice |
|---|---|
| Approach | **C — big-bang**: one branch, per-unit commit grouping, single gate at the end |
| Scope | All 128 individually proven-dead data objects + cosmetics (user approved "all", refined 129→128 after excluding `RW_IN_Activate_fp`) |
| Write-only `cvar_t*` pointers | **Drop the pointer variable + orphaned extern, KEEP the `Cvar_Get` registration** so the user-visible cvar persists (no behavior change). Applies to all 24 (§3). |
| Monster animation chains | Remove each `mmove_t` together with its only-consumer `mframe_t` array as one unit; shared end-functions untouched |
| `RW_IN_Activate_fp` | **Excluded** (renderer-ABI contract + validation entanglement) |
| qgl table / byte-swap cluster | **Excluded** (see Out of scope) |
| Gate | One final `make clean && build && verify-load` + `+map base1` runtime smoke |

---

## 2. Audit method (how "proven dead" was established)

`nm -gU` over `build/game/*.o`, `build/client/*.o`, `build/ref_gl/*.o`,
covering symbol types B/D/S/R **and C** (common symbols, which a plain BDSR
filter misses). Every candidate was then word-grepped through all sources of
its own link unit, and cross-object coupling was ruled out via `nm -u` over
every unit. Notes: `game/q_shared.c` is compiled into **all three** link units;
`game/m_flash.c` into both the game DLL and the executable.

---

## 3. Removal inventory (128 data objects)

### 3a. game/ — monster animation chains (32 `mmove_t` + 32 `mframe_t` = 64)
Each `mmove_t` is unreferenced (its only occurrence is its definition); each
`mframe_t` array has exactly two occurrences (definition + the dead move that
consumes it). Remove each move together with its frames array. The
end-function named in each initializer is shared with live moves — **do not
touch end-functions**, with the single exception below.

| Move (`mmove_t`) | Frames array | File |
|---|---|---|
| `berserk_move_attack_strike` :246 | `berserk_frames_attack_strike` :228 | `game/m_berserk.c` |
| `boss2_move_fidget` :213 | `boss2_frames_fidget` :180 | `game/m_boss2.c` |
| `jorg_move_start_walk` :191 | `jorg_frames_start_walk` :183 | `game/m_boss31.c` |
| `jorg_move_end_walk` :221 | `jorg_frames_end_walk` :212 | `game/m_boss31.c` |
| `makron_move_death3` :390 | `makron_frames_death3` :367 | `game/m_boss32.c` |
| `brain_move_defense` :201 | `brain_frames_defense` :189 | `game/m_brain.c` |
| `floater_move_activate` :238 | `floater_frames_activate` :205 | `game/m_float.c` |
| `floater_move_death` :344 | `floater_frames_death` :328 | `game/m_float.c` |
| `floater_move_pain3` :386 | `floater_frames_pain3` :371 | `game/m_float.c` |
| `flyer_move_start` :245 | `flyer_frames_start` :236 | `game/m_flyer.c` |
| `flyer_move_rollright` :272 | `flyer_frames_rollright` :260 | `game/m_flyer.c` |
| `flyer_move_rollleft` :286 | `flyer_frames_rollleft` :274 | `game/m_flyer.c` |
| `flyer_move_defense` :329 | `flyer_frames_defense` :320 | `game/m_flyer.c` |
| `flyer_move_bankright` :341 | `flyer_frames_bankright` :331 | `game/m_flyer.c` |
| `flyer_move_bankleft` :353 | `flyer_frames_bankleft` :343 | `game/m_flyer.c` |
| `gunner_move_runandshoot` :230 | `gunner_frames_runandshoot` :220 | `game/m_gunner.c` |
| `hover_move_stop1` :112 | `hover_frames_stop1` :100 | `game/m_hover.c` |
| `hover_move_stop2` :125 | `hover_frames_stop2` :114 | `game/m_hover.c` |
| `hover_move_takeoff` :160 | `hover_frames_takeoff` :127 | `game/m_hover.c` |
| `hover_move_land` :230 | `hover_frames_land` :226 | `game/m_hover.c` |
| `hover_move_forward` :270 | `hover_frames_forward` :232 | `game/m_hover.c` |
| `hover_move_backward` :395 | `hover_frames_backward` :368 | `game/m_hover.c` |
| `parasite_move_stop_run` :188 | `parasite_frames_stop_run` :179 | `game/m_parasite.c` |
| `parasite_move_stop_walk` :235 | `parasite_frames_stop_walk` :226 | `game/m_parasite.c` |
| `supertank_move_turn_right` :224 | `supertank_frames_turn_right` :203 | `game/m_supertank.c` |
| `supertank_move_turn_left` :247 | `supertank_frames_turn_left` :226 | `game/m_supertank.c` |
| `supertank_move_backward` :327 | `supertank_frames_backward` :306 | `game/m_supertank.c` |
| `supertank_move_attack4` :338 | `supertank_frames_attack4` :329 | `game/m_supertank.c` |
| `supertank_move_attack3` :370 | `supertank_frames_attack3` :340 | `game/m_supertank.c` |
| `tank_move_start_walk` :134 | `tank_frames_start_walk` :127 | `game/m_tank.c` |
| `tank_move_stop_walk` :165 | `tank_frames_stop_walk` :157 | `game/m_tank.c` |
| `tank_move_stop_run` :217 | `tank_frames_stop_run` :209 | `game/m_tank.c` |

**Special case — `floater_move_death` chain also removes a function.** Its
end-function `floater_dead` (`game/m_float.c` decl :53, def :593) is referenced
*only* by this dead move (verified: no other `monsterinfo` wiring). Removing
`floater_move_death` orphans `floater_dead`, so delete the function and its
forward declaration too. All other end-functions are shared with live moves and
stay.

### 3b. game/ — standalone orphan frames array (1)
- `makron_frames_walk` — `game/m_boss32.c:189`. Unreferenced, not transitive:
  the alive `makron_move_walk` (:202) uses `makron_frames_run` instead.

### 3c. game/ — write-only static sound indices (15)
Each appears exactly twice (the `static int` decl + one `gi.soundindex(...)`
assignment) and is never passed to `gi.sound`/`gi.positioned_sound`. Delete the
decl and the assignment line.

| Symbol | File | Decl | Assign |
|---|---|---|---|
| `sound_firegun` | `game/m_boss31.c` | :44 | :686 |
| `sound_death_hit` | `game/m_boss31.c` | :47 | :687 |
| `sound_tentacles_extend` | `game/m_brain.c` | :33 | :575 |
| `sound_idle1` | `game/m_brain.c` | :36 | :578 |
| `sound_idle2` | `game/m_brain.c` | :37 | :579 |
| `sound_missile_launch` | `game/m_chick.c` | :40 | :635 |
| `sound_melee_hit` | `game/m_chick.c` | :42 | :637 |
| `sound_fall_down` | `game/m_chick.c` | :46 | :641 |
| `sound_search` | `game/m_chick.c` | :53 | :648 |
| `sound_attack` | `game/m_flipper.c` | :33 | :373 |
| `sound_idle` | `game/m_flipper.c` | :37 | :374 |
| `sound_search` | `game/m_flipper.c` | :38 | :375 |
| `sound_gunshot` | `game/m_infantry.c` | :39 | :569 |
| `sound_search` | `game/m_infantry.c` | :44 | :575 |
| `sound_search` | `game/m_parasite.c` | :42 | :513 |

### 3d. game/ — write-only debug counters (2)
- `c_yes`, `c_no` — `game/m_move.c:35` (one shared decl line). Id-era
  `M_CheckBottom` counters; only `c_yes++` (:60, :95) and `c_no++` (:64), never
  read. Remove the decl and the three increment statements.

### 3e. game/q_shared.c — file-scope strays dead in all units (3)
- `int i;` :265 and `vec3_t corners[2];` :266 — common symbols `_i`/`_corners`;
  `nm -u` over all three units shows zero references. **Remove only lines
  265–266** — not the function-local `int i;` at :345/:411/:471/:791.
- `paged_total` :787 — written only by `paged_total += buffer[i];` :794 inside
  `Com_PageInMemory`, never read anywhere. Remove the variable and the `+=`
  line; **keep `Com_PageInMemory`** (alive in the exe unit).

### 3f. Executable link unit — 30 objects
**Unreferenced (5)** — declaration (or commented-out assignment) only:
- `cl_mod_plasmaexplo` — `client/cl_tent.c:109`
- `cl_sfx_plasexp` — `client/cl_tent.c:96` (only other mention is a commented-out registration at :145)
- `keys_cursor` — `client/menu.c:592`
- `snd_initialized` — `client/snd_dma.c:44`
- `cache_full_cycle` — `client/snd_mem.c:25`

**Write-only, plain (9)** — fully removed:
- `bitcounts[32]` — `client/cl_ents.c:46` (only `bitcounts[i]++` :73)
- `scr_centertime_start` — `client/cl_scrn.c:184` (write :205)
- `scr_erase_center` — `client/cl_scrn.c:187` (write :265)
- `cl_mod_parasite_tip` — `client/cl_tent.c:104` (RegisterModel :179, never used)
- `solidleaf` — `qcommon/cmodel.c:79` (**shares its declaration line with the
  LIVE `emptyleaf`** — `int emptyleaf, solidleaf;` becomes `int emptyleaf;`);
  remove the write at :316
- `numentitychars` — `qcommon/cmodel.c:94` (writes :532, :575, no reads)
- `listener_forward` — `client/snd_dma.c:50` (VectorCopy :1031) + orphaned
  extern `client/snd_loc.h:132`
- `listener_up` — `client/snd_dma.c:52` (VectorCopy :1033) + orphaned extern
  `client/snd_loc.h:134`
- `m_game_cursor` (static) — `client/menu.c:1787` (only write `= 1` at :1930)

**Write-only `cvar_t*` pointers (15)** — drop variable + extern, keep
`Cvar_Get` registration:
- `adr0`…`adr8` (9) — `client/cl_main.c:26-34` (Cvar_Get :1419-1427)
- `info_password` :77, `info_spectator` :78 — `client/cl_main.c`
  (CVAR_USERINFO registration side effect)
- `scr_showturtle` :50, `scr_printspeed` :52 — `client/cl_scrn.c`
- `vid_xpos` :18, `vid_ypos` :19 — `sdl/vid_sdl.c` (CVAR_ARCHIVE)

**Plus `cl_autoskins`** (`client/cl_main.c:43`, Cvar_Get :1442, orphaned extern
`client/client.h:258`) — same treatment as the 15 (drop variable + extern, keep
registration). Counted in the 30 above as part of the write-only cvar handling.

### 3g. ref_gl link unit — 13 objects
**Unreferenced (2):**
- `base_textureid` — `ref_gl/gl_image.c:25`
- `r_base_world_matrix` — `ref_gl/gl_rmain.c:62`

**Write-only, plain (3):**
- `scrap_uploads` — `ref_gl/gl_image.c:397` (only `scrap_uploads++` :401)
- `lightplane` — `ref_gl/gl_light.c:181` (only write :227; shadows removed)
- `c_sky` — `ref_gl/gl_warp.c:260` (only `c_sky++` :305)

**Write-only `cvar_t*` pointers (8)** — drop variable + orphaned extern in
`ref_gl/gl_local.h`, keep `Cvar_Get` registration:
- `gl_allow_software` `ref_gl/gl_rmain.c:84`; `gl_nosubimage` :83;
  `gl_ext_multitexture` :97; `gl_ext_compiled_vertex_array` :99;
  `gl_ext_swapinterval` :95; `gl_bitdepth` :102; `gl_playermip` :124;
  `gl_swapinterval` :126

**Total: 85 (game) + 30 (exe) + 13 (ref_gl) = 128 data objects.**

---

## 4. Cosmetic leftovers (round-2 deferrals, zero risk)
- Orphaned `// gl underwater warp` comment — `ref_gl/gl_model.c:734`
- Orphaned `** draw our stereo patterns` banner — `ref_gl/gl_rmain.c:1172`
- Orphaned `// walk2 is FUBAR` comment (+ dead `brain_move_walk2` reference) — `game/m_brain.c`
- Double blank line left by round-2 `s_primary` removal — `client/snd_dma.c` ~:80
- Commented-out calls naming round-2-removed functions:
  `// CL_Tracker_Explode (pos);` `client/cl_tent.c:1171`;
  `// DumpChunks ();` `client/snd_mem.c:275`
- Restore the blank line after `PM_trace` — `game/p_client.c`

---

## 5. Verification
- **Final gate** (once, after all removals): `make clean && make build &&
  make verify-load` — must exit 0.
- **Runtime smoke**: `./build/quake2 +set developer 1 +map base1` must reach
  `SpawnServer: base1` + full precache with no FATAL/ShutdownError (the
  validated round-2 substitute; the attract-loop smoke is environmentally
  blocked by CoreAudio -66681, proven not a regression).
- Because removals are grouped per-unit into separate commits, a failed final
  gate can be bisected by category.

## 6. Risks & mitigations
| Risk | Mitigation |
|---|---|
| A frames array is actually shared with a live move | Audit verified each has exactly 2 occurrences (def + dead move); plan re-greps each before deletion |
| `floater_dead` has hidden wiring | Verified: only decl + dead move + definition; removed with its chain |
| `solidleaf` removal disturbs live `emptyleaf` | Edit the shared declaration to `int emptyleaf;` only |
| A write-only cvar is read by name elsewhere | Audit word-grepped each; plan keeps the `Cvar_Get` registration regardless (no behavior change) |
| Big-bang regression hard to localize | Per-unit commit grouping enables category bisection |
| Gate misses a runtime-only issue | `+map base1` runtime smoke after the build gate |
