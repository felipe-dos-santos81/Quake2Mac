# Folder Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure the seven source directories (`client server qcommon game ref_gl sdl linux`) into module/component subfolders with root-relative include discipline, with zero behavior change.

**Architecture:** Link-unit boundaries stay as top-level dirs (`client server qcommon game ref_gl` + merged `platform/{posix,sdl}`); large dirs subdivide. Includes are normalized first (tree still flat), then the Makefile switches from the vpath-basename hack to a source-path mirror rule, then each unit is moved/renamed with a clean build gate after every commit. An `nm` symbol-snapshot diff proves nothing changed at the binary level.

**Tech Stack:** C (id-era style), GNU make, Apple clang, SDL3, macOS/arm64. `git mv` for all moves (rename detection preserves history).

**Spec:** `docs/superpowers/specs/2026-09-02-folder-restructure-design.md` — read it first; the mapping table there is authoritative.

## Global Constraints

- **Zero behavior change:** moves, renames, include-path rewrites, and Makefile mechanics only. No C logic changes anywhere.
- **Gate per commit:** `make clean && make verify-load` must exit 0. The clean is mandatory on move commits — stale `.d` files referencing old paths break incremental make.
- **Symbol proof:** `nm -j | LC_ALL=C sort` snapshots of `build/quake2`, `build/ref_gl.so`, `baseq2/gamearm64.so`, `build/verify_load` taken in Task 0 must be byte-identical after Task 7.
- **Staging:** explicit paths only — never `git add -A` or `git commit -a`. `baseq2/` is user territory and is never staged (the DLL lands there as a build side effect only).
- **Protected content** (rename/include changes only, never touch the logic): `qgl*` dispatch table + 7 `qgl*EXT` pointers (`platform/sdl/qgl.c`), byte-swap cluster in `game/q_shared.c`, `RW_IN_Activate_fp` (`platform/sdl/vid.c`), `R_Init()` returning `true` (`ref_gl/rmain.c`).
- **Benign noise unchanged:** the `ld` warning `reducing alignment of section __DATA,__common` and ~200 pre-existing `-Wall` warnings are expected in every build; do not "fix" them.
- Work happens on branch `chore/folder-restructure` (already created; spec is commit `9e77b6f`).

---

### Task 0: Baseline build + symbol snapshots

**Files:**
- Create: `/tmp/q2restructure/{exe,ref,game,verify}.syms`

- [ ] **Step 1: Clean baseline build from branch tip**

```bash
make clean && make verify-load
```

Expected: exit 0. The build produces all four binaries (verify-load implies build) and prints the export-contract checks. The benign `ld` alignment warning and the ~200 `-Wall` warnings are expected.

- [ ] **Step 2: Snapshot the symbol tables**

```bash
mkdir -p /tmp/q2restructure
nm -j build/quake2          | LC_ALL=C sort > /tmp/q2restructure/exe.syms
nm -j build/ref_gl.so       | LC_ALL=C sort > /tmp/q2restructure/ref.syms
nm -j baseq2/gamearm64.so   | LC_ALL=C sort > /tmp/q2restructure/game.syms
nm -j build/verify_load     | LC_ALL=C sort > /tmp/q2restructure/verify.syms
wc -l /tmp/q2restructure/*.syms
```

Expected: four non-empty files. These are the zero-change proof for Task 7. No commit for this task.

---

### Task 1: Normalize includes to repo-root-relative paths (tree stays flat)

**Files:**
- Modify: `Makefile` (add `-I.` to `BASE_CFLAGS`)
- Modify: every `.c`/`.h` in `client server qcommon game ref_gl sdl linux` that contains a quoted include
- Delete: `ref_gl/anorms.h` (byte-identical duplicate of `client/anorms.h`)

- [ ] **Step 1: Add `-I.` to the Makefile**

In `Makefile`, change:

```make
BASE_CFLAGS = -O2 -g -Wall $(SDL_CFLAGS) -MMD -MP
```

to:

```make
BASE_CFLAGS = -O2 -g -Wall -I. $(SDL_CFLAGS) -MMD -MP
```

- [ ] **Step 2: Delete the duplicate header**

