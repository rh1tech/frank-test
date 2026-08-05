/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tests_sd.c — the microSD socket.
 *
 *
 * WHAT IS BEING TESTED
 *
 * Two things, in two rows, because they fail for different reasons and
 * conflating them would waste the distinction:
 *
 *   SD card    the card answers CMD0/CMD8/ACMD41 and hands back its CID
 *              and CSD. This proves the command channel: CS, CLK and both
 *              data directions all work, and the card is alive. The
 *              manufacturer, product name, revision, date and capacity
 *              all come out of those two registers.
 *   SD read    sector 0 comes back and carries a boot signature. This
 *              proves bulk data transfer, which the register reads do not
 *              — a 16-byte reply can succeed on a link that falls apart
 *              over 512.
 *
 *
 * WHY IT IS BIT-BANGED
 *
 * The same reasoning as tests_psram_spi.c. This is a detection test, not
 * a filesystem: it moves about forty bytes and then stops, so the clock
 * rate is irrelevant, and a bit-banged probe that works is easier to
 * trust than a PIO program that has to be debugged through a state
 * machine. All three PIOs are also spoken for — link, I2S, gamepads.
 *
 * Deliberately no FatFs. Whether a card holds a filesystem this firmware
 * recognises says nothing about whether the *board* works, and pulling
 * in a filesystem to find out would be a large dependency answering the
 * wrong question. Sector 0 and its signature are as far as the hardware
 * question goes.
 *
 *
 * PROVENANCE
 *
 * The CID and CSD decode, and the manufacturer table, are ported from
 * SpeccyP by Constantin (billgilbert7000),
 * https://github.com/billgilbert7000/SpeccyP — drivers/pico_fatfs/tf_card.c — decode_cid_register(),
 * decode_csd_register() and manufacturer_map[]. One difference:
 * SpeccyP's get_manufacturer_name() returns NULL for an unknown ID and
 * the caller strncpy()s it, which would fault on the first unrecognised
 * card. Here an unknown ID prints as a number, which is also more useful
 * than "Unknown" — it is the thing you would look up.
 */

#include "registry.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* Commands, in the SPI-mode numbering. */
#define CMD0    0        /* GO_IDLE_STATE      */
#define CMD8    8        /* SEND_IF_COND       */
#define CMD9    9        /* SEND_CSD           */
#define CMD10   10       /* SEND_CID           */
#define CMD17   17       /* READ_SINGLE_BLOCK  */
#define CMD24   24       /* WRITE_BLOCK        */
#define CMD55   55       /* APP_CMD            */
#define CMD58   58       /* READ_OCR           */
#define ACMD41  41       /* SD_SEND_OP_COND    */

/* Only two commands need a real CRC, and only before the card leaves
 * idle: CMD0 and CMD8. After that CRC checking is off in SPI mode, so
 * everything else ships a dummy. */
#define CRC_CMD0  0x95u
#define CRC_CMD8  0x87u
#define CRC_DUMMY 0x01u

#define R1_IDLE   0x01u
#define TOKEN_BLK 0xFEu

/* Half-cycle. SD cards must be initialised at 400 kHz or less; this
 * stays at roughly 250 kHz throughout because the whole exchange is
 * under six hundred bytes and going faster would buy nothing. */
#define SD_HALF_US 2u

typedef struct { uint cs, clk, mosi, miso; } sd_pins_t;

/* ------------------------------------------------------------------ */
/* Wire                                                                */
/* ------------------------------------------------------------------ */

/* Full duplex: SPI always shifts both ways, and the SD protocol relies
 * on it — a response is read by clocking out 0xFF. */
static uint8_t sd_xfer(const sd_pins_t *p, uint8_t out) {
    uint8_t in = 0;
    for (int b = 7; b >= 0; b--) {
        gpio_put(p->mosi, (out >> b) & 1u);
        busy_wait_us_32(SD_HALF_US);
        gpio_put(p->clk, 1);
        in = (uint8_t)((in << 1) | (gpio_get(p->miso) ? 1u : 0u));
        busy_wait_us_32(SD_HALF_US);
        gpio_put(p->clk, 0);
    }
    return in;
}

