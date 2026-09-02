# Quake II Dead-Code Cleanup Round 2 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the dead code left behind by the round-1 cleanup — 52 `#if 0`
blocks, the CD-audio stub subsystem and its call sites, dead menu items and
Win32-era cvar remnants, and orphaned functions — keeping the SDL3 arm64 build
fully functional, and making bare `make` print the help menu.

**Architecture:** Phased low-to-high risk: in-file edits first (CD-audio call
sites, menu/cvar wiring, trivial remnants), then file deletions + Makefile
edits, then the mechanical `#if 0` sweep in three subsystem batches, then a
symbol-level dead-function audit. Every task ends with the
`make clean && make build && make verify-load` gate. A validated bare-launch
attract-loop smoke runs before the `--no-ff` merge to `main`.

**Tech Stack:** Original GPL Quake II 3.21 C sources, the SDL3 port (`sdl/`),
Apple clang, GNU make 3.81, macOS arm64.

**Spec:** `docs/superpowers/specs/2026-09-01-quake2-dead-code-round-2-design.md`
(read it — the plan argues from it, and it carries the verified evidence table).

## Global Constraints

1. **Regression gate** after every task: `make clean && make build && make verify-load` — must exit 0 before any commit.
2. **Makefile edits are limited to exactly three changes** (Task 3 only): remove `$(BUILD_DIR)/client/cd_null.o` from `SYS_EXE_OBJS`; remove `null` from the `vpath %.c` line; change `.DEFAULT_GOAL := build` to `.DEFAULT_GOAL := help`. No other Makefile change, in any task.
3. **Preserve fix `ada44e2` verbatim:** in `sdl/qgl_sdl.c` the seven legacy extension pointers (`qglLockArraysEXT`, `qglUnlockArraysEXT`, `qglPointParameterfEXT`, `qglPointParameterfvEXT`, `qglColorTableEXT`, `qglSelectTextureSGIS`, `qglMTexCoord2fSGIS`) stay unresolved/NULL; in `ref_gl/gl_rmain.c`, `R_Init()` keeps its `return true;`. Task 6 edits `gl_rmain.c` — verify both invariants after.
4. **Never** `git add -A` or `git commit -a`. Stage explicit paths only. `baseq2/` holds user play data (paks untracked; saves/config mutated by play) and must never enter a commit.
5. **Never push.**
6. **`qcommon/md4.c` stays** — `Com_BlockChecksum` is called from `cmodel.c:595`, `files.c:480`, `common.c:1234` (verified this round; the round-1 "dead TU" claim was wrong).
7. **POSIX-layer names stay:** `linux/`, `sys_linux.c`, `q_shlinux.c` are not renamed.
8. **`CS_CDTRACK` stays** — wire-protocol configstring constant; only its consuming call sites are removed.
9. If a step fails or a file's content doesn't match what a step expects: **stop and report** with the exact output. Do not improvise around it.
10. Commit messages are given verbatim per task. One commit per task; phase boundaries are the commit groups (Tasks 2–3 = phase 1+2, Tasks 4–6 = phase 3, Task 7 = phase 4).

## Verified context (read before starting)

Facts proven empirically on 2026-09-01 — they explain the smoke design and
prevent re-investigation:

- `newgame` is **not a C command**; it's an alias in `default.cfg` inside
  `pak0.pak`: `alias newgame " killserver ; maxclients 1 ; deathmatch 0 ;
  map *ntro.cin+base1"`. The GPL sources (verified against an upstream clone)
  register it nowhere. Do not "fix" its absence — it is out of scope.
- `Sys_ConsoleInput` (`linux/sys_linux.c`) reads stdin **only when
  `dedicated 1`** — console-command injection into a listen-mode run is
  impossible. Do not attempt stdin-driven smokes.
- `Con_ToggleConsole_f` queues `d1` (the demo loop) whenever
  `cls.state == ca_disconnected`; boot-time cinematics pause while
  `cls.key_dest != key_game` (the menu owns it at boot). Hence the smoke uses
  the engine's own attract chain, not injected commands.
