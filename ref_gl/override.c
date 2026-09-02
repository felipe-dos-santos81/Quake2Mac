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
// gl_override.c -- hi-res replacements for .wal world textures.
//
// For textures/<dir>/<name>.wal the renderer may find
// textures/<dir>/<name>.png / .tga / .jpg on disk (baseq2/textures/, written
// by tools/extract_textures.py or by hand). This file builds those paths and
// decodes the files; it is the only file that includes stb_image.h.
// Nothing here may call ri.Sys_Error: a bad override falls back to the .wal.

#include "ref_gl/local.h"

// stb_image: only the formats we probe, no stdio (we read through ri.FS_LoadFile)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384	// bounds decode memory for absurd files
#include "ref_gl/stb_image.h"

// probe order; the first file that decodes wins
static const char *override_exts[] = { "png", "tga", "jpg" };

/*
================
GL_OverridePath

textures/e1u1/PIP04_4.wal, "png" -> textures/e1u1/pip04_4.png (lowercased,
so it matches what the extractor writes on a case-sensitive volume).
Returns false if walname lacks a .wal suffix or the result does not fit.
================
*/
qboolean GL_OverridePath (const char *walname, const char *ext, char *out, size_t outsize)
{
	size_t	len, stem, i;

	if (!walname || !ext || !out || outsize == 0)
		return false;

	len = strlen (walname);
	if (len < 5)
		return false;
	if (walname[len-4] != '.'
		|| tolower ((unsigned char)walname[len-3]) != 'w'
		|| tolower ((unsigned char)walname[len-2]) != 'a'
		|| tolower ((unsigned char)walname[len-1]) != 'l')
		return false;

	stem = len - 4;
	if (stem + 1 + strlen (ext) + 1 > outsize)
		return false;

	for (i = 0 ; i < stem ; i++)
		out[i] = tolower ((unsigned char)walname[i]);
	out[stem] = '.';
	strcpy (out + stem + 1, ext);
	return true;
}

/*
================
GL_LoadOverride

Probes textures/<name>.png, .tga, .jpg through the engine filesystem and
decodes the first hit to RGBA (4 bytes per texel, top row first). Returns
NULL if nothing decodes. A file that exists but does not decode is reported
and skipped -- never fatal.
================
*/
byte *GL_LoadOverride (const char *walname, int *width, int *height)
{
	char	path[MAX_QPATH];
	byte	*buf, *pix;
	int		len, i, w, h;

	for (i = 0 ; i < (int)(sizeof(override_exts)/sizeof(override_exts[0])) ; i++)
	{
		if (!GL_OverridePath (walname, override_exts[i], path, sizeof(path)))
			return NULL;
		len = ri.FS_LoadFile (path, (void **)&buf);
		if (!buf)
			continue;
		pix = stbi_load_from_memory (buf, len, &w, &h, NULL, STBI_rgb_alpha);
		ri.FS_FreeFile (buf);
		if (!pix)
		{
			ri.Con_Printf (PRINT_ALL, "GL_LoadOverride: bad %s: %s\n", path, stbi_failure_reason ());
			continue;
		}
		ri.Con_Printf (PRINT_DEVELOPER, "GL_LoadOverride: %s (%ix%i)\n", path, w, h);
		*width = w;
		*height = h;
		return pix;
	}
	return NULL;
}

/*
================
GL_FreeOverride
================
*/
void GL_FreeOverride (byte *pixels)
{
	stbi_image_free (pixels);
}