static void sd_select(const sd_pins_t *p)   { gpio_put(p->cs, 0); sd_xfer(p, 0xFF); }
static void sd_deselect(const sd_pins_t *p) { gpio_put(p->cs, 1); sd_xfer(p, 0xFF); }

static void sd_pins_init(const sd_pins_t *p) {
    gpio_init(p->cs);   gpio_set_dir(p->cs,   GPIO_OUT); gpio_put(p->cs, 1);
    gpio_init(p->clk);  gpio_set_dir(p->clk,  GPIO_OUT); gpio_put(p->clk, 0);
    gpio_init(p->mosi); gpio_set_dir(p->mosi, GPIO_OUT); gpio_put(p->mosi, 1);

    /* Pull up the data line so an empty socket reads as a steady 0xFF
     * rather than as noise that might occasionally look like a response.
     * Pull-up rather than down for the same reason as everywhere else in
     * this firmware — the RP2350-E9 erratum. */
    gpio_init(p->miso); gpio_set_dir(p->miso, GPIO_IN); gpio_pull_up(p->miso);
}

/* Send a command and return R1. 0xFF means the card never answered. */
static uint8_t sd_cmd(const sd_pins_t *p, uint8_t cmd, uint32_t arg, uint8_t crc) {
    sd_xfer(p, (uint8_t)(0x40u | cmd));
    sd_xfer(p, (uint8_t)(arg >> 24));
    sd_xfer(p, (uint8_t)(arg >> 16));
    sd_xfer(p, (uint8_t)(arg >> 8));
    sd_xfer(p, (uint8_t)arg);
    sd_xfer(p, crc);

    /* The card may take up to eight bytes to respond, and CMD12 needs a
     * stuff byte first — which this never sends, so eight is the bound. */
    for (int i = 0; i < 10; i++) {
        const uint8_t r = sd_xfer(p, 0xFF);
        if (!(r & 0x80u)) return r;
    }
    return 0xFF;
}

static uint8_t sd_acmd(const sd_pins_t *p, uint8_t cmd, uint32_t arg) {
    sd_cmd(p, CMD55, 0, CRC_DUMMY);
    return sd_cmd(p, cmd, arg, CRC_DUMMY);
}

/* Wait for a data token and take `len` bytes plus the two CRC bytes. */
static bool sd_rx_block(const sd_pins_t *p, uint8_t *buf, unsigned len) {
    absolute_time_t deadline = make_timeout_time_ms(300);
    uint8_t t;
    do {
        t = sd_xfer(p, 0xFF);
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) return false;
    } while (t == 0xFF);

    if (t != TOKEN_BLK) return false;

    for (unsigned i = 0; i < len; i++) buf[i] = sd_xfer(p, 0xFF);
    sd_xfer(p, 0xFF);          /* CRC, discarded — SPI mode does not check */
    sd_xfer(p, 0xFF);
    return true;
}

/* ------------------------------------------------------------------ */
/* Registers                                                           */
/* ------------------------------------------------------------------ */

/* From SpeccyP's manufacturer_map[]. These are SD Association-assigned
 * MID values; the list is not exhaustive and does not need to be, since
 * an unrecognised one still prints as a number. */
static const struct { uint8_t id; const char *name; } mfg[] = {
    { 0x01, "Panasonic" }, { 0x02, "Toshiba"    }, { 0x03, "SanDisk"   },
    { 0x08, "SP"        }, { 0x18, "Infineon"   }, { 0x1B, "Samsung"   },
    { 0x1D, "AData"     }, { 0x27, "Phison"     }, { 0x28, "Lexar"     },
    { 0x30, "SanDisk"   }, { 0x31, "SP"         }, { 0x33, "STM"       },
    { 0x41, "Kingston"  }, { 0x6F, "STM"        }, { 0x70, "Kingston"  },
    { 0x76, "Patriot"   }, { 0x82, "Sony"       }, { 0x83, "Team Group"},
    { 0x88, "Team Group"}, { 0x89, "Team Group" }, { 0x9C, "Angelbird" },
    { 0x9E, "WD"        }, { 0xAD, "Transcend"  },
};

