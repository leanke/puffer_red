#ifndef OPTIM_H
#define OPTIM_H

#include <stdint.h>
#include <stdbool.h>

#ifndef LIKELY
  #ifdef __GNUC__
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
  #else
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
  #endif
#endif

#ifdef __GNUC__
  #define PREFETCH_READ(ptr)  __builtin_prefetch((ptr), 0, 3)
  #define PREFETCH_WRITE(ptr) __builtin_prefetch((ptr), 1, 3)
#else
  #define PREFETCH_READ(ptr)  ((void)0)
  #define PREFETCH_WRITE(ptr) ((void)0)
#endif

// Run N emulator frames with the same key held
#define STEP_N_FRAMES(core, keys, n) do {           \
    struct mCore* _core = (core);                   \
    if (LIKELY(_core != NULL)) {                    \
        _core->setKeys(_core, (keys) & 0xFF);       \
        for (int _i = 0; _i < (n); _i++) {          \
            _core->runFrame(_core);                 \
        }                                           \
    }                                               \
} while(0)

// Press-hold-release: hold keys for press_n frames, then idle for remaining
#define STEP_ACTION_FRAMES(core, keys, press_n, total_n) do {  \
    struct mCore* _core = (core);                              \
    int _press = (press_n);                                    \
    int _total = (total_n);                                    \
    if (_press > _total) _press = _total;                      \
    if (LIKELY(_core != NULL)) {                               \
        _core->setKeys(_core, (keys) & 0xFF);                  \
        for (int _i = 0; _i < _press; _i++)                    \
            _core->runFrame(_core);                            \
        _core->setKeys(_core, 0);                               \
        for (int _i = _press; _i < _total; _i++)               \
            _core->runFrame(_core);                            \
    }                                                          \
} while(0)

// Process-local shared ROM buffer.
// All envs within the same OS process share one copy of the ROM in memory.
// Safe with Python multiprocessing: each forked worker gets its own globals
// via copy-on-write, and the first env_init in each worker reads the file once.
#include <sys/stat.h>

static void  *g_shared_rom_data = NULL;
static size_t g_shared_rom_size = 0;
static int    g_shared_rom_refs = 0;

static inline bool acquire_shared_rom(const char *path) {
    if (g_shared_rom_data) {
        g_shared_rom_refs++;
        return true;
    }
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    struct stat st;
    if (fstat(fileno(f), &st) != 0) { fclose(f); return false; }
    g_shared_rom_size = (size_t)st.st_size;
    g_shared_rom_data = malloc(g_shared_rom_size);
    if (!g_shared_rom_data) { fclose(f); return false; }
    if (fread(g_shared_rom_data, 1, g_shared_rom_size, f) != g_shared_rom_size) {
        free(g_shared_rom_data);
        g_shared_rom_data = NULL;
        g_shared_rom_size = 0;
        fclose(f);
        return false;
    }
    fclose(f);
    g_shared_rom_refs = 1;
    return true;
}
static inline void *get_shared_rom(void)  { return g_shared_rom_data; }
static inline size_t get_shared_rom_size(void) { return g_shared_rom_size; }
static inline void release_shared_rom(void) {
    if (g_shared_rom_refs > 0) g_shared_rom_refs--;
    if (g_shared_rom_refs == 0 && g_shared_rom_data) {
        free(g_shared_rom_data);
        g_shared_rom_data = NULL;
        g_shared_rom_size = 0;
    }
}

static inline void configure_headless_mode(struct mCore* core) {
    if (UNLIKELY(!core)) return;
    core->setAudioBufferSize(core, 0);
    mCoreConfigSetValue(&core->config, "audio.quality", "0");
    mCoreConfigSetValue(&core->config, "audio.volume", "0");
}

#endif // OPTIM_H