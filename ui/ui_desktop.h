/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_desktop.h — the screen this firmware actually presents.
 *
 * A test rig that prints a wall of log lines makes the reader do the
 * work of finding the one thing that failed. A list of subsystems, each
 * with an icon and a state, does not — and the state of a board is
 * exactly the kind of thing a screen full of small repeated elements
 * shows better than prose.
 *
 * The log still exists, on UART and in a window, because when something
 * is wrong the detail is the whole point. It is just no longer the
 * primary interface.
 */
#ifndef UI_DESKTOP_H
#define UI_DESKTOP_H

#include "ui_gfx.h"
#include "ui_icons.h"
#include "ui_menu.h"
#include "ui_window.h"

/* Menu commands. The interface names them; main.c decides what they do,
 * so ui/ stays free of any knowledge about tests or hardware. */
enum {
    CMD_NONE = 0,
    CMD_ABOUT, CMD_BOARD_INFO, CMD_UNIT_SERIAL,
    CMD_RESTART, CMD_BOOTSEL, CMD_SET_BOARD, CMD_SHOW_SIG,
    CMD_CONSOLE,
    CMD_VIDEO_AUTO, CMD_VIDEO_HDMI, CMD_VIDEO_VGA, CMD_VIDEO_COMPOSITE,
    CMD_VIDEO_TESTCARD,
    CMD_AUDIO_PWM, CMD_AUDIO_I2S, CMD_AUDIO_TS,
    CMD_RUN_ALL, CMD_RUN_SELECTED, CMD_NESPAD, CMD_TAPE, CMD_PS2, CMD_LED,
    CMD_CONFIRM_LEGIBLE,
};

typedef enum {
    TEST_PENDING = 0,
    TEST_RUNNING,
    TEST_PASS,
    TEST_FAIL,
    TEST_NA,          /* the board has no such hardware — not a defect */
    TEST_NORUN,       /* could not complete — result unknown           */
} ui_test_state_t;

typedef struct {
    ui_icon_id_t    icon;
    const char     *name;
    const char     *detail;    /* the measured number, or why not */
    ui_test_state_t state;
    int             progress;  /* permille, only while TEST_RUNNING */
} ui_test_row_t;

typedef struct {
    const char *board_name;
    const char *mcu_name;
    const char *video_name;
    const char *unit_serial;

    /* The switches firmware cannot reach, from the board descriptor.
     * NULL means this board has none. */
    const char *manual_note;

    const ui_test_row_t *rows;
    int                  row_count;
    int                  first_visible;
    int                  selected;

    /* Summary counters shown in the status strip. */
    int passed, failed, na, remaining;

    /* Set while a modal dialog is up; the desktop dims nothing but the
     * dialog takes the keyboard. */
    const char *dialog_title;
    const char *dialog_body[4];
    const char *dialog_buttons[3];
    int         dialog_focus;    /* ringed, and what Return takes */

    /* List picker, drawn over everything when picker_title is set. */
    const char        *picker_title;
    const char *const *picker_items;
    int                picker_count;
    int                picker_sel;
    int         dialog_button_count;
} ui_desktop_t;

/* Compose one whole frame into `s`. Pure: it reads state and writes
 * pixels, which is what makes the host preview possible. */
void ui_desktop_draw(ui_surface_t *s, const ui_desktop_t *d,
                     const ui_menubar_t *mb,
                     int mouse_x, int mouse_y, bool mouse_visible);

/* The same composition, confined to a rectangle.
 *
 * The scanline renderer reads the framebuffer in place — there is no
 * back buffer, and at 4 bpp a second one would cost another 153,600
 * bytes. So a full recomposition rewrites pixels the beam is currently
 * displaying, and moving through a menu makes the whole screen flicker.
 * Repainting only the rectangle the menu occupies confines that to an
 * area the eye is already looking at, and the rest of the screen simply
 * never changes. */
void ui_desktop_draw_clipped(ui_surface_t *s, const ui_desktop_t *d,
                             const ui_menubar_t *mb,
                             int mouse_x, int mouse_y, bool mouse_visible,
                             int cx, int cy, int cw, int ch);

/* The pointer, drawn over whatever is already there.
 *
 * Exposed because the modal dialogs compose the desktop underneath
 * themselves and then draw on top of it — so if the desktop drew the
 * cursor, the dialog would immediately cover it and the pointer would
 * disappear behind the window it is trying to click on. Dialogs paint
 * the background with the cursor suppressed and call this last. */
void ui_desktop_draw_cursor(ui_surface_t *s, int x, int y);

/* Move the pointer by patching the live front buffer instead of
 * recomposing — see ui_cursor.c. Costs microseconds rather than a frame,
 * which is what lets the mouse stay live while audio is playing.
 *
 * reset() must be called after every present(): a swap invalidates the
 * saved patch. A caller using this must NOT also draw the cursor into
 * its composition, or there will be two. */
void ui_cursor_overlay_reset(void);
void ui_cursor_overlay_move(int x, int y);

/* The standard menu set. Exposed so the host preview and the firmware
 * show the same thing. */
const ui_menubar_t *ui_desktop_menus(void);

/* Enable or disable one command, by command rather than by position.
 *
 * The audio and gamepad items only make sense on boards that have the
 * hardware, and which board that is only becomes known after detection —
 * and changes again when the operator overrides it. A (menu, index) pair
 * would silently mean something else the first time a separator moved,
 * so this walks the tables looking for the command itself. */
void ui_desktop_set_cmd_enabled(int cmd, bool enabled);
void ui_desktop_set_cmd_checked(int cmd, bool checked);

#endif /* UI_DESKTOP_H */
