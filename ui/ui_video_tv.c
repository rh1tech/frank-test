/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_video_tv.c — composite PAL/NTSC backend.
 *
 * The driver underneath is frank-msx's drivers/tv/, itself ported from
 * murmnes, and it is taken verbatim: a software composite encoder that
 * builds the whole line — sync, burst, luma, chroma — into a buffer and
 * clocks it out of one PIO state machine through the same GP12-19
 * resistor ladder that carries VGA. Three DMA channels and a hardware
 * alarm, no core of its own beyond the init call, and nothing in this
 * file second-guesses any of it. See drivers/tv/tv_rename.h for how its
 * public symbols are kept from colliding with ours.
 *
 *
 * WHY THE PICTURE IS SMALLER
 *
 * Composite video cannot carry 640x480. A PAL line is 52 us of active
 * picture and the colour subcarrier sits at 4.43 MHz, which puts the
 * usable horizontal resolution somewhere near 320 whatever the source
 * does — and 480 lines do not fit in 288 either. The driver's native
 * mode is 320x240, so the desktop is halved in both directions on the
 * way out.
 *
 * That is a real loss and it is not hidden: 6-pixel-wide type becomes
 * 3 pixels wide and is not readable on a TV. What composite is *for*
 * here is proving the connector, the ladder and the encoder work —
 * which the colour, the sync lock and the shape of the windows all
 * demonstrate perfectly well at half size. Anyone who needs to read the
 * results plugs in HDMI or VGA.
 *
 *
 * THE HALVING
 *
 * Cheaper than it sounds, because of how 4 bpp packs. One source byte
 * is two pixels, left in the high nibble — so taking every other pixel
 * is taking the high nibble of every byte, and taking every other line
 * is stepping the source pointer by two rows. One read and one shift
 * per output pixel, no averaging and no branches.
 *
 * Point sampling rather than averaging is deliberate: a 2x2 box filter
 * over a 16-colour indexed image has to average *indices*, which is
 * meaningless, or convert to RGB and back, which needs a nearest-colour
 * search per pixel. Dropping pixels keeps the palette exact.
 */

#include "ui_desktop.h"
#include "ui_textpage.h"
#include "ui_video.h"
#include "ui_palette.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* The vendored driver, renamed. Declared here rather than by including
 * its header, because tv_rename.h rewrites the names at compile time
 * for the driver's own translation units only. */
extern void tv_graphics_init(void);
extern void tv_graphics_set_buffer(uint8_t *buffer, uint16_t width, uint16_t height);
extern void tv_graphics_set_mode(int mode);
extern void tv_graphics_set_palette(uint8_t i, uint32_t color888);
extern void tv_graphics_set_offset(int x, int y);

/* enum graphics_mode_t from drivers/tv/graphics.h. Spelled out rather
 * than included for the same reason as the prototypes. */
#define TV_TEXTMODE_DEFAULT   0
#define TV_GRAPHICSMODE_DEFAULT 1

/* 256 wide, centred in the ~320-pixel active line by a 32-pixel offset —
 * exactly what frank-msx does, and not an arbitrary choice. The renderer
 * treats everything outside the image as border, and handing it a full
 * 320 is the one geometry that is not known to work. */
#define TV_W 256
#define TV_H 240
#define TV_X ((320 - TV_W) / 2)

/* One byte per pixel, palette index: 76,800 bytes, and there is not
 * that much left — two 4 bpp screen buffers already take 307,200 of the
 * RP2350's 520 KB and asking for another 76,800 overflows RAM by 64 KB.
 *
 * So it is borrowed. Composite scans out of the driver's own buffer and
 * never touches ours, which leaves one of the double-buffered pair doing
 * nothing at all — that is where this lives. The price is that this
 * backend must never swap them, which it has no reason to: there is no
 * vertical blank on this path to swap at.
 *
 * The driver's 4 bpp mode would have halved it and would also have been
 * our native format, but that branch is commented out in the source and
 * this is a verbatim port. */
static uint8_t *tv_frame;

volatile uint32_t ui_tv_vsync_count;

extern volatile uint32_t ui_hstx_swap_timeouts;   /* shared counter */

static uint8_t *fb_bits;

/* Halve the composed desktop into the driver's framebuffer.
 *
 * Unlike the HDMI and VGA paths this is not a scanline callback — the
 * TV driver owns a whole framebuffer and reads it on its own schedule,
 * so there is nothing to synchronise with and present() simply
 * converts. Which also means no vertical-blank swap: the driver may be
 * reading the frame as it is rewritten, and at 320x240 with no moving
 * content between frames that tears at pixel granularity and is not
 * visible. frank-msx ships the same trade. */
