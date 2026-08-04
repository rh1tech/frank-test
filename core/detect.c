/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * detect.c — board identification.
 *
 * Ordering is deliberate: the cheapest and most certain evidence first,
 * so that by the time the fingerprint runs it is only being asked to
 * separate boards that everything else already agrees on.
 *
 *   Tier 0  silicon      free, touches nothing, splits the fleet in three
 *   Tier 1  active probes each answers yes for one or two boards only
 *   Tier 2  pin signature separates what is left
 *   Tier 3  declared     resolves what physics cannot
 *
 * Tier 1 is where the confidence comes from. A DS2401 answering with a
 * valid family byte and CRC is not an inference; a PSRAM responding on
 * CS 8 with a coherent size is not an inference. The fingerprint is the
 * weakest of the three and is treated accordingly — it breaks ties, it
 * does not make findings.
 */

#include "detect.h"
#include "pinsig.h"
#include "mem_test.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/sysinfo.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tier 0 — silicon                                                    */
/* ------------------------------------------------------------------ */

static void tier0_silicon(detect_result_t *r) {
    uint32_t chip_id = *((io_ro_32 *)(SYSINFO_BASE + SYSINFO_CHIP_ID_OFFSET));
    r->chip_rev = (uint8_t)((chip_id >> 28) & 0xFu);

    /* PACKAGE_SEL bit 0: 1 = QFN-60 (A, 30 GPIOs), 0 = QFN-80 (B, 48).
     * The PGA2350 module carries a B die, so it lands here too. */
    uint32_t pkg = *((io_ro_32 *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET));
    r->mcu = (pkg & 1u) ? FRANK_MCU_RP2350A : FRANK_MCU_RP2350B;

    /* Takes the QMI out of XIP to issue a raw 0x9F, so it must run with
     * the other core parked — which at this point in boot it is. */
    mem_test_flash_identify(&r->flash_jedec, r->chip_id);
    r->flash_bytes = mem_test_flash_capacity(r->flash_jedec);
}

/* ------------------------------------------------------------------ */
/* Bit-banged I2C                                                      */
/* ------------------------------------------------------------------ */

/* Bit-banged rather than hardware I2C on purpose. The probe has to run
 * before we know what board this is, which means before we can be sure
 * the pins map onto a controller instance the way the descriptor claims.
 * A software master works on any pin pair and cannot leave a peripheral
 * half-configured behind it. It is also slow, which is the right trade
 * for a bus with 4.7K pull-ups and unknown capacitance. */

#define I2C_DELAY_US 5

static void i2c_pin_release(unsigned pin) {   /* open-drain high */
    gpio_set_dir(pin, GPIO_IN);
}
static void i2c_pin_drive_low(unsigned pin) {
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_OUT);
}

static void i2c_bb_init(unsigned sda, unsigned scl) {
    gpio_init(sda); gpio_init(scl);
    gpio_put(sda, 0); gpio_put(scl, 0);
    i2c_pin_release(sda); i2c_pin_release(scl);
    gpio_pull_up(sda); gpio_pull_up(scl);
    busy_wait_us_32(I2C_DELAY_US * 4);
}

static void i2c_bb_start(unsigned sda, unsigned scl) {
    i2c_pin_release(sda); busy_wait_us_32(I2C_DELAY_US);
    i2c_pin_release(scl); busy_wait_us_32(I2C_DELAY_US);
    i2c_pin_drive_low(sda); busy_wait_us_32(I2C_DELAY_US);
    i2c_pin_drive_low(scl); busy_wait_us_32(I2C_DELAY_US);
}

static void i2c_bb_stop(unsigned sda, unsigned scl) {
    i2c_pin_drive_low(sda); busy_wait_us_32(I2C_DELAY_US);
    i2c_pin_release(scl);   busy_wait_us_32(I2C_DELAY_US);
    i2c_pin_release(sda);   busy_wait_us_32(I2C_DELAY_US);
}