```bash
git rm ref_gl/anorms.h
```

(`client/anorms.h` remains the single canonical copy; `ref_gl/gl_mesh.c` will resolve to it through the map below.)

- [ ] **Step 3: Build the header map and verify basename uniqueness**

```bash
find client server qcommon game ref_gl sdl linux -name '*.h' \
  | awk -F/ '{print $NF, $0}' | LC_ALL=C sort > /tmp/q2restructure/hdrmap.txt
awk '{print $1}' /tmp/q2restructure/hdrmap.txt | uniq -d
```

Expected: the `uniq -d` output is **empty** (every basename resolves to exactly one header now that `ref_gl/anorms.h` is gone). If it is not empty, stop and investigate.

- [ ] **Step 4: Rewrite every quoted include to its root-relative path**

This rewrites both bare forms (`"foo.h"`) and any `../`-prefixed form (`"../foo.h"`, `"../../foo.h"`) in one pass per header:

```bash
while read -r base path; do
  esc=$(printf '%s' "$base" | sed 's/\./\\./g')
  find client server qcommon game ref_gl sdl linux \( -name '*.c' -o -name '*.h' \) \
    -exec sed -i '' "s|#include \"\(\.\./\)*${esc}\"|#include \"${path}\"|g" {} +
done < /tmp/q2restructure/hdrmap.txt
```

- [ ] **Step 5: Lint — no bare or relative includes remain**

```bash
grep -rn '#include "' client server qcommon game ref_gl sdl linux --include='*.c' --include='*.h' \
  | grep -v '#include ".*/'
grep -rn '\.\./' client server qcommon game ref_gl sdl linux --include='*.c' --include='*.h'
```

Expected: both commands print nothing. (Angle-bracket includes like `<SDL3/SDL.h>` are untouched.)

- [ ] **Step 6: Gate**

```bash
make clean && make verify-load && make test-ref
```

Expected: exit 0 for both; `test_override` passes.

- [ ] **Step 7: Commit**

```bash
git add client server qcommon game ref_gl sdl linux Makefile
git commit -m "refactor: normalize includes to repo-root-relative paths

Add -I. and rewrite every quoted include as a repo-root-relative path.
Delete ref_gl/anorms.h (byte-identical duplicate of client/anorms.h).
Tree layout unchanged; no code changes."
```

---

### Task 2: Makefile mirror rule (no files move yet)

**Files:**
- Modify: `Makefile`

The mirror rule produces the same object paths as today on the flat tree (`client/cl_main.c → build/client/cl_main.o`), except multi-unit sources stop being compiled 2–3×: `game/q_shared.c`, `game/m_flash.c`, `linux/q_shlinux.c`, `linux/glob.c` each build once at their own source-mirrored path.

- [ ] **Step 1: Replace the vpath mechanism with the mirror rule**

Delete this block:

```make
vpath %.c client server qcommon game ref_gl linux sdl

# GNU make keeps the directory part of the stem (e.g. `client/cmd`) when
# searching vpath, so strip it: the prerequisite is the basename .c file,
# resolved through the vpath order above.
.SECONDEXPANSION:
$(BUILD_DIR)/%.o: $$(notdir $$*).c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<
```

Replace it with:

```make
# Object paths mirror source paths (client/cl_main.c → build/client/cl_main.o).
# Multi-unit sources compile ONCE and link into several units (game/q_shared.c,
# game/m_flash.c, linux/q_shlinux.c, linux/glob.c) — CFLAGS is uniform across
# units; keep it that way or reintroduce per-unit object directories.
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<
```

- [ ] **Step 2: Rewrite the object lists to mirror source paths**

Replace `COMMON_OBJS`, `CLIENT_OBJS`, `SERVER_OBJS`, `SYS_EXE_OBJS`, `REF_EXTRA_OBJS` with exactly:

