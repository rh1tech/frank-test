/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "oc_request.h"

#include "hardware/clocks.h"
#include "hardware/structs/watchdog.h"
#include "hardware/vreg.h"

/* scratch[3] carries the video request; these take two of the three
 * remaining. The magic keeps a cold boot, where the registers hold
 * whatever they held, from reading as a request. */
#define OC_A       1u
#define OC_B       2u
#define OC_MAGIC   0x4F43u          /* 'OC' */

const uint16_t oc_cpu_ladder[] = { 252, 378, 504 };
const unsigned oc_cpu_ladder_len =
    (unsigned)(sizeof(oc_cpu_ladder) / sizeof(oc_cpu_ladder[0]));

uint8_t oc_vreg_for_clock(uint16_t mhz) {
    if (mhz >= 504u) return 0x14u;   /* VREG_VOLTAGE_1_65 */
    if (mhz >= 300u) return 0x13u;   /* VREG_VOLTAGE_1_60 */
    return 0x12u;                    /* VREG_VOLTAGE_1_50 */
}

unsigned oc_ladder_index(uint16_t mhz) {
    unsigned best = 0;
    for (unsigned i = 0; i < oc_cpu_ladder_len; i++)
        if (oc_cpu_ladder[i] <= mhz) best = i;
    return best;
}

/* What the firmware currently runs, and what a request defaults to. */
static oc_request_t s_live = {
    /* Flash ceiling 66, not 88 — see the note in main().  */
    .cpu_mhz = 252, .vreg_sel = 0, .psram_mhz = 133, .flash_mhz = 66,
};

void oc_request_set(const oc_request_t *r) {
    if (!r) return;
    watchdog_hw->scratch[OC_A] = ((uint32_t)OC_MAGIC << 16) | r->cpu_mhz;
    watchdog_hw->scratch[OC_B] = ((uint32_t)r->vreg_sel << 16) |
                                 ((uint32_t)r->psram_mhz << 8) |
                                  (uint32_t)r->flash_mhz;
}

bool oc_request_take(oc_request_t *out) {
    const uint32_t a = watchdog_hw->scratch[OC_A];
    const uint32_t b = watchdog_hw->scratch[OC_B];

    watchdog_hw->scratch[OC_A] = 0;
    watchdog_hw->scratch[OC_B] = 0;

    if ((a >> 16) != OC_MAGIC) return false;

    const uint16_t cpu = (uint16_t)(a & 0xFFFFu);
    if (cpu < 48u || cpu > 600u) return false;   /* obviously not ours */

    if (out) {
        out->cpu_mhz   = cpu;
        out->vreg_sel  = (uint8_t)((b >> 16) & 0xFFu);
        out->psram_mhz = (uint8_t)((b >> 8) & 0xFFu);
        out->flash_mhz = (uint8_t)(b & 0xFFu);
    }
    return true;
}

void oc_request_current(oc_request_t *out) {
    if (!out) return;
    *out = s_live;
    /* The clock is asked of the hardware rather than remembered: if the
     * requested one could not be reached, the dialog should open on what
     * the board is really doing. */
    out->cpu_mhz  = (uint16_t)(clock_get_hz(clk_sys) / 1000000u);
    out->vreg_sel = (uint8_t)vreg_get_voltage();
}

/* Called by main() once it has applied a request, so the dialog opens on
 * the values in force rather than on the defaults. */
void oc_request_note_applied(const oc_request_t *r) { if (r) s_live = *r; }
