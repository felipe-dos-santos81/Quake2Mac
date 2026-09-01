# Quake II Apple Silicon-Only Cleanup — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Quake II tree Apple Silicon/arm64-only by deleting all Windows/Linux/other-platform-only code and dead code (210 files + in-file conditional sweep + dead-function audit), keeping the SDL3 build fully functional.

**Architecture:** Pure deletion first (two phases, proven unreferenced by the Makefile object lists and include graph), then careful edits to live files (never-compiled conditional sweep, then symbol-level dead-function audit). Every phase ends with the build+load gate. The root `Makefile` is never edited; the branch merges to master with four cleanup commits.

**Tech Stack:** Original GPL Quake II 3.21 C sources, the 2026-08-31 SDL3 port (`sdl/`), Apple clang, GNU make 3.81, macOS arm64.

**Spec:** `docs/superpowers/specs/2026-09-01-quake2-apple-silicon-cleanup-design.md` (read it — the plan argues from it).

## Global Constraints

1. **Regression gate** after every phase and after every phase-4 batch:
   `make clean && make build && make verify-load` — must exit 0.
2. **The root `Makefile` is never edited.** Nothing deleted is in any object list, and no deletion changes vpath basename resolution (spec §7).
3. **Preserve fix `ada44e2` verbatim:** in `sdl/qgl_sdl.c` the seven legacy extension pointers (`qglLockArraysEXT`, `qglUnlockArraysEXT`, `qglPointParameterfEXT`, `qglPointParameterfvEXT`, `qglColorTableEXT`, `qglSelectTextureSGIS`, `qglMTexCoord2fSGIS`) stay unresolved/NULL (the Metal-backed legacy GL framework exports them but rejects their enums with `GL_INVALID_ENUM`; the renderer null-checks and takes GL 1.1 fallbacks). In `ref_gl/gl_rmain.c`, `R_Init()` keeps its `return true;`. Never re-introduce resolution of these pointers anywhere.
4. **Never** `git add -A` or `git commit -a`. Stage explicit paths only. `baseq2/` holds user play data (paks untracked; `save/save0/*.ssv` tracked-but-mutated by play) and must never enter a commit; same for `.DS_Store`.
5. **Never push.** `origin` is the upstream `id-Software/Quake-2` repository.
6. `#if 0` blocks and commented-out code are left untouched.
7. `qcommon/md4.c` stays (removing it would require a Makefile edit). Its dead status is recorded in the phase-4 commit message as a follow-up.
8. Branch `cleanup/apple-silicon-only`, four commits with the exact messages given below, merged to master with `--no-ff` at the end.
9. If a step fails or a file's content doesn't match what a step expects: **stop and report** with the exact output. Do not improvise around it.

---

### Task 1: Branch and baseline gate

**Files:** none (git state + build only)

**Interfaces:**
- Consumes: master at the spec-update commit (`f5ffbd3` or later) with the `ada44e2` fix included
- Produces: branch `cleanup/apple-silicon-only` with a verified-green baseline build; every later task starts from this branch

- [ ] **Step 1: Verify starting state**

```bash
cd /Users/felipe.dos.santos/code/theirs/Quake-2
git status --short
git log --oneline -3
```

Expected: `git status` shows only ` M baseq2/save/save0/game.ssv`, ` M baseq2/save/save0/server.ssv`, and untracked `baseq2/` data plus `.DS_Store` files — no other tracked modifications. `git log` shows `f5ffbd3` (or later) with `ada44e2 fix: resolve renderer init failure on macOS ...` in history. If any other tracked file is modified, stop and report.

- [ ] **Step 2: Baseline gate on master**

```bash
make clean && make build && make verify-load
```

Expected: builds `build/quake2`, `build/ref_gl.so`, `baseq2/gamearm64.so`, then verify-load prints its OK lines for both bundles and exits 0.

- [ ] **Step 3: Create the branch**

```bash
git checkout -b cleanup/apple-silicon-only
```

No commit in this task.

---

### Task 2: Phase 1 — delete dead platform ports and modules (161 files)

**Files:**
- Delete (dirs): `win32/` (25), `ctf/` (54), `ref_soft/` (41), `rhapsody/` (12), `irix/` (8), `solaris/` (8), `unix/` (3)
- Delete (root): `quake2.001` `quake2.bce` `quake2.bcp` `quake2.dsp` `quake2.dsw` `quake2.mak` `quake2.opt` `quake2.plg` `makezip` `makezip.bat`

**Interfaces:**
- Consumes: Task 1's branch with green baseline
- Produces: commit 1; tree without any dead platform directory; unchanged build outputs