static const char *mfg_name(uint8_t id) {
    for (unsigned i = 0; i < count_of(mfg); i++)
        if (mfg[i].id == id) return mfg[i].name;
    return NULL;
}

/* One pool of sector buffers for every test here.
 *
 * They had a static 512 bytes each and there are five of them, which is
 * two and a half kilobytes of RAM held permanently for tests that run
 * one at a time. Nothing in this file is re-entrant and nothing runs
 * concurrently with anything else, so they share. */
static uint8_t s_scratch[3][512];
static uint8_t *sd_scratch(unsigned i) { return s_scratch[i % 3u]; }

/* What the probe found, kept for the read test and for the log. */
static struct {
    bool     present;
    bool     block_addressed;   /* SDHC/SDXC address in sectors, not bytes */
    char     type[8];
    char     name[8];           /* 5 ASCII characters from the CID        */
    uint8_t  mid;
    uint8_t  rev;
    uint32_t serial;
    unsigned year, month;
    uint64_t bytes;
} s_card;

/* CSD, both structure versions. v1 encodes capacity as a size and a
 * multiplier; v2 as a straight count of 512 KB units. */
static uint64_t csd_capacity(const uint8_t *csd) {
    const unsigned ver = (csd[0] >> 6) & 0x03u;

    if (ver == 0) {
        const uint32_t c_size = ((uint32_t)(csd[6] & 0x03u) << 10)
                              | ((uint32_t)csd[7] << 2)
                              | ((csd[8] >> 6) & 0x03u);
        const uint32_t c_mult = ((uint32_t)(csd[9] & 0x03u) << 1)
                              | ((csd[10] >> 7) & 0x01u);
        const uint32_t blk    = 1u << (csd[5] & 0x0Fu);
        return (uint64_t)(c_size + 1u) * (1u << (c_mult + 2u)) * blk;
    }
    if (ver == 1) {
        const uint32_t c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16)
                              | ((uint32_t)csd[8] << 8)
                              | csd[9];
        return (uint64_t)(c_size + 1u) * 512u * 1024u;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Bring-up                                                            */
/* ------------------------------------------------------------------ */

static bool pins_from(const frank_pins_t *fp, sd_pins_t *out) {
    /* In SPI mode CMD is the card's input and DAT0 its output — the
     * names come from the native 4-bit interface these boards do not
     * use. Getting them the wrong way round gives a socket that never
     * answers, which is indistinguishable from an empty one. */
    if (fp->sd_cs == PIN_NC || fp->sd_clk == PIN_NC ||
        fp->sd_cmd == PIN_NC || fp->sd_dat0 == PIN_NC)
        return false;
    out->cs   = (uint)fp->sd_cs;
    out->clk  = (uint)fp->sd_clk;
    out->mosi = (uint)fp->sd_cmd;
    out->miso = (uint)fp->sd_dat0;
    return true;
}

static bool sd_bring_up(const sd_pins_t *p, const char **why) {
    sd_pins_init(p);

    /* At least 74 clocks with CS high, which is how a card is told to
     * enter SPI mode rather than the native one. */
    gpio_put(p->cs, 1);
    for (int i = 0; i < 12; i++) sd_xfer(p, 0xFF);

    sd_select(p);

    /* CMD0 can need a few attempts: a card that was mid-operation when
     * the board reset has to finish before it will go idle. */
    bool idle = false;
    for (int i = 0; i < 8 && !idle; i++)
        idle = (sd_cmd(p, CMD0, 0, CRC_CMD0) == R1_IDLE);

    if (!idle) {
        sd_deselect(p);
        *why = "no card (CMD0 unanswered)";
        return false;
    }

    /* CMD8 separates v2 cards from v1. A v2 card echoes the check
     * pattern back in the last byte of its R7. */
    bool v2 = false;
    if (sd_cmd(p, CMD8, 0x1AAu, CRC_CMD8) == R1_IDLE) {
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) r7[i] = sd_xfer(p, 0xFF);
        v2 = (r7[2] == 0x01u && r7[3] == 0xAAu);
        if (!v2) {
            sd_deselect(p);
            *why = "CMD8 voltage mismatch";
            return false;
        }
    }

    /* ACMD41 until the card leaves idle. HCS asks for high capacity,
     * which a v1 card ignores. */
    const uint32_t hcs = v2 ? 0x40000000u : 0;
    absolute_time_t deadline = make_timeout_time_ms(1000);
    uint8_t r;
    do {
        r = sd_acmd(p, ACMD41, hcs);
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) {
            sd_deselect(p);
            *why = "card never left idle (ACMD41)";
            return false;
        }
    } while (r == R1_IDLE);

    if (r) {
        sd_deselect(p);
        *why = "ACMD41 error";
        return false;
    }

    /* CCS in the OCR says whether addresses are sectors or bytes. This
     * matters for the read test, and getting it wrong on a >2 GB card
     * reads the wrong place rather than failing outright. */
    s_card.block_addressed = false;
    if (v2 && sd_cmd(p, CMD58, 0, CRC_DUMMY) == 0) {
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = sd_xfer(p, 0xFF);
        s_card.block_addressed = (ocr[0] & 0x40u) != 0;
    }

    snprintf(s_card.type, sizeof(s_card.type), "%s",
             !v2                     ? "SDSC"
           : s_card.block_addressed  ? "SDHC"
                                     : "SDSCv2");
    return true;
}

