/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * pinsig.h — classify what is passively attached to a pad.
 *
 * Used by the Tier 2 board fingerprint and, indirectly, by the video
 * detector. See pinsig.c for why the internal pull-down is never used.
 */
#ifndef PINSIG_H
#define PINSIG_H

#include "board_desc.h"

#include <stdbool.h>
#include <stdint.h>

/* Classify one pad. Leaves the pin as a plain input with no pull.
 *
 * Returns PINSIG_FLOAT for a pin this function refuses to touch (see
 * pinsig_is_safe) rather than an error, because a signature entry for an
 * unsafe pin is a table bug and should show up as a mismatch, not as a
 * silently skipped comparison. */
pinsig_t pinsig_classify(unsigned pin);

/* Some pins must not be probed: on an RP2040 module GP23 drives the SMPS
 * mode, GP24 senses VBUS and GP29 divides VSYS. Driving those to
 * discharge a pad is at best rude and at worst damaging. */
bool pinsig_is_safe(unsigned pin);

/* Compare a measured set of pins against a descriptor's expected
 * signature.
 *
 * `mismatches` (optional) receives the pins that disagreed, up to
 * `max_mismatch`. Returns the number of entries that matched; the caller
 * compares that against desc->sig_len and against the runner-up to get
 * a margin. PINSIG_DONTCARE entries count as matched and cost nothing. */
unsigned pinsig_score(const frank_board_desc_t *desc,
                      uint8_t *mismatches, unsigned max_mismatch,
                      unsigned *out_mismatch_count);

/* Human-readable, for the report. */
const char *pinsig_name(pinsig_t s);

#endif /* PINSIG_H */
