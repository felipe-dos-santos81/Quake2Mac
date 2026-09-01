# Quake II on Apple Silicon via SDL3 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and run the original GPL Quake II source tree natively on Apple Silicon (arm64), windowed, with SDL3 video/input/audio, driven by a self-documenting root Makefile.

**Architecture:** Keep the engine's runtime-loading design intact: the `quake2` executable `dlopen`s `ref_gl.so` (renderer) and `baseq2/gamearm64.so` (game DLL). Five new files in `sdl/` replace the Linux/X11/OSS platform glue; all other original sources are fixup-only. Bundles keep the `.so` extension so the engine's hard-coded loader names work unchanged.

**Tech Stack:** C (Apple clang 21, arm64-apple-darwin), SDL3 3.4.x (Homebrew, `pkg-config sdl3`), GNU make, OpenGL legacy profile via `-framework OpenGL`.

**Spec:** `docs/superpowers/specs/2026-08-31-quake2-sdl3-apple-silicon-design.md`

## Global Constraints

- Windowed mode only; `GLimp_SetMode` always produces a windowed mode regardless of the `fullscreen` flag.
- The mouse cursor must NEVER be grabbed: no calls to `SDL_SetWindowMouseGrab` or any relative-mouse API anywhere.
- All new SDL3 code lives in `sdl/`; original sources are fixup-only (smallest correct edit, guarded with `__APPLE__` where platform-specific).
- Out of scope (v1): `ref_soft`, CTF/xatrix DLLs, joystick, fullscreen, dedicated-server polish.
- ARCH=arm64; bundle extension stays `.so`; game DLL is loaded by the engine from `baseq2/gamearm64.so`.
- Minimal blast radius: never restructure original code to silence a warning — add the missing prototype/include instead.

## Environment Facts (verified 2026-08-31)

- SDL3 3.4.14 at `/opt/homebrew/opt/sdl3`; there is **no** `sdl3-config` — use `pkg-config sdl3` (`--cflags` → `-I/opt/homebrew/include`, `--libs` → `-L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3`).
- Compiler: Apple clang 21 (`arm64-apple-darwin25.6.0`). clang ≥16 treats implicit function declarations as **errors** — expect these in the 1997 sources; fix by adding prototypes/includes.
- SDL3 API deltas used here (all verified against the installed headers): `SDL_CreateWindow(title, w, h, flags)`; event enums `SDL_EVENT_KEY_DOWN/KEY_UP/MOUSE_MOTION/MOUSE_BUTTON_DOWN/MOUSE_BUTTON_UP/MOUSE_WHEEL/QUIT`; `SDL_KeyboardEvent{.key,.down}`; `SDL_MouseMotionEvent{.xrel,.yrel}` (float); `SDL_MouseButtonEvent{.button,.down}`; `SDL_AudioSpec{format,channels,freq}`; `SDL_AUDIO_S16LE`; `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`; `SDL_OpenAudioDevice(devid,&spec)`; `SDL_CreateAudioStream(&src,&dst)`; `SDL_BindAudioStream(dev,stream)`; `SDL_ResumeAudioStreamDevice(stream)`; `SDL_PutAudioStreamData(stream,buf,len)`; `SDL_GetAudioStreamAvailable(stream)`; `SDL_DestroyAudioStream(stream)`; `SDL_GL_GetProcAddress(name)` returns `SDL_FunctionPointer`; `SDL_GL_CONTEXT_PROFILE_COMPATIBILITY` exists but on macOS request GL 2.1 and leave the profile unset (legacy profile).
- Engine loader facts (verified): `vid_so.c` builds the renderer name as `"ref_%s.so"`; `sys_linux.c` builds the game DLL name from an `#ifdef __i386__/__alpha__` block and loads `<cwd>/<gamedir>/<gamename>`; the renderer bundle must export `GetRefAPI`, `RW_IN_Init/Shutdown/Activate/Commands/Move/Frame`, `KBD_Init/Update/Close`; `gl_local.h:452-459` declares the `GLimp_*` contract; `snd_loc.h` expects `SNDDMA_Init/GetDMAPos/Shutdown/BeginPainting/Submit` + global `dma_t dma`; `paintedtime` is `extern int` in `snd_loc.h:129`.
- Key codes: `client/keys.h` — printable chars pass as lowercase ASCII; specials are `K_TAB 9, K_ENTER 13, K_ESCAPE 27, K_SPACE 32, K_BACKSPACE 127`, arrows 128-131, `K_ALT/K_CTRL/K_SHIFT` 132-134, F-keys 135-146, `K_MOUSE1..3` 200-202, `K_MWHEELDOWN/UP` 239-240.

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `Makefile` (root) | Create (grows across tasks) | Self-documenting build: `help/install/data/build/run/clean` |
| `sdl/vid_sdl.c` | Create (from `linux/vid_so.c`) | Client VID glue: dlopen `ref_gl.so`, fill `refimport_t`, wire input/keyboard pointers |
| `sdl/glw_sdl.c` | Create | Renderer-side `GLimp_*`: SDL3 window + GL context (windowed, no grab) |
| `sdl/in_sdl.c` | Create | Renderer-side `RW_IN_*` + `KBD_*`: SDL3 event pump, key/mouse mapping |
| `sdl/snd_sdl.c` | Create | Client-side `SNDDMA_*` over an SDL3 audio stream |
| `sdl/qgl_sdl.c` | Create | qgl function-pointer table via `SDL_GL_GetProcAddress` |
| `linux/sys_linux.c` | Modify | arm64 game-DLL name; guard `setreuid`/`mntent` code for macOS |
| `ref_gl/gl_local.h`, `ref_gl/qgl.h` | Modify | `__APPLE__` → `<OpenGL/gl.h>`/`<OpenGL/glu.h>` includes |
| other originals | Modify as needed | Implicit-declaration fixes only (Task 2) |

Note on "tests": this tree has no test harness. Each task's test cycle is a **compile/link/run gate** with the exact command and expected result stated.

---

### Task 1: Makefile scaffold — `help`, `install`, `data`, `clean`

**Files:**
- Create: `Makefile`

**Interfaces:**
- Consumes: nothing
- Produces: variables block and `help/install/data/clean` targets used verbatim by all later tasks; later tasks append object/link/run sections to this file.

- [ ] **Step 1: Write the Makefile scaffold**

Create `Makefile` at the repo root:

```make
# Makefile for Quake II — Apple Silicon (arm64) with SDL3
# Stages: 1 install → 2 data → 3 build → 4 run; plus help/clean.
# Styled after the "3AM Reel" template: self-documenting `help` via grep/awk.

# Variables ───────────────────────────────────────────────────────────────────
CC ?= cc
ARCH = arm64
SHLIBEXT = so
BUILD_DIR = build

SDL_CFLAGS := $(shell pkg-config sdl3 --cflags)
SDL_LIBS   := $(shell pkg-config sdl3 --libs)

BASE_CFLAGS = -O2 -g -Wall -Dstricmp=strcasecmp $(SDL_CFLAGS)
CFLAGS = $(BASE_CFLAGS)

EXE = $(BUILD_DIR)/quake2
REF_GL = $(BUILD_DIR)/ref_gl.$(SHLIBEXT)
GAME_DLL = baseq2/game$(ARCH).$(SHLIBEXT)

EXE_LDFLAGS = $(SDL_LIBS) -ldl -lm
BUNDLE_LDFLAGS = -bundle -undefined dynamic_lookup

.PHONY: help install data build run clean

# ── Stage 0 · Help ───────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32mQuake II SDL3 — Apple Silicon build\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

# ── Stage 1 · Dependencies ───────────────────────────────────────────────────

install: ## Check toolchain and SDL3 dependency (brew install sdl3 if missing)
	@$(CC) --version | head -1
	@pkg-config sdl3 --modversion > /dev/null 2>&1 || \
		{ echo "ERROR: sdl3 not found. Run: brew install sdl3"; exit 1; }
	@echo "SDL3 $$(pkg-config sdl3 --modversion) OK"

# ── Stage 2 · Game data ──────────────────────────────────────────────────────

data: ## Check that Quake II game data (baseq2/pak0.pak) is present
	@if [ -f baseq2/pak0.pak ]; then \
		echo "Game data OK: baseq2/pak0.pak"; \
	else \
		echo "ERROR: baseq2/pak0.pak not found."; \
		echo "Copy your Quake II baseq2 pak files (pak0.pak, ...) into ./baseq2/"; \
		exit 1; \
	fi

# ── Stage 5 · Cleanup ────────────────────────────────────────────────────────

clean: ## Remove build outputs (build/ and the baseq2 game DLL copy)
	rm -rf $(BUILD_DIR)
	rm -f $(GAME_DLL)
```

