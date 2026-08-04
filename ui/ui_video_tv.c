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

/* ------------------------------------------------------------------ */
/* The text page                                                       */
/* ------------------------------------------------------------------ */

/*
 * Composite does not get the desktop. It gets its own screen.
 *
 * Scaling 640x480 down to fit was the obvious thing and it is useless:
 * 6-pixel-wide type resampled by two and a half is a grey smear, and a
 * test rig whose results cannot be read is not reporting anything. The
 * pixels are not there to be had either — a PAL line carries something
 * near 320 usable samples however the source is arranged.
 *
 * So this draws a text page at native resolution instead. 256 pixels at
 * six per glyph is 42 columns, 240 at eight per row is 30 lines, and
 * every glyph lands on exactly the pixels the font intended. That is a
 * classic television text screen, and it is legible.
 */
#define TXT_COLS (TV_W / UI_CHAR_W)     /* 42 */
#define TXT_ROWS (TV_H / UI_CHAR_H)     /* 30 */

/* Safe area. A television overscans, and the first capture put the
 * header off the top edge and the totals off the bottom — both rows
 * were being drawn correctly and neither was on screen. One row and one
 * column of margin costs almost nothing and is the difference between a
 * report and a report you can read the ends of. */
#define TXT_M_X 1
#define TXT_M_Y 1

extern const uint8_t ui_font_6x8[95][8];

static const ui_desktop_t *s_desk;
static const ui_menubar_t *s_menu;

void ui_video_tv_set_desktop(const ui_desktop_t *d) { s_desk = d; }
void ui_video_tv_set_menubar(const ui_menubar_t *mb) { s_menu = mb; }

/* One glyph, straight into the 8 bpp frame. The font stores each row
 * left-justified in the top 6 bits. */
static void __not_in_flash_func(txt_glyph)(int col, int row, char ch,
                                           uint8_t fg, uint8_t bg) {
    if (col < 0 || col >= TXT_COLS || row < 0 || row >= TXT_ROWS) return;

    const unsigned u = (unsigned char)ch;
    const uint8_t *g = ui_font_6x8[(u < 32 || u > 126) ? 0 : u - 32];
    uint8_t *dst = tv_frame + (size_t)row * UI_CHAR_H * TV_W + col * UI_CHAR_W;

    for (int y = 0; y < UI_CHAR_H; y++, dst += TV_W) {
        const uint8_t bits = g[y];
        for (int x = 0; x < UI_CHAR_W; x++)
            dst[x] = (bits & (0x80u >> x)) ? fg : bg;
    }
}

static void txt_puts(int col, int row, const char *str, uint8_t fg, uint8_t bg) {
    for (int i = 0; str[i] && col + i < TXT_COLS; i++)
        txt_glyph(col + i, row, str[i], fg, bg);
}

/* A centred box of lines — used for the menu, dialogs and the board
 * picker, all of which are modal and none of which the report needs to
 * be visible behind. */
static void __not_in_flash_func(txt_box)(const char *title,
                                         const char *const *items, int count,
                                         int selected) {
    const int rows = count + 2;
    int width = (int)strlen(title) + 2;
    for (int i = 0; i < count; i++) {
        const int w = (int)strlen(items[i]) + 4;
        if (w > width) width = w;
    }
    if (width > TXT_COLS - 2 * TXT_M_X) width = TXT_COLS - 2 * TXT_M_X;

    const int x0 = (TXT_COLS - width) / 2;
    const int y0 = (TXT_ROWS - rows) / 2;

    for (int c = 0; c < width; c++) txt_glyph(x0 + c, y0, ' ', UI_WHITE, UI_ACCENT);
    txt_puts(x0 + 1, y0, title, UI_WHITE, UI_ACCENT);

    for (int i = 0; i < count; i++) {
        const int y = y0 + 1 + i;
        const bool sel = (i == selected);
        const uint8_t bg = sel ? UI_ACCENT : UI_GREY_5;
        for (int c = 0; c < width; c++) txt_glyph(x0 + c, y, ' ', UI_WHITE, bg);
        txt_puts(x0 + 2, y, items[i], UI_WHITE, bg);
        if (sel) txt_glyph(x0 + 1, y, '>', UI_WHITE, bg);
    }
    for (int c = 0; c < width; c++)
        txt_glyph(x0 + c, y0 + rows - 1, ' ', UI_WHITE, UI_GREY_5);
}

/* The whole page, rebuilt each present. Cheap: 42x30 glyphs is 60,480
 * byte writes, well under a frame.
 *
 * Menus, dialogs and the board picker are drawn here too. They have to
 * be: composite renders its own screen rather than a copy of the
 * desktop, so without this the interface is invisible on it — and on an
 * unidentified board that means no way to reach Set Board and no way to
 * run anything at all. */
