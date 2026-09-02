/*
test_override.c -- host test for ref_gl/gl_override.c.

No GL context, no game data: a stub refimport_t serves files from a temp
directory. Built and run by `make test-ref`.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../gl_local.h"

refimport_t	ri;				/* gl_override.c calls ri.FS_LoadFile / FS_FreeFile / Con_Printf */

static char	tmpdir[1024];
static int	failures;

#define CHECK(cond) do { if (!(cond)) { failures++; \
	fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- stub refimport --------------------------------------------------- */

static void stub_Con_Printf (int print_level, char *fmt, ...)
{
	(void)print_level; (void)fmt;
}

static int stub_FS_LoadFile (char *name, void **buf)
{
	char	path[2048];
	FILE	*f;
	long	len;

	snprintf (path, sizeof(path), "%s/%s", tmpdir, name);
	f = fopen (path, "rb");
	if (!f)
	{
		if (buf)
			*buf = NULL;
		return -1;
	}
	fseek (f, 0, SEEK_END);
	len = ftell (f);
	fseek (f, 0, SEEK_SET);
	if (buf)
	{
		*buf = malloc (len + 1);
		fread (*buf, 1, len, f);
		((char *)*buf)[len] = 0;
	}
	fclose (f);
	return (int)len;
}

static void stub_FS_FreeFile (void *buf)
{
	free (buf);
}

static void write_file (const char *name, const unsigned char *data, size_t len)
{
	char	path[2048];
	FILE	*f;

	snprintf (path, sizeof(path), "%s/%s", tmpdir, name);
	f = fopen (path, "wb");
	if (!f) { perror (path); exit (2); }
	fwrite (data, 1, len, f);
	fclose (f);
}

static void remove_file (const char *name)
{
	char	path[2048];

	snprintf (path, sizeof(path), "%s/%s", tmpdir, name);
	unlink (path);
}

/* ---- tests ------------------------------------------------------------- */

static void test_override_path (void)
{
	char	out[MAX_QPATH];
	char	small[16];

	CHECK (GL_OverridePath ("textures/e1u1/PIP04_4.wal", "png", out, sizeof(out)));
	CHECK (!strcmp (out, "textures/e1u1/pip04_4.png"));

	CHECK (GL_OverridePath ("textures/e1u1/floor1_1.WAL", "jpg", out, sizeof(out)));
	CHECK (!strcmp (out, "textures/e1u1/floor1_1.jpg"));

	CHECK (!GL_OverridePath ("textures/e1u1/floor1_1.pcx", "png", out, sizeof(out)));
	CHECK (!GL_OverridePath (".wal", "png", out, sizeof(out)));
	CHECK (!GL_OverridePath ("textures/e1u1/floor1_1.wal", "png", small, sizeof(small)));
	CHECK (GL_OverridePath ("a.wal", "png", small, sizeof(small)));
	CHECK (!strcmp (small, "a.png"));
}

int main (void)
{
	char		sub[1100];
	const char	*base = getenv ("TMPDIR");

	snprintf (tmpdir, sizeof(tmpdir), "%s/q2_override_XXXXXX", base ? base : "/tmp");
	if (!mkdtemp (tmpdir)) { perror ("mkdtemp"); return 2; }
	snprintf (sub, sizeof(sub), "%s/textures", tmpdir);
	mkdir (sub, 0700);
	snprintf (sub, sizeof(sub), "%s/textures/e1u1", tmpdir);
	mkdir (sub, 0700);

	ri.Con_Printf = stub_Con_Printf;
	ri.FS_LoadFile = stub_FS_LoadFile;
	ri.FS_FreeFile = stub_FS_FreeFile;

	test_override_path ();

	rmdir (sub);
	snprintf (sub, sizeof(sub), "%s/textures", tmpdir);
	rmdir (sub);
	rmdir (tmpdir);

	if (failures)
	{
		fprintf (stderr, "test_override: %d failure(s)\n", failures);
		return 1;
	}
	printf ("test_override: all checks passed\n");
	return 0;
}
