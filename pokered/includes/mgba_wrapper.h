/**
 * @file mgba_wrapper.h
 * @brief A wrapper header for integrating mGBA emulator into projects
 * 
 * This header provides a simplified interface for:
 * - Loading and initializing mGBA cores (GBA and GB/GBC)
 * - Loading ROMs and BIOS files
 * - Managing video rendering (conditional rendering support)
 * - Saving and loading game states
 * - Managing save data (SRAM)
 * 
 * @note Requires linking against libmgba
 * @see https://github.com/mgba-emu/mgba
 * 
 * License: Mozilla Public License 2.0
 */

#ifndef MGBA_WRAPPER_H
#define MGBA_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * INCLUDES
 *============================================================================*/

#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba/core/config.h>
#include <mgba/core/interface.h>
#include <mgba-util/vfs.h>

/* Platform-specific core headers */
#include <mgba/gba/core.h>  /* GBA core: GBACoreCreate() */
#include <mgba/gb/core.h>   /* GB/GBC core: GBCoreCreate() */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * CONSTANTS
 *============================================================================*/

/** GBA screen dimensions */
#define MGBA_GBA_WIDTH  240
#define MGBA_GBA_HEIGHT 160

/** GB/GBC screen dimensions */
#define MGBA_GB_WIDTH   160
#define MGBA_GB_HEIGHT  144

/** SGB screen dimensions (with border) */
#define MGBA_SGB_WIDTH  256
#define MGBA_SGB_HEIGHT 224

/** Maximum video buffer size (accounts for largest possible output) */
#define MGBA_VIDEO_BUFFER_SIZE (MGBA_SGB_WIDTH * MGBA_SGB_HEIGHT * sizeof(uint32_t))

/** Savestate flags for mCoreSaveStateNamed / mCoreLoadStateNamed */
#define MGBA_SAVESTATE_SCREENSHOT  SAVESTATE_SCREENSHOT  /* Include screenshot (1) */
#define MGBA_SAVESTATE_SAVEDATA    SAVESTATE_SAVEDATA    /* Include SRAM (2) */
#define MGBA_SAVESTATE_CHEATS      SAVESTATE_CHEATS      /* Include cheats (4) */
#define MGBA_SAVESTATE_RTC         SAVESTATE_RTC         /* Include RTC data (8) */
#define MGBA_SAVESTATE_METADATA    SAVESTATE_METADATA    /* Include metadata (16) */
#define MGBA_SAVESTATE_ALL         SAVESTATE_ALL         /* All flags (31) */

/*============================================================================
 * VERSION COMPATIBILITY
 *============================================================================*/

/**
 * mGBA API compatibility layer
 * Version 0.10 uses desiredVideoDimensions, version 0.11 uses baseVideoSize
 * We detect the API by checking for the presence of baseVideoSize
 */

/* Macro to get base video dimensions - works with both 0.10 and 0.11 */
#ifndef MGBA_GET_VIDEO_DIMENSIONS
#define MGBA_GET_VIDEO_DIMENSIONS(core, w, h) \
    do { \
        if ((core)->desiredVideoDimensions) { \
            (core)->desiredVideoDimensions((core), (w), (h)); \
        } \
    } while(0)
#endif

/*============================================================================
 * TYPE DEFINITIONS
 *============================================================================*/

/**
 * @brief Wrapper context for mGBA emulator instance
 */
typedef struct MGBAContext {
    struct mCore* core;           /**< The mGBA core instance */
    uint32_t* videoBuffer;        /**< Video output buffer (RGBA8888) */
    size_t videoBufferStride;     /**< Stride of the video buffer */
    unsigned width;               /**< Current video width */
    unsigned height;              /**< Current video height */
    bool initialized;             /**< Whether the context is initialized */
    bool romLoaded;               /**< Whether a ROM is currently loaded */
    enum mPlatform platform;      /**< Current platform (GBA, GB) */
} MGBAContext;

/**
 * @brief Render callback function type for conditional rendering
 * @param ctx The mGBA context
 * @param buffer The pixel buffer (RGBA8888 format)
 * @param width Frame width
 * @param height Frame height
 * @param stride Buffer stride
 * @param userData User-provided data pointer
 * @return true to continue rendering, false to skip this frame
 */
typedef bool (*MGBARenderCallback)(MGBAContext* ctx, const uint32_t* buffer,
                                   unsigned width, unsigned height,
                                   size_t stride, void* userData);

/*============================================================================
 * CORE INITIALIZATION AND LIFECYCLE
 *============================================================================*/

/**
 * @brief Create and initialize an mGBA context
 * 
 * Creates a new emulator context. Call mgba_destroy() when done.
 * 
 * @return Newly allocated context, or NULL on failure
 * 
 * @code
 * MGBAContext* ctx = mgba_create();
 * if (ctx) {
 *     // Use the context
 *     mgba_destroy(ctx);
 * }
 * @endcode
 */
static inline MGBAContext* mgba_create(void) {
    MGBAContext* ctx = (MGBAContext*)calloc(1, sizeof(MGBAContext));
    if (!ctx) return NULL;
    
    ctx->platform = mPLATFORM_NONE;
    ctx->initialized = false;
    ctx->romLoaded = false;
    
    return ctx;
}

/**
 * @brief Destroy an mGBA context and free all resources
 * 
 * @param ctx Context to destroy (safe to pass NULL)
 */