- [ ] **Step 1: Delete the seven directories**

```bash
git rm -r win32 irix solaris rhapsody unix ref_soft ctf
```

- [ ] **Step 2: Delete the root MSVC/packaging files**

```bash
git rm quake2.001 quake2.bce quake2.bcp quake2.dsp quake2.dsw quake2.mak quake2.opt quake2.plg makezip makezip.bat
```

- [ ] **Step 3: Verify exactly 161 files are staged**

```bash
git diff --cached --name-only | wc -l
git diff --cached --stat | tail -1
```

Expected: `161` and `... 161 files changed ...` (all deletions, 0 insertions). If the count differs, stop and report — do not commit.

- [ ] **Step 4: Gate**

```bash
make clean && make build && make verify-load
```

Expected: exit 0. (Nothing deleted was in any object list, so the build is bit-for-bit the same pipeline.)

- [ ] **Step 5: Commit**

```bash
git commit -m "cleanup: remove dead platform ports, unused renderers/modules, and MSVC build files"
```

- [ ] **Step 6: Verify commit contents**

```bash
git status --short
git show --stat HEAD | tail -3
```

Expected: status shows only the known `baseq2/`/`.DS_Store` noise; the commit stat ends with `161 files changed`.

---

### Task 3: Phase 2 — prune dead files from live directories (49 files)

**Files:**
- Delete in `linux/` (28): `Makefile.AXP` `Makefile.i386` `block16.h` `block8.h` `cd_linux.c` `d_copy.s` `d_ifacea.h` `d_polysa.s` `gl_fxmesa.c` `in_linux.c` `math.s` `qasm.h` `qgl_linux.c` `r_aclipa.s` `r_draw16.s` `r_drawa.s` `r_edgea.s` `r_scana.s` `r_spr8.s` `r_surf8.s` `r_varsa.s` `rw_in_svgalib.c` `rw_svgalib.c` `rw_x11.c` `snd_linux.c` `snd_mixa.s` `sys_dosa.s` `vid_so.c`
- Delete in `null/` (7): `cl_null.c` `glimp_null.c` `in_null.c` `snddma_null.c` `swimp_null.c` `sys_null.c` `vid_null.c`
- Delete in `client/` (5): `x86.c` `asm_i386.h` `adivtab.h` `block8.h` `block16.h`
- Delete in `server/` (1): `sv_null.c`
- Delete in `game/` (4): `game.001` `game.def` `game.dsp` `game.plg`
- Delete in `ref_gl/` (4): `ref_gl.001` `ref_gl.def` `ref_gl.dsp` `ref_gl.plg`

**Interfaces:**
- Consumes: commit 1
- Produces: commit 2; `linux/` reduced to the 7 files the build uses; `client/anorms.h` still present (included by `qcommon/common.c`)

