# Quake II on Apple Silicon via SDL3 — Design

**Date:** 2026-08-31
**Status:** Approved (brainstorming, approach A)
**Goal:** Build and run the original GPL Quake II source tree natively on Apple
Silicon (arm64), using SDL3 for window/input/audio, driven by a root `Makefile`.
Windowed mode only; the mouse cursor must never be grabbed/locked by the game.

---

## 1. Scope and constraints

- **In scope:** single-player Quake II using the `ref_gl` (OpenGL) renderer,
  built from this tree, running on macOS arm64 with SDL3.
- **Out of scope (v1):** `ref_soft`, CTF/xatrix game DLLs, joystick, fullscreen,
  dedicated-server polish, networking beyond what compiles by default.
- **Standing constraints (from the request):**
  - Prefer the change with the **minimal blast radius**.
  - **Reuse existing libraries** (SDL3) to avoid rewriting engine internals.
  - Windowed mode; **do not lock/grab the mouse cursor**.
  - Confine all new SDL3 code to a `sdl/` directory; treat original sources as
    fixup-only.

### Locked decisions
| Decision | Choice |
|---|---|
| Approach | **A** — SDL3 platform layer; keep the original runtime-loading architecture (`quake2` exe dlopens `ref_gl` + game DLL). Static-link and Yamagi-adoption were rejected. |
| Renderer | `ref_gl` (OpenGL) |
| Game data | User owns Quake II and supplies `baseq2/*.pak`; Makefile provides a `data` check target (not the demo pak) |
| Build driver | Root `Makefile`, styled after the reference "3AM Reel" Makefile (self-documenting `help` via grep/awk, numbered stage targets) |
| Platform layer location | New `sdl/` directory |

---

## 2. Architecture

The engine's existing loader design is preserved unchanged. The `quake2`
executable loads the renderer (`ref_gl.so`) and the game DLL (`gamearm64.so`)
at runtime via `dlopen`, exactly as the Linux build does. SDL3 is introduced
only at the OS-facing edges.

```
build/quake2  (executable)                build/ref_gl.so   (renderer bundle)
├─ client/  server/  qcommon/             ├─ ref_gl/gl_*.c        (unchanged core)
├─ game/q_shared.c  pmove.c               ├─ sdl/qgl_sdl.c        (GL proc table)
├─ sdl/vid_sdl.c   (VID_ glue, dlopen)    ├─ sdl/glw_sdl.c        (SDL window+ctx)
├─ sdl/snd_sdl.c   (SNDDMA_ over SDL3)    ├─ sdl/in_sdl.c         (RW_IN_*+KBD_*)
├─ linux/sys_linux.c  (kept, tiny fix)    ├─ game/q_shared.c
├─ linux/net_udp.c  (kept)                ├─ linux/q_shlinux.c
├─ linux/q_shlinux.c (kept)               └─ linux/glob.c
├─ linux/glob.c     (kept)
├─ linux/vid_menu.c (kept)                build/gamearm64.so  (game bundle)
└─ null/cd_null.c   (kept, as-is)         └─ game/*.c              (unchanged)
```

### What the client loads at runtime
`vid_so.c`/`vid_sdl.c` `dlopen`s `ref_gl.so` and `dlsym`s these entry points,
which the renderer bundle must therefore **export**:

- `GetRefAPI` (renderer API) — already provided by `ref_gl`.
- Input: `RW_IN_Init`, `RW_IN_Shutdown`, `RW_IN_Activate`, `RW_IN_Commands`,
  `RW_IN_Move`, `RW_IN_Frame`  → supplied by `sdl/in_sdl.c`.
- Keyboard: `KBD_Init`, `KBD_Update`, `KBD_Close` → supplied by `sdl/in_sdl.c`.

The renderer core calls into `sdl/glw_sdl.c` through the `GLimp_*` contract
declared in `ref_gl/gl_local.h` (lines 452–459):
`GLimp_Init`, `GLimp_Shutdown`, `GLimp_SetMode`, `GLimp_BeginFrame`,
`GLimp_EndFrame`, `GLimp_AppActivate`, `GLimp_EnableLogging`,
`GLimp_LogNewFrame`.

---

## 3. Components

All new SDL3 code lives in a new `sdl/` directory. Each file replaces exactly
one platform-specific file from the Linux/Win32 ports; the portable engine is
untouched.

### 3.1 New files (in `sdl/`)

