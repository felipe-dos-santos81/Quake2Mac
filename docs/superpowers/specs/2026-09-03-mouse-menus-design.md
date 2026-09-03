# Mouse-Operable Menus — 2026-09-03

Goal: every menu in the game is fully operable with the mouse — hover
highlights, click activates, sliders drag, right click goes back, the
wheel navigates — with zero changes to keyboard navigation, the in-game
walk/look scheme, or key bindings.

Decisions locked in brainstorming (2026-09-03):

- **Scope:** all menus (main + every sub-menu), not just the title screen.
- **Depth:** full mouse operation — hover + click + slider/spinner
  click-and-drag + right-click-back + wheel.
- **Pointer:** the OS cursor as-is. It is never grabbed in this port
  (it is the same pointer used for walk-mode steering), so no drawn
  cursor and no hide/show management. The main menu's decorative
  animated hand is removed with keyboard-only selection.
- **Approach:** draw-time rect capture — each draw function records the
  rectangle it just drew, and one shared layer does hover/click/drag
  against those rects. This mirrors the existing `SCR_DrawWalkButton`
  pattern ("the rect is kept for click hit-testing").

## Existing plumbing (no new infrastructure needed)

- `IN_CursorPos()` (`client/input.h`, implemented via `RW_IN_CursorPos`
  in the ref bundle) already reports the pointer in window drawable
  pixels — the same space menus are drawn in (`viddef` pixels; qmenu
  items render at native pixel size, centered via `Menu_Center` and
  `menu->x/y`).
- Mouse buttons already reach the menu key handler when
  `key_dest == key_menu`: the walk/look block in `Key_Event`
  (`client/screen/keys.c`) is gated on `key_game`, and the final
  dispatch sends down events to `M_Keydown`. `Default_MenuKey` even
  has `K_MOUSE1/2/3` cases — today they act as Enter on whatever the
  *keyboard* cursor is over. Wheel events arrive as `K_MWHEELUP/DOWN`.
- `keydown[]` is maintained for mouse buttons in menu mode too, so a
  drag's release can be detected per-frame without routing key-up
  events into `M_Keydown` (which only ever sees downs).

## Menu inventory

| Menu | Draw/key | Framework |
|---|---|---|
| Main | `M_Main_Draw` / `M_Main_Key` | special (5 pics, `m_main_cursor`) |
| Multiplayer, Keys, Options, Game, Load, Save, Join server, Start server, DM options, Download options, Address book, Player config | `*_MenuDraw` / `*_MenuKey` | qmenu; keys delegate to `Default_MenuKey` |
| Video | `VID_MenuDraw` / `VID_MenuKey` (`platform/posix/vid_menu.c`) | qmenu, own key switch |
| Credits | `M_Credits_MenuDraw` / `M_Credits_Key` | special (scrolling text) |
| Quit | `M_Quit_Draw` / `M_Quit_Key` | special (single baked pic) |

## Design

### 1. qmenu framework (`client/screen/qmenu.h`, `client/screen/qmenu.c`)

**Rect recording.** `menucommon_s` gains `int hit_x, hit_y, hit_w, hit_h;`
(last drawn rect, drawable pixels). Each draw function records its rect
in the same coordinates it draws in:

| Type | Rect |
|---|---|
| `MTYPE_ACTION` | the drawn name string: 8 px/char, 8 px tall; R2L strings extend leftward from their anchor |
| `MTYPE_FIELD` | the input box (chars at rows `y-4`/`y+4`, from `x+16` to `x+24+visible_length*8`) |
| `MTYPE_SLIDER` | label + track; the track starts at `x + RCOLUMN_OFFSET`, is `(SLIDER_RANGE+2)*8` px wide, and the knob travels `(SLIDER_RANGE-1)*8` px from `x + RCOLUMN_OFFSET + 8` (exactly `Slider_Draw`'s formula) |
| `MTYPE_SPINCONTROL` | label + value text (value may be two rows when it contains `\n` — cover both) |
| `MTYPE_LIST` | the column block: rows at `y + 10*(i+1)`, 10 px each |
| `MTYPE_SEPARATOR` | none (`hit_w = hit_h = 0`); never hit-testable |

**Hit test.** New `void *Menu_ItemAtPoint(menuframework_s *m, int x, int y)`
— first item whose recorded rect contains the point (separators excluded).
Rects exist after the menu's first draw; input cannot precede that.

**Per-frame mouse pass.** `Menu_Draw` runs it after drawing the items:

- Pointer over an item → `m->cursor` moves to that item's index.
  Hover is silent (no move sound); separators are skipped; pointer over
  nothing leaves the cursor where it is. Mouse and keyboard share the
  one cursor index, so arrow keys keep working from wherever the
  pointer left the selection.
- Slider drag: a file-static `drag slider` pointer is set when a press
  starts a drag (below). While it is set and `keydown[K_MOUSE1]` holds,
  the slider's value tracks the pointer's x within the track geometry
  above (clamp to `[minvalue, maxvalue]`, fire the callback — same as
  `Slider_DoSlide`). `keydown[K_MOUSE1]` released ends the drag.

**`Default_MenuKey` pointer semantics** (replacing today's
"any mouse button = Enter on keyboard cursor"):

- `K_MOUSE1` down: item at pointer; none → nothing (no sound). Slider
  → start drag and set the value at that spot immediately. Field →
  move cursor to it and place the text caret at the clicked column
  (8 px/char from the box's left edge, clamped, accounting for
  `visible_offset`). Otherwise → move cursor to the item and activate
  it: ACTION fires its callback (via `Menu_SelectItem`); LIST sets
  `curvalue` to the clicked row; SPINCONTROL advances one step, i.e.
  behaves like one `K_RIGHTARROW`.
- `K_MOUSE2` down: `M_PopMenu()` — back, same as Escape.
- `K_MWHEELUP/DOWN`: move the selection up/down like the arrow keys —
  except when the selection is on a spinner or slider, the wheel
  adjusts its value instead.
- Sounds: existing ones only (`menu_move_sound` on discrete changes;
  hover silent). Activation on button *down*, matching the routed
  events.

### 2. Main menu (`menu.c`)

The geometry is already computed in `M_Main_Draw` (`xoffset`,
`ystart`, five rows at 40 px spacing); factor it into a helper shared
with input. Row *i* is a 40-px-tall strip starting at
`ystart + i*40 + 13`, full `widest` width:

- Hover sets `m_main_cursor` (the existing `_sel` pic is the highlight).
- `K_MOUSE1` down inside row *i* → activate exactly like Enter on that
  row (the existing switch).
- `K_MOUSE2` down → `M_PopMenu()`.
- Remove the `M_DrawCursor` call and, now provably dead, the function,
  `NUM_CURSOR_FRAMES`, and its pic caching.

### 3. Video menu (`platform/posix/vid_menu.c`)

`VID_MenuKey` is a strict subset of `Default_MenuKey` (same Escape,
arrow, Enter semantics; fewer KP variants). Replace its switch with a
delegation to `Default_MenuKey(s_current_menu, key)` — the video menu
then inherits KP-arrow/TAB parity plus all mouse and wheel behavior for
free. The `DriverCallback`/banner logic is untouched.

### 4. Credits and quit (`menu.c`)

- Credits: no selectable items; `K_MOUSE2` pops (same as Escape);
  `K_MOUSE1` does nothing.
- Quit: the `quit` pic has its text baked in, so no hit areas —
  keyboard `y`/`n` stays as the only confirmation path; `K_MOUSE2`
  cancels (same as Escape). A stray click can never quit the game.

### 5. Unchanged (invariants)

- All keyboard navigation paths and sounds.
- The walk/look mouse scheme: its `Key_Event` block stays gated on
  `key_dest == key_game && cls.state == ca_active`; menu clicks cannot
  touch it, and the HUD walk button is not drawn in menus.
- Attract loop (any key there becomes `K_ESCAPE` before menu routing).
- Key bindings: `MOUSE1 "+attack"` still never fires in menus (the
  binding dispatch skips non-`menubound` keys in `key_menu`).
- The `key_waiting` modal prompt swallows all keys exactly as today.

## Files touched

| File | Change |
|---|---|
| `client/screen/qmenu.h` | rect fields on `menucommon_s`; `Menu_ItemAtPoint` declaration |
| `client/screen/qmenu.c` | rect recording in the 5 draw functions; hit test; per-frame mouse pass in `Menu_Draw`; pointer semantics in `Default_MenuKey` |
| `client/screen/menu.c` | main menu hover/click/back + geometry helper; `M_DrawCursor` removal; credits/quit right-click |
| `platform/posix/vid_menu.c` | `VID_MenuKey` delegates to `Default_MenuKey` |
| `README.md`, `AGENTS.md` | mouse-controls sections |

Single link unit (the executable); `qmenu` and `vid_menu.c` are not
linked into either bundle.

## Verification

- **Gate:** `make clean && make verify-load` (zero warnings) and
  `make smoke`, per the regression gate in `AGENTS.md`.
- **Include hygiene:** the two lint greps print nothing (root-relative
  includes only — `client/input.h` if `qmenu.c` needs it).
- **Manual pass (`make run`), scripted checklist:** hover each main-menu
  row highlights it; click enters; options menu: hover tracks, click
  fires actions, drag a slider and watch the value + callback (screen
  size is visible), spinner click advances, wheel over spinner adjusts,
  wheel elsewhere moves the selection; field click moves the caret;
  right click backs out at every level including video; quit dialog:
  click does nothing, right click cancels; keyboard arrows still work in
  every menu after mouse use.
- No host-test harness exists for client menu code; the manual pass is
  the interactive verification.

## Git

Branch `feature/mouse-menus`; commits per area — (1) qmenu rect capture,
hit test, mouse pass and `Default_MenuKey` semantics; (2) main menu,
credits, quit; (3) video menu delegation; (4) docs. Gate on the branch,
then `--no-ff` merge to `main`, push only when asked.
