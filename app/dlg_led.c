/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dlg_led.c — the indicator LEDs, driven so someone can look at them.
 *
 *
 * WHY THIS IS NOT A ROW
 *
 * Firmware can drive a pin and cannot see light. Whether the LED is
 * fitted, the right way round, or lit at all is not knowable from this
 * side, so a PASS would be an assertion about something never measured.
 * The dialog drives the hardware and says what should be visible; the
 * verdict is the operator's, which is the honest division of labour.
 *
 * What it does prove, when the answer is "no": the pin, and for the
 * WS2812 the bit protocol as well. A WS2812 that stays dark is usually a
 * data-line fault rather than a dead part, and that is worth catching.
 *
 *
 * ONE SEQUENCE, WHATEVER THE BOARD HAS
 *
 * The plain LED blinks first, then the WS2812 fades up and down through
 * red, green and blue. Boards with only one of the two run only that
 * part and the dialog closes the loop at the end of it, so there is
 * nothing to configure and nothing board-specific to remember.
 *
 * Fading rather than switching, for the addressable one, because a fade
 * is much harder to fake. A data line that is marginal — a long stub, a
 * missing series resistor, a cold joint — usually still manages full
 * brightness and falls apart in the middle of the range, where a bit
 * error turns a dim red into a bright green. Watching it climb smoothly
 * and come back down tests the protocol far harder than three static
 * colours ever did.
 *
 *
 * THE WS2812 IS BIT-BANGED
 *
 * It wants one PIO state machine and there is not one going spare: pio0
 * carries the inter-processor link, pio1 the I2S and PS/2, pio2 the
 * gamepad reader and the composite encoder. Claiming one here would mean
 * taking it from something that is also a test.
 *
 * So the waveform is made by hand, with interrupts off for the 30
 * microseconds a 24-bit frame takes. That is safe here specifically
 * because the video scanout lives on core 1 and its interrupt is core
 * 1's — masking core 0 cannot starve it. On a single-core arrangement
 * this would tear the picture, and would not be worth doing.
 *
 * Timings are the WS2812B datasheet's, which are looser than the part
 * number suggests: a zero is 0.4 us high then 0.85 low, a one is 0.8
 * then 0.45, and anything past 50 us low latches the frame. At 252 MHz a
 * cycle counter is precise enough and needs no calibration.
 */

#include "dlgs.h"

#include "ui_desktop.h"
#include "ui_gfx.h"
#include "ui_input.h"
#include "ui_video.h"
#include "ui_window.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <stdio.h>

#define DLG_W   420
#define SWATCH  46

static int s_stop_x, s_stop_y, s_stop_w, s_stop_h;

/* ------------------------------------------------------------------ */
/* WS2812                                                              */
/* ------------------------------------------------------------------ */

/* Busy-wait in system clock cycles. sleep_us() cannot express 400 ns and
 * would not be accurate at that scale anyway. */
static inline void __not_in_flash_func(spin_ns)(uint32_t ns, uint32_t hz) {
    const uint32_t cycles = (uint32_t)(((uint64_t)ns * hz) / 1000000000u);
    for (volatile uint32_t i = 0; i < cycles / 3u; i++) tight_loop_contents();
}

static void __not_in_flash_func(ws2812_send)(unsigned pin, uint32_t grb,
                                             uint32_t hz) {
    const uint32_t irq = save_and_disable_interrupts();

    for (int b = 23; b >= 0; b--) {
        const bool one = (grb >> b) & 1u;
        gpio_put(pin, 1);
        spin_ns(one ? 800u : 400u, hz);
        gpio_put(pin, 0);
        spin_ns(one ? 450u : 850u, hz);
    }

    restore_interrupts(irq);
    sleep_us(60);                 /* latch */
}

/* ------------------------------------------------------------------ */

/* The three primaries, in the order the eye finds easiest to judge. The
 * component is scaled by the fade level rather than switched, so the
 * whole range is exercised and not just the ends. */
static const struct { const char *name; uint8_t r, g, b; uint8_t ink; } s_hues[] = {
    { "red",   1u, 0u, 0u, UI_FAIL   },
    { "green", 0u, 1u, 0u, UI_OK     },
    { "blue",  0u, 0u, 1u, UI_ACCENT },
};