```make
# qcommon + shared
COMMON_OBJS = \
	$(BUILD_DIR)/qcommon/cmd.o $(BUILD_DIR)/qcommon/cmodel.o \
	$(BUILD_DIR)/qcommon/common.o $(BUILD_DIR)/qcommon/crc.o \
	$(BUILD_DIR)/qcommon/cvar.o $(BUILD_DIR)/qcommon/files.o \
	$(BUILD_DIR)/qcommon/md4.o $(BUILD_DIR)/qcommon/net_chan.o \
	$(BUILD_DIR)/qcommon/pmove.o $(BUILD_DIR)/game/q_shared.o

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
	$(BUILD_DIR)/game/m_flash.o $(BUILD_DIR)/client/cl_newfx.o

# server
SERVER_OBJS = \
	$(BUILD_DIR)/server/sv_ccmds.o $(BUILD_DIR)/server/sv_ents.o \
	$(BUILD_DIR)/server/sv_game.o $(BUILD_DIR)/server/sv_init.o \
	$(BUILD_DIR)/server/sv_main.o $(BUILD_DIR)/server/sv_send.o \
	$(BUILD_DIR)/server/sv_user.o $(BUILD_DIR)/server/sv_world.o

# platform layer linked into the executable
SYS_EXE_OBJS = \
	$(BUILD_DIR)/linux/q_shlinux.o \
	$(BUILD_DIR)/linux/vid_menu.o $(BUILD_DIR)/linux/sys_linux.o \
	$(BUILD_DIR)/linux/glob.o $(BUILD_DIR)/linux/net_udp.o \
	$(BUILD_DIR)/sdl/snd_sdl.o $(BUILD_DIR)/sdl/vid_sdl.o
```

```make
REF_EXTRA_OBJS = \
	$(BUILD_DIR)/linux/q_shlinux.o $(BUILD_DIR)/linux/glob.o \
	$(BUILD_DIR)/sdl/qgl_sdl.o $(BUILD_DIR)/sdl/glw_sdl.o \
	$(BUILD_DIR)/sdl/in_sdl.o
```

`REF_CORE_OBJS` and `GAME_OBJS` already mirror source paths — leave them unchanged. Note that `game/q_shared.o`, `game/m_flash.o`, `linux/q_shlinux.o`, `linux/glob.o` now appear at one shared path in two lists each; make builds each once.

- [ ] **Step 3: Point the verify_load rule at its mirrored object**

Change:

```make
$(VERIFY_LOAD): $(BUILD_DIR)/verify_load.o
	$(CC) $(CFLAGS) -o $@ $(BUILD_DIR)/verify_load.o $(SDL_LIBS) -ldl
```

to:

```make
$(VERIFY_LOAD): $(BUILD_DIR)/sdl/verify_load.o
	$(CC) $(CFLAGS) -o $@ $(BUILD_DIR)/sdl/verify_load.o $(SDL_LIBS) -ldl
```

(Keep the comment above it about `SDL_LIBS`.) The `TEST_OVERRIDE` rule already uses the mirrored path `$(BUILD_DIR)/ref_gl/gl_override.o` — leave it.

- [ ] **Step 4: Gate**

```bash
make clean && make verify-load && make test-ref
```

Expected: exit 0. Note `make` now compiles `q_shared.c` once instead of three times.

- [ ] **Step 5: Commit**

```bash
git add Makefile
git commit -m "refactor: replace vpath basename hack with mirror-tree build rule

Objects now mirror source paths; multi-unit sources (q_shared.c, m_flash.c,
q_shlinux.c, glob.c) compile once and link into every unit that needs them.
No source files moved yet."
```

---

### Task 3: Restructure game/ (monsters/ + player/, drop g_/m_/p_ prefixes)

**Files:**
- Move/rename: 70 files inside `game/` (mapping in the spec)
- Modify: `Makefile` (`GAME_OBJS`, and `CLIENT_OBJS` reference to `m_flash.o`)

- [ ] **Step 1: Create subdirs and move the monster module**

```bash
mkdir -p game/monsters game/player

# monsters with .c + .h pairs
for n in actor berserk boss2 boss31 boss32 brain chick flipper float flyer \
         gladiator gunner hover infantry insane medic mutant parasite \
         soldier supertank tank; do
  git mv game/m_$n.c game/monsters/$n.c
  git mv game/m_$n.h game/monsters/$n.h
done

# the three special cases
git mv game/m_boss3.c  game/monsters/boss3.c    # has no header
git mv game/m_move.c   game/monsters/move.c     # has no header
git mv game/m_flash.c  game/monsters/flash.c    # has no header
git mv game/m_player.h game/monsters/player.h   # header only
```