| New file | Replaces | Responsibility |
|---|---|---|
| `sdl/vid_sdl.c` | `linux/vid_so.c` | `VID_Init/Shutdown/CheckChanges`, mode table, and `VID_LoadRefresh` (dlopen `ref_gl.so`, fill `refimport_t`, `dlsym` `RW_IN_*`/`KBD_*`). Drops the Linux-only root/seteuid and `/etc/quake2.conf` logic; loads `ref_gl.so` from the build dir. |
| `sdl/glw_sdl.c` | `linux/gl_fxmesa.c` (conceptually `win32/glw_imp.c`) | Implements the `GLimp_*` contract using SDL3: create a resizable `SDL_WINDOW_OPENGL` window, create the GL context, `SDL_GL_MakeCurrent`, swap via `SDL_GL_SwapWindow`. **Windowed only; never grabs the cursor.** |
| `sdl/in_sdl.c` | `linux/rw_in_svgalib.c` | Exports `RW_IN_*` + `KBD_*`. Pumps `SDL_PollEvent` each frame; maps SDL scancodes/buttons to Quake keys and `usercmd_t` movement. Delivers keys via the `Key_Event_fp` callback. **Mouse: relative-motion deltas without `SDL_SetRelativeMouseMode`/grab.** |
| `sdl/snd_sdl.c` | `linux/snd_linux.c` | Implements `SNDDMA_Init/Shutdown/BeginPainting/Submit/GetDMAPos` on top of an `SDL_AudioStream` (SDL3) fed from the engine's DMA buffer. |
| `sdl/qgl_sdl.c` | `linux/qgl_linux.c` | Populates the `qgl*` function-pointer table via `dlopen` of `/System/Library/Frameworks/OpenGL.framework/OpenGL`. (`SDL_GL_GetProcAddress` is unusable here: it returns NULL until an SDL GL window exists, and the engine resolves the table before window creation — found in the final review.) |

### 3.2 Reused without change
- **Portable engine:** `client/`, `server/`, `qcommon/`, `ref_gl/gl_*.c`,
  `game/`.
- **Linux platform files that are already portable:** `linux/sys_linux.c`
  (tiny arm64 fix, §4), `linux/net_udp.c`, `linux/q_shlinux.c`,
  `linux/glob.c`, `linux/vid_menu.c`.
- **CD:** `null/cd_null.c` (no CD audio on this target; satisfies the `cd`
  interface with stubs).

---

## 4. Changes to original sources (fixup-only)

These are the **only** edits to pre-existing files. Each is minimal.

1. **`linux/sys_linux.c`** — add an arm64 branch to the game-library selector
   so it resolves `gamearm64.so` (currently only `__i386__`/`__alpha__`, with
   `#error Unknown arch` otherwise). Everything else in this file is
   POSIX-portable and stays.
2. **OpenGL header selection** — `ref_gl`/`qgl` headers must include
   `<OpenGL/gl.h>` (or rely on `SDL_opengl.h`) under `__APPLE__` instead of
   `<GL/gl.h>`. Guarded by `#ifdef __APPLE__`.
3. **Implicit-declaration cleanups** — any `-Wimplicit-function-declaration`
   errors that block the build are fixed with the smallest correct prototype or
   include. No behavioral change.

No engine logic, renderer math, game code, or file-format code is modified.

---

## 5. Build system (root `Makefile`)

Styled after the reference "3AM Reel" Makefile: a header comment, a variables
block, `.PHONY`, self-documenting `help` built from `## ` comments with
grep/awk, and numbered stage targets.

### Variables
```
CC ?= cc                       # Apple clang
ARCH = arm64
SHLIBEXT = so
BUILD_DIR = build
SDL_CFLAGS  := $(shell sdl3-config --cflags 2>/dev/null || pkg-config sdl3 --cflags)
SDL_LIBS    := $(shell sdl3-config --libs   2>/dev/null || pkg-config sdl3 --libs)
BASE_CFLAGS = -Dstricmp=strcasecmp
```

### Targets (numbered pipeline)
| # | Target | Purpose |
|---|---|---|
| 0 | `help` | Self-documenting list from `## ` comments |
| 1 | `install` | Ensure SDL3 is present (`brew install sdl3` if missing) |
| 2 | `data` | Verify `baseq2/pak0.pak` exists; print where to put game data if not |
| 3 | `build` | Compile `quake2`, `ref_gl.so`, `gamearm64.so` into `build/` |
| 4 | `run` | Launch `./build/quake2 +set vid_ref gl` |
| — | `clean` | Remove `build/` |

`all` = `data build`. Default goal = `build`.

### Link notes
- Executable and both bundles compile with `-fPIC`.
- `ref_gl.so` and `gamearm64.so` link as macOS bundles:
  `-bundle -undefined dynamic_lookup` (symbols resolve back against the host
  process at `dlopen` time). They keep the `.so` extension so the engine's
  hard-coded `ref_%s.so` / `gamearm64.so` names load unchanged.
- Objects for bundles get `-fPIC`; the executable links `$(SDL_LIBS) -ldl -lm`.

---

## 6. Runtime behavior

### Windowed mode, no cursor lock
- `glw_sdl.c` creates the window with `SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE`
  and **does not** set `SDL_WINDOW_FULLSCREEN`. In v1, `GLimp_SetMode` always
  produces a windowed mode regardless of the `fullscreen` flag it is passed.
- The window is created **without** `SDL_SetWindowMouseGrab` and without
  `SDL_SetRelativeMouseMode`. Cursor stays free and can leave the window.

### Input model (`in_sdl.c`)
- Keyboard: translate `SDL_EVENT_KEY_DOWN/UP` scancodes to Quake `K_*` codes;
  deliver through the `Key_Event_fp` set in `KBD_Init`.
