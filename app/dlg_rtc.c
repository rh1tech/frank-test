/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_rtc.c — setting the wall clock on the boards that have one.
 *
 *
 * WHY THIS EXISTS
 *
 * The DS3231 was read-only in this firmware: the report stamped itself
 * from it and the RTC test watched its oscillator, and neither had any
 * way to correct a part that had never been set or had lost its coin
 * cell. A test rig that prints 2000-01-01 on every report and cannot fix
 * it is publishing a wrong date, once per board, for as long as it is in
 * service.
 *
 *
 * ENTER WITH NOTHING CHANGED IS NOT A NO-OP
 *
 * It is the "just clear the flag" case, and that is deliberate rather
 * than a happy accident.
 *
 * The oscillator-stop flag latches whenever the part has lost time and
 * is the only evidence that a battery has gone flat, so the RTC test
 * reports it and this firmware never clears it on its own. But a board
 * whose clock is *right* and whose flag is *stale* — the usual state
 * after someone has set it from a console, or after a cell was replaced
 * while the rig was powered — has no fault to report and a row that says
 * it does.
 *
 * So the dialog opens on the time the part currently holds. Pressing
 * Enter writes that same time back and clears the flag with it, which is
 * exactly the minimum the operator asked for, using the control that was
 * already here rather than a second one that means something subtly
 * different.
 *
 *
 * FIVE FIELDS, NOT SIX
 *
 * Seconds are written as zero. Nobody sets a clock to the second from a
 * keyboard, the write lands whenever the operator gets round to pressing
 * Enter, and a seconds field would advertise a precision the method does
 * not have.
 */

#include "dlgs.h"

#include "rtc_ds3231.h"
#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_input.h"
#include "ui_video.h"
#include "ui_textpage.h"
#include "ui_window.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define DLG_W   400
#define ROW_H    18
#define ROWS      5

static int s_set_x, s_set_y, s_set_w, s_set_h;
static int s_cancel_x, s_cancel_w;

static const char *const ROW_NAME[ROWS] = {
    "Year", "Month", "Day", "Hour", "Minute"
};

/* Step one field, wrapping at its own ends. The day is clamped rather
 * than wrapped when the month or the year moves under it: an operator
 * who sets 31 January and then walks the month to February should get
 * the 28th, not a date the part will refuse. */
static void bump(rtc_time_t *t, int row, int delta) {
    switch (row) {
        case 0:
            if (delta > 0 && t->year < 2099u) t->year++;
            if (delta < 0 && t->year > 2000u) t->year--;
            break;
        case 1:
            t->mon = (uint8_t)((t->mon - 1 + 12 + delta) % 12 + 1);
            break;
        case 2: {
            const uint8_t n = rtc_days_in_month(t->year, t->mon);
            t->day = (uint8_t)((t->day - 1 + n + delta) % n + 1);
            return;                       /* no clamp needed, it just moved */
        }
        case 3: t->hour = (uint8_t)((t->hour + 24 + delta) % 24); break;
        default: t->min = (uint8_t)((t->min + 60 + delta) % 60); break;
    }

    const uint8_t n = rtc_days_in_month(t->year, t->mon);
    if (t->day > n) t->day = n;
}