- [ ] **Step 2: Verify each target**

Run: `make help`
Expected: colored target list containing `help`, `install`, `data`, `clean`.

Run: `make install`
Expected: clang version line, then `SDL3 3.4.x OK` (or the brew-install error message if SDL3 were missing).

Run: `make data`
Expected: `Game data OK` if `baseq2/pak0.pak` exists; otherwise the ERROR message and exit code 1. Both outcomes are correct behavior for this gate; record which one occurred.

Run: `make clean`
Expected: removes `build/` (not present yet) and prints nothing on error.

- [ ] **Step 3: Commit**

```bash
git add Makefile
git commit -m "build: Makefile scaffold with help/install/data/clean targets"
```

---

### Task 2: Compile all original engine objects (arm64/clang fixups)

**Files:**
- Modify: `Makefile` (append object lists, pattern rule, `objects` target)
- Modify: `linux/sys_linux.c` (arm64 game-DLL name, macOS guards)
- Modify: `ref_gl/gl_local.h`, `ref_gl/qgl.h` (`__APPLE__` GL includes)
- Modify: any file with an implicit-declaration error (minimal prototype/include fixes)

**Interfaces:**
- Consumes: Task 1 variables (`BUILD_DIR`, `CFLAGS`)
- Produces: `make objects` compiles every original translation unit into `$(BUILD_DIR)/…/*.o`; later tasks link these objects and add their `sdl/*.o` to the same lists. Object variable names produced here and used by later tasks: `COMMON_OBJS`, `CLIENT_OBJS`, `SERVER_OBJS`, `SYS_EXE_OBJS`, `REF_CORE_OBJS`, `REF_EXTRA_OBJS`, `GAME_OBJS`.

- [ ] **Step 1: Apply the known sys_linux.c fixups**

In `linux/sys_linux.c`:

(a) Guard the `<mntent.h>` include (macOS has no such header):

```c
#include <errno.h>
#ifndef __APPLE__
#include <mntent.h>
#endif
```

(b) In `Sys_GetGameAPI` (around line 203), add the arm64 branch:

```c
#ifdef __i386__
	const char *gamename = "gamei386.so";
#elif defined __alpha__
	const char *gamename = "gameaxp.so";
#elif defined(__aarch64__) || defined(__arm64__)
	const char *gamename = "gamearm64.so";
#else
#error Unknown arch
#endif
```

(c) In `Sys_GetGameAPI`, guard the privilege calls (macOS has no `setreuid`):

```c
#ifndef __APPLE__
	setreuid(getuid(), getuid());
	setegid(getgid());
#endif
```

(d) Replace `Sys_CopyProtect` (uses `/etc/mtab`, dead on macOS and unused by the client/server — verify with `grep -rn Sys_CopyProtect client server qcommon | grep -v qcommon.h`, which shows only the declaration) with a stub on macOS:

```c
void Sys_CopyProtect(void)
{
#ifdef __APPLE__
	return;
#else
	FILE *mnt;
	... existing body unchanged ...
#endif
}
```

(wrap the existing body; keep the function visible because `qcommon/qcommon.h:804` declares it).

- [ ] **Step 2: Apply the OpenGL header fixups**

In `ref_gl/gl_local.h` (lines 34-35):

```c
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif
```

In `ref_gl/qgl.h` (line 31):

```c
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
```

- [ ] **Step 3: Append object machinery to the Makefile**

Append to `Makefile` (before the `clean` section):

```make
# ── Stage 3 · Build ──────────────────────────────────────────────────────────

vpath %.c client server qcommon game ref_gl linux null sdl

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<

# qcommon + shared
COMMON_OBJS = \
	$(BUILD_DIR)/client/cmd.o $(BUILD_DIR)/client/cmodel.o \
	$(BUILD_DIR)/client/common.o $(BUILD_DIR)/client/crc.o \
	$(BUILD_DIR)/client/cvar.o $(BUILD_DIR)/client/files.o \
	$(BUILD_DIR)/client/md4.o $(BUILD_DIR)/client/net_chan.o \
	$(BUILD_DIR)/client/q_shared.o $(BUILD_DIR)/client/pmove.o

# client
CLIENT_OBJS = \
	$(BUILD_DIR)/client/cl_cin.o $(BUILD_DIR)/client/cl_ents.o \
	$(BUILD_DIR)/client/cl_fx.o $(BUILD_DIR)/client/cl_input.o \
	$(BUILD_DIR)/client/cl_inv.o $(BUILD_DIR)/client/cl_main.o \
	$(BUILD_DIR)/client/cl_parse.o $(BUILD_DIR)/client/cl_pred.o \
	$(BUILD_DIR)/client/cl_tent.o $(BUILD_DIR)/client/cl_scrn.o \
	$(BUILD_DIR)/client/cl_view.o $(BUILD_DIR)/client/console.o \
	$(BUILD_DIR)/client/keys.o $(BUILD_DIR)/client/menu.o \
	$(BUILD_DIR)/client/snd_dma.o $(BUILD_DIR)/client/snd_mem.o \
	$(BUILD_DIR)/client/snd_mix.o $(BUILD_DIR)/client/qmenu.o \
	$(BUILD_DIR)/client/m_flash.o

# server
SERVER_OBJS = \
	$(BUILD_DIR)/client/sv_ccmds.o $(BUILD_DIR)/client/sv_ents.o \
	$(BUILD_DIR)/client/sv_game.o $(BUILD_DIR)/client/sv_init.o \
	$(BUILD_DIR)/client/sv_main.o $(BUILD_DIR)/client/sv_send.o \
	$(BUILD_DIR)/client/sv_user.o $(BUILD_DIR)/client/sv_world.o

# platform layer linked into the executable
# (sdl/vid_sdl.o and sdl/snd_sdl.o are appended by Tasks 7-8)
SYS_EXE_OBJS = \
	$(BUILD_DIR)/client/cd_null.o $(BUILD_DIR)/client/q_shlinux.o \
	$(BUILD_DIR)/client/vid_menu.o $(BUILD_DIR)/client/sys_linux.o \
	$(BUILD_DIR)/client/glob.o $(BUILD_DIR)/client/net_udp.o

# ref_gl renderer core (sdl/ objects appended by Tasks 3-5)
REF_CORE_OBJS = \
	$(BUILD_DIR)/ref_gl/gl_draw.o $(BUILD_DIR)/ref_gl/gl_image.o \
	$(BUILD_DIR)/ref_gl/gl_light.o $(BUILD_DIR)/ref_gl/gl_mesh.o \
	$(BUILD_DIR)/ref_gl/gl_model.o $(BUILD_DIR)/ref_gl/gl_rmain.o \
	$(BUILD_DIR)/ref_gl/gl_rmisc.o $(BUILD_DIR)/ref_gl/gl_rsurf.o \
	$(BUILD_DIR)/ref_gl/gl_warp.o
REF_EXTRA_OBJS = \
	$(BUILD_DIR)/ref_gl/q_shared.o $(BUILD_DIR)/ref_gl/q_shlinux.o \
	$(BUILD_DIR)/ref_gl/glob.o

# game DLL
GAME_OBJS = \
	$(BUILD_DIR)/game/g_ai.o $(BUILD_DIR)/game/p_client.o \
	$(BUILD_DIR)/game/g_cmds.o $(BUILD_DIR)/game/g_svcmds.o \
	$(BUILD_DIR)/game/g_combat.o $(BUILD_DIR)/game/g_func.o \
	$(BUILD_DIR)/game/g_items.o $(BUILD_DIR)/game/g_main.o \
	$(BUILD_DIR)/game/g_misc.o $(BUILD_DIR)/game/g_monster.o \
	$(BUILD_DIR)/game/g_phys.o $(BUILD_DIR)/game/g_save.o \
	$(BUILD_DIR)/game/g_spawn.o $(BUILD_DIR)/game/g_target.o \
	$(BUILD_DIR)/game/g_trigger.o $(BUILD_DIR)/game/g_turret.o \
	$(BUILD_DIR)/game/g_utils.o $(BUILD_DIR)/game/g_weapon.o \
	$(BUILD_DIR)/game/m_actor.o $(BUILD_DIR)/game/m_berserk.o \
	$(BUILD_DIR)/game/m_boss2.o $(BUILD_DIR)/game/m_boss3.o \
	$(BUILD_DIR)/game/m_boss31.o $(BUILD_DIR)/game/m_boss32.o \
	$(BUILD_DIR)/game/m_brain.o $(BUILD_DIR)/game/m_chick.o \
	$(BUILD_DIR)/game/m_flipper.o $(BUILD_DIR)/game/m_float.o \
	$(BUILD_DIR)/game/m_flyer.o $(BUILD_DIR)/game/m_gladiator.o \
	$(BUILD_DIR)/game/m_gunner.o $(BUILD_DIR)/game/m_hover.o \
	$(BUILD_DIR)/game/m_infantry.o $(BUILD_DIR)/game/m_insane.o \
	$(BUILD_DIR)/game/m_medic.o $(BUILD_DIR)/game/m_move.o \
	$(BUILD_DIR)/game/m_mutant.o $(BUILD_DIR)/game/m_parasite.o \
	$(BUILD_DIR)/game/m_soldier.o $(BUILD_DIR)/game/m_supertank.o \
	$(BUILD_DIR)/game/m_tank.o $(BUILD_DIR)/game/p_hud.o \
	$(BUILD_DIR)/game/p_trail.o $(BUILD_DIR)/game/p_view.o \
	$(BUILD_DIR)/game/p_weapon.o $(BUILD_DIR)/game/q_shared.o \
	$(BUILD_DIR)/game/m_flash.o

ALL_ORIGINAL_OBJS = $(COMMON_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) \
	$(SYS_EXE_OBJS) $(REF_CORE_OBJS) $(REF_EXTRA_OBJS) $(GAME_OBJS)

objects: $(ALL_ORIGINAL_OBJS) ## Compile all engine objects (no linking)
```

