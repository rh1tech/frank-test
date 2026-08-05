/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlgs.h — the two modal windows that are not test results.
 *
 * Audio and gamepads have the same shape as each other and a different
 * shape from everything else in this firmware: they run until the
 * operator says stop, and the verdict is something a person reaches
 * rather than something the firmware measures. Neither belongs in a list
 * of rows with a pass or a fail against it.
 *
 * They take the desktop painter as a callback rather than reaching for
 * main.c's state, so that ui/ stays unaware of tests and app/ keeps one
 * definition of what the screen behind a dialog looks like.
 */
#ifndef DLGS_H
#define DLGS_H

#include "detect.h"
#include "registry.h"
#include "frank_audio.h"

/* Dialog margins.
 *
 * UI_WIN_PAD alone is six pixels, which is right for a list window whose
 * rows carry their own inset and much too tight for a dialog: text
 * landed hard against the frame on all three, and the button sat on the
 * bottom edge. These sit *inside* the content rectangle, so every dialog
 * breathes the same amount and none of them can drift.
 *
 *   INSET  left and right
 *   TOP    above the first line
 *   FOOT   between the last line and the button row
 *   BOT    below the button
 */
#define DLG_INSET 12
#define DLG_TOP    8
#define DLG_FOOT  16
#define DLG_BOT    8

typedef struct {
    const detect_result_t *detect;

    /* Compose the desktop into the back buffer. Must NOT present: the
     * dialog draws on top and presents once, so the operator never sees
     * a frame with the background but not the window. */
    void (*paint_background)(void);
} dlg_ctx_t;

/* Loop the melody through left, right and centre until Esc or Stop. */
void dlg_audio(const dlg_ctx_t *c, audio_src_t src);

/* Every keyboard shortcut, on screen. F1, or the mark menu. */
void dlg_help(const dlg_ctx_t *c);

/* A keyboard on screen, lighting keys while they are held. Both the USB
 * and PS/2 keyboards feed it. */
void dlg_keys(const dlg_ctx_t *c);

/* Live button display for both controller ports, until Esc or Stop. */
void dlg_nespad(const dlg_ctx_t *c);

/* Watch the tape input and draw what arrives, until Esc or Stop. */
void dlg_tape(const dlg_ctx_t *c);

/* The PS/2 ports, watched live. A port with nothing plugged into it is
 * silent and so is a broken one, so this shows the traffic and lets the
 * operator judge. */
void dlg_ps2(const dlg_ctx_t *c);

/* The indicator LEDs. Firmware can drive a pin and cannot see light, so
 * this drives them and leaves the verdict to whoever is looking. */
void dlg_led(const dlg_ctx_t *c);

/* The whole suite on repeat, counting failures per row. Catches the
 * class of fault a single cold pass never will. */
void dlg_burnin(const dlg_ctx_t *c, registry_results_t *r);

/* Clocks and voltage for the next boot, and only that one. Nothing is
 * stored: a clock the board cannot hold is undone by a power cycle. */
void dlg_overclock(const dlg_ctx_t *c);

#endif /* DLGS_H */
