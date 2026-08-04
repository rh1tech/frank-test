/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * pinsig.c — passive pad classification.
 *
 * The question is "what is attached to this pin when nothing is driving
 * it": an external pull-up, a load to ground, or nothing at all. Three
 * outcomes, and the obvious two-phase test does not produce them
 * reliably on this silicon.
 *
 *
 * WHY NOT THE OBVIOUS TEST
 *
 * The textbook approach is: enable the internal pull-down and read, then
 * enable the internal pull-up and read. (1,1) means externally pulled up,
 * (0,0) externally pulled down, (0,1) floating.
 *
 * RP2350 A2 silicon has a documented anomaly on exactly that first half:
 * a pad configured as an input with the pull-down enabled can sit at an
 * intermediate voltage and read back high. So the pull-down phase can
 * report "externally pulled up" for a pin with nothing on it at all —
 * which is the single most damaging way for a board detector to be
 * wrong, because it invents hardware rather than missing it.
 *
 * frank-msx's DispHSTX video detector avoids this by using pull-ups only
 * (drivers/disphstx/disphstx_vmode.c, DispHstxAutoDispSel), and it is
 * proven on real boards. This function follows the same rule: the
 * internal pull-down is never enabled.
 *
 *
 * WHAT IS DONE INSTEAD
 *
 * Phase A — input, internal pull-up on, settle, read.
 *   Reads 0 only if something outside is pulling harder than the ~55 kOhm
 *   internal pull-up. That is a load to ground: PINSIG_LOW. Unambiguous,
 *   and it needs no pull-down.
 *
 * Phase B — separate an external pull-up from a floating pad, both of
 *   which read 1 in phase A. Discharge the pad, release it with no pull
 *   at all, and see whether anything pulls it back up:
 *
 *     external pull-up   10 kOhm into ~10 pF, tau = 100 ns -> high again
 *                        within a microsecond
 *     floating           only pad leakage, measured in nanoamps -> stays
 *                        low for milliseconds
 *
 *   Sampling 20 us after release separates those by three orders of
 *   magnitude, which is the kind of margin worth having in a test whose
 *   wrong answer is a wrong board.
 *
 * The passive version of phase B — charge the pad through the internal
 * pull-up, remove the pull, and watch it decay — does not work. A good
 * pad leaks so little that 10 pF holds its charge for far longer than a
 * boot-time probe can wait, so floating and pulled-up look identical.
 * Discharging actively is the only way to get a fast, decisive edge.
 *
 *
 * CONTENTION
 *
 * Phase B drives the pin low for a few microseconds. On a pin held high
 * by a resistor that is harmless. On a pin driven high by another chip —
 * an ESP-01S idling its UART TX, say — it is a brief fight.
 *
 * The pad's drive strength is dropped to 2 mA first, which bounds our
 * side of that fight to a few milliamps for a few microseconds. The
 * result is that a genuinely driven pin still classifies as HIGH (the
 * driver wins the recharge instantly, which is the correct answer) and
 * nothing is stressed.
 *
 *
 * THE PAD FUNCTION MUST BE PUT BACK
 *
 * gpio_init() routes a pad to SIO. On GP0 and GP1 that is the console
 * UART, and those two pins are exactly what separates frank_pga from
 * oldskoolfrank — so probing them silently disconnects the UART and
 * every subsequent printf goes nowhere.
 *
 * That is not a hypothetical: it is how this was found. The detection
 * log stopped mid-word at "[boot] detect" and the run looked like a
 * hang, on a board that was in fact finishing normally.
 *
 * So the pad's function is saved and restored around every probe. The
 * byte being transmitted at that instant is still corrupted — there is
 * no way to drive a pin low and not disturb what it was doing — but a
 * garbled character is a very different thing from a dead console.
 */

#include "pinsig.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <stdio.h>

/* Phase A settle. Long enough for the internal pull-up to charge any
 * plausible cable or connector capacitance through a series resistor —
 * oldskoolfrank's PS/2 lines sit behind 1K, and the VGA ladder behind up
 * to 1K, so this is not a bare-pad time constant. */