Note the `vpath` order: `game` precedes any directory containing another `q_shared.c` (the `ctf/` copy is never in the vpath), and `linux/net_udp.c` is the net_udp source (there is none in `qcommon/`).

- [ ] **Step 4: Compile, fix, repeat**

Run: `make objects 2>&1 | tee /tmp/q2-objects.log`
Expected first run: errors. Fix each error with the smallest correct edit:
- `implicit declaration of function 'X'` → add the missing `#include` or a prototype to the header that already declares X's siblings (check `qcommon/qcommon.h`, `client/client.h`, `game/q_shared.h` first). Never add `-Wno-implicit-function-declaration`.
- `incompatible pointer type` / `int conversion` in original code → add the exact cast the surrounding code style already uses elsewhere in the same file.
- Anything Linux-specific hit here (unexpected, since the replaced files are not compiled) → guard with `#ifdef __APPLE__` providing the portable equivalent.

Re-run until: `make objects` completes with **zero errors** (warnings are acceptable; list any remaining warnings in the commit message).

- [ ] **Step 5: Commit**

```bash
git add Makefile linux/sys_linux.c ref_gl/gl_local.h ref_gl/qgl.h <other files fixed>
git commit -m "build: compile original engine objects on arm64/clang; macOS fixups"
```

---

### Task 3: `sdl/qgl_sdl.c` — GL function-pointer table

**Files:**
- Create: `sdl/qgl_sdl.c`
- Modify: `Makefile` (append one object to `REF_EXTRA_OBJS`)

**Interfaces:**
- Consumes: `qgl.h` extern declarations (`qboolean QGL_Init(const char *dllname); void QGL_Shutdown(void);` — qgl.h:33-34); the qgl pointer definitions to copy from `linux/qgl_linux.c` lines 16-361.
- Produces: definitions of every `qgl*` global declared in `ref_gl/qgl.h`; `QGL_Init` called from `ref_gl/gl_rmain.c:1123`, `QGL_Shutdown` from `gl_rmain.c:1125/1133/1143/1367`.

- [ ] **Step 1: Generate the init/shutdown bodies from the original definitions**

The qgl pointer definitions already exist in `linux/qgl_linux.c` lines 16-361 (all lines matching `TYPE ( APIENTRY * qglName )(args);`, including the extension pointers `qglLockArraysEXT`…`qglSelectTextureSGIS`). Generate matching assignment code:

```bash
perl -ne 'if (/^\s*([A-Za-z_][A-Za-z0-9_ \t]*?)\s*\(\s*APIENTRY\s*\*\s*(qgl[A-Za-z0-9]+)\s*\)\s*(\(.*\))\s*;/) {
    my ($type, $name, $args) = ($1, $2, $3);
    (my $glname = $name) =~ s/^qgl/gl/;
    print "\t$name = ($type (APIENTRY *)$args)gpa(\"$glname\");\n";
}' linux/qgl_linux.c > /tmp/qgl_init_body.txt

perl -ne 'if (/^\s*[A-Za-z_][A-Za-z0-9_ \t]*\(\s*APIENTRY\s*\*\s*(qgl[A-Za-z0-9]+)\s*\)/) {
    print "\t$1 = NULL;\n";
}' linux/qgl_linux.c > /tmp/qgl_shutdown_body.txt

wc -l /tmp/qgl_init_body.txt /tmp/qgl_shutdown_body.txt
```

Expected: ~230 lines in each file (matches the ~230 `qgl*` pointers; cross-check against `grep -c 'qgl.*= NULL;' linux/qgl_linux.c` → 336 includes re-nulling inside QGL_Shutdown; the definition count is the number of lines 16-361 matching the pattern — the two generated files must have **equal** line counts).

- [ ] **Step 2: Write `sdl/qgl_sdl.c`**

Create `sdl/qgl_sdl.c`: start with this skeleton, then paste `/tmp/qgl_init_body.txt` into `QGL_Init` and `/tmp/qgl_shutdown_body.txt` into `QGL_Shutdown`:

```c
// qgl_sdl.c -- qgl function-pointer table loaded via SDL_GL_GetProcAddress.
// Replaces linux/qgl_linux.c (which assigned compile-time-linked libGL
// symbols). The log/dll wrapper layers are dropped: qgl* point straight at
// the driver entry points; unavailable extensions stay NULL and the
// renderer null-checks them.

#include <SDL3/SDL.h>

#include "../ref_gl/gl_local.h"

/* Global qgl* pointer definitions -- copied verbatim from
 * linux/qgl_linux.c lines 16-361. */
<COPY lines 16-361 of linux/qgl_linux.c here (sed -n '16,361p' linux/qgl_linux.c)>

static SDL_FunctionPointer gpa(const char *name)
{
	return SDL_GL_GetProcAddress(name);
}

qboolean QGL_Init(const char *dllname)
{
	(void)dllname; /* SDL3 owns GL loading; gl_driver cvar is unused */

<PASTE /tmp/qgl_init_body.txt>

	/* GL 1.1 essentials must exist or the renderer cannot run */
	if (!qglBegin || !qglEnd || !qglBindTexture || !qglTexImage2D ||
		!qglDrawArrays || !qglEnable || !qglDisable || !qglViewport ||
		!qglMatrixMode || !qglLoadIdentity || !qglClearColor || !qglClear ||
		!qglGetError || !qglGetString)
		return false;

	return true;
}

void QGL_Shutdown(void)
{
<PASTE /tmp/qgl_shutdown_body.txt>
}
```