static inline void mgba_destroy(MGBAContext* ctx) {
    if (!ctx) return;
    
    if (ctx->core) {
        if (ctx->romLoaded) {
            ctx->core->unloadROM(ctx->core);
        }
        ctx->core->deinit(ctx->core);
        ctx->core = NULL;
    }
    
    if (ctx->videoBuffer) {
        free(ctx->videoBuffer);
        ctx->videoBuffer = NULL;
    }
    
    free(ctx);
}

/**
 * @brief Initialize the core for a specific platform
 * 
 * @param ctx Context to initialize
 * @param platform Platform type (mPLATFORM_GBA or mPLATFORM_GB)
 * @return true on success, false on failure
 */
static inline bool mgba_init_core(MGBAContext* ctx, enum mPlatform platform) {
    if (!ctx || ctx->initialized) return false;
    
    /* Create the appropriate core */
    switch (platform) {
        case mPLATFORM_GBA:
            ctx->core = GBACoreCreate();
            ctx->width = MGBA_GBA_WIDTH;
            ctx->height = MGBA_GBA_HEIGHT;
            break;
        case mPLATFORM_GB:
            ctx->core = GBCoreCreate();
            ctx->width = MGBA_GB_WIDTH;
            ctx->height = MGBA_GB_HEIGHT;
            break;
        default:
            return false;
    }
    
    if (!ctx->core) return false;
    
    /* Initialize the core */
    if (!ctx->core->init(ctx->core)) {
        ctx->core = NULL;
        return false;
    }
    
    ctx->platform = platform;
    
    /* Initialize configuration */
    mCoreInitConfig(ctx->core, NULL);
    
    /* Allocate video buffer */
    ctx->videoBufferStride = ctx->width;
    ctx->videoBuffer = (uint32_t*)calloc(ctx->width * ctx->height, sizeof(uint32_t));
    if (!ctx->videoBuffer) {
        ctx->core->deinit(ctx->core);
        ctx->core = NULL;
        return false;
    }
    
    /* Set video buffer */
    ctx->core->setVideoBuffer(ctx->core, (color_t*)ctx->videoBuffer, ctx->videoBufferStride);
    
    ctx->initialized = true;
    return true;
}

/**
 * @brief Auto-detect platform and initialize core from ROM file
 * 
 * Automatically detects whether the ROM is a GBA or GB/GBC game
 * and initializes the appropriate core.
 * 
 * @param ctx Context to initialize
 * @param romPath Path to the ROM file
 * @return true on success, false on failure
 */
static inline bool mgba_init_from_file(MGBAContext* ctx, const char* romPath) {
    if (!ctx || ctx->initialized || !romPath) return false;
    
#ifdef ENABLE_VFS
    /* Use mCoreFind to auto-detect and create the appropriate core */
    ctx->core = mCoreFind(romPath);
    if (!ctx->core) return false;
    
    /* Initialize the core */
    if (!ctx->core->init(ctx->core)) {
        ctx->core = NULL;
        return false;
    }
    
    /* Get platform and dimensions */
    ctx->platform = ctx->core->platform(ctx->core);
    MGBA_GET_VIDEO_DIMENSIONS(ctx->core, &ctx->width, &ctx->height);
    
    /* Initialize configuration */
    mCoreInitConfig(ctx->core, NULL);
    
    /* Allocate video buffer */
    ctx->videoBufferStride = ctx->width;
    ctx->videoBuffer = (uint32_t*)calloc(ctx->width * ctx->height, sizeof(uint32_t));
    if (!ctx->videoBuffer) {
        ctx->core->deinit(ctx->core);
        ctx->core = NULL;
        return false;
    }
    
    /* Set video buffer */
    ctx->core->setVideoBuffer(ctx->core, (color_t*)ctx->videoBuffer, ctx->videoBufferStride);
    
    ctx->initialized = true;
    return true;
#else
    (void)romPath;
    return false;
#endif
}

/*============================================================================
 * ROM AND BIOS LOADING
 *============================================================================*/

/**
 * @brief Load a ROM from a file path
 * 
 * @param ctx Initialized context
 * @param romPath Path to the ROM file
 * @return true on success, false on failure
 * 
 * @code
 * if (mgba_load_rom(ctx, "game.gba")) {
 *     mgba_reset(ctx);  // Reset to start execution
 * }
 * @endcode
 */
static inline bool mgba_load_rom(MGBAContext* ctx, const char* romPath) {
    if (!ctx || !ctx->initialized || !ctx->core || !romPath) return false;
    
#ifdef ENABLE_VFS
    if (!mCoreLoadFile(ctx->core, romPath)) {
        return false;
    }
    ctx->romLoaded = true;
    return true;
#else
    (void)romPath;
    return false;
#endif
}

/**
 * @brief Load a ROM from a VFile (virtual file)
 * 
 * Useful for loading ROMs from memory or custom file systems.
 * 
 * @param ctx Initialized context
 * @param vf VFile containing the ROM data (ownership transferred to core)
 * @return true on success, false on failure
 */
static inline bool mgba_load_rom_vfile(MGBAContext* ctx, struct VFile* vf) {
    if (!ctx || !ctx->initialized || !ctx->core || !vf) return false;
    
    if (!ctx->core->loadROM(ctx->core, vf)) {
        return false;
    }
    ctx->romLoaded = true;
    return true;
}

