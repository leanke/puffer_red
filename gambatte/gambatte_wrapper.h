/*
 * gambatte_wrapper.h — Gambatte emulator wrapper.
 *
 * Provides the public API surface that game modules (pokered, template) expect:
 *
 *   Types:   Emulator (struct), GBKey, GBAction, color_t
 *   Funcs:   read_mem, write_mem, read_bcd, read_uint16, write_uint16, ...
 *            action_to_key, set_keys
 *            gb_init_core, initial_load_state, c_save_state_file,
 *            c_load_state_file, gb_render_frame, gb_destroy_renderer
 *   Macros:  GB_KEY_*, GB_ACTION_*, STEP_N_FRAMES, STEP_ACTION_FRAMES
 *
 * Internally everything is routed through the thin C API in gambatte_c.h
 * which delegates to the C++ gambatte::GB class compiled in gambatte_c.cpp.
 */
#ifndef GAMBATTE_WRAPPER_H
#define GAMBATTE_WRAPPER_H

#include <SDL2/SDL.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SDL_WINDOW_ALLOW_HIGHDPI
#define SDL_WINDOW_ALLOW_HIGHDPI 0
#endif

/* The C wrapper around libgambatte. */
#include "gambatte_c.h"

/* Gambatte always outputs XRGB8888 (uint32_t) in default build. */
typedef uint32_t color_t;

/* ── GB_LOAD flags ─────────────────────────────────────────────────────── */
#define GB_LOAD_FORCE_DMG  1

/* ── Gambatte video constants ──────────────────────────────────────────── */
#define GB_SCREEN_WIDTH   160
#define GB_SCREEN_HEIGHT  144
#define GB_VIDEO_PITCH    256   /* Gambatte uses a 256-wide stride */

/* ── Main emulator struct ──────────────────────────────────────────────── */

typedef struct {
  gambatte_handle   gb;          /* opaque pointer to GBState (C++ side) */
  color_t          *video_buffer;/* 256*144 XRGB8888 frame buffer         */
  char              rom_path[256];
  char              state_path[256];
  int32_t           frame_skip;
  int32_t           press_frames;
  bool              render_enabled;
  bool              uses_shared_rom;
  /* SDL rendering */
  SDL_Window       *window;
  SDL_Renderer     *renderer;
  SDL_Texture      *texture;
  uint32_t          window_id;
  int               video_width;
  int               video_height;
  bool              renderer_initialized;
  bool              sdl_registered;
} Emulator;

/* ── Performance helpers (shared ROM, branch hints, …) ─────────────────── */
#include "optim.h"

/* ── Key / action enums (identical bitmask layout to Gambatte's input) ── */

typedef enum {
  GB_KEY_A      = (1 << 0),
  GB_KEY_B      = (1 << 1),
  GB_KEY_SELECT = (1 << 2),
  GB_KEY_START  = (1 << 3),
  GB_KEY_RIGHT  = (1 << 4),
  GB_KEY_LEFT   = (1 << 5),
  GB_KEY_UP     = (1 << 6),
  GB_KEY_DOWN   = (1 << 7),
} GBKey;

typedef enum {
  GB_ACTION_NOOP = 0,
  GB_ACTION_A,
  GB_ACTION_B,
  GB_ACTION_SELECT,
  GB_ACTION_START,
  GB_ACTION_RIGHT,
  GB_ACTION_LEFT,
  GB_ACTION_UP,
  GB_ACTION_DOWN,
  GB_ACTION_COUNT
} GBAction;

/* ── Forward declarations ──────────────────────────────────────────────── */

static void gb_init_core(Emulator *env, const char *rom_path);
static void initial_load_state(Emulator *env, const char *state_path);
static bool c_save_state_file(Emulator *env, const char *path);
static bool c_load_state_file(Emulator *env, const char *path);

/* ── Inline helpers ────────────────────────────────────────────────────── */

static inline uint32_t action_to_key(int action) {
  if (action <= 0 || action >= GB_ACTION_COUNT)
    return 0;
  return (1 << (action - 1));
}

