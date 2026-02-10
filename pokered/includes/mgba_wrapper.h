#ifndef MGBA_WRAPPER_H
#define MGBA_WRAPPER_H

#include <SDL2/SDL.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SDL_WINDOW_ALLOW_HIGHDPI
#define SDL_WINDOW_ALLOW_HIGHDPI 0
#endif

#include <mgba-util/vfs.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/gb/core.h>
#include <mgba/gb/interface.h>

typedef struct {
  struct mCore *core;
  color_t *video_buffer;
  char rom_path[256];
  char state_path[256];
  int32_t frame_skip;
  bool render_enabled;
  bool uses_shared_rom;
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  uint32_t window_id;
  int video_width;
  int video_height;
  bool renderer_initialized;
  bool sdl_registered;
} mGBA;

#include "optim.h" // needed below mGBA struct?

typedef enum {
  GB_KEY_A = (1 << 0),      // 0x01
  GB_KEY_B = (1 << 1),      // 0x02
  GB_KEY_SELECT = (1 << 2), // 0x04
  GB_KEY_START = (1 << 3),  // 0x08
  GB_KEY_RIGHT = (1 << 4),  // 0x10
  GB_KEY_LEFT = (1 << 5),   // 0x20
  GB_KEY_UP = (1 << 6),     // 0x40
  GB_KEY_DOWN = (1 << 7),   // 0x80
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



void mgba_init_core(mGBA *env, const char *rom_path);
void initial_load_state(mGBA *env, const char *state_path);
bool c_save_state_file(mGBA *env, const char *path);
bool c_load_state_file(mGBA *env, const char *path);

static void silent_log(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args) {
  (void)logger;
  (void)category;
  (void)level;
  (void)format;
  (void)args;
}
static struct mLogger s_silentLogger = {.log = silent_log, .filter = NULL};
static int g_stderr_backup = -1;
static int g_devnull_fd = -1;
static inline void suppress_stderr(void) {
  fflush(stderr);
  g_stderr_backup = dup(STDERR_FILENO);
  g_devnull_fd = open("/dev/null", O_WRONLY);
  if (g_devnull_fd >= 0) {
    dup2(g_devnull_fd, STDERR_FILENO);
  }
}
static inline void restore_stderr(void) {
  fflush(stderr);
  if (g_stderr_backup >= 0) {
    dup2(g_stderr_backup, STDERR_FILENO);
    close(g_stderr_backup);
    g_stderr_backup = -1;
  }
  if (g_devnull_fd >= 0) {
    close(g_devnull_fd);
    g_devnull_fd = -1;
  }
}

static inline uint32_t action_to_key(int action) {
  if (action <= 0 || action >= GB_ACTION_COUNT)
    return 0;
  return (1 << (action - 1));
}
static inline void set_keys(mGBA *env, uint32_t action) {
  if (env && env->core)
    env->core->setKeys(env->core, action & 0xFF);
}
static inline uint8_t read_mem(mGBA *env, uint16_t addr) {
  return env && env->core ? (uint8_t)env->core->rawRead8(env->core, addr, -1)
                          : 0;
}
static inline uint32_t read_bcd(mGBA *env, uint16_t addr) {
  uint8_t h = read_mem(env, addr);
  uint8_t m = read_mem(env, addr + 1);
  uint8_t l = read_mem(env, addr + 2);
  return ((h >> 4) * 100000) + ((h & 0xF) * 10000) + ((m >> 4) * 1000) +
         ((m & 0xF) * 100) + ((l >> 4) * 10) + (l & 0xF);
}
static inline uint16_t read_uint16(mGBA *env, uint16_t addr) {
  uint8_t low = read_mem(env, addr);
  uint8_t high = read_mem(env, addr + 1);
  return (uint16_t)(low | (high << 8));
}

static inline void write_mem(mGBA *env, uint16_t addr, uint8_t value) {
  if (env && env->core)
    env->core->rawWrite8(env->core, addr, -1, value);
}
static inline void write_bcd(mGBA *env, uint16_t addr, uint32_t value) {
  uint8_t h = ((value / 100000) << 4) | ((value / 10000) % 10);
  uint8_t m = (((value / 1000) % 10) << 4) | ((value / 100) % 10);
  uint8_t l = (((value / 10) % 10) << 4) | (value % 10);
  write_mem(env, addr, h);
  write_mem(env, addr + 1, m);
  write_mem(env, addr + 2, l);
}
static inline void write_uint16(mGBA *env, uint16_t addr, uint16_t value) {
  write_mem(env, addr, (uint8_t)(value & 0xFF));
  write_mem(env, addr + 1, (uint8_t)(value >> 8));
}

typedef struct RenderRegistryNode {
  uint32_t window_id;
  mGBA *env;
  struct RenderRegistryNode *next;
} RenderRegistryNode;

static RenderRegistryNode *g_render_registry = NULL;
static int g_sdl_video_users = 0;

static void mgba_destroy_renderer(mGBA *env);

static bool mgba_acquire_sdl_video(void) {
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
static void mgba_release_sdl_video(void) {
  if (g_sdl_video_users == 0)
    return;
  g_sdl_video_users--;
  if (g_sdl_video_users == 0) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();
  }
}
static void mgba_register_window(mGBA *env) {
  if (!env || env->window_id == 0)
    return;
  RenderRegistryNode *node = (RenderRegistryNode *)calloc(1, sizeof(RenderRegistryNode));
  if (!node)
    return;
  node->window_id = env->window_id;
  node->env = env;
  node->next = g_render_registry;
  g_render_registry = node;
}
static void mgba_unregister_window(uint32_t window_id) {
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
static mGBA *mgba_lookup_env(uint32_t window_id) {
  RenderRegistryNode *node = g_render_registry;
  while (node) {
    if (node->window_id == window_id)
      return node->env;
    node = node->next;
  }
  return NULL;
}
static SDL_Rect mgba_calculate_dest_rect(const mGBA *env) {
  SDL_Rect rect = {0, 0, 0, 0};
  if (!env || !env->window || env->video_width <= 0 || env->video_height <= 0)
    return rect;

  int window_w = 0;
  int window_h = 0;
  SDL_GetWindowSize(env->window, &window_w, &window_h);
  if (window_w <= 0 || window_h <= 0) {
    rect.w = env->video_width;
    rect.h = env->video_height;
    return rect;
  }

  int scaled_w = window_w;
  int scaled_h = (env->video_height * scaled_w) / (env->video_width ? env->video_width : 1);
  if (scaled_h > window_h) {
    scaled_h = window_h;
    scaled_w = (env->video_width * scaled_h) / (env->video_height ? env->video_height : 1);
  }

  if (scaled_w <= 0)
    scaled_w = env->video_width;
  if (scaled_h <= 0)
    scaled_h = env->video_height;

  rect.w = scaled_w;
  rect.h = scaled_h;
  rect.x = (window_w - rect.w) / 2;
  rect.y = (window_h - rect.h) / 2;
  return rect;
}
static bool mgba_ensure_renderer(mGBA *env) {
  if (!env || !env->render_enabled)
    return false;
  if (env->renderer_initialized && env->window && env->renderer && env->texture)
    return true;

  if (!mgba_acquire_sdl_video())
    return false;
  env->sdl_registered = true;

  int width = env->video_width > 0 ? env->video_width : 160;
  int height = env->video_height > 0 ? env->video_height : 144;
  env->window = SDL_CreateWindow(env->rom_path[0] ? env->rom_path : "Pokemon Red",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 width * 3, height * 3,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!env->window) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    mgba_destroy_renderer(env);
    return false;
  }

  env->renderer = SDL_CreateRenderer(env->window, -1,
                                     SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
  if (!env->renderer) {
    env->renderer = SDL_CreateRenderer(env->window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!env->renderer) {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    mgba_destroy_renderer(env);
    return false;
  }

  env->texture = SDL_CreateTexture(env->renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!env->texture) {
    fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    mgba_destroy_renderer(env);
    return false;
  }

  env->video_width = width;
  env->video_height = height;
  env->window_id = SDL_GetWindowID(env->window);
  mgba_register_window(env);
  env->renderer_initialized = true;
  SDL_ShowWindow(env->window);
  return true;
}
static void mgba_destroy_renderer(mGBA *env) {
  if (!env)
    return;

  if (env->texture) {
    SDL_DestroyTexture(env->texture);
    env->texture = NULL;
  }
  if (env->renderer) {
    SDL_DestroyRenderer(env->renderer);
    env->renderer = NULL;
  }
  if (env->window) {
    SDL_DestroyWindow(env->window);
    env->window = NULL;
  }

  if (env->window_id) {
    mgba_unregister_window(env->window_id);
    env->window_id = 0;
  }

  if (env->sdl_registered) {
    mgba_release_sdl_video();
    env->sdl_registered = false;
  }

  env->renderer_initialized = false;
}
static void mgba_dispatch_events(void) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      while (g_render_registry) {
        mGBA *target = g_render_registry->env;
        if (target) {
          target->render_enabled = false;
          mgba_destroy_renderer(target);
        } else {
          RenderRegistryNode *orphan = g_render_registry;
          g_render_registry = orphan->next;
          free(orphan);
        }
      }
      break;
    }

    if (event.type == SDL_WINDOWEVENT) {
      mGBA *target = mgba_lookup_env(event.window.windowID);
      if (!target)
        continue;
      if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
        fprintf(stderr, "Rendering disabled after window close (env %p). Recreate the environment to re-enable.\n",
                (void *)target);
        target->render_enabled = false;
        mgba_destroy_renderer(target);
      }
    }
  }
}
static void mgba_render_frame(mGBA *env) {
  if (!env || !env->render_enabled || !env->video_buffer)
    return;

  mgba_dispatch_events();
  if (!mgba_ensure_renderer(env))
    return;

  const int pitch = env->video_width * (int)sizeof(color_t);
  if (SDL_UpdateTexture(env->texture, NULL, env->video_buffer, pitch) != 0) {
    fprintf(stderr, "SDL_UpdateTexture failed: %s\n", SDL_GetError());
    return;
  }

  SDL_SetRenderDrawColor(env->renderer, 0, 0, 0, 255);
  SDL_RenderClear(env->renderer);
  SDL_Rect dest = mgba_calculate_dest_rect(env);
  SDL_RenderCopy(env->renderer, env->texture, NULL, dest.w > 0 && dest.h > 0 ? &dest : NULL);
  SDL_RenderPresent(env->renderer);
}

