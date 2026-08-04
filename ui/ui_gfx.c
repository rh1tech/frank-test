/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_gfx.c — 4 bpp primitives.
 *
 * Deliberately free of any SDK dependency so the whole interface can be
 * compiled for the host and rendered to a PNG. Designing a pixel
 * interface by flashing a board and squinting at a monitor is slow
 * enough to stop you iterating, which shows in the result.
 */

#include "ui_gfx.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Surface                                                             */
/* ------------------------------------------------------------------ */

void ui_surface_init(ui_surface_t *s, uint8_t *bits, int w, int h) {
    s->bits   = bits;
    s->w      = w;
    s->h      = h;
    s->stride = (w + 1) / 2;
    ui_clip_reset(s);
}

void ui_clip_set(ui_surface_t *s, int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    s->clip_x = x; s->clip_y = y; s->clip_w = w; s->clip_h = h;
}

/* The repaint region, if one is in force. See ui_clip_region(). */
static int  g_rgn_x, g_rgn_y, g_rgn_w, g_rgn_h;

void ui_clip_region(int x, int y, int w, int h) {
    g_rgn_x = x; g_rgn_y = y; g_rgn_w = w; g_rgn_h = h;
}

void ui_clip_reset(ui_surface_t *s) {
    if (g_rgn_w > 0 && g_rgn_h > 0) {
        ui_clip_set(s, g_rgn_x, g_rgn_y, g_rgn_w, g_rgn_h);
        return;
    }
    s->clip_x = 0; s->clip_y = 0; s->clip_w = s->w; s->clip_h = s->h;
}

static inline bool in_clip(const ui_surface_t *s, int x, int y) {
    return x >= s->clip_x && x < s->clip_x + s->clip_w &&
           y >= s->clip_y && y < s->clip_y + s->clip_h;
}

/* ------------------------------------------------------------------ */
/* Pixels                                                              */
/* ------------------------------------------------------------------ */

/* Leftmost pixel in the high nibble. */
static inline int shift_for(int x) { return (x & 1) ? 0 : 4; }

void ui_pset(ui_surface_t *s, int x, int y, uint8_t c) {
    if (!in_clip(s, x, y)) return;
    uint8_t *p  = &s->bits[y * s->stride + (x >> 1)];
    int      sh = shift_for(x);
    *p = (uint8_t)((*p & ~(0x0Fu << sh)) | ((c & 0x0Fu) << sh));
}

uint8_t ui_pget(const ui_surface_t *s, int x, int y) {
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return UI_PAPER;
    return (uint8_t)((s->bits[y * s->stride + (x >> 1)] >> shift_for(x)) & 0x0Fu);
}

/* ------------------------------------------------------------------ */
/* Lines and rectangles                                                */
/* ------------------------------------------------------------------ */

void ui_hline(ui_surface_t *s, int x, int y, int w, uint8_t c) {
    for (int i = 0; i < w; i++) ui_pset(s, x + i, y, c);
}

void ui_vline(ui_surface_t *s, int x, int y, int h, uint8_t c) {
    for (int i = 0; i < h; i++) ui_pset(s, x, y + i, c);
}

void ui_fill(ui_surface_t *s, int x, int y, int w, int h, uint8_t c) {
    /* Clip first, then fill whole bytes where we can. A full-screen
     * clear happens on every frame of a drag, so the inner loop is worth
     * keeping off the per-pixel path. */
    int x0 = x < s->clip_x ? s->clip_x : x;
    int y0 = y < s->clip_y ? s->clip_y : y;
    int x1 = x + w, y1 = y + h;
    if (x1 > s->clip_x + s->clip_w) x1 = s->clip_x + s->clip_w;
    if (y1 > s->clip_y + s->clip_h) y1 = s->clip_y + s->clip_h;
    if (x0 >= x1 || y0 >= y1) return;

    const uint8_t pair = (uint8_t)((c & 0x0Fu) * 0x11u);  /* c in both nibbles */

    for (int yy = y0; yy < y1; yy++) {
        uint8_t *row = &s->bits[yy * s->stride];
        int xx = x0;

        if (xx < x1 && (xx & 1)) { ui_pset(s, xx, yy, c); xx++; }
        int bytes = (x1 - xx) >> 1;
        if (bytes > 0) { memset(&row[xx >> 1], pair, (size_t)bytes); xx += bytes * 2; }
        while (xx < x1) { ui_pset(s, xx, yy, c); xx++; }
    }
}

void ui_frame(ui_surface_t *s, int x, int y, int w, int h, uint8_t c) {
    if (w <= 0 || h <= 0) return;
    ui_hline(s, x, y, w, c);
    ui_hline(s, x, y + h - 1, w, c);
    ui_vline(s, x, y, h, c);
    ui_vline(s, x + w - 1, y, h, c);
}

