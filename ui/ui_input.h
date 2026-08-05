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
/* `ps2_kbd_clk` and `ps2_mouse_clk` are GPIO numbers, or -1 for absent.
 * The data line is always clock + 1, which is what the PS/2 driver's PIO
 * program requires and what every FRANK board wires.
 *
 * Pass -1 for the mouse when its pins are needed elsewhere. On these
 * boards the mouse sits on GP0/GP1, which is also UART0, so bringing it
 * up on a board using the serial console costs the console. */
void ui_input_init(int ps2_kbd_clk, int ps2_mouse_clk);

/* The PS/2 keyboard alone, cheap and immediate.
 *
 * Called before video selection so the two-second boot window has an
 * input. It used to have only the console, and on every board with PS/2
 * the mouse now takes GP0/GP1 from the console UART — which left those
 * boards with no way to choose a video mode at all. */
void ui_input_init_keyboard(int ps2_kbd_clk);

/* Bring the USB host up on its own, before the rest of the interface.
 * Enumeration takes a moment, so the boot window can only offer a USB
 * keyboard if this has been running for a while by the time it opens. */
void ui_input_init_usb(void);

/* Next key from the keyboard as a plain character, or -1. For the video
 * boot window, which wants one letter and has no use for arrows. */
int  ui_input_getchar(void);

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
 * keyboard, and the console. One queue, because the caller genuinely does
 * not care which keyboard a key came from. */
int ui_input_getkey(void);

/* For the report line: what was found. */
bool ui_input_mouse_connected(void);
bool ui_input_keyboard_connected(void);


/* The PS/2 side, for the PS/2 test dialog. These are implemented in
 * ui_input_ps2.c and are no-ops in a build without PS/2 support.
 *
 * The byte count is raw, taken off the wire before decoding: a port
 * carrying garbage is wired but wrong, and looks identical to a dead one
 * if you only watch decoded keys. */
bool     ui_ps2_keyboard_up(void);
bool     ui_ps2_mouse_up(void);
bool     ui_ps2_mouse_read(int *dx, int *dy, int *wheel, unsigned *buttons);
uint32_t ui_ps2_kbd_bytes(void);
uint8_t  ui_ps2_kbd_last_byte(void);

#endif /* UI_INPUT_H */