#define SETTLE_US        500

/* Phase B: discharge long enough to be sure, sample long enough after
 * release for a 10K pull-up to have won and short enough that leakage
 * on a floating pad is still irrelevant. */
#define DISCHARGE_US      10
#define RECHARGE_US       20

/* Majority of three. The pins being probed are unterminated stubs on a
 * board that may have a switching regulator a centimetre away; a single
 * sample is not worth a board identification. */
#define SAMPLES            3

bool pinsig_is_safe(unsigned pin) {
    /* Every bank-0 GPIO on these parts is ours. The QSPI pins are not in
     * bank 0 and cannot be reached through this API at all.
     *
     * (The RP2040 module carve-outs that used to live here — GP23 SMPS
     * mode, GP24 VBUS sense, GP29 VSYS divider — went with the RP2040
     * target. Restore them if a Pico-1 build ever comes back.) */
    return pin < NUM_BANK0_GPIOS;
}

static pinsig_t classify_once(unsigned pin) {
    /* ---- Phase A: is something holding this pin down? ---- */
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
    busy_wait_us_32(SETTLE_US);
    bool high_with_pullup = gpio_get(pin);

    if (!high_with_pullup) {
        gpio_disable_pulls(pin);
        return PINSIG_LOW;
    }

    /* ---- Phase B: pull-up, or nothing at all? ---- */
    gpio_disable_pulls(pin);
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
    busy_wait_us_32(DISCHARGE_US);

    gpio_set_dir(pin, GPIO_IN);          /* released, no pull */
    busy_wait_us_32(RECHARGE_US);
    bool recovered = gpio_get(pin);

    return recovered ? PINSIG_HIGH : PINSIG_FLOAT;
}

pinsig_t pinsig_classify(unsigned pin) {
    if (!pinsig_is_safe(pin)) return PINSIG_FLOAT;

    /* Drain anything queued before we disturb the pad, so a half-sent
     * line is not left dangling for the length of the probe. */
    stdio_flush();

    const gpio_function_t was = gpio_get_function(pin);

    unsigned votes[4] = { 0, 0, 0, 0 };
    for (unsigned i = 0; i < SAMPLES; i++)
        votes[(unsigned)classify_once(pin)]++;

    /* Leave the pad in the least surprising state for whatever runs
     * next: a plain input with no pull and default drive... */
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);

    /* ...except that "whatever runs next" may be the console. Give the
     * pad back whatever owned it. */
    if (was != GPIO_FUNC_SIO && was != GPIO_FUNC_NULL)
        gpio_set_function(pin, was);

    pinsig_t best = PINSIG_FLOAT;
    unsigned best_n = 0;
    for (unsigned s = PINSIG_FLOAT; s <= PINSIG_LOW; s++) {
        if (votes[s] > best_n) { best_n = votes[s]; best = (pinsig_t)s; }
    }
    return best;
}

unsigned pinsig_score(const frank_board_desc_t *desc,
                      uint8_t *mismatches, unsigned max_mismatch,
                      unsigned *out_mismatch_count) {
    unsigned matched = 0, bad = 0;

    for (unsigned i = 0; i < desc->sig_len; i++) {
        const pinsig_entry_t *e = &desc->sig[i];

        if (e->expect == PINSIG_DONTCARE) { matched++; continue; }

        if (pinsig_classify(e->pin) == e->expect) {
            matched++;
        } else {
            if (mismatches && bad < max_mismatch) mismatches[bad] = e->pin;
            bad++;
        }
    }

    if (out_mismatch_count) *out_mismatch_count = bad;
    return matched;
}

const char *pinsig_name(pinsig_t s) {
    switch (s) {
        case PINSIG_FLOAT: return "float";
        case PINSIG_HIGH:  return "high";
        case PINSIG_LOW:   return "low";
        default:           return "-";
    }
}
