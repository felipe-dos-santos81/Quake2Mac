# Mouse-Operable Menus — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every menu in the game is fully operable with the mouse — hover highlights, click activates, sliders click-and-drag, right click goes back, the wheel navigates — with zero changes to keyboard navigation or the in-game walk/look scheme.

**Architecture:** Draw-time rect capture. Each `qmenu` draw function records the rectangle it just drew into new `hit_x/hit_y/hit_w/hit_h` fields on `menucommon_s`; one shared layer (`Menu_ItemAtPoint`, a per-frame mouse pass in `Menu_Draw`, and pointer cases in `Default_MenuKey`) does hover/click/drag against those rects. The main menu, credits, and quit screens are special-cased in `menu.c`; the video menu delegates its key handler to `Default_MenuKey`. The OS cursor stays — nothing is drawn or hidden.

**Tech Stack:** C (Apple clang, GNU make, id-era style: tabs, K&R braces, `/* banner */` comment blocks), SDL3 input already plumbed (`IN_CursorPos`, `K_MOUSE1/2`, `K_MWHEELUP/DOWN`).

**Spec:** `docs/superpowers/specs/2026-09-03-mouse-menus-design.md` — read it first; the plan argues from it.

## Global Constraints

1. **Build verification** (run whenever a task says "build-check"):
   `make verify-load` — must exit 0. This is incremental (header-dependency aware) and implies the build. Then confirm zero warnings: `make build 2>&1 | grep -i warning` must print nothing.
2. **Final regression gate** (Task 6 only): `make clean && make verify-load && make smoke` — every command must exit 0.
3. **No C unit-test harness exists for client code** in this repo (`ref_gl/tests/` and `tools/` have their own, not reused here). The verification cycle is the build-check above plus the manual checklist in Task 6. Do not invent a test framework.
4. **Branch:** `feature/mouse-menus` (already exists; the spec is committed on it as `82fc4f4`). Work stays on it until Task 6's merge.
5. **Never** `git add -A`, `git add .`, or `git commit -a`. Stage explicit paths only. `baseq2/` (paks, saves, configs, textures, `textures-old/`) must never enter a commit. **Never push.**
6. **Zero-warning rule:** the tree builds clean under `-Wall`; new code must not add warnings (unused variables, sign issues, missing prototypes).
7. **Include hygiene:** every `#include "..."` is repo-root-relative; the two AGENTS.md lint greps must print nothing. All files touched here already include what they need (`client/client.h` chains `client/input.h` and `client/screen/keys.h`).
8. **Do not touch:** the walk/look block in `client/screen/keys.c` (gated on `key_game`), `platform/sdl/input.c`, `platform/verify_load.c`, any protected item listed in AGENTS.md.
9. If a step's expected file content or command output does not match what the step says: **stop and report** the exact output. Do not improvise around it.

---

## File structure

| Path | Responsibility |
|------|----------------|
| `client/screen/qmenu.h` | New rect fields on `menucommon_s`; `Menu_ItemAtPoint` declaration. Task 1. |
| `client/screen/qmenu.c` | Rect recording in the five draw functions; `Menu_ItemAtPoint`; drag/hover helpers; per-frame mouse pass in `Menu_Draw`; pointer semantics in `Default_MenuKey`. Tasks 1–2. |
| `client/screen/menu.c` | Main-menu geometry helper + hover/click/back; `M_DrawCursor` removal; credits/quit right-click. Task 3. |
| `platform/posix/vid_menu.c` | `VID_MenuKey` delegates to `Default_MenuKey`. Task 4. |
| `README.md`, `AGENTS.md` | Mouse-controls documentation. Task 5. |

All changed objects belong to the executable only (`CLIENT_OBJS` / `SYS_EXE_OBJS`); neither bundle nor the game DLL links `qmenu.o` or `vid_menu.o`.

---

### Task 1: qmenu rect recording + hit test (zero behavior change)

