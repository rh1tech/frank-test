/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_nespad.c — both controller ports, drawn and lit.
 *
 * A gamepad has eight buttons and no way to report which one is broken
 * except by someone pressing all eight. A row saying "gamepad: PASS"
 * cannot mean anything, and a row saying "gamepad: 0x21" means something
 * to nobody. So this draws the controller, lights the button under the
 * operator's thumb, and names it — which turns "press every button and
 * watch" into a job that takes ten seconds and needs no interpretation.
 *
 * Two pads, always drawn. A board with only one port greys the second
 * out rather than hiding it: an absent port and a dead one look
 * identical from the firmware side, and the difference is worth stating.
 *
 * The reader is murmnes' PIO driver, unchanged except for moving it to
 * pio2 — the one PIO the link and the I2S audio have not claimed.
 */

#include "dlgs.h"
#include "nespad.h"

#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_input.h"
#include "ui_video.h"
#include "ui_textpage.h"
#include "ui_window.h"

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

#define PAD_W   250
#define PAD_H   112
#define PAD_GAP 16

#define DLG_W   (PAD_W * 2 + PAD_GAP + 2 * (1 + UI_WIN_PAD + DLG_INSET) + 8)

/* The D-pad hub, relative to the pad body. */
#define DP_CX   54
#define DP_CY   62
#define DP_ARM  18

static const struct { uint32_t mask; const char *name; } buttons[] = {
    { DPAD_UP,     "UP"     },
    { DPAD_DOWN,   "DOWN"   },
    { DPAD_LEFT,   "LEFT"   },
    { DPAD_RIGHT,  "RIGHT"  },
    { DPAD_SELECT, "SELECT" },
    { DPAD_START,  "START"  },
    { DPAD_B,      "B"      },
    { DPAD_A,      "A"      },
};
#define BUTTON_COUNT (sizeof(buttons) / sizeof(buttons[0]))

static int  s_stop_x, s_stop_y, s_stop_w, s_stop_h;

/* One schematic controller. `live` is false for a port this board does
 * not have, which greys everything and stops the buttons lighting. */
static void draw_pad(ui_surface_t *s, int x, int y, const char *label,
                     uint32_t state, bool live) {
    const uint8_t body = live ? UI_GREY_1 : UI_GREY_2;
    const uint8_t ink  = live ? UI_BLACK  : UI_GREY_4;
    const uint8_t idle = live ? UI_GREY_2 : UI_GREY_3;
    const uint8_t hot  = UI_ACCENT;

    ui_plate(s, x, y, PAD_W, PAD_H, body);
    ui_text_bold(s, x + 10, y + 8, label, ink);
    if (!live) ui_text(s, x + 10 + ui_text_width(label) + 12, y + 8,
                       "not on this board", UI_GREY_4);

    /* The cross. Drawn as four arms around an empty hub, which is what
     * makes a diagonal legible: two lit arms and a dark centre reads as
     * up-and-left in a way a lit plus sign never would. */
    const struct { int dx, dy; uint32_t mask; } arms[4] = {
        {  0, -DP_ARM, DPAD_UP    },
        {  0,  DP_ARM, DPAD_DOWN  },
        { -DP_ARM,  0, DPAD_LEFT  },
        {  DP_ARM,  0, DPAD_RIGHT },
    };
    for (int i = 0; i < 4; i++) {
        const int ax = x + DP_CX + arms[i].dx - DP_ARM / 2;
        const int ay = y + DP_CY + arms[i].dy - DP_ARM / 2;
        const bool on = live && (state & arms[i].mask);
        ui_plate(s, ax, ay, DP_ARM, DP_ARM, on ? hot : idle);
    }
    ui_fill(s, x + DP_CX - DP_ARM / 2, y + DP_CY - DP_ARM / 2,
            DP_ARM, DP_ARM, idle);
    ui_frame(s, x + DP_CX - DP_ARM / 2, y + DP_CY - DP_ARM / 2,
             DP_ARM, DP_ARM, ink);

    /* Select and Start: two flat pills, angled on a real controller and
     * square here, because a 6-pixel font in a rotated pill is not
     * legible and the position already says which is which. */
    const struct { int px; uint32_t mask; const char *name; } pills[2] = {
        { 100, DPAD_SELECT, "SEL"   },
        { 142, DPAD_START,  "START" },
    };
    for (int i = 0; i < 2; i++) {
        const bool on = live && (state & pills[i].mask);
        const int px = x + pills[i].px, py = y + DP_CY - 7;
        ui_round_fill(s, px, py, 38, 14, on ? hot : idle);
        ui_round_frame(s, px, py, 38, 14, ink);
        ui_text_centred(s, px, py + 3, 38, pills[i].name,
                        on ? UI_WHITE : ink);
    }

    /* B then A, left to right, which is the order they sit on the
     * hardware and the opposite of alphabetical. Getting this backwards
     * would make every report of a dead button wrong. */
    const struct { int px; uint32_t mask; const char *name; } round[2] = {
        { 186, DPAD_B, "B" },
        { 216, DPAD_A, "A" },
    };
    for (int i = 0; i < 2; i++) {
        const bool on = live && (state & round[i].mask);
        const int px = x + round[i].px, py = y + DP_CY - 13;
        ui_round_fill(s, px, py, 26, 26, on ? hot : UI_FAIL_L);
        ui_round_frame(s, px, py, 26, 26, ink);
        ui_text_centred(s, px, py + 9, 26, round[i].name,
                        on ? UI_WHITE : ink);
    }
}

