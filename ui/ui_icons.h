/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_icons.h — 16x16 monochrome icons, one per subsystem.
 *
 * Authored as ASCII art in ui_icons.c and packed into bitmaps at
 * startup. That costs about 3 KB of flash for the art and 400 bytes of
 * RAM for the packed form, and buys the ability to see and edit an icon
 * in a diff. Hex tables for artwork are write-only.
 */
#ifndef UI_ICONS_H
#define UI_ICONS_H

#include "ui_gfx.h"

typedef enum {
    ICON_CHIP = 0,      /* silicon / CPU        */
    ICON_FLASH,         /* QSPI flash           */
    ICON_RAM,           /* PSRAM                */
    ICON_DISK,          /* microSD              */
    ICON_DISPLAY,       /* video                */
    ICON_SPEAKER,       /* audio                */
    ICON_KEYBOARD,      /* PS/2 + USB keyboard  */
    ICON_MOUSE,         /* PS/2 + USB mouse     */
    ICON_GAMEPAD,
    ICON_CASSETTE,      /* tape in              */
    ICON_CLOCK,         /* RTC                  */
    ICON_USB,
    ICON_LINK,          /* inter-processor link */
    ICON_CHIP_SMALL,    /* companion processor  */
    ICON_TICK,          /* pass                 */
    ICON_CROSS,         /* fail                 */
    ICON_DASH,          /* not applicable       */
    ICON_QUERY,         /* not run / unknown    */
    ICON_FRANK,         /* the mark, for the menu bar and About */
    ICON_COUNT
} ui_icon_id_t;

/* Unpack the ASCII art once. Safe to call more than once. */
void ui_icons_init(void);

const ui_bitmap_t *ui_icon(ui_icon_id_t id);

/* Number of art rows that are not exactly UI_ICON_W characters long.
 * Zero is the only acceptable answer; ui/hostpreview fails its build on
 * anything else. */
int ui_icons_validate(void);

#define UI_ICON_W 16
#define UI_ICON_H 16

#endif /* UI_ICONS_H */
