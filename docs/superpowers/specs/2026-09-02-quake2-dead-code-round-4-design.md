# Quake II — Dead-Code Cleanup Round 4 (Deferred Micro-Pass) — Design

**Date:** 2026-09-02
**Status:** Approved (brainstorming done inline; direct pass + gate chosen by user)
**Goal:** Remove the small cleanup leftovers that rounds 1–3 deliberately
deferred, with **zero behavior change**. Rounds 1–3 removed dead platform
code, `#if 0` blocks, dead functions, and 128 dead data objects; round 4 is
the micro-pass over the residue those removals left behind.

Companion specs:
`2026-09-02-quake2-dead-code-round-3-design.md` (round 3 — source of all
deferred candidates), plus the round-1/round-2/port specs listed there.

---

## 1. Scope and constraints

- **In scope (4 groups, all verified on main 17047e8):**
  - **A — 73 dead `FRAME_*` definitions** in 16 monster headers (§3).
  - **B — 2 whitespace residues** left by round-3 comment/banner removals (§4).
  - **C — 1 redundant forward declaration** (`tank_walk`, §5).
  - **D — `S_Update` unused parameter removal** (`forward`, `up`, §6).
- **Out of scope (explicitly excluded):**
  - The **~4,800 id-era dead `FRAME_*` definitions** that were already
    unreferenced in the first commit (headers enumerate every model frame;
    code only names animation endpoints). Too large a deviation from the
    ModelGen-generated files for this round.
  - `FRAME_stand401` / `FRAME_stand452` — phantom names: referenced only
    inside the `#if 0` soldier stand4 block removed by the round-1/2 sweep;
    defined nowhere, nothing to remove.
  - All round-3 standing exclusions (qgl table, byte-swap cluster,
    `RW_IN_Activate_fp`, `baseq2/`).
- **Standing constraints (carried from rounds 1–3):**
  - **No behavior change** is the paramount rule.
  - Never `git add -A` / `git commit -a`; stage explicit paths only.
  - Gate `make clean && make build && make verify-load` + `+map base1`
    runtime smoke at the end.

### Locked decisions
| Decision | Choice |
|---|---|
| FRAME_* scope | **48 true orphans + 25 shared-name dead definitions = 73** (user chose; full id-era backlog excluded) |
| S_Update params | **Include** — drop unused `forward`/`up` (declaration + definition + 1 call site) |
| Execution | **Direct pass + gate** (no SDD subagents — change is ~77 line removals + one signature) |
| After gate | **Merge `--no-ff` into main, push, delete branch** |

---

## 2. Audit method

For every `FRAME_*` symbol defined in `game/m_*.h`: word-match
(`grep -wF`) references in the `.c` files that include that header (each
monster header/source pair is its own translation unit; several headers share
constant *names* with different values — deadness is per-definition, not
per-name). A definition is removed only if:
1. it has **zero references** in its includers at HEAD, and
2. it (or a same-name definition in a paired includer) was referenced at the
   first commit `7fd6505` — i.e. it is residue of *our* removals — or it is a
   same-name definition in another header whose paired `.c` never referenced
   it (dead since id-era, approved by the user for this round).

All removals are independent `#define NAME <value>` lines with **explicit
values**, so removing any of them cannot shift any other constant's value.

## 3. Removal group A — 73 `FRAME_*` definitions (16 headers)

