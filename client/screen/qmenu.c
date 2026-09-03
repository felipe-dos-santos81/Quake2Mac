/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include <string.h>
#include <ctype.h>

#include "client/client.h"
#include "client/screen/qmenu.h"

static void	 Action_DoEnter( menuaction_s *a );
static void	 Action_Draw( menuaction_s *a );
static void  Menu_DrawStatusBar( const char *string );
static void	 MenuList_Draw( menulist_s *l );
static void	 MenuList_SelectFromPixel( menulist_s *l, int py );
static void	 Separator_Draw( menuseparator_s *s );
static void	 Slider_DoSlide( menuslider_s *s, int dir );
static void	 Slider_Draw( menuslider_s *s );
static void	 Slider_SetFromPixel( menuslider_s *s, int px );
static void	 SpinControl_Draw( menulist_s *s );
static void	 SpinControl_DoSlide( menulist_s *s, int dir );

static menuslider_s *s_drag_slider;	/* slider being dragged, or 0 */

#define RCOLUMN_OFFSET  16
#define LCOLUMN_OFFSET -16
#define SLIDER_RANGE	10

extern refexport_t re;
extern viddef_t viddef;

#define VID_WIDTH viddef.width
#define VID_HEIGHT viddef.height

#define Draw_Char re.DrawChar
#define Draw_Fill re.DrawFill

void Action_DoEnter( menuaction_s *a )
{
	if ( a->generic.callback )
		a->generic.callback( a );
}

void Action_Draw( menuaction_s *a )
{
	if ( a->generic.flags & QMF_LEFT_JUSTIFY )
	{
		if ( a->generic.flags & QMF_GRAYED )
			Menu_DrawStringDark( a->generic.x + a->generic.parent->x + LCOLUMN_OFFSET, a->generic.y + a->generic.parent->y, a->generic.name );
		else
			Menu_DrawString( a->generic.x + a->generic.parent->x + LCOLUMN_OFFSET, a->generic.y + a->generic.parent->y, a->generic.name );
	}
	else
	{
		if ( a->generic.flags & QMF_GRAYED )
			Menu_DrawStringR2LDark( a->generic.x + a->generic.parent->x + LCOLUMN_OFFSET, a->generic.y + a->generic.parent->y, a->generic.name );
		else
			Menu_DrawStringR2L( a->generic.x + a->generic.parent->x + LCOLUMN_OFFSET, a->generic.y + a->generic.parent->y, a->generic.name );
	}
	if ( a->generic.ownerdraw )
		a->generic.ownerdraw( a );

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
}

qboolean Field_DoEnter( menufield_s *f )
{
	if ( f->generic.callback )
	{
		f->generic.callback( f );
		return true;
	}
	return false;
}

void Field_Draw( menufield_s *f )
{
	int i;
	char tempbuffer[128]="";

	if ( f->generic.name )
		Menu_DrawStringR2LDark( f->generic.x + f->generic.parent->x + LCOLUMN_OFFSET, f->generic.y + f->generic.parent->y, f->generic.name );

	strncpy( tempbuffer, f->buffer + f->visible_offset, f->visible_length );

	Draw_Char( f->generic.x + f->generic.parent->x + 16, f->generic.y + f->generic.parent->y - 4, 18 );
	Draw_Char( f->generic.x + f->generic.parent->x + 16, f->generic.y + f->generic.parent->y + 4, 24 );

	Draw_Char( f->generic.x + f->generic.parent->x + 24 + f->visible_length * 8, f->generic.y + f->generic.parent->y - 4, 20 );
	Draw_Char( f->generic.x + f->generic.parent->x + 24 + f->visible_length * 8, f->generic.y + f->generic.parent->y + 4, 26 );

	for ( i = 0; i < f->visible_length; i++ )
	{
		Draw_Char( f->generic.x + f->generic.parent->x + 24 + i * 8, f->generic.y + f->generic.parent->y - 4, 19 );
		Draw_Char( f->generic.x + f->generic.parent->x + 24 + i * 8, f->generic.y + f->generic.parent->y + 4, 25 );
	}

	Menu_DrawString( f->generic.x + f->generic.parent->x + 24, f->generic.y + f->generic.parent->y, tempbuffer );

	if ( Menu_ItemAtCursor( f->generic.parent ) == f )
	{
		int offset;

		if ( f->visible_offset )
			offset = f->visible_length;
		else
			offset = f->cursor;

		if ( ( ( int ) ( Sys_Milliseconds() / 250 ) ) & 1 )
		{
			Draw_Char( f->generic.x + f->generic.parent->x + ( offset + 2 ) * 8 + 8,
					   f->generic.y + f->generic.parent->y,
					   11 );
		}
		else
		{
			Draw_Char( f->generic.x + f->generic.parent->x + ( offset + 2 ) * 8 + 8,
					   f->generic.y + f->generic.parent->y,
					   ' ' );
		}
	}

	f->generic.hit_x = f->generic.x + f->generic.parent->x + 16;
	f->generic.hit_y = f->generic.y + f->generic.parent->y - 4;
	f->generic.hit_w = f->visible_length * 8 + 8;
	f->generic.hit_h = 16;
}

