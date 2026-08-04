/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * video_mode.h — the four choices, shared by the detector, the boot-time
 * override, the persisted settings record and the backend vtable.
 *
 * Its own header because settings.c must not have to include the video
 * driver stack to store one enum.
 */
#ifndef VIDEO_MODE_H
#define VIDEO_MODE_H

typedef enum {
    VIDEO_AUTO      = 0,   /* let the detector decide          */
    VIDEO_HDMI      = 1,
    VIDEO_VGA       = 2,
    VIDEO_COMPOSITE = 3,
    VIDEO_NONE      = 4,   /* the board has no video at all    */
} frank_video_mode_t;

static inline const char *frank_video_mode_name(frank_video_mode_t m) {
    switch (m) {
        case VIDEO_HDMI:      return "HDMI";
        case VIDEO_VGA:       return "VGA";
        case VIDEO_COMPOSITE: return "composite";
        case VIDEO_NONE:      return "none";
        default:              return "auto";
    }
}

/* Accepts the boot-window keys and the console words alike, so there is
 * one place that decides what 'v' means. Returns VIDEO_AUTO for 'a' and
 * for anything unrecognised — the caller distinguishes by checking the
 * input itself, because "auto" is a legitimate request. */
static inline frank_video_mode_t frank_video_mode_from_key(int c) {
    switch (c) {
        case 'h': case 'H': return VIDEO_HDMI;
        case 'v': case 'V': return VIDEO_VGA;
        case 'c': case 'C': return VIDEO_COMPOSITE;
        default:            return VIDEO_AUTO;
    }
}

#endif /* VIDEO_MODE_H */
