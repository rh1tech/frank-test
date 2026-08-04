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

const frank_test_t frank_tests_link[] = {
    { "Processor link", ICON_LINK, CAP_LINK, 0, t_link       },
    { "Slave reset",    ICON_LINK, CAP_LINK, 0, t_link_reset },
};

const unsigned frank_tests_link_len =
    sizeof(frank_tests_link) / sizeof(frank_tests_link[0]);
