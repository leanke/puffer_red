#include "pokered.h"

void update_observations(PokemonRedEnv *env) {
  if (!env || !env->emu.video_buffer || !env->observations)
    return;

  PREFETCH_READ(env->emu.video_buffer);
  PREFETCH_WRITE(env->observations);
  const color_t *vbuf = env->emu.video_buffer;
  float *obs = env->observations;
  CoreState *core = &env->gstate.core;
  mGBA *emu = &env->emu;

  for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
    for (int sx = 0; sx < SCALED_WIDTH; sx++) {
      int src_y = sy * 2;
      int src_x = sx * 2;

      uint32_t gray_sum = 0;
      for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
          int src_idx = (src_y + dy) * SCREEN_WIDTH + (src_x + dx);
          color_t pixel = vbuf[src_idx];
          uint32_t r = (pixel >> 16) & 0xFF;
          uint32_t g = (pixel >> 8) & 0xFF;
          uint32_t b = pixel & 0xFF;
          gray_sum += r * 77 + g * 150 + b * 29;
        }
      }
      obs[sy * SCALED_WIDTH + sx] = (float)(gray_sum >> 10);
    }
  }

  if (env->exploration_heatmap) {
    uint8_t map_n = core->map_n;
    for (int ty = 0; ty < SCREEN_TILES_Y; ty++) {
      for (int tx = 0; tx < SCREEN_TILES_X; tx++) {
        int mx = (int)core->x - PLAYER_TILE_X + tx;
        int my = (int)core->y - PLAYER_TILE_Y + ty;
        if (mx < 0 || my < 0 || mx >= MAX_X || my >= MAX_Y)
          continue;

        uint32_t ci = coord_index(map_n, (uint8_t)mx, (uint8_t)my);
        uint16_t visits = env->exploration_heatmap[ci];
        if (visits == 0)
          continue;

        float level = 1.0f;
        uint16_t v = visits >> 1;
        while (v) { level += 1.0f; v >>= 1; }
        float add = level * HEATMAP_BRIGHTNESS;
        if (add > HEATMAP_MAX_BRIGHT)
          add = HEATMAP_MAX_BRIGHT;

        int px0 = tx * METATILE_PX;
        int py0 = ty * METATILE_PX;
        for (int py = py0; py < py0 + METATILE_PX; py++) {
          for (int px = px0; px < px0 + METATILE_PX; px++) {
            int oi = py * SCALED_WIDTH + px;
            float val = obs[oi] + add;
            obs[oi] = val > 255.0f ? 255.0f : val;
          }
        }
      }
    }
  }

  int o = SCALED_PIXELS;

  obs[o + 0] = (float)core->x;
  obs[o + 1] = (float)core->y;
  obs[o + 2] = (float)core->map_n;
  obs[o + 3] = (float)core->badges;
  obs[o + 4] = (float)core->party_count;
  obs[o + 5] = is_battle_active(&env->gstate.battle) ? 1.0f : 0.0f;

  uint16_t hp    = read_big_endian_16(emu, PKMN1_BASE + PKMN_CURRENT_HP_OFFSET);
  uint16_t maxhp = read_big_endian_16(emu, PKMN1_BASE + PKMN_MAX_HP_OFFSET);
  obs[o + 6] = maxhp > 0 ? (float)hp / (float)maxhp : 1.0f;
  obs[o + 7] = (float)(read_mem(emu, PKMN_FACING_ADDR) / 4);

  obs[o + 8]  = (float)read_mem(emu, MENU_CURRENT_ITEM_ADDR);
  obs[o + 9]  = (float)read_mem(emu, MENU_MAX_ITEM_ADDR);
  obs[o + 10] = (float)read_mem(emu, MENU_SCROLL_OFFSET_ADDR);
  obs[o + 11] = (float)read_mem(emu, MENU_TEXTBOX_ID_ADDR);
  obs[o + 12] = (float)read_mem(emu, MENU_TOP_ITEM_Y_ADDR);
  obs[o + 13] = (float)read_mem(emu, MENU_TOP_ITEM_X_ADDR);

  obs[o + 14] = (float)read_mem(emu, DAMAGE_MULTIPLIERS_ADDR);
  obs[o + 15] = (float)read_mem(emu, MOVE_MISSED_ADDR);
  obs[o + 16] = (float)read_mem(emu, PLAYER_BATTLE_STATUS1_ADDR);
  obs[o + 17] = (float)read_mem(emu, PLAYER_BATTLE_STATUS2_ADDR);
  obs[o + 18] = (float)read_mem(emu, PLAYER_BATTLE_STATUS3_ADDR);
  obs[o + 19] = (float)read_mem(emu, ENEMY_BATTLE_STATUS1_ADDR);
  obs[o + 20] = (float)read_mem(emu, ENEMY_BATTLE_STATUS2_ADDR);
  obs[o + 21] = (float)read_mem(emu, ENEMY_BATTLE_STATUS3_ADDR);
  obs[o + 22] = (float)read_mem(emu, CUR_OPPONENT_ADDR);

  obs[o + 23] = (float)read_mem(emu, PLAYER_MOVING_DIR_ADDR);
  obs[o + 24] = (float)read_mem(emu, NUM_STEPS_TO_TAKE_ADDR);
  obs[o + 25] = (float)read_mem(emu, NUM_SPRITES_ADDR);
  obs[o + 26] = (float)read_mem(emu, REPEL_STEPS_ADDR);
  obs[o + 27] = (float)read_mem(emu, CUR_MAP_TILESET_ADDR);
  obs[o + 28] = (float)read_mem(emu, CUR_MAP_HEIGHT_ADDR);
  obs[o + 29] = (float)read_mem(emu, CUR_MAP_WIDTH_ADDR);

  uint8_t mode = env->game_mode;
  obs[o + 30] = (mode == GAME_MODE_GENERAL) ? 1.0f : 0.0f;
  obs[o + 31] = (mode == GAME_MODE_BATTLE)  ? 1.0f : 0.0f;

  if (mode != GAME_MODE_BATTLE) {
    for (int i = 14; i <= 22; i++)
      obs[o + i] = 0.0f;
  }

  int h = SCALED_PIXELS + SCALAR_OBS;
  if (env->exploration_heatmap) {
    uint8_t map_n = core->map_n;
    for (int ty = 0; ty < SCREEN_TILES_Y; ty++) {
      for (int tx = 0; tx < SCREEN_TILES_X; tx++) {
        int mx = (int)core->x - PLAYER_TILE_X + tx;
        int my = (int)core->y - PLAYER_TILE_Y + ty;
        int idx = h + ty * SCREEN_TILES_X + tx;
        if (mx < 0 || my < 0 || mx >= MAX_X || my >= MAX_Y) {
          obs[idx] = 0.0f;
          continue;
        }
        uint32_t ci = coord_index(map_n, (uint8_t)mx, (uint8_t)my);
        obs[idx] = (float)env->exploration_heatmap[ci];
      }
    }
  } else {
    for (int i = 0; i < HEATMAP_OBS; i++)
      obs[h + i] = 0.0f;
  }
}

void update_core_state(PokemonRedEnv *env) {
  CoreState *core = &env->gstate.core;
  mGBA *emu = &env->emu;

  core->x = read_mem(emu, PKMN_X_ADDR);
  core->y = read_mem(emu, PKMN_Y_ADDR);
  core->map_n = read_mem(emu, PKMN_MAP_ADDR);
  core->idx = coord_index(core->map_n, core->x, core->y);
  core->badges = read_mem(emu, PKMN_BADGES_ADDR);
  core->party_count = read_mem(emu, PARTY_COUNT_ADDR);
  for (int i = 0; i < 6; i++)
    core->levels[i] = read_mem(emu, PKMN1_BASE + PKMN_LEVEL_OFFSET + i * PKMN_STRUCT_SIZE);
}
