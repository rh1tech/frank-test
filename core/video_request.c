/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "video_request.h"

#include "hardware/structs/watchdog.h"

/* scratch[0..3] are free for firmware; the SDK and the bootrom use the
 * upper four for reboot vectors. The magic keeps a cold boot, where the
 * register holds whatever it held, from reading as a request. */
#define REQ_SCRATCH  3u
#define REQ_MAGIC    0x5644u          /* 'VD' */
#define REQ_SHIFT    16u

void video_request_set(frank_video_mode_t mode) {
    watchdog_hw->scratch[REQ_SCRATCH] =
        ((uint32_t)REQ_MAGIC << REQ_SHIFT) | (uint32_t)mode;
}

frank_video_mode_t video_request_take(void) {
    const uint32_t v = watchdog_hw->scratch[REQ_SCRATCH];
    watchdog_hw->scratch[REQ_SCRATCH] = 0;

    if ((v >> REQ_SHIFT) != REQ_MAGIC) return VIDEO_AUTO;

    const uint32_t mode = v & 0xFFFFu;
    if (mode > (uint32_t)VIDEO_NONE) return VIDEO_AUTO;
    return (frank_video_mode_t)mode;
}
