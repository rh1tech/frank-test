/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_menu.h — the menu bar and its pull-downs.
 *
 * Every command in the firmware lives in a menu, and every menu item
 * carries a keyboard equivalent. That is not decoration: `nyx` has no
 * USB host and no PS/2 connector, so on some boards the only pointing
 * device is the arrow keys, and a menu that can only be opened by
 * clicking would be unusable exactly where the hardware is thinnest.
 */
#ifndef UI_MENU_H
#define UI_MENU_H

#include "ui_gfx.h"
#include "ui_icons.h"

#define UI_MENUBAR_H 20

/* Same offset as a window's shadow, so a pull-down reads as the same
 * kind of object sitting on the same desktop. */
#define UI_SHADOW_MENU 2

typedef struct {
    const char *label;      /* NULL means a separator line */
    char        key;        /* keyboard equivalent, 0 for none */
    bool        enabled;
    bool        checked;    /* draws a tick in the left gutter */

    /* What this item does. Named rather than dispatched by position:
     * a (menu, index) pair silently means something different the moment
     * a separator moves, and separators move. */
    int         cmd;
} ui_menu_item_t;

typedef struct {
    const char           *title;
    const ui_menu_item_t *items;
    int                   count;
    bool                  is_mark;   /* draws the icon instead of a title */

    /* Alt+this opens the menu. Every menu needs one: on `nyx` and the
     * core2 halves there is no pointing device at all, so a menu bar
     * that can only be opened by clicking is decoration. */
    char                  alt_key;
} ui_menu_t;

typedef struct {
    const ui_menu_t *menus;
    int              count;
    int              open;       /* index of the open menu, -1 for none */
    int              highlight;  /* highlighted item within it, -1 none */
} ui_menubar_t;

/* Draw the bar itself. Always drawn, always at the top. */
void ui_menubar_draw(ui_surface_t *s, const ui_menubar_t *mb);

/* Draw the open pull-down, if any. Called after the windows so the menu
 * lands on top of everything, which is the one place in this interface
 * where z-order is not simply "later wins by accident". */
void ui_menubar_draw_dropdown(ui_surface_t *s, const ui_menubar_t *mb);

/* Hit testing, for when there is a mouse. Returns the menu index under
 * `x` on the bar, or -1. */
int  ui_menubar_hit(const ui_menubar_t *mb, int x, int y);

/* Item index under the pointer while a menu is open, or -1. */
int  ui_menubar_hit_item(const ui_menubar_t *mb, int x, int y);

/* ------------------------------------------------------------------ */
/* Keyboard navigation                                                 */
/* ------------------------------------------------------------------ */

/* Bounds of the open pull-down including its shadow, or false if none is
 * open. Lets a caller repaint just that rectangle instead of the whole
 * screen — which is what stops menu navigation from flickering, since
 * there is no back buffer to compose into. */
bool ui_menubar_dropdown_rect(const ui_menubar_t *mb,
                              int *x, int *y, int *w, int *h);

/* Menu index whose Alt-key is `c`, or -1. Case-insensitive. */
int  ui_menubar_find_alt(const ui_menubar_t *mb, char c);

/* Move the highlight by `dir` (+1 down, -1 up), skipping separators and
 * disabled items and wrapping at the ends. Safe to call with no menu
 * open, in which case it does nothing. */
void ui_menubar_move(ui_menubar_t *mb, int dir);

/* Move to the previous or next menu, keeping it open. */
void ui_menubar_cycle(ui_menubar_t *mb, int dir);

/* The command of the highlighted item, or CMD_NONE (0). */
int  ui_menubar_current_cmd(const ui_menubar_t *mb);

#endif /* UI_MENU_H */
