# Quake II — Apple Silicon Port

An educational fork of [id Software's Quake II](https://github.com/id-Software/Quake-2)
(GPL source, v3.19), simplified for a native Apple Silicon (arm64) macOS port:
SDL3 for video/input/audio, OpenGL renderer, all other platform backends removed.

Built for educational purposes only.

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
└── players/     player models and skins
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
renderer and game bundles without game data; `make help` lists every target.

The engine reads `baseq2/` from the repository root, and the game DLL is built
straight into `baseq2/gamearm64.so`.
