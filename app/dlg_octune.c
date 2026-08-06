/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_octune.c — climb the clock ladder until the board stops coping.
 *
 *
 * WHAT IT ANSWERS, AND WHAT IT DOES NOT
 *
 * It reports the highest clock on the ladder that started, reached the
 * interface, and then survived a soak of the memory buses and the core.
 * That is a useful number and it is not "the maximum stable overclock",
 * and the difference is the whole reason this file has a comment.
 *
 * Stability is a claim about hours, temperature and every workload the
 * board will ever run. Thirty seconds of soak per candidate finds the
 * clocks that are grossly wrong — the ones where the QSPI timing has
 * fallen over, the PSRAM reads back short, the core faults. It cannot
 * find the one that fails after forty minutes in a warm case, and a
 * result labelled "max stable" would be read as a promise that it had.
 *
 * So the wording on screen, in the log and in the report is "survived
 * N s", and anyone who wants the stronger claim has Burn-in, which asks
 * the same question for as long as they are willing to wait.
 *
 *
 * THREE CLOCKS, NOT A SWEEP
 *
 * 252, 378 and 504 MHz, and nothing between them — see oc_request.h.
 * They are the multiples of 126 that let clk_hstx land on 126 MHz
 * exactly, and every other frequency gives a fractional divide and a
 * monitor that will not lock.
 *
 * This mattered more than it looks. The first version swept twelve
 * frequencies, and the soak passed happily at 336 because it never
 * looks at the display — so the run reported clocks whose only symptom
 * was a dark screen, on the instrument the operator was using to read
 * the result.
 *
 *
 * VOLTAGE FOLLOWS THE CLOCK
 *
 * By default each clock is tried at the voltage the shipping firmwares
 * pair with it: 1.50 V, 1.60 V, 1.65 V. Clock and voltage are not
 * independent on this silicon, and asking an operator to pair them by
 * hand invites the combination nobody has ever booted — which is what
 * happened when this offered a fixed voltage capped at 1.35 V and then
 * reported 504 MHz as failing, having never given it the 1.65 V the
 * fleet runs it at. An override is still there for anyone deliberately
 * looking for the edge.
 *
 *
 * HOW A FAILURE IS SURVIVED
 *
 * Every candidate is applied by the same one-boot mechanism the manual
 * dialog uses, so nothing is ever stored that could stop the board
 * starting. Before the clock changes, the watchdog is armed: a candidate
 * that hangs before the interface comes up resets the board, and the
 * next boot finds the campaign record still saying RUNNING with no clock
 * request pending — which is precisely the signature of "the last one
 * killed it", and is recorded as such.
 *
 * The worst case left is a hang the watchdog cannot reach. That costs a
 * power cycle, which clears the campaign along with everything else, and
 * the board comes up stock. No state this writes can prevent a boot.
 */

#include "dlgs.h"

#include "mem_test.h"
#include "oc_campaign.h"
#include "oc_request.h"
#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_input.h"
#include "ui_video.h"
#include "ui_textpage.h"
#include "ui_window.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define DLG_W    440
#define ROW_H     18

/* Long enough for detection, the memory benchmark and video to come up
 * at a clock that works, short enough that a hang is not mistaken for a
 * slow boot. The benchmark is the long pole and stays under two seconds
 * on the largest PSRAM in the fleet. */
#define BOOT_WATCHDOG_MS  8000u

/* 0 is "the voltage that goes with the clock" — oc_vreg_for_clock(),
 * the 1.50/1.60/1.65 pairing frank-msx and murmgenesis both use — and
 * is the right answer for almost everyone. The rest are overrides.
 *
 * This list used to stop at 1.35 V, which was simply too low to be
 * useful: the fleet runs 504 MHz at 1.65 V, so an auto-test that
 * refused to go past 1.35 could never reproduce the configuration these
 * boards actually ship in, and reported the top of the ladder as failing
 * when it had only ever been undervolted. It stops at 1.65 because that
 * is where the fleet stops. */
