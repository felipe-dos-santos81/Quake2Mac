# Folder restructure design — module/component subfolders

Date: 2026-09-02 · Branch: `chore/folder-restructure` · Status: approved

## Goals and locked decisions

Restructure the seven source directories (`client server qcommon game ref_gl
sdl linux`) into module/component subfolders, and establish header discipline.

Locked during brainstorming (all choices user-approved):

1. **Goal scope:** folders + header discipline (includes become explicit
   repo-root-relative paths).
2. **Traceability:** modularity first. Upstream diff-ability against the id
   tree is deliberately sacrificed; the mapping table in this spec is the
   permanent old→new cross-reference.
3. **Filenames:** rename files to match their folders — drop directory-
   redundant prefixes (`cl_`, `snd_`, `sv_`, `g_`, `m_`, `p_`, `gl_`,
   `*_sdl`, `*_linux`).
4. **Approach A + platform merge:** link-unit boundaries stay as top-level
   directories; large directories subdivide; `linux/` + `sdl/` merge into
   `platform/{posix,sdl}`. `qcommon/` stays flat and unrenamed.
5. **Zero behavior change:** moves, renames, include-path rewrites, and
   Makefile mechanics only. No C logic changes. Verified at symbol level
   (nm snapshot, §7).

Flagged judgement calls, approved as-is: `main.c` exists in `client/`,
`server/`, `game/` and `local.h` in `game/`, `ref_gl/` (consequence of
strict prefix-stripping); `snd_sdl.c → platform/sdl/sound.c` and
`snd_loc.h → client/sound/loc.h`; `verify_load.c` moves up to `platform/`
root (it uses no SDL API); `m_player.h` lives in `game/monsters/player.h`.

## Target tree

```
client/                          # client core ── exe unit
├── main.c input.c               # was cl_main, cl_input
├── client.h ref.h vid.h input.h anorms.h
├── net/                         # was cl_parse cl_pred cl_ents cl_tent cl_fx cl_newfx
│   └── parse.c predict.c ents.c tents.c fx.c newfx.c
├── screen/                      # was cl_scrn cl_view cl_cin cl_inv console keys menu qmenu
│   ├── scrn.c view.c cinematic.c inv.c console.c keys.c menu.c qmenu.c
│   └── screen.h console.h keys.h qmenu.h
└── sound/                       # was snd_dma snd_mem snd_mix
    ├── dma.c mem.c mix.c
    └── sound.h loc.h            # loc.h was snd_loc.h

server/                          # flat, exe unit — sv_* prefix dropped
├── main.c init.c send.c user.c world.c ents.c game.c ccmds.c
└── server.h

qcommon/                         # unchanged (names already prefix-free)

game/                            # game-DLL unit
├── ai.c chase.c cmds.c combat.c func.c items.c main.c misc.c
│   monster.c phys.c save.c spawn.c svcmds.c target.c trigger.c
│   turret.c utils.c weapon.c    # was g_*; g_local.h → local.h
├── game.h q_shared.c q_shared.h
├── monsters/                    # was m_* — prefix dropped
│   ├── actor.c … tank.c  move.c  flash.c
│   └── actor.h … tank.h  player.h   # player.h was m_player.h
└── player/                      # was p_*
    └── client.c hud.c trail.c view.c weapon.c

ref_gl/                          # flat, renderer bundle — gl_* prefix dropped
├── draw.c image.c light.c mesh.c model.c rmain.c rmisc.c
│   rsurf.c warp.c override.c
├── local.h model.h qgl.h        # was gl_local.h gl_model.h; qgl.h keeps its name
├── stb_image.h anormtab.h warpsin.h
└── tests/test_override.c
                                 # anorms.h copy DELETED (§4)

platform/                        # merge of linux/ + sdl/
├── verify_load.c                # moved to platform root (uses no SDL API)
├── posix/                       # was linux/
│   ├── sys.c udp.c glob.c shared.c vid_menu.c
│   └── glob.h rw.h              # rw.h was rw_linux.h
└── sdl/                         # was sdl/
    └── vid.c sound.c input.c glw.c qgl.c
```

