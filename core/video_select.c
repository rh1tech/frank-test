/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * video_select.c — the boot window.
 *
 * THE CHICKEN AND EGG
 *
 * Reading a key before video comes up needs an input device that is
 * already alive, and the candidates differ by an order of magnitude in
 * how quickly they are ready:
 *
 *   config DIP / jumper   instant, no stack at all
 *   UART console          instant, and always present
 *   PS/2 keyboard         ~500 ms to BAT, self-clocking, no enumeration
 *   USB HID keyboard      0.5 - 1.5 s (host enumeration + TinyUSB)
 *
 * So the window polls every source the board has, for its whole
 * duration, and takes the first answer. Sources that only become ready
 * near the end still count. Waiting for the slowest one before starting
 * would make the window useless on the boards that need it most.
 *
 * There is no USB CDC in this firmware. The USB controller is a HID
 * *host* so that keyboards and mice can be tested, which means it cannot
 * also enumerate as a serial device — and that turns out to help here:
 * CDC was the slowest input by a wide margin, routinely taking longer to
 * come up than any window worth waiting through. The console is UART,
 * which is ready before the first line of main() runs.
 *
 *
 * YOU MAY BE PRESSING BLIND
 *
 * If autodetect guessed wrong, the banner inviting the keypress is going
 * out of an output nobody can see. That is the normal case for
 * composite, and it is fine: press C anyway and the right output comes
 * up. Two consequences follow and both are implemented here —
 *
 *   the keys must work with no visual feedback whatsoever, and
 *   the console must echo what was accepted, so an operator on a serial
 *   line gets the confirmation the screen could not give them.
 */

#include "video_select.h"
#include "settings.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define MAX_SOURCES 4

static struct {
    video_key_source_fn fn;
    const char         *name;
} g_sources[MAX_SOURCES];
static unsigned g_source_count;

/* The console is always available and costs nothing, so it registers
 * itself rather than making every caller remember to. */
static int console_key_source(void) {
    int c = getchar_timeout_us(0);
    return (c == PICO_ERROR_TIMEOUT) ? -1 : c;
}

void video_select_add_source(video_key_source_fn fn, const char *name) {
    if (g_source_count < MAX_SOURCES) {
        g_sources[g_source_count].fn   = fn;
        g_sources[g_source_count].name = name;
        g_source_count++;
    }
}

const char *video_choice_source_name(video_choice_source_t s) {
    switch (s) {
        case VIDEO_CHOICE_BOOT_KEY: return "boot key";
        case VIDEO_CHOICE_STICKY:   return "stored choice";
        case VIDEO_CHOICE_COMPILED: return "compiled-in";
        case VIDEO_CHOICE_AUTO:     return "autodetect";
        default:                    return "default";
    }
}

/* Is this mode wired on this board at all? Forcing VGA on microfrank
 * would drive eight pins into a connector that does not exist. */
static bool mode_supported(const frank_board_desc_t *b, frank_video_mode_t m) {
    if (!b) return false;
    switch (m) {
        case VIDEO_HDMI:      return (b->caps & CAP_VIDEO_HDMI)      != 0;
        case VIDEO_VGA:       return (b->caps & CAP_VIDEO_VGA)       != 0;
        case VIDEO_COMPOSITE: return (b->caps & CAP_VIDEO_COMPOSITE) != 0;
        default:              return true;
    }
}

static void banner(const frank_board_desc_t *board, uint32_t ms) {
    printf("\n[video] %s: hold H=HDMI  V=VGA  C=composite  A=auto  (%u ms)\n",
           board ? board->name : "unknown", (unsigned)ms);
    printf("[video] inputs: ");
    for (unsigned i = 0; i < g_source_count; i++)
        printf("%s%s", i ? ", " : "", g_sources[i].name);
    printf("\n");
    stdio_flush();
}

