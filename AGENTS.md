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
| `client/` | Client (cl_*), sound (snd_*), menu/keys/console, plus sv_* server sources and shared headers (`client.h`, `ref.h`, `sound.h`) |
| `server/` | Server headers + sv_* sources compiled into the executable |
| `qcommon/` | Engine core: `common.c`, `cmd.c`, `cvar.c`, `files.c`, `cmodel.c`, `pmove.c`, crc/md4, `qcommon.h`, `qfiles.h` |
| `game/` | Game DLL sources: game logic (g_*), player (p_*), monsters (m_*.c + ModelGen-generated m_*.h), `q_shared.c/h` |
| `ref_gl/` | OpenGL renderer (gl_*) + texture-override loader (`gl_override.c`), `stb_image.h`, host tests in `ref_gl/tests/` |
| `sdl/` | SDL3 platform layer: `vid_sdl.c`, `snd_sdl.c`, `in_sdl.c`, `glw_sdl.c`, `qgl_sdl.c` (GL dispatch loader), `verify_load.c` |
| `linux/` | POSIX layer kept from the Linux port: `sys_linux.c`, `net_udp.c`, `glob.c`, `q_shlinux.c`, `vid_menu.c` |
| `tools/` | Standalone Python (uv-managed) pak `.wal` → PNG texture extractor + pytest suite |
| `docs/superpowers/specs/` | Design specs: the SDL3 port, cleanups rounds 1–4, texture overrides, texture recreation kit |
| `baseq2/` | **User territory** — game data (paks, players/, textures/, saves). Contents selectively git-ignored; only `hudtest.cfg` is tracked |
| `build/` | Build outputs: `quake2`, `ref_gl.so`, `verify_load` (git-ignored; the game DLL installs to `baseq2/gamearm64.so`) |
| `.superpowers/` | Transient SDD agent workspaces (`sdd/<date>-<topic>/` briefs, reports, review diffs). Git-ignored — never commit |

## Architecture Notes (load-bearing)

- **Three link units:**
  1. the executable (`build/quake2`) — client + server + qcommon + `linux/` + the SDL platform objects from `sdl/`;
  2. `build/ref_gl.so` — renderer bundle;
  3. `baseq2/gamearm64.so` — game DLL, dlopened from `<cwd>/baseq2/`.
  Bundles use `-bundle -undefined dynamic_lookup` (flat namespace, symbols resolve against the host executable at load time).
- **Shared sources compile into multiple units:** `game/q_shared.c` builds into **all three** units (`build/client/q_shared.o`, `build/ref_gl/q_shared.o`, `build/game/q_shared.o`); `game/m_flash.c` into the game DLL and the executable. Code that looks dead in one unit may be alive in another.
- **Renderer/game ABI:** the engine dlopens both bundles and resolves entry points (`GetRefAPI`, `GetGameAPI`, `RW_IN_*` exports). `sdl/verify_load.c` encodes the expected export contract.
- **Protected items (survived 4 cleanup rounds, do not disturb without a spec):** the `qgl*` GL dispatch table + 7 `qgl*EXT` pointers in `sdl/qgl_sdl.c`; the byte-swap cluster in `game/q_shared.c` (`Swap_Init`, `_Big*/_Little*`, wrappers — dead in the game DLL only, alive in the other two units); `RW_IN_Activate_fp` in `sdl/vid_sdl.c`; `R_Init()` returning `true` in `ref_gl/gl_rmain.c`.
- **Texture overrides:** the renderer probes `baseq2/textures/<path>.{png,tga,jpg}` before the pak `.wal`; cvars `gl_textureoverride`, `gl_override_maxsize`. See README and `2026-09-01-texture-overrides-design.md`.

## Building and Running

Requirements: macOS/arm64, Xcode CLT (`cc`, `make`), `brew install sdl3`
(optional: `brew install uv` for the texture tools).

`make help` lists every target with its description (the Makefile's `##`
comments are the source of truth). Targets agents use most: `build`,
`verify-load`, `run`, `clean`, `test-ref`, `tools-test`, `textures`.

**Regression gate used by all cleanup rounds:**

```sh
make clean && make verify-load                 # must exit 0 (verify-load implies build)
./build/quake2 +set developer 1 +map base1     # runtime smoke
```

For iterative work an incremental `make verify-load` (header-dependency-aware
via `-MMD -MP`) suffices; run the full clean gate for final verification.

Smoke assertions: `SpawnServer: base1`, `client_connect`, `maps/base1.bsp`
loaded, no `FATAL`/`ShutdownError`. (The attract-loop launch can hit a
CoreAudio `-66681` environment error on some machines — proven not a code
regression; the `+map base1` smoke is the deterministic substitute.)

**Known benign build noise:** an `ld` warning
`reducing alignment of section __DATA,__common`, and ~200+ pre-existing
`-Wall` warnings (mostly `-Wmissing-braces` in monster frame-array
initializers, `-Wpointer-sign` in cl_main). Do not "fix" these in
unrelated changes.

## Development Conventions

- **id-era C style:** tabs, K&R-ish braces, `/* banner */` function comment blocks, `qboolean`/`vec3_t` typedefs from `game/q_shared.h`. Match surrounding code exactly.
- **Minimal blast radius:** prefer the narrowest correct change; reuse existing code paths before adding new ones.
- **Zero behavior change for cleanup work:** every removal must be *provably* dead — `grep -rnw SYMBOL` must show declarations/writes only, no reads — and remember symbols may be alive in another link unit. `FRAME_*` constants are explicit-value `#define`s (ModelGen headers): removal is value-safe, but they are per-definition (the same name can exist in several monster headers for different translation units).
- **Git discipline:** stage explicit paths only — never `git add -A` or `git commit -a`. `baseq2/` game data (paks, saves, configs, textures) must never enter a commit — `baseq2/textures/` is id Software data, and the texture recreation kit's batch data lives off-repo. Push only when explicitly asked. Feature work lands on a branch, gated, then `--no-ff` merged to `main`.
- **Specs before rounds:** larger changes follow the pattern in `docs/superpowers/specs/` — dated design doc committed on the branch, then per-unit commits, then the gate.
