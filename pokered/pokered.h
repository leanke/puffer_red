#ifndef POKEMONREDENV_H
#define POKEMONREDENV_H

#include "../mgba/mgba_wrapper.h"
#include "./includes/battle.h"
#include "./includes/events.h"

#define SCREEN_WIDTH 160
#define SCALED_WIDTH 80
#define SCALED_HEIGHT 72
#define SCALED_PIXELS (SCALED_WIDTH * SCALED_HEIGHT)

#define METATILE_PX 8
#define SCREEN_TILES_X 10
#define SCREEN_TILES_Y 9
#define PLAYER_TILE_X 4
#define PLAYER_TILE_Y 4
#define HEATMAP_BRIGHTNESS 8.0f
#define HEATMAP_MAX_BRIGHT 100.0f

#define SCALAR_OBS 32
#define HEATMAP_OBS (SCREEN_TILES_X * SCREEN_TILES_Y)
#define EXTRA_OBS (SCALAR_OBS + HEATMAP_OBS)
#define TOTAL_OBSERVATIONS (SCALED_PIXELS + EXTRA_OBS)

#define GAME_MODE_GENERAL 0
#define GAME_MODE_BATTLE  1

#define PKMN_X_ADDR 0xD362
#define PKMN_Y_ADDR 0xD361
#define PKMN_MAP_ADDR 0xD35E
#define PKMN_BADGES_ADDR 0xD356
#define PKMN_MONEY_ADDR 0xD347

#define PKMN_FACING_ADDR 0xC109

#define MENU_CURRENT_ITEM_ADDR  0xCC26
#define MENU_MAX_ITEM_ADDR      0xCC28
#define MENU_SCROLL_OFFSET_ADDR 0xCC36
#define MENU_TEXTBOX_ID_ADDR    0xD125
#define MENU_TOP_ITEM_Y_ADDR    0xCC24
#define MENU_TOP_ITEM_X_ADDR    0xCC25

#define DAMAGE_MULTIPLIERS_ADDR    0xD05B
#define MOVE_MISSED_ADDR           0xD05F
#define PLAYER_BATTLE_STATUS1_ADDR 0xD062
#define PLAYER_BATTLE_STATUS2_ADDR 0xD063
#define PLAYER_BATTLE_STATUS3_ADDR 0xD064
#define ENEMY_BATTLE_STATUS1_ADDR  0xD067
#define ENEMY_BATTLE_STATUS2_ADDR  0xD068
#define ENEMY_BATTLE_STATUS3_ADDR  0xD069
#define CUR_OPPONENT_ADDR          0xD059

#define PLAYER_MOVING_DIR_ADDR  0xD528
#define NUM_STEPS_TO_TAKE_ADDR  0xCCA1
#define NUM_SPRITES_ADDR        0xD4E1
#define REPEL_STEPS_ADDR        0xD0DB
#define CUR_MAP_TILESET_ADDR    0xD367
#define CUR_MAP_HEIGHT_ADDR     0xD368
#define CUR_MAP_WIDTH_ADDR      0xD369

#define WD72E_ADDR              0xD72E
#define WD72E_DISABLE_BATTLES_BIT 4

#define VIRIDIAN_CITY_MAP       0x01
#define VIRIDIAN_SCRIPT_ADDR    0xD5F4
#define BATTLE_TYPE_OLD_MAN     0x02

#define WEIGHT_BATTLE      0.02f
#define WEIGHT_EXPLORATION 1.00f
#define WEIGHT_EVENTS      0.30f
#define WEIGHT_LEVELING    0.10f
#define WEIGHT_MILESTONES  0.50f

#define MAX_MAPS 256
#define MAX_X 128
#define MAX_Y 128
#define VISITED_COORDS_SIZE (MAX_MAPS * MAX_X * MAX_Y)

typedef struct {
  float episode_length;
  float episode_return;
  float money;
  float level_sum;
  float pkmn1_lvl;
  float pkmn2_lvl;
  float pkmn3_lvl;
  float pkmn4_lvl;
  float pkmn5_lvl;
  float pkmn6_lvl;
  float party_count;
  float party_hp;
  float badges;
  float event_sum;
  float unique_coords;
  float map_n;
  float battles_won;
  float battles_lost;
  float battle_steps;
  float run_attempts;
  float battles_fled;
  float explore_signal;
  float battle_signal;
  float events_signal;
  float leveling_signal;
  float milestone_signal;
  float n;
} Log;

typedef struct {
  uint32_t idx;
  uint8_t x;
  uint8_t y;
  uint8_t map_n;
  uint8_t badges;
  uint8_t party_count;
  uint8_t levels[6];
} CoreState;

typedef struct {
  CoreState core;
  BattleState battle;
  CoreState prev_core;
  BattleState prev_battle;
} GameState;

typedef struct {
  float total_explore_signal;
  float total_battle_signal;
  float total_events_signal;
  float total_leveling_signal;
  float total_milestone_signal;
  uint32_t battle_steps;
  uint16_t battles_won;
  uint16_t battles_lost;
  uint16_t battles_fled;
  uint16_t run_attempts;
} EpisodeStats;

typedef struct {
  Log log;
  mGBA emu;
  GameState gstate;

  float *observations;
  int *actions;
  float *rewards;
  unsigned char *terminals;
  unsigned char *truncations;
  uint8_t *episode_visits;
  uint16_t *exploration_heatmap;
  uint8_t *prev_events;

  EpisodeStats stats;

  int32_t frame_count;
  int32_t step_count;
  int32_t max_episode_length;
  int32_t prev_event_sum;
  uint32_t unique_coords_count;
  float score;
  float heatmap_decay;
  int32_t heatmap_decay_interval;
  float prev_party_hp_frac;
  int prev_action;
  uint8_t game_mode;

  bool full_reset;
  bool disable_wild_until_badge;
  bool verbose;
} PokemonRedEnv;

void update_observations(PokemonRedEnv *env);
void update_core_state(PokemonRedEnv *env);

int calc_level_sum(CoreState *core);
int calc_event_sum(mGBA *emu, uint8_t *prev_events, bool verbose);
float calculate_rewards(PokemonRedEnv *env);

void allocate(PokemonRedEnv *env);
void free_allocated(PokemonRedEnv *env);
void add_log(PokemonRedEnv *env);
void c_reset(PokemonRedEnv *env);
void c_step(PokemonRedEnv *env);
void c_render(PokemonRedEnv *env);
void c_close(PokemonRedEnv *env);

static inline uint32_t coord_index(uint8_t map, uint8_t x, uint8_t y) {
  uint32_t cx = x < MAX_X ? x : MAX_X - 1;
  uint32_t cy = y < MAX_Y ? y : MAX_Y - 1;
  return (uint32_t)map * (MAX_X * MAX_Y) + cx * MAX_Y + cy;
}
static inline bool is_directional_action(int action) {
  return action >= MGBA_ACTION_RIGHT && action <= MGBA_ACTION_DOWN;
}

static inline uint8_t detect_game_mode(mGBA *emu) {
  if (read_mem(emu, BATTLE_FLAG_ADDR) != 0)
    return GAME_MODE_BATTLE;
  return GAME_MODE_GENERAL;
}

#endif // POKEMONREDENV_H
