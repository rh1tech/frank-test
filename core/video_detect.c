/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * video_detect.c
 *
 * Two detectors, both taken from code that already works on real
 * boards. They answer different questions and both situations happen, so
 * this runs both rather than choosing:
 *
 *   (a) DispHstxAutoDispSel(), frank-msx drivers/disphstx/disphstx_vmode.c
 *       "Is there a VGA monitor on the DB15?"
 *       Pull-ups on the six RGB pins; a monitor's 75R to ground drags
 *       them down, an HDMI sink does not.
 *
 *   (b) testPins(), frank-msx drivers/test_pins.c (originally murmnes)
 *       "Is there a passive HDMI-to-VGA ribbon on the HDMI socket?"
 *       Such an adapter shorts the clock pair; a real HDMI cable does
 *       not.
 *
 *
 * COMPOSITE CANNOT BE DETECTED, AND THAT IS NOT AN OVERSIGHT
 *
 * The software composite driver drives the same eight pins (frank-msx
 * board_m2.h sets TV_BASE_PIN 12, the same as HDMI_BASE_PIN and
 * VGA_BASE_PIN). A composite monitor terminates CVBS with 75R to ground
 * — electrically the same signature detector (a) reads as "VGA".
 *
 * There is no channel on this hardware by which composite could announce
 * itself. That is why the boot-time override exists, and why it is a
 * requirement rather than a convenience.
 *
 *
 * WHY (b) STILL USES THE INTERNAL PULL-DOWN
 *
 * pinsig.c avoids pull-downs because of the RP2350 A2 input anomaly.
 * testPins() uses them, and is kept exactly as it is anyway: its verdict
 * is a comparison against magic values (0x00 and 0x1F) that were
 * calibrated by measurement on assembled boards, with whatever the
 * silicon actually does baked into them. "Improving" the routine would
 * silently invalidate the constants that make it work.
 *
 * The constants are also board-specific — murmulator-tester uses
 * `== 0x1F` alone on Murmulator 1.x and `== 0 || == 0x1F` on Murmulator
 * 2.0 for the same test. Treat the values here as measured for the FRANK
 * boards and re-measure before extending them to a new one.
 */

#include "video_detect.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include "pico/stdio.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* (a) RGB pull-up probe                                               */
/* ------------------------------------------------------------------ */

/* The six pins carrying the VGA resistor-ladder RGB. On the boards with
 * a VGA connector these are video_base+0..5; video_base+6 and +7 are
 * HSYNC and VSYNC, deliberately excluded — a monitor presents them to a
 * high-impedance TTL input and so loads them either way, which would
 * only add two pins of noise to the verdict. */
#define RGB_PINS 6

/* DispHSTX waits 500 us here. Kept: the pins sit behind 330R..1K of
 * ladder plus a metre or two of cable, so this is not a bare-pad time
 * constant. */
#define RGB_SETTLE_US 500

static uint8_t rgb_pullup_read(unsigned base) {
    uint8_t mask = 0;

    for (unsigned i = 0; i < RGB_PINS; i++) {
        gpio_init(base + i);
        gpio_set_dir(base + i, GPIO_IN);
        gpio_pull_up(base + i);
    }

    busy_wait_us_32(RGB_SETTLE_US);

    for (unsigned i = 0; i < RGB_PINS; i++)
        if (gpio_get(base + i)) mask |= (uint8_t)(1u << i);

    for (unsigned i = 0; i < RGB_PINS; i++)
        gpio_disable_pulls(base + i);

    return mask;
}

/* ------------------------------------------------------------------ */
/* (b) clock-pair short test — port of frank-msx drivers/test_pins.c    */
/* ------------------------------------------------------------------ */

/* Result encoding, preserved from the original so its log lines and the
 * murmulator-tester documentation still read the same way:
 *
 *   bit 5  the link was confirmed by an active drive test, not just by
 *          reading pulls
 *   bit 4  pin0 with internal pull-down
 *   bit 3  pin0 with internal pull-up
 *   bit 2  pin1 with internal pull-down
 *   bit 1  pin1 with internal pull-up
 *   bit 0  the two pins look connected
 */

/* Put a pad back the way it was found. SIO and NULL both mean "nobody
 * owned this", so leaving those alone is correct. */
static void restore_pad(unsigned pin, gpio_function_t f) {
    if (f != GPIO_FUNC_SIO && f != GPIO_FUNC_NULL)
        gpio_set_function(pin, f);
}

