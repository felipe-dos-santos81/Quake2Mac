# Quake II — Texture Recreation Kit (ComfyUI batch) — Design

**Date:** 2026-09-02
**Status:** Approved (brainstorming: faithful mode, gated seam pass, sibling kit)
**Goal:** Recreate the 2188 extracted world textures under `baseq2/textures/`
as photorealistic 4× versions with ComfyUI on the GPU box, keeping each
texture's layout and colours, keeping tileable textures tileable, and writing
them into a mirrored tree that drops straight into `baseq2/textures/` for the
renderer's override path.

Companion spec: `docs/superpowers/specs/2026-09-01-texture-overrides-design.md`
(the extractor that produces the input set and the renderer that consumes the
output).

---

## 1. Scope and constraints

- **In scope:**
  - A new kit `~/comfy/batch_regen_q2/` on the GPU box (`felipe@192.168.1.1`):
    driver, two API-format workflow templates, run script, README, offline
    tests.
  - Per-texture pipeline: classification, canvas planning, faithful render
    conditioned on the source, crop and downscale, seam measurement, gated
    seam repaint, verification before a file lands in the output tree.
  - Driver-side VLM prompting with retries and caching.
  - rsync commands for moving the input set to the box and the results back,
    documented in the kit README and in this repo's README.
- **Out of scope:** skins, HUD pics, skies and model textures (the override
  path does not load them); any change to the AITD kit `~/comfy/batch_regen/`;
  any change to the engine; a shared library between the two kits; upscalers
  other than the diffusion render; text and lettering fidelity on signs.
- **Standing constraints:**
  - The AITD kit is left byte-for-byte untouched. The new kit copies what it
    needs and owns its copies.
  - `~/comfy/` on the box is a git repository with unrelated unstaged
    changes. Only the new kit's own paths are ever staged there.
  - This repo receives only this spec and a README note. `baseq2/textures/`
    stays git-ignored. Never `git add -A` or `git commit -a`.
  - Every generated file is validated before it enters the output tree. A bad
    render is a failure, not a file.

## 2. Findings that shape the design

Verified on the box and against `baseq2/textures/` on 2026-09-02:

1. **Box.** RTX 5090, 32 GB. ComfyUI at `~/comfy/ComfyUI` (checkout of
   2026-08-26) running on `:8188` under
   `COMFY_PYTHON=/home/felipe/.pyenv/versions/comfy/bin/python` (Pillow and
   numpy present). Root disk 98 % full with 75 GB free. `~/comfy/.env` holds a
   DashScope key.
2. **AITD kit.** `~/comfy/batch_regen/` drives one API-format workflow per
   image: `LoadImage` → `ImageScale` → `HostedVLMAPI` → `TextEncodeQwenImageEdit`
   → union ControlNet (Canny) → `KSampler` → `SaveImage`, with a size contract
   checked before the move, resumable skips, `--verify`, and per-folder style
   anchors cached in the output tree. Its last log shows 10 in-graph VLM
   connection errors in 38 images, so VLM calls need retries.
3. **Nodes available** (server `object_info`): union types `tile`, `repaint`,
   `canny/lineart/anime_lineart/mlsd`, …; core `InpaintModelConditioning`,
   `SetLatentNoiseMask`, `ImageCompositeMasked`, `Canny`, `ImageScale`,
   `EmptyQwenImageLayeredLatentImage`. `LoadImage` returns a MASK equal to
   `1 − alpha` of an RGBA input.
4. **Models**: fp8 Qwen-Image-2512 UNET, `qwen_2.5_vl_7b` text encoder,
   `qwen_image_vae`, `Qwen-Image-2512-Fun-Controlnet-Union-2602`.
5. **Input set**: 2188 PNGs, all mode `RGB` (no index-255 texels in any world
   texture, so no alpha to carry). Sizes from 16×16 up to 320×128 and 128×192; 748 are 64×64,
   440 are 128×128, 308 are 32×32, 227 are 64×128, 127 are 16×16; long
   strips (64×16, 128×16, 16×128, 32×128, …) are trims. **Every dimension is
   a multiple of 16.** 33 are not power-of-two. 77 files carry a `+N`
   animation prefix. About 54 are tool textures never drawn (`clip`, `hint`,
   `skip`, `trigger`, `origin`, `sky*`).
