# Quake II — Texture Extraction and Hi-Res Overrides — Design

**Date:** 2026-09-01
**Status:** Approved (brainstorming, approach A)
**Goal:** Let the original Quake II world textures be extracted from the pak
files into `baseq2/textures/` as PNGs, and let the OpenGL renderer use any
PNG/TGA/JPG found there in place of the pak `.wal`. The extracted PNGs are the
input set for recreating the textures at higher resolution with generative AI;
the recreated files drop into the same folder and the game picks them up.

Companion specs:
`docs/superpowers/specs/2026-08-31-quake2-sdl3-apple-silicon-design.md`,
`docs/superpowers/specs/2026-09-01-quake2-apple-silicon-cleanup-design.md`

---

## 1. Scope and constraints

- **In scope:**
  - A standalone extractor in `tools/` that turns every `textures/**/*.wal`
    in the game paks into a PNG under `<gamedir>/textures/`.
  - A renderer change so `GL_LoadWal` prefers an on-disk PNG/TGA/JPG
    override at any resolution (power-of-two recommended), while keeping
    world texture coordinates correct.
  - Raising the renderer's 256×256 upload cap for those overrides.
  - Tests for both halves, Makefile targets, README and `.gitignore` updates.
- **Out of scope:** overrides for model skins, HUD pics, or skies; changes to
  the filesystem search order in `qcommon/files.c`; anisotropic filtering or
  other quality settings; producing the AI textures themselves; metadata
  sidecars.
- **Standing constraints:**
  - `tools/` must not include or link any engine code (decoupled).
  - The regression gate from earlier specs still applies:
    `make clean && make build && make verify-load`.
  - Original `.wal` rendering must be bit-for-bit unchanged when no override
    exists or `gl_textureoverride` is 0.
  - **Never** `git add -A` or `git commit -a`. `baseq2/textures/` holds
    extracted id Software data and is git-ignored.

## 2. Findings that shape the design

Verified against the tree and `baseq2/pak0.pak` on 2026-09-01:

1. **Paks beat loose files.** `FS_AddGameDirectory` (`qcommon/files.c`)
   pushes the directory first and each `pak0..pak9` on top, so
   `FS_FOpenFile` finds pak entries before loose files in the same game dir.
   A loose `.wal` can never override a pak `.wal`; overrides must use an
   extension the pak does not contain.
2. **Only WAL, PCX, TGA decode today.** `GL_FindImage` dispatches on the
   extension; there is no PNG or JPEG decoder in the tree.
3. **Texture coordinates use the source size.** `GL_BuildPolygonFromSurface`
   (`ref_gl/gl_rsurf.c:1487`) divides `s`/`t` by `image->width`/`height`.
   A 4× override must report the *original* WAL size there or every wall
   tiles four times too small. Warp surfaces (`gl_warp.c`) use a fixed
   1/64 scale and are unaffected.
4. **256×256 cap.** `GL_Upload32` clamps `scaled_width/height` to 256 and
   uses `unsigned scaled[256*256]` / `paletted_texture[256*256]` stack
   buffers; `GL_ResampleTexture` has `p1[1024], p2[1024]` row tables.
5. **Transparency is palette index 255.** `GL_Upload8` sets alpha 0 and
   copies RGB from an adjacent non-255 texel to avoid fringes.
   `Draw_GetPalette` reads the palette from `pics/colormap.pcx`.
6. **Surface flags and animation chains come from the BSP**, not the WAL
   (`Mod_LoadTexinfo`), so an override needs pixels only.
7. **Inventory:** pak0 has 3307 files, 2118 `.wal`, all under `textures/`
   in 12 folders (`e1u1` … `e3u3`, `test`, plus `REGION.wal`). A few names
   are mixed case (`e1u1/PIP04_4.wal`). Pak lookup is case-insensitive
   (`Q_strcasecmp`); loose-file lookup is whatever the OS does.
8. **Tooling on this machine:** Python 3.12.12, uv 0.12.1, Pillow 12.3.
   `vgio 1.3.0` (PyPI, `vgio.quake2.pak.PakFile`, `vgio.quake2.wal.Wal`)
   opens pak0 and parses a WAL; `Wal.pixels` holds all four mip levels
   concatenated, mip 0 first. Pillow reads `colormap.pcx` as mode `P` with
   a 256-entry palette. `stb_image.h` v2.30 decodes PNG/JPEG/TGA from
   memory with `stbi_load_from_memory(..., STBI_rgb_alpha)`.

## 3. Extractor: `tools/`

### 3.1 Layout

```
tools/
├── pyproject.toml          # deps: vgio==1.3.0, pillow; dev: pytest
├── uv.lock
├── extract_textures.py     # the tool, single file
└── tests/
    └── test_extract_textures.py
```

Run as `uv run --project tools tools/extract_textures.py`. No venv setup, no
engine code, nothing under `tools/` is compiled into the game.

### 3.2 Command line