- [ ] **Step 2: Move the player module**

```bash
for n in client hud trail view weapon; do
  git mv game/p_$n.c game/player/$n.c
done
```

- [ ] **Step 3: Drop the g_ prefix at the game/ root**

```bash
for n in ai chase cmds combat func items main misc monster phys save spawn \
         svcmds target trigger turret utils weapon; do
  git mv game/g_$n.c game/$n.c
done
git mv game/g_local.h game/local.h
```

- [ ] **Step 4: Rewrite includes for the moved headers**

```bash
find game \( -name '*.c' -o -name '*.h' \) | xargs sed -i '' \
  -e 's|#include "game/g_local\.h"|#include "game/local.h"|g' \
  -e 's|#include "game/m_player\.h"|#include "game/monsters/player.h"|g' \
  -e 's|#include "game/m_\([a-z0-9]*\)\.h"|#include "game/monsters/\1.h"|g'
```

(No file outside `game/` includes any `game/m_*` or `game/g_*` header — verified during design. `server/server.h` includes `game/game.h` and `qcommon/qcommon.h` includes `game/q_shared.h`; both paths are unchanged.)

- [ ] **Step 5: Update the Makefile**

In `CLIENT_OBJS`, change `$(BUILD_DIR)/game/m_flash.o` to `$(BUILD_DIR)/game/monsters/flash.o`.

Replace `GAME_OBJS` with exactly:

```make
# game DLL
GAME_OBJS = \
	$(BUILD_DIR)/game/ai.o $(BUILD_DIR)/game/player/client.o \
	$(BUILD_DIR)/game/cmds.o $(BUILD_DIR)/game/svcmds.o \
	$(BUILD_DIR)/game/combat.o $(BUILD_DIR)/game/func.o \
	$(BUILD_DIR)/game/items.o $(BUILD_DIR)/game/main.o \
	$(BUILD_DIR)/game/misc.o $(BUILD_DIR)/game/monster.o \
	$(BUILD_DIR)/game/phys.o $(BUILD_DIR)/game/save.o \
	$(BUILD_DIR)/game/spawn.o $(BUILD_DIR)/game/target.o \
	$(BUILD_DIR)/game/trigger.o $(BUILD_DIR)/game/turret.o \
	$(BUILD_DIR)/game/utils.o $(BUILD_DIR)/game/weapon.o \
	$(BUILD_DIR)/game/monsters/actor.o $(BUILD_DIR)/game/monsters/berserk.o \
	$(BUILD_DIR)/game/monsters/boss2.o $(BUILD_DIR)/game/monsters/boss3.o \
	$(BUILD_DIR)/game/monsters/boss31.o $(BUILD_DIR)/game/monsters/boss32.o \
	$(BUILD_DIR)/game/monsters/brain.o $(BUILD_DIR)/game/monsters/chick.o \
	$(BUILD_DIR)/game/monsters/flipper.o $(BUILD_DIR)/game/monsters/float.o \
	$(BUILD_DIR)/game/monsters/flyer.o $(BUILD_DIR)/game/monsters/gladiator.o \
	$(BUILD_DIR)/game/monsters/gunner.o $(BUILD_DIR)/game/monsters/hover.o \
	$(BUILD_DIR)/game/monsters/infantry.o $(BUILD_DIR)/game/monsters/insane.o \
	$(BUILD_DIR)/game/monsters/medic.o $(BUILD_DIR)/game/monsters/move.o \
	$(BUILD_DIR)/game/monsters/mutant.o $(BUILD_DIR)/game/monsters/parasite.o \
	$(BUILD_DIR)/game/monsters/soldier.o $(BUILD_DIR)/game/monsters/supertank.o \
	$(BUILD_DIR)/game/monsters/tank.o $(BUILD_DIR)/game/player/hud.o \
	$(BUILD_DIR)/game/player/trail.o $(BUILD_DIR)/game/player/view.o \
	$(BUILD_DIR)/game/player/weapon.o $(BUILD_DIR)/game/q_shared.o \
	$(BUILD_DIR)/game/monsters/flash.o $(BUILD_DIR)/game/chase.o
```

