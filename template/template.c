#include "template.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_keyboard_action(bool *quit_requested) {
  if (quit_requested)
    *quit_requested = false;

  SDL_PumpEvents();

  SDL_Event evt;
  if (quit_requested &&
      SDL_PeepEvents(&evt, 1, SDL_PEEKEVENT, SDL_QUIT, SDL_QUIT) > 0) {
    *quit_requested = true;
    return GB_ACTION_NOOP;
  }

  const Uint8 *state = SDL_GetKeyboardState(NULL);
  if (!state)
    return GB_ACTION_NOOP;

  if (state[SDL_SCANCODE_ESCAPE]) {
    if (quit_requested)
      *quit_requested = true;
    return GB_ACTION_NOOP;
  }
  if (state[SDL_SCANCODE_RIGHT])  return GB_ACTION_RIGHT;
  if (state[SDL_SCANCODE_LEFT])   return GB_ACTION_LEFT;
  if (state[SDL_SCANCODE_UP])     return GB_ACTION_UP;
  if (state[SDL_SCANCODE_DOWN])   return GB_ACTION_DOWN;
  if (state[SDL_SCANCODE_Z] || state[SDL_SCANCODE_SPACE])
    return GB_ACTION_A;
  if (state[SDL_SCANCODE_X])      return GB_ACTION_B;
  if (state[SDL_SCANCODE_RETURN]) return GB_ACTION_START;
  if (state[SDL_SCANCODE_BACKSPACE] || state[SDL_SCANCODE_RSHIFT] ||
      state[SDL_SCANCODE_LSHIFT])
    return GB_ACTION_SELECT;

  return GB_ACTION_NOOP;
}

static int init_env(TemplateEnv *env, const char *rom_path,
                    const char *state_path) {
  env->emu.frame_skip = 1;
  env->max_episode_length = 20480;
  env->emu.render_enabled = true;
  env->full_reset = (state_path != NULL);

  if (state_path) {
    snprintf(env->emu.state_path, sizeof(env->emu.state_path), "%s",
             state_path);
  }
  snprintf(env->emu.rom_path, sizeof(env->emu.rom_path), "%s", rom_path);

  FILE *rom_file = fopen(rom_path, "rb");
  if (!rom_file) {
    printf("ROM file not found: %s\n", rom_path);
    return -1;
  }
  fclose(rom_file);

  mgba_init_core(&env->emu, rom_path);
  if (!env->emu.core) {
    printf("Failed to initialize mGBA core\n");
    return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <rom_path> [state_path]\n", argv[0]);
    return 1;
  }

  TemplateEnv env = {0};
  const char *state_path = (argc >= 3) ? argv[2] : NULL;
  if (init_env(&env, argv[1], state_path) != 0)
    return 1;

  allocate(&env);
  c_reset(&env);
  c_render(&env);

  bool running = true;
  while (running) {
    bool quit_requested = false;
    env.actions[0] = read_keyboard_action(&quit_requested);
    if (quit_requested)
      break;

    c_step(&env);
    c_render(&env);

    if (env.terminals[0] || env.truncations[0]) {
      printf("Episode finished (terminal=%u, truncation=%u)\n",
             env.terminals[0], env.truncations[0]);
      c_reset(&env);
    }

    if (!env.emu.render_enabled)
      running = false;

    SDL_Delay(1);
  }

  c_close(&env);
  free_allocated(&env);
  return 0;
}
