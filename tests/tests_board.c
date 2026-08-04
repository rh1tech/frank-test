/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tests_board.c — video, the inter-processor link, the peripherals the
 * detector already touched, and the assembly-defect scan.
 *
 * Several of these are reporting on work Tier 1 did during detection
 * rather than repeating it. That is deliberate: probing an I2C bus twice
 * proves nothing the first probe did not, and a test rig whose runtime
 * is dominated by re-answering settled questions gets skipped.
 */

#include "registry.h"
#include "video_detect.h"
#include "ui_video.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Video                                                               */
/* ------------------------------------------------------------------ */

static ui_test_state_t t_video_detect(const detect_result_t *d, char *detail,
                                      unsigned len, test_progress_fn p) {
    (void)p;
    /* Re-running the detectors here would mean tearing the video mode
     * down mid-run, so this reports what came up instead. What is being
     * tested is the decision, not the probe. */
    const ui_video_backend_t *b = ui_video_current();
    if (!b) {
        snprintf(detail, len, "no backend");
        return TEST_FAIL;
    }
    /* The detector ran and reached a verdict; that is what this row is
     * about. Whether a sink is attached is unknowable on this hardware
     * (no HPD anywhere in the fleet) and is not this board's fault, so it
     * is not folded into a pass or a fail. */
    snprintf(detail, len, "%s", frank_video_mode_name(b->mode));
    return TEST_PASS;
}

/* Does the pipeline actually produce frames?
 *
 * This used to sit at "could not run" until the operator pressed a key to
 * say they could read the screen — which was asking someone looking at
 * the answer to type it in, and left a question mark on a working board.
 *
 * There is something objective to measure instead. The HSTX scanout
 * raises a vertical-blank callback once per frame, so counting it over a
 * short window proves the whole chain from framebuffer to pixel clock is
 * live: 60 Hz is three in 50 ms.
 *
 * It still cannot prove a monitor is plugged in — no FRANK board wires
 * HPD, so nothing can — but an absent sink is not this board failing,
 * and conflating the two is what put a "?" on hardware that worked. */
static ui_test_state_t t_video_out(const detect_result_t *d, char *detail,
                                   unsigned len, test_progress_fn p) {
    (void)d; (void)p;
    const ui_video_backend_t *b = ui_video_current();
    if (!b || !b->frames) {
        snprintf(detail, len, "no backend running");
        return TEST_FAIL;
    }

    /* Through the vtable, so this counts whichever backend is live —
     * it read the HDMI counter directly before VGA existed, which would
     * have reported a working VGA output as producing no frames. */
    const uint32_t before = b->frames();
    sleep_ms(50);
    const uint32_t frames = b->frames() - before;

    if (frames == 0) {
        snprintf(detail, len, "no frames");
        return TEST_FAIL;
    }

    /* No resolution here. It used to say "640x480" unconditionally,
     * which is a lie on composite — that path scans 320x240. The
     * backend's own name carries the geometry and "Video detect" prints
     * the mode, so this row reports the one thing it measured. */
    snprintf(detail, len, "%u Hz", (unsigned)(frames * 20u));
    return TEST_PASS;
}

/* ------------------------------------------------------------------ */
/* Peripherals the detector already established                        */
/* ------------------------------------------------------------------ */

static ui_test_state_t t_rtc(const detect_result_t *d, char *detail,
                             unsigned len, test_progress_fn p) {
    (void)p;
    if (!d->i2c_ds3231) {
        snprintf(detail, len, "no ack at 0x68");
        return TEST_FAIL;
    }
    snprintf(detail, len, "DS3231 at 0x68");
    return TEST_PASS;
}

static ui_test_state_t t_onewire(const detect_result_t *d, char *detail,
                                 unsigned len, test_progress_fn p) {
    (void)p;
    if (!d->onewire_found) {
        snprintf(detail, len, "no response");
        return TEST_FAIL;
    }
    /* The serial is the useful output, not the pass — this is the only
     * per-unit identity in the fleet that is not a probe's guess. */
    snprintf(detail, len, "%02X%02X%02X%02X%02X%02X",
             d->onewire_rom[6], d->onewire_rom[5], d->onewire_rom[4],
             d->onewire_rom[3], d->onewire_rom[2], d->onewire_rom[1]);
    return TEST_PASS;
}


/* ------------------------------------------------------------------ */
/* Adjacent-pin short scan                                             */
/* ------------------------------------------------------------------ */

/* Borrowed from murmulator-tester, which blinks the offending pin number
 * on the LED. Here it names the pair.
 *
 * This is an assembly-defect test, not a function test: it finds solder
 * bridges between neighbouring pads, which otherwise show up much later
 * as one inexplicable peripheral failure.
 *
 * Pins already known to be connected to each other are skipped rather
 * than reported — the link buses on core2 deliberately run adjacent
 * pins to the same places, and flagging those every run would train the
 * operator to ignore the result. */
/* Pins that are deliberately tied to each other, and so are not defects.
 *
 * On megafrank GP9/10/11 carry I2S, the TurboSound shift register *and*
 * the PWM audio path, and the analogue network between them (R43-R46 and
 * the RC filters) connects them to one another by design. The scan
 * reported GP9-GP10 as a short on the first board that had this circuit,
 * which is true and useless. */