- [ ] **Step 6: Gate**

```bash
make clean && make verify-load
```

Expected: exit 0.

- [ ] **Step 7: Commit**

```bash
git add game Makefile
git commit -m "refactor: restructure game/ into monsters/ and player/ modules

Drop g_/m_/p_ prefixes per the restructure spec; GAME_OBJS mirrors the new
layout. Content unchanged apart from include paths."
```

---

### Task 4: Restructure client/ (net/ + screen/ + sound/, drop cl_/snd_ prefixes)

**Files:**
- Move/rename: 25 files inside `client/`
- Modify: include paths in `linux/vid_menu.c` (`client/qmenu.h`), `sdl/snd_sdl.c` (`client/snd_loc.h`), `sdl/in_sdl.c` (`client/keys.h`)
- Modify: `Makefile` (`CLIENT_OBJS`)

- [ ] **Step 1: Create subdirs and move**

```bash
mkdir -p client/net client/screen client/sound

git mv client/cl_parse.c  client/net/parse.c
git mv client/cl_pred.c   client/net/predict.c
git mv client/cl_ents.c   client/net/ents.c
git mv client/cl_tent.c   client/net/tents.c
git mv client/cl_fx.c     client/net/fx.c
git mv client/cl_newfx.c  client/net/newfx.c

git mv client/cl_scrn.c   client/screen/scrn.c
git mv client/cl_view.c   client/screen/view.c
git mv client/cl_cin.c    client/screen/cinematic.c
git mv client/cl_inv.c    client/screen/inv.c
git mv client/console.c   client/screen/console.c
git mv client/keys.c      client/screen/keys.c
git mv client/menu.c      client/screen/menu.c
git mv client/qmenu.c     client/screen/qmenu.c
git mv client/screen.h    client/screen/screen.h
git mv client/console.h   client/screen/console.h
git mv client/keys.h      client/screen/keys.h
git mv client/qmenu.h     client/screen/qmenu.h

git mv client/snd_dma.c   client/sound/dma.c
git mv client/snd_mem.c   client/sound/mem.c
git mv client/snd_mix.c   client/sound/mix.c
git mv client/sound.h     client/sound/sound.h
git mv client/snd_loc.h   client/sound/loc.h

git mv client/cl_main.c   client/main.c
git mv client/cl_input.c  client/input.c
```

`client/client.h`, `ref.h`, `vid.h`, `input.h`, `anorms.h` stay at the root — no move.

- [ ] **Step 2: Rewrite includes for the moved headers (tree-wide)**

```bash
find client server qcommon game ref_gl sdl linux \( -name '*.c' -o -name '*.h' \) \
  | xargs sed -i '' \
  -e 's|#include "client/screen\.h"|#include "client/screen/screen.h"|g' \
  -e 's|#include "client/console\.h"|#include "client/screen/console.h"|g' \
  -e 's|#include "client/keys\.h"|#include "client/screen/keys.h"|g' \
  -e 's|#include "client/qmenu\.h"|#include "client/screen/qmenu.h"|g' \
  -e 's|#include "client/sound\.h"|#include "client/sound/sound.h"|g' \
  -e 's|#include "client/snd_loc\.h"|#include "client/sound/loc.h"|g'
```

Known outside consumers: `linux/vid_menu.c` (qmenu.h), `sdl/snd_sdl.c` (snd_loc.h), `sdl/in_sdl.c` (keys.h). `client/client.h`, `client/ref.h`, `client/vid.h`, `client/input.h`, `client/anorms.h` paths are unchanged.

- [ ] **Step 3: Update `CLIENT_OBJS` in the Makefile**