/* Gamma. An LED driven at half the numeric value looks far brighter than
 * half, so a linear ramp appears to spend most of its time near full and
 * the fade is not obviously a fade. Squaring is not the real curve but
 * it is close enough to look right and costs one multiply. */
static uint8_t fade_curve(unsigned level) {     /* level 0..255 */
    return (uint8_t)((level * level) / 255u);
}

static uint32_t hue_grb(unsigned hue, unsigned level) {
    const uint8_t v = fade_curve(level);
    return ((uint32_t)(s_hues[hue].g * v) << 16) |
           ((uint32_t)(s_hues[hue].r * v) << 8)  |
            (uint32_t)(s_hues[hue].b * v);
}

/* Which half of the sequence is running. */
typedef enum { PH_BLINK, PH_FADE } phase_t;

static void draw(const dlg_ctx_t *c, phase_t phase, bool have_plain,
                 bool plain_on, bool have_rgb, unsigned hue, unsigned level,
                 int plain_pin, int rgb_pin) {
    ui_surface_t *s = ui_video_surface();
    c->paint_background();

    const int content_h = 14 + SWATCH + 34;
    const int h = UI_TITLE_H + UI_WIN_PAD + DLG_TOP + content_h
                + DLG_FOOT + 18 + DLG_BOT + UI_WIN_PAD;
    const int x = (s->w - DLG_W) / 2;
    const int y = (s->h - h) / 2 - 20;

    ui_window_t win = {
        .x = x, .y = y, .w = DLG_W, .h = h,
        .title = "LEDs", .active = true,
        .closable = false, .shadow = true,
    };
    ui_window_draw(s, &win);

    int cx, cy, cw, chh;
    ui_window_content(&win, &cx, &cy, &cw, &chh);
    cx += DLG_INSET; cw -= 2 * DLG_INSET; cy += DLG_TOP;

    ui_text(s, cx, cy, "Watch the board. Nothing here can see the light.",
            UI_GREY_5);

    char line[72];
    int  col = cx;

    if (have_plain) {
        const bool lit = (phase == PH_BLINK) && plain_on;
        ui_bevel_in(s, col, cy + 16, SWATCH, SWATCH);
        ui_fill(s, col + 1, cy + 17, SWATCH - 2, SWATCH - 2,
                lit ? UI_WARN : UI_GREY_1);

        snprintf(line, sizeof(line), "LED on GP%d", plain_pin);
        ui_text(s, col + SWATCH + 10, cy + 22, line,
                phase == PH_BLINK ? UI_BLACK : UI_GREY_4);
        ui_text(s, col + SWATCH + 10, cy + 36,
                phase != PH_BLINK ? "done"
                                  : (lit ? "driven high - should be lit"
                                         : "driven low - should be dark"),
                UI_GREY_5);
        col += SWATCH + 190;
    }

    if (have_rgb) {
        /* The swatch shows what is being sent, not what is on the board.
         * If they disagree, that is the finding. It dims with the fade so
         * the panel and the LED tell the same story. */
        const bool active = (phase == PH_FADE);
        ui_bevel_in(s, col, cy + 16, SWATCH, SWATCH);
        ui_fill(s, col + 1, cy + 17, SWATCH - 2, SWATCH - 2,
                (active && level > 40u) ? s_hues[hue].ink : UI_GREY_1);

        snprintf(line, sizeof(line), "WS2812 on GP%d", rgb_pin);
        ui_text(s, col + SWATCH + 10, cy + 22, line,
                active ? UI_BLACK : UI_GREY_4);
        if (active) snprintf(line, sizeof(line), "%s, fading %u%%",
                             s_hues[hue].name, (level * 100u) / 255u);
        else        snprintf(line, sizeof(line), "waiting");
        ui_text(s, col + SWATCH + 10, cy + 36, line, UI_GREY_5);
    }

    if (!have_plain && !have_rgb)
        ui_text(s, cx, cy + 24, "This board declares no LED pins.", UI_GREY_4);

    ui_text(s, cx, cy + 16 + SWATCH + 12,
            "A WS2812 that stays dark is usually the data line, not the part.",
            UI_GREY_5);

    ui_clip_reset(s);

    s_stop_w = ui_button_width("Stop");
    s_stop_h = 18;
    s_stop_x = x + DLG_W - 16 - s_stop_w;
    s_stop_y = y + h - UI_WIN_PAD - DLG_BOT - 18;
    ui_button(s, s_stop_x, s_stop_y, s_stop_w, s_stop_h, "Stop", true, false, true);

    ui_text(s, cx, s_stop_y + 5, "Esc or Stop to finish", UI_GREY_5);

    ui_video_present();
}