/* ------------------------------------------------------------------ */
/* The rows                                                            */
/* ------------------------------------------------------------------ */

static ui_test_state_t t_sd(const detect_result_t *d, char *detail,
                            unsigned len, test_progress_fn p) {
    memset(&s_card, 0, sizeof(s_card));

    sd_pins_t sp;
    if (!pins_from(&d->board->pins, &sp)) {
        snprintf(detail, len, "no SD pins");
        return TEST_NORUN;
    }

    const char *why = "unknown";
    if (!sd_bring_up(&sp, &why)) {
        snprintf(detail, len, "%s", why);
        return TEST_FAIL;
    }
    if (p) p(400, NULL);

    uint8_t cid[16], csd[16];

    if (sd_cmd(&sp, CMD10, 0, CRC_DUMMY) != 0 || !sd_rx_block(&sp, cid, 16)) {
        sd_deselect(&sp);
        snprintf(detail, len, "%s, no CID", s_card.type);
        return TEST_FAIL;
    }
    if (p) p(700, NULL);

    if (sd_cmd(&sp, CMD9, 0, CRC_DUMMY) != 0 || !sd_rx_block(&sp, csd, 16)) {
        sd_deselect(&sp);
        snprintf(detail, len, "%s, no CSD", s_card.type);
        return TEST_FAIL;
    }
    sd_deselect(&sp);

    /* CID layout: [0] manufacturer, [1-2] OEM, [3-7] product name,
     * [8] revision, [9-12] serial, [13-14] date. */
    s_card.mid = cid[0];
    for (int i = 0; i < 5; i++) {
        const char c = (char)cid[3 + i];
        s_card.name[i] = (c >= 0x20 && c < 0x7F) ? c : ' ';
    }
    s_card.name[5] = 0;
    s_card.rev    = cid[8];
    s_card.serial = ((uint32_t)cid[9] << 24) | ((uint32_t)cid[10] << 16)
                  | ((uint32_t)cid[11] << 8) | cid[12];
    s_card.year   = 2000u + (unsigned)(((cid[13] & 0x0Fu) << 4) | (cid[14] >> 4));
    s_card.month  = cid[14] & 0x0Fu;

    s_card.bytes   = csd_capacity(csd);
    s_card.present = true;

    if (p) p(1000, NULL);

    /* The console gets everything; the row gets what fits. */
    const char *m = mfg_name(s_card.mid);
    printf("  SD: %s%s%s '%s' rev %u.%u  serial %08X  %u/%02u  %s  %llu MB\n",
           m ? m : "MID 0x", m ? "" : "", m ? "" : "",
           s_card.name,
           (unsigned)(s_card.rev >> 4), (unsigned)(s_card.rev & 0x0Fu),
           (unsigned)s_card.serial, s_card.year, s_card.month,
           s_card.type,
           (unsigned long long)(s_card.bytes / (1024ull * 1024ull)));

    const unsigned long long mb = s_card.bytes / (1024ull * 1024ull);
    if (m)
        snprintf(detail, len, "%s %s %s %llu MB", m, s_card.name, s_card.type,
                 mb >= 1024ull ? mb / 1024ull : mb);
    else
        snprintf(detail, len, "MID %02X %s %s %llu MB", s_card.mid, s_card.name,
                 s_card.type, mb >= 1024ull ? mb / 1024ull : mb);

    /* Capacities are quoted in GB once past a gigabyte, which is how
     * cards are sold and how the operator will recognise the one in
     * their hand. */
    if (mb >= 1024ull) {
        const unsigned n = (unsigned)strlen(detail);
        if (n >= 2 && n + 1 < len) {
            detail[n - 2] = 'G';
            detail[n - 1] = 'B';
        }
    }
    return TEST_PASS;
}

