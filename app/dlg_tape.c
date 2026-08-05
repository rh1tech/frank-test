/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_tape.c — the tape input, watched.
 *
 *
 * WHAT THE HARDWARE GIVES
 *
 * A CD4069 hex inverter wired as a squaring comparator. Audio goes in,
 * a clean digital square wave comes out on one GPIO — GP22 on every
 * board that has it except core2u, which uses GP45. There is no ADC and
 * no PIO involved: the pin is either high or low, and the only thing
 * worth measuring is when it changes.
 *
 * SpeccyP treats it exactly that way. gpio_in_init() is gpio_init, dir
 * in, *pull up* — and hw_zx_get_bit_LOAD() is a bare gpio_get() feeding
 * the Z80's EAR bit on port 0xFE. That is the whole driver, and it is
 * why this file is a sampler and not a peripheral.
 *
 * The pull-up is load-bearing rather than incidental. On the three
 * boards where a DIP gates the connection, an open switch leaves the pin
 * floating, and a floating pin picks up enough to toggle — which would
 * read as a signal arriving from a tape that is not connected. Pulled
 * up, an open switch is a steady high and reports honestly as silence.
 * (Pull *down* is not an option: see the RP2350-E9 erratum, which is
 * also why pinsig.c only ever pulls up.)
 *
 *
 * WHY THE ANIMATION IS NOT DECORATION
 *
 * A ZX Spectrum paints one pair of border lines per tape pulse, so the
 * stripe thickness *is* the pulse length: thick red-and-cyan bands are
 * pilot tone, thin blue-and-yellow ones are data. That is a genuinely
 * good visualisation, arrived at by accident forty years ago, and it is
 * reproduced here rather than imitated — each band is one measured
 * pulse, its height scaled from that pulse's duration and its colour
 * chosen by the same threshold that classifies it.
 *
 * So a screen full of thick bands means a pilot tone is playing, thin
 * ones mean data is going past, and a stalled picture means nothing is
 * arriving. None of that needs the operator to read a number.
 *
 * The exact Spectrum palette does not exist in a 16-colour interface
 * palette that also has to draw greys and bevels — there is no cyan and
 * no magenta. Pale blue stands in for cyan; the rest are close enough
 * that the pattern reads correctly, which is what matters.
 */

#include "dlgs.h"

#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_icons.h"
#include "ui_input.h"
#include "ui_video.h"
#include "ui_textpage.h"
#include "ui_window.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define DLG_W      560
#define PANEL_H    132

/* How long each frame spends watching the pin.
 *
 * Long enough for good statistics — a 2168 T-state pilot half-pulse is
 * 619 us at the Spectrum's 3.5 MHz, so 25 ms holds about forty edges —
 * and short enough that Esc still feels immediate. */
#define WINDOW_US  25000u

/* ZX Spectrum tape timings, in microseconds at 3.5 MHz. These are the
 * numbers the ROM loader writes and every tape in existence carries.
 *
 *   pilot   2168 T   619 us
 *   sync     667 T   191 us  /  735 T  210 us
 *   bit 0    855 T   244 us
 *   bit 1   1710 T   489 us
 *
 * The split between "pilot" and "data" therefore sits comfortably above
 * a one bit and below a pilot half-pulse. */
#define PILOT_US   619u
#define BIT1_US    489u
#define PILOT_MIN  ((PILOT_US + BIT1_US) / 2)      /* 554 us */

/* Anything slower than this is not a tape — it is hum, or a stray hand
 * on the connector. Naming a bound stops a 50 Hz mains buzz being
 * reported as a very slow pilot tone. */
#define SLOWEST_US 4000u

/* ------------------------------------------------------------------ */
/* The pulse ring                                                      */
/* ------------------------------------------------------------------ */

/* One entry per measured pulse, newest last. Sized so the panel can
 * always be filled: the thinnest band is 2 px, so 132 pixels needs at
 * most 66 of them. */
#define RING_LEN 96

static uint16_t s_ring[RING_LEN];
static unsigned s_ring_head;      /* index one past the newest */
static unsigned s_ring_used;

static void ring_push(uint32_t us) {
    if (us > 0xFFFFu) us = 0xFFFFu;
    s_ring[s_ring_head] = (uint16_t)us;
    s_ring_head = (s_ring_head + 1u) % RING_LEN;
    if (s_ring_used < RING_LEN) s_ring_used++;
}