static const uint8_t VREG[] = { 0, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14 };
#define VREG_LEN ((int)(sizeof(VREG) / sizeof(VREG[0])))

static const char *vreg_text(uint8_t sel) {
    static char buf[12];
    if (sel == 0) return "per clock";

    static const uint16_t mv[] = {
         550,  600,  650,  700,  750,  800,  850,  900,
         950, 1000, 1050, 1100, 1150, 1200, 1250, 1300,
        1350, 1400, 1500, 1600, 1650, 1700, 1800, 1900,
    };
    if (sel >= sizeof(mv) / sizeof(mv[0])) return "?";
    snprintf(buf, sizeof(buf), "%u.%02u V", mv[sel] / 1000u,
             (mv[sel] % 1000u) / 10u);
    return buf;
}

/* ------------------------------------------------------------------ */
/* The soak                                                            */
/* ------------------------------------------------------------------ */

/* A pattern held in SRAM and checked against itself.
 *
 * The memory tests below exercise the buses. This exercises the core and
 * the SRAM it runs from, which is where a clock that is merely slightly
 * too fast fails first: not with a bus error but with an instruction
 * that produced the wrong number. Small enough to stay in one bank and
 * fast enough to run between the slower checks. */
#define TORTURE_WORDS 512
static uint32_t s_torture[TORTURE_WORDS];

static bool torture_pass(uint32_t seed) {
    uint32_t x = seed | 1u;

    for (unsigned i = 0; i < TORTURE_WORDS; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        s_torture[i] = x;
    }

    x = seed | 1u;
    for (unsigned i = 0; i < TORTURE_WORDS; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        if (s_torture[i] != x) return false;
    }
    return true;
}

typedef struct {
    unsigned iterations;
    unsigned flash_errors;
    unsigned psram_errors;
    unsigned core_errors;
} soak_result_t;

