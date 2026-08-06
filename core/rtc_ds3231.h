/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * rtc_ds3231.h — reading and setting the wall clock, where there is one.
 *
 * Two boards in the fleet carry a DS3231: megafrank and frank_next, both
 * on the I2C pair the descriptor names. Everything else has no notion of
 * the date at all, and this says so rather than inventing one — a
 * plausible wrong timestamp on a test report is worse than a missing
 * one, because it gets believed later.
 *
 *
 * WHY THERE IS NO NTP HERE
 *
 * It was asked for, and the fleet will not support it. The two boards
 * with an RTC and the two boards with a network interface are disjoint
 * sets except for frank_next, whose ESP32 runs firmware this repository
 * does not own. So the time comes from a person: either the dialog, or
 * `time 2026-08-06 14:31:00` typed at the console by a host that already
 * knows what time it is, which is the same answer with fewer keystrokes.
 *
 *
 * THE OSCILLATOR-STOP FLAG
 *
 * The DS3231 sets OSF itself whenever its oscillator has stopped since
 * the flag was last cleared, and nothing but a write clears it. That
 * makes it the one piece of evidence that a coin cell has gone flat, and
 * the RTC test in tests_board.c reports it as such.
 *
 * Which is exactly why clearing it is never automatic. rtc_set() clears
 * it because a freshly-set clock is by definition no longer stale; that
 * is an operator holding down a key, not a firmware deciding on their
 * behalf. A boot-time clear would quietly disarm the check that catches
 * a dead battery.
 *
 * tests_board.c keeps its own register-level probe rather than calling
 * this. It is not reading the time — it watches the seconds register
 * advance across a second of wall clock to tell a part that answers from
 * a part that is actually running, and that is a different question from
 * "what time is it".
 */
#ifndef RTC_DS3231_H
#define RTC_DS3231_H

#include "detect.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t year;    /* full year, 2000..2099 — the part stores two digits */
    uint8_t  mon;     /* 1..12 */
    uint8_t  day;     /* 1..31 */
    uint8_t  hour;    /* 0..23, always: the 12-hour mode is never selected */
    uint8_t  min;     /* 0..59 */
    uint8_t  sec;     /* 0..59 */
} rtc_time_t;

/* True when this board has an RTC *and* it answered during detection.
 * Both halves matter: a board that should have one and does not is a
 * fault, not a board without a clock. */
bool rtc_present(const detect_result_t *d);

/* The time, and whether the part is flagging that its oscillator has
 * stopped since OSF was last cleared. `osf` may be NULL.
 *
 * False means the bus did not answer, in which case `out` is untouched.
 * A true return with `osf` set is a real reading of a clock that has
 * lost time — the registers hold whatever they held when it stopped. */
bool rtc_read(const detect_result_t *d, rtc_time_t *out, bool *osf);

/* Set the clock, and clear OSF because it is no longer true.
 *
 * Rejects a time that is not a real one rather than writing BCD the part
 * will read back as garbage. Returns false on a rejected value or a bus
 * that would not take the write. */
bool rtc_set(const detect_result_t *d, const rtc_time_t *t);

/* Clear the oscillator-stop flag and nothing else — the minimum the
 * operator asked for when the clock is right and only the flag is
 * stale. */
bool rtc_clear_osf(const detect_result_t *d);

/* Sanity, exposed so a dialog can grey out its own Set button rather
 * than offering one that will be refused. */
bool rtc_time_valid(const rtc_time_t *t);

/* Days in a month, leap years included. The dialog needs it to clamp the
 * day when the month or the year moves under it. */
uint8_t rtc_days_in_month(uint16_t year, uint8_t mon);

/* The firmware's own build date, as a starting point for a clock that
 * has never been set. It is wrong by however long the image has been in
 * the field, and it is a great deal closer than 2000-01-01 — the
 * operator is adjusting from something recognisable rather than typing
 * six fields from scratch. */
void rtc_build_time(rtc_time_t *out);

#endif /* RTC_DS3231_H */
