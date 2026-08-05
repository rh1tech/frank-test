/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_overclock.c — clocks and voltage for the next boot.
 *
 *
 * WHY IT IS NOT SAVED
 *
 * This is the one setting where persistence is actively dangerous.
 * Every other choice in this firmware can be undone from the interface,
 * because the interface still comes up. A clock the board cannot hold
 * fails during start-up, before anything is on screen to offer the way
 * back, and the fix would be a debug probe and an erase — on a rig whose
 * whole purpose is not needing one.
 *
 * So it rides a watchdog scratch register across exactly one reboot and
 * is consumed there. Pull the power and the board is back to what it was
 * built with. That is a recovery every operator already knows, and it
 * means the honest way to use this is to try the setting you doubt,
 * rather than the one you are sure of.
 *
 *
 * WHAT IS ON OFFER
 *
 * The clocks the fleet's firmwares actually use, rather than a slider.
 * 252 for HSTX video, 378 for the composite encoder, 504 where a board
 * has been pushed that far — those are the numbers that have been run in
 * anger, and a list of them is more useful than the freedom to ask for
 * 391.
 *
 * Voltage is separate because the two are not the same question. Stock
 * is 1.10 V and anything at or under 1.30 needs no permission; above
 * that the regulator refuses the write unless the limit comes off first,
 * which is handled in main() and is exactly how an overclock fails in a
 * way that looks like bad silicon rather than a setting.
 *
 * The flash and PSRAM ceilings are here because they have to move with
 * the clock. Both dividers are derived from the system clock against a
 * maximum, so a board taken to 504 with the flash ceiling left at 88
 * simply divides harder; leave them alone and the parts stay in spec
 * while the core does not.
 */

#include "dlgs.h"

#include "oc_request.h"
#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_input.h"
#include "ui_video.h"
#include "ui_textpage.h"
#include "ui_window.h"

#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define DLG_W   440
#define ROW_H    18

static int s_apply_x, s_apply_y, s_apply_w, s_apply_h;
static int s_cancel_x, s_cancel_w;

/* The clocks the fleet actually runs. 150 is the SDK default and the way
 * back if something has been pushed too far. */
static const uint16_t CPU[]   = { 150, 200, 252, 264, 300, 308, 336, 378,
                                  396, 420, 450, 504 };
/* Selector values, not millivolts — see hardware/vreg.h. Stops at 1.65,
 * which is as far as any firmware in this fleet has gone. */
static const uint8_t  VREG[]  = { 0, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                                  0x10, 0x11, 0x12, 0x13, 0x14 };
static const uint8_t  PSRAM[] = { 84, 109, 133, 150, 166, 200 };
static const uint8_t  FLASH[] = { 66, 88, 100, 133 };