static inline void set_keys(Emulator *env, uint32_t keys) {
  if (env && env->gb)
    gambatte_set_input(env->gb, keys & 0xFF);
}

static inline uint8_t read_mem(Emulator *env, uint16_t addr) {
  return (env && env->gb) ? gambatte_read_mem(env->gb, addr) : 0;
}

static inline uint32_t read_bcd(Emulator *env, uint16_t addr) {
  uint8_t h = read_mem(env, addr);
  uint8_t m = read_mem(env, addr + 1);
  uint8_t l = read_mem(env, addr + 2);
  return ((h >> 4) * 100000) + ((h & 0xF) * 10000) + ((m >> 4) * 1000) +
         ((m & 0xF) * 100) + ((l >> 4) * 10) + (l & 0xF);
}

static inline uint16_t read_uint16(Emulator *env, uint16_t addr) {
  uint8_t low  = read_mem(env, addr);
  uint8_t high = read_mem(env, addr + 1);
  return (uint16_t)(low | (high << 8));
}

static inline void write_mem(Emulator *env, uint16_t addr, uint8_t value) {
  if (env && env->gb)
    gambatte_write_mem(env->gb, addr, value);
}

static inline void write_bcd(Emulator *env, uint16_t addr, uint32_t value) {
  uint8_t h = ((value / 100000) << 4) | ((value / 10000) % 10);
  uint8_t m = (((value / 1000) % 10) << 4) | ((value / 100) % 10);
  uint8_t l = (((value / 10) % 10) << 4) | (value % 10);
  write_mem(env, addr, h);
  write_mem(env, addr + 1, m);
  write_mem(env, addr + 2, l);
}

static inline void write_uint16(Emulator *env, uint16_t addr, uint16_t value) {
  write_mem(env, addr, (uint8_t)(value & 0xFF));
  write_mem(env, addr + 1, (uint8_t)(value >> 8));
}

