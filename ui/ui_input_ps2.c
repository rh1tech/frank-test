/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_input_ps2.c — PS/2 keyboard as a second source of keystrokes.
 *
 * The driver underneath is drivers/ps2, vendored from cabal by way of
 * frank-quest. It hands back raw bytes; everything here is the scan code
 * set 2 decoding on top, producing the same codes ui_input.c already
 * gets from USB HID so the rest of the interface cannot tell which
 * keyboard it is talking to.
 *
 *
 * WHY SET 2 AND NOT THE TRANSLATED SET
 *
 * A PS/2 keyboard powers up in set 2 and stays there unless something
 * asks otherwise. The host-side translation to set 1 that PC BIOSes do
 * happens in the keyboard controller, and there is no keyboard
 * controller here — just a PIO shifting bits off the wire. So set 2 is
 * what arrives.
 *
 * It is a slightly odd encoding to read if you are used to set 1. Break
 * codes are a 0xF0 prefix followed by the make code, rather than the
 * make code with the top bit set. Extended keys — the arrow cluster, the
 * right-hand modifiers — are prefixed 0xE0, and their break is 0xE0 0xF0
 * 0xXX. Both are handled by a two-flag state machine below rather than a
 * table, because the table would need doubling for something that is two
 * booleans.
 *
 *
 * THE MOUSE
 *
 * Not initialised here, and that is a hardware constraint rather than an
 * omission. On every FRANK board with PS/2 the mouse is on GP0 and GP1,
 * which is also UART0. Bringing the mouse up takes the console away, so
 * ui_input_init() only does it when the console is somewhere else, and
 * says so on the console when it declines.
 */

#include "ui_input.h"

#if UI_INPUT_PS2

#include "ps2.h"

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include <stdio.h>

/* pio0 carries the inter-processor link and pio2 the gamepad reader and
 * the composite encoder. pio1 has I2S on one state machine and room for
 * this on another; between them they fit the 32-instruction store. The
 * vendored driver also picks its interrupt with a pio0/pio1 test, so
 * pio2 was never an option for it. */
#define PS2_PIO pio1

static bool s_kbd_up;
static bool s_mouse_up;

/* Set 2 make codes for the keys that carry a character. Index is the
 * scan code; the two columns are unshifted and shifted. */
static const char s_ascii[128][2] = {
    [0x1C] = {'a','A'}, [0x32] = {'b','B'}, [0x21] = {'c','C'},
    [0x23] = {'d','D'}, [0x24] = {'e','E'}, [0x2B] = {'f','F'},
    [0x34] = {'g','G'}, [0x33] = {'h','H'}, [0x43] = {'i','I'},
    [0x3B] = {'j','J'}, [0x42] = {'k','K'}, [0x4B] = {'l','L'},
    [0x3A] = {'m','M'}, [0x31] = {'n','N'}, [0x44] = {'o','O'},
    [0x4D] = {'p','P'}, [0x15] = {'q','Q'}, [0x2D] = {'r','R'},
    [0x1B] = {'s','S'}, [0x2C] = {'t','T'}, [0x3C] = {'u','U'},
    [0x2A] = {'v','V'}, [0x1D] = {'w','W'}, [0x22] = {'x','X'},
    [0x35] = {'y','Y'}, [0x1A] = {'z','Z'},

    [0x45] = {'0',')'}, [0x16] = {'1','!'}, [0x1E] = {'2','@'},
    [0x26] = {'3','#'}, [0x25] = {'4','$'}, [0x2E] = {'5','%'},
    [0x36] = {'6','^'}, [0x3D] = {'7','&'}, [0x3E] = {'8','*'},
    [0x46] = {'9','('},

    [0x29] = {' ',' '},  [0x4E] = {'-','_'}, [0x55] = {'=','+'},
    [0x54] = {'[','{'},  [0x5B] = {']','}'}, [0x5D] = {'\\','|'},
    [0x4C] = {';',':'},  [0x52] = {'\'','"'},
    [0x41] = {',','<'},  [0x49] = {'.','>'}, [0x4A] = {'/','?'},
    [0x0E] = {'`','~'},
};

/* Decoder state. Two flags and two modifier booleans is the whole thing. */
/* What the port has actually carried, for the PS/2 test dialog. A
 * decoded key proves the whole chain but hides where it broke; the raw
 * byte and the count are what tell a silent port apart from one that is
 * receiving noise. */
static volatile uint32_t s_kbd_bytes;
static volatile uint8_t  s_kbd_last;

uint32_t ui_ps2_kbd_bytes(void)   { return s_kbd_bytes; }
uint8_t  ui_ps2_kbd_last_byte(void) { return s_kbd_last; }

/* Which keys are physically down.
 *
 * The queue reports presses and throws the rest away, which is all a
 * menu needs and not enough to light a key on screen while it is held.
 * A make sets the bit, a break clears it. Extended codes get their own
 * bitmap because 0xE0 0x75 and 0x75 are different keys sharing a number
 * - cursor up and keypad 8 - and one bitmap would light both. */