/* Drive pin0, watch pin1, once per polarity. */
static bool drive_and_watch(unsigned pin0, unsigned pin1, bool drive_high) {
    gpio_init(pin0);
    gpio_set_dir(pin0, GPIO_OUT);
    gpio_put(pin0, drive_high);

    gpio_init(pin1);
    gpio_set_dir(pin1, GPIO_IN);
    /* Pull the watched pin the opposite way, so only a real connection
     * can drag it across. */
    if (drive_high) gpio_pull_down(pin1); else gpio_pull_up(pin1);
    sleep_ms(33);

    const bool followed = drive_high ? gpio_get(pin1) : !gpio_get(pin1);

    gpio_deinit(pin0);
    gpio_deinit(pin1);
    return followed;
}

/* A pair is only connected if pin1 follows pin0 *both* ways.
 *
 * Testing one polarity was enough to produce a short that appeared on
 * the first run after reset and never again: a floating pin holds charge
 * from whatever last drove it, and against a pull-down it can read high
 * once and settle by the time the operator runs the tests a second time.
 * Nothing physical behaves that way. A soldered-together pair follows
 * high when driven high and low when driven low, every time, so both are
 * now required and the transient cannot pass.
 *
 * The value was also written after the settle rather than before it, so
 * the pad spent the wait driving low and the level was sampled almost
 * immediately after it changed. */
static int test_drive_case(unsigned pin0, unsigned pin1, int res,
                           bool drive_high) {
    if (!drive_and_watch(pin0, pin1, true))  return res;
    if (!drive_and_watch(pin0, pin1, false)) return res;

    res |= drive_high ? ((1 << 5) | 1) : 1;
    return res;
}

int video_test_pins(unsigned pin0, unsigned pin1) {
    int res = 0;

    /* Save both pads' functions and put them back on the way out.
     *
     * gpio_init() routes a pad to SIO and gpio_deinit() leaves it
     * unassigned, so probing GP0 or GP1 disconnects the console UART for
     * good. Video detection only ever touches GP12/13 and never noticed;
     * the adjacent-pin short scan walks every pin on the chip and killed
     * the console on its first run, silently, right after the row above
     * it had printed. */
    const gpio_function_t f0 = gpio_get_function(pin0);
    const gpio_function_t f1 = gpio_get_function(pin1);

#ifdef PICO_DEFAULT_LED_PIN
    if (pin0 == PICO_DEFAULT_LED_PIN || pin1 == PICO_DEFAULT_LED_PIN) return res;
#endif

    /* Anything queued should leave before the pads are disturbed. */
    stdio_flush();

    /* Passive pass: both pins with pull-down, then both with pull-up. */
    gpio_init(pin0); gpio_set_dir(pin0, GPIO_IN); gpio_pull_down(pin0);
    gpio_init(pin1); gpio_set_dir(pin1, GPIO_IN); gpio_pull_down(pin1);
    sleep_ms(33);
    int p0_pd = gpio_get(pin0), p1_pd = gpio_get(pin1);
    gpio_deinit(pin0); gpio_deinit(pin1);

    gpio_init(pin0); gpio_set_dir(pin0, GPIO_IN); gpio_pull_up(pin0);
    gpio_init(pin1); gpio_set_dir(pin1, GPIO_IN); gpio_pull_up(pin1);
    sleep_ms(33);
    int p0_pu = gpio_get(pin0), p1_pu = gpio_get(pin1);
    gpio_deinit(pin0); gpio_deinit(pin1);

    res = (p0_pd << 4) | (p0_pu << 3) | (p1_pd << 2) | (p1_pu << 1);

    /* A connection is only possible when both pins present the same
     * passive picture. When they do, confirm it by driving one and
     * watching the other; when they do not, the passive reading already
     * rules it out and nothing is driven. */
    if (p0_pd == p1_pd && p0_pu == p1_pu) {
        if (p0_pd == 1 && p0_pu == 1)       res = test_drive_case(pin0, pin1, res, false);
        else if (p0_pd == 0 && p0_pu == 1)  res = test_drive_case(pin0, pin1, res, true);
        else if (p0_pd == 0 && p0_pu == 0)  res = test_drive_case(pin0, pin1, res, true);
    } else if (p0_pd == 1 && p0_pu == 0 && p1_pd == 1 && p1_pu == 0) {
        /* Contradictory on both pins in the same way — the original
         * treats this as connected without a further test. */
        res |= (1 << 5) | 1;
    }

    restore_pad(pin0, f0);
    restore_pad(pin1, f1);
    return res;
}

