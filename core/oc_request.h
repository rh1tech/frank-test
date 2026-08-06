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

/* The only three system clocks this hardware can show a picture at.
 *
 * Not a taste for round numbers: the HSTX TMDS path needs clk_hstx at
 * exactly 126 MHz, and clk_hstx is clk_sys divided by an integer. So
 * clk_sys has to be a multiple of 126 — 252, 378, 504 — and every other
 * frequency produces a fractional divide, a jittery TMDS clock and a
 * monitor that will not lock. The picture is the instrument here, so a
 * clock that blanks it is not a clock this firmware can offer.
 *
 * This list used to run 150, 200, 252, 264, 300, 308, 336, ... and it
 * was wrong in a way that hid itself: the soak passes perfectly well at
 * 336 because it never looks at the display, so the auto-test happily
 * reported clocks whose only symptom is a dark screen.
 *
 * Confirmed against the two firmwares that run these boards in anger —
 * murmgenesis declares "CPU clock in MHz: 252, 378, 504" outright, and
 * frank-msx defaults to 252, or 378 for composite because its TV
 * scanline dispatcher will not keep up at 252.
 *
 * VGA and composite are each fixed at a single clock rather than being
 * a range: DispHSTX reconfigures clk_sys to 252 itself, and the
 * composite encoder needs 378. Neither leaves anything to sweep, which
 * is why the auto-test only offers itself on HDMI. */
extern const uint16_t oc_cpu_ladder[];
extern const unsigned oc_cpu_ladder_len;

/* The core voltage the fleet pairs with a given clock.
 *
 * Taken from frank-msx and murmgenesis, which use the same three steps
 * for the same silicon: 1.50 V up to 300, 1.60 V to just under 504, and
 * 1.65 V at 504. The RP2350 regulator has no 1.55 V step, which is why
 * the ladder looks coarse.
 *
 * Returning the fleet's number rather than asking the operator is the
 * point. Clock and voltage are not independent here — every one of
 * these clocks has a voltage it is known to run at, and making someone
 * pair them by hand invites the combination nobody has ever booted. */
uint8_t oc_vreg_for_clock(uint16_t mhz);

/* Index of the highest ladder entry at or below `mhz`. Never fails: an
 * absurd input lands on one end or the other. */
unsigned oc_ladder_index(uint16_t mhz);

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
