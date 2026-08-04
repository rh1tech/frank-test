/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_input.c
 *
 * USB HID host, plus the console. PS/2 joins here when that driver
 * lands — the point of the merged key queue is that the caller never
 * learns which keyboard a character came from.
 *
 *
 * WHY THERE IS NO USB SERIAL CONSOLE
 *
 * The RP2350's native USB controller is either a host or a device, not
 * both. Testing a USB keyboard and mouse means being a host, so the CDC
 * console cannot exist at the same time. The console is UART.
 *
 * That is a deliberate trade and it costs something real: on a board
 * with no UART header broken out, a bare USB cable now gives you no log
 * at all. It buys the ability to test the two input devices every one of
 * these boards is built to talk to.
 */

#include "ui_input.h"
#include "ui_gfx.h"

#include "pico/stdlib.h"

#include <string.h>

#if UI_INPUT_USB_HID
#include "usbhid.h"
#endif

static ui_pointer_t s_ptr;
static bool         s_last_button;
static bool         s_init_done;

#if UI_INPUT_PS2
bool ui_ps2_init(int kbd_clk, int mouse_clk);
bool ui_ps2_keyboard_up(void);
bool ui_ps2_mouse_up(void);
int  ui_ps2_getkey(void);
void ui_ps2_task(void);
bool ui_ps2_mouse_read(int *dx, int *dy, int *wheel, unsigned *buttons);
#endif

void ui_input_init_keyboard(int ps2_kbd_clk) {
#if UI_INPUT_PS2
    ui_ps2_init(ps2_kbd_clk, -1);
#else
    (void)ps2_kbd_clk;
#endif
}

int ui_input_getchar(void) {
    /* Pump the stacks. This is a boot-window key source, and the window
     * runs before the main loop exists to do it - without this a USB
     * keyboard is enumerated but never read, which looks exactly like a
     * key that does not work. */
    ui_input_task();

    const int k = ui_input_getkey();
    if (k == UI_KEY_NONE) return -1;
    if (k & UI_KEY_ALT)   return -1;
    if (k > 0xFF)         return -1;
    return k;
}

void ui_input_init_usb(void) {
#if UI_INPUT_USB_HID
    static bool done;
    if (done) return;
    done = true;
    usbhid_init();
#endif
}

void ui_input_init(int ps2_kbd_clk, int ps2_mouse_clk) {
    if (s_init_done) return;
    s_init_done = true;

    /* Start the pointer in the middle rather than at 0,0: a cursor in
     * the top-left corner is indistinguishable from one that has not
     * been drawn yet. */
    s_ptr.x = UI_SCREEN_W / 2;
    s_ptr.y = UI_SCREEN_H / 2;

    ui_input_init_usb();   /* idempotent: main() starts it far earlier */

#if UI_INPUT_PS2
    ui_ps2_init(ps2_kbd_clk, ps2_mouse_clk);
#else
    (void)ps2_kbd_clk; (void)ps2_mouse_clk;
#endif
}

void ui_input_task(void) {
#if UI_INPUT_USB_HID
    usbhid_task();
#endif
#if UI_INPUT_PS2
    ui_ps2_task();
#endif
}

bool ui_input_mouse_connected(void) {
#if UI_INPUT_PS2
    if (ui_ps2_mouse_up()) return true;
#endif
#if UI_INPUT_USB_HID
    return usbhid_mouse_connected() != 0;
#else
    return false;
#endif
}

bool ui_input_keyboard_connected(void) {
#if UI_INPUT_PS2
    if (ui_ps2_keyboard_up()) return true;
#endif
#if UI_INPUT_USB_HID
    return usbhid_keyboard_connected() != 0;
#else
    return false;
#endif
}

const ui_pointer_t *ui_input_pointer_last(void) { return &s_ptr; }

/* Apply one mouse report to the shared pointer state. Both the USB and
 * PS/2 paths land here so the cursor behaves identically whichever is
 * attached, and a board with both does not get two sets of rules. */