- Bare launch with no arguments: `Com_Init` queues `d1` → attract chain plays
  `idlog.cin`, then `demo1.dm2`/`demo2.dm2` via `nextserver` — full runtime
  stack, no input. Observed map loads: `base2`, `waste2`, and (in one run) the
  full ntro→`base1` chain.
- `SpawnServer:` is printed via `Com_DPrintf` — visible only with
  `+set developer 1`. The smoke asserts on the map precache dump instead.
- `build/` objects: engine objects land in `build/client/` (common + client +
  server + sys), renderer in `build/ref_gl/`, game DLL in `build/game/`.
- The branch `cleanup/dead-code-round-2` already exists (created with this
  plan), based on `main` at the amended spec. Do not recreate it.

---

### Task 1: Verify baseline gate

**Files:** none (build state only)

**Interfaces:**
- Consumes: branch `cleanup/dead-code-round-2` at the amended spec + this plan
- Produces: a verified-green baseline build that every later task starts from

- [ ] **Step 1: Verify branch state**

```bash
cd /Users/felipe.dos.santos/code/theirs/Quake-2
git branch --show-current   # expect: cleanup/dead-code-round-2
git status --short          # expect: empty
git log --oneline -3        # expect: plan commit, spec-amendment, spec commit
```

- [ ] **Step 2: Run the gate**

```bash
make clean && make build && make verify-load
```

Expected: exit 0; `verify-load` prints its dlopen/entry-point checks and exits 0.

If the gate fails here, **stop and report** — the branch base is broken.

---

### Task 2: Phase 1 — remove CD-audio call sites, dead menu/cvar wiring, trivial remnants (in-file edits only)

**Files:**
- Modify: `client/cl_view.c`, `client/cl_parse.c`, `client/cl_cin.c`,
  `client/cl_main.c`, `client/cl_scrn.c`, `client/client.h`,
  `qcommon/files.c` (CD-audio call sites)
- Modify: `client/menu.c`, `client/snd_dma.c`, `client/snd_loc.h`,
  `sdl/vid_sdl.c` (menu items + cvar wiring)
- Modify: `linux/net_udp.c`, `qcommon/qcommon.h` (trivial remnants)

**Interfaces:**
- Consumes: Task 1's green baseline
- Produces: tree with zero `CDAudio_*` references outside `null/cd_null.c` +
  `client/cdaudio.h` (those two files are deleted in Task 3), no
  `s_primary`/`win_noalttab`/`in_joystick` wiring, no `#ifdef NeXT`, version
  strings `"MacOS"`/`"arm64"`; gate green

- [ ] **Step 1: Remove CD-audio call sites (7 files)**

`client/cl_view.c` — at the end of `CL_PrepRefresh` (~line 354), delete both
lines:

```c
	// start the cd track
	CDAudio_Play (atoi(cl.configstrings[CS_CDTRACK]), true);
```

`client/cl_parse.c` — in the configstring update chain (~line 534), delete the
entire `CS_CDTRACK` arm (keep the `else if` chain intact around it):

```c
	else if (i == CS_CDTRACK)
	{
		if (cl.refresh_prepped)
			CDAudio_Play (atoi(cl.configstrings[CS_CDTRACK]), true);
	}
```

`client/cl_cin.c` — in `SCR_PlayCinematic` (~line 583), delete both lines:

```c
	// make sure CD isn't playing music
	CDAudio_Stop();
```

`client/cl_main.c` — three deletions:

1. In `CL_Frame` (~line 1746), delete the call (leave the surrounding
   `S_Update(...)` / `CL_RunDLights()` flow untouched):
```c
	CDAudio_Update();
```
2. In `CL_Init` (~line 1807), delete:
```c
	CDAudio_Init ();
```
3. In `CL_Shutdown` (~line 1839), delete:
```c
	CDAudio_Shutdown ();
```

`client/cl_scrn.c` — in `SCR_BeginLoadingPlaque` (~line 594), delete:

```c
	CDAudio_Stop ();
```

`client/client.h` — delete the include (~line 37):

```c
#include "cdaudio.h"
```

