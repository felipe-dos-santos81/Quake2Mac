# Quake II — Dead-Code Cleanup Round 2 — Design

**Date:** 2026-09-01
**Status:** Approved (brainstorming, approach A, scope: all four candidate groups)
**Goal:** Remove the dead code left behind by the round-1 cleanup
(`docs/superpowers/specs/2026-09-01-quake2-apple-silicon-cleanup-design.md`):
the 52 `#if 0` blocks, the CD-audio stub subsystem and its call sites, dead
menu items and Win32-era cvar remnants, and any functions orphaned by all of
the above. The SDL3 arm64 build must stay fully functional.

Companion specs (this work preserves them):
`docs/superpowers/specs/2026-08-31-quake2-sdl3-apple-silicon-design.md` (port),
`docs/superpowers/specs/2026-09-01-quake2-apple-silicon-cleanup-design.md` (round 1).

---

## 1. Scope and constraints

- **In scope:**
  1. Trivial remnants — last platform conditional, dead headers, Win32-era
     strings/cvars (full list in §4).
  2. All 52 `#if 0` blocks in live sources (~25 files).
  3. The CD-audio stub subsystem: `null/cd_null.c`, `client/cdaudio.h`, and
     every `CDAudio_*` call site; the root Makefile is **editable** this round
     (the round-1 no-edit rule is lifted for exactly the cd_null/vpath change).
  4. Symbol-level dead-function audit on the final tree.
- **Out of scope:** any behavioral change to the arm64 build; renaming the
  POSIX layer (`linux/`, `sys_linux.c`, `q_shlinux.c` keep their historic
  names — decided); gameplay/renderer/audio fixes; play-testing; pushing to
  any remote; `baseq2/` contents; `#if 0`-adjacent commented-out code outside
  the blocks themselves.
- **Standing constraints (from the request and prior rounds):**
  - Prefer the change with the **minimal blast radius** at each step.
  - `make clean && make build && make verify-load` is the per-phase
    regression gate; a scripted headless newgame smoke runs before the merge.
  - **Preserve the `ada44e2` runtime fix:** in `sdl/qgl_sdl.c` the seven
    legacy extension pointers (`qglLockArraysEXT`, `qglUnlockArraysEXT`,
    `qglPointParameterfEXT`, `qglPointParameterfvEXT`, `qglColorTableEXT`,
    `qglSelectTextureSGIS`, `qglMTexCoord2fSGIS`) stay unresolved/NULL; in
    `ref_gl/gl_rmain.c`, `R_Init()` keeps its `return true;`. Phase 3 edits
    `gl_rmain.c` `#if 0` blocks — neither invariant may change.
  - **Never** `git add -A` or `git commit -a`: `baseq2/` holds user play data
    (paks, saves, config) that must never enter a commit. Stage explicit paths.

### Locked decisions
| Decision | Choice |
|---|---|
| Approach | **A** — phased, low-to-high risk; in-file edits before file deletions |
| Scope | All four candidate groups (trivial remnants, `#if 0` blocks, CD-audio stubs, dead-function audit) |
| `qcommon/md4.c` | **Keep** — round-1's "no external callers" finding was wrong: `Com_BlockChecksum` is called from `cmodel.c`, `files.c`, `common.c` (map/pak checksums). Re-verified 2026-09-01 |
| POSIX-layer names | **Keep** `linux/`, `sys_linux.c`, `q_shlinux.c` — they are shared POSIX glue, renaming risks Makefile vpath/include churn for zero functional gain |
| `files.c` FS_Read retry | Keep the one-retry-on-zero-read logic; drop only the `CDAudio_Stop` call and declaration |
| `CS_CDTRACK` | Keep — wire-protocol configstring index; only the call sites consuming it are removed |
| Dead menu items | Remove four: *CD music* (`cd_nocd`), *sound compatibility* (`s_primary`), *joystick* (`in_joystick`), *alt-tab disable* (`win_noalttab`), plus their callbacks and cvar plumbing |
| `BUILDSTRING`/`CPUSTRING` | `"MacOS"` / `"arm64"` (used only in the version banner, `common.c:1441`) |
| Git workflow | Branch `cleanup/dead-code-round-2` → one commit per phase (phase 3: ≤ 3 subsystem-batched commits) → `--no-ff` merge to `main`; **no push** |
| Regression gate | Per phase: `make clean && make build && make verify-load`. Pre-merge: scripted headless newgame smoke (throwaway harness in `.qwen/tmp/`, never committed) |