**Files:**
- Modify: `client/screen/qmenu.h` (struct `menucommon_s`, declarations block)
- Modify: `client/screen/qmenu.c` (`Action_Draw`, `Field_Draw`, `Slider_Draw`, `SpinControl_Draw`, `MenuList_Draw`, `Separator_Draw`; new `Menu_ItemAtPoint`; move `SLIDER_RANGE` define)

**Interfaces:**
- Produces (used by Task 2):
  - `int hit_x, hit_y, hit_w, hit_h;` on `menucommon_s` — last drawn rect in drawable (viddef) pixels; separators keep `hit_w = hit_h = 0`.
  - `void *Menu_ItemAtPoint( menuframework_s *m, int x, int y )` — item whose rect contains the point, or 0.
  - `#define SLIDER_RANGE 10` moved to the top defines block (Task 2's helpers need it before `Menu_Draw`).

This task is write-only: nothing reads the rects yet, so behavior is unchanged.

- [ ] **Step 1: Add rect fields and the declaration to `client/screen/qmenu.h`**

In `menucommon_s`, after the `cursordraw` pointer, add:

```c
	void (*callback)( void *self );
	void (*statusbarfunc)( void *self );
	void (*ownerdraw)( void *self );
	void (*cursordraw)( void *self );

	int hit_x, hit_y, hit_w, hit_h;	/* last drawn rect, drawable px, for mouse hit-testing */
} menucommon_s;
```

In the declarations block, next to `Menu_ItemAtCursor`:

```c
void	*Menu_ItemAtCursor( menuframework_s *m );
void	*Menu_ItemAtPoint( menuframework_s *m, int x, int y );
qboolean Menu_SelectItem( menuframework_s *s );
```

- [ ] **Step 2: Move `SLIDER_RANGE` to the top defines in `client/screen/qmenu.c`**

Current state: `#define SLIDER_RANGE 10` sits immediately above `Slider_Draw`, mid-file. Delete it there and add it next to the column offsets at the top:

```c
#define RCOLUMN_OFFSET  16
#define LCOLUMN_OFFSET -16
#define SLIDER_RANGE	10
```

- [ ] **Step 3: Record rects in the five draw functions of `client/screen/qmenu.c`**

Append to the end of `Action_Draw` (covers both the `QMF_LEFT_JUSTIFY` and right-to-left string layouts drawn above it):

```c
	{
		int	l = ( int ) strlen( a->generic.name );
		int	bx = a->generic.x + a->generic.parent->x;
		int	by = a->generic.y + a->generic.parent->y;

		if ( a->generic.flags & QMF_LEFT_JUSTIFY )
			a->generic.hit_x = bx + LCOLUMN_OFFSET;
		else
			a->generic.hit_x = bx + LCOLUMN_OFFSET - 8 * ( l - 1 );
		a->generic.hit_y = by;
		a->generic.hit_w = 8 * l;
		a->generic.hit_h = 8;
	}
```

Append to the end of `Field_Draw` (the input box spans rows `y-4`/`y+4` from `x+16` to `x+24+visible_length*8`, exactly as the chars above draw it):

```c
	f->generic.hit_x = f->generic.x + f->generic.parent->x + 16;
	f->generic.hit_y = f->generic.y + f->generic.parent->y - 4;
	f->generic.hit_w = f->visible_length * 8 + 8;
	f->generic.hit_h = 16;
```

Append to the end of `Slider_Draw` (label row + track; the track is `(SLIDER_RANGE+2)` chars starting at `RCOLUMN_OFFSET`):

```c
	{
		int	bx = s->generic.x + s->generic.parent->x;
		int	left = bx + RCOLUMN_OFFSET;
		int	right = left + ( SLIDER_RANGE + 2 ) * 8;

		if ( s->generic.name )
		{
			int	ll = bx + LCOLUMN_OFFSET - 8 * ( ( int ) strlen( s->generic.name ) - 1 );
			if ( ll < left )
				left = ll;
		}
		s->generic.hit_x = left;
		s->generic.hit_y = s->generic.y + s->generic.parent->y;
		s->generic.hit_w = right - left;
		s->generic.hit_h = 8;
	}
```

Append to the end of `SpinControl_Draw` (label row + value; a value containing `\n` draws two rows 10 px apart):

```c
	{
		int	bx = s->generic.x + s->generic.parent->x;
		const char *v = s->itemnames[s->curvalue];
		const char *nl = strchr( v, '\n' );
		int	left = bx + RCOLUMN_OFFSET;
		int	right;

		if ( s->generic.name )
		{
			int	ll = bx + LCOLUMN_OFFSET - 8 * ( ( int ) strlen( s->generic.name ) - 1 );
			if ( ll < left )
				left = ll;
		}
		if ( nl )
		{
			int	r1 = bx + RCOLUMN_OFFSET + 8 * ( int ) ( nl - v );
			int	r2 = bx + RCOLUMN_OFFSET + 8 * ( int ) strlen( nl + 1 );
			right = r1 > r2 ? r1 : r2;
		}
		else
			right = bx + RCOLUMN_OFFSET + 8 * ( int ) strlen( v );

		s->generic.hit_x = left;
		s->generic.hit_y = s->generic.y + s->generic.parent->y;
		s->generic.hit_w = right - left;
		s->generic.hit_h = nl ? 18 : 8;
	}
```

Append to the end of `MenuList_Draw` (the column block: the fill rect starts 112 px left of `x` and rows run 10 px apart below the title row):

```c
	{
		int	n = 0;
		const char **c = l->itemnames;

		while ( *c )
			n++, c++;
		l->generic.hit_x = l->generic.x - 112 + l->generic.parent->x;
		l->generic.hit_y = l->generic.y + l->generic.parent->y;
		l->generic.hit_w = 128;
		l->generic.hit_h = 10 * ( n + 1 );
	}
```

Append to the end of `Separator_Draw` (never hit):

```c
	s->generic.hit_w = s->generic.hit_h = 0;
```

- [ ] **Step 4: Add `Menu_ItemAtPoint` to `client/screen/qmenu.c`**

Insert immediately after `Menu_ItemAtCursor`:

```c
/*
** Menu_ItemAtPoint
**
** Returns the item whose last drawn rect contains the point, or 0.
** Separators are never hit.  Rects exist after the menu's first draw;
** input cannot arrive before that.
*/
void *Menu_ItemAtPoint( menuframework_s *m, int x, int y )
{
	int	i;
	menucommon_s	*item;

	for ( i = 0; i < m->nitems; i++ )
	{
		item = ( menucommon_s * ) m->items[i];
		if ( item->type == MTYPE_SEPARATOR || item->hit_w <= 0 )
			continue;
		if ( x >= item->hit_x && x < item->hit_x + item->hit_w
			&& y >= item->hit_y && y < item->hit_y + item->hit_h )
			return item;
	}

	return 0;
}
```

- [ ] **Step 5: Build-check**

Run: `make verify-load` — expected: exit 0.
Run: `make build 2>&1 | grep -i warning` — expected: no output.

- [ ] **Step 6: Commit**

```bash
git add client/screen/qmenu.h client/screen/qmenu.c
git commit -m "qmenu: record drawn item rects for mouse hit-testing"
```

---

### Task 2: Hover, click, drag, back, wheel in the framework

**Files:**
- Modify: `client/screen/qmenu.c` (new statics + helpers, `Menu_Draw`, `Default_MenuKey`)

**Interfaces:**
- Consumes: `hit_*` fields, `Menu_ItemAtPoint` (Task 1).
- Produces (used by Tasks 3–4): full pointer behavior in `Default_MenuKey` — any menu whose key handler is/delegates to it gets mouse support automatically.

- [ ] **Step 1: Add the drag state and helpers to `client/screen/qmenu.c`**

Insert after the forward declarations at the top of the file (below the `static void …` list, above `#define RCOLUMN_OFFSET`):

```c
static menuslider_s *s_drag_slider;	/* slider being dragged, or 0 */
```

Insert after `Field_Key` (before `Menu_AddItem`) — caret placement for field clicks (the box starts 24 px right of the field origin, 8 px per char, per `Field_Draw`):

```c
/*
** Field_SetCaretFromPixel
**
** Places the text caret at the clicked column.
*/
static void Field_SetCaretFromPixel( menufield_s *f, int px )
{
	int	base = f->generic.x + f->generic.parent->x + 24;
	int	col;

	col = ( px - base ) / 8;
	if ( col < 0 )
		col = 0;
	if ( col > f->visible_length )
		col = f->visible_length;

	f->cursor = f->visible_offset + col;
	if ( f->cursor > ( int ) strlen( f->buffer ) )
		f->cursor = ( int ) strlen( f->buffer );
	if ( f->cursor > f->length )
		f->cursor = f->length;
}
```

Insert after `Slider_Draw` — value-from-pointer using the same geometry `Slider_Draw` uses for the knob (starts 8 px right of the track's left cap, travels `(SLIDER_RANGE-1)*8` px):

```c
/*
** Slider_SetFromPixel
**
** Maps a pointer x inside the slider track to a value, clamped, and
** fires the callback exactly once per changed step — the drag can
** call this every frame.
*/
static void Slider_SetFromPixel( menuslider_s *s, int px )
{
	int	base;
	int	travel;
	float	range;
	int	value;

	base = s->generic.x + s->generic.parent->x + RCOLUMN_OFFSET + 8;
	travel = ( SLIDER_RANGE - 1 ) * 8;

	range = ( px - base ) / ( float ) travel;
	if ( range < 0 )
		range = 0;
	if ( range > 1 )
		range = 1;

	value = ( int ) ( s->minvalue + range * ( s->maxvalue - s->minvalue ) + 0.5f );
	if ( value == ( int ) s->curvalue )
		return;
	s->curvalue = value;

	if ( s->generic.callback )
		s->generic.callback( s );
}
```

Insert after `SpinControl_Draw` — row selection for list clicks (rows start 10 px below the item origin, 10 px apart, per `MenuList_Draw`):

```c
/*
** MenuList_SelectFromPixel
**
** Sets curvalue to the row under the pointer, clamped.
*/
static void MenuList_SelectFromPixel( menulist_s *l, int py )
{
	int	n = 0;
	int	row;
	const char **c = l->itemnames;

	while ( *c )
		n++, c++;

	row = ( py - ( l->generic.y + l->generic.parent->y + 10 ) ) / 10;
	if ( row < 0 )
		row = 0;
	if ( row >= n )
		row = n - 1;
	l->curvalue = row;
}
```

Insert before `Menu_Draw` — the per-frame pass:

```c
/*
** Menu_MousePass
**
** Per-frame mouse handling for framework menus.  While a slider drag
** is live the pointer owns the value; otherwise the menu cursor moves
** silently to the item under the pointer.  Mouse and keyboard share
** the one cursor index, so arrow keys keep working from wherever the
** pointer left the selection.
*/
static void Menu_MousePass( menuframework_s *m )
{
	extern int keydown[];
	menucommon_s	*item;
	int	x, y, i;

	IN_CursorPos( &x, &y );

	if ( s_drag_slider )
	{
		if ( !keydown[K_MOUSE1] || s_drag_slider->generic.parent != m )
			s_drag_slider = 0;
		else
		{
			Slider_SetFromPixel( s_drag_slider, x );
			return;
		}
	}

	item = ( menucommon_s * ) Menu_ItemAtPoint( m, x, y );
	if ( !item || item == Menu_ItemAtCursor( m ) )
		return;

	for ( i = 0; i < m->nitems; i++ )
	{
		if ( m->items[i] == item )
		{
			m->cursor = i;
			break;
		}
	}
}
```

(`keydown` is `extern int keydown[]` — the same pattern `Field_Key` in this file already uses. `IN_CursorPos` and `K_MOUSE1` come through `client/client.h`.)

- [ ] **Step 2: Call the pass from `Menu_Draw`**

In `Menu_Draw`, immediately before `item = Menu_ItemAtCursor( menu );` (after the item-draw switch), insert:

```c
	/* let the pointer move the selection before the highlight draws */
	Menu_MousePass( menu );

	item = Menu_ItemAtCursor( menu );
```

- [ ] **Step 3: Rewire the mouse cases in `Default_MenuKey`**

The current switch contains this block — mouse buttons falling through to the Enter path:

```c
	case K_MOUSE1:
	case K_MOUSE2:
	case K_MOUSE3:
```

Replace the region from `case K_MOUSE1:` down to (but not including) `case K_MOUSE3:` with:

```c
	case K_MOUSE1:
		if ( m )
		{
			menucommon_s	*hit;
			int	x, y, i;

			IN_CursorPos( &x, &y );
			hit = ( menucommon_s * ) Menu_ItemAtPoint( m, x, y );
			if ( hit )
			{
				/* move the selection to the clicked item */
				for ( i = 0; i < m->nitems; i++ )
					if ( m->items[i] == hit )
					{
						m->cursor = i;
						break;
					}

				switch ( hit->type )
				{
				case MTYPE_SLIDER:
					s_drag_slider = ( menuslider_s * ) hit;
					Slider_SetFromPixel( ( menuslider_s * ) hit, x );
					break;
				case MTYPE_FIELD:
					Field_SetCaretFromPixel( ( menufield_s * ) hit, x );
					break;
				case MTYPE_SPINCONTROL:
					SpinControl_DoSlide( ( menulist_s * ) hit, 1 );
					break;
				case MTYPE_LIST:
					MenuList_SelectFromPixel( ( menulist_s * ) hit, y );
					break;
				case MTYPE_ACTION:
					Action_DoEnter( ( menuaction_s * ) hit );
					break;
				}
				sound = menu_move_sound;
			}
		}
		break;

	case K_MOUSE2:
		M_PopMenu();
		return menu_out_sound;

	case K_MWHEELUP:
	case K_MWHEELDOWN:
		if ( m )
		{
			menucommon_s	*cur = ( menucommon_s * ) Menu_ItemAtCursor( m );

			if ( cur && ( cur->type == MTYPE_SLIDER || cur->type == MTYPE_SPINCONTROL ) )
				Menu_SlideItem( m, key == K_MWHEELUP ? 1 : -1 );
			else if ( key == K_MWHEELUP )
			{
				m->cursor--;
				Menu_AdjustCursor( m, -1 );
			}
			else
			{
				m->cursor++;
				Menu_AdjustCursor( m, 1 );
			}
			sound = menu_move_sound;
		}
		break;

	case K_MOUSE3:
```

Notes for the implementer: `SpinControl_DoSlide`, `Action_DoEnter`, and `M_PopMenu` are already reachable from this file (first two are static-forward-declared at the top; `M_PopMenu` is used by the `K_ESCAPE` case above). `K_MOUSE3`, the joystick/AUX cases, and `K_KP_ENTER`/`K_ENTER` keep their existing fall-through behavior unchanged. Clicks on empty areas now do nothing — the old "any mouse button acts as Enter on the keyboard cursor" behavior is gone by design.

- [ ] **Step 4: Build-check**

Run: `make verify-load` — expected: exit 0.
Run: `make build 2>&1 | grep -i warning` — expected: no output.

- [ ] **Step 5: Commit**

```bash
git add client/screen/qmenu.c
git commit -m "client: menu hover, click, slider drag, right-click back, wheel"
```

---

### Task 3: Main menu, credits, quit (`client/screen/menu.c`)

**Files:**
- Modify: `client/screen/menu.c` (MAIN MENU section; `M_Credits_Key`; `M_Quit_Key`; remove `NUM_CURSOR_FRAMES` and `M_DrawCursor`)

**Interfaces:**
- Consumes: `IN_CursorPos` (via `client/client.h`), `K_MOUSE1/2`, `M_PopMenu`, `menu_out_sound` (all already in scope in this file).
- Produces: nothing for later tasks.

- [ ] **Step 1: Remove the decorative cursor**

Delete `#define NUM_CURSOR_FRAMES 15` (near the top of the file, after `static int m_main_cursor;`), and delete the entire `M_DrawCursor` function including its banner comment (the one that caches and draws `m_cursor%d` pics). Verify no other reference remains:

Run: `grep -rn 'M_DrawCursor\|NUM_CURSOR_FRAMES' client/ platform/` — expected: no output.

- [ ] **Step 2: Rewrite the MAIN MENU section**

Replace everything between the `MAIN MENU` banner comments — from `#define	MAIN_ITEMS	5` through the end of `M_Main_Key` — with:

```c
#define	MAIN_ITEMS	5

static char *s_main_names[MAIN_ITEMS] =
{
	"m_main_game",
	"m_main_multiplayer",
	"m_main_options",
	"m_main_video",
	"m_main_quit"
};

/*
================
M_Main_Geometry

Layout shared by the draw and the mouse hit-testing: the five entry
pics stacked 40 px apart, left edge at xoffset.
================
*/
static void M_Main_Geometry( int *xoffset, int *ystart, int *widest )
{
	int	i, w, h;

	*widest = -1;
	for ( i = 0; i < MAIN_ITEMS; i++ )
	{
		re.DrawGetPicSize( &w, &h, s_main_names[i] );
		if ( w > *widest )
			*widest = w;
	}

	*ystart = ( viddef.height / 2 - 110 );
	*xoffset = ( viddef.width - *widest + 70 ) / 2;
}

/*
================
M_Main_RowAtPoint

Returns the entry row under the pointer, or -1.  Each row is a 40 px
tall strip starting where its pic is drawn.
================
*/
static int M_Main_RowAtPoint( int x, int y )
{
	int	i;
	int	xoffset, ystart, widest;

	M_Main_Geometry( &xoffset, &ystart, &widest );
	if ( x < xoffset || x >= xoffset + widest )
		return -1;

	for ( i = 0; i < MAIN_ITEMS; i++ )
		if ( y >= ystart + i * 40 + 13 && y < ystart + i * 40 + 53 )
			return i;

	return -1;
}

/*
================
M_Main_Activate
================
*/
static void M_Main_Activate( int row )
{
	m_entersound = true;

	switch ( row )
	{
	case 0:
		M_Menu_Game_f ();
		break;

	case 1:
		M_Menu_Multiplayer_f();
		break;

	case 2:
		M_Menu_Options_f ();
		break;

	case 3:
		M_Menu_Video_f ();
		break;

	case 4:
		M_Menu_Quit_f ();
		break;
	}
}

void M_Main_Draw (void)
{
	int i;
	int w, h;
	int ystart;
	int	xoffset;
	int widest;
	int	x, y, row;
	char litname[80];

	/* the pointer moves the selection; the _sel pic is the highlight */
	IN_CursorPos( &x, &y );
	row = M_Main_RowAtPoint( x, y );
	if ( row >= 0 )
		m_main_cursor = row;

	M_Main_Geometry( &xoffset, &ystart, &widest );

	for ( i = 0; i < MAIN_ITEMS; i++ )
	{
		if ( i != m_main_cursor )
			re.DrawPic( xoffset, ystart + i * 40 + 13, s_main_names[i] );
	}
	strcpy( litname, s_main_names[m_main_cursor] );
	strcat( litname, "_sel" );
	re.DrawPic( xoffset, ystart + m_main_cursor * 40 + 13, litname );

	re.DrawGetPicSize( &w, &h, "m_main_plaque" );
	re.DrawPic( xoffset - 30 - w, ystart, "m_main_plaque" );

	re.DrawPic( xoffset - 30 - w, ystart + h + 5, "m_main_logo" );
}


const char *M_Main_Key (int key)
{
	const char *sound = menu_move_sound;

	switch (key)
	{
	case K_ESCAPE:
		M_PopMenu ();
		break;

	case K_KP_DOWNARROW:
	case K_DOWNARROW:
		if (++m_main_cursor >= MAIN_ITEMS)
			m_main_cursor = 0;
		return sound;

	case K_KP_UPARROW:
	case K_UPARROW:
		if (--m_main_cursor < 0)
			m_main_cursor = MAIN_ITEMS - 1;
		return sound;

	case K_MOUSE1:
	{
		int	x, y, row;

		IN_CursorPos( &x, &y );
		row = M_Main_RowAtPoint( x, y );
		if ( row < 0 )
			break;
		m_main_cursor = row;
		M_Main_Activate( row );
		break;
	}

	case K_MOUSE2:
		M_PopMenu ();
		return menu_out_sound;

	case K_KP_ENTER:
	case K_ENTER:
		M_Main_Activate( m_main_cursor );
		break;
	}

	return NULL;
}
```

`M_Menu_Main_f` below this region is unchanged.

- [ ] **Step 3: Credits — right click backs out with the same cleanup as Escape**

In `M_Credits_Key`, change the single case to:

```c
	switch (key)
	{
	case K_ESCAPE:
	case K_MOUSE2:
		if (creditsBuffer)
			FS_FreeFile (creditsBuffer);
		M_PopMenu ();
		break;
	}
```

- [ ] **Step 4: Quit — right click cancels**

In `M_Quit_Key`, add `K_MOUSE2` to the cancel group (the quit pic's text is baked in, so there are deliberately no clickable areas):

```c
	switch (key)
	{
	case K_ESCAPE:
	case 'n':
	case 'N':
	case K_MOUSE2:
		M_PopMenu ();
		break;
```

- [ ] **Step 5: Build-check**

Run: `make verify-load` — expected: exit 0.
Run: `make build 2>&1 | grep -i warning` — expected: no output.

- [ ] **Step 6: Commit**

```bash
git add client/screen/menu.c
git commit -m "client: main menu mouse support; drop decorative cursor; credits/quit right-click back"
```

---

### Task 4: Video menu delegation (`platform/posix/vid_menu.c`)

**Files:**
- Modify: `platform/posix/vid_menu.c` (`VID_MenuKey` only)

**Interfaces:**
- Consumes: `Default_MenuKey` from `client/screen/menu.c` (no header declares it; use a function-local `extern`, the same pattern the current code uses for `M_PopMenu`).

`VID_MenuKey`'s current switch is a strict subset of `Default_MenuKey` (same Escape/arrows/Enter). Delegating gains KP-arrow/TAB parity plus all mouse and wheel behavior. Behavior delta (accepted in the spec): Escape now plays `menu_out_sound`.

- [ ] **Step 1: Replace the `VID_MenuKey` body**

Replace the whole function (switch included) with:

```c
/*
================
VID_MenuKey
================
*/
const char *VID_MenuKey( int key )
{
	extern const char *Default_MenuKey( menuframework_s *m, int key );

	return Default_MenuKey( s_current_menu, key );
}
```

(`s_current_menu` is set by `VID_MenuDraw`, which always runs before a key can arrive — unchanged invariant.)

- [ ] **Step 2: Build-check**

Run: `make verify-load` — expected: exit 0.
Run: `make build 2>&1 | grep -i warning` — expected: no output.

- [ ] **Step 3: Commit**

```bash
git add platform/posix/vid_menu.c
git commit -m "vid_menu: delegate VID_MenuKey to Default_MenuKey for mouse support"
```

---

### Task 5: Documentation

**Files:**
- Modify: `README.md` ("Mouse controls" section)
- Modify: `AGENTS.md` (Architecture Notes bullets)

- [ ] **Step 1: README**

In the "Mouse controls" section, after the sentence ending "…the wheel switches weapons.", insert:

```markdown
In the menus the mouse takes over: hovering highlights, clicking
activates (or starts a slider drag — hold and move to set the value),
right click goes back like Escape, and the wheel moves the selection
or, over a slider or spinner, adjusts its value. The OS cursor is the
pointer everywhere; nothing is drawn or hidden.
```

- [ ] **Step 2: AGENTS.md**

After the **Walk/look mouse scheme** bullet in "Architecture Notes (load-bearing)", add:

```markdown
- **Mouse-operable menus (2026-09-03):** every menu supports hover, click, slider drag, right-click-back, and the wheel. Draw-time rect capture: each `qmenu` draw function records `hit_x/hit_y/hit_w/hit_h` on its `menucommon_s` (same pattern as `SCR_DrawWalkButton`), `Menu_Draw` runs a per-frame mouse pass, and `Default_MenuKey` owns the pointer cases — `VID_MenuKey` delegates to it; the main menu, credits, and quit are special-cased in `menu.c`. Hover is silent; mouse and keyboard share the one cursor index. The main menu's decorative `M_DrawCursor` hand was removed with keyboard-only selection; the OS cursor is the only pointer.
```

- [ ] **Step 3: Commit**

```bash
git add README.md AGENTS.md
git commit -m "docs: mouse-operable menus"
```

---

### Task 6: Full gate, manual checklist, merge

**Files:** none (verification + merge)

- [ ] **Step 1: Full clean regression gate**

Run: `make clean && make verify-load` — expected: exit 0, zero warnings.
Run: `make smoke` — expected: exit 0; output contains `SpawnServer: base1`, `client_connect`, `maps/base1.bsp`, no `FATAL`/`ShutdownError`.

- [ ] **Step 2: Include-hygiene lint (both must print nothing)**

```bash
grep -rn '#include "' --include='*.[ch]' client server qcommon game ref_gl platform | grep -v '#include ".*/'
grep -rn '#include "\.\./' --include='*.[ch]' client server qcommon game ref_gl platform
```

- [ ] **Step 3: Manual interactive checklist (`make run`)**

Report this checklist to the user for their hands-on pass (or run through it with them):

1. Main menu: pointer over each of the 5 rows lights its `_sel` pic; keyboard arrows still move the selection.
2. Click **Options** → options menu opens; hovering tracks; clicking an action fires it.
3. Drag the screen-size slider: value follows the pointer, release stops it; keyboard left/right still works.
4. Click a spinner (e.g. crosshair): advances one step; wheel over it adjusts both ways; wheel elsewhere moves the selection.
5. Open Join server → click into the address field: caret moves to the clicked column; type.
6. Right click backs out one level at a time until the game is back.
7. Video menu: hover/click/wheel work; slider drag works; Escape/right click exits.
8. Main menu → Quit: a click does nothing (no accidental quit); right click or `n` cancels.
9. In-game: walk/look scheme unchanged — right click toggles walk mode, HUD button works, left click shoots/exits walk, double-click jumps.

- [ ] **Step 4: Merge to main (no push)**

```bash
git checkout main
git merge --no-ff feature/mouse-menus
make clean && make verify-load && make smoke
```

Expected: merge commit created; gate passes on `main`; `git status` clean except untracked user data in `baseq2/`.
