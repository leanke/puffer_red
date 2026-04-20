SHELL := /bin/bash
PYTHON := python3
CC ?= gcc
CXX ?= g++
NUMPY_INCLUDE := $(shell $(PYTHON) -c "import numpy; print(numpy.get_include())")
POKERED_DIR := pokered
POKERED_PLAY_BIN := pokered_play
POKERED_PLAY_SRC := $(POKERED_DIR)/pokered.c
POKERED_IMPL_SRCS := $(POKERED_DIR)/environment.c $(POKERED_DIR)/rewards.c $(POKERED_DIR)/observations.c
GAMBATTE_CPP_SRC := gambatte/gambatte_c.cpp
GAMBATTE_CPP_OBJ := gambatte/gambatte_c.o
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

# Gambatte abstraction layer
GAMBATTE_DIR := gambatte

# Local Gambatte build (set to use custom build, or leave empty for system libgambatte)
# Build with: cd ~/loft/puffer/gambatte && make && make install
GAMBATTE_LIB_DIR ?= $(HOME)/loft/puffer/gambatte/install

ifneq ($(wildcard $(GAMBATTE_LIB_DIR)/lib/libgambatte.a),)
    GAMBATTE_CFLAGS := -I$(GAMBATTE_LIB_DIR)/include
    GAMBATTE_LDFLAGS := -L$(GAMBATTE_LIB_DIR)/lib -lgambatte -lstdc++
    $(info Using local Gambatte: $(GAMBATTE_LIB_DIR))
else ifneq ($(wildcard $(GAMBATTE_LIB_DIR)/lib/libgambatte.so),)
    GAMBATTE_CFLAGS := -I$(GAMBATTE_LIB_DIR)/include
    GAMBATTE_LDFLAGS := -L$(GAMBATTE_LIB_DIR)/lib -Wl,-rpath,$(GAMBATTE_LIB_DIR)/lib -lgambatte -lstdc++
    $(info Using local Gambatte (shared): $(GAMBATTE_LIB_DIR))
else
    GAMBATTE_CFLAGS :=
    GAMBATTE_LDFLAGS := -lgambatte -lstdc++
    $(info Using system Gambatte)
endif

# CFLAGS/LDFLAGS used only for the standalone play target
CFLAGS := -DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION -DPLATFORM_DESKTOP -I$(NUMPY_INCLUDE) -I$(GAMBATTE_DIR) $(GAMBATTE_CFLAGS) -Wno-alloc-size-larger-than -Wno-implicit-function-declaration -fmax-errors=3 $(OPT_FLAGS) -fopenmp
CXXFLAGS := -std=c++17 -D__LIBRETRO__ -DHAVE_CSTDINT -I$(GAMBATTE_DIR) $(GAMBATTE_CFLAGS) $(OPT_FLAGS)
LDFLAGS := -fwrapv -Bsymbolic-functions $(LINK_OPT_FLAGS) $(GAMBATTE_LDFLAGS) -fopenmp -lm

.PHONY: all clean help pokered play

all: pokered

pokered: $(BINDING_SO)

play: $(POKERED_PLAY_BIN)

$(GAMBATTE_CPP_OBJ): $(GAMBATTE_CPP_SRC) $(GAMBATTE_DIR)/gambatte_c.h
	@echo "Compiling Gambatte C++ wrapper..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BINDING_SO): $(POKERED_DIR)/binding.c $(POKERED_IMPL_SRCS) $(POKERED_DIR)/pokered.h $(GAMBATTE_DIR)/gambatte_wrapper.h $(GAMBATTE_DIR)/optim.h $(GAMBATTE_DIR)/env_binding.h $(GAMBATTE_DIR)/gambatte_c.h
	@echo "Compiling Pokemon Red binding..."
	DEBUG=$(DEBUG) $(PYTHON) setup.py build_c --inplace

$(POKERED_PLAY_BIN): $(POKERED_PLAY_SRC) $(POKERED_IMPL_SRCS) $(POKERED_DIR)/pokered.h $(GAMBATTE_CPP_OBJ)
	@echo "Compiling standalone Pokemon Red player..."
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -I$(POKERED_DIR) -I$(POKERED_DIR)/includes -I$(GAMBATTE_DIR) $< $(POKERED_IMPL_SRCS) $(GAMBATTE_CPP_OBJ) -o $@ $(LDFLAGS) $(SDL2_LIBS)

clean:
	@echo "Cleaning..."
	@find $(POKERED_DIR) -name "*.so" -delete
	@find $(POKERED_DIR) -name "build" -type d -exec rm -rf {} + 2>/dev/null || true
	@rm -rf build
	@rm -f $(POKERED_PLAY_BIN)
	@rm -f $(GAMBATTE_CPP_OBJ)

install-deps:
	@echo "Installing Gambatte (gambatte-libretro)..."
	@echo ""
	@echo "Step 1: Clone the repository"
	@if [ -d /tmp/gambatte-libretro ]; then rm -rf /tmp/gambatte-libretro; fi
	git clone https://github.com/libretro/gambatte-libretro.git /tmp/gambatte-libretro
	@echo ""
	@echo "Step 2: Build libgambatte as a static library"
	cd /tmp/gambatte-libretro/libgambatte && \
		$(CXX) -std=c++17 -O3 -fPIC \
			-D__LIBRETRO__ -DHAVE_CSTDINT \
			-Iinclude -Isrc \
			-I../common \
			-Ilibretro -Ilibretro-common/include \
			-c $$(find src -name '*.cpp') \
			   libretro/gambatte_log.c && \
		ar rcs libgambatte.a *.o
	@echo ""
	@echo "Step 3: Install to $(GAMBATTE_LIB_DIR)"
	mkdir -p $(GAMBATTE_LIB_DIR)/lib $(GAMBATTE_LIB_DIR)/include
	cp /tmp/gambatte-libretro/libgambatte/libgambatte.a $(GAMBATTE_LIB_DIR)/lib/
	cp /tmp/gambatte-libretro/libgambatte/include/*.h $(GAMBATTE_LIB_DIR)/include/
	cp /tmp/gambatte-libretro/common/*.h $(GAMBATTE_LIB_DIR)/include/
	@echo ""
	@echo "Step 4: Clean up"
	rm -rf /tmp/gambatte-libretro
	@echo ""
	@echo "Gambatte installed to $(GAMBATTE_LIB_DIR)"
	@echo "  Headers: $(GAMBATTE_LIB_DIR)/include/"
	@echo "  Library: $(GAMBATTE_LIB_DIR)/lib/libgambatte.a"

help:
	@echo "Pokemon Red RL Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make                 - Build pokered binding (release, optimized)"
	@echo "  make clean           - Clean environment"
	@echo "  make play            - Build standalone SDL player"
	@echo "  make install-deps    - Install Gambatte development libraries"
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