/* ------------------------------------------------------------------ */

void dlg_led(const dlg_ctx_t *c) {
    const detect_result_t *d = c->detect;
    if (!d || !d->board) return;

    const frank_pins_t *p = &d->board->pins;
    const bool have_plain = (d->board->caps & CAP_LED_PLAIN) &&
                            p->led_plain != PIN_NC;
    const bool have_rgb   = (d->board->caps & CAP_LED_WS2812) &&
                            p->led_ws2812 != PIN_NC;

    const uint32_t hz = clock_get_hz(clk_sys);

    if (have_plain) {
        gpio_init((uint)p->led_plain);
        gpio_set_dir((uint)p->led_plain, GPIO_OUT);
    }
    if (have_rgb) {
        gpio_init((uint)p->led_ws2812);
        gpio_set_dir((uint)p->led_ws2812, GPIO_OUT);
        gpio_put((uint)p->led_ws2812, 0);
    }

    ui_input_task();
    while (ui_input_getkey() != UI_KEY_NONE) { }
    ui_cursor_overlay_reset();

    /* Blink first, then fade. A board with only one of the two starts on
     * whichever it has and the sequence is that much shorter. */
    phase_t  phase    = have_plain ? PH_BLINK : PH_FADE;
    bool     plain_on = false;
    unsigned blinks   = 0;
    unsigned hue      = 0;
    unsigned level    = 0;
    bool     rising   = true;
    bool     stop     = false;
    bool     first    = true;
    absolute_time_t next = get_absolute_time();

    while (!stop) {
        ui_input_task();

        int k = ui_input_getkey();
        while (k != UI_KEY_NONE) {
            if (k == UI_KEY_ESC || k == UI_KEY_ENTER) stop = true;
            k = ui_input_getkey();
        }

        const ui_pointer_t *pt = ui_input_pointer();
        const bool moved = pt->moved;
        if (pt->pressed &&
            pt->x >= s_stop_x && pt->x < s_stop_x + s_stop_w &&
            pt->y >= s_stop_y && pt->y < s_stop_y + s_stop_h)
            stop = true;

        const bool due = absolute_time_diff_us(get_absolute_time(), next) <= 0;
        if (due || first) {
            first = false;

            if (phase == PH_BLINK) {
                plain_on = !plain_on;
                gpio_put((uint)p->led_plain, plain_on);
                next = make_timeout_time_ms(400);

                /* Six transitions is three flashes: long enough to be
                 * sure it is blinking and not just on. */
                if (++blinks >= 6u) {
                    gpio_put((uint)p->led_plain, 0);
                    if (have_rgb) { phase = PH_FADE; }
                    else          { blinks = 0; }      /* nothing else to do */
                }
            } else {
                ws2812_send((uint)p->led_ws2812, hue_grb(hue, level), hz);

                /* 16 a step at 20 ms is a touch over a second and a half
                 * each way, which reads as a fade rather than a ramp. */
                if (rising) {
                    if (level >= 255u - 16u) { level = 255u; rising = false; }
                    else level += 16u;
                } else {
                    if (level <= 16u) {
                        level  = 0u;
                        rising = true;
                        hue    = (hue + 1u) % (unsigned)count_of(s_hues);
                        /* Back to the top of the sequence once the three
                         * primaries have been round. */
                        if (hue == 0u && have_plain) { phase = PH_BLINK; blinks = 0; }
                    } else level -= 16u;
                }
                next = make_timeout_time_ms(20);
            }

            draw(c, phase, have_plain, plain_on, have_rgb, hue, level,
                 p->led_plain, p->led_ws2812);
        } else if (moved) {
            if (pt->present) ui_cursor_overlay_move(pt->x, pt->y);
        }

        sleep_ms(4);
    }

    /* Left dark rather than however the cycle happened to end. A board
     * whose LED is on because a dialog closed at the wrong moment is a
     * small lie about its state. */
    if (have_plain) gpio_put((uint)p->led_plain, 0);
    if (have_rgb)   ws2812_send((uint)p->led_ws2812, 0u, hz);
}
