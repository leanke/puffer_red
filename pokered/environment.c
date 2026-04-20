#include "pokered.h"

void allocate(PokemonRedEnv *env) {
  env->observations = (float *)calloc(TOTAL_OBSERVATIONS, sizeof(float));
  env->actions = (int *)calloc(1, sizeof(int));
  env->rewards = (float *)calloc(1, sizeof(float));
  env->terminals = (unsigned char *)calloc(1, sizeof(unsigned char));
  env->truncations = (unsigned char *)calloc(1, sizeof(unsigned char));
}

void free_allocated(PokemonRedEnv *env) {
  free(env->observations);
  free(env->actions);
  free(env->rewards);
  free(env->terminals);
  free(env->truncations);
  free(env->episode_visits);
  free(env->exploration_heatmap);
  free(env->prev_events);
}

void add_log(PokemonRedEnv *env) {
  CoreState *core = &env->gstate.core;

  env->log.episode_length = env->step_count;
  env->log.episode_return = env->score;
  env->log.money = read_bcd(&env->emu, PKMN_MONEY_ADDR);

  env->log.level_sum = calc_level_sum(core);
  for (int i = 0; i < 6; i++)
    (&env->log.pkmn1_lvl)[i] = core->levels[i];
  env->log.party_count = core->party_count;
  env->log.party_hp = party_hp_fraction(&env->emu);

  env->log.badges = core->badges;
  env->log.event_sum = env->prev_event_sum;
  env->log.unique_coords = env->unique_coords_count;
  env->log.map_n = core->map_n;

  env->log.battles_won = env->stats.battles_won;
  env->log.battles_lost = env->stats.battles_lost;
  env->log.battles_fled = env->stats.battles_fled;
  env->log.battle_steps = env->stats.battle_steps;
  env->log.run_attempts = env->stats.run_attempts;

  env->log.explore_signal = env->stats.total_explore_signal;
  env->log.battle_signal = env->stats.total_battle_signal;
  env->log.events_signal = env->stats.total_events_signal;
  env->log.leveling_signal = env->stats.total_leveling_signal;
  env->log.milestone_signal = env->stats.total_milestone_signal;
  env->log.n++;
}

void c_reset(PokemonRedEnv *env) {
  if (!env || !env->emu.gb)
    return;
  if (env->full_reset) {
    initial_load_state(&env->emu, env->emu.state_path);
  }
  update_core_state(env);

  env->gstate.prev_core = env->gstate.core;
  update_battle_state(&env->gstate.battle, &env->emu);
  env->gstate.prev_battle = env->gstate.battle;
  update_observations(env);
  memset(env->episode_visits, 0, VISITED_COORDS_SIZE);
  env->unique_coords_count = 0;

  env->rewards[0] = 0;
  env->terminals[0] = 0;
  env->step_count = env->frame_count = 0;
  env->score = 0.0f;
  env->prev_action = GB_ACTION_NOOP;
  env->prev_event_sum = calc_event_sum(&env->emu, NULL, false);
  env->prev_party_hp_frac = party_hp_fraction(&env->emu);
  env->game_mode = detect_game_mode(&env->emu);
  memset(&env->stats, 0, sizeof(EpisodeStats));
  for (size_t i = 0; i < EVENT_COUNT; ++i) {
    uint8_t value = read_mem(&env->emu, EVENT_LIST[i].address);
    env->prev_events[i] = (value >> EVENT_LIST[i].bit) & 1;
  }

  for (int i = 0; i < 4; i++)
    gambatte_run_frame(env->emu.gb, env->emu.video_buffer);
}

void c_step(PokemonRedEnv *env) {
  if (!env || !env->emu.gb)
    return;
  env->rewards[0] = 0;
  env->terminals[0] = 0;
  env->step_count++;

  if (env->disable_wild_until_badge) {
    uint8_t flags = read_mem(&env->emu, WD72E_ADDR);
    if (env->gstate.core.badges == 0) {
      write_mem(&env->emu, WD72E_ADDR,
                flags | (1 << WD72E_DISABLE_BATTLES_BIT));
    } else {
      write_mem(&env->emu, WD72E_ADDR,
                flags & ~(1 << WD72E_DISABLE_BATTLES_BIT));
    }
  }

  if (env->gstate.core.map_n == VIRIDIAN_CITY_MAP &&
      read_mem(&env->emu, VIRIDIAN_SCRIPT_ADDR) == 1) {
    write_mem(&env->emu, VIRIDIAN_SCRIPT_ADDR, 0);
    write_mem(&env->emu, BATTLE_TYPE_ADDR, 0);
  }

  int skip = env->emu.frame_skip > 0 ? env->emu.frame_skip : 24;
  int press = env->emu.press_frames > 0 ? env->emu.press_frames : 8;
  env->prev_action = env->actions[0];
  uint32_t action_key = action_to_key(env->actions[0]);
  STEP_ACTION_FRAMES(env->emu.gb, action_key, env->emu.video_buffer, press, skip);
  env->frame_count += skip;

  float reward = calculate_rewards(env);
  env->game_mode = detect_game_mode(&env->emu);
  update_observations(env);
  env->rewards[0] = reward;
  env->score += reward;

  if (env->game_mode == GAME_MODE_BATTLE)
    env->stats.battle_steps++;

  if (env->heatmap_decay > 0.0f && env->heatmap_decay_interval > 0 &&
      env->step_count % env->heatmap_decay_interval == 0) {
    float keep = 1.0f - env->heatmap_decay;
    for (size_t i = 0; i < VISITED_COORDS_SIZE; i++) {
      env->exploration_heatmap[i] =
          (uint16_t)((float)env->exploration_heatmap[i] * keep);
    }
  }

  if (env->step_count >= env->max_episode_length) {
    env->terminals[0] = 1;
    add_log(env);
    c_reset(env);
  }
}

void c_render(PokemonRedEnv *env) { gb_render_frame(&env->emu); }

void c_close(PokemonRedEnv *env) {
  if (!env)
    return;

  gb_destroy_renderer(&env->emu);

  if (env->emu.gb) {
    gambatte_destroy(env->emu.gb);
    env->emu.gb = NULL;
  }

  if (env->emu.uses_shared_rom) {
    release_shared_rom();
    env->emu.uses_shared_rom = false;
  }

  if (env->emu.video_buffer) {
    free(env->emu.video_buffer);
    env->emu.video_buffer = NULL;
  }
}
