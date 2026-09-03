# Quake II — Apple Silicon Port

An educational fork of [id Software's Quake II](https://github.com/id-Software/Quake-2)
(GPL source, v3.19), simplified for a native Apple Silicon (arm64) macOS port:
SDL3 for video/input/audio, OpenGL renderer, all other platform backends removed.

Built for educational purposes only.

Development conventions and agent instructions live in [`AGENTS.md`](AGENTS.md).

**This repository does not ship the game data.** You must add the base game files
from a retail Quake II installation yourself.

## Requirements

- macOS on Apple Silicon (arm64)
- Xcode Command Line Tools (`cc`, `make`)
- SDL3 — `brew install sdl3`

## Game data

Copy the base game files into `baseq2/` at the repository root:

```
baseq2/
├── pak0.pak     required — base game
├── pak1.pak     optional — The Reckoning
├── pak2.pak     optional — Ground Zero
├── players/     player models and skins
└── textures/    optional — PNG/TGA/JPG texture overrides (see below)
```

Only `pak0.pak` is required to play. Saves, screenshots, and configs also live
under `baseq2/` and are git-ignored.

## Build and run

```
make install   # check toolchain and SDL3
make build     # compile engine, renderer (ref_gl), and game DLL
make run       # launch, windowed GL
```

`make all` checks game data then builds; `make verify-load` smoke-tests the
renderer and game bundles without game data; `make smoke` boots `+map base1`
and asserts the spawn/connect/bsp markers (needs game data); `make help`
lists every target.

The engine reads `baseq2/` from the repository root, and the game DLL is built
straight into `baseq2/gamearm64.so`.

## Mouse controls

The mouse looks by default (X turns, Y looks up/down). Right-click toggles
**walk mode**: the view levels to the horizon and the cursor steers the
player like a joystick centered on the screen — hold it above center to
keep walking forward, below to back up, left/right to strafe (farther from
center = faster, small dead zone at center). A fixed `WALK`/`LOOK` button at
the bottom center of the HUD offers the same toggle for mice without a
right button; it is drawn at 70% opacity while the mouse is moving and
turns solid once the mouse rests. While walking, a left click returns
to look mode without shooting; otherwise left click shoots, and a left
double-click also jumps. The wheel switches weapons. `walkmode` is
archived, toggles apply immediately.

## Texture overrides

The renderer looks for hi-res replacements of the world textures on disk
before falling back to the `.wal` inside the paks. For a surface that uses
`textures/e1u1/floor1_1.wal` it probes, in order:

```
baseq2/textures/e1u1/floor1_1.png
baseq2/textures/e1u1/floor1_1.tga
baseq2/textures/e1u1/floor1_1.jpg
```

The first file that decodes is uploaded at its own resolution. Texture
coordinates still use the original `.wal` size, so a 4× image covers the
same wall area. Any size works, but non-power-of-two images are resampled at load and,
like the originals, rounded *down* to the next power of two (a 384×128
override of a 96×32 original becomes 256×128), exactly as the engine
already treats the 33 original textures that are not power-of-two.
Power-of-two overrides skip that step. File names must be lowercase.

To dump the originals as PNGs, the starting point for recreating them:

```
brew install uv          # once
make textures            # writes baseq2/textures/**/*.png, skips files that exist
make textures FORCE=1    # re-extract the originals over everything
```

`baseq2/textures/` is git-ignored: the extracted files are id Software's
data. Palette index 255 is written as alpha 0.

To recreate the extracted textures with generative AI, the batch kit lives on
the GPU box at `~/comfy/batch_regen_q2/` (design in
`docs/superpowers/specs/2026-09-02-quake2-texture-recreation-kit-design.md`).
Move the data with:

```
rsync -a --delete baseq2/textures/ felipe@192.168.1.1:comfy/data/quake2/textures/
rsync -a felipe@192.168.1.1:comfy/data/quake2/textures-ai/ baseq2/textures/
```

The second command overwrites the originals in place, which is what the
renderer probes; `make textures FORCE=1` restores them from the paks.

Cvars (both archived; changes apply on `vid_restart`):

| cvar                  | default | meaning                                                  |
|-----------------------|---------|----------------------------------------------------------|
| `gl_textureoverride`  | `1`     | `0` disables the on-disk probe                           |
| `gl_override_maxsize` | `1024`  | largest edge uploaded for world textures (≤ driver max)  |

The extractor lives in `tools/` and shares no code with the engine.
`make tools-test` runs its tests; `make test-ref` runs the renderer's
override-loader host test.