6. **Tileability**: a wrap-seam ratio (mean luminance step across the wrap
   divided by the mean step between neighbouring pixels) has median 1.09,
   p25 0.69, p75 1.60 over the set. Walls, floors and ceilings wrap; signs,
   doors and screens do not. The property is per axis (a trim strip wraps
   along its long axis only).
7. **Renderer contract** (companion spec): the override is probed as
   `textures/<folder>/<name>.png`, texture coordinates use the WAL size, so
   any override size works; mipmapped uploads round to a power of two and are
   capped at `gl_override_maxsize` (1024). Output wider than 1024 is
   resampled by the engine at load, harmlessly.
8. **Diffusion floor**: Qwen-Image renders sensibly around 1 MP. A 64×64
   texture cannot be rendered at 256×256; every texture must be rendered at a
   working canvas near 1024 px and downscaled.

## 3. Layout and data flow

```
~/comfy/
├── batch_regen/                  # AITD kit, untouched
├── batch_regen_q2/               # this kit
│   ├── README.md
│   ├── run_batch.sh              # copy of the AITD script: env.sh, Pillow+numpy check, server check
│   ├── batch_textures.py         # driver
│   ├── texture_prompt.json       # pass 1 template (render)
│   ├── seam_prompt.json          # pass 2 template (seam repaint)
│   └── tests/
│       └── test_batch_textures.py
├── data/quake2/textures/         # input, rsync'd from the Mac (read-only for the kit)
└── data/quake2/textures-ai/      # output, same layout; also holds the caches below
```

`run_server.sh` from the AITD kit starts the shared server; the new kit does
not duplicate it.

Transfer (from the Quake-2 checkout on the Mac):

```
rsync -a --delete baseq2/textures/ felipe@192.168.1.1:comfy/data/quake2/textures/
rsync -a felipe@192.168.1.1:comfy/data/quake2/textures-ai/ baseq2/textures/
```

The second command overwrites the extracted originals in place, which is the
point: the renderer probes that path. The originals are recoverable at any
time with `tools/extract_textures.py --force`.

Caches written into the output tree, all JSON, all safe to delete:

| file | content |
|---|---|
| `.prompts.json` | relative path → VLM prompt string |
| `.style_anchors.json` | folder → anchor sentence |
| `.seams.json` | relative path → `{source: [rx, ry], pass1: [rx, ry], final: [rx, ry], repainted: ["x", "y"]}` |
| `.stages/` (only with `--keep-stages`) | full-resolution pass 1 and pass 2 crops per texture, for tuning |

## 4. Per-texture pipeline

### 4.1 Classification

`classify(src, rel)` returns `("copy", reason)` or `("render", (4w, 4h))`.
A file is copied byte-for-byte when:

- its suffix is not `.png`;
- its stem is exactly one of `clip`, `hint`, `skip`, `trigger`, `origin`,
  `nodraw`, `null`, or starts with `sky` (never drawn by the renderer);
- it is flat: grayscale standard deviation below 2.0 (a diffusion model
  cannot recreate "black");
- it is undecodable.

Everything else renders. **Output size is exactly 4× the source in each
axis**, no rounding, no cap, so texel density stays uniform across the world
and a 16×16 trim is no sharper than the 128×128 wall beside it.

### 4.2 Canvas rule

Diffusion needs a working canvas near 1024 px with a sane aspect. Every source
dimension is a multiple of 16, so integer scales keep every canvas a multiple
of 16.

```
nx = ny = 1
if w >= 2*h: ny = round(w / h)        # wide strip: stack copies vertically
elif h >= 2*w: nx = round(h / w)      # tall strip: copies side by side
tw, th = nx*w, ny*h                   # tiled canvas, native resolution
s = max(4, ceil(1024 / max(tw, th)))  # integer render scale
canvas = (s*tw, s*th); period = (s*w, s*h); target = (4*w, 4*h)
```

Consequences, asserted by the tests: canvas long edge in [1024, 1280],
aspect within 2:1, canvas and period multiples of 16, period never smaller
than target. Examples:

