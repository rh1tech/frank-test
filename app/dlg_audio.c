/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_audio.c — play it until someone has heard it.
 *
 * The three sound paths cannot be tested, only listened to (see
 * frank_audio.h), and on a board with a 4:1 audio mux the operator has to
 * move two switches and listen again. So this loops: left, right, centre,
 * left, ... naming the channel as it goes, until Esc or Stop.
 *
 * The channel name is the load-bearing part. Three tones played in
 * succession leave the listener working out which one they just heard,
 * and "the right speaker was silent" is only a finding if you knew which
 * pass was the right one.
 */

#include "dlgs.h"

#include "attest.h"

#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_icons.h"
#include "ui_input.h"
#include "ui_video.h"
#include "ui_window.h"

#include "pico/stdlib.h"

#include <stdio.h>

#define DLG_W 380

/* Where Stop is, so the abort poll can hit-test it between notes without
 * re-deriving the layout. */
static int  s_stop_x, s_stop_y, s_stop_w, s_stop_h;
static int  s_yes_x, s_yes_w, s_no_x, s_no_w;
static bool s_abort;

/* What the operator said on the way out, if anything. Nothing on these
 * boards can hear the output - see core/attest.h - so their answer is
 * the only evidence the audio path works, and leaving without giving one
 * is a perfectly reasonable thing to do. */
static attest_t s_answer;

static bool inside(int x, int y, int bx, int bw) {
    return x >= bx && x < bx + bw &&
           y >= s_stop_y && y < s_stop_y + s_stop_h;
}

static bool inside_stop(int x, int y) { return inside(x, y, s_stop_x, s_stop_w); }

/* Polled between notes — about every 150 ms, which is close enough to
 * instant for a key press and far cheaper than making a note
 * interruptible. The PWM path in particular busy-waits a half-cycle at a
 * time and has nowhere to check. */
static bool poll_abort(void) {
    ui_input_task();

    int k = ui_input_getkey();
    while (k != UI_KEY_NONE) {
        /* Y and N answer and leave; Esc leaves without answering, which
         * is not the same as "no" and must not be recorded as one. */
        if (k == 'y' || k == 'Y') { s_answer = ATTEST_YES; s_abort = true; }
        else if (k == 'n' || k == 'N') { s_answer = ATTEST_NO; s_abort = true; }
        else if (k == UI_KEY_ESC || k == UI_KEY_ENTER || k == ' ') s_abort = true;
        k = ui_input_getkey();
    }

    const ui_pointer_t *p = ui_input_pointer();
    if (p->pressed) {
        if (inside_stop(p->x, p->y)) s_abort = true;
        else if (inside(p->x, p->y, s_yes_x, s_yes_w)) {
            s_answer = ATTEST_YES; s_abort = true;
        } else if (inside(p->x, p->y, s_no_x, s_no_w)) {
            s_answer = ATTEST_NO;  s_abort = true;
        }
    }

    /* Move the pointer, do not recompose.
     *
     * This used to call redraw_now(), which cost tens of milliseconds
     * against a FIFO holding 180 us of audio — so the melody gapped
     * whenever the mouse moved, and the cursor still only updated once
     * per note. The overlay patches the front buffer directly and costs
     * microseconds, so the pointer now tracks at the rate reports
     * arrive and the sound is untouched either way. */
    if (p->moved) ui_cursor_overlay_move(p->x, p->y);

    return s_abort;
}

