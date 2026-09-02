# Makefile for Quake II — Apple Silicon (arm64) with SDL3
# Stages: 1 install → 2 data → 3 build → 4 run; plus help/clean.
# Styled after the "3AM Reel" template: self-documenting `help` via grep/awk.
# The -include'd .d dependency files land in MAKEFILE_LIST after a build, so
# `help` filters them out (and uses grep -h) to keep filenames out of the list.

# Variables ───────────────────────────────────────────────────────────────────
CC ?= cc
ARCH = arm64
SHLIBEXT = so
BUILD_DIR = build

SDL_CFLAGS := $(shell pkg-config sdl3 --cflags)
SDL_LIBS   := $(shell pkg-config sdl3 --libs)

# -MMD -MP: emit per-object header dependency files so editing a header
# (e.g. client/ref.h) rebuilds every object that includes it; without
# this a stale object/ref_gl.so mix corrupted the ref interface at runtime
BASE_CFLAGS = -O2 -g -Wall -I. $(SDL_CFLAGS) -MMD -MP
CFLAGS = $(BASE_CFLAGS)

EXE = $(BUILD_DIR)/quake2
REF_GL = $(BUILD_DIR)/ref_gl.$(SHLIBEXT)
GAME_DLL = baseq2/game$(ARCH).$(SHLIBEXT)
VERIFY_LOAD = $(BUILD_DIR)/verify_load

EXE_LDFLAGS = $(SDL_LIBS) -ldl -lm
BUNDLE_LDFLAGS = -bundle -undefined dynamic_lookup

.DEFAULT_GOAL := help

.PHONY: help install data objects build all verify-load run clean tools-test textures test-ref

# ── Stage 0 · Help ───────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32mQuake II SDL3 — Apple Silicon build\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target]\n\n\033[33mTargets:\033[0m\n"
	@grep -hE '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(filter-out %.d,$(MAKEFILE_LIST)) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

# ── Stage 1 · Dependencies ───────────────────────────────────────────────────

install: ## Check toolchain and SDL3 dependency (brew install sdl3 if missing)
	@$(CC) --version | head -1
	@pkg-config sdl3 --modversion > /dev/null 2>&1 || \
		{ echo "ERROR: sdl3 not found. Run: brew install sdl3"; exit 1; }
	@echo "SDL3 $$(pkg-config sdl3 --modversion) OK"
	@command -v uv > /dev/null 2>&1 && echo "uv $$(uv --version | cut -d' ' -f2) OK" || \
		echo "uv not found (optional, needed for 'make textures'): brew install uv"

# ── Stage 2 · Game data ──────────────────────────────────────────────────────

data: ## Check that Quake II game data (baseq2/pak0.pak) is present
	@if [ -f baseq2/pak0.pak ]; then \
		echo "Game data OK: baseq2/pak0.pak"; \
	else \
		echo "ERROR: baseq2/pak0.pak not found."; \
		echo "Copy your Quake II baseq2 pak files (pak0.pak, ...) into ./baseq2/"; \
		exit 1; \
	fi

# ── Stage 3 · Build ──────────────────────────────────────────────────────────

# Object paths mirror source paths (client/main.c → build/client/main.o).
# Multi-unit sources compile ONCE and link into several units (game/q_shared.c,
# game/monsters/flash.c, platform/posix/shared.c, platform/posix/glob.c) —
# CFLAGS is uniform across units; keep it that way or reintroduce per-unit object directories.
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<

# qcommon + shared
COMMON_OBJS = \
	$(BUILD_DIR)/qcommon/cmd.o $(BUILD_DIR)/qcommon/cmodel.o \
	$(BUILD_DIR)/qcommon/common.o $(BUILD_DIR)/qcommon/crc.o \
	$(BUILD_DIR)/qcommon/cvar.o $(BUILD_DIR)/qcommon/files.o \
	$(BUILD_DIR)/qcommon/md4.o $(BUILD_DIR)/qcommon/net_chan.o \
	$(BUILD_DIR)/qcommon/pmove.o $(BUILD_DIR)/game/q_shared.o

