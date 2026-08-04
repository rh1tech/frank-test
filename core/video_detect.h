/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * video_detect.h — which display is plugged in.
 *
 * HPD (pin 19), DDC SCL/SDA (15/16) and CEC (13) are unconnected on
 * every FRANK board — verified across all ten boards that have an HDMI
 * connector; pin 17 goes to ground and the rest go nowhere. There is no
 * EDID to read and no hot-plug line to sense. Everything below therefore
 * works off the eight driver pins themselves.
 */
#ifndef VIDEO_DETECT_H
#define VIDEO_DETECT_H

#include "board_desc.h"
#include "video_mode.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* Detector (a): pull-ups on the six RGB pins. A VGA monitor's 75R to
     * ground drags them all down; an HDMI sink does not. */
    uint8_t rgb_pullup_mask;   /* bit n = pin video_base+n read high */
    bool    vga_monitor;

    /* Detector (b): are the clock-pair pins shorted? A passive
     * HDMI-to-VGA ribbon bridges them; a real HDMI cable does not. */
    int  clock_pair_code;      /* raw testPins() result, for the log */
    bool vga_adapter;

    /* Combined verdict. VIDEO_COMPOSITE is never produced here — see
     * the note in video_detect.c. */
    /* Whether each detector actually ran. Without these the report
     * prints a zeroed struct as though it were a measurement. */
    bool ran_rgb;
    bool ran_pair;

    frank_video_mode_t verdict;
    bool               any_sink;
} video_detect_t;

/* Probe. Must run before the video backend claims the pins, and leaves
 * them as plain inputs with no pull. Safe to re-run later provided the
 * backend is torn down first. */
void video_detect_run(const frank_board_desc_t *board, video_detect_t *out);

/* Port of frank-msx drivers/test_pins.c, unchanged in behaviour.
 * Exposed because the same routine drives the adjacent-pin short test. */
int video_test_pins(unsigned pin0, unsigned pin1);

void video_detect_report(const video_detect_t *d);

#endif /* VIDEO_DETECT_H */