- [ ] **Step 1: Prune linux/**

```bash
git rm linux/Makefile.AXP linux/Makefile.i386 linux/block16.h linux/block8.h \
	linux/cd_linux.c linux/d_copy.s linux/d_ifacea.h linux/d_polysa.s \
	linux/gl_fxmesa.c linux/in_linux.c linux/math.s linux/qasm.h \
	linux/qgl_linux.c linux/r_aclipa.s linux/r_draw16.s linux/r_drawa.s \
	linux/r_edgea.s linux/r_scana.s linux/r_spr8.s linux/r_surf8.s \
	linux/r_varsa.s linux/rw_in_svgalib.c linux/rw_svgalib.c linux/rw_x11.c \
	linux/snd_linux.c linux/snd_mixa.s linux/sys_dosa.s linux/vid_so.c
```

- [ ] **Step 2: Prune null/, client/, server/**

```bash
git rm null/cl_null.c null/glimp_null.c null/in_null.c null/snddma_null.c \
	null/swimp_null.c null/sys_null.c null/vid_null.c
git rm client/x86.c client/asm_i386.h client/adivtab.h client/block8.h client/block16.h
git rm server/sv_null.c
```

- [ ] **Step 3: Prune MSVC files from game/ and ref_gl/**

```bash
git rm game/game.001 game/game.def game/game.dsp game/game.plg
git rm ref_gl/ref_gl.001 ref_gl/ref_gl.def ref_gl/ref_gl.dsp ref_gl/ref_gl.plg
```

- [ ] **Step 4: Verify 49 files staged and the keep-set intact**

```bash
git diff --cached --name-only | wc -l
ls linux null
```

Expected: `49`. `linux/` contains exactly: `glob.c glob.h net_udp.c q_shlinux.c rw_linux.h sys_linux.c vid_menu.c`. `null/` contains exactly `cd_null.c`. Also confirm `ls client/anorms.h client/input.h client/cdaudio.h` all exist (still-kept headers).

- [ ] **Step 5: Gate**

```bash
make clean && make build && make verify-load
```

Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
git commit -m "cleanup: prune dead files from live directories (linux asm/svgalib/x11, null stubs, x86 client files, MSVC files)"
```

---

### Task 4: Phase 3 — sweep never-compiled conditionals (42 sites, 17 files)

**Files (modify):** `client/cl_main.c`, `client/menu.c`, `client/snd_mix.c`, `client/client.h`, `qcommon/cmodel.c`, `qcommon/files.c`, `qcommon/md4.c`, `qcommon/qcommon.h`, `game/g_save.c`, `game/q_shared.c`, `game/q_shared.h`, `ref_gl/gl_local.h`, `ref_gl/qgl.h`, `ref_gl/gl_warp.c`, `ref_gl/gl_rmisc.c`, `ref_gl/gl_rmain.c`, `linux/sys_linux.c`

**Interfaces:**
- Consumes: commit 2
- Produces: commit 3; zero non-`__APPLE__` platform conditionals in live sources; `ada44e2` behavior intact

Rules for every edit below (spec §5): keep exactly the branch that compiles on arm64 macOS today; remove the `#if/#else/#endif` scaffolding around it; no behavioral change. Line numbers are guides as of `ada44e2` — always anchor on the quoted text. Process files one at a time, top to bottom.

- [ ] **Step 1: `client/cl_main.c` (~line 1788)**

Replace

```c
#if defined __linux__ || defined __sgi
	S_Init ();
	VID_Init ();
#else
	VID_Init ();
	S_Init ();	// sound must be initialized after window is created
#endif
```

with

```c
	VID_Init ();
	S_Init ();	// sound must be initialized after window is created
```

- [ ] **Step 2: `client/menu.c` (two sites)**

Site A (~line 21) — delete these three lines entirely:

```c
#ifdef _WIN32
#include <io.h>
#endif
```

Site B (~line 2545) — replace

```c
#ifdef _WIN32
		length = filelength( fileno( fp  ) );
#else
		fseek(fp, 0, SEEK_END);
		length = ftell(fp);
		fseek(fp, 0, SEEK_SET);
#endif
```

with

```c
		fseek(fp, 0, SEEK_END);
		length = ftell(fp);
		fseek(fp, 0, SEEK_SET);
```

- [ ] **Step 3: `client/snd_mix.c` (two nested wrappers)**

Site A — `S_WriteLinearBlastStereo16` (~lines 33–106). Delete the two guard lines

```c
#if !(defined __linux__ && defined __i386__)
#if	!id386
```

that immediately precede `void S_WriteLinearBlastStereo16 (void)`. Then delete everything from the `#else` that follows the C function's closing `}` through the end of the MSVC `__declspec( naked )` copy, i.e. delete the `#else` line, the whole `__declspec( naked ) void S_WriteLinearBlastStereo16 (void) { __asm { ... } }` function (its body is x86 `push edi ... LWLBLoopTop ... ret` assembly), and the final two lines

```c
#endif
#endif
```

Keep the C implementation of `S_WriteLinearBlastStereo16` exactly as is.

Site B — `S_PaintChannelFrom8` (~lines 365–467). Same shape: delete the two guard lines

```c
#if !(defined __linux__ && defined __i386__)
#if	!id386
```

preceding `void S_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int count, int offset)`, then delete from the `#else` after the C function's closing `}` through the MSVC `__declspec( naked )` copy (`push esi ... LMix8Loop ... ret` assembly) and its trailing

```c
#endif
#endif
```

Keep the C implementation of `S_PaintChannelFrom8`. `S_PaintChannelFrom16` below it is untouched.

- [ ] **Step 4: `client/client.h` (~line 579)**

Delete this block entirely:

```c
#if id386
void x86_TimerStart( void );
void x86_TimerStop( void );
void x86_TimerInit( unsigned long smallest, unsigned longest );
unsigned long *x86_TimerGetHistogram( void );
#endif
```

- [ ] **Step 5: `qcommon/cmodel.c` (two sites)**

Delete (~line 1446):

```c
#ifdef _WIN32
#pragma optimize( "", off )
#endif
```

Delete (~line 1511):

```c
#ifdef _WIN32
#pragma optimize( "", on )
#endif
```

- [ ] **Step 6: `qcommon/files.c` (~line 718)**

Delete these three lines:

```c
#ifdef _WIN32
			strlwr( list[nfiles] );
#endif
```

- [ ] **Step 7: `qcommon/md4.c` (~line 12)**

Replace

```c
/* UINT4 defines a four byte word */
#ifdef __alpha__
typedef unsigned int UINT4;
#else
typedef unsigned long int UINT4;
#endif
```

with

```c
/* UINT4 defines a four byte word */
typedef unsigned int UINT4;
```

(4-byte words are what MD4 requires; this is what the `__alpha__` branch selected for LP64 targets. Do not delete any other part of this file — spec §1 md4.c decision.)

- [ ] **Step 8: `qcommon/qcommon.h` (~lines 30–71)**

Replace the entire BUILDSTRING/CPUSTRING chain — from `#ifdef WIN32` through the final `#endif` before the `typedef struct sizebuf_s` banner, i.e.