static bool soak_clean(const soak_result_t *r) {
    return r->flash_errors == 0 && r->psram_errors == 0 && r->core_errors == 0;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static char s_tp[4][40];
static const char *s_tp_lines[4];

static void publish_textpage(const char *title, const char *a, const char *b,
                             const char *c, const char *foot) {
    snprintf(s_tp[0], sizeof(s_tp[0]), "%s", a ? a : "");
    snprintf(s_tp[1], sizeof(s_tp[1]), "%s", b ? b : "");
    snprintf(s_tp[2], sizeof(s_tp[2]), "%s", c ? c : "");
    for (int i = 0; i < 3; i++) s_tp_lines[i] = s_tp[i];
    ui_textpage_modal(title, s_tp_lines, 3, -1, foot);
}

/* The ladder, with what is known about each rung. Drawn as a column of
 * clocks rather than a progress bar over the whole run: the operator
 * wants to see which clocks passed, and a bar would only say how far
 * through the list it had got. */
static void draw_ladder(ui_surface_t *s, int x, int y, int w,
                        const oc_campaign_t *camp, bool running) {
    char line[40];

    /* A verdict against every clock.
     *
     * The first version printed "held" against one rung and a dash
     * against all the others, which reads as "nothing is known about
     * these" when in fact each had been tried and passed. A dash is not
     * a result. Now every rung carries its own bit, so a clock that
     * passed says so.
     *
     * "not tried" is kept distinct from "failed" for the same reason the
     * whole test list separates n/a from fail: a rung the run never
     * reached and a rung that fell over are different facts. */
    for (unsigned i = 0; i < oc_cpu_ladder_len; i++) {
        const int ry = y + (int)i * 12;
        const char *mark;
        uint8_t col;

        if (running && i == camp->trying) {
            mark = camp->retry ? "retrying" : "testing";  col = UI_ACCENT;
        } else if (!(camp->tested & (1u << i))) {
            mark = "not tried";                           col = UI_GREY_5;
        } else if (camp->passed & (1u << i)) {
            mark = "passed";                              col = UI_OK;
        } else {
            mark = "failed";                              col = UI_FAIL;
        }

        snprintf(line, sizeof(line), "%3u MHz  %s", oc_cpu_ladder[i],
                 vreg_text(camp->vreg_sel ? camp->vreg_sel
                                          : oc_vreg_for_clock(oc_cpu_ladder[i])));
        ui_text(s, x, ry, line, UI_BLACK);
        ui_text(s, x + w - ui_text_width(mark), ry, mark, col);
    }
}

/* ------------------------------------------------------------------ */
/* The setup dialog                                                    */
/* ------------------------------------------------------------------ */

static int s_start_x, s_start_y, s_start_w, s_start_h;
static int s_cancel_x, s_cancel_w;

typedef struct { int vreg, soak; } setup_t;

static void draw_setup(const dlg_ctx_t *c, const setup_t *sel, int row,
                       unsigned from) {
    ui_surface_t *s = ui_video_surface();

    char l0[40], l1[40], l2[40];
    snprintf(l0, sizeof(l0), "Core voltage  %s", vreg_text(VREG[sel->vreg]));
    snprintf(l1, sizeof(l1), "Soak per step %u s", oc_soak_choices[sel->soak]);
    snprintf(l2, sizeof(l2), "From %u MHz upward", oc_cpu_ladder[from]);
    publish_textpage("Overclock Auto-test", l0, l1, l2,
                     "arrows change   Enter start   Esc cancel");

    c->paint_background();

    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + 44 + ROW_H * 2 + 54
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 10;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "Overclock Auto-test", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    /* What is about to happen, before it happens.
     *
     * Every rung is a reboot, and a reboot at a clock that does not work
     * is a dark screen for several seconds. Without this the run is
     * indistinguishable from a board that has hung, and the operator's
     * only reasonable move — pull the power — is the one that abandons
     * it. Saying how long, and what "actually stuck" looks like, is the
     * difference between waiting and panicking. */
    ui_text(s, cx, cy,
            "252, 378 and 504 MHz are tried in turn - the", UI_GREY_5);
    ui_text(s, cx, cy + 12,
            "only clocks HDMI can lock to. Each is a reboot:", UI_GREY_5);
    ui_text(s, cx, cy + 24,
            "the screen goes dark, then comes back here.", UI_GREY_5);

    for (int i = 0; i < 2; i++) {
        const int  ry = cy + 44 + i * ROW_H;
        const bool on = (i == row);
        char val[16];

        if (on) ui_fill(s, cx - 2, ry - 3, cw + 4, ROW_H - 1, UI_ACCENT_L);

        ui_text(s, cx, ry, i == 0 ? "Core voltage" : "Soak per step", UI_BLACK);
        if (i == 0) snprintf(val, sizeof(val), "%s", vreg_text(VREG[sel->vreg]));
        else        snprintf(val, sizeof(val), "%u s", oc_soak_choices[sel->soak]);

        const int vx = cx + cw - ui_text_width(val);
        ui_text(s, vx, ry, val, UI_BLACK);
        if (on) {
            ui_text(s, vx - 16, ry, "<", UI_GREY_5);
            ui_text(s, cx + cw + 6, ry, ">", UI_GREY_5);
        }
    }

    const int ny = cy + 44 + ROW_H * 2 + 8;
    ui_separator(s, cx, ny, cw);

    /* The number that decides whether a dark screen is normal. Each rung
     * costs a boot, its soak, a boot back to 252 to show the result, and
     * a retry if it fails - so the worst case is what to quote. */
    char est[64];
    const unsigned per = (oc_soak_choices[sel->soak] + 8u) * 2u + 8u;
    snprintf(est, sizeof(est), "About %u min. Dark for up to %u s at a time -",
             (oc_cpu_ladder_len * per + 59u) / 60u, per);
    ui_text(s, cx, ny + 8, est, UI_GREY_5);
    ui_text(s, cx, ny + 20,
            "longer than that and it is stuck: power off,", UI_GREY_5);
    ui_text(s, cx, ny + 32,
            "which cancels the run and loses nothing.", UI_GREY_5);

    ui_clip_reset(s);

    s_start_w  = ui_button_width("Start");
    s_start_h  = 18;
    s_start_x  = x + DLG_W - 16 - s_start_w;
    s_start_y  = y + h - UI_WIN_PAD - DLG_BOT - 18;
    s_cancel_w = ui_button_width("Cancel");
    s_cancel_x = s_start_x - 8 - s_cancel_w;

    ui_button(s, s_cancel_x, s_start_y, s_cancel_w, s_start_h, "Cancel",
              true, false, true);
    ui_button(s, s_start_x, s_start_y, s_start_w, s_start_h, "Start",
              true, false, true);

    ui_video_present();
}

/* Ask for a candidate and go. Never returns. */
static void launch(const oc_campaign_t *camp) {
    oc_request_t cur;
    oc_request_current(&cur);

    const uint16_t mhz = oc_cpu_ladder[camp->trying];
    const oc_request_t r = {
        .cpu_mhz = mhz,
        /* No override means the clock's own voltage. Pairing them is
         * what the shipping firmwares do, and it is the pairing that has
         * actually been booted on this silicon. */
        .vreg_sel = camp->vreg_sel ? camp->vreg_sel : oc_vreg_for_clock(mhz),
        /* The ceilings come from whatever is in force. Both are divided
         * down from the system clock against a maximum, so they follow
         * the candidate on their own — see dlg_overclock.c. */
        .psram_mhz = cur.psram_mhz,
        .flash_mhz = cur.flash_mhz,
    };

    printf("[octune] trying %u MHz at %s, soak %u s%s\n",
           r.cpu_mhz, vreg_text(r.vreg_sel), camp->soak_s,
           camp->retry ? " (retry)" : "");

    oc_campaign_store(camp);
    oc_request_set(&r);
    stdio_flush();
    sleep_ms(50);
    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();
}

/* Back to stock so the operator can see the table, then on to `next`.
 * Never returns. No clock request, so this boot comes up at 252 where
 * the picture works. */
static void show_then(oc_campaign_t *camp, unsigned next) {
    camp->state  = OC_CAMP_SHOWING;
    camp->trying = (uint8_t)next;
    camp->retry  = 0;
    oc_campaign_store(camp);
    stdio_flush();
    sleep_ms(50);
    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();
}

void dlg_octune(const dlg_ctx_t *c) {
    oc_request_t cur;
    oc_request_current(&cur);

    /* Every rung, including the one running now.
     *
     * It used to start above the current clock, on the reasoning that a
     * clock this firmware is executing at needs no proof. That is true
     * of booting and not of the soak, and it made the result table open
     * with rows that had never been tried — which is half of why the
     * table said so little. Three rungs is cheap enough to just do them
     * all, and a 252 that fails its own soak is worth knowing about. */
    const unsigned from = 0;

    setup_t sel = { .vreg = 0, .soak = 1 };
    for (int i = 0; i < VREG_LEN; i++) if (VREG[i] == cur.vreg_sel) sel.vreg = i;

    int  row  = 0;
    bool stop = false;

    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }
    ui_cursor_overlay_reset();
    draw_setup(c, &sel, row, from);

    while (!stop) {
        ui_input_task();

        bool dirty = false;
        bool go    = false;
        int  k     = ui_input_getkey();

        while (k != UI_KEY_NONE) {
            int *v = (row == 0) ? &sel.vreg : &sel.soak;
            const int n = (row == 0) ? VREG_LEN : (int)oc_soak_choices_len;

            switch (k) {
                case UI_KEY_UP:    if (row > 0) row--; dirty = true; break;
                case UI_KEY_DOWN:  if (row < 1) row++; dirty = true; break;
                case UI_KEY_LEFT:  if (*v > 0)     { (*v)--; dirty = true; } break;
                case UI_KEY_RIGHT: if (*v < n - 1) { (*v)++; dirty = true; } break;
                case UI_KEY_ESC:   stop = true; break;
                case UI_KEY_ENTER: go = true; stop = true; break;
                default: break;
            }
            k = ui_input_getkey();
        }

        const ui_pointer_t *pt = ui_input_pointer();
        if (pt->pressed && pt->y >= s_start_y && pt->y < s_start_y + s_start_h) {
            if (pt->x >= s_start_x && pt->x < s_start_x + s_start_w) {
                go = true; stop = true;
            }
            if (pt->x >= s_cancel_x && pt->x < s_cancel_x + s_cancel_w)
                stop = true;
        }

        if (go) {
            oc_campaign_t camp = {
                .state    = OC_CAMP_RUNNING,
                .trying   = (uint8_t)from,
                .vreg_sel = VREG[sel.vreg],
                .soak_s   = oc_soak_choices[sel.soak],
            };
            ui_textpage_modal_clear();
            launch(&camp);              /* does not return */
        }

        if (dirty) draw_setup(c, &sel, row, from);
        else if (pt->moved && pt->present) ui_cursor_overlay_move(pt->x, pt->y);

        sleep_ms(8);
    }

    ui_textpage_modal_clear();
}

