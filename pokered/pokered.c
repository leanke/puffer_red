#include "pokered.h"
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
  if (state[SDL_SCANCODE_RIGHT])
    return GB_ACTION_RIGHT;
  if (state[SDL_SCANCODE_LEFT])
    return GB_ACTION_LEFT;
  if (state[SDL_SCANCODE_UP])
    return GB_ACTION_UP;
  if (state[SDL_SCANCODE_DOWN])
    return GB_ACTION_DOWN;
  if (state[SDL_SCANCODE_Z] || state[SDL_SCANCODE_SPACE])
    return GB_ACTION_A;
  if (state[SDL_SCANCODE_X])
    return GB_ACTION_B;
  if (state[SDL_SCANCODE_RETURN])
    return GB_ACTION_START;
  if (state[SDL_SCANCODE_BACKSPACE] || state[SDL_SCANCODE_RSHIFT] ||
      state[SDL_SCANCODE_LSHIFT])
    return GB_ACTION_SELECT;

  return GB_ACTION_NOOP;
}
static int init_env(PokemonRedEnv *env) {
  env->emu.frame_skip = 1;
  env->max_episode_length = 20480;
  env->emu.render_enabled = true;
  env->full_reset = true;
  snprintf(env->emu.state_path, sizeof(env->emu.state_path),
           "./pokered/states/nballs.ss1");
  snprintf(env->emu.rom_path, sizeof(env->emu.rom_path),
           "./pokemon_red.gb");
  FILE *rom_file = fopen(env->emu.rom_path, "rb");
  if (!rom_file) {
    printf("ROM file not found: %s\n", env->emu.rom_path);
    return -1;
  }
  fclose(rom_file);

  mgba_init_core(&env->emu, env->emu.rom_path);
  env->visited_coords = (uint8_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint8_t));
  env->prev_visited_coords = (uint8_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint8_t));
  memset(env->prev_visited_coords, 1, VISITED_COORDS_SIZE);
  env->unique_coords_count = 0;

  if (!env->emu.core) {
    printf("Failed to initialize mGBA core\n");
    return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  PokemonRedEnv env = {0};
  init_env(&env);
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