/* Returns true if the slave pulled SDA low for the ACK bit. */
static bool i2c_bb_write_byte(unsigned sda, unsigned scl, uint8_t v) {
    for (int i = 7; i >= 0; i--) {
        if (v & (1u << i)) i2c_pin_release(sda); else i2c_pin_drive_low(sda);
        busy_wait_us_32(I2C_DELAY_US);
        i2c_pin_release(scl);   busy_wait_us_32(I2C_DELAY_US);
        i2c_pin_drive_low(scl); busy_wait_us_32(I2C_DELAY_US);
    }
    i2c_pin_release(sda);       busy_wait_us_32(I2C_DELAY_US);
    i2c_pin_release(scl);       busy_wait_us_32(I2C_DELAY_US);
    bool ack = !gpio_get(sda);
    i2c_pin_drive_low(scl);     busy_wait_us_32(I2C_DELAY_US);
    return ack;
}

static bool i2c_bb_probe(unsigned sda, unsigned scl, uint8_t addr7) {
    i2c_bb_start(sda, scl);
    bool ack = i2c_bb_write_byte(sda, scl, (uint8_t)(addr7 << 1));  /* write */
    i2c_bb_stop(sda, scl);
    return ack;
}

static void i2c_bb_release(unsigned sda, unsigned scl) {
    gpio_disable_pulls(sda); gpio_disable_pulls(scl);
    gpio_set_dir(sda, GPIO_IN); gpio_set_dir(scl, GPIO_IN);
}

/* ------------------------------------------------------------------ */
/* Bit-banged 1-Wire (DS2401)                                          */
/* ------------------------------------------------------------------ */

/* Timings from the DS2401 datasheet's standard speed. They are written
 * as literals rather than derived, because 1-Wire's margins are in
 * microseconds and a "tidier" expression is how they get broken. */

static bool ow_reset(unsigned pin) {
    gpio_init(pin);
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_OUT);        /* pull the bus low       */
    busy_wait_us_32(480);
    gpio_set_dir(pin, GPIO_IN);         /* release; 4.7K pulls up */
    busy_wait_us_32(70);
    bool presence = !gpio_get(pin);     /* the part answers low   */
    busy_wait_us_32(410);
    return presence;
}

static void ow_write_bit(unsigned pin, bool bit) {
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_OUT);
    busy_wait_us_32(bit ? 6 : 60);
    gpio_set_dir(pin, GPIO_IN);
    busy_wait_us_32(bit ? 64 : 10);
}

static bool ow_read_bit(unsigned pin) {
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_OUT);
    busy_wait_us_32(6);
    gpio_set_dir(pin, GPIO_IN);
    busy_wait_us_32(9);
    bool b = gpio_get(pin);
    busy_wait_us_32(55);
    return b;
}

static void ow_write_byte(unsigned pin, uint8_t v) {
    for (int i = 0; i < 8; i++) ow_write_bit(pin, (v >> i) & 1u);
}

static uint8_t ow_read_byte(unsigned pin) {
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) if (ow_read_bit(pin)) v |= (uint8_t)(1u << i);
    return v;
}

static uint8_t ow_crc8(const uint8_t *d, unsigned len) {
    uint8_t crc = 0;
    while (len--) {
        uint8_t b = *d++;
        for (int i = 0; i < 8; i++) {
            uint8_t mix = (uint8_t)((crc ^ b) & 1u);
            crc >>= 1;
            if (mix) crc ^= 0x8Cu;
            b >>= 1;
        }
    }
    return crc;
}

/* Read the 64-bit ROM. Returns true only when the family byte says
 * DS2401 (0x01) and the CRC checks — a presence pulse alone is too easy
 * to fake with a floating pin and a stray pull-up. */
static bool ds2401_read(unsigned pin, uint8_t rom[8]) {
    if (!ow_reset(pin)) return false;

    ow_write_byte(pin, 0x33);          /* READ ROM */
    for (int i = 0; i < 8; i++) rom[i] = ow_read_byte(pin);

    gpio_set_dir(pin, GPIO_IN);
    gpio_disable_pulls(pin);

    if (rom[0] != 0x01) return false;
    return ow_crc8(rom, 8) == 0;
}

/* ------------------------------------------------------------------ */
/* Tier 1 — active probes                                              */
/* ------------------------------------------------------------------ */