/* ------------------------------------------------------------------ */
/* The run, one candidate per boot                                     */
/* ------------------------------------------------------------------ */

static void draw_progress(const dlg_ctx_t *c, const oc_campaign_t *camp,
                          const soak_result_t *r, int permille,
                          bool finished, bool showing) {
    ui_surface_t *s = ui_video_surface();
    const int best = oc_campaign_best(camp);

    char l0[40], l1[40], l2[40];
    if (finished) {
        if (best < 0) snprintf(l0, sizeof(l0), "No clock held.");
        else snprintf(l0, sizeof(l0), "Highest that held: %u MHz",
                      oc_cpu_ladder[best]);
        snprintf(l1, sizeof(l1), "%u s soak per clock", camp->soak_s);
        snprintf(l2, sizeof(l2), "Retried once where it failed.");
    } else if (showing) {
        snprintf(l0, sizeof(l0), "Next: %u MHz", oc_cpu_ladder[camp->trying]);
        snprintf(l1, sizeof(l1), "The screen goes dark while it reboots.");
        snprintf(l2, sizeof(l2), "%s",
                 best < 0 ? "Nothing has held yet." : "");
    } else {
        snprintf(l0, sizeof(l0), "Testing %u MHz", oc_cpu_ladder[camp->trying]);
        snprintf(l1, sizeof(l1), "Soak %d%%, %u passes", permille / 10,
                 r->iterations);
        snprintf(l2, sizeof(l2), " ");
    }
    publish_textpage("Overclock Auto-test", l0, l1, l2,
                     finished ? "Enter closes" : "Esc stops the run");

    c->paint_background();

    /* Sized to what is drawn, and nothing more.
     *
     * This used to reserve DLG_FOOT plus a button row, which no version
     * of this window has ever drawn — it closes on a keypress. On a
     * nine-rung ladder the dead strip along the bottom was most of a
     * button's height with nothing in it. */
    const int rungs = (int)oc_cpu_ladder_len;
    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + 22 + rungs * 12 + 6
                + 44 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 10;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "Overclock Auto-test", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    ui_text(s, cx, cy, l0, UI_BLACK);
    draw_ladder(s, cx, cy + 22, cw, camp, !finished && !showing);

    const int ny = cy + 22 + rungs * 12 + 6;
    ui_separator(s, cx, ny, cw);

    if (finished) {
        ui_text(s, cx, ny + 8, l1, UI_GREY_5);
        ui_text(s, cx, ny + 20,
                "Survived a soak, which is not the same as", UI_GREY_5);
        ui_text(s, cx, ny + 32, "proven stable.", UI_GREY_5);
    } else if (showing) {
        ui_text(s, cx, ny + 8, l1, UI_GREY_5);
        ui_text(s, cx, ny + 20, "Back here in a few seconds.", UI_GREY_5);
    } else {
        ui_progress(s, cx, ny + 8, cw, 10, permille);
        char det[48];
        snprintf(det, sizeof(det), "%u passes   flash %u  psram %u  core %u",
                 r->iterations, r->flash_errors, r->psram_errors,
                 r->core_errors);
        ui_text(s, cx, ny + 24, det, UI_GREY_5);
    }

    ui_clip_reset(s);
    ui_video_present();
}

