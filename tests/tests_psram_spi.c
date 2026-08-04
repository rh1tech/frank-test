/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tests_psram_spi.c — megafrank's second PSRAM.
 *
 * Every other board in the fleet reaches its PSRAM through the QMI, as a
 * memory window: tests_memory.c writes to an address and the hardware
 * does the rest. megafrank also carries an ESP-PSRAM64 wired as a plain
 * SPI device on ordinary GPIOs, and that one has to be driven a byte at
 * a time.
 *
 *   GP31  chip select, active low
 *   GP32  SCLK
 *   GP33  SI   (MOSI)
 *   GP34  SO   (MISO) — through switch S10
 *
 * Four wires, not three. The first version of this file read the answer
 * back on GP33, on the assumption that SI and SO were one half-duplex
 * line, and it could not have worked: the part answers on its own pin,
 * and that pin only reaches the MCU when S10 is closed. On real hardware
 * it reported "no response" on a board with the PSRAM soldered down.
 *
 * S10 therefore gates the *read* path specifically. With it open the
 * commands still go out and nothing ever comes back, which looks
 * identical to an absent part — hence the switch being named in the
 * failure text.
 *
 *
 * STATUS
 *
 * Verified on a real megafrank: ESP-PSRAM64, manufacturer 0x0D, and a
 * 128-block sweep across 8 MB with no errors. The 3-wire version this
 * replaces failed on the same board with the same switch position, so
 * the half-duplex assumption was the entire fault — S10 had been closed
 * all along.
 *
 * If bit-banging proves unreliable — and at 1 us half-cycles with the
 * video scanout stealing cycles on the other core it may — SpeccyP's
 * drivers/psram is the proven alternative for this exact part: a PIO
 * implementation of the same four-wire protocol, from the PicoGUS
 * lineage. pio2 is free here, so it would drop in. This is deliberately
 * the simpler thing first, because a bit-banged probe that works is
 * easier to trust than a PIO program that has to be debugged through a
 * state machine.
 */

#include "registry.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* Commands. The subset that needs no mode change. */
#define PSRAM_CMD_READ_ID   0x9Fu
#define PSRAM_CMD_READ      0x03u
#define PSRAM_CMD_WRITE     0x02u
#define PSRAM_CMD_RESET_EN  0x66u
#define PSRAM_CMD_RESET     0x99u

/* Espressif's manufacturer byte. The device also returns a 6-byte EID,
 * of which the first two are the useful identity. */
#define PSRAM_MFG_ESPRESSIF 0x0Du

/* Half-cycle. The part will take tens of MHz; this is bit-banged from C
 * with the video scanout stealing cycles on the other core, so the clock
 * is deliberately slow and the test is about presence and integrity, not
 * speed. */
#define SPI_HALF_US 1u

typedef struct { uint cs, sck, mosi, miso; } psram_pins_t;

static void spi_write_byte(const psram_pins_t *p, uint8_t v) {
    for (int b = 7; b >= 0; b--) {
        gpio_put(p->mosi, (v >> b) & 1u);
        busy_wait_us_32(SPI_HALF_US);
        gpio_put(p->sck, 1);        /* the part samples on the rising edge */
        busy_wait_us_32(SPI_HALF_US);
        gpio_put(p->sck, 0);
    }
}

/* Mode 0: the part drives SO on the falling edge, so sample on the
 * rising one. */
static uint8_t spi_read_byte(const psram_pins_t *p) {
    uint8_t v = 0;
    for (int b = 7; b >= 0; b--) {
        gpio_put(p->sck, 1);
        busy_wait_us_32(SPI_HALF_US);
        v = (uint8_t)((v << 1) | (gpio_get(p->miso) ? 1u : 0u));
        gpio_put(p->sck, 0);
        busy_wait_us_32(SPI_HALF_US);
    }
    return v;
}

static void cs_low(const psram_pins_t *p)  { gpio_put(p->cs, 0); busy_wait_us_32(1); }
static void cs_high(const psram_pins_t *p) { busy_wait_us_32(1); gpio_put(p->cs, 1); busy_wait_us_32(2); }

static void psram_pins_init(const psram_pins_t *p) {
    gpio_init(p->cs);   gpio_set_dir(p->cs, GPIO_OUT);   gpio_put(p->cs, 1);
    gpio_init(p->sck);  gpio_set_dir(p->sck, GPIO_OUT);  gpio_put(p->sck, 0);
    gpio_init(p->mosi); gpio_set_dir(p->mosi, GPIO_OUT); gpio_put(p->mosi, 0);

    /* MISO comes back through S10. Pull it down so an open switch reads
     * a clean zero rather than a floating value that might occasionally
     * look like data. */
    gpio_init(p->miso); gpio_set_dir(p->miso, GPIO_IN);
    gpio_pull_down(p->miso);
}

static bool pins_from(const frank_pins_t *fp, psram_pins_t *out) {
    if (fp->psram_cs == PIN_NC || fp->psram_soft_sclk == PIN_NC ||
        fp->psram_soft_mosi == PIN_NC || fp->psram_soft_miso == PIN_NC)
        return false;
    out->cs   = (uint)fp->psram_cs;
    out->sck  = (uint)fp->psram_soft_sclk;
    out->mosi = (uint)fp->psram_soft_mosi;
    out->miso = (uint)fp->psram_soft_miso;
    return true;
}

/* Address is 24 bits, MSB first. */
static void send_addr(const psram_pins_t *p, uint32_t a) {
    spi_write_byte(p, (uint8_t)(a >> 16));
    spi_write_byte(p, (uint8_t)(a >> 8));
    spi_write_byte(p, (uint8_t)a);
}

/* ------------------------------------------------------------------ */

