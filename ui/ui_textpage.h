/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_textpage.h — the interface as a page of text, for the outputs that
 * cannot show the desktop. See ui_textpage.c.
 */
#ifndef UI_TEXTPAGE_H
#define UI_TEXTPAGE_H

#include <stdbool.h>
#include <stdint.h>

/* The desktop and menu bar types are anonymous structs behind
 * typedefs, so they cannot be forward declared. */
#include "ui_desktop.h"
#include "ui_menu.h"

/* An 8 bpp indexed frame, one byte per pixel, and its size. */
void ui_textpage_target(uint8_t *frame, int w, int h);

void ui_textpage_set_desktop(const ui_desktop_t *d);
void ui_textpage_set_menubar(const ui_menubar_t *mb);

/* Rebuild the whole page. Cheap enough to do every present. */
void ui_textpage_draw(void);

/* True once the interface has handed its state over. Backends fall back
 * to scaling until then, because the page would otherwise be blank. */
bool ui_textpage_ready(void);

#endif /* UI_TEXTPAGE_H */