static void __not_in_flash_func(txt_page)(void) {
    memset(tv_frame, UI_BLACK, (size_t)TV_W * TV_H);
    if (!s_desk) return;

    char line[TXT_COLS + 1];

    /* Modal things first, and nothing behind them. */
    if (s_desk->picker_title) {
        txt_box(s_desk->picker_title, s_desk->picker_items,
                s_desk->picker_count > 16 ? 16 : s_desk->picker_count,
                s_desk->picker_sel);
        txt_puts(TXT_M_X, TXT_ROWS - TXT_M_Y - 1,
                 "arrows choose   Enter accept   Esc cancel",
                 UI_GREY_2, UI_BLACK);
        return;
    }
    if (s_desk->dialog_title) {
        const char *body[4]; int n = 0;
        for (int i = 0; i < 4; i++)
            if (s_desk->dialog_body[i]) body[n++] = s_desk->dialog_body[i];
        txt_box(s_desk->dialog_title, body, n, -1);
        txt_puts(TXT_M_X, TXT_ROWS - TXT_M_Y - 1,
                 "any key to continue", UI_GREY_2, UI_BLACK);
        return;
    }
    if (s_menu && s_menu->open >= 0 && s_menu->open < s_menu->count) {
        const ui_menu_t *m = &s_menu->menus[s_menu->open];
        const char *items[12]; int n = 0, sel = -1;
        for (int i = 0; i < m->count && n < 12; i++) {
            if (!m->items[i].label) continue;
            if (i == s_menu->highlight) sel = n;
            items[n++] = m->items[i].label;
        }
        txt_box(m->is_mark ? "Menu" : m->title, items, n, sel);
        txt_puts(TXT_M_X, TXT_ROWS - TXT_M_Y - 1,
                 "arrows choose   Enter run   Esc close", UI_GREY_2, UI_BLACK);
        return;
    }

    /* Title bar: fill the row first, then the text over it. */
    for (int c = TXT_M_X; c < TXT_COLS - TXT_M_X; c++)
        txt_glyph(c, TXT_M_Y, ' ', UI_WHITE, UI_ACCENT);
    snprintf(line, sizeof(line), " FRANK TEST  %s", s_desk->board_name);
    txt_puts(TXT_M_X, TXT_M_Y, line, UI_WHITE, UI_ACCENT);

    snprintf(line, sizeof(line), "%s  unit %s", s_desk->mcu_name,
             s_desk->unit_serial ? s_desk->unit_serial : "-");
    txt_puts(TXT_M_X, TXT_M_Y + 1, line, UI_GREY_2, UI_BLACK);

    /* One line per test: name, verdict, and as much of the measurement
     * as fits. The verdict is coloured because on a television that is
     * the only thing readable from across the room. */
    int row = TXT_M_Y + 3;
    for (int i = 0; i < s_desk->row_count && row < TXT_ROWS - TXT_M_Y - 2; i++, row++) {
        const ui_test_row_t *r = &s_desk->rows[i];

        const char *tag; uint8_t col;
        switch (r->state) {
            case TEST_PASS:  tag = "ok  "; col = UI_OK;     break;
            case TEST_FAIL:  tag = "FAIL"; col = UI_FAIL;   break;
            case TEST_NA:    tag = "n/a "; col = UI_GREY_4; break;
            case TEST_NORUN: tag = "?   "; col = UI_WARN;   break;
            default:         tag = "....";  col = UI_GREY_4; break;
        }

        snprintf(line, sizeof(line), "%-15.15s", r->name);
        txt_puts(TXT_M_X, row, line, UI_WHITE, UI_BLACK);
        txt_puts(TXT_M_X + 16, row, tag, col, UI_BLACK);
        if (r->detail) {
            snprintf(line, sizeof(line), "%.19s", r->detail);
            txt_puts(TXT_M_X + 21, row, line, UI_GREY_2, UI_BLACK);
        }
    }

    snprintf(line, sizeof(line), "%d passed  %d failed  %d n/a",
             s_desk->passed, s_desk->failed, s_desk->na);
    txt_puts(TXT_M_X, TXT_ROWS - TXT_M_Y - 1, line, UI_WHITE, UI_BLACK);
}

/* Halve the composed desktop into the driver's framebuffer.
 *
 * Unlike the HDMI and VGA paths this is not a scanline callback — the
 * TV driver owns a whole framebuffer and reads it on its own schedule,
 * so there is nothing to synchronise with and present() simply
 * converts. Which also means no vertical-blank swap: the driver may be
 * reading the frame as it is rewritten, and at 320x240 with no moving
 * content between frames that tears at pixel granularity and is not
 * visible. frank-msx ships the same trade. */
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

static void tv_core1_run(void) {
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
    if (s_desk) {
        txt_page();
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
