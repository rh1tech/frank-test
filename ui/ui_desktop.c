/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui_desktop.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Menus                                                               */
/* ------------------------------------------------------------------ */

/* Items that are not implemented are marked disabled rather than left
 * enabled and inert. An enabled command that does nothing when you press
 * Return teaches the operator that Return does not work, which is a much
 * more expensive lesson than a greyed-out line. */
/* Only what works.
 *
 * Every item here is implemented. Items that were declared and then left
 * greyed out have been deleted rather than kept as placeholders: a menu
 * full of permanently-disabled commands is noise, and it makes the ones
 * that are legitimately disabled — Stop, when nothing is running —
 * impossible to distinguish from the ones that are simply unfinished. */

/* Not const. The audio and gamepad items are enabled per board, and a
 * board is only known after detection — see ui_desktop_set_cmd_enabled().
 * The alternative was a parallel array of enable flags, which is one more
 * thing that can disagree with what is drawn. */
static ui_menu_item_t mark_items[] = {
    { "About FRANK Test...", 0,   true, false, CMD_ABOUT },
    { NULL,                  0,   true, false, CMD_NONE },
    { "Keyboard Shortcuts",  0,   true, false, CMD_HELP },
    { "Board Info...",       'I', true, false, CMD_BOARD_INFO },
    { "Pin Signature...",    0,   true, false, CMD_SHOW_SIG },
};

static ui_menu_item_t file_items[] = {
    /* Ticked when the console is being kept. The change lands on the
     * next boot, because the pins are claimed once during start-up and
     * taking them back from a running PS/2 mouse is not something this
     * firmware can do safely. */
    { "Clocks and Voltage",  0,   true, false, CMD_OVERCLOCK },
    { "Save Report to SD",   0,   true, false, CMD_REPORT },
    { "Serial Console",      0,   true, false, CMD_CONSOLE },
    { NULL,                  0,   true, false, CMD_NONE },
    { "Restart",             'R', true, false, CMD_RESTART },
    /* No keyboard equivalent, deliberately.
     *
     * This is the one command that ends the session — reset_usb_boot()
     * leaves the application and the screen goes dark. ui_input.c does
     * not decode Ctrl, so the "^B" this used to advertise arrived as a
     * bare 'B', and any stray press of that key dropped the board into
     * BOOTSEL. Reported as "I pressed Ctrl+B and lost the signal",
     * which is exactly what happened. Menu only now. */
    { "Enter BOOTSEL",       0,   true, false, CMD_BOOTSEL },
};

static ui_menu_item_t board_items[] = {
    { "Set Board...",        'S', true, false, CMD_SET_BOARD },
};

/* The one place the operator can override what no probe can determine.
 * Composite is selectable here and nowhere else, because nothing on any
 * FRANK board can tell you a composite monitor is attached. */
static ui_menu_item_t video_items[] = {
    { "Automatic",           0,   true, false, CMD_VIDEO_AUTO },
    { NULL,                  0,   true, false, CMD_NONE },
    { "HDMI",                'H', true, false, CMD_VIDEO_HDMI },
    { "VGA",                 'V', true, false, CMD_VIDEO_VGA },
    { "Composite",           'C', true, false, CMD_VIDEO_COMPOSITE },
    { NULL,                  0,   true, false, CMD_NONE },
    { "Show Test Card",      'T', true, false, CMD_VIDEO_TESTCARD },
};

/* Sound is not a test and never was: nothing on any FRANK board can hear
 * the output, so there is no verdict for the firmware to reach. These
 * open a dialog that loops the melody through each channel until the
 * operator has heard it — which is where the verdict actually lives.
 * Each is enabled only on a board that has that path. */
static ui_menu_item_t audio_items[] = {
    { "PWM",                 'P', false, false, CMD_AUDIO_PWM },
    { "TDA (I2S)",           '2', false, false, CMD_AUDIO_I2S },
    { "TurboSound",          'U', false, false, CMD_AUDIO_TS },
    { "PCM5122",             'M', false, false, CMD_AUDIO_PCM5122 },
};

