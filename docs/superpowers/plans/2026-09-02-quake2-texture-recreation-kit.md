# Quake II Texture Recreation Kit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A ComfyUI batch kit on the GPU box that recreates the 2188 extracted Quake II world textures as photorealistic 4× PNGs, keeping layout, colours and tileability, in a tree that drops into `baseq2/textures/`.

**Architecture:** A sibling of the AITD kit at `~/comfy/batch_regen_q2/` on `felipe@192.168.1.1`, split into four modules: `texplan.py` (pure planning: classify, canvas, seed), `teximage.py` (pixel ops: tiling, crop, wrap-seam ratio, roll/mask/composite), `comfy_client.py` (the two API-format graphs and the queue/wait cycle), `vlm_client.py` (DashScope prompts with retries and JSON caches), driven by `batch_textures.py` (CLI, loop, verify). Every module is testable offline with Pillow and numpy; the ComfyUI and VLM transports are injected.

**Tech Stack:** Python 3.12 (`$COMFY_PYTHON` on the box, `uv run` locally), Pillow, numpy, pytest, ComfyUI HTTP API on `127.0.0.1:8188`, Qwen-Image-2512 + Fun ControlNet Union, DashScope `qwen3-vl-plus`.

**Spec:** `docs/superpowers/specs/2026-09-02-quake2-texture-recreation-kit-design.md`

## Global Constraints

- The AITD kit `~/comfy/batch_regen/` is never modified (spec §1).
- On the box, `~/comfy/` is a git repo with unrelated unstaged changes; stage only `batch_regen_q2/` there. In this repo, never `git add -A` or `git commit -a`; `baseq2/textures/` stays git-ignored (spec §1).
- Output is exactly `4×` the source in each axis, mode `RGB`; a failed render is never written (spec §4.1, §4.6).
- Canvas rule: tile the short axis to within 2:1, integer scale `s = max(4, ceil(1024 / long edge))` (spec §4.2).
- Seam gate constants: `SOURCE_WRAP_MAX = 1.5`, `RESULT_WRAP_MAX = 1.5`; band half-width `max(16, round(0.05 · period))`; feather 8 px (spec §4.4, §4.5).
- Node ids the driver writes: 1, 2, 9, 10, 11, 12, 13, 15, 16, 17, 19 (render); 1, 2, 9, 10, 11, 13, 15, 16, 17, 19, 20, 21 (seam). Node 8 is union type `tile`, node 18 is `canny/lineart/anime_lineart/mlsd` (spec §4.3, §4.5).
- VLM: one call plus three retries with 5 s, 15 s, 45 s backoff; a final failure fails the image, no fallback prompt (spec §5.1).
- Commit messages on either machine end with:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_0127eikzBGwe4iGJzEeXsg3r
  ```

## Working conventions

The kit lives on the box; the Mac holds a working copy outside the Quake-2 repo. Set these once per shell:

```bash
export BOX=felipe@192.168.1.1
export KIT="${KIT:-/private/tmp/claude-501/-Users-felipe-dos-santos-code-theirs-Quake-2/b0f5e0ef-552b-465e-b8f5-5ceea044c29f/scratchpad/batch_regen_q2}"
mkdir -p "$KIT/tests"
# If the box already has the kit (a later session), start from it:
ssh $BOX 'test -d ~/comfy/batch_regen_q2' && rsync -a --exclude __pycache__ $BOX:comfy/batch_regen_q2/ "$KIT"/
```

Four commands are used by every task; the steps below refer to them by name:

```bash
# LOCAL TESTS
( cd "$KIT" && uv run --no-project --with pillow --with numpy --with pytest python -m pytest tests -q )
# SYNC
rsync -a --delete --exclude __pycache__ --exclude .pytest_cache "$KIT"/ $BOX:comfy/batch_regen_q2/
# REMOTE TESTS
ssh $BOX 'cd ~/comfy/batch_regen_q2 && . ~/comfy/env.sh && "$COMFY_PYTHON" -m pytest tests -q'
# COMMIT ON THE BOX  (replace MSG; the trailer lines are required)
ssh $BOX 'cd ~/comfy && git add batch_regen_q2 && git commit -q -m "MSG

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_0127eikzBGwe4iGJzEeXsg3r" && git log --oneline -1'
```

Local tests need `uv` (present on the Mac). Remote tests need pytest in the comfy env; Task 1 installs it.

## File structure

| file | responsibility |
|---|---|
| `texplan.py` | `classify`, `plan_canvas` → `CanvasPlan`, `seed_for`, `base_name`, `image_size`; pure rules |
| `teximage.py` | `build_canvas`, `crop_period`, `downscale`, `wrap_ratio`, `source_axes`, `needs_repair`, `half_shift`, `roll`, `seam_mask`, `feather`, `stage_rgba`, `composite` |
| `comfy_client.py` | template loading with a node guard, `prepare_render`, `prepare_seam`, `ComfyClient` (stage/run/unstage), `http_json` |
| `vlm_client.py` | `load_api_key`, `image_data_url`, `with_retries`, `describe_texture`, `folder_anchor`, `spread`, `JsonCache` |
| `batch_textures.py` | CLI, `Context`, `anchor_for`, `process_one`, `verify_tree`, `main` |
| `texture_prompt.json`, `seam_prompt.json` | pass 1 and pass 2 graphs |
| `run_batch.sh` | interpreter and server checks, then the driver |
| `tests/conftest.py`, `tests/test_*.py` | one test file per module |
| `README.md` | how to run, what gets repainted, tuning |

---

### Task 1: Kit scaffold and `texplan.py`

**Files:**
- Create: `$KIT/texplan.py`
- Create: `$KIT/tests/conftest.py`
- Create: `$KIT/tests/test_texplan.py`
- Create: `$KIT/run_batch.sh`

**Interfaces:**
- Produces: `classify(src, rel=None) -> ("render", (w*4, h*4)) | ("copy", reason)`; `plan_canvas(w, h) -> CanvasPlan` with `.nx .ny .s .w .h` and properties `.canvas .period .target` (all `(width, height)` tuples); `seed_for(rel) -> int`; `base_name(stem) -> str`; `image_size(path) -> (w, h) | None`; constants `SCALE`, `CANVAS_MIN_EDGE`, `FLAT_STD`, `TOOL_STEMS`.

- [ ] **Step 1: Install pytest in the comfy env on the box and confirm the interpreter**

```bash
ssh $BOX '. ~/comfy/env.sh && "$COMFY_PYTHON" -m pip install -q pytest && "$COMFY_PYTHON" -c "import pytest, numpy, PIL; print(pytest.__version__, numpy.__version__, PIL.__version__)"'
```
Expected: three version numbers (numpy 2.4.x, Pillow 12.x).

- [ ] **Step 2: Write `tests/conftest.py` and `run_batch.sh`**

`$KIT/tests/conftest.py`:
```python
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
```

`$KIT/run_batch.sh` (then `chmod +x`):
```bash
#!/bin/bash
# Run the Quake II texture recreation driver against the local ComfyUI server.
# Forwards args to batch_textures.py (e.g. --dry-run, --verify, --only, --pick).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMFY_ROOT="$HOME/comfy"

# The driver needs Pillow and numpy; the system python3 has neither.
# env.sh (written by the AITD kit's install.sh) names the interpreter that does.
if [ -f "$COMFY_ROOT/env.sh" ]; then
  . "$COMFY_ROOT/env.sh"
fi
PY="${COMFY_PYTHON:-python3}"
"$PY" -c 'import PIL, numpy' 2>/dev/null || {
  echo "error: $PY lacks Pillow or numpy - set COMFY_PYTHON in $COMFY_ROOT/env.sh"
  exit 1
}

# --verify and --dry-run are offline: no server needed.
need_server=1
for a in "$@"; do
  case "$a" in --verify|--dry-run) need_server=0 ;; esac
done

if [ "$need_server" = 1 ]; then
  curl -sf --max-time 5 http://127.0.0.1:8188/queue >/dev/null || {
    echo "error: ComfyUI server not responding on http://127.0.0.1:8188 - start it first (../batch_regen/run_server.sh)"
    exit 1
  }
fi

exec "$PY" "$SCRIPT_DIR/batch_textures.py" "$@"
```

- [ ] **Step 3: Write the failing tests for `texplan`**

`$KIT/tests/test_texplan.py`:
```python
import numpy as np
import pytest
from PIL import Image

from texplan import classify, plan_canvas, seed_for, base_name, image_size


def png(path, w, h, noise=True, color=(0, 0, 0)):
    if noise:
        rng = np.random.default_rng(w * 1000 + h)
        arr = rng.integers(0, 256, (h, w, 3), dtype=np.uint8)
        Image.fromarray(arr, "RGB").save(path)
    else:
        Image.new("RGB", (w, h), color).save(path)
    return path


@pytest.mark.parametrize("stem", ["clip", "hint", "skip", "trigger", "origin", "nodraw",
                                  "null", "sky1", "sky_e3", "CLIP"])
def test_tool_textures_are_copied(tmp_path, stem):
    p = png(tmp_path / f"{stem}.png", 64, 64)
    kind, reason = classify(p)
    assert kind == "copy" and "tool" in reason


def test_flat_texture_is_copied(tmp_path):
    p = png(tmp_path / "black.png", 32, 32, noise=False)
    assert classify(p) == ("copy", "flat texture")


def test_non_png_is_copied(tmp_path):
    p = tmp_path / "notes.json"
    p.write_text("{}")
    assert classify(p) == ("copy", "not a PNG")


def test_undecodable_png_is_copied(tmp_path):
    p = tmp_path / "bad.png"
    p.write_bytes(b"not a png")
    assert classify(p) == ("copy", "undecodable as an image")
    assert image_size(p) is None


def test_normal_texture_renders_at_4x(tmp_path):
    p = png(tmp_path / "metal1_1.png", 64, 64)
    assert classify(p) == ("render", (256, 256))
    assert image_size(p) == (64, 64)


SPEC_TABLE = [
    # (w, h, nx, ny, s, canvas, period, target)  -- spec §4.2
    (16, 16, 1, 1, 64, (1024, 1024), (1024, 1024), (64, 64)),
    (64, 64, 1, 1, 16, (1024, 1024), (1024, 1024), (256, 256)),
    (128, 128, 1, 1, 8, (1024, 1024), (1024, 1024), (512, 512)),
    (64, 16, 1, 4, 16, (1024, 1024), (1024, 256), (256, 64)),
    (128, 16, 1, 8, 8, (1024, 1024), (1024, 128), (512, 64)),
    (32, 128, 4, 1, 8, (1024, 1024), (256, 1024), (128, 512)),
    (256, 128, 1, 2, 4, (1024, 1024), (1024, 512), (1024, 512)),
    (320, 128, 1, 2, 4, (1280, 1024), (1280, 512), (1280, 512)),
    (240, 128, 1, 1, 5, (1200, 640), (1200, 640), (960, 512)),
    (176, 128, 1, 1, 6, (1056, 768), (1056, 768), (704, 512)),
    (64, 96, 1, 1, 11, (704, 1056), (704, 1056), (256, 384)),
]


@pytest.mark.parametrize("w,h,nx,ny,s,canvas,period,target", SPEC_TABLE)
def test_plan_canvas_matches_spec_table(w, h, nx, ny, s, canvas, period, target):
    p = plan_canvas(w, h)
    assert (p.nx, p.ny, p.s) == (nx, ny, s)
    assert p.canvas == canvas and p.period == period and p.target == target
    assert (p.w, p.h) == (w, h)


# every (w, h) present in the real extracted set (spec §2.5)
REAL_SIZES = [(64, 64), (128, 128), (32, 32), (64, 128), (16, 16), (64, 16), (128, 64),
              (32, 128), (32, 64), (64, 32), (256, 128), (128, 32), (16, 32), (16, 128),
              (128, 16), (32, 16), (96, 32), (64, 96), (240, 128), (320, 128), (176, 128),
              (32, 48), (16, 64), (32, 96), (128, 176), (48, 48), (128, 192), (64, 80)]


@pytest.mark.parametrize("w,h", REAL_SIZES)
def test_plan_canvas_bounds_over_real_sizes(w, h):
    p = plan_canvas(w, h)
    cw, ch = p.canvas
    assert 1024 <= max(cw, ch) <= 1280
    assert max(cw, ch) / min(cw, ch) <= 2.0
    assert cw % 16 == 0 and ch % 16 == 0
    pw, ph = p.period
    assert pw % 16 == 0 and ph % 16 == 0
    tw, th = p.target
    assert pw >= tw and ph >= th
    assert p.s >= 4


def test_base_name_strips_animation_prefix():
    assert base_name("+0butn4") == "butn4"
    assert base_name("+1butn4") == "butn4"
    assert base_name("+abtshoot2") == "btshoot2"
    assert base_name("butn4") == "butn4"
    assert base_name("+") == "+"


def test_seed_shared_by_animation_frames_and_stable():
    a = seed_for("e1u1/+0comp1.png")
    assert seed_for("e1u1/+3comp1.png") == a
    assert seed_for("e1u1/comp1.png") == a
    assert seed_for("e2u1/comp1.png") != a
    assert 0 <= a <= 0x7FFFFFFF
    assert seed_for("e1u1/+0comp1.png") == a
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: LOCAL TESTS
Expected: `ModuleNotFoundError: No module named 'texplan'`

- [ ] **Step 5: Write `texplan.py`**