qboolean Field_Key( menufield_s *f, int key )
{
	extern int keydown[];

	switch ( key )
	{
	case K_KP_SLASH:
		key = '/';
		break;
	case K_KP_MINUS:
		key = '-';
		break;
	case K_KP_PLUS:
		key = '+';
		break;
	case K_KP_HOME:
		key = '7';
		break;
	case K_KP_UPARROW:
		key = '8';
		break;
	case K_KP_PGUP:
		key = '9';
		break;
	case K_KP_LEFTARROW:
		key = '4';
		break;
	case K_KP_5:
		key = '5';
		break;
	case K_KP_RIGHTARROW:
		key = '6';
		break;
	case K_KP_END:
		key = '1';
		break;
	case K_KP_DOWNARROW:
		key = '2';
		break;
	case K_KP_PGDN:
		key = '3';
		break;
	case K_KP_INS:
		key = '0';
		break;
	case K_KP_DEL:
		key = '.';
		break;
	}

	if ( key > 127 )
	{
		switch ( key )
		{
		case K_DEL:
		default:
			return false;
		}
	}

	/*
	** support pasting from the clipboard
	*/
	if ( ( toupper( key ) == 'V' && keydown[K_CTRL] ) ||
		 ( ( ( key == K_INS ) || ( key == K_KP_INS ) ) && keydown[K_SHIFT] ) )
	{
		char *cbd;
		
		if ( ( cbd = Sys_GetClipboardData() ) != 0 )
		{
			strtok( cbd, "\n\r\b" );

			strncpy( f->buffer, cbd, f->length - 1 );
			f->cursor = strlen( f->buffer );
			f->visible_offset = f->cursor - f->visible_length;
			if ( f->visible_offset < 0 )
				f->visible_offset = 0;

			free( cbd );
		}
		return true;
	}

	switch ( key )
	{
	case K_KP_LEFTARROW:
	case K_LEFTARROW:
	case K_BACKSPACE:
		if ( f->cursor > 0 )
		{
			memmove( &f->buffer[f->cursor-1], &f->buffer[f->cursor], strlen( &f->buffer[f->cursor] ) + 1 );
			f->cursor--;

			if ( f->visible_offset )
			{
				f->visible_offset--;
			}
		}
		break;

	case K_KP_DEL:
	case K_DEL:
		memmove( &f->buffer[f->cursor], &f->buffer[f->cursor+1], strlen( &f->buffer[f->cursor+1] ) + 1 );
		break;

	case K_KP_ENTER:
	case K_ENTER:
	case K_ESCAPE:
	case K_TAB:
		return false;

	case K_SPACE:
	default:
		if ( !isdigit( key ) && ( f->generic.flags & QMF_NUMBERSONLY ) )
			return false;

		if ( f->cursor < f->length )
		{
			f->buffer[f->cursor++] = key;
			f->buffer[f->cursor] = 0;

			if ( f->cursor > f->visible_length )
			{
				f->visible_offset++;
			}
		}
	}

	return true;
}

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