Note: some definitions in the copied block are `GLboolean ( APIENTRY * qglAreTexturesResparent )...` — the perl regex captures the return type generically, so the generated casts are type-correct; do not hand-edit them.

- [ ] **Step 3: Add the object to the Makefile**

In `Makefile`, extend `REF_EXTRA_OBJS`:

```make
REF_EXTRA_OBJS = \
	$(BUILD_DIR)/ref_gl/q_shared.o $(BUILD_DIR)/ref_gl/q_shlinux.o \
	$(BUILD_DIR)/ref_gl/glob.o $(BUILD_DIR)/ref_gl/qgl_sdl.o
```

- [ ] **Step 4: Compile gate**

Run: `make build/ref_gl/qgl_sdl.o`
Expected: compiles with zero errors.

- [ ] **Step 5: Commit**

```bash
git add sdl/qgl_sdl.c Makefile
git commit -m "sdl: qgl function table via SDL_GL_GetProcAddress"
```

---

### Task 4: `sdl/glw_sdl.c` — SDL3 window and GL context (GLimp_*)

**Files:**
- Create: `sdl/glw_sdl.c`
- Modify: `Makefile` (append one object to `REF_EXTRA_OBJS`)

**Interfaces:**
- Consumes: `GLimp_*` declarations from `ref_gl/gl_local.h:452-459`; `rserr_t` values (`rserr_ok`, `rserr_invalid_mode`, `rserr_unknown` — gl_local.h:114-120); `ri.Vid_GetModeInfo`, `ri.Vid_NewWindow`, `ri.Con_Printf` from `refimport_t`; `qglFlush` from Task 3.
- Produces: `GLimp_Init/Shutdown/SetMode/BeginFrame/EndFrame/AppActivate/EnableLogging/LogNewFrame` linked into `ref_gl.so` (called from `ref_gl/gl_rmain.c` / `gl_rmisc.c`). Window exists after `GLimp_SetMode`; no mouse grab ever set.

- [ ] **Step 1: Write `sdl/glw_sdl.c`**

```c
// glw_sdl.c -- SDL3 implementation of the GLimp_* contract expected by
// ref_gl (see ref_gl/gl_local.h). Replaces linux/gl_fxmesa.c.
// Windowed only; the mouse cursor is never grabbed.

#include <SDL3/SDL.h>

#include "../ref_gl/gl_local.h"

static SDL_Window    *sdl_window;
static SDL_GLContext  sdl_context;

extern cvar_t *vid_fullscreen;
extern cvar_t *vid_ref;

/*
** GLimp_SetMode
** Always windowed in v1: the fullscreen flag is accepted and ignored.
*/
int GLimp_SetMode( int *pwidth, int *pheight, int mode, qboolean fullscreen )
{
	int width, height;

	(void)fullscreen;

	ri.Con_Printf( PRINT_ALL, "Initializing OpenGL display\n" );
	ri.Con_Printf( PRINT_ALL, "...setting mode %d:", mode );

	if ( !ri.Vid_GetModeInfo( &width, &height, mode ) )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return rserr_invalid_mode;
	}

	ri.Con_Printf( PRINT_ALL, " %d %d\n", width, height );

	// destroy the existing window, if any
	GLimp_Shutdown();

	if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) )
	{
		ri.Con_Printf( PRINT_ALL, "...SDL_InitSubSystem(VIDEO) failed: %s\n",
			SDL_GetError() );
		return rserr_unknown;
	}

	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	// Legacy-profile GL on macOS: request 2.1, set no profile mask.
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 1 );

	sdl_window = SDL_CreateWindow( "Quake II", width, height,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE );
	if ( !sdl_window )
	{
		ri.Con_Printf( PRINT_ALL, "...SDL_CreateWindow failed: %s\n",
			SDL_GetError() );
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		return rserr_unknown;
	}

	sdl_context = SDL_GL_CreateContext( sdl_window );
	if ( !sdl_context )
	{
		ri.Con_Printf( PRINT_ALL, "...SDL_GL_CreateContext failed: %s\n",
			SDL_GetError() );
		SDL_DestroyWindow( sdl_window );
		sdl_window = NULL;
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		return rserr_unknown;
	}

	SDL_GL_MakeCurrent( sdl_window, sdl_context );

	*pwidth = width;
	*pheight = height;

	// let the sound and input subsystems know about the new window
	ri.Vid_NewWindow( width, height );

	return rserr_ok;
}

void GLimp_Shutdown( void )
{
	if ( sdl_context )
	{
		SDL_GL_DestroyContext( sdl_context );
		sdl_context = NULL;
	}
	if ( sdl_window )
	{
		SDL_DestroyWindow( sdl_window );
		sdl_window = NULL;
	}
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
}

int GLimp_Init( void *hinstance, void *wndproc )
{
	(void)hinstance;
	(void)wndproc;
	return true;
}

void GLimp_BeginFrame( float camera_seperation )
{
	(void)camera_seperation;
}

void GLimp_EndFrame( void )
{
	qglFlush();
	if ( sdl_window )
		SDL_GL_SwapWindow( sdl_window );
}

void GLimp_AppActivate( qboolean active )
{
	(void)active;
}

void GLimp_EnableLogging( qboolean enable )
{
	(void)enable;
}

void GLimp_LogNewFrame( void )
{
}
```

- [ ] **Step 2: Add the object to the Makefile**

Append `$(BUILD_DIR)/ref_gl/glw_sdl.o` to `REF_EXTRA_OBJS`.

- [ ] **Step 3: Compile gate**

Run: `make build/ref_gl/glw_sdl.o`
Expected: zero errors.

- [ ] **Step 4: Commit**

```bash
git add sdl/glw_sdl.c Makefile
git commit -m "sdl: GLimp window/context layer (windowed, no cursor grab)"
```

---

### Task 5: `sdl/in_sdl.c` — input (RW_IN_* + KBD_*), no grab

**Files:**
- Create: `sdl/in_sdl.c`
- Modify: `Makefile` (append one object to `REF_EXTRA_OBJS`)

**Interfaces:**
- Consumes: `in_state_t`, `Key_Event_fp_t` from `linux/rw_linux.h`; `K_*` codes from `client/keys.h`; `ri.Cvar_Get`, `ri.Cmd_AddCommand`, `ri.Cmd_RemoveCommand`, `ri.Cmd_ExecuteText`; `usercmd_t`, `cvar_t` via `gl_local.h` → `ref.h` → `qcommon.h`; mouse-look cvar pattern from `linux/rw_in_svgalib.c:228-388`.
- Produces: exports dlsym'd by `vid_sdl.c` (Task 8): `KBD_Init(Key_Event_fp_t)`, `KBD_Update(void)`, `KBD_Close(void)`, `RW_IN_Init(in_state_t*)`, `RW_IN_Shutdown(void)`, `RW_IN_Activate(qboolean)`, `RW_IN_Commands(void)`, `RW_IN_Move(usercmd_t*)`, `RW_IN_Frame(void)`.

- [ ] **Step 1: Write `sdl/in_sdl.c`**