| source | tiled | s | canvas | period | target |
|---|---|---|---|---|---|
| 16×16 | 1×1 | 64 | 1024×1024 | 1024×1024 | 64×64 |
| 64×64 | 1×1 | 16 | 1024×1024 | 1024×1024 | 256×256 |
| 128×128 | 1×1 | 8 | 1024×1024 | 1024×1024 | 512×512 |
| 64×16 | 1×4 | 16 | 1024×1024 | 1024×256 | 256×64 |
| 128×16 | 1×8 | 8 | 1024×1024 | 1024×128 | 512×64 |
| 32×128 | 4×1 | 8 | 1024×1024 | 256×1024 | 128×512 |
| 256×128 | 1×2 | 4 | 1024×1024 | 1024×512 | 1024×512 |
| 320×128 | 1×2 | 4 | 1280×1024 | 1280×512 | 1280×512 |
| 240×128 | 1×1 | 5 | 1200×640 | 1200×640 | 960×512 |
| 176×128 | 1×1 | 6 | 1056×768 | 1056×768 | 704×512 |
| 64×96 | 1×1 | 11 | 704×1056 | 704×1056 | 256×384 |

The driver builds the tiled canvas at native resolution with Pillow and
stages it as `ComfyUI/input/__q2_src.png`. All upscaling happens in-graph so
Canny runs on native pixels (the AITD lesson: Canny on an upscaled image is
nearly empty).

### 4.3 Pass 1: render (`texture_prompt.json`)

| node | class | role |
|---|---|---|
| 1 | `LoadImage` | staged tiled canvas, native size |
| 2 | `ImageScale` bicubic → canvas | control image for `tile` |
| 16 | `Canny` (0.1 / 0.3) on 1 | edges at native size |
| 17 | `ImageScale` nearest-exact → canvas | control image for `canny` |
| 4, 5, 6, 7 | CLIP / UNET / VAE / ControlNet loaders | as the AITD template |
| 8 | `SetUnionControlNetType` `tile` | |
| 18 | `SetUnionControlNetType` `canny/lineart/anime_lineart/mlsd` | |
| 9 | `TextEncodeQwenImageEdit` | positive, prompt string set by the driver |
| 10 | `TextEncodeQwenImageEdit` | negative (fixed string, §5.4) |
| 11 | `ControlNetApplyAdvanced` (9, 10, 8, image 2, `TILE_STRENGTH`, vae 6) | |
| 19 | `ControlNetApplyAdvanced` (11.0, 11.1, 18, image 17, `CANNY_STRENGTH`, vae 6) | chained |
| 12 | `EmptyQwenImageLayeredLatentImage` canvas | |
| 13 | `KSampler` (model 5, seed, 30 steps, cfg 4, euler, simple, 19.0, 19.1, latent 12, denoise 1.0) | |
| 14 | `VAEDecode` | |
| 15 | `SaveImage` prefix `q2/batch` | |

Per image the driver sets: node 1 image name; width/height on 2, 12, 17;
prompt on 9; seed on 13. `TILE_STRENGTH = 0.7` and `CANNY_STRENGTH = 0.5`
are module constants, tuned in the pilot. No VLM node is in the graph.

After the render: `crop = render[0:period_h, 0:period_w]` (the first period,
at the origin, so no alignment bookkeeping), then `result = crop.resize(target,
LANCZOS)`. When `s == 4` the resize is the identity.

### 4.4 Seam measurement

```
wrap_ratio(img) -> (rx, ry)      # img: grayscale float array, h, w >= 4
  ix = mean |a[:,1:] - a[:,:-1]|      iy = mean |a[1:,:] - a[:-1,:]|
  wx = mean |a[:,0]  - a[:,-1]|       wy = mean |a[0,:]  - a[-1,:]|
  return wx / (ix + eps), wy / (iy + eps)
```

Same-axis normalisation: a horizontal wrap step is compared with horizontal
neighbour steps, so a striped texture is judged fairly on each axis.

Constants: `SOURCE_WRAP_MAX = 1.5` (the source wraps on an axis when its
ratio is at or below this), `RESULT_WRAP_MAX = 1.5` (the result has a seam on
an axis when its ratio is above this). Both are tuned in the pilot. The source
is measured at native size, the result at target size after the downscale.

An axis needs repair when the source wraps on it and the result does not.
With `--no-seam`, or when no axis needs repair, the result is final.

### 4.5 Pass 2: seam repaint (`seam_prompt.json`)

Runs on the full-resolution crop `C` (period size `Pw × Ph`), never on the
downscaled result. For the failing axes `X ⊆ {x, y}`:

1. **Roll**: `R = roll(C, dy = Ph/2 if y ∈ X else 0, dx = Pw/2 if x ∈ X else 0)`.
   The old wrap seams now run through the middle of the image; the new
   borders were generated adjacent to each other and are continuous.
2. **Mask** `M` (1 = repaint): a vertical band of columns
   `[Pw/2 − bx, Pw/2 + bx)` with `bx = max(16, round(0.05 · Pw))` when
   `x ∈ X`, and the horizontal counterpart when `y ∈ X`. A cross when both.
3. **Stage** `R` as RGBA `__q2_roll.png` with `alpha = 255 · (1 − M)` (ComfyUI's
   `LoadImage` mask is `1 − alpha`, so the band reads as mask 1), and the
   single-period source rolled the same way at native size as `__q2_rollsrc.png`.
   The rolled source is seamless by construction, so the control images carry
   no seam.
4. **Graph**: nodes 4–10, 8, 18 as pass 1; node 1 `LoadImage` (`__q2_roll.png`,
   image and mask); node 21 `LoadImage` (`__q2_rollsrc.png`); 2 and 16/17 scale
   node 21 to `Pw × Ph` for `tile` and `canny`; 11 and 19 as pass 1; node 20
   `InpaintModelConditioning` (positive 19.0, negative 19.1, vae 6, pixels 1.0,
   mask 1.1, noise_mask true) → (positive, negative, latent); 13 `KSampler`
   (same seed, denoise 1.0) on 20.2; 14 `VAEDecode`; 15 `SaveImage` prefix
   `q2/seam`. Same prompt string as pass 1.
5. **Composite**: the VAE round trip softens unmasked pixels, so the driver
   keeps pass 1 outside the band: `F = R · (1 − m) + D · m` with `D` the decoded
   pass 2 image and `m` the mask feathered linearly over 8 px at render
   resolution.
6. **Roll back** `F` by `(−dy, −dx)`, downscale to target, re-measure. A ratio
   still above `RESULT_WRAP_MAX` is recorded in `.seams.json` and printed as
   `warning: seam remains on x` but the file is kept. There is no third pass.

### 4.6 Acceptance before the move

`verify_render(path, expect)` raises unless the file decodes, is exactly
`expect` in size, and is mode `RGB`. Only then is the result written to
`<dst>/<rel>` (atomic: write to `<dst>/<rel>.tmp`, then rename). Staged inputs
and the ComfyUI output files of both passes are deleted after each image so
`ComfyUI/input` and `ComfyUI/output/q2` never accumulate.

### 4.7 Fallbacks decided up front

- If `tile` conditioning fights the pixel-art source in the pilot (blurry,
  posterised output), node 11's strength goes to 0 and the graph is Canny-only;
  no driver change.
- If inpainting misbehaves, pass 2 is replaced by an edge cross-fade over the
  same band in the driver; the gate, the cache and `--verify` are unchanged.

## 5. Prompting

### 5.1 Per-texture prompt (driver-side)

The driver calls the DashScope chat-completions endpoint the AITD kit already
uses for anchors (`https://dashscope-intl.aliyuncs.com/compatible-mode/v1`,
model `qwen3-vl-plus`, key from `~/comfy/.env`). The image sent is the source
period upscaled by nearest neighbour so its long edge is at least 512 px, as
PNG base64. Meta-prompt:

> This is a texture from a 1997 video game, shown enlarged. Describe the
> real-world material it depicts so that a photorealistic image generator can
> recreate it as a straight-on, evenly lit texture photograph. Cover: the
> material (metal, concrete, rock, painted panel, grating, …), its surface
> finish and wear, the layout of panels, seams, bolts, pipes, lights or
> markings exactly as arranged and proportioned in the image, and its exact
> colours. *[when the source wraps on x and/or y:]* The surface repeats
> seamlessly horizontally / vertically; describe it as one continuous
> surface. Rules: no perspective, no camera angle, no dramatic lighting, no
> vignette, no cast shadows from outside objects; do not invent objects that
> are not in the image; do not mention pixels, low resolution or video games;
> do not describe any text or lettering. Output only one comma-separated
> string, no markdown.