static uint8_t s_down[32], s_down_ext[32];

bool ui_ps2_key_down(uint8_t code, bool ext) {
    const uint8_t *map = ext ? s_down_ext : s_down;
    return (map[code >> 3] & (uint8_t)(1u << (code & 7u))) != 0;
}

static bool s_break;      /* 0xF0 seen: the next code is a key release  */
static bool s_ext;        /* 0xE0 seen: the next code is an extended key */
static bool s_shift, s_ctrl, s_alt;

bool ui_ps2_init(int kbd_clk, int mouse_clk) {
    if (kbd_clk < 0) return false;

    /* Idempotent for the keyboard: main() brings it up early so the
     * video boot window has something to listen to, then calls again for
     * the mouse once a display is running. A second ps2_kbd_pio_init()
     * would claim a second state machine for the same keyboard. */
    if (!s_kbd_up) s_kbd_up = ps2_kbd_pio_init(PS2_PIO, (uint)kbd_clk);
    if (!s_kbd_up) return false;

    /* The mouse only when the caller says its pins are free. */
    if (mouse_clk >= 0) {
        s_mouse_up = ps2_mouse_pio_init(PS2_PIO, (uint)mouse_clk)
                  && ps2_mouse_init_device();
    }
    return true;
}

bool ui_ps2_keyboard_up(void) { return s_kbd_up; }
bool ui_ps2_mouse_up(void)    { return s_mouse_up; }

/* One key, or UI_KEY_NONE. Releases update the modifier state and then
 * report nothing, which is what the interface wants: it acts on presses
 * and only ever asks "is Alt down" implicitly, through the code. */
int ui_ps2_getkey(void) {
    if (!s_kbd_up) return UI_KEY_NONE;

    while (ps2_kbd_has_data()) {
        const int b = ps2_kbd_get_byte();
        if (b < 0) break;

        s_kbd_bytes++;
        s_kbd_last = (uint8_t)b;

        if (b == 0xF0) { s_break = true; continue; }
        if (b == 0xE0) { s_ext   = true; continue; }

        const bool released = s_break;
        const bool ext      = s_ext;
        s_break = false;
        s_ext   = false;

        {
            uint8_t *map = ext ? s_down_ext : s_down;
            const uint8_t bit = (uint8_t)(1u << (b & 7u));
            if (released) map[b >> 3] &= (uint8_t)~bit;
            else          map[b >> 3] |= bit;
        }

        /* Modifiers are state, not keystrokes. */
        switch (b) {
            case 0x12: case 0x59: s_shift = !released; continue;  /* shift */
            case 0x14:            s_ctrl  = !released; continue;  /* ctrl  */
            case 0x11:            s_alt   = !released; continue;  /* alt   */
            default: break;
        }

        if (released) continue;

        if (ext) {
            switch (b) {
                case 0x75: return UI_KEY_UP;
                case 0x72: return UI_KEY_DOWN;
                case 0x6B: return UI_KEY_LEFT;
                case 0x74: return UI_KEY_RIGHT;
                case 0x6C: return UI_KEY_HOME;
                case 0x69: return UI_KEY_END;
                case 0x7D: return UI_KEY_PGUP;
                case 0x7A: return UI_KEY_PGDN;
                case 0x5A: return UI_KEY_ENTER;   /* keypad enter */
                default:   continue;
            }
        }

        switch (b) {
            case 0x5A: return UI_KEY_ENTER;
            case 0x76: return UI_KEY_ESC;
            case 0x05: return UI_KEY_F1;
            case 0x0D: return UI_KEY_TAB;
            case 0x66: return 0x08;           /* backspace */
            default: break;
        }

        if (b < 128 && s_ascii[b][0]) {
            const char c = s_ascii[b][s_shift ? 1 : 0];

            /* Alt+letter is how the menus are opened, and the code the
             * rest of the interface expects has the letter upper-cased.
             * Ctrl is deliberately not folded in: nothing in this
             * firmware binds it, and the one command that used to be
             * reachable by a bare letter — BOOTSEL — no longer is. */
            if (s_alt) {
                const char u = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
                return UI_KEY_ALT | (unsigned char)u;
            }
            return (unsigned char)c;
        }
    }
    return UI_KEY_NONE;
}

void ui_ps2_task(void) {
    if (s_mouse_up) ps2_mouse_poll();
}

bool ui_ps2_mouse_read(int *dx, int *dy, int *wheel, unsigned *buttons) {
    if (!s_mouse_up) return false;

    int16_t x = 0, y = 0; int8_t w = 0; uint8_t b = 0;
    const bool moved = ps2_mouse_get_state(&x, &y, &w, &b);

    *dx = x;
    /* PS/2 reports Y upward, the screen counts downward. */
    *dy = -y;
    *wheel   = w;
    *buttons = b;
    return moved;
}

#endif /* UI_INPUT_PS2 */