/* `back` counts backwards from the newest: 0 is the most recent pulse. */
static uint16_t ring_at(unsigned back) {
    const unsigned i = (s_ring_head + RING_LEN - 1u - back) % RING_LEN;
    return s_ring[i];
}

/* ------------------------------------------------------------------ */
/* Sampling                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t edges;         /* transitions seen in the window          */
    uint32_t mean_us;       /* mean half-period, 0 when nothing moved  */
    uint32_t shortest_us;
    uint32_t longest_us;
    bool     level;         /* where the pin finished                  */
} tape_window_t;

/* Watch the pin for WINDOW_US and record every transition.
 *
 * Polled rather than interrupt- or PIO-driven, and that is a deliberate
 * trade rather than the easy way out. At 252 MHz this loop samples in
 * the tens of nanoseconds, which is three orders of magnitude faster
 * than the ~190 us shortest pulse a Spectrum tape carries — so nothing
 * is missed, and it costs no PIO on a chip whose three PIOs are already
 * carrying the link, the audio and the gamepads. */
static void sample(uint pin, tape_window_t *w) {
    memset(w, 0, sizeof(*w));
    w->shortest_us = 0xFFFFFFFFu;

    const uint32_t t0 = time_us_32();
    bool     last      = gpio_get(pin);
    uint32_t last_edge = t0;
    uint32_t sum       = 0;
    uint32_t counted   = 0;

    while (time_us_32() - t0 < WINDOW_US) {
        const bool now = gpio_get(pin);
        if (now == last) continue;

        const uint32_t t  = time_us_32();
        const uint32_t us = t - last_edge;
        last      = now;
        last_edge = t;

        /* The first edge is discarded: the window opened at an arbitrary
         * point in whatever pulse was already running, so its measured
         * length is an artefact of when the operator opened the dialog. */
        if (w->edges++ == 0) continue;

        if (us < w->shortest_us) w->shortest_us = us;
        if (us > w->longest_us)  w->longest_us  = us;
        sum += us;
        counted++;
        ring_push(us);
    }

    w->level   = last;
    w->mean_us = counted ? sum / counted : 0;
    if (w->shortest_us == 0xFFFFFFFFu) w->shortest_us = 0;
}

/* ------------------------------------------------------------------ */
/* Reading the numbers                                                 */
/* ------------------------------------------------------------------ */

typedef enum { TAPE_SILENT, TAPE_PILOT, TAPE_DATA, TAPE_NOISE } tape_kind_t;

static tape_kind_t classify(const tape_window_t *w) {
    if (w->edges < 2 || !w->mean_us)  return TAPE_SILENT;
    if (w->mean_us > SLOWEST_US)      return TAPE_NOISE;
    if (w->mean_us >= PILOT_MIN)      return TAPE_PILOT;
    return TAPE_DATA;
}

static const char *kind_name(tape_kind_t k) {
    switch (k) {
        case TAPE_PILOT: return "pilot tone";
        case TAPE_DATA:  return "data";
        case TAPE_NOISE: return "something slow - hum?";
        default:         return "silent";
    }
}

/* ------------------------------------------------------------------ */
/* The switch                                                          */
/* ------------------------------------------------------------------ */

/* Pull the tape clause out of the board's own manual note.
 *
 * Every gated board already spells out which switch does this, in the
 * descriptor, in its own words — "S1 9-3 closes tape onto GP22",
 * "JP1: pin6-7 closes tape onto GP22", "S2: 1-4 closes tape onto GP22".
 * Repeating that here as a generic "turn the switch on" would be less
 * useful and could drift out of step with the descriptor, so this finds
 * the sentence that mentions tape and shows exactly that. */
