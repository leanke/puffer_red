#include "pokered.h"

int calc_level_sum(CoreState *core) {
  int sum = 0;
  for (int i = 0; i < 6; i++)
    sum += core->levels[i];
  return sum;
}

int calc_event_sum(mGBA *emu, uint8_t *prev_events) {
  int sum = 0;
  for (size_t i = 0; i < EVENT_COUNT; ++i) {
    uint8_t value = read_mem(emu, EVENT_LIST[i].address);
    uint8_t completed = (value >> EVENT_LIST[i].bit) & 1;
    if (completed) {
      if (prev_events && !prev_events[i]) {
        printf("Event completed: %s\n", EVENT_LIST[i].name);
      }
      sum++;
    }
    if (prev_events) {
      prev_events[i] = completed;
    }
  }
  return sum;
}

static float compute_battle_signal(PokemonRedEnv *env) {
  BattleState *curr = &env->gstate.battle;
  BattleState *prev = &env->gstate.prev_battle;
  mGBA *emu = &env->emu;
  float signal = 0.0f;

  float curr_php = party_hp_fraction(emu);

  if (is_battle_active(curr) && is_battle_active(prev) &&
      curr->enemy_maxhp > 0) {
    if (prev->enemy_hp > curr->enemy_hp) {
      float dmg_frac =
          (float)(prev->enemy_hp - curr->enemy_hp) / (float)curr->enemy_maxhp;
      signal += dmg_frac;

      uint8_t multiplier = read_mem(emu, DAMAGE_MULTIPLIERS_ADDR);
      if (multiplier > 10)
        signal += 0.1f;
    }
  }

  if (battle_just_ended(curr, prev)) {
    if (battle_was_lost(emu))
      printf("Battle lost (blackout)\n");
    else {
      signal += 0.3f;
      printf("Battle won!\n");
    }
  }

  if (curr->run_attempts > prev->run_attempts)
    signal -= 0.001f;

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
  if (env->visited_coords[idx])
    return 0.0f;

  uint16_t count = env->exploration_heatmap[idx];
  float novelty_bonus = 0.01f / (1.0f + (float)count);
  return 0.02f + novelty_bonus;
}

static float compute_events_signal(PokemonRedEnv *env) {
  int event_sum = calc_event_sum(&env->emu, env->prev_events);
  float signal = (event_sum > env->prev_event_sum) ? 1.0f : 0.0f;
  env->prev_event_sum = event_sum;
  return signal;
}

static float compute_leveling_signal(PokemonRedEnv *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  int level_sum = calc_level_sum(core);
  int prev_level_sum = calc_level_sum(prev);
  if (level_sum > prev_level_sum && core->party_count == prev->party_count) {
    printf("You have leveled up! New level sum: %d\n", level_sum);
    return 1.0f;
  }
  return 0.0f;
}

static float compute_milestones_signal(PokemonRedEnv *env) {
  CoreState *core = &env->gstate.core;
  CoreState *prev = &env->gstate.prev_core;
  float signal = 0.0f;

  if (core->badges > prev->badges) {
    signal += 1.0f;
    printf("You beat a gym! Badge count: %d\n", core->badges);
  }
  if (core->party_count > prev->party_count && core->party_count <= 6) {
    signal += 0.2f;
    printf("You caught a new Pokemon! Party count: %d\n", core->party_count);
  }

  if (signal > 1.0f)
    signal = 1.0f;
  return signal;
}

float calculate_rewards(PokemonRedEnv *env) {
  PREFETCH_READ(&env->gstate);
  PREFETCH_READ(env->visited_coords);

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
    else
      env->stats.battles_won++;
  }
  if (env->gstate.battle.run_attempts > env->gstate.prev_battle.run_attempts)
    env->stats.run_attempts++;

  uint32_t idx = env->gstate.core.idx;
  PREFETCH_WRITE(env->exploration_heatmap);
  if (idx < VISITED_COORDS_SIZE && env->exploration_heatmap[idx] < UINT16_MAX)
    env->exploration_heatmap[idx]++;

  if (!is_coord_visited(env)) {
    mark_coord_visited(env);
    env->unique_coords_count++;
  }

  env->gstate.prev_core = env->gstate.core;

  return WEIGHT_EXPLORATION * s_explore + WEIGHT_BATTLE * s_battle +
         WEIGHT_EVENTS * s_events + WEIGHT_LEVELING * s_leveling +
         WEIGHT_MILESTONES * s_milestone;
}
