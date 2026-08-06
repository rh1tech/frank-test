/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "oc_campaign.h"

#include "oc_request.h"

#include "hardware/structs/watchdog.h"

/* scratch[1] and [2] carry the clock request, [3] the video mode. This
 * is the last of the four the firmware owns — see video_request.c. */
#define CAMP_SCRATCH  0

/* One byte of magic, because a cold boot leaves these registers holding
 * whatever they held and a campaign that started itself would be a
 * memorable bug. */
#define CAMP_MAGIC    0xC5u

const uint8_t  oc_soak_choices[]   = { 15, 30, 60, 120 };
const unsigned oc_soak_choices_len =
    (unsigned)(sizeof(oc_soak_choices) / sizeof(oc_soak_choices[0]));

static unsigned soak_index(uint8_t secs) {
    for (unsigned i = 0; i < oc_soak_choices_len; i++)
        if (oc_soak_choices[i] == secs) return i;
    return 1;                                   /* 30 s, the default */
}

/* One 32-bit word:
 *
 *   31:24  magic     23:22  state    21:20  trying    19  retry
 *   18:14  vreg_sel  13:12  soak     11:9   tested   8:6  passed
 *
 * Three rungs, so two bits address any of them and one bit each records
 * what happened. */
bool oc_campaign_load(oc_campaign_t *out) {
    const uint32_t v = watchdog_hw->scratch[CAMP_SCRATCH];

    if (out) {
        out->state = 0; out->trying = 0; out->retry = 0;
        out->vreg_sel = 0; out->soak_s = oc_soak_choices[1];
        out->tested = 0; out->passed = 0;
    }
    if ((v >> 24) != CAMP_MAGIC) return false;

    const uint8_t state = (uint8_t)((v >> 22) & 0x03u);
    if (state == OC_CAMP_IDLE) return false;

    if (out) {
        out->state    = state;
        out->trying   = (uint8_t)((v >> 20) & 0x03u);
        out->retry    = (uint8_t)((v >> 19) & 0x01u);
        out->vreg_sel = (uint8_t)((v >> 14) & 0x1Fu);
        out->soak_s   = oc_soak_choices[(v >> 12) & 0x03u];
        out->tested   = (uint8_t)((v >>  9) & 0x07u);
        out->passed   = (uint8_t)((v >>  6) & 0x07u);
    }
    return true;
}

void oc_campaign_store(const oc_campaign_t *c) {
    if (!c) return;

    watchdog_hw->scratch[CAMP_SCRATCH] =
        ((uint32_t)CAMP_MAGIC << 24) |
        (((uint32_t)c->state    & 0x03u) << 22) |
        (((uint32_t)c->trying   & 0x03u) << 20) |
        (((uint32_t)c->retry    & 0x01u) << 19) |
        (((uint32_t)c->vreg_sel & 0x1Fu) << 14) |
        (((uint32_t)soak_index(c->soak_s) & 0x03u) << 12) |
        (((uint32_t)c->tested   & 0x07u) <<  9) |
        (((uint32_t)c->passed   & 0x07u) <<  6);
}

int oc_campaign_best(const oc_campaign_t *c) {
    if (!c) return -1;
    for (int i = (int)oc_cpu_ladder_len - 1; i >= 0; i--)
        if (c->passed & (1u << i)) return i;
    return -1;
}

void oc_campaign_clear(void) {
    watchdog_hw->scratch[CAMP_SCRATCH] = 0;
}