- Mouse look: accumulate `SDL_EVENT_MOUSE_MOTION` `xrel/yrel` into the view
  angles **without** enabling SDL relative/grab mode. Because the cursor is not
  locked, mouse-look is bounded by on-screen cursor travel — this is an accepted
  consequence of the no-grab requirement.
- Mouse buttons map to attack/strafe/etc. via `SDL_EVENT_MOUSE_BUTTON_*`.
- `RW_IN_Activate(active)` is a deliberate no-op in v1: with no grab, SDL already scopes event delivery to the focused window.

### Audio (`snd_sdl.c`)
- Open an SDL3 audio stream at the engine's requested rate/channels (typically
  22 kHz or 44.1 kHz, 16-bit). Each frame, `SNDDMA_BeginPainting`/`Submit` copy
  the mixed DMA buffer into the stream; SDL pulls it to the device.

### GL loader (`qgl_sdl.c`)
- Fill the `qgl` table by `dlopen`-ing
  `/System/Library/Frameworks/OpenGL.framework/OpenGL` and `dlsym`-ing each
  entry point. The handle is process-lifetime (never `dlclose`d, so GL is not
  unloaded across `vid_restart`). Extension entry points not present on macOS
  are left `NULL`; the renderer already null-checks optional extensions.
- Rationale (found in final review): `SDL_GL_GetProcAddress` returns `NULL`
  until an SDL GL window exists, but the engine resolves the `qgl` table in
  `QGL_Init` *before* `GLimp_SetMode` creates the window — so an
  SDL-based loader fills the table with NULLs. Framework `dlopen` is
  order-independent and keeps `ref_gl/gl_rmain.c` untouched.

---

## 7. Error handling

- **Missing game data:** `make data` exits non-zero with a clear message if
  `baseq2/pak0.pak` is absent; `make run` depends on it so the user is told
  before the engine starts.
- **Renderer load failure:** `VID_LoadRefresh` prints the `dlerror()`/SDL error
  and the engine falls back to dropping the console, mirroring existing
  behavior — no new crash path.
- **GL context failure:** `GLimp_Init`/`GLimp_SetMode` return failure; the
  renderer's existing retry/shutdown path reports it through `ri.Sys_Error`.
- **Audio device failure:** `SNDDMA_Init` returns false and the engine runs
  silently (existing `s_nosound` behavior), rather than aborting.
- All SDL calls that can fail are checked; failures surface via
  `ri.Con_Printf`/`Com_Printf` with `SDL_GetError()` text.

---

## 8. Testing

There is no automated test harness in this tree; verification is a short
manual ladder, each rung independently checkable.

1. **Compiles clean:** `make build` produces `build/quake2`, `build/ref_gl.so`,
   `build/gamearm64.so` with no errors.
2. **Launches:** `make run` opens a resizable window, renders the console /
   main menu, and logs show `ref_gl.so` loaded and GL initialized.
3. **Data check:** remove `baseq2/pak0.pak` → `make data` (and `make run`)
   fail with a helpful message; restore → launches again.
4. **Plays:** start a single-player map; confirm movement, weapons, sound, and
   that the cursor is never captured (can move it outside the window).
5. **No regressions in reused code:** a quick dedicated-server smoke
   (`./build/quake2 +set dedicated 1`) still starts, since `sys_linux.c` and
   networking are reused.

---

## 9. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Modern clang rejects 1997-era C (implicit decls, old idioms) | Fixup-only policy (§4); add `-Wno-...` only where a correct fix is larger than the warning it silences. |
| Original GL renderer assumes GL 1.x fixed-function | macOS still provides a legacy/compatibility GL profile; `qgl_sdl.c` only resolves entry points the renderer actually uses. |
| Renderer or game DLL expects symbols at link time | Build as `-bundle -undefined dynamic_lookup`; both are loaded into the host process. |
| Audio timing differs from OSS | SDL3 `SDL_AudioStream` handles buffering; `SNDDMA_GetDMAPos` derives position from SDL's queued/streamed byte count. |
| 64-bit pointer / `long` assumptions in old code | arm64 is LP64 like the supported `axp` (Alpha) target; reuse the portable files that already built for non-x86. |

---

## 10. Assumptions (stated explicitly)

- **Apple Silicon only.** The build targets `arm64` macOS. x86_64/Intel is not
  a v1 goal (the `#error Unknown arch` fix is added for arm64 specifically).
- **`sdl3-config`/`pkg-config` available.** SDL3 dev files are resolvable via
  `sdl3-config` or `pkg-config sdl3`; Homebrew's `sdl3` provides these.
- **`.so` bundles load via `dlopen` on macOS.** They do (Mach-O bundle/dylib),
  so the engine's hard-coded `.so` names need no change.
- **No mouse grab is acceptable for gameplay.** Mouse-look without a locked
  cursor is bounded; this is the explicit requirement and is accepted.
- **Full game data present.** The user supplies `baseq2/pak0.pak` (and any
  additional paks); the demo pak path is intentionally not used.
- **Single-player focus.** Multiplayer compiles but is not a v1 verification
  target.
