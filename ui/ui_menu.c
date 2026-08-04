/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui_menu.h"
#include "ui_window.h"   /* ui_separator, ui_bevel_out */

#include <string.h>

#define BAR_PAD_X     10   /* padding either side of a title       */
#define MARK_W        24   /* width of the mark's slot             */
#define ITEM_H        13
#define SEP_H          6
#define DROP_PAD_L    22   /* gutter for the checkmark             */
#define DROP_PAD_R    14
#define KEY_GAP       26   /* space reserved for the key equivalent */

/* Where does menu `i` start on the bar? Walked rather than cached: the
 * menus are static, there are six of them, and a cache would be one more
 * thing to invalidate when a title changes. */
static int menu_x(const ui_menubar_t *mb, int i) {
    int x = 0;
    for (int m = 0; m < i; m++) {
        x += mb->menus[m].is_mark ? MARK_W
                                  : ui_text_width(mb->menus[m].title) + BAR_PAD_X * 2;
    }
    return x;
}

/* Which character of the title is the Alt key? First case-insensitive
 * match, or -1 when the letter does not appear in the title at all —
 * which would be a table bug, and shows up as a missing underline. */
static int alt_index(const ui_menu_t *m) {
    if (!m->title || !m->alt_key) return -1;

    char want = m->alt_key;
    if (want >= 'a' && want <= 'z') want = (char)(want - 'a' + 'A');

    for (int i = 0; m->title[i]; i++) {
        char c = m->title[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == want) return i;
    }
    return -1;
}

static int menu_w(const ui_menu_t *m) {
    return m->is_mark ? MARK_W : ui_text_width(m->title) + BAR_PAD_X * 2;
}

void ui_menubar_draw(ui_surface_t *s, const ui_menubar_t *mb) {
    ui_clip_reset(s);

    ui_fill(s, 0, 0, s->w, UI_MENUBAR_H - 1, UI_GREY_1);
    ui_hline(s, 0, 0, s->w, UI_WHITE);
    ui_hline(s, 0, UI_MENUBAR_H - 2, s->w, UI_GREY_4);
    ui_hline(s, 0, UI_MENUBAR_H - 1, s->w, UI_BLACK);

    for (int i = 0; i < mb->count; i++) {
        const ui_menu_t *m = &mb->menus[i];
        int x = menu_x(mb, i), w = menu_w(m);

        /* The open menu's title gets the accent, and keeps it while the
         * pull-down is down — that is what ties the two together. Drawn
         * before the label so the label lands on top of it. */
        if (i == mb->open) ui_fill(s, x, 1, w, UI_MENUBAR_H - 3, UI_ACCENT);

        const uint8_t fg = (i == mb->open) ? UI_WHITE : UI_BLACK;
        const int     ly = (UI_MENUBAR_H - 2 - UI_CHAR_H) / 2;

        if (m->is_mark) {
            ui_blit_tinted(s, ui_icon(ICON_FRANK), x + 4, 1, fg);
        } else {
            const int tx = x + BAR_PAD_X;

            if (i == mb->open) ui_text(s, tx, ly, m->title, fg);
            else               ui_text_embossed(s, tx, ly, m->title, fg);

            /* Underline the Alt key. Without it the accelerators are
             * undiscoverable: nothing on screen says that Alt+V is the
             * Video menu, and a keyboard-only board is most of the
             * fleet. */
            int n = alt_index(m);
            if (n >= 0) {
                const int ux = tx + n * UI_CHAR_W;
                ui_hline(s, ux, ly + UI_CHAR_H, UI_CHAR_W - 1, fg);
            }
        }
    }
}

static int dropdown_width(const ui_menu_t *m) {
    int w = 0;
    bool any_key = false;

    for (int i = 0; i < m->count; i++) {
        if (!m->items[i].label) continue;
        int tw = ui_text_width(m->items[i].label);
        if (tw > w) w = tw;
        if (m->items[i].key) any_key = true;
    }
    return DROP_PAD_L + w + (any_key ? KEY_GAP : 0) + DROP_PAD_R;
}

static int dropdown_height(const ui_menu_t *m) {
    int h = 2;
    for (int i = 0; i < m->count; i++)
        h += m->items[i].label ? ITEM_H : SEP_H;
    return h + 2;
}

