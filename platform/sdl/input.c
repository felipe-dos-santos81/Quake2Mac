// in_sdl.c -- SDL3 input for the ref_gl bundle. Replaces
// linux/rw_in_svgalib.c. Exports RW_IN_* and KBD_* which the client's
// VID layer dlsym's after loading the renderer.
//
// The mouse cursor is NEVER grabbed: no SDL_SetWindowMouseGrab and no
// relative mouse mode. Look-around uses motion deltas delivered while the
// cursor is inside the window.

#include <SDL3/SDL.h>

#include "ref_gl/local.h"
#include "client/screen/keys.h"
#include "platform/posix/rw.h"

/*****************************************************************************/
/* KEYBOARD                                                                  */
/*****************************************************************************/

static Key_Event_fp_t Key_Event_fp;

static int SDLKey_To_QuakeKey(SDL_Keycode k)
{
	switch (k)
	{
	case SDLK_ESCAPE:    return K_ESCAPE;
	case SDLK_RETURN:    return K_ENTER;
	case SDLK_TAB:       return K_TAB;
	case SDLK_SPACE:     return K_SPACE;
	case SDLK_BACKSPACE: return K_BACKSPACE;
	case SDLK_UP:        return K_UPARROW;
	case SDLK_DOWN:      return K_DOWNARROW;
	case SDLK_LEFT:      return K_LEFTARROW;
	case SDLK_RIGHT:     return K_RIGHTARROW;
	case SDLK_LALT:
	case SDLK_RALT:      return K_ALT;
	case SDLK_LCTRL:
	case SDLK_RCTRL:     return K_CTRL;
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:    return K_SHIFT;
	case SDLK_F1:        return K_F1;
	case SDLK_F2:        return K_F2;
	case SDLK_F3:        return K_F3;
	case SDLK_F4:        return K_F4;
	case SDLK_F5:        return K_F5;
	case SDLK_F6:        return K_F6;
	case SDLK_F7:        return K_F7;
	case SDLK_F8:        return K_F8;
	case SDLK_F9:        return K_F9;
	case SDLK_F10:       return K_F10;
	case SDLK_F11:       return K_F11;
	case SDLK_F12:       return K_F12;
	case SDLK_INSERT:    return K_INS;
	case SDLK_DELETE:    return K_DEL;
	case SDLK_PAGEDOWN:  return K_PGDN;
	case SDLK_PAGEUP:    return K_PGUP;
	case SDLK_HOME:      return K_HOME;
	case SDLK_END:       return K_END;
	case SDLK_PAUSE:     return K_PAUSE;
	case SDLK_CAPSLOCK:  return 0; /* ignore */
	case SDLK_KP_1:      return K_KP_END;
	case SDLK_KP_2:      return K_KP_DOWNARROW;
	case SDLK_KP_3:      return K_KP_PGDN;
	case SDLK_KP_4:      return K_KP_LEFTARROW;
	case SDLK_KP_5:      return K_KP_5;
	case SDLK_KP_6:      return K_KP_RIGHTARROW;
	case SDLK_KP_7:      return K_KP_HOME;
	case SDLK_KP_8:      return K_KP_UPARROW;
	case SDLK_KP_9:      return K_KP_PGUP;
	case SDLK_KP_0:      return K_KP_INS;
	case SDLK_KP_PERIOD: return K_KP_DEL;
	case SDLK_KP_ENTER:  return K_KP_ENTER;
	case SDLK_KP_MINUS:  return K_KP_MINUS;
	case SDLK_KP_PLUS:   return K_KP_PLUS;
	case SDLK_KP_DIVIDE: return K_KP_SLASH;
	case SDLK_GRAVE:     return '`';
	default:
		/* SDL3 keycodes for printable characters are lowercase ASCII */
		if (k >= 32 && k < 127)
			return (int)k;
		return 0; /* unmapped: ignore */
	}
}

void KBD_Init(Key_Event_fp_t fp)
{
	Key_Event_fp = fp;
}

void KBD_Close(void)
{
	Key_Event_fp = NULL;
}

/*****************************************************************************/
/* MOUSE + EVENT PUMP                                                        */
/*****************************************************************************/

static qboolean UseMouse = true;

static int   mx, my;                 /* accumulated motion since last Move */

static qboolean mlooking;

static in_state_t *in_state;

static cvar_t *m_filter;
static cvar_t *in_mouse;
static cvar_t *freelook;
static cvar_t *lookstrafe;
static cvar_t *sensitivity;
static cvar_t *m_pitch;
static cvar_t *m_yaw;
static cvar_t *m_forward;
static cvar_t *m_side;

static void Force_CenterView_f(void)
{
	in_state->viewangles[PITCH] = 0;
}

static void RW_IN_MLookDown(void)
{
	mlooking = true;
}

static void RW_IN_MLookUp(void)
{
	mlooking = false;
	in_state->IN_CenterView_fp();
}

