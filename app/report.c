/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * report.c — the run, written to the card.
 *
 *
 * WHY THIS EXISTS
 *
 * Everything else here is a bench tool: someone runs the tests, looks at
 * the screen, and the result exists only in their memory. That is enough
 * for debugging one board and not enough for building them. A rig that
 * assembles a batch wants a record per unit, and it wants that record to
 * carry an identity that came off the board rather than off a label.
 *
 * So the file is named for the DS2401 serial where there is one - that
 * part exists to give a board a permanent name - and the run is appended
 * rather than overwriting, so a board tested twice shows both.
 *
 *
 * WHAT IT COSTS
 *
 * A filesystem. tests_sd.c argues against FatFs and the argument still
 * holds for the *tests*: whether a card carries a filesystem this
 * firmware recognises says nothing about whether the board works, and a
 * test that needed one would be answering the wrong question. Writing a
 * report is a different job, and the operator asked for it explicitly
 * from a menu, so the dependency is theirs to invoke rather than
 * something every run drags in.
 *
 * Long filenames are compiled out - the Unicode tables are two megabytes
 * - so the name is 8.3 and looks it.
 *
 *
 * THE TIMESTAMP
 *
 * From the DS3231 when the board has one, and plainly absent when it
 * does not. Four boards carry an RTC; a report that invented a date on
 * the rest would be worse than one that says it has none, because a
 * plausible wrong date is the kind of thing that gets believed later.
 */

#include "report.h"

#include "attest.h"
#include "ff.h"
#include "i2c_bb.h"
#include "sdblock.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DS3231_ADDR 0x68u

/* Same string the banner and the About box use; the build defines it. */
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0"
#endif
#define REPORT_VERSION FIRMWARE_VERSION

/* BCD, as everything in a DS3231 is. */
static unsigned bcd(uint8_t v) { return (v >> 4) * 10u + (v & 0x0Fu); }

/* "2026-08-05 14:31:07", or an empty string when the board has no RTC or
 * the RTC will not answer. The caller says so rather than substituting
 * something that looks like a date. */
static void rtc_stamp(const detect_result_t *d, char *out, unsigned len) {
    out[0] = '\0';

    const frank_pins_t *p = d->board ? &d->board->pins : NULL;
    if (!d->i2c_ds3231 || !p ||
        p->i2c_sda == PIN_NC || p->i2c_scl == PIN_NC) return;

    const unsigned sda = (unsigned)p->i2c_sda, scl = (unsigned)p->i2c_scl;
    uint8_t r[7];

    i2c_bb_init(sda, scl);
    const bool got = i2c_bb_read_regs(sda, scl, DS3231_ADDR, 0x00u, r, sizeof(r));
    i2c_bb_release(sda, scl);
    if (!got) return;

    snprintf(out, len, "20%02u-%02u-%02u %02u:%02u:%02u",
             bcd(r[6]), bcd(r[5] & 0x1Fu), bcd(r[4]),
             bcd(r[2] & 0x3Fu), bcd(r[1]), bcd(r[0]));
}

static const char *state_name(ui_test_state_t st) {
    switch (st) {
        case TEST_PASS:  return "PASS";
        case TEST_FAIL:  return "FAIL";
        case TEST_NA:    return "n/a ";
        case TEST_NORUN: return "----";
        default:         return "?   ";
    }
}

/* Everything is written through one buffer and one f_write, because a
 * FatFs write per line on a card with a 32 KiB cluster is slow enough to
 * look like a hang. */
#define REPORT_MAX 4096

static int append(char *buf, int at, const char *fmt, ...) {
    if (at < 0 || at >= REPORT_MAX - 1) return at;

    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf + at, (size_t)(REPORT_MAX - at), fmt, ap);
    va_end(ap);

    return (n < 0) ? at : (at + n);
}

