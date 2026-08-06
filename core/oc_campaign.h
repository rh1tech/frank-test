/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * oc_campaign.h — an overclock search that spans reboots.
 *
 * Finding the clock a particular board will hold means trying clocks it
 * will not, and a clock a board will not hold generally takes the
 * firmware down with it. So the search cannot live inside one run: each
 * candidate needs its own boot, and something has to remember where the
 * search had got to when the previous one died.
 *
 *
 * WHY THIS IS NOT IN FLASH
 *
 * Because it must not survive a power cycle, for exactly the reason
 * oc_request.h gives. The only durable state that could brick a board is
 * a stored clock the board cannot start at, and a campaign record in
 * flash is one bad interruption away from being read as exactly that.
 *
 * A watchdog scratch register survives what needs surviving — a reset,
 * including the watchdog reset that a hung candidate triggers — and is
 * cleared by the one thing an operator will always try, which is pulling
 * the plug. A campaign interrupted that way is simply abandoned, and
 * abandoning it costs five minutes rather than a debug probe.
 *
 *
 * WHAT SURVIVING MEANS
 *
 * Two separate things, and the campaign needs both:
 *
 *   it started    the candidate was applied, the board reached the
 *                 interface, and the clock it actually reached is the
 *                 one that was asked for. A boot that hangs is caught by
 *                 the watchdog and shows up as this record still saying
 *                 RUNNING on a boot that took no request at all.
 *
 *   it held       the candidate then survived a soak. Booting proves
 *                 very little: the failures worth finding are the ones
 *                 that need the parts warm and the buses busy.
 *
 * `trying` is therefore the candidate this boot was asked for, and
 * `best` is the highest index that has done both.
 */
#ifndef OC_CAMPAIGN_H
#define OC_CAMPAIGN_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OC_CAMP_IDLE = 0,
    OC_CAMP_RUNNING,     /* a candidate is out there */
    OC_CAMP_DONE,        /* finished; the result is waiting to be shown */

    /* Back at stock, showing the operator what just happened before the
     * next candidate goes out.
     *
     * A campaign is several minutes of black screen punctuated by
     * reboots, and from the outside that is indistinguishable from a
     * board that has hung — which is exactly how it was reported. So
     * every rung is followed by a boot at the stock clock, where the
     * picture works, holding the table up for a few seconds. It costs a
     * reboot per rung and buys the one thing the run had no way to
     * show: progress. */
    OC_CAMP_SHOWING,
} oc_camp_state_t;

typedef struct {
    uint8_t state;       /* oc_camp_state_t                            */
    uint8_t trying;      /* index into oc_cpu_ladder                   */
    uint8_t soak_s;      /* seconds of soak per candidate              */

    /* 0 means "use the clock's own voltage" — oc_vreg_for_clock(), the
     * pairing frank-msx and murmgenesis both use. Anything else is an
     * operator override held for the whole run. */
    uint8_t vreg_sel;

    /* This attempt is a second go at a rung that already failed once.
     *
     * Rungs are not reliably pass or fail. Measured on a Core 2U master:
     * 504 MHz soaked clean with 129 passes on one attempt and would not
     * reach the interface on the next, same settings, same board. One
     * attempt per rung therefore reports a coin toss as a measurement.
     *
     * A rung is only recorded as failed when it has failed twice. */
    uint8_t retry;

    /* One bit per rung, so the result table can say what happened to
     * every clock rather than naming a single winner and leaving the
     * rest as dashes. A dash is not a result and it read as one.
     *
     * Three rungs is what makes this affordable: the ladder used to have
     * twelve and there was no room in the scratch word for a bit each,
     * which is precisely why the first version could only report a
     * "best". Cutting the ladder to the clocks that can actually show a
     * picture paid for the honest table. */
    uint8_t tested;      /* bit i set: rung i has a verdict  */
    uint8_t passed;      /* bit i set: rung i held           */
} oc_campaign_t;

/* Highest rung that held, or -1 when none has. */
int oc_campaign_best(const oc_campaign_t *c);

/* False when no campaign is recorded, in which case `out` is zeroed. */
bool oc_campaign_load(oc_campaign_t *out);

void oc_campaign_store(const oc_campaign_t *c);
void oc_campaign_clear(void);

/* The soak lengths on offer. Anything shorter than fifteen seconds
 * finds only the candidates that fail immediately, which the boot
 * already catches; anything longer than two minutes turns a five-minute
 * answer into a twenty-minute one and is better served by Burn-in. */
extern const uint8_t oc_soak_choices[];
extern const unsigned oc_soak_choices_len;

#endif /* OC_CAMPAIGN_H */