static void pointer_apply(int dx, int dy, int wheel, unsigned buttons) {
    if (dx || dy) {
        const int ox = s_ptr.x, oy = s_ptr.y;
        s_ptr.x += dx;
        s_ptr.y += dy;
        if (s_ptr.x < 0) s_ptr.x = 0;
        if (s_ptr.y < 0) s_ptr.y = 0;
        if (s_ptr.x >= UI_SCREEN_W) s_ptr.x = UI_SCREEN_W - 1;
        if (s_ptr.y >= UI_SCREEN_H) s_ptr.y = UI_SCREEN_H - 1;
        if (s_ptr.x != ox || s_ptr.y != oy) s_ptr.moved = true;
    }

    /* s_last_button is the same edge-detector the USB path uses, so the
     * two cannot disagree about whether a click has already been
     * reported. */
    const bool down = (buttons & 1u) != 0;
    if (down && !s_last_button) s_ptr.pressed  = true;
    if (!down && s_last_button) s_ptr.released = true;
    s_last_button = down;
    s_ptr.button  = down;

    s_ptr.wheel += wheel;
}

const ui_pointer_t *ui_input_pointer(void) {
    s_ptr.pressed  = false;
    s_ptr.released = false;
    s_ptr.moved    = false;
    s_ptr.wheel    = 0;

#if UI_INPUT_PS2
    if (ui_ps2_mouse_up()) {
        s_ptr.present = true;
        int dx = 0, dy = 0, w = 0; unsigned b = 0;
        if (ui_ps2_mouse_read(&dx, &dy, &w, &b)) pointer_apply(dx, dy, w, b);
    }
#endif

#if UI_INPUT_USB_HID
    /* Or-in, never assign.
     *
     * This used to write usbhid_mouse_connected() straight into
     * s_ptr.present, which clobbered the PS/2 block above: with a PS/2
     * mouse and no USB mouse, present went back to false, the cursor was
     * never drawn and every report was discarded. It looked exactly like
     * the PS/2 mouse not working, and the USB one still did. */
    const bool usb_mouse = usbhid_mouse_connected() != 0;
    if (usb_mouse) s_ptr.present = true;

    if (usb_mouse) {
        usbhid_mouse_state_t m;
        usbhid_get_mouse_state(&m);

        if (m.dx || m.dy) {
            const int ox = s_ptr.x, oy = s_ptr.y;

            /* One report unit per pixel. No acceleration: this is a
             * 640x480 screen with 20-pixel rows, and a pointer that
             * accelerates is harder to land on a menu item than one that
             * does not. */
            s_ptr.x += m.dx;

            /* hid_app.c already inverts Y for the emulator's benefit —
             * up-on-the-mouse means up-in-the-game. A screen wants the
             * opposite, so invert it back rather than "fixing" a shared
             * driver two other projects depend on. */
            s_ptr.y -= m.dy;

            if (s_ptr.x < 0) s_ptr.x = 0;
            if (s_ptr.y < 0) s_ptr.y = 0;
            if (s_ptr.x >= UI_SCREEN_W) s_ptr.x = UI_SCREEN_W - 1;
            if (s_ptr.y >= UI_SCREEN_H) s_ptr.y = UI_SCREEN_H - 1;

            s_ptr.moved = (s_ptr.x != ox) || (s_ptr.y != oy);
        }

        s_ptr.wheel += m.wheel;

        const bool now = (m.buttons & 1u) != 0;
        s_ptr.pressed  = now && !s_last_button;
        s_ptr.released = !now && s_last_button;
        s_ptr.button   = now;
        s_last_button  = now;
    }
#else
    s_ptr.present = false;
#endif

    return &s_ptr;
}

/* ------------------------------------------------------------------ */
/* Keys                                                                */
/* ------------------------------------------------------------------ */

/* --- the console ---
 *
 * A terminal sends arrows as ESC [ A..D and Alt+X as ESC followed by X.
 * Both start with ESC, and a lone ESC means "close the menu" — so the
 * three can only be told apart by what arrives next, and how soon.
 *
 * The state machine below waits up to ESC_GAP_US for a continuation. At
 * 115200 baud the rest of an escape sequence follows within about 90 us,
 * so 40 ms is enormously generous and still far below the ~200 ms it
 * takes a person to type ESC and then a letter deliberately. */
#define ESC_GAP_US 40000

