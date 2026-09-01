// verify_load.c -- load-smoke gate between "links" and "plays".
// dlopen's the renderer and game bundles with RTLD_NOW and checks that
// every required entry point resolves. Needs no game data, no SDL.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static void *load(const char *path)
{
	void *h = dlopen(path, RTLD_NOW);
	if (!h)
	{
		fprintf(stderr, "verify-load: dlopen(%s) failed: %s\n", path, dlerror());
		exit(1);
	}
	return h;
}

static void check(void *h, const char *name)
{
	if (!dlsym(h, name))
	{
		fprintf(stderr, "verify-load: missing entry point: %s\n", name);
		exit(1);
	}
}

int main(void)
{
	static const char *ref_exports[] = {
		"GetRefAPI", "RW_IN_Init", "RW_IN_Shutdown", "RW_IN_Activate",
		"RW_IN_Commands", "RW_IN_Move", "RW_IN_Frame",
		"KBD_Init", "KBD_Update", "KBD_Close"
	};
	static const char *game_exports[] = { "GetGameAPI" };
	size_t i;

	void *ref = load("./build/ref_gl.so");
	for (i = 0; i < sizeof(ref_exports) / sizeof(ref_exports[0]); i++)
		check(ref, ref_exports[i]);

	void *game = load("./baseq2/gamearm64.so");
	for (i = 0; i < sizeof(game_exports) / sizeof(game_exports[0]); i++)
		check(game, game_exports[i]);

	printf("verify-load: OK (both bundles load, all entry points present)\n");
	return 0;
}