void ui_fill_pattern(ui_surface_t *s, int x, int y, int w, int h,
                     const ui_pattern_t *p, uint8_t fg, uint8_t bg) {
    for (int yy = 0; yy < h; yy++) {
        int      sy  = y + yy;
        /* Aligned to the surface, not the rectangle: a pattern that
         * moves with the window it fills reads as texture sliding under
         * a hole, which is wrong and very visible when dragging. */
        uint8_t  row = p->row[sy & 7];
        for (int xx = 0; xx < w; xx++) {
            int sx = x + xx;
            ui_pset(s, sx, sy, (row & (0x80u >> (sx & 7))) ? fg : bg);
        }
    }
}

void ui_invert(ui_surface_t *s, int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            if (!in_clip(s, xx, yy)) continue;
            uint8_t c = ui_pget(s, xx, yy);
            /* Only paper and ink swap. Leaving every other colour alone
             * means a red FAIL stays red inside a highlighted row, which
             * is the behaviour you want the one time it matters. */
            if (c == UI_PAPER || c == UI_WHITE) ui_pset(s, xx, yy, UI_BLACK);
            else if (c == UI_BLACK)             ui_pset(s, xx, yy, UI_PAPER);
        }
    }
}

/* The corner nick: one pixel off each corner. Cheaper than a real
 * rounded rect and exactly what the original controls looked like. */
void ui_round_frame(ui_surface_t *s, int x, int y, int w, int h, uint8_t c) {
    if (w < 4 || h < 4) { ui_frame(s, x, y, w, h, c); return; }
    ui_hline(s, x + 1, y,         w - 2, c);
    ui_hline(s, x + 1, y + h - 1, w - 2, c);
    ui_vline(s, x,         y + 1, h - 2, c);
    ui_vline(s, x + w - 1, y + 1, h - 2, c);
}

void ui_round_fill(ui_surface_t *s, int x, int y, int w, int h, uint8_t c) {
    if (w < 4 || h < 4) { ui_fill(s, x, y, w, h, c); return; }
    ui_fill(s, x + 1, y,         w - 2, 1,     c);
    ui_fill(s, x,     y + 1,     w,     h - 2, c);
    ui_fill(s, x + 1, y + h - 1, w - 2, 1,     c);
}

/* ------------------------------------------------------------------ */
/* Patterns                                                            */
/* ------------------------------------------------------------------ */

const ui_pattern_t UI_PAT_WHITE  = {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}};
const ui_pattern_t UI_PAT_BLACK  = {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};
const ui_pattern_t UI_PAT_GREY50 = {{0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55}};
const ui_pattern_t UI_PAT_GREY25 = {{0x88,0x22,0x88,0x22,0x88,0x22,0x88,0x22}};
const ui_pattern_t UI_PAT_GREY75 = {{0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77}};
/* Alternating full rows: the drag region of an active window. */
const ui_pattern_t UI_PAT_TITLEBAR = {{0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00}};

/* ------------------------------------------------------------------ */
/* Bitmaps                                                             */
/* ------------------------------------------------------------------ */

static inline bool bit_at(const uint8_t *p, int stride, int x, int y) {
    return (p[y * stride + (x >> 3)] & (0x80u >> (x & 7))) != 0;
}

void ui_blit(ui_surface_t *s, const ui_bitmap_t *b, int x, int y) {
    for (int yy = 0; yy < b->h; yy++) {
        for (int xx = 0; xx < b->w; xx++) {
            if (b->mask && !bit_at(b->mask, b->stride, xx, yy)) continue;
            ui_pset(s, x + xx, y + yy,
                    bit_at(b->data, b->stride, xx, yy) ? UI_BLACK : UI_PAPER);
        }
    }
}

