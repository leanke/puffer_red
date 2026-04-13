#include "pokered.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool agent_view = false;

static int read_keyboard_action(bool *quit_requested, bool *toggle_view) {
  *quit_requested = false;
  *toggle_view = false;

  SDL_PumpEvents();

  SDL_Event evt;
  while (SDL_PeepEvents(&evt, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0)
    *quit_requested = true;

  while (SDL_PeepEvents(&evt, 1, SDL_GETEVENT, SDL_KEYDOWN, SDL_KEYDOWN) > 0) {
    if (evt.key.keysym.scancode == SDL_SCANCODE_TAB && !evt.key.repeat)
      *toggle_view = true;
  }

  const Uint8 *state = SDL_GetKeyboardState(NULL);
  if (!state)
    return MGBA_ACTION_NOOP;

  if (state[SDL_SCANCODE_ESCAPE]) {
    *quit_requested = true;
    return MGBA_ACTION_NOOP;
  }
  if (state[SDL_SCANCODE_RIGHT])  return MGBA_ACTION_RIGHT;
  if (state[SDL_SCANCODE_LEFT])   return MGBA_ACTION_LEFT;
  if (state[SDL_SCANCODE_UP])     return MGBA_ACTION_UP;
  if (state[SDL_SCANCODE_DOWN])   return MGBA_ACTION_DOWN;
  if (state[SDL_SCANCODE_Z] || state[SDL_SCANCODE_SPACE])
    return MGBA_ACTION_A;
  if (state[SDL_SCANCODE_X])      return MGBA_ACTION_B;
  if (state[SDL_SCANCODE_RETURN]) return MGBA_ACTION_START;
  if (state[SDL_SCANCODE_BACKSPACE] || state[SDL_SCANCODE_RSHIFT] ||
      state[SDL_SCANCODE_LSHIFT])
    return MGBA_ACTION_SELECT;

  return MGBA_ACTION_NOOP;
}

static void render_agent_view(PokemonRedEnv *env) {
  if (!env->emu.renderer || !env->emu.texture || !env->observations)
    return;

  uint32_t pixels[SCALED_HEIGHT * SCALED_WIDTH];
  for (int i = 0; i < SCALED_HEIGHT * SCALED_WIDTH; i++) {
    uint8_t g = (uint8_t)(env->observations[i] > 255.0f ? 255 : env->observations[i]);
    pixels[i] = 0xFF000000 | (g << 16) | (g << 8) | g;
  }

  SDL_Texture *agent_tex = SDL_CreateTexture(
      env->emu.renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, SCALED_WIDTH, SCALED_HEIGHT);
  if (!agent_tex)
    return;

  SDL_UpdateTexture(agent_tex, NULL, pixels, SCALED_WIDTH * sizeof(uint32_t));
  SDL_SetRenderDrawColor(env->emu.renderer, 0, 0, 0, 255);
  SDL_RenderClear(env->emu.renderer);
  SDL_RenderCopy(env->emu.renderer, agent_tex, NULL, NULL);
  SDL_RenderPresent(env->emu.renderer);
  SDL_DestroyTexture(agent_tex);
}

static void print_step_info(PokemonRedEnv *env) {
  CoreState *core = &env->gstate.core;
  BattleState *battle = &env->gstate.battle;

  printf("\033[2J\033[H");
  printf("=== Pokemon Red Debug Viewer ===\n");
  printf("Step: %d/%d  |  Score: %.2f  |  View: %s (Tab to toggle)\n\n",
         env->step_count, env->max_episode_length,
         env->score, agent_view ? "AGENT" : "GAME");

  printf("--- State ---\n");
  printf("  Map: %3d  Pos: (%d, %d)  Facing: %d\n",
         core->map_n, core->x, core->y,
         read_mem(&env->emu, PKMN_FACING_ADDR) / 4);
  printf("  Badges: %d  Party: %d  Money: $%d\n",
         core->badges, core->party_count,
         read_bcd(&env->emu, PKMN_MONEY_ADDR));
  printf("  In Battle: %s\n",
         battle->in_battle == 0 ? "No" :
         battle->in_battle == 1 ? "Wild" :
         battle->in_battle == 2 ? "Trainer" : "Lost");
  if (is_battle_active(battle)) {
    printf("  Enemy HP: %d/%d (%.0f%%)\n",
           battle->enemy_hp, battle->enemy_maxhp,
           battle->enemy_maxhp > 0
               ? (float)battle->enemy_hp / (float)battle->enemy_maxhp * 100.0f
               : 0.0f);
  }
  printf("  Party HP: %.0f%%\n", party_hp_fraction(&env->emu) * 100.0f);

  printf("\n--- Levels ---\n");
  printf("  Pkmn1: %d  Pkmn2: %d  Pkmn3: %d\n",
         core->levels[0], core->levels[1], core->levels[2]);
  printf("  Pkmn4: %d  Pkmn5: %d  Pkmn6: %d  (Sum: %d)\n",
         core->levels[3], core->levels[4], core->levels[5], calc_level_sum(core));

  printf("\n--- Reward (this step) ---\n");
  printf("  Step reward: %+.4f\n", env->rewards[0]);

  printf("\n--- Episode Totals ---\n");
  printf("  Explore:   %+8.2f  (×%.2f = %+.2f)\n",
         env->stats.total_explore_signal, WEIGHT_EXPLORATION,
         env->stats.total_explore_signal * WEIGHT_EXPLORATION);
  printf("  Battle:    %+8.2f  (×%.2f = %+.2f)\n",
         env->stats.total_battle_signal, WEIGHT_BATTLE,
         env->stats.total_battle_signal * WEIGHT_BATTLE);
  printf("  Events:    %+8.2f  (×%.2f = %+.2f)\n",
         env->stats.total_events_signal, WEIGHT_EVENTS,
         env->stats.total_events_signal * WEIGHT_EVENTS);
  printf("  Leveling:  %+8.2f  (×%.2f = %+.2f)\n",
         env->stats.total_leveling_signal, WEIGHT_LEVELING,
         env->stats.total_leveling_signal * WEIGHT_LEVELING);
  printf("  Milestone: %+8.2f  (×%.2f = %+.2f)\n",
         env->stats.total_milestone_signal, WEIGHT_MILESTONES,
         env->stats.total_milestone_signal * WEIGHT_MILESTONES);

  printf("\n--- Battle Stats ---\n");
  printf("  Won: %d  Lost: %d  Steps in battle: %d\n",
         env->stats.battles_won, env->stats.battles_lost, env->stats.battle_steps);

  printf("\n--- Exploration ---\n");
  printf("  Unique coords (episode): %d  Events: %d\n",
         env->unique_coords_count, env->prev_event_sum);
}

static int init_env(PokemonRedEnv *env, const char *state_path) {
  env->emu.frame_skip = 1;
  env->max_episode_length = 20480;
  env->emu.render_enabled = true;
  env->full_reset = true;
  snprintf(env->emu.state_path, sizeof(env->emu.state_path), "%s", state_path);
  snprintf(env->emu.rom_path, sizeof(env->emu.rom_path), "./pokemon_red.gb");

  FILE *rom_file = fopen(env->emu.rom_path, "rb");
  if (!rom_file) {
    printf("ROM file not found: %s\n", env->emu.rom_path);
    return -1;
  }
  fclose(rom_file);

  mgba_init_core(&env->emu, env->emu.rom_path);
  if (!env->emu.core) {
    printf("Failed to initialize mGBA core\n");
    return -1;
  }

  env->visited_coords = (uint8_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint8_t));
  env->exploration_heatmap = (uint16_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint16_t));
  env->prev_events = (uint8_t *)calloc(EVENT_COUNT, sizeof(uint8_t));
  env->unique_coords_count = 0;

  return 0;
}

int main(int argc, char **argv) {
  const char *state_path = (argc > 1) ? argv[1] : "./pokered/states/new_start.ss1";

  PokemonRedEnv env = {0};
  if (init_env(&env, state_path) != 0)
    return 1;
  allocate(&env);
  c_reset(&env);
  c_render(&env);

  printf("Controls: Arrow keys=Move, Z/Space=A, X=B, Enter=Start, "
         "Shift=Select, Tab=Agent view, Esc=Quit\n");

  bool running = true;
  while (running) {
    bool quit_requested = false;
    bool toggle_view = false;
    env.actions[0] = read_keyboard_action(&quit_requested, &toggle_view);
    if (quit_requested)
      break;
    if (toggle_view)
      agent_view = !agent_view;

    c_step(&env);

    if (agent_view)
      render_agent_view(&env);
    else
      c_render(&env);

    print_step_info(&env);

    if (env.terminals[0] || env.truncations[0]) {
      printf("\n*** Episode finished (terminal=%u, truncation=%u) ***\n",
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