report_result_t report_write(const detect_result_t *d,
                             const registry_results_t *r,
                             char *name_out, unsigned name_len) {
    if (name_out && name_len) name_out[0] = '\0';

    if (!d || !d->board) return REPORT_NO_BOARD;
    if (!sdblock_init(&d->board->pins)) return REPORT_NO_CARD;

    static FATFS fs;
    static FIL   fil;
    report_result_t rc = REPORT_OK;

    if (f_mount(&fs, "", 1) != FR_OK) { sdblock_release(); return REPORT_NO_FS; }

    /* Named for the board's own serial where there is one. A batch of
     * files called REPORT.TXT would be one file. */
    char path[32];
    if (d->onewire_found)
        snprintf(path, sizeof(path), "%02X%02X%02X%02X.TXT",
                 d->onewire_rom[4], d->onewire_rom[3],
                 d->onewire_rom[2], d->onewire_rom[1]);
    else
        snprintf(path, sizeof(path), "%02X%02X%02X%02X.TXT",
                 d->chip_id[0], d->chip_id[1], d->chip_id[2], d->chip_id[3]);

    if (f_open(&fil, path, FA_WRITE | FA_OPEN_APPEND) != FR_OK) {
        f_mount(NULL, "", 0);
        sdblock_release();
        return REPORT_NO_WRITE;
    }

    static char buf[REPORT_MAX];
    int at = 0;

    char stamp[32];
    rtc_stamp(d, stamp, sizeof(stamp));

    at = append(buf, at, "\r\n=== FRANK test firmware v%s ===\r\n", REPORT_VERSION);
    at = append(buf, at, "board    %s\r\n", d->board->name);
    at = append(buf, at, "silicon  %s rev %u\r\n",
                frank_mcu_class_name(d->mcu), (unsigned)d->chip_rev);
    at = append(buf, at, "chip id  %02X%02X%02X%02X\r\n",
                d->chip_id[0], d->chip_id[1], d->chip_id[2], d->chip_id[3]);
    if (d->onewire_found)
        at = append(buf, at, "unit     %02X%02X%02X%02X%02X%02X\r\n",
                    d->onewire_rom[6], d->onewire_rom[5], d->onewire_rom[4],
                    d->onewire_rom[3], d->onewire_rom[2], d->onewire_rom[1]);
    at = append(buf, at, "time     %s\r\n",
                stamp[0] ? stamp : "no RTC on this board");
    at = append(buf, at, "\r\n");

    for (unsigned i = 0; i < r->count; i++)
        at = append(buf, at, "%-4s %-18s %s\r\n",
                    state_name(r->rows[i].state), r->rows[i].name,
                    r->detail[i][0] ? r->detail[i] : "-");

    at = append(buf, at, "\r\n%u passed, %u failed, %u n/a, %u could not run\r\n",
                r->passed, r->failed, r->na, r->norun);

    /* What a person said, where they said it. It is the only evidence
     * for the audio paths and belongs in the record as plainly as the
     * measurements do. */
    static const struct { attest_subject_t s; const char *name; } att[] = {
        { ATTEST_AUDIO_PWM, "audio PWM"  },
        { ATTEST_AUDIO_I2S, "audio I2S"  },
        { ATTEST_AUDIO_TS,  "TurboSound" },
    };
    for (unsigned i = 0; i < sizeof(att) / sizeof(att[0]); i++) {
        const attest_t v = attest_get(att[i].s);
        if (v == ATTEST_UNKNOWN) continue;
        at = append(buf, at, "by ear: %s %s\r\n", att[i].name,
                    v == ATTEST_YES ? "heard" : "silent");
    }

    UINT written = 0;
    if (f_write(&fil, buf, (UINT)at, &written) != FR_OK || (int)written != at)
        rc = REPORT_NO_WRITE;

    f_close(&fil);
    f_mount(NULL, "", 0);
    sdblock_release();

    if (rc == REPORT_OK && name_out && name_len)
        snprintf(name_out, name_len, "%s", path);
    return rc;
}
