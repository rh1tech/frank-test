/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tests_memory.c — silicon, flash and QSPI PSRAM.
 *
 * "QSPI PSRAM" means the part on the QMI chip select, which is what every
 * board with PSRAM has — except megafrank, which has that *and* a
 * bit-banged SPI part on GP31/32/33. The two are named apart because on a
 * board carrying both, a row labelled simply "PSRAM" is a coin toss as to
 * which one the result refers to. The SPI part lives in
 * tests_psram_spi.c.
 *
 * These run first because everything after them depends on being right:
 * a marginal QSPI clock makes every later throughput figure suspect, and
 * a PSRAM that is not really there makes a memory test pass by reading
 * back its own cache.
 */

#include "registry.h"
#include "mem_test.h"
#include "frank_xip.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/structs/sysinfo.h"

#include <stdio.h>
#include <string.h>

/* Human sizes. 8 MB rather than 8388608 — the reader is checking a part
 * number, not doing arithmetic. */
static void fmt_size(char *out, unsigned len, uint64_t bytes) {
    if (bytes >= 1024ull * 1024 * 1024)
        snprintf(out, len, "%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        snprintf(out, len, "%u MB", (unsigned)(bytes / (1024 * 1024)));
    else if (bytes >= 1024)
        snprintf(out, len, "%u KB", (unsigned)(bytes / 1024));
    else
        snprintf(out, len, "%u B", (unsigned)bytes);
}

static void fmt_rate(char *out, unsigned len, uint32_t kbps) {
    if (kbps >= 1024)
        snprintf(out, len, "%.1f MiB/s", (double)kbps / 1024.0);
    else
        snprintf(out, len, "%u KiB/s", (unsigned)kbps);
}

/* ------------------------------------------------------------------ */

/* The core voltage, in millivolts, as the regulator is actually set.
 *
 * Read back rather than remembered. What the firmware asked for and what
 * the regulator is doing are different things - a value above 1.30 V is
 * refused unless the limit has been lifted first, and the request then
 * quietly does nothing - so the only honest number is the one in the
 * register.
 *
 * The enum is not a linear scale: it steps 50 mV to 1.30, then 50 again
 * to 1.40, then 100 to 1.90. Hence a table rather than arithmetic. */
static unsigned core_mv(void) {
    static const uint16_t mv[] = {
         550,  600,  650,  700,  750,  800,  850,  900,   /* 0-7   */
         950, 1000, 1050, 1100, 1150, 1200, 1250, 1300,   /* 8-15  */
        1350, 1400, 1500, 1600, 1650, 1700, 1800, 1900,   /* 16-23 */
    };
    const unsigned sel = (unsigned)vreg_get_voltage();
    return (sel < count_of(mv)) ? mv[sel] : 0u;
}

/* Die temperature, from the sensor on the last ADC channel.
 *
 * The datasheet's conversion: 27 degrees at 0.706 V, falling 1.721 mV
 * per degree. It is not a calibrated part - the absolute figure is worth
 * a couple of degrees at best - but the *change* is trustworthy, and
 * change is what matters here. A burn-in that fails on the fortieth
 * cycle is a different report depending on whether the die was at thirty
 * degrees or seventy, and until now there was no way to know which.
 *
 * Returns tenths of a degree so the row can show one decimal without
 * floating point.
 *
 * The ADC is left as it was found. Nothing else in this firmware uses
 * it, but a board where something did would not thank us for leaving
 * the temperature sensor selected. */
static int die_decidegrees(void) {
    const bool was_on = (adc_hw->cs & ADC_CS_TS_EN_BITS) != 0u;

    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);

    /* A handful of readings, averaged. One sample of a 12-bit converter
     * on a sensor this small is a couple of degrees of noise. */
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += adc_read();
    const uint32_t raw = sum / 16u;

    if (!was_on) adc_set_temp_sensor_enabled(false);

    /* microvolts = raw * 3300000 / 4096 */
    const int32_t uv = (int32_t)((raw * 3300000u) / 4096u);
    /* T = 27 - (V - 0.706)/0.001721, in tenths */
    return 270 - (int)(((int64_t)uv - 706000) * 10 / 1721);
}

static ui_test_state_t t_silicon(const detect_result_t *d, char *detail,
                                 unsigned len, test_progress_fn p) {
    (void)p;

    const unsigned mv  = core_mv();
    const unsigned mhz = (unsigned)(clock_get_hz(clk_sys) / 1000000u);
    const int      dc  = die_decidegrees();

    /* The voltage is worth a row of its own only when it is unusual. At
     * the stock 1.10 V it is noise; anything else is a deliberate
     * overvolt and the first thing to know when a board is unstable. */
    if (mv)
        snprintf(detail, len, "%s r%u, %u MHz, %u.%02u V, %d.%d C",
                 frank_mcu_class_name(d->mcu), (unsigned)d->chip_rev,
                 mhz, mv / 1000u, (mv % 1000u) / 10u,
                 dc / 10, (dc < 0 ? -dc : dc) % 10);
    else
        snprintf(detail, len, "%s r%u, %u MHz, %d.%d C",
                 frank_mcu_class_name(d->mcu), (unsigned)d->chip_rev, mhz,
                 dc / 10, (dc < 0 ? -dc : dc) % 10);

    return TEST_PASS;
}

