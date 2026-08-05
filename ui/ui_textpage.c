/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_textpage.c — the interface as a page of text.
 *
 * Two outputs in this fleet cannot show the desktop and both need the
 * same answer. Composite carries somewhere near 320 usable samples a
 * line however the source is arranged, and the Waveshare PiZero's PIO
 * HDMI driver scans a 320x240 indexed framebuffer doubled to 640x480.
 * Scaling 640x480 type into either is a grey smear, and a rig whose
 * results cannot be read is not reporting anything.
 *
 * So both draw a page of text at native resolution instead, where every
 * glyph lands on exactly the pixels the font intended. This was written
 * for composite and lived inside that backend with its geometry baked
 * in; the PiZero needed the same thing at a different size, and one copy
 * that takes its dimensions at run time is better than two that drift.
 *
 * Menus, dialogs and the board picker are drawn here too. They have to
 * be: on these outputs there is no desktop behind them, so without this
 * the interface is invisible - and on an unidentified board that means
 * no way to reach Set Board and no way to run anything at all.
 */

#include "ui_textpage.h"

#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_menu.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* Where the page is drawn, and how big it is. Set by whichever backend
 * owns the frame. */
static uint8_t *s_frame;
static int      s_w, s_h, s_cols, s_rows;

void ui_textpage_target(uint8_t *frame, int w, int h) {
    s_frame = frame;
    s_w = w; s_h = h;
    s_cols = w / UI_CHAR_W;
    s_rows = h / UI_CHAR_H;
}

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

void ui_textpage_set_desktop(const ui_desktop_t *d) { s_desk = d; }

bool ui_textpage_ready(void) { return s_desk != NULL; }
void ui_textpage_set_menubar(const ui_menubar_t *mb) { s_menu = mb; }

/* One glyph, straight into the 8 bpp frame. The font stores each row
 * left-justified in the top 6 bits. */
static void __not_in_flash_func(txt_glyph)(int col, int row, char ch,
                                           uint8_t fg, uint8_t bg) {
    if (col < 0 || col >= s_cols || row < 0 || row >= s_rows) return;

    const unsigned u = (unsigned char)ch;
    const uint8_t *g = ui_font_6x8[(u < 32 || u > 126) ? 0 : u - 32];
    uint8_t *dst = s_frame + (size_t)row * UI_CHAR_H * s_w + col * UI_CHAR_W;

    for (int y = 0; y < UI_CHAR_H; y++, dst += s_w) {
        const uint8_t bits = g[y];
        for (int x = 0; x < UI_CHAR_W; x++)
            dst[x] = (bits & (0x80u >> x)) ? fg : bg;
    }
}

static void txt_puts(int col, int row, const char *str, uint8_t fg, uint8_t bg) {
    for (int i = 0; str[i] && col + i < s_cols; i++)
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
    if (width > s_cols - 2 * TXT_M_X) width = s_cols - 2 * TXT_M_X;

    const int x0 = (s_cols - width) / 2;
    const int y0 = (s_rows - rows) / 2;

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
void __not_in_flash_func(ui_textpage_draw)(void) {
    if (!s_frame) return;
    memset(s_frame, UI_BLACK, (size_t)s_w * s_h);
    if (!s_desk) return;

    char line[80];

    /* Modal things first, and nothing behind them. */
    if (s_desk->picker_title) {
        txt_box(s_desk->picker_title, s_desk->picker_items,
                s_desk->picker_count > 16 ? 16 : s_desk->picker_count,
                s_desk->picker_sel);
        txt_puts(TXT_M_X, s_rows - TXT_M_Y - 1,
                 "arrows choose   Enter accept   Esc cancel",
                 UI_GREY_2, UI_BLACK);
        return;
    }
    if (s_desk->dialog_title) {
        const char *body[4]; int n = 0;
        for (int i = 0; i < 4; i++)
            if (s_desk->dialog_body[i]) body[n++] = s_desk->dialog_body[i];
        txt_box(s_desk->dialog_title, body, n, -1);
        txt_puts(TXT_M_X, s_rows - TXT_M_Y - 1,
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
        txt_puts(TXT_M_X, s_rows - TXT_M_Y - 1,
                 "arrows choose   Enter run   Esc close", UI_GREY_2, UI_BLACK);
        return;
    }

    /* Title bar: fill the row first, then the text over it. */
    for (int c = TXT_M_X; c < s_cols - TXT_M_X; c++)
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
    for (int i = 0; i < s_desk->row_count && row < s_rows - TXT_M_Y - 2; i++, row++) {
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
        /* As much of the measurement as the line actually has room for,
         * measured rather than assumed. This was a fixed 19 characters
         * against a page 30 wide here, so results were cut mid-word -
         * "512 B written and r" - with eleven columns of black to the
         * right of them. Every detail on a full run fits the real
         * width; the marker below is for the few that do not. */
        if (r->detail) {
            const int col_x = TXT_M_X + 21;
            const int room  = s_cols - TXT_M_X - col_x;
            if (room > 1) {
                snprintf(line, sizeof(line), "%.*s", room, r->detail);
                /* A cut is worth seeing. Silent truncation reads as a
                 * complete sentence that happens to end oddly. */
                if ((int)strlen(r->detail) > room) line[room - 1] = '>';
                txt_puts(col_x, row, line, UI_GREY_2, UI_BLACK);
            }
        }
    }

    snprintf(line, sizeof(line), "%d passed  %d failed  %d n/a",
             s_desk->passed, s_desk->failed, s_desk->na);
    txt_puts(TXT_M_X, s_rows - TXT_M_Y - 1, line, UI_WHITE, UI_BLACK);
}

