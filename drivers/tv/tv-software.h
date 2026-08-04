/*
 * Vendored from frank-msx (drivers/tv), itself from murmnes. Upstream
 * authorship and licence are unchanged; see the repository README for
 * the two local edits and why they were needed.
 *   https://github.com/rh1tech/frank-msx
 */

#pragma once

#include "inttypes.h"
#include "stdbool.h"

/* pio0 upstream, pio2 here.
 *
 * The only edit to this file. frank-msx and murmnes have pio0 free; in
 * this firmware it carries the inter-processor link, and pio1 carries
 * the I2S audio. The TV program is a single instruction and takes one
 * state machine, so it sits alongside the gamepad reader on pio2
 * without either noticing. */
#ifndef PIO_VIDEO
#define PIO_VIDEO pio2
#endif

#ifndef TV_BASE_PIN
#define TV_BASE_PIN (12)
#endif

#define TEXTMODE_COLS 40
#define TEXTMODE_ROWS 30
#define RGB888(r, g, b) ((r<<16) | (g << 8 ) | b )

typedef enum g_out_TV_t {
    g_TV_OUT_PAL,
    g_TV_OUT_NTSC
} g_out_TV_t;


typedef enum NUM_TV_LINES_t {
    _624_lines, _625_lines, _524_lines, _525_lines,
} NUM_TV_LINES_t;

typedef enum COLOR_FREQ_t {
    _3579545, _4433619
} COLOR_FREQ_t;

// TODO: Сделать настраиваемо
static const uint8_t textmode_palette[16] = {
    200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215
};
static void graphics_set_flashmode(bool flash_line, bool flash_frame) {
    // dummy
}

static void graphics_set_bgcolor(uint32_t color888) {
    // dummy
}
