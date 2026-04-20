#ifndef POKERED_PARTY_H
#define POKERED_PARTY_H

#include "../../gambatte/gambatte_wrapper.h"
#include <stddef.h>
#include <stdint.h>

#ifndef PKMN1_BASE
#define PKMN1_BASE 0xD16B
#define PKMN2_BASE 0xD197
#define PKMN3_BASE 0xD1C3
#define PKMN4_BASE 0xD1EF
#define PKMN5_BASE 0xD21B
#define PKMN6_BASE 0xD247
#endif

#define PKMN_STRUCT_SIZE 0x2C
#define PKMN_MAX_PARTY_SIZE 6
#define PKMN_PARTY_COUNT_ADDR 0xD163

#define PKMN_SPECIES_OFFSET 0x00
#define PKMN_CURRENT_HP_OFFSET 0x01
#define PKMN_BOX_LEVEL_OFFSET 0x03
#define PKMN_STATUS_OFFSET 0x04
#define PKMN_TYPE1_OFFSET 0x05
#define PKMN_TYPE2_OFFSET 0x06
#define PKMN_CATCH_RATE_OFFSET 0x07
#define PKMN_MOVE1_OFFSET 0x08
#define PKMN_MOVE2_OFFSET 0x09
#define PKMN_MOVE3_OFFSET 0x0A
#define PKMN_MOVE4_OFFSET 0x0B
#define PKMN_TRAINER_ID_OFFSET 0x0C
#define PKMN_EXP_OFFSET 0x0E
#define PKMN_HP_EV_OFFSET 0x11
#define PKMN_ATTACK_EV_OFFSET 0x13
#define PKMN_DEFENSE_EV_OFFSET 0x15
#define PKMN_SPEED_EV_OFFSET 0x17
#define PKMN_SPECIAL_EV_OFFSET 0x19
#define PKMN_ATTACK_DEFENSE_IV_OFFSET 0x1B
#define PKMN_SPEED_SPECIAL_IV_OFFSET 0x1C
#define PKMN_PP_OFFSET 0x1D
#define PKMN_LEVEL_OFFSET 0x21
#define PKMN_MAX_HP_OFFSET 0x22
#define PKMN_ATTACK_OFFSET 0x24
#define PKMN_DEFENSE_OFFSET 0x26
#define PKMN_SPEED_OFFSET 0x28
#define PKMN_SPECIAL_OFFSET 0x2A

typedef struct {
	uint8_t species;
	uint16_t current_hp;
	uint8_t stored_level;
	uint8_t status;
	uint8_t type1;
	uint8_t type2;
	uint8_t catch_rate;
	uint8_t moves[4];
	uint16_t trainer_id;
	uint32_t experience;
	uint16_t hp_ev;
	uint16_t attack_ev;
	uint16_t defense_ev;
	uint16_t speed_ev;
	uint16_t special_ev;
	uint8_t attack_defense_iv;
	uint8_t speed_special_iv;
	uint8_t move_pp[4];
	uint8_t level;
	uint16_t max_hp;
	uint16_t attack;
	uint16_t defense;
	uint16_t speed;
	uint16_t special;
} PartyPokemon;

static inline uint16_t party_slot_base(uint8_t slot) {
	return (uint16_t)(PKMN1_BASE + (slot * PKMN_STRUCT_SIZE));
}

static inline uint32_t read_uint24(mGBA *emu, uint16_t addr) {
	if (!emu)
		return 0;
	uint32_t b0 = read_mem(emu, addr);
	uint32_t b1 = read_mem(emu, addr + 1);
	uint32_t b2 = read_mem(emu, addr + 2);
	return b0 | (b1 << 8) | (b2 << 16);
}

