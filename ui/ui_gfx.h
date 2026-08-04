/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_gfx.h — 4-bit-per-pixel drawing primitives.
 *
 *
 * WHY 4 BPP
 *
 * 640x480 at 4 bpp is 153,600 bytes: 30% of an RP2350's 520 KB, which
 * leaves 366 KB for everything else. That is comfortable.
 *
 * It was 2 bpp, sized to fit an RP2040's 264 KB. That constraint turned
 * out to be self-imposed: the fleet's three RP2040s are `hecate` and
 * `frank`'s RP2040-Zero — both PS/2-to-USB adapters with no video
 * hardware at all — plus `frank`'s Pico socket, which always takes a
 * Pico 2. Nothing that needs a framebuffer is an RP2040, so nothing was
 * being bought by the restriction except dithered greys.
 *
 * Sixteen entries is enough for the greys a bevel needs, an accent, and
 * genuine pass/fail/warn colours, without the 307,200 bytes an 8 bpp
 * screen would cost.
 *
 *
 * MEMORY LAYOUT
 *
 * Two pixels per byte, leftmost pixel in the HIGH nibble, so a row of
 * 640 pixels is 320 bytes. One source byte maps to exactly one output
 * word of two packed RGB565 pixels, which makes the scanline expander a
 * single 256-entry table lookup per byte — see ui_expand4bpp.h.
 */
#ifndef UI_GFX_H
#define UI_GFX_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Palette                                                             */
/* ------------------------------------------------------------------ */

/* Roles, not colours. A theme can move any of these without every call
 * site starting to lie about what it is drawing.
 *
 * The greys are ordered light to dark so bevel code can do arithmetic on
 * them, which is worth the small oddity of GREY_1 being the lightest. */
enum {
    UI_WHITE   = 0,    /* pure white — bevel highlights, field interiors */
    UI_PAPER   = 1,    /* warm off-white — window and menu backgrounds   */
    UI_GREY_1  = 2,    /* lightest grey — face of a raised control       */
    UI_GREY_2  = 3,
    UI_GREY_3  = 4,    /* mid grey — bevel shadow                       */
    UI_GREY_4  = 5,
    UI_GREY_5  = 6,    /* darkest grey — frame shadow, disabled text    */
    UI_BLACK   = 7,    /* ink, frames, text                             */

    UI_ACCENT  = 8,    /* selection                                     */
    UI_ACCENT_L= 9,    /* selection, light — selected-row backgrounds   */
    UI_OK      = 10,   /* pass                                          */
    UI_OK_L    = 11,
    UI_FAIL    = 12,   /* fail                                          */
    UI_FAIL_L  = 13,
    UI_WARN    = 14,   /* could not run / needs a manual step           */
    UI_DESKTOP = 15,   /* the desktop itself                            */
};

#define UI_PALETTE_LEN 16

#define UI_SCREEN_W 640
#define UI_SCREEN_H 480
#define UI_STRIDE   (UI_SCREEN_W / 2)          /* 320 bytes per row  */
#define UI_FB_BYTES (UI_STRIDE * UI_SCREEN_H)  /* 153,600            */

typedef struct {
    uint8_t *bits;
    int      w, h;
    int      stride;      /* bytes per row */
    /* Clip rectangle. Every primitive respects it, which is what lets a
     * window draw its contents without knowing it is partly off-screen
     * or behind something else. */
    int      clip_x, clip_y, clip_w, clip_h;
} ui_surface_t;

void ui_surface_init(ui_surface_t *s, uint8_t *bits, int w, int h);
void ui_clip_set(ui_surface_t *s, int x, int y, int w, int h);

/* "Back to everything" — where "everything" is the whole surface unless a
 * repaint region is in force, in which case it is that region.
 *
 * The distinction exists because window and menu drawing legitimately
 * reset the clip partway through (a window frame is drawn outside the
 * content clip it then sets), and a partial repaint has to survive that
 * without every one of those call sites knowing about it. */
void ui_clip_reset(ui_surface_t *s);

/* Constrain every subsequent ui_clip_reset() to this rectangle. Pass
 * w or h <= 0 to lift the restriction. */
void ui_clip_region(int x, int y, int w, int h);

/* ------------------------------------------------------------------ */
/* Patterns                                                            */
/* ------------------------------------------------------------------ */

