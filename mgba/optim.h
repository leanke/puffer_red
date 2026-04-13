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

// Shared ROM stubs (disabled — mmap issues with Python multiprocessing)
static inline void release_shared_rom(void) {}
static inline void* get_shared_rom(void) { return NULL; }
static inline size_t get_shared_rom_size(void) { return 0; }

static inline void configure_headless_mode(struct mCore* core) {
    if (UNLIKELY(!core)) return;
    core->setAudioBufferSize(core, 0);
    mCoreConfigSetValue(&core->config, "audio.quality", "0");
    mCoreConfigSetValue(&core->config, "audio.volume", "0");
}

#endif // OPTIM_H