/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * settings.h — the persisted identity and preference record ("FRANKID").
 *
 * Two things need to survive a power cycle, and for the same reason:
 * neither can be worked out reliably from the hardware.
 *
 *   board    core2 and core2u share a pin map. Autodetect narrows to the
 *            pair and stops; whichever the operator confirms is stored
 *            here so the question is asked once per board rather than
 *            once per boot.
 *
 *   video    composite drives the same eight GPIOs as HDMI and VGA and
 *            presents the same 75R-to-ground signature as VGA, so it can
 *            never be autodetected. A forced choice has to stick, or the
 *            boot-time key becomes a permanent tax on every start.
 *
 * The record lives in its own flash sector, well clear of the
 * application, so reflashing the firmware does not erase the board's
 * identity — which is the whole point of writing it down.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include "frank_caps.h"
#include "video_mode.h"

#include <stdbool.h>
#include <stdint.h>

#define FRANKID_MAGIC    0x4B4E5246u   /* "FRNK", little-endian */
#define FRANKID_VERSION  1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t board;        /* frank_board_id_t */
    uint16_t role;         /* frank_role_t     */
    uint16_t video;        /* frank_video_mode_t */
    uint16_t board_rev;    /* PCB revision, operator-entered, 0 = unknown */

    /* Keep the UART console instead of the PS/2 mouse, on the boards
     * where they share GP0/GP1. Non-zero means keep it. This sits in
     * what was the spare word, so records written by earlier builds stay
     * valid and read as zero - which is the old behaviour, the mouse
     * winning. */
    uint16_t console;
    uint8_t  unit_serial[8];
    uint32_t crc32;        /* over everything above */
} frank_settings_t;

/* Read the record. Returns false when the sector is blank, the magic is
 * wrong, the version is newer than we understand, or the CRC fails — in
 * every one of those cases `out` is left at safe defaults rather than
 * partially filled, because a half-trusted identity is worse than none. */
bool settings_load(frank_settings_t *out);

/* Erase and rewrite the sector. Returns false if the read-back does not
 * match what was written; a settings write that silently failed would
 * make the next boot ask the same question again with no explanation.
 *
 * Must not run with the other core executing from flash. */
bool settings_save(const frank_settings_t *s);

/* Convenience wrappers that load, modify one field and save. */
bool settings_set_board(frank_board_id_t id, frank_role_t role);
/* Video is deliberately not stored - see core/video_request.h. An
 * autodetected mode that outlives its boot is a trap; a deliberate
 * one is cheap to repeat by holding H, V or C.
 *
 * The console preference is different and is stored: it is a deliberate
 * choice about how the operator works, not a guess about what is
 * plugged in, and having to hold a key every boot to keep a serial
 * session is exactly the friction that made it worth a menu item. */
bool settings_set_console(bool keep);


/* Wipe the record entirely — `board auto` / `video auto` at the console. */
bool settings_clear(void);

#endif /* SETTINGS_H */
