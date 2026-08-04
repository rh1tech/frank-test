/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * detect.h — work out which board this is.
 *
 * Four tiers, most authoritative first. Every tier records what it
 * concluded, and the report prints all of them, because a disagreement
 * between "the binary says core2u" and "the pins say core2" is a fact
 * worth surfacing — it means either a wrong flash or a hardware fault,
 * and silently resolving it in favour of either one loses the signal.
 */
#ifndef DETECT_H
#define DETECT_H

#include "board_desc.h"

#define DETECT_MAX_CANDIDATES 6
#define DETECT_MAX_MISMATCH   8

typedef enum {
    DETECT_SRC_NONE = 0,
    DETECT_SRC_COMPILED,      /* -DFRANK_BOARD=<slug>       */
    DETECT_SRC_FLASH_RECORD,  /* the persisted FRANKID      */
    DETECT_SRC_SD_CONFIG,     /* frank.cfg on the card      */
    DETECT_SRC_AUTO,          /* tiers 0-2 agreed           */
    DETECT_SRC_OPERATOR,      /* picked from the menu       */
} detect_source_t;

typedef struct {
    /* ---- Tier 0: silicon ---- */
    frank_mcu_class_t mcu;
    uint8_t           chip_rev;
    uint8_t           chip_id[8];      /* flash unique ID, per-unit */
    uint32_t          flash_jedec;
    uint32_t          flash_bytes;

    /* ---- Tier 1: active probes ---- */
    int8_t   psram_cs_found;           /* PIN_NC if none responded  */
    uint32_t psram_bytes;
    bool     psram_softspi;            /* megafrank's bit-banged part */
    bool     i2c_ds3231;               /* 0x68 answered             */
    bool     i2c_tlv320;               /* 0x18 answered             */
    bool     onewire_found;
    uint8_t  onewire_rom[8];           /* family 0x01 + 48-bit serial */
    int8_t   onewire_pin;
    bool     link_peer;                /* a doorbell answered        */

    /* ---- Memory throughput, measured before video starts ----
     *
     * These have to be taken with core 1 parked. The HSTX scanout runs a
     * tight loop out of flash, and its instruction fetches contend with
     * core 0's XIP reads through the single QMI: measured on a Core 2
     * master, flash read is 33.6 MiB/s before video_output_init() and
     * 7.6 MiB/s after. The same 4x applies to PSRAM.
     *
     * Reporting the post-video figure would make a healthy part look
     * like a failing one, so the benchmark runs during boot, while the
     * screen does not exist yet. */
    uint32_t flash_read_kbps;
    uint32_t psram_write_kbps, psram_read_kbps, psram_byte_errors;
    bool     psram_sweep_ok, psram_sweep_run;

    /* ---- Tier 2: fingerprint ---- */
    const frank_board_desc_t *candidates[DETECT_MAX_CANDIDATES];
    unsigned  candidate_count;
    unsigned  best_score;              /* pins matched               */
    unsigned  best_of;                 /* pins compared              */
    int       margin;                  /* best minus runner-up       */
    uint8_t   mismatch[DETECT_MAX_MISMATCH];
    unsigned  mismatch_count;

    /* True when two or more descriptors scored identically and nothing
     * else separates them. core2 vs core2u is the permanent case: the
     * boards share a pin map, and the only difference visible from the
     * master is a bare pad versus a CMOS input on GP45. */
    bool ambiguous;

    /* ---- Verdict ---- */
    const frank_board_desc_t *board;   /* NULL if undecided          */
    detect_source_t           source;
    const frank_board_desc_t *auto_guess;  /* what tiers 0-2 thought */
    bool                      disagrees;   /* declared != autodetected */
} detect_result_t;

/* Run every tier and fill `out`.
 *
 * Must be called before anything claims the pins it probes — in
 * practice, before graphics_init() and before the link is brought up.
 * It leaves every pin it touched as a plain input with no pull. */
void detect_run(detect_result_t *out);

/* Measure flash and PSRAM throughput. Must be called with core 1 parked
 * — i.e. after detect_run() and before any video backend is opened. */
void detect_benchmark(detect_result_t *out);

/* Print the full reasoning to the console: what each tier saw, the
 * candidate scores, the margin, and every pin that disagreed. This is
 * the output someone reads when the verdict is wrong, so it is
 * deliberately verbose. */
void detect_report(const detect_result_t *r);

const char *detect_source_name(detect_source_t s);

#endif /* DETECT_H */