static const char *vreg_text(uint8_t sel) {
    static char buf[12];
    if (sel == 0) return "default";

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

/* Index of `v` in `arr`, or the nearest one at or below it. */
static int index_of(const uint16_t *arr, int n, uint16_t v) {
    int best = 0;
    for (int i = 0; i < n; i++) if (arr[i] <= v) best = i;
    return best;
}
static int index_of8(const uint8_t *arr, int n, uint8_t v) {
    for (int i = 0; i < n; i++) if (arr[i] == v) return i;
    return 0;
}

typedef struct { int cpu, vreg, psram, flash; } sel_t;

/* The text-page version - see ui_textpage_modal(). The only dialog here
 * the operator changes values in rather than watches, so this is the one
 * that needs the selected row marked: without it the arrow keys move
 * something invisible. */
static char s_tp[4][40];
static const char *s_tp_lines[4];

static void publish_textpage(const sel_t *sel, int row) {
    snprintf(s_tp[0], sizeof(s_tp[0]), "System clock   %u MHz", CPU[sel->cpu]);
    snprintf(s_tp[1], sizeof(s_tp[1]), "Core voltage   %s",
             vreg_text(VREG[sel->vreg]));
    snprintf(s_tp[2], sizeof(s_tp[2]), "PSRAM ceiling  %u MHz", PSRAM[sel->psram]);
    snprintf(s_tp[3], sizeof(s_tp[3]), "Flash ceiling  %u MHz", FLASH[sel->flash]);

    for (int i = 0; i < 4; i++) s_tp_lines[i] = s_tp[i];
    ui_textpage_modal("Clocks and Voltage", s_tp_lines, 4, row,
                      "arrows change   Enter restart   Esc cancel");
}

static void draw(const dlg_ctx_t *c, const sel_t *sel, int row) {
    ui_surface_t *s = ui_video_surface();
    publish_textpage(sel, row);
    c->paint_background();

    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + 30 + ROW_H * 4 + 42
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 10;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "Clocks and Voltage", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    ui_text(s, cx, cy, "Applied on the next boot, and only that one.", UI_GREY_5);

    char val[24];

    for (int i = 0; i < 4; i++) {
        const int ry = cy + 22 + i * ROW_H;
        const bool on = (i == row);

        if (on) ui_fill(s, cx - 2, ry - 3, cw + 4, ROW_H - 1, UI_ACCENT_L);

        const char *name = (i == 0) ? "System clock"
                         : (i == 1) ? "Core voltage"
                         : (i == 2) ? "PSRAM ceiling" : "Flash ceiling";
        ui_text(s, cx, ry, name, UI_BLACK);

        switch (i) {
            case 0: snprintf(val, sizeof(val), "%u MHz", CPU[sel->cpu]); break;
            case 1: snprintf(val, sizeof(val), "%s", vreg_text(VREG[sel->vreg])); break;
            case 2: snprintf(val, sizeof(val), "%u MHz", PSRAM[sel->psram]); break;
            default: snprintf(val, sizeof(val), "%u MHz", FLASH[sel->flash]); break;
        }

        /* Arrows on the selected row only, so it is obvious which one
         * left and right will act on. */
        const int vx = cx + cw - ui_text_width(val);
        ui_text(s, vx, ry, val, UI_BLACK);
        if (on) {
            ui_text(s, vx - 16, ry, "<", UI_GREY_5);
            ui_text(s, cx + cw + 6, ry, ">", UI_GREY_5);
        }
    }

    const int ny = cy + 22 + ROW_H * 4 + 8;
    ui_separator(s, cx, ny, cw);
    ui_text(s, cx, ny + 8,
            "Nothing is stored. If the board will not start,", UI_GREY_5);
    ui_text(s, cx, ny + 20,
            "power it off and on and it returns to defaults.", UI_GREY_5);

    ui_clip_reset(s);

    s_apply_w  = ui_button_width("Apply and Restart");
    s_apply_h  = 18;
    s_apply_x  = x + DLG_W - 16 - s_apply_w;
    s_apply_y  = y + h - UI_WIN_PAD - DLG_BOT - 18;
    s_cancel_w = ui_button_width("Cancel");
    s_cancel_x = s_apply_x - 8 - s_cancel_w;

    ui_button(s, s_cancel_x, s_apply_y, s_cancel_w, s_apply_h, "Cancel",
              true, false, true);
    ui_button(s, s_apply_x, s_apply_y, s_apply_w, s_apply_h,
              "Apply and Restart", true, false, true);

    ui_text(s, cx, s_apply_y + 5, "arrows to change, Esc to cancel", UI_GREY_5);

    ui_video_present();
}

/* ------------------------------------------------------------------ */

void dlg_overclock(const dlg_ctx_t *c) {
    oc_request_t cur;
    oc_request_current(&cur);

    sel_t sel = {
        .cpu   = index_of(CPU, (int)(sizeof(CPU) / sizeof(CPU[0])), cur.cpu_mhz),
        .vreg  = index_of8(VREG, (int)sizeof(VREG), cur.vreg_sel),
        .psram = index_of8(PSRAM, (int)sizeof(PSRAM), cur.psram_mhz),
        .flash = index_of8(FLASH, (int)sizeof(FLASH), cur.flash_mhz),
    };

    int  row  = 0;
    bool stop = false;

    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }
    ui_cursor_overlay_reset();
    draw(c, &sel, row);

    while (!stop) {
        ui_input_task();

        bool dirty = false;
        int k = ui_input_getkey();
        while (k != UI_KEY_NONE) {
            int *v = (row == 0) ? &sel.cpu : (row == 1) ? &sel.vreg
                   : (row == 2) ? &sel.psram : &sel.flash;
            const int n = (row == 0) ? (int)(sizeof(CPU) / sizeof(CPU[0]))
                        : (row == 1) ? (int)sizeof(VREG)
                        : (row == 2) ? (int)sizeof(PSRAM) : (int)sizeof(FLASH);

            switch (k) {
                case UI_KEY_UP:    if (row > 0) row--; dirty = true; break;
                case UI_KEY_DOWN:  if (row < 3) row++; dirty = true; break;
                case UI_KEY_LEFT:  if (*v > 0)     { (*v)--; dirty = true; } break;
                case UI_KEY_RIGHT: if (*v < n - 1) { (*v)++; dirty = true; } break;
                case UI_KEY_ESC:   stop = true; break;
                case UI_KEY_ENTER: {
                    const oc_request_t r = {
                        .cpu_mhz   = CPU[sel.cpu],
                        .vreg_sel  = VREG[sel.vreg],
                        .psram_mhz = PSRAM[sel.psram],
                        .flash_mhz = FLASH[sel.flash],
                    };
                    printf("[oc] next boot: %u MHz, vreg %s, psram %u, flash %u\n",
                           r.cpu_mhz, vreg_text(r.vreg_sel), r.psram_mhz,
                           r.flash_mhz);
                    oc_request_set(&r);
                    stdio_flush();
                    sleep_ms(50);
                    watchdog_reboot(0, 0, 0);
                    while (true) tight_loop_contents();
                }
                default: break;
            }
            k = ui_input_getkey();
        }

        const ui_pointer_t *pt = ui_input_pointer();
        if (pt->pressed && pt->y >= s_apply_y && pt->y < s_apply_y + s_apply_h) {
            if (pt->x >= s_apply_x && pt->x < s_apply_x + s_apply_w) {
                const oc_request_t r = {
                    .cpu_mhz   = CPU[sel.cpu],
                    .vreg_sel  = VREG[sel.vreg],
                    .psram_mhz = PSRAM[sel.psram],
                    .flash_mhz = FLASH[sel.flash],
                };
                oc_request_set(&r);
                stdio_flush();
                sleep_ms(50);
                watchdog_reboot(0, 0, 0);
                while (true) tight_loop_contents();
            }
            if (pt->x >= s_cancel_x && pt->x < s_cancel_x + s_cancel_w)
                stop = true;
        }

        if (dirty) draw(c, &sel, row);
        else if (pt->moved && pt->present) ui_cursor_overlay_move(pt->x, pt->y);

        sleep_ms(8);
    }

    ui_textpage_modal_clear();
}