```c
// in_sdl.c -- SDL3 input for the ref_gl bundle. Replaces
// linux/rw_in_svgalib.c. Exports RW_IN_* and KBD_* which the client's
// VID layer dlsym's after loading the renderer.
//
// The mouse cursor is NEVER grabbed: no SDL_SetWindowMouseGrab and no
// relative mouse mode. Look-around uses motion deltas delivered while the
// cursor is inside the window.

#include <SDL3/SDL.h>

#include "../ref_gl/gl_local.h"
#include "../client/keys.h"
#include "../linux/rw_linux.h"

/*****************************************************************************/
/* KEYBOARD                                                                  */
/*****************************************************************************/

static Key_Event_fp_t Key_Event_fp;

static int SDLKey_To_QuakeKey(SDL_Keycode k)
{
	switch (k)
	{
	case SDLK_ESCAPE:    return K_ESCAPE;
	case SDLK_RETURN:    return K_ENTER;
	case SDLK_TAB:       return K_TAB;
	case SDLK_SPACE:     return K_SPACE;
	case SDLK_BACKSPACE: return K_BACKSPACE;
	case SDLK_UP:        return K_UPARROW;
	case SDLK_DOWN:      return K_DOWNARROW;
	case SDLK_LEFT:      return K_LEFTARROW;
	case SDLK_RIGHT:     return K_RIGHTARROW;
	case SDLK_LALT:
	case SDLK_RALT:      return K_ALT;
	case SDLK_LCTRL:
	case SDLK_RCTRL:     return K_CTRL;
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:    return K_SHIFT;
	case SDLK_F1:        return K_F1;
	case SDLK_F2:        return K_F2;
	case SDLK_F3:        return K_F3;
	case SDLK_F4:        return K_F4;
	case SDLK_F5:        return K_F5;
	case SDLK_F6:        return K_F6;
	case SDLK_F7:        return K_F7;
	case SDLK_F8:        return K_F8;
	case SDLK_F9:        return K_F9;
	case SDLK_F10:       return K_F10;
	case SDLK_F11:       return K_F11;
	case SDLK_F12:       return K_F12;
	case SDLK_INSERT:    return K_INS;
	case SDLK_DELETE:    return K_DEL;
	case SDLK_PAGEDOWN:  return K_PGDN;
	case SDLK_PAGEUP:    return K_PGUP;
	case SDLK_HOME:      return K_HOME;
	case SDLK_END:       return K_END;
	case SDLK_PAUSE:     return K_PAUSE;
	case SDLK_CAPSLOCK:  return 0; /* ignore */
	case SDLK_KP_1:      return K_KP_END;
	case SDLK_KP_2:      return K_KP_DOWNARROW;
	case SDLK_KP_3:      return K_KP_PGDN;
	case SDLK_KP_4:      return K_KP_LEFTARROW;
	case SDLK_KP_5:      return K_KP_5;
	case SDLK_KP_6:      return K_KP_RIGHTARROW;
	case SDLK_KP_7:      return K_KP_HOME;
	case SDLK_KP_8:      return K_KP_UPARROW;
	case SDLK_KP_9:      return K_KP_PGUP;
	case SDLK_KP_0:      return K_KP_INS;
	case SDLK_KP_PERIOD: return K_KP_DEL;
	case SDLK_KP_ENTER:  return K_KP_ENTER;
	case SDLK_KP_MINUS:  return K_KP_MINUS;
	case SDLK_KP_PLUS:   return K_KP_PLUS;
	case SDLK_KP_DIVIDE: return K_KP_SLASH;
	case SDLK_GRAVE:     return '`';
	default:
		/* SDL3 keycodes for printable characters are lowercase ASCII */
		if (k >= 32 && k < 127)
			return (int)k;
		return 0; /* unmapped: ignore */
	}
}

void KBD_Init(Key_Event_fp_t fp)
{
	Key_Event_fp = fp;
}

void KBD_Close(void)
{
	Key_Event_fp = NULL;
}

/*****************************************************************************/
/* MOUSE + EVENT PUMP                                                        */
/*****************************************************************************/

static qboolean UseMouse = true;

static int   mx, my;                 /* accumulated motion since last Move */
static int   mouse_buttonstate;
static int   mouse_oldbuttonstate;

static qboolean mlooking;

static in_state_t *in_state;

static cvar_t *m_filter;
static cvar_t *in_mouse;
static cvar_t *freelook;
static cvar_t *lookstrafe;
static cvar_t *sensitivity;
static cvar_t *m_pitch;
static cvar_t *m_yaw;
static cvar_t *m_forward;
static cvar_t *m_side;

static void Force_CenterView_f(void)
{
	in_state->viewangles[PITCH] = 0;
}

static void RW_IN_MLookDown(void)
{
	mlooking = true;
}

static void RW_IN_MLookUp(void)
{
	mlooking = false;
	in_state->IN_CenterView_fp();
}

void KBD_Update(void)
{
	SDL_Event e;

	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
		case SDL_EVENT_QUIT:
			/* route through the engine's own quit so config is saved */
			ri.Cmd_ExecuteText(EXEC_APPEND, "quit\n");
			break;

		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		{
			int key = SDLKey_To_QuakeKey(e.key.key);
			if (key && Key_Event_fp)
				Key_Event_fp(key, e.key.down);
			break;
		}

		case SDL_EVENT_MOUSE_MOTION:
			if (UseMouse)
			{
				mx += (int)e.motion.xrel;
				my += (int)e.motion.yrel;
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (UseMouse && Key_Event_fp &&
				e.button.button >= SDL_BUTTON_LEFT &&
				e.button.button <= SDL_BUTTON_MIDDLE)
			{
				Key_Event_fp(K_MOUSE1 + (e.button.button - SDL_BUTTON_LEFT),
					e.button.down);
			}
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			if (UseMouse && Key_Event_fp)
			{
				if (e.wheel.y > 0)
				{
					Key_Event_fp(K_MWHEELUP, true);
					Key_Event_fp(K_MWHEELUP, false);
				}
				else if (e.wheel.y < 0)
				{
					Key_Event_fp(K_MWHEELDOWN, true);
					Key_Event_fp(K_MWHEELDOWN, false);
				}
			}
			break;

		default:
			break;
		}
	}
}

void RW_IN_Init(in_state_t *in_state_p)
{
	in_state = in_state_p;

	m_filter    = ri.Cvar_Get("m_filter", "0", 0);
	in_mouse    = ri.Cvar_Get("in_mouse", "1", CVAR_ARCHIVE);
	freelook    = ri.Cvar_Get("freelook", "0", 0);
	lookstrafe  = ri.Cvar_Get("lookstrafe", "0", 0);
	sensitivity = ri.Cvar_Get("sensitivity", "3", 0);
	m_pitch     = ri.Cvar_Get("m_pitch", "0.022", 0);
	m_yaw       = ri.Cvar_Get("m_yaw", "0.022", 0);
	m_forward   = ri.Cvar_Get("m_forward", "1", 0);
	m_side      = ri.Cvar_Get("m_side", "0.8", 0);

	ri.Cmd_AddCommand("+mlook", RW_IN_MLookDown);
	ri.Cmd_AddCommand("-mlook", RW_IN_MLookUp);
	ri.Cmd_AddCommand("force_centerview", Force_CenterView_f);

	UseMouse = (in_mouse->value != 0);
	ri.Con_Printf(PRINT_ALL, "SDL3 input initialized (cursor never grabbed)\n");
}

void RW_IN_Shutdown(void)
{
	ri.Cmd_RemoveCommand("+mlook");
	ri.Cmd_RemoveCommand("-mlook");
	ri.Cmd_RemoveCommand("force_centerview");
	UseMouse = false;
}

void RW_IN_Commands(void)
{
	/* button state is delivered as K_MOUSE* key events in KBD_Update */
}