/* ══════════════════════════════════════════════════════════════════════════
 *  SDL rendering pipeline
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct RenderRegistryNode {
  uint32_t window_id;
  Emulator *env;
  struct RenderRegistryNode *next;
} RenderRegistryNode;

static RenderRegistryNode *g_render_registry = NULL;
static int g_sdl_video_users = 0;

static void gb_destroy_renderer(Emulator *env);

static bool gb_acquire_sdl_video(void) {
  if (g_sdl_video_users == 0) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
      return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  }
  g_sdl_video_users++;
  return true;
}

static void gb_release_sdl_video(void) {
  if (g_sdl_video_users == 0)
    return;
  g_sdl_video_users--;
  if (g_sdl_video_users == 0) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();
  }
}

static void gb_register_window(Emulator *env) {
  if (!env || env->window_id == 0)
    return;
  RenderRegistryNode *node =
      (RenderRegistryNode *)calloc(1, sizeof(RenderRegistryNode));
  if (!node)
    return;
  node->window_id = env->window_id;
  node->env = env;
  node->next = g_render_registry;
  g_render_registry = node;
}

static void gb_unregister_window(uint32_t window_id) {
  RenderRegistryNode **curr = &g_render_registry;
  while (*curr) {
    if ((*curr)->window_id == window_id) {
      RenderRegistryNode *victim = *curr;
      *curr = victim->next;
      free(victim);
      return;
    }
    curr = &(*curr)->next;
  }
}

static Emulator *gb_lookup_env(uint32_t window_id) {
  RenderRegistryNode *node = g_render_registry;
  while (node) {
    if (node->window_id == window_id)
      return node->env;
    node = node->next;
  }
  return NULL;
}

static SDL_Rect gb_calculate_dest_rect(const Emulator *env) {
  SDL_Rect rect = {0, 0, 0, 0};
  if (!env || !env->window || env->video_width <= 0 || env->video_height <= 0)
    return rect;

  int window_w = 0, window_h = 0;
  SDL_GetWindowSize(env->window, &window_w, &window_h);
  if (window_w <= 0 || window_h <= 0) {
    rect.w = env->video_width;
    rect.h = env->video_height;
    return rect;
  }

  int scaled_w = window_w;
  int scaled_h = (env->video_height * scaled_w) /
                 (env->video_width ? env->video_width : 1);
  if (scaled_h > window_h) {
    scaled_h = window_h;
    scaled_w = (env->video_width * scaled_h) /
               (env->video_height ? env->video_height : 1);
  }
  if (scaled_w <= 0) scaled_w = env->video_width;
  if (scaled_h <= 0) scaled_h = env->video_height;

  rect.w = scaled_w;
  rect.h = scaled_h;
  rect.x = (window_w - rect.w) / 2;
  rect.y = (window_h - rect.h) / 2;
  return rect;
}

static bool gb_ensure_renderer(Emulator *env) {
  if (!env || !env->render_enabled)
    return false;
  if (env->renderer_initialized && env->window && env->renderer && env->texture)
    return true;

  if (!gb_acquire_sdl_video())
    return false;
  env->sdl_registered = true;

  int width  = env->video_width  > 0 ? env->video_width  : GB_SCREEN_WIDTH;
  int height = env->video_height > 0 ? env->video_height : GB_SCREEN_HEIGHT;

  env->window = SDL_CreateWindow(
      env->rom_path[0] ? env->rom_path : "Gambatte",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      width * 3, height * 3,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!env->window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    gb_destroy_renderer(env);
    return false;
  }

  env->renderer = SDL_CreateRenderer(
      env->window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (!env->renderer)
    env->renderer = SDL_CreateRenderer(env->window, -1, SDL_RENDERER_SOFTWARE);
  if (!env->renderer) {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    gb_destroy_renderer(env);
    return false;
  }

  env->texture = SDL_CreateTexture(
      env->renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!env->texture) {
    fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    gb_destroy_renderer(env);
    return false;
  }

  env->video_width  = width;
  env->video_height = height;
  env->window_id    = SDL_GetWindowID(env->window);
  gb_register_window(env);
  env->renderer_initialized = true;
  SDL_ShowWindow(env->window);
  return true;
}

static void gb_destroy_renderer(Emulator *env) {
  if (!env) return;
  if (env->texture)  { SDL_DestroyTexture(env->texture);   env->texture  = NULL; }
  if (env->renderer) { SDL_DestroyRenderer(env->renderer); env->renderer = NULL; }
  if (env->window)   { SDL_DestroyWindow(env->window);     env->window   = NULL; }
  if (env->window_id) {
    gb_unregister_window(env->window_id);
    env->window_id = 0;
  }
  if (env->sdl_registered) {
    gb_release_sdl_video();
    env->sdl_registered = false;
  }
  env->renderer_initialized = false;
}

static void gb_dispatch_events(void) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      while (g_render_registry) {
        Emulator *target = g_render_registry->env;
        if (target) {
          target->render_enabled = false;
          gb_destroy_renderer(target);
        } else {
          RenderRegistryNode *orphan = g_render_registry;
          g_render_registry = orphan->next;
          free(orphan);
        }
      }
      break;
    }
    if (event.type == SDL_WINDOWEVENT) {
      Emulator *target = gb_lookup_env(event.window.windowID);
      if (!target) continue;
      if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
        fprintf(stderr, "Rendering disabled after window close (env %p).\n",
                (void *)target);
        target->render_enabled = false;
        gb_destroy_renderer(target);
      }
    }
  }
}

static void gb_render_frame(Emulator *env) {
  if (!env || !env->render_enabled || !env->video_buffer)
    return;

  gb_dispatch_events();
  if (!gb_ensure_renderer(env))
    return;

  /*
   * Gambatte's video buffer has pitch=256 (256 uint32 per row, only first 160 used).
   * We need to copy the meaningful 160-pixel-wide strip into a contiguous
   * 160×144 buffer for the SDL texture.
   */
  uint32_t compact[GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT];
  for (int y = 0; y < GB_SCREEN_HEIGHT; y++) {
    memcpy(&compact[y * GB_SCREEN_WIDTH],
           &env->video_buffer[y * GB_VIDEO_PITCH],
           GB_SCREEN_WIDTH * sizeof(uint32_t));
  }

  const int pitch = env->video_width * (int)sizeof(color_t);
  if (SDL_UpdateTexture(env->texture, NULL, compact, pitch) != 0) {
    fprintf(stderr, "SDL_UpdateTexture failed: %s\n", SDL_GetError());
    return;
  }

  SDL_SetRenderDrawColor(env->renderer, 0, 0, 0, 255);
  SDL_RenderClear(env->renderer);
  SDL_Rect dest = gb_calculate_dest_rect(env);
  SDL_RenderCopy(env->renderer, env->texture, NULL,
                 dest.w > 0 && dest.h > 0 ? &dest : NULL);
  SDL_RenderPresent(env->renderer);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Core init / state load / save
 * ══════════════════════════════════════════════════════════════════════════ */

