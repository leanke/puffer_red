SHELL := /bin/bash
PYTHON := python3
CC ?= gcc
NUMPY_INCLUDE := $(shell $(PYTHON) -c "import numpy; print(numpy.get_include())")
POKERED_DIR := pokered
POKERED_PLAY_BIN := pokered_play
POKERED_PLAY_SRC := $(POKERED_DIR)/pokered.c
POKERED_IMPL_SRCS := $(POKERED_DIR)/environment.c $(POKERED_DIR)/rewards.c $(POKERED_DIR)/observations.c
BINDING_SO := $(shell $(PYTHON) -c "import sysconfig; print('$(POKERED_DIR)/binding' + sysconfig.get_config_var('EXT_SUFFIX'))")
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null || echo -lSDL2)

DEBUG ?= 0
PROFILE ?= 0

ifeq ($(DEBUG),1)
	OPT_FLAGS := -O0 -g -fsanitize=address,undefined,bounds,pointer-overflow,leak -fno-omit-frame-pointer
	LINK_OPT_FLAGS := -g -fsanitize=address,undefined,bounds,pointer-overflow,leak
else ifeq ($(PROFILE),1)
	# Profile build: optimized with debug symbols for perf/profiling
	OPT_FLAGS := -O2 -g -flto -march=native -mtune=native -mavx2 -mfma -ffast-math -funroll-loops -DENABLE_PERF_COUNTERS
	LINK_OPT_FLAGS := -O2 -g -flto
else
	# Release build: maximum optimization
	OPT_FLAGS := -O3 -flto -march=native -mtune=native -mavx2 -mfma -ffast-math -DNDEBUG -fomit-frame-pointer -funroll-loops
	LINK_OPT_FLAGS := -O3 -flto
endif

# Shared mGBA abstraction layer
MGBA_DIR := mgba

# CFLAGS/LDFLAGS used only for the standalone play target
CFLAGS := -DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION -DPLATFORM_DESKTOP -I$(NUMPY_INCLUDE) -I$(MGBA_DIR) -Wno-alloc-size-larger-than -Wno-implicit-function-declaration -fmax-errors=3 $(OPT_FLAGS) -DENABLE_VFS
LDFLAGS := -fwrapv -Bsymbolic-functions $(LINK_OPT_FLAGS) -lmgba

.PHONY: all clean help pokered play

all: pokered

pokered: $(BINDING_SO)

play: $(POKERED_PLAY_BIN)

$(BINDING_SO): $(POKERED_DIR)/binding.c $(POKERED_IMPL_SRCS) $(POKERED_DIR)/pokered.h $(MGBA_DIR)/mgba_wrapper.h $(MGBA_DIR)/optim.h $(MGBA_DIR)/env_binding.h
	@echo "Compiling Pokemon Red binding..."
	DEBUG=$(DEBUG) $(PYTHON) setup.py build_c --inplace

$(POKERED_PLAY_BIN): $(POKERED_PLAY_SRC) $(POKERED_IMPL_SRCS) $(POKERED_DIR)/pokered.h
	@echo "Compiling standalone Pokemon Red player..."
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -I$(POKERED_DIR) -I$(POKERED_DIR)/includes -I$(MGBA_DIR) $< $(POKERED_IMPL_SRCS) -o $@ $(LDFLAGS) $(SDL2_LIBS)

clean:
	@echo "Cleaning..."
	@find $(POKERED_DIR) -name "*.so" -delete
	@find $(POKERED_DIR) -name "build" -type d -exec rm -rf {} + 2>/dev/null || true
	@rm -rf build
	@rm -f $(POKERED_PLAY_BIN)

install-deps:
	@echo "Installing mGBA development libraries..."
	sudo apt-get update && sudo apt-get install -y libmgba0.10t64 libmgba-dev

help:
	@echo "Pokemon Red RL Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make                 - Build pokered binding (release, optimized)"
	@echo "  make clean           - Clean environment"
	@echo "  make pokered_play    - Build standalone SDL player"
	@echo "  make install-deps    - Install mGBA development libraries"
	@echo "  make test            - Run quick test"
	@echo "  make bench           - Run benchmark"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1              - Build with debug symbols and sanitizers"
	@echo "  PROFILE=1            - Build optimized with debug symbols for profiling"

# Quick test target
.PHONY: test bench
test: pokered
	@echo "Running quick test..."
	@$(PYTHON) tests/pokered.py 100

bench: pokered
	@echo "Running benchmark (1000 steps)..."
	@$(PYTHON) tests/pokered.py 1000


