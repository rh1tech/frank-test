/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_ps2.c — the PS/2 ports, watched live.
 *
 *
 * WHY A DIALOG AND NOT A ROW
 *
 * A PS/2 port cannot be tested without a device on it, and a device
 * cannot be tested without someone using it. There is no measurement
 * that distinguishes a working port with nothing plugged in from a
 * broken one, because both are silent. So this shows what the port is
 * carrying and lets the operator decide, in the ten seconds it takes to
 * press a key and move the mouse.
 *
 * Those connectors are a common cold-solder site, and a joint that
 * passes continuity can still fail under the level shifter's load. The
 * useful evidence is bytes arriving, which is what this counts.
 *
 *
 * WHAT THE NUMBERS MEAN
 *
 * The byte count is the raw one, taken off the wire before any decoding.
 * It rises for anything the receiver clocks in, valid or not, which is
 * the point: a port that carries garbage is wired but wrong — the clock
 * or the data line crossed, usually — and looks identical to a dead one
 * if you only watch decoded keys. Bytes climbing with nothing sensible
 * decoded is a different fault from no bytes at all, and this separates
 * them.
 *
 * The last raw code is shown next to it for the same reason. A keyboard
 * announces itself with 0xAA when it finishes its self-test, so a port
 * that shows 0xAA and nothing else has a keyboard that is alive and a
 * host that is not hearing keystrokes.
 *
 *
 * THE MOUSE MAY NOT BE THERE TO TEST
 *
 * On every board in the fleet the PS/2 mouse sits on GP0/GP1, which is
 * also UART0, so the two cannot both exist. Where the operator has asked
 * to keep the console — File > Serial Console, or U held at boot — the
 * mouse was never initialised and this says so rather than reporting a
 * dead port. That distinction is the whole reason the panel names which
 * of the two it is.
 */

#include "dlgs.h"

#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_input.h"
#include "ui_video.h"
#include "ui_textpage.h"
#include "ui_window.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

#define DLG_W    460
#define PANEL_W  ((DLG_W - 2 * DLG_INSET - 12) / 2)
#define PANEL_H  118

static int s_stop_x, s_stop_y, s_stop_w, s_stop_h;

/* Where the pointer has been driven to by the PS/2 mouse alone, so the
 * panel can show that both axes move and in which direction. Clamped to
 * the box rather than wrapped: a value that wraps looks like a fault. */
typedef struct {
    int      x, y;
    int      wheel;
    unsigned buttons;
    uint32_t packets;
} ms_state_t;

static void panel(ui_surface_t *s, int x, int y, const char *title,
                  bool live, const char *why_not) {
    ui_bevel_in(s, x, y, PANEL_W, PANEL_H);
    ui_fill(s, x + 1, y + 1, PANEL_W - 2, PANEL_H - 2,
            live ? UI_PAPER : UI_GREY_1);
    ui_text(s, x + 6, y + 5, title, live ? UI_BLACK : UI_GREY_4);
    ui_hline(s, x + 5, y + 16, PANEL_W - 10, UI_GREY_3);

    if (!live && why_not)
        ui_text(s, x + 6, y + 24, why_not, UI_GREY_4);
}

/* A button pip, filled while the button is down. */
static void pip(ui_surface_t *s, int x, int y, const char *label, bool down) {
    ui_bevel_in(s, x, y, 22, 14);
    ui_fill(s, x + 1, y + 1, 20, 12, down ? UI_ACCENT : UI_GREY_1);
    ui_text(s, x + 7, y + 3, label, down ? UI_PAPER : UI_GREY_4);
}

/* The text-page version - see ui_textpage_modal(). Raw byte counts, for
 * the same reason the panels show them: a port carrying garbage is wired
 * and wrong, and looks identical to a dead one if you only watch keys. */
static char s_tp[3][40];
static const char *s_tp_lines[3];