```
extract_textures.py [--gamedir DIR] [--out DIR] [--force]
  --gamedir  game data directory containing pak*.pak   (default: baseq2)
  --out      output root; textures land in <out>/textures  (default: --gamedir)
  --force    overwrite existing files (default: skip them)
```

### 3.3 Behaviour

1. Open `pak0.pak` … `pak9.pak` in `--gamedir`, in numeric order, skipping
   missing ones. Build a `name → (pak, entry)` map where later paks replace
   earlier entries, mirroring engine precedence. If no pak is found, print a
   clear message and exit 2.
2. Load the 256-colour palette from `pics/colormap.pcx` (from the winning
   pak) with Pillow: `Image.open(...).getpalette()` → 768 ints.
3. For each entry matching `textures/**/*.wal`:
   - Parse with `vgio.quake2.wal.Wal`; take `pixels[:width*height]` (mip 0).
   - Destination: `<out>/textures/<relative path with .wal → .png>`, path
     **lowercased** (`textures/e1u1/PIP04_4.wal` → `textures/e1u1/pip04_4.png`).
   - If the destination exists and `--force` is not set: count as skipped.
   - Otherwise build the image:
     - If no texel is index 255: mode `RGB`, palette lookup per texel.
     - Else: mode `RGBA`; index 255 → alpha 0 and RGB copied from the first
       non-255 neighbour in the engine's order (above, below, left, right),
       falling back to palette entry 0. All other texels alpha 255.
   - `mkdir -p` the parent and save as PNG.
4. A WAL that fails to parse is reported to stderr with its name, counted as
   failed, and the run continues.
5. Print `extracted N, skipped N, failed N`; exit 1 if failed > 0, else 0.

### 3.4 Not included (deliberate)

No metadata sidecars (see finding 6), no upscaling, no folder filter, no
JPG output, no writing of `.wal` files (they would be shadowed by the pak).

## 4. Renderer: override path in `ref_gl/`

### 4.1 New files

- `ref_gl/stb_image.h` — vendored verbatim, v2.30, public domain / MIT
  (dual). Bumping it is a file swap.
- `ref_gl/gl_override.c` — the only file that includes `stb_image.h`. It
  defines `STB_IMAGE_IMPLEMENTATION` with `STBI_ONLY_PNG`, `STBI_ONLY_JPEG`,
  `STBI_ONLY_TGA`, and exposes (declared in `gl_local.h`):

```c
// textures/e1u1/PIP04_4.wal + "png" -> textures/e1u1/pip04_4.png
// returns false if name lacks a ".wal" suffix or the result would not fit
qboolean GL_OverridePath (const char *walname, const char *ext,
                          char *out, size_t outsize);

// probes .png, .tga, .jpg through ri.FS_LoadFile; decodes the first hit
// to RGBA. returns pixels to release with GL_FreeOverride, or NULL.
byte *GL_LoadOverride (const char *walname, int *width, int *height);
void  GL_FreeOverride (byte *pixels);
```

`GL_LoadOverride` uses `stbi_load_from_memory(buf, len, w, h, NULL,
STBI_rgb_alpha)`, frees the file buffer with `ri.FS_FreeFile`, and returns
the stb buffer; `GL_FreeOverride` wraps `stbi_image_free` so `gl_image.c`
never touches stb directly. A decode failure logs
`PRINT_ALL "GL_LoadOverride: bad %s: %s"` with `stbi_failure_reason()` and
tries the next extension. Going through `ri.FS_LoadFile` (not `fopen`)
keeps the engine search path in charge, so overrides could ship in a pak
later.

### 4.2 Hook in `GL_LoadWal` (`ref_gl/gl_image.c`)

```
mt = ri.FS_LoadFile(name)                     may be NULL
pic = gl_textureoverride->value ? GL_LoadOverride(name, &ow, &oh) : NULL
if pic:
    image = GL_LoadPic(name, pic, ow, oh, it_wall, 32)
    if mt: image->width = mt->width; image->height = mt->height
    GL_FreeOverride(pic); if mt: ri.FS_FreeFile(mt)
    return image
if !mt:
    print "GL_FindImage: can't load"; return r_notexture   (unchanged)
today's 8-bit upload from mt                              (unchanged)
```

- `image->name` stays the `.wal` name, so `GL_FindImage`'s cache lookup,
  `Mod_LoadTexinfo`, and animation chains are untouched.
- Resetting `width/height` to the WAL's values is what keeps
  `gl_rsurf.c:1487` correct (finding 3). `upload_width/height` still
  reflect the real upload, so `imagelist` shows the override size.
- If the `.wal` is missing but an override exists, the override's own
  dimensions are used and the existing "can't load" message is suppressed.
- Any override failure falls back silently to the WAL (after the PRINT_ALL
  line from 4.1). Never `Sys_Error` because of an override.

### 4.3 Raising the cap in `GL_Upload32` / `GL_ResampleTexture`

- Replace `unsigned scaled[256*256]` and `unsigned char
  paletted_texture[256*256]` with heap buffers sized
  `scaled_width * scaled_height` (`malloc`/`free` around the upload; the
  paletted buffer only when the paletted path is taken).
