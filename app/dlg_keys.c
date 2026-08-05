/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_keys.c — a keyboard on screen, lighting up under the operator's
 * fingers.
 *
 *
 * WHAT IT PROVES THAT A BYTE COUNT DOES NOT
 *
 * The ports dialog answers "is this port carrying anything", which is
 * the right question for a cold-solder joint and the wrong one for a
 * keyboard. A membrane with one dead row, a stuck key holding a line
 * down, two keys wired to the same code — all of those produce a
 * perfectly healthy byte count. The only way to find them is to press
 * every key and see whether the right one answers, which is a thing a
 * person does and a picture reports.
 *
 * So this is the gamepad dialog's argument applied to a keyboard: draw
 * the hardware, light what is pressed, and let the operator sweep the
 * whole thing in a few seconds. Nothing here reaches a verdict — a key
 * nobody pressed and a key that does not work look identical, and always
 * will.
 *
 *
 * BOTH KEYBOARDS, IN THEIR OWN CODES
 *
 * Every key below carries a HID usage and a PS/2 set 2 scancode, and
 * ui_key_held() asks each source in the codes it already speaks. The
 * alternative was translating PS/2 into HID usages, which is a third
 * table to keep correct for no gain: this way a wrong number shows up as
 * one key that never lights, on one kind of keyboard, rather than as a
 * silent shift in a mapping layer.
 *
 * A board can have both attached at once, and that is deliberately not
 * distinguished here. The question is whether the key works.
 */

#include "dlgs.h"

#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_input.h"
#include "ui_textpage.h"
#include "ui_video.h"
#include "ui_window.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* Key geometry. Small, because the whole board has to fit beside its own
 * frame in 640x480 and still leave room for the message. */
#define KEY_W    26
#define KEY_H    18
#define KEY_GAP   2

typedef struct {
    const char *label;
    uint8_t     usage;      /* HID usage ID                */
    uint8_t     ps2;        /* set 2 scancode              */
    bool        ext;        /* ...prefixed with 0xE0       */
    uint8_t     width;      /* in quarter-keys; 4 = normal */
} key_t;

/* A 60% layout: the keys every keyboard has, in the places they are on
 * every keyboard. The numeric pad and the F-row above F1..F10 are left
 * out rather than drawn wrong - plenty of keyboards in this fleet's
 * drawers do not have them, and an empty key that cannot be pressed
 * reads as a fault. */
static const key_t row0[] = {
    { "Esc", 0x29, 0x76, false, 4 },
    { "1",   0x1E, 0x16, false, 4 }, { "2", 0x1F, 0x1E, false, 4 },
    { "3",   0x20, 0x26, false, 4 }, { "4", 0x21, 0x25, false, 4 },
    { "5",   0x22, 0x2E, false, 4 }, { "6", 0x23, 0x36, false, 4 },
    { "7",   0x24, 0x3D, false, 4 }, { "8", 0x25, 0x3E, false, 4 },
    { "9",   0x26, 0x46, false, 4 }, { "0", 0x27, 0x45, false, 4 },
    { "-",   0x2D, 0x4E, false, 4 }, { "=", 0x2E, 0x55, false, 4 },
    { "Bksp", 0x2A, 0x66, false, 8 },
};

static const key_t row1[] = {
    { "Tab", 0x2B, 0x0D, false, 6 },
    { "Q", 0x14, 0x15, false, 4 }, { "W", 0x1A, 0x1D, false, 4 },
    { "E", 0x08, 0x24, false, 4 }, { "R", 0x15, 0x2D, false, 4 },
    { "T", 0x17, 0x2C, false, 4 }, { "Y", 0x1C, 0x35, false, 4 },
    { "U", 0x18, 0x3C, false, 4 }, { "I", 0x0C, 0x43, false, 4 },
    { "O", 0x12, 0x44, false, 4 }, { "P", 0x13, 0x4D, false, 4 },
    { "[", 0x2F, 0x54, false, 4 }, { "]", 0x30, 0x5B, false, 4 },
    { "\\", 0x31, 0x5D, false, 6 },
};

static const key_t row2[] = {
    { "Caps", 0x39, 0x58, false, 7 },
    { "A", 0x04, 0x1C, false, 4 }, { "S", 0x16, 0x1B, false, 4 },
    { "D", 0x07, 0x23, false, 4 }, { "F", 0x09, 0x2B, false, 4 },
    { "G", 0x0A, 0x34, false, 4 }, { "H", 0x0B, 0x33, false, 4 },
    { "J", 0x0D, 0x3B, false, 4 }, { "K", 0x0E, 0x42, false, 4 },
    { "L", 0x0F, 0x4B, false, 4 }, { ";", 0x33, 0x4C, false, 4 },
    { "'", 0x34, 0x52, false, 4 },
    { "Enter", 0x28, 0x5A, false, 9 },
};