/**
 * @brief Load a ROM from memory buffer
 * 
 * @param ctx Initialized context
 * @param data ROM data buffer
 * @param size Size of the ROM data
 * @return true on success, false on failure
 */
static inline bool mgba_load_rom_from_memory(MGBAContext* ctx, void* data, size_t size) {
    if (!ctx || !ctx->initialized || !ctx->core || !data || size == 0) return false;
    
    struct VFile* vf = VFileFromMemory(data, size);
    if (!vf) return false;
    
    return mgba_load_rom_vfile(ctx, vf);
}

/**
 * @brief Load a BIOS file
 * 
 * @param ctx Initialized context
 * @param biosPath Path to the BIOS file
 * @return true on success, false on failure
 */
static inline bool mgba_load_bios(MGBAContext* ctx, const char* biosPath) {
    if (!ctx || !ctx->initialized || !ctx->core || !biosPath) return false;
    
#ifdef ENABLE_VFS
    struct VFile* vf = VFileOpen(biosPath, O_RDONLY);
    if (!vf) return false;
    
    return ctx->core->loadBIOS(ctx->core, vf, 0);
#else
    (void)biosPath;
    return false;
#endif
}

/**
 * @brief Unload the currently loaded ROM
 * 
 * @param ctx Context with loaded ROM
 */
static inline void mgba_unload_rom(MGBAContext* ctx) {
    if (!ctx || !ctx->core || !ctx->romLoaded) return;
    
    ctx->core->unloadROM(ctx->core);
    ctx->romLoaded = false;
}

/*============================================================================
 * EMULATION CONTROL
 *============================================================================*/

/**
 * @brief Reset the emulator
 * 
 * Must be called after loading a ROM before running frames.
 * 
 * @param ctx Context with loaded ROM
 */
static inline void mgba_reset(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return;
    ctx->core->reset(ctx->core);
}

/**
 * @brief Run a single frame of emulation
 * 
 * @param ctx Context with loaded ROM
 */
static inline void mgba_run_frame(MGBAContext* ctx) {
    if (!ctx || !ctx->core || !ctx->romLoaded) return;
    ctx->core->runFrame(ctx->core);
}

/**
 * @brief Run emulation continuously (one iteration)
 * 
 * This runs a small unit of emulation, useful for frame timing control.
 * 
 * @param ctx Context with loaded ROM
 */
static inline void mgba_run_loop(MGBAContext* ctx) {
    if (!ctx || !ctx->core || !ctx->romLoaded) return;
    ctx->core->runLoop(ctx->core);
}

/**
 * @brief Step a single CPU instruction
 * 
 * @param ctx Context with loaded ROM
 */
static inline void mgba_step(MGBAContext* ctx) {
    if (!ctx || !ctx->core || !ctx->romLoaded) return;
    ctx->core->step(ctx->core);
}

/*============================================================================
 * INPUT HANDLING
 *============================================================================*/

/** GBA/GB button constants */
typedef enum MGBAKey {
    MGBA_KEY_A      = 0,
    MGBA_KEY_B      = 1,
    MGBA_KEY_SELECT = 2,
    MGBA_KEY_START  = 3,
    MGBA_KEY_RIGHT  = 4,
    MGBA_KEY_LEFT   = 5,
    MGBA_KEY_UP     = 6,
    MGBA_KEY_DOWN   = 7,
    MGBA_KEY_R      = 8,
    MGBA_KEY_L      = 9
} MGBAKey;

/**
 * @brief Set the current button state
 * 
 * @param ctx Context with loaded ROM
 * @param keys Bitmask of pressed keys (1 << MGBA_KEY_*)
 */
static inline void mgba_set_keys(MGBAContext* ctx, uint32_t keys) {
    if (!ctx || !ctx->core) return;
    ctx->core->setKeys(ctx->core, keys);
}

/**
 * @brief Add keys to the current state (press)
 * 
 * @param ctx Context with loaded ROM
 * @param keys Bitmask of keys to press
 */
static inline void mgba_add_keys(MGBAContext* ctx, uint32_t keys) {
    if (!ctx || !ctx->core) return;
    ctx->core->addKeys(ctx->core, keys);
}

/**
 * @brief Remove keys from the current state (release)
 * 
 * @param ctx Context with loaded ROM
 * @param keys Bitmask of keys to release
 */
static inline void mgba_clear_keys(MGBAContext* ctx, uint32_t keys) {
    if (!ctx || !ctx->core) return;
    ctx->core->clearKeys(ctx->core, keys);
}

/**
 * @brief Get the current key state
 * 
 * @param ctx Context with loaded ROM
 * @return Current bitmask of pressed keys
 */
static inline uint32_t mgba_get_keys(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->getKeys(ctx->core);
}

/*============================================================================
 * VIDEO / RENDERING
 *============================================================================*/

/**
 * @brief Get the current video dimensions
 * 
 * @param ctx Initialized context
 * @param width Output: frame width
 * @param height Output: frame height
 */
static inline void mgba_get_video_size(MGBAContext* ctx, unsigned* width, unsigned* height) {
    if (!ctx || !ctx->core) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    MGBA_GET_VIDEO_DIMENSIONS(ctx->core, width, height);
}

/**
 * @brief Get the base (unscaled) video dimensions
 * 
 * @param ctx Initialized context
 * @param width Output: base frame width
 * @param height Output: base frame height
 */