The driver appends a fixed tail: `photorealistic PBR texture, flat even
lighting, orthographic straight-on view, highly detailed, 8k`, then
`UNIT MATERIAL STANDARD: <anchor>` when the folder has one.

Retries: 3 attempts with 5 s, 15 s, 45 s backoff on any exception or on an
empty response. After the third failure the image **fails** (stderr, counted,
not written) so the next run retries it. There is no fallback prompt. Cached
in `<dst>/.prompts.json` before the render is queued, so a render failure does
not repeat the VLM call.

### 5.2 Per-folder anchor

One sentence per map folder (`e1u1` … `e3u3`, `test`, root), from up to four
representative textures chosen evenly from the folder's render candidates,
same call path and retries as above but a failure is a warning and the folder
proceeds without an anchor (as in the AITD kit). Question:

> These are textures from the same area of a game. Write one sentence that
> specifies the shared material palette and surface standard all of them
> must follow: dominant materials, colour palette, wear level and lighting
> mood. Output only the sentence.

Cached in `<dst>/.style_anchors.json`.

### 5.3 Seed

`seed = crc32("<folder>/<base>") & 0x7fffffff`, where `base` is the stem
with a leading `+` plus one character removed (`+0butn4` → `butn4`). All
frames of an animated panel share a seed; the seed is stable across runs and
machines.

### 5.4 Negative prompt

`blurry, low quality, perspective, camera angle, vignette, depth of field,
text, lettering, logo, watermark, seam, border, frame`

### 5.5 Sampler

Starting point is the AITD template: 30 steps, `euler`, `simple`, cfg 4.0,
denoise 1.0. These and the two ControlNet strengths are the pilot's tuning
knobs; the README's Tuning section names each by node id.

## 6. Run behaviour

### 6.1 Command line

```
./run_batch.sh [--dry-run] [--verify] [--src DIR] [--dst DIR]
               [--only FOLDER]... [--exclude FOLDER]...
               [--pick REL]... [--limit N] [--no-seam] [--keep-stages]
```

- `--dry-run`: print the per-folder render/copy plan and, per render, the
  canvas plan; no server, no VLM.
- `--verify`: audit `--dst` against `--src` (§6.4); no server.
- `--only` / `--exclude`: top-level folders, as in the AITD kit.
- `--pick e1u1/metal1_1.png` (repeatable): restrict to named textures.
- `--limit N`: stop after the first N render candidates in sorted order.
- `--no-seam`: skip §4.5 entirely (A/B in the pilot).
- `--keep-stages`: also write the full-resolution pass 1 and pass 2 crops to
  `<dst>/.stages/<rel>.pass1.png` and `.pass2.png`.

`run_batch.sh` is the AITD script with the Pillow check extended to numpy and
the driver name changed. `--verify` and `--dry-run` run without the server.

### 6.2 Loop

Sequential, sorted by relative path. Per file: skip if the destination exists
and is non-empty (resumable); copy-as-is per §4.1; otherwise prompt (cached),
pass 1, measure, optional pass 2, verify, atomic move. Progress line
`[i/N] rel  canvas WxH s=K  seams x=.. y=..` per image. Any exception in a
single image is printed to stderr with the relative path and the run
continues. Final line:

```
done: processed=N skipped=N copied=N seamfixed=N seamwarn=N failed=N
```

Exit 1 if `failed > 0`.

### 6.3 Errors and limits

- Server queue and history as in the AITD kit (`HISTORY_TIMEOUT = 1800`).
  An execution whose `status_str` is not `success` fails the image with the
  node id and message from the history entry.
- VLM per §5.1. The style-anchor failure is a warning.
- A result that fails `verify_render` fails the image; nothing is written.
- Disk: about half a gigabyte for the full output tree. The kit adds nothing
  to `ComfyUI/output` beyond the file in flight.
- Time: roughly 30 s per pass on the 5090 at 1 MP; with pass 2 on about half
  the set, the full run is one to two days. Run under
  `./run_batch.sh |& tee batch.log` in `tmux`.

### 6.4 Verify

For every source file: `MISSING` if the destination is absent; for copy-as-is
files `ALTERED` if the bytes differ; for renders `UNREADABLE`, `WRONGSIZE`
(not exactly 4×) or `WRONGMODE` (not `RGB`). These are failures and set exit
1. Additionally `SEAM x` / `SEAM y` when the source wraps on that axis and the
destination does not (§4.4 thresholds); these are warnings, counted and
printed, exit code unaffected. Last line:
`verify: N problem(s), M seam warning(s)`.