static const key_t row3[] = {
    { "Shift", 0xE1, 0x12, false, 9 },
    { "Z", 0x1D, 0x1A, false, 4 }, { "X", 0x1B, 0x22, false, 4 },
    { "C", 0x06, 0x21, false, 4 }, { "V", 0x19, 0x2A, false, 4 },
    { "B", 0x05, 0x32, false, 4 }, { "N", 0x11, 0x31, false, 4 },
    { "M", 0x10, 0x3A, false, 4 }, { ",", 0x36, 0x41, false, 4 },
    { ".", 0x37, 0x49, false, 4 }, { "/", 0x38, 0x4A, false, 4 },
    { "Shift", 0xE5, 0x59, false, 11 },
};

static const key_t row4[] = {
    { "Ctrl", 0xE0, 0x14, false, 6 },
    { "Alt",  0xE2, 0x11, false, 6 },
    { "Space", 0x2C, 0x29, false, 28 },
    { "Alt",  0xE6, 0x11, true,  6 },
    { "Ctrl", 0xE4, 0x14, true,  6 },
};

/* The cursor block, drawn to the right of the main board. Extended
 * scancodes, which is exactly the distinction s_down_ext exists for. */
static const key_t arrows_up[]  = { { "Up",   0x52, 0x75, true, 4 } };
static const key_t arrows_low[] = {
    { "Lt", 0x50, 0x6B, true, 4 },
    { "Dn", 0x51, 0x72, true, 4 },
    { "Rt", 0x4F, 0x74, true, 4 },
};

static const struct { const key_t *keys; int n; } rows[] = {
    { row0, (int)(sizeof(row0) / sizeof(row0[0])) },
    { row1, (int)(sizeof(row1) / sizeof(row1[0])) },
    { row2, (int)(sizeof(row2) / sizeof(row2[0])) },
    { row3, (int)(sizeof(row3) / sizeof(row3[0])) },
    { row4, (int)(sizeof(row4) / sizeof(row4[0])) },
};
#define ROW_COUNT ((int)(sizeof(rows) / sizeof(rows[0])))

static int s_stop_x, s_stop_y, s_stop_w, s_stop_h;

/* One key. Green while held, which is the whole point of the dialog —
 * and green rather than the accent colour because this is the one place
 * in the interface where the answer is "that works", not "that is
 * selected". */
static void draw_key(ui_surface_t *s, int x, int y, const key_t *k) {
    const int w = (KEY_W * k->width) / 4;
    const bool on = ui_key_held(k->usage, k->ps2, k->ext);

    ui_plate(s, x, y, w, KEY_H, on ? UI_OK : UI_GREY_1);
    ui_frame(s, x, y, w, KEY_H, UI_GREY_4);

    /* Centred, and clipped by the plate rather than by guessing which
     * labels fit: "Enter" in a 58-pixel key is fine, "Shift" in the
     * narrow one is not, and the frame is what says where it ends. */
    const int tw = ui_text_width(k->label);
    ui_clip_set(s, x + 1, y + 1, w - 2, KEY_H - 2);
    ui_text(s, x + (w - tw) / 2, y + 5, k->label, on ? UI_WHITE : UI_BLACK);
    ui_clip_reset(s);
}

static int row_width(const key_t *keys, int n) {
    int w = 0;
    for (int i = 0; i < n; i++) w += (KEY_W * keys[i].width) / 4 + KEY_GAP;
    return w - KEY_GAP;
}

/* The text-page version. A 6x8 grid cannot hold a keyboard, so it names
 * what is down instead - the same trade the gamepad dialog makes, and
 * the same reason: the fact being checked is which key answered. */
static char s_tp[3][40];
static const char *s_tp_lines[3];

static void publish_textpage(void) {
    char held[64];
    unsigned at = 0;
    held[0] = '\0';

    for (int r = 0; r < ROW_COUNT && at < sizeof(held) - 8; r++) {
        for (int i = 0; i < rows[r].n; i++) {
            const key_t *k = &rows[r].keys[i];
            if (!ui_key_held(k->usage, k->ps2, k->ext)) continue;
            const int n = snprintf(held + at, sizeof(held) - at, "%s%s",
                                   at ? " " : "", k->label);
            if (n < 0 || (unsigned)n >= sizeof(held) - at) break;
            at += (unsigned)n;
        }
    }
    if (!at) snprintf(held, sizeof(held), "-");

    snprintf(s_tp[0], sizeof(s_tp[0]), "Held: %.32s", held);
    snprintf(s_tp[1], sizeof(s_tp[1]), "PS/2: %lu bytes, last 0x%02X",
             (unsigned long)ui_ps2_kbd_bytes(), ui_ps2_kbd_last_byte());
    snprintf(s_tp[2], sizeof(s_tp[2]), "USB:  %lu keys, last 0x%02X",
             (unsigned long)ui_usb_kbd_events(), ui_usb_kbd_last_usage());

    for (int i = 0; i < 3; i++) s_tp_lines[i] = s_tp[i];
    ui_textpage_modal("Keyboard", s_tp_lines, 3, -1,
                      "press every key   Esc closes");
}

