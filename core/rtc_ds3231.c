/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rtc_ds3231.h"

#include "i2c_bb.h"

#include <string.h>

#define DS3231_ADDR      0x68u
#define DS3231_REG_SECS  0x00u
#define DS3231_REG_STAT  0x0Fu
#define DS3231_STAT_OSF  0x80u

/* Everything in a DS3231 is BCD. */
static unsigned from_bcd(uint8_t v) { return (v >> 4) * 10u + (v & 0x0Fu); }
static uint8_t  to_bcd(unsigned v)  { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

/* The pins, or false when this board has no bus to put them on. */
static bool rtc_pins(const detect_result_t *d, unsigned *sda, unsigned *scl) {
    if (!d || !d->i2c_ds3231 || !d->board) return false;

    const frank_pins_t *p = &d->board->pins;
    if (p->i2c_sda == PIN_NC || p->i2c_scl == PIN_NC) return false;

    *sda = (unsigned)p->i2c_sda;
    *scl = (unsigned)p->i2c_scl;
    return true;
}

bool rtc_present(const detect_result_t *d) {
    unsigned sda, scl;
    return rtc_pins(d, &sda, &scl);
}

uint8_t rtc_days_in_month(uint16_t year, uint8_t mon) {
    static const uint8_t len[12] = { 31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31 };
    if (mon < 1u || mon > 12u) return 31u;
    if (mon == 2u) {
        /* 2000..2099, so the century rule never comes up — but it costs
         * nothing to be right about it. */
        const bool leap = (year % 4u == 0u) &&
                          (year % 100u != 0u || year % 400u == 0u);
        return leap ? 29u : 28u;
    }
    return len[mon - 1u];
}

bool rtc_time_valid(const rtc_time_t *t) {
    if (!t) return false;
    if (t->year < 2000u || t->year > 2099u) return false;
    if (t->mon  < 1u || t->mon > 12u)       return false;
    if (t->day  < 1u || t->day > rtc_days_in_month(t->year, t->mon)) return false;
    if (t->hour > 23u || t->min > 59u || t->sec > 59u) return false;
    return true;
}

bool rtc_read(const detect_result_t *d, rtc_time_t *out, bool *osf) {
    unsigned sda, scl;
    if (!out || !rtc_pins(d, &sda, &scl)) return false;

    uint8_t r[7], stat = 0;

    i2c_bb_init(sda, scl);
    const bool got_time = i2c_bb_read_regs(sda, scl, DS3231_ADDR,
                                           DS3231_REG_SECS, r, sizeof(r));
    const bool got_stat = i2c_bb_read_regs(sda, scl, DS3231_ADDR,
                                           DS3231_REG_STAT, &stat, 1);
    i2c_bb_release(sda, scl);

    if (!got_time) return false;

    out->sec  = (uint8_t)from_bcd(r[0] & 0x7Fu);
    out->min  = (uint8_t)from_bcd(r[1] & 0x7Fu);
    /* Bit 6 selects 12-hour mode. Nothing here ever sets it, but a part
     * that arrived from somewhere else might have it on, and reading
     * 0x52 as 52 o'clock would be worse than masking it off. */
    out->hour = (uint8_t)from_bcd(r[2] & 0x3Fu);
    out->day  = (uint8_t)from_bcd(r[4] & 0x3Fu);
    out->mon  = (uint8_t)from_bcd(r[5] & 0x1Fu);
    out->year = (uint16_t)(2000u + from_bcd(r[6]));

    if (osf) *osf = got_stat && (stat & DS3231_STAT_OSF);
    return true;
}

bool rtc_set(const detect_result_t *d, const rtc_time_t *t) {
    unsigned sda, scl;
    if (!rtc_time_valid(t) || !rtc_pins(d, &sda, &scl)) return false;

    /* Register pointer, then seven registers written in one burst — the
     * part auto-increments, so the whole date lands atomically and the
     * clock cannot be seen half-set. Seconds first, which also restarts
     * the count from a known edge.
     *
     * The day-of-week register (0x03) is written too. Nothing in this
     * firmware reads it, but leaving it at whatever the part held would
     * put a date and a weekday that disagree in front of anything that
     * does. */
    static const uint8_t dow_ref = 6u;   /* 2000-01-01 was a Saturday */
    unsigned days = 0;
    for (uint16_t y = 2000u; y < t->year; y++)
        days += ((y % 4u == 0u) && (y % 100u != 0u || y % 400u == 0u)) ? 366u : 365u;
    for (uint8_t m = 1u; m < t->mon; m++)
        days += rtc_days_in_month(t->year, m);
    days += (unsigned)(t->day - 1u);

    const uint8_t buf[8] = {
        DS3231_REG_SECS,
        to_bcd(t->sec),
        to_bcd(t->min),
        to_bcd(t->hour),                              /* bit 6 clear: 24-hour */
        (uint8_t)(((days + dow_ref - 1u) % 7u) + 1u), /* 1..7, Sunday = 1     */
        to_bcd(t->day),
        to_bcd(t->mon),                               /* bit 7 is the century */
        to_bcd((unsigned)(t->year - 2000u)),
    };

    i2c_bb_init(sda, scl);
    bool ok = i2c_bb_write(sda, scl, DS3231_ADDR, buf, sizeof(buf));

    /* And the flag, because a clock that was just set is no longer one
     * that has lost time. Done inside the same claim of the bus so a
     * half-applied set cannot leave the flag disagreeing with the time.
     */
    if (ok) {
        uint8_t stat = 0;
        if (i2c_bb_read_regs(sda, scl, DS3231_ADDR, DS3231_REG_STAT, &stat, 1)) {
            const uint8_t w[2] = { DS3231_REG_STAT,
                                   (uint8_t)(stat & (uint8_t)~DS3231_STAT_OSF) };
            ok = i2c_bb_write(sda, scl, DS3231_ADDR, w, sizeof(w));
        } else {
            ok = false;
        }
    }
    i2c_bb_release(sda, scl);

    return ok;
}

bool rtc_clear_osf(const detect_result_t *d) {
    unsigned sda, scl;
    if (!rtc_pins(d, &sda, &scl)) return false;

    uint8_t stat = 0;

    i2c_bb_init(sda, scl);
    bool ok = i2c_bb_read_regs(sda, scl, DS3231_ADDR, DS3231_REG_STAT, &stat, 1);
    if (ok) {
        /* Read-modify-write rather than a blind zero: the same register
         * carries the 32 kHz output enable and the alarm flags, and
         * clobbering those to clear one bit would be a side effect
         * nobody asked for. */
        const uint8_t w[2] = { DS3231_REG_STAT,
                               (uint8_t)(stat & (uint8_t)~DS3231_STAT_OSF) };
        ok = i2c_bb_write(sda, scl, DS3231_ADDR, w, sizeof(w));
    }
    i2c_bb_release(sda, scl);

    return ok;
}

void rtc_build_time(rtc_time_t *out) {
    if (!out) return;

    /* __DATE__ is "Mmm dd yyyy" and __TIME__ is "hh:mm:ss", both fixed
     * width, both guaranteed by the standard. Parsed rather than
     * preprocessed into a number because there is no way to do the
     * latter in C without a build step, and this costs nothing. */
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;
    const char *t = __TIME__;

    uint8_t mon = 1;
    for (uint8_t i = 0; i < 12u; i++) {
        if (d[0] == months[i * 3] && d[1] == months[i * 3 + 1] &&
            d[2] == months[i * 3 + 2]) { mon = (uint8_t)(i + 1u); break; }
    }

    out->mon  = mon;
    /* Day is space-padded below ten, so the tens digit may not be one. */
    out->day  = (uint8_t)((d[4] == ' ' ? 0u : (unsigned)(d[4] - '0') * 10u)
                          + (unsigned)(d[5] - '0'));
    out->year = (uint16_t)((d[7] - '0') * 1000 + (d[8] - '0') * 100 +
                           (d[9] - '0') * 10 + (d[10] - '0'));
    out->hour = (uint8_t)((t[0] - '0') * 10 + (t[1] - '0'));
    out->min  = (uint8_t)((t[3] - '0') * 10 + (t[4] - '0'));
    out->sec  = 0;

    if (!rtc_time_valid(out)) {
        const rtc_time_t fallback = { 2026u, 1u, 1u, 0u, 0u, 0u };
        *out = fallback;
    }
}