```make
# client
CLIENT_OBJS = \
	$(BUILD_DIR)/client/screen/cinematic.o $(BUILD_DIR)/client/net/ents.o \
	$(BUILD_DIR)/client/net/fx.o $(BUILD_DIR)/client/input.o \
	$(BUILD_DIR)/client/screen/inv.o $(BUILD_DIR)/client/main.o \
	$(BUILD_DIR)/client/net/parse.o $(BUILD_DIR)/client/net/predict.o \
	$(BUILD_DIR)/client/net/tents.o $(BUILD_DIR)/client/screen/scrn.o \
	$(BUILD_DIR)/client/screen/view.o $(BUILD_DIR)/client/screen/console.o \
	$(BUILD_DIR)/client/screen/keys.o $(BUILD_DIR)/client/screen/menu.o \
	$(BUILD_DIR)/client/sound/dma.o $(BUILD_DIR)/client/sound/mem.o \
	$(BUILD_DIR)/client/sound/mix.o $(BUILD_DIR)/client/screen/qmenu.o \
	$(BUILD_DIR)/game/monsters/flash.o $(BUILD_DIR)/client/net/newfx.o
```

- [ ] **Step 4: Gate**

```bash
make clean && make verify-load
```

Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add client linux sdl Makefile
git commit -m "refactor: restructure client/ into net/, screen/, sound/ modules

Drop cl_/snd_ prefixes per the restructure spec; CLIENT_OBJS mirrors the new
layout. Content unchanged apart from include paths."
```

---

### Task 5: server/ — drop the sv_ prefix (stays flat)

**Files:**
- Rename: 8 files in `server/`
- Modify: `Makefile` (`SERVER_OBJS`)

- [ ] **Step 1: Rename**

```bash
for n in ccmds ents game init main send user world; do
  git mv server/sv_$n.c server/$n.c
done
```

No include rewrites: nothing includes a `server/sv_*` path, and `server/server.h` keeps its path.

- [ ] **Step 2: Update `SERVER_OBJS` in the Makefile**

```make
# server
SERVER_OBJS = \
	$(BUILD_DIR)/server/ccmds.o $(BUILD_DIR)/server/ents.o \
	$(BUILD_DIR)/server/game.o $(BUILD_DIR)/server/init.o \
	$(BUILD_DIR)/server/main.o $(BUILD_DIR)/server/send.o \
	$(BUILD_DIR)/server/user.o $(BUILD_DIR)/server/world.o
```

- [ ] **Step 3: Gate**

```bash
make clean && make verify-load
```

Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add server Makefile
git commit -m "refactor: drop sv_ prefix from server/ sources"
```

---

### Task 6: ref_gl/ — drop the gl_ prefix (stays flat)

**Files:**
- Rename: 12 files in `ref_gl/`
- Modify: include paths in `ref_gl/*`, `ref_gl/tests/test_override.c`, and `sdl/{qgl_sdl,glw_sdl,in_sdl}.c`
- Modify: `Makefile` (`REF_CORE_OBJS`, `TEST_OVERRIDE` rule)

- [ ] **Step 1: Rename**

```bash
for n in draw image light mesh model rmain rmisc rsurf warp override; do
  git mv ref_gl/gl_$n.c ref_gl/$n.c
done
git mv ref_gl/gl_local.h ref_gl/local.h
git mv ref_gl/gl_model.h ref_gl/model.h
```

`qgl.h`, `stb_image.h`, `anormtab.h`, `warpsin.h`, `tests/` keep their paths.

- [ ] **Step 2: Rewrite includes (tree-wide)**

```bash
find client server qcommon game ref_gl sdl linux \( -name '*.c' -o -name '*.h' \) \
  | xargs sed -i '' \
  -e 's|#include "ref_gl/gl_local\.h"|#include "ref_gl/local.h"|g' \
  -e 's|#include "ref_gl/gl_model\.h"|#include "ref_gl/model.h"|g'
```

Outside consumers: `sdl/qgl_sdl.c`, `sdl/glw_sdl.c`, `sdl/in_sdl.c` (gl_local.h).

- [ ] **Step 3: Update the Makefile**

Replace `REF_CORE_OBJS` with exactly:

```make
# ref_gl renderer core
REF_CORE_OBJS = \
	$(BUILD_DIR)/ref_gl/draw.o $(BUILD_DIR)/ref_gl/image.o \
	$(BUILD_DIR)/ref_gl/light.o $(BUILD_DIR)/ref_gl/mesh.o \
	$(BUILD_DIR)/ref_gl/model.o $(BUILD_DIR)/ref_gl/rmain.o \
	$(BUILD_DIR)/ref_gl/rmisc.o $(BUILD_DIR)/ref_gl/rsurf.o \
	$(BUILD_DIR)/ref_gl/warp.o $(BUILD_DIR)/ref_gl/override.o
```