`$KIT/texplan.py`:
```python
#!/usr/bin/env python3
"""Planning rules for the Quake II texture kit: which files render, at what
canvas and scale, and with which seed. Pure functions; the only I/O is
opening an image to classify it.

Spec: Quake-2/docs/superpowers/specs/2026-09-02-quake2-texture-recreation-kit-design.md
      sections 4.1, 4.2 and 5.3.
"""
import math
import zlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

SCALE = 4                 # output is exactly SCALE x the source in each axis
CANVAS_MIN_EDGE = 1024    # render canvas long edge is at least this
FLAT_STD = 2.0            # grayscale std below this: nothing to recreate
# Never drawn by the renderer: editor-only brushes and sky surfaces.
TOOL_STEMS = {"clip", "hint", "skip", "trigger", "origin", "nodraw", "null"}
SKY_PREFIX = "sky"


def is_tool_texture(stem):
    s = stem.lower()
    return s in TOOL_STEMS or s.startswith(SKY_PREFIX)


def image_size(path):
    """(w, h) or None when the file is not a decodable image."""
    try:
        with Image.open(path) as im:
            return im.width, im.height
    except Exception:
        return None


def is_flat(im):
    return float(np.asarray(im.convert("L"), dtype=np.float64).std()) < FLAT_STD


def classify(src, rel=None):
    """('render', (w*SCALE, h*SCALE)) or ('copy', reason).

    The only path to the model. Tool textures are never drawn, flat
    textures have nothing to recreate, and anything that is not a decodable
    PNG is data that happens to live in the tree.
    """
    src = Path(src)
    if src.suffix.lower() != ".png":
        return "copy", "not a PNG"
    if is_tool_texture(src.stem):
        return "copy", "tool texture, never drawn"
    try:
        with Image.open(src) as im:
            im.load()
            if is_flat(im):
                return "copy", "flat texture"
            return "render", (im.width * SCALE, im.height * SCALE)
    except Exception:
        return "copy", "undecodable as an image"


@dataclass(frozen=True)
class CanvasPlan:
    w: int      # source size
    h: int
    nx: int     # copies of the source laid side by side
    ny: int     # copies stacked
    s: int      # integer render scale

    @property
    def canvas(self):
        return self.s * self.nx * self.w, self.s * self.ny * self.h

    @property
    def period(self):
        return self.s * self.w, self.s * self.h

    @property
    def target(self):
        return SCALE * self.w, SCALE * self.h


def plan_canvas(w, h):
    """Tile the short axis until the canvas is within 2:1, then pick the
    smallest integer scale >= SCALE that puts the long edge at
    CANVAS_MIN_EDGE or more. Python's round() halves to even: 320x128
    gives ny=2, which is the spec table."""
    nx = ny = 1
    if w >= 2 * h:
        ny = round(w / h)
    elif h >= 2 * w:
        nx = round(h / w)
    tw, th = nx * w, ny * h
    s = max(SCALE, math.ceil(CANVAS_MIN_EDGE / max(tw, th)))
    return CanvasPlan(w, h, nx, ny, s)


def base_name(stem):
    """'+0butn4' -> 'butn4', '+abtshoot2' -> 'btshoot2': animation frames
    share a base name and therefore a seed."""
    return stem[2:] if len(stem) > 2 and stem[0] == "+" else stem


def seed_for(rel):
    rel = Path(rel)
    key = f"{rel.parent.as_posix()}/{base_name(rel.stem)}"
    return zlib.crc32(key.encode()) & 0x7FFFFFFF
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: LOCAL TESTS
Expected: all pass (about 50 tests, most parametrised).

- [ ] **Step 7: Sync, run on the box, commit**

Run: SYNC, then REMOTE TESTS (expected: same pass count), then COMMIT ON THE BOX with
`MSG` = `feat(q2): texture kit scaffold and planning rules (classify, canvas, seed)`.

---

### Task 2: `teximage.py` pixel operations

**Files:**
- Create: `$KIT/teximage.py`
- Create: `$KIT/tests/test_teximage.py`

**Interfaces:**
- Produces: `build_canvas(im, nx, ny) -> Image`; `crop_period(render, (pw, ph)) -> Image`; `downscale(im, (tw, th)) -> Image`; `wrap_ratio(im) -> (rx, ry)`; `wraps(r) -> bool`; `has_seam(r) -> bool`; `source_axes((rx, ry)) -> tuple[str]`; `needs_repair(src_ratios, res_ratios) -> tuple[str]`; `half_shift((w, h), axes) -> (dx, dy)`; `roll(im, dx, dy) -> Image`; `seam_mask((w, h), axes) -> np.uint8[h, w]`; `feather(mask, px=8) -> np.float64[h, w]`; `stage_rgba(im, mask) -> RGBA Image`; `composite(base, painted, m) -> Image`; constants `AXES = ("x", "y")`, `SOURCE_WRAP_MAX`, `RESULT_WRAP_MAX`, `BAND_FRAC`, `BAND_MIN`, `FEATHER_PX`.

- [ ] **Step 1: Write the failing tests**

`$KIT/tests/test_teximage.py`:
```python
import numpy as np
from PIL import Image

from teximage import (build_canvas, crop_period, downscale, wrap_ratio, needs_repair,
                      source_axes, half_shift, roll, seam_mask, feather, stage_rgba,
                      composite, BAND_MIN, FEATHER_PX)


def periodic(w, h, seed=0):
    """Wraps on both axes: cosines with integer periods start at zero slope,
    so the wrap step is tiny. seed only offsets brightness so files differ."""
    y, x = np.mgrid[0:h, 0:w]
    v = (120 + (seed % 7) + 60 * np.cos(2 * np.pi * 3 * x / w)
         + 50 * np.cos(2 * np.pi * 2 * y / h))
    arr = np.clip(v, 0, 255).astype(np.uint8)
    return Image.fromarray(np.stack([arr] * 3, axis=-1), "RGB")


def test_build_canvas_tiles_exact_copies():
    im = periodic(32, 16)
    c = build_canvas(im, 2, 3)
    assert c.size == (64, 48)
    a = np.asarray(im)
    ca = np.asarray(c)
    for j in range(3):
        for i in range(2):
            assert np.array_equal(ca[j * 16:(j + 1) * 16, i * 32:(i + 1) * 32], a)


def test_crop_period_takes_origin():
    rng = np.random.default_rng(1)
    render = Image.fromarray(rng.integers(0, 256, (64, 96, 3), dtype=np.uint8), "RGB")
    crop = crop_period(render, (48, 32))
    assert crop.size == (48, 32)
    assert np.array_equal(np.asarray(crop), np.asarray(render)[:32, :48])


def test_downscale_exact_size_and_identity_at_target():
    im = periodic(64, 32)
    assert downscale(im, (16, 8)).size == (16, 8)
    same = downscale(im, (64, 32))
    assert np.array_equal(np.asarray(same), np.asarray(im))


def test_wrap_ratio_low_for_periodic_high_when_cut():
    im = periodic(128, 64)
    rx, ry = wrap_ratio(im)
    assert rx < 1.0 and ry < 1.0
    a = np.asarray(im)
    cut_x = Image.fromarray(a[:, :65], "RGB")      # column 64 is at cos = -1
    rx2, ry2 = wrap_ratio(cut_x)
    assert rx2 > 3.0 and ry2 < 1.0
    cut_y = Image.fromarray(a[:49, :], "RGB")      # row 48 is at cos = -1
    rx3, ry3 = wrap_ratio(cut_y)
    assert rx3 < 1.0 and ry3 > 3.0


def test_source_axes_and_needs_repair():
    assert source_axes((0.5, 2.0)) == ("x",)
    assert source_axes((1.5, 1.5)) == ("x", "y")
    assert source_axes((1.6, 1.6)) == ()
    assert needs_repair((0.5, 0.5), (2.0, 0.5)) == ("x",)
    assert needs_repair((0.5, 0.5), (2.0, 2.0)) == ("x", "y")
    assert needs_repair((3.0, 0.5), (2.0, 2.0)) == ("y",)      # source did not wrap on x
    assert needs_repair((0.5, 0.5), (1.0, 1.0)) == ()


def test_half_shift_and_roll_roundtrip():
    im = periodic(64, 32)
    assert half_shift((64, 32), ("x",)) == (32, 0)
    assert half_shift((64, 32), ("x", "y")) == (32, 16)
    assert half_shift((64, 32), ()) == (0, 0)
    r = roll(im, 32, 16)
    back = roll(r, -32, -16)
    assert np.array_equal(np.asarray(back), np.asarray(im))
    assert np.array_equal(np.asarray(r)[16, 32], np.asarray(im)[0, 0])


def test_seam_mask_bands():
    m = seam_mask((256, 64), ("x",))
    assert m.shape == (64, 256) and m.dtype == np.uint8
    assert m[:, 128 - BAND_MIN:128 + BAND_MIN].all()
    assert not m[:, :128 - BAND_MIN].any() and not m[:, 128 + BAND_MIN:].any()
    m2 = seam_mask((1024, 512), ("y",))
    by = round(0.05 * 512)                          # 26 > BAND_MIN
    assert m2[256 - by:256 + by, :].all() and not m2[:256 - by, :].any()
    cross = seam_mask((256, 256), ("x", "y"))
    assert cross[128, 0] == 1 and cross[0, 128] == 1 and cross[0, 0] == 0
    assert seam_mask((64, 64), ()).sum() == 0


