/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * registry.h — the test list, and what gates it.
 *
 * Each test declares the capabilities it needs. The runner compares that
 * against the detected board and skips what the board does not have,
 * reporting **n/a** rather than **fail** — the distinction the whole
 * design rests on, because conflating "this board has no PS/2" with "the
 * PS/2 is broken" is how a test suite stops being believed.
 *
 * A test that cannot reach a verdict returns NORUN with a reason. That
 * is a third outcome, not a soft failure: "the exchange never completed"
 * and "two bytes were wrong" send you to different places.
 */
#ifndef REGISTRY_H
#define REGISTRY_H

#include "board_desc.h"
#include "detect.h"
#include "ui_desktop.h"

#define TEST_DETAIL_LEN 40

/* Called by a long test so the row can show what it is doing.
 *
 * `permille` is 0..1000 for the thermometer. `status`, when not NULL,
 * replaces the row's detail text and is redrawn immediately — which is
 * what lets an audio test say "Testing Left" while the left channel is
 * actually playing, rather than reporting three channels after the fact
 * and leaving the operator to guess which one they just heard.
 *
 * May be NULL, in which case the test simply runs without narrating. */
typedef void (*test_progress_fn)(int permille, const char *status);

typedef struct {
    const char      *name;
    ui_icon_id_t     icon;

    /* Every bit here must be present, or the test is n/a. Zero means
     * "runs anywhere". */
    frank_caps_t     requires;

    /* At least one bit here must be present. Separate from `requires`
     * because a mask like CAP_VIDEO_ANY is a set of alternatives, and
     * feeding it to an all-bits test demands HDMI *and* VGA *and*
     * composite — which no board has, so every video test silently
     * became n/a. Zero means "no such constraint". */
    frank_caps_t     requires_any;

    /* There is no "destructive" flag.
     *
     * There was one, with a "Skip Destructive" menu toggle, on the theory
     * that resetting the slave was something to opt into. It is not:
     * rebooting a processor that is meant to be rebootable is the test,
     * and gating it behind a switch meant the one path that recovers a
     * wedged slave was off by default. Nothing this rig does destroys
     * anything.
     */

    ui_test_state_t (*run)(const detect_result_t *d,
                           char *detail, unsigned detail_len,
                           test_progress_fn progress);
} frank_test_t;

/* The table, defined in registry.c from the tests/ directory. */
extern const frank_test_t frank_tests[];
extern const unsigned     frank_tests_len;

/* One row per test, in registry order. Filled by registry_prepare() and
 * updated in place as the run proceeds, which is what lets the interface
 * redraw a partially-complete run without knowing anything about tests. */
typedef struct {
    ui_test_row_t rows[40];
    char          detail[40][TEST_DETAIL_LEN];
    unsigned      count;
    unsigned      passed, failed, na, norun, pending;
} registry_results_t;

/* Build the row list for this board: every test, with the ones the board
 * cannot support already marked n/a. Nothing has run yet.
 *
 * When the board could not be identified, every row is marked "select a
 * board first" instead — see the note in registry_prepare(). */
void registry_prepare(registry_results_t *r, const detect_result_t *d);

/* Capabilities the probes actually demonstrated, whatever the descriptor
 * claims. Evidence outranks the table: a PSRAM that answered is present
 * even when the board could not be named, which is exactly the case on
 * an ambiguous core2/core2u. */
frank_caps_t registry_detected_caps(const detect_result_t *d);

/* Run test `i`, updating its row. Split from a run-everything loop so
 * the caller can redraw between tests — a test rig that goes blank for
 * thirty seconds and then prints everything is much harder to trust than
 * one you can watch. */
void registry_run_one(registry_results_t *r, unsigned i,
                      const detect_result_t *d, test_progress_fn progress);

/* Recount the summary tallies. */
void registry_tally(registry_results_t *r);

#endif /* REGISTRY_H */
