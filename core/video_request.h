/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * video_request.h - carry a video mode across the reboot that applies it.
 *
 * Changing mode from the Video menu means restarting, because no backend
 * can be torn down safely. The request therefore has to survive the
 * restart, and it deliberately does not survive anything more than that.
 *
 * A watchdog scratch register holds it: preserved across a watchdog
 * reboot, cleared by a power cycle, and never written to flash. That
 * last part is the point. A stored mode outranks detection and lives in
 * its own flash sector, so it survives reflashing too - a board told
 * once to use VGA keeps coming up VGA regardless of what is loaded onto
 * it afterwards, and says nothing about why. Nothing autodetected is
 * worth that. Hold H, V or C at boot to choose deliberately.
 */
#ifndef VIDEO_REQUEST_H
#define VIDEO_REQUEST_H

#include "video_mode.h"

/* Ask the next boot for this mode. Survives the reboot, nothing else. */
void video_request_set(frank_video_mode_t mode);

/* The pending request, or VIDEO_AUTO. Clears it, so it applies once. */
frank_video_mode_t video_request_take(void);

#endif
