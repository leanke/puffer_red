/*
 * optim.h — Performance helpers for the Gambatte-based emulator wrapper.
 *
 * Provides:
 *   - Branch-prediction hints (LIKELY / UNLIKELY)
 *   - Prefetch intrinsics
 *   - STEP_N_FRAMES / STEP_ACTION_FRAMES macros (press-hold-release)
 *   - Process-local shared ROM buffer (one file-read per worker process)
 */
#ifndef OPTIM_H
#define OPTIM_H

#include <stdbool.h>
#include <stdint.h>
#include "gambatte_c.h"

/* ── Branch hints ──────────────────────────────────────────────────────── */

#ifndef LIKELY
  #ifdef __GNUC__
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
  #else
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
  #endif
#endif

/* ── Prefetch ──────────────────────────────────────────────────────────── */

#ifdef __GNUC__
  #define PREFETCH_READ(ptr)  __builtin_prefetch((ptr), 0, 3)
  #define PREFETCH_WRITE(ptr) __builtin_prefetch((ptr), 1, 3)
#else
  #define PREFETCH_READ(ptr)  ((void)0)
  #define PREFETCH_WRITE(ptr) ((void)0)
#endif

/* ── Frame-stepping macros ─────────────────────────────────────────────── */

/*
 * Run N emulator frames with the same button mask held.
 *
 * `gb`   — gambatte_handle  (opaque pointer)
 * `keys` — button bitmask (A=0x01 … DOWN=0x80)
 * `vbuf` — uint32_t* video buffer (256*144), or NULL for headless
 * `n`    — number of frames
 */
#define STEP_N_FRAMES(gb, keys, vbuf, n) do {                \
    gambatte_handle _gb = (gb);                              \
    if (LIKELY(_gb != NULL)) {                               \
        gambatte_set_input(_gb, (keys) & 0xFF);              \
        for (int _i = 0; _i < (n); _i++) {                  \
            gambatte_run_frame(_gb, (vbuf));                 \
        }                                                    \
    }                                                        \
} while(0)

/*
 * Press-hold-release: hold `keys` for `press_n` frames, then idle (keys=0)
 * for the remaining frames up to `total_n`.
 */
#define STEP_ACTION_FRAMES(gb, keys, vbuf, press_n, total_n) do {  \
    gambatte_handle _gb = (gb);                                    \
    int _press = (press_n);                                        \
    int _total = (total_n);                                        \
    if (_press > _total) _press = _total;                          \
    if (LIKELY(_gb != NULL)) {                                     \
        gambatte_set_input(_gb, (keys) & 0xFF);                    \
        for (int _i = 0; _i < _press; _i++)                       \
            gambatte_run_frame(_gb, (vbuf));                       \
        gambatte_set_input(_gb, 0);                                \
        for (int _i = _press; _i < _total; _i++)                  \
            gambatte_run_frame(_gb, (vbuf));                       \
    }                                                              \
} while(0)

/* ── Process-local shared ROM buffer ───────────────────────────────────── */

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

static inline void *get_shared_rom(void)       { return g_shared_rom_data; }
static inline size_t get_shared_rom_size(void)  { return g_shared_rom_size; }

static inline void release_shared_rom(void) {
    if (g_shared_rom_refs > 0) g_shared_rom_refs--;
    if (g_shared_rom_refs == 0 && g_shared_rom_data) {
        free(g_shared_rom_data);
        g_shared_rom_data = NULL;
        g_shared_rom_size = 0;
    }
}

#endif /* OPTIM_H */
