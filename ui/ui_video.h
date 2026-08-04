/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_video.h — the video backend vtable.
 *
 * Four ways to get pixels off these boards, all driving the same eight
 * GPIOs, and each one claims core 1, DMA_IRQ_0 or both:
 *
 *   HSTX HDMI       RP2350 only, drivers/pico_hdmi
 *   PIO HDMI        RP2040 and RP2350, frank-msx drivers/HDMI.c
 *   PIO / HSTX VGA  frank-msx drivers/HDMI_vga.c or disphstx
 *   composite       frank-msx drivers/tv
 *
 * frank-msx picks one at compile time and they are mutually exclusive in
 * its CMakeLists. This firmware has to switch at runtime, so they go
 * behind a vtable — but see ui_video_switch() for what "switch" actually
 * means, because writing four correct teardown paths to save a reboot is
 * poor value and this does not pretend otherwise.
 */
#ifndef UI_VIDEO_H
#define UI_VIDEO_H

#include "ui_gfx.h"
#include "video_mode.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char        *name;
    frank_video_mode_t mode;

    /* Bring the output up against the shared framebuffer. Returns false
     * if this backend cannot run on this chip — the HSTX path on an
     * RP2040, say — which the caller must treat as "try the next one",
     * not as a fault. */
    bool (*init)(void);

    /* Only the backends that can genuinely be torn down implement this.
     * NULL means "this one is a one-way door", and ui_video_switch()
     * reboots instead. */
    void (*shutdown)(void);

    /* Called after a frame is composed. May be a no-op: a scanline
     * renderer reading the framebuffer directly needs no present step,
     * and saying so beats a stub that copies 76 KB for nothing. */
    void (*present)(void);

    /* Frames emitted since boot. The one thing a backend can say that
     * proves the whole chain from framebuffer to pixel clock is live —
     * and the only evidence "Video output" has, since no FRANK board
     * wires HPD and none of them can tell whether a monitor is
     * attached. In the vtable rather than a global because the test
     * would otherwise have to know which backend is running and read a
     * different symbol for each. */
    uint32_t (*frames)(void);
} ui_video_backend_t;

/* The back buffer — where composition happens. 153,600 bytes, and there
 * are two of them.
 *
 * A single buffer means the scanline renderer reads pixels while they are
 * being rewritten, so any redraw large enough to outlast a frame tears
 * visibly. Composing a full desktop takes several frames' worth of time,
 * and moving a mouse forces one on every report — which is what the
 * flicker was.
 *
 * 307,200 bytes of an RP2350's 520 KB is a lot to spend on this. It buys
 * the difference between an interface that looks finished and one that
 * does not, on a rig whose whole job is to be believed. */
ui_surface_t *ui_video_surface(void);

/* Bring up the best available backend for `mode`. Returns the mode
 * actually opened, which may differ: asking for VGA on a board wired
 * only for HDMI gets HDMI and says so. VIDEO_NONE means nothing came up.
 */
frank_video_mode_t ui_video_open(frank_video_mode_t mode);

/* Publish the back buffer: hand it to the scanout at the next vertical
 * blank, wait for the swap, then bring the new back buffer up to date so
 * partial repaints remain valid. */
void ui_video_present(void);

const ui_video_backend_t *ui_video_current(void);

/* Is there a backend compiled in that can drive this mode?
 *
 * Distinct from "does the board have the connector", which is what the
 * capability bits answer. A board can be perfectly capable of VGA while
 * this firmware has no code to generate it, and offering the menu item
 * anyway is how VGA came to look broken rather than absent. */
bool ui_video_mode_implemented(frank_video_mode_t mode);

/* The framebuffer the active backend is NOT scanning out of.
 *
 * For HDMI and VGA there is no such thing — both buffers are in the
 * double-buffer rotation. The composite backend is different: its driver
 * owns a 320x240 framebuffer of its own and never reads ours, so one of
 * the two 153,600-byte buffers is doing nothing and it borrows the space
 * rather than asking for 76,800 more that will not fit. Valid only for a
 * backend that never calls ui_video_swap_buffers(). */
uint8_t *ui_video_spare_bits(void);

/* Does the active backend scan directly out of the front buffer?
 *
 * False for composite, and the cursor overlay has to know: it patches
 * the front buffer in place, which under composite is the borrowed
 * region above. */
bool ui_video_scans_front_buffer(void);

/* Change mode at runtime.
 *
 * Returns true if the change is live. Returns false when the caller must
 * persist the choice and reboot — which is the normal answer for a
 * cross-backend change, and is deliberate: the boot path is the only
 * code path that brings a video backend up which has ever been tested,
 * so reusing it beats a teardown path that has not been.
 */
bool ui_video_switch(frank_video_mode_t mode);

/* The palette lives in ui_palette.h, included by both the backends and
 * ui/hostpreview, so there is exactly one definition of it. */

#endif /* UI_VIDEO_H */
