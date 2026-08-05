/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * attest.h — what the operator saw or heard, recorded as their word.
 *
 * Some hardware cannot be measured from this chip, and no amount of
 * cleverness changes that. The TurboSound is the clearest case: its two
 * AYs sit behind a pair of 74HC595s whose outputs go only to the AY data
 * bus, the second shift register's serial output is not connected to
 * anything, and both output-enables are tied to ground so the chain
 * cannot be tri-stated. There is no path back. The audio leaves through
 * the mixer, and no board in the fleet has an ADC.
 *
 * So the firmware drives it and someone listens. That answer is worth
 * keeping - it is the only evidence there will ever be - and worth
 * labelling honestly, which is what this is for: a verdict that says on
 * its face that a person gave it, not a measurement.
 *
 * Deliberately not written to flash. An attestation is about this board
 * on this bench a minute ago; one that survived a power cycle would be
 * a claim about hardware nobody has looked at since.
 */
#ifndef ATTEST_H
#define ATTEST_H

typedef enum {
    ATTEST_AUDIO_PWM = 0,
    ATTEST_AUDIO_I2S,
    ATTEST_AUDIO_TS,
    ATTEST_AUDIO_PCM5122,
    ATTEST_SUBJECT_COUNT
} attest_subject_t;

typedef enum {
    ATTEST_UNKNOWN = 0,   /* nobody has been asked yet */
    ATTEST_YES,           /* heard it / saw it         */
    ATTEST_NO,            /* driven, and nothing       */
} attest_t;

void     attest_set(attest_subject_t subject, attest_t verdict);
attest_t attest_get(attest_subject_t subject);

#endif /* ATTEST_H */
