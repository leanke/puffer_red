#include "mgba_wrapper.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// 0 = not in battle, 1 = wild battle, 2 = trainer battle, -1 (0xFF) = lost
#define BATTLE_FLAG_ADDR 0xD057
// 0 = normal, 1 = old man tutorial, 2 = safari zone
#define BATTLE_TYPE_ADDR 0xD05A
// Is gym leader battle music playing?
#define GYM_BATTLE_MUSIC_ADDR 0xD05C
// Number of turns in current battle
#define TURN_COUNT_ADDR 0xCCD5
// Damage about to be dealt (may show max possible before actual)
#define DAMAGE_PENDING_ADDR 0xD0D8
// Pokemon party base addresses
#define PKMN1_BASE 0xD16B
#define PKMN2_BASE 0xD197
#define PKMN3_BASE 0xD1C3
#define PKMN4_BASE 0xD1EF
#define PKMN5_BASE 0xD21B
#define PKMN6_BASE 0xD247
// Offsets within party Pokemon structure
#define PKMN_CURRENT_HP_OFFSET 0x01
#define PKMN_MAX_HP_OFFSET 0x22
#define PKMN_LEVEL_OFFSET 0x21
#define PKMN_SPECIES_OFFSET 0x00
// Current HP addresses
#define PKMN1_HP_ADDR (PKMN1_BASE + PKMN_CURRENT_HP_OFFSET) // 0xD16C
#define PKMN2_HP_ADDR (PKMN2_BASE + PKMN_CURRENT_HP_OFFSET) // 0xD198
#define PKMN3_HP_ADDR (PKMN3_BASE + PKMN_CURRENT_HP_OFFSET) // 0xD1C4
#define PKMN4_HP_ADDR (PKMN4_BASE + PKMN_CURRENT_HP_OFFSET) // 0xD1F0
#define PKMN5_HP_ADDR (PKMN5_BASE + PKMN_CURRENT_HP_OFFSET) // 0xD21C
#define PKMN6_HP_ADDR (PKMN6_BASE + PKMN_CURRENT_HP_OFFSET) // 0xD248
// Max HP addresses
#define PKMN1_MAXHP_ADDR (PKMN1_BASE + PKMN_MAX_HP_OFFSET) // 0xD18D
#define PKMN2_MAXHP_ADDR (PKMN2_BASE + PKMN_MAX_HP_OFFSET) // 0xD1B9
#define PKMN3_MAXHP_ADDR (PKMN3_BASE + PKMN_MAX_HP_OFFSET) // 0xD1E5
#define PKMN4_MAXHP_ADDR (PKMN4_BASE + PKMN_MAX_HP_OFFSET) // 0xD211
#define PKMN5_MAXHP_ADDR (PKMN5_BASE + PKMN_MAX_HP_OFFSET) // 0xD23D
#define PKMN6_MAXHP_ADDR (PKMN6_BASE + PKMN_MAX_HP_OFFSET) // 0xD269
// Party count address
#define PARTY_COUNT_ADDR 0xD163

typedef struct {
  int8_t in_battle;      // 0=none, 1=wild, 2=trainer, -1=lost
  uint8_t battle_type;   // 0=normal, 1=old man, 2=safari
  uint8_t is_gym_battle; // Gym leader music flag
  uint8_t turn_count;    // Turns in current battle
  bool battle_active;    // Whether currently in an active battle
} BattleState;

// Read battle flag (-1=lost, 0=none, 1=wild, 2=trainer)
static inline int8_t read_battle_flag(mGBA *emu) {
  return (int8_t)read_mem(emu, BATTLE_FLAG_ADDR);
}
// Read battle type (0=normal, 1=old man, 2=safari)
static inline uint8_t read_battle_type(mGBA *emu) {
  return read_mem(emu, BATTLE_TYPE_ADDR);
}
// Check if gym battle music is playing
static inline bool is_gym_battle(mGBA *emu) {
  return read_mem(emu, GYM_BATTLE_MUSIC_ADDR) != 0;
}
// Read current turn count in battle
static inline uint8_t read_turn_count(mGBA *emu) {
  return read_mem(emu, TURN_COUNT_ADDR);
}
// Read party count
static inline uint8_t read_party_count(mGBA *emu) {
  return read_mem(emu, PARTY_COUNT_ADDR);
}
static inline void update_battle_state(BattleState *battle, mGBA *emu) {
  battle->in_battle = read_battle_flag(emu);
  battle->battle_type = read_battle_type(emu);
  battle->is_gym_battle = is_gym_battle(emu) ? 1 : 0;
  battle->turn_count = read_turn_count(emu);
  // Battle is active if flag is 1 (wild) or 2 (trainer)
  battle->battle_active = (battle->in_battle == 1 || battle->in_battle == 2);
}

// Check if battle just started (was not in battle, now in battle)
static inline bool battle_just_started(const BattleState *curr,
                                       const BattleState *prev) {
  return curr->battle_active && !prev->battle_active;
}

// Check if battle just ended (was in battle, now not)
static inline bool battle_just_ended(const BattleState *curr,
                                     const BattleState *prev) {
  return !curr->battle_active && prev->battle_active;
}

// Check if battle was lost (flag == -1 / 0xFF)
static inline bool battle_was_lost(const BattleState *battle) {
  return battle->in_battle == -1;
}
