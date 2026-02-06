SHELL := /bin/bash
PYTHON := python3
CC ?= gcc
NUMPY_INCLUDE := $(shell $(PYTHON) -c "import numpy; print(numpy.get_include())")
POKERED_DIR := pokered
POKERED_PLAY_BIN := pokered_play
POKERED_PLAY_SRC := $(POKERED_DIR)/pokered.c
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
	# Release build: maximum optimization (Strategy #9 from optim_strat.md)
	OPT_FLAGS := -O3 -flto -march=native -mtune=native -mavx2 -mfma -ffast-math -DNDEBUG -fomit-frame-pointer -funroll-loops
	LINK_OPT_FLAGS := -O3 -flto
endif

CFLAGS := -DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION -DPLATFORM_DESKTOP -I$(NUMPY_INCLUDE) -Wno-alloc-size-larger-than -Wno-implicit-function-declaration -fmax-errors=3 $(OPT_FLAGS) -DENABLE_VFS
LDFLAGS := -fwrapv -Bsymbolic-functions $(LINK_OPT_FLAGS) -lmgba

.PHONY: all clean help pokered pokered_play

all: pokered

pokered: $(POKERED_DIR)/binding.so

play: $(POKERED_PLAY_BIN)


$(POKERED_DIR)/binding.so: $(POKERED_DIR)/binding.c 
	@echo "Compiling Pokemon Red binding..."
	@cd $(POKERED_DIR) && \
	$(PYTHON) -c "from distutils.core import setup, Extension; import numpy; \
	setup(ext_modules=[Extension('binding', ['binding.c'], \
	    include_dirs=[numpy.get_include(), 'include', '-DENABLE_VFS'], \
	    extra_compile_args='$(CFLAGS)'.split(), \
	    extra_link_args='$(LDFLAGS)'.split())])" build_ext --inplace

$(POKERED_PLAY_BIN): $(POKERED_PLAY_SRC)
	@echo "Compiling standalone Pokemon Red player..."
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -I$(POKERED_DIR) -I$(POKERED_DIR)/includes $< -o $@ $(LDFLAGS) $(SDL2_LIBS)

clean:
	@echo "Cleaning..."
	@find $(POKERED_DIR) -name "*.so" -delete
	@find $(POKERED_DIR) -name "build" -type d -exec rm -rf {} + 2>/dev/null || true
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


