/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * video_select.h — deciding which output to bring up.
 *
 * Precedence, highest first:
 *
 *   1. a key held during the boot window   H / V / C / A
 *   2. the sticky choice in the FRANKID record
 *   3. -DFRANK_VIDEO=<hdmi|vga|composite>
 *   4. autodetect (video_detect.c)
 *   5. HDMI
 *
 * The boot window exists because composite can never be autodetected and
 * oldskoolfrank's output is chosen by solder jumpers the firmware cannot
 * see. It is sticky because a window that has to be hit on every boot is
 * a tax on every boot; hitting it once per board is not.
 */
#ifndef VIDEO_SELECT_H
#define VIDEO_SELECT_H

#include "board_desc.h"
#include "video_detect.h"
#include "video_mode.h"

/* A place a keypress can come from. Returns the character, or -1 when
 * nothing is pending. Must not block.
 *
 * Registered rather than called directly because the sources differ per
 * board and per build: USB HID is only present in the HID configuration,
 * PS/2 only on the boards that have a connector, and neither should be a
 * link dependency of the core. */
typedef int (*video_key_source_fn)(void);

/* Say the UART pins are not a console on this board — on every board
 * with PS/2 they are the mouse. Call before the boot window. */
void video_select_no_console(void);

void video_select_add_source(video_key_source_fn fn, const char *name);

typedef enum {
    VIDEO_CHOICE_BOOT_KEY = 0,
    VIDEO_CHOICE_STICKY,
    VIDEO_CHOICE_COMPILED,
    VIDEO_CHOICE_AUTO,
    VIDEO_CHOICE_DEFAULT,
} video_choice_source_t;

typedef struct {
    frank_video_mode_t    mode;
    video_choice_source_t source;
    const char           *key_source_name;  /* which input answered */
    bool                  sticky_written;
} video_choice_t;

/* Run the boot window and decide.
 *
 * `window_ms` should be long enough for the slowest input a board has
 * that could realistically answer — USB HID enumeration is the limit at
 * roughly a second and a half. Sources that become ready late still
 * count, provided they answer before the window closes.
 *
 * A key pressed here is written to the FRANKID record, so the next boot
 * does not need the window at all. 'A' clears it back to auto.
 */
void video_select_boot_window(const frank_board_desc_t *board,
                              const video_detect_t *detected,
                              uint32_t window_ms,
                              video_choice_t *out);

/* Runtime change. Returns true if the caller should reboot to apply it.
 *
 * Switching inside one backend family is live; switching across families
 * is a persist-and-reboot, because HSTX HDMI, DispHSTX VGA and the
 * composite driver each claim core 1, DMA_IRQ_0 and the same eight pins,
 * and writing four correct teardown paths to save 200 ms of reboot is
 * poor value. See video_backend.h. */
bool video_select_set(frank_video_mode_t mode, bool persist);

const char *video_choice_source_name(video_choice_source_t s);

#endif /* VIDEO_SELECT_H */