`qcommon/`, `tools/`, `docs/`, `baseq2/`, `build/` are untouched.
Net: 155 files in the seven dirs; 128 moved/renamed; 1 deleted
(`ref_gl/anorms.h`); zero code changes.

## Complete mapping (old → new)

### client/ (30 files: 25 move, 5 unchanged)

| old | new |
|---|---|
| cl_main.c | client/main.c |
| cl_input.c | client/input.c |
| cl_parse.c | client/net/parse.c |
| cl_pred.c | client/net/predict.c |
| cl_ents.c | client/net/ents.c |
| cl_tent.c | client/net/tents.c |
| cl_fx.c | client/net/fx.c |
| cl_newfx.c | client/net/newfx.c |
| cl_scrn.c | client/screen/scrn.c |
| cl_view.c | client/screen/view.c |
| cl_cin.c | client/screen/cinematic.c |
| cl_inv.c | client/screen/inv.c |
| console.c | client/screen/console.c |
| keys.c | client/screen/keys.c |
| menu.c | client/screen/menu.c |
| qmenu.c | client/screen/qmenu.c |
| snd_dma.c | client/sound/dma.c |
| snd_mem.c | client/sound/mem.c |
| snd_mix.c | client/sound/mix.c |
| screen.h | client/screen/screen.h |
| console.h | client/screen/console.h |
| keys.h | client/screen/keys.h |
| qmenu.h | client/screen/qmenu.h |
| sound.h | client/sound/sound.h |
| snd_loc.h | client/sound/loc.h |
| client.h, ref.h, vid.h, input.h, anorms.h | unchanged (anorms.h becomes the single canonical copy) |

### server/ (9 files: 8 rename, server.h unchanged)

sv_main.c → server/main.c · sv_init.c → server/init.c · sv_send.c →
server/send.c · sv_user.c → server/user.c · sv_world.c → server/world.c ·
sv_ents.c → server/ents.c · sv_game.c → server/game.c · sv_ccmds.c →
server/ccmds.c

### qcommon/ (12 files: unchanged)

common.c cmd.c cvar.c files.c cmodel.c pmove.c net_chan.c crc.c md4.c
qcommon.h qfiles.h crc.h

### game/ (73 files: 70 move, 3 unchanged)

g_* → game/ root, prefix dropped (18 .c): g_ai.c → ai.c · g_chase.c →
chase.c · g_cmds.c → cmds.c · g_combat.c → combat.c · g_func.c → func.c ·
g_items.c → items.c · g_main.c → main.c · g_misc.c → misc.c · g_monster.c →
monster.c · g_phys.c → phys.c · g_save.c → save.c · g_spawn.c → spawn.c ·
g_svcmds.c → svcmds.c · g_target.c → target.c · g_trigger.c → trigger.c ·
g_turret.c → turret.c · g_utils.c → utils.c · g_weapon.c → weapon.c ·
g_local.h → local.h. **Unchanged:** game.h, q_shared.c, q_shared.h.

p_* → game/player/ (5 .c): p_client.c → player/client.c · p_hud.c →
player/hud.c · p_trail.c → player/trail.c · p_view.c → player/view.c ·
p_weapon.c → player/weapon.c

m_* → game/monsters/ (24 .c + 22 .h), prefix dropped: each `m_<name>.c/.h`
→ `monsters/<name>.c/.h` for actor, berserk, boss2, boss3, boss31, boss32,
brain, chick, flipper, float, flyer, gladiator, gunner, hover, infantry,
insane, medic, mutant, parasite, soldier, supertank, tank. Plus: m_move.c →
monsters/move.c (no header) · m_flash.c → monsters/flash.c (no header) ·
m_player.h → monsters/player.h (no .c; there is no m_boss3.h).

### ref_gl/ (18 files: 12 rename, 5 unchanged, 1 deleted)

gl_draw.c → draw.c · gl_image.c → image.c · gl_light.c → light.c ·
gl_mesh.c → mesh.c · gl_model.c → model.c · gl_rmain.c → rmain.c ·
gl_rmisc.c → rmisc.c · gl_rsurf.c → rsurf.c · gl_warp.c → warp.c ·
gl_override.c → override.c · gl_local.h → local.h · gl_model.h → model.h.
**Unchanged:** qgl.h, stb_image.h, anormtab.h, warpsin.h,
tests/test_override.c (its include paths update). **Deleted:** anorms.h
(byte-identical to client/anorms.h, `cmp`-verified 2026-09-02; gl_mesh.c
switches to `"client/anorms.h"`).