void ui_menubar_draw_dropdown(ui_surface_t *s, const ui_menubar_t *mb) {
    if (mb->open < 0 || mb->open >= mb->count) return;
    ui_clip_reset(s);

    const ui_menu_t *m = &mb->menus[mb->open];
    const int x = menu_x(mb, mb->open);
    const int y = UI_MENUBAR_H - 1;
    const int w = dropdown_width(m);
    const int h = dropdown_height(m);

    /* Shadow, then body, then frame — same order and same offset as a
     * window, so a menu reads as the same kind of object. */
    ui_fill(s, x + UI_SHADOW_MENU, y + h, w, UI_SHADOW_MENU, UI_BLACK);
    ui_fill(s, x + w, y + UI_SHADOW_MENU, UI_SHADOW_MENU, h, UI_BLACK);

    ui_fill(s, x, y, w, h, UI_PAPER);
    ui_frame(s, x, y, w, h, UI_BLACK);
    ui_bevel_out(s, x + 1, y + 1, w - 2, h - 2);

    int iy = y + 2;
    for (int i = 0; i < m->count; i++) {
        const ui_menu_item_t *it = &m->items[i];

        if (!it->label) {
            ui_separator(s, x + 2, iy + SEP_H / 2 - 1, w - 4);
            iy += SEP_H;
            continue;
        }

        /* Disabled items stay readable in grey: knowing a command exists
         * but is unavailable is most of what greying it out is for. */
        const uint8_t fg = it->enabled ? UI_BLACK : UI_GREY_4;

        ui_text(s, x + DROP_PAD_L, iy + 2, it->label, fg);

        if (it->checked)
            ui_text(s, x + 8, iy + 2, "*", fg);

        if (it->key) {
            char eq[4] = { '^', it->key, 0, 0 };
            ui_text(s, x + w - DROP_PAD_R - ui_text_width(eq), iy + 2,
                    eq, it->enabled ? UI_GREY_5 : UI_GREY_4);
        }

        if (i == mb->highlight && it->enabled) {
            /* Redraw over the accent rather than inverting, so the key
             * equivalent and the checkmark stay legible. */
            ui_fill(s, x + 1, iy, w - 2, ITEM_H, UI_ACCENT);
            ui_text(s, x + DROP_PAD_L, iy + 2, it->label, UI_WHITE);
            if (it->checked) ui_text(s, x + 8, iy + 2, "*", UI_WHITE);
            if (it->key) {
                char eq2[4] = { '^', it->key, 0, 0 };
                ui_text(s, x + w - DROP_PAD_R - ui_text_width(eq2), iy + 2,
                        eq2, UI_WHITE);
            }
        }

        iy += ITEM_H;
    }
}

bool ui_menubar_dropdown_rect(const ui_menubar_t *mb,
                              int *x, int *y, int *w, int *h) {
    if (mb->open < 0 || mb->open >= mb->count) return false;
    const ui_menu_t *m = &mb->menus[mb->open];
    *x = menu_x(mb, mb->open);
    *y = UI_MENUBAR_H - 1;
    *w = dropdown_width(m)  + UI_SHADOW_MENU;
    *h = dropdown_height(m) + UI_SHADOW_MENU;
    return true;
}

int ui_menubar_hit(const ui_menubar_t *mb, int x, int y) {
    if (y < 0 || y >= UI_MENUBAR_H) return -1;
    for (int i = 0; i < mb->count; i++) {
        int mx = menu_x(mb, i), mw = menu_w(&mb->menus[i]);
        if (x >= mx && x < mx + mw) return i;
    }
    return -1;
}

int ui_menubar_hit_item(const ui_menubar_t *mb, int x, int y) {
    if (mb->open < 0) return -1;

    const ui_menu_t *m = &mb->menus[mb->open];
    const int mx = menu_x(mb, mb->open);
    const int w  = dropdown_width(m);
    if (x < mx || x >= mx + w) return -1;

    int iy = UI_MENUBAR_H - 1 + 2;
    for (int i = 0; i < m->count; i++) {
        int ih = m->items[i].label ? ITEM_H : SEP_H;
        if (y >= iy && y < iy + ih)
            return (m->items[i].label && m->items[i].enabled) ? i : -1;
        iy += ih;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Keyboard navigation                                                 */
/* ------------------------------------------------------------------ */

static bool selectable(const ui_menu_t *m, int i) {
    return i >= 0 && i < m->count && m->items[i].label && m->items[i].enabled;
}

int ui_menubar_find_alt(const ui_menubar_t *mb, char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (int i = 0; i < mb->count; i++) {
        char k = mb->menus[i].alt_key;
        if (k >= 'a' && k <= 'z') k = (char)(k - 'a' + 'A');
        if (k && k == c) return i;
    }
    return -1;
}

/* First selectable item, for when a menu is opened by keyboard. Opening
 * onto a separator or a greyed-out item and making the operator press
 * down before anything is highlighted is a small thing that reads as
 * broken. */
static int first_selectable(const ui_menu_t *m) {
    for (int i = 0; i < m->count; i++) if (selectable(m, i)) return i;
    return -1;
}

void ui_menubar_move(ui_menubar_t *mb, int dir) {
    if (mb->open < 0 || mb->open >= mb->count) return;
    const ui_menu_t *m = &mb->menus[mb->open];

    if (mb->highlight < 0) { mb->highlight = first_selectable(m); return; }

    for (int step = 0; step < m->count; step++) {
        mb->highlight += dir;
        if (mb->highlight < 0)          mb->highlight = m->count - 1;
        if (mb->highlight >= m->count)  mb->highlight = 0;
        if (selectable(m, mb->highlight)) return;
    }
    mb->highlight = -1;   /* nothing selectable at all */
}

void ui_menubar_cycle(ui_menubar_t *mb, int dir) {
    if (mb->count <= 0) return;
    if (mb->open < 0) { mb->open = 0; }
    else {
        mb->open += dir;
        if (mb->open < 0)           mb->open = mb->count - 1;
        if (mb->open >= mb->count)  mb->open = 0;
    }
    mb->highlight = first_selectable(&mb->menus[mb->open]);
}

int ui_menubar_current_cmd(const ui_menubar_t *mb) {
    if (mb->open < 0 || mb->open >= mb->count) return 0;
    const ui_menu_t *m = &mb->menus[mb->open];
    if (!selectable(m, mb->highlight)) return 0;
    return m->items[mb->highlight].cmd;
}