static void gb_init_core(Emulator *env, const char *rom_path) {
  if (!env) return;

  env->uses_shared_rom = false;
  env->window            = NULL;
  env->renderer          = NULL;
  env->texture           = NULL;
  env->window_id         = 0;
  env->renderer_initialized = false;
  env->sdl_registered       = false;

  /* Create Gambatte instance. */
  env->gb = gambatte_create();
  if (!env->gb) {
    fprintf(stderr, "Failed to create Gambatte instance\n");
    return;
  }

  /* Try shared ROM buffer first (one file-read per process). */
  bool rom_loaded = false;
  if (acquire_shared_rom(rom_path)) {
    if (gambatte_load(env->gb, g_shared_rom_data,
                      (unsigned)g_shared_rom_size,
                      GB_LOAD_FORCE_DMG) == 0) {
      env->uses_shared_rom = true;
      rom_loaded = true;
    } else {
      release_shared_rom();
    }
  }
  if (!rom_loaded) {
    /* Fall back: read the file ourselves. */
    FILE *f = fopen(rom_path, "rb");
    if (!f) {
      fprintf(stderr, "Failed to open ROM: %s\n", rom_path);
      gambatte_destroy(env->gb);
      env->gb = NULL;
      return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
      fclose(f);
      gambatte_destroy(env->gb);
      env->gb = NULL;
      return;
    }
    void *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
      free(buf);
      fclose(f);
      gambatte_destroy(env->gb);
      env->gb = NULL;
      return;
    }
    fclose(f);
    if (gambatte_load(env->gb, buf, (unsigned)sz, GB_LOAD_FORCE_DMG) != 0) {
      fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
      free(buf);
      gambatte_destroy(env->gb);
      env->gb = NULL;
      return;
    }
    free(buf);
  }

  /* Allocate video buffer.  Gambatte expects pitch=256 so allocate 256*144. */
  env->video_buffer =
      (color_t *)calloc(GB_VIDEO_PITCH * GB_SCREEN_HEIGHT, sizeof(color_t));
  env->video_width  = GB_SCREEN_WIDTH;
  env->video_height = GB_SCREEN_HEIGHT;

  strncpy(env->rom_path, rom_path, sizeof(env->rom_path) - 1);
}

static bool c_save_state_file(Emulator *env, const char *path) {
  if (!env || !env->gb || !path) return false;
  return gambatte_save_state_file(env->gb, path);
}

static bool c_load_state_file(Emulator *env, const char *path) {
  if (!env || !env->gb || !path) return false;
  return gambatte_load_state_file(env->gb, path);
}

static void initial_load_state(Emulator *env, const char *state_path) {
  if (!env || !env->gb || !state_path) return;
  if (!gambatte_load_state_file(env->gb, state_path)) {
    fprintf(stderr, "Warning: Failed to load state file: %s\n", state_path);
  }
}

#endif /* GAMBATTE_WRAPPER_H */