/* Sector 0, which on any card that has ever been formatted carries the
 * 0xAA55 signature at offset 510 — MBR or FAT boot sector alike.
 *
 * A card can hand over its CID and CSD and still fail here: those are
 * sixteen bytes each and this is five hundred and twelve, over a link
 * whose marginal cases show up with length. That is the whole reason
 * this is a separate row. */
static ui_test_state_t t_sd_read(const detect_result_t *d, char *detail,
                                 unsigned len, test_progress_fn p) {
    sd_pins_t sp;
    if (!pins_from(&d->board->pins, &sp)) {
        snprintf(detail, len, "no SD pins");
        return TEST_NORUN;
    }
    if (!s_card.present) {
        snprintf(detail, len, "no card identified");
        return TEST_NORUN;
    }

    uint8_t *sec = sd_scratch(0);
    sd_select(&sp);

    /* Byte address on SDSC, sector address on SDHC and SDXC. Sector 0
     * is address 0 either way, which is why this test can afford to be
     * this simple — but the flag is set correctly regardless, because
     * getting it wrong is a silent wrong-place read rather than an
     * error. */
    if (sd_cmd(&sp, CMD17, 0, CRC_DUMMY) != 0 || !sd_rx_block(&sp, sec, 512)) {
        sd_deselect(&sp);
        snprintf(detail, len, "CMD17 failed");
        return TEST_FAIL;
    }
    sd_deselect(&sp);
    if (p) p(1000, NULL);

    if (sec[510] == 0x55u && sec[511] == 0xAAu) {
        snprintf(detail, len, "512 B, signature ok");
        return TEST_PASS;
    }

    /* Data came back, so the transfer worked; the card simply has no
     * signature there. An unformatted or zeroed card is not a board
     * fault, and calling it one would send someone looking for a defect
     * that is not there. */
    snprintf(detail, len, "512 B, no signature (unformatted?)");
    return TEST_NORUN;
}


/* ------------------------------------------------------------------ */
/* Throughput                                                          */
/* ------------------------------------------------------------------ */

