/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tests_link.c — the inter-processor link, wrapped as tests.
 *
 * The doorbell edge Tier 1 catches during detection is enough to say
 * "something answered". It is not enough to call the link tested, and it
 * has no recovery at all: a slave that is still booting, wedged, or in
 * lockup all look identical to it, and it does nothing about any of them.
 *
 * link_diag.c carries the real logic, ported from frank_core2's master
 * firmware where it was validated on hardware. What matters here is that
 * recovery *escalates* rather than reaching for the biggest hammer:
 *
 *   nothing      a slave still booting, or genuinely absent
 *   FS request   a slave whose foreground is stuck — it samples FS from
 *                a timer interrupt, so it can still reboot itself
 *   reset pulse  a slave in lockup with interrupts off, which no amount
 *                of software on either side can reach
 *
 * with a backoff after each request, because probing again before the
 * slave has finished booting just resets it a second time — and at the
 * wrong interval that becomes a reboot loop the operator reads as "the
 * slave never comes up".
 */

#include "registry.h"
#include "link_diag.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

static bool               s_inited;
static diag_link_result_t s_result;

/* Bring the PIO link up once. Safe to call when the board has no link —
 * it simply does not. */
void tests_link_init(const detect_result_t *d) {
    if (s_inited) return;
    if (!d->board || !(d->board->caps & CAP_LINK)) return;
    diag_link_init();
    s_inited = true;
}

/* Called from the idle loop. Returns true when a slave that was absent
 * has just appeared, so the caller can re-run the affected tests. */
bool tests_link_poll(void) {
    if (!s_inited || s_result.contacted) return false;
    return diag_link_try_reconnect();
}

/* ------------------------------------------------------------------ */

static ui_test_state_t t_link(const detect_result_t *d, char *detail,
                              unsigned len, test_progress_fn p) {
    if (!s_inited) {
        snprintf(detail, len, "link not initialised");
        return TEST_NORUN;
    }

    if (p) p(100, NULL);
    diag_link_run(&s_result);
    if (p) p(1000, NULL);

    if (!s_result.contacted) {
        /* Ask it to reboot before giving up. The slave may be wedged
         * rather than absent, and the difference is not visible from
         * here — but it is recoverable, and one FS pulse costs 250 ms. */
        snprintf(detail, len, "no answer; asked slave to reset");
        diag_link_try_reconnect();
        return TEST_NORUN;
    }

    if (!s_result.all_passed) {
        snprintf(detail, len, "%u byte errors at peak",
                 (unsigned)s_result.sweep[0].byte_errors);
        return TEST_FAIL;
    }

    /* Aggregate duplex at the fastest divider is the number worth
     * printing: it is what the link is actually good for. */
    const uint32_t kib = s_result.sweep[0].duplex_bytes_per_s / 1024u;
    snprintf(detail, len, "%u.%u MiB/s duplex",
             (unsigned)(kib / 1024u), (unsigned)((kib % 1024u) * 10u / 1024u));
    return TEST_PASS;
}

/* The reset path, tested rather than assumed.
 *
 * Marked destructive because it deliberately knocks the slave over. A
 * pass means all three of: it was answering, it stopped, and it came
 * back — checking only the last would pass a slave that never rebooted. */
static ui_test_state_t t_link_reset(const detect_result_t *d, char *detail,
                                    unsigned len, test_progress_fn p) {
    if (!s_inited) {
        snprintf(detail, len, "link not initialised");
        return TEST_NORUN;
    }
    if (p) p(200, NULL);

    if (diag_link_reset_slave()) {
        snprintf(detail, len, "FS reset: down and back");
        return TEST_PASS;
    }
    snprintf(detail, len, "slave did not reset via FS");
    return TEST_FAIL;
}

/* The link over minutes rather than milliseconds.
 *
 * "Processor link" measures a burst: one sweep, a few hundred
 * milliseconds, and a throughput figure. That answers whether the link
 * works and says nothing about whether it keeps working, which is a
 * different question and the one that matters for a board going into a
 * case. A link with a marginal joint, a reflection, or a slave whose
 * clock drifts as it warms passes every burst and drops bytes every few
 * seconds.
 *
 * So this repeats the sweep for a fixed spell and counts. What comes out
 * is an error rate - errors against bytes moved - which is the figure
 * you can actually compare between two boards, or between the same board
 * cold and warm.
 *
 * Thirty seconds is a compromise and worth naming as one. It is long
 * enough to catch a fault that shows up every few seconds and far too
 * short to characterise one that shows up every few minutes; a rig doing
 * acceptance rather than diagnosis should run the burn-in instead, which
 * has no limit and includes this test in every cycle.
 *
 * A single error fails the row. Not because one error is a catastrophe -
 * it is not, on a link with no retry - but because the burst test
 * already passes on a clean link, so a soak that tolerated errors would
 * report exactly what the burst reported and be worth nothing. */
#define SOAK_MS 30000u

static ui_test_state_t t_link_soak(const detect_result_t *d, char *detail,
                                   unsigned len, test_progress_fn p) {
    (void)d;

    if (!s_inited) {
        snprintf(detail, len, "link not initialised");
        return TEST_NORUN;
    }

    diag_link_result_t r;
    uint32_t errors = 0, passes = 0;

    const absolute_time_t started = get_absolute_time();
    const absolute_time_t deadline = make_timeout_time_ms(SOAK_MS);

    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        diag_link_run(&r);

        if (!r.contacted) {
            snprintf(detail, len, "slave stopped answering after %lus",
                     (unsigned long)(absolute_time_diff_us(started,
                         get_absolute_time()) / 1000000));
            return TEST_FAIL;
        }

        passes++;
        errors += r.sweep[0].byte_errors;

        if (p) {
            const int64_t gone = absolute_time_diff_us(started, get_absolute_time());
            p((int)((gone / 1000) * 1000 / SOAK_MS), NULL);
        }
    }
    if (p) p(1000, NULL);

    /* Passes and errors, not bytes. The diagnostic reports throughput
     * and error counts but not how much it moved, and multiplying one by
     * elapsed time would produce a MiB figure that looks measured and is
     * not. */
    if (errors) {
        snprintf(detail, len, "%lu errors over %lu passes",
                 (unsigned long)errors, (unsigned long)passes);
        return TEST_FAIL;
    }

    snprintf(detail, len, "%lu passes clean in %lus",
             (unsigned long)passes, (unsigned long)(SOAK_MS / 1000u));
    return TEST_PASS;
}

const frank_test_t frank_tests_link[] = {
    { "Processor link", ICON_LINK, CAP_LINK, 0, t_link       },
    { "Link soak",      ICON_LINK, CAP_LINK, 0, t_link_soak  },
    { "Slave reset",    ICON_LINK, CAP_LINK, 0, t_link_reset },
};

const unsigned frank_tests_link_len =
    sizeof(frank_tests_link) / sizeof(frank_tests_link[0]);
