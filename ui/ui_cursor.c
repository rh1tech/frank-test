/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_cursor.c — moving the pointer without redrawing anything.
 *
 *
 * THE PROBLEM
 *
 * Moving the mouse used to mean recomposing the whole screen and
 * presenting it: several milliseconds of drawing, a 153,600-byte
 * back-buffer sync, and a wait of up to one frame — 16.7 ms — for the
 * vertical blank to take the swap. That is fine at idle and hopeless
 * while audio is playing, because the I2S FIFO is eight samples deep,
 * which is 180 us at 44.1 kHz. Anything that goes away for longer than
 * that leaves the DAC holding its last level.
 *
 * So the audio dialog only repainted between notes, and the pointer
 * updated six times a second.
 *
 *
 * THE FIX
 *
 * A mouse cursor does not need any of that. It is 16x16 pixels of
 * unchanging artwork, and the only thing under it that matters is the
 * pixels it is about to cover. So: save that patch, draw the arrow, and
 * when it moves, put the patch back and do it again somewhere else.
 *
 * Written straight into the *front* buffer — the one the scanout is
 * reading — deliberately. That skips the compose, the sync and the
 * vblank wait entirely, and costs about 300 bytes of memcpy: a few
 * microseconds, comfortably inside the 180 us the FIFO gives us. The
 * price is that a moving cursor can tear, and at 16x16 with no interior
 * detail that is invisible. It is what a hardware sprite would do if
 * this chip had one.
 *
 *
 * THE ONE RULE
 *
 * The saved patch describes a particular buffer at a particular moment.
 * A present() swaps the buffers and syncs them, so everything this file
 * remembers becomes wrong. Call ui_cursor_overlay_reset() after every
 * present, then move() again to re-establish it — which is also why the
 * dialogs no longer draw the cursor as part of their composition: two
 * cursors, one composed and one overlaid, is exactly what happens if
 * they do.
 */

#include "ui_desktop.h"
#include "ui_gfx.h"

#include <stddef.h>
#include <string.h>

extern uint8_t *ui_video_front_bits(void);

#define CUR_W 16
#define CUR_H 16

/* Nine, not eight. Sixteen pixels is eight bytes at 4 bpp only when the
 * left edge is byte-aligned; at an odd x the run straddles one more. */
#define PATCH_BYTES 9

static uint8_t patch[CUR_H][PATCH_BYTES];
static int     px, py;
static bool    active;

static void patch_copy(uint8_t *fbp, int x, int y, bool save) {
    const int bx = (x >= 0) ? (x >> 1) : -(((-x) + 1) >> 1);

    for (int r = 0; r < CUR_H; r++) {
        const int sy = y + r;
        if (sy < 0 || sy >= UI_SCREEN_H) continue;

        uint8_t *row = fbp + (size_t)sy * UI_STRIDE;
        for (int b = 0; b < PATCH_BYTES; b++) {
            const int cb = bx + b;
            if (cb < 0 || cb >= UI_STRIDE) continue;
            if (save) patch[r][b] = row[cb];
            else      row[cb]     = patch[r][b];
        }
    }
}

void ui_cursor_overlay_reset(void) {
    active = false;
}

void ui_cursor_overlay_move(int x, int y) {
    /* Composite has no front buffer to patch: its driver scans out of
     * its own 320x240 framebuffer and ours holds nothing it reads, so
     * writing an arrow into it would corrupt the borrowed region — see
     * ui_video_spare_bits(). There the pointer moves at the dialog's
     * own repaint rate instead, which on a display that cannot render
     * readable text is not a loss worth code. */
    extern bool ui_video_scans_front_buffer(void);
    if (!ui_video_scans_front_buffer()) return;

    uint8_t *fbp = ui_video_front_bits();
    if (!fbp) return;

    if (active && x == px && y == py) return;

    if (active) patch_copy(fbp, px, py, false);
    patch_copy(fbp, x, y, true);
    px = x; py = y; active = true;

    ui_surface_t s;
    ui_surface_init(&s, fbp, UI_SCREEN_W, UI_SCREEN_H);
    ui_desktop_draw_cursor(&s, x, y);
}
