# Adding a New Game

This template shows how to create a new mGBA-based RL environment for a
Game Boy, Game Boy Color, or Game Boy Advance game.

## Quick start

```bash
# 1. Copy the template
cp -r template/ mygame/

# 2. Rename files
mv mygame/template.h   mygame/mygame.h
mv mygame/template.c   mygame/mygame.c
mv mygame/template.py  mygame/mygame.py

# 3. Edit mygame.h — define your env struct, memory addresses, rewards
# 4. Edit mygame/binding.c — update the #include and #define Env
# 5. Edit mygame/mygame.py — update observation/action spaces
# 6. Register in setup.py (see below)
# 7. Build:  make mygame   (after adding a Makefile target)
```

## Architecture overview

```
mgba/                     Shared (game-agnostic)
  mgba_wrapper.h          mGBA core init, state save/load, SDL rendering,
                          memory read/write helpers
  optim.h                 Performance macros (PREFETCH, STEP_N_FRAMES, etc.)
  env_binding.h           Generic Python ↔ C binding (vec_init, vec_step, …)

mygame/                   Your game module
  includes/               Game-specific headers (events, battle tracking, etc.)
  mygame.h                Env struct + c_reset/c_step/c_render/c_close
  mygame.c                Standalone SDL player (optional)
  binding.c               Python binding — defines Env type, my_init, my_log
  mygame.py               PufferEnv subclass
  __init__.py             Package exports
```

## Key concepts

### The mGBA struct

Every environment embeds an `mGBA emu` field. This gives you:

| Field              | Description                              |
|--------------------|------------------------------------------|
| `emu.core`         | The mGBA core — call `runFrame`, `setKeys`, etc. |
| `emu.video_buffer` | Raw ARGB8888 pixel buffer                |
| `emu.rom_path`     | Path to the ROM file                     |
| `emu.state_path`   | Path to the save-state file              |
| `emu.frame_skip`   | Frames per agent step                    |
| `emu.render_enabled` | Whether SDL rendering is on            |

### Reading game memory

```c
#include "../mgba/mgba_wrapper.h"

uint8_t  val  = read_mem(&env->emu, 0xD362);     // 8-bit read
uint16_t val2 = read_uint16(&env->emu, 0xD347);  // 16-bit LE read
uint32_t bcd  = read_bcd(&env->emu, 0xD347);     // 3-byte BCD decode
```

### Stepping frames

```c
// Step N frames with the same key held (from optim.h)
uint32_t key = action_to_key(env->actions[0]);
STEP_N_FRAMES(env->emu.core, key, env->emu.frame_skip);
```

### Required C functions

`env_binding.h` expects these to exist (via macro hooks):

| Function            | Called by             |
|---------------------|-----------------------|
| `c_reset(Env*)`    | `vec_reset`           |
| `c_step(Env*)`     | `vec_step`            |
| `c_render(Env*)`   | `vec_render`          |
| `c_close(Env*)`    | `vec_close`           |
| `allocate(Env*)`   | Internal (from init)  |
| `free_allocated(Env*)` | `vec_close`       |

And in `binding.c`:

| Function                                         | Purpose             |
|--------------------------------------------------|---------------------|
| `my_init(Env*, PyObject* args, PyObject* kwargs)` | Initialize env     |
| `my_log(PyObject* dict, Log* log)`                | Export metrics      |

### The Log struct

**All fields must be `float`**. The generic `vec_log` aggregator iterates
over the struct as a flat `float[]` and sums across environments.

```c
typedef struct {
  float episode_length;
  float episode_return;
  float my_custom_metric;
  float n;                // always last — episode counter
} Log;
```

## Registering in the build system

### setup.py

Add your game module to `MGBA_GAME_MODULES`:

```python
MGBA_GAME_MODULES = ["pokered", "mygame"]
```

And add it to `packages`:

```python
packages = ['pufferlib', 'pufferlib.extensions', 'pokered', 'mygame']
```

### Makefile (optional)

Add build targets following the pokered pattern:

```makefile
MYGAME_DIR := mygame
MYGAME_BINDING_SO := $(shell ...)

mygame: $(MYGAME_BINDING_SO)

$(MYGAME_BINDING_SO): $(MYGAME_DIR)/binding.c $(MYGAME_DIR)/mygame.h $(MGBA_DIR)/mgba_wrapper.h
	DEBUG=$(DEBUG) $(PYTHON) setup.py build_c --inplace
```

## GBC vs GBA differences

| Property        | GB / GBC      | GBA           |
|-----------------|---------------|---------------|
| Screen size     | 160 × 144     | 240 × 160     |
| Address space   | 16-bit (64KB) | 32-bit (ROM + RAM banks) |
| ROM extension   | `.gb` / `.gbc`| `.gba`        |
| Input buttons   | Same 8-button layout | Same 8-button layout + L/R |

The mGBA wrapper handles both platforms transparently — `mCoreFind()`
auto-detects the ROM type, and `desiredVideoDimensions()` returns the
correct screen size. Just set `SCREEN_WIDTH` / `SCREEN_HEIGHT` in your
header to match.

> **Note on GBA L/R buttons**: The current `MGBAAction` enum covers 8
> buttons (same as GB). If your GBA game needs L/R, extend the enum in
> your game header or submit a PR to add them to `mgba_wrapper.h`.