/* Every candidate chip-select in the fleet. The sweep is ordered so the
 * commonest comes first, but all of them are tried: a board that answers
 * on two would be a finding worth seeing, not something to short-circuit
 * past. */
static const uint8_t psram_cs_candidates[] = { 8, 47, 0 };

static void tier1_probes(detect_result_t *r,
                         const frank_board_desc_t **cands, unsigned n) {
    r->psram_cs_found = PIN_NC;
    r->onewire_pin    = PIN_NC;

    /* ---- PSRAM on the QMI ---- */
    for (unsigned i = 0; i < sizeof(psram_cs_candidates); i++) {
        unsigned cs = psram_cs_candidates[i];

        /* Only try chip selects some candidate actually uses. Probing a
         * pin that is a UART line on this board would drive it. */
        bool plausible = false;
        for (unsigned c = 0; c < n; c++)
            if (cands[c]->pins.psram_cs == (int8_t)cs) plausible = true;
        if (!plausible) continue;

        uint32_t bytes = mem_test_psram_probe(cs);
        if (bytes) {
            r->psram_cs_found = (int8_t)cs;
            r->psram_bytes    = bytes;
            break;
        }
    }

    /* ---- I2C: DS3231 at 0x68, TLV320DAC3100 at 0x18 ---- */
    for (unsigned c = 0; c < n; c++) {
        const frank_pins_t *p = &cands[c]->pins;
        if (p->i2c_sda == PIN_NC || p->i2c_scl == PIN_NC) continue;

        i2c_bb_init((unsigned)p->i2c_sda, (unsigned)p->i2c_scl);
        r->i2c_ds3231 |= i2c_bb_probe((unsigned)p->i2c_sda,
                                      (unsigned)p->i2c_scl, 0x68);
        r->i2c_tlv320 |= i2c_bb_probe((unsigned)p->i2c_sda,
                                      (unsigned)p->i2c_scl, 0x18);
        i2c_bb_release((unsigned)p->i2c_sda, (unsigned)p->i2c_scl);
        break;   /* every candidate with I2C uses GP28/29 */
    }

    /* ---- 1-Wire: GP23 on frank_next, GP30 on megafrank ---- */
    for (unsigned c = 0; c < n && !r->onewire_found; c++) {
        int8_t pin = cands[c]->pins.onewire;
        if (pin == PIN_NC) continue;
        if (ds2401_read((unsigned)pin, r->onewire_rom)) {
            r->onewire_found = true;
            r->onewire_pin   = pin;
        }
    }

    /* ---- The inter-processor link ----
     *
     * Raise our doorbell and see whether the other half raises its own
     * inside 200 ms. Nothing here depends on the two chips agreeing
     * about absolute time, which is what makes it safe to run during
     * detection: the peer may still be booting, and a no-answer is a
     * legitimate result rather than a failure. */
    for (unsigned c = 0; c < n; c++) {
        const frank_pins_t *p = &cands[c]->pins;
        if (p->link_db_out == PIN_NC || p->link_db_in == PIN_NC) continue;

        gpio_init((unsigned)p->link_db_out);
        gpio_set_dir((unsigned)p->link_db_out, GPIO_OUT);
        gpio_put((unsigned)p->link_db_out, 1);

        gpio_init((unsigned)p->link_db_in);
        gpio_set_dir((unsigned)p->link_db_in, GPIO_IN);
        gpio_pull_down((unsigned)p->link_db_in);

        absolute_time_t deadline = make_timeout_time_ms(200);
        while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
            if (gpio_get((unsigned)p->link_db_in)) { r->link_peer = true; break; }
            sleep_us(200);
        }

        gpio_put((unsigned)p->link_db_out, 0);
        gpio_set_dir((unsigned)p->link_db_out, GPIO_IN);
        gpio_disable_pulls((unsigned)p->link_db_in);
        break;
    }
}

/* Does a candidate agree with what Tier 1 actually found?
 *
 * Returned as a veto rather than a score. These are positive
 * identifications: a board that claims a DS2401 when no DS2401 answered
 * is not "slightly less likely", it is wrong. Scoring them alongside
 * fuzzy pin states would let a good fingerprint outvote a hard fact. */