# client
CLIENT_OBJS = \
	$(BUILD_DIR)/client/screen/cinematic.o $(BUILD_DIR)/client/net/ents.o \
	$(BUILD_DIR)/client/net/fx.o $(BUILD_DIR)/client/input.o \
	$(BUILD_DIR)/client/screen/inv.o $(BUILD_DIR)/client/main.o \
	$(BUILD_DIR)/client/net/parse.o $(BUILD_DIR)/client/net/predict.o \
	$(BUILD_DIR)/client/net/tents.o $(BUILD_DIR)/client/screen/scrn.o \
	$(BUILD_DIR)/client/screen/view.o $(BUILD_DIR)/client/screen/console.o \
	$(BUILD_DIR)/client/screen/keys.o $(BUILD_DIR)/client/screen/menu.o \
	$(BUILD_DIR)/client/sound/dma.o $(BUILD_DIR)/client/sound/mem.o \
	$(BUILD_DIR)/client/sound/mix.o $(BUILD_DIR)/client/screen/qmenu.o \
	$(BUILD_DIR)/game/monsters/flash.o $(BUILD_DIR)/client/net/newfx.o

# server
SERVER_OBJS = \
	$(BUILD_DIR)/server/ccmds.o $(BUILD_DIR)/server/ents.o \
	$(BUILD_DIR)/server/game.o $(BUILD_DIR)/server/init.o \
	$(BUILD_DIR)/server/main.o $(BUILD_DIR)/server/send.o \
	$(BUILD_DIR)/server/user.o $(BUILD_DIR)/server/world.o

# platform layer linked into the executable
SYS_EXE_OBJS = \
	$(BUILD_DIR)/platform/posix/shared.o \
	$(BUILD_DIR)/platform/posix/vid_menu.o $(BUILD_DIR)/platform/posix/sys.o \
	$(BUILD_DIR)/platform/posix/glob.o $(BUILD_DIR)/platform/posix/udp.o \
	$(BUILD_DIR)/platform/sdl/sound.o $(BUILD_DIR)/platform/sdl/vid.o

# ref_gl renderer core
REF_CORE_OBJS = \
	$(BUILD_DIR)/ref_gl/draw.o $(BUILD_DIR)/ref_gl/image.o \
	$(BUILD_DIR)/ref_gl/light.o $(BUILD_DIR)/ref_gl/mesh.o \
	$(BUILD_DIR)/ref_gl/model.o $(BUILD_DIR)/ref_gl/rmain.o \
	$(BUILD_DIR)/ref_gl/rmisc.o $(BUILD_DIR)/ref_gl/rsurf.o \
	$(BUILD_DIR)/ref_gl/warp.o $(BUILD_DIR)/ref_gl/override.o
REF_EXTRA_OBJS = \
	$(BUILD_DIR)/game/q_shared.o \
	$(BUILD_DIR)/platform/posix/shared.o $(BUILD_DIR)/platform/posix/glob.o \
	$(BUILD_DIR)/platform/sdl/qgl.o $(BUILD_DIR)/platform/sdl/glw.o \
	$(BUILD_DIR)/platform/sdl/input.o

# game DLL
GAME_OBJS = \
	$(BUILD_DIR)/game/ai.o $(BUILD_DIR)/game/player/client.o \
	$(BUILD_DIR)/game/cmds.o $(BUILD_DIR)/game/svcmds.o \
	$(BUILD_DIR)/game/combat.o $(BUILD_DIR)/game/func.o \
	$(BUILD_DIR)/game/items.o $(BUILD_DIR)/game/main.o \
	$(BUILD_DIR)/game/misc.o $(BUILD_DIR)/game/monster.o \
	$(BUILD_DIR)/game/phys.o $(BUILD_DIR)/game/save.o \
	$(BUILD_DIR)/game/spawn.o $(BUILD_DIR)/game/target.o \
	$(BUILD_DIR)/game/trigger.o $(BUILD_DIR)/game/turret.o \
	$(BUILD_DIR)/game/utils.o $(BUILD_DIR)/game/weapon.o \
	$(BUILD_DIR)/game/monsters/actor.o $(BUILD_DIR)/game/monsters/berserk.o \
	$(BUILD_DIR)/game/monsters/boss2.o $(BUILD_DIR)/game/monsters/boss3.o \
	$(BUILD_DIR)/game/monsters/boss31.o $(BUILD_DIR)/game/monsters/boss32.o \
	$(BUILD_DIR)/game/monsters/brain.o $(BUILD_DIR)/game/monsters/chick.o \
	$(BUILD_DIR)/game/monsters/flipper.o $(BUILD_DIR)/game/monsters/float.o \
	$(BUILD_DIR)/game/monsters/flyer.o $(BUILD_DIR)/game/monsters/gladiator.o \
	$(BUILD_DIR)/game/monsters/gunner.o $(BUILD_DIR)/game/monsters/hover.o \
	$(BUILD_DIR)/game/monsters/infantry.o $(BUILD_DIR)/game/monsters/insane.o \
	$(BUILD_DIR)/game/monsters/medic.o $(BUILD_DIR)/game/monsters/move.o \
	$(BUILD_DIR)/game/monsters/mutant.o $(BUILD_DIR)/game/monsters/parasite.o \
	$(BUILD_DIR)/game/monsters/soldier.o $(BUILD_DIR)/game/monsters/supertank.o \
	$(BUILD_DIR)/game/monsters/tank.o $(BUILD_DIR)/game/player/hud.o \
	$(BUILD_DIR)/game/player/trail.o $(BUILD_DIR)/game/player/view.o \
	$(BUILD_DIR)/game/player/weapon.o $(BUILD_DIR)/game/q_shared.o \
	$(BUILD_DIR)/game/monsters/flash.o $(BUILD_DIR)/game/chase.o

