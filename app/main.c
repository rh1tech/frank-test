/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * main.c — the application.
 *
 * Boot order is not arbitrary:
 *
 *   1. clocks and vreg        — the HSTX path needs a clock it can
 *                               divide to 126 MHz; see ui_video_hstx.c
 *   2. console                — so a failure after this point is visible
 *   3. detection              — must run before anything claims the pins
 *                               it probes, and before core 1 starts:
 *                               the flash identify step drops the QMI out
 *                               of XIP and would hang a core fetching
 *                               from flash
 *   4. video                  — launches core 1 for scanout
 *   5. tests, redrawing between each
 */

#include "detect.h"
#include "dlgs.h"
#include "frank_audio.h"
#include "mem_test.h"
#include "registry.h"

void tests_link_init(const detect_result_t *d);
bool tests_link_poll(void);
#include "settings.h"
#include "ui_desktop.h"
#include "ui_input.h"
#include "ui_video.h"
#include "video_detect.h"
#include "video_select.h"

#include "hardware/clocks.h"
#include "hardware/structs/timer.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "hardware/structs/qmi.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.2"
#endif

static detect_result_t    g_detect;
static video_detect_t     g_video;
static video_choice_t     g_choice;

/* The stored "keep the serial console" preference, read once at start-up
 * and toggled from File > Serial Console. Only meaningful on boards
 * where the PS/2 mouse and the console UART share GP0/GP1. */
static bool               g_console_kept;
static registry_results_t g_results;
static ui_menubar_t       g_menubar;
static ui_desktop_t       g_desk;

static char g_mcu_line[32];
static char g_serial[24];

/* Progress marker. This is a diagnostic firmware, so "it stopped and
 * told you nothing" is the one failure mode it must not have. */
