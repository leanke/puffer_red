#ifndef TEMPLATE_ENV_H
#define TEMPLATE_ENV_H

#include "../mgba/mgba_wrapper.h"

// ── Screen & Observation ─────────────────────────────────────────────
// GB/GBC: 160×144   GBA: 240×160
// Adjust SCREEN_WIDTH/HEIGHT to match your platform, then choose a
// downscale factor for the observation fed to the policy network.
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144
#define SCALE_FACTOR 2
#define SCALED_WIDTH (SCREEN_WIDTH / SCALE_FACTOR)
#define SCALED_HEIGHT (SCREEN_HEIGHT / SCALE_FACTOR)
#define SCALED_PIXELS (SCALED_WIDTH * SCALED_HEIGHT)

// Extra scalar observations appended after the pixel data.
// Customize this for your game (e.g. score, lives, level).
#define EXTRA_OBS 0
#define TOTAL_OBSERVATIONS (SCALED_PIXELS + EXTRA_OBS)

// ── Memory Addresses ─────────────────────────────────────────────────
// Define game-specific RAM addresses here.
// Example:
// #define PLAYER_X_ADDR   0xC100
// #define PLAYER_Y_ADDR   0xC104
// #define SCORE_ADDR      0xD000

// ── Reward Constants ─────────────────────────────────────────────────
// #define REWARD_GOAL   1.0f
// #define PENALTY_DEATH -0.5f

// ── Log ──────────────────────────────────────────────────────────────
// All fields MUST be float so the generic vec_log aggregator works.
typedef struct {
  float episode_length;
  float episode_return;
  float n;
} Log;

// ── Game State ───────────────────────────────────────────────────────
// Put any per-step RAM snapshots or derived state here.
typedef struct {
  // Example fields:
  // uint8_t player_x;
  // uint8_t player_y;
  // uint16_t score;
  uint8_t _placeholder;
} GameState;

// ── Environment ──────────────────────────────────────────────────────
typedef struct {
  Log log;
  mGBA emu;
  GameState state;
  GameState prev_state;
  float *observations;
  int *actions;
  float *rewards;
  unsigned char *terminals;
  unsigned char *truncations;

  int32_t frame_count;
  int32_t step_count;
  int32_t max_episode_length;
  float score;
  bool full_reset;
} TemplateEnv;

// ── Required function signatures ─────────────────────────────────────
// The generic env_binding.h calls these via c_reset / c_step / etc.
void c_reset(TemplateEnv *env);
void c_step(TemplateEnv *env);
void c_render(TemplateEnv *env);
void c_close(TemplateEnv *env);
void allocate(TemplateEnv *env);
void free_allocated(TemplateEnv *env);
void add_log(TemplateEnv *env);

// ── Observation builder ──────────────────────────────────────────────
static inline void update_observations(TemplateEnv *env) {
  if (!env || !env->emu.video_buffer || !env->observations)
    return;

  const color_t *vbuf = env->emu.video_buffer;
  float *obs = env->observations;

  // Downsample & greyscale
  for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
    for (int sx = 0; sx < SCALED_WIDTH; sx++) {
      int src_y = sy * SCALE_FACTOR;
      int src_x = sx * SCALE_FACTOR;
      float gray_sum = 0.0f;
      for (int dy = 0; dy < SCALE_FACTOR; dy++) {
        for (int dx = 0; dx < SCALE_FACTOR; dx++) {
          int idx = (src_y + dy) * SCREEN_WIDTH + (src_x + dx);
          color_t px = vbuf[idx];
          float r = (float)((px >> 16) & 0xFF);
          float g = (float)((px >> 8) & 0xFF);
          float b = (float)(px & 0xFF);
          gray_sum += 0.299f * r + 0.587f * g + 0.114f * b;
        }
      }
      obs[sy * SCALED_WIDTH + sx] =
          gray_sum / (float)(SCALE_FACTOR * SCALE_FACTOR);
    }
  }

  // Append extra observations here:
  // int offset = SCALED_PIXELS;
  // obs[offset + 0] = (float)env->state.player_x;
}

// ── Core logic ───────────────────────────────────────────────────────
void allocate(TemplateEnv *env) {
  env->observations = (float *)calloc(TOTAL_OBSERVATIONS, sizeof(float));
  env->actions = (int *)calloc(1, sizeof(int));
  env->rewards = (float *)calloc(1, sizeof(float));
  env->terminals = (unsigned char *)calloc(1, sizeof(unsigned char));
  env->truncations = (unsigned char *)calloc(1, sizeof(unsigned char));
}

void free_allocated(TemplateEnv *env) {
  free(env->observations);
  free(env->actions);
  free(env->rewards);
  free(env->terminals);
  free(env->truncations);
}

void add_log(TemplateEnv *env) {
  env->log.episode_length = (float)env->step_count;
  env->log.episode_return = env->score;
  env->log.n++;
}

static float calculate_rewards(TemplateEnv *env) {
  float reward = 0.0f;
  // TODO: read game RAM and compute reward
  // Example:
  //   uint16_t score = read_uint16(&env->emu, SCORE_ADDR);
  //   reward = (float)(score - prev_score) * 0.01f;
  return reward;
}

void c_reset(TemplateEnv *env) {
  if (!env || !env->emu.core)
    return;

  if (env->full_reset) {
    initial_load_state(&env->emu, env->emu.state_path);
  }

  update_observations(env);
  env->rewards[0] = 0;
  env->terminals[0] = 0;
  env->step_count = env->frame_count = 0;
  env->score = 0.0f;

  // Run a few warmup frames
  for (int i = 0; i < 4; i++)
    env->emu.core->runFrame(env->emu.core);
}

void c_step(TemplateEnv *env) {
  if (!env || !env->emu.core)
    return;

  env->rewards[0] = 0;
  env->terminals[0] = 0;
  env->step_count++;

  int skip = env->emu.frame_skip > 0 ? env->emu.frame_skip : 1;
  uint32_t action_key = action_to_key(env->actions[0]);
  STEP_N_FRAMES(env->emu.core, action_key, skip);
  env->frame_count += skip;

  float reward = calculate_rewards(env);
  update_observations(env);
  env->rewards[0] = reward;
  env->score += reward;

  if (env->step_count >= env->max_episode_length) {
    env->terminals[0] = 1;
    add_log(env);
    c_reset(env);
  }
}

void c_render(TemplateEnv *env) { mgba_render_frame(&env->emu); }

void c_close(TemplateEnv *env) {
  if (!env)
    return;

  mgba_destroy_renderer(&env->emu);

  if (env->emu.core) {
    env->emu.core->setVideoBuffer(env->emu.core, NULL, 0);
    mCoreConfigDeinit(&env->emu.core->config);
    env->emu.core->deinit(env->emu.core);
    env->emu.core = NULL;
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

#endif // TEMPLATE_ENV_H