static inline void mgba_get_base_video_size(MGBAContext* ctx, unsigned* width, unsigned* height) {
    if (!ctx || !ctx->core) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    MGBA_GET_VIDEO_DIMENSIONS(ctx->core, width, height);
}

/**
 * @brief Get the video buffer pointer
 * 
 * Returns a pointer to the internal video buffer containing the current frame.
 * The format is RGBA8888 (or platform native mColor).
 * 
 * @param ctx Context with loaded ROM
 * @return Pointer to video buffer, or NULL on error
 */
static inline const uint32_t* mgba_get_video_buffer(MGBAContext* ctx) {
    if (!ctx) return NULL;
    return ctx->videoBuffer;
}

/**
 * @brief Get pixels from the renderer
 * 
 * @param ctx Context with loaded ROM
 * @param buffer Output: pointer to pixel buffer
 * @param stride Output: buffer stride
 */
static inline void mgba_get_pixels(MGBAContext* ctx, const void** buffer, size_t* stride) {
    if (!ctx || !ctx->core) {
        if (buffer) *buffer = NULL;
        if (stride) *stride = 0;
        return;
    }
    ctx->core->getPixels(ctx->core, buffer, stride);
}

/**
 * @brief Set a custom video buffer
 * 
 * @param ctx Initialized context
 * @param buffer Custom buffer (must be at least width * height * sizeof(uint32_t))
 * @param stride Buffer stride in pixels
 * @return true on success, false on failure
 */
static inline bool mgba_set_video_buffer(MGBAContext* ctx, uint32_t* buffer, size_t stride) {
    if (!ctx || !ctx->core || !buffer) return false;
    
    ctx->core->setVideoBuffer(ctx->core, (color_t*)buffer, stride);
    
    /* Update internal tracking (don't free user-provided buffer) */
    if (ctx->videoBuffer && ctx->videoBuffer != buffer) {
        free(ctx->videoBuffer);
    }
    ctx->videoBuffer = buffer;
    ctx->videoBufferStride = stride;
    
    return true;
}

/**
 * @brief Run a frame with conditional rendering
 * 
 * Executes a frame and calls the render callback with the result.
 * If the callback returns false, rendering can be skipped for frame-skip.
 * 
 * @param ctx Context with loaded ROM
 * @param callback Render callback function
 * @param userData User data passed to callback
 * @return Result of the callback, or false on error
 * 
 * @code
 * bool my_render(MGBAContext* ctx, const uint32_t* buffer,
 *                unsigned w, unsigned h, size_t stride, void* ud) {
 *     // Only render every other frame
 *     static int frameCount = 0;
 *     if (++frameCount % 2 == 0) return false;  // Skip frame
 *     
 *     // Render the frame
 *     render_to_screen(buffer, w, h, stride);
 *     return true;
 * }
 * 
 * mgba_run_frame_conditional(ctx, my_render, NULL);
 * @endcode
 */
static inline bool mgba_run_frame_conditional(MGBAContext* ctx, 
                                              MGBARenderCallback callback,
                                              void* userData) {
    if (!ctx || !ctx->core || !ctx->romLoaded || !callback) return false;
    
    ctx->core->runFrame(ctx->core);
    
    unsigned width, height;
    MGBA_GET_VIDEO_DIMENSIONS(ctx->core, &width, &height);
    
    return callback(ctx, ctx->videoBuffer, width, height, 
                    ctx->videoBufferStride, userData);
}

/**
 * @brief Get the current frame counter
 * 
 * @param ctx Context with loaded ROM
 * @return Frame counter value
 */
static inline uint32_t mgba_get_frame_counter(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->frameCounter(ctx->core);
}

/*============================================================================
 * SAVE STATES
 *============================================================================*/

/**
 * @brief Save state to a file
 * 
 * @param ctx Context with loaded ROM
 * @param path File path to save state to
 * @param flags Savestate flags (MGBA_SAVESTATE_*)
 * @return true on success, false on failure
 * 
 * @code
 * // Save with screenshot and SRAM
 * mgba_save_state_to_file(ctx, "save.ss0", 
 *     MGBA_SAVESTATE_SCREENSHOT | MGBA_SAVESTATE_SAVEDATA);
 * 
 * // Save everything
 * mgba_save_state_to_file(ctx, "save.ss0", MGBA_SAVESTATE_ALL);
 * @endcode
 */
static inline bool mgba_save_state_to_file(MGBAContext* ctx, const char* path, int flags) {
    if (!ctx || !ctx->core || !ctx->romLoaded || !path) return false;
    
#ifdef ENABLE_VFS
    struct VFile* vf = VFileOpen(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!vf) return false;
    
    bool success = mCoreSaveStateNamed(ctx->core, vf, flags);
    vf->close(vf);
    return success;
#else
    (void)path;
    (void)flags;
    return false;
#endif
}

/**
 * @brief Load state from a file
 * 
 * @param ctx Context with loaded ROM
 * @param path File path to load state from
 * @param flags Savestate flags (MGBA_SAVESTATE_*)
 * @return true on success, false on failure
 */
static inline bool mgba_load_state_from_file(MGBAContext* ctx, const char* path, int flags) {
    if (!ctx || !ctx->core || !ctx->romLoaded || !path) return false;
    
#ifdef ENABLE_VFS
    struct VFile* vf = VFileOpen(path, O_RDONLY);
    if (!vf) return false;
    
    bool success = mCoreLoadStateNamed(ctx->core, vf, flags);
    vf->close(vf);
    return success;
#else
    (void)path;
    (void)flags;
    return false;
#endif
}