static int console_getkey(void) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return UI_KEY_NONE;

    if (c != 0x1B) {
        if (c == '\r' || c == '\n') return UI_KEY_ENTER;
        if (c == '\t') return UI_KEY_TAB;
        return c;
    }

    /* ESC seen: is anything following it? */
    int n = getchar_timeout_us(ESC_GAP_US);
    if (n == PICO_ERROR_TIMEOUT) return UI_KEY_ESC;

    if (n == '[' || n == 'O') {
        int a = getchar_timeout_us(ESC_GAP_US);
        switch (a) {
            case 'A': return UI_KEY_UP;
            case 'B': return UI_KEY_DOWN;
            case 'C': return UI_KEY_RIGHT;
            case 'D': return UI_KEY_LEFT;
            case 'H': return UI_KEY_HOME;
            case 'F': return UI_KEY_END;
            case '5': getchar_timeout_us(ESC_GAP_US); return UI_KEY_PGUP;
            case '6': getchar_timeout_us(ESC_GAP_US); return UI_KEY_PGDN;
            default:  return UI_KEY_NONE;
        }
    }

    /* ESC + letter is how a terminal sends Alt+letter. */
    if (n >= 'a' && n <= 'z') n = n - 'a' + 'A';
    if (n >= 'A' && n <= 'Z') return UI_KEY_ALT | n;
    return UI_KEY_ESC;
}

#if UI_INPUT_USB_HID
/* --- the USB keyboard ---
 *
 * Read the raw keycode array and do our own edge detection rather than
 * using the driver's character queue. The queue only yields printable
 * characters — no arrows, no Escape, and no modifier context — and
 * draining both it and the state would have the two fighting over the
 * same reports. */
#define HID_MOD_ALT  (0x04u | 0x40u)   /* left | right */

static uint8_t s_prev_keys[6];

static bool was_down(uint8_t kc) {
    for (int i = 0; i < 6; i++) if (s_prev_keys[i] == kc) return true;
    return false;
}

static int hid_getkey(void) {
    usbhid_keyboard_state_t st;
    usbhid_get_keyboard_state(&st);

    int out = UI_KEY_NONE;

    for (int i = 0; i < 6 && out == UI_KEY_NONE; i++) {
        const uint8_t kc = st.keycode[i];
        if (!kc || was_down(kc)) continue;      /* held, not newly pressed */

        switch (kc) {
            case 0x52: out = UI_KEY_UP;    break;
            case 0x51: out = UI_KEY_DOWN;  break;
            case 0x50: out = UI_KEY_LEFT;  break;
            case 0x4F: out = UI_KEY_RIGHT; break;
            case 0x28: out = UI_KEY_ENTER; break;
            case 0x29: out = UI_KEY_ESC;   break;
            case 0x2B: out = UI_KEY_TAB;   break;
            case 0x4A: out = UI_KEY_HOME;  break;
            case 0x4D: out = UI_KEY_END;   break;
            case 0x4B: out = UI_KEY_PGUP;  break;
            case 0x4E: out = UI_KEY_PGDN;  break;
            default:
                if (kc >= 0x04 && kc <= 0x1D) {           /* A..Z */
                    const char letter = (char)('A' + (kc - 0x04));
                    out = (st.modifier & HID_MOD_ALT)
                        ? (UI_KEY_ALT | letter)
                        : (int)((st.modifier & 0x22u) ? letter
                                                      : letter - 'A' + 'a');
                } else if (kc >= 0x1E && kc <= 0x27) {    /* 1..0 */
                    out = (kc == 0x27) ? '0' : ('1' + (kc - 0x1E));
                } else if (kc == 0x2C) {
                    out = ' ';
                }
                break;
        }
    }

    memcpy(s_prev_keys, st.keycode, sizeof(s_prev_keys));
    return out;
}
#endif

int ui_input_getkey(void) {
#if UI_INPUT_USB_HID
    int k = hid_getkey();
    if (k != UI_KEY_NONE) return k;
#endif
#if UI_INPUT_PS2
    int p = ui_ps2_getkey();
    if (p != UI_KEY_NONE) return p;
#endif
    return console_getkey();
}