In the `TEST_OVERRIDE` rule, change both occurrences of `$(BUILD_DIR)/ref_gl/gl_override.o` to `$(BUILD_DIR)/ref_gl/override.o`.

- [ ] **Step 4: Gate**

```bash
make clean && make verify-load && make test-ref
```

Expected: exit 0 for both.

- [ ] **Step 5: Commit**

```bash
git add ref_gl sdl Makefile
git commit -m "refactor: drop gl_ prefix in ref_gl/"
```

---

### Task 7: platform/ — merge linux/ and sdl/

**Files:**
- Move/rename: `linux/*` → `platform/posix/*`, `sdl/*` → `platform/sdl/*` (+ `platform/verify_load.c`)
- Delete: empty `linux/`, `sdl/`
- Modify: `Makefile` (`SYS_EXE_OBJS`, `REF_EXTRA_OBJS`, `VERIFY_LOAD` rule)

- [ ] **Step 1: Move and rename**

```bash
mkdir -p platform/posix platform/sdl

git mv linux/sys_linux.c  platform/posix/sys.c
git mv linux/net_udp.c    platform/posix/udp.c
git mv linux/glob.c       platform/posix/glob.c
git mv linux/q_shlinux.c  platform/posix/shared.c
git mv linux/vid_menu.c   platform/posix/vid_menu.c
git mv linux/glob.h       platform/posix/glob.h
git mv linux/rw_linux.h   platform/posix/rw.h

git mv sdl/vid_sdl.c      platform/sdl/vid.c
git mv sdl/snd_sdl.c      platform/sdl/sound.c
git mv sdl/in_sdl.c       platform/sdl/input.c
git mv sdl/glw_sdl.c      platform/sdl/glw.c
git mv sdl/qgl_sdl.c      platform/sdl/qgl.c
git mv sdl/verify_load.c  platform/verify_load.c

rmdir linux sdl
```

- [ ] **Step 2: Rewrite includes for the moved posix headers**

```bash
find client server qcommon game ref_gl platform \( -name '*.c' -o -name '*.h' \) \
  | xargs sed -i '' \
  -e 's|#include "linux/glob\.h"|#include "platform/posix/glob.h"|g' \
  -e 's|#include "linux/rw_linux\.h"|#include "platform/posix/rw.h"|g'
```

(All consumers — `platform/posix/{glob,shared,sys}.c` and `platform/sdl/{input,vid}.c` — are inside `platform/` by now. No file outside `platform/` includes these headers.)

- [ ] **Step 3: Update the Makefile**

Replace `SYS_EXE_OBJS` with exactly:

```make
# platform layer linked into the executable
SYS_EXE_OBJS = \
	$(BUILD_DIR)/platform/posix/shared.o \
	$(BUILD_DIR)/platform/posix/vid_menu.o $(BUILD_DIR)/platform/posix/sys.o \
	$(BUILD_DIR)/platform/posix/glob.o $(BUILD_DIR)/platform/posix/udp.o \
	$(BUILD_DIR)/platform/sdl/sound.o $(BUILD_DIR)/platform/sdl/vid.o
```

Replace `REF_EXTRA_OBJS` with exactly:

```make
REF_EXTRA_OBJS = \
	$(BUILD_DIR)/platform/posix/shared.o $(BUILD_DIR)/platform/posix/glob.o \
	$(BUILD_DIR)/platform/sdl/qgl.o $(BUILD_DIR)/platform/sdl/glw.o \
	$(BUILD_DIR)/platform/sdl/input.o
```

Change the `VERIFY_LOAD` rule to:

```make
$(VERIFY_LOAD): $(BUILD_DIR)/platform/verify_load.o
	$(CC) $(CFLAGS) -o $@ $(BUILD_DIR)/platform/verify_load.o $(SDL_LIBS) -ldl
```

- [ ] **Step 4: Gate**

```bash
make clean && make verify-load
```

Expected: exit 0.