static void draw(const dlg_ctx_t *c, audio_src_t src, int ch, unsigned laps) {
    ui_surface_t *s = ui_video_surface();
    c->paint_background();

    const char *hint = audio_src_switch_hint(c->detect, src);

    /* One line of body per thing worth saying, and no fixed height: an
     * empty band under a two-line message reads as a layout mistake
     * rather than as breathing room. */
    const int rows_h = 34            /* the big channel name          */
                     + 22            /* the three channel plates      */
                     + (hint ? 26 : 8);
    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + 14 + rows_h
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 20;

    char title[40];
    snprintf(title, sizeof(title), "Audio - %s", audio_src_name(src));

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = title, .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    ui_blit_icon(s, ui_icon(ICON_SPEAKER), cx, cy + 6, UI_ACCENT);

    ui_text(s, cx + 28, cy, "Playing the melody. Listen.", UI_GREY_5);

    /* The channel, in the largest type the interface has. This is the
     * one fact the operator needs at the moment the sound arrives. */
    {
        char line[32];
        snprintf(line, sizeof(line), "%s channel", audio_channel_name(ch));
        ui_text_big(s, cx + 28, cy + 16, line, UI_BLACK);
    }

    /* Three plates, the live one lit: the name says which channel, and
     * these say where in the cycle it is — so a pass that never advances
     * is visible rather than merely inaudible. */
    for (int i = 0; i < AUDIO_CHANNELS; i++) {
        const int px = cx + 28 + i * 92;
        const int py = cy + 42;
        const bool live = (i == ch);
        ui_plate(s, px, py, 84, 16, live ? UI_ACCENT : UI_GREY_1);
        ui_text_centred(s, px, py + 4, 84, audio_channel_name(i),
                        live ? UI_WHITE : UI_GREY_5);
    }

    if (hint) {
        ui_separator(s, cx, cy + 66, cw);
        ui_text(s, cx, cy + 74, hint, UI_WARN);
    }

    {
        char line[40];
        snprintf(line, sizeof(line), "pass %u", laps + 1);
        ui_text(s, cx + cw - ui_text_width(line), cy, line, UI_GREY_5);
    }

    ui_clip_reset(s);

    s_stop_w = ui_button_width("Stop");
    s_stop_h = 18;
    s_stop_x = x + DLG_W - 16 - s_stop_w;
    s_stop_y = y + h - UI_WIN_PAD - DLG_BOT - 18;
    ui_button(s, s_stop_x, s_stop_y, s_stop_w, s_stop_h, "Stop", true, false, true);

    /* The two that record something. They sit left of Stop so the
     * destructive-looking one is not the default landing place for a
     * click, and both say what they mean rather than OK/Cancel. */
    s_no_w  = ui_button_width("Silent");
    s_no_x  = s_stop_x - 8 - s_no_w;
    s_yes_w = ui_button_width("I hear it");
    s_yes_x = s_no_x - 8 - s_yes_w;
    ui_button(s, s_yes_x, s_stop_y, s_yes_w, s_stop_h, "I hear it", true, false, true);
    ui_button(s, s_no_x,  s_stop_y, s_no_w,  s_stop_h, "Silent",    true, false, true);

    ui_text(s, cx, s_stop_y + 5, "Y / N, or Esc to leave it open", UI_GREY_5);

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

void dlg_audio(const dlg_ctx_t *c, audio_src_t src) {
    if (!audio_src_available(c->detect, src)) return;

    printf("[audio] %s: looping L/R/C\n", audio_src_name(src));

    s_abort  = false;
    s_answer = ATTEST_UNKNOWN;
    int ch = 0;
    unsigned laps = 0;

    /* Drain anything the menu left behind, so the keystroke that opened
     * this dialog does not immediately close it. */
    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }

    while (!s_abort) {
        draw(c, src, ch, laps);

        if (!audio_play(c->detect, src, ch, poll_abort) && !s_abort) {
            /* A stall is real and worth stopping for — but only the I2S
             * path can detect one, so this is rare and specific rather
             * than a general failure route. */
            printf("[audio] %s stalled on the %s channel\n",
                   audio_src_name(src), audio_channel_name(ch));
            break;
        }

        if (++ch >= AUDIO_CHANNELS) { ch = 0; laps++; }
    }

    audio_stop(c->detect, src);

    /* Only recorded when they actually said something. Closing the
     * dialog is not a verdict, and storing it as one would turn "did not
     * check" into "does not work". */
    if (s_answer != ATTEST_UNKNOWN) {
        static const attest_subject_t subj[] = {
            [AUDIO_SRC_PWM] = ATTEST_AUDIO_PWM,
            [AUDIO_SRC_I2S] = ATTEST_AUDIO_I2S,
            [AUDIO_SRC_TS]  = ATTEST_AUDIO_TS,
        };
        attest_set(subj[src], s_answer);
    }

    printf("[audio] %s: stopped after %u pass(es), operator said %s\n",
           audio_src_name(src), laps * AUDIO_CHANNELS + (unsigned)ch,
           s_answer == ATTEST_YES ? "heard it"
                                  : (s_answer == ATTEST_NO ? "silent" : "nothing"));
}
