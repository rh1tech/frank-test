/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_input.h — pointer and keyboard, from whatever is attached.
 *
 * The interface has to be complete without a mouse. `nyx` has no USB
 * host and no PS/2 connector; the core2 halves have no PS/2 either. On
 * those boards the only input is the UART, so every menu command has a
 * keyboard equivalent and nothing is reachable by clicking alone.
 *
 * Where a mouse *is* attached, it drives a cursor and the menu bar and
 * the test list respond to it — which is the point of drawing a menu bar
 * rather than a list of key bindings.
 */
#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* Pointer, in screen coordinates, clamped to the display. */
    int  x, y;
    bool present;          /* a mouse answered enumeration */

    bool button;           /* left button, current level  */
    bool pressed;          /* went down since the last poll */
    bool released;         /* came up since the last poll   */
    int  wheel;            /* accumulated detents, consumed on read */

    bool moved;            /* position changed since the last poll */
} ui_pointer_t;

/* Bring up whatever input hardware this board has. Safe to call when
 * there is none — it simply finds nothing. */
void ui_input_init(void);

/* Service the USB host stack. Must be called often: TinyUSB host is
 * cooperative, and a loop that goes away for a second drops reports. */
void ui_input_task(void);

/* Current pointer state. `pressed`, `released`, `moved` and `wheel` are
 * edge/accumulator fields and are cleared by this call, so it must be
 * called once per iteration and its result used, not polled twice. */
const ui_pointer_t *ui_input_pointer(void);

/* The same state without consuming the edges — for the renderer, which
 * needs the position on every frame but must not eat a click the event
 * loop has not seen yet. */
const ui_pointer_t *ui_input_pointer_last(void);

/* ------------------------------------------------------------------ */
/* Keys                                                                */
/* ------------------------------------------------------------------ */

/* Printable characters come through as themselves. Everything else gets
 * a code above 0xFF, so a caller can switch on the lot in one place. */
enum {
    UI_KEY_NONE  = -1,
    UI_KEY_UP    = 0x100,
    UI_KEY_DOWN,
    UI_KEY_LEFT,
    UI_KEY_RIGHT,
    UI_KEY_ENTER,
    UI_KEY_ESC,
    UI_KEY_TAB,
    UI_KEY_HOME,
    UI_KEY_END,
    UI_KEY_PGUP,
    UI_KEY_PGDN,

    /* Alt+letter, as UI_KEY_ALT | 'F'. The letter is upper-cased. */
    UI_KEY_ALT   = 0x200,
};

/* Next key, or UI_KEY_NONE. Merges every source: USB HID keyboard, PS/2
 * when that driver lands, and the console. One queue, because the caller
 * genuinely does not care which keyboard a key came from. */
int ui_input_getkey(void);

/* For the report line: what was found. */
bool ui_input_mouse_connected(void);
bool ui_input_keyboard_connected(void);

#endif /* UI_INPUT_H */