static inline void read_party_pokemon_at(mGBA *emu, uint16_t base, PartyPokemon *out) {
	if (!emu || !out)
		return;
	out->species = read_mem(emu, base + PKMN_SPECIES_OFFSET);
	out->current_hp = read_uint16(emu, base + PKMN_CURRENT_HP_OFFSET);
	out->stored_level = read_mem(emu, base + PKMN_BOX_LEVEL_OFFSET);
	out->status = read_mem(emu, base + PKMN_STATUS_OFFSET);
	out->type1 = read_mem(emu, base + PKMN_TYPE1_OFFSET);
	out->type2 = read_mem(emu, base + PKMN_TYPE2_OFFSET);
	out->catch_rate = read_mem(emu, base + PKMN_CATCH_RATE_OFFSET);
	out->moves[0] = read_mem(emu, base + PKMN_MOVE1_OFFSET);
	out->moves[1] = read_mem(emu, base + PKMN_MOVE2_OFFSET);
	out->moves[2] = read_mem(emu, base + PKMN_MOVE3_OFFSET);
	out->moves[3] = read_mem(emu, base + PKMN_MOVE4_OFFSET);
	out->trainer_id = read_uint16(emu, base + PKMN_TRAINER_ID_OFFSET);
	out->experience = read_uint24(emu, base + PKMN_EXP_OFFSET);
	out->hp_ev = read_uint16(emu, base + PKMN_HP_EV_OFFSET);
	out->attack_ev = read_uint16(emu, base + PKMN_ATTACK_EV_OFFSET);
	out->defense_ev = read_uint16(emu, base + PKMN_DEFENSE_EV_OFFSET);
	out->speed_ev = read_uint16(emu, base + PKMN_SPEED_EV_OFFSET);
	out->special_ev = read_uint16(emu, base + PKMN_SPECIAL_EV_OFFSET);
	out->attack_defense_iv = read_mem(emu, base + PKMN_ATTACK_DEFENSE_IV_OFFSET);
	out->speed_special_iv = read_mem(emu, base + PKMN_SPEED_SPECIAL_IV_OFFSET);
	out->move_pp[0] = read_mem(emu, base + PKMN_PP_OFFSET + 0);
	out->move_pp[1] = read_mem(emu, base + PKMN_PP_OFFSET + 1);
	out->move_pp[2] = read_mem(emu, base + PKMN_PP_OFFSET + 2);
	out->move_pp[3] = read_mem(emu, base + PKMN_PP_OFFSET + 3);
	out->level = read_mem(emu, base + PKMN_LEVEL_OFFSET);
	out->max_hp = read_uint16(emu, base + PKMN_MAX_HP_OFFSET);
	out->attack = read_uint16(emu, base + PKMN_ATTACK_OFFSET);
	out->defense = read_uint16(emu, base + PKMN_DEFENSE_OFFSET);
	out->speed = read_uint16(emu, base + PKMN_SPEED_OFFSET);
	out->special = read_uint16(emu, base + PKMN_SPECIAL_OFFSET);
}

static inline bool read_party_pokemon(mGBA *emu, uint8_t slot, PartyPokemon *out) {
	if (!emu || !out)
		return false;
	uint8_t party_count = read_mem(emu, PKMN_PARTY_COUNT_ADDR);
	if (slot >= party_count || slot >= PKMN_MAX_PARTY_SIZE)
		return false;
	read_party_pokemon_at(emu, party_slot_base(slot), out);
	return true;
}

static inline uint8_t read_party(mGBA *emu, PartyPokemon *out, size_t max_slots) {
	if (!emu || !out || max_slots == 0)
		return 0;
	uint8_t party_count = read_mem(emu, PKMN_PARTY_COUNT_ADDR);
	if (party_count > PKMN_MAX_PARTY_SIZE)
		party_count = PKMN_MAX_PARTY_SIZE;
	if (party_count > max_slots)
		party_count = (uint8_t)max_slots;
	for (uint8_t i = 0; i < party_count; ++i) {
		read_party_pokemon_at(emu, party_slot_base(i), &out[i]);
	}
	return party_count;
}

#endif // POKERED_PARTY_H