static void stage(const char *what) {
    printf("[boot] %s\n", what);
    stdio_flush();
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

/* The row a test is currently narrating into. */
static unsigned g_running_row;

static void test_progress(int permille, const char *status);

static bool g_dirty = true;
static bool g_menu_only = false;

extern volatile uint32_t ui_hstx_vsync_count;
extern volatile uint32_t ui_hstx_swap_timeouts;
static uint32_t g_full_redraws, g_menu_redraws;

static void redraw(void);

static void redraw(void);

/* Repaint only the rectangle a menu occupies.
 *
 * There is no back buffer — the scanline renderer reads the framebuffer
 * in place — so a full recomposition rewrites pixels the beam is
 * displaying, and arrowing through a menu makes the entire screen
 * shimmer. The union of where the menu was and where it now is covers
 * everything that can have changed. */
static int g_last_menu_x, g_last_menu_y, g_last_menu_w, g_last_menu_h;

static void redraw_menu_area(void) {
    /* Composite draws its own screen and knows nothing of the menu's
     * rectangle, so a partial repaint would leave it showing the report
     * with the menu invisible. Send it the full path instead. */
    if (!ui_video_scans_front_buffer()) { redraw(); return; }

    g_menu_redraws++;
    int nx, ny, nw, nh;
    bool open = ui_menubar_dropdown_rect(&g_menubar, &nx, &ny, &nw, &nh);

    /* Union with the previous rectangle so a menu that just closed, or
     * one that grew, leaves nothing behind. */
    int x0 = 0, y0 = 0, x1 = UI_SCREEN_W, y1 = UI_MENUBAR_H;
    if (open) {
        if (nx < x0) x0 = nx;
        if (ny < y0) y0 = ny;
        if (nx + nw > x1) x1 = nx + nw;
        if (ny + nh > y1) y1 = ny + nh;
    }
    if (g_last_menu_w) {
        if (g_last_menu_x < x0) x0 = g_last_menu_x;
        if (g_last_menu_y < y0) y0 = g_last_menu_y;
        if (g_last_menu_x + g_last_menu_w > x1) x1 = g_last_menu_x + g_last_menu_w;
        if (g_last_menu_y + g_last_menu_h > y1) y1 = g_last_menu_y + g_last_menu_h;
    }

    const ui_pointer_t *pt = ui_input_pointer_last();
    ui_desktop_draw_clipped(ui_video_surface(), &g_desk, &g_menubar,
                            pt->x, pt->y, pt->present,
                            x0, y0, x1 - x0, y1 - y0);
    ui_video_present();

    g_last_menu_x = open ? nx : 0;
    g_last_menu_y = open ? ny : 0;
    g_last_menu_w = open ? nw : 0;
    g_last_menu_h = open ? nh : 0;
    g_dirty = false;
}

static void redraw(void) {
    g_full_redraws++;
    g_desk.rows      = g_results.rows;
    g_desk.row_count = (int)g_results.count;
    g_desk.passed    = (int)g_results.passed;
    g_desk.failed    = (int)g_results.failed;
    g_desk.na        = (int)g_results.na;
    g_desk.remaining = (int)g_results.pending;

    const ui_pointer_t *pt = ui_input_pointer_last();
    ui_desktop_draw(ui_video_surface(), &g_desk, &g_menubar,
                    pt->x, pt->y, pt->present);
    ui_video_present();
    g_dirty = false;
}

/* Compose the desktop without publishing it.
 *
 * The modal dialogs draw on top of this and present once, so the
 * operator never sees a frame with the background repainted but the
 * dialog not yet drawn. redraw() cannot be used for that: it presents,
 * which is exactly the flash this avoids. */
static void paint_desktop(void) {
    g_desk.rows      = g_results.rows;
    g_desk.row_count = (int)g_results.count;
    g_desk.passed    = (int)g_results.passed;
    g_desk.failed    = (int)g_results.failed;
    g_desk.na        = (int)g_results.na;
    g_desk.remaining = (int)g_results.pending;

    /* Cursor suppressed: the dialog draws over this and would bury it.
     * Each dialog calls ui_desktop_draw_cursor() as its last act. */
    ui_desktop_draw(ui_video_surface(), &g_desk, &g_menubar, 0, 0, false);
}

/* Keep the running test visible. The list is longer than the window, and
 * a run that scrolls off the bottom looks stalled. */
static void follow(unsigned i) {
    const int visible = ui_desktop_rows_shown();
    if ((int)i < g_desk.first_visible)            g_desk.first_visible = (int)i;
    if ((int)i >= g_desk.first_visible + visible) g_desk.first_visible = (int)i - visible + 1;
    g_desk.selected = (int)i;
}

/* ------------------------------------------------------------------ */
/* Board identity                                                      */
/* ------------------------------------------------------------------ */

/* Chosen by the operator this session. RAM only, deliberately.
 *
 * It used to be written to flash so the question would be asked once per
 * board rather than once per boot. That is the wrong trade for a bench
 * rig: a stored answer is invisible, survives the operator changing
 * their mind, and follows the *firmware* rather than the board when an
 * image is moved. A reset should start from what the hardware says. */
static const frank_board_desc_t *g_manual_board;

/* Enable exactly the menu items this board can honour.
 *
 * Called after detection and again whenever the operator overrides the
 * board, because both change the answer. An item that is enabled on
 * hardware it cannot reach is the worst of the three states: it invites
 * a keystroke and then either does nothing or drives pins that belong to
 * something else. */
static void gate_menus(void) {
    static const struct { audio_src_t src; int cmd; } audio_cmds[] = {
        { AUDIO_SRC_PWM, CMD_AUDIO_PWM },
        { AUDIO_SRC_I2S, CMD_AUDIO_I2S },
        { AUDIO_SRC_TS,  CMD_AUDIO_TS  },
    };
    for (unsigned i = 0; i < count_of(audio_cmds); i++)
        ui_desktop_set_cmd_enabled(audio_cmds[i].cmd,
                                   audio_src_available(&g_detect, audio_cmds[i].src));

    const bool nes = g_detect.board &&
                     g_detect.board->id != FRANK_BOARD_UNKNOWN &&
                     (g_detect.board->caps & CAP_GAMEPAD_NES) &&
                     g_detect.board->pins.pad_clk   != PIN_NC &&
                     g_detect.board->pins.pad_latch != PIN_NC &&
                     g_detect.board->pins.pad_d1    != PIN_NC;
    ui_desktop_set_cmd_enabled(CMD_NESPAD, nes);

    /* The dialog is worth opening whenever the board has a port, even
     * with nothing plugged in: "no bytes" against a port that exists is
     * a result, and the panel says what to check. */
    const bool ps2 = g_detect.board &&
                     g_detect.board->id != FRANK_BOARD_UNKNOWN &&
                     (g_detect.board->caps & CAP_PS2) &&
                     (g_detect.board->pins.ps2_kb_clk != PIN_NC ||
                      g_detect.board->pins.ps2_ms_clk != PIN_NC);
    ui_desktop_set_cmd_enabled(CMD_PS2, ps2);

    const bool leds = g_detect.board &&
                      g_detect.board->id != FRANK_BOARD_UNKNOWN &&
                      (((g_detect.board->caps & CAP_LED_PLAIN) &&
                        g_detect.board->pins.led_plain != PIN_NC) ||
                       ((g_detect.board->caps & CAP_LED_WS2812) &&
                        g_detect.board->pins.led_ws2812 != PIN_NC));
    ui_desktop_set_cmd_enabled(CMD_LED, leds);

    /* The tape input needs both the comparator and a pin to read it on.
     * A DIP-gated board still qualifies: the switch is the operator's to
     * close, and the dialog says which one. */
    const bool tape = g_detect.board &&
                      g_detect.board->id != FRANK_BOARD_UNKNOWN &&
                      (g_detect.board->caps & CAP_TAPE_IN) &&
                      g_detect.board->pins.tape_in != PIN_NC;
    ui_desktop_set_cmd_enabled(CMD_TAPE, tape);

    /* A video mode needs both: a connector on the board, and a backend
     * in this firmware that can drive it. Only the first was being
     * checked, so VGA and Composite sat enabled on boards that have the
     * sockets — and selecting them quietly fell back to HDMI, which
     * looks exactly like VGA being broken. */
    static const struct { frank_video_mode_t mode; uint64_t cap; int cmd; } vid[] = {
        { VIDEO_HDMI,      CAP_VIDEO_HDMI,      CMD_VIDEO_HDMI      },
        { VIDEO_VGA,       CAP_VIDEO_VGA,       CMD_VIDEO_VGA       },
        { VIDEO_COMPOSITE, CAP_VIDEO_COMPOSITE, CMD_VIDEO_COMPOSITE },
    };
    /* The mode already running is greyed out too. Selecting it is a
     * no-op that still reads as a command the firmware ignored, and on
     * the analogue outputs it is worse than nothing: the display drops
     * sync while the interface repaints for a change that never
     * happens. */
    const ui_video_backend_t *open_now = ui_video_current();
    for (unsigned i = 0; i < count_of(vid); i++) {
        const bool on_board = g_detect.board &&
                              (g_detect.board->caps & vid[i].cap);
        const bool running  = open_now && open_now->mode == vid[i].mode;
        ui_desktop_set_cmd_enabled(vid[i].cmd,
                                   on_board && !running &&
                                   ui_video_mode_implemented(vid[i].mode));
    }
}

/* ------------------------------------------------------------------ */
/* Selection and hit-testing                                           */
/* ------------------------------------------------------------------ */

/* The interface publishes what it drew.
 *
 * This used to be a copy of ui_desktop.c's constants with a fixed row
 * count beside them, on the reasoning that one place should get to be
 * wrong rather than two. The copy drifted - it believed twenty rows fit
 * where the draw fitted more, which is why the scroll bar never appeared
 * - and rows stopped being a fixed height once a long detail started
 * wrapping onto a second line. Asking is now the only thing that can be
 * right. */
static int hit_row(int x, int y) {
    return ui_desktop_hit_row(&g_desk, x, y);
}

static void scroll_to_selection(void) {
    if (g_desk.selected < 0) return;
    if (g_desk.selected < g_desk.first_visible)
        g_desk.first_visible = g_desk.selected;
    if (g_desk.selected >= g_desk.first_visible + ui_desktop_rows_shown())
        g_desk.first_visible = g_desk.selected - ui_desktop_rows_shown() + 1;
}

static void scroll_by(int rows) {
    g_desk.first_visible += rows;
    int max = (int)g_results.count - ui_desktop_rows_shown();
    if (max < 0) max = 0;
    if (g_desk.first_visible > max) g_desk.first_visible = max;
    if (g_desk.first_visible < 0)   g_desk.first_visible = 0;
}

static void move_selection(int delta) {
    if (!g_results.count) return;
    if (g_desk.selected < 0) g_desk.selected = 0;
    else                     g_desk.selected += delta;
    if (g_desk.selected < 0) g_desk.selected = 0;
    if (g_desk.selected >= (int)g_results.count)
        g_desk.selected = (int)g_results.count - 1;
    scroll_to_selection();
}

/* A modal box, dismissed by any key. On screen, because a menu command
 * whose only effect is a line on a serial port is indistinguishable from
 * one that does nothing — which is exactly how this was reported. */
static char g_dlg[4][40];

static void show_dialog(const char *title, int nlines) {
    g_desk.dialog_title = title;
    for (int i = 0; i < 4; i++)
        g_desk.dialog_body[i] = (i < nlines) ? g_dlg[i] : NULL;
    g_desk.dialog_buttons[0]   = "OK";
    g_desk.dialog_button_count = 1;
    g_desk.dialog_focus        = 0;
    redraw();

    absolute_time_t end = make_timeout_time_ms(20000);
    while (absolute_time_diff_us(get_absolute_time(), end) > 0) {
        ui_input_task();
        if (ui_input_getkey() != UI_KEY_NONE) break;
        const ui_pointer_t *p = ui_input_pointer();
        if (p->pressed) break;
        sleep_ms(5);
    }

    g_desk.dialog_title        = NULL;
    g_desk.dialog_button_count = 0;
    redraw();
}

static void show_about(void) {
    snprintf(g_dlg[0], sizeof(g_dlg[0]), "FRANK test firmware v%s",
             FIRMWARE_VERSION);
    snprintf(g_dlg[1], sizeof(g_dlg[1]), "%s", g_desk.board_name);
    snprintf(g_dlg[2], sizeof(g_dlg[2]), "%s", g_desk.mcu_name);
    snprintf(g_dlg[3], sizeof(g_dlg[3]), "unit %s", g_desk.unit_serial);
    show_dialog("About", 4);
}

static void show_board_info(void) {
    detect_report(&g_detect);
    snprintf(g_dlg[0], sizeof(g_dlg[0]), "%s", g_desk.board_name);
    snprintf(g_dlg[1], sizeof(g_dlg[1]), "flash %06X   psram %u MB",
             (unsigned)g_detect.flash_jedec,
             (unsigned)(g_detect.psram_bytes / (1024u * 1024u)));
    snprintf(g_dlg[2], sizeof(g_dlg[2]), "fingerprint %u/%u pins",
             g_detect.best_score, g_detect.best_of);
    snprintf(g_dlg[3], sizeof(g_dlg[3]), "full detail on the console");
    show_dialog("Board Info", 4);
}

/* Every board this silicon could be — not only the ones that tied.
 *
 * Restricting the list to the fingerprint's candidates would mean the
 * operator can only confirm what the firmware already suspects, and the
 * whole reason this exists is that they may know something the pins
 * cannot show. */
#define PICK_MAX 24
static const frank_board_desc_t *g_pick[PICK_MAX];
static const char *g_pick_names[PICK_MAX];
static char        g_pick_label[PICK_MAX][32];

static void set_board_dialog(void) {
    int n = 0;
    for (unsigned i = 0; i < frank_board_table_len && n < PICK_MAX; i++) {
        const frank_board_desc_t *b = &frank_board_table[i];

        /* Slaves are not something you choose.
         *
         * A Core 2 slave is the other half of a board, not a board, and
         * it runs the peer image rather than this one. Listing it invited
         * picking it on the master, which would gate every test on the
         * wrong pin map. */
        if (b->role == FRANK_ROLE_SLAVE) continue;

        /* Every board, including the ones whose silicon does not match.
         *
         * This used to skip anything whose MCU differed from what was
         * detected, which quietly hid FRANK, microFRANK, miniFRANK and
         * zeroFRANK whenever the chip was an RP2350B. That is the wrong
         * call twice over: the whole point of this dialog is that the
         * operator knows something the probes do not, and a detector
         * that got the package wrong is exactly when you need to
         * override it. A mismatch is annotated, not hidden. */
        const bool fits = (b->mcu == g_detect.mcu || b->mcu == FRANK_MCU_ANY);
        if (fits) {
            snprintf(g_pick_label[n], sizeof(g_pick_label[n]), "%s", b->name);
        } else {
            snprintf(g_pick_label[n], sizeof(g_pick_label[n]), "%s  (%s)",
                     b->name, frank_mcu_class_name(b->mcu));
        }

        g_pick[n]       = b;
        g_pick_names[n] = g_pick_label[n];
        n++;
    }
    if (!n) return;

    /* Start on what is showing now, so Return is a no-op rather than a
     * surprise. */
    int sel = 0;
    for (int i = 0; i < n; i++)
        if (g_detect.board && g_pick[i] == g_detect.board) sel = i;

    g_desk.picker_title = "Set Board";
    g_desk.picker_items = g_pick_names;
    g_desk.picker_count = n;
    g_desk.picker_sel   = sel;
    redraw();

    printf("[board] pick with arrows, Enter to accept, Esc to cancel\n");

    absolute_time_t end = make_timeout_time_ms(60000);
    while (absolute_time_diff_us(get_absolute_time(), end) > 0) {
        ui_input_task();
        int k = ui_input_getkey();
        if (k == UI_KEY_NONE) { sleep_ms(5); continue; }

        if (k == UI_KEY_UP)   { if (--g_desk.picker_sel < 0) g_desk.picker_sel = n - 1; redraw(); }
        else if (k == UI_KEY_DOWN) { if (++g_desk.picker_sel >= n) g_desk.picker_sel = 0; redraw(); }
        else if (k == UI_KEY_ESC) break;
        else if (k == UI_KEY_ENTER) {
            g_manual_board   = g_pick[g_desk.picker_sel];
            g_detect.board   = g_manual_board;
            g_detect.source  = DETECT_SRC_OPERATOR;
            g_desk.board_name  = g_manual_board->name;
            g_desk.manual_note = g_manual_board->manual_note;
            printf("[board] set to %s (this session only)\n",
                   g_manual_board->slug);
            /* The capability gate changes with the board, so the test
             * list has to be rebuilt — several rows will move between
             * n/a and pending. */
            /* The link stack may only now be known to exist. */
            tests_link_init(&g_detect);
            registry_prepare(&g_results, &g_detect);
            gate_menus();
            break;
        }
    }

    g_desk.picker_title = NULL;
    redraw();
}

/* The measured pin states against what the winning descriptor expected.
 * This is the evidence behind the verdict, and when the verdict is wrong
 * it is the first thing worth looking at. */
static void show_pin_signature(void) {
    snprintf(g_dlg[0], sizeof(g_dlg[0]), "%s", 
             g_detect.auto_guess ? g_detect.auto_guess->name : "no match");
    snprintf(g_dlg[1], sizeof(g_dlg[1]), "matched %u of %u pins",
             g_detect.best_score, g_detect.best_of);
    snprintf(g_dlg[2], sizeof(g_dlg[2]), "margin %d%%  %s",
             g_detect.margin / 10, g_detect.ambiguous ? "AMBIGUOUS" : "");

    if (g_detect.mismatch_count) {
        int off = snprintf(g_dlg[3], sizeof(g_dlg[3]), "disagreed:");
        for (unsigned i = 0; i < g_detect.mismatch_count && off < 34; i++)
            off += snprintf(g_dlg[3] + off, sizeof(g_dlg[3]) - off,
                            " GP%u", (unsigned)g_detect.mismatch[i]);
    } else {
        snprintf(g_dlg[3], sizeof(g_dlg[3]), "every pin agreed");
    }
    show_dialog("Pin Signature", 4);
}

/* Colour bars, so a monitor's own no-signal pattern cannot be mistaken
 * for ours. Held until a key. */
static void show_test_card(void) {
    ui_surface_t *s = ui_video_surface();
    static const uint8_t bars[8] = {
        UI_WHITE, UI_WARN, UI_OK, UI_OK_L, UI_ACCENT_L, UI_ACCENT,
        UI_FAIL, UI_BLACK
    };
    for (int i = 0; i < 8; i++)
        ui_fill(s, i * (UI_SCREEN_W / 8), 0, UI_SCREEN_W / 8, UI_SCREEN_H - 40,
                bars[i]);
    ui_fill(s, 0, UI_SCREEN_H - 40, UI_SCREEN_W, 40, UI_PAPER);
    ui_text_centred(s, 0, UI_SCREEN_H - 26, UI_SCREEN_W,
                    "640x480  -  press any key", UI_BLACK);
    ui_video_present();

    absolute_time_t end = make_timeout_time_ms(30000);
    while (absolute_time_diff_us(get_absolute_time(), end) > 0) {
        ui_input_task();
        if (ui_input_getkey() != UI_KEY_NONE) break;
        sleep_ms(5);
    }
    redraw();
}

static void print_help(void) {
    printf("\n  Alt+F/B/V/U/T  open a menu      arrows  navigate\n"
           "  Enter          activate / run   Esc     close\n"
           "  A run all   R restart   Y confirm screen   ? this\n");
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static void run_all(void);

/* Which command does this bare key equate to? The menus already declare
 * their equivalents, so walk them rather than keeping a second list that
 * can disagree with what is drawn on screen. */
static int cmd_for_key(int k) {
    if (k < 0 || k > 0xFF) return CMD_NONE;
    char c = (char)k;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');

    for (int m = 0; m < g_menubar.count; m++) {
        const ui_menu_t *mn = &g_menubar.menus[m];
        for (int i = 0; i < mn->count; i++) {
            const ui_menu_item_t *it = &mn->items[i];
            if (!it->label || !it->enabled || !it->key) continue;
            char ik = it->key;
            if (ik >= 'a' && ik <= 'z') ik = (char)(ik - 'a' + 'A');
            if (ik == c) return it->cmd;
        }
    }
    return CMD_NONE;
}

static void do_command(int cmd) {
    switch (cmd) {
        case CMD_RUN_ALL:      run_all(); break;
        case CMD_RUN_SELECTED:
            if (g_desk.selected >= 0) {
                g_running_row = (unsigned)g_desk.selected;
                registry_run_one(&g_results, g_running_row, &g_detect,
                                 test_progress);
            }
            break;

        case CMD_VIDEO_HDMI:      ui_video_switch(VIDEO_HDMI);      break;
        case CMD_VIDEO_VGA:       ui_video_switch(VIDEO_VGA);       break;
        case CMD_VIDEO_COMPOSITE: ui_video_switch(VIDEO_COMPOSITE); break;
        case CMD_VIDEO_AUTO:      ui_video_switch(VIDEO_AUTO);      break;

        /* Stored, not applied. The mouse claims GP0/GP1 once during
         * start-up and there is no safe way to hand them back to a UART
         * underneath a running PS/2 state machine, so this takes effect
         * on the next boot and the dialog says so. */
        case CMD_CONSOLE: {
            const bool keep = !g_console_kept;
            if (settings_set_console(keep)) {
                g_console_kept = keep;
                ui_desktop_set_cmd_checked(CMD_CONSOLE, keep);
                if (keep) {
                    snprintf(g_dlg[0], sizeof(g_dlg[0]), "Kept from the next boot.");
                    snprintf(g_dlg[1], sizeof(g_dlg[1]), "The PS/2 mouse stays off:");
                    snprintf(g_dlg[2], sizeof(g_dlg[2]), "it shares GP0/GP1 with the");
                    snprintf(g_dlg[3], sizeof(g_dlg[3]), "console UART.");
                } else {
                    snprintf(g_dlg[0], sizeof(g_dlg[0]), "Off from the next boot.");
                    snprintf(g_dlg[1], sizeof(g_dlg[1]), "The PS/2 mouse takes");
                    snprintf(g_dlg[2], sizeof(g_dlg[2]), "GP0/GP1, and the console");
                    snprintf(g_dlg[3], sizeof(g_dlg[3]), "goes with them.");
                }
                show_dialog("Serial Console", 4);
            } else {
                snprintf(g_dlg[0], sizeof(g_dlg[0]), "Could not write the setting.");
                show_dialog("Serial Console", 1);
            }
            break;
        }

        case CMD_RESTART:
            stdio_flush(); sleep_ms(50); watchdog_reboot(0, 0, 0);
            break;

        case CMD_BOOTSEL:
            printf("[sys] entering BOOTSEL\n"); stdio_flush();
            sleep_ms(50);
            reset_usb_boot(0, 0);
            break;

        case CMD_ABOUT:  show_about();   break;

        case CMD_BOARD_INFO:    show_board_info();   break;
        case CMD_SET_BOARD:     set_board_dialog();  break;
        case CMD_SHOW_SIG:      show_pin_signature(); break;
        case CMD_VIDEO_TESTCARD: show_test_card();   break;

        case CMD_AUDIO_PWM: case CMD_AUDIO_I2S: case CMD_AUDIO_TS: {
            const dlg_ctx_t ctx = { .detect = &g_detect,
                                    .paint_background = paint_desktop };
            dlg_audio(&ctx, cmd == CMD_AUDIO_PWM ? AUDIO_SRC_PWM
                          : cmd == CMD_AUDIO_I2S ? AUDIO_SRC_I2S
                                                 : AUDIO_SRC_TS);
            redraw();
            break;
        }

        case CMD_NESPAD: {
            const dlg_ctx_t ctx = { .detect = &g_detect,
                                    .paint_background = paint_desktop };
            dlg_nespad(&ctx);
            redraw();
            break;
        }

        case CMD_BURNIN: {
            const dlg_ctx_t ctx = { .detect = &g_detect,
                                    .paint_background = paint_desktop };
            dlg_burnin(&ctx, &g_results);
            redraw();
            break;
        }

        case CMD_LED: {
            const dlg_ctx_t ctx = { .detect = &g_detect,
                                    .paint_background = paint_desktop };
            dlg_led(&ctx);
            redraw();
            break;
        }

        case CMD_PS2: {
            const dlg_ctx_t ctx = { .detect = &g_detect,
                                    .paint_background = paint_desktop };
            dlg_ps2(&ctx);
            redraw();
            break;
        }

        case CMD_TAPE: {
            const dlg_ctx_t ctx = { .detect = &g_detect,
                                    .paint_background = paint_desktop };
            dlg_tape(&ctx);
            redraw();
            break;
        }

        /* Everything else is disabled in the menus, so reaching here at
         * all means an item was enabled without being wired up. */
        default:
            if (cmd) printf("[ui] command %d has no handler\n", cmd);
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Running                                                             */
/* ------------------------------------------------------------------ */

static void test_progress(int permille, const char *status) {
    if (g_running_row >= g_results.count) return;

    g_results.rows[g_running_row].progress = permille;

    if (status) {
        snprintf(g_results.detail[g_running_row], TEST_DETAIL_LEN,
                 "%s", status);
        /* A status line the operator never sees is not a status line, so
         * this redraws immediately rather than waiting for the test to
         * finish. Costs a frame; the tests that use it are the slow ones
         * where a frame is nothing. */
        redraw();
    }
}

static void run_all(void) {
    registry_prepare(&g_results, &g_detect);
    redraw();

    if (!g_detect.board || g_detect.board->id == FRANK_BOARD_UNKNOWN) {
        printf("\n[run] board not identified - nothing run.\n"
               "      Board > Set Board (Alt+B) to choose one.\n");
        stdio_flush();
        return;
    }

    for (unsigned i = 0; i < g_results.count; i++) {
        if (g_results.rows[i].state == TEST_NA) continue;

        follow(i);
        g_results.rows[i].state = TEST_RUNNING;
        redraw();

        g_running_row = i;
        registry_run_one(&g_results, i, &g_detect, test_progress);
        redraw();

        printf("  %-16s %s\n", g_results.rows[i].name,
               g_results.detail[i][0] ? g_results.detail[i] : "-");
        stdio_flush();
    }

    /* Back to the top when the run finishes.
     *
     * The run scrolls as it goes, so it ends parked on the last test -
     * which on a full board is Slave reset, three screens down, and
     * almost never what anyone wants to look at. The interesting rows
     * are at the top and the summary reads from there, so a finished run
     * leaves the list where a fresh one starts.
     *
     * The selection follows rather than being cleared: "Run Selected"
     * needs something to act on, and an enabled menu item with nothing
     * selected was the question that put this line here in the first
     * place. */
    if (g_results.count) {
        g_desk.selected      = 0;
        g_desk.first_visible = 0;
    }
    redraw();

    printf("\n[run] %u passed, %u failed, %u n/a, %u could not run\n",
           g_results.passed, g_results.failed, g_results.na, g_results.norun);
    print_help();
    stdio_flush();
}

/* Flash timing for the raised clock. Runs from SRAM, because it is
 * reconfiguring the interface it would otherwise be fetched through.
 *
 * The boards ask for PICO_FLASH_SPI_CLKDIV 2, which at 252 MHz clocks
 * the flash at 126 MHz with the SDK's default RX delay. Plenty of parts
 * will not do that, and a socketed module brings its own. When the reads
 * come back wrong the symptom is not a flash error - it is the firmware
 * crashing somewhere unrelated, on some boards and not others, which is
 * how this presented: MegaFRANK dark while Core 2 was fine, and murmnes
 * happy on both.
 *
 * Ported from murmnes, which runs this same clock on this same hardware.
 * 88 MHz is its ceiling too. */
static void __no_inline_not_in_flash_func(set_flash_timings)(int cpu_mhz,
                                                             int flash_max_mhz) {
    const int clock_hz      = cpu_mhz * 1000000;
    const int max_flash_freq = flash_max_mhz * 1000000;

    int divisor = (clock_hz + max_flash_freq - (max_flash_freq >> 4) - 1) /
                  max_flash_freq;
    if (divisor == 1 && clock_hz >= 166000000) divisor = 2;

    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000 && clock_hz >= 166000000) rxdelay += 1;

    qmi_hw->m[0].timing = 0x60007000 |
                          (uint32_t)rxdelay << QMI_M0_TIMING_RXDELAY_LSB |
                          (uint32_t)divisor << QMI_M0_TIMING_CLKDIV_LSB;
}

int main(void) {
    /* Never let an attached debugger stop the clock.
     *
     * TIMER0 pauses by default whenever a core is halted, and every
     * sleep_ms() in the SDK waits on it. With a probe attached — which
     * on this rig is always — a single halt for an `mdw` leaves the
     * timebase stopped, and the next sleep_ms() never returns. The
     * board then looks comprehensively dead: no banner, no USB
     * enumeration, no video, core 0 spinning in the alarm pool. It cost
     * an afternoon to recognise, because every symptom pointed at the
     * feature being worked on rather than at the debugger.
     *
     * A diagnostic firmware is debugged more than most, so it opts out
     * permanently. First statement in main(), before anything sleeps. */
    timer_hw->dbgpause = 0;

    /* 252 MHz pairs with MODE_HSTX_CLK_DIV=2 to give clk_hstx = 126 MHz,
     * which is what 640x480@60 TMDS needs. ui_video_hstx.c also sets
     * clk_hstx explicitly, so the video path survives a different choice
     * here — but this is the pairing frank_core2u ships and is proven. */
    /* The regulator is deliberately left alone. 252 MHz is within the
     * RP2350's stock operating point and needs no help; raising the core
     * to 1.50 V for it is a large overvolt against a 1.10 V nominal and
     * pushes flash and USB timing out of spec for nothing. frank-msx
     * runs this same clock on this same hardware without touching vreg,
     * and says as much in its own bring-up. Only an overclock past 252
     * would justify it, and then the flash timings have to move too. */
    set_flash_timings(252, 88);
    sleep_ms(10);
    if (!set_sys_clock_khz(252000, false))
        set_sys_clock_khz(126000, false);

    /* NOT overclocked for composite, and the reason is worth recording.
     *
     * frank-msx builds its composite images at 378 MHz because the
     * software TV encoder assembles a whole line inside a 33 us alarm at
     * 30 kHz and does not finish at 252. Reproducing that here does not
     * work: raising clk_sys late — once the mode is known — faults the
     * core outright, because stdio, USB and the PIOs have already been
     * configured from the old frequency. Raising it early from the
     * stored setting, which is what frank-msx effectively does, stops
     * this firmware booting at all when composite is selected.
     *
     * Leaving composite at 252 MHz is therefore the honest state: the
     * encoder streams continuously and a TV will not lock to it. That
     * is a worse outcome than a working picture and a better one than a
     * board that hangs the moment composite is chosen. */

    stdio_init_all();
    sleep_ms(600);   /* let a USB CDC host attach, if there is one */

    printf("\n\nFRANK test firmware v%s   sys_clk %u MHz\n",
           FIRMWARE_VERSION, (unsigned)(clock_get_hz(clk_sys) / 1000000u));

    /* Both of these touch the QMI directly and must run with the other
     * core parked; video_open below is what wakes it. */
    /* The USB host first, because enumerating a keyboard takes long
     * enough to miss the boot window otherwise. Detection and the
     * benchmark below give it that time for free. */
    ui_input_init_usb();

    {
        frank_settings_t cs;
        if (settings_load(&cs)) g_console_kept = (cs.console != 0u);
    }

    stage("detect");
    detect_run(&g_detect);
    detect_report(&g_detect);

    /* Pretend to be another board, for bring-up on a bench rig.
     *
     * Boards in this fleet share the video pins and often the chip, so
     * what separates them at boot is the descriptor, not the hardware.
     * A board that misbehaves in the field can therefore be reproduced
     * on whichever board has a debug probe on it, by borrowing its
     * descriptor. Off in every shipped image.
     *
     *   cmake -B bld -DPICO_BOARD=frank_b -DFRANK_FORCE_BOARD=<id> */
#if defined(FRANK_FORCE_BOARD)
    {
        const frank_board_desc_t *forced =
            frank_board_desc((frank_board_id_t)FRANK_FORCE_BOARD, FRANK_ROLE_SINGLE);
        if (forced) {
            printf("[detect] FRANK_FORCE_BOARD: posing as %s\n", forced->name);
            g_detect.board = forced;
        } else {
            printf("[detect] FRANK_FORCE_BOARD=%d: no such board\n",
                   FRANK_FORCE_BOARD);
        }
    }
#endif

    /* Memory throughput, while core 1 is still parked. See
     * detect_benchmark() for why this cannot wait until the tests run. */
    stage("benchmark");
    detect_benchmark(&g_detect);
    printf("  flash read %u KiB/s   psram w %u r %u KiB/s\n",
           (unsigned)g_detect.flash_read_kbps,
           (unsigned)g_detect.psram_write_kbps,
           (unsigned)g_detect.psram_read_kbps);

    stage("video detect");
    video_detect_run(g_detect.board, &g_video);
    video_detect_report(&g_video);

    /* The keyboard before the boot window, so there is something to
     * hold a key on. The window's only input used to be the console, and
     * on every board with PS/2 the mouse now takes GP0/GP1 from the
     * console UART — which left exactly those boards unable to choose a
     * video mode. */
    {
        const frank_pins_t *ip = g_detect.board ? &g_detect.board->pins : NULL;
        if (ip && ip->ps2_kb_clk != PIN_NC)
            ui_input_init_keyboard(ip->ps2_kb_clk);

        /* One source for both keyboards: ui_input_getchar() reads
         * whichever is present. Registered unconditionally, because a
         * USB keyboard is the only one some boards have and gating this
         * on the PS/2 pins left those boards unable to hold a key. */
        video_select_add_source(ui_input_getchar, "keyboard");

        /* And take the console away where it is not one. The PS/2 mouse
         * shares GP0/GP1 with UART0, so its power-up chatter arrives at
         * getchar() as characters; one of them landing on 'v' or 'c'
         * chooses a video mode nobody asked for. */
        if (ip && (ip->ps2_ms_dat == PICO_DEFAULT_UART_RX_PIN ||
                   ip->ps2_ms_clk == PICO_DEFAULT_UART_RX_PIN ||
                   ip->ps2_ms_dat == PICO_DEFAULT_UART_TX_PIN ||
                   ip->ps2_ms_clk == PICO_DEFAULT_UART_TX_PIN)) {
            printf("[video] UART0 is the PS/2 mouse here; not a key source\n");
            video_select_no_console();
        }
    }

    stage("video select");
    video_select_boot_window(g_detect.board, &g_video, 2000, &g_choice);


    /* The boot window and the sticky setting both check the board's
     * capabilities, which is the wrong half of the question — a board
     * can have a composite socket this firmware has no code to drive.
     * Catching it here keeps a stored choice from silently falling back
     * to HDMI on every boot with no explanation. */
    if (g_choice.mode != VIDEO_AUTO &&
        !ui_video_mode_implemented(g_choice.mode)) {
        printf("[video] %s is wired on this board but not implemented "
               "in this firmware; using autodetect\n",
               frank_video_mode_name(g_choice.mode));
        g_choice.mode = VIDEO_AUTO;
    }

    /* A bring-up escape hatch, off in every shipped image. Selection
     * has several layers - a saved setting, a boot key, detection, the
     * board default - and when a board comes up dark it is not obvious
     * which of them chose, or whether any of them is at fault rather
     * than the HSTX driver underneath. This skips the lot and asks for
     * HDMI directly, which splits that question in one flash.
     *
     *   cmake -B bld -DPICO_BOARD=frank_b -DFRANK_FORCE_VIDEO=1   (1=HDMI 2=VGA 3=composite) */
#if FRANK_FORCE_VIDEO
    printf("[video] FRANK_FORCE_VIDEO=%d: selection skipped\n",
           FRANK_FORCE_VIDEO);
    g_choice.mode   = (frank_video_mode_t)FRANK_FORCE_VIDEO;
    g_choice.source = VIDEO_CHOICE_DEFAULT;
#endif

    stage("video open");
    printf("[video] opening %s (%s)\n", frank_video_mode_name(g_choice.mode),
           video_choice_source_name(g_choice.source));
    frank_video_mode_t opened = ui_video_open(g_choice.mode);

    snprintf(g_mcu_line, sizeof(g_mcu_line), "%s rev %u",
             frank_mcu_class_name(g_detect.mcu), (unsigned)g_detect.chip_rev);
    snprintf(g_serial, sizeof(g_serial), "%02X%02X%02X%02X",
             g_detect.chip_id[0], g_detect.chip_id[1],
             g_detect.chip_id[2], g_detect.chip_id[3]);

    g_menubar = *ui_desktop_menus();
    g_desk.board_name  = g_detect.board ? g_detect.board->name : "Unknown";
    g_desk.mcu_name    = g_mcu_line;
    g_desk.video_name  = frank_video_mode_name(opened);
    g_desk.unit_serial = g_serial;
    g_desk.manual_note = g_detect.board ? g_detect.board->manual_note : NULL;
    g_desk.selected    = -1;

    ui_desktop_set_cmd_checked(CMD_CONSOLE, g_console_kept);
    gate_menus();

    /* Composite renders its own text page rather than a scaled copy of
     * the desktop — see ui_video_tv.c. It needs the same state the
     * desktop draws from, so hand it the pointer. Harmless on the other
     * backends, which never look at it. */
    {
        extern void ui_video_tv_set_desktop(const ui_desktop_t *d);
        extern void ui_video_tv_set_menubar(const ui_menubar_t *mb);
        ui_video_tv_set_desktop(&g_desk);
        ui_video_tv_set_menubar(&g_menubar);
    }

    stage("link");
    tests_link_init(&g_detect);

    stage("input");
    {
        const frank_pins_t *ip = g_detect.board ? &g_detect.board->pins : NULL;
        const int kbd = (ip && ip->ps2_kb_clk != PIN_NC) ? ip->ps2_kb_clk : -1;

        /* The PS/2 mouse, even though it costs the console.
         *
         * On every FRANK board that has PS/2 the mouse is GP0/GP1, which
         * is also UART0. This started out declining to take those pins,
         * on the reasoning that a diagnostic firmware needs its console
         * more than a pointer. That was the wrong way round: testing the
         * PS/2 mouse is the whole reason the port is on the board, and a
         * rig that quietly refuses to exercise a connector is not doing
         * its job. The console survives on every board without PS/2, and
         * the screen is the primary output regardless.
         *
         * Said out loud on the way past, while the console still
         * works. */
        int mouse = (ip && ip->ps2_ms_clk != PIN_NC) ? ip->ps2_ms_clk : -1;
        if (mouse >= 0 && ip->uart_tx != PIN_NC &&
            (ip->uart_tx == mouse || ip->uart_rx == mouse ||
             ip->uart_tx == mouse + 1 || ip->uart_rx == mouse + 1)) {

            /* Unless the operator asked to keep it. Holding U in the boot
             * window says the console is worth more than the pointer on
             * this run — debugging over a serial line, mostly, where
             * losing stdout the moment the interface comes up is the
             * difference between a session and a guess. The mouse can be
             * exercised on a boot where nobody is watching the UART. */
            if (video_select_console_kept() || g_console_kept) {
                printf("[input] console kept (%s): PS/2 mouse left off\n",
                       video_select_console_kept() ? "U held" : "stored setting");
                mouse = -1;
            } else {
                printf("[input] PS/2 mouse takes GP%d/%d from the console UART; "
                       "this is the last line you will see (hold U at boot to "
                       "keep it)\n", mouse, mouse + 1);
                stdio_flush();
                sleep_ms(20);
            }
        }

        ui_input_init(kbd, mouse);
    }
    printf("[input] keyboard:%s  mouse:%s\n",
           ui_input_keyboard_connected() ? "yes" : "no",
           ui_input_mouse_connected() ? "yes" : "no");

    stage("tests");
    run_all();

    /* ---------------------------------------------------------------
     * Event loop.
     *
     * Keyboard first and completely: Alt+F/B/V/T/W open menus, arrows
     * navigate, Enter activates, Esc closes. Every command in the menu
     * bar is reachable without a pointer, because on `zerofrank` and the core2
     * halves there is no pointing device to reach it with.
     * --------------------------------------------------------------- */
    while (true) {
        ui_input_task();

        /* ---- pointer ---- */
        const ui_pointer_t *pt = ui_input_pointer();
        if (pt->present) {
            if (pt->moved) g_dirty = true;

            if (pt->moved && g_menubar.open >= 0) {
                int it = ui_menubar_hit_item(&g_menubar, pt->x, pt->y);
                if (it >= 0 && it != g_menubar.highlight) {
                    g_menubar.highlight = it;
                }
            }

            if (pt->pressed) {
                int m = ui_menubar_hit(&g_menubar, pt->x, pt->y);
                if (m >= 0) {
                    /* Click the open menu's own title to close it. */
                    g_menubar.open = (m == g_menubar.open) ? -1 : m;
                    g_menubar.highlight = -1;
                    if (g_menubar.open >= 0) ui_menubar_move(&g_menubar, +1);
                } else if (g_menubar.open >= 0) {
                    int it = ui_menubar_hit_item(&g_menubar, pt->x, pt->y);
                    if (it >= 0) {
                        g_menubar.highlight = it;
                        int cmd = ui_menubar_current_cmd(&g_menubar);
                        g_menubar.open = -1; g_menubar.highlight = -1;
                        do_command(cmd);
                    } else {
                        g_menubar.open = -1; g_menubar.highlight = -1;
                    }
                } else {
                    /* The scroll bar first: a click that lands on it is
                     * not a click on the row behind it. */
                    const int f = ui_desktop_scroll_hit(&g_desk, pt->x, pt->y,
                                                        true, false);
                    if (f >= 0) {
                        g_desk.first_visible = f;
                    } else {
                        int row = hit_row(pt->x, pt->y);
                        if (row >= 0) g_desk.selected = row;
                    }
                }
                g_dirty = true;
            }

            /* Dragging the thumb. Only while the button is down and only
             * on the bar, so a drag that started on a row cannot end up
             * scrolling the list out from under it. */
            if (pt->button && !pt->pressed) {
                const int f = ui_desktop_scroll_hit(&g_desk, pt->x, pt->y,
                                                    false, true);
                if (f >= 0 && f != g_desk.first_visible) {
                    g_desk.first_visible = f;
                    g_dirty = true;
                }
            }

            if (pt->wheel) { scroll_by(-pt->wheel); g_dirty = true; }
        }

        /* ---- keyboard ---- */
        int k = ui_input_getkey();
        if (k != UI_KEY_NONE) {
            g_dirty = true;

            if (k & UI_KEY_ALT) {
                int m = ui_menubar_find_alt(&g_menubar, (char)(k & 0xFF));
                if (m >= 0) {
                    g_menubar.open = (m == g_menubar.open) ? -1 : m;
                    g_menubar.highlight = -1;
                    if (g_menubar.open >= 0) ui_menubar_move(&g_menubar, +1);
                    g_menu_only = true;
                }
            } else if (g_menubar.open >= 0) {
                /* A menu is down: it owns the keyboard. */
                switch (k) {
                    case UI_KEY_ESC:
                        g_menubar.open = -1; g_menubar.highlight = -1;
                        g_menu_only = true;
                        break;
                    case UI_KEY_UP:    ui_menubar_move(&g_menubar, -1);  g_menu_only = true; break;
                    case UI_KEY_DOWN:  ui_menubar_move(&g_menubar, +1);  g_menu_only = true; break;
                    case UI_KEY_LEFT:  ui_menubar_cycle(&g_menubar, -1); g_menu_only = true; break;
                    case UI_KEY_RIGHT: ui_menubar_cycle(&g_menubar, +1); g_menu_only = true; break;
                    case UI_KEY_ENTER: {
                        int cmd = ui_menubar_current_cmd(&g_menubar);
                        printf("[ui] enter: menu %d item %d -> cmd %d\n",
                               g_menubar.open, g_menubar.highlight, cmd);
                        g_menubar.open = -1; g_menubar.highlight = -1;
                        do_command(cmd);
                        break;
                    }
                    default: {
                        /* A bare letter picks the item whose equivalent
                         * it is, so ^H works with or without the menu
                         * open — which is what people expect of both. */
                        int cmd = cmd_for_key(k);
                        if (cmd) {
                            g_menubar.open = -1; g_menubar.highlight = -1;
                            do_command(cmd);
                        }
                        break;
                    }
                }
            } else {
                switch (k) {
                    case UI_KEY_UP:   move_selection(-1); break;
                    case UI_KEY_DOWN: move_selection(+1); break;
                    /* A page is what is on screen. Ten was a guess that
                     * happened to be close while every row was twenty
                     * pixels tall, and stopped being close when they
                     * were not. */
                    case UI_KEY_PGUP: move_selection(-ui_desktop_rows_shown()); break;
                    case UI_KEY_PGDN: move_selection(+ui_desktop_rows_shown()); break;
                    case UI_KEY_HOME: g_desk.selected = 0; scroll_to_selection(); break;
                    case UI_KEY_END:  g_desk.selected = (int)g_results.count - 1;
                                      scroll_to_selection(); break;
                    case UI_KEY_ENTER: do_command(CMD_RUN_SELECTED); break;
                    case UI_KEY_ESC:   g_desk.selected = -1; break;
            case '#':
                printf("[perf] vsync=%u swaps_timed_out=%u "
                       "full_redraws=%u menu_redraws=%u\n",
                       (unsigned)ui_hstx_vsync_count,
                       (unsigned)ui_hstx_swap_timeouts,
                       (unsigned)g_full_redraws, (unsigned)g_menu_redraws);
                break;
                    default: {
                        int cmd = cmd_for_key(k);
                        if (cmd) do_command(cmd);
                        else if (k == '?') print_help();
                        break;
                    }
                }
            }
        }

        /* Keep looking for a slave that was not there.
         *
         * Nothing on this board lets the master power-cycle the slave, so
         * one that boots late, gets reflashed, or reboots on its own has
         * to be noticed rather than forced into step. */
        static absolute_time_t next_link_probe;
        if (absolute_time_diff_us(get_absolute_time(), next_link_probe) < 0) {
            next_link_probe = make_timeout_time_ms(5000);
            if (tests_link_poll()) {
                printf("[link] slave appeared - re-running\n");
                run_all();
            }
        }

        if (g_dirty) {
            if (g_menu_only) redraw_menu_area(); else redraw();
            g_menu_only = false;
        }
        sleep_ms(5);
    }
}