/*
===========
RW_IN_Move
===========
*/
void RW_IN_Move(usercmd_t *cmd)
{
	float mouse_x, mouse_y;
	static float old_mouse_x, old_mouse_y;

	if (!UseMouse)
		return;

	if (m_filter->value)
	{
		mouse_x = (mx + old_mouse_x) * 0.5;
		mouse_y = (my + old_mouse_y) * 0.5;
	}
	else
	{
		mouse_x = mx;
		mouse_y = my;
	}
	old_mouse_x = mx;
	old_mouse_y = my;

	if (!mx && !my)
		return;

	mx = my = 0; /* clear for next frame */

	mouse_x *= sensitivity->value;
	mouse_y *= sensitivity->value;

	/* add mouse X/Y movement to cmd */
	if ((*in_state->in_strafe_state & 1) ||
		(lookstrafe->value && mlooking))
		cmd->sidemove += m_side->value * mouse_x;
	else
		in_state->viewangles[YAW] -= m_yaw->value * mouse_x;

	if ((mlooking || freelook->value) &&
		!(*in_state->in_strafe_state & 1))
	{
		in_state->viewangles[PITCH] += m_pitch->value * mouse_y;
	}
	else
	{
		cmd->forwardmove -= m_forward->value * mouse_y;
	}
}

void RW_IN_Frame(void)
{
}

void RW_IN_Activate(qboolean active)
{
	/* windowed with no grab: nothing to grab or release */
	(void)active;
}
```

(`SDL_BUTTON_LEFT` is 1, `SDL_BUTTON_MIDDLE` is 3 in SDL3; `EXEC_APPEND` comes from `qcommon.h`. If `KBD_Update` triggers an `SDL_EVENT_QUIT` before the ref's `ri` is fully set, the `ri.Cmd_ExecuteText` pointer is already populated by `VID_LoadRefresh` before `KBD_Init` is called, so this is safe.)

- [ ] **Step 2: Add the object to the Makefile**

Append `$(BUILD_DIR)/ref_gl/in_sdl.o` to `REF_EXTRA_OBJS`.

- [ ] **Step 3: Compile gate**

Run: `make build/ref_gl/in_sdl.o`
Expected: zero errors.

- [ ] **Step 4: Commit**

```bash
git add sdl/in_sdl.c Makefile
git commit -m "sdl: input layer with SDL3 event pump, never grabs cursor"
```

---

### Task 6: Link `ref_gl.so`

**Files:**
- Modify: `Makefile` (append the bundle link rule and extend `build`)

**Interfaces:**
- Consumes: `REF_CORE_OBJS` (Task 2), `REF_EXTRA_OBJS` extended by Tasks 3-5.
- Produces: `build/ref_gl.so` exporting `GetRefAPI`, `RW_IN_*`, `KBD_*` — the exact symbol set `vid_sdl.c` dlsym's in Task 8.

- [ ] **Step 1: Append the link rule**

Append to the Makefile build section:

```make
$(REF_GL): $(REF_CORE_OBJS) $(REF_EXTRA_OBJS)
	$(CC) $(CFLAGS) $(BUNDLE_LDFLAGS) -framework OpenGL -o $@ \
		$(REF_CORE_OBJS) $(REF_EXTRA_OBJS)

build: objects $(REF_GL)
```

- [ ] **Step 2: Link gate**

Run: `make build/ref_gl.so`
Expected: links with zero errors. (`-undefined dynamic_lookup` covers any host-resolved symbols; `-framework OpenGL` provides GL.)

- [ ] **Step 3: Symbol gate**

Run:
```bash
nm -gU build/ref_gl.so | grep -E "GetRefAPI|RW_IN_Init|RW_IN_Shutdown|RW_IN_Activate|RW_IN_Commands|RW_IN_Move|RW_IN_Frame|KBD_Init|KBD_Update|KBD_Close" | wc -l
```
Expected: `10` (all ten exports present). If any are missing, that file's function signature drifted from the list above — fix the signature, re-link.

- [ ] **Step 4: Commit**

```bash
git add Makefile
git commit -m "build: link ref_gl.so bundle with SDL3 platform layer"
```

---

### Task 7: `sdl/snd_sdl.c` — SNDDMA over SDL3 audio stream

**Files:**
- Create: `sdl/snd_sdl.c`
- Modify: `Makefile` (append one object to `SYS_EXE_OBJS`)

**Interfaces:**
- Consumes: `dma_t dma` and the five `SNDDMA_*` declarations from `client/snd_loc.h`; `paintedtime` (`extern int`, snd_loc.h:129); `Cvar_Get`, `Com_Printf` via `client/client.h`.
- Produces: `SNDDMA_Init/GetDMAPos/BeginPainting/Submit/Shutdown` linked into the executable; called from `client/snd_dma.c` (774, 777, 1093, 1120, 1146-1148).

- [ ] **Step 1: Write `sdl/snd_sdl.c`**

```c
// snd_sdl.c -- SNDDMA_* on an SDL3 audio stream. Replaces
// linux/snd_linux.c (OSS mmap). The engine paints into dma.buffer
// (a plain malloc ring); Submit pushes freshly painted regions to SDL.
//
// UNITS (critical): paintedtime/soundtime are FRAMES ("sample pairs",
// see snd_dma.c). dma.samples and ring offsets are MONO-EQUIVALENT
// samples (frames * channels). last_pushed tracks paintedtime in
// frames; every ring access converts by dma.channels.

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "../client/client.h"
#include "../client/snd_loc.h"

static SDL_AudioStream *audio_stream;
static SDL_AudioDeviceID audio_device;
static int snd_inited;
static int last_pushed;      /* absolute frame cursor handed to SDL (paintedtime units) */

cvar_t *sndbits;
cvar_t *sndspeed;
cvar_t *sndchannels;