static void field_text(const rtc_time_t *t, int row, char *out, unsigned len) {
    static const char *const MON[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    switch (row) {
        case 0:  snprintf(out, len, "%u", (unsigned)t->year); break;
        /* Named, not numbered. A rig is used by people in more than one
         * country and 03/04 is a different day in each of them. */
        case 1:  snprintf(out, len, "%s", MON[t->mon - 1u]);  break;
        case 2:  snprintf(out, len, "%u", (unsigned)t->day);  break;
        case 3:  snprintf(out, len, "%02u", (unsigned)t->hour); break;
        default: snprintf(out, len, "%02u", (unsigned)t->min);  break;
    }
}

/* ------------------------------------------------------------------ */

static char s_tp[ROWS + 1][40];
static const char *s_tp_lines[ROWS + 1];

static void publish_textpage(const rtc_time_t *t, bool osf, int row) {
    char val[16];
    for (int i = 0; i < ROWS; i++) {
        field_text(t, i, val, sizeof(val));
        snprintf(s_tp[i], sizeof(s_tp[i]), "%-8s %s", ROW_NAME[i], val);
    }
    snprintf(s_tp[ROWS], sizeof(s_tp[ROWS]), "Power-loss flag: %s",
             osf ? "set" : "clear");

    for (int i = 0; i <= ROWS; i++) s_tp_lines[i] = s_tp[i];
    ui_textpage_modal("Date and Time", s_tp_lines, ROWS + 1, row,
                      "arrows change   Enter set   Esc cancel");
}

static void draw(const dlg_ctx_t *c, const rtc_time_t *t, bool osf, int row) {
    ui_surface_t *s = ui_video_surface();
    publish_textpage(t, osf, row);
    c->paint_background();

    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + 30 + ROW_H * ROWS + 42
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 10;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "Date and Time", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    ui_text(s, cx, cy, "The DS3231. Seconds are set to zero.", UI_GREY_5);

    char val[16];
    for (int i = 0; i < ROWS; i++) {
        const int  ry = cy + 22 + i * ROW_H;
        const bool on = (i == row);

        if (on) ui_fill(s, cx - 2, ry - 3, cw + 4, ROW_H - 1, UI_ACCENT_L);
        ui_text(s, cx, ry, ROW_NAME[i], UI_BLACK);

        field_text(t, i, val, sizeof(val));
        const int vx = cx + cw - ui_text_width(val);
        ui_text(s, vx, ry, val, UI_BLACK);
        if (on) {
            ui_text(s, vx - 16, ry, "<", UI_GREY_5);
            ui_text(s, cx + cw + 6, ry, ">", UI_GREY_5);
        }
    }

    const int ny = cy + 22 + ROW_H * ROWS + 8;
    ui_separator(s, cx, ny, cw);

    /* The flag, and what pressing Enter will do about it. This is the
     * whole reason an unchanged Enter is worth having. */
    if (osf) {
        ui_text(s, cx, ny + 8,
                "The clock has lost time since it was last", UI_GREY_5);
        ui_text(s, cx, ny + 20,
                "set. Setting it clears that flag.", UI_GREY_5);
    } else {
        ui_text(s, cx, ny + 8,
                "The clock has not lost time since it was", UI_GREY_5);
        ui_text(s, cx, ny + 20,
                "last set.", UI_GREY_5);
    }

    ui_clip_reset(s);

    s_set_w    = ui_button_width("Set Clock");
    s_set_h    = 18;
    s_set_x    = x + DLG_W - 16 - s_set_w;
    s_set_y    = y + h - UI_WIN_PAD - DLG_BOT - 18;
    s_cancel_w = ui_button_width("Cancel");
    s_cancel_x = s_set_x - 8 - s_cancel_w;

    ui_button(s, s_cancel_x, s_set_y, s_cancel_w, s_set_h, "Cancel",
              true, false, true);
    ui_button(s, s_set_x, s_set_y, s_set_w, s_set_h, "Set Clock",
              true, false, true);

    ui_text(s, cx, s_set_y + 5, "arrows to change, Esc to cancel", UI_GREY_5);

    ui_video_present();
}

/* ------------------------------------------------------------------ */

dlg_rtc_result_t dlg_rtc(const dlg_ctx_t *c) {
    if (!rtc_present(c->detect)) return DLG_RTC_NO_CLOCK;

    rtc_time_t t;
    bool osf = false;

    /* Open on what the part holds, so an unchanged Enter is a flag
     * clear. A part that has never run holds something implausible, and
     * starting an operator on 2000-01-01 makes them walk twenty-six
     * years with an arrow key. */
    if (!rtc_read(c->detect, &t, &osf) || !rtc_time_valid(&t)) {
        rtc_build_time(&t);
        osf = true;
    }
    t.sec = 0;

    int  row  = 0;
    bool stop = false;
    dlg_rtc_result_t result = DLG_RTC_CANCELLED;

    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }
    ui_cursor_overlay_reset();
    draw(c, &t, osf, row);

    while (!stop) {
        ui_input_task();

        bool dirty = false;
        int  k = ui_input_getkey();
        while (k != UI_KEY_NONE) {
            switch (k) {
                case UI_KEY_UP:    if (row > 0) row--;        dirty = true; break;
                case UI_KEY_DOWN:  if (row < ROWS - 1) row++; dirty = true; break;
                case UI_KEY_LEFT:  bump(&t, row, -1);         dirty = true; break;
                case UI_KEY_RIGHT: bump(&t, row, +1);         dirty = true; break;
                case UI_KEY_ESC:   stop = true; break;
                case UI_KEY_ENTER:
                    result = rtc_set(c->detect, &t) ? DLG_RTC_SET
                                                    : DLG_RTC_FAILED;
                    printf("[rtc] set %04u-%02u-%02u %02u:%02u:00 - %s\n",
                           (unsigned)t.year, (unsigned)t.mon, (unsigned)t.day,
                           (unsigned)t.hour, (unsigned)t.min,
                           result == DLG_RTC_SET ? "ok" : "the bus refused it");
                    stop = true;
                    break;
                default: break;
            }
            k = ui_input_getkey();
        }

        const ui_pointer_t *pt = ui_input_pointer();
        if (pt->pressed && pt->y >= s_set_y && pt->y < s_set_y + s_set_h) {
            if (pt->x >= s_set_x && pt->x < s_set_x + s_set_w) {
                result = rtc_set(c->detect, &t) ? DLG_RTC_SET : DLG_RTC_FAILED;
                stop = true;
            }
            if (pt->x >= s_cancel_x && pt->x < s_cancel_x + s_cancel_w)
                stop = true;
        }

        if (dirty) draw(c, &t, osf, row);
        else if (pt->moved && pt->present) ui_cursor_overlay_move(pt->x, pt->y);

        sleep_ms(8);
    }

    ui_textpage_modal_clear();
    return result;
}