/* Kept as the names main() already calls. */
void ui_video_tv_set_desktop(const ui_desktop_t *d) { ui_textpage_set_desktop(d); }
void ui_video_tv_set_menubar(const ui_menubar_t *mb) { ui_textpage_set_menubar(mb); }

static void __not_in_flash_func(tv_blit)(void) {
    if (!fb_bits) return;

    uint8_t *dst = tv_frame;
    for (int y = 0; y < TV_H; y++) {
        const uint8_t *src = fb_bits + (size_t)(y * 2) * UI_STRIDE;
        /* 640 -> 256 is 2.5:1, so this cannot be the nibble trick the
         * 2:1 version used; step a fixed-point source index instead. */
        uint32_t sx = 0;                       /* 24.8 fixed point */
        for (int x = 0; x < TV_W; x++) {
            const unsigned p = sx >> 8;
            const uint8_t  b = src[p >> 1];
            *dst++ = (p & 1u) ? (b & 0x0Fu) : (b >> 4);
            sx += 640u * 256u / TV_W;
        }
    }
}

/* From RAM, for the reason in drivers/pico_vga/vga_output.c: core 0
 * saturates XIP whenever it repaints, and a scanout loop living in flash
 * stalls with it. Composite is analogue and drops sync for it, exactly
 * as VGA did. */
static void __not_in_flash_func(tv_core1_run)(void) {
    /* Claims its PIO state machine, three DMA channels and the alarm.
     * Must run on core 1: the scanline work is time-critical and core 0
     * is running the interface. */
    tv_graphics_init();

    for (unsigned i = 0; i < UI_PALETTE_LEN; i++)
        tv_graphics_set_palette((uint8_t)i, ui_palette_rgb888[i]);

    /* Slot 200 is what the renderer paints outside the image, and
     * tv_graphics_init() leaves it at its greyscale default — 0xC8C8C8.
     * frank-msx forces it black for the same reason: the margins either
     * side of a 256-wide image are 32 pixels of whatever this holds. */
    tv_graphics_set_palette(200, 0x000000);

    tv_graphics_set_buffer(tv_frame, TV_W, TV_H);
    tv_graphics_set_mode(TV_GRAPHICSMODE_DEFAULT);
    tv_graphics_set_offset(TV_X, 0);

    while (true) tight_loop_contents();
}

static bool tv_init(void) {
    extern uint8_t *ui_video_front_bits(void);
    ui_video_surface();
    fb_bits = ui_video_front_bits();

    tv_frame = ui_video_spare_bits();
    ui_textpage_target(tv_frame, TV_W, TV_H);
    memset(tv_frame, 0, TV_W * TV_H);

    /* Core 1 is reset before it is launched, not merely launched.
     *
     * ui_video_switch() changes mode by persisting the choice and
     * rebooting, and a watchdog reboot does not reset core 1: it carries
     * on running the previous scanout loop across the restart. The next
     * boot then spins in multicore_launch_core1() waiting for a handshake
     * from a core that is already busy, and hangs before any backend
     * opens - no signal on any output, which reads as dead video rather
     * than as a hung boot. A cold power-on hides it, so it only bites
     * after a mode switch. */
    multicore_reset_core1();
    multicore_launch_core1(tv_core1_run);
    return true;
}

static void tv_present(void) {
    extern uint8_t *ui_video_back_bits(void);

    /* The text page when there is one to draw, the scaled desktop only
     * as a fallback before the interface has handed its state over. */
    if (ui_textpage_ready()) {
        ui_textpage_draw();
    } else {
        fb_bits = ui_video_back_bits();
        tv_blit();
    }

    /* Deliberately no swap. tv_frame lives in the other buffer, so
     * rotating them would hand the compositor the framebuffer the TV
     * driver is reading and point this at the desktop. */
    ui_tv_vsync_count++;
}

/* Frames the encoder has clocked out, not frames this file has
 * converted. ui_tv_vsync_count only advances when the interface
 * repaints, so "Video output" sampled it across a 50 ms sleep, saw no
 * change and reported FAIL on a perfectly good picture. */
extern volatile uint32_t tv_frames_emitted;
static uint32_t tv_frames(void) { return tv_frames_emitted; }

const ui_video_backend_t ui_video_backend_tv = {
    .name     = "Composite 320x240",
    .mode     = VIDEO_COMPOSITE,
    .init     = tv_init,
    /* Same one-way door as the other two: core 1, three DMA channels, a
     * PIO state machine and an alarm handler. ui_video_switch() reboots
     * rather than unwinding them. */
    .shutdown = NULL,
    .present  = tv_present,
    .frames   = tv_frames,
};