static bool tier1_consistent(const detect_result_t *r,
                             const frank_board_desc_t *d) {
    const frank_pins_t *p = &d->pins;

    bool wants_qmi_psram = (p->psram_cs != PIN_NC) && !(d->caps & CAP_PSRAM_SOFTSPI);
    if (wants_qmi_psram && r->psram_cs_found != p->psram_cs) return false;
    if (!wants_qmi_psram && r->psram_cs_found != PIN_NC &&
        !(d->caps & CAP_PSRAM_SOFTSPI)) return false;

    if ((bool)(d->caps & CAP_RTC_DS3231)     != r->i2c_ds3231)   return false;
    if ((bool)(d->caps & CAP_AUDIO_CODEC_I2C) != r->i2c_tlv320)  return false;

    /* A DS2401 that answered proves the board has one. A DS2401 that did
     * not answer proves nothing — the part is a DNP option on some
     * builds — so this veto only runs one way. */
    if (r->onewire_found && !(d->caps & CAP_ONEWIRE_DS2401)) return false;
    if (r->onewire_found && p->onewire != r->onewire_pin)    return false;

    /* Likewise: a peer that answered means this is a linked board. A
     * silent peer might just be a slave that has not booted yet. */
    if (r->link_peer && !(d->caps & CAP_LINK)) return false;

    return true;
}

/* ------------------------------------------------------------------ */
/* Tier 2 — fingerprint                                                */
/* ------------------------------------------------------------------ */

/* Rank by the *fraction* of a signature that matched, not the count.
 *
 * Counting absolute matches compares signatures of different lengths as
 * though they were the same evidence, and the failure is not academic:
 * on a core2 master, `nyx` matched 6 of its 7 pins and `core2` matched 6
 * of 6, which scored identically and produced a spurious three-way
 * ambiguity — with `nyx` first in the list. The one pin nyx got wrong
 * was GP43, the 10K pull-up that is precisely what distinguishes them.
 *
 * A ratio makes a single mismatch decisive, which it should be: these
 * are not noisy measurements, they are pull-ups that either exist or do
 * not. Ties then break toward the longer signature, on the grounds that
 * more agreeing evidence beats less. */
static void tier2_fingerprint(detect_result_t *r,
                              const frank_board_desc_t **cands, unsigned n) {
    unsigned best = 0, second = 0;        /* permille */
    unsigned best_len = 0;
    const frank_board_desc_t *winner = NULL;

    for (unsigned i = 0; i < n; i++) {
        uint8_t  bad_pins[DETECT_MAX_MISMATCH];
        unsigned bad = 0;
        unsigned matched = pinsig_score(cands[i], bad_pins,
                                        DETECT_MAX_MISMATCH, &bad);
        unsigned len   = cands[i]->sig_len;
        unsigned score = len ? (matched * 1000u) / len : 0u;

        bool better = (score > best) ||
                      (score == best && len > best_len);

        if (better) {
            if (score > best) second = best;
            best     = score;
            best_len = len;
            winner   = cands[i];
            r->best_score      = matched;
            r->best_of         = len;
            r->mismatch_count  = bad < DETECT_MAX_MISMATCH ? bad : DETECT_MAX_MISMATCH;
            memcpy(r->mismatch, bad_pins, r->mismatch_count);
        } else if (score > second) {
            second = score;
        }
    }

    r->margin     = (int)best - (int)second;   /* permille */
    r->auto_guess = winner;

    /* Re-derive the candidate list as *the boards that tied for best*,
     * not everything Tier 1 failed to rule out.
     *
     * Those are different sets and the difference matters, because this
     * list is what the operator is asked to choose from. On a core2
     * master with the link peer briefly silent, the viable set included
     * `nyx` — which the fingerprint had already ranked well below the
     * other two — and it appeared as option 1 in the dialog. Offering a
     * board the evidence has ruled out is a good way to have it picked. */
    r->candidate_count = 0;
    for (unsigned i = 0; i < n && r->candidate_count < DETECT_MAX_CANDIDATES; i++) {
        unsigned len = cands[i]->sig_len;
        if (!len) continue;
        uint8_t  scratch[DETECT_MAX_MISMATCH];
        unsigned bad = 0;
        unsigned matched = pinsig_score(cands[i], scratch,
                                        DETECT_MAX_MISMATCH, &bad);
        if ((matched * 1000u) / len == best)
            r->candidates[r->candidate_count++] = cands[i];
    }

    /* A zero margin means two descriptors explained the pins equally
     * well. Saying so is the correct output; picking the first is how a
     * detector produces a confident wrong answer. */
    r->ambiguous = (winner != NULL) && (r->margin == 0) && (n > 1);
}