static void publish_textpage(const ms_state_t *m, bool mouse_live) {
    if (ui_ps2_keyboard_up())
        snprintf(s_tp[0], sizeof(s_tp[0]), "Keyboard: %lu bytes, last 0x%02X",
                 (unsigned long)ui_ps2_kbd_bytes(), ui_ps2_kbd_last_byte());
    else
        snprintf(s_tp[0], sizeof(s_tp[0]), "Keyboard: no pins on this board");

    if (mouse_live)
        snprintf(s_tp[1], sizeof(s_tp[1]), "Mouse: %lu packets, buttons %u",
                 (unsigned long)m->packets, m->buttons);
    else
        snprintf(s_tp[1], sizeof(s_tp[1]), "Mouse: not running");

    snprintf(s_tp[2], sizeof(s_tp[2]), "USB kbd: %lu keys, last 0x%02X",
             (unsigned long)ui_usb_kbd_events(), ui_usb_kbd_last_usage());

    for (int i = 0; i < 3; i++) s_tp_lines[i] = s_tp[i];
    ui_textpage_modal("PS/2 Ports", s_tp_lines, 3, -1,
                      "type, or move the mouse   Esc closes");
}

static void draw(const dlg_ctx_t *c, const ms_state_t *m, bool mouse_live,
                 const char *mouse_why) {
    ui_surface_t *s = ui_video_surface();
    publish_textpage(m, mouse_live);
    c->paint_background();

    /* +34 for the USB line and its hint below the panels. */
    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + 14 + PANEL_H + 34
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 20;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "PS/2 Ports", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    ui_text(s, cx, cy, "Press keys and move the mouse. The counts should climb.",
            UI_GREY_5);

    const int py = cy + 14;
    const int mx = cx + PANEL_W + 12;
    char line[64];

    /* ---- keyboard ---- */
    const bool kbd_live = ui_ps2_keyboard_up();
    panel(s, cx, py, "Keyboard", kbd_live,
          "no PS/2 keyboard pins on this board");

    if (kbd_live) {
        const uint32_t bytes = ui_ps2_kbd_bytes();

        snprintf(line, sizeof(line), "bytes  %lu", (unsigned long)bytes);
        ui_text(s, cx + 6, py + 24, line, bytes ? UI_BLACK : UI_GREY_4);

        snprintf(line, sizeof(line), "last   0x%02X", ui_ps2_kbd_last_byte());
        ui_text(s, cx + 6, py + 38, line, bytes ? UI_BLACK : UI_GREY_4);

        if (!bytes) {
            ui_text(s, cx + 6, py + 60, "nothing yet.", UI_GREY_4);
            ui_text(s, cx + 6, py + 72, "press a key, or check the", UI_GREY_4);
            ui_text(s, cx + 6, py + 84, "connector and level shifter", UI_GREY_4);
        } else {
            ui_text(s, cx + 6, py + 60, "the port is carrying data.", UI_GREY_5);
            ui_text(s, cx + 6, py + 72, "0xAA alone means the keyboard", UI_GREY_5);
            ui_text(s, cx + 6, py + 84, "booted but keys are not heard", UI_GREY_5);
        }
    }

    /* ---- mouse ---- */
    panel(s, mx, py, "Mouse", mouse_live, mouse_why);

    if (mouse_live) {
        snprintf(line, sizeof(line), "packets  %lu", (unsigned long)m->packets);
        ui_text(s, mx + 6, py + 24, line, m->packets ? UI_BLACK : UI_GREY_4);

        snprintf(line, sizeof(line), "x %+4d   y %+4d   wheel %+d",
                 m->x, m->y, m->wheel);
        ui_text(s, mx + 6, py + 38, line, m->packets ? UI_BLACK : UI_GREY_4);

        pip(s, mx + 6,  py + 56, "L", (m->buttons & 1u) != 0u);
        pip(s, mx + 32, py + 56, "M", (m->buttons & 4u) != 0u);
        pip(s, mx + 58, py + 56, "R", (m->buttons & 2u) != 0u);

        if (!m->packets)
            ui_text(s, mx + 6, py + 84, "move it, or press a button", UI_GREY_4);
        else
            ui_text(s, mx + 6, py + 84, "both axes should change", UI_GREY_5);
    }

    /* ---- USB, under both panels ----
     *
     * The scancode test was PS/2 only, which made it useless on the
     * boards whose only keyboard is USB - and those are the majority
     * now. Same question, same counters: a keyboard that enumerates and
     * then sends nothing is a different fault from an empty socket, and
     * without a count the two look alike.
     *
     * One line rather than a third panel: the interesting comparison is
     * against the PS/2 numbers above, and HID gives usage IDs rather
     * than scancodes, so there is no second byte stream to show. */
    {
        const uint32_t keys = ui_usb_kbd_events();
        snprintf(line, sizeof(line), "USB keyboard:  %lu key%s, last usage 0x%02X",
                 (unsigned long)keys, keys == 1 ? "" : "s",
                 ui_usb_kbd_last_usage());
        ui_text(s, cx, py + PANEL_H + 6, line, keys ? UI_BLACK : UI_GREY_4);

        if (!keys)
            ui_text(s, cx, py + PANEL_H + 20,
                    "nothing yet - press a key on a USB keyboard, if one is fitted",
                    UI_GREY_4);
    }

    ui_clip_reset(s);

    s_stop_w = ui_button_width("Stop");
    s_stop_h = 18;
    s_stop_x = x + DLG_W - 16 - s_stop_w;
    s_stop_y = y + h - UI_WIN_PAD - DLG_BOT - 18;
    ui_button(s, s_stop_x, s_stop_y, s_stop_w, s_stop_h, "Stop", true, false, true);

    ui_text(s, cx, s_stop_y + 5, "Esc or Stop to finish", UI_GREY_5);

    ui_video_present();
}