static bool audio_pin(const frank_pins_t *p, unsigned pin) {
    if (p->i2s_data     != PIN_NC && pin == (unsigned)p->i2s_data)     return true;
    if (p->i2s_clk_base != PIN_NC &&
        (pin == (unsigned)p->i2s_clk_base ||
         pin == (unsigned)p->i2s_clk_base + 1)) return true;
    if (p->i2s_mclk != PIN_NC && pin == (unsigned)p->i2s_mclk) return true;

    if (p->ay_rclk  != PIN_NC && pin == (unsigned)p->ay_rclk)  return true;
    if (p->ay_srclk != PIN_NC && pin == (unsigned)p->ay_srclk) return true;
    if (p->ay_ser   != PIN_NC && pin == (unsigned)p->ay_ser)   return true;
    return false;
}

/* Does this pin belong to the link — bus or control? */
static bool link_pin(const frank_pins_t *p, unsigned pin) {
    if (p->link_a_data != PIN_NC &&
        pin >= (unsigned)p->link_a_data &&
        pin <= (unsigned)p->link_a_data + 9) return true;
    if (p->link_b_data != PIN_NC &&
        pin >= (unsigned)p->link_b_data &&
        pin <= (unsigned)p->link_b_data + 9) return true;

    if (p->link_fs     != PIN_NC && pin == (unsigned)p->link_fs)     return true;
    if (p->link_db_out != PIN_NC && pin == (unsigned)p->link_db_out) return true;
    if (p->link_db_in  != PIN_NC && pin == (unsigned)p->link_db_in)  return true;
    return false;
}

static ui_test_state_t t_gpio_short(const detect_result_t *d, char *detail,
                                    unsigned len, test_progress_fn p) {
    const frank_pins_t *pins = d->board ? &d->board->pins : NULL;
    unsigned found = 0;
    int first_a = -1, first_b = -1;

    const unsigned top = (d->mcu == FRANK_MCU_RP2350B) ? 47 : 29;

    for (unsigned pin = 0; pin < top; pin++) {
        if (p) p((int)((pin * 1000) / top), NULL);

        /* Skip the console UART. video_test_pins() puts the pad function
         * back, but it still drives the line low for tens of
         * milliseconds, which mangles whatever is being transmitted. A
         * short between the two UART pins is also not a defect this
         * board could survive far enough to report. */
        if (pins && pins->uart_tx != PIN_NC &&
            (pin == (unsigned)pins->uart_tx ||
             pin + 1 == (unsigned)pins->uart_tx)) continue;
        if (pins && pins->uart_rx != PIN_NC &&
            (pin == (unsigned)pins->uart_rx ||
             pin + 1 == (unsigned)pins->uart_rx)) continue;

        /* Skip the video pins: the sink's own termination ties them
         * together as far as this test can tell. */
        if (pins && pins->video_base != PIN_NC &&
            pin + 1 >= (unsigned)pins->video_base &&
            pin <= (unsigned)pins->video_base + 7) continue;

        /* Skip everything belonging to the inter-processor link.
         *
         * The data buses, because by design both ends sit on consecutive
         * pins and would be reported as shorts every run.
         *
         * And the control lines — FS, and both doorbells — because this
         * test *drives* pins for tens of milliseconds. FS held high is
         * how the master asks the slave to reboot, so scanning across it
         * reset the slave; driving the doorbells left the handshake out
         * of step. The symptom was a link that answered a probe from the
         * idle loop and then failed every HELLO during the run that
         * followed, which looked like an intermittent link and was in
         * fact this test breaking it. */
        if (pins && link_pin(pins, pin))     continue;
        if (pins && link_pin(pins, pin + 1)) continue;

        if (pins && audio_pin(pins, pin))     continue;
        if (pins && audio_pin(pins, pin + 1)) continue;

        int link = video_test_pins(pin, pin + 1);
        if (link & 1) {
            if (first_a < 0) { first_a = (int)pin; first_b = (int)pin + 1; }
            found++;
        }
    }
    if (p) p(1000, NULL);

    if (found) {
        snprintf(detail, len, "GP%d-GP%d%s", first_a, first_b,
                 found > 1 ? " +more" : "");
        return TEST_FAIL;
    }
    snprintf(detail, len, "no shorts");
    return TEST_PASS;
}

/* ------------------------------------------------------------------ */

const frank_test_t frank_tests_board[] = {
    { "Video detect",   ICON_DISPLAY, 0, CAP_VIDEO_ANY, t_video_detect },
    { "Video output",   ICON_DISPLAY, 0, CAP_VIDEO_ANY, t_video_out    },
    { "RTC",            ICON_CLOCK,   CAP_RTC_DS3231, 0, t_rtc          },
    { "Unit serial",    ICON_CHIP_SMALL, CAP_ONEWIRE_DS2401, 0, t_onewire },
    { "GPIO short scan", ICON_CHIP,   0, 0, t_gpio_short   },
};

const unsigned frank_tests_board_len =
    sizeof(frank_tests_board) / sizeof(frank_tests_board[0]);
