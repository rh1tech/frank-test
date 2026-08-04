/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_window.h — window frames and controls.
 *
 * The visual language is deliberate and consistent: one-pixel black
 * frames, a hairline-striped drag region, a hard offset shadow, and
 * selection by inversion. Nothing is anti-aliased and nothing has a
 * gradient, because at 2 bpp the honest version of a bevel is a dither
 * pattern and the honest version of a highlight is an inverted rect.
 */
#ifndef UI_WINDOW_H
#define UI_WINDOW_H

#include "ui_gfx.h"

#define UI_TITLE_H     14   /* drag region height                     */
#define UI_SHADOW      2    /* offset of the hard drop shadow         */
#define UI_WIN_PAD     6    /* content inset from the frame           */

typedef struct {
    int         x, y, w, h;     /* outer frame, excluding the shadow */
    const char *title;
    bool        active;         /* stripes and a close box, or not   */
    bool        closable;
    bool        shadow;
} ui_window_t;

/* Draw the frame and clear the content area to paper. Afterwards the
 * surface is clipped to the content rectangle, so a caller can draw
 * without bounds-checking anything; call ui_clip_reset() when done. */
void ui_window_draw(ui_surface_t *s, const ui_window_t *w);

/* The content rectangle in screen coordinates. */
void ui_window_content(const ui_window_t *w, int *x, int *y, int *cw, int *ch);

/* ------------------------------------------------------------------ */
/* Controls                                                            */
/* ------------------------------------------------------------------ */

/* A push button. The default button gets the second, heavier outline —
 * the one the Return key activates. */
void ui_button(ui_surface_t *s, int x, int y, int w, int h,
               const char *label, bool is_default, bool pressed,
               bool enabled);

/* Returns the width a button needs for this label, respecting the
 * classic minimum so a row of one-word buttons still lines up. */
int  ui_button_width(const char *label);

void ui_checkbox(ui_surface_t *s, int x, int y, const char *label,
                 bool checked, bool enabled);
void ui_radio(ui_surface_t *s, int x, int y, const char *label,
              bool selected, bool enabled);

/* The thermometer. `frac` is 0..1000 rather than a float — there is no
 * FPU worth using here and a permille is finer than a pixel at any width
 * this interface uses. */
void ui_progress(ui_surface_t *s, int x, int y, int w, int h, int frac);

/* Vertical scroll bar with arrows and a proportional thumb. */
void ui_scrollbar(ui_surface_t *s, int x, int y, int h,
                  int total, int visible, int first);

/* A horizontal rule, the divider used inside dialogs and menus. */
void ui_separator(ui_surface_t *s, int x, int y, int w);

/* Grow box in the bottom-right corner, purely decorative here: nothing
 * in this firmware resizes, but its absence is more noticeable than its
 * presence. */
void ui_grow_box(ui_surface_t *s, int x, int y);

#endif /* UI_WINDOW_H */
