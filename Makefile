# Makefile for Quake II — Apple Silicon (arm64) with SDL3
# Stages: 1 install → 2 data → 3 build → 4 run; plus help/clean.
# Styled after the "3AM Reel" template: self-documenting `help` via grep/awk.

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
BASE_CFLAGS = -O2 -g -Wall -Dstricmp=strcasecmp $(SDL_CFLAGS) -MMD -MP
CFLAGS = $(BASE_CFLAGS)

EXE = $(BUILD_DIR)/quake2
REF_GL = $(BUILD_DIR)/ref_gl.$(SHLIBEXT)
GAME_DLL = baseq2/game$(ARCH).$(SHLIBEXT)
VERIFY_LOAD = $(BUILD_DIR)/verify_load

EXE_LDFLAGS = $(SDL_LIBS) -ldl -lm
BUNDLE_LDFLAGS = -bundle -undefined dynamic_lookup

.DEFAULT_GOAL := build

.PHONY: help install data objects build all verify-load run clean

# ── Stage 0 · Help ───────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32mQuake II SDL3 — Apple Silicon build\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'

# ── Stage 1 · Dependencies ───────────────────────────────────────────────────

install: ## Check toolchain and SDL3 dependency (brew install sdl3 if missing)
	@$(CC) --version | head -1
	@pkg-config sdl3 --modversion > /dev/null 2>&1 || \
		{ echo "ERROR: sdl3 not found. Run: brew install sdl3"; exit 1; }
	@echo "SDL3 $$(pkg-config sdl3 --modversion) OK"

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

vpath %.c client server qcommon game ref_gl linux null sdl

# GNU make keeps the directory part of the stem (e.g. `client/cmd`) when
# searching vpath, so strip it: the prerequisite is the basename .c file,
# resolved through the vpath order above.
.SECONDEXPANSION:
$(BUILD_DIR)/%.o: $$(notdir $$*).c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<

# qcommon + shared
COMMON_OBJS = \
	$(BUILD_DIR)/client/cmd.o $(BUILD_DIR)/client/cmodel.o \
	$(BUILD_DIR)/client/common.o $(BUILD_DIR)/client/crc.o \
	$(BUILD_DIR)/client/cvar.o $(BUILD_DIR)/client/files.o \
	$(BUILD_DIR)/client/md4.o $(BUILD_DIR)/client/net_chan.o \
	$(BUILD_DIR)/client/q_shared.o $(BUILD_DIR)/client/pmove.o

# client
CLIENT_OBJS = \
	$(BUILD_DIR)/client/cl_cin.o $(BUILD_DIR)/client/cl_ents.o \
	$(BUILD_DIR)/client/cl_fx.o $(BUILD_DIR)/client/cl_input.o \
	$(BUILD_DIR)/client/cl_inv.o $(BUILD_DIR)/client/cl_main.o \
	$(BUILD_DIR)/client/cl_parse.o $(BUILD_DIR)/client/cl_pred.o \
	$(BUILD_DIR)/client/cl_tent.o $(BUILD_DIR)/client/cl_scrn.o \
	$(BUILD_DIR)/client/cl_view.o $(BUILD_DIR)/client/console.o \
	$(BUILD_DIR)/client/keys.o $(BUILD_DIR)/client/menu.o \
	$(BUILD_DIR)/client/snd_dma.o $(BUILD_DIR)/client/snd_mem.o \
	$(BUILD_DIR)/client/snd_mix.o $(BUILD_DIR)/client/qmenu.o \
	$(BUILD_DIR)/client/m_flash.o $(BUILD_DIR)/client/cl_newfx.o

# server
SERVER_OBJS = \
	$(BUILD_DIR)/client/sv_ccmds.o $(BUILD_DIR)/client/sv_ents.o \
	$(BUILD_DIR)/client/sv_game.o $(BUILD_DIR)/client/sv_init.o \
	$(BUILD_DIR)/client/sv_main.o $(BUILD_DIR)/client/sv_send.o \
	$(BUILD_DIR)/client/sv_user.o $(BUILD_DIR)/client/sv_world.o

# platform layer linked into the executable
# (sdl/vid_sdl.o is appended by Task 8)
SYS_EXE_OBJS = \
	$(BUILD_DIR)/client/cd_null.o $(BUILD_DIR)/client/q_shlinux.o \
	$(BUILD_DIR)/client/vid_menu.o $(BUILD_DIR)/client/sys_linux.o \
	$(BUILD_DIR)/client/glob.o $(BUILD_DIR)/client/net_udp.o \
	$(BUILD_DIR)/client/snd_sdl.o $(BUILD_DIR)/client/vid_sdl.o

