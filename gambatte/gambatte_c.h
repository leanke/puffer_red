/*
 * gambatte_c.h — C-compatible API wrapping libgambatte (C++) for use from
 * pure-C game modules.  Every function here has extern "C" linkage when
 * compiled with a C++ compiler, so the game code never touches C++ directly.
 */
#ifndef GAMBATTE_C_H
#define GAMBATTE_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle — points to gambatte::GB on the C++ side. */
typedef void *gambatte_handle;

/* ── Instance lifecycle ────────────────────────────────────────────────── */

gambatte_handle gambatte_create(void);
void            gambatte_destroy(gambatte_handle gb);

/* Load ROM from memory buffer.
 * flags:  1 = FORCE_DMG (original Game Boy mode — use for Pokémon Red).
 * Returns 0 on success. */
int  gambatte_load(gambatte_handle gb, const void *romdata,
                   unsigned romsize, unsigned flags);

/* Full power-cycle reset. */
void gambatte_reset(gambatte_handle gb);

/* ── Frame stepping ────────────────────────────────────────────────────── */

/* Run exactly one full video frame (35112 audio samples).
 * videoBuf must hold at least 256*144 uint32_t (XRGB8888, pitch=256).
 * Pass NULL for videoBuf if you don't need the pixels (headless). */
void gambatte_run_frame(gambatte_handle gb, uint32_t *videoBuf);

/* ── Input ─────────────────────────────────────────────────────────────── */

/* Set button bitmask.  Bits:
 *   A=0x01 B=0x02 SELECT=0x04 START=0x08
 *   RIGHT=0x10 LEFT=0x20 UP=0x40 DOWN=0x80
 * Stays latched until the next call. */
void gambatte_set_input(gambatte_handle gb, unsigned buttons);

/* ── Memory access ─────────────────────────────────────────────────────── */

/* Direct read/write into the full 64 KiB address space (via cpuRead/cpuWrite
 * exposed in the modified libgambatte).  Works for any address the CPU can
 * see: ROM (0x0000-0x7FFF), VRAM (0x8000-0x9FFF), cart RAM (0xA000-0xBFFF),
 * WRAM (0xC000-0xDFFF), OAM (0xFE00-0xFE9F), IO (0xFF00-0xFF7F),
 * HRAM (0xFF80-0xFFFE), IE (0xFFFF). */
uint8_t gambatte_read_mem(gambatte_handle gb, uint16_t addr);
void    gambatte_write_mem(gambatte_handle gb, uint16_t addr, uint8_t val);

/* ── Save-state ────────────────────────────────────────────────────────── */

/* Size of a serialised save-state in bytes. */
size_t gambatte_state_size(gambatte_handle gb);

/* Serialise the current state into buf (must be ≥ gambatte_state_size). */
void gambatte_save_state(gambatte_handle gb, void *buf);

/* Restore state from buf. */
void gambatte_load_state(gambatte_handle gb, const void *buf);

/* Save state to a file.  Returns true on success. */
bool gambatte_save_state_file(gambatte_handle gb, const char *path);

/* Load state from a file.  Returns true on success. */
bool gambatte_load_state_file(gambatte_handle gb, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* GAMBATTE_C_H */