qboolean SNDDMA_Init(void)
{
	SDL_AudioSpec spec;

	if (snd_inited)
		return true;

	sndbits     = Cvar_Get("sndbits", "16", CVAR_ARCHIVE);
	sndspeed    = Cvar_Get("sndspeed", "22050", CVAR_ARCHIVE);
	sndchannels = Cvar_Get("sndchannels", "2", CVAR_ARCHIVE);

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		Com_Printf("SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
		return false;
	}

	dma.samplebits = ((int)sndbits->value == 8) ? 8 : 16;
	dma.speed = (int)sndspeed->value;
	if (dma.speed <= 0)
		dma.speed = 22050;
	dma.channels = (int)sndchannels->value;
	if (dma.channels != 1 && dma.channels != 2)
		dma.channels = 2;

	spec.format   = (dma.samplebits == 16) ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
	spec.channels = dma.channels;
	spec.freq     = dma.speed;

	audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
	if (!audio_device)
	{
		Com_Printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	audio_stream = SDL_CreateAudioStream(&spec, &spec);
	if (!audio_stream || !SDL_BindAudioStream(audio_device, audio_stream))
	{
		Com_Printf("SDL audio stream setup failed: %s\n", SDL_GetError());
		if (audio_stream)
			SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
		SDL_CloseAudioDevice(audio_device);
		audio_device = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	dma.samples = 32768;           /* mono-equivalent samples in the ring */
	dma.submission_chunk = 1;
	dma.buffer = (byte *)malloc(dma.samples * (dma.samplebits / 8));
	if (!dma.buffer)
	{
		Com_Printf("SNDDMA_Init: could not allocate dma buffer\n");
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
		SDL_CloseAudioDevice(audio_device);
		audio_device = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}
	memset(dma.buffer, 0, dma.samples * (dma.samplebits / 8));

	dma.samplepos = 0;
	last_pushed = 0;

	SDL_ResumeAudioStreamDevice(audio_stream);

	snd_inited = 1;
	Com_Printf("SDL3 audio: %d Hz, %d channels, %d bit\n",
		dma.speed, dma.channels, dma.samplebits);
	return true;
}

/*
** SNDDMA_GetDMAPos
**
** Returns the consumed position in MONO-EQUIVALENT samples modulo
** dma.samples (the engine divides by dma.channels to get frames).
** Estimated as pushed-minus-still-queued.
*/
int SNDDMA_GetDMAPos(void)
{
	int avail;
	int played_mono;

	if (!snd_inited)
		return 0;

	avail = SDL_GetAudioStreamAvailable(audio_stream);
	if (avail < 0)
		avail = 0;

	played_mono = (int)(((long long)last_pushed * dma.channels)
		- avail / (dma.samplebits / 8));
	if (played_mono < 0)
		played_mono = 0;

	dma.samplepos = played_mono % dma.samples;
	return dma.samplepos;
}

void SNDDMA_BeginPainting(void)
{
}

/*
==============
SNDDMA_Submit

Push the region painted since the last submit into the SDL stream.
paintedtime is in frames; the ring is indexed in mono-equivalent samples.
==============
*/
void SNDDMA_Submit(void)
{
	int bytes_per_sample, bytes_per_frame, frames_in_ring;
	int pos, chunk;

	if (!snd_inited)
		return;

	bytes_per_sample = dma.samplebits / 8;
	bytes_per_frame  = bytes_per_sample * dma.channels;
	frames_in_ring   = dma.samples / dma.channels;

	/* the engine chops paintedtime after very long sessions and snd_restart
	** can regress it; resync if it ever moves backwards */
	if (paintedtime < last_pushed)
		last_pushed = paintedtime;

	/* backpressure: never hold more than half the ring queued in SDL */
	if (SDL_GetAudioStreamAvailable(audio_stream) / bytes_per_frame
		>= frames_in_ring / 2)
		return;

	while (last_pushed < paintedtime)
	{
		pos = last_pushed % frames_in_ring;  /* frame position in ring */
		chunk = paintedtime - last_pushed;   /* frames */
		if (chunk > frames_in_ring - pos)
			chunk = frames_in_ring - pos;
		if (!SDL_PutAudioStreamData(audio_stream,
			dma.buffer + (pos * dma.channels) * bytes_per_sample,
			chunk * bytes_per_frame))
		{
			Com_Printf("SNDDMA_Submit: SDL_PutAudioStreamData failed: %s\n",
				SDL_GetError());
			return;
		}
		last_pushed += chunk;
	}
}

void SNDDMA_Shutdown(void)
{
	if (!snd_inited)
		return;

	if (audio_stream)
	{
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
	}
	if (audio_device)
	{
		SDL_CloseAudioDevice(audio_device);
		audio_device = 0;
	}
	SDL_QuitSubSystem(SDL_INIT_AUDIO);

	if (dma.buffer)
	{
		free(dma.buffer);
		dma.buffer = NULL;
	}
	snd_inited = 0;
}
```

(`SDL_AUDIO_U8` is the SDL3 name for unsigned 8-bit; verify with `grep SDL_AUDIO_U8 /opt/homebrew/opt/sdl3/include/SDL3/SDL_audio.h` if the 8-bit branch is ever used — the 16-bit path is the default.)

- [ ] **Step 2: Add the object to the Makefile**

Append `$(BUILD_DIR)/client/snd_sdl.o` to `SYS_EXE_OBJS`.

- [ ] **Step 3: Compile gate**

Run: `make build/client/snd_sdl.o`
Expected: zero errors.

- [ ] **Step 4: Commit**

```bash
git add sdl/snd_sdl.c Makefile
git commit -m "sdl: SNDDMA audio over SDL3 audio stream"
```

---

### Task 8: `sdl/vid_sdl.c` — VID glue (renderer loading)

**Files:**
- Create: `sdl/vid_sdl.c` (derived from `linux/vid_so.c`)
- Modify: `Makefile` (append one object to `SYS_EXE_OBJS`)

**Interfaces:**
- Consumes: `client/vid.h` contract (`VID_Init/Shutdown/CheckChanges`, `viddef`); everything `vid_so.c` used from `client/client.h`; `rw_linux.h` types for the KBD/RW_IN pointers (which `linux/sys_linux.c` also references via `KBD_Update_fp`).
- Produces: `VID_Init` etc. linked into the executable; loads `build/ref_gl.so` at runtime; defines `KBD_Update_fp` consumed by `linux/sys_linux.c:256` and `Do_Key_Event`.

- [ ] **Step 1: Copy and edit**

Run: `cp linux/vid_so.c sdl/vid_sdl.c`

Apply these exact edits to `sdl/vid_sdl.c`:

(a) Top of file — drop `SO_FILE` and the errno include:

```c
// Main windowed graphics interface module (SDL3 port). Loads the
// renderer DLL and wires input/keyboard callbacks. Derived from
// linux/vid_so.c with the Linux root/privilege and /etc/quake2.conf
// logic removed.

#include <assert.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../client/client.h"

#include "../linux/rw_linux.h"
```

(b) `VID_LoadRefresh` — replace everything from `regain root` / `seteuid(saved_euid)` through the `dlopen` failure block (the `SO_FILE` read, permission checks, `setreuid` calls) with path-based loading:

```c
	/* look for the renderer next to the executable layout, then cwd */
	{
		static const char *search[] = { "./build/", "./" };
		size_t i;
		int ok = 0;

		for (i = 0; i < sizeof(search)/sizeof(search[0]); i++)
		{
			snprintf(fn, sizeof(fn), "%s%s", search[i], name);
			reflib_library = dlopen(fn, RTLD_NOW);
			if (reflib_library)
			{
				ok = 1;
				break;
			}
		}
		if (!ok)
		{
			Com_Printf("LoadLibrary(\"%s\") failed: %s\n", name, dlerror());
			return false;
		}
	}
```

Keep `char fn[MAX_OSPATH];` in the local declarations; delete the `struct stat st;` and `extern uid_t saved_euid;` locals and the `<errno.h>`/`SO_FILE` usage. Keep everything else in the function unchanged (the `ri` fill, `GetRefAPI`, `api_version` check, `in_state` wiring, `RW_IN_*` dlsyms, `Real_IN_Init`, `re.Init`, `KBD_*` dlsyms).

(c) At the end of `VID_LoadRefresh`, delete the trailing privilege calls:

```c
	// give up root now
	setreuid(getuid(), getuid());
	setegid(getgid());
```
(remove these four lines entirely).

(d) `VID_CheckChanges` — replace the soft-fallback block with a hard error (there is no ref_soft in v1):

```c
		sprintf( name, "ref_%s.so", vid_ref->string );
		if ( !VID_LoadRefresh( name ) )
		{
			Com_Error( ERR_FATAL, "Couldn't load %s (check ./build/)", name );
		}
```
(replacing the `if ( !VID_LoadRefresh( name ) ) { ...soft fallback... Cvar_Set("vid_ref","soft")... }` block).

(e) `VID_Init` — default to the GL renderer and drop the Glide env hack:

```c
void VID_Init (void)
{
	/* Create the video variables so we know how to start the graphics drivers */
	vid_ref = Cvar_Get ("vid_ref", "gl", CVAR_ARCHIVE);
	vid_xpos = Cvar_Get ("vid_xpos", "3", CVAR_ARCHIVE);
	vid_ypos = Cvar_Get ("vid_ypos", "22", CVAR_ARCHIVE);
	vid_fullscreen = Cvar_Get ("vid_fullscreen", "0", CVAR_ARCHIVE);
	vid_gamma = Cvar_Get( "vid_gamma", "1", CVAR_ARCHIVE );

	/* Add some console commands that we want to handle */
	Cmd_AddCommand ("vid_restart", VID_Restart_f);

	/* Start the graphics mode and load refresh DLL */
	VID_CheckChanges();
}
```

- [ ] **Step 2: Add the object to the Makefile**

Append `$(BUILD_DIR)/client/vid_sdl.o` to `SYS_EXE_OBJS`.

- [ ] **Step 3: Compile gate**

Run: `make build/client/vid_sdl.o`
Expected: zero errors.

- [ ] **Step 4: Commit**

```bash
git add sdl/vid_sdl.c Makefile
git commit -m "sdl: VID glue loads ref_gl.so without Linux privilege logic"
```

---

### Task 9: Link `gamearm64.so` and the `quake2` executable

**Files:**
- Modify: `Makefile` (append game DLL rule, executable rule, `build` target completion)

**Interfaces:**
- Consumes: `GAME_OBJS` (Task 2), `COMMON_OBJS`/`CLIENT_OBJS`/`SERVER_OBJS`/`SYS_EXE_OBJS` (Tasks 2, 7, 8), `EXE_LDFLAGS`, `BUNDLE_LDFLAGS`; `GAME_DLL` = `baseq2/gamearm64.so` (engine loads it from `<cwd>/baseq2/` — see `linux/sys_linux.c` `Sys_GetGameAPI`).
- Produces: `build/quake2`, `baseq2/gamearm64.so`.

- [ ] **Step 1: Append link rules**

```make
# game DLL -- the engine dlopen's it from <cwd>/baseq2/, so install it there
$(GAME_DLL): $(GAME_OBJS)
	$(CC) $(CFLAGS) $(BUNDLE_LDFLAGS) -o $@ $(GAME_OBJS)

$(EXE): $(COMMON_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) $(SYS_EXE_OBJS)
	$(CC) $(CFLAGS) -o $@ $(COMMON_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) \
		$(SYS_EXE_OBJS) $(EXE_LDFLAGS)

build: objects $(REF_GL) $(GAME_DLL) $(EXE)
```

(Replace the `build: objects $(REF_GL)` line added in Task 6 with the final one above.)

- [ ] **Step 2: Link gate**

Run: `make build`
Expected: zero errors; `build/quake2`, `build/ref_gl.so`, `baseq2/gamearm64.so` all exist.

Run: `file build/quake2 baseq2/gamearm64.so`
Expected: Mach-O 64-bit executable arm64; Mach-O 64-bit bundle arm64 (dylib accepted too).

Run: `otool -L build/quake2 | head -5`
Expected: links `libSDL3.0.dylib` (via `@rpath` or `/opt/homebrew/lib`), `libSystem`, no X11.

- [ ] **Step 3: Dedicated-server smoke (no video, no game data needed)**

Run: `./build/quake2 +set dedicated 1 +quit`
Expected: initializes, prints version/server init text, exits cleanly (exit code 0) without opening a window. If it aborts on a missing symbol, `nm -u build/quake2` shows it — fix the offending object list entry.

- [ ] **Step 4: Commit**

```bash
git add Makefile
git commit -m "build: link gamearm64.so and quake2 executable"
```

---

### Task 10: `run` target and end-to-end verification

**Files:**
- Modify: `Makefile` (append `run` target)

**Interfaces:**
- Consumes: everything from Tasks 1-9; `baseq2/pak0.pak` supplied by the user.
- Produces: a playable windowed game; spec §8 test ladder complete.

- [ ] **Step 1: Append the run target**

```make
# ── Stage 4 · Run ────────────────────────────────────────────────────────────

run: build data ## Launch Quake II (windowed, GL renderer, no cursor grab)
	./$(EXE) +set vid_ref gl +set vid_fullscreen 0
```

Also change the `.PHONY` line to include all targets: `.PHONY: help install data objects build run clean`.

- [ ] **Step 2: Data gate**

Confirm `baseq2/pak0.pak` is present (user copies their Quake II files): `make data` → `Game data OK`. If missing, stop here and tell the user to copy `pak0.pak` (plus `players/`, `video/` etc. for full content) into `baseq2/`.

- [ ] **Step 3: Launch and verify (manual ladder from spec §8)**

Run: `make run`

Verify in order, stopping on the first failure and diagnosing before continuing:
1. A resizable "Quake II" window opens; console text scrolls; log shows `------- Loading ref_gl.so -------` and GL initialization lines (`Initializing OpenGL display`, `...setting mode`).
2. Type `map base1` (or whichever map the installed paks provide) at the console — the level loads and renders.
3. Keyboard: WASD moves, `~` opens console, ESC opens menu.
4. Mouse: look works while the cursor is over the window; the cursor is **never** captured — it can leave the window at any time; Cmd+Tab switches away cleanly.
5. Sound: menu select blips and weapon fire are audible.
6. Play for ~10 minutes; no crash; exit with `quit`.

Common failure points and first checks:
- black window, `GLimp` errors → GL context: check SDL log line; retry without the `SDL_GL_CONTEXT_*_VERSION` attributes (some macOS/SDL combos want them omitted).
- `dlsym` failures on `RW_IN_*`/`KBD_*` → Task 6 symbol gate drifted; re-check signatures.
- no sound → `s_nosound 0`, check `SDL_OpenAudioDevice failed` in console.
- crash in `Sys_GetGameAPI` → `baseq2/gamearm64.so` missing or not arm64 (`file baseq2/gamearm64.so`).

- [ ] **Step 4: Final commit**

```bash
git add Makefile
git commit -m "build: run target; verified playable windowed SDL3 build"
```

---

## Self-Review Record

- **Spec coverage:** §3 components → Tasks 3-8; §4 fixups → Task 2; §5 Makefile stages → Tasks 1/2/6/9/10; §6 runtime behavior (windowed, no grab, input, audio, GL loader) → Tasks 3/4/5/7; §7 error handling → embedded in each file's failure paths + Task 10 ladder; §8 testing → Task 10 (plus dedicated smoke in Task 9). §10 assumptions carried into Global Constraints.
- **Placeholder scan:** no TBD/TODO; every code step contains full code or an exact edit recipe; Task 2's iterative fixes are bounded by an explicit policy (prototype/include first, never a silencing flag).
- **Type/name consistency:** `REF_EXTRA_OBJS` extended in Tasks 3/4/5 with names matching the Task 6 link rule; `SYS_EXE_OBJS` extended in Tasks 7/8 matching Task 9's exe rule; export names (`RW_IN_*`, `KBD_*`, `GLimp_*`, `SNDDMA_*`, `QGL_Init/Shutdown`) match the in-tree declarations cited in Environment Facts.

---

## Post-final-review fix wave (2026-09-01, commit 608daa4)

The whole-branch review empirically reproduced two runtime blockers invisible to compile/link gates, plus gate/cleanup items. All fixed and re-verified (`make verify-load` OK):

1. **C1 — `_Q_ftol` unresolvable at dlopen:** `game/q_shared.h:156` guard extended with `&& !defined __APPLE__` so the `(long)(f)` macro form is used (Q_ftol is defined only for x86 Win32).
2. **C2 — qgl table resolved before any SDL window exists:** `sdl/qgl_sdl.c` now loads GL via `dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL")` + `dlsym` (order-independent; `SDL_GL_GetProcAddress` returns NULL until an SDL GL window exists, which is created only after `QGL_Init`). Supersedes the Task 3 SDL_GL_GetProcAddress text above.
3. **I1 — load-smoke gate:** new `make verify-load` target + `sdl/verify_load.c` harness: dlopens both bundles with `RTLD_NOW` and checks all required entry points; no game data needed. Harness links `$(SDL_LIBS)` because the bundles' undefined SDL symbols resolve only in a process that has loaded libSDL3.
4. **I2 + cleanups:** dead statics/externs/includes removed from `sdl/in_sdl.c`, `sdl/glw_sdl.c`, `sdl/vid_sdl.c` (incl. pre-existing `inupdate`); power-of-two comment at `dma.samples`; `all: data build` target and `.DEFAULT_GOAL := build` added.

Spec errata applied: `SNDDMA_LockBuffer` does not exist in this 3.21 tree (removed from spec §3.1); RW_IN_Activate no-op recorded as deliberate.