```c
#ifdef WIN32

#ifdef NDEBUG
#define BUILDSTRING "Win32 RELEASE"
#else
#define BUILDSTRING "Win32 DEBUG"
#endif

#ifdef _M_IX86
#define	CPUSTRING	"x86"
#elif defined _M_ALPHA
#define	CPUSTRING	"AXP"
#endif

#elif defined __linux__

#define BUILDSTRING "Linux"

#ifdef __i386__
#define CPUSTRING "i386"
#elif defined __alpha__
#define CPUSTRING "axp"
#else
#define CPUSTRING "Unknown"
#endif

#elif defined __sun__

#define BUILDSTRING "Solaris"

#ifdef __i386__
#define CPUSTRING "i386"
#else
#define CPUSTRING "sparc"
#endif

#else	// !WIN32

#define BUILDSTRING "NON-WIN32"
#define	CPUSTRING	"NON-WIN32"

#endif
```

with

```c
#define BUILDSTRING "NON-WIN32"
#define	CPUSTRING	"NON-WIN32"
```

(These are the values arm64 macOS compiles today, via the final `#else`.)

- [ ] **Step 9: `game/g_save.c` (~line 712)**

Replace

```c
#ifdef _WIN32
	if (base != (void *)InitGame)
	{
		fclose (f);
		gi.error ("ReadLevel: function pointers have moved");
	}
#else
	gi.dprintf("Function offsets %d\n", ((byte *)base) - ((byte *)InitGame));
#endif
```

with

```c
	gi.dprintf("Function offsets %d\n", ((byte *)base) - ((byte *)InitGame));
```

- [ ] **Step 10: `game/q_shared.c` (five sites)**

Site A (~line 28) — delete:

```c
#ifdef _WIN32
#pragma optimize( "", off )
#endif
```

Site B (~line 87) — delete:

```c
#ifdef _WIN32
#pragma optimize( "", on )
#endif
```

Site C (~line 264) — delete the whole block:

```c
#if defined _M_IX86 && !defined C_ONLY
#pragma warning (disable:4035)
__declspec( naked ) long Q_ftol( float f )
{
	static int tmp;
	__asm fld dword ptr [esp+4]
	__asm fistp tmp
	__asm mov eax, tmp
	__asm ret
}
#pragma warning (default:4035)
#endif
```

Site D — `BoxOnPlaneSide` (~line 348). First replace the guard

```c
#if !id386 || defined __linux__
int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct cplane_s *p)
```

with

```c
int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct cplane_s *p)
```

Then delete the entire `#else` half: from the line `#else` that follows the C version's closing `}` (right after `return sides;` / `}`), through `#pragma warning( disable: 4035 )`, the whole `__declspec( naked ) int BoxOnPlaneSide ...` x86 jump-table assembly (labels `Lcase0`…`Lcase7`, `LSetSides`, `Lerror`), `#pragma warning( default: 4035 )`, and the final `#endif` (~line 648). The C version stays complete; `BoxOnPlaneSide2` above it is untouched.

Site E — `Q_stricmp` (~line 1180). Replace

```c
int Q_stricmp (char *s1, char *s2)
{
#if defined(WIN32)
	return _stricmp (s1, s2);
#else
	return strcasecmp (s1, s2);
#endif
}
```

with

```c
int Q_stricmp (char *s1, char *s2)
{
	return strcasecmp (s1, s2);
}
```

- [ ] **Step 11: `game/q_shared.h` (three sites)**

Site A (~line 23) — delete the whole block:

```c
#ifdef _WIN32
// unknown pragmas are SUPPOSED to be ignored, but....
#pragma warning(disable : 4244)     // MIPS
#pragma warning(disable : 4136)     // X86
#pragma warning(disable : 4051)     // ALPHA

#pragma warning(disable : 4018)     // signed/unsigned mismatch
#pragma warning(disable : 4305)		// truncation from const double to float

#endif
```

