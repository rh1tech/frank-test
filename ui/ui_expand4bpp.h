/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_expand4bpp.h — 4 bpp framebuffer to packed RGB565 pairs.
 *
 * Header-only and SDK-free on purpose: this is the one piece of the
 * video path that runs inside the scanline interrupt, and it is also the
 * one piece whose bugs are silent. A transposed pixel pair does not
 * crash, it produces an image that looks almost right — fine vertical
 * detail combed, large shapes correct — which is exactly the kind of
 * fault that survives a casual look at a monitor.
 *
 * Keeping it here lets ui/hostpreview run *this* code, not a copy of it,
 * and compare the result against the framebuffer it came from.
 *
 *
 * PACKING
 *
 * The destination is drivers/pico_hdmi/video_output.c's
 *
 *     static uint16_t line_buffer[MODE_H_ACTIVE_PIXELS];
 *
 * handed to the callback as a uint32_t*. Two pixels per word, and on a
 * little-endian core the LOW half-word is line_buffer[2n] — the left
 * pixel. Read off the declaration, not assumed.
 *
 * The source is 4 bpp, two pixels per byte, leftmost in the high nibble.
 * That is a happy alignment: one source byte is exactly one output word,
 * so a 256-entry table turns the whole conversion into a single lookup
 * per byte with no shifts, no masking and no branches. A row is 320
 * lookups.
 *
 * The table costs 1 KB of RAM. At 2 bpp it was 64 bytes and needed two
 * lookups per byte; this is the better trade on a part with 520 KB.
 */
#ifndef UI_EXPAND4BPP_H
#define UI_EXPAND4BPP_H

#include <stdint.h>

/* Set to 1 if an image ever comes up with pixel pairs transposed. */
#ifndef UI_HSTX_SWAP_PAIR
#define UI_HSTX_SWAP_PAIR 0
#endif

static inline uint16_t ui_rgb888_to_rgb565(uint32_t c) {
    return (uint16_t)((((c >> 16) & 0xF8u) << 8) |
                      (((c >>  8) & 0xFCu) << 3) |
                      (( c        & 0xFFu) >> 3));
}

/* Build the byte table from a sixteen-entry RGB888 palette. */
static inline void ui_expand4bpp_build(uint32_t lut[256],
                                       const uint32_t palette_rgb888[16]) {
    uint16_t p[16];
    for (int i = 0; i < 16; i++) p[i] = ui_rgb888_to_rgb565(palette_rgb888[i]);

    for (int b = 0; b < 256; b++) {
        /* High nibble is the left pixel: the source stores its leftmost
         * pixel there, and the whole layout was chosen so this stays
         * true all the way to the wire. */
        uint16_t left  = p[(b >> 4) & 15];
        uint16_t right = p[b & 15];
#if UI_HSTX_SWAP_PAIR
        lut[b] = ((uint32_t)left << 16) | right;
#else
        lut[b] = ((uint32_t)right << 16) | left;
#endif
    }
}

/* One row: `stride` source bytes to `stride` destination words.
 *
 * Unrolled by four. The compiler does not unroll this on its own at -O2
 * and the difference is measurable inside a 25 us active line. */
static inline void ui_expand4bpp_row(uint32_t *out, const uint8_t *src,
                                     int stride, const uint32_t lut[256]) {
    for (int i = 0; i < stride; i += 4) {
        out[0] = lut[src[i]];
        out[1] = lut[src[i + 1]];
        out[2] = lut[src[i + 2]];
        out[3] = lut[src[i + 3]];
        out += 4;
    }
}

#endif /* UI_EXPAND4BPP_H */
