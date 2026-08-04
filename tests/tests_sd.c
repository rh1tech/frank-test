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
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* Commands, in the SPI-mode numbering. */
#define CMD0    0        /* GO_IDLE_STATE      */
#define CMD8    8        /* SEND_IF_COND       */
#define CMD9    9        /* SEND_CSD           */
#define CMD10   10       /* SEND_CID           */
#define CMD17   17       /* READ_SINGLE_BLOCK  */
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

    static uint8_t sec[512];
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

const frank_test_t frank_tests_sd[] = {
    { "SD card", ICON_DISK, CAP_SD, 0, t_sd      },
    { "SD read", ICON_DISK, CAP_SD, 0, t_sd_read },
};

const unsigned frank_tests_sd_len =
    sizeof(frank_tests_sd) / sizeof(frank_tests_sd[0]);
