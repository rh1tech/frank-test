/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * oc_request.h — clocks and voltage for the next boot, and only the next.
 *
 * Overclocking is the one setting where persistence is actively
 * dangerous. A stored 504 MHz that a board cannot hold means it fails
 * during start-up, before anything can offer to undo it — and the fix
 * would be a debug probe and an erase, on a rig whose whole point is not
 * needing one.
 *
 * So the request rides a watchdog scratch register: it survives the
 * reboot that applies it, is consumed there, and a power cycle takes the
 * board back to the values it was built with. Anything that goes wrong
 * is undone by pulling the plug, which is the one recovery every
 * operator already knows.
 *
 * Same mechanism as the video request, for the same reason, and the two
 * use different scratch registers so a mode switch does not lose an
 * overclock or the other way round.
 */
#ifndef OC_REQUEST_H
#define OC_REQUEST_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t cpu_mhz;      /* system clock                              */
    uint8_t  vreg_sel;     /* the vreg_voltage enum, not millivolts     */
    uint8_t  psram_mhz;    /* ceiling the QMI divider is derived from   */
    uint8_t  flash_mhz;    /* likewise, for the flash side              */
} oc_request_t;

/* Ask the next boot for these. Survives one reboot, nothing more. */
void oc_request_set(const oc_request_t *r);

/* The pending request. Clears it, so it applies once. */
bool oc_request_take(oc_request_t *out);

/* What the running firmware is actually using, for the dialog to show
 * and to start editing from. */
void oc_request_current(oc_request_t *out);

/* main() tells this what it applied, so the dialog opens on the values
 * in force rather than on the built-in defaults. */
void oc_request_note_applied(const oc_request_t *r);

#endif /* OC_REQUEST_H */