`qcommon/files.c` — two deletions. First the forward declaration above
`FS_Read` (~line 344):

```c
void CDAudio_Stop(void);
```

Then inside `FS_Read` (~line 365), remove the call and its comment, keeping
the one-retry logic unchanged:

```c
		read = fread (buf, 1, block, f);
		if (read == 0)
		{
			// we might have been trying to read from a CD
			if (!tries)
			{
				tries = 1;
				CDAudio_Stop();
			}
			else
				Com_Error (ERR_FATAL, "FS_Read: 0 bytes read");
		}
```

becomes:

```c
		read = fread (buf, 1, block, f);
		if (read == 0)
		{
			if (!tries)
			{
				tries = 1;
			}
			else
				Com_Error (ERR_FATAL, "FS_Read: 0 bytes read");
		}
```

- [ ] **Step 2: Remove dead menu items and cvar wiring (`client/menu.c`)**

In the CONTROLS MENU section:

1. Delete the two cvar declarations at the top (~lines 996-997):
```c
static cvar_t *win_noalttab;
extern cvar_t *in_joystick;
```
2. Delete these widget declarations from the static block (~lines 1004, 1011,
   1012, 1014):
```c
static menulist_s		s_options_noalttab_box;
```
```c
static menulist_s		s_options_joystick_box;
static menulist_s		s_options_cdvolume_box;
```
```c
static menulist_s		s_options_compatibility_list;
```
3. Delete `JoystickFunc` entirely (~line 1021):
```c
static void JoystickFunc( void *unused )
{
	Cvar_SetValue( "in_joystick", s_options_joystick_box.curvalue );
}
```
4. Delete `UpdateCDVolumeFunc` entirely (~line 1119):
```c
static void UpdateCDVolumeFunc( void *unused )
{
	Cvar_SetValue( "cd_nocd", !s_options_cdvolume_box.curvalue );
}
```
5. In `UpdateSoundQualityFunc` (~line 1157), delete only this line (keep the
   `s_khz`/`s_loadas8bit` logic and the rest of the function):
```c
	Cvar_SetValue( "s_primary", s_options_compatibility_list.curvalue );
```
6. In `ControlsSetMenuItemValues`, delete these three fragments (~lines 1057,
   1078-1081):
```c
	s_options_cdvolume_box.curvalue 		= !Cvar_VariableValue("cd_nocd");
```
```c
	Cvar_SetValue( "in_joystick", ClampCvar( 0, 1, in_joystick->value ) );
	s_options_joystick_box.curvalue		= in_joystick->value;

	s_options_noalttab_box.curvalue			= win_noalttab->value;
```
7. In `Options_MenuInit`:
   - delete `static const char *cd_music_items[]` (the `"disabled",
     "enabled", 0` array, ~line 1172) and `static const char
     *compatibility_items[]` (`"max compatibility", "max performance", 0`,
     ~line 1183);
   - delete `win_noalttab = Cvar_Get( "win_noalttab", "0", CVAR_ARCHIVE );`
     (~line 1204);
   - delete the `s_options_cdvolume_box` setup block (7 lines,
     `.type`/`.x`/`.y = 10`/`.name "CD music"`/`.callback`/`.itemnames`/
     `.curvalue`, ~lines 1222-1228);
   - delete the `s_options_compatibility_list` setup block (7 lines, `.y =
     30`, `"sound compatibility"`, ~lines 1238-1244);
   - delete the commented-out `s_options_noalttab_box` block (`/* ... */`,
     ~lines 1294-1302);
   - delete the `s_options_joystick_box` setup block (6 lines, `.y = 120`,
     `"use joystick"`, `JoystickFunc`, ~lines 1303-1308);
   - **renumber the `.y` positions** of the remaining items in steps of 10:
     `quality_list` 20→10 (also fix the `= 20;;` double-semicolon typo to a
     single `;`), `sensitivity_slider` 50→20, `alwaysrun_box` 60→30,
     `invertmouse_box` 70→40, `lookspring_box` 80→50, `lookstrafe_box` 90→60,
     `freelook_box` 100→70, `crosshair_box` 110→80,
     `customize_options_action` 140→90, `defaults_action` 150→100,
     `console_action` 160→110. Leave `sfxvolume_slider` at 0 and the menu's
     `.x`/`.y` offsets unchanged;
   - in the trailing `Menu_AddItem` list, delete these three lines:
```c
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_cdvolume_box );
```
```c
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_compatibility_list );
```
```c
	Menu_AddItem( &s_options_menu, ( void * ) &s_options_joystick_box );
```

- [ ] **Step 3: Remove the cvar plumbing behind the deleted menu items**

`client/snd_dma.c` — delete the global (~line 80):
```c
cvar_t		*s_primary;
```
and its `Cvar_Get` in `S_Init` (~line 133):
```c
		s_primary = Cvar_Get ("s_primary", "0", CVAR_ARCHIVE);	// win32 specific
```

`client/snd_loc.h` — delete (~line 148):
```c
extern cvar_t	*s_primary;
```

`sdl/vid_sdl.c` — delete the global (~line 366) and empty out `IN_Init`'s body
(~line 371), leaving the function in place (it is called from `CL_Init`):
```c
cvar_t	*in_joystick;
```
```c
void IN_Init (void)
{
	in_joystick	= Cvar_Get ("in_joystick", "0", CVAR_ARCHIVE);
}
```
becomes:
```c
void IN_Init (void)
{
}
```

- [ ] **Step 4: Trivial remnants**

`linux/net_udp.c` — delete the NeXT block (lines 16-18):
```c
#ifdef NeXT
#include <libc.h>
#endif
```
and fix the misleading file-header comment on line 1 from `// net_wins.c` to
`// net_udp.c`.

`qcommon/qcommon.h` — replace the version strings (~lines 30-31):
```c
#define BUILDSTRING "NON-WIN32"
#define	CPUSTRING	"NON-WIN32"
```
with:
```c
#define BUILDSTRING "MacOS"
#define	CPUSTRING	"arm64"
```

- [ ] **Step 5: Verify no stray references remain**

```bash
grep -rn "CDAudio" client server qcommon game ref_gl sdl --include="*.c" --include="*.h"
```
Expected: matches only in `client/cdaudio.h` and `null/cd_null.c` (deleted in
Task 3).

```bash
grep -rn "s_primary\|win_noalttab\|in_joystick\|cd_nocd" client sdl --include="*.c" --include="*.h"
```
Expected: no matches.

- [ ] **Step 6: Gate**

```bash
make clean && make build && make verify-load
```
Expected: exit 0, no new warnings.

- [ ] **Step 7: Commit**

```bash
git add client/cl_view.c client/cl_parse.c client/cl_cin.c client/cl_main.c \
        client/cl_scrn.c client/client.h qcommon/files.c client/menu.c \
        client/snd_dma.c client/snd_loc.h sdl/vid_sdl.c linux/net_udp.c \
        qcommon/qcommon.h
git commit -m "chore: remove CD-audio call sites, dead menu items, and Win32 remnants"
```

---

### Task 3: Phase 2 — delete stub files + Makefile edits

**Files:**
- Delete: `null/cd_null.c`, `client/cdaudio.h`, `game/m_rider.h`
- Modify: `Makefile` (exactly the three changes in Global Constraint 2)

**Interfaces:**
- Consumes: Task 2's tree (no live references to the deleted files)
- Produces: `null/` gone from the tree; bare `make` prints help; gate green

- [ ] **Step 1: Delete the three files**

```bash
git rm null/cd_null.c client/cdaudio.h game/m_rider.h
```

- [ ] **Step 2: Edit the Makefile (three changes only)**

1. Remove `null` from the vpath line:
```make
vpath %.c client server qcommon game ref_gl linux null sdl
```
becomes:
```make
vpath %.c client server qcommon game ref_gl linux sdl
```
2. In `SYS_EXE_OBJS`, delete the line:
```make
	$(BUILD_DIR)/client/cd_null.o
```
(keep the backslash continuation on the preceding line correct).
3. Change the default goal:
```make
.DEFAULT_GOAL := build
```
becomes:
```make
.DEFAULT_GOAL := help
```