### linux/ → platform/posix/ (7 files)

sys_linux.c → platform/posix/sys.c · net_udp.c → platform/posix/udp.c ·
glob.c → platform/posix/glob.c · q_shlinux.c → platform/posix/shared.c ·
vid_menu.c → platform/posix/vid_menu.c (name kept) · glob.h →
platform/posix/glob.h · rw_linux.h → platform/posix/rw.h

### sdl/ → platform/ (6 files)

vid_sdl.c → platform/sdl/vid.c · snd_sdl.c → platform/sdl/sound.c ·
in_sdl.c → platform/sdl/input.c · glw_sdl.c → platform/sdl/glw.c ·
qgl_sdl.c → platform/sdl/qgl.c · verify_load.c → platform/verify_load.c

## Include discipline

- `BASE_CFLAGS` gains `-I.`; every quoted include becomes repo-root-relative:
  `#include "game/q_shared.h"`, `#include "client/client.h"`,
  `#include "platform/posix/rw.h"`.
- Mechanical one-pass rewrite (~25 distinct basenames; `g_local.h` ×46 the
  largest). Existing `../` hacks normalize into the same scheme:
  `../client/anorms.h` (qcommon/common.c), `../qcommon/qcommon.h` ×4,
  `../ref_gl/gl_local.h` ×3, `../linux/rw_linux.h` ×3,
  `../client/client.h` ×3 (server sources), `../linux/glob.h` ×2,
  `../client/qmenu.h` ×2.
- The anorms.h collision is resolved by the §3 deletion, not by paths.
- Include-guard macro names are unchanged.
- Lint rule (goes into AGENTS.md): `grep -rn '#include "' --include='*.[ch]' | grep -v '/'`
  must return nothing; no bare-basename or `../` includes; angle-bracket
  includes (SDL3, system) unchanged.

## Makefile rewrite

- Delete `vpath %.c client server qcommon game ref_gl linux sdl`,
  `.SECONDEXPANSION`, and the `$(notdir $*).c` basename hack. New rule:

  ```make
  $(BUILD_DIR)/%.o: %.c
  	@mkdir -p $(dir $@)
  	$(CC) $(CFLAGS) -o $@ -c $<
  ```

- Object paths mirror source paths; object group lists are rewritten to
  match:

  ```make
  COMMON_OBJS   = build/qcommon/{cmd,cmodel,common,crc,cvar,files,md4,net_chan,pmove}.o
                  + build/game/q_shared.o
  CLIENT_OBJS   = build/client/{main,input}.o
                  + build/client/net/{parse,predict,ents,tents,fx,newfx}.o
                  + build/client/screen/{scrn,view,cinematic,inv,console,keys,menu,qmenu}.o
                  + build/client/sound/{dma,mem,mix}.o
                  + build/game/monsters/flash.o
  SERVER_OBJS   = build/server/{ccmds,ents,game,init,main,send,user,world}.o
  SYS_EXE_OBJS  = build/platform/posix/{shared,vid_menu,sys,glob,udp}.o
                  + build/platform/sdl/{sound,vid}.o
  REF_CORE_OBJS = build/ref_gl/{draw,image,light,mesh,model,rmain,rmisc,rsurf,warp,override}.o
  REF_EXTRA_OBJS= build/platform/posix/{shared,glob}.o
                  + build/platform/sdl/{qgl,glw,input}.o
  GAME_OBJS     = build/game/{ai,chase,cmds,combat,func,items,main,misc,monster,
                  phys,save,spawn,svcmds,target,trigger,turret,utils,weapon}.o
                  + build/game/player/{client,hud,trail,view,weapon}.o
                  + build/game/monsters/{actor,berserk,boss2,boss3,boss31,boss32,
                  brain,chick,flash,flipper,float,flyer,gladiator,gunner,hover,
                  infantry,insane,medic,move,mutant,parasite,soldier,supertank,tank}.o
                  + build/game/q_shared.o
  ```