void mgba_init_core(mGBA *env, const char *rom_path) {
  if (!env)
    return;

  env->uses_shared_rom = false;
  env->window = NULL;
  env->renderer = NULL;
  env->texture = NULL;
  env->window_id = 0;
  env->renderer_initialized = false;
  env->sdl_registered = false;

  mLogSetDefaultLogger(&s_silentLogger);
  env->core = mCoreFind(rom_path);
  if (!env->core || !env->core->init(env->core)) {
    fprintf(stderr, "Failed to initialize mGBA core\n");
    env->core = NULL;
    return;
  }
  mCoreInitConfig(env->core, NULL);
  mCoreConfigSetValue(&env->core->config, "sgb.borders", "0");
  mCoreConfigSetValue(&env->core->config, "gb.model", "DMG");
  env->core->loadConfig(env->core, &env->core->config);
  if (!mCoreLoadFile(env->core, rom_path)) {
    fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
    env->core->deinit(env->core);
    env->core = NULL;
    return;
  }
  unsigned int w, h;
  env->core->desiredVideoDimensions(env->core, &w, &h);
  env->video_buffer = (color_t *)calloc(w * h + 256, sizeof(color_t));
  if (env->video_buffer) {
    env->core->setVideoBuffer(env->core, env->video_buffer, w);
  }
  env->video_width = (int)w;
  env->video_height = (int)h;
  
  configure_headless_mode(env->core);
  
  env->core->reset(env->core);
  strncpy(env->rom_path, rom_path, sizeof(env->rom_path) - 1);
}
bool c_save_state_file(mGBA *env, const char *path) {
  if (!env || !env->core || !path)
    return false;
  struct VFile *vf = VFileOpen(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!vf)
    return false;
  bool result = mCoreSaveStateNamed(env->core, vf, SAVESTATE_ALL);
  vf->close(vf);
  return result;
}
bool c_load_state_file(mGBA *env, const char *path) {
  if (!env || !env->core || !path)
    return false;
  struct VFile *vf = VFileOpen(path, O_RDONLY);
  if (!vf)
    return false;
  suppress_stderr();  // Silence libpng warnings
  bool result = mCoreLoadStateNamed(env->core, vf, SAVESTATE_ALL);
  restore_stderr();
  vf->close(vf);
  return result;
}
void initial_load_state(mGBA *env, const char *state_path) {
  struct VFile *vf = VFileOpen(state_path, O_RDONLY);
  if (vf) {
    suppress_stderr();
    bool load_success = mCoreLoadStateNamed(env->core, vf, SAVESTATE_ALL);
    restore_stderr();
    vf->close(vf);
    if (!load_success) {
      fprintf(stderr, "Warning: Failed to load state from file: %s\n", state_path);
    }
  } else {
    fprintf(stderr, "Warning: Could not open state file: %s\n", state_path);
  }
}

#endif // MGBA_WRAPPER_H