ALL_ORIGINAL_OBJS = $(COMMON_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) \
	$(SYS_EXE_OBJS) $(REF_CORE_OBJS) $(REF_EXTRA_OBJS) $(GAME_OBJS)

-include $(ALL_ORIGINAL_OBJS:.o=.d)

objects: $(ALL_ORIGINAL_OBJS) ## Compile all engine objects (no linking)

$(REF_GL): $(REF_CORE_OBJS) $(REF_EXTRA_OBJS)
	$(CC) $(CFLAGS) $(BUNDLE_LDFLAGS) -framework OpenGL -o $@ \
		$(REF_CORE_OBJS) $(REF_EXTRA_OBJS)

# game DLL -- the engine dlopen's it from <cwd>/baseq2/, so install it there
$(GAME_DLL): $(GAME_OBJS)
	$(CC) $(CFLAGS) $(BUNDLE_LDFLAGS) -o $@ $(GAME_OBJS)

$(EXE): $(COMMON_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) $(SYS_EXE_OBJS)
	$(CC) $(CFLAGS) -o $@ $(COMMON_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) \
		$(SYS_EXE_OBJS) $(EXE_LDFLAGS)

build: objects $(REF_GL) $(GAME_DLL) $(EXE) ## Compile the engine, renderer, and game DLL

all: data build ## Check game data, then build everything

# Linked with SDL_LIBS so ref_gl.so's undefined SDL symbols (glw.o,
# input.o) resolve in the flat namespace, as they do inside the engine
# executable; verify_load.c itself uses no SDL APIs.
$(VERIFY_LOAD): $(BUILD_DIR)/platform/verify_load.o
	$(CC) $(CFLAGS) -o $@ $(BUILD_DIR)/platform/verify_load.o $(SDL_LIBS) -ldl

verify-load: build $(VERIFY_LOAD) ## Load-smoke: dlopen both bundles and check entry points (no game data needed)
	./$(VERIFY_LOAD)

# Host test for the override loader: links override.o alone against a
# stub refimport_t, so it needs neither a GL context nor game data.
TEST_OVERRIDE = $(BUILD_DIR)/test_override

$(TEST_OVERRIDE): ref_gl/tests/test_override.c $(BUILD_DIR)/ref_gl/override.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ ref_gl/tests/test_override.c $(BUILD_DIR)/ref_gl/override.o

test-ref: $(TEST_OVERRIDE) ## Host test for the texture override loader (no GL, no game data)
	./$(TEST_OVERRIDE)

# ── Stage 4 · Run ────────────────────────────────────────────────────────────

run: build data ## Launch Quake II (windowed, GL renderer, no cursor grab)
	./$(EXE) +set vid_ref gl +set vid_fullscreen 0

# ── Stage 5 · Cleanup ────────────────────────────────────────────────────────

clean: ## Remove build outputs (build/ and the baseq2 game DLL copy)
	rm -rf $(BUILD_DIR)
	rm -f $(GAME_DLL)

# ── Stage 6 · Tools (standalone Python in tools/, needs uv) ──────────────────

UV ?= uv

define require_uv
	@command -v $(UV) > /dev/null 2>&1 || \
		{ echo "ERROR: uv not found. Install: brew install uv"; exit 1; }
endef

textures: ## Extract pak .wal textures to baseq2/textures/*.png (skips existing; FORCE=1 overwrites)
	$(require_uv)
	$(UV) run --project tools tools/extract_textures.py --gamedir baseq2 $(if $(FORCE),--force,)

tools-test: ## Run the tools/ pytest suite (extractor tests, no game data needed)
	$(require_uv)
	$(UV) run --project tools pytest tools/tests