/* ------------------------------------------------------------------ */
/* Driver                                                              */
/* ------------------------------------------------------------------ */

void detect_run(detect_result_t *out) {
    memset(out, 0, sizeof(*out));
    out->psram_cs_found = PIN_NC;
    out->onewire_pin    = PIN_NC;

    tier0_silicon(out);

    /* Everything this MCU class could be. */
    const frank_board_desc_t *all[DETECT_MAX_CANDIDATES * 4];
    unsigned n = frank_boards_for_mcu(out->mcu, all,
                                      sizeof(all) / sizeof(all[0]));

    tier1_probes(out, all, n);

    /* Keep only what Tier 1 did not rule out. */
    const frank_board_desc_t *viable[DETECT_MAX_CANDIDATES * 4];
    unsigned v = 0;
    for (unsigned i = 0; i < n; i++)
        if (tier1_consistent(out, all[i])) viable[v++] = all[i];

    /* If the vetoes eliminated everything, the board is something the
     * table does not describe, or a probe misfired. Fall back to the
     * unfiltered set so the fingerprint still has something to say —
     * a low-confidence guess plus a visible warning beats no output. */
    if (v == 0) {
        for (unsigned i = 0; i < n; i++) viable[v++] = all[i];
    }

    /* tier2_fingerprint() fills in the candidate list, from the boards
     * that tied rather than from everything viable. */
    tier2_fingerprint(out, viable, v);

    /* ---- Tier 3: declared identity wins, but never silently ---- */
#ifdef FRANK_BOARD_COMPILED
    frank_board_id_t compiled = frank_board_from_slug(FRANK_BOARD_COMPILED);
    if (compiled != FRANK_BOARD_UNKNOWN) {
        out->board  = frank_board_desc(compiled, out->auto_guess
                                       ? out->auto_guess->role
                                       : FRANK_ROLE_SINGLE);
        out->source = DETECT_SRC_COMPILED;
    }
#endif

    /* No stored board.
     *
     * There was one, in a dedicated flash sector, so the core2/core2u
     * question would be asked once per board rather than once per boot.
     * It is gone: a stored answer is invisible, outlives the operator
     * changing their mind, and travels with the firmware image rather
     * than with the board. A reset should start from what the hardware
     * says, and the operator overrides it from Board > Set Board for as
     * long as that session lasts. */

    if (!out->board && !out->ambiguous) {
        out->board  = out->auto_guess;
        out->source = out->board ? DETECT_SRC_AUTO : DETECT_SRC_NONE;
    }

    /* Still nothing — ambiguous, or the table does not describe this
     * board. Fall back to the fleet-wide conventions so the interface
     * can come up and ask, rather than leaving the operator a dark
     * screen and a question they cannot see. `source` stays NONE, so the
     * report keeps saying UNDECIDED. */
    if (!out->board) {
        out->board  = frank_board_fallback();
        out->source = DETECT_SRC_NONE;
    }

    /* A disagreement is only meaningful when the fingerprint actually
     * preferred something. When two descriptors tie — core2 and core2u
     * share a signature exactly — `auto_guess` is whichever the table
     * happens to list first, and warning that the declared board
     * "does not look like" an arbitrary tie-break is worse than saying
     * nothing: it teaches the operator to ignore the one warning that
     * catches a genuinely wrong flash.
     *
     * So: no warning if the fingerprint was ambiguous and the declared
     * board is one of the boards that tied. */
    bool declared_is_a_candidate = false;
    for (unsigned i = 0; i < out->candidate_count; i++)
        if (out->board && out->candidates[i]->id == out->board->id)
            declared_is_a_candidate = true;

    out->disagrees = out->board && out->auto_guess &&
                     out->board->id != FRANK_BOARD_UNKNOWN &&
                     out->board->id != out->auto_guess->id &&
                     !(out->ambiguous && declared_is_a_candidate);
}