void Menu_AddItem( menuframework_s *menu, void *item )
{
	if ( menu->nitems == 0 )
		menu->nslots = 0;

	if ( menu->nitems < MAXMENUITEMS )
	{
		menu->items[menu->nitems] = item;
		( ( menucommon_s * ) menu->items[menu->nitems] )->parent = menu;
		menu->nitems++;
	}

	menu->nslots = Menu_TallySlots( menu );
}

/*
** Menu_AdjustCursor
**
** This function takes the given menu, the direction, and attempts
** to adjust the menu's cursor so that it's at the next available
** slot.
*/
void Menu_AdjustCursor( menuframework_s *m, int dir )
{
	menucommon_s *citem;

	/*
	** see if it's in a valid spot
	*/
	if ( m->cursor >= 0 && m->cursor < m->nitems )
	{
		if ( ( citem = Menu_ItemAtCursor( m ) ) != 0 )
		{
			if ( citem->type != MTYPE_SEPARATOR )
				return;
		}
	}

	/*
	** it's not in a valid spot, so crawl in the direction indicated until we
	** find a valid spot
	*/
	if ( dir == 1 )
	{
		while ( 1 )
		{
			citem = Menu_ItemAtCursor( m );
			if ( citem )
				if ( citem->type != MTYPE_SEPARATOR )
					break;
			m->cursor += dir;
			if ( m->cursor >= m->nitems )
				m->cursor = 0;
		}
	}
	else
	{
		while ( 1 )
		{
			citem = Menu_ItemAtCursor( m );
			if ( citem )
				if ( citem->type != MTYPE_SEPARATOR )
					break;
			m->cursor += dir;
			if ( m->cursor < 0 )
				m->cursor = m->nitems - 1;
		}
	}
}

void Menu_Center( menuframework_s *menu )
{
	int height;

	height = ( ( menucommon_s * ) menu->items[menu->nitems-1])->y;
	height += 10;

	menu->y = ( VID_HEIGHT - height ) / 2;
}

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

/*
================
Menu_MouseKey

Pointer and wheel semantics for framework menus: left click
activates the item under the pointer (a press on a slider starts a
drag), right click goes back like Escape, and the wheel moves the
selection or adjusts the value under it.  Returns the sound to play,
or NULL.
================
*/
const char *Menu_MouseKey( menuframework_s *m, int key )
{
	extern void M_PopMenu( void );

	switch ( key )
	{
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
				return "misc/menu2.wav";
			}
		}
		return NULL;

	case K_MOUSE2:
		M_PopMenu();
		return "misc/menu3.wav";

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
			return "misc/menu2.wav";
		}
		return NULL;
	}

	return NULL;
}

void Menu_Draw( menuframework_s *menu )
{
	int i;
	menucommon_s *item;

	/*
	** draw contents
	*/
	for ( i = 0; i < menu->nitems; i++ )
	{
		switch ( ( ( menucommon_s * ) menu->items[i] )->type )
		{
		case MTYPE_FIELD:
			Field_Draw( ( menufield_s * ) menu->items[i] );
			break;
		case MTYPE_SLIDER:
			Slider_Draw( ( menuslider_s * ) menu->items[i] );
			break;
		case MTYPE_LIST:
			MenuList_Draw( ( menulist_s * ) menu->items[i] );
			break;
		case MTYPE_SPINCONTROL:
			SpinControl_Draw( ( menulist_s * ) menu->items[i] );
			break;
		case MTYPE_ACTION:
			Action_Draw( ( menuaction_s * ) menu->items[i] );
			break;
		case MTYPE_SEPARATOR:
			Separator_Draw( ( menuseparator_s * ) menu->items[i] );
			break;
		}
	}

	/* let the pointer move the selection before the highlight draws */
	Menu_MousePass( menu );

	item = Menu_ItemAtCursor( menu );

	if ( item && item->cursordraw )
	{
		item->cursordraw( item );
	}
	else if ( menu->cursordraw )
	{
		menu->cursordraw( menu );
	}
	else if ( item && item->type != MTYPE_FIELD )
	{
		if ( item->flags & QMF_LEFT_JUSTIFY )
		{
			Draw_Char( menu->x + item->x - 24 + item->cursor_offset, menu->y + item->y, 12 + ( ( int ) ( Sys_Milliseconds()/250 ) & 1 ) );
		}
		else
		{
			Draw_Char( menu->x + item->cursor_offset, menu->y + item->y, 12 + ( ( int ) ( Sys_Milliseconds()/250 ) & 1 ) );
		}
	}

	if ( item )
	{
		if ( item->statusbarfunc )
			item->statusbarfunc( ( void * ) item );
		else if ( item->statusbar )
			Menu_DrawStatusBar( item->statusbar );
		else
			Menu_DrawStatusBar( menu->statusbar );

	}
	else
	{
		Menu_DrawStatusBar( menu->statusbar );
	}
}

