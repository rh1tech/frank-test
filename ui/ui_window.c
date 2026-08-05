/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui_window.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Windows                                                             */
/* ------------------------------------------------------------------ */

void ui_window_content(const ui_window_t *w, int *x, int *y, int *cw, int *ch) {
    *x  = w->x + 1 + UI_WIN_PAD;
    *y  = w->y + UI_TITLE_H + UI_WIN_PAD;
    *cw = w->w - 2 - UI_WIN_PAD * 2;
    *ch = w->h - UI_TITLE_H - UI_WIN_PAD * 2 - 1;
}

void ui_window_draw(ui_surface_t *s, const ui_window_t *w) {
    ui_clip_reset(s);

    /* Hard offset shadow, drawn first so the frame lands on top of it.
     * Two pixels and no blur: the shadow exists to lift the window off
     * the patterned desktop, and a soft one at 2 bpp is a dither cloud. */
    if (w->shadow) {
        ui_fill(s, w->x + UI_SHADOW, w->y + w->h,
                w->w, UI_SHADOW, UI_BLACK);
        ui_fill(s, w->x + w->w, w->y + UI_SHADOW,
                UI_SHADOW, w->h, UI_BLACK);
    }

    /* Body and frame. Content area is paper; the chrome around it is
     * the grey face, which is what separates "where the information is"
     * from "where the controls are" without a single label. */
    ui_fill(s, w->x, w->y, w->w, w->h, UI_GREY_1);
    ui_frame(s, w->x, w->y, w->w, w->h, UI_BLACK);
    ui_bevel_out(s, w->x + 1, w->y + 1, w->w - 2, w->h - 2);

    /* ---- drag region ---- */
    const int tx = w->x + 1, ty = w->y + 1;
    const int tw = w->w - 2;

    if (w->active) {
        /* Hairlines, inset so they never touch the frame, with a gap
         * cleared behind the title and the close box. Alternating light
         * and mid grey rather than solid black: a filled title bar reads
         * as "selected" in this visual language, and every window would
         * look selected. */
        for (int i = 0; i < 6; i++) {
            ui_hline(s, tx + 1, ty + i * 2,     tw - 2, UI_WHITE);
            ui_hline(s, tx + 1, ty + i * 2 + 1, tw - 2, UI_GREY_3);
        }
    } else {
        ui_fill(s, tx, ty, tw, UI_TITLE_H - 1, UI_GREY_1);
    }
    ui_hline(s, w->x, w->y + UI_TITLE_H, w->w, UI_BLACK);

    if (w->title && *w->title) {
        int tw_px = ui_text_width(w->title);
        int cx    = w->x + (w->w - tw_px) / 2;
        ui_fill(s, cx - 6, ty, tw_px + 12, UI_TITLE_H - 1, UI_GREY_1);
        /* Embossed: a white shadow one pixel down. On a mid-tone face
         * that is the difference between legible and squinting. */
        ui_text_embossed(s, cx, w->y + 3, w->title,
                         w->active ? UI_BLACK : UI_GREY_5);
    }

    if (w->closable && w->active) {
        const int bx = tx + 5, by = ty + 2;
        ui_fill(s, bx - 3, ty, 14, UI_TITLE_H - 1, UI_GREY_1);
        ui_plate(s, bx, by, 10, 10, UI_GREY_1);
    }

    /* The content well: paper, recessed. */
    int cx, cy, cw, ch;
    ui_window_content(w, &cx, &cy, &cw, &ch);
    ui_fill(s, cx - 1, cy - 1, cw + 2, ch + 2, UI_PAPER);
    ui_bevel_in(s, cx - 2, cy - 2, cw + 4, ch + 4);

    /* Clip so callers cannot scribble on the chrome just drawn. */
    ui_clip_set(s, cx, cy, cw, ch);
}

/* ------------------------------------------------------------------ */
/* Buttons                                                             */
/* ------------------------------------------------------------------ */

#define BTN_MIN_W 58
#define BTN_H     18

