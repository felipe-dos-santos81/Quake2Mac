# Quake II — Apple Silicon-Only Cleanup — Design

**Date:** 2026-09-01 (updated same day post-commit `ada44e2`)
**Status:** Approved (brainstorming, approach A, scope C)
**Goal:** Make the tree build and read as Apple Silicon/arm64-only: remove all
code required solely for Windows, Linux, IRIX, Solaris, Rhapsody, or x86, plus
dead code inside live files. The SDL3 build produced by the 2026-08-31 port
must stay fully functional.

Companion spec (the port this cleanup preserves):
`docs/superpowers/specs/2026-08-31-quake2-sdl3-apple-silicon-design.md`

---

## 1. Scope and constraints

- **In scope:** deleting whole dead platform directories, pruning dead files
  out of live directories, removing never-compiled platform conditionals from
  live files, and removing provably-unreferenced functions.
- **Out of scope:** any behavioral change to the arm64 build, Makefile logic
  changes, gameplay/renderer/audio fixes, play-testing (still user-gated on
  `baseq2/pak0.pak`).
- **Standing constraints (from the request):**
  - Prefer the change with the **minimal blast radius** at each step.
  - Keep answers/artifacts clear and concise; state assumptions.
  - The SDL3 port's load-bearing files (`sdl/`, the 7 reused `linux/` files,
    `null/cd_null.c`, the root `Makefile` object lists) must not break —
    `make clean && make build && make verify-load` is the regression gate.
  - **Preserve the post-port runtime fix `ada44e2`** ("resolve renderer init
    failure on macOS"): `sdl/qgl_sdl.c` must keep the seven legacy extension
    pointers (`qglLockArraysEXT`, `qglUnlockArraysEXT`,
    `qglPointParameterfEXT`, `qglPointParameterfvEXT`, `qglColorTableEXT`,
    `qglSelectTextureSGIS`, `qglMTexCoord2fSGIS`) NULL — the Metal-backed
    legacy OpenGL framework exports those symbols but rejects their legacy
    enums with `GL_INVALID_ENUM`, and the renderer's null-checks take the
    GL 1.1 fallback paths. `R_Init()` in `ref_gl/gl_rmain.c` must keep its
    `return true;`. The phase-3 sweep deletes code that must not come back.
  - **Never** `git add -A` or `git commit -a`: `baseq2/` holds user play
    data (paks, saves, config, players/, video/) that is untracked or
    user-modified — no cleanup commit may include it.

### Locked decisions
| Decision | Choice |
|---|---|
| Aggressiveness | **Scope C** — file deletions + in-file conditional sweep + symbol-level dead-function audit |
| `ctf/` module | **Delete** (dead in this build; recoverable from git history) |
| Approach | **A** — phased demolition, one commit per phase, gate after each phase |
| Workflow | Branch `cleanup/apple-silicon-only` → 4 commits → merge to master locally; not pushed |
| Text docs | **Keep** — `gnu.txt`, changelogs (`3.1*_Changes.txt`, `changes.txt`), `readme.txt`, `joystick.txt` are history/license, not dead code |
| `graphify-out/` | **Keep** as committed analysis history; its references to deleted files are fine (it is a snapshot) |
| `#if 0` blocks | **Keep** — dormant experimental/reference code is out of scope; the sweep targets platform conditionals only |
| `qcommon/md4.c` | **Keep** — fully self-referential (no external callers), but removing it would require editing `COMMON_OBJS` in the Makefile, which this cleanup does not touch. Recorded as a documented finding/follow-up |

### Headline numbers
- 690 tracked files at start; master baseline is `ada44e2` (renderer fix).
- **210 files deleted** across phases 1–2 (≈30% of the tree), zero of them
  referenced by anything that compiles.
- 42 conditional sites swept in phase 3 across 17 files; ≤ a few dozen
  functions removed in phase 4 (modest yield expected).
- **The root `Makefile` needs no edits at all**: nothing deleted appears in
  any object list, and no deletion changes vpath basename resolution (§7).

---

## 2. Invariants — the alive set

Everything in this set must survive untouched (or with phase-3/4 edits only):

- **Executable:** `client/`, `server/`, `qcommon/` cores; platform layer
  `linux/{glob.c,glob.h,net_udp.c,q_shlinux.c,rw_linux.h,sys_linux.c,vid_menu.c}`;
  `null/cd_null.c`; `sdl/{snd_sdl.c,vid_sdl.c}`.
- **Renderer bundle `ref_gl.so`:** `ref_gl/gl_*.c`, `ref_gl/{gl_local.h,gl_model.h,qgl.h,anorms.h,anormtab.h,warpsin.h}`;
  `sdl/{qgl_sdl.c,glw_sdl.c,in_sdl.c}`.
- **Game bundle `gamearm64.so`:** all of `game/` `.c/.h`.
- **Build tooling:** root `Makefile`, `sdl/verify_load.c`, `.gitignore`,
  `make verify-load` (the gate that needs no game data).
- **dlsym export contract:** `GetRefAPI`, `RW_IN_{Init,Shutdown,Activate,Commands,Move,Frame}`,
  `KBD_{Init,Update,Close}` (renderer), `GetGameAPI` (game). These must never
  be removed or renamed by phase 4.
- **`ada44e2` runtime-fix behavior** (see §1): the seven legacy GL extension
  pointers stay NULL in `sdl/qgl_sdl.c`; `R_Init()` returns `true`.
- **Docs:** `docs/`, `graphify-out/`, root `*.txt`.

Liveness was proven two ways: (a) every surviving file is in the Makefile
object lists or included (directly or transitively, incl. `../linux/…` forms)
by one that is; (b) every deleted file has zero references from the alive set
(verified via include-graph grep, including path-prefixed includes).
Notable keep-because-included: `linux/rw_linux.h` (included by
`sdl/vid_sdl.c`, `sdl/in_sdl.c`), `linux/glob.h` (included by
`linux/q_shlinux.c`, `linux/glob.c`), `client/anorms.h` (included by
`qcommon/common.c`).

---

## 3. Phase 1 — delete dead platforms & modules (161 tracked files)

Pure `git rm`. Whole directories:

| Dir | Files | Contents |
|---|---|---|
| `win32/` | 25 | Win32 sys/video/input/sound/net, WGL loader, MSVC resources |
| `ctf/` | 54 | standalone CTF game DLL source (incl. `ctf/docs/`) |
| `ref_soft/` | 41 | software renderer: C core + x86 `.asm` + MSVC files |
| `rhapsody/` | 12 | NeXT/Rhapsody port (`.m` sources, PB project, icons) |
| `irix/` | 8 | IRIX port |
| `solaris/` | 8 | Solaris port |
| `unix/` | 3 | two stale makefiles + a stray 1997 `.o` build artifact |

Root files (10): `quake2.001`, `quake2.bce`, `quake2.bcp`, `quake2.dsp`,
`quake2.dsw`, `quake2.mak`, `quake2.opt`, `quake2.plg` (MSVC project),
`makezip`, `makezip.bat` (Win32-era packaging).

Commit 1: `cleanup: remove dead platform ports, unused renderers/modules, and MSVC build files`.

---

## 4. Phase 2 — prune live directories (49 tracked files)

Pure `git rm`; per-directory keep/delete:

| Dir | Keep | Delete |
|---|---|---|
| `linux/` 35→7 | `glob.c` `glob.h` `net_udp.c` `q_shlinux.c` `rw_linux.h` `sys_linux.c` `vid_menu.c` | `Makefile.AXP` `Makefile.i386` `block16.h` `block8.h` `cd_linux.c` `d_copy.s` `d_ifacea.h` `d_polysa.s` `gl_fxmesa.c` `in_linux.c` `math.s` `qasm.h` `qgl_linux.c` `r_aclipa.s` `r_draw16.s` `r_drawa.s` `r_edgea.s` `r_scana.s` `r_spr8.s` `r_surf8.s` `r_varsa.s` `rw_in_svgalib.c` `rw_svgalib.c` `rw_x11.c` `snd_linux.c` `snd_mixa.s` `sys_dosa.s` `vid_so.c` (28) |
| `null/` 8→1 | `cd_null.c` | `cl_null.c` `glimp_null.c` `in_null.c` `snddma_null.c` `swimp_null.c` `sys_null.c` `vid_null.c` (7) |
| `client/` | all compiled files + `anorms.h` | `x86.c` `asm_i386.h` `adivtab.h` `block8.h` `block16.h` (5) |
| `server/` | all compiled files | `sv_null.c` (1) |
| `game/` | all `.c/.h` | `game.001` `game.def` `game.dsp` `game.plg` (4) |
| `ref_gl/` | all `.c/.h` | `ref_gl.001` `ref_gl.def` `ref_gl.dsp` `ref_gl.plg` (4) |

Commit 2: `cleanup: prune dead files from live directories (linux asm/svgalib/x11, null stubs, x86 client files, MSVC files)`.

---

## 5. Phase 3 — in-file conditional sweep (42 sites, 17 files)

Remove every never-compiled conditional branch for
`WIN32 / _WIN32 / id386 / _M_IX86 / _M_ALPHA / __i386__ / __alpha__ /
__sun__ / __sgi / C_ONLY`.
Where a block is `#ifdef X … #else … #endif`, keep the `#else` side.
No behavioral change on arm64; each hunk reviewed against its counterpart
before committing. Because the tree is now Apple-only, blocks that are
*guarded out* on Apple (`#ifndef __APPLE__`, `#ifdef __APPLE__ return; #else …`)
are dead too: fold them the same way.

Site census (from live-tree grep):

| File | Sites | Notes |
|---|---|---|
| `qcommon/qcommon.h` | 7 | BUILDSTRING/CPUSTRING chain → keep the final `#else` values (`"NON-WIN32"`) unconditionally |
| `game/q_shared.h` | 6 | delete `_WIN32` pragma block; delete `id386`/`idaxp` macro definitions (all users swept); fold `Q_ftol` guard to the macro form `#define Q_ftol( f ) ( long ) (f)` |
| `game/q_shared.c` | 5 | delete `_WIN32` pragma pairs and the `_M_IX86` naked-asm `Q_ftol`; keep the C `BoxOnPlaneSide` (drop the MSVC-asm `#else` half); fold `Q_stricmp` to `strcasecmp` |
| `client/snd_mix.c` | 4 | two nested `#if !(…__linux__ && …__i386__)` / `#if !id386` wrappers: keep the C versions of `S_WriteLinearBlastStereo16` and `S_PaintChannelFrom8`, drop the MSVC-asm `#else` halves |
| `linux/sys_linux.c` | 3 | `Sys_Init`: drop `#if id386` stub; arch selector folds to `const char *gamename = "gamearm64.so";`; drop the `#ifndef __APPLE__` `setreuid/setegid` block and `#include <mntent.h>` guard; `Sys_CopyProtect` folds to `return;` (drop the mntent CD-check `#else` half). The trailing `#if 0` `Sys_MakeCodeWriteable` block is left alone (`#if 0` policy, §1) |
| `ref_gl/qgl.h` | 3 | drop `_WIN32` `<windows.h>` include; delete the whole `_WIN32` `qwgl*` extern block; fold the `__sgi` `GL_SHARED_TEXTURE_PALETTE_EXT` define to `0x81FB` |
| `ref_gl/gl_warp.c` | 2 | keep the `#if !id386` portable expressions, drop the `Q_ftol` `#else` halves |
| `qcommon/cmodel.c` | 2 | drop both `_WIN32` `#pragma optimize` guards |
| `client/menu.c` | 2 | drop `_WIN32` `<io.h>` include; keep the `fseek/ftell` file-length `#else` half |
| `ref_gl/gl_rmisc.c` | 1 | drop the `_WIN32` `qwglSwapIntervalEXT` call and the enclosing `if ( !gl_state.stereo_enabled )` block that becomes empty |
| `ref_gl/gl_rmain.c` | 1 | delete the `#ifdef WIN32` extension-grab block (the Win32-only resolution of the same seven pointers `ada44e2` deliberately leaves NULL — it must not be re-introduced anywhere); **preserve `return true;` in `R_Init()` (ada44e2)** |
| `ref_gl/gl_local.h` | 1 | drop `_WIN32` `<windows.h>` include; keep the `__APPLE__` GL-header selection |
| `qcommon/md4.c` | 1 | fold the `__alpha__` typedef to `typedef unsigned int UINT4;` (32-bit words, the algorithm's requirement; same as the `__alpha__` branch did for LP64). File itself is kept (§1 md4.c decision) |
| `qcommon/files.c` | 1 | drop the `_WIN32` `strlwr` call |
| `game/g_save.c` | 1 | keep the `gi.dprintf` function-offset `#else` half |
| `client/client.h` | 1 | delete the `#if id386` x86-timer prototype block (`client/x86.c` is deleted in phase 2) |
| `client/cl_main.c` | 1 | drop the `#if defined __linux__ || defined __sgi` init-order branch; keep the `#else` order (`VID_Init()` before `S_Init()` — sound after window) |

Rules:
1. Our port's `__APPLE__` guards are preserved where they select Apple
   behavior (GL headers), and folded away where they guard *out* dead
   non-Apple code (`#ifndef __APPLE__` blocks, `Sys_CopyProtect`).
2. If a conditional's kept branch would become unconditional, the surrounding
   `#if/#else/#endif` scaffolding is removed too.
3. `#if 0` blocks are out of scope and left untouched.
4. Anything ambiguous is left alone and recorded in the commit message.
5. After the sweep, `grep -rn 'id386\|idaxp\|WIN32\|_M_IX86\|_M_ALPHA\|__i386__\|__alpha__\|__sun__\|__sgi\|C_ONLY'`
   across the live source dirs returns nothing (the `__APPLE__` guards are
   the only platform conditionals that may remain).

Commit 3: `cleanup: remove never-compiled Win32/x86 conditionals from live sources`.

---

## 6. Phase 4 — dead-function audit

### 4a — proven dead statics (from `-Wunused-function`)
Delete:
- `NoAltTabFunc` — `client/menu.c` (Win32 leftover; may already fall out in phase 3)
- `Menulist_DoEnter`, `SpinControl_DoEnter` — `client/qmenu.c`
- `glob_pattern_p` — `linux/glob.c`
- `GL_DrawStereoPattern` — `ref_gl/gl_rmain.c`

### 4b — symbol-level audit of non-static functions
1. Enumerate every non-static function defined in live `.c` files.
2. A symbol **survives** if any of:
   - it has a word-boundary reference in the live tree outside its defining
     file (this catches `Cmd_AddCommand` callbacks, the game spawn table, and
     function-pointer assignments, all of which mention the identifier);
   - it appears in `nm -u` of `build/ref_gl.so` or `baseq2/gamearm64.so`
     (symbols the bundles resolve against the host process at dlopen time);
   - it is a dlsym export (§2 contract) or `main`.
3. Zero-reference symbols become deletion candidates; vet them in small
   per-module batches, re-gating after each batch (§7). Ambiguous candidates
   are kept.

Expectation: modest yield, concentrated in `qcommon/` and `client/`. Game
monster AI functions are mostly live via function-pointer tables even when
never called by name; do not chase yield.

Known finding: `qcommon/md4.c` is a fully dead translation unit (all MD4
symbols are referenced only by each other inside the file), but it stays —
removing it would require editing `COMMON_OBJS` in the Makefile (§1). The
audit rule "reference outside the defining file" naturally keeps its
mutually-referencing cluster. Report it in the commit message as a follow-up.

Commit 4: `cleanup: remove unreferenced functions (unused statics + symbol audit)`.

---

## 7. Verification & acceptance

**Gate (after every phase, and after each phase-4 batch):**
```
make clean && make build && make verify-load
```
`verify-load` dlopens both bundles with `RTLD_NOW` and checks every entry
point. It stays the per-phase gate because it is fast and opens no window.

**Runtime smoke (acceptance only, not per-phase):** game data is now present
(`baseq2/pak0.pak`, `pak1.pak`, `pak2.pak` were added by the user on
2026-09-01, and the `ada44e2` fix made the game actually launch), so final
acceptance additionally runs a short dedicated-server smoke
(`./build/quake2 +set dedicated 1 +quit`, which loads the paks and exits
cleanly) and a user-visual `make run` windowed launch.

**vpath safety check:** the Makefile resolves objects by basename through
`vpath %.c client server qcommon game ref_gl linux null sdl`. No deleted file
shares a basename with a surviving file in another vpath directory (verified:
e.g. `vid_so.c`/`vid_sdl.c`, `in_linux.c`/`in_sdl.c` differ; `ctf/q_shared.c`
was never in the vpath order). Therefore no object can silently re-resolve
after deletions, and the Makefile stays untouched.

**Acceptance criteria:**
1. Gate green on `master` post-merge.
2. Spot-check grep finds no `WIN32|svgalib|wgl|id386` in any `*.c/*.h`
   outside `docs/` and `graphify-out/`.
3. History shows the four cleanup commits; the merge diff contains only
   deletions plus the phase-3/4 edits.
4. `git ls-files | wc -l` ≈ 480 (690 − 210).
5. Dedicated-server smoke exits cleanly; no cleanup commit contains
   `baseq2/` play data or `.DS_Store` files.

---

## 8. Risks & mitigations

| Risk | Mitigation |
|---|---|
| A deleted file turns out to be referenced after all | Deletion set proven against Makefile object lists **and** the full include graph (incl. path-prefixed includes); gate after each phase; everything recoverable from git history |
| Phase 3 mishandles an `#else` branch | Per-hunk review against its counterpart; gate after the phase; ambiguous blocks left alone and recorded |
| Phase 4 removes a function referenced by string/pointer dispatch | Survival rule is word-boundary grep (covers `Cmd_AddCommand`, spawn tables, pointer assignment) plus bundle `nm -u` lists plus the dlsym contract; ambiguous candidates kept |
| Deleting a file changes vpath basename resolution | Checked: no basename collisions between deleted and surviving files across vpath dirs (§7) |
| `verify-load` passes but runtime regresses (no pak data on disk) | Same constraint as the port; user play-test once `baseq2/pak0.pak` is present. Phases 3–4 are the only ones touching live code, and they are behavior-preserving by construction |

---

## 9. Assumptions (stated explicitly)

- **Apple Silicon only** remains the target; nothing is done to make other
  platforms buildable again.
- **Game data is present** as of 2026-09-01 (`baseq2/pak0.pak`, `pak1.pak`,
  `pak2.pak`, untracked). The per-phase gate still uses `verify-load`
  (fast, no window); the runtime smoke and play-test use the real data.
- **`baseq2/` is user territory.** The paks/`players/`/`video/`/`maps.lst`
  are untracked; `baseq2/save/save0/*.ssv` and `baseq2/config.cfg` are
  tracked but mutated by play. Commits stage explicit paths only.
- Deleting `ctf/` and `ref_soft/` is acceptable because git history (and the
  upstream id-Software tree) preserves them.
- The built `baseq2/gamearm64.so` is gitignored and stays out of commits.
- Commit-per-phase granularity on one feature branch, merged locally to
  master, not pushed — `origin` is the upstream id-Software repository and
  must never be pushed to.
- The `ada44e2` renderer fix is on master and is preserved verbatim by this
  cleanup (see §1 constraints).