static ui_menu_item_t tests_items[] = {
    { "Run All",             'A', true,  false, CMD_RUN_ALL },
    { "Run Selected",        'E', true,  false, CMD_RUN_SELECTED },
    { "Burn-in...",          0,   true,  false, CMD_BURNIN },
    { NULL,                  0,   true,  false, CMD_NONE },
    { "NES Gamepad(s)...",   'G', false, false, CMD_NESPAD },
    /* K, not P: bare letters are resolved by scanning the menus in
     * order, Audio comes before Tests, and Audio's PWM item already has
     * P - so this one could never be reached by its own key. Nobody
     * noticed because nothing listed the shortcuts until F1. */
    { "PS/2 Ports...",       'K', false, false, CMD_PS2 },
    { "Keyboard...",         'Y', true,  false, CMD_KEYS },
    { "LEDs...",             'L', false, false, CMD_LED },
    { "Tape In...",          'N', false, false, CMD_TAPE },
};

/* The counts here are the length of each item array and nothing else.
 * Getting one wrong silently truncates a menu - the PCM5122 item existed,
 * was enabled, and simply did not appear, because this said three. */
static const ui_menu_t menus[] = {
    { "",        mark_items,   5, true,  'A' },
    { "File",    file_items,   6, false, 'F' },
    { "Board",   board_items,  1, false, 'B' },
    { "Video",   video_items,  7, false, 'V' },
    { "Audio",   audio_items,  4, false, 'U' },
    { "Tests",   tests_items,  9, false, 'T' },
};

static const ui_menubar_t default_bar = {
    menus, (int)(sizeof(menus) / sizeof(menus[0])), -1, -1
};

const ui_menubar_t *ui_desktop_menus(void) { return &default_bar; }

void ui_desktop_set_cmd_checked(int cmd, bool checked) {
    if (cmd == CMD_NONE) return;
    for (int m = 0; m < default_bar.count; m++) {
        const ui_menu_t *mn = &default_bar.menus[m];
        ui_menu_item_t *items = (ui_menu_item_t *)mn->items;
        for (int i = 0; i < mn->count; i++)
            if (items[i].cmd == cmd) items[i].checked = checked;
    }
}

void ui_desktop_set_cmd_enabled(int cmd, bool enabled) {
    if (cmd == CMD_NONE) return;
    for (int m = 0; m < default_bar.count; m++) {
        const ui_menu_t *mn = &default_bar.menus[m];
        /* The tables are the mutable originals; ui_menu_t holds them by
         * const pointer because nothing in ui_menu.c has any business
         * writing to them. */
        ui_menu_item_t *items = (ui_menu_item_t *)mn->items;
        for (int i = 0; i < mn->count; i++)
            if (items[i].cmd == cmd) items[i].enabled = enabled;
    }
}

/* ------------------------------------------------------------------ */
/* Status glyphs                                                       */
/* ------------------------------------------------------------------ */