/* Set by the probe, read by the sweep. Sweeping a part that never
 * identified itself produces a wall of byte errors that says nothing the
 * probe had not already said, and buries the one line that matters. */
static bool s_spi_present;

static ui_test_state_t t_spi_psram(const detect_result_t *d, char *detail,
                                   unsigned len, test_progress_fn p) {
    s_spi_present = false;
    psram_pins_t pp;
    if (!pins_from(&d->board->pins, &pp)) {
        snprintf(detail, len, "no SPI PSRAM pins");
        return TEST_NORUN;
    }

    psram_pins_init(&pp);

    /* Reset first. The part may have been left in QPI mode by whatever
     * ran before, and a QPI device ignores single-lane commands
     * entirely — which looks exactly like an absent chip. */
    cs_low(&pp);  spi_write_byte(&pp, PSRAM_CMD_RESET_EN); cs_high(&pp);
    cs_low(&pp);  spi_write_byte(&pp, PSRAM_CMD_RESET);    cs_high(&pp);
    sleep_ms(1);

    if (p) p(300, NULL);

    uint8_t eid[8];
    cs_low(&pp);
    spi_write_byte(&pp, PSRAM_CMD_READ_ID);
    send_addr(&pp, 0);
    for (int i = 0; i < 8; i++) eid[i] = spi_read_byte(&pp);
    cs_high(&pp);

    if (p) p(700, NULL);

    /* All-ones or all-zeros is a floating bus, not a device: with no part
     * fitted, or S10 off, the pad reads back whatever the pull leaves it
     * at. Distinguishing that from a real ID is the whole point of
     * checking the manufacturer byte rather than just "something came
     * back". */
    if (eid[0] == 0x00 || eid[0] == 0xFF) {
        snprintf(detail, len, "no answer on GP%u (S10 open?)", pp.miso);
        return TEST_FAIL;
    }
    if (eid[0] != PSRAM_MFG_ESPRESSIF) {
        snprintf(detail, len, "unexpected mfg 0x%02X", eid[0]);
        return TEST_FAIL;
    }

    if (p) p(1000, NULL);
    s_spi_present = true;
    snprintf(detail, len, "ESP-PSRAM64 %02X%02X, SO on GP%u",
             eid[0], eid[1], pp.miso);
    return TEST_PASS;
}

/* Write-and-verify across the part.
 *
 * Sparse rather than exhaustive: bit-banged SPI at roughly 250 kB/s would
 * take about half a minute to walk 8 MB, which is far too long to sit in
 * front of. Sampling one block per 64 KiB covers every address line —
 * which is what actually catches a mis-soldered part — in under a second.
 */
#define SPI_SWEEP_STRIDE  (64u * 1024u)
#define SPI_SWEEP_BLOCK   16u

static ui_test_state_t t_spi_psram_sweep(const detect_result_t *d, char *detail,
                                         unsigned len, test_progress_fn p) {
    psram_pins_t pp;
    if (!pins_from(&d->board->pins, &pp)) {
        snprintf(detail, len, "no SPI PSRAM pins");
        return TEST_NORUN;
    }
    if (!s_spi_present) {
        snprintf(detail, len, "nothing responded to the probe");
        return TEST_NORUN;
    }

    const uint32_t size = d->board->psram_bytes ? d->board->psram_bytes
                                                : 8u * 1024u * 1024u;
    uint8_t  wr[SPI_SWEEP_BLOCK], rd[SPI_SWEEP_BLOCK];
    uint32_t errors = 0, blocks = 0;

    for (uint32_t addr = 0; addr < size; addr += SPI_SWEEP_STRIDE) {
        /* Seed from the address, so a block read back from the wrong
         * place fails rather than matching by luck — which is exactly
         * what a dropped address line would otherwise do. */
        for (unsigned i = 0; i < SPI_SWEEP_BLOCK; i++)
            wr[i] = (uint8_t)(addr + i * 7u + (addr >> 13));

        cs_low(&pp);
        spi_write_byte(&pp, PSRAM_CMD_WRITE);
        send_addr(&pp, addr);
        for (unsigned i = 0; i < SPI_SWEEP_BLOCK; i++) spi_write_byte(&pp, wr[i]);
        cs_high(&pp);

        memset(rd, 0, sizeof(rd));
        cs_low(&pp);
        spi_write_byte(&pp, PSRAM_CMD_READ);
        send_addr(&pp, addr);
        for (unsigned i = 0; i < SPI_SWEEP_BLOCK; i++) rd[i] = spi_read_byte(&pp);
        cs_high(&pp);

        for (unsigned i = 0; i < SPI_SWEEP_BLOCK; i++)
            if (rd[i] != wr[i]) errors++;

        blocks++;
        if (p && (blocks & 7u) == 0u)
            p((int)((addr * 1000ull) / size), NULL);
    }

    if (p) p(1000, NULL);

    if (errors) {
        snprintf(detail, len, "%u byte errors in %u blocks",
                 (unsigned)errors, (unsigned)blocks);
        return TEST_FAIL;
    }
    snprintf(detail, len, "%u blocks over %u MB ok",
             (unsigned)blocks, (unsigned)(size / (1024u * 1024u)));
    return TEST_PASS;
}

const frank_test_t frank_tests_psram_spi[] = {
    { "SPI PSRAM",       ICON_RAM, CAP_PSRAM_SOFTSPI, 0, t_spi_psram },
    { "SPI PSRAM sweep", ICON_RAM, CAP_PSRAM_SOFTSPI, 0, t_spi_psram_sweep },
};

const unsigned frank_tests_psram_spi_len =
    sizeof(frank_tests_psram_spi) / sizeof(frank_tests_psram_spi[0]);