static bool tape_switch_note(const frank_board_desc_t *b, char *out, unsigned len) {
    out[0] = 0;
    if (!b || !(b->caps & CAP_TAPE_DIP_GATED) || !b->manual_note) return false;

    const char *n = b->manual_note;
    while (*n) {
        /* One clause, delimited the way the notes are written. */
        const char *end = n;
        while (*end && *end != ';' && *end != '.') end++;

        bool mentions = false;
        for (const char *p = n; p + 3 < end; p++)
            if ((p[0] == 't' || p[0] == 'T') && p[1] == 'a' &&
                p[2] == 'p' && p[3] == 'e') { mentions = true; break; }

        if (mentions) {
            unsigned k = 0;
            while (n < end && k + 1 < len) {
                if (!(k == 0 && *n == ' ')) out[k++] = *n;
                n++;
            }
            out[k] = 0;
            return k > 0;
        }

        n = end;
        while (*n == ';' || *n == '.' || *n == ' ') n++;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static int s_stop_x, s_stop_y, s_stop_w, s_stop_h;

/* One band per pulse, newest at the bottom, exactly as a Spectrum paints
 * its border. Height is the pulse duration, so the picture is a
 * time-scaled trace of the signal rather than a pattern. */
static void draw_stripes(ui_surface_t *s, int x, int y, int w, int h,
                         bool any) {
    ui_bevel_in(s, x - 2, y - 2, w + 4, h + 4);

    if (!any) {
        ui_fill(s, x, y, w, h, UI_GREY_2);
        ui_text_centred(s, x, y + h / 2 - 4, w,
                        "no signal on the tape input", UI_GREY_5);
        return;
    }

    int  cy    = y + h;             /* filled bottom-up */
    bool phase = false;             /* alternates every band */

    for (unsigned back = 0; back < s_ring_used && cy > y; back++) {
        const uint16_t us = ring_at(back);

        /* Height from duration, clamped so a 190 us sync pulse is still
         * visible and a long gap does not fill the panel by itself. */
        int bh = (int)(us / 60u);
        if (bh < 2)  bh = 2;
        if (bh > 14) bh = 14;

        uint8_t c;
        if (us >= PILOT_MIN) c = phase ? UI_FAIL   : UI_ACCENT_L; /* red / "cyan" */
        else                 c = phase ? UI_ACCENT : UI_WARN;     /* blue / yellow */
        phase = !phase;

        cy -= bh;
        if (cy < y) { bh -= (y - cy); cy = y; }
        if (bh > 0) ui_fill(s, x, cy, w, bh, c);
    }

    /* Anything the ring could not reach yet. */
    if (cy > y) ui_fill(s, x, y, w, cy - y, UI_BLACK);
}

/* The text-page version - see ui_textpage_modal(). */
static char s_tp[3][40];
static const char *s_tp_lines[3];

static void publish_textpage(const tape_window_t *w, tape_kind_t kind,
                             uint32_t total_edges, uint pin) {
    snprintf(s_tp[0], sizeof(s_tp[0]), "GP%u: %s, idles %s", pin,
             kind_name(kind), w->level ? "high" : "low");
    snprintf(s_tp[1], sizeof(s_tp[1]), "Edges: %lu in window, %lu total",
             (unsigned long)w->edges, (unsigned long)total_edges);
    if (w->mean_us)
        snprintf(s_tp[2], sizeof(s_tp[2]), "Half-period: %lu us (%lu-%lu)",
                 (unsigned long)w->mean_us, (unsigned long)w->shortest_us,
                 (unsigned long)w->longest_us);
    else
        snprintf(s_tp[2], sizeof(s_tp[2]), "Half-period: nothing moving");

    for (int i = 0; i < 3; i++) s_tp_lines[i] = s_tp[i];
    ui_textpage_modal("Tape In", s_tp_lines, 3, -1, "play a tape   Esc closes");
}

static void draw(const dlg_ctx_t *c, const tape_window_t *w, tape_kind_t kind,
                 uint32_t total_edges, const char *note, uint pin) {
    ui_surface_t *s = ui_video_surface();
    publish_textpage(w, kind, total_edges, pin);
    c->paint_background();

    const int note_h    = note[0] ? 24 : 0;
    const int content_h = 12 + note_h + PANEL_H + 4 + 12 + 12;
    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + content_h
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 20;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "Tape In", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    {
        char line[64];
        snprintf(line, sizeof(line), "Play a tape into the input. GP%u.", pin);
        ui_text(s, cx, cy, line, UI_GREY_5);
    }

    int py = cy + 12;
    if (note[0]) {
        ui_text(s, cx, py, "This board gates the tape input:", UI_BLACK);
        ui_text(s, cx, py + 11, note, UI_WARN);
        py += note_h;
    }

    draw_stripes(s, cx, py, cw, PANEL_H, s_ring_used > 0);
    py += PANEL_H + 8;

    /* The numbers behind the picture. Kept to one line: the stripes are
     * the primary reading and this is what you check once they have told
     * you something is wrong. */
    {
        char line[96];
        if (kind == TAPE_SILENT) {
            snprintf(line, sizeof(line), "%s   pin %s   %u edges so far",
                     kind_name(kind), w->level ? "high" : "low",
                     (unsigned)total_edges);
        } else {
            snprintf(line, sizeof(line),
                     "%s   %u Hz   pulse %u us (%u-%u)   %u edges",
                     kind_name(kind),
                     (unsigned)(500000u / (w->mean_us ? w->mean_us : 1u)),
                     (unsigned)w->mean_us,
                     (unsigned)w->shortest_us, (unsigned)w->longest_us,
                     (unsigned)total_edges);
        }
        ui_text(s, cx, py, line,
                kind == TAPE_SILENT ? UI_GREY_5
              : kind == TAPE_NOISE  ? UI_WARN : UI_OK);
    }

    ui_clip_reset(s);

    s_stop_w = ui_button_width("Stop");
    s_stop_h = 18;
    s_stop_x = x + DLG_W - 16 - s_stop_w;
    s_stop_y = y + h - UI_WIN_PAD - DLG_BOT - 18;
    ui_button(s, s_stop_x, s_stop_y, s_stop_w, s_stop_h, "Stop", true, false, true);

    ui_text(s, cx, s_stop_y + 5, "Esc or Stop to finish", UI_GREY_5);

    ui_video_present();

    /* The swap invalidates whatever the overlay had saved, so it is
     * re-established here rather than composed in — compositing it would
     * leave a second, stale cursor behind the moment the overlay moved. */
    ui_cursor_overlay_reset();
    {
        const ui_pointer_t *pt = ui_input_pointer_last();
        if (pt->present) ui_cursor_overlay_move(pt->x, pt->y);
    }
}

/* ------------------------------------------------------------------ */

void dlg_tape(const dlg_ctx_t *c) {
    const detect_result_t *d = c->detect;
    if (!d || !d->board) return;

    const int8_t tp = d->board->pins.tape_in;
    if (tp == PIN_NC) return;
    const uint pin = (uint)tp;

    /* Give the pad back on the way out. The tape pin doubles as a config
     * DIP input on three boards, and detection has already read it —
     * leaving it owned by SIO would make a later re-read report this
     * dialog's pull-up instead of the switch. */
    const gpio_function_t was = gpio_get_function(pin);

    /* Pull up, per SpeccyP's gpio_in_init(). An open gate must read as a
     * steady high, not as a floating pin that toggles on nothing. */
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);

    char note[96];
    tape_switch_note(d->board, note, sizeof(note));

    printf("[tape] listening on GP%u%s\n", pin,
           note[0] ? " (gated - see the note on screen)" : "");

    s_ring_head = 0;
    s_ring_used = 0;

    uint32_t    total = 0;
    tape_kind_t last_kind = TAPE_NOISE + 1;   /* force the first report */
    bool        stop = false;

    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }

    while (!stop) {
        tape_window_t w;
        sample(pin, &w);
        total += w.edges;

        const tape_kind_t kind = classify(&w);
        if (kind != last_kind) {
            printf("[tape] %s (mean %u us, %u edges)\n",
                   kind_name(kind), (unsigned)w.mean_us, (unsigned)w.edges);
            last_kind = kind;
        }

        draw(c, &w, kind, total, note, pin);

        ui_input_task();
        int k = ui_input_getkey();
        while (k != UI_KEY_NONE) {
            if (k == UI_KEY_ESC || k == UI_KEY_ENTER) stop = true;
            k = ui_input_getkey();
        }

        const ui_pointer_t *pt = ui_input_pointer();
        if (pt->pressed &&
            pt->x >= s_stop_x && pt->x < s_stop_x + s_stop_w &&
            pt->y >= s_stop_y && pt->y < s_stop_y + s_stop_h)
            stop = true;
    }

    gpio_set_function(pin, was);
    ui_textpage_modal_clear();
    printf("[tape] stopped, %u edges total\n", (unsigned)total);
}