void Menu_DrawStatusBar( const char *string )
{
	if ( string )
	{
		int l = strlen( string );
		int maxcol = VID_WIDTH / 8;
		int col = maxcol / 2 - l / 2;

		Draw_Fill( 0, VID_HEIGHT-8, VID_WIDTH, 8, 4 );
		Menu_DrawString( col*8, VID_HEIGHT - 8, string );
	}
	else
	{
		Draw_Fill( 0, VID_HEIGHT-8, VID_WIDTH, 8, 0 );
	}
}

void Menu_DrawString( int x, int y, const char *string )
{
	unsigned i;

	for ( i = 0; i < strlen( string ); i++ )
	{
		Draw_Char( ( x + i*8 ), y, string[i] );
	}
}

void Menu_DrawStringDark( int x, int y, const char *string )
{
	unsigned i;

	for ( i = 0; i < strlen( string ); i++ )
	{
		Draw_Char( ( x + i*8 ), y, string[i] + 128 );
	}
}

void Menu_DrawStringR2L( int x, int y, const char *string )
{
	unsigned i;

	for ( i = 0; i < strlen( string ); i++ )
	{
		Draw_Char( ( x - i*8 ), y, string[strlen(string)-i-1] );
	}
}

void Menu_DrawStringR2LDark( int x, int y, const char *string )
{
	unsigned i;

	for ( i = 0; i < strlen( string ); i++ )
	{
		Draw_Char( ( x - i*8 ), y, string[strlen(string)-i-1]+128 );
	}
}

void *Menu_ItemAtCursor( menuframework_s *m )
{
	if ( m->cursor < 0 || m->cursor >= m->nitems )
		return 0;

	return m->items[m->cursor];
}

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

qboolean Menu_SelectItem( menuframework_s *s )
{
	menucommon_s *item = ( menucommon_s * ) Menu_ItemAtCursor( s );

	if ( item )
	{
		switch ( item->type )
		{
		case MTYPE_FIELD:
			return Field_DoEnter( ( menufield_s * ) item ) ;
		case MTYPE_ACTION:
			Action_DoEnter( ( menuaction_s * ) item );
			return true;
		case MTYPE_LIST:
//			Menulist_DoEnter( ( menulist_s * ) item );
			return false;
		case MTYPE_SPINCONTROL:
//			SpinControl_DoEnter( ( menulist_s * ) item );
			return false;
		}
	}
	return false;
}

void Menu_SetStatusBar( menuframework_s *m, const char *string )
{
	m->statusbar = string;
}

void Menu_SlideItem( menuframework_s *s, int dir )
{
	menucommon_s *item = ( menucommon_s * ) Menu_ItemAtCursor( s );

	if ( item )
	{
		switch ( item->type )
		{
		case MTYPE_SLIDER:
			Slider_DoSlide( ( menuslider_s * ) item, dir );
			break;
		case MTYPE_SPINCONTROL:
			SpinControl_DoSlide( ( menulist_s * ) item, dir );
			break;
		}
	}
}

int Menu_TallySlots( menuframework_s *menu )
{
	int i;
	int total = 0;

	for ( i = 0; i < menu->nitems; i++ )
	{
		if ( ( ( menucommon_s * ) menu->items[i] )->type == MTYPE_LIST )
		{
			int nitems = 0;
			const char **n = ( ( menulist_s * ) menu->items[i] )->itemnames;

			while (*n)
				nitems++, n++;

			total += nitems;
		}
		else
		{
			total++;
		}
	}

	return total;
}