## 7. Pilot and rollout

1. **Kit and offline tests** (§8.1) on the box; `--dry-run` over the real
   tree must report about 2130 renders and about 58 copies with every canvas
   within the §4.2 bounds.
2. **Pilot**: `--pick` a dozen textures spanning the classes: a 64×64 wall, a
   128×128 floor, a 16×128 or 64×16 trim, a 32×32 tile, a sign, a 320×128
   door, one full animated panel (`+0…+N`), one non-power-of-two file, with
   `--keep-stages`. rsync them to the Mac, load a map that uses them, and
   check tiling, alignment and colour against the originals. This doubles as
   the visual play test the override feature never had (companion spec §5.3:
   `imagelist`, `gl_textureoverride 0; vid_restart`).
3. **Tune** strengths, thresholds and the meta-prompt from the pilot; delete
   the pilot outputs and re-run them until accepted.
4. **One folder** (`--only e1u1`, 390 files), review in-game, then the rest.
   Per-folder runs let the anchor and the look be judged before the next unit.
5. Commit the kit in the `~/comfy` repository on the box, staging only
   `batch_regen_q2/`. Add the README note in this repo.

## 8. Testing

### 8.1 Offline unit tests — `tests/test_batch_textures.py`

Run with `$COMFY_PYTHON -m pytest tests` in the kit directory (pytest is
installed into the comfy env if absent). Pillow and numpy only; no server, no
network. Synthetic images are built in `tmp_path`.

- `classify`: each tool stem and a `sky3` file copy with a reason; a flat
  black file copies; a `.json` copies; a normal 64×64 renders with target
  256×256.
- `plan_canvas`: the table in §4.2 verbatim, plus a sweep over every size in
  the real set's histogram asserting long edge in [1024, 1280], aspect
  ≤ 2:1, canvas and period multiples of 16, period ≥ target.
- `build_canvas`: the tiled canvas is `nx × ny` exact copies of the source.
- `crop_and_downscale`: from a synthetic render, the first period is taken
  and the result is exactly `target`; with `s == 4` pixels are unchanged.
- `wrap_ratio`: a periodic pattern gives ratios below 1 on both axes; the
  same pattern with one column shifted gives a high `rx` and a low `ry`;
  the mirror case for `ry`.
- `needs_repair`: the gate table over (source wraps, result wraps) per axis.
- `roll_and_mask`: rolling by half twice is the identity; the mask band is
  centred, has the expected width per axis, becomes a cross for both; the
  staged RGBA has alpha 0 exactly where the mask is 1; the feathered mask
  ramps over 8 px.
- `composite`: outside the band the composite equals pass 1 exactly.
- `seed_for`: `+0butn4` and `+1butn4` agree; `butn4` in another folder
  differs; deterministic.
- `verify_tree`: a temp tree exercising MISSING, ALTERED, WRONGSIZE,
  WRONGMODE and a SEAM warning, with the exit code rule.
- `templates`: both JSON files load; every node id the driver sets has the
  expected `class_type` (guards against editing a template and silently
  breaking the driver); node 8 is `tile`, node 18 is the canny type; pass 2
  has node 20 as `InpaintModelConditioning`.

### 8.2 Integration

- `--dry-run` on the real tree (§7.1).
- Pilot in-game check (§7.2), which is the acceptance test for tuning.
- `--verify` after the full run reports zero problems; seam warnings are
  reviewed by name.

## 9. Files touched

| where | file | change |
|---|---|---|
| box | `~/comfy/batch_regen_q2/README.md` | new |
| box | `~/comfy/batch_regen_q2/run_batch.sh` | new, from the AITD script |
| box | `~/comfy/batch_regen_q2/batch_textures.py` | new |
| box | `~/comfy/batch_regen_q2/texture_prompt.json` | new, from the AITD template |
| box | `~/comfy/batch_regen_q2/seam_prompt.json` | new |
| box | `~/comfy/batch_regen_q2/tests/test_batch_textures.py` | new |
| Mac | `docs/superpowers/specs/2026-09-02-quake2-texture-recreation-kit-design.md` | this spec |
| Mac | `README.md` | rsync push/pull note under the texture overrides section |
