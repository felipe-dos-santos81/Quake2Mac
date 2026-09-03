# Warning-Cleanup Round 5 — 2026-09-03

Goal: a full `make clean && make build` with **zero** compiler warnings and
no `ld` alignment warning, at **zero behavior change**. No `-Wno-*` flags,
no pragmas — every warning gets a real fix.

## Inventory (full shadow compile of every TU with -Wall)

4,640 warnings:

| Count | Flag | Where |
|---|---|---|
| 4437 | `-Wmissing-braces` | `game/monsters/*.c` — flat `mframe_t` initializers (`ai_stand, 0, NULL,`) |
| 117 | `-Wpointer-to-int-cast` | `game/save.c` `FOFS` field-offset macro (87), `game/player/*`, `client/screen/scrn.c`, misc |
| 26 | `-Wunused-but-set-variable` | scattered |
| 26 | `-Wpointer-sign` | scattered (`client/main.c`, `cinematic.c`, ...) |
| 6 | `-Wdangling-else` | add braces |
| 6 | `-Wabsolute-value` | wrong `abs` family |
| 5 | `-Wincompatible-pointer-types-discards-qualifiers` | const/cast fixes |
| 4 | `-Wreturn-type` | missing returns |
| 3 | `-Wunused-variable` | remove |
| 3 | `-Wswitch` | missing cases |
| 3 | `-Wincompatible-pointer-types` | fix types/casts |
| 2 | `-Wparentheses-equality` | drop redundant parens |
| 1 | `-Wsometimes-uninitialized` | initialize |
| 1 | `-Wmisleading-indentation` | reformat |

Plus at link: `ld: warning: reducing alignment of section
__DATA,__common from 0x8000 to 0x4000` — ld64 aligns large COMMON symbols
to their size rounded up to a power of two; the `dma` struct (0x7f2c),
`file_from_pak` (0x56e8) and the vid common (0x4688) demand 0x8000.

## Strategy

1. **`-fno-common` in BASE_CFLAGS.** Tentative definitions become real
   definitions, `__common` disappears, the ld warning goes away, and it
   matches upstream clang's default. Verified safe for the multi-unit
   shared sources (`game/q_shared.c`, `game/monsters/flash.c`,
   `platform/posix/{shared,glob}.c`): their writable globals (`bigendien`,
   `com_token`, `maxhunksize`, `curhunksize`, `curtime`) are never read by
   another link unit — ref_gl references none of them, parsing through
   `com_token` is synchronous per unit, and `bigendien` is false on arm64
   either way. The exe links with no duplicate-symbol errors, so no TU pair
   relied on common merging.
2. **Brace the `mframe_t` initializers** in `game/monsters/*.c` with a
   scripted mechanical pass (`ai_stand, 0, NULL,` → `{ai_stand, 0, NULL},`).
   Identical initializer semantics.
3. **`FOFS`** (`game/local.h`): `(int)(intptr_t)&(((edict_t *)0)->x)` —
   keeps the int field layout and the savegame format byte-identical; the
   truncation is benign (field offsets) but must stay value-identical.
   Same rule for every other cast: preserve the exact current value; never
   change struct layouts or on-disk formats.
4. **Everything else:** per-site real fixes (braces for dangling-else,
   correct abs/fabs/labs, explicit casts for pointer-sign, missing returns,
   switch cases, initializers, reformatting).
5. **Per-unit commits:** build flag; game monsters; game core; client;
   server+qcommon; ref_gl+platform; docs touch-up (the "known benign build
   noise" paragraph in AGENTS.md retires with this round).

## Gate

`make clean && make verify-load` must print no `warning:` line and no ld
warning; then `make smoke` and an attract-loop sanity run. Full clean gate
again on main after the `--no-ff` merge.