static void draw_state(ui_surface_t *s, int x, int y, const ui_test_row_t *r) {
    switch (r->state) {
        case TEST_PASS:
            ui_blit_tinted(s, ui_icon(ICON_TICK), x, y, UI_OK);
            break;
        case TEST_FAIL:
            ui_blit_tinted(s, ui_icon(ICON_CROSS), x, y, UI_FAIL);
            break;
        case TEST_NA:
            /* Grey, never the fail colour. "This board has no PS/2" and
             * "the PS/2 is broken" must not look alike. */
            ui_blit_tinted(s, ui_icon(ICON_DASH), x, y, UI_GREY_4);
            break;
        case TEST_NORUN:
            /* Amber: neither pass nor fail, and distinct from n/a — this
             * one needs the operator to do something. */
            ui_blit_tinted(s, ui_icon(ICON_QUERY), x, y, UI_WARN);
            break;
        case TEST_RUNNING:
        case TEST_PENDING:
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Composition                                                         */
/* ------------------------------------------------------------------ */

/* ui_clip_reset() already honours any repaint region set by
 * ui_clip_region(), so composition needs nothing special. */
#define reclip ui_clip_reset

#define ROW_H      20
/* A wrapped row is two lines with the same margins a single line gets:
 * six above, six below, two between. Anything less and the second line
 * sits tight against the row below it, which reads as a different row. */
#define ROW_H_WRAP 30
/* ui_scrollbar() draws fifteen wide. Reserving thirteen let it overhang
 * the content area and lose its right-hand edge to the clip. */
#define BAR_W      15
#define LIST_X     14
#define LIST_PAD   12                      /* gap under the menu bar */
#define LIST_Y     (UI_MENUBAR_H + LIST_PAD)
#define LIST_W     416

/* The same gap at the bottom as at the top. It was a fixed 390, which
 * left a 58-pixel band of bare desktop under the window and read as the
 * layout having run out rather than as deliberate margin. Deriving it
 * from LIST_PAD means the two can no longer drift apart.
 *
 * Then rounded down to a whole number of plain rows, so the common case
 * - every row the same height, nothing wrapped - ends exactly on a row
 * boundary with nothing clipped. When a wrapped row makes that
 * impossible the list simply runs past the bottom edge and is clipped
 * there, which is what a scrolling list should look like. */
#define LIST_CHROME (UI_TITLE_H + UI_WIN_PAD * 2 + 1)
#define LIST_H_RAW  (UI_SCREEN_H - LIST_Y - LIST_PAD)
#define LIST_ROWS   ((LIST_H_RAW - LIST_CHROME) / ROW_H)
#define LIST_H      (LIST_CHROME + LIST_ROWS * ROW_H)

#define INFO_X     (LIST_X + LIST_W + 14)
#define INFO_W     182

/* ------------------------------------------------------------------ */
/* List geometry                                                       */
/* ------------------------------------------------------------------ */

/* What the last draw actually laid out.
 *
 * Hit-testing and scrolling used to repeat the sums from a copy of the
 * constants in main.c, with a hardcoded row count beside them. That was
 * survivable while every row was the same height and wrong the moment
 * one of them wrapped - and it had already drifted: the copy said twenty
 * rows fit, the draw fitted more, and the scroll bar never appeared
 * because the two disagreed about whether anything overflowed.
 *
 * The draw records what it did instead, and everything else reads it. */
static struct {
    int cx, cy, cw, ch;
    int y[40];
    int h[40];
    int count;

    /* The scroll bar, so clicks on it can be resolved against what was
     * drawn rather than against a second guess at where it is. */
    int  bar_x, bar_y, bar_h;
    bool bar_shown;

    /* The width rows were laid out at, which decides which of them
     * wrapped and so how tall they are. */
    int  list_w;
} s_geom;

/* Where the detail column ends and how wide it may be. Kept beside the
 * drawing that uses it so the two cannot drift apart. */
static int detail_avail(int list_w) {
    return (list_w - 22) - 176;
}

static int row_height(const ui_test_row_t *r, int list_w) {
    if (!r->detail || r->state == TEST_RUNNING) return ROW_H;
    return (ui_text_width(r->detail) <= detail_avail(list_w)) ? ROW_H
                                                              : ROW_H_WRAP;
}

/* What the whole list would stand at, at a given width. */
static int list_total_h(const ui_desktop_t *d, int list_w) {
    int total = 0;
    for (int i = 0; i < d->row_count; i++)
        total += row_height(&d->rows[i], list_w);
    return total;
}

int ui_desktop_hit_row(const ui_desktop_t *d, int x, int y) {
    if (x < s_geom.cx || x >= s_geom.cx + s_geom.cw) return -1;
    for (int i = 0; i < s_geom.count; i++) {
        if (y < s_geom.y[i] || y >= s_geom.y[i] + s_geom.h[i]) continue;
        const int idx = d->first_visible + i;
        return (idx < d->row_count) ? idx : -1;
    }
    return -1;
}

int ui_desktop_rows_shown(void) {
    return s_geom.count ? s_geom.count : 1;
}

/* A click, or a held button, on the scroll bar.
 *
 * The arrows step a row, the track pages, and the thumb is dragged. The
 * arrows only act on the press so a held button does not run away with
 * the list, while the thumb tracks continuously, which is what makes it
 * feel like a thumb rather than a jump target. */
/* Where the list has to start for `index` to be shown in full.
 *
 * Rows may be clipped by the bottom of the window - that is what stops
 * the list ending in a band of grey - but the selected row must never
 * be one of them. A highlight cut in half looks like a fault, and it is
 * the one row the operator is actually reading.
 *
 * Answers with the smallest scroll that achieves it: if the row is
 * already fully visible the list does not move at all, and if it is
 * below the fold the list scrolls just far enough to seat it against the
 * bottom edge rather than jumping it to the top. */
int ui_desktop_first_showing(const ui_desktop_t *d, int index) {
    if (index < 0 || index >= d->row_count) return d->first_visible;
    if (index < d->first_visible) return index;
    if (s_geom.ch <= 0) return index;

    /* Walk back from the target, taking rows while they still fit. */
    int first = index;
    int used  = row_height(&d->rows[index], s_geom.list_w);

    while (first > 0) {
        const int h = row_height(&d->rows[first - 1], s_geom.list_w);
        if (used + h > s_geom.ch) break;
        used += h;
        first--;
    }

    /* Already seated somewhere above that point: leave it alone. */
    return (d->first_visible > first) ? d->first_visible : first;
}

int ui_desktop_scroll_hit(const ui_desktop_t *d, int x, int y,
                          bool pressed, bool held) {
    if (!s_geom.bar_shown) return -1;
    if (x < s_geom.bar_x || x >= s_geom.bar_x + BAR_W) return -1;
    if (y < s_geom.bar_y || y >= s_geom.bar_y + s_geom.bar_h) return -1;

    const int shown = ui_desktop_rows_shown();
    const int max   = (d->row_count > shown) ? d->row_count - shown : 0;
    int first = d->first_visible;

    const int arrow_top = s_geom.bar_y + BAR_W;
    const int arrow_bot = s_geom.bar_y + s_geom.bar_h - BAR_W;

    if (y < arrow_top)  { if (pressed) first -= 1; }
    else if (y >= arrow_bot) { if (pressed) first += 1; }
    else if (held || pressed) {
        /* Proportional, from the middle of the thumb, so the row under
         * the cursor is the one that stays there while dragging. */
        const int track = arrow_bot - arrow_top;
        int pos = y - arrow_top - 6;
        if (track > 12 && max > 0) first = (pos * max) / (track - 12);
        else                       first = 0;
    } else {
        return -1;
    }

    if (first > max) first = max;
    if (first < 0)   first = 0;
    return first;
}

static void draw_results_window(ui_surface_t *s, const ui_desktop_t *d) {
    /* No close box, no scroll bar, no grow box. Nothing in this
     * firmware closes or resizes a window, and a control that does
     * nothing is worse than an absent one: it invites a click and then
     * teaches the operator that clicking does not work. Scrolling is by
     * arrow key and by wheel. */
    ui_window_t w = {
        .x = LIST_X, .y = LIST_Y, .w = LIST_W, .h = LIST_H,
        .title = "Board Tests", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &w);

    int cx, cy, cw, ch;
    ui_window_content(&w, &cx, &cy, &cw, &ch);

    /* Laid out twice when it has to be.
     *
     * How wide a row is decides whether its detail wraps, which decides
     * how tall it is, which decides whether the list overflows, which
     * decides whether there is a scroll bar - which decides how wide a
     * row is. Reserving the bar's width unconditionally broke that
     * circle and left a blank strip down the right of every board whose
     * tests happen to fit.
     *
     * So: lay out at full width, and if that overflows, lay out again
     * with the bar's width taken off. Twice is the most it can ever be,
     * because the second pass only ever makes rows taller and a list
     * that overflowed at full width cannot stop overflowing at less. */
    int list_w = cw;
    if (d->first_visible > 0 || list_total_h(d, cw) > ch)
        list_w = cw - BAR_W;

    s_geom.cx = cx; s_geom.cy = cy; s_geom.cw = cw; s_geom.ch = ch;
    s_geom.list_w = list_w;
    s_geom.count = 0;

    int y = cy;
    for (int i = 0; i + d->first_visible < d->row_count; i++) {
        const int idx = d->first_visible + i;
        const ui_test_row_t *r = &d->rows[idx];

        const int row_h = row_height(r, list_w);
        const int left  = (cy + ch) - y;        /* room still to fill */

        /* Every row that starts inside the window is drawn, and clipped
         * where it runs past the bottom. Nothing is refused for being
         * too tall to finish.
         *
         * The alternative was tried and is worse. Whole rows only leaves
         * a band of bare grey at the bottom while the scroll bar says
         * there is more below, which reads as the list having ended
         * rather than as it continuing past the edge. A row cut through
         * the middle is what a scrolling list is supposed to look like. */
        if (left <= 0) break;

        /* Remembered so hit-testing and scrolling use the geometry that
         * was actually drawn rather than a second copy of the sums. */
        if (s_geom.count < (int)(sizeof(s_geom.y) / sizeof(s_geom.y[0]))) {
            s_geom.y[s_geom.count] = y;
            s_geom.h[s_geom.count] = (row_h < left) ? row_h : left;
            s_geom.count++;
        }

        /* Alternating row tint: a long list of identical rows is much
         * harder to track across than one with a rhythm. */
        /* Clipped to what is left, so a part-drawn row tints only the
         * part of it that is on screen. */
        const int fill_h = (row_h - 1 < left) ? row_h - 1 : left;

        if (idx & 1) ui_fill(s, cx, y, list_w, fill_h, UI_GREY_1);

        const bool sel = (idx == d->selected);
        if (sel) ui_fill(s, cx, y, list_w, fill_h, UI_ACCENT_L);

        /* No drop shadow on these: the 16x16 art is dense enough that a
         * one-pixel offset copy closes the gaps and turns a chip into a
         * blob. The shadow earns its place only on the larger, sparser
         * icons — see the Board window. */
        /* The icon and the name stay on the first line whatever the row
         * height, so a wrapped row still scans as one entry. */
        ui_blit_tinted(s, ui_icon(r->icon), cx + 2, y + 2, UI_BLACK);
        ui_text(s, cx + 24, y + 6, r->name, UI_BLACK);

        if (r->state == TEST_RUNNING) {
            /* The thermometer replaces the detail text while a test is
             * running, because "37%" and "31.8 MiB/s" in the same column
             * mean different things and should not look alike. */
            ui_progress(s, cx + 176, y + 5, list_w - 200, 10, r->progress);
        } else if (r->detail) {
            /* Measurements in a softer ink than the name: the name is
             * what you scan for, the number is what you read once you
             * have found it. */
            const uint8_t ink = (r->state == TEST_FAIL) ? UI_FAIL : UI_GREY_5;
            const int right  = cx + list_w - 22;
            const int avail  = right - (cx + 176);

            if (ui_text_width(r->detail) <= avail) {
                ui_text(s, right - ui_text_width(r->detail), y + 6, r->detail, ink);
            } else {
                /* Two lines rather than a sentence running under the
                 * status tick. Broken at a space, and at the last one
                 * that still fits, so the first line is as full as it
                 * can be and the tail is what wraps. A detail with no
                 * spaces left to break on is drawn as it was and allowed
                 * to overrun: truncating a measurement is worse than
                 * crowding one. */
                /* Comfortably over any detail a test writes; the loop
                 * bounds on it rather than trusting the string. */
                char head[80];
                int  cut = -1;
                for (int k = 0; r->detail[k] && k < (int)sizeof(head) - 1; k++) {
                    if (r->detail[k] != ' ') continue;
                    memcpy(head, r->detail, (size_t)k);
                    head[k] = '\0';
                    if (ui_text_width(head) > avail) break;
                    cut = k;
                }

                if (cut < 0) {
                    ui_text(s, right - ui_text_width(r->detail), y + 6, r->detail, ink);
                } else {
                    memcpy(head, r->detail, (size_t)cut);
                    head[cut] = '\0';
                    const char *tail = r->detail + cut + 1;
                    ui_text(s, right - ui_text_width(head), y + 6,  head, ink);
                    ui_text(s, right - ui_text_width(tail), y + 16, tail, ink);
                }
            }
        }

        draw_state(s, cx + list_w - 18, y + 2, r);

        if (row_h <= left) ui_hline(s, cx, y + row_h - 1, list_w, UI_GREY_2);
        y += row_h;
    }

    /* Drawn whenever the whole list does not fit, which is now measured
     * from what was laid out rather than assumed from a fixed count. */
    s_geom.bar_x     = cx + cw - BAR_W;
    s_geom.bar_y     = cy;
    s_geom.bar_h     = ch;
    s_geom.bar_shown = (list_w != cw);

    if (s_geom.bar_shown)
        ui_scrollbar(s, s_geom.bar_x, s_geom.bar_y, s_geom.bar_h,
                     d->row_count, s_geom.count, d->first_visible);

    reclip(s);
}

static void draw_info_window(ui_surface_t *s, const ui_desktop_t *d) {
    ui_window_t w = {
        .x = INFO_X, .y = LIST_Y, .w = INFO_W, .h = 176,
        .title = "Board", .active = false,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &w);

    int cx, cy, cw, ch;
    ui_window_content(&w, &cx, &cy, &cw, &ch);

    ui_blit_icon(s, ui_icon(ICON_CHIP), cx + (cw - UI_ICON_W) / 2, cy, UI_BLACK);
    {
        int bx = cx + (cw - ui_text_width(d->board_name)) / 2;
        ui_text_bold(s, bx, cy + 22, d->board_name, UI_BLACK);
    }
    ui_text_centred(s, cx, cy + 34, cw, d->mcu_name, UI_GREY_5);

    ui_separator(s, cx, cy + 50, cw);

    ui_text(s, cx, cy + 60, "Video", UI_GREY_5);
    ui_text(s, cx + 54, cy + 60, d->video_name, UI_BLACK);

    ui_text(s, cx, cy + 74, "Unit", UI_GREY_5);
    ui_text(s, cx + 54, cy + 74, d->unit_serial, UI_BLACK);

    ui_separator(s, cx, cy + 90, cw);

    char line[40];
    snprintf(line, sizeof(line), "%d passed", d->passed);
    ui_text(s, cx, cy + 100, line, UI_BLACK);
    ui_blit_tinted(s, ui_icon(ICON_TICK), cx + cw - 18, cy + 96, UI_OK);

    snprintf(line, sizeof(line), "%d failed", d->failed);
    ui_text(s, cx, cy + 116, line, d->failed ? UI_FAIL : UI_GREY_5);
    if (d->failed) ui_blit_tinted(s, ui_icon(ICON_CROSS), cx + cw - 18, cy + 112, UI_FAIL);

    snprintf(line, sizeof(line), "%d n/a", d->na);
    ui_text(s, cx, cy + 132, line, UI_GREY_5);

    reclip(s);
}

/* The strip along the bottom: what the operator has to do by hand.
 * Every board has switches the firmware cannot reach — audio muxes, USB
 * muxes, the tape jumper — and a test rig that silently tests one half
 * of a switched path is claiming coverage it does not have. */
static void draw_manual_strip(ui_surface_t *s, const ui_desktop_t *d) {
    const int y = LIST_Y + 190;
    ui_window_t w = {
        .x = INFO_X, .y = y, .w = INFO_W, .h = LIST_Y + LIST_H - y,
        .title = "Manual Steps", .active = false,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &w);

    int cx, cy, cw, ch;
    ui_window_content(&w, &cx, &cy, &cw, &ch);

    ui_text(s, cx, cy,      "Switches this firmware", UI_BLACK);
    ui_text(s, cx, cy + 11, "cannot reach:", UI_BLACK);

    /* Word-wrapped from the descriptor, not hardcoded. The first
     * hardware capture showed miniFRANK's switch list on a Core 2, which
     * is worse than showing nothing: it tells the operator to flip
     * switches the board does not have. */
    const char *n = d->manual_note;
    if (!n || !*n) {
        ui_text(s, cx, cy + 33, "None on this board.", UI_GREY_5);
    } else {
        const int cols  = cw / UI_CHAR_W;
        /* Derived from the window, not counted off the old 212-pixel
         * height: the window now stretches to the same bottom margin as
         * the list, and a hardcoded limit would just leave the extra
         * space blank. */
        const int lines = ch / 11;
        int line = 3;
        while (*n && line < lines) {
            int take = 0, brk = 0;
            while (n[take] && take < cols) {
                if (n[take] == ' ') brk = take;
                take++;
            }
            if (n[take] && brk) take = brk;
            ui_text_n(s, cx, cy + line * 11, n, take, UI_WARN);
            n += take;
            while (*n == ' ') n++;
            line++;
        }
    }

    reclip(s);
}

/* A list picker. Used for Set Board, which has to offer every board the
 * silicon could be — not just the two the fingerprint tied on, because
 * the operator may know something the pins cannot show. */
static void draw_picker(ui_surface_t *s, const ui_desktop_t *d) {
    if (!d->picker_title) return;

    /* Sixteen rows and a wider box. The fleet is thirteen descriptors and
     * the list no longer filters any of them out, so twelve meant the
     * board you wanted could be off the end of a list you did not know
     * was scrolling. Sixteen rows at 14px still fits 480 comfortably. */
    const int rows = d->picker_count > 16 ? 16 : d->picker_count;
    const int w = 360;
    const int h = UI_TITLE_H + UI_WIN_PAD + rows * 14 + 14 + 18 + UI_WIN_PAD;
    const int x = (s->w - w) / 2;
    const int y = (s->h - h) / 2 - 20;

    ui_window_t win = {
        .x = x, .y = y, .w = w, .h = h,
        .title = d->picker_title, .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, ch;
    ui_window_content(&win, &cx, &cy, &cw, &ch);

    int first = d->picker_sel - rows + 1;
    if (first < 0) first = 0;

    for (int i = 0; i < rows; i++) {
        const int idx = first + i;
        if (idx >= d->picker_count) break;
        const int ry = cy + i * 14;

        if (idx == d->picker_sel) {
            ui_fill(s, cx, ry, cw, 13, UI_ACCENT);
            ui_text(s, cx + 6, ry + 3, d->picker_items[idx], UI_WHITE);
        } else {
            ui_text(s, cx + 6, ry + 3, d->picker_items[idx], UI_BLACK);
        }
    }

    ui_clip_reset(s);
    int bw = ui_button_width("OK");
    ui_button(s, x + w - 16 - bw, y + h - UI_WIN_PAD - 18, bw, 18,
              "OK", true, false, true);
}

static void draw_dialog(ui_surface_t *s, const ui_desktop_t *d) {
    if (!d->dialog_title) return;

    /* Size to the content. A fixed height leaves a slab of empty paper
     * under a four-line message, which reads as a layout mistake rather
     * than as breathing room. */
    int lines = 0;
    for (int i = 0; i < 4; i++) if (d->dialog_body[i]) lines = i + 1;
    if (lines < 1) lines = 1;

    const int body_h = lines * 11;
    const int icon_h = UI_ICON_H;
    const int content_h = (body_h > icon_h) ? body_h : icon_h;

    const int w = 340;
    const int h = UI_TITLE_H + UI_WIN_PAD + content_h + 14 + 18 + UI_WIN_PAD;
    const int x = (s->w - w) / 2;
    const int y = (s->h - h) / 2 - 20;

    ui_window_t win = {
        .x = x, .y = y, .w = w, .h = h,
        .title = d->dialog_title, .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, ch;
    ui_window_content(&win, &cx, &cy, &cw, &ch);

    ui_blit_icon(s, ui_icon(ICON_QUERY), cx + 4, cy + 4, UI_WARN);

    for (int i = 0; i < 4; i++)
        if (d->dialog_body[i])
            ui_text(s, cx + 32, cy + 4 + i * 11, d->dialog_body[i], UI_BLACK);

    reclip(s);

    /* The bold label marks the *focused* button, not a fixed default:
     * with more than one choice the operator has to be able to see which
     * one Return will take, and move it. */
    int bx = x + w - 16;
    for (int i = d->dialog_button_count - 1; i >= 0; i--) {
        int bw = ui_button_width(d->dialog_buttons[i]);
        bx -= bw;
        ui_button(s, bx, y + h - UI_WIN_PAD - 18, bw, 18, d->dialog_buttons[i],
                  i == d->dialog_focus, false, true);
        bx -= 14;
    }
}

/* The arrow. Drawn last, over everything, with a white outline so it
 * stays visible on ink as well as on paper. */
static const char *const arrow_art[16] = {
    "#...............",
    "##..............",
    "#@#.............",
    "#@@#............",
    "#@@@#...........",
    "#@@@@#..........",
    "#@@@@@#.........",
    "#@@@@@@#........",
    "#@@@@@@@#.......",
    "#@@@@@#####.....",
    "#@@#@@#.........",
    "#@#.#@@#........",
    "##..#@@#........",
    "#....#@@#.......",
    ".....#@@#.......",
    "......##........",
};

void ui_desktop_draw_cursor(ui_surface_t *s, int x, int y) {
    reclip(s);
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++) {
            char ch = arrow_art[r][c];
            if (ch == '#') ui_pset(s, x + c, y + r, UI_BLACK);
            else if (ch == '@') ui_pset(s, x + c, y + r, UI_WHITE);
            else if (ch == '.') continue;
        }
}

static void compose(ui_surface_t *s, const ui_desktop_t *d,
                    const ui_menubar_t *mb,
                    int mouse_x, int mouse_y, bool mouse_visible);

void ui_desktop_draw(ui_surface_t *s, const ui_desktop_t *d,
                     const ui_menubar_t *mb,
                     int mouse_x, int mouse_y, bool mouse_visible) {
    reclip(s);
    compose(s, d, mb, mouse_x, mouse_y, mouse_visible);
}

void ui_desktop_draw_clipped(ui_surface_t *s, const ui_desktop_t *d,
                             const ui_menubar_t *mb,
                             int mouse_x, int mouse_y, bool mouse_visible,
                             int cx, int cy, int cw, int ch) {
    /* Every primitive honours the surface clip, and the composition
     * functions reset it as they go — so the clip is re-applied by
     * compose() itself rather than set once here. */
    ui_clip_region(cx, cy, cw, ch);
    compose(s, d, mb, mouse_x, mouse_y, mouse_visible);
    ui_clip_region(0, 0, 0, 0);
}

static void compose(ui_surface_t *s, const ui_desktop_t *d,
                    const ui_menubar_t *mb,
                    int mouse_x, int mouse_y, bool mouse_visible) {
    ui_icons_init();
    reclip(s);

    /* The desktop: flat. This is the single element that does most of
     * the work of making the windows read as objects rather than as
     * regions of a page, and it does that job by being quiet.
     *
     * A dither of two greys was tried and is worse: at 4 bpp the two
     * colours average into mud instead of reading as texture, which is
     * the opposite of the effect the 1-bit machines got for free. */
    ui_fill(s, 0, 0, s->w, s->h, UI_DESKTOP);

    draw_results_window(s, d);
    draw_info_window(s, d);
    draw_manual_strip(s, d);
    draw_dialog(s, d);
    draw_picker(s, d);

    ui_menubar_draw(s, mb);
    ui_menubar_draw_dropdown(s, mb);

    if (mouse_visible) ui_desktop_draw_cursor(s, mouse_x, mouse_y);
}