/* ------------------------------------------------------------------ */

void dlg_ps2(const dlg_ctx_t *c) {
    const detect_result_t *d = c->detect;
    if (!d || !d->board) return;

    const frank_pins_t *p = &d->board->pins;

    /* The mouse is only up when nothing else claimed its pins. Naming
     * which of the two reasons it is off matters: "no port on this
     * board" and "you asked to keep the console" send an operator to
     * completely different places. */
    const bool mouse_live = ui_ps2_mouse_up();
    const char *mouse_why =
        (p->ps2_ms_clk == PIN_NC) ? "no PS/2 mouse pins on this board"
                                  : "off: the console has GP0/GP1";

    ms_state_t m = {0};

    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }

    ui_cursor_overlay_reset();

    uint32_t last_bytes = 0xFFFFFFFFu;
    ms_state_t last_m = { .packets = 0xFFFFFFFFu };
    bool stop = false;

    while (!stop) {
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

        /* Read the mouse directly rather than through the pointer, which
         * merges USB and PS/2 — a USB mouse moving the cursor would
         * otherwise look like a working PS/2 port. */
        if (mouse_live) {
            int dx = 0, dy = 0, wheel = 0;
            unsigned buttons = 0;
            if (ui_ps2_mouse_read(&dx, &dy, &wheel, &buttons)) {
                if (dx || dy || wheel) m.packets++;
                m.x += dx; m.y += dy; m.wheel += wheel;
                if (m.x < -999) m.x = -999;
                if (m.x >  999) m.x =  999;
                if (m.y < -999) m.y = -999;
                if (m.y >  999) m.y =  999;
            }
            m.buttons = buttons;
        }

        const uint32_t bytes = ui_ps2_kbd_bytes();
        const bool changed = (bytes != last_bytes) ||
                             (m.packets != last_m.packets) ||
                             (m.buttons != last_m.buttons) ||
                             (m.x != last_m.x) || (m.y != last_m.y);

        if (changed || moved) {
            if (changed) draw(c, &m, mouse_live, mouse_why);
            if (pt->present) ui_cursor_overlay_move(pt->x, pt->y);
            last_bytes = bytes;
            last_m     = m;
        }

        sleep_ms(8);
    }

    ui_textpage_modal_clear();
}