### Headline numbers
- 164 tracked files at start; branch base is `main` tip `6732e52`.
- 3 files deleted (phases 1–2): `null/cd_null.c`, `client/cdaudio.h`,
  `game/m_rider.h`. `null/` becomes empty and disappears.
- 52 `#if 0` blocks swept across ~25 files (phase 3).
- 4 dead menu items + 3 dead cvar wirings removed (phase 1).
- Root Makefile: exactly two line-level edits (drop `cd_null.o` from
  `SYS_EXE_OBJS`; drop `null` from the vpath line).
- Dead-function audit (phase 4) yield unknown until run; findings recorded in
  the commit message.

---

## 2. Verified findings (2026-09-01, this design's evidence base)

| Finding | Evidence |
|---|---|
| `md4.c` is **live** | `Com_BlockChecksum` (md4.c:261) called from `qcommon/cmodel.c:595`, `qcommon/files.c:480`, `qcommon/common.c:1234` |
| CD audio is a pure no-op stub | `null/cd_null.c` implements all six `CDAudio_*` functions as no-ops; nothing else implements them |
| `CDAudio_Activate` is already unreferenced | Declared in `cdaudio.h` only; zero call sites |
| `s_primary` is menu-wired | `menu.c:1157/1244` ↔ "max compatibility / max performance" list; `snd_loc.h:148` extern; `snd_dma.c:80/133` definition. The SDL backend never reads it |
| `win_noalttab`, `in_joystick`, `cd_nocd` are dead | `win_noalttab` created/read only in `menu.c`; `in_joystick` created in `sdl/vid_sdl.c:371`, toggled by menu, read by nothing; `cd_nocd` read/written only in `menu.c`, and CD audio is being removed |
| Last platform conditional | `linux/net_udp.c:16` `#ifdef NeXT` (3-line block) |
| Version strings are Win32-relative | `qcommon/qcommon.h:30-31` `"NON-WIN32"` ×2; sole consumer `common.c:1441` |
| `game/m_rider.h` is orphaned | Zero includers (the rider monster was cut content; boss3 models referencing `boss3/rider` paths are asset strings, not header uses) |

---

## 3. Campaign structure

Branch `cleanup/dead-code-round-2` from `main`. One commit per phase; phase 3
may split into up to three subsystem-batched commits, each gated. After the
phase-4 gate and the headless smoke pass, merge to `main` with `--no-ff`.
Never push.

**Phase 1 — in-file edits (no deletions).** Removing `cd_null.c` while call
sites still reference `CDAudio_*` would break the link step, so call sites go
first. The CD-audio stubs remain in-tree but unreferenced at phase end.

**Phase 2 — deletions + Makefile.** Delete the now-unreferenced stub files
and the orphan header; apply the two Makefile edits.

**Phase 3 — `#if 0` sweep.** Mechanical deletion of all 52 blocks, including
`#else`/`#elif` halves of each construct, with attention to nested
conditionals inside blocks. Batches: (a) `client/` + `qcommon/`,
(b) `server/` + `linux/` + `ref_gl/`, (c) `game/`. Gate after each batch.
`ref_gl/gl_rmain.c` gets special care for the `ada44e2` invariants (§1).

**Phase 4 — dead-function audit.** Two passes: (1) rebuild with
`-Wunused-function` to surface orphaned statics; (2) `nm` cross-check of
non-static definitions against references across all three binaries
(`build/quake2`, `ref_gl.so`, `gamearm64.so`). Functions reachable through
the `GetRefAPI`/`GetGameAPI` struct wiring are alive by definition. Removal
batches re-gated; findings recorded in the commit message.

---

## 4. Phase 1 edit inventory

