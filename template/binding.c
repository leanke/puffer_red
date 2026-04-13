#include "../mgba/mgba_wrapper.h"
#include "template.h"
#include <Python.h>

#define Env TemplateEnv

#include "../mgba/env_binding.h"

static int my_init(Env *env, PyObject *args, PyObject *kwargs) {
  const char *rom_path = NULL;

  env->emu.frame_skip = unpack(kwargs, "frameskip");
  env->max_episode_length = unpack(kwargs, "max_episode_length");
  env->emu.render_enabled = !unpack(kwargs, "headless");
  env->full_reset = unpack(kwargs, "full_reset");

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
  if (!env->emu.core) {
    PyErr_SetString(PyExc_RuntimeError, "Failed to initialize mGBA core");
    return -1;
  }

  return 0;
}

static int my_log(PyObject *dict, Log *log) {
  assign_to_dict(dict, "episode_length", log->episode_length);
  assign_to_dict(dict, "episode_return", log->episode_return);
  assign_to_dict(dict, "n", log->n);
  return 0;
}