/* ------------------------------------------------------------------ */
/* Combined                                                            */
/* ------------------------------------------------------------------ */

void video_detect_run(const frank_board_desc_t *board, video_detect_t *out) {
    memset(out, 0, sizeof(*out));

    if (!board || board->pins.video_base == PIN_NC ||
        !(board->caps & CAP_VIDEO_ANY)) {
        out->verdict = VIDEO_NONE;
        return;   /* ran_rgb / ran_pair stay false: see the report */
    }

    const unsigned base = (unsigned)board->pins.video_base;

    /* (a) only means anything on a board that has a VGA connector.
     * Running it on an HDMI-only board would read "all high, no VGA",
     * which is true but says nothing, so skip it and keep the log
     * honest about what was actually tested. */
    if (board->caps & CAP_VIDEO_VGA) {
        out->rgb_pullup_mask = rgb_pullup_read(base);
        out->vga_monitor = (out->rgb_pullup_mask == 0);
        out->ran_rgb     = true;
    } else {
        out->rgb_pullup_mask = 0x3F;
    }

    /* (b) applies anywhere there is an HDMI socket to plug an adapter
     * into, which is every video board in the fleet. */
    out->clock_pair_code = video_test_pins(base, base + 1);
    out->ran_pair = true;
    out->vga_adapter = (out->clock_pair_code == 0x00) ||
                       (out->clock_pair_code == 0x1F);

    /* The VGA signature is measured, reported, and deliberately not
     * acted on where there is also an HDMI socket.
     *
     * (a) reads the RGB ladder as loaded whenever the pins sit low under
     * a pull-up. A VGA monitor's 75-ohm terminations do that. So do the
     * board's own ladder resistors, which are soldered to GP12-19 and
     * cannot be disconnected. MegaFRANK reports a VGA monitor with
     * nothing in the VGA socket at all, and always will: the test cannot
     * separate a display from the board it is mounted on.
     *
     * (b) has the same trouble from the other side. HDMI, VGA and
     * composite all share GP12-19, so a code on the clock pair says
     * something is loading those pins, not which connector it arrived
     * through.
     *
     * Neither can confirm a sink, because no board wires hot-plug
     * detect. Acting on them cost a working HDMI display on MegaFRANK.
     * Where a board has HDMI, that is the default and the measurements
     * stay in the report as evidence rather than as a decision. VGA and
     * composite are a boot key or a menu choice, both deliberate.
     *
     * A VGA-only board is the one case where the verdict still stands:
     * there is nothing else it could be. */
    if ((out->vga_monitor || out->vga_adapter) &&
        !(board->caps & CAP_VIDEO_HDMI)) {
        out->verdict  = VIDEO_VGA;
        out->any_sink = false;
    } else if (board->caps & CAP_VIDEO_HDMI) {
        /* No VGA signature. That is consistent with an HDMI sink and
         * also with nothing being plugged in at all — the two are not
         * separable without HPD, which no board has. Default to HDMI and
         * let the "press a key if you can read this" confirmation be the
         * thing that actually establishes a display is there. */
        out->verdict  = VIDEO_HDMI;
        out->any_sink = false;
    } else {
        out->verdict  = VIDEO_NONE;
        out->any_sink = false;
    }
}

void video_detect_report(const video_detect_t *d) {
    printf("--- video detection ---\n");
    /* A detector that did not run must not print a zero that reads like
     * a measurement — that is how "we never looked" becomes "we looked
     * and found nothing". */
    if (d->ran_rgb)
        printf("  rgb pull-up  0x%02X  -> VGA monitor: %s\n",
               d->rgb_pullup_mask, d->vga_monitor ? "yes" : "no");
    else
        printf("  rgb pull-up  not run (board has no VGA connector)\n");

    if (d->ran_pair)
        printf("  clock pair   0x%02X  -> VGA adapter: %s\n",
               d->clock_pair_code, d->vga_adapter ? "yes" : "no");
    else
        printf("  clock pair   not run\n");
    printf("  verdict      %s%s\n", frank_video_mode_name(d->verdict),
           d->any_sink ? "" : "  (unconfirmed - no HPD on any FRANK board)");
    printf("  composite cannot be detected: it drives the same pins and\n"
           "  presents the same 75R-to-ground load as VGA. Hold C at boot\n"
           "  or use `video composite`.\n");
}
