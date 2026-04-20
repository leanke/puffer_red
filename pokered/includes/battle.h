#include "../../gambatte/gambatte_wrapper.h"
#include <stdbool.h>
#include <stdint.h>

#define BATTLE_FLAG_ADDR 0xD057
#define BATTLE_TYPE_ADDR 0xD05A
#define BATTLE_RESULT_ADDR 0xCF0B
#define GYM_LEADER_NO_ADDR 0xD05C
#define ENEMY_MON_HP_ADDR    0xCFE6
#define ENEMY_MON_MAXHP_ADDR 0xCFF4

#define PKMN1_BASE 0xD16B
#define PKMN_STRUCT_SIZE 0x2C
#define PKMN_CURRENT_HP_OFFSET 0x01
#define PKMN_MAX_HP_OFFSET 0x22
#define PKMN_LEVEL_OFFSET 0x21

#define PARTY_COUNT_ADDR 0xD163
#define NUM_RUN_ATTEMPTS_ADDR 0xD120
#define PLAYER_SELECTED_MOVE_ADDR 0xCCDC

typedef struct {
  uint16_t enemy_hp;
  uint16_t enemy_maxhp;
  int8_t in_battle;
  uint8_t battle_type;
  uint8_t is_gym_battle;
  uint8_t run_attempts;
  uint8_t selected_move;
} BattleState;

static inline bool is_battle_active(const BattleState *b) {
  return b->in_battle == 1 || b->in_battle == 2;
}

static inline uint16_t read_big_endian_16(Emulator *emu, uint16_t addr) {
  return (uint16_t)(read_mem(emu, addr) << 8) | read_mem(emu, addr + 1);
}

static inline void update_battle_state(BattleState *battle, Emulator *emu) {
  battle->in_battle = (int8_t)read_mem(emu, BATTLE_FLAG_ADDR);
  battle->battle_type = read_mem(emu, BATTLE_TYPE_ADDR);
  battle->is_gym_battle = read_mem(emu, GYM_LEADER_NO_ADDR) != 0;
  battle->run_attempts = read_mem(emu, NUM_RUN_ATTEMPTS_ADDR);
  battle->selected_move = read_mem(emu, PLAYER_SELECTED_MOVE_ADDR);
  if (is_battle_active(battle)) {
    battle->enemy_hp = read_big_endian_16(emu, ENEMY_MON_HP_ADDR);
    battle->enemy_maxhp = read_big_endian_16(emu, ENEMY_MON_MAXHP_ADDR);
  } else {
    battle->enemy_hp = 0;
    battle->enemy_maxhp = 0;
  }
}

static inline bool battle_just_ended(const BattleState *curr,
                                     const BattleState *prev) {
  return !is_battle_active(curr) && is_battle_active(prev);
}

static inline bool battle_was_lost(Emulator *emu) {
  return read_mem(emu, BATTLE_RESULT_ADDR) == 1;
}

static inline bool battle_was_fled(Emulator *emu) {
  return read_mem(emu, BATTLE_RESULT_ADDR) == 2;
}

static inline float party_hp_fraction(Emulator *emu) {
  uint8_t count = read_mem(emu, PARTY_COUNT_ADDR);
  if (count == 0 || count > 6) return 1.0f;

  uint32_t total_hp = 0, total_maxhp = 0;
  for (int i = 0; i < count; i++) {
    uint16_t base = PKMN1_BASE + i * PKMN_STRUCT_SIZE;
    total_hp    += read_big_endian_16(emu, base + PKMN_CURRENT_HP_OFFSET);
    total_maxhp += read_big_endian_16(emu, base + PKMN_MAX_HP_OFFSET);
  }
  return (total_maxhp > 0) ? (float)total_hp / (float)total_maxhp : 1.0f;
}