void video_select_boot_window(const frank_board_desc_t *board,
                              const video_detect_t *detected,
                              uint32_t window_ms,
                              video_choice_t *out) {
    memset(out, 0, sizeof(*out));

    /* Register the console once, on first use. */
    static bool console_registered = false;
    if (!console_registered) {
        video_select_add_source(console_key_source, "console");
        console_registered = true;
    }

    if (!board || !(board->caps & CAP_VIDEO_ANY)) {
        out->mode   = VIDEO_NONE;
        out->source = VIDEO_CHOICE_DEFAULT;
        return;
    }

    banner(board, window_ms);

    /* ---- 1. the window ---- */
    absolute_time_t deadline = make_timeout_time_ms(window_ms);
    int             key      = -1;
    const char     *from     = NULL;

    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        for (unsigned i = 0; i < g_source_count && key < 0; i++) {
            if (!g_sources[i].fn) continue;
            int c = g_sources[i].fn();
            if (c < 0) continue;
            if (c == 'h' || c == 'H' || c == 'v' || c == 'V' ||
                c == 'c' || c == 'C' || c == 'a' || c == 'A') {
                key  = c;
                from = g_sources[i].name;
            }
            /* Anything else is ignored rather than ending the window:
             * a stray newline from a terminal handshake must not count
             * as a choice. */
        }
        if (key >= 0) break;
        sleep_ms(2);
    }

    if (key >= 0) {
        bool want_auto = (key == 'a' || key == 'A');
        frank_video_mode_t m = want_auto ? VIDEO_AUTO
                                         : frank_video_mode_from_key(key);

        if (!want_auto && !mode_supported(board, m)) {
            printf("[video] '%c' -> %s is not wired on %s; ignoring.\n",
                   key, frank_video_mode_name(m), board->slug);
        } else {
            /* Echo, because the screen may not be able to. */
            printf("[video] '%c' from %s -> %s%s\n", key, from,
                   want_auto ? "auto" : frank_video_mode_name(m),
                   want_auto ? " (stored choice cleared)" : " (stored)");

            out->sticky_written = settings_set_video(m);
            if (!out->sticky_written)
                printf("[video] WARNING: could not write the stored choice; "
                       "this will need holding again next boot.\n");

            if (!want_auto) {
                out->mode            = m;
                out->source          = VIDEO_CHOICE_BOOT_KEY;
                out->key_source_name = from;
                return;
            }
            /* 'A' falls through to autodetect below. */
        }
    }

    /* ---- 2. the sticky choice ---- */
    frank_settings_t s;
    if (settings_load(&s) && s.video != VIDEO_AUTO &&
        mode_supported(board, (frank_video_mode_t)s.video)) {
        out->mode   = (frank_video_mode_t)s.video;
        out->source = VIDEO_CHOICE_STICKY;
        return;
    }

    /* ---- 3. compiled-in ---- */
#ifdef FRANK_VIDEO_COMPILED
    {
        frank_video_mode_t m = frank_video_mode_from_key(FRANK_VIDEO_COMPILED[0]);
        if (m != VIDEO_AUTO && mode_supported(board, m)) {
            out->mode   = m;
            out->source = VIDEO_CHOICE_COMPILED;
            return;
        }
    }
#endif

    /* ---- 4. autodetect ---- */
    if (detected && detected->verdict != VIDEO_NONE &&
        mode_supported(board, detected->verdict)) {
        out->mode   = detected->verdict;
        out->source = VIDEO_CHOICE_AUTO;
        return;
    }

    /* ---- 5. default ---- */
    out->mode   = (board->caps & CAP_VIDEO_HDMI) ? VIDEO_HDMI : VIDEO_NONE;
    out->source = VIDEO_CHOICE_DEFAULT;
}

bool video_select_set(frank_video_mode_t mode, bool persist) {
    if (persist && !settings_set_video(mode))
        printf("[video] WARNING: stored choice not written\n");

    /* Every cross-family transition is a reboot, and within a family the
     * caller still has to tear the backend down. Returning "reboot" here
     * unconditionally keeps the one tested path — boot — as the only way
     * a video mode ever comes up. video_backend.c may override this for
     * the PIO HDMI/VGA pair, where both pipelines already live in one
     * binary and switching is genuinely cheap. */
    return true;
}
