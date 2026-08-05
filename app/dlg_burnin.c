/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_burnin.c — the whole suite, over and over, counting.
 *
 *
 * WHAT A SINGLE PASS CANNOT SEE
 *
 * Every other row in this firmware answers "does it work", once, on a
 * cold board. That misses an entire class of fault, and not an unusual
 * one: PSRAM that reads back wrong only once it is warm, a link that
 * degrades as the room heats, a joint that conducts until something
 * expands. All of those pass the first time and fail on the fortieth,
 * and a rig that only ever asks once will certify them.
 *
 * So this asks repeatedly and keeps score. What it reports is not a
 * verdict but a rate: forty-one passes and one failure is a different
 * board from forty-two passes, and both look identical to a single run.
 *
 *
 * WHAT IS COUNTED
 *
 * Failures per test, and cycles. Not the measurements - a burn-in that
 * kept every number would need somewhere to put them, and the useful
 * signal is which row moved rather than by how much. A row that has
 * never failed shows nothing at all, so the display is empty until
 * something goes wrong, which is the state worth being able to read
 * from across a bench.
 *
 * Tests that report "could not run" are counted separately from
 * failures. An empty SD socket is not an intermittent fault, and lumping
 * the two together would bury a real one under forty of them.
 *
 *
 * IT STOPS WHEN ASKED, NOT WHEN IT FEELS LIKE IT
 *
 * There is no cycle limit. The operator decides when there is enough
 * evidence, which is the only party who knows what the board is for -
 * an hour for something going in a case, one pass for a board about to
 * be reworked anyway.
 */

#include "dlgs.h"

#include "registry.h"
#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_input.h"
#include "ui_video.h"
#include "ui_textpage.h"
#include "ui_window.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define DLG_W    470
#define LIST_H   190
#define ROW_H     14

static int s_stop_x, s_stop_y, s_stop_w, s_stop_h;

typedef struct {
    unsigned cycles;
    unsigned fails[40];
    unsigned norun[40];
    unsigned fail_total;
    absolute_time_t started;
} burn_t;

static void hms(uint32_t secs, char *out, unsigned len) {
    snprintf(out, len, "%luh %02lum %02lus",
             (unsigned long)(secs / 3600u),
             (unsigned long)((secs / 60u) % 60u),
             (unsigned long)(secs % 60u));
}

/* The same window for the text-page outputs, which do not scan the
 * surface this draws into - see ui_textpage_modal(). */
static char s_tp[3][40];
static const char *s_tp_lines[3];

static void publish_textpage(const burn_t *b, const registry_results_t *r,
                             int running_row) {
    char t[24];
    hms((uint32_t)(absolute_time_diff_us(b->started, get_absolute_time())
                   / 1000000), t, sizeof(t));

    snprintf(s_tp[0], sizeof(s_tp[0]), "Cycle %u, running %s", b->cycles, t);
    snprintf(s_tp[1], sizeof(s_tp[1]), "Failures so far: %u", b->fail_total);
    snprintf(s_tp[2], sizeof(s_tp[2]), "Now: %s",
             (running_row >= 0 && running_row < (int)r->count)
                 ? r->rows[running_row].name : "-");

    for (int i = 0; i < 3; i++) s_tp_lines[i] = s_tp[i];
    ui_textpage_modal("Burn-in", s_tp_lines, 3, -1, "Esc stops after this test");
}