void detect_benchmark(detect_result_t *out) {
    out->flash_read_kbps = mem_test_flash_read_kbps(1u * 1024 * 1024);

    if (out->psram_bytes) {
        out->psram_sweep_run = true;
        out->psram_sweep_ok  = mem_test_psram_sweep(out->psram_bytes,
                                                    &out->psram_write_kbps,
                                                    &out->psram_read_kbps,
                                                    &out->psram_byte_errors);
    }
}

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

const char *detect_source_name(detect_source_t s) {
    switch (s) {
        case DETECT_SRC_COMPILED:      return "compiled-in";
        case DETECT_SRC_FLASH_RECORD:  return "FRANKID record";
        case DETECT_SRC_SD_CONFIG:     return "frank.cfg";
        case DETECT_SRC_AUTO:          return "autodetect";
        case DETECT_SRC_OPERATOR:      return "operator";
        default:                       return "undecided";
    }
}

void detect_report(const detect_result_t *r) {
    printf("\n--- board detection ---\n");

    printf("  silicon    %s rev %u, flash %06X (%u MB)\n",
           frank_mcu_class_name(r->mcu), (unsigned)r->chip_rev,
           (unsigned)r->flash_jedec,
           (unsigned)(r->flash_bytes / (1024u * 1024u)));

    printf("  unit id    %02X%02X%02X%02X%02X%02X%02X%02X\n",
           r->chip_id[0], r->chip_id[1], r->chip_id[2], r->chip_id[3],
           r->chip_id[4], r->chip_id[5], r->chip_id[6], r->chip_id[7]);

    if (r->psram_cs_found != PIN_NC)
        printf("  psram      %u MB on CS GP%d\n",
               (unsigned)(r->psram_bytes / (1024u * 1024u)),
               (int)r->psram_cs_found);
    else
        printf("  psram      none responded\n");

    printf("  i2c        DS3231:%s  TLV320:%s\n",
           r->i2c_ds3231 ? "yes" : "no", r->i2c_tlv320 ? "yes" : "no");

    if (r->onewire_found)
        printf("  1-wire     DS2401 on GP%d, serial %02X%02X%02X%02X%02X%02X\n",
               (int)r->onewire_pin, r->onewire_rom[6], r->onewire_rom[5],
               r->onewire_rom[4], r->onewire_rom[3], r->onewire_rom[2],
               r->onewire_rom[1]);
    else
        printf("  1-wire     no DS2401\n");

    printf("  link peer  %s\n", r->link_peer ? "answered" : "silent");

    printf("  candidates ");
    for (unsigned i = 0; i < r->candidate_count; i++)
        printf("%s%s", i ? ", " : "", r->candidates[i]->slug);
    printf("\n");

    printf("  fingerprint %u/%u pins, margin %d%%%s\n",
           r->best_score, r->best_of, r->margin / 10,
           r->ambiguous ? "  AMBIGUOUS" : "");

    if (r->mismatch_count) {
        printf("  disagreed  ");
        for (unsigned i = 0; i < r->mismatch_count; i++)
            printf("GP%u ", (unsigned)r->mismatch[i]);
        printf("\n");
    }

    printf("  verdict    %s  (%s)\n",
           (r->board && r->source != DETECT_SRC_NONE) ? r->board->name
                                                      : "UNDECIDED",
           detect_source_name(r->source));

    if (r->disagrees)
        printf("  ** WARNING: declared board is %s but the pins look like "
               "%s. Either this image was flashed onto the wrong board, or "
               "something on this one is broken. **\n",
               r->board->slug, r->auto_guess->slug);

    if (r->ambiguous && r->source == DETECT_SRC_NONE)
        printf("  ** Two boards fit equally well and nothing has declared "
               "which this is. Use `board set <slug>` to settle it once; "
               "the answer is stored in flash. **\n");

    if (r->board && r->board->manual_note)
        printf("  note       %s\n", r->board->manual_note);

    printf("\n");
}
