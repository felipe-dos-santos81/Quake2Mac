// glw_sdl.c -- SDL3 implementation of the GLimp_* contract expected by
// ref_gl (see ref_gl/gl_local.h). Replaces linux/gl_fxmesa.c.
// Windowed only; the mouse cursor is never grabbed.

#include <SDL3/SDL.h>

#include "ref_gl/local.h"

static SDL_Window    *sdl_window;
static SDL_GLContext  sdl_context;

/*
** GLimp_SetMode
** Always windowed in v1: the fullscreen flag is accepted and ignored.
*/
int GLimp_SetMode( int *pwidth, int *pheight, int mode, qboolean fullscreen )
{
	int width, height;

	(void)fullscreen;

	ri.Con_Printf( PRINT_ALL, "Initializing OpenGL display\n" );
	ri.Con_Printf( PRINT_ALL, "...setting mode %d:", mode );

	if ( !ri.Vid_GetModeInfo( &width, &height, mode ) )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return rserr_invalid_mode;
	}

	ri.Con_Printf( PRINT_ALL, " %d %d\n", width, height );

	// destroy the existing window, if any
	GLimp_Shutdown();

	if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) )
	{
		ri.Con_Printf( PRINT_ALL, "...SDL_InitSubSystem(VIDEO) failed: %s\n",
			SDL_GetError() );
		return rserr_unknown;
	}

	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	// Legacy-profile GL on macOS: request 2.1, set no profile mask.
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 1 );

	sdl_window = SDL_CreateWindow( "Quake II", width, height,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE );
	if ( !sdl_window )
	{
		ri.Con_Printf( PRINT_ALL, "...SDL_CreateWindow failed: %s\n",
			SDL_GetError() );
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		return rserr_unknown;
	}

	sdl_context = SDL_GL_CreateContext( sdl_window );
	if ( !sdl_context )
	{
		ri.Con_Printf( PRINT_ALL, "...SDL_GL_CreateContext failed: %s\n",
			SDL_GetError() );
		SDL_DestroyWindow( sdl_window );
		sdl_window = NULL;
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		return rserr_unknown;
	}

	SDL_GL_MakeCurrent( sdl_window, sdl_context );

	*pwidth = width;
	*pheight = height;

	// let the sound and input subsystems know about the new window
	ri.Vid_NewWindow( width, height );

	return rserr_ok;
}

void GLimp_Shutdown( void )
{
	if ( sdl_context )
	{
		SDL_GL_DestroyContext( sdl_context );
		sdl_context = NULL;
	}
	if ( sdl_window )
	{
		SDL_DestroyWindow( sdl_window );
		sdl_window = NULL;
	}
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
}

int GLimp_Init( void *hinstance, void *wndproc )
{
	(void)hinstance;
	(void)wndproc;
	return true;
}

void GLimp_BeginFrame( float camera_seperation )
{
	(void)camera_seperation;
}

void GLimp_EndFrame( void )
{
	/* no qglFlush(): SDL_GL_SwapWindow already flushes the drawable */
	if ( sdl_window )
		SDL_GL_SwapWindow( sdl_window );
}

void GLimp_AppActivate( qboolean active )
{
	(void)active;
}

void GLimp_EnableLogging( qboolean enable )
{
	(void)enable;
}

void GLimp_LogNewFrame( void )
{
}