- [ ] **Step 3: Verify the default goal change**

```bash
make | head -5
```
Expected: the help menu (usage + targets list), not a build.

- [ ] **Step 4: Gate**

```bash
make clean && make build && make verify-load
```
Expected: exit 0.

- [ ] **Step 5: Verify zero references to deleted files**

```bash
grep -rn "cd_null\|cdaudio\.h\|m_rider" client server qcommon game ref_gl sdl Makefile --include="*.c" --include="*.h" || echo CLEAN
```
Expected: `CLEAN`.

- [ ] **Step 6: Commit**

```bash
git add Makefile
git commit -m "chore: delete CD-audio stub and orphan header; make help the default goal"
```

---

### Task 4: Phase 3a — sweep `#if 0` blocks in `client/` + `qcommon/`

**Files:**
- Modify: `client/cl_parse.c`, `client/cl_main.c`, `client/cl_input.c`,
  `client/cl_ents.c`, `client/console.c`, `client/menu.c`,
  `qcommon/cmodel.c`, `qcommon/pmove.c`, `qcommon/common.c`

**Interfaces:**
- Consumes: Task 3's tree
- Produces: zero `^#if 0` matches in `client/` and `qcommon/`; gate green

Known sites at plan time (line numbers shift as you edit — re-grep each file):
`cl_parse.c:246`; `cl_main.c:1710`; `cl_input.c:201`; `cl_ents.c:39, 671, 741,
1424`; `console.c:598`; `menu.c:3849`; `cmodel.c:1275`; `pmove.c:174, 310,
588, 730, 1123`; `common.c:1203` — 16 sites.

- [ ] **Step 1: Sweep each file**

For each file: `grep -n "^#if 0" <file>`, open each site, delete the entire
construct from `#if 0` through its matching `#endif` — including any
`#else`/`#elif` halves (the construct is dead in all branches). Nested
conditionals inside a dead block are deleted with it; salvage nothing from
inside dead blocks. Do not touch commented-out code outside `#if 0`
constructs. Re-grep after each file: expected zero matches.

- [ ] **Step 2: Gate**

```bash
make clean && make build && make verify-load
```
Expected: exit 0. If a build error points at a sweep edit, re-read the site —
the matching `#endif` was likely misidentified.

- [ ] **Step 3: Commit**

```bash
git add client/cl_parse.c client/cl_main.c client/cl_input.c client/cl_ents.c \
        client/console.c client/menu.c qcommon/cmodel.c qcommon/pmove.c \
        qcommon/common.c
git commit -m "chore: sweep #if 0 blocks in client and qcommon"
```

---

### Task 5: Phase 3b — sweep `#if 0` blocks in `server/` + `linux/` + `ref_gl/`

**Files:**
- Modify: `server/sv_world.c`, `server/sv_ents.c`, `server/sv_main.c`,
  `linux/vid_menu.c`, `linux/sys_linux.c`, `ref_gl/gl_warp.c`,
  `ref_gl/gl_local.h`, `ref_gl/gl_rmain.c`, `ref_gl/gl_rsurf.c`,
  `ref_gl/gl_light.c`, `ref_gl/gl_image.c`, `ref_gl/gl_mesh.c`,
  `ref_gl/gl_model.c`

**Interfaces:**
- Consumes: Task 4's tree
- Produces: zero `^#if 0` matches in `server/`, `linux/`, `ref_gl/`; `ada44e2`
  invariants verified; gate green

Known sites at plan time: `sv_world.c:591`; `sv_ents.c:31, 133, 206, 541,
648`; `sv_main.c:150, 533, 553`; `vid_menu.c:138, 242`; `sys_linux.c:293`;
`gl_warp.c:306, 558, 602`; `gl_local.h:22, 303`; `gl_rmain.c:194, 205, 567,
1207`; `gl_rsurf.c:100, 1323`; `gl_light.c:46`; `gl_image.c:1025`;
`gl_mesh.c:791`; `gl_model.c:735` — 27 sites.

- [ ] **Step 1: Sweep `server/` and `linux/` files**

