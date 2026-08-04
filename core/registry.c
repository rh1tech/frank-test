/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "registry.h"

#include <stdio.h>
#include <string.h>

/* The suites, gathered here rather than each registering itself at
 * startup. A static list is inspectable in a diff and orders itself the
 * way it will appear on screen, which a constructor-based registry does
 * not. */
extern const frank_test_t frank_tests_memory[];
extern const unsigned     frank_tests_memory_len;
extern const frank_test_t frank_tests_board[];
extern const unsigned     frank_tests_board_len;
extern const frank_test_t frank_tests_link[];
extern const unsigned     frank_tests_link_len;
extern const frank_test_t frank_tests_psram_spi[];
extern const unsigned     frank_tests_psram_spi_len;
extern const frank_test_t frank_tests_sd[];
extern const unsigned     frank_tests_sd_len;
extern const frank_test_t frank_tests_esp[];
extern const unsigned     frank_tests_esp_len;

/* Flattened once at first use. */
static frank_test_t all[40];
static unsigned     all_len;

static void gather(void) {
    if (all_len) return;

    for (unsigned i = 0; i < frank_tests_memory_len && all_len < 40; i++)
        all[all_len++] = frank_tests_memory[i];
    for (unsigned i = 0; i < frank_tests_board_len && all_len < 40; i++)
        all[all_len++] = frank_tests_board[i];
    for (unsigned i = 0; i < frank_tests_psram_spi_len && all_len < 40; i++)
        all[all_len++] = frank_tests_psram_spi[i];
    for (unsigned i = 0; i < frank_tests_sd_len && all_len < 40; i++)
        all[all_len++] = frank_tests_sd[i];
    for (unsigned i = 0; i < frank_tests_esp_len && all_len < 40; i++)
        all[all_len++] = frank_tests_esp[i];
    for (unsigned i = 0; i < frank_tests_link_len && all_len < 40; i++)
        all[all_len++] = frank_tests_link[i];
}

const frank_test_t frank_tests[] = { { 0 } };   /* unused; see gather() */
const unsigned     frank_tests_len = 0;

void registry_prepare(registry_results_t *r, const detect_result_t *d) {
    gather();
    memset(r, 0, sizeof(*r));

    /* The union of what the board claims and what the probes proved.
     *
     * Without the second half, a board that detection could not name
     * falls back to the fleet-wide minimum (board_table.c) and then
     * reports its 8 MB of PSRAM as "not fitted" — with the size and chip
     * select printed two lines above in the detection log. */
    const frank_caps_t caps = (d->board ? d->board->caps : 0)
                            | registry_detected_caps(d);

    /* An unidentified board runs nothing.
     *
     * The fallback descriptor only asserts the pin conventions the whole
     * fleet shares, so tests gated on anything else would report "not
     * fitted" about hardware nobody has looked for — and the ones that
     * *did* run would be measuring pins whose function is a guess. A
     * screen full of confident results derived from an unknown pin map is
     * worse than an empty one. */
    const bool identified = d->board && d->board->id != FRANK_BOARD_UNKNOWN;

    for (unsigned i = 0; i < all_len; i++) {
        const frank_test_t *t = &all[i];

        r->detail[i][0] = '\0';
        r->rows[i].icon   = t->icon;
        r->rows[i].name   = t->name;
        r->rows[i].detail = r->detail[i];

        /* The gate. A board that lacks the hardware gets n/a *before*
         * anything runs, so the list is complete and honest from the
         * first frame rather than filling in as tests are skipped. */
        const bool lacks_all = t->requires &&
                               (caps & t->requires) != t->requires;
        const bool lacks_any = t->requires_any &&
                               (caps & t->requires_any) == 0;

        if (!identified) {
            r->rows[i].state = TEST_NORUN;
            snprintf(r->detail[i], TEST_DETAIL_LEN, "select a board first");
        } else if (lacks_all || lacks_any) {
            /* "not fitted" is a claim about the hardware. It can only be
             * made when the board is known — on the fallback descriptor
             * the capability mask is the fleet-wide minimum, so a missing
             * bit means "we never found out", which is a different answer
             * and a different colour.
             *
             * Getting this wrong told the operator a Core 2 had no
             * inter-processor link, on a board whose defining feature is
             * an inter-processor link. */
            if (identified) {
                r->rows[i].state = TEST_NA;
                snprintf(r->detail[i], TEST_DETAIL_LEN, "not fitted");
            } else {
                r->rows[i].state = TEST_NORUN;
                snprintf(r->detail[i], TEST_DETAIL_LEN, "board not identified");
            }
        } else {
            r->rows[i].state = TEST_PENDING;
        }
    }
    r->count = all_len;
    registry_tally(r);
}

void registry_run_one(registry_results_t *r, unsigned i,
                      const detect_result_t *d, test_progress_fn progress) {
    gather();
    if (i >= r->count) return;

    const frank_test_t *t = &all[i];

    if (r->rows[i].state == TEST_NA) return;   /* decided in prepare */

    r->rows[i].state    = TEST_RUNNING;
    r->rows[i].progress = 0;

    r->rows[i].state = t->run(d, r->detail[i], TEST_DETAIL_LEN, progress);
    registry_tally(r);
}

frank_caps_t registry_detected_caps(const detect_result_t *d) {
    frank_caps_t c = 0;

    if (d->psram_cs_found != PIN_NC && d->psram_bytes) c |= CAP_PSRAM_QMI;
    if (d->i2c_ds3231)    c |= CAP_RTC_DS3231 | CAP_I2C;
    if (d->i2c_tlv320)    c |= CAP_AUDIO_CODEC_I2C | CAP_I2C;
    if (d->onewire_found) c |= CAP_ONEWIRE_DS2401;
    /* A link peer only counts on a board that has link pins.
     *
     * Evidence outranks the table, but not to the point of inventing
     * hardware. On FRANK the link probe drives GP20-29, which there are
     * the gamepad ports and the ESP-01S UART, and something answered —
     * so a single-processor board grew an inter-processor link and ran
     * two tests against it. */
    if (d->link_peer && d->board && d->board->pins.link_a_data != PIN_NC)
        c |= CAP_LINK;

    /* Video is not inferred here. A backend that came up proves the
     * firmware configured one, not that anything is plugged in — and
     * ui_video_current() is the honest place to ask that. */
    return c;
}

void registry_tally(registry_results_t *r) {
    r->passed = r->failed = r->na = r->norun = r->pending = 0;
    for (unsigned i = 0; i < r->count; i++) {
        switch (r->rows[i].state) {
            case TEST_PASS:  r->passed++;  break;
            case TEST_FAIL:  r->failed++;  break;
            case TEST_NA:    r->na++;      break;
            case TEST_NORUN: r->norun++;   break;
            default:         r->pending++; break;
        }
    }
}
