#include "pokered.h"

int calc_level_sum(CoreState *core) {
  int sum = 0;
  for (int i = 0; i < 6; i++)
    sum += core->levels[i];
  return sum;
}

int calc_event_sum(Emulator *emu, uint8_t *prev_events, bool verbose) {
  int sum = 0;
  for (size_t i = 0; i < EVENT_COUNT; ++i) {
    uint8_t value = read_mem(emu, EVENT_LIST[i].address);
    uint8_t completed = (value >> EVENT_LIST[i].bit) & 1;
    if (completed) {
      if (prev_events && !prev_events[i])
        printf("Event completed: %s\n", EVENT_LIST[i].name);
      sum++;
    }
    if (prev_events)
      prev_events[i] = completed;
  }
  return sum;
}

static float compute_battle_signal(PokemonRedEnv *env) {
  BattleState *curr = &env->gstate.battle;
  BattleState *prev = &env->gstate.prev_battle;
  Emulator *emu = &env->emu;
  float signal = 0.0f;

  float curr_php = party_hp_fraction(emu);

  // Penalize HP lost during active battle
  float hp_loss = env->prev_party_hp_frac - curr_php;
  if (hp_loss > 0.0f && is_battle_active(curr))
    signal -= hp_loss * 0.5f;

  if (is_battle_active(curr) && is_battle_active(prev) &&
      curr->enemy_maxhp > 0) {
    if (prev->enemy_hp > curr->enemy_hp) {
      float dmg_frac =
          (float)(prev->enemy_hp - curr->enemy_hp) / (float)curr->enemy_maxhp;
      signal += dmg_frac * 0.1f;

      uint8_t multiplier = read_mem(emu, DAMAGE_MULTIPLIERS_ADDR);
      if (multiplier > 10)
        signal += 0.1f;
    }
  }

  if (battle_just_ended(curr, prev)) {
    if (battle_was_lost(emu)) {
      signal -= 1.0f;
      memset(env->episode_visits, 0, VISITED_COORDS_SIZE);
      memset(env->chunk_episode_visits, 0, VISITED_CHUNKS_SIZE);
      if (env->verbose)
        printf("Battle lost (blackout) — episode visits cleared\n");
    } else if (battle_was_fled(emu)) {
      signal -= 0.1f;
      if (env->verbose)
        printf("Ran from battle\n");
    } else {
      signal += 0.2f;
      if (env->verbose)
        printf("Battle won!\n");
    }
  }

  if (curr->run_attempts > prev->run_attempts)
    signal -= 0.02f;

  env->prev_party_hp_frac = curr_php;
  return signal;
}

static float compute_exploration_signal(PokemonRedEnv *env) {
  int action = env->prev_action;
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;

  if (!is_directional_action(action) || is_battle_active(&env->gstate.battle))
    return 0.0f;

  if (core->x == prev->x && core->y == prev->y && core->map_n == prev->map_n)
    return 0.0f;

  uint32_t idx = core->idx;
  if (idx >= VISITED_COORDS_SIZE)
    return 0.0f;

  float signal = 0.0f;
  uint16_t global_count = env->exploration_heatmap[idx];

  if (global_count < UINT16_MAX)
    env->exploration_heatmap[idx] = global_count + 1;
  if (env->episode_visits[idx] < UINT8_MAX)
    env->episode_visits[idx]++;

  if (global_count == 0) {
    env->unique_coords_count++;
    signal += EXPLORE_TILE_NOVEL;
  }

  uint32_t cidx = chunk_index(core->map_n, core->x, core->y);
  if (cidx < VISITED_CHUNKS_SIZE) {
    uint16_t chunk_global = env->chunk_heatmap[cidx];
    uint8_t chunk_ep = env->chunk_episode_visits[cidx];

    if (chunk_global < UINT16_MAX)
      env->chunk_heatmap[cidx] = chunk_global + 1;
    if (chunk_ep < UINT8_MAX)
      env->chunk_episode_visits[cidx] = chunk_ep + 1;

    if (chunk_global == 0)
      signal += EXPLORE_CHUNK_NOVEL;
    else if (chunk_ep == 0)
      signal += EXPLORE_CHUNK_EPFRESH;
  }

  return signal;
}

static float compute_events_signal(PokemonRedEnv *env) {
  int event_sum = calc_event_sum(&env->emu, env->prev_events, env->verbose);
  float signal = (event_sum > env->prev_event_sum)
      ? (float)(event_sum - env->prev_event_sum)
      : 0.0f;
  env->prev_event_sum = event_sum;
  return signal;
}

static float compute_leveling_signal(PokemonRedEnv *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  int level_sum = calc_level_sum(core);
  int prev_level_sum = calc_level_sum(prev);
  int delta = level_sum - prev_level_sum;
  if (delta > 0 && core->party_count == prev->party_count) {
    if (env->verbose)
      printf("You have leveled up! New level sum: %d\n", level_sum);
    return (float)delta * 0.1f;
  }
  return 0.0f;
}

static float compute_milestones_signal(PokemonRedEnv *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  float signal = 0.0f;

  if (core->badges > prev->badges) {
    signal += 1.0f;
    if (env->verbose)
      printf("You beat a gym! Badge count: %d\n", core->badges);
  }
  if (core->party_count > prev->party_count && core->party_count <= 6) {
    signal += 0.2f;
    if (env->verbose)
      printf("You caught a new Pokemon! Party count: %d\n", core->party_count);
  }

  return signal;
}

float calculate_rewards(PokemonRedEnv *env) {
  PREFETCH_READ(&env->gstate);
  PREFETCH_READ(env->episode_visits);

  update_core_state(env);
  env->gstate.prev_battle = env->gstate.battle;
  update_battle_state(&env->gstate.battle, &env->emu);

  float s_explore = compute_exploration_signal(env);
  float s_battle = compute_battle_signal(env);
  float s_events = compute_events_signal(env);
  float s_leveling = compute_leveling_signal(env);
  float s_milestone = compute_milestones_signal(env);

  env->stats.total_explore_signal += s_explore;
  env->stats.total_battle_signal += s_battle;
  env->stats.total_events_signal += s_events;
  env->stats.total_leveling_signal += s_leveling;
  env->stats.total_milestone_signal += s_milestone;

  if (battle_just_ended(&env->gstate.battle, &env->gstate.prev_battle)) {
    if (battle_was_lost(&env->emu))
      env->stats.battles_lost++;
    else if (battle_was_fled(&env->emu))
      env->stats.battles_fled++;
    else
      env->stats.battles_won++;
  }
  if (env->gstate.battle.run_attempts > env->gstate.prev_battle.run_attempts)
    env->stats.run_attempts++;

  env->gstate.prev_core = env->gstate.core;

  return WEIGHT_EXPLORATION * s_explore + WEIGHT_BATTLE * s_battle +
         WEIGHT_EVENTS * s_events + WEIGHT_LEVELING * s_leveling +
         WEIGHT_MILESTONES * s_milestone;
}