/* "A, START" — what is held right now, in the order the buttons are
 * listed rather than the order they were pressed, so a chord reads the
 * same every time it is made. */
static void describe(char *out, unsigned len, uint32_t state) {
    unsigned off = 0;
    out[0] = 0;
    for (unsigned i = 0; i < BUTTON_COUNT && off + 8 < len; i++) {
        if (!(state & buttons[i].mask)) continue;
        off += (unsigned)snprintf(out + off, len - off, "%s%s",
                                  off ? ", " : "", buttons[i].name);
    }
}

/* The text-page version - see ui_textpage_modal(). The schematic pads
 * cannot be drawn in a 6x8 grid, so this names the buttons that are down
 * instead, which is the fact the operator is checking for. */
static char s_tp[2][40];
static const char *s_tp_lines[2];

static void names_for(uint32_t state, char *out, unsigned len) {
    unsigned at = 0;
    out[0] = '\0';
    for (unsigned i = 0; i < BUTTON_COUNT; i++) {
        if (!(state & buttons[i].mask)) continue;
        const int n = snprintf(out + at, len - at, "%s%s",
                               at ? " " : "", buttons[i].name);
        if (n < 0 || (unsigned)n >= len - at) break;
        at += (unsigned)n;
    }
    if (!at) snprintf(out, len, "-");
}

static void publish_textpage(uint32_t s1, uint32_t s2, bool have2) {
    char b[32];

    names_for(s1, b, sizeof(b));
    snprintf(s_tp[0], sizeof(s_tp[0]), "Port 1: %s", b);

    if (have2) {
        names_for(s2, b, sizeof(b));
        snprintf(s_tp[1], sizeof(s_tp[1]), "Port 2: %s", b);
    } else {
        snprintf(s_tp[1], sizeof(s_tp[1]), "Port 2: not fitted");
    }

    for (int i = 0; i < 2; i++) s_tp_lines[i] = s_tp[i];
    ui_textpage_modal("NES Gamepads", s_tp_lines, 2, -1,
                      "press buttons   Esc closes");
}

static void draw(const dlg_ctx_t *c, uint32_t s1, uint32_t s2, bool have2) {
    ui_surface_t *s = ui_video_surface();
    publish_textpage(s1, s2, have2);
    c->paint_background();

    const int content_h = PAD_H + 10 + 12;
    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + 14 + content_h
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 20;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "NES Gamepads", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    ui_text(s, cx, cy, "Press every button. It lights up and is named below.",
            UI_GREY_5);

    draw_pad(s, cx,                     cy + 14, "Port 1", s1, true);
    draw_pad(s, cx + PAD_W + PAD_GAP,   cy + 14, "Port 2", s2, have2);

    char line[64];
    const int ty = cy + 14 + PAD_H + 8;

    describe(line, sizeof(line), s1);
    ui_text(s, cx, ty, "Port 1:", UI_GREY_5);
    ui_text(s, cx + 50, ty, line[0] ? line : "-", line[0] ? UI_ACCENT : UI_GREY_4);

    describe(line, sizeof(line), s2);
    ui_text(s, cx + PAD_W + PAD_GAP, ty, "Port 2:", UI_GREY_5);
    ui_text(s, cx + PAD_W + PAD_GAP + 50, ty,
            have2 ? (line[0] ? line : "-") : "n/a",
            (have2 && line[0]) ? UI_ACCENT : UI_GREY_4);

    ui_clip_reset(s);

    s_stop_w = ui_button_width("Stop");
    s_stop_h = 18;
    s_stop_x = x + DLG_W - 16 - s_stop_w;
    s_stop_y = y + h - UI_WIN_PAD - DLG_BOT - 18;
    ui_button(s, s_stop_x, s_stop_y, s_stop_w, s_stop_h, "Stop", true, false, true);

    ui_text(s, cx, s_stop_y + 5, "Esc or Stop to finish", UI_GREY_5);

    ui_video_present();

    /* The swap invalidates whatever the overlay had saved, so it is
     * re-established here rather than composed in — compositing it would
     * leave a second, stale cursor behind the moment the overlay moved. */
    ui_cursor_overlay_reset();
    {
        const ui_pointer_t *pt = ui_input_pointer_last();
        if (pt->present) ui_cursor_overlay_move(pt->x, pt->y);
    }
}

