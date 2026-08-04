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

/* Which pins does the descriptor already claim?
 *
 * The scan has produced four false positives on four boards, and every
 * one was a pin with something other than the MCU on its net: the ESP
 * UART, where the module drives its own TX; the gamepad data lines,
 * behind a clamp diode and a connector; the SD socket, whose pins float
 * with no card in it; and GP23, which on a Pico module is the SMPS
 * control and reaches no header at all.
 *
 * Each was patched individually until it became clear they are one
 * problem. An adjacent-pin test drives one pad and asks whether the
 * neighbour follows, and it can only answer that for a pad which is
 * nothing but a pad. The moment a net has a connector, a pull, a diode
 * or another driver on it, the reading describes the circuit rather than
 * the assembly.
 *
 * So the rule is now the inverse of a skip list: a pin is scanned only
 * if the board descriptor gives it no function. That is a much smaller
 * set on a populated board, and the row says how much smaller, because a
 * "no shorts" that quietly tested nine pins is worth less than one that
 * tested forty and the operator should be able to tell which they got.
 */
typedef uint64_t pinmask_t;

static void claim(pinmask_t *m, int pin) {
    if (pin != PIN_NC && pin >= 0 && pin < 64) *m |= (pinmask_t)1u << pin;
}

static void claim_run(pinmask_t *m, int base, int count) {
    if (base == PIN_NC) return;
    for (int i = 0; i < count; i++) claim(m, base + i);
}

static pinmask_t claimed_pins(const frank_board_desc_t *b) {
    pinmask_t m = 0;
    if (!b) return m;
    const frank_pins_t *p = &b->pins;

    claim(&m, p->uart_tx);      claim(&m, p->uart_rx);
    claim(&m, p->ps2_kb_clk);   claim(&m, p->ps2_kb_dat);
    claim(&m, p->ps2_ms_clk);   claim(&m, p->ps2_ms_dat);
    claim(&m, p->sd_dat0);      claim(&m, p->sd_cs);
    claim(&m, p->sd_clk);       claim(&m, p->sd_cmd);
    claim(&m, p->sd_dat1);      claim(&m, p->sd_dat2);
    claim(&m, p->i2s_data);     claim(&m, p->i2s_mclk);
    claim_run(&m, p->i2s_clk_base, 2);
    claim_run(&m, p->video_base, 8);
    claim(&m, p->psram_cs);
    claim(&m, p->psram_soft_sclk); claim(&m, p->psram_soft_mosi);
    claim(&m, p->psram_soft_miso);
    claim(&m, p->led_ws2812);   claim(&m, p->led_plain);
    claim(&m, p->esp_uart_tx);  claim(&m, p->esp_uart_rx);
    claim(&m, p->esp_chip_pu);  claim(&m, p->esp_gpio0);
    claim(&m, p->esp_spi_miso); claim(&m, p->esp_spi_cs);
    claim(&m, p->esp_spi_sck);  claim(&m, p->esp_spi_mosi);
    claim(&m, p->esp_hs);       claim(&m, p->esp_ready);
    claim(&m, p->esp_mux_sel);
    claim(&m, p->pad_latch);    claim(&m, p->pad_clk);
    claim(&m, p->pad_d1);       claim(&m, p->pad_d2);
    claim(&m, p->tape_in);      claim(&m, p->dip);
    claim(&m, p->i2c_sda);      claim(&m, p->i2c_scl);
    claim(&m, p->onewire);      claim(&m, p->pio_usb_dp);
    claim(&m, p->ay_rclk);      claim(&m, p->ay_srclk);
    claim(&m, p->ay_ser);
    claim_run(&m, p->link_a_data, 10);
    claim_run(&m, p->link_b_data, 10);
    claim(&m, p->link_fs);      claim(&m, p->link_db_out);
    claim(&m, p->link_db_in);

    /* A Pico-form-factor socket breaks out GP0-GP22 and GP26-GP28 only,
     * and driving GP23 — the module's SMPS mode pin — to discover that is
     * a bad idea on its own merits. Read from the descriptor rather than
     * inferred from flash_bytes, which was the first attempt and was
     * wrong: a PGA2350 carries its own flash too and exposes all 48. */
    if (b->pico_socket) {
        claim_run(&m, 23, 3);
        for (int i = 29; i < 48; i++) claim(&m, i);
    }
    return m;
}

static ui_test_state_t t_gpio_short(const detect_result_t *d, char *detail,
                                    unsigned len, test_progress_fn p) {
    unsigned found = 0, tested = 0;
    int first_a = -1, first_b = -1;

    const unsigned top = (d->mcu == FRANK_MCU_RP2350B) ? 47 : 29;
    const pinmask_t used = claimed_pins(d->board);

    for (unsigned pin = 0; pin < top; pin++) {
        if (p) p((int)((pin * 1000) / top), NULL);

        /* Both pins of the pair must be unclaimed. */
        const pinmask_t pair = ((pinmask_t)1u << pin) | ((pinmask_t)1u << (pin + 1));
        if (used & pair) continue;
        tested++;




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

    /* The count is the point. On a board where the descriptor claims
     * almost every pin this test has almost nothing to look at, and
     * saying so is the difference between evidence and reassurance. */
    if (tested == 0) {
        snprintf(detail, len, "no free pin pairs to test");
        return TEST_NORUN;
    }
    snprintf(detail, len, "no shorts in %u pairs", tested);
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