Site B (~line 42) — delete both macro definitions:

```c
#if (defined _M_IX86 || defined __i386__) && !defined C_ONLY && !defined __sun__
#define id386	1
#else
#define id386	0
#endif

#if defined _M_ALPHA && !defined C_ONLY
#define idaxp	1
#else
#define idaxp	0
#endif
```

Site C (~line 156) — replace

```c
#if !defined C_ONLY && !defined __linux__ && !defined __sgi && !defined __APPLE__
extern long Q_ftol( float f );
#else
#define Q_ftol( f ) ( long ) (f)
#endif
```

with

```c
#define Q_ftol( f ) ( long ) (f)
```

(The macro branch is what arm64 macOS compiles today; the `extern` branch's function definition was deleted in Site C above. Leave the two commented `Q_fabs` lines above this block untouched.)

- [ ] **Step 12: `ref_gl/gl_local.h` (~line 28)**

Delete:

```c
#ifdef _WIN32
#  include <windows.h>
#endif
```

Leave the `#ifdef __APPLE__` GL-header selection (`<OpenGL/gl.h>` / `<OpenGL/glu.h>`) exactly as is.

- [ ] **Step 13: `ref_gl/qgl.h` (three sites)**

Site A (~line 27) — delete:

```c
#ifdef _WIN32
#  include <windows.h>
#endif
```

Site B (~lines 391–427) — delete the entire `qwgl` extern block: from the `#ifdef _WIN32` that follows `extern	void ( APIENTRY * qglSelectTextureSGIS)( GLenum );` through the `#endif` that follows `extern BOOL ( WINAPI * qwglSetDeviceGammaRampEXT ) ( const unsigned char *pRed, const unsigned char *pGreen, const unsigned char *pBlue );`. Everything in between is `WINAPI`-style `qwgl*` function-pointer declarations (`qwglChoosePixelFormat`, `qwglSwapBuffers`, `qwglGetProcAddress`, `qwglSwapIntervalEXT`, gamma-ramp, etc.). Leave `qglLockArraysEXT`/`qglUnlockArraysEXT`/`qglMTexCoord2fSGIS`/`qglSelectTextureSGIS` externs above the block in place — they remain declared-and-NULL per the `ada44e2` constraint.

Site C (~line 437) — replace

```c
#ifdef __sgi
#define GL_SHARED_TEXTURE_PALETTE_EXT		GL_TEXTURE_COLOR_TABLE_SGI
#else
#define GL_SHARED_TEXTURE_PALETTE_EXT		0x81FB
#endif
```

with

```c
#define GL_SHARED_TEXTURE_PALETTE_EXT		0x81FB
```

- [ ] **Step 14: `ref_gl/gl_warp.c` (two sites)**

Site A (~line 234) — replace

```c
#if !id386
			s = os + r_turbsin[(int)((ot*0.125+r_newrefdef.time) * TURBSCALE) & 255];
#else
			s = os + r_turbsin[Q_ftol( ((ot*0.125+rdt) * TURBSCALE) ) & 255];
#endif
```

with

```c
			s = os + r_turbsin[(int)((ot*0.125+r_newrefdef.time) * TURBSCALE) & 255];
```

Site B (~line 242) — replace

```c
#if !id386
			t = ot + r_turbsin[(int)((os*0.125+rdt) * TURBSCALE) & 255];
#else
			t = ot + r_turbsin[Q_ftol( ((os*0.125+rdt) * TURBSCALE) ) & 255];
#endif
```

with

```c
			t = ot + r_turbsin[(int)((os*0.125+rdt) * TURBSCALE) & 255];
```

(Keep the first branch of each: it is what arm64 compiles today. Note the two branches differ in more than the cast — do not "merge" them.)

- [ ] **Step 15: `ref_gl/gl_rmisc.c` (~lines 238–244)**

Delete the whole block, which becomes empty once the `_WIN32` call is removed:

```c
		if ( !gl_state.stereo_enabled )
		{
#ifdef _WIN32
			if ( qwglSwapIntervalEXT )
				qwglSwapIntervalEXT( gl_swapinterval->value );
#endif
		}
```

- [ ] **Step 16: `ref_gl/gl_rmain.c` (one block; ada44e2-sensitive)**

Delete the `/*\n** grab extensions\n*/` comment and the entire `#ifdef WIN32 ... #endif` block that follows it (~lines 1243–1321): from `#ifdef WIN32` through the `#endif` immediately before `GL_SetDefaultState();`. The block is the Win32-only resolution of compiled-vertex-array, `WGL_EXT_swap_control`, point-parameters, paletted-texture, and SGIS-multitexture pointers via `qwglGetProcAddress` — the Win32 counterpart of exactly the seven pointers `ada44e2` deliberately leaves NULL. Do not re-add any resolution of them. **Verify `R_Init()` still ends with `return true;` (ada44e2) after the edit.**

- [ ] **Step 17: `linux/sys_linux.c` (five edits)**

Edit A — delete the mntent include guard at the top:

```c
#ifndef __APPLE__
#include <mntent.h>
#endif
```

Edit B — replace

```c
void Sys_Init(void)
{
#if id386
//	Sys_SetFPCW();
#endif
}
```

with

```c
void Sys_Init(void)
{
}
```

Edit C — replace the arch selector

```c
#ifdef __i386__
	const char *gamename = "gamei386.so";
#elif defined __alpha__
	const char *gamename = "gameaxp.so";
#elif defined(__aarch64__) || defined(__arm64__)
	const char *gamename = "gamearm64.so";
#else
#error Unknown arch
#endif
```

with

```c
	const char *gamename = "gamearm64.so";
```

Edit D — delete:

```c
#ifndef __APPLE__
	setreuid(getuid(), getuid());
	setegid(getgid());
#endif
```

Edit E — replace the **entire** `Sys_CopyProtect` function — from its signature `void Sys_CopyProtect(void)` through its closing `}` immediately before the trailing `#if 0` block, i.e. the `#ifdef __APPLE__ return; #else` form whose `#else` half holds the whole `setmntent`/`getmntent` CD check ending in

```c
	Com_Error (ERR_FATAL, "Unable to find a mounted iso9660 file system.\n"
		"You must mount the Quake2 CD in a cdrom drive in order to play.");
#endif
}
```

Replace all of it with

```c
void Sys_CopyProtect(void)
{
	return;
}
```

Leave the trailing `#if 0 ... Sys_MakeCodeWriteable ... #endif` block untouched (spec §1 `#if 0` policy).

- [ ] **Step 18: Verify no platform conditionals remain**

```bash
grep -rn 'id386\|idaxp\|WIN32\|_M_IX86\|_M_ALPHA\|__i386__\|__alpha__\|__sun__\|__sgi\|C_ONLY\|svgalib\|qwgl' \
	client qcommon server game ref_gl sdl linux null --include='*.c' --include='*.h'
```

Expected: **no output**. (`__APPLE__` guards may remain — they are the target platform.) If anything matches, resolve it per the rules above or stop and report.

- [ ] **Step 19: Gate**

```bash
make clean && make build && make verify-load
```

Expected: exit 0. Also confirm with `git diff --stat` that only the 17 files listed above are modified.

- [ ] **Step 20: Commit**

```bash
git add client/cl_main.c client/menu.c client/snd_mix.c client/client.h \
	qcommon/cmodel.c qcommon/files.c qcommon/md4.c qcommon/qcommon.h \
	game/g_save.c game/q_shared.c game/q_shared.h \
	ref_gl/gl_local.h ref_gl/qgl.h ref_gl/gl_warp.c ref_gl/gl_rmisc.c ref_gl/gl_rmain.c \
	linux/sys_linux.c
git commit -m "cleanup: remove never-compiled Win32/x86 conditionals from live sources"
```

(Explicit paths only — Global Constraint 4.)

---

### Task 5: Phase 4 — remove unreferenced functions

**Files:**
- Modify (4a): `client/menu.c`, `client/qmenu.c`, `linux/glob.c`, `ref_gl/gl_rmain.c`
- Modify (4b): determined by the audit, expected in `qcommon/` and `client/`

**Interfaces:**
- Consumes: commit 3
- Produces: commit 4; a fresh build with no `-Wunused-function` candidates among the deleted set; md4.c follow-up recorded in the commit message

#### Part 4a — proven-dead statics

- [ ] **Step 1: `client/menu.c` — delete `NoAltTabFunc` (~line 1065)**

```c
static void NoAltTabFunc( void *unused )
{
	Cvar_SetValue( "win_noalttab", s_options_noalttab_box.curvalue );
}
```

(Its only mention elsewhere is inside a `/* ... */` commented-out menu-item block near line 1319 — comments don't compile; leave that comment untouched.)

- [ ] **Step 2: `client/qmenu.c` — delete `Menulist_DoEnter` and `SpinControl_DoEnter`**

Delete the forward declarations (~lines 29 and 34):

```c
static void	 Menulist_DoEnter( menulist_s *l );
```

```c
static void	 SpinControl_DoEnter( menulist_s *s );
```

Delete the definition of `Menulist_DoEnter` (~line 555):

```c
void Menulist_DoEnter( menulist_s *l )
{
	int start;

	start = l->generic.y / 10 + 1;

	l->curvalue = l->generic.parent->cursor - start;

	if ( l->generic.callback )
		l->generic.callback( l );
}
```

Delete the definition of `SpinControl_DoEnter` (~line 628):

```c
void SpinControl_DoEnter( menulist_s *s )
{
	s->curvalue++;
	if ( s->itemnames[s->curvalue] == 0 )
		s->curvalue = 0;

	if ( s->generic.callback )
		s->generic.callback( s );
}
```

(The internal-linkage `static` comes from the declarations being deleted. Their only "uses" are commented-out calls near lines 496/499 — leave those comments untouched.)

- [ ] **Step 3: `linux/glob.c` — delete `glob_pattern_p` (~lines 31–58)**

Delete the comment and the function:

```c
/* Return nonzero if PATTERN has any special globbing chars in it.  */
static int glob_pattern_p(char *pattern)
{
	register char *p = pattern;
	register char c;
	int open = 0;

	while ((c = *p++) != '\0')
		switch (c) {
		case '?':
		case '*':
			return 1;

		case '[':		/* Only accept an open brace if there is a close */
			open++;		/* brace to match it.  Bracket expressions must be */
			continue;	/* complete, according to Posix.2 */
		case ']':
			if (open)
				return 1;
			continue;

		case '\\':
			if (*p++ == '\0')
				return 0;
		}

	return 0;
}
```

- [ ] **Step 4: `ref_gl/gl_rmain.c` — delete `GL_DrawStereoPattern` and its helper (~lines 878–917)**

Delete `GL_DrawColoredStereoLinePair`:

```c
static void GL_DrawColoredStereoLinePair( float r, float g, float b, float y )
{
	qglColor3f( r, g, b );
	qglVertex2f( 0, y );
	qglVertex2f( vid.width, y );
	qglColor3f( 0, 0, 0 );
	qglVertex2f( 0, y + 1 );
	qglVertex2f( vid.width, y + 1 );
}
```

and `GL_DrawStereoPattern`:

```c
static void GL_DrawStereoPattern( void )
{
	int i;

	if ( !( gl_config.renderer & GL_RENDERER_INTERGRAPH ) )
		return;

	if ( !gl_state.stereo_enabled )
		return;

	R_SetGL2D();

	qglDrawBuffer( GL_BACK_LEFT );

	for ( i = 0; i < 20; i++ )
	{
		qglBegin( GL_LINES );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 0 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 2 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 4 );
			GL_DrawColoredStereoLinePair( 1, 0, 0, 6 );
			GL_DrawColoredStereoLinePair( 0, 1, 0, 8 );
			GL_DrawColoredStereoLinePair( 1, 1, 0, 10);
			GL_DrawColoredStereoLinePair( 1, 1, 0, 12);
			GL_DrawColoredStereoLinePair( 0, 1, 0, 14);
		qglEnd();
		
		GLimp_EndFrame();
	}
}
```

(The helper's only caller is the function being deleted; both are dead.)

- [ ] **Step 5: Gate the 4a edits**

```bash
make clean && make build && make verify-load
```

Expected: exit 0. If a `-Wunused-function` warning now names another function in these files, stop and report it — do not delete it under this step.

#### Part 4b — symbol-level audit

- [ ] **Step 6: Collect the symbol universe from the fresh build**

```bash
nm -g build/quake2 | awk '$2=="T" {print $3}' | sed 's/^_//' | sort -u > /tmp/cleanup-syms-exe.txt
nm -g build/ref_gl.so | awk '$2=="T" {print $3}' | sed 's/^_//' | sort -u > /tmp/cleanup-syms-ref.txt
nm -g baseq2/gamearm64.so | awk '$2=="T" {print $3}' | sed 's/^_//' | sort -u > /tmp/cleanup-syms-game.txt
nm -u build/ref_gl.so | awk '{print $NF}' | sed 's/^_//' | sort -u > /tmp/cleanup-undef-ref.txt
nm -u baseq2/gamearm64.so | awk '{print $NF}' | sed 's/^_//' | sort -u > /tmp/cleanup-undef-game.txt
```

- [ ] **Step 7: Generate candidates**

For each symbol in the union of the three defined lists, run:

```bash
grep -rw --include='*.c' --include='*.h' SYMBOL client qcommon server game ref_gl sdl linux null
```

A symbol is a **deletion candidate** only if every match lies inside its defining file. A symbol **survives unconditionally** if:
- it is `main`, or a dlsym export: `GetRefAPI`, `RW_IN_Init`, `RW_IN_Shutdown`, `RW_IN_Activate`, `RW_IN_Commands`, `RW_IN_Move`, `RW_IN_Frame`, `KBD_Init`, `KBD_Update`, `KBD_Close`, `GetGameAPI`; or
- it appears in `/tmp/cleanup-undef-ref.txt` or `/tmp/cleanup-undef-game.txt` (the bundles resolve it against the host process at dlopen time); or
- the match outside the defining file is a header `extern` declaration paired with a real use — treat header declaration + any use anywhere as live.

Record the candidate list (symbol, defining file) before deleting anything.

- [ ] **Step 8: Vet and delete in per-file batches**

Group candidates by defining file. For each file: delete the candidate function definitions (and their static forward declarations if any), then run the gate:

```bash
make clean && make build && make verify-load
```

Expected: exit 0 per batch. If a deletion causes any failure or warning, restore that function (`git checkout -- <file>` if the batch is unrecoverable, or re-add the single function), record it as kept, and continue with the remaining batches. If a candidate is ambiguous (macro-generated callers, token-pasting, uncertain dispatch), keep it and note why.

- [ ] **Step 9: Commit 4**

Stage exactly the files modified in steps 1–4 and 8 (explicit paths only — Global Constraint 4):

```bash
git add client/menu.c client/qmenu.c linux/glob.c ref_gl/gl_rmain.c <files-from-step-8>
git commit -m "cleanup: remove unreferenced functions (unused statics + symbol audit)

Deletes the 5 proven-dead statics (NoAltTabFunc, Menulist_DoEnter,
SpinControl_DoEnter, glob_pattern_p, GL_DrawStereoPattern +
GL_DrawColoredStereoLinePair) plus audit-confirmed unreferenced
functions. Follow-up: qcommon/md4.c is a fully dead translation unit
but stays until the Makefile may be touched (spec §1)."
```

(Adjust the body if step 8 deleted additional functions — name them or give the count. If step 8 deleted nothing, say so explicitly.)

---

### Task 6: Acceptance and merge to master

**Files:** none modified (verification + git only)

**Interfaces:**
- Consumes: commits 1–4 on `cleanup/apple-silicon-only`
- Produces: master merged with `--no-ff`, final gate green on master, branch deleted, summary report

- [ ] **Step 1: Full clean gate on the branch**

```bash
make clean && make build && make verify-load
```

Expected: exit 0.

- [ ] **Step 2: Acceptance greps**

```bash
grep -rn 'WIN32\|svgalib\|wgl\|id386' client qcommon server game ref_gl sdl linux null --include='*.c' --include='*.h'
grep -rn 'idaxp\|_M_IX86\|_M_ALPHA\|__i386__\|__alpha__\|__sun__\|__sgi\|C_ONLY' client qcommon server game ref_gl sdl linux null --include='*.c' --include='*.h'
git ls-files | wc -l
git log --oneline master..HEAD | wc -l
```

Expected: both greps produce no output; file count is `480` (690 − 210); commit count is `4`. If the file count differs only because step 8 of Task 5 deleted nothing extra, 480 still holds — any other number means stop and report.

- [ ] **Step 3: Dedicated-server runtime smoke (game data is present)**

```bash
./build/quake2 +set dedicated 1 +quit > /tmp/cleanup-ded-smoke.log 2>&1
echo "exit: $?"
grep -c "Added packfile" /tmp/cleanup-ded-smoke.log
```

Expected: exit 0, and 3 `Added packfile` lines (pak0/pak1/pak2). If it hangs instead of exiting, kill it after ~15 s and report with the log tail — do not proceed to the merge.

- [ ] **Step 4: Merge to master**

```bash
git checkout master
git merge --no-ff cleanup/apple-silicon-only -m "Merge branch 'cleanup/apple-silicon-only' — Apple Silicon-only cleanup"
git branch -d cleanup/apple-silicon-only
```

- [ ] **Step 5: Final gate on master**

```bash
make clean && make build && make verify-load
```

Expected: exit 0.

- [ ] **Step 6: Verify no play data leaked into history**

```bash
git log --name-only --pretty=format: master~1..master | grep -c baseq2
git status --short
```

Expected: `0` merged files under `baseq2/`; status shows only the known pre-existing `baseq2/`/`.DS_Store` noise (still untracked/unstaged, as before Task 1).

- [ ] **Step 7: Report**

Summarize: files deleted (expected 210), conditional sites swept (42 across 17 files), functions removed in phase 4 (with the list), md4.c follow-up, gate results, and that a windowed play-test via `make run` remains the user's final visual check. Do **not** push anywhere.
