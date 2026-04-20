/*
 * gambatte_c.cpp — C++ implementation of the C wrapper declared in gambatte_c.h.
 *
 * This is the ONLY file in the project that touches C++.  Everything else
 * stays pure C and includes gambatte_c.h (or gambatte_wrapper.h which
 * includes it).
 *
 * Build requirement: link against libgambatte (built from gambatte-libretro
 * with -D__LIBRETRO__ so that memory-pointer helpers are available).
 *
 * Compile with:
 *   g++ -std=c++17 -O3 -c gambatte_c.cpp -I<gambatte>/libgambatte/include
 */

#include "gambatte_c.h"

#include <gambatte.h>
#include <inputgetter.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* ── Stubs for libretro-only symbols referenced by libgambatte ─────────── */
/* cartridge.cpp declares this extern; normally libretro.cpp provides it.
 * We don't use rumble, so provide a no-op. */
void cartridge_set_rumble(unsigned /*active*/) {}

/* ── Concrete InputGetter used by every instance ───────────────────────── */

class SimpleInput : public gambatte::InputGetter {
public:
    unsigned buttons_ = 0;
    unsigned operator()() override { return buttons_; }
};

/* ── Helpers ───────────────────────────────────────────────────────────── */

/* Audio samples per video frame. */
static constexpr unsigned SAMPLES_PER_FRAME = 35112;
/* We process in chunks of this many samples. */
static constexpr unsigned SAMPLES_PER_RUN   = 2064;
/* Sound buffer needs room for one chunk + possible overshoot. */
static constexpr unsigned SOUND_BUF_SZ      = SAMPLES_PER_RUN + 2064;

struct GBState {
    gambatte::GB   gb;
    SimpleInput    input;
    uint32_t       sound_buf[SOUND_BUF_SZ];
};

static inline GBState *as_state(gambatte_handle h) {
    return static_cast<GBState *>(h);
}

/* ── Public API ────────────────────────────────────────────────────────── */

extern "C" {

gambatte_handle gambatte_create(void) {
    GBState *s = new (std::nothrow) GBState();
    if (!s) return nullptr;
    s->gb.setInputGetter(&s->input);
    return static_cast<gambatte_handle>(s);
}

void gambatte_destroy(gambatte_handle gb) {
    delete as_state(gb);
}

int gambatte_load(gambatte_handle gb, const void *romdata,
                  unsigned romsize, unsigned flags) {
    if (!gb || !romdata || romsize == 0) return -1;
    return as_state(gb)->gb.load(romdata, romsize, flags);
}

void gambatte_reset(gambatte_handle gb) {
    if (gb) as_state(gb)->gb.reset();
}

void gambatte_run_frame(gambatte_handle gb, uint32_t *videoBuf) {
    if (!gb) return;
    GBState *s = as_state(gb);
    /*
     * Gambatte's runFor() processes *audio samples*, not frames.
     * One video frame = 35112 stereo samples.
     * We loop calling runFor(2064 samples) until the total reaches 35112.
     *
     * videoBuf layout: 256 * 144 uint32_t (XRGB8888).
     * Pitch = 256 pixels.  Only the leftmost 160 per row are meaningful.
     */
    unsigned total = 0;
    while (total < SAMPLES_PER_FRAME) {
        unsigned samples = SAMPLES_PER_RUN;
        s->gb.runFor(videoBuf, 256, s->sound_buf, SOUND_BUF_SZ, samples);
        total += samples;
    }
}

void gambatte_set_input(gambatte_handle gb, unsigned buttons) {
    if (gb) as_state(gb)->input.buttons_ = buttons;
}

/* ── Memory access (via __LIBRETRO__ bank pointers) ────────────────────── */
/*
 * gambatte-libretro exposes direct memory bank pointers when built with
 * -D__LIBRETRO__.  We use those instead of cpuRead/cpuWrite (which don't
 * exist in the public API).  This covers the full Game Boy address space
 * that Pokemon Red's game code touches (all in 0xC000–0xDFFF WRAM).
 */

uint8_t gambatte_read_mem(gambatte_handle gb, uint16_t addr) {
    if (!gb) return 0;
    gambatte::GB &g = as_state(gb)->gb;

    if (addr >= 0xC000 && addr <= 0xCFFF)
        return static_cast<uint8_t *>(g.rambank0_ptr())[addr - 0xC000];
    if (addr >= 0xD000 && addr <= 0xDFFF)
        return static_cast<uint8_t *>(g.rambank1_ptr())[addr - 0xD000];
    if (addr >= 0xFF80 && addr <= 0xFFFE)
        return static_cast<uint8_t *>(g.zeropage_ptr())[addr - 0xFF80];
    if (addr >= 0xFE00 && addr <= 0xFE9F)
        return static_cast<uint8_t *>(g.oamram_ptr())[addr - 0xFE00];
    if (addr >= 0x8000 && addr <= 0x9FFF)
        return static_cast<uint8_t *>(g.vram_ptr())[addr - 0x8000];
    if (addr <= 0x3FFF)
        return static_cast<uint8_t *>(g.rombank0_ptr())[addr];
    if (addr >= 0x4000 && addr <= 0x7FFF)
        return static_cast<uint8_t *>(g.rombank1_ptr())[addr - 0x4000];

    return 0xFF; /* I/O registers — not reachable via bank pointers */
}

void gambatte_write_mem(gambatte_handle gb, uint16_t addr, uint8_t val) {
    if (!gb) return;
    gambatte::GB &g = as_state(gb)->gb;

    if (addr >= 0xC000 && addr <= 0xCFFF)
        static_cast<uint8_t *>(g.rambank0_ptr())[addr - 0xC000] = val;
    else if (addr >= 0xD000 && addr <= 0xDFFF)
        static_cast<uint8_t *>(g.rambank1_ptr())[addr - 0xD000] = val;
    else if (addr >= 0xFF80 && addr <= 0xFFFE)
        static_cast<uint8_t *>(g.zeropage_ptr())[addr - 0xFF80] = val;
    /* Writes to ROM/VRAM/OAM/IO silently ignored — game only writes WRAM */
}

/* ── Save-state ────────────────────────────────────────────────────────── */

size_t gambatte_state_size(gambatte_handle gb) {
    if (!gb) return 0;
    return as_state(gb)->gb.stateSize();
}

void gambatte_save_state(gambatte_handle gb, void *buf) {
    if (!gb || !buf) return;
    as_state(gb)->gb.saveState(buf);
}

void gambatte_load_state(gambatte_handle gb, const void *buf) {
    if (!gb || !buf) return;
    as_state(gb)->gb.loadState(buf);
}

bool gambatte_save_state_file(gambatte_handle gb, const char *path) {
    if (!gb || !path) return false;
    GBState *s = as_state(gb);
    size_t sz = s->gb.stateSize();
    void *buf = malloc(sz);
    if (!buf) return false;

    s->gb.saveState(buf);

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return false; }
    bool ok = fwrite(buf, 1, sz, f) == sz;
    fclose(f);
    free(buf);
    return ok;
}

bool gambatte_load_state_file(gambatte_handle gb, const char *path) {
    if (!gb || !path) return false;
    GBState *s = as_state(gb);
    size_t expected = s->gb.stateSize();

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || (size_t)len < expected) {
        fclose(f);
        return false;
    }

    void *buf = malloc(expected);
    if (!buf) { fclose(f); return false; }

    bool ok = fread(buf, 1, expected, f) == expected;
    fclose(f);

    if (ok) s->gb.loadState(buf);
    free(buf);
    return ok;
}

} /* extern "C" */