/* Read speed, over the hardware SPI rather than the bit-banged one.
 *
 * The rest of this file bangs the bus by hand, deliberately - a probe
 * that moves forty bytes has no use for a peripheral, and a bit-banged
 * one cannot leave a half-configured SPI block behind it. But timing
 * that path would measure this firmware's inner loop and call it a card,
 * which is not a number anyone wants.
 *
 * So this hands the same four pins to the SPI block for the duration and
 * gives them back. Every board wires the socket to a pin quartet that
 * maps onto an instance - RX, CSn, SCK, TX in that order within a group
 * of eight - and the mapping is checked rather than assumed, because a
 * board that broke the pattern would otherwise get a confident number
 * off pins that are not connected to anything.
 *
 * CMD18 rather than a loop of CMD17: a multi-block read is one command
 * and then data until told to stop, so the figure is the transfer rather
 * than the per-command overhead. Chip select is left as plain GPIO and
 * driven by hand, because the block's own CS deasserts between transfers
 * and an SD card treats that as the end of the read.
 */
#define SPEED_SECTORS 1024u          /* 512 KiB, about a second at 4 MB/s */
#define SPI_HZ        12500000u      /* well inside the 25 MHz SPI-mode ceiling */

static spi_inst_t *spi_for(const sd_pins_t *p) {
    /* Within each group of eight GPIOs the SPI functions run RX, CSn,
     * SCK, TX and then repeat; alternate groups belong to alternate
     * instances. */
    if ((p->miso & 3u) != 0u || (p->cs & 3u) != 1u ||
        (p->clk  & 3u) != 2u || (p->mosi & 3u) != 3u) return NULL;
    if ((p->miso / 8u) != (p->clk / 8u) || (p->clk / 8u) != (p->mosi / 8u))
        return NULL;

    return ((p->clk / 8u) & 1u) ? spi1 : spi0;
}