/**
 * @brief Save state to a slot (0-9)
 * 
 * @param ctx Context with loaded ROM
 * @param slot Slot number (0-9)
 * @param flags Savestate flags (MGBA_SAVESTATE_*)
 * @return true on success, false on failure
 */
static inline bool mgba_save_state_slot(MGBAContext* ctx, int slot, int flags) {
    if (!ctx || !ctx->core || !ctx->romLoaded) return false;
    if (slot < 0 || slot > 9) return false;
    
#if defined(ENABLE_VFS) && defined(ENABLE_DIRECTORIES)
    return mCoreSaveState(ctx->core, slot, flags);
#else
    (void)slot;
    (void)flags;
    return false;
#endif
}

/**
 * @brief Load state from a slot (0-9)
 * 
 * @param ctx Context with loaded ROM
 * @param slot Slot number (0-9)
 * @param flags Savestate flags (MGBA_SAVESTATE_*)
 * @return true on success, false on failure
 */
static inline bool mgba_load_state_slot(MGBAContext* ctx, int slot, int flags) {
    if (!ctx || !ctx->core || !ctx->romLoaded) return false;
    if (slot < 0 || slot > 9) return false;
    
#if defined(ENABLE_VFS) && defined(ENABLE_DIRECTORIES)
    return mCoreLoadState(ctx->core, slot, flags);
#else
    (void)slot;
    (void)flags;
    return false;
#endif
}

/**
 * @brief Get the size needed for a raw state buffer
 * 
 * @param ctx Context with loaded ROM
 * @return Size in bytes, or 0 on error
 */
static inline size_t mgba_get_state_size(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->stateSize(ctx->core);
}

/**
 * @brief Save state to a memory buffer
 * 
 * @param ctx Context with loaded ROM
 * @param buffer Buffer to save state to (must be at least mgba_get_state_size() bytes)
 * @return true on success, false on failure
 */
static inline bool mgba_save_state_to_buffer(MGBAContext* ctx, void* buffer) {
    if (!ctx || !ctx->core || !ctx->romLoaded || !buffer) return false;
    return ctx->core->saveState(ctx->core, buffer);
}

/**
 * @brief Load state from a memory buffer
 * 
 * @param ctx Context with loaded ROM
 * @param buffer Buffer containing the state data
 * @return true on success, false on failure
 */
static inline bool mgba_load_state_from_buffer(MGBAContext* ctx, const void* buffer) {
    if (!ctx || !ctx->core || !ctx->romLoaded || !buffer) return false;
    return ctx->core->loadState(ctx->core, buffer);
}

/**
 * @brief Save state to a VFile with flags
 * 
 * @param ctx Context with loaded ROM
 * @param vf VFile to save to
 * @param flags Savestate flags (MGBA_SAVESTATE_*)
 * @return true on success, false on failure
 */
static inline bool mgba_save_state_vfile(MGBAContext* ctx, struct VFile* vf, int flags) {
    if (!ctx || !ctx->core || !ctx->romLoaded || !vf) return false;
    return mCoreSaveStateNamed(ctx->core, vf, flags);
}

/**
 * @brief Load state from a VFile with flags
 * 
 * @param ctx Context with loaded ROM
 * @param vf VFile to load from
 * @param flags Savestate flags (MGBA_SAVESTATE_*)
 * @return true on success, false on failure
 */
static inline bool mgba_load_state_vfile(MGBAContext* ctx, struct VFile* vf, int flags) {
    if (!ctx || !ctx->core || !ctx->romLoaded || !vf) return false;
    return mCoreLoadStateNamed(ctx->core, vf, flags);
}

/*============================================================================
 * SAVE DATA (SRAM)
 *============================================================================*/

/**
 * @brief Load save data (SRAM) from a file
 * 
 * @param ctx Context with loaded ROM
 * @param path Path to save file
 * @param temporary If true, changes won't be written back to disk
 * @return true on success, false on failure
 */
static inline bool mgba_load_save_file(MGBAContext* ctx, const char* path, bool temporary) {
    if (!ctx || !ctx->core || !path) return false;
    
#ifdef ENABLE_VFS
    return mCoreLoadSaveFile(ctx->core, path, temporary);
#else
    (void)path;
    (void)temporary;
    return false;
#endif
}

/**
 * @brief Auto-load save data for the current ROM
 * 
 * Looks for a .sav file with the same name as the ROM.
 * 
 * @param ctx Context with loaded ROM
 * @return true on success, false on failure
 */
static inline bool mgba_autoload_save(MGBAContext* ctx) {
    if (!ctx || !ctx->core || !ctx->romLoaded) return false;
    
#if defined(ENABLE_VFS) && defined(ENABLE_DIRECTORIES)
    return mCoreAutoloadSave(ctx->core);
#else
    return false;
#endif
}

/**
 * @brief Clone current SRAM data to a buffer
 * 
 * @param ctx Context with loaded ROM
 * @param sramOut Output: pointer to allocated SRAM buffer (caller must free)
 * @return Size of SRAM data, or 0 on error
 */
static inline size_t mgba_clone_savedata(MGBAContext* ctx, void** sramOut) {
    if (!ctx || !ctx->core || !sramOut) return 0;
    return ctx->core->savedataClone(ctx->core, sramOut);
}