# ref_gl renderer core (sdl/ objects appended by Tasks 3-5)
REF_CORE_OBJS = \
	$(BUILD_DIR)/ref_gl/gl_draw.o $(BUILD_DIR)/ref_gl/gl_image.o \
	$(BUILD_DIR)/ref_gl/gl_light.o $(BUILD_DIR)/ref_gl/gl_mesh.o \
	$(BUILD_DIR)/ref_gl/gl_model.o $(BUILD_DIR)/ref_gl/gl_rmain.o \
	$(BUILD_DIR)/ref_gl/gl_rmisc.o $(BUILD_DIR)/ref_gl/gl_rsurf.o \
	$(BUILD_DIR)/ref_gl/gl_warp.o
REF_EXTRA_OBJS = \
	$(BUILD_DIR)/ref_gl/q_shared.o $(BUILD_DIR)/ref_gl/q_shlinux.o \
	$(BUILD_DIR)/ref_gl/glob.o $(BUILD_DIR)/ref_gl/qgl_sdl.o \
	$(BUILD_DIR)/ref_gl/glw_sdl.o $(BUILD_DIR)/ref_gl/in_sdl.o

# game DLL
GAME_OBJS = \
	$(BUILD_DIR)/game/g_ai.o $(BUILD_DIR)/game/p_client.o \
	$(BUILD_DIR)/game/g_cmds.o $(BUILD_DIR)/game/g_svcmds.o \
	$(BUILD_DIR)/game/g_combat.o $(BUILD_DIR)/game/g_func.o \
	$(BUILD_DIR)/game/g_items.o $(BUILD_DIR)/game/g_main.o \
	$(BUILD_DIR)/game/g_misc.o $(BUILD_DIR)/game/g_monster.o \
	$(BUILD_DIR)/game/g_phys.o $(BUILD_DIR)/game/g_save.o \
	$(BUILD_DIR)/game/g_spawn.o $(BUILD_DIR)/game/g_target.o \
	$(BUILD_DIR)/game/g_trigger.o $(BUILD_DIR)/game/g_turret.o \
	$(BUILD_DIR)/game/g_utils.o $(BUILD_DIR)/game/g_weapon.o \
	$(BUILD_DIR)/game/m_actor.o $(BUILD_DIR)/game/m_berserk.o \
	$(BUILD_DIR)/game/m_boss2.o $(BUILD_DIR)/game/m_boss3.o \
	$(BUILD_DIR)/game/m_boss31.o $(BUILD_DIR)/game/m_boss32.o \
	$(BUILD_DIR)/game/m_brain.o $(BUILD_DIR)/game/m_chick.o \
	$(BUILD_DIR)/game/m_flipper.o $(BUILD_DIR)/game/m_float.o \
	$(BUILD_DIR)/game/m_flyer.o $(BUILD_DIR)/game/m_gladiator.o \
	$(BUILD_DIR)/game/m_gunner.o $(BUILD_DIR)/game/m_hover.o \
	$(BUILD_DIR)/game/m_infantry.o $(BUILD_DIR)/game/m_insane.o \
	$(BUILD_DIR)/game/m_medic.o $(BUILD_DIR)/game/m_move.o \
	$(BUILD_DIR)/game/m_mutant.o $(BUILD_DIR)/game/m_parasite.o \
	$(BUILD_DIR)/game/m_soldier.o $(BUILD_DIR)/game/m_supertank.o \
	$(BUILD_DIR)/game/m_tank.o $(BUILD_DIR)/game/p_hud.o \
	$(BUILD_DIR)/game/p_trail.o $(BUILD_DIR)/game/p_view.o \
	$(BUILD_DIR)/game/p_weapon.o $(BUILD_DIR)/game/q_shared.o \
	$(BUILD_DIR)/game/m_flash.o $(BUILD_DIR)/game/g_chase.o

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

# Linked with SDL_LIBS so ref_gl.so's undefined SDL symbols (glw_sdl.o,
# in_sdl.o) resolve in the flat namespace, as they do inside the engine
# executable; verify_load.c itself uses no SDL APIs.
$(VERIFY_LOAD): $(BUILD_DIR)/verify_load.o
	$(CC) $(CFLAGS) -o $@ $(BUILD_DIR)/verify_load.o $(SDL_LIBS) -ldl

verify-load: build $(VERIFY_LOAD) ## Load-smoke: dlopen both bundles and check entry points (no game data needed)
	./$(VERIFY_LOAD)

# ── Stage 4 · Run ────────────────────────────────────────────────────────────

run: build data ## Launch Quake II (windowed, GL renderer, no cursor grab)
	./$(EXE) +set vid_ref gl +set vid_fullscreen 0

# ── Stage 5 · Cleanup ────────────────────────────────────────────────────────

clean: ## Remove build outputs (build/ and the baseq2 game DLL copy)
	rm -rf $(BUILD_DIR)
	rm -f $(GAME_DLL)