static void draw(const dlg_ctx_t *c, const burn_t *b,
                 const registry_results_t *r, int running_row) {
    ui_surface_t *s = ui_video_surface();
    publish_textpage(b, r, running_row);
    c->paint_background();

    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + 30 + LIST_H
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 10;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "Burn-in", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    char line[80], t[32];
    const uint32_t secs =
        (uint32_t)(absolute_time_diff_us(b->started, get_absolute_time()) / 1000000);
    hms(secs, t, sizeof(t));

    snprintf(line, sizeof(line), "%lu cycle%s in %s",
             (unsigned long)b->cycles, b->cycles == 1 ? "" : "s", t);
    ui_text(s, cx, cy, line, UI_BLACK);

    /* The headline is the failure count, in the ink that matches what it
     * means, because it is the one number worth reading from a distance. */
    if (b->fail_total)
        snprintf(line, sizeof(line), "%lu failure%s",
                 (unsigned long)b->fail_total, b->fail_total == 1 ? "" : "s");
    else
        snprintf(line, sizeof(line), "no failures");
    ui_text(s, cx + cw - ui_text_width(line), cy, line,
            b->fail_total ? UI_FAIL : UI_OK);

    ui_separator(s, cx, cy + 14, cw);

    /* Only rows that have gone wrong. A clean burn-in is a blank panel,
     * which is the point: anything on it is something to look at. */
    int ly = cy + 22;
    int shown = 0;
    for (unsigned i = 0; i < r->count && ly < cy + 22 + LIST_H - ROW_H; i++) {
        if (!b->fails[i] && !b->norun[i]) continue;

        ui_text(s, cx, ly, r->rows[i].name, UI_BLACK);
        if (b->fails[i]) {
            snprintf(line, sizeof(line), "%lu failed",
                     (unsigned long)b->fails[i]);
            ui_text(s, cx + cw - ui_text_width(line), ly, line, UI_FAIL);
        } else {
            snprintf(line, sizeof(line), "%lu could not run",
                     (unsigned long)b->norun[i]);
            ui_text(s, cx + cw - ui_text_width(line), ly, line, UI_GREY_5);
        }
        ly += ROW_H;
        shown++;
    }

    if (!shown) {
        ui_text(s, cx, cy + 30,
                b->cycles ? "Nothing has failed yet."
                          : "Starting...", UI_GREY_5);
        ui_text(s, cx, cy + 44,
                "Rows appear here only when they go wrong.", UI_GREY_5);
    }

    if (running_row >= 0 && (unsigned)running_row < r->count) {
        snprintf(line, sizeof(line), "now: %s", r->rows[running_row].name);
        ui_text(s, cx, cy + 22 + LIST_H - 12, line, UI_GREY_5);
    }

    ui_clip_reset(s);

    s_stop_w = ui_button_width("Stop");
    s_stop_h = 18;
    s_stop_x = x + DLG_W - 16 - s_stop_w;
    s_stop_y = y + h - UI_WIN_PAD - DLG_BOT - 18;
    ui_button(s, s_stop_x, s_stop_y, s_stop_w, s_stop_h, "Stop", true, false, true);

    ui_text(s, cx, s_stop_y + 5, "Esc or Stop to finish", UI_GREY_5);

    ui_video_present();
}

/* ------------------------------------------------------------------ */

void dlg_burnin(const dlg_ctx_t *c, registry_results_t *r) {
    const detect_result_t *d = c->detect;
    if (!d || !d->board || d->board->id == FRANK_BOARD_UNKNOWN) return;

    burn_t b;
    memset(&b, 0, sizeof(b));
    b.started = get_absolute_time();

    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }
    ui_cursor_overlay_reset();

    bool stop = false;
    draw(c, &b, r, -1);

    while (!stop) {
        registry_prepare(r, d);

        for (unsigned i = 0; i < r->count && !stop; i++) {
            if (r->rows[i].state == TEST_NA) continue;

            /* Between tests, not during. A test owns its pins and its
             * timing while it runs, and stopping in the middle of one
             * would leave the hardware however that test had it. */
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
            if (pt->moved && pt->present) ui_cursor_overlay_move(pt->x, pt->y);
            if (stop) break;

            draw(c, &b, r, (int)i);
            registry_run_one(r, i, d, NULL);

            if (r->rows[i].state == TEST_FAIL) {
                b.fails[i]++;
                b.fail_total++;
                printf("[burn] cycle %lu: %s failed - %s\n",
                       (unsigned long)b.cycles + 1u, r->rows[i].name,
                       r->detail[i][0] ? r->detail[i] : "-");
            } else if (r->rows[i].state == TEST_NORUN) {
                b.norun[i]++;
            }
        }

        if (!stop) {
            b.cycles++;
            printf("[burn] cycle %lu done, %lu failure(s) so far\n",
                   (unsigned long)b.cycles, (unsigned long)b.fail_total);
            draw(c, &b, r, -1);
        }
    }

    ui_textpage_modal_clear();

    const uint32_t secs =
        (uint32_t)(absolute_time_diff_us(b.started, get_absolute_time()) / 1000000);
    printf("[burn] stopped after %lu cycle(s), %lu failure(s), %lu s\n",
           (unsigned long)b.cycles, (unsigned long)b.fail_total,
           (unsigned long)secs);
}
