/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui_video.h"
#include "video_request.h"
#include "ui_palette.h"
#include "settings.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* The one framebuffer. 153,600 bytes, shared by whichever backend is up
 * — the reason the vtable exists rather than each driver owning its own,
 * and at this size not something to have two of. */
static uint8_t      fb[2][UI_FB_BYTES];
static ui_surface_t surface;
static bool         surface_ready;
static unsigned     back_idx;

extern const ui_video_backend_t ui_video_backend_hstx_hdmi;
extern const ui_video_backend_t ui_video_backend_hstx_vga;
extern const ui_video_backend_t ui_video_backend_tv;

/* Ordered by preference within each mode. The first one whose init()
 * succeeds wins; a backend that cannot run returns false and the next is
 * tried.
 *
 * The VGA and composite backends land here as they are ported — see
 * PLAN.md phase 5f. Until they do, asking for VGA falls back to HDMI and
 * says so, which is the correct behaviour rather than a placeholder: the
 * operator gets a picture and a message, not a blank screen. */
static const ui_video_backend_t *const backends[] = {
    &ui_video_backend_hstx_hdmi,
    &ui_video_backend_hstx_vga,
    &ui_video_backend_tv,
};

static const ui_video_backend_t *current;

ui_surface_t *ui_video_surface(void) {
    if (!surface_ready) {
        memset(fb, 0, sizeof(fb));
        back_idx = 1;
        ui_surface_init(&surface, fb[back_idx], UI_SCREEN_W, UI_SCREEN_H);
        surface_ready = true;
    }
    return &surface;
}

/* The buffer the scanout is currently reading. */
uint8_t *ui_video_front_bits(void) { return fb[back_idx ^ 1u]; }
uint8_t *ui_video_back_bits(void)  { return fb[back_idx]; }
uint8_t *ui_video_spare_bits(void) { return fb[back_idx ^ 1u]; }

void ui_video_swap_buffers(void) {
    back_idx ^= 1u;
    surface.bits = fb[back_idx];
}

const ui_video_backend_t *ui_video_current(void) { return current; }

bool ui_video_scans_front_buffer(void) {
    return current && current->mode != VIDEO_COMPOSITE;
}

bool ui_video_mode_implemented(frank_video_mode_t mode) {
    if (mode == VIDEO_AUTO) return true;
    for (unsigned i = 0; i < sizeof(backends) / sizeof(backends[0]); i++)
        if (backends[i]->mode == mode) return true;
    return false;
}

frank_video_mode_t ui_video_open(frank_video_mode_t mode) {
    ui_video_surface();

    if (mode == VIDEO_NONE) return VIDEO_NONE;

    /* First pass: a backend for exactly the mode asked for. */
    for (unsigned i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
        if (backends[i]->mode != mode) continue;
        if (backends[i]->init()) {
            current = backends[i];
            printf("[video] %s\n", current->name);
            return current->mode;
        }
        printf("[video] %s unavailable on this chip\n", backends[i]->name);
    }

    /* Second pass: anything at all. Coming up on the wrong output is
     * recoverable — the operator holds a key and gets the right one —
     * whereas coming up on no output at all leaves them nothing to read
     * the instructions on. */
    for (unsigned i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
        if (backends[i]->mode == mode) continue;
        if (backends[i]->init()) {
            current = backends[i];
            printf("[video] %s asked for, fell back to %s\n",
                   frank_video_mode_name(mode), current->name);
            return current->mode;
        }
    }

    printf("[video] no backend came up\n");
    return VIDEO_NONE;
}

void ui_video_present(void) {
    if (current && current->present) current->present();
}

/* Copy the newly-published frame back into what is now the back buffer.
 *
 * Without this, the back buffer holds the frame before last, and a
 * partial repaint — the menu-only path — would leave the rest of the
 * screen a frame stale, which flickers between two old images. 153,600
 * bytes at 252 MHz is about 150 us; a full recomposition is orders of
 * magnitude more. */
void ui_video_sync_back(void) {
    memcpy(fb[back_idx], fb[back_idx ^ 1u], UI_FB_BYTES);
}

bool ui_video_switch(frank_video_mode_t mode) {
    if (current && current->mode == mode) return true;

    /* Live switching is only offered where a backend can genuinely be
     * torn down. Nothing implements shutdown() yet, so this is currently
     * always the reboot path — which is the honest state of affairs
     * rather than a stub pretending otherwise. */
    if (current && current->shutdown) {
        for (unsigned i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
            if (backends[i]->mode != mode) continue;
            current->shutdown();
            current = NULL;
            if (backends[i]->init()) { current = backends[i]; return true; }
            break;
        }
    }

    /* Ask for it, then restart. The request rides a watchdog scratch
     * register across the reboot and is consumed by the next boot. It is
     * not written to flash, so it applies once and a power cycle comes
     * back up on whatever the board actually has. */
    video_request_set(mode);

    printf("[video] restarting into %s\n", frank_video_mode_name(mode));
    stdio_flush();
    sleep_ms(50);
    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();
}