static void draw(const dlg_ctx_t *c) {
    ui_surface_t *s = ui_video_surface();
    publish_textpage();
    c->paint_background();

    /* Wide enough for the longest row plus the cursor block. */
    const int board_w = row_width(row0, rows[0].n);
    const int arrows_w = 3 * (KEY_W + KEY_GAP);
    const int inner_w = board_w + 12 + arrows_w;

    const int w = inner_w + 2 * DLG_INSET + 2 * UI_WIN_PAD;
    const int content_h = 14 + ROW_COUNT * (KEY_H + KEY_GAP) + 10 + 12;
    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + content_h
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;

    const int x = (s->w - w) / 2;
    const int y = (s->h - h) / 2 - 10;

    ui_window_t win = {
        .x = x, .y = y, .w = w, .h = h,
        .title = "Keyboard", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    ui_text(s, cx, cy, "Press every key. Each one lights while held.",
            UI_GREY_5);

    int ry = cy + 14;
    for (int r = 0; r < ROW_COUNT; r++) {
        int kx = cx;
        for (int i = 0; i < rows[r].n; i++) {
            draw_key(s, kx, ry, &rows[r].keys[i]);
            kx += (KEY_W * rows[r].keys[i].width) / 4 + KEY_GAP;
        }
        ry += KEY_H + KEY_GAP;
    }

    /* The cursor keys, in their inverted-T, beside the bottom rows. */
    {
        const int ax = cx + board_w + 12;
        const int ay = cy + 14 + 3 * (KEY_H + KEY_GAP);
        draw_key(s, ax + KEY_W + KEY_GAP, ay, &arrows_up[0]);
        for (int i = 0; i < 3; i++)
            draw_key(s, ax + i * (KEY_W + KEY_GAP), ay + KEY_H + KEY_GAP,
                     &arrows_low[i]);
    }

    /* The traffic counters, because a key that never lights has two
     * explanations and these separate them: no bytes at all is a dead
     * port, bytes with nothing lighting is a keyboard talking in codes
     * this table does not have. */
    {
        char line[64];
        snprintf(line, sizeof(line),
                 "PS/2 %lu bytes (last 0x%02X)   USB %lu keys (last 0x%02X)",
                 (unsigned long)ui_ps2_kbd_bytes(), ui_ps2_kbd_last_byte(),
                 (unsigned long)ui_usb_kbd_events(), ui_usb_kbd_last_usage());
        ui_text(s, cx, ry + 8, line, UI_GREY_5);
    }

    ui_clip_reset(s);

    s_stop_w = ui_button_width("Close");
    s_stop_h = 18;
    s_stop_x = x + w - UI_WIN_PAD - DLG_INSET - s_stop_w;
    s_stop_y = y + h - UI_WIN_PAD - DLG_BOT - 18;
    ui_button(s, s_stop_x, s_stop_y, s_stop_w, s_stop_h, "Close",
              true, false, true);

    ui_video_present();
}

void dlg_keys(const dlg_ctx_t *c) {
    /* Esc is a key like any other here: it has to light when pressed, so
     * it cannot also be the way out on its own. Holding it briefly is
     * the compromise - the dialog closes on release, which means the key
     * gets tested and still closes the window.
     *
     * The Close button and the mouse work at any time, and on a board
     * with no pointer that leaves Esc as the only exit, which is why it
     * has to work at all. */
    bool stop = false;
    bool esc_seen = false;

    draw(c);

    while (!stop) {
        ui_input_task();

        /* Drain the queue rather than acting on it: this dialog reads
         * held state, and leaving keystrokes queued would hand a pile of
         * them to whatever opens next. */
        while (ui_input_getkey() != UI_KEY_NONE) { }

        const bool esc_now = ui_key_held(0x29, 0x76, false);
        if (esc_now) esc_seen = true;
        else if (esc_seen) stop = true;

        const ui_pointer_t *p = ui_input_pointer();
        if (p->pressed && p->y >= s_stop_y && p->y < s_stop_y + s_stop_h &&
            p->x >= s_stop_x && p->x < s_stop_x + s_stop_w)
            stop = true;

        draw(c);
        sleep_ms(16);
    }

    ui_textpage_modal_clear();
}