### CD-audio call sites
| File | Edit |
|---|---|
| `client/cl_view.c` | Drop `CDAudio_Play (atoi(cl.configstrings[CS_CDTRACK]), true)` (~:355) |
| `client/cl_parse.c` | Drop the same call in the configstring-update path (~:537) |
| `client/cl_cin.c` | Drop `CDAudio_Stop()` (~:584) |
| `client/cl_main.c` | Drop `CDAudio_Update()` (~:1746), `CDAudio_Init()` (~:1807), `CDAudio_Shutdown()` (~:1839) |
| `client/cl_scrn.c` | Drop `CDAudio_Stop()` (~:594) |
| `client/client.h` | Drop `#include "cdaudio.h"` (~:38) |
| `qcommon/files.c` | Drop the `void CDAudio_Stop(void);` forward declaration (~:344), its call inside `FS_Read` (~:370), and the CD-specific "we might have been trying to read from a CD" comment; keep the one-retry logic itself unchanged |

### Menu cleanup (`client/menu.c`)
Remove the four items — widget declarations, init-time setup, their
callbacks, and their item-array entries — then renumber the hardcoded `.y`
positions of the remaining items. Drop the
`win_noalttab` static cvar + `Cvar_Get`, the `extern cvar_t *in_joystick` and
its reads/writes (~:1024, :1078-1079), and the `cd_nocd` reads/writes
(~:1057, :1121, :1228). Also drop `compatibility_items[]` (~:1183) and the
CD-music item-name array.

### Other phase-1 remnants
| File | Edit |
|---|---|
| `sdl/vid_sdl.c` | Drop `in_joystick` definition + `Cvar_Get` (~:366, :371) once menu references are gone |
| `client/snd_dma.c` | Drop `s_primary` global (~:80) and its `Cvar_Get` (~:133) |
| `client/snd_loc.h` | Drop `extern cvar_t *s_primary;` (~:148) |
| `linux/net_udp.c` | Drop the `#ifdef NeXT` / `#include <libc.h>` / `#endif` block (:16-18); change the misleading file-header comment `// net_wins.c` to `// net_udp.c` |
| `qcommon/qcommon.h` | `BUILDSTRING` → `"MacOS"`, `CPUSTRING` → `"arm64"` (:30-31) |

## 5. Phase 2 edit inventory

- Delete `null/cd_null.c`, `client/cdaudio.h`, `game/m_rider.h`.
- `Makefile`: remove `$(BUILD_DIR)/client/cd_null.o` from `SYS_EXE_OBJS`;
  remove `null` from the `vpath %.c` line. No other Makefile change.
- `null/` is now empty; the directory leaves the tree.

## 6. Phase 3 rules

- Delete each `#if 0` … matching `#endif` construct entirely, including any
  `#else`/`#elif` halves (the construct is dead in all branches).
- Nested conditionals inside a block are deleted with it; do not attempt to
  salvage code inside dead blocks.
- Do not touch comment-only or commented-out code outside `#if 0` constructs.
- After each batch: `make clean && make build && make verify-load`.

## 7. Phase 4 rules

- Static functions: compile with `-Wunused-function` (added to a throwaway
  build invocation, not committed to the Makefile) and remove each confirmed
  orphan with its declaration.
- Non-static functions: `nm` every object in `build/`; a function is dead
  only if defined in one object and referenced by none of the three link
  units (executable, renderer bundle, game bundle). Exported entry points
  (`GetRefAPI`, `GetGameAPI`, the `RW_IN_*`/`KBD_*`/`SNDDMA_*` contracts,
  `Sys_*`/`Com_*` cross-module APIs) are exempt by definition.
- Record removed symbols and the evidence for each in the phase-4 commit
  message.

## 8. Final smoke (pre-merge)

Throwaway harness in `.qwen/tmp/` (never committed): launch
`./build/quake2` headlessly, drive `newgame`, supply the keypress that ends
the `ntro.cin` cinematic, and assert the log reaches `SpawnServer: base1` →
precache → `ca_active`, then clean exit. Same chain verified end-to-end on
2026-09-01 against the pre-round-2 tree.

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Menu y-renumbering introduces a visual/layout bug | Compile gate + review of the item table; boot-path smoke exercises menu-system init |
| A `#if 0` block is referenced by a stray declaration | Build gate after every batch catches unresolved symbols before commit |
| Phase-3 edits near `gl_rmain.c` invariants | Explicit constraint in §1; verify `return true;` and the NULL pointers after the ref_gl batch |
| Dead-function audit false positive (dlopen/struct-wired symbols) | §7 exemption list; only remove symbols with nm evidence across all three binaries |
| `baseq2/` swept into a commit accidentally | Explicit-path staging only; `git status` review before every commit |