def test_feather_ramps_over_feather_px():
    m = seam_mask((256, 64), ("x",))          # band is columns [112, 144)
    f = feather(m)
    assert f[10, 128] == 1.0
    assert f[10, 100] == 0.0
    ramp = f[10, 112 - FEATHER_PX // 2: 112 + FEATHER_PX // 2]
    assert ((ramp > 0) & (ramp < 1)).all()
    assert np.all(np.diff(ramp) > 0)


def test_stage_rgba_alpha_is_zero_in_band():
    im = periodic(256, 64)
    m = seam_mask((256, 64), ("x",))
    rgba = stage_rgba(im, m)
    assert rgba.mode == "RGBA"
    a = np.asarray(rgba)
    assert (a[:, :, 3][m == 1] == 0).all() and (a[:, :, 3][m == 0] == 255).all()
    assert np.array_equal(a[:, :, :3], np.asarray(im))


def test_composite_keeps_base_outside_band_and_paint_inside():
    base = periodic(256, 64, seed=1)
    painted = periodic(256, 64, seed=2)          # differs from base by 1 everywhere
    m = feather(seam_mask((256, 64), ("x",)))
    out = np.asarray(composite(base, painted, m))
    assert np.array_equal(out[:, :100], np.asarray(base)[:, :100])
    assert np.array_equal(out[:, 120:136], np.asarray(painted)[:, 120:136])
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: LOCAL TESTS
Expected: `ModuleNotFoundError: No module named 'teximage'`

- [ ] **Step 3: Write `teximage.py`**

`$KIT/teximage.py`:
```python
"""Pixel operations for the Quake II texture kit: tiled canvases, period
crops, wrap-seam measurement, and the roll / mask / composite steps of the
seam repaint. Pillow in, Pillow out; numpy inside.

Spec sections 4.3 to 4.5.
"""
import numpy as np
from PIL import Image

SOURCE_WRAP_MAX = 1.5   # source wraps on an axis when its ratio is <= this
RESULT_WRAP_MAX = 1.5   # result has a seam on an axis when its ratio is > this
BAND_FRAC = 0.05        # half-width of the repaint band as a fraction of the period
BAND_MIN = 16           # ... but at least this many pixels
FEATHER_PX = 8          # ramp width of the composite mask
AXES = ("x", "y")


def build_canvas(im, nx, ny):
    """nx by ny exact copies of im."""
    w, h = im.size
    out = Image.new("RGB", (w * nx, h * ny))
    for j in range(ny):
        for i in range(nx):
            out.paste(im, (i * w, j * h))
    return out


def crop_period(render, period):
    pw, ph = period
    return render.crop((0, 0, pw, ph))


def downscale(im, target):
    if im.size == tuple(target):
        return im.copy()
    return im.resize(tuple(target), Image.LANCZOS)


def wrap_ratio(im):
    """(rx, ry): luminance step across the wrap on each axis divided by the
    mean step between neighbouring pixels along the same axis."""
    a = np.asarray(im.convert("L"), dtype=np.float64)
    eps = 1e-6
    ix = np.abs(np.diff(a, axis=1)).mean()
    iy = np.abs(np.diff(a, axis=0)).mean()
    wx = np.abs(a[:, 0] - a[:, -1]).mean()
    wy = np.abs(a[0, :] - a[-1, :]).mean()
    return float(wx / (ix + eps)), float(wy / (iy + eps))


def wraps(ratio):
    return ratio <= SOURCE_WRAP_MAX


def has_seam(ratio):
    return ratio > RESULT_WRAP_MAX


def source_axes(ratios):
    """Axes on which a source texture wraps."""
    return tuple(ax for ax, r in zip(AXES, ratios) if wraps(r))


def needs_repair(source_ratios, result_ratios):
    """Axes on which the source wraps but the result shows a seam."""
    return tuple(ax for ax, rs, rr in zip(AXES, source_ratios, result_ratios)
                 if wraps(rs) and has_seam(rr))


def half_shift(size, axes):
    """(dx, dy) that rolls the wrap seams of the given axes into the middle."""
    w, h = size
    return (w // 2 if "x" in axes else 0, h // 2 if "y" in axes else 0)


def roll(im, dx, dy):
    a = np.asarray(im)
    return Image.fromarray(np.roll(a, (dy, dx), axis=(0, 1)), im.mode)


def seam_mask(size, axes):
    """uint8 (h, w) array, 1 inside the repaint band(s), 0 elsewhere."""
    w, h = size
    m = np.zeros((h, w), dtype=np.uint8)
    if "x" in axes:
        bx = max(BAND_MIN, round(BAND_FRAC * w))
        m[:, w // 2 - bx: w // 2 + bx] = 1
    if "y" in axes:
        by = max(BAND_MIN, round(BAND_FRAC * h))
        m[h // 2 - by: h // 2 + by, :] = 1
    return m


def _box_blur_1d(a, r, axis):
    """Mean over a window of 2r+1 samples along axis, edge-padded. A box
    blur of a hard edge is a linear ramp, which is all the feather needs."""
    if r <= 0:
        return a
    a = np.moveaxis(a, axis, 0)
    p = np.pad(a, [(r, r)] + [(0, 0)] * (a.ndim - 1), mode="edge")
    c = np.cumsum(p, axis=0, dtype=np.float64)
    c = np.concatenate([np.zeros_like(c[:1]), c], axis=0)
    n = a.shape[0]
    out = (c[2 * r + 1: 2 * r + 1 + n] - c[:n]) / (2 * r + 1)
    return np.moveaxis(out, 0, axis)


def feather(mask, px=FEATHER_PX):
    """Float mask in [0, 1] whose edges ramp linearly over about px pixels."""
    f = mask.astype(np.float64)
    r = px // 2
    f = _box_blur_1d(f, r, 0)
    f = _box_blur_1d(f, r, 1)
    return np.clip(f, 0.0, 1.0)


def stage_rgba(im, mask):
    """RGBA copy of im whose alpha is 0 exactly where mask is 1. ComfyUI's
    LoadImage exposes 1 - alpha as its MASK output, so the band reads as 1."""
    rgba = im.convert("RGBA")
    alpha = Image.fromarray(((1 - mask) * 255).astype(np.uint8), "L")
    rgba.putalpha(alpha)
    return rgba


def composite(base, painted, m):
    """base where m is 0, painted where m is 1, blended between."""
    b = np.asarray(base.convert("RGB"), dtype=np.float64)
    p = np.asarray(painted.convert("RGB"), dtype=np.float64)
    mm = m[..., None]
    out = b * (1.0 - mm) + p * mm
    return Image.fromarray(np.clip(np.rint(out), 0, 255).astype(np.uint8), "RGB")
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: LOCAL TESTS
Expected: all pass.

- [ ] **Step 5: Sync, run on the box, commit**

Run: SYNC, REMOTE TESTS, COMMIT ON THE BOX with
`MSG` = `feat(q2): pixel ops for canvas, wrap-seam ratio and seam repaint`.

---

### Task 3: Workflow templates and `comfy_client.py`

**Files:**
- Create: `$KIT/texture_prompt.json`
- Create: `$KIT/seam_prompt.json`
- Create: `$KIT/comfy_client.py`
- Create: `$KIT/tests/test_comfy_client.py`

**Interfaces:**
- Produces: `load_template(path, expected) -> dict` (raises `TemplateError`); `prepare_render(template, *, image_name, canvas, prompt, seed, prefix="q2/batch") -> dict`; `prepare_seam(template, *, roll_name, src_name, period, prompt, seed, prefix="q2/seam") -> dict`; `ComfyClient(url, input_dir, output_dir, http, sleep, clock)` with `.stage(image, name) -> name`, `.unstage(*names)`, `.run(graph, save_node=N_SAVE) -> Path`; `http_json(url, data=None, timeout=60, token=None)`; `ComfyError`; constants `RENDER_TEMPLATE`, `SEAM_TEMPLATE`, `RENDER_NODES`, `SEAM_NODES`, node ids `N_*`, `TILE_STRENGTH`, `CANNY_STRENGTH`, `NEGATIVE_PROMPT`, `POLL_SECONDS`, `HISTORY_TIMEOUT`, `INPUT_DIR`, `OUTPUT_DIR`.

- [ ] **Step 1: Write the two templates**

`$KIT/texture_prompt.json` (pass 1, spec §4.3):
```json
{
 "prompt": {
  "1": {"class_type": "LoadImage", "inputs": {"image": "example.png"}},
  "2": {"class_type": "ImageScale", "inputs": {"image": ["1", 0], "upscale_method": "bicubic", "width": 1024, "height": 1024, "crop": "disabled"}},
  "4": {"class_type": "CLIPLoader", "inputs": {"clip_name": "qwen_2.5_vl_7b.safetensors", "type": "qwen_image"}},
  "5": {"class_type": "UNETLoader", "inputs": {"unet_name": "split_files/diffusion_models/qwen_image_2512_fp8_e4m3fn.safetensors", "weight_dtype": "default"}},
  "6": {"class_type": "VAELoader", "inputs": {"vae_name": "qwen_image_vae.safetensors"}},
  "7": {"class_type": "ControlNetLoader", "inputs": {"control_net_name": "Qwen-Image-2512-Fun-Controlnet-Union-2602.safetensors"}},
  "8": {"class_type": "SetUnionControlNetType", "inputs": {"control_net": ["7", 0], "type": "tile"}},
  "18": {"class_type": "SetUnionControlNetType", "inputs": {"control_net": ["7", 0], "type": "canny/lineart/anime_lineart/mlsd"}},
  "9": {"class_type": "TextEncodeQwenImageEdit", "inputs": {"clip": ["4", 0], "prompt": ""}},
  "10": {"class_type": "TextEncodeQwenImageEdit", "inputs": {"clip": ["4", 0], "prompt": ""}},
  "16": {"class_type": "Canny", "inputs": {"image": ["1", 0], "low_threshold": 0.1, "high_threshold": 0.3}},
  "17": {"class_type": "ImageScale", "inputs": {"image": ["16", 0], "upscale_method": "nearest-exact", "width": 1024, "height": 1024, "crop": "disabled"}},
  "11": {"class_type": "ControlNetApplyAdvanced", "inputs": {"positive": ["9", 0], "negative": ["10", 0], "control_net": ["8", 0], "image": ["2", 0], "strength": 0.7, "start_percent": 0.0, "end_percent": 1.0, "vae": ["6", 0]}},
  "19": {"class_type": "ControlNetApplyAdvanced", "inputs": {"positive": ["11", 0], "negative": ["11", 1], "control_net": ["18", 0], "image": ["17", 0], "strength": 0.5, "start_percent": 0.0, "end_percent": 1.0, "vae": ["6", 0]}},
  "12": {"class_type": "EmptyQwenImageLayeredLatentImage", "inputs": {"width": 1024, "height": 1024, "layers": 0, "batch_size": 1}},
  "13": {"class_type": "KSampler", "inputs": {"model": ["5", 0], "seed": 0, "steps": 30, "cfg": 4.0, "sampler_name": "euler", "scheduler": "simple", "positive": ["19", 0], "negative": ["19", 1], "latent_image": ["12", 0], "denoise": 1.0}},
  "14": {"class_type": "VAEDecode", "inputs": {"samples": ["13", 0], "vae": ["6", 0]}},
  "15": {"class_type": "SaveImage", "inputs": {"images": ["14", 0], "filename_prefix": "q2/batch"}}
 }
}
```

`$KIT/seam_prompt.json` (pass 2, spec §4.5; node 1 is the rolled RGBA crop whose alpha carries the band, node 21 the rolled source period):
```json
{
 "prompt": {
  "1": {"class_type": "LoadImage", "inputs": {"image": "example.png"}},
  "21": {"class_type": "LoadImage", "inputs": {"image": "example.png"}},
  "2": {"class_type": "ImageScale", "inputs": {"image": ["21", 0], "upscale_method": "bicubic", "width": 1024, "height": 1024, "crop": "disabled"}},
  "4": {"class_type": "CLIPLoader", "inputs": {"clip_name": "qwen_2.5_vl_7b.safetensors", "type": "qwen_image"}},
  "5": {"class_type": "UNETLoader", "inputs": {"unet_name": "split_files/diffusion_models/qwen_image_2512_fp8_e4m3fn.safetensors", "weight_dtype": "default"}},
  "6": {"class_type": "VAELoader", "inputs": {"vae_name": "qwen_image_vae.safetensors"}},
  "7": {"class_type": "ControlNetLoader", "inputs": {"control_net_name": "Qwen-Image-2512-Fun-Controlnet-Union-2602.safetensors"}},
  "8": {"class_type": "SetUnionControlNetType", "inputs": {"control_net": ["7", 0], "type": "tile"}},
  "18": {"class_type": "SetUnionControlNetType", "inputs": {"control_net": ["7", 0], "type": "canny/lineart/anime_lineart/mlsd"}},
  "9": {"class_type": "TextEncodeQwenImageEdit", "inputs": {"clip": ["4", 0], "prompt": ""}},
  "10": {"class_type": "TextEncodeQwenImageEdit", "inputs": {"clip": ["4", 0], "prompt": ""}},
  "16": {"class_type": "Canny", "inputs": {"image": ["21", 0], "low_threshold": 0.1, "high_threshold": 0.3}},
  "17": {"class_type": "ImageScale", "inputs": {"image": ["16", 0], "upscale_method": "nearest-exact", "width": 1024, "height": 1024, "crop": "disabled"}},
  "11": {"class_type": "ControlNetApplyAdvanced", "inputs": {"positive": ["9", 0], "negative": ["10", 0], "control_net": ["8", 0], "image": ["2", 0], "strength": 0.7, "start_percent": 0.0, "end_percent": 1.0, "vae": ["6", 0]}},
  "19": {"class_type": "ControlNetApplyAdvanced", "inputs": {"positive": ["11", 0], "negative": ["11", 1], "control_net": ["18", 0], "image": ["17", 0], "strength": 0.5, "start_percent": 0.0, "end_percent": 1.0, "vae": ["6", 0]}},
  "20": {"class_type": "InpaintModelConditioning", "inputs": {"positive": ["19", 0], "negative": ["19", 1], "vae": ["6", 0], "pixels": ["1", 0], "mask": ["1", 1], "noise_mask": true}},
  "13": {"class_type": "KSampler", "inputs": {"model": ["5", 0], "seed": 0, "steps": 30, "cfg": 4.0, "sampler_name": "euler", "scheduler": "simple", "positive": ["20", 0], "negative": ["20", 1], "latent_image": ["20", 2], "denoise": 1.0}},
  "14": {"class_type": "VAEDecode", "inputs": {"samples": ["13", 0], "vae": ["6", 0]}},
  "15": {"class_type": "SaveImage", "inputs": {"images": ["14", 0], "filename_prefix": "q2/seam"}}
 }
}
```

Node input names were confirmed against the server's `object_info` on 2026-09-02: `InpaintModelConditioning(positive, negative, vae, pixels, mask, noise_mask) -> (CONDITIONING, CONDITIONING, LATENT)`, `LoadImage -> (IMAGE, MASK)`, `ImageScale` methods include `bicubic` and `nearest-exact`.

- [ ] **Step 2: Write the failing tests**

`$KIT/tests/test_comfy_client.py`:
```python
import json

import pytest
from PIL import Image

import comfy_client as cc


def test_render_template_has_expected_nodes_and_wiring():
    g = cc.load_template(cc.RENDER_TEMPLATE, cc.RENDER_NODES)
    assert g[cc.N_UNION_TILE]["inputs"]["type"] == cc.UNION_TILE
    assert g[cc.N_UNION_CANNY]["inputs"]["type"] == cc.UNION_CANNY
    assert g[cc.N_TILE_APPLY]["inputs"]["image"] == [cc.N_TILE_SCALE, 0]
    assert g[cc.N_CANNY_APPLY]["inputs"]["positive"] == [cc.N_TILE_APPLY, 0]
    assert g[cc.N_CANNY_APPLY]["inputs"]["image"] == [cc.N_CANNY_SCALE, 0]
    assert g[cc.N_SAMPLER]["inputs"]["positive"] == [cc.N_CANNY_APPLY, 0]
    assert g[cc.N_SAMPLER]["inputs"]["latent_image"] == [cc.N_LATENT, 0]
    assert g[cc.N_CANNY]["inputs"]["image"] == [cc.N_LOAD, 0]
    assert g[cc.N_TILE_SCALE]["inputs"]["image"] == [cc.N_LOAD, 0]


def test_seam_template_has_expected_nodes_and_wiring():
    g = cc.load_template(cc.SEAM_TEMPLATE, cc.SEAM_NODES)
    inp = g[cc.N_INPAINT]["inputs"]
    assert inp["pixels"] == [cc.N_LOAD, 0] and inp["mask"] == [cc.N_LOAD, 1]
    assert inp["positive"] == [cc.N_CANNY_APPLY, 0] and inp["noise_mask"] is True
    assert g[cc.N_SAMPLER]["inputs"]["latent_image"] == [cc.N_INPAINT, 2]
    assert g[cc.N_SAMPLER]["inputs"]["positive"] == [cc.N_INPAINT, 0]
    assert g[cc.N_TILE_SCALE]["inputs"]["image"] == [cc.N_LOAD_SRC, 0]
    assert g[cc.N_CANNY]["inputs"]["image"] == [cc.N_LOAD_SRC, 0]
    assert cc.N_LATENT not in g


def test_load_template_rejects_wrong_class_and_union_type(tmp_path):
    bad = json.loads(cc.RENDER_TEMPLATE.read_text())
    bad["prompt"]["13"]["class_type"] = "KSamplerAdvanced"
    bad["prompt"]["8"]["inputs"]["type"] = "depth"
    p = tmp_path / "t.json"
    p.write_text(json.dumps(bad))
    with pytest.raises(cc.TemplateError) as e:
        cc.load_template(p, cc.RENDER_NODES)
    assert "node 13" in str(e.value) and "node 8" in str(e.value)


def test_prepare_render_sets_every_per_image_field():
    t = cc.load_template(cc.RENDER_TEMPLATE, cc.RENDER_NODES)
    g = cc.prepare_render(t, image_name="__q2_src.png", canvas=(1280, 1024),
                          prompt="rusty steel", seed=123)
    assert g["1"]["inputs"]["image"] == "__q2_src.png"
    for node in ("2", "12", "17"):
        assert (g[node]["inputs"]["width"], g[node]["inputs"]["height"]) == (1280, 1024)
    assert g["9"]["inputs"]["prompt"] == "rusty steel"
    assert g["10"]["inputs"]["prompt"] == cc.NEGATIVE_PROMPT
    assert g["13"]["inputs"]["seed"] == 123
    assert g["11"]["inputs"]["strength"] == cc.TILE_STRENGTH
    assert g["19"]["inputs"]["strength"] == cc.CANNY_STRENGTH
    assert g["15"]["inputs"]["filename_prefix"] == "q2/batch"
    assert t["1"]["inputs"]["image"] == "example.png"        # template untouched


def test_prepare_seam_sets_both_images_and_period():
    t = cc.load_template(cc.SEAM_TEMPLATE, cc.SEAM_NODES)
    g = cc.prepare_seam(t, roll_name="__q2_roll.png", src_name="__q2_rollsrc.png",
                        period=(1024, 256), prompt="p", seed=7)
    assert g["1"]["inputs"]["image"] == "__q2_roll.png"
    assert g["21"]["inputs"]["image"] == "__q2_rollsrc.png"
    for node in ("2", "17"):
        assert (g[node]["inputs"]["width"], g[node]["inputs"]["height"]) == (1024, 256)
    assert g["9"]["inputs"]["prompt"] == "p" and g["13"]["inputs"]["seed"] == 7
    assert g["15"]["inputs"]["filename_prefix"] == "q2/seam"


class FakeHttp:
    """Answers /prompt with a fixed id and /history with `history` after two
    empty polls."""

    def __init__(self, history):
        self.calls, self.history, self.polls = [], history, 0

    def __call__(self, url, data=None, timeout=60, token=None):
        self.calls.append((url, data))
        if url.endswith("/prompt"):
            return {"prompt_id": "pid1"}
        self.polls += 1
        return {} if self.polls < 3 else {"pid1": self.history}


def test_client_stage_run_unstage(tmp_path):
    hist = {"status": {"status_str": "success"},
            "outputs": {"15": {"images": [{"filename": "batch_00001_.png", "subfolder": "q2"}]}}}
    http = FakeHttp(hist)
    slept = []
    c = cc.ComfyClient(input_dir=tmp_path / "in", output_dir=tmp_path / "out",
                       http=http, sleep=slept.append, clock=lambda: 0)
    name = c.stage(Image.new("RGB", (4, 4)), "__q2_src.png")
    assert name == "__q2_src.png" and (tmp_path / "in" / name).exists()
    out = c.run({"1": {}})
    assert out == tmp_path / "out" / "q2" / "batch_00001_.png"
    assert http.calls[0][0].endswith("/prompt")
    assert json.loads(http.calls[0][1])["prompt"] == {"1": {}}
    assert slept == [cc.POLL_SECONDS, cc.POLL_SECONDS]
    c.unstage(name, "never_staged.png")
    assert not (tmp_path / "in" / name).exists()


def test_client_run_reports_failing_node():
    hist = {"status": {"status_str": "error", "messages": [
        ["execution_start", {}],
        ["execution_error", {"node_id": "3", "node_type": "HostedVLMAPI",
                             "exception_message": "Connection error.\n"}]]},
            "outputs": {}}
    c = cc.ComfyClient(http=FakeHttp(hist), sleep=lambda s: None, clock=lambda: 0)
    with pytest.raises(cc.ComfyError, match=r"node 3 \(HostedVLMAPI\): Connection error\."):
        c.run({})


def test_client_run_times_out():
    t = [0]

    def clock():
        t[0] += 1000
        return t[0]

    c = cc.ComfyClient(http=FakeHttp({}), sleep=lambda s: None, clock=clock)
    with pytest.raises(cc.ComfyError, match="no history"):
        c.run({})
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: LOCAL TESTS
Expected: `ModuleNotFoundError: No module named 'comfy_client'`

- [ ] **Step 4: Write `comfy_client.py`**

`$KIT/comfy_client.py`:
```python
"""ComfyUI side of the Quake II texture kit: the two API-format templates,
the node ids the driver writes, and the queue / wait / fetch cycle.

Spec sections 4.3, 4.5 and 4.6.
"""
import json
import time
import urllib.request
from pathlib import Path

COMFY_URL = "http://127.0.0.1:8188"
COMFY_DIR = Path.home() / "comfy" / "ComfyUI"
INPUT_DIR = COMFY_DIR / "input"
OUTPUT_DIR = COMFY_DIR / "output"
HISTORY_TIMEOUT = 1800
POLL_SECONDS = 2

KIT_DIR = Path(__file__).resolve().parent
RENDER_TEMPLATE = KIT_DIR / "texture_prompt.json"
SEAM_TEMPLATE = KIT_DIR / "seam_prompt.json"

# Tuning knobs (spec 5.5). Both ControlNets share the union model.
TILE_STRENGTH = 0.7
CANNY_STRENGTH = 0.5
NEGATIVE_PROMPT = ("blurry, low quality, perspective, camera angle, vignette, "
                   "depth of field, text, lettering, logo, watermark, seam, border, frame")
UNION_TILE = "tile"
UNION_CANNY = "canny/lineart/anime_lineart/mlsd"

# Node ids shared by both templates.
N_LOAD, N_TILE_SCALE, N_UNION_TILE, N_POS, N_NEG = "1", "2", "8", "9", "10"
N_TILE_APPLY, N_LATENT, N_SAMPLER, N_SAVE = "11", "12", "13", "15"
N_CANNY, N_CANNY_SCALE, N_UNION_CANNY, N_CANNY_APPLY = "16", "17", "18", "19"
# Pass 2 only.
N_INPAINT, N_LOAD_SRC = "20", "21"

_COMMON = {
    N_LOAD: "LoadImage", N_TILE_SCALE: "ImageScale",
    N_UNION_TILE: "SetUnionControlNetType", N_POS: "TextEncodeQwenImageEdit",
    N_NEG: "TextEncodeQwenImageEdit", N_TILE_APPLY: "ControlNetApplyAdvanced",
    N_SAMPLER: "KSampler", N_SAVE: "SaveImage", N_CANNY: "Canny",
    N_CANNY_SCALE: "ImageScale", N_UNION_CANNY: "SetUnionControlNetType",
    N_CANNY_APPLY: "ControlNetApplyAdvanced",
}
RENDER_NODES = {**_COMMON, N_LATENT: "EmptyQwenImageLayeredLatentImage"}
SEAM_NODES = {**_COMMON, N_INPAINT: "InpaintModelConditioning", N_LOAD_SRC: "LoadImage"}


class TemplateError(ValueError):
    pass


class ComfyError(RuntimeError):
    pass


def load_template(path, expected):
    """The 'prompt' graph of an API-format workflow file, after checking that
    every node the driver writes exists with the class it expects. Editing a
    template by hand and breaking the driver fails here, not mid-run."""
    graph = json.loads(Path(path).read_text())["prompt"]
    problems = []
    for node, cls in expected.items():
        got = graph.get(node, {}).get("class_type")
        if got != cls:
            problems.append(f"node {node}: expected {cls}, found {got}")
    if graph.get(N_UNION_TILE, {}).get("inputs", {}).get("type") != UNION_TILE:
        problems.append(f"node {N_UNION_TILE}: union type must be {UNION_TILE!r}")
    if graph.get(N_UNION_CANNY, {}).get("inputs", {}).get("type") != UNION_CANNY:
        problems.append(f"node {N_UNION_CANNY}: union type must be {UNION_CANNY!r}")
    if problems:
        raise TemplateError(f"{path}: " + "; ".join(problems))
    return graph


def _set_size(graph, node, size):
    graph[node]["inputs"]["width"] = int(size[0])
    graph[node]["inputs"]["height"] = int(size[1])


def _set_common(graph, prompt, seed, prefix):
    graph[N_POS]["inputs"]["prompt"] = prompt
    graph[N_NEG]["inputs"]["prompt"] = NEGATIVE_PROMPT
    graph[N_TILE_APPLY]["inputs"]["strength"] = TILE_STRENGTH
    graph[N_CANNY_APPLY]["inputs"]["strength"] = CANNY_STRENGTH
    graph[N_SAMPLER]["inputs"]["seed"] = int(seed)
    graph[N_SAVE]["inputs"]["filename_prefix"] = prefix


def prepare_render(template, *, image_name, canvas, prompt, seed, prefix="q2/batch"):
    """Pass 1 graph for one texture: staged tiled canvas in, render out."""
    g = json.loads(json.dumps(template))
    g[N_LOAD]["inputs"]["image"] = image_name
    for node in (N_TILE_SCALE, N_LATENT, N_CANNY_SCALE):
        _set_size(g, node, canvas)
    _set_common(g, prompt, seed, prefix)
    return g


def prepare_seam(template, *, roll_name, src_name, period, prompt, seed, prefix="q2/seam"):
    """Pass 2 graph: rolled RGBA crop (alpha = band) and rolled source in."""
    g = json.loads(json.dumps(template))
    g[N_LOAD]["inputs"]["image"] = roll_name
    g[N_LOAD_SRC]["inputs"]["image"] = src_name
    for node in (N_TILE_SCALE, N_CANNY_SCALE):
        _set_size(g, node, period)
    _set_common(g, prompt, seed, prefix)
    return g


def http_json(url, data=None, timeout=60, token=None):
    headers = {}
    if data is not None:
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


def _failure_summary(status):
    """'node 3 (HostedVLMAPI): message' from a history status, else the raw dict."""
    for item in status.get("messages", []):
        if isinstance(item, list) and len(item) == 2 and item[0] == "execution_error":
            d = item[1]
            return (f"node {d.get('node_id')} ({d.get('node_type')}): "
                    f"{str(d.get('exception_message', '')).strip()}")
    return str(status)


class ComfyClient:
    """Stage inputs, queue a graph, wait for it, hand back the saved file.
    The transport and clock are injectable so the cycle is testable offline."""

    def __init__(self, url=COMFY_URL, input_dir=INPUT_DIR, output_dir=OUTPUT_DIR,
                 http=http_json, sleep=time.sleep, clock=time.time):
        self.url = url
        self.input_dir, self.output_dir = Path(input_dir), Path(output_dir)
        self.http, self.sleep, self.clock = http, sleep, clock

    def stage(self, image, name):
        self.input_dir.mkdir(parents=True, exist_ok=True)
        image.save(self.input_dir / name)
        return name

    def unstage(self, *names):
        for n in names:
            (self.input_dir / n).unlink(missing_ok=True)

    def run(self, graph, save_node=N_SAVE):
        resp = self.http(f"{self.url}/prompt", json.dumps({"prompt": graph}).encode())
        entry = self._wait(resp["prompt_id"])
        status = entry.get("status", {})
        if status.get("status_str") != "success":
            raise ComfyError(f"execution failed: {_failure_summary(status)}")
        img = entry["outputs"][save_node]["images"][0]
        return self.output_dir.joinpath(img.get("subfolder") or "", img["filename"])

    def _wait(self, prompt_id):
        deadline = self.clock() + HISTORY_TIMEOUT
        while self.clock() < deadline:
            hist = self.http(f"{self.url}/history/{prompt_id}")
            if prompt_id in hist:
                return hist[prompt_id]
            self.sleep(POLL_SECONDS)
        raise ComfyError(f"no history for {prompt_id} within {HISTORY_TIMEOUT}s")
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: LOCAL TESTS
Expected: all pass.

- [ ] **Step 6: Validate both graphs against the live server without rendering**

ComfyUI rejects a malformed graph at queue time with a 400 whose body names the node. Queue each template with a tiny staged image and immediately interrupt, so no GPU minutes are spent:

```bash
SYNC
ssh $BOX 'cd ~/comfy/batch_regen_q2 && . ~/comfy/env.sh && "$COMFY_PYTHON" - <<'"'"'EOF'"'"'
import json, urllib.error, urllib.request
from PIL import Image
import comfy_client as cc
Image.new("RGB", (64, 64), (90, 90, 90)).save(cc.INPUT_DIR / "__q2_src.png")
Image.new("RGBA", (256, 256), (90, 90, 90, 255)).save(cc.INPUT_DIR / "__q2_roll.png")
Image.new("RGB", (64, 64), (90, 90, 90)).save(cc.INPUT_DIR / "__q2_rollsrc.png")
r = cc.prepare_render(cc.load_template(cc.RENDER_TEMPLATE, cc.RENDER_NODES),
                      image_name="__q2_src.png", canvas=(1024, 1024), prompt="grey concrete", seed=1)
s = cc.prepare_seam(cc.load_template(cc.SEAM_TEMPLATE, cc.SEAM_NODES),
                    roll_name="__q2_roll.png", src_name="__q2_rollsrc.png", period=(256, 256), prompt="grey concrete", seed=1)
for name, g in (("render", r), ("seam", s)):
    try:
        resp = cc.http_json(f"{cc.COMFY_URL}/prompt", json.dumps({"prompt": g}).encode())
        print(name, "accepted", resp.get("prompt_id"), "errors:", resp.get("node_errors"))
    except urllib.error.HTTPError as e:
        print(name, "REJECTED", e.read().decode()[:2000])
    urllib.request.urlopen(urllib.request.Request(f"{cc.COMFY_URL}/interrupt", data=b"", method="POST")).read()
    urllib.request.urlopen(urllib.request.Request(f"{cc.COMFY_URL}/queue", data=json.dumps({"clear": True}).encode(), headers={"Content-Type": "application/json"})).read()
for n in ("__q2_src.png", "__q2_roll.png", "__q2_rollsrc.png"):
    (cc.INPUT_DIR / n).unlink(missing_ok=True)
EOF'
```
Expected: both lines say `accepted` with `errors: {}`. A `REJECTED` line names the node and input to fix in the template; fix and re-run before continuing.

- [ ] **Step 7: Run on the box, commit**

Run: REMOTE TESTS, COMMIT ON THE BOX with
`MSG` = `feat(q2): render and seam workflow templates with a ComfyUI client`.

---

### Task 4: `vlm_client.py`

**Files:**
- Create: `$KIT/vlm_client.py`
- Create: `$KIT/tests/test_vlm_client.py`

**Interfaces:**
- Consumes: `comfy_client.http_json`.
- Produces: `load_api_key(env_file=ENV_FILE) -> str`; `image_data_url(im, min_edge=512) -> str`; `with_retries(fn, delays=RETRY_DELAYS, sleep=time.sleep, log=None)`; `describe_texture(key, im, axes, anchor="", http=http_json, sleep=time.sleep, log=None) -> str`; `folder_anchor(key, images, http=http_json, sleep=time.sleep, log=None) -> str`; `spread(items, n) -> list`; `JsonCache(path)` with `.get(key)`, `.set(key, value)`; `VLMError`; constants `PROMPT_TAIL`, `ANCHOR_MAX_IMAGES`, `VLM_MODEL`, `RETRY_DELAYS`.

- [ ] **Step 1: Write the failing tests**

`$KIT/tests/test_vlm_client.py`:
```python
import base64
import io
import json

import pytest
from PIL import Image

import vlm_client as vc


def test_load_api_key(tmp_path):
    env = tmp_path / ".env"
    env.write_text("# comment\nOTHER=1\nDASHSCOPE_API_KEY=sk-abc \n")
    assert vc.load_api_key(env) == "sk-abc"
    assert vc.load_api_key(tmp_path / "missing") == ""


def decode(url):
    assert url.startswith("data:image/png;base64,")
    return Image.open(io.BytesIO(base64.b64decode(url.split(",", 1)[1])))


def test_image_data_url_upscales_small_images_by_nearest():
    im = Image.new("RGB", (64, 16), (10, 20, 30))
    d = decode(vc.image_data_url(im))
    assert d.size == (512, 128) and d.getpixel((0, 0)) == (10, 20, 30)
    assert decode(vc.image_data_url(Image.new("RGB", (600, 300)))).size == (600, 300)


def test_with_retries_sleeps_then_succeeds():
    calls, slept = [], []

    def fn():
        calls.append(1)
        if len(calls) < 3:
            raise ConnectionError("down")
        return "ok"

    assert vc.with_retries(fn, sleep=slept.append) == "ok"
    assert slept == [5, 15]


def test_with_retries_raises_last_error_after_three_retries():
    slept, logged = [], []

    def fn():
        raise ConnectionError("still down")

    with pytest.raises(ConnectionError, match="still down"):
        vc.with_retries(fn, sleep=slept.append, log=logged.append)
    assert slept == [5, 15, 45]
    assert len(logged) == 3 and "retrying in 45s" in logged[-1]


def reply(text):
    return {"choices": [{"message": {"content": text}}]}


def test_describe_texture_builds_prompt_with_tiling_and_anchor():
    seen = []

    def http(url, data=None, timeout=60, token=None):
        seen.append((url, json.loads(data), token))
        return reply("  brushed steel panels, rivets  ")

    im = Image.new("RGB", (64, 64), (100, 100, 100))
    p = vc.describe_texture("sk-1", im, ("x", "y"), anchor="cold grey steel",
                            http=http, sleep=lambda s: None)
    assert p == ("brushed steel panels, rivets, " + vc.PROMPT_TAIL
                 + ". UNIT MATERIAL STANDARD: cold grey steel")
    url, body, token = seen[0]
    assert url.endswith("/chat/completions") and token == "sk-1"
    assert body["model"] == vc.VLM_MODEL
    content = body["messages"][0]["content"]
    assert "repeats seamlessly in both directions" in content[0]["text"]
    assert content[1]["image_url"]["url"].startswith("data:image/png;base64,")


def test_describe_texture_without_tiling_or_anchor():
    def http(url, data=None, timeout=60, token=None):
        assert "repeats seamlessly" not in json.loads(data)["messages"][0]["content"][0]["text"]
        return reply("concrete")

    p = vc.describe_texture("k", Image.new("RGB", (16, 16)), (), http=http, sleep=lambda s: None)
    assert p == "concrete, " + vc.PROMPT_TAIL


def test_describe_texture_horizontal_only():
    def http(url, data=None, timeout=60, token=None):
        assert "seamlessly horizontally" in json.loads(data)["messages"][0]["content"][0]["text"]
        return reply("trim")

    vc.describe_texture("k", Image.new("RGB", (16, 16)), ("x",), http=http, sleep=lambda s: None)


def test_describe_texture_empty_reply_is_an_error_after_retries():
    slept = []

    def http(url, data=None, timeout=60, token=None):
        return reply("   ")

    with pytest.raises(vc.VLMError, match="empty"):
        vc.describe_texture("k", Image.new("RGB", (16, 16)), (), http=http, sleep=slept.append)
    assert slept == [5, 15, 45]


def test_folder_anchor_sends_at_most_four_images():
    seen = []

    def http(url, data=None, timeout=60, token=None):
        seen.append(json.loads(data))
        return reply("rusty industrial steel")

    ims = [Image.new("RGB", (16, 16), (i, i, i)) for i in range(6)]
    assert vc.folder_anchor("k", ims, http=http, sleep=lambda s: None) == "rusty industrial steel"
    assert len(seen[0]["messages"][0]["content"]) == 1 + vc.ANCHOR_MAX_IMAGES
    assert vc.folder_anchor("k", [], http=http) == ""


def test_spread_picks_evenly():
    assert vc.spread(list(range(10)), 4) == [0, 2, 5, 7]
    assert vc.spread([1, 2], 4) == [1, 2]
    assert vc.spread([], 4) == []


def test_json_cache_persists_and_survives_garbage(tmp_path):
    path = tmp_path / "out" / ".prompts.json"
    c = vc.JsonCache(path)
    assert c.get("a") is None
    c.set("a", "x")
    assert vc.JsonCache(path).get("a") == "x"
    path.write_text("{not json")
    assert vc.JsonCache(path).get("a") is None
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: LOCAL TESTS
Expected: `ModuleNotFoundError: No module named 'vlm_client'`

- [ ] **Step 3: Write `vlm_client.py`**

`$KIT/vlm_client.py`:
```python
"""DashScope VLM calls for the Quake II texture kit: one material prompt
per texture, one style anchor per folder, retries, and JSON caches.

Spec sections 5.1 and 5.2.
"""
import base64
import io
import json
import time
from pathlib import Path

from PIL import Image

from comfy_client import http_json

ENV_FILE = Path.home() / "comfy" / ".env"
VLM_BASE_URL = "https://dashscope-intl.aliyuncs.com/compatible-mode/v1"
VLM_MODEL = "qwen3-vl-plus"
RETRY_DELAYS = (5, 15, 45)      # three attempts; sleep between them
VLM_MIN_EDGE = 512              # nearest-upscale so the VLM can read pixel art
ANCHOR_MAX_IMAGES = 4

TEXTURE_QUESTION = (
    "This is a texture from a 1997 video game, shown enlarged. Describe the "
    "real-world material it depicts so that a photorealistic image generator "
    "can recreate it as a straight-on, evenly lit texture photograph. Cover: "
    "the material (metal, concrete, rock, painted panel, grating, ...), its "
    "surface finish and wear, the layout of panels, seams, bolts, pipes, "
    "lights or markings exactly as arranged and proportioned in the image, "
    "and its exact colours.{tiling} Rules: no perspective, no camera angle, "
    "no dramatic lighting, no vignette, no cast shadows from outside objects; "
    "do not invent objects that are not in the image; do not mention pixels, "
    "low resolution or video games; do not describe any text or lettering. "
    "Output only one comma-separated string, no markdown."
)
TILING = {
    ("x",): " The surface repeats seamlessly horizontally; describe it as one continuous surface.",
    ("y",): " The surface repeats seamlessly vertically; describe it as one continuous surface.",
    ("x", "y"): " The surface repeats seamlessly in both directions; describe it as one continuous surface.",
    (): "",
}
PROMPT_TAIL = ("photorealistic PBR texture, flat even lighting, orthographic "
               "straight-on view, highly detailed, 8k")
ANCHOR_QUESTION = (
    "These are textures from the same area of a game. Write one sentence that "
    "specifies the shared material palette and surface standard all of them "
    "must follow: dominant materials, colour palette, wear level and lighting "
    "mood. Output only the sentence."
)


class VLMError(RuntimeError):
    pass


def load_api_key(env_file=ENV_FILE):
    try:
        for line in Path(env_file).read_text().splitlines():
            if line.startswith("DASHSCOPE_API_KEY="):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


def image_data_url(im, min_edge=VLM_MIN_EDGE):
    """PNG data URL of im, nearest-upscaled so its long edge is >= min_edge."""
    w, h = im.size
    k = max(1, -(-min_edge // max(w, h)))          # ceil division
    if k > 1:
        im = im.resize((w * k, h * k), Image.NEAREST)
    buf = io.BytesIO()
    im.convert("RGB").save(buf, "PNG")
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def with_retries(fn, delays=RETRY_DELAYS, sleep=time.sleep, log=None):
    """Call fn(); on any exception sleep and retry, once per delay. The last
    exception is raised when every attempt has failed."""
    delays = list(delays)
    last = None
    for attempt in range(len(delays) + 1):
        try:
            return fn()
        except Exception as e:      # any transport or parse failure
            last = e
            if attempt == len(delays):
                break
            if log:
                log(f"  vlm attempt {attempt + 1} failed: {e}; retrying in {delays[attempt]}s")
            sleep(delays[attempt])
    raise last


def chat(key, content, max_tokens, http=http_json):
    body = json.dumps({"model": VLM_MODEL, "max_tokens": max_tokens,
                       "messages": [{"role": "user", "content": content}]}).encode()
    resp = http(f"{VLM_BASE_URL}/chat/completions", body, timeout=180, token=key)
    text = resp["choices"][0]["message"]["content"].strip()
    if not text:
        raise VLMError("empty response")
    return text


def describe_texture(key, im, axes, anchor="", http=http_json, sleep=time.sleep, log=None):
    """The full positive prompt for one texture: VLM description, fixed tail,
    folder anchor. Raises after the retries are exhausted; no fallback."""
    question = TEXTURE_QUESTION.format(tiling=TILING[tuple(axes)])
    content = [{"type": "text", "text": question},
               {"type": "image_url", "image_url": {"url": image_data_url(im)}}]
    text = with_retries(lambda: chat(key, content, 400, http), sleep=sleep, log=log)
    prompt = f"{text}, {PROMPT_TAIL}"
    if anchor:
        prompt += f". UNIT MATERIAL STANDARD: {anchor}"
    return prompt


def folder_anchor(key, images, http=http_json, sleep=time.sleep, log=None):
    """One sentence for a folder from up to ANCHOR_MAX_IMAGES PIL images."""
    pick = list(images)[:ANCHOR_MAX_IMAGES]
    if not pick:
        return ""
    content = [{"type": "text", "text": ANCHOR_QUESTION}]
    content += [{"type": "image_url", "image_url": {"url": image_data_url(im)}} for im in pick]
    return with_retries(lambda: chat(key, content, 200, http), sleep=sleep, log=log)


def spread(items, n):
    """Up to n items spaced evenly through the list."""
    items = list(items)
    if len(items) <= n:
        return items
    step = len(items) / n
    return [items[int(i * step)] for i in range(n)]


class JsonCache:
    """A dict persisted to a JSON file on every set(). Unreadable files
    start empty; the caches are all regenerable."""

    def __init__(self, path):
        self.path = Path(path)
        self.data = {}
        if self.path.exists():
            try:
                self.data = json.loads(self.path.read_text())
            except Exception:
                self.data = {}

    def get(self, key):
        return self.data.get(key)

    def set(self, key, value):
        self.data[key] = value
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_name(self.path.name + ".tmp")
        tmp.write_text(json.dumps(self.data, indent=1, sort_keys=True))
        tmp.replace(self.path)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: LOCAL TESTS
Expected: all pass.

- [ ] **Step 5: One live VLM call from the box to prove the key and endpoint**

```bash
SYNC
ssh $BOX 'cd ~/comfy/batch_regen_q2 && . ~/comfy/env.sh && "$COMFY_PYTHON" - <<'"'"'EOF'"'"'
import numpy as np
from PIL import Image
import vlm_client as vc
key = vc.load_api_key()
assert key, "no key in ~/comfy/.env"
y, x = np.mgrid[0:64, 0:64]
panel = (110 + 40 * ((x // 16 + y // 16) % 2)).astype(np.uint8)   # grey checker plates
im = Image.fromarray(np.stack([panel, panel, panel + 10], axis=-1), "RGB")
print(vc.describe_texture(key, im, ("x", "y"), anchor="", log=print)[:400])
EOF'
```
Expected: one comma-separated description ending in the `PROMPT_TAIL` text. A network failure prints three `vlm attempt N failed` lines and raises, which is the designed behaviour; retry later rather than changing code.

- [ ] **Step 6: Run on the box, commit**

Run: REMOTE TESTS, COMMIT ON THE BOX with
`MSG` = `feat(q2): DashScope prompts with retries and JSON caches`.

---

### Task 5: `batch_textures.py` driver

**Files:**
- Create: `$KIT/batch_textures.py`
- Create: `$KIT/tests/test_batch_textures.py`

**Interfaces:**
- Consumes: everything from Tasks 1 to 4 by the names listed there.
- Produces: `Context` dataclass (`client, render_template, seam_template, key, prompts, anchors, seams, src_root, dst_root, no_seam=False, keep_stages=False, describe=None, anchor_fn=None, anchor_failed=set(), log=print`); `anchor_for(ctx, rel, candidates) -> str`; `process_one(ctx, rel, target, candidates) -> (repainted_axes, remaining_axes)`; `verify_render(path, expect)`; `write_atomic(image, dst, expect)`; `verify_tree(src_root, dst_root) -> (problems, seam_warnings)`; `select_files(src_root, args) -> list[Path]`; `build_parser()`; `main(argv=None) -> int`; constants `SRC_ROOT`, `DST_ROOT`, `STAGE_SRC`, `STAGE_ROLL`, `STAGE_ROLLSRC`.

- [ ] **Step 1: Write the failing tests**

`$KIT/tests/test_batch_textures.py`:
```python
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

import batch_textures as bt
import comfy_client as cc
import texplan as tp
import vlm_client as vc


def periodic(w, h, seed=0):
    """Wraps on both axes (cosines start at zero slope). seed offsets brightness."""
    y, x = np.mgrid[0:h, 0:w]
    v = (120 + (seed % 7) + 60 * np.cos(2 * np.pi * 3 * x / w)
         + 50 * np.cos(2 * np.pi * 2 * y / h))
    arr = np.clip(v, 0, 255).astype(np.uint8)
    return Image.fromarray(np.stack([arr] * 3, axis=-1), "RGB")


def make_tree(root):
    """Two folders: walls, a trim, an animated pair, a flat file, a tool
    texture, and a JSON sidecar."""
    (root / "e1u1").mkdir(parents=True)
    (root / "e2u1").mkdir(parents=True)
    periodic(64, 64).save(root / "e1u1" / "metal1_1.png")
    periodic(64, 16, 1).save(root / "e1u1" / "trim1_1.png")
    periodic(32, 32, 2).save(root / "e1u1" / "+0comp1.png")
    periodic(32, 32, 3).save(root / "e1u1" / "+1comp1.png")
    Image.new("RGB", (32, 32)).save(root / "e1u1" / "black.png")
    periodic(64, 64, 4).save(root / "e1u1" / "clip.png")
    periodic(128, 128, 5).save(root / "e2u1" / "floor1_1.png")
    (root / "manifest.json").write_text("{}")


class FakeClient:
    """Stands in for ComfyClient. Pass 1 'renders' by nearest-upscaling the
    staged canvas (seamless stays seamless); with `seamy` the left half is
    inverted so the x wrap breaks. Pass 2 paints flat grey."""

    def __init__(self, tmp, seamy=False):
        self.input_dir, self.output_dir = tmp / "in", tmp / "out"
        self.input_dir.mkdir()
        self.output_dir.mkdir()
        self.staged, self.runs, self.seamy = {}, [], seamy

    def stage(self, image, name):
        image.save(self.input_dir / name)
        self.staged[name] = image.copy()
        return name

    def unstage(self, *names):
        for n in names:
            (self.input_dir / n).unlink(missing_ok=True)

    def run(self, graph, save_node=cc.N_SAVE):
        self.runs.append(graph)
        src = self.staged[graph["1"]["inputs"]["image"]].convert("RGB")
        if cc.N_LATENT in graph:                          # pass 1
            w, h = graph[cc.N_LATENT]["inputs"]["width"], graph[cc.N_LATENT]["inputs"]["height"]
            out = src.resize((w, h), Image.NEAREST)
            if self.seamy:
                a = np.asarray(out).copy()
                a[:, :w // 2] = 255 - a[:, :w // 2]
                out = Image.fromarray(a, "RGB")
        else:                                             # pass 2
            out = Image.new("RGB", src.size, (128, 128, 128))
        p = self.output_dir / f"{save_node}_{len(self.runs)}.png"
        out.save(p)
        return p


def make_ctx(tmp, client, no_seam=False, keep_stages=False):
    prompts = []

    def describe(key, im, axes, anchor="", **kw):
        prompts.append((axes, anchor))
        return f"prompt for {im.size} {axes}"

    def anchor_fn(key, images, **kw):
        return f"anchor from {len(images)} images"

    ctx = bt.Context(client=client,
                     render_template=cc.load_template(cc.RENDER_TEMPLATE, cc.RENDER_NODES),
                     seam_template=cc.load_template(cc.SEAM_TEMPLATE, cc.SEAM_NODES),
                     key="sk-test",
                     prompts=vc.JsonCache(tmp / "dst" / ".prompts.json"),
                     anchors=vc.JsonCache(tmp / "dst" / ".style_anchors.json"),
                     seams=vc.JsonCache(tmp / "dst" / ".seams.json"),
                     src_root=tmp / "src", dst_root=tmp / "dst",
                     no_seam=no_seam, keep_stages=keep_stages,
                     describe=describe, anchor_fn=anchor_fn, log=lambda *a: None)
    return ctx, prompts


REL = Path("e1u1/metal1_1.png")


def test_process_one_seamless_render_needs_no_repair(tmp_path):
    make_tree(tmp_path / "src")
    client = FakeClient(tmp_path)
    ctx, prompts = make_ctx(tmp_path, client)
    axes, remaining = bt.process_one(ctx, REL, (256, 256),
                                     ["e1u1/metal1_1.png", "e1u1/trim1_1.png"])
    assert axes == () and remaining == ()
    out = tmp_path / "dst" / "e1u1" / "metal1_1.png"
    with Image.open(out) as im:
        assert im.size == (256, 256) and im.mode == "RGB"
    assert len(client.runs) == 1
    g = client.runs[0]
    assert g[cc.N_LATENT]["inputs"]["width"] == 1024
    assert g[cc.N_POS]["inputs"]["prompt"].startswith("prompt for")
    assert g[cc.N_SAMPLER]["inputs"]["seed"] == tp.seed_for(REL)
    assert prompts[0] == (("x", "y"), "anchor from 2 images")
    assert ctx.prompts.get("e1u1/metal1_1.png").startswith("prompt for")
    assert ctx.anchors.get("e1u1") == "anchor from 2 images"
    seams = ctx.seams.get("e1u1/metal1_1.png")
    assert seams["repainted"] == [] and len(seams["source"]) == 2 and len(seams["final"]) == 2
    assert not list((tmp_path / "in").iterdir())        # staged inputs removed
    assert not list((tmp_path / "out").iterdir())       # render removed
    assert not (out.parent / (out.name + ".tmp")).exists()


def test_process_one_repairs_a_seam_on_x(tmp_path):
    make_tree(tmp_path / "src")
    client = FakeClient(tmp_path, seamy=True)
    ctx, _ = make_ctx(tmp_path, client, keep_stages=True)
    axes, remaining = bt.process_one(ctx, REL, (256, 256), ["e1u1/metal1_1.png"])
    assert axes == ("x",)
    assert len(client.runs) == 2
    g2 = client.runs[1]
    assert g2[cc.N_LOAD]["inputs"]["image"] == bt.STAGE_ROLL
    assert g2[cc.N_LOAD_SRC]["inputs"]["image"] == bt.STAGE_ROLLSRC
    assert g2[cc.N_TILE_SCALE]["inputs"]["width"] == 1024 and cc.N_INPAINT in g2
    rolled = client.staged[bt.STAGE_ROLL]
    assert rolled.mode == "RGBA" and rolled.size == (1024, 1024)
    alpha = np.asarray(rolled)[:, :, 3]
    assert (alpha[:, 512 - 16] == 0).all() and (alpha[:, 0] == 255).all()
    assert client.staged[bt.STAGE_ROLLSRC].size == (64, 64)
    assert ctx.seams.get("e1u1/metal1_1.png")["repainted"] == ["x"]
    stages = tmp_path / "dst" / ".stages" / "e1u1"
    assert (stages / "metal1_1.png.pass1.png").exists()
    assert (stages / "metal1_1.png.pass2.png").exists()
    with Image.open(tmp_path / "dst" / "e1u1" / "metal1_1.png") as im:
        assert im.size == (256, 256)
    assert not list((tmp_path / "in").iterdir()) and not list((tmp_path / "out").iterdir())


def test_process_one_no_seam_flag_skips_pass_two(tmp_path):
    make_tree(tmp_path / "src")
    client = FakeClient(tmp_path, seamy=True)
    ctx, _ = make_ctx(tmp_path, client, no_seam=True)
    axes, remaining = bt.process_one(ctx, REL, (256, 256), ["e1u1/metal1_1.png"])
    assert axes == () and remaining == () and len(client.runs) == 1


def test_process_one_uses_cached_prompt_without_vlm(tmp_path):
    make_tree(tmp_path / "src")
    client = FakeClient(tmp_path)
    ctx, prompts = make_ctx(tmp_path, client)
    ctx.prompts.set("e1u1/metal1_1.png", "cached prompt")
    ctx.key = ""
    bt.process_one(ctx, REL, (256, 256), ["e1u1/metal1_1.png"])
    assert prompts == []
    assert client.runs[0][cc.N_POS]["inputs"]["prompt"] == "cached prompt"


def test_process_one_fails_when_vlm_fails_and_writes_nothing(tmp_path):
    make_tree(tmp_path / "src")
    client = FakeClient(tmp_path)
    ctx, _ = make_ctx(tmp_path, client)

    def boom(*a, **k):
        raise vc.VLMError("empty response")

    ctx.describe = boom
    with pytest.raises(vc.VLMError):
        bt.process_one(ctx, REL, (256, 256), ["e1u1/metal1_1.png"])
    assert not (tmp_path / "dst" / "e1u1" / "metal1_1.png").exists()
    assert client.runs == []


def test_process_one_without_key_or_cache_fails(tmp_path):
    make_tree(tmp_path / "src")
    ctx, _ = make_ctx(tmp_path, FakeClient(tmp_path))
    ctx.key = ""
    with pytest.raises(RuntimeError, match="DASHSCOPE_API_KEY"):
        bt.process_one(ctx, REL, (256, 256), ["e1u1/metal1_1.png"])


def test_anchor_failure_is_a_warning(tmp_path, capsys):
    make_tree(tmp_path / "src")
    ctx, prompts = make_ctx(tmp_path, FakeClient(tmp_path))

    def bad_anchor(*a, **k):
        raise ConnectionError("down")

    ctx.anchor_fn = bad_anchor
    bt.process_one(ctx, REL, (256, 256), ["e1u1/metal1_1.png"])
    assert prompts[0][1] == ""
    assert "style anchor failed for e1u1" in capsys.readouterr().err
    assert "e1u1" in ctx.anchor_failed


def test_verify_render_and_write_atomic(tmp_path):
    good = tmp_path / "good.png"
    periodic(8, 4).save(good)
    bt.verify_render(good, (8, 4))
    with pytest.raises(RuntimeError, match="expected 16x4"):
        bt.verify_render(good, (16, 4))
    rgba = tmp_path / "rgba.png"
    periodic(8, 4).convert("RGBA").save(rgba)
    with pytest.raises(RuntimeError, match="mode is RGBA"):
        bt.verify_render(rgba, (8, 4))
    bad = tmp_path / "bad.png"
    bad.write_bytes(b"nope")
    with pytest.raises(RuntimeError, match="not a decodable image"):
        bt.verify_render(bad, (8, 4))
    dst = tmp_path / "out" / "a" / "b.png"
    with pytest.raises(RuntimeError):
        bt.write_atomic(periodic(8, 4), dst, (9, 4))
    assert not dst.exists() and not dst.with_name("b.png.tmp").exists()
    bt.write_atomic(periodic(8, 4), dst, (8, 4))
    assert dst.exists()


def test_verify_tree_reports_each_problem_class(tmp_path, capsys):
    src, dst = tmp_path / "src", tmp_path / "dst"
    make_tree(src)
    (dst / "e1u1").mkdir(parents=True)
    (dst / "e2u1").mkdir(parents=True)
    periodic(256, 256).save(dst / "e1u1" / "metal1_1.png")                     # good
    periodic(64, 16).save(dst / "e1u1" / "trim1_1.png")                        # WRONGSIZE
    periodic(128, 128).convert("RGBA").save(dst / "e1u1" / "+0comp1.png")      # WRONGMODE
    # +1comp1 missing                                                          # MISSING
    Image.new("RGB", (32, 32), (1, 1, 1)).save(dst / "e1u1" / "black.png")     # ALTERED
    (dst / "e1u1" / "clip.png").write_bytes((src / "e1u1" / "clip.png").read_bytes())
    a = np.asarray(periodic(560, 512))[:, :512]                                # SEAM x
    Image.fromarray(a, "RGB").save(dst / "e2u1" / "floor1_1.png")
    (dst / "manifest.json").write_text("{}")
    bad, warn = bt.verify_tree(src, dst)
    out = capsys.readouterr().out
    assert "WRONGSIZE e1u1/trim1_1.png" in out
    assert "WRONGMODE e1u1/+0comp1.png" in out
    assert "MISSING  e1u1/+1comp1.png" in out
    assert "ALTERED  e1u1/black.png" in out
    assert "SEAM x   e2u1/floor1_1.png" in out
    assert "metal1_1" not in out and "clip" not in out and "manifest" not in out
    assert (bad, warn) == (4, 1)
    assert out.strip().endswith("verify: 4 problem(s), 1 seam warning(s)")


def test_main_dry_run_and_limit(tmp_path, capsys):
    src = tmp_path / "src"
    make_tree(src)
    rc = bt.main(["--dry-run", "--src", str(src), "--dst", str(tmp_path / "dst"), "--limit", "2"])
    out = capsys.readouterr().out
    assert rc == 0
    assert out.startswith("2 textures to render + 2 copy-as-is files")
    assert "canvas 1024x1024 s=32 tiles 1x1" in out           # +0comp1 is 32x32
    assert "(copy: tool texture, never drawn)" in out          # clip
    assert "(copy: flat texture)" in out                       # black
    assert "metal1_1" not in out                               # cut by --limit


def test_main_only_and_exclude(tmp_path, capsys):
    src = tmp_path / "src"
    make_tree(src)
    bt.main(["--dry-run", "--src", str(src), "--dst", str(tmp_path / "dst"), "--only", "e2u1"])
    out = capsys.readouterr().out
    assert out.startswith("1 textures to render + 0 copy-as-is files")
    bt.main(["--dry-run", "--src", str(src), "--dst", str(tmp_path / "dst"), "--exclude", "e1u1"])
    out = capsys.readouterr().out
    assert "e2u1/floor1_1.png" in out and "e1u1" not in out.split("\n", 1)[1]


def test_main_verify_exit_code(tmp_path):
    src = tmp_path / "src"
    make_tree(src)
    assert bt.main(["--verify", "--src", str(src), "--dst", str(tmp_path / "empty")]) == 1


def test_main_pick_loop_is_resumable(tmp_path, monkeypatch, capsys):
    src = tmp_path / "src"
    make_tree(src)
    client = FakeClient(tmp_path)
    monkeypatch.setattr(cc, "ComfyClient", lambda: client)
    monkeypatch.setattr(vc, "load_api_key", lambda: "sk-test")
    monkeypatch.setattr(vc, "describe_texture", lambda key, im, axes, anchor="", **kw: "p")
    monkeypatch.setattr(vc, "folder_anchor", lambda key, images, **kw: "a")
    dst = tmp_path / "dst"
    args = ["--src", str(src), "--dst", str(dst), "--pick", "e1u1/metal1_1.png", "--pick", "e1u1/clip.png"]
    rc = bt.main(args)
    out = capsys.readouterr().out
    assert rc == 0
    assert (dst / "e1u1" / "metal1_1.png").exists()
    assert (dst / "e1u1" / "clip.png").read_bytes() == (src / "e1u1" / "clip.png").read_bytes()
    assert "done: processed=1 skipped=0 copied=1 seamfixed=0 seamwarn=0 failed=0" in out
    assert bt.main(args) == 0
    assert "skipped=2" in capsys.readouterr().out


def test_main_counts_a_failed_texture_and_exits_1(tmp_path, monkeypatch, capsys):
    src = tmp_path / "src"
    make_tree(src)
    monkeypatch.setattr(cc, "ComfyClient", lambda: FakeClient(tmp_path))
    monkeypatch.setattr(vc, "load_api_key", lambda: "sk-test")

    def boom(*a, **k):
        raise vc.VLMError("empty response")

    monkeypatch.setattr(vc, "describe_texture", boom)
    monkeypatch.setattr(vc, "folder_anchor", lambda key, images, **kw: "a")
    rc = bt.main(["--src", str(src), "--dst", str(tmp_path / "dst"), "--pick", "e1u1/metal1_1.png"])
    captured = capsys.readouterr()
    assert rc == 1 and "failed=1" in captured.out
    assert "ERROR: e1u1/metal1_1.png: empty response" in captured.err
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: LOCAL TESTS
Expected: `ModuleNotFoundError: No module named 'batch_textures'`

- [ ] **Step 3: Write `batch_textures.py`**

`$KIT/batch_textures.py`:
```python
#!/usr/bin/env python3
"""Recreate the Quake II world textures as photorealistic 4x versions.

Walks --src (the tree written by tools/extract_textures.py), renders every
world texture through ComfyUI, and mirrors the results into --dst, which
drops straight into baseq2/textures/. Tool textures and flat textures are
copied byte-for-byte. Resumable: existing outputs are skipped.

Per texture: VLM material prompt (cached) -> tiled canvas -> pass 1 render ->
period crop -> downscale to 4x -> wrap-seam check -> optional pass 2 seam
repaint -> verify -> atomic move.

Spec: Quake-2/docs/superpowers/specs/2026-09-02-quake2-texture-recreation-kit-design.md
"""
import argparse
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image

import comfy_client as cc
import teximage as ti
import texplan as tp
import vlm_client as vc

HOME = Path.home()
SRC_ROOT = HOME / "comfy" / "data" / "quake2" / "textures"
DST_ROOT = HOME / "comfy" / "data" / "quake2" / "textures-ai"
STAGE_SRC, STAGE_ROLL, STAGE_ROLLSRC = "__q2_src.png", "__q2_roll.png", "__q2_rollsrc.png"


def subtree(rel):
    parts = Path(rel).parts
    return parts[0] if len(parts) > 1 else ""


def verify_render(path, expect):
    """Raise unless path decodes, is exactly expect, and is RGB (spec 4.6)."""
    try:
        with Image.open(path) as im:
            size, mode = im.size, im.mode
    except Exception as e:
        raise RuntimeError(f"result is not a decodable image: {path}: {e}")
    if size != tuple(expect):
        raise RuntimeError(f"result is {size[0]}x{size[1]}, expected {expect[0]}x{expect[1]}")
    if mode != "RGB":
        raise RuntimeError(f"result mode is {mode}, expected RGB")


def write_atomic(image, dst, expect):
    """Write to <dst>.tmp, verify, rename. Nothing lands unverified."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_name(dst.name + ".tmp")
    try:
        image.save(tmp, "PNG")
        verify_render(tmp, expect)
    except Exception:
        tmp.unlink(missing_ok=True)
        raise
    tmp.replace(dst)


@dataclass
class Context:
    client: object
    render_template: dict
    seam_template: dict
    key: str
    prompts: vc.JsonCache
    anchors: vc.JsonCache
    seams: vc.JsonCache
    src_root: Path
    dst_root: Path
    no_seam: bool = False
    keep_stages: bool = False
    describe: object = None       # defaults to vlm_client.describe_texture, resolved at call time
    anchor_fn: object = None      # defaults to vlm_client.folder_anchor
    anchor_failed: set = field(default_factory=set)
    log: object = print


def anchor_for(ctx, rel, candidates):
    """The folder's style anchor, fetched once; '' when unavailable. A
    failure is a warning: the folder proceeds without an anchor (spec 5.2)."""
    folder = Path(rel).parent.as_posix()
    cached = ctx.anchors.get(folder)
    if cached is not None:
        return cached
    if not ctx.key or folder in ctx.anchor_failed:
        return ""
    pick = vc.spread([p for p in candidates if Path(p).parent.as_posix() == folder],
                     vc.ANCHOR_MAX_IMAGES)
    fetch = ctx.anchor_fn or vc.folder_anchor
    try:
        images = [Image.open(ctx.src_root / p).convert("RGB") for p in pick]
        text = fetch(ctx.key, images, log=ctx.log)
        ctx.anchors.set(folder, text)
        ctx.log(f"  style anchor [{folder}]: {text}")
        return text
    except Exception as e:
        ctx.anchor_failed.add(folder)
        print(f"  warning: style anchor failed for {folder}: {e}", file=sys.stderr)
        return ""


def _save_stage(ctx, rel, stage, image):
    p = ctx.dst_root / ".stages" / rel.parent / f"{rel.name}.{stage}.png"
    p.parent.mkdir(parents=True, exist_ok=True)
    image.save(p, "PNG")


def process_one(ctx, rel, target, candidates):
    """Render one texture into ctx.dst_root / rel (spec 4.3 to 4.6).
    Returns (axes repainted, axes still showing a seam)."""
    rel = Path(rel)
    src = ctx.src_root / rel
    dst = ctx.dst_root / rel
    im = Image.open(src).convert("RGB")
    plan = tp.plan_canvas(*im.size)
    seed = tp.seed_for(rel)
    src_ratio = ti.wrap_ratio(im)
    src_axes = ti.source_axes(src_ratio)
    ctx.log(f"  canvas {plan.canvas[0]}x{plan.canvas[1]} s={plan.s} tiles {plan.nx}x{plan.ny}"
            f"  source wrap x={src_ratio[0]:.2f} y={src_ratio[1]:.2f}")

    key = rel.as_posix()
    prompt = ctx.prompts.get(key)
    if prompt is None:
        if not ctx.key:
            raise RuntimeError("no DASHSCOPE_API_KEY in ~/comfy/.env and no cached prompt")
        anchor = anchor_for(ctx, rel, candidates)
        describe = ctx.describe or vc.describe_texture
        prompt = describe(ctx.key, im, src_axes, anchor, log=ctx.log)
        ctx.prompts.set(key, prompt)

    # pass 1: render the tiled canvas, keep the first period
    canvas = ti.build_canvas(im, plan.nx, plan.ny)
    name = ctx.client.stage(canvas, STAGE_SRC)
    graph = cc.prepare_render(ctx.render_template, image_name=name, canvas=plan.canvas,
                              prompt=prompt, seed=seed)
    out = ctx.client.run(graph)
    render = Image.open(out).convert("RGB")
    out.unlink(missing_ok=True)
    ctx.client.unstage(name)
    crop = ti.crop_period(render, plan.period)
    if ctx.keep_stages:
        _save_stage(ctx, rel, "pass1", crop)
    result = ti.downscale(crop, target)
    r1 = ti.wrap_ratio(result)
    axes = () if ctx.no_seam else ti.needs_repair(src_ratio, r1)

    # pass 2: roll the seams to the middle, repaint the band, roll back
    if axes:
        dx, dy = ti.half_shift(crop.size, axes)
        rolled = ti.roll(crop, dx, dy)
        mask = ti.seam_mask(crop.size, axes)
        rolled_src = ti.roll(im, *ti.half_shift(im.size, axes))
        rname = ctx.client.stage(ti.stage_rgba(rolled, mask), STAGE_ROLL)
        sname = ctx.client.stage(rolled_src, STAGE_ROLLSRC)
        graph2 = cc.prepare_seam(ctx.seam_template, roll_name=rname, src_name=sname,
                                 period=plan.period, prompt=prompt, seed=seed)
        out2 = ctx.client.run(graph2)
        painted = Image.open(out2).convert("RGB")
        out2.unlink(missing_ok=True)
        ctx.client.unstage(rname, sname)
        fixed = ti.composite(rolled, painted, ti.feather(mask))
        crop2 = ti.roll(fixed, -dx, -dy)
        if ctx.keep_stages:
            _save_stage(ctx, rel, "pass2", crop2)
        result = ti.downscale(crop2, target)

    final = ti.wrap_ratio(result)
    remaining = tuple(ax for ax, r in zip(ti.AXES, final) if ax in axes and ti.has_seam(r))
    ctx.seams.set(key, {"source": [round(v, 3) for v in src_ratio],
                        "pass1": [round(v, 3) for v in r1],
                        "final": [round(v, 3) for v in final],
                        "repainted": list(axes)})
    for ax in remaining:
        print(f"  warning: seam remains on {ax} for {key}", file=sys.stderr)
    write_atomic(result, dst, target)
    return axes, remaining


def verify_tree(src_root, dst_root):
    """Audit dst against src (spec 6.4). Returns (problems, seam_warnings)."""
    src_root, dst_root = Path(src_root), Path(dst_root)
    bad = warn = 0
    for src in sorted(p for p in src_root.rglob("*") if p.is_file()):
        rel = src.relative_to(src_root)
        dst = dst_root / rel
        kind, info = tp.classify(src, rel)
        if not dst.exists():
            print(f"MISSING  {rel}")
            bad += 1
            continue
        if kind == "copy":
            if dst.read_bytes() != src.read_bytes():
                print(f"ALTERED  {rel}  (should be a byte-for-byte copy: {info})")
                bad += 1
            continue
        try:
            with Image.open(dst) as im:
                size, mode = im.size, im.mode
                res = im.convert("RGB")
        except Exception:
            print(f"UNREADABLE {rel}")
            bad += 1
            continue
        if size != tuple(info):
            print(f"WRONGSIZE {rel}  is {size[0]}x{size[1]}, expected {info[0]}x{info[1]}")
            bad += 1
            continue
        if mode != "RGB":
            print(f"WRONGMODE {rel}  is {mode}, expected RGB")
            bad += 1
            continue
        with Image.open(src) as s:
            src_ratio = ti.wrap_ratio(s.convert("RGB"))
        for ax in ti.needs_repair(src_ratio, ti.wrap_ratio(res)):
            print(f"SEAM {ax}   {rel}")
            warn += 1
    print(f"verify: {bad} problem(s), {warn} seam warning(s)")
    return bad, warn


def select_files(src_root, args):
    files = sorted(p for p in src_root.rglob("*") if p.is_file())
    if args.pick:
        wanted = {Path(p).as_posix() for p in args.pick}
        files = [p for p in files if p.relative_to(src_root).as_posix() in wanted]
    if args.only or args.exclude:
        keep = set(args.only) if args.only else None
        files = [p for p in files
                 if (keep is None or subtree(p.relative_to(src_root)) in keep)
                 and subtree(p.relative_to(src_root)) not in args.exclude]
    return files


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true", help="print the plan, queue nothing")
    ap.add_argument("--verify", action="store_true", help="audit --dst against --src and exit")
    ap.add_argument("--src", type=Path, default=SRC_ROOT, help="input tree (default: %(default)s)")
    ap.add_argument("--dst", type=Path, default=DST_ROOT, help="output tree (default: %(default)s)")
    ap.add_argument("--only", action="append", metavar="FOLDER",
                    help="restrict to these top-level folders (repeatable)")
    ap.add_argument("--exclude", action="append", default=[], metavar="FOLDER",
                    help="skip these top-level folders (repeatable)")
    ap.add_argument("--pick", action="append", metavar="REL",
                    help="restrict to these textures, e.g. e1u1/metal1_1.png (repeatable)")
    ap.add_argument("--limit", type=int, metavar="N", help="stop after the first N render candidates")
    ap.add_argument("--no-seam", action="store_true", help="skip the seam repaint pass")
    ap.add_argument("--keep-stages", action="store_true",
                    help="save full-resolution pass 1/2 crops under <dst>/.stages/")
    return ap


def main(argv=None):
    args = build_parser().parse_args(argv)
    if not args.src.is_dir():
        sys.exit(f"error: --src is not a directory: {args.src}")
    if args.verify:
        bad, _ = verify_tree(args.src, args.dst)
        return 1 if bad else 0

    files = select_files(args.src, args)
    plan = {p: tp.classify(p, p.relative_to(args.src)) for p in files}
    if args.limit is not None:
        kept, n = [], 0
        for p in files:
            if plan[p][0] == "render":
                if n >= args.limit:
                    break
                n += 1
            kept.append(p)
        files = kept
    render = [p for p in files if plan[p][0] == "render"]
    candidates = [p.relative_to(args.src).as_posix() for p in render]
    print(f"{len(render)} textures to render + {len(files) - len(render)} "
          f"copy-as-is files under {args.src}")
    by_tree = {}
    for p in files:
        t = subtree(p.relative_to(args.src)) or "(root)"
        by_tree.setdefault(t, [0, 0])[0 if plan[p][0] == "render" else 1] += 1
    for t, (r, c) in sorted(by_tree.items()):
        print(f"  {t:20} render {r:5}  copy {c:5}")

    ctx = None
    if not args.dry_run:
        ctx = Context(
            client=cc.ComfyClient(),
            render_template=cc.load_template(cc.RENDER_TEMPLATE, cc.RENDER_NODES),
            seam_template=cc.load_template(cc.SEAM_TEMPLATE, cc.SEAM_NODES),
            key=vc.load_api_key(),
            prompts=vc.JsonCache(args.dst / ".prompts.json"),
            anchors=vc.JsonCache(args.dst / ".style_anchors.json"),
            seams=vc.JsonCache(args.dst / ".seams.json"),
            src_root=args.src, dst_root=args.dst,
            no_seam=args.no_seam, keep_stages=args.keep_stages)
        if not ctx.key:
            print("warning: no DASHSCOPE_API_KEY in ~/comfy/.env - textures without a "
                  "cached prompt will fail", file=sys.stderr)

    processed = skipped = copied = seamfixed = seamwarn = failed = 0
    for i, src in enumerate(files, 1):
        rel = src.relative_to(args.src)
        dst = args.dst / rel
        kind, info = plan[src]
        if dst.exists() and dst.stat().st_size > 0:
            skipped += 1
            continue
        print(f"[{i}/{len(files)}] {rel}")
        if args.dry_run:
            if kind == "render":
                cp = tp.plan_canvas(*tp.image_size(src))
                print(f"  -> {dst}  {info[0]}x{info[1]}  canvas {cp.canvas[0]}x{cp.canvas[1]} "
                      f"s={cp.s} tiles {cp.nx}x{cp.ny}")
            else:
                print(f"  -> {dst}  (copy: {info})")
            continue
        if kind == "copy":
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            copied += 1
            continue
        try:
            axes, remaining = process_one(ctx, rel, info, candidates)
            processed += 1
            seamfixed += bool(axes)
            seamwarn += bool(remaining)
        except Exception as e:      # one bad texture must not stop a two-day run
            failed += 1
            print(f"  ERROR: {rel}: {e}", file=sys.stderr)

    print(f"done: processed={processed} skipped={skipped} copied={copied} "
          f"seamfixed={seamfixed} seamwarn={seamwarn} failed={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: LOCAL TESTS
Expected: all pass. If `test_process_one_seamless_render_needs_no_repair` reports `axes == ('x',)` or similar, the fake render's Lanczos downscale has produced a wrap ratio above 1.5; print `ctx.seams.get(...)` and check the source ratio is near 0 before touching thresholds. The synthetic cosine textures start at zero slope, so both ratios should be well under 0.5.

- [ ] **Step 5: Sync, run on the box, commit**

Run: SYNC, REMOTE TESTS, then `chmod +x` and a smoke test of the entry points on the box:
```bash
ssh $BOX 'cd ~/comfy/batch_regen_q2 && chmod +x run_batch.sh batch_textures.py && ./run_batch.sh --help | head -5 && mkdir -p /tmp/q2smoke/src && ./run_batch.sh --verify --src /tmp/q2smoke/src --dst /tmp/q2smoke/dst; echo "exit $?"; rm -rf /tmp/q2smoke'
```
Expected: the help text, then `verify: 0 problem(s), 0 seam warning(s)` and `exit 0`.

COMMIT ON THE BOX with `MSG` = `feat(q2): batch driver with seam gate, verify and resumable loop`.

---

### Task 6: README, input tree on the box, dry run, repo note

**Files:**
- Create: `$KIT/README.md`
- Modify: `README.md` in this repo (after the paragraph ending `Palette index 255 is written as alpha 0.` in the Texture overrides section, around line 78)

- [ ] **Step 1: Write the kit README**

`$KIT/README.md`:
````markdown
# Quake II Texture Recreation

Recreates the Quake II world textures under `~/comfy/data/quake2/textures/`
(the PNGs written by `tools/extract_textures.py` in the Quake-2 repo) as
photorealistic 4× renders with ComfyUI (Qwen-Image-2512 + Fun ControlNet
Union), mirroring the folder layout into `~/comfy/data/quake2/textures-ai/`.
That output tree drops straight into `baseq2/textures/` on the game side.

A sibling of `../batch_regen` (the AITD kit); nothing is shared, and the
ComfyUI server is started with `../batch_regen/run_server.sh`.

Design: `Quake-2/docs/superpowers/specs/2026-09-02-quake2-texture-recreation-kit-design.md`.

## Per texture

1. A DashScope VLM (`qwen3-vl-plus`) writes a material description from
   the source, nearest-upscaled so it can read the pixel art. Prompts are
   cached in `<dst>/.prompts.json`; one style-anchor sentence per map folder
   (`e1u1` …) is cached in `<dst>/.style_anchors.json`.
2. The source is tiled along its short axis to within 2:1 and rendered at an
   integer scale that puts the canvas long edge at 1024 or more. The union
   ControlNet sees the upscaled source in `tile` mode and its Canny edges in
   `canny` mode, so layout and colours are locked to the original.
3. The first period is cropped from the render and Lanczos-downscaled to
   **exactly 4× the source**.
4. A wrap-seam ratio (step across the wrap ÷ step between neighbours, per
   axis) is measured on the source and on the result. When the source wraps
   on an axis and the result does not, pass 2 rolls the full-resolution crop
   by half on that axis, repaints a band of a tenth of the period with inpaint
   conditioning while the ControlNet sees the rolled source (seamless by
   construction), composites the band back, and rolls back. Ratios and the
   repainted axes go to `<dst>/.seams.json`.
5. The file is verified (size, `RGB`) and moved into the tree atomically.

## What is copied, not repainted

Tool textures never drawn by the renderer (`clip`, `hint`, `skip`, `trigger`,
`origin`, `nodraw`, `null`, `sky*`), flat textures (grayscale std < 2), and
anything that is not a decodable PNG are copied byte-for-byte.

## Run

```
../batch_regen/run_server.sh           # ComfyUI on :8188 (skip if running)
./run_batch.sh --dry-run               # plan per folder and per texture, no server needed
./run_batch.sh --pick e1u1/c_met5_1.png --keep-stages   # a few textures, keep pass crops
./run_batch.sh --only e1u1             # one map folder
./run_batch.sh                         # everything
./run_batch.sh --verify                # audit the output tree, no server needed
```

Flags: `--src`/`--dst` override the trees; `--only`/`--exclude` select
top-level folders; `--pick REL` (repeatable) selects textures; `--limit N`
stops after N render candidates; `--no-seam` skips pass 2; `--keep-stages`
writes the full-resolution pass 1/2 crops to `<dst>/.stages/`.

Behaviour:

- Sequential, about 30 s per pass on the 5090; pass 2 runs on the textures
  that need it. Expect one to two days for the full set. Run it in `tmux`
  with `./run_batch.sh |& tee batch.log`.
- Resumable: existing non-empty outputs are skipped. To redo a texture,
  delete it from the output tree (and its entry in `.prompts.json` if the
  prompt should be regenerated).
- Per-texture failures go to stderr and the run continues; ends with
  `done: processed=N skipped=N copied=N seamfixed=N seamwarn=N failed=N`,
  exit 1 if any failed. VLM calls retry three times (5 s, 15 s, 45 s);
  after that the texture fails and is retried on the next run.
- `--verify` reports `MISSING` / `ALTERED` / `UNREADABLE` / `WRONGSIZE` /
  `WRONGMODE` as problems (exit 1) and `SEAM x` / `SEAM y` as warnings.

## Moving data

From the Quake-2 checkout on the Mac:

```
rsync -a --delete baseq2/textures/ felipe@192.168.1.1:comfy/data/quake2/textures/
rsync -a felipe@192.168.1.1:comfy/data/quake2/textures-ai/ baseq2/textures/
```

The second command overwrites the extracted originals, which is what the
renderer probes. `make textures FORCE=1` restores them from the paks.

## Files

| File | Purpose |
|---|---|
| `run_batch.sh` | picks `$COMFY_PYTHON`, checks Pillow/numpy and the server, runs the driver |
| `batch_textures.py` | driver: CLI, per-texture loop, verify |
| `texplan.py` | classification, canvas rule, seeds |
| `teximage.py` | tiling, crop, wrap-seam ratio, roll/mask/composite |
| `comfy_client.py` | templates, node ids, queue/wait cycle |
| `vlm_client.py` | DashScope prompts, retries, JSON caches |
| `texture_prompt.json` | pass 1 graph |
| `seam_prompt.json` | pass 2 graph |
| `tests/` | offline tests: `$COMFY_PYTHON -m pytest tests` |

## Tuning

- `comfy_client.py`: `TILE_STRENGTH` (node 11, 0.7) and `CANNY_STRENGTH`
  (node 19, 0.5). If `tile` fights the pixel-art source, set `TILE_STRENGTH`
  to 0 for a Canny-only render. `NEGATIVE_PROMPT` (node 10).
- `teximage.py`: `SOURCE_WRAP_MAX` / `RESULT_WRAP_MAX` (1.5) decide which
  textures get pass 2; `BAND_FRAC` / `BAND_MIN` size the repaint band;
  `FEATHER_PX` the composite ramp.
- `vlm_client.py`: `TEXTURE_QUESTION`, `PROMPT_TAIL`, `ANCHOR_QUESTION`.
  Delete `<dst>/.prompts.json` entries to regenerate prompts.
- Templates: node 13 sampler settings (30 steps, euler, simple, cfg 4).
  Sizes on nodes 2, 12, 17 are overwritten per texture; edit `texplan.py`
  to change the canvas rule, not the template.
- Seeds derive from folder + base name with the `+N` animation prefix
  stripped, so frames of one panel share a seed.
````

- [ ] **Step 2: Sync and commit the README on the box**

Run: SYNC, then COMMIT ON THE BOX with `MSG` = `docs(q2): kit README`.

- [ ] **Step 3: Push the input tree to the box and dry-run the real set**

From the Quake-2 checkout:
```bash
cd /Users/felipe.dos.santos/code/theirs/Quake-2
find baseq2/textures -name '*.png' | wc -l          # expect 2188
rsync -a --delete baseq2/textures/ $BOX:comfy/data/quake2/textures/
ssh $BOX 'find ~/comfy/data/quake2/textures -name "*.png" | wc -l; cd ~/comfy/batch_regen_q2 && ./run_batch.sh --dry-run > /tmp/q2plan.txt; head -15 /tmp/q2plan.txt; echo ...; grep -c "canvas" /tmp/q2plan.txt; grep -o "canvas [0-9]*x[0-9]*" /tmp/q2plan.txt | sort | uniq -c | sort -rn; grep -o "(copy: [^)]*)" /tmp/q2plan.txt | sort | uniq -c'
```
Expected: 2188 files on the box; the summary line reports about 2130 renders and about 58 copies; every canvas is one of `1024x1024`, `1280x1024`, `1200x640`, `1056x768`, `768x1056`, `704x1056`, `1056x1056`, `768x1152`, `832x1040`; copies are only `tool texture, never drawn` and `flat texture`. Any other canvas size or copy reason is a bug in `texplan.py` to fix before the pilot.

- [ ] **Step 4: Add the note to this repo's README and commit it**

Insert after the paragraph that ends `Palette index 255 is written as alpha 0.` in `README.md`:

```markdown
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
```

Then:
```bash
cd /Users/felipe.dos.santos/code/theirs/Quake-2
git status --short              # expect only README.md modified
git add README.md
git commit -m "docs: point at the texture recreation kit and its rsync commands

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_0127eikzBGwe4iGJzEeXsg3r"
```

---

### Task 7: Pilot, tuning, and the first folder

This task needs the user in the loop for the in-game check. Everything up to the check is scripted.

**Files:**
- Modify (only if the pilot says so): `$KIT/comfy_client.py` (strengths), `$KIT/teximage.py` (thresholds), `$KIT/vlm_client.py` (meta-prompt), `$KIT/README.md` (final values)

- [ ] **Step 1: Render the pilot set with stages kept**

Twelve picks spanning the classes in spec §7.2 (a 128×128 wall, a 64×64 panel, a 64×128 metal, a 16×128 trim, a 32×32 tile, a 256×128 rock, a non-power-of-two 96×32 crate, a 320×128 door, a sign, an animated 5-frame panel, a tileable ceiling):
```bash
ssh $BOX 'cd ~/comfy/batch_regen_q2 && tmux new -d -s q2pilot "./run_batch.sh --keep-stages \
  --pick e1u1/c_met5_1.png --pick e1u1/bluekeypad.png --pick e1u1/metal2_1.png \
  --pick e1u1/jaildr1_3.png --pick e1u1/alarm0.png --pick e1u1/rocks19_1.png \
  --pick e1u1/box1_1.png --pick e2u1/hdoor1_1.png --pick e2u2/sign1_4.png \
  --pick e1u1/+0comp10_1.png --pick e1u1/+1comp10_1.png --pick e1u1/+2comp10_1.png \
  --pick e1u1/+3comp10_1.png --pick e1u1/+4comp10_1.png --pick e1u1/ceil1_1.png \
  |& tee pilot.log"'
```
Poll with `ssh $BOX 'tail -5 ~/comfy/batch_regen_q2/pilot.log'` until the `done:` line appears (about 10 to 20 minutes). Expected: `failed=0`; `seamfixed` between 3 and 10.

- [ ] **Step 2: Look at the results before involving the game**

```bash
mkdir -p "$KIT/../pilot" && rsync -a $BOX:comfy/data/quake2/textures-ai/ "$KIT/../pilot/"
ssh $BOX 'cat ~/comfy/data/quake2/textures-ai/.seams.json; echo; cat ~/comfy/data/quake2/textures-ai/.prompts.json | head -c 1500'
```
Open each `pilot/<folder>/<name>.png` next to `baseq2/textures/<folder>/<name>.png` with the Read tool and judge: same layout and colours (faithful), photoreal detail (not a blurred upscale), no perspective, no invented objects, the `+N` frames consistent with each other, and the `.pass1` / `.pass2` crops under `pilot/.stages/` showing the band repaint blending in. Note per texture what is off.

- [ ] **Step 3: Pull into the game and hand the check to the user**

```bash
cd /Users/felipe.dos.santos/code/theirs/Quake-2
rsync -a --exclude '.*' $BOX:comfy/data/quake2/textures-ai/ baseq2/textures/
make build && make run
```
Ask the user to load a map from unit 1 (`map base1`), find the pilot surfaces, and check: tiling with no visible grid on `c_met5_1` and `ceil1_1`, correct alignment of `hdoor1_1` and `bluekeypad`, colour continuity with the untouched neighbours, `imagelist` showing the 4× upload sizes, and `gl_textureoverride 0; vid_restart` restoring the originals. This is also the play test from the texture-overrides spec §5.3. Collect their verdict per texture.

- [ ] **Step 4: Tune and re-run until accepted**

Apply the findings, one knob at a time, each followed by deleting the affected outputs and re-running the same pilot command:

- Output looks like a blurred upscale: lower `TILE_STRENGTH` (0.7 → 0.5), or raise `CANNY_STRENGTH`.
- Layout or colours drift: raise `TILE_STRENGTH` (→ 0.85).
- Seams remain on tiling walls (`seamwarn > 0` or visible in-game): raise `BAND_FRAC` to 0.08, or lower `RESULT_WRAP_MAX` to 1.2 so more textures get pass 2.
- Non-tiling textures get pass 2 needlessly: raise `SOURCE_WRAP_MAX` sensitivity by lowering it to 1.2.
- Prompts mention perspective or invent objects: edit `TEXTURE_QUESTION`, delete `.prompts.json`, re-run.
- If `tile` mode cannot be tuned into faithfulness: set `TILE_STRENGTH = 0` (spec §4.7).
- If inpainting produces visible patches: replace pass 2 by a cross-fade (spec §4.7). Add to `teximage.py`:

  ```python
  def crossfade_seam(rolled, axes):
      """Fallback for pass 2: blend the rolled crop with itself rolled back by
      half, weighted by a tent that is 1 on the seam line and 0 at the band
      edge. Continuous everywhere, soft inside the band."""
      w, h = rolled.size
      t = np.zeros((h, w), dtype=np.float64)
      if "x" in axes:
          bx = max(BAND_MIN, round(BAND_FRAC * w))
          x = np.arange(w)
          t = np.maximum(t, np.clip(1.0 - np.abs(x - w // 2) / bx, 0.0, 1.0)[None, :])
      if "y" in axes:
          by = max(BAND_MIN, round(BAND_FRAC * h))
          y = np.arange(h)
          t = np.maximum(t, np.clip(1.0 - np.abs(y - h // 2) / by, 0.0, 1.0)[:, None])
      other = roll(rolled, *half_shift(rolled.size, axes))   # the unrolled crop, continuous at the centre
      return composite(rolled, other, t)
  ```

  and in `process_one` replace the block from `rolled_src = ...` to `fixed = ti.composite(...)` with `fixed = ti.crossfade_seam(rolled, axes)`. Add a test that `wrap_ratio(roll(crossfade_seam(rolled, ("x",)), -dx, 0))[0]` is below `RESULT_WRAP_MAX` for a crop built by cutting a `periodic` image. Keep the gate, the caches and `--keep-stages`.

To redo a texture: `ssh $BOX 'rm ~/comfy/data/quake2/textures-ai/e1u1/c_met5_1.png'` then re-run the pilot command. To regenerate its prompt too, remove its key from `.prompts.json`.

After each change: LOCAL TESTS (update a constant in a test only when the spec value changes), SYNC, REMOTE TESTS, COMMIT ON THE BOX with `MSG` = `tune(q2): <what changed and why>`. Record the final values in the kit README's Tuning section.

- [ ] **Step 5: First folder, then the rest**

```bash
ssh $BOX 'cd ~/comfy/batch_regen_q2 && tmux new -d -s q2e1u1 "./run_batch.sh --only e1u1 |& tee batch-e1u1.log"'
```
Expected on completion (a few hours for 390 files): `failed=0` or a handful of VLM failures that a second identical run clears. Then:
```bash
ssh $BOX 'cd ~/comfy/batch_regen_q2 && ./run_batch.sh --verify --only e1u1 2>/dev/null | tail -3'
```
Note: `--verify` audits the whole tree; MISSING lines for other folders are expected at this point, so read the `e1u1` lines only. Pull to the Mac as in Step 3 and let the user play `base1` through `base3`. On acceptance start the full run:
```bash
ssh $BOX 'cd ~/comfy/batch_regen_q2 && tmux new -d -s q2all "./run_batch.sh |& tee batch.log"'
```
When it ends: `./run_batch.sh --verify` must print `0 problem(s)`; review the `SEAM` lines by name, pull, and update the memory note about the texture overrides feature with the outcome.

---

## Self-review

**Spec coverage.** §3 layout and caches: Tasks 1, 4, 5, 6. §4.1 classification: Task 1. §4.2 canvas rule and table: Task 1 tests. §4.3 pass 1 graph and per-image fields: Task 3, `process_one` in Task 5. §4.4 wrap ratio and gate: Task 2. §4.5 pass 2 roll/mask/inpaint/composite/roll back: Tasks 2, 3, 5. §4.6 verify before move, cleanup of staged files: Task 5. §4.7 fallbacks: Task 7 step 4. §5.1 to §5.5 prompting, anchors, seeds, negative, sampler: Tasks 1, 3, 4. §6.1 flags: Task 5. §6.2 loop and summary line: Task 5. §6.3 errors and timeouts: Tasks 3, 4, 5. §6.4 verify: Task 5. §7 pilot and rollout: Tasks 6, 7. §8.1 offline tests: one file per module, Tasks 1 to 5, including the template guard. §8.2 dry run on the real tree: Task 6. §9 files: all present; the repo README note is Task 6 step 4.

**Placeholders.** None; every code step carries the full file.

**Type consistency.** `plan_canvas` returns `CanvasPlan` with `.canvas/.period/.target` used by `process_one` and the dry run. `classify` returns `("render", (w, h))`, and `main` passes that tuple as `target` to `process_one`. `ComfyClient.run(graph, save_node=N_SAVE)` returns a `Path`, matched by `FakeClient.run`. `describe_texture(key, im, axes, anchor="", http, sleep, log)` and `folder_anchor(key, images, http, sleep, log)` are called with `log=` by `anchor_for`/`process_one` and stubbed with `**kw` in the tests. `JsonCache.get/set` names match across `vlm_client` and `batch_textures`. Node id constants (`N_LOAD`, `N_LATENT`, `N_POS`, `N_SAMPLER`, `N_TILE_SCALE`, `N_INPAINT`, `N_LOAD_SRC`) are the same strings in the templates, `comfy_client`, and the tests.