static ui_test_state_t t_flash_id(const detect_result_t *d, char *detail,
                                  unsigned len, test_progress_fn p) {
    (void)p;
    if (!d->flash_jedec || d->flash_jedec == 0xFFFFFF) {
        snprintf(detail, len, "no JEDEC response");
        return TEST_FAIL;
    }

    char sz[16];
    fmt_size(sz, sizeof(sz), d->flash_bytes);
    snprintf(detail, len, "%06X  %s", (unsigned)d->flash_jedec, sz);

    /* If the descriptor states a size, disagreeing with it is a finding:
     * either the wrong part is fitted or the wrong board was detected. */
    if (d->board && d->board->flash_bytes &&
        d->board->flash_bytes != d->flash_bytes)
        return TEST_FAIL;

    return TEST_PASS;
}

/* Reports the figure taken during boot, not a fresh one.
 *
 * Re-measuring here would measure the wrong thing: by this point the
 * HSTX scanout is running and its instruction fetches contend with XIP,
 * costing about three quarters of the bandwidth. A healthy 16 MB part
 * reads at 33.6 MiB/s with core 1 parked and 7.6 MiB/s without — and the
 * second number, printed next to a part number, reads as a fault. */
static ui_test_state_t t_flash_read(const detect_result_t *d, char *detail,
                                    unsigned len, test_progress_fn p) {
    (void)p;
    fmt_rate(detail, len, d->flash_read_kbps);
    /* A floor, not a target: anything this slow means the QSPI fell back
     * to a much lower clock, which is worth seeing even though it works. */
    return (d->flash_read_kbps > 16384) ? TEST_PASS : TEST_FAIL;
}

/* CRC-32 over the first 64 KiB, computed twice.
 *
 * The second pass is the whole point. One CRC tells you nothing — there
 * is no reference to compare it against. Two that disagree tell you the
 * QSPI timing is marginal, which makes every other number on the screen
 * suspect. */
static ui_test_state_t t_flash_crc(const detect_result_t *d, char *detail,
                                   unsigned len, test_progress_fn p) {
    (void)d;
    if (p) p(200, NULL);
    uint32_t a = mem_test_flash_crc(64 * 1024);
    if (p) p(700, NULL);
    uint32_t b = mem_test_flash_crc(64 * 1024);
    if (p) p(1000, NULL);

    if (a != b) {
        snprintf(detail, len, "%08X vs %08X", (unsigned)a, (unsigned)b);
        return TEST_FAIL;
    }
    snprintf(detail, len, "%08X", (unsigned)a);
    return TEST_PASS;
}

static ui_test_state_t t_psram_probe(const detect_result_t *d, char *detail,
                                     unsigned len, test_progress_fn p) {
    (void)p;
    if (d->psram_cs_found == PIN_NC) {
        snprintf(detail, len, "no response");
        return TEST_FAIL;
    }
    char sz[16];
    fmt_size(sz, sizeof(sz), d->psram_bytes);
    snprintf(detail, len, "%s on GP%d", sz, (int)d->psram_cs_found);

    if (d->board && d->board->psram_bytes &&
        d->board->psram_bytes != d->psram_bytes)
        return TEST_FAIL;

    return TEST_PASS;
}

/* Full write-and-verify sweep. Slow — seconds, for 8 MB — which is why
 * it reports progress. */
/* Also from the boot-time benchmark, for the same reason as flash read —
 * and additionally because a full 8 MB write-and-verify takes seconds,
 * which is a long time to hold the screen still. */
static ui_test_state_t t_psram_sweep(const detect_result_t *d, char *detail,
                                     unsigned len, test_progress_fn p) {
    (void)p;
    if (!d->psram_sweep_run) {
        snprintf(detail, len, "not measured");
        return TEST_NORUN;
    }

    if (!d->psram_sweep_ok || d->psram_byte_errors) {
        snprintf(detail, len, "%u byte errors",
                 (unsigned)d->psram_byte_errors);
        return TEST_FAIL;
    }

    char w[16], r[16];
    fmt_rate(w, sizeof(w), d->psram_write_kbps);
    fmt_rate(r, sizeof(r), d->psram_read_kbps);
    snprintf(detail, len, "w %s  r %s", w, r);
    return TEST_PASS;
}

/* ------------------------------------------------------------------ */

const frank_test_t frank_tests_memory[] = {
    { "Silicon",     ICON_CHIP,  0, 0, t_silicon    },
    { "Flash ID",    ICON_FLASH, 0, 0, t_flash_id   },
    { "Flash read",  ICON_FLASH, 0, 0, t_flash_read },
    { "Flash CRC32", ICON_FLASH, 0, 0, t_flash_crc  },
    { "QSPI PSRAM",       ICON_RAM, CAP_PSRAM_QMI, 0, t_psram_probe },
    { "QSPI PSRAM sweep", ICON_RAM, CAP_PSRAM_QMI, 0, t_psram_sweep },
};

const unsigned frank_tests_memory_len =
    sizeof(frank_tests_memory) / sizeof(frank_tests_memory[0]);
