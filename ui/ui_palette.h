/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_palette.h — the sixteen colours, defined exactly once.
 *
 * The display driver installs these, and ui/hostpreview renders with
 * them. Previously each kept its own copy and a comment asking whoever
 * edited one to remember the other, which is not a mechanism. One
 * header, included by both, is.
 *
 * The greys are a Platinum-style ramp: a face colour with a white
 * highlight above it and two darker steps below, which is what a bevel
 * needs to read as an edge rather than as an outline. They are spaced
 * perceptually rather than linearly — the light end needs finer steps
 * than the dark end for the highlight to look like light rather than
 * like another line.
 */
#ifndef UI_PALETTE_H
#define UI_PALETTE_H

#include <stdint.h>

#include "ui_gfx.h"

static const uint32_t ui_palette_rgb888[UI_PALETTE_LEN] = {
    [UI_WHITE]    = 0xFFFFFFu,
    [UI_PAPER]    = 0xEEEEE9u,   /* warm off-white: pure white for a whole
                                  * window glares on a CRT               */
    [UI_GREY_1]   = 0xDDDDD8u,   /* raised control face                  */
    [UI_GREY_2]   = 0xC4C4BFu,
    [UI_GREY_3]   = 0xA8A8A3u,   /* icon drop shadow                     */
    [UI_GREY_4]   = 0x808079u,   /* bevel shadow                         */
    [UI_GREY_5]   = 0x55554Fu,   /* disabled text                        */
    [UI_BLACK]    = 0x16161Au,   /* near-black: 0/255 against pure white
                                  * rings on cheap panels                */

    [UI_ACCENT]   = 0x33559Eu,
    [UI_ACCENT_L] = 0xB9C8E4u,
    [UI_OK]       = 0x1E8A44u,
    [UI_OK_L]     = 0x9BD6AEu,
    [UI_FAIL]     = 0xC42B2Bu,
    [UI_FAIL_L]   = 0xEFB3B3u,
    [UI_WARN]     = 0xC98A1Au,
    [UI_DESKTOP]  = 0x64789Au,   /* muted blue, so windows read as objects
                                  * sitting on something                 */
};

#endif /* UI_PALETTE_H */
