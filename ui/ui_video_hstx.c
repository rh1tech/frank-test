/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_video_hstx.c — HSTX HDMI backend, 4 bpp direct scanout.
 *
 * Deliberately NOT an edit to drivers/HDMI_hstx.c. That file is copied
 * verbatim from frank-msx so the two projects can re-sync; adding a
 * second pixel format to it would end that. This installs its own
 * scanline callback on the same drivers/pico_hdmi core instead.
 *
 *
 * THE EXPANDER
 *
 * The scanline callback is handed a uint32_t* aimed at
 * video_output.c's `static uint16_t line_buffer[MODE_H_ACTIVE_PIXELS]`,
 * so each word carries two pixels and — this being little-endian — the
 * LOW half-word is the left one. That is read off the declaration
 * rather than assumed, but it is also the one thing here that would
 * produce a subtly mirrored image rather than an obviously broken one,
 * so it gets a named constant to flip.
 *
 * Source is 2 bpp: one byte is four pixels, leftmost in the high bits.
 * Rather than shift per pixel, a 16-entry table maps a nibble (two
 * pixels) straight to an output word. One row is then 160 bytes in and
 * 320 words out, two table lookups per byte, no branches:
 *
 *     out[0] = pair_lut[b >> 4];
 *     out[1] = pair_lut[b & 15];
 *
 * At 640x480x60 that is 320 words in about 25 us of active line time,
 * which the M33 does comfortably from SRAM. The callback and its table
 * live in RAM (__not_in_flash) because a scanline that has to wait on
 * XIP is a scanline that tears.
 */

#include "ui_video.h"
#include "ui_palette.h"
#include "ui_expand4bpp.h"

#include "pico_hdmi/video_output.h"
#include "pico_hdmi/hstx_data_island_queue.h"

#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <string.h>

#define OUT_W MODE_H_ACTIVE_PIXELS   /* 640 */
#define OUT_H MODE_V_ACTIVE_LINES    /* 480 */

/* The table and the callback live in RAM: a scanline that has to wait on
 * XIP is a scanline that tears. */
static uint32_t __not_in_flash("ui_pair_lut") pair_lut[256];
static uint8_t *fb_bits;          /* what scanout reads: the front buffer */
static uint8_t *fb_pending;       /* set by present(), taken at vblank    */
static volatile bool swap_done;
static bool     running;

static void __not_in_flash("ui_scanline") scanline_cb(
        uint32_t v_scanline, uint32_t active_line, uint32_t *dst) {
    (void)v_scanline;
    if (!fb_bits || active_line >= (uint32_t)OUT_H) return;
    ui_expand4bpp_row(dst, fb_bits + (size_t)active_line * UI_STRIDE,
                      UI_STRIDE, pair_lut);
}

/* Swap at the vertical blank, not immediately: handing the scanout a new
 * buffer part-way down a frame shows the top of one image and the bottom
 * of another. */
volatile uint32_t ui_hstx_vsync_count;
volatile uint32_t ui_hstx_swap_timeouts;

static void __not_in_flash("ui_vsync") vsync_cb(void) {
    ui_hstx_vsync_count++;
    if (fb_pending) {
        fb_bits    = fb_pending;
        fb_pending = NULL;
        swap_done  = true;
    }
}

static bool hstx_init(void) {
#if PICO_RP2040
    /* No HSTX on this part. Not a fault — the caller tries the next
     * backend, which on frank-with-a-Pico-1 is the PIO path. */
    return false;
#else
    if (running) return true;

    /* Scanout starts on the *other* buffer; the first present() swaps
     * the composed one in. */
    extern uint8_t *ui_video_front_bits(void);
    ui_video_surface();
    fb_bits = ui_video_front_bits();
    ui_expand4bpp_build(pair_lut, ui_palette_rgb888);

    video_output_set_scanline_callback(scanline_cb);
    video_output_set_vsync_callback(vsync_cb);
    video_output_init(OUT_W, OUT_H);

    /* The data-island scheduler. Even with no audio it has to be
     * initialised: the scanline dispatcher pulls from its queue every
     * vblank and an uninitialised queue is not a quiet one. */
    hstx_di_queue_init();
    pico_hdmi_set_audio_sample_rate(48000);

    /* Force clk_hstx to 126 MHz.
     *
     * TMDS is 10 bits per pixel and HSTX emits 2 bits per lane per
     * clock, so 640x480@60 needs five HSTX clocks per pixel at a
     * 25.2 MHz pixel rate. video_output_init() derives it as
     * clk_sys / MODE_HSTX_CLK_DIV, which only lands on 126 MHz for
     * particular pairings — 126/1 or 252/2. Setting it explicitly here
     * makes the video path independent of whatever clock the rest of
     * the firmware chose, which is one fewer way to get a confidently
     * reported HDMI output that no sink can lock to. */
    clock_configure(clk_hstx, 0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    clock_get_hz(clk_sys), 126000000);

    /* And finally start it. video_output_init() only *configures* the
     * HSTX and DMA; nothing comes out of the connector until this runs,
     * which is exactly the failure this cost an evening to find: the
     * backend reported success, the log said "HSTX HDMI 640x480", and
     * the capture card showed its no-signal pattern. */
    multicore_launch_core1(video_output_core1_run);

    running = true;
    return true;
#endif
}

static void hstx_present(void) {
    extern uint8_t *ui_video_back_bits(void);
    extern void     ui_video_swap_buffers(void);
    extern void     ui_video_sync_back(void);

    swap_done  = false;
    fb_pending = ui_video_back_bits();

    /* Wait for the vblank to take it. Bounded: if the scanout is not
     * running — no backend, or a build without video — this must not
     * become an infinite loop in the middle of a diagnostic. */
    absolute_time_t deadline = make_timeout_time_ms(50);
    while (!swap_done &&
           absolute_time_diff_us(get_absolute_time(), deadline) > 0)
        tight_loop_contents();

    if (!swap_done) ui_hstx_swap_timeouts++;

    ui_video_swap_buffers();
    ui_video_sync_back();
}

static uint32_t hstx_frames(void) { return ui_hstx_vsync_count; }

const ui_video_backend_t ui_video_backend_hstx_hdmi = {
    .name     = "HSTX HDMI 640x480",
    .mode     = VIDEO_HDMI,
    .init     = hstx_init,
    /* No shutdown. video_output_init() claims DMA 0/1, an exclusive
     * DMA_IRQ_0 handler and core 1; unwinding all three correctly is
     * more risk than the reboot it would save. ui_video_switch() knows
     * to persist and restart instead. */
    .shutdown = NULL,
    .present  = hstx_present,
    .frames   = hstx_frames,
};