void KBD_Update(void)
{
	SDL_Event e;

	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
		case SDL_EVENT_QUIT:
			/* route through the engine's own quit so config is saved */
			ri.Cmd_ExecuteText(EXEC_APPEND, "quit\n");
			break;

		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		{
			int key = SDLKey_To_QuakeKey(e.key.key);
			if (key && Key_Event_fp)
				Key_Event_fp(key, e.key.down);
			break;
		}

		case SDL_EVENT_MOUSE_MOTION:
			if (UseMouse)
			{
				mx += (int)e.motion.xrel;
				my += (int)e.motion.yrel;
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			/*
			 * SDL3 numbers buttons LEFT=1, MIDDLE=2, RIGHT=3; Quake's
			 * K_MOUSE* convention (see rw_in_svgalib.c) is MOUSE1=left,
			 * MOUSE2=right, MOUSE3=middle, so map explicitly.
			 */
			if (UseMouse && Key_Event_fp)
			{
				int qkey = 0;

				if (e.button.button == SDL_BUTTON_LEFT)
					qkey = K_MOUSE1;
				else if (e.button.button == SDL_BUTTON_RIGHT)
					qkey = K_MOUSE2;
				else if (e.button.button == SDL_BUTTON_MIDDLE)
					qkey = K_MOUSE3;

				if (qkey)
					Key_Event_fp(qkey, e.button.down);
			}
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			if (UseMouse && Key_Event_fp)
			{
				float y = e.wheel.y;

				/* macOS natural scrolling reports SDL_MOUSEWHEEL_FLIPPED;
				   undo it so physical wheel-up is always K_MWHEELUP */
				if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
					y = -y;

				if (y > 0)
				{
					Key_Event_fp(K_MWHEELUP, true);
					Key_Event_fp(K_MWHEELUP, false);
				}
				else if (y < 0)
				{
					Key_Event_fp(K_MWHEELDOWN, true);
					Key_Event_fp(K_MWHEELDOWN, false);
				}
			}
			break;

		default:
			break;
		}
	}
}

void RW_IN_Init(in_state_t *in_state_p)
{
	in_state = in_state_p;

	m_filter    = ri.Cvar_Get("m_filter", "0", 0);
	in_mouse    = ri.Cvar_Get("in_mouse", "1", CVAR_ARCHIVE);
	freelook    = ri.Cvar_Get("freelook", "0", 0);
	lookstrafe  = ri.Cvar_Get("lookstrafe", "0", 0);
	sensitivity = ri.Cvar_Get("sensitivity", "3", 0);
	m_pitch     = ri.Cvar_Get("m_pitch", "0.022", 0);
	m_yaw       = ri.Cvar_Get("m_yaw", "0.022", 0);
	m_forward   = ri.Cvar_Get("m_forward", "1", 0);
	m_side      = ri.Cvar_Get("m_side", "0.8", 0);

	ri.Cmd_AddCommand("+mlook", RW_IN_MLookDown);
	ri.Cmd_AddCommand("-mlook", RW_IN_MLookUp);
	ri.Cmd_AddCommand("force_centerview", Force_CenterView_f);

	UseMouse = (in_mouse->value != 0);
	ri.Con_Printf(PRINT_ALL, "SDL3 input initialized (cursor never grabbed)\n");
}

void RW_IN_Shutdown(void)
{
	ri.Cmd_RemoveCommand("+mlook");
	ri.Cmd_RemoveCommand("-mlook");
	ri.Cmd_RemoveCommand("force_centerview");
	UseMouse = false;
}

void RW_IN_Commands(void)
{
	/* button state is delivered as K_MOUSE* key events in KBD_Update */
}

/*
===========
RW_IN_Move
===========
*/
void RW_IN_Move(usercmd_t *cmd)
{
	float mouse_x, mouse_y;
	static float old_mouse_x, old_mouse_y;

	if (!UseMouse)
		return;

	if (m_filter->value)
	{
		mouse_x = (mx + old_mouse_x) * 0.5;
		mouse_y = (my + old_mouse_y) * 0.5;
	}
	else
	{
		mouse_x = mx;
		mouse_y = my;
	}
	old_mouse_x = mx;
	old_mouse_y = my;

	if (!mx && !my)
		return;

	mx = my = 0; /* clear for next frame */

	mouse_x *= sensitivity->value;
	mouse_y *= sensitivity->value;

	/* add mouse X/Y movement to cmd */
	if ((*in_state->in_strafe_state & 1) ||
		(lookstrafe->value && mlooking))
		cmd->sidemove += m_side->value * mouse_x;
	else
		in_state->viewangles[YAW] -= m_yaw->value * mouse_x;

	if ((mlooking || freelook->value) &&
		!(*in_state->in_strafe_state & 1))
	{
		in_state->viewangles[PITCH] += m_pitch->value * mouse_y;
	}
	else
	{
		cmd->forwardmove -= m_forward->value * mouse_y;
	}
}

void RW_IN_Frame(void)
{
}

void RW_IN_Activate(qboolean active)
{
	/* windowed with no grab: nothing to grab or release */
	(void)active;
}