/* Run the soak for this candidate. Returns false if the operator stopped
 * it, in which case the campaign is abandoned rather than recorded. */
static bool run_soak(const dlg_ctx_t *c, const oc_campaign_t *camp,
                     soak_result_t *out) {
    memset(out, 0, sizeof(*out));

    const detect_result_t *d = c->detect;
    const uint32_t ref = mem_test_flash_crc(64u * 1024u);

    /* A slice of the PSRAM rather than all of it. The whole part takes
     * seconds to sweep, which would make the progress bar move once per
     * candidate and Esc take just as long to be noticed. A megabyte is
     * more than the XIP cache by a wide margin, which is the property
     * that matters.
     *
     * QMI-mapped parts only. mem_test_psram_sweep() works through the
     * XIP window, and megafrank's PSRAM is bit-banged on three ordinary
     * GPIOs with no window to read — sweeping it here would be reading
     * an address space nothing answers at, and calling whatever came
     * back an overclock failure. tests_psram_spi.c is where that part is
     * exercised. */
    const uint32_t psram_slice =
        (d && d->psram_bytes && !d->psram_softspi)
            ? ((d->psram_bytes < 1024u * 1024u) ? d->psram_bytes
                                                : 1024u * 1024u)
            : 0u;

    const absolute_time_t started = get_absolute_time();
    const int64_t total_us = (int64_t)camp->soak_s * 1000000;

    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }
    draw_progress(c, camp, out, 0, false, false);

    while (true) {
        const int64_t done = absolute_time_diff_us(started, get_absolute_time());
        if (done >= total_us) break;

        /* Petted here and nowhere else in the soak. A candidate that
         * takes the core down stops petting by definition, and the
         * watchdog is what turns that into a reboot rather than a board
         * that has to be unplugged. */
        watchdog_update();

        if (!torture_pass((uint32_t)out->iterations * 2654435761u + 1u))
            out->core_errors++;

        if (mem_test_flash_crc(64u * 1024u) != ref) out->flash_errors++;

        if (psram_slice) {
            uint32_t wk = 0, rk = 0, errs = 0;
            if (!mem_test_psram_sweep(psram_slice, &wk, &rk, &errs) || errs)
                out->psram_errors++;
        }

        out->iterations++;

        ui_input_task();
        int k = ui_input_getkey();
        while (k != UI_KEY_NONE) {
            if (k == UI_KEY_ESC) return false;
            k = ui_input_getkey();
        }

        draw_progress(c, camp, out,
                      (int)((done * 1000) / total_us), false, false);
    }

    watchdog_update();
    return true;
}

