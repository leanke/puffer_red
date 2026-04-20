#include "pokered.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool agent_view = false;

static int read_keyboard_action(bool *quit_requested, bool *toggle_view,
                                bool *save_requested) {
  *quit_requested = false;
  *toggle_view = false;
  *save_requested = false;

  SDL_PumpEvents();

  SDL_Event evt;
  while (SDL_PeepEvents(&evt, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0)
    *quit_requested = true;

  while (SDL_PeepEvents(&evt, 1, SDL_GETEVENT, SDL_KEYDOWN, SDL_KEYDOWN) > 0) {
    if (evt.key.keysym.scancode == SDL_SCANCODE_TAB && !evt.key.repeat)
      *toggle_view = true;
    if (evt.key.keysym.scancode == SDL_SCANCODE_S && !evt.key.repeat)
      *save_requested = true;
  }

  const Uint8 *state = SDL_GetKeyboardState(NULL);
  if (!state)
    return GB_ACTION_NOOP;

  if (state[SDL_SCANCODE_ESCAPE]) {
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

static void render_agent_view(PokemonRedEnv *env) {
  if (!env->emu.renderer || !env->observations)
    return;

  static SDL_Texture *agent_tex = NULL;
  static SDL_Renderer *cached_renderer = NULL;

  if (cached_renderer != env->emu.renderer || !agent_tex) {
    if (agent_tex)
      SDL_DestroyTexture(agent_tex);
    agent_tex = SDL_CreateTexture(env->emu.renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, SCALED_WIDTH,
                                  SCALED_HEIGHT);
    cached_renderer = env->emu.renderer;
    if (!agent_tex)
      return;
  }

  float *obs = env->observations;

  // Grayscale pixels from obs[0:SCALED_PIXELS]
  uint32_t pixels[SCALED_HEIGHT * SCALED_WIDTH];
  for (int i = 0; i < SCALED_HEIGHT * SCALED_WIDTH; i++) {
    uint8_t g = (uint8_t)(obs[i] > 255.0f ? 255 : obs[i]);
    pixels[i] = 0xFF000000 | (g << 16) | (g << 8) | g;
  }

  // Heatmap tile overlay: color-code the 10×9 grid from
  // obs[SCALED_PIXELS+SCALAR_OBS]
  int h = SCALED_PIXELS + SCALAR_OBS;
  for (int ty = 0; ty < SCREEN_TILES_Y; ty++) {
    for (int tx = 0; tx < SCREEN_TILES_X; tx++) {
      float visits = obs[h + ty * SCREEN_TILES_X + tx];
      if (visits <= 0.0f)
        continue;

      // Log-scale color: green → yellow → red
      uint8_t r, g, b;
      if (visits < 5.0f) {
        r = 0;
        g = 180;
        b = 0;
      } else if (visits < 17.0f) {
        r = 180;
        g = 180;
        b = 0;
      } else {
        r = 200;
        g = 0;
        b = 0;
      }

      int px0 = tx * METATILE_PX;
      int py0 = ty * METATILE_PX;
      for (int py = py0; py < py0 + METATILE_PX && py < SCALED_HEIGHT; py++) {
        for (int px = px0; px < px0 + METATILE_PX && px < SCALED_WIDTH; px++) {
          int idx = py * SCALED_WIDTH + px;
          uint8_t base = pixels[idx] & 0xFF;
          // 30% tint blend
          uint8_t fr = (uint8_t)(base * 0.7f + r * 0.3f);
          uint8_t fg = (uint8_t)(base * 0.7f + g * 0.3f);
          uint8_t fb = (uint8_t)(base * 0.7f + b * 0.3f);
          pixels[idx] = 0xFF000000 | (fr << 16) | (fg << 8) | fb;
        }
      }
    }
  }

  // Mode indicator border: green=general, red=battle
  uint8_t mode_r = 0, mode_g = 0, mode_b = 0;
  float mode_general = obs[SCALED_PIXELS + 30];
  if (mode_general > 0.5f) {
    mode_r = 0;
    mode_g = 200;
    mode_b = 0;
  } else {
    mode_r = 200;
    mode_g = 0;
    mode_b = 0;
  }
  // Top and bottom border (2px)
  for (int x = 0; x < SCALED_WIDTH; x++) {
    for (int t = 0; t < 2; t++) {
      pixels[t * SCALED_WIDTH + x] =
          0xFF000000 | (mode_r << 16) | (mode_g << 8) | mode_b;
      pixels[(SCALED_HEIGHT - 1 - t) * SCALED_WIDTH + x] =
          0xFF000000 | (mode_r << 16) | (mode_g << 8) | mode_b;
    }
  }
  // Left and right border (2px)
  for (int y = 0; y < SCALED_HEIGHT; y++) {
    for (int t = 0; t < 2; t++) {
      pixels[y * SCALED_WIDTH + t] =
          0xFF000000 | (mode_r << 16) | (mode_g << 8) | mode_b;
      pixels[y * SCALED_WIDTH + (SCALED_WIDTH - 1 - t)] =
          0xFF000000 | (mode_r << 16) | (mode_g << 8) | mode_b;
    }
  }

  SDL_UpdateTexture(agent_tex, NULL, pixels, SCALED_WIDTH * sizeof(uint32_t));
  SDL_SetRenderDrawColor(env->emu.renderer, 0, 0, 0, 255);
  SDL_RenderClear(env->emu.renderer);
  SDL_RenderCopy(env->emu.renderer, agent_tex, NULL, NULL);
  SDL_RenderPresent(env->emu.renderer);
}

static void print_step_info(PokemonRedEnv *env) {
  CoreState *core = &env->gstate.core;
  BattleState *battle = &env->gstate.battle;

  printf("\033[2J\033[H");
  printf("=== Pokemon Red Debug Viewer ===\n");
  printf("Step: %d/%d  |  Score: %.2f  |  View: %s (Tab to toggle)\n\n",
         env->step_count, env->max_episode_length, env->score,
         agent_view ? "AGENT" : "GAME");

  printf("--- State ---\n");
  printf("  Map: %3d  Pos: (%d, %d)  Facing: %d\n", core->map_n, core->x,
         core->y, read_mem(&env->emu, PKMN_FACING_ADDR) / 4);
  printf("  Badges: %d  Party: %d  Money: $%d\n", core->badges,
         core->party_count, read_bcd(&env->emu, PKMN_MONEY_ADDR));
  printf("  In Battle: %s\n", battle->in_battle == 0   ? "No"
                              : battle->in_battle == 1 ? "Wild"
                              : battle->in_battle == 2 ? "Trainer"
                                                       : "Lost");
  if (is_battle_active(battle)) {
    printf("  Enemy HP: %d/%d (%.0f%%)\n", battle->enemy_hp,
           battle->enemy_maxhp,
           battle->enemy_maxhp > 0
               ? (float)battle->enemy_hp / (float)battle->enemy_maxhp * 100.0f
               : 0.0f);
  }
  printf("  Party HP: %.0f%%\n", party_hp_fraction(&env->emu) * 100.0f);

  printf("\n--- Levels ---\n");
  printf("  Pkmn1: %d  Pkmn2: %d  Pkmn3: %d\n", core->levels[0],
         core->levels[1], core->levels[2]);
  printf("  Pkmn4: %d  Pkmn5: %d  Pkmn6: %d  (Sum: %d)\n", core->levels[3],
         core->levels[4], core->levels[5], calc_level_sum(core));

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
  printf("  Won: %d  Lost: %d  Steps in battle: %d\n", env->stats.battles_won,
         env->stats.battles_lost, env->stats.battle_steps);

  printf("\n--- Exploration ---\n");
  printf("  Unique coords (episode): %d  Events: %d\n",
         env->unique_coords_count, env->prev_event_sum);
}

static int init_env(PokemonRedEnv *env, const char *state_path) {
  env->emu.frame_skip = 16;
  env->emu.press_frames = 8;
  env->max_episode_length = 20480;
  env->emu.render_enabled = true;
  env->full_reset = true;
  env->verbose = true;
  snprintf(env->emu.state_path, sizeof(env->emu.state_path), "%s", state_path);
  snprintf(env->emu.rom_path, sizeof(env->emu.rom_path), "./pokemon_red.gb");

  FILE *rom_file = fopen(env->emu.rom_path, "rb");
  if (!rom_file) {
    printf("ROM file not found: %s\n", env->emu.rom_path);
    return -1;
  }
  fclose(rom_file);

  gb_init_core(&env->emu, env->emu.rom_path);
  if (!env->emu.gb) {
    printf("Failed to initialize Gambatte core\n");
    return -1;
  }

  env->episode_visits = (uint8_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint8_t));
  env->exploration_heatmap =
      (uint16_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint16_t));
  env->prev_events = (uint8_t *)calloc(EVENT_COUNT, sizeof(uint8_t));
  env->unique_coords_count = 0;

  return 0;
}

int main(int argc, char **argv) {
  const char *state_path =
      (argc > 1) ? argv[1] : "./pokered/states/bulba_start";

  PokemonRedEnv env = {0};
  if (init_env(&env, state_path) != 0)
    return 1;
  allocate(&env);
  c_reset(&env);
  c_render(&env);

  printf("Controls: Arrow keys=Move, Z/Space=A, X=B, Enter=Start, "
         "Shift=Select, Tab=Agent view, S=Save state, Esc=Quit\n");

  bool running = true;
  while (running) {
    bool quit_requested = false;
    bool toggle_view = false;
    bool save_requested = false;
    env.actions[0] =
        read_keyboard_action(&quit_requested, &toggle_view, &save_requested);
    if (quit_requested)
      break;
    if (toggle_view)
      agent_view = !agent_view;
    if (save_requested) {
      if (c_save_state_file(&env.emu, "play_save_state"))
        printf("State saved to play_save_state\n");
      else
        printf("Failed to save state\n");
    }

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
