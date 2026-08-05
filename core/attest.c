/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "attest.h"

static attest_t s_verdict[ATTEST_SUBJECT_COUNT];

void attest_set(attest_subject_t subject, attest_t verdict) {
    if (subject < ATTEST_SUBJECT_COUNT) s_verdict[subject] = verdict;
}

attest_t attest_get(attest_subject_t subject) {
    return (subject < ATTEST_SUBJECT_COUNT) ? s_verdict[subject]
                                            : ATTEST_UNKNOWN;
}
