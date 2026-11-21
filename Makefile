# SPDX-License-Identifier: GPL-3.0-or-later
#
# Panoptix - Makefile
#
# Haeufige Ziele:
#   make                 Spiel bauen (build/linux/panoptix)
#   make run             bauen und starten
#   make test            Unit-Tests bauen und ausfuehren
#   make sim             headless Balance-/Smoke-Simulation (ohne SDL)
#   make DEBUG=1         Debug-Build mit AddressSanitizer + UBSan
#   make windows         Windows-Build per MinGW-Cross-Compiler (braucht SDL2-MinGW-Dev-Dateien)
#   make release         Linux-Release-Archiv in dist/ erzeugen
#   make install         installieren (PREFIX=/usr/local)
#   make format          clang-format auf alle Quellen
#   make clean
#
# Einzige Abhaengigkeit: SDL2 (Debian/Ubuntu: apt install libsdl2-dev).

NAME     := panoptix
VERSION  := 0.1.0
PLATFORM ?= linux
PREFIX   ?= /usr/local

BUILD_DIR := build/$(PLATFORM)
OBJ_DIR   := $(BUILD_DIR)/obj

# ---- Compiler ---------------------------------------------------------
ifeq ($(PLATFORM),windows)
  CROSS ?= x86_64-w64-mingw32-
  EXE   := .exe
else
  CROSS ?=
  EXE   :=
endif
CC ?= cc
ifneq ($(CROSS),)
  CC := $(CROSS)gcc
endif

CFLAGS_BASE := -std=c11 -Wall -Wextra -Wpedantic -Isrc -ffp-contract=off -DPNX_VERSION=\"$(VERSION)\"
ifeq ($(DEBUG),1)
  CFLAGS_OPT := -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS_SAN := -fsanitize=address,undefined
else
  CFLAGS_OPT := -O2 -g
  LDFLAGS_SAN :=
endif
ifeq ($(WERROR),1)
  CFLAGS_BASE += -Werror
endif
CFLAGS += $(CFLAGS_BASE) $(CFLAGS_OPT) -MMD -MP
ifneq ($(DATADIR),)
  CFLAGS += -DPNX_DATADIR=\"$(DATADIR)\"
endif

# ---- SDL2 ---------------------------------------------------------------
# Ueberschreibbar: make SDL2_CFLAGS=... SDL2_LIBS=...
ifeq ($(PLATFORM),windows)
  SDL2_CFLAGS ?= $(shell $(CROSS)pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
  SDL2_LIBS   ?= -lmingw32 -lSDL2main -lSDL2 -mwindows
else
  SDL2_CFLAGS ?= $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
  SDL2_LIBS   ?= $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)
endif
LDLIBS := -lm

# ---- Quellen --------------------------------------------------------------
# Kern (ohne SDL): core, data, world, sim, rules, game, plattformspezifisch
CORE_DIRS  := src/core src/data src/world src/sim src/rules src/game src/platform/$(PLATFORM)
# Frontend (mit SDL): render, audio, ui, app
FRONT_DIRS := src/render src/audio src/ui src/app

CORE_SRC  := $(foreach d,$(CORE_DIRS),$(wildcard $(d)/*.c))
FRONT_SRC := $(foreach d,$(FRONT_DIRS),$(wildcard $(d)/*.c))
TEST_SRC  := $(wildcard tests/*.c)

CORE_OBJ  := $(patsubst %.c,$(OBJ_DIR)/%.o,$(CORE_SRC))
FRONT_OBJ := $(patsubst %.c,$(OBJ_DIR)/%.o,$(FRONT_SRC))
TEST_OBJ  := $(patsubst %.c,$(OBJ_DIR)/%.o,$(TEST_SRC))
SIM_OBJ   := $(OBJ_DIR)/tools/sim_main.o

GAME_BIN  := $(BUILD_DIR)/$(NAME)$(EXE)
SIM_BIN   := $(BUILD_DIR)/$(NAME)_sim$(EXE)
TEST_BIN  := $(BUILD_DIR)/$(NAME)_tests$(EXE)

.PHONY: all game run test sim check clean format windows release install uninstall dist-src help data-check

all: game

help:
	@sed -n '3,16p' Makefile

game: $(GAME_BIN)

$(GAME_BIN): $(CORE_OBJ) $(FRONT_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_SAN) $(SDL2_LIBS) $(LDLIBS)

sim: $(SIM_BIN)

$(SIM_BIN): $(CORE_OBJ) $(SIM_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_SAN) $(LDLIBS)

$(TEST_BIN): $(CORE_OBJ) $(TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_SAN) $(LDLIBS)

# SDL-Header nur fuer Frontend-Objekte
$(OBJ_DIR)/src/render/%.o $(OBJ_DIR)/src/audio/%.o $(OBJ_DIR)/src/ui/%.o $(OBJ_DIR)/src/app/%.o: CFLAGS += $(SDL2_CFLAGS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: game
	PANOPTIX_DATA=$(CURDIR)/data $(GAME_BIN)

test: $(TEST_BIN)
	PANOPTIX_DATA=$(CURDIR)/data $(TEST_BIN)

check: test data-check

data-check: $(TEST_BIN)
	PANOPTIX_DATA=$(CURDIR)/data $(TEST_BIN) --data-only

windows:
	$(MAKE) PLATFORM=windows game

format:
	clang-format -i $(shell find src tests tools -name '*.c' -o -name '*.h')

# ---- Release ----------------------------------------------------------------
DIST_NAME := $(NAME)-$(VERSION)-$(PLATFORM)
release: game sim
	rm -rf dist/$(DIST_NAME)
	mkdir -p dist/$(DIST_NAME)
	cp $(GAME_BIN) dist/$(DIST_NAME)/
	cp -r data dist/$(DIST_NAME)/data
	mkdir -p dist/$(DIST_NAME)/docs && cp -r docs/*.md dist/$(DIST_NAME)/docs/ 2>/dev/null || true
	cp README.md LICENSE CREDITS.md THIRD_PARTY.md CHANGELOG.md dist/$(DIST_NAME)/ 2>/dev/null || true
	cd dist && tar czf $(DIST_NAME).tar.gz $(DIST_NAME)
	@echo "Release: dist/$(DIST_NAME).tar.gz"

dist-src:
	git archive --format=tar.gz --prefix=$(NAME)-$(VERSION)/ -o dist/$(NAME)-$(VERSION)-src.tar.gz HEAD

install: game
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/$(NAME)
	install -m 755 $(GAME_BIN) $(DESTDIR)$(PREFIX)/bin/$(NAME)
	cp -r data $(DESTDIR)$(PREFIX)/share/$(NAME)/
	@echo "Hinweis: Fuer systemweite Installation mit make DATADIR=$(PREFIX)/share/$(NAME)/data bauen."

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(NAME)
	rm -rf $(DESTDIR)$(PREFIX)/share/$(NAME)

clean:
	rm -rf build dist

-include $(CORE_OBJ:.o=.d) $(FRONT_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(SIM_OBJ:.o=.d)