- **Multi-unit sources compile once** (was 2–3×): `game/q_shared.o` links
  all three units; `game/monsters/flash.o` exe+DLL;
  `platform/posix/{shared,glob}.o` exe+ref_gl. This is safe because CFLAGS
  is uniform across units. *Recorded invariant:* keep CFLAGS uniform, or
  reintroduce per-unit object directories.
- All 12 targets, `##` help text, `-MMD -MP`, and link-line semantics are
  unchanged. `$(VERIFY_LOAD)` builds from `platform/verify_load.c`;
  `test-ref` links `ref_gl/tests/test_override.c` + `build/ref_gl/override.o`.

## Migration plan

Branch `chore/folder-restructure`. Every commit gated by
`make clean && make verify-load`. The clean is required on move commits —
stale `.d` files referencing old paths would break incremental make.
All moves use `git mv` so rename detection preserves history.

| # | Commit | Content |
|---|--------|---------|
| 1 | spec | this document |
| 2 | includes | `-I.` + root-relative include rewrite + anorms.h dedup; tree still flat |
| 3 | Makefile | mirror rule replaces vpath hack; multi-unit objects deduped; tree still flat (object paths identical) |
| 4 | game/ | monsters/ + player/ + prefix drops + GAME_OBJS + include-path updates |
| 5 | client/ | net/ screen/ sound/ + cl_/snd_ prefix drops + CLIENT_OBJS |
| 6 | server/ | sv_ prefix drops + SERVER_OBJS |
| 7 | ref_gl/ | gl_ prefix drops + REF_OBJS + test-ref path update |
| 8 | platform/ | linux/+sdl/ → platform/{posix,sdl}, verify_load move, delete old dirs, SYS_EXE/REF_EXTRA/VERIFY_LOAD updates |
| 9 | docs | AGENTS.md layout/conventions rewrite; README touch-ups |

## Verification

- **Per commit (2–8):** `make clean && make verify-load` — must exit 0.
- **Commit 9 final suite:** full gate above, plus runtime smoke
  `./build/quake2 +set developer 1 +map base1` (assertions:
  `SpawnServer: base1`, `client_connect`, `maps/base1.bsp`, no
  `FATAL`/`ShutdownError`), `make test-ref`, `make tools-test`.
- **Symbol snapshot (zero-change proof):** before commit 2, snapshot
  `nm -j <binary> | LC_ALL=C sort` for `build/quake2`, `build/ref_gl.so`,
  `baseq2/gamearm64.so`, `build/verify_load` (built from current main).
  After commit 8, repeat and diff — the lists must be identical; no symbol
  was renamed, only files moved.
- Benign build noise is expected to be unchanged: the `ld` warning
  `reducing alignment of section __DATA,__common` and the ~200+ pre-existing
  `-Wall` warnings.

## Protected items (contents undisturbed; new paths listed)

- `platform/sdl/qgl.c` (was sdl/qgl_sdl.c): the `qgl*` GL dispatch table +
  7 `qgl*EXT` pointers — rename + include changes only.
- `game/q_shared.c`: byte-swap cluster (`Swap_Init`, `_Big*/_Little*`,
  wrappers) — unchanged, still compiled once into all three units.
- `platform/sdl/vid.c` (was sdl/vid_sdl.c): `RW_IN_Activate_fp` — untouched.
- `ref_gl/rmain.c` (was ref_gl/gl_rmain.c): `R_Init()` returning `true` —
  untouched.
- `baseq2/` remains user territory; nothing there is staged by this work.

## Risks

- Mechanical include rewrite across ~150 files — mitigated by the
  compile-gated staging (includes normalized before any file moves).
- Stale `.d` header-dependency files after moves — mitigated by the clean
  gate on every move commit.
- Ambiguity from duplicate basenames (`main.c` ×3, `local.h` ×2) — accepted;
  root-relative includes make every reference unambiguous.
- Loss of upstream (id tree) diff-ability — accepted; this spec's mapping
  table is the cross-reference.

## Out of scope

- Any C logic change; anything in `qcommon/`, `tools/`, `baseq2/`, `docs/`
  other than the layout-table updates in AGENTS.md (commit 9); behavior
  changes of any kind. The only deletion is the duplicated `ref_gl/anorms.h`.