void dlg_octune_resume(const dlg_ctx_t *c, bool applied) {
    oc_campaign_t camp;
    if (!oc_campaign_load(&camp)) return;
    if (camp.state != OC_CAMP_RUNNING && camp.state != OC_CAMP_SHOWING) return;

    if (camp.trying >= oc_cpu_ladder_len) {   /* cannot happen; not trusted */
        oc_campaign_clear();
        return;
    }

    /* The between-rungs pause: back at stock, picture working, table on
     * screen for long enough to read. Then straight on to the next
     * candidate without asking — an unattended run that stops for a
     * keypress is not unattended. */
    if (camp.state == OC_CAMP_SHOWING) {
        watchdog_disable();
        soak_result_t none;
        memset(&none, 0, sizeof(none));
        draw_progress(c, &camp, &none, 0, false, true);

        const absolute_time_t until = make_timeout_time_ms(4000);
        while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
            ui_input_task();
            if (ui_input_getkey() == UI_KEY_ESC) {
                printf("[octune] stopped by the operator\n");
                oc_campaign_clear();
                ui_textpage_modal_clear();
                return;
            }
            sleep_ms(8);
        }
        camp.state = OC_CAMP_RUNNING;
        ui_textpage_modal_clear();
        launch(&camp);                       /* does not return */
    }

    const uint16_t want = oc_cpu_ladder[camp.trying];
    const uint16_t got  = (uint16_t)(clock_get_hz(clk_sys) / 1000000u);

    bool held = false;
    bool abandoned = false;

    if (!applied) {
        /* No request was pending on a boot the campaign expected to be
         * an attempt. Something reset the board between the request
         * being made and this point, which is the watchdog doing its
         * job. The candidate is a failure and the log says why. */
        printf("[octune] %u MHz did not reach the interface\n", want);
    } else if (got != want) {
        /* set_sys_clock_khz refused the frequency and main() fell back.
         * The board is fine; this clock is simply not one the PLL can
         * make. Recorded as a failure because it is one, from the
         * operator's point of view. */
        printf("[octune] %u MHz was not reachable (running at %u)\n", want, got);
    } else {
        soak_result_t r;
        if (!run_soak(c, &camp, &r)) {
            abandoned = true;
        } else {
            held = soak_clean(&r);
            printf("[octune] %u MHz: %u passes, flash %u psram %u core %u - %s\n",
                   want, r.iterations, r.flash_errors, r.psram_errors,
                   r.core_errors, held ? "held" : "failed");
        }
    }

    if (abandoned) {
        printf("[octune] stopped by the operator\n");
        oc_campaign_clear();
        watchdog_disable();
        ui_textpage_modal_clear();
        return;
    }

    /* A failed rung gets a second attempt before it is believed, and a
     * believed failure no longer ends the run.
     *
     * Both of those were wrong in the first version, and hardware said
     * so. On a Core 2U master, 378 MHz failed while 504 MHz soaked clean
     * with 129 passes — so the ladder is not monotonic, and stopping at
     * the first failure reported 336 on a board that runs 504. The same
     * 504 rung then failed on later attempts with identical settings, so
     * a single attempt does not settle a rung either.
     *
     * Climbing the whole ladder costs a few more reboots and is the only
     * way the answer is about the board rather than about which attempt
     * happened to land. `best` stays the highest rung that held, so a
     * gap below it is visible rather than fatal. */
    if (!held && !camp.retry) {
        printf("[octune] %u MHz failed; trying it once more\n", want);
        camp.retry = 1;
        ui_textpage_modal_clear();
        launch(&camp);                   /* does not return */
    }

    /* Record the verdict for this rung, then move on. */
    camp.tested |= (uint8_t)(1u << camp.trying);
    if (held) camp.passed |= (uint8_t)(1u << camp.trying);
    camp.retry = 0;

    if ((unsigned)camp.trying + 1u < oc_cpu_ladder_len) {
        ui_textpage_modal_clear();
        show_then(&camp, (unsigned)camp.trying + 1u);   /* does not return */
    }
    /* Off the top of the ladder. */

    /* Done, one way or the other. The watchdog goes away first: from
     * here on this is an ordinary session, and a firmware that reboots
     * itself because an operator spent nine seconds reading a dialog
     * would be a far worse bug than the one it was guarding against. */
    watchdog_disable();

    camp.state = OC_CAMP_DONE;
    oc_campaign_store(&camp);

    {
        const int best = oc_campaign_best(&camp);
        if (best < 0) printf("[octune] finished: no clock held\n");
        else printf("[octune] finished: %u MHz held for %u s\n",
                    oc_cpu_ladder[best], camp.soak_s);
    }

    /* The result, until it is dismissed. Held rather than flashed: this
     * is the answer the whole run existed to produce, and five minutes
     * of reboots ending in a dialog that timed out while nobody was
     * looking would be the one unforgivable outcome. */
    soak_result_t empty;
    memset(&empty, 0, sizeof(empty));
    draw_progress(c, &camp, &empty, 1000, true, false);

    while (true) {
        ui_input_task();
        const int k = ui_input_getkey();
        if (k == UI_KEY_ENTER || k == UI_KEY_ESC) break;
        const ui_pointer_t *pt = ui_input_pointer();
        if (pt->pressed) break;
        sleep_ms(8);
    }

    oc_campaign_clear();
    ui_textpage_modal_clear();
}
