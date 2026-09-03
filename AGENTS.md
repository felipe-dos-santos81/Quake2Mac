# AGENTS.md — Quake II Apple Silicon Port

## Project Overview

An educational fork of id Software's **GPL Quake II** source (v3.19 per
README), ported to native **Apple Silicon (arm64) macOS** with **SDL3** for
video/input/audio and an OpenGL renderer. All non-Apple platform backends
(Win32, X11, OSS/ALSA, software renderer, other CPUs) were deliberately
removed. The game ships **without game data**: `baseq2/pak0.pak` from a
retail Quake II install must be provided locally (it is git-ignored).

The codebase is C (id-era style), built with Apple clang + GNU make, plus a
small standalone Python tool (`tools/`) for texture extraction.

## Repository Layout

| Path | Contents |
|---|---|
| `client/` | Client core (`main.c`, `input.c`) with `net/` (parse, predict, ents, tents, fx, newfx), `screen/` (scrn, view, cinematic, inv, console, keys, menu, qmenu), `sound/` (dma, mem, mix) subfolders; shared headers (`client.h`, `ref.h`, `vid.h`, `input.h`) |
| `server/` | Server sources compiled into the executable: `main.c`, `init.c`, `send.c`, `user.c`, `world.c`, `ents.c`, `game.c`, `ccmds.c` + `server.h` |
| `qcommon/` | Engine core: `common.c`, `cmd.c`, `cvar.c`, `files.c`, `cmodel.c`, `pmove.c`, crc/md4, `qcommon.h`, `qfiles.h`, `anorms.h` (shared vertex-normal table) |
| `game/` | Game DLL sources: game logic at the root, `player/`, `monsters/` (.c + ModelGen-generated .h), `q_shared.c/h` |
| `ref_gl/` | OpenGL renderer + texture-override loader (`override.c`), `stb_image.h`, host tests in `ref_gl/tests/` |
| `platform/` | Platform layer: `posix/` (`sys.c`, `udp.c`, `glob.c`, `shared.c`, `vid_menu.c`) + `sdl/` (`vid.c`, `sound.c`, `input.c`, `glw.c`, `qgl.c` GL dispatch loader), plus `verify_load.c` at the platform root |
| `tools/` | Standalone Python (uv-managed) pak `.wal` → PNG texture extractor + pytest suite |
| `docs/superpowers/specs/` | Design specs: the SDL3 port, cleanups rounds 1–4, texture overrides, texture recreation kit, folder restructure; plus the 2026-09-03 loading-plaque race audit and warning-cleanup round 5 |
| `baseq2/` | **User territory** — game data (paks, players/, textures/, saves). Contents selectively git-ignored; only `hudtest.cfg` is tracked |
| `build/` | Build outputs; object paths mirror the source tree (`client/main.c` → `build/client/main.o`): `quake2`, `ref_gl.so`, `verify_load` (git-ignored; the game DLL installs to `baseq2/gamearm64.so`) |
| `.superpowers/` | Transient SDD agent workspaces (`sdd/<date>-<topic>/` briefs, reports, review diffs). Git-ignored — never commit |

## Architecture Notes (load-bearing)

- **Three link units:**
  1. the executable (`build/quake2`) — client + server + qcommon + the platform layer (`platform/posix/` + `platform/sdl/`);
  2. `build/ref_gl.so` — renderer bundle;
  3. `baseq2/gamearm64.so` — game DLL, dlopened from `<cwd>/baseq2/`.
  Bundles use `-bundle -undefined dynamic_lookup` (flat namespace, symbols resolve against the host executable at load time).