int ui_button_width(const char *label) {
    int w = ui_text_width(label) + 24;
    return w < BTN_MIN_W ? BTN_MIN_W : w;
}

void ui_button(ui_surface_t *s, int x, int y, int w, int h,
               const char *label, bool is_default, bool pressed,
               bool enabled) {
    if (h <= 0) h = BTN_H;

    ui_round_fill(s, x, y, w, h, UI_GREY_1);
    ui_round_frame(s, x, y, w, h, UI_BLACK);
    ui_bevel_out(s, x + 1, y + 1, w - 2, h - 2);

    /* Disabled text is a real grey now, not a dither — which is both
     * more legible and less noisy than knocking out alternate pixels. */
    int ty = y + (h - UI_CHAR_H) / 2;
    int tx = x + (w - ui_text_width(label)) / 2;

    /* "Return does this" is a bolder label, not a ring.
     *
     * It used to be the classic double rounded frame sitting three
     * pixels clear of the button. At this scale that is a heavy black
     * box around a small control, and on a dialog whose only button is
     * the default — which is all of them here — it framed the one thing
     * that needed no emphasis. Weight carries the same meaning and
     * takes no space. */
    if (is_default && enabled)
        ui_text_bold(s, tx, ty, label, UI_BLACK);
    else
        ui_text_embossed(s, tx, ty, label, enabled ? UI_BLACK : UI_GREY_5);

    if (pressed) {
        /* Pressed: swap the bevel and nudge the label down-right, so the
         * control looks pushed in rather than merely recoloured. */
        ui_round_fill(s, x + 1, y + 1, w - 2, h - 2, UI_GREY_2);
        ui_bevel_in(s, x + 1, y + 1, w - 2, h - 2);
        ui_text(s, tx + 1, ty + 1, label, enabled ? UI_BLACK : UI_GREY_5);
    }
}

/* ------------------------------------------------------------------ */
/* Checkbox and radio                                                  */
/* ------------------------------------------------------------------ */

void ui_checkbox(ui_surface_t *s, int x, int y, const char *label,
                 bool checked, bool enabled) {
    const int box = 11;
    ui_fill(s, x, y, box, box, UI_WHITE);
    ui_frame(s, x, y, box, box, UI_BLACK);
    ui_bevel_in(s, x + 1, y + 1, box - 2, box - 2);

    if (checked) {
        /* An X, not a tick: the tick is reserved for test results and
         * two meanings for one glyph is one too many. */
        for (int i = 2; i < box - 2; i++) {
            ui_pset(s, x + i, y + i, UI_BLACK);
            ui_pset(s, x + box - 1 - i, y + i, UI_BLACK);
        }
    }
    ui_text_embossed(s, x + box + 5, y + 2, label,
                     enabled ? UI_BLACK : UI_GREY_5);
}

void ui_radio(ui_surface_t *s, int x, int y, const char *label,
              bool selected, bool enabled) {
    const int d = 11;
    /* A circle this small is a rounded square; drawing it as one keeps
     * it symmetric, which a midpoint circle at r=5 is not. */
    ui_round_fill(s, x, y, d, d, UI_WHITE);
    ui_round_frame(s, x, y, d, d, UI_BLACK);
    ui_bevel_in(s, x + 1, y + 1, d - 2, d - 2);
    if (selected) ui_fill(s, x + 3, y + 3, d - 6, d - 6, UI_BLACK);

    ui_text_embossed(s, x + d + 5, y + 2, label,
                     enabled ? UI_BLACK : UI_GREY_5);
}

/* ------------------------------------------------------------------ */
/* Progress and scrolling                                              */
/* ------------------------------------------------------------------ */

