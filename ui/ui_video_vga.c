/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_video_vga.c — HSTX VGA backend, 4 bpp direct scanout.
 *
 * The sibling of ui_video_hstx.c, and deliberately parallel to it: same
 * framebuffer, same double buffering, same vsync swap. Only the expander
 * differs, because the wire wants something else at the far end.
 *
 *
 * THE EXPANDER
 *
 * HDMI gets two RGB565 pixels per output word. VGA gets four 8-bit
 * pixels per word — see vga_output.h — so a 16-entry table indexed by
 * colour maps a 4 bpp source nibble straight to the byte that pixel
 * needs, and two source bytes assemble into one output word.
 *
 * 320 source bytes in, 160 words out, four lookups per word, no
 * branches.
 * The table and the callback live in RAM: a scanline that has to wait on
 * XIP is a scanline that tears.
 *
 *
 * SIX BITS OF COLOUR
 *
 * The resistor ladder gives two bits per channel — 64 colours against
 * the palette's 24-bit entries — so every colour is quantised on the way
 * out. That is a real loss and it lands hardest on the greys, which is
 * most of this interface: four levels have to carry white, paper, five
 * greys and black.
 *
 * Rounded rather than truncated (+0x20 before the shift), because
 * truncation drags every grey downward and collapses the two lightest
 * onto each other — the bevel highlights disappear and the windows go
 * flat. Rounding keeps them distinct.
 */

#include "ui_video.h"
#include "ui_palette.h"

#include "pico_vga/vga_output.h"

#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <string.h>

#define OUT_W VGA_H_ACTIVE_PIXELS
#define OUT_H VGA_V_ACTIVE_LINES

/* Colour index -> the 8-bit pixel the wire wants, syncs already idle. */
static uint8_t __not_in_flash("ui_vga_lut") colour_lut[UI_PALETTE_LEN];

/* Published so the video-output test can prove frames are being
 * generated, exactly as the HDMI path does with ui_hstx_vsync_count. */
volatile uint32_t ui_vga_vsync_count;

extern volatile uint32_t ui_hstx_swap_timeouts;   /* shared counter */

static uint8_t      *fb_bits;      /* what scanout reads: the front buffer */
static uint8_t      *fb_pending;   /* set by present(), taken at vblank    */
static volatile bool swap_done;

static void build_lut(void) {
    for (unsigned i = 0; i < UI_PALETTE_LEN; i++) {
        const uint32_t rgb = ui_palette_rgb888[i];
        const unsigned r8 = (rgb >> 16) & 0xFFu;
        const unsigned g8 = (rgb >> 8)  & 0xFFu;
        const unsigned b8 =  rgb        & 0xFFu;

        /* Round to two bits rather than truncate. 0xDD (GREY_1) and 0xEE
         * (PAPER) both truncate to 3, which erases the bevel highlight;
         * rounding keeps 0xDD at 3 and lifts nothing above range because
         * the cap is applied after. */
        unsigned r = (r8 + 0x20u) >> 6; if (r > 3u) r = 3u;
        unsigned g = (g8 + 0x20u) >> 6; if (g > 3u) g = 3u;
        unsigned b = (b8 + 0x20u) >> 6; if (b > 3u) b = 3u;

        colour_lut[i] = vga_pixel_byte(r, g, b, VGA_SYNC_IDLE);
    }
}

static void __not_in_flash_func(vga_scanline)(uint32_t active_line,
                                              uint32_t *out) {
    if (!fb_bits || active_line >= (uint32_t)OUT_H) return;
    const uint8_t *src = fb_bits + (size_t)active_line * UI_STRIDE;

    /* One source nibble is one pixel is one output word. 320 bytes in,
     * 640 words out, two lookups per byte. */
    for (unsigned x = 0; x < UI_STRIDE; x++) {
        const uint8_t b = src[x];
        out[0] = colour_lut[b >> 4];
        out[1] = colour_lut[b & 0x0Fu];
        out += 2;
    }
}

static void __not_in_flash_func(vga_vsync)(void) {
    ui_vga_vsync_count++;

    /* Swap at the blank, never mid-frame. Same contract as the HDMI
     * backend, and the reason a present() is allowed to block. */
    if (fb_pending) {
        fb_bits    = fb_pending;
        fb_pending = NULL;
        swap_done  = true;
    }
}

static bool vga_init(void) {
    build_lut();

    vga_output_init();
    vga_output_set_scanline_callback(vga_scanline);
    vga_output_set_vsync_callback(vga_vsync);

    extern uint8_t *ui_video_front_bits(void);
    fb_bits = ui_video_front_bits();

    multicore_launch_core1(vga_output_core1_run);
    return true;
}

static void vga_present(void) {
    extern uint8_t *ui_video_back_bits(void);
    extern void     ui_video_swap_buffers(void);
    extern void     ui_video_sync_back(void);

    swap_done  = false;
    fb_pending = ui_video_back_bits();

    absolute_time_t deadline = make_timeout_time_ms(50);
    while (!swap_done &&
           absolute_time_diff_us(get_absolute_time(), deadline) > 0)
        tight_loop_contents();

    if (!swap_done) ui_hstx_swap_timeouts++;

    ui_video_swap_buffers();
    ui_video_sync_back();
}

static uint32_t vga_frames(void) { return ui_vga_vsync_count; }

const ui_video_backend_t ui_video_backend_hstx_vga = {
    .name     = "HSTX VGA 640x480",
    .mode     = VIDEO_VGA,
    .init     = vga_init,
    /* Same reasoning as the HDMI backend: bringing this down means
     * unwinding two DMA channels, an exclusive IRQ and core 1, which is
     * more risk than the reboot ui_video_switch() does instead. */
    .shutdown = NULL,
    .present  = vga_present,
    .frames   = vga_frames,
};