Same rules as Task 4 Step 1.

- [ ] **Step 2: Sweep `ref_gl/` files**

Same rules. The block at `gl_rmain.c:1207` carries the historical comment
`// commented out until H3D pays us the money they owe us` — delete the whole
construct including that comment line.

- [ ] **Step 3: Verify `ada44e2` invariants**

```bash
grep -n "return true;" ref_gl/gl_rmain.c | head -3
grep -n "qglLockArraysEXT\|qglPointParameterfEXT\|qglColorTableEXT" sdl/qgl_sdl.c | head -8
```
Expected: `R_Init` still ends with `return true;`; the seven legacy extension
pointers are still declared-and-never-resolved in `sdl/qgl_sdl.c` (no new
`SDL_GL_GetProcAddress`/assignment lines for them). Also visually confirm the
`R_Init` function body is otherwise unchanged.

- [ ] **Step 4: Gate**

```bash
make clean && make build && make verify-load
```
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add server/sv_world.c server/sv_ents.c server/sv_main.c linux/vid_menu.c \
        linux/sys_linux.c ref_gl/gl_warp.c ref_gl/gl_local.h \
        ref_gl/gl_rmain.c ref_gl/gl_rsurf.c ref_gl/gl_light.c \
        ref_gl/gl_image.c ref_gl/gl_mesh.c ref_gl/gl_model.c
git commit -m "chore: sweep #if 0 blocks in server, linux, and ref_gl"
```

---

### Task 6: Phase 3c — sweep `#if 0` blocks in `game/`

**Files:**
- Modify: `game/g_phys.c`, `game/g_spawn.c`, `game/m_brain.c`,
  `game/m_boss31.c`, `game/m_boss2.c`, `game/m_soldier.c`, `game/q_shared.c`

**Interfaces:**
- Consumes: Task 5's tree
- Produces: zero `^#if 0` matches in the whole tree; gate green

Known sites at plan time: `g_phys.c:605`; `g_spawn.c:625`; `m_brain.c:178`;
`m_boss31.c:562`; `m_boss2.c:514`; `m_soldier.c:147, 703`; `q_shared.c:245,
274` — 9 sites.

- [ ] **Step 1: Sweep each file**

Same rules as Task 4 Step 1.

- [ ] **Step 2: Verify tree-wide**

```bash
grep -rn "^#if 0" client server qcommon game ref_gl sdl linux null --include="*.c" --include="*.h" || echo CLEAN
```
Expected: `CLEAN`.

- [ ] **Step 3: Gate**

```bash
make clean && make build && make verify-load
```
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add game/g_phys.c game/g_spawn.c game/m_brain.c game/m_boss31.c \
        game/m_boss2.c game/m_soldier.c game/q_shared.c
git commit -m "chore: sweep #if 0 blocks in game"
```

---

### Task 7: Phase 4 — dead-function audit

**Files:**
- Modify: whichever files the audit proves dead (determined below)

**Interfaces:**
- Consumes: Task 6's tree (all `#if 0` blocks gone — no dead code hides)
- Produces: orphaned functions removed with evidence recorded in the commit
  message; gate green

- [ ] **Step 1: Static-function pass (compiler)**

The build already uses `-Wall`, which includes `-Wunused-function`. A fresh
build surfaces every orphaned static:

```bash
make clean >/dev/null && make build 2>&1 | grep -E "warning:.*defined but not used" || echo NO-STATIC-ORPHANS
```

For each reported static: delete the function and any prototype/extern for it
(if the prototype exists only to satisfy the now-deleted definition). Deletion
can orphan further statics — re-run until the pass prints
`NO-STATIC-ORPHANS`.

- [ ] **Step 2: Non-static pass (nm cross-reference)**

Engine objects live in `build/client/`, renderer in `build/ref_gl/`, game in
`build/game/`. A non-static function is dead only if defined in one link unit
and referenced by none of the objects in that unit nor by either dlopen'd
bundle:

```bash
cd build
engine_defs=$(nm -gU client/*.o | awk '{print $NF}' | sed 's/^_//' | sort -u)
engine_refs=$(nm -u client/*.o | awk '{print $NF}' | sed 's/^_//' | sort -u)
bundle_refs=$(nm -u ref_gl/*.o game/*.o | awk '{print $NF}' | sed 's/^_//' | sort -u)
comm -23 <(echo "$engine_defs") <(sort -u <(echo "$engine_refs") <(echo "$bundle_refs"))
cd ..
```

(Adapt the `awk` field to the local `nm` output format if it differs; macOS
`nm -gU` prints `<address> <type> <name>` and `nm -u` prints `U <name>` —
verify with a one-line sample first.) Repeat the same cross-reference for
`build/ref_gl/` (defs vs refs within ref_gl + refs from the executable is not
applicable — bundles resolve against the exe at dlopen, so ref_gl symbols
referenced nowhere inside ref_gl are dead unless exported through `GetRefAPI`)
and `build/game/` (same, via `GetGameAPI`).

**Exempt by definition (never delete):** `main`, `GetRefAPI`, `GetGameAPI`,
and anything the struct wiring in `ref.h`/`game.h` names. If unsure whether a
symbol is struct-wired, grep the header — wired symbols stay.

- [ ] **Step 3: Confirm each candidate against the sources**

For every candidate from Steps 1-2:

```bash
grep -rn "\<SYMBOL\>" client server qcommon game ref_gl sdl linux --include="*.c" --include="*.h"
```

Delete only if the matches are the definition and its declaration. Delete the
function and its prototype together.

- [ ] **Step 4: Gate**

```bash
make clean && make build && make verify-load
```
Expected: exit 0, zero `defined but not used` warnings.

- [ ] **Step 5: Commit with evidence**

Check `git status --short` first — the only modifications should be the audit
deletions in tracked source files — then stage them:

```bash
git add -u   # audit changes are source-only; verified via git status above
git commit -m "chore: remove orphaned functions after round-2 sweep

Audit: -Wunused-function pass + nm cross-reference of all three link units.
Removed: <list each symbol with its file and the evidence it was unreferenced>."
```

(If the audit finds nothing to remove, skip this task's commit and record
"phase 4 audit: zero orphaned symbols found" in the Task 8 merge message
instead.)

---

### Task 8: Final smoke + merge to `main`

**Files:** none committed (smoke harness is throwaway)

**Interfaces:**
- Consumes: Tasks 2-7 complete, branch green
- Produces: `main` advanced to the round-2 cleanup via `--no-ff` merge; no
  push

- [ ] **Step 1: Run the validated smoke (spec §8)**

From the repo root:

```bash
./build/quake2 > /tmp/q2-smoke.log 2>&1 & SMOKE_PID=$!
sleep 60
kill $SMOKE_PID 2>/dev/null
for m in "Quake2 Initialized" "client_connect"; do
  grep -q "$m" /tmp/q2-smoke.log || { echo "FAIL: $m"; exit 1; }
done
grep -qE "maps/[a-z0-9]+\.bsp" /tmp/q2-smoke.log || { echo "FAIL: no map load"; exit 1; }
grep -qE "FATAL|ShutdownError" /tmp/q2-smoke.log && { echo "FAIL: fatal"; exit 1; }
echo "SMOKE PASS"
```

Expected: `SMOKE PASS`. A window opens briefly during the run (the attract
chain renders it) and `kill` ends the process — both normal. If any assertion
fails: **stop and report** with the log tail; do not merge.

- [ ] **Step 2: Merge to `main`**

```bash
git checkout main
git merge --no-ff cleanup/dead-code-round-2 \
  -m "Merge branch 'cleanup/dead-code-round-2' — round-2 dead-code cleanup"
git status --short
```

Expected: merge succeeds; `git status` clean (untracked `build/` and
`baseq2/` play data may show — they must NOT be staged).

- [ ] **Step 3: Final gate on `main`**

```bash
make clean && make build && make verify-load
```
Expected: exit 0.

- [ ] **Step 4: Confirm no push**

```bash
git log --oneline -3 main
```
Do not run `git push`. Report the merge commit hash and the per-phase commit
hashes as the completion summary.