- [ ] **Step 5: Zero-change proof — symbol diff**

```bash
nm -j build/quake2        | LC_ALL=C sort > /tmp/q2restructure/exe.after
nm -j build/ref_gl.so     | LC_ALL=C sort > /tmp/q2restructure/ref.after
nm -j baseq2/gamearm64.so | LC_ALL=C sort > /tmp/q2restructure/game.after
nm -j build/verify_load   | LC_ALL=C sort > /tmp/q2restructure/verify.after

diff /tmp/q2restructure/exe.syms    /tmp/q2restructure/exe.after
diff /tmp/q2restructure/ref.syms    /tmp/q2restructure/ref.after
diff /tmp/q2restructure/game.syms   /tmp/q2restructure/game.after
diff /tmp/q2restructure/verify.syms /tmp/q2restructure/verify.after
```

Expected: **all four diffs print nothing**. Any output means a symbol changed — stop and investigate before committing.

- [ ] **Step 6: Commit**

```bash
git add platform Makefile
git commit -m "refactor: merge linux/ and sdl/ into platform/{posix,sdl}

Drop the *_linux/*_sdl suffixes; verify_load moves to the platform root.
nm symbol snapshots of all four binaries are unchanged."
```

---

### Task 8: Docs + final gate suite

**Files:**
- Modify: `AGENTS.md`
- Check: `README.md` (expected: no changes needed — it only references `baseq2/` and `tools/`)

- [ ] **Step 1: Update AGENTS.md Repository Layout table**

Replace the rows for `client/ server/ qcommon/ game/ ref_gl/ sdl/ linux/` with rows describing the new tree, using the spec's **Target tree** section verbatim as the source of truth (`client/` with net/screen/sound, `server/` flat, `qcommon/` unchanged, `game/` with monsters/player, `ref_gl/` flat, `platform/` with posix/sdl). Update the `build/` row to note it now mirrors the source tree. Keep the table's existing one-line-per-row style.

- [ ] **Step 2: Update AGENTS.md Architecture Notes**

Follow the spec's **Makefile rewrite** and **Protected items** sections:

- Shared-sources bullet: `game/q_shared.c` builds **once** as `build/game/q_shared.o` and links all three units; `game/monsters/flash.c` → `build/game/monsters/flash.o` into exe+DLL; `platform/posix/{shared,glob}.c` into exe+ref_gl. Add: CFLAGS is uniform across units — keep it that way, or reintroduce per-unit object dirs.
- Protected items bullet: update paths (`platform/sdl/qgl.c`, `platform/sdl/vid.c`, `ref_gl/rmain.c`; `game/q_shared.c` unchanged).

- [ ] **Step 3: Add the include convention to Development Conventions**

Add a bullet:

```markdown
- **Root-relative includes:** every `#include "..."` is a repo-root-relative path (`-I.` is set in the Makefile). No bare-basename or `../` includes. Lint: `grep -rn '#include "' --include='*.[ch]' client server qcommon game ref_gl platform | grep -v '#include ".*/'` must print nothing.
```

- [ ] **Step 4: Final gate suite**

```bash
make clean && make verify-load
./build/quake2 +set developer 1 +map base1   # smoke test — then quit (Esc → quit or Cmd+Q)
make test-ref
make tools-test
```

Smoke assertions: log shows `SpawnServer: base1`, `client_connect`, `maps/base1.bsp` loaded, no `FATAL`/`ShutdownError`. (The CoreAudio `-66681` attract-loop issue is environmental, not a regression.)

Re-run the four symbol diffs from Task 7 Step 5 — still empty.

- [ ] **Step 5: Commit**

```bash
git add AGENTS.md
git commit -m "docs: update AGENTS.md for folder restructure"
```

---

### Task 9: Merge

- [ ] **Step 1: Confirm the branch state**

```bash
git status          # clean
git log --oneline main..HEAD   # spec + 8 commits
```

- [ ] **Step 2: Merge to main (requires user sign-off)**

```bash
git switch main
git merge --no-ff chore/folder-restructure -m "Merge branch 'chore/folder-restructure' — module/component folder restructure"
```

Do not push unless the user explicitly asks.