void dlg_nespad(const dlg_ctx_t *c) {
    const detect_result_t *d = c->detect;
    if (!d || !d->board) return;

    const frank_pins_t *p = &d->board->pins;
    if (p->pad_clk == PIN_NC || p->pad_latch == PIN_NC || p->pad_d1 == PIN_NC)
        return;

    const bool have2 = (p->pad_d2 != PIN_NC);

    /* Brought up here rather than at boot: it claims two state machines
     * and a program on pio2, and a board whose gamepad ports are never
     * looked at should not be paying for that. nespad_begin() is
     * idempotent: our copy returns early once pad_initialized is set, so
     * a second visit reuses the state machines rather than claiming two
     * more and eventually running out. */
    if (!nespad_begin(clock_get_hz(clk_sys) / 1000,
                      (uint8_t)p->pad_clk, (uint8_t)p->pad_d1,
                      have2 ? (uint8_t)p->pad_d2 : NESPAD_DATA_PIN_NONE,
                      (uint8_t)p->pad_latch)) {
        printf("[nespad] could not claim pio2 state machines\n");
        return;
    }

    printf("[nespad] CLK=GP%d LAT=GP%d D1=GP%d D2=%s\n",
           p->pad_clk, p->pad_latch, p->pad_d1,
           have2 ? "present" : "none");

    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }

    uint32_t last1 = 0xFFFFFFFFu, last2 = 0xFFFFFFFFu;
    bool     stop  = false;

    while (!stop) {
        /* Input first. ui_input_pointer() consumes the edge flags, so
         * `moved` has to be read here and carried into the redraw
         * decision below — reading it after the draw would always find
         * it already cleared, and the cursor would never repaint. */
        ui_input_task();

        int k = ui_input_getkey();
        while (k != UI_KEY_NONE) {
            if (k == UI_KEY_ESC || k == UI_KEY_ENTER) stop = true;
            k = ui_input_getkey();
        }

        const ui_pointer_t *pt = ui_input_pointer();
        const bool moved = pt->moved;
        if (pt->pressed &&
            pt->x >= s_stop_x && pt->x < s_stop_x + s_stop_w &&
            pt->y >= s_stop_y && pt->y < s_stop_y + s_stop_h)
            stop = true;

        nespad_read();
        const uint32_t s1 = nespad_state;
        const uint32_t s2 = have2 ? nespad_state2 : 0;

        /* Redraw only when something changed. At 4 bpp a full desktop
         * composition costs several frames, so repainting an unchanged
         * screen sixty times a second would make the dialog feel slower
         * than the controller it is reporting. Pointer movement is not
         * a change here — the overlay handles it without a frame. */
        if (moved) ui_cursor_overlay_move(pt->x, pt->y);

        if (s1 != last1 || s2 != last2) {
            draw(c, s1, s2, have2);
            if (s1 && s1 != last1) {
                char line[64];
                describe(line, sizeof(line), s1);
                printf("[nespad] port 1: %s\n", line);
            }
            if (s2 && s2 != last2) {
                char line[64];
                describe(line, sizeof(line), s2);
                printf("[nespad] port 2: %s\n", line);
            }
            last1 = s1;
            last2 = s2;
        }

        sleep_ms(8);
    }

    ui_textpage_modal_clear();
}