void ui_progress(ui_surface_t *s, int x, int y, int w, int h, int frac) {
    if (frac < 0) frac = 0;
    if (frac > 1000) frac = 1000;

    ui_fill(s, x, y, w, h, UI_WHITE);
    ui_frame(s, x, y, w, h, UI_BLACK);
    ui_bevel_in(s, x + 1, y + 1, w - 2, h - 2);

    int filled = ((w - 2) * frac) / 1000;
    /* Accent, with its own highlight row: a flat bar reads as a
     * rectangle, a lit one reads as a level. */
    ui_fill(s, x + 1, y + 1, filled, h - 2, UI_ACCENT);
    if (filled > 0 && h > 4)
        ui_hline(s, x + 1, y + 1, filled, UI_ACCENT_L);
}

/* The scroll bar, drawn quietly.
 *
 * It was a black-framed track with black-framed plates in it and solid
 * black arrows - the same treatment a button gets, which is right for a
 * control you press and much too loud for a strip that sits beside
 * twenty rows of text. Next to a list of measurements it was the
 * heaviest thing on screen and drew the eye away from the results.
 *
 * So: no outline around the track, one grey rule separating it from the
 * rows, a lightly recessed channel, and a thumb that is a plain raised
 * face rather than a framed plate. The arrows are grey and a pixel
 * smaller. Nothing here needs a hard edge to read as a control, because
 * its shape and position already say what it is.
 */
void ui_scrollbar(ui_surface_t *s, int x, int y, int h,
                  int total, int visible, int first) {
    const int w = 15;

    /* The channel, and a single rule where it meets the list. */
    ui_fill(s, x, y, w, h, UI_GREY_1);
    ui_vline(s, x, y, h, UI_GREY_3);

    /* Recessed, but in greys rather than against black: a shadow at the
     * top and left, a highlight at the bottom and right. */
    ui_hline(s, x + 1, y, w - 1, UI_GREY_3);
    ui_hline(s, x + 1, y + h - 1, w - 1, UI_WHITE);

    /* Arrow buttons: a face and a soft edge, no frame. */
    for (int e = 0; e < 2; e++) {
        const int by = e ? (y + h - w) : y;
        ui_fill(s, x + 1, by, w - 1, w, UI_GREY_1);
        ui_hline(s, x + 1, e ? by : by + w - 1, w - 1, UI_GREY_3);
    }

    /* The glyphs, in the same ink as a disabled label rather than in
     * black. Four rows, narrowing to a point. */
    for (int i = 0; i < 4; i++) {
        ui_hline(s, x + 7 - i, y + 5 + i, 1 + i * 2, UI_GREY_5);
        ui_hline(s, x + 7 - i, y + h - 6 - i, 1 + i * 2, UI_GREY_5);
    }

    if (total <= visible || total <= 0) return;

    int track = h - w * 2;
    int th    = (track * visible) / total;
    if (th < 16) th = 16;
    if (th > track) th = track;

    const int span = (total > visible) ? (total - visible) : 1;
    int ty = y + w + ((track - th) * first) / span;

    /* The thumb: a raised face with a highlight and a shadow, and no
     * outline. It is the only part that moves, which is enough to make
     * it the thing you reach for. */
    ui_fill(s, x + 3, ty, w - 5, th, UI_GREY_2);
    ui_hline(s, x + 3, ty, w - 5, UI_WHITE);
    ui_vline(s, x + 3, ty, th, UI_WHITE);
    ui_hline(s, x + 3, ty + th - 1, w - 5, UI_GREY_4);
    ui_vline(s, x + w - 3, ty, th, UI_GREY_4);
}

void ui_separator(ui_surface_t *s, int x, int y, int w) {
    /* Engraved: a dark line with a white one under it. Two solid rows
     * now that there are greys to do it with. */
    ui_hline(s, x, y,     w, UI_GREY_4);
    ui_hline(s, x, y + 1, w, UI_WHITE);
}

void ui_grow_box(ui_surface_t *s, int x, int y) {
    ui_plate(s, x, y, 15, 15, UI_GREY_1);
    ui_frame(s, x + 2, y + 2, 8, 8, UI_GREY_5);
    ui_fill(s, x + 5, y + 5, 7, 7, UI_WHITE);
    ui_frame(s, x + 5, y + 5, 7, 7, UI_BLACK);
}