void MenuList_Draw( menulist_s *l )
{
	const char **n;
	int y = 0;

	Menu_DrawStringR2LDark( l->generic.x + l->generic.parent->x + LCOLUMN_OFFSET, l->generic.y + l->generic.parent->y, l->generic.name );

	n = l->itemnames;

  	Draw_Fill( l->generic.x - 112 + l->generic.parent->x, l->generic.parent->y + l->generic.y + l->curvalue*10 + 10, 128, 10, 16 );
	while ( *n )
	{
		Menu_DrawStringR2LDark( l->generic.x + l->generic.parent->x + LCOLUMN_OFFSET, l->generic.y + l->generic.parent->y + y + 10, *n );

		n++;
		y += 10;
	}

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
}

void Separator_Draw( menuseparator_s *s )
{
	if ( s->generic.name )
		Menu_DrawStringR2LDark( s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y, s->generic.name );

	s->generic.hit_w = s->generic.hit_h = 0;
}

void Slider_DoSlide( menuslider_s *s, int dir )
{
	s->curvalue += dir;

	if ( s->curvalue > s->maxvalue )
		s->curvalue = s->maxvalue;
	else if ( s->curvalue < s->minvalue )
		s->curvalue = s->minvalue;

	if ( s->generic.callback )
		s->generic.callback( s );
}

void Slider_Draw( menuslider_s *s )
{
	int	i;

	Menu_DrawStringR2LDark( s->generic.x + s->generic.parent->x + LCOLUMN_OFFSET,
		                s->generic.y + s->generic.parent->y, 
						s->generic.name );

	s->range = ( s->curvalue - s->minvalue ) / ( float ) ( s->maxvalue - s->minvalue );

	if ( s->range < 0)
		s->range = 0;
	if ( s->range > 1)
		s->range = 1;
	Draw_Char( s->generic.x + s->generic.parent->x + RCOLUMN_OFFSET, s->generic.y + s->generic.parent->y, 128);
	for ( i = 0; i < SLIDER_RANGE; i++ )
		Draw_Char( RCOLUMN_OFFSET + s->generic.x + i*8 + s->generic.parent->x + 8, s->generic.y + s->generic.parent->y, 129);
	Draw_Char( RCOLUMN_OFFSET + s->generic.x + i*8 + s->generic.parent->x + 8, s->generic.y + s->generic.parent->y, 130);
	Draw_Char( ( int ) ( 8 + RCOLUMN_OFFSET + s->generic.parent->x + s->generic.x + (SLIDER_RANGE-1)*8 * s->range ), s->generic.y + s->generic.parent->y, 131);

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
}

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

void SpinControl_DoSlide( menulist_s *s, int dir )
{
	s->curvalue += dir;

	if ( s->curvalue < 0 )
		s->curvalue = 0;
	else if ( s->itemnames[s->curvalue] == 0 )
		s->curvalue--;

	if ( s->generic.callback )
		s->generic.callback( s );
}

void SpinControl_Draw( menulist_s *s )
{
	char buffer[100];

	if ( s->generic.name )
	{
		Menu_DrawStringR2LDark( s->generic.x + s->generic.parent->x + LCOLUMN_OFFSET, 
							s->generic.y + s->generic.parent->y, 
							s->generic.name );
	}
	if ( !strchr( s->itemnames[s->curvalue], '\n' ) )
	{
		Menu_DrawString( RCOLUMN_OFFSET + s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y, s->itemnames[s->curvalue] );
	}
	else
	{
		strcpy( buffer, s->itemnames[s->curvalue] );
		*strchr( buffer, '\n' ) = 0;
		Menu_DrawString( RCOLUMN_OFFSET + s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y, buffer );
		strcpy( buffer, strchr( s->itemnames[s->curvalue], '\n' ) + 1 );
		Menu_DrawString( RCOLUMN_OFFSET + s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y + 10, buffer );
	}

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
}

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