void ui_blit_tinted(ui_surface_t *s, const ui_bitmap_t *b, int x, int y,
                    uint8_t c) {
    for (int yy = 0; yy < b->h; yy++)
        for (int xx = 0; xx < b->w; xx++)
            if (bit_at(b->data, b->stride, xx, yy))
                ui_pset(s, x + xx, y + yy, c);
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

extern const uint8_t ui_font_6x8[95][8];

static const uint8_t *glyph(char ch) {
    unsigned char u = (unsigned char)ch;
    if (u < 32 || u > 126) u = '?';
    return ui_font_6x8[u - 32];
}

/* The font stores each row left-justified in the top 6 bits. */
static void draw_glyph(ui_surface_t *s, int x, int y, char ch, uint8_t c) {
    const uint8_t *g = glyph(ch);
    for (int row = 0; row < UI_CHAR_H; row++)
        for (int col = 0; col < UI_CHAR_W; col++)
            if (g[row] & (0x80u >> col)) ui_pset(s, x + col, y + row, c);
}

int ui_text_n(ui_surface_t *s, int x, int y, const char *str, int n, uint8_t c) {
    int cx = x;
    for (int i = 0; i < n && str[i]; i++) {
        draw_glyph(s, cx, y, str[i], c);
        cx += UI_CHAR_W;
    }
    return cx;
}

int ui_text(ui_surface_t *s, int x, int y, const char *str, uint8_t c) {
    int cx = x;
    for (const char *p = str; *p; p++) { draw_glyph(s, cx, y, *p, c); cx += UI_CHAR_W; }
    return cx;
}

int ui_text_bold(ui_surface_t *s, int x, int y, const char *str, uint8_t c) {
    /* Smear one pixel right, as QuickDraw did. Drawing twice offset by
     * one is not the same as a real bold face, but it is what these
     * screens looked like and it costs one extra pass. */
    ui_text(s, x, y, str, c);
    return ui_text(s, x + 1, y, str, c);
}

int ui_text_big(ui_surface_t *s, int x, int y, const char *str, uint8_t c) {
    int cx = x;
    for (const char *p = str; *p; p++) {
        const uint8_t *g = glyph(*p);
        for (int row = 0; row < UI_CHAR_H; row++)
            for (int col = 0; col < UI_CHAR_W; col++)
                if (g[row] & (0x80u >> col)) {
                    ui_pset(s, cx + col * 2,     y + row * 2,     c);
                    ui_pset(s, cx + col * 2 + 1, y + row * 2,     c);
                    ui_pset(s, cx + col * 2,     y + row * 2 + 1, c);
                    ui_pset(s, cx + col * 2 + 1, y + row * 2 + 1, c);
                }
        cx += UI_CHAR_W * 2;
    }
    return cx;
}

int ui_text_width(const char *str) {
    int n = 0;
    while (str[n]) n++;
    return n * UI_CHAR_W;
}

int ui_text_centred(ui_surface_t *s, int x, int y, int w, const char *str,
                    uint8_t c) {
    int tx = x + (w - ui_text_width(str)) / 2;
    ui_text(s, tx, y, str, c);
    return tx;
}

/* ------------------------------------------------------------------ */
/* Bevels                                                              */
/* ------------------------------------------------------------------ */

/* Light source is top-left, always, everywhere. Consistency is the whole
 * trick: the moment two controls disagree about where the light comes
 * from, both stop reading as physical. */
static void bevel(ui_surface_t *s, int x, int y, int w, int h,
                  uint8_t tl, uint8_t br) {
    if (w < 2 || h < 2) return;
    ui_hline(s, x, y, w - 1, tl);
    ui_vline(s, x, y, h - 1, tl);
    ui_hline(s, x + 1, y + h - 1, w - 1, br);
    ui_vline(s, x + w - 1, y + 1, h - 1, br);
}

void ui_bevel_out(ui_surface_t *s, int x, int y, int w, int h) {
    bevel(s, x, y, w, h, UI_WHITE, UI_GREY_4);
}

void ui_bevel_in(ui_surface_t *s, int x, int y, int w, int h) {
    bevel(s, x, y, w, h, UI_GREY_4, UI_WHITE);
}

void ui_plate(ui_surface_t *s, int x, int y, int w, int h, uint8_t face) {
    ui_fill(s, x, y, w, h, face);
    ui_frame(s, x, y, w, h, UI_BLACK);
    ui_bevel_out(s, x + 1, y + 1, w - 2, h - 2);
}

/* ------------------------------------------------------------------ */
/* Icons with a shadow                                                 */
/* ------------------------------------------------------------------ */

void ui_blit_icon(ui_surface_t *s, const ui_bitmap_t *b, int x, int y,
                  uint8_t c) {
    /* Shadow first, one pixel down-right, in a mid grey. Without it
     * 1-bit artwork sits flat on a coloured field and looks pasted on. */
    ui_blit_tinted(s, b, x + 1, y + 1, UI_GREY_3);
    ui_blit_tinted(s, b, x, y, c);
}

/* ------------------------------------------------------------------ */
/* Embossed text                                                       */
/* ------------------------------------------------------------------ */

int ui_text_embossed(ui_surface_t *s, int x, int y, const char *str,
                     uint8_t c) {
    ui_text(s, x, y + 1, str, UI_WHITE);
    return ui_text(s, x, y, str, c);
}