static ui_test_state_t t_sd_speed(const detect_result_t *d, char *detail,
                                  unsigned len, test_progress_fn p) {
    sd_pins_t sp;
    if (!pins_from(&d->board->pins, &sp)) {
        snprintf(detail, len, "no SD pins");
        return TEST_NORUN;
    }
    if (!s_card.present) {
        snprintf(detail, len, "no card identified");
        return TEST_NORUN;
    }

    spi_inst_t *spi = spi_for(&sp);
    if (!spi) {
        snprintf(detail, len, "pins are not an SPI quartet");
        return TEST_NORUN;
    }

    /* CS stays a GPIO: the block would drop it between transfers and the
     * card would end the read. */
    const unsigned hz = spi_init(spi, SPI_HZ);
    spi_set_format(spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(sp.clk,  GPIO_FUNC_SPI);
    gpio_set_function(sp.mosi, GPIO_FUNC_SPI);
    gpio_set_function(sp.miso, GPIO_FUNC_SPI);

    uint8_t *buf = sd_scratch(0);
    unsigned  sectors = 0;
    bool      ok      = true;

    gpio_put(sp.cs, 0);
    spi_write_blocking(spi, (const uint8_t[]){0xFF}, 1);

    /* CMD18, from sector zero. Byte address on the older cards. */
    const uint32_t arg = s_card.block_addressed ? 0u : 0u;
    const uint8_t  cmd[6] = { 0x40u | 18u,
                              (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
                              (uint8_t)(arg >> 8),  (uint8_t)arg, 0x01u };
    spi_write_blocking(spi, cmd, sizeof(cmd));

    uint8_t r = 0xFF;
    for (int i = 0; i < 16 && r == 0xFF; i++) spi_read_blocking(spi, 0xFF, &r, 1);
    if (r != 0x00) ok = false;

    const absolute_time_t t0 = get_absolute_time();

    while (ok && sectors < SPEED_SECTORS) {
        /* Wait for the data token that opens each block. */
        uint8_t tok = 0xFF;
        for (int i = 0; i < 4000 && tok == 0xFF; i++)
            spi_read_blocking(spi, 0xFF, &tok, 1);
        if (tok != 0xFE) { ok = false; break; }

        spi_read_blocking(spi, 0xFF, buf, sizeof(buf));
        uint8_t crc[2];
        spi_read_blocking(spi, 0xFF, crc, sizeof(crc));

        sectors++;
        if (p && (sectors % 64u) == 0u)
            p((int)((sectors * 1000u) / SPEED_SECTORS), NULL);
    }

    const int64_t us = absolute_time_diff_us(t0, get_absolute_time());

    /* CMD12 stops the stream. Without it the card keeps sending and the
     * next command lands in the middle of a block. */
    const uint8_t stop[6] = { 0x40u | 12u, 0, 0, 0, 0, 0x01u };
    spi_write_blocking(spi, stop, sizeof(stop));
    for (int i = 0; i < 16; i++) { uint8_t x; spi_read_blocking(spi, 0xFF, &x, 1); }
    gpio_put(sp.cs, 1);

    /* Hand the pins back, so a re-run of the tests above finds them as
     * they expect and not driven by a peripheral. */
    spi_deinit(spi);
    sd_pins_init(&sp);

    if (p) p(1000, NULL);

    if (!ok || sectors == 0 || us <= 0) {
        snprintf(detail, len, "read stalled after %u sectors", sectors);
        return TEST_FAIL;
    }

    /* KiB/s in integer arithmetic, then shown as MiB/s to two places. */
    const uint32_t kibps = (uint32_t)(((uint64_t)sectors * 512u * 1000000u) /
                                      ((uint64_t)us * 1024u));
    snprintf(detail, len, "%lu.%02lu MiB/s at %u MHz",
             (unsigned long)(kibps / 1024u),
             (unsigned long)(((kibps % 1024u) * 100u) / 1024u),
             hz / 1000000u);
    return TEST_PASS;
}

/* The write path, on a sector the card is not using for anything.
 *
 * Everything above this point reads. A card can answer every register,
 * hand over its CID and stream half a megabyte and still fail to take a
 * byte, because the direction that was never exercised is the one that
 * is broken - a cold joint on CMD, a card that has worn its blocks out,
 * a socket whose write-protect notch is shorting.
 *
 *
 * WHY THIS DOES NOT DESTROY ANYTHING
 *
 * It writes to the last sector of the card and puts back exactly what
 * was there. Read, write a pattern, read it back, compare, restore, read
 * once more to confirm the restore took. If any step fails the original
 * is written back anyway before the row reports.
 *
 * The last sector rather than sector zero: a filesystem's boot sector is
 * the one place where a failed restore is unrecoverable, and the tail of
 * a card is almost always slack space in the last cluster. "Almost
 * always" is doing work in that sentence, which is why the restore is
 * verified rather than assumed - if the read-back after restore does not
 * match, the row says so in as many words rather than passing quietly.
 *
 * There is no consent dialog. The roadmap wanted one, on the grounds
 * that writing is destructive; it is not destructive if the original
 * goes back and the restore is checked, and a prompt nobody can answer
 * usefully - "may I write to a sector you cannot see?" - is a worse
 * design than doing the safe thing and saying what was done.
 */
#define WRITE_PATTERN_A 0xA5u
#define WRITE_PATTERN_B 0x5Au

/* Send one block and wait for the card to accept and finish it. */
static bool sd_tx_block(const sd_pins_t *p, const uint8_t *buf, unsigned len) {
    sd_xfer(p, 0xFF);
    sd_xfer(p, 0xFE);                       /* single-block start token */
    for (unsigned i = 0; i < len; i++) sd_xfer(p, buf[i]);
    sd_xfer(p, 0xFF); sd_xfer(p, 0xFF);     /* CRC, ignored in SPI mode */

    /* The data response: xxx0sss1, where 010 means accepted. */
    const uint8_t resp = sd_xfer(p, 0xFF);
    if ((resp & 0x1Fu) != 0x05u) return false;

    /* Then the card holds the line low until the block is committed.
     * Cards can take a surprising while over this - a quarter second is
     * within spec for a worn one - so the wait is generous. */
    for (unsigned i = 0; i < 500000u; i++)
        if (sd_xfer(p, 0xFF) != 0x00u) return true;
    return false;
}

static ui_test_state_t t_sd_write(const detect_result_t *d, char *detail,
                                  unsigned len, test_progress_fn p) {
    sd_pins_t sp;
    if (!pins_from(&d->board->pins, &sp)) {
        snprintf(detail, len, "no SD pins");
        return TEST_NORUN;
    }
    if (!s_card.present || s_card.bytes < 1024u * 1024u) {
        snprintf(detail, len, "no card identified");
        return TEST_NORUN;
    }

    /* The last sector the card admits to. Derived from the CSD capacity
     * rather than assumed, because getting it wrong here writes
     * somewhere real. */
    const uint32_t sector = (uint32_t)((s_card.bytes / 512ull) - 1ull);
    const uint32_t arg = s_card.block_addressed ? sector : sector * 512u;

    uint8_t *orig = sd_scratch(0), *work = sd_scratch(1), *back = sd_scratch(2);
    bool ok = true;
    const char *why = NULL;

    sd_select(&sp);

    if (sd_cmd(&sp, CMD17, arg, CRC_DUMMY) != 0 || !sd_rx_block(&sp, orig, 512)) {
        sd_deselect(&sp);
        snprintf(detail, len, "could not read the last sector");
        return TEST_FAIL;
    }
    if (p) p(250, NULL);

    /* A pattern that is not a constant: a stuck bus reads back the same
     * byte everywhere and would match a fill. The counter catches an
     * address line that is wrong as well as a data line. */
    for (unsigned i = 0; i < sizeof(work); i++)
        work[i] = (uint8_t)((i & 1u) ? (WRITE_PATTERN_A ^ (i >> 1))
                                     : (WRITE_PATTERN_B ^ (i >> 1)));

    if (sd_cmd(&sp, CMD24, arg, CRC_DUMMY) != 0 || !sd_tx_block(&sp, work, 512)) {
        ok = false; why = "card would not take the write";
    }
    if (p) p(500, NULL);

    if (ok) {
        if (sd_cmd(&sp, CMD17, arg, CRC_DUMMY) != 0 || !sd_rx_block(&sp, back, 512)) {
            ok = false; why = "wrote, but could not read back";
        } else if (memcmp(work, back, sizeof(work)) != 0) {
            ok = false; why = "read back different from written";
        }
    }
    if (p) p(750, NULL);

    /* Put it back whatever happened above, and check that it went. */
    bool restored = false;
    if (sd_cmd(&sp, CMD24, arg, CRC_DUMMY) == 0 && sd_tx_block(&sp, orig, 512)) {
        if (sd_cmd(&sp, CMD17, arg, CRC_DUMMY) == 0 &&
            sd_rx_block(&sp, back, 512))
            restored = (memcmp(orig, back, sizeof(orig)) == 0);
    }

    sd_deselect(&sp);
    if (p) p(1000, NULL);

    if (!restored) {
        /* Louder than the original fault, because it is worse: the card
         * now holds something nobody chose. */
        snprintf(detail, len, "RESTORE FAILED on sector %lu",
                 (unsigned long)sector);
        return TEST_FAIL;
    }

    if (!ok) {
        snprintf(detail, len, "%s", why ? why : "write failed");
        return TEST_FAIL;
    }

    snprintf(detail, len, "512 B written and restored");
    return TEST_PASS;
}

const frank_test_t frank_tests_sd[] = {
    { "SD card", ICON_DISK, CAP_SD, 0, t_sd      },
    { "SD read", ICON_DISK, CAP_SD, 0, t_sd_read },
    { "SD speed", ICON_DISK, CAP_SD, 0, t_sd_speed },
    { "SD write", ICON_DISK, CAP_SD, 0, t_sd_write },
};

const unsigned frank_tests_sd_len =
    sizeof(frank_tests_sd) / sizeof(frank_tests_sd[0]);