/**
 * @brief Restore SRAM data from a buffer
 * 
 * @param ctx Context with loaded ROM
 * @param sram SRAM data buffer
 * @param size Size of SRAM data
 * @param writeback If true, write changes to save file
 * @return true on success, false on failure
 */
static inline bool mgba_restore_savedata(MGBAContext* ctx, const void* sram, 
                                         size_t size, bool writeback) {
    if (!ctx || !ctx->core || !sram) return false;
    return ctx->core->savedataRestore(ctx->core, sram, size, writeback);
}

/*============================================================================
 * AUDIO
 *============================================================================*/

/**
 * @brief Get the audio sample rate
 * 
 * @param ctx Initialized context
 * @return Sample rate in Hz (hardcoded for mGBA 0.10 compatibility)
 */
static inline unsigned mgba_get_audio_sample_rate(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return 0;
    /* mGBA 0.10 doesn't have audioSampleRate - use standard GB/GBA sample rate */
    return 32768;  /* Standard mGBA audio sample rate */
}

/**
 * @brief Get the audio buffer
 * 
 * Note: Not available in mGBA 0.10, returns NULL
 * 
 * @param ctx Context with loaded ROM
 * @return NULL (not supported in 0.10)
 */
static inline struct blip_t* mgba_get_audio_buffer(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return NULL;
    /* Use getAudioChannel for mGBA 0.10 compatibility */
    return ctx->core->getAudioChannel ? ctx->core->getAudioChannel(ctx->core, 0) : NULL;
}

/**
 * @brief Set the audio buffer size
 * 
 * @param ctx Initialized context
 * @param samples Number of samples
 */
static inline void mgba_set_audio_buffer_size(MGBAContext* ctx, size_t samples) {
    if (!ctx || !ctx->core) return;
    ctx->core->setAudioBufferSize(ctx->core, samples);
}

/**
 * @brief Get the current audio buffer size
 * 
 * @param ctx Initialized context
 * @return Buffer size in samples
 */
static inline size_t mgba_get_audio_buffer_size(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->getAudioBufferSize(ctx->core);
}

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

/**
 * @brief Load configuration from default locations
 * 
 * @param ctx Initialized context
 */
static inline void mgba_load_config(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return;
    mCoreLoadConfig(ctx->core);
}

/**
 * @brief Set a configuration option
 * 
 * @param ctx Initialized context
 * @param key Configuration key
 * @param value Configuration value
 */
static inline void mgba_set_config_value(MGBAContext* ctx, const char* key, const char* value) {
    if (!ctx || !ctx->core || !key || !value) return;
    mCoreConfigSetValue(&ctx->core->config, key, value);
}

/**
 * @brief Get a configuration option
 * 
 * @param ctx Initialized context
 * @param key Configuration key
 * @return Configuration value, or NULL if not found
 */
static inline const char* mgba_get_config_value(MGBAContext* ctx, const char* key) {
    if (!ctx || !ctx->core || !key) return NULL;
    return mCoreConfigGetValue(&ctx->core->config, key);
}

/*============================================================================
 * GAME INFO
 *============================================================================*/

/**
 * @brief Get game title
 * 
 * @param ctx Context with loaded ROM
 * @param title Output buffer for title (must be at least 17 bytes)
 */
static inline void mgba_get_game_title(MGBAContext* ctx, char* title) {
    if (!ctx || !ctx->core || !title) return;
    if (ctx->core->getGameTitle) {
        ctx->core->getGameTitle(ctx->core, title);
    } else {
        title[0] = '\0';
    }
}

/**
 * @brief Get game code
 * 
 * @param ctx Context with loaded ROM
 * @param code Output buffer for code (must be at least 9 bytes)
 */
static inline void mgba_get_game_code(MGBAContext* ctx, char* code) {
    if (!ctx || !ctx->core || !code) return;
    if (ctx->core->getGameCode) {
        ctx->core->getGameCode(ctx->core, code);
    } else {
        code[0] = '\0';
    }
}

/**
 * @brief Get ROM size
 * 
 * @param ctx Context with loaded ROM
 * @return ROM size in bytes
 */
static inline size_t mgba_get_rom_size(MGBAContext* ctx) {
    if (!ctx || !ctx->core || !ctx->romLoaded) return 0;
    return ctx->core->romSize(ctx->core);
}

/*============================================================================
 * CALLBACKS
 *============================================================================*/

/**
 * @brief Add core callbacks
 * 
 * @param ctx Initialized context
 * @param callbacks Callback structure (must remain valid)
 */
static inline void mgba_add_callbacks(MGBAContext* ctx, struct mCoreCallbacks* callbacks) {
    if (!ctx || !ctx->core || !callbacks) return;
    ctx->core->addCoreCallbacks(ctx->core, callbacks);
}

/**
 * @brief Clear all core callbacks
 * 
 * @param ctx Initialized context
 */
static inline void mgba_clear_callbacks(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return;
    ctx->core->clearCoreCallbacks(ctx->core);
}

/**
 * @brief Set an AV stream for audio/video capture
 * 
 * @param ctx Initialized context
 * @param stream AV stream structure (or NULL to clear)
 */
static inline void mgba_set_av_stream(MGBAContext* ctx, struct mAVStream* stream) {
    if (!ctx || !ctx->core) return;
    ctx->core->setAVStream(ctx->core, stream);
}

/*============================================================================
 * UTILITY FUNCTIONS
 *============================================================================*/