### A1. True orphans — 48 (sole consumer deleted by rounds 1–3)
| Header | Constants | Deleted consumer (round) |
|---|---|---|
| `game/m_berserk.h` | `att_c21`, `att_c34` | `berserk_move_attack_strike` (R3) |
| `game/m_boss31.h` | `walk25` | `jorg_move_end_walk` (R3) |
| `game/m_boss32.h` | `death320` | `makron_move_death3` (R3) |
| `game/m_brain.h` | `defens01`, `defens08` | `brain_move_defense` (R3) |
| `game/m_brain.h` | `walk201`, `walk220`, `walk240` | `brain_move_walk2` + `nextframe` write (R2) |
| `game/m_float.h` | `actvat01`, `actvat31` | `floater_move_activate` (R3) |
| `game/m_float.h` | `pain312` | `floater_move_pain3` (R3) |
| `game/m_flyer.h` | `start01/06`, `rollf01/09`, `rollr01/09`, `defens01/06`, `bankr01/07`, `bankl01/07` | 6 flyer chains (R3) |
| `game/m_gunner.h` | `runs06` | `gunner_move_runandshoot` (R3) |
| `game/m_hover.h` | `stop101/109`, `stop201/208`, `takeof01/30`, `land01`, `backwd01/24` | 6 hover chains (R3) |
| `game/m_parasite.h` | `run10`, `run15` | `parasite_move_stop_run`/`stop_walk` (R3) |
| `game/m_supertank.h` | `right_1/18`, `left_1/18`, `backwd_1/18`, `attak4_1/6`, `attak3_1/27` | 5 supertank chains (R3) |
| `game/m_tank.h` | `walk21`, `walk25` | `tank_move_stop_walk` (R3) |

### A2. Shared-name definitions — 25 (same names, dead in their own unit since the first commit)
`m_actor.h`: `run10`, `runs06` · `m_boss31.h`: `pain312`, `walk21` ·
`m_boss32.h`: `attak505`, `attak508`, `pain312`, `walk201`, `walk21`,
`walk25` · `m_brain.h`: `defens06` · `m_chick.h`: `pain312`, `walk21`,
`walk25` · `m_gunner.h`: `walk21` · `m_insane.h`: `walk21`, `walk25` ·
`m_mutant.h`: `walk21` · `m_soldier.h`: `death320`, `pain312`, `run10`,
`runs06`, `walk201`, `walk220` · `m_tank.h`: `pain312`

Headers say "generated by ModelGen - Do NOT Modify"; removal is an accepted,
documented deviation (consistent with rounds 1–3), safe because every value
is explicit.

## 4. Removal group B — whitespace residue (2)
- `ref_gl/gl_rmain.c` `R_Init` — collapse the double blank line left where
  the `** draw our stereo patterns` banner was removed (commit c08c235).
- `game/m_brain.c` — collapse the double blank line left where
  `// walk2 is FUBAR, do not use` was removed (same commit).

## 5. Removal group C — redundant forward declaration (1)
- `game/m_tank.c:125` `void tank_walk (edict_t *self);` — its only
  pre-definition consumer was `tank_move_start_walk` (deleted R3); the
  definition at :148 precedes every remaining use.

## 6. Removal group D — `S_Update` signature (3 files)
Round 3 removed `listener_forward`/`listener_up`, the only consumers of the
`forward`/`up` parameters. Drop the parameters:
- `client/sound.h:33` → `void S_Update (vec3_t origin, vec3_t right);`
- `client/snd_dma.c:1003` → definition updated (`origin`/`right` still used)
- `client/cl_main.c:1726` → `S_Update (cl.refdef.vieworg, cl.v_right);`

`cl.v_forward` / `cl.v_up` remain — used elsewhere.

## 7. Verification
- Final gate after all commits: `make clean && make build && make verify-load`.
- Runtime smoke: `./build/quake2 +set developer 1 +map base1` must reach
  `SpawnServer: base1` + full precache with no FATAL/ShutdownError.
- Per-commit grouping (FRAME / cosmetics / S_Update) allows bisection.

## 8. Risks & mitigations
| Risk | Mitigation |
|---|---|
| A `FRAME_*` definition still referenced via its header | Per-header includer word-grep at HEAD before removal; post-removal grep asserts zero occurrences |
| Removing a `#define` shifts other constants | Impossible — all values explicit (not auto-numbered enums) |
| `S_Update` has another caller | Word-grep shows exactly one call site (`cl_main.c:1726`) |
| Whitespace edits touch code | Edits limited to blank lines / one declaration / one signature |