- Replace the literal 256 clamp with `gl_override_maxsize->value`, clamped
  at each use to `[1, gl_config.max_texsize]`, where `gl_config.max_texsize`
  is filled by `qglGetIntegerv(GL_MAX_TEXTURE_SIZE, ...)` in
  `GL_InitImages`.
- `GL_ResampleTexture`: allocate `p1`/`p2` with `outwidth` entries instead
  of the fixed 1024.
- Everything else (power-of-two rounding, `gl_round_down`, `gl_picmip`,
  mip generation, paletted path, filters) is unchanged. Original WALs are
  ≤256, so at the default cap their uploads are identical to before.
- Memory note: at the default cap a 1024² RGBA texture with mips is ~5.3 MB;
  a typical map's 150–300 world textures at 4× original size land in the
  hundreds of MB, fine for Apple Silicon unified memory. `gl_override_maxsize
  512` or `gl_picmip 1` halve it.

### 4.4 Cvars (`R_Register` in `gl_rmain.c`)

| cvar                  | default | flags        | meaning                                   |
|-----------------------|---------|--------------|-------------------------------------------|
| `gl_textureoverride`  | `1`     | CVAR_ARCHIVE | 0 disables the on-disk probe entirely     |
| `gl_override_maxsize` | `1024`  | CVAR_ARCHIVE | largest edge uploaded for world textures  |

Both take effect on the next `vid_restart`. A plain map change is not
enough: `GL_FindImage` returns cached images by name, and textures shared
between the old and new map are never reloaded.

### 4.5 Case handling

`GL_OverridePath` lowercases the whole path and the extractor writes
lowercase names, so the two agree regardless of what case the BSP texinfo
or the pak directory uses. Default APFS is case-insensitive anyway; this
keeps it correct on case-sensitive volumes too.

## 5. Testing

### 5.1 Extractor — `tools/tests/test_extract_textures.py` (pytest)

A fixture writes a synthetic pak into `tmp_path` using `struct` for the
pak header/directory and `vgio`/Pillow for contents: a `pics/colormap.pcx`
with a known palette, one opaque WAL, one WAL with index-255 texels, one
with a mixed-case name, plus a `pak1.pak` redefining one texture. Tests:

- output paths mirror the pak layout and are lowercased;
- pixel values equal the palette entries; only mip 0 is used;
- index 255 → alpha 0 with neighbour RGB; opaque textures are `RGB`, not `RGBA`;
- `pak1` wins over `pak0`;
- existing files are skipped by default, replaced with `--force`;
- a corrupt WAL is reported, counted, run continues, exit code 1;
- `--out` redirects the root.

Makefile: `tools-test` → `uv run --project tools pytest tools/tests`.
End-to-end check after implementation: run against the real `baseq2/` and
confirm 2118 PNGs under `baseq2/textures/`.

### 5.2 Renderer — `ref_gl/tests/test_override.c` (host binary, no GL)

`gl_override.c` depends only on `ri.FS_LoadFile`/`ri.FS_FreeFile`,
`ri.Con_Printf`, and stb. The test provides a stub `ri` that serves files
from a temp directory and asserts:

- `GL_OverridePath` lowercases, swaps `.wal` → `.<ext>`, rejects names
  without `.wal`, rejects results that do not fit `outsize`;
- an embedded 2×2 PNG byte array decodes to the expected RGBA and size;
- probe order is PNG, then TGA, then JPG; nothing present → NULL;
- a corrupt PNG → NULL and the next extension is tried.

Makefile: `test-ref` builds `build/test_override` from
`ref_gl/tests/test_override.c` + `build/ref_gl/gl_override.o` and runs it.
The `GL_Upload32` buffer change is covered by the existing gate
(`make clean && make build && make verify-load`) and the play test.

### 5.3 Play test (user-run, needs the window)

1. `make textures && make run`, load a map: must look identical to before.
2. Replace one extracted PNG with a 4× version carrying a visible marker:
   it must tile at the same world scale; `imagelist` shows the larger
   upload size.
3. `gl_textureoverride 0; vid_restart`: the original returns.
4. `gl_override_maxsize 256; vid_restart`: the marker texture is downscaled.

## 6. Housekeeping

- `.gitignore`: add `baseq2/textures/`, `tools/.venv/`, `__pycache__/`,
  `.pytest_cache/`.
- `Makefile`: `gl_override.o` joins `REF_CORE_OBJS` (the `vpath` already
  covers `ref_gl`); new self-documenting targets `textures`, `tools-test`,
  `test-ref`; `install` reports whether `uv` is present without failing.
- `README.md`: a "Texture overrides" section — extract command, output
  location, accepted formats and probe order, power-of-two note, the two
  cvars, skip-vs-`--force`.

## 7. Rollout order

1. Extractor with its tests (independent, immediately useful for the AI work).
2. Renderer override, tested with real extracted PNGs.
3. README, `.gitignore`, Makefile polish; final gate and play test.