/**
 * @brief Check if a file is a valid ROM
 * 
 * @param path Path to the file
 * @return Platform type if valid, mPLATFORM_NONE otherwise
 */
static inline enum mPlatform mgba_identify_rom(const char* path) {
#ifdef ENABLE_VFS
    struct VFile* vf = VFileOpen(path, O_RDONLY);
    if (!vf) return mPLATFORM_NONE;
    
    enum mPlatform platform = mCoreIsCompatible(vf);
    vf->close(vf);
    return platform;
#else
    (void)path;
    return mPLATFORM_NONE;
#endif
}

/**
 * @brief Get the platform name as a string
 * 
 * @param platform Platform type
 * @return Platform name string
 */
static inline const char* mgba_platform_name(enum mPlatform platform) {
    switch (platform) {
        case mPLATFORM_GBA: return "GBA";
        case mPLATFORM_GB:  return "GB/GBC";
        default:            return "Unknown";
    }
}

/**
 * @brief Get the emulator frequency (cycles per second)
 * 
 * @param ctx Initialized context
 * @return Frequency in Hz
 */
static inline int32_t mgba_get_frequency(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->frequency(ctx->core);
}

/**
 * @brief Get cycles per frame
 * 
 * @param ctx Initialized context
 * @return Cycles per frame
 */
static inline int32_t mgba_get_frame_cycles(MGBAContext* ctx) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->frameCycles(ctx->core);
}

/*============================================================================
 * MEMORY ACCESS (for game-specific state reading)
 *============================================================================*/

/** Color type alias for video buffer pixels */
typedef uint32_t color_t;

/**
 * @brief Read a single byte from emulator memory
 * 
 * @param ctx Context with loaded ROM
 * @param addr Memory address to read
 * @return Byte value at address, or 0 on error
 */
static inline uint8_t mgba_read_mem(MGBAContext* ctx, uint32_t addr) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->busRead8(ctx->core, addr);
}

/**
 * @brief Read a 16-bit value from emulator memory (little-endian)
 * 
 * @param ctx Context with loaded ROM
 * @param addr Memory address to read
 * @return 16-bit value at address, or 0 on error
 */
static inline uint16_t mgba_read_mem16(MGBAContext* ctx, uint32_t addr) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->busRead16(ctx->core, addr);
}

/**
 * @brief Read a BCD-encoded value (3 bytes, used for money in Pokemon)
 * 
 * @param ctx Context with loaded ROM
 * @param addr Memory address to read
 * @return Decoded decimal value
 */
static inline uint32_t mgba_read_bcd(MGBAContext* ctx, uint32_t addr) {
    if (!ctx || !ctx->core) return 0;
    uint32_t value = 0;
    for (int i = 0; i < 3; i++) {
        uint8_t byte = ctx->core->busRead8(ctx->core, addr + i);
        value = value * 100 + ((byte >> 4) * 10) + (byte & 0x0F);
    }
    return value;
}

/**
 * @brief Write a single byte to emulator memory
 * 
 * @param ctx Context with loaded ROM
 * @param addr Memory address to write
 * @param value Byte value to write
 */
static inline void mgba_write_mem(MGBAContext* ctx, uint32_t addr, uint8_t value) {
    if (!ctx || !ctx->core) return;
    ctx->core->busWrite8(ctx->core, addr, value);
}

/*============================================================================
 * INPUT CONVERSION (for RL environments)
 *============================================================================*/

/**
 * @brief Convert action index to key bitmask
 * 
 * Maps discrete action indices to button combinations for Game Boy.
 * 
 * @param action Action index (0-9 typically)
 * @return Key bitmask for mgba_set_keys()
 */
static inline uint32_t mgba_action_to_key(int action) {
    switch (action) {
        case 0: return 0;                    // No button
        case 1: return (1 << MGBA_KEY_A);    // A
        case 2: return (1 << MGBA_KEY_B);    // B
        case 3: return (1 << MGBA_KEY_START);// Start
        case 4: return (1 << MGBA_KEY_SELECT);// Select
        case 5: return (1 << MGBA_KEY_UP);   // Up
        case 6: return (1 << MGBA_KEY_DOWN); // Down
        case 7: return (1 << MGBA_KEY_LEFT); // Left
        case 8: return (1 << MGBA_KEY_RIGHT);// Right
        default: return 0;
    }
}

/*============================================================================
 * EXTENDED mGBA CONTEXT (for RL environments)
 *============================================================================*/

/**
 * @brief Extended context for RL environments
 * 
 * Wraps MGBAContext with additional fields needed for RL training:
 * - frame_skip: Number of frames to run per step
 * - state_path: Path to initial state file
 * - rom_path: Path to ROM file
 * - render_enabled: Whether rendering is enabled
 */
typedef struct mGBA {
    struct mCore* core;           /**< The mGBA core instance (from MGBAContext) */
    uint32_t* video_buffer;       /**< Video output buffer (RGBA8888) */
    size_t video_buffer_stride;   /**< Stride of the video buffer */
    unsigned width;               /**< Current video width */
    unsigned height;              /**< Current video height */
    bool initialized;             /**< Whether the context is initialized */
    bool rom_loaded;              /**< Whether a ROM is currently loaded */
    enum mPlatform platform;      /**< Current platform (GBA, GB) */
    
    /* Extended fields for RL environments */
    int frame_skip;               /**< Frames per step */
    char state_path[512];         /**< Path to initial state file */
    char rom_path[512];           /**< Path to ROM file */
    bool render_enabled;          /**< Whether rendering is enabled */
    bool uses_shared_rom;         /**< Whether using shared ROM memory */
} mGBA;