/* An 8x8 tile, one bit per pixel, MSB leftmost. Where the bit is set the
 * `fg` colour is drawn, otherwise `bg`. Far less load-bearing than at
 * 2 bpp — greys are now real colours — but still the right tool for the
 * desktop texture and for knocking out disabled text. */
typedef struct { uint8_t row[8]; } ui_pattern_t;

extern const ui_pattern_t UI_PAT_WHITE;
extern const ui_pattern_t UI_PAT_BLACK;
extern const ui_pattern_t UI_PAT_GREY50;
extern const ui_pattern_t UI_PAT_GREY25;
extern const ui_pattern_t UI_PAT_GREY75;
extern const ui_pattern_t UI_PAT_TITLEBAR;

/* ------------------------------------------------------------------ */
/* Primitives                                                          */
/* ------------------------------------------------------------------ */

void ui_pset(ui_surface_t *s, int x, int y, uint8_t c);
uint8_t ui_pget(const ui_surface_t *s, int x, int y);

void ui_hline(ui_surface_t *s, int x, int y, int w, uint8_t c);
void ui_vline(ui_surface_t *s, int x, int y, int h, uint8_t c);
void ui_fill(ui_surface_t *s, int x, int y, int w, int h, uint8_t c);
void ui_frame(ui_surface_t *s, int x, int y, int w, int h, uint8_t c);

void ui_fill_pattern(ui_surface_t *s, int x, int y, int w, int h,
                     const ui_pattern_t *p, uint8_t fg, uint8_t bg);

/* Swap paper and ink inside the rectangle, leaving every other colour
 * alone — so a red FAIL stays red inside a selected row, which is the
 * one time it matters. Kept for menu highlighting; list selection now
 * uses a real accent colour instead. */
void ui_invert(ui_surface_t *s, int x, int y, int w, int h);

void ui_round_frame(ui_surface_t *s, int x, int y, int w, int h, uint8_t c);
void ui_round_fill(ui_surface_t *s, int x, int y, int w, int h, uint8_t c);

/* ------------------------------------------------------------------ */
/* Bevels                                                              */
/* ------------------------------------------------------------------ */

/* A raised or recessed edge: light on the top and left, dark on the
 * bottom and right, or the reverse. This is what 16 colours buys over
 * four, and it is most of why the interface now reads as having depth
 * without a single gradient or blur. */
void ui_bevel_out(ui_surface_t *s, int x, int y, int w, int h);
void ui_bevel_in(ui_surface_t *s, int x, int y, int w, int h);

/* Raised face with a black outline — the standard control body. */
void ui_plate(ui_surface_t *s, int x, int y, int w, int h, uint8_t face);

/* ------------------------------------------------------------------ */
/* Bitmaps                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint8_t *data;
    const uint8_t *mask;    /* may be NULL */
    int            w, h;
    int            stride;  /* bytes per row of the 1-bit source */
} ui_bitmap_t;

void ui_blit(ui_surface_t *s, const ui_bitmap_t *b, int x, int y);
void ui_blit_tinted(ui_surface_t *s, const ui_bitmap_t *b, int x, int y,
                    uint8_t c);

/* Icon with a one-pixel drop shadow in GREY_3, which is what stops the
 * 1-bit artwork looking pasted on. */
void ui_blit_icon(ui_surface_t *s, const ui_bitmap_t *b, int x, int y,
                  uint8_t c);

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

#define UI_CHAR_W 6
#define UI_CHAR_H 8

int  ui_text(ui_surface_t *s, int x, int y, const char *str, uint8_t c);
int  ui_text_n(ui_surface_t *s, int x, int y, const char *str, int n, uint8_t c);
int  ui_text_width(const char *str);
int  ui_text_big(ui_surface_t *s, int x, int y, const char *str, uint8_t c);
int  ui_text_bold(ui_surface_t *s, int x, int y, const char *str, uint8_t c);
int  ui_text_centred(ui_surface_t *s, int x, int y, int w, const char *str,
                     uint8_t c);

/* Text with a one-pixel white offset underneath — the engraved look used
 * for labels on grey. Cheap, and it is what makes small type legible
 * against a mid-tone face. */
int  ui_text_embossed(ui_surface_t *s, int x, int y, const char *str,
                      uint8_t c);

#endif /* UI_GFX_H */
