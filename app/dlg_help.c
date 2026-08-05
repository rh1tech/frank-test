/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_help.c — what the keys do, on the screen rather than in a README.
 *
 *
 * WHY THIS EXISTS
 *
 * The interface has a menu bar, and on the outputs that can draw it that
 * is discovery enough: open a menu and the equivalents are written
 * beside the items. The text-page outputs — composite, and the PiZero's
 * PIO HDMI — draw menus as a plain list with no room for that column, so
 * on those boards the shortcuts existed and nothing said so.
 *
 * That is not a cosmetic gap. Those are the boards most likely to be
 * driven from a PS/2 keyboard with no mouse, where the keys are the only
 * way in.
 *
 *
 * ONE LIST, DRAWN TWICE
 *
 * The lines below are the single source: the window renders them and the
 * text page renders the same array through ui_textpage_modal(). Two
 * hand-maintained copies would disagree within a release, and the copy
 * that disagreed would be the one on the board that needed it.
 *
 * They are checked against the menu tables rather than remembered — the
 * one collision this turned up (PS/2 Ports on P, which Audio's PWM item
 * already owned, so it could never be reached) is exactly the kind of
 * thing that survives indefinitely while nothing lists it.
 */

#include "dlgs.h"

#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_input.h"
#include "ui_textpage.h"
#include "ui_video.h"
#include "ui_window.h"

#include "pico/stdlib.h"

#define DLG_W 420

/* Grouped by where the command lives, because that is how someone looks
 * for one: they know they want a video mode, not that they want 'C'. */
static const char *const lines[] = {
    "Alt+F File   Alt+B Board   Alt+V Video",
    "Alt+U Audio  Alt+T Tests   Alt+A About",
    "",
    "Tests    A run all       E run selected",
    "         Enter runs the selected row",
    "         G gamepads      K PS/2 ports",
    "         L LEDs          N tape in",
    "",
    "Audio    P PWM           2 TDA (I2S)",
    "         U TurboSound    M PCM5122",
    "",
    "Video    H HDMI          V VGA",
    "         C composite     T test card",
    "",
    "Board    I board info    S set board",
    "         R restart",
    "",
    "List     arrows  PgUp/PgDn  Home/End",
    "At boot  hold H V C A video, U console",
};
#define LINE_COUNT ((int)(sizeof(lines) / sizeof(lines[0])))

void dlg_help(const dlg_ctx_t *c) {
    ui_surface_t *s = ui_video_surface();

    ui_textpage_modal("Keyboard Shortcuts", lines, LINE_COUNT, -1,
                      "any key closes");

    c->paint_background();

    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + LINE_COUNT * 12
                + DLG_FOOT + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "Keyboard Shortcuts", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cy += DLG_TOP;

    for (int i = 0; i < LINE_COUNT; i++) {
        /* A group heading is a line that starts in the first column;
         * continuations are indented. Colouring them apart is what makes
         * this scannable rather than a wall of pairs. */
        const bool heading = lines[i][0] != ' ' && lines[i][0] != '\0';
        ui_text(s, cx, cy + i * 12, lines[i],
                heading ? UI_BLACK : UI_GREY_5);
    }

    ui_text(s, cx, cy + LINE_COUNT * 12 + 6, "any key closes", UI_GREY_5);

    ui_video_present();

    /* Dismissed by anything at all, including the mouse. A help screen
     * that needs the right key to leave is its own joke. */
    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }

    while (true) {
        ui_input_task();
        if (ui_input_getkey() != UI_KEY_NONE) break;
        if (ui_input_pointer()->pressed) break;
        sleep_ms(8);
    }

    ui_textpage_modal_clear();
}