/**
 * @brief Initialize mGBA context from ROM file path (for RL environments)
 * 
 * Auto-detects the platform and initializes the emulator.
 * Note: This preserves frame_skip, state_path, render_enabled, and uses_shared_rom
 * that may have been set before calling this function.
 * 
 * @param ctx Context to initialize
 * @param rom_path Path to ROM file
 * @return true on success, false on failure
 */
static inline bool mgba_init_rl_env(mGBA* ctx, const char* rom_path) {
    if (!ctx || !rom_path) return false;
    
    /* Preserve RL-specific fields that may have been set before init */
    int saved_frame_skip = ctx->frame_skip;
    char saved_state_path[512];
    strncpy(saved_state_path, ctx->state_path, sizeof(saved_state_path) - 1);
    saved_state_path[sizeof(saved_state_path) - 1] = '\0';
    bool saved_render_enabled = ctx->render_enabled;
    bool saved_uses_shared_rom = ctx->uses_shared_rom;
    
    /* Clear core fields only */
    ctx->core = NULL;
    ctx->video_buffer = NULL;
    ctx->video_buffer_stride = 0;
    ctx->width = 0;
    ctx->height = 0;
    ctx->initialized = false;
    ctx->rom_loaded = false;
    ctx->platform = mPLATFORM_NONE;
    
    strncpy(ctx->rom_path, rom_path, sizeof(ctx->rom_path) - 1);
    ctx->rom_path[sizeof(ctx->rom_path) - 1] = '\0';
    
    /* Restore preserved fields */
    ctx->frame_skip = saved_frame_skip > 0 ? saved_frame_skip : 1;
    strncpy(ctx->state_path, saved_state_path, sizeof(ctx->state_path) - 1);
    ctx->render_enabled = saved_render_enabled;
    ctx->uses_shared_rom = saved_uses_shared_rom;
    
#ifdef ENABLE_VFS
    /* Use mCoreFind to auto-detect and create the appropriate core */
    ctx->core = mCoreFind(rom_path);
    if (!ctx->core) return false;
    
    /* Initialize the core */
    if (!ctx->core->init(ctx->core)) {
        ctx->core = NULL;
        return false;
    }
    
    /* Get platform and dimensions */
    ctx->platform = ctx->core->platform(ctx->core);
    MGBA_GET_VIDEO_DIMENSIONS(ctx->core, &ctx->width, &ctx->height);
    
    /* Initialize configuration */
    mCoreInitConfig(ctx->core, NULL);
    
    /* Allocate video buffer */
    ctx->video_buffer_stride = ctx->width;
    ctx->video_buffer = (uint32_t*)calloc(ctx->width * ctx->height, sizeof(uint32_t));
    if (!ctx->video_buffer) {
        ctx->core->deinit(ctx->core);
        ctx->core = NULL;
        return false;
    }
    
    /* Set video buffer */
    ctx->core->setVideoBuffer(ctx->core, (color_t*)ctx->video_buffer, ctx->video_buffer_stride);
    
    /* Load the ROM */
    if (!mCoreLoadFile(ctx->core, rom_path)) {
        free(ctx->video_buffer);
        ctx->video_buffer = NULL;
        ctx->core->deinit(ctx->core);
        ctx->core = NULL;
        return false;
    }
    
    ctx->rom_loaded = true;
    ctx->initialized = true;
    /* Note: frame_skip is already preserved from earlier, don't overwrite it */
    
    /* Reset to start execution */
    ctx->core->reset(ctx->core);
    
    return true;
#else
    (void)rom_path;
    return false;
#endif
}

/**
 * @brief Load a state from file (initial state for RL reset)
 * 
 * @param ctx Initialized context
 * @param path Path to state file
 * @return true on success, false on failure
 */
static inline bool initial_load_state(mGBA* ctx, const char* path) {
    if (!ctx || !ctx->core || !ctx->rom_loaded || !path || path[0] == '\0') return false;
    
#ifdef ENABLE_VFS
    struct VFile* vf = VFileOpen(path, O_RDONLY);
    if (!vf) return false;
    
    bool success = mCoreLoadStateNamed(ctx->core, vf, SAVESTATE_RTC);
    vf->close(vf);
    return success;
#else
    (void)path;
    return false;
#endif
}

/* Compatibility aliases for the mGBA struct */
static inline uint8_t read_mem(mGBA* ctx, uint32_t addr) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->busRead8(ctx->core, addr);
}

static inline uint16_t read_uint16(mGBA* ctx, uint32_t addr) {
    if (!ctx || !ctx->core) return 0;
    return ctx->core->busRead16(ctx->core, addr);
}

static inline uint32_t read_bcd(mGBA* ctx, uint32_t addr) {
    if (!ctx || !ctx->core) return 0;
    uint32_t value = 0;
    for (int i = 0; i < 3; i++) {
        uint8_t byte = ctx->core->busRead8(ctx->core, addr + i);
        value = value * 100 + ((byte >> 4) * 10) + (byte & 0x0F);
    }
    return value;
}

static inline uint32_t action_to_key(int action) {
    return mgba_action_to_key(action);
}

#ifdef __cplusplus
}
#endif

#endif /* MGBA_WRAPPER_H */
