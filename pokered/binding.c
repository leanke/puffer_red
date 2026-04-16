#include "../mgba/mgba_wrapper.h"
#include "pokered.h"
#include <Python.h>

#define Env PokemonRedEnv

static PyObject *vec_get_positions(PyObject *self, PyObject *args);

#define MY_METHODS                                                             \
  {"vec_get_positions", vec_get_positions, METH_VARARGS,                       \
   "Get positions of all envs"}

#include "../mgba/env_binding.h"

static PyObject *vec_get_positions(PyObject *self, PyObject *args) {
  VecEnv *vec = unpack_vecenv(args);
  if (!vec)
    return NULL;

  PyObject *list = PyList_New(vec->num_envs);
  for (int i = 0; i < vec->num_envs; i++) {
    Env *env = vec->envs[i];

    PyObject *pos = Py_BuildValue("(iii)", env->gstate.core.x,
                                  env->gstate.core.y, env->gstate.core.map_n);
    PyList_SetItem(list, i, pos);
  }
  return list;
}

static int my_init(Env *env, PyObject *args, PyObject *kwargs) {
  const char *rom_path = NULL;
  env->emu.frame_skip = unpack(kwargs, "frameskip");
  env->emu.press_frames = unpack(kwargs, "press_frames");
  env->max_episode_length = unpack(kwargs, "max_episode_length");
  env->emu.render_enabled = !unpack(kwargs, "headless");
  env->full_reset = unpack(kwargs, "full_reset");
  env->disable_wild_until_badge = unpack(kwargs, "disable_wild_until_badge");
  env->verbose = unpack(kwargs, "verbose");

  PyObject *state_path_obj = PyDict_GetItemString(kwargs, "state_path");
  if (state_path_obj && state_path_obj != Py_None) {
    const char *state_path = PyUnicode_AsUTF8(state_path_obj);
    strncpy(env->emu.state_path, state_path, sizeof(env->emu.state_path) - 1);
  }

  PyObject *rom_path_obj = PyDict_GetItemString(kwargs, "rom_path");
  if (rom_path_obj && rom_path_obj != Py_None) {
    rom_path = PyUnicode_AsUTF8(rom_path_obj);
  }
  if (!rom_path) {
    PyErr_SetString(PyExc_ValueError, "rom_path is required");
    return -1;
  }
  strncpy(env->emu.rom_path, rom_path, sizeof(env->emu.rom_path) - 1);
  FILE *rom_file = fopen(rom_path, "rb");
  if (!rom_file) {
    PyErr_Format(PyExc_FileNotFoundError, "ROM file not found: %s", rom_path);
    return -1;
  }
  fclose(rom_file);

  mgba_init_core(&env->emu, rom_path);

  // Tell mGBA PPU to skip rendering all but 1 frame per action step
  if (env->emu.core && env->emu.frame_skip > 1) {
    char fs_str[16];
    snprintf(fs_str, sizeof(fs_str), "%d", env->emu.frame_skip - 1);
    mCoreConfigSetValue(&env->emu.core->config, "frameskip", fs_str);
    env->emu.core->reloadConfigOption(env->emu.core, "frameskip", NULL);
  }

  env->episode_visits = (uint8_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint8_t));
  env->exploration_heatmap =
      (uint16_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint16_t));
  env->unique_coords_count = 0;
  env->prev_events = (uint8_t *)calloc(EVENT_COUNT, sizeof(uint8_t));
  memset(env->prev_events, 0, EVENT_COUNT);
  env->prev_action = MGBA_ACTION_NOOP;

  PyObject *decay_obj = PyDict_GetItemString(kwargs, "heatmap_decay");
  if (decay_obj && decay_obj != Py_None) {
    env->heatmap_decay = (float)PyFloat_AsDouble(decay_obj);
  } else {
    env->heatmap_decay = 0.0f;
  }
  env->heatmap_decay_interval = env->max_episode_length / 1;
  if (!env->emu.core) {
    PyErr_SetString(PyExc_RuntimeError, "Failed to initialize mGBA core");
    return -1;
  }
  initial_load_state(&env->emu, env->emu.state_path);

  return 0;
}

static int my_log(PyObject *dict, Log *log) {
  assign_to_dict(dict, "episode_length", log->episode_length);
  assign_to_dict(dict, "episode_return", log->episode_return);
  assign_to_dict(dict, "money", log->money);
  assign_to_dict(dict, "level_sum", log->level_sum);
  assign_to_dict(dict, "pkmn1_lvl", log->pkmn1_lvl);
  assign_to_dict(dict, "pkmn2_lvl", log->pkmn2_lvl);
  assign_to_dict(dict, "pkmn3_lvl", log->pkmn3_lvl);
  assign_to_dict(dict, "pkmn4_lvl", log->pkmn4_lvl);
  assign_to_dict(dict, "pkmn5_lvl", log->pkmn5_lvl);
  assign_to_dict(dict, "pkmn6_lvl", log->pkmn6_lvl);
  assign_to_dict(dict, "party_count", log->party_count);
  assign_to_dict(dict, "party_hp", log->party_hp);
  assign_to_dict(dict, "badges", log->badges);
  assign_to_dict(dict, "event_sum", log->event_sum);
  assign_to_dict(dict, "unique_coords", log->unique_coords);
  assign_to_dict(dict, "map_n", log->map_n);
  assign_to_dict(dict, "battles_won", log->battles_won);
  assign_to_dict(dict, "battles_lost", log->battles_lost);
  assign_to_dict(dict, "battle_steps", log->battle_steps);
  assign_to_dict(dict, "run_attempts", log->run_attempts);
  assign_to_dict(dict, "battles_fled", log->battles_fled);
  assign_to_dict(dict, "explore_signal", log->explore_signal);
  assign_to_dict(dict, "battle_signal", log->battle_signal);
  assign_to_dict(dict, "events_signal", log->events_signal);
  assign_to_dict(dict, "leveling_signal", log->leveling_signal);
  assign_to_dict(dict, "milestone_signal", log->milestone_signal);
  return 0;
}