- **Shared sources compile once and link into multiple units:** `game/q_shared.c` builds **once** as `build/game/q_shared.o` and links all three units; `game/monsters/flash.c` (`build/game/monsters/flash.o`) into the executable and the game DLL; `platform/posix/{shared,glob}.c` into the executable and `ref_gl.so`. This is safe because CFLAGS is uniform across units — keep it that way, or reintroduce per-unit object directories. Code that looks dead in one unit may be alive in another.
- **Renderer/game ABI:** the engine dlopens both bundles and resolves entry points (`GetRefAPI`, `GetGameAPI`, `RW_IN_*` exports). `platform/verify_load.c` encodes the expected export contract.
- **Protected items (survived 4 cleanup rounds, do not disturb without a spec):** the `qgl*` GL dispatch table + 7 `qgl*EXT` pointers in `platform/sdl/qgl.c`; the byte-swap cluster in `game/q_shared.c` (`Swap_Init`, `_Big*/_Little*`, wrappers — dead in the game DLL only, alive in the other two units); `RW_IN_Activate_fp` in `platform/sdl/vid.c`; `R_Init()` returning `true` in `ref_gl/rmain.c`.
- **Texture overrides:** the renderer probes `baseq2/textures/<path>.{png,tga,jpg}` before the pak `.wal`; cvars `gl_textureoverride`, `gl_override_maxsize`. See README and `2026-09-01-texture-overrides-design.md`.
- **Walk/look mouse scheme (2026-09-03):** look is the default; right click or the HUD `WALK`/`LOOK` button toggles the archived `walkmode` cvar and levels pitch on entry; left double-click jumps, and a left click while walking exits walk mode (consumed, no shot). Walk mode is position-driven (cursor offset from screen center, 8 px dead zone, max 400, scaled by `sensitivity`) because the never-grabbed cursor's motion deltas die at the window edge — do not "simplify" it back to deltas. `RW_IN_CursorPos` is part of the ref export contract; the ref bundle reads the renderer's `vid` size, not the client's `viddef`. `+mlook`/`freelook`/`lookstrafe` are deliberate no-ops kept for old configs, and the wheel's weapon-switch binds are deliberately alive — do not "clean" either.
- **Mouse-operable menus (2026-09-03):** every menu supports hover, click, slider drag, right-click-back, and the wheel. Draw-time rect capture: each `qmenu` draw function records `hit_x/hit_y/hit_w/hit_h` on its `menucommon_s` (same pattern as `SCR_DrawWalkButton`), `Menu_Draw` runs a per-frame mouse pass, and `Default_MenuKey` owns the pointer cases — `VID_MenuKey` delegates to it; the main menu, credits, and quit are special-cased in `menu.c`. Hover is silent; mouse and keyboard share the one cursor index. The main menu's decorative `M_DrawCursor` hand was removed with keyboard-only selection; the OS cursor is the only pointer.

## Building and Running

Requirements: macOS/arm64, Xcode CLT (`cc`, `make`), `brew install sdl3`
(optional: `brew install uv` for the texture tools).

`make help` lists every target with its description (the Makefile's `##`
comments are the source of truth). Targets agents use most: `build`,
`verify-load`, `run`, `smoke`, `clean`, `test-ref`, `tools-test`, `textures`.

**Regression gate used by all cleanup rounds:**

```sh
make clean && make verify-load                 # must exit 0 (verify-load implies build)
make smoke                                     # runtime smoke: +map base1 spawn/connect/bsp assertions
```

For iterative work an incremental `make verify-load` (header-dependency-aware
via `-MMD -MP`) suffices; run the full clean gate for final verification.

Smoke assertions: `SpawnServer: base1`, `client_connect`, `maps/base1.bsp`
loaded, no `FATAL`/`ShutdownError`. (The attract-loop launch can hit a
CoreAudio `-66681` environment error on some machines — proven not a code
regression; the `+map base1` smoke is the deterministic substitute.)

**Build hygiene:** since the 2026-09-03 warning-cleanup round the tree
builds with zero `-Wall` warnings and no `ld` alignment warning
(`-fno-common` in BASE_CFLAGS retires the `__DATA,__common` note).
Keep it that way: new code must compile warning-free.

## Development Conventions

- **id-era C style:** tabs, K&R-ish braces, `/* banner */` function comment blocks, `qboolean`/`vec3_t` typedefs from `game/q_shared.h`. Match surrounding code exactly.
- **Root-relative includes:** every `#include "..."` is a repo-root-relative path (`-I.` is set in the Makefile). No bare-basename or `../` includes. Lint (both must print nothing): `grep -rn '#include "' --include='*.[ch]' client server qcommon game ref_gl platform | grep -v '#include ".*/'` (no bare basenames) and `grep -rn '#include "\.\./' --include='*.[ch]' client server qcommon game ref_gl platform` (no `../`).
- **Minimal blast radius:** prefer the narrowest correct change; reuse existing code paths before adding new ones.
- **Zero behavior change for cleanup work:** every removal must be *provably* dead — `grep -rnw SYMBOL` must show declarations/writes only, no reads — and remember symbols may be alive in another link unit. `FRAME_*` constants are explicit-value `#define`s (ModelGen headers): removal is value-safe, but they are per-definition (the same name can exist in several monster headers for different translation units).
- **Git discipline:** stage explicit paths only — never `git add -A` or `git commit -a`. `baseq2/` game data (paks, saves, configs, textures) must never enter a commit — `baseq2/textures/` is id Software data, and the texture recreation kit's batch data lives off-repo. Push only when explicitly asked. Feature work lands on a branch, gated, then `--no-ff` merged to `main`.
- **Specs before rounds:** larger changes follow the pattern in `docs/superpowers/specs/` — dated design doc committed on the branch, then per-unit commits, then the gate.
