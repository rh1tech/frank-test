/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * main.c — link check for the core layer.
 *
 * Calls every public entry point once so the linker has to resolve them
 * all. Running it on hardware is a legitimate first bring-up step (it
 * prints the detection reasoning and nothing else), but its job here is
 * to fail the build if the core has drifted out of shape.
 */
#include "detect.h"
#include "settings.h"
#include "video_detect.h"
#include "video_select.h"

#include "ui_desktop.h"
#include "ui_video.h"

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include <stdio.h>

int main(void) {
    /* The HSTX HDMI path needs clk_hstx == 126 MHz: TMDS is 10 bits per
     * pixel, HSTX emits 2 bits per lane per clock, so five HSTX clocks
     * per pixel at a 25.2 MHz pixel rate.
     *
     * video_output.c derives clk_hstx as clk_sys / MODE_HSTX_CLK_DIV, so
     * the two have to be chosen together: 252 MHz with DIV=2 (set in
     * CMakeLists) is the pairing frank_core2u ships and is proven on this
     * board. At the SDK's default 150 MHz the divider yields 150 MHz and
     * there is no valid signal at all — the sink sees nothing and shows
     * its no-input pattern, which is exactly how this was found. */
    vreg_set_voltage(VREG_VOLTAGE_1_50);
    sleep_ms(10);
    if (!set_sys_clock_khz(252000, false))
        set_sys_clock_khz(126000, false);   /* still a valid HSTX pairing
                                             * at DIV=1, if 252 will not
                                             * lock */

    stdio_init_all();
    for (int i = 0; i < 8; i++) {
        printf("core selftest  sys_clk %u MHz\n",
               (unsigned)(clock_get_hz(clk_sys) / 1000000u));
        sleep_ms(250);
    }

    detect_result_t d;
    detect_run(&d);
    detect_report(&d);

    video_detect_t v;
    video_detect_run(d.board, &v);
    video_detect_report(&v);

    video_choice_t c;
    video_select_boot_window(d.board, &v, 2000, &c);
    printf("[video] chose %s via %s\n",
           frank_video_mode_name(c.mode), video_choice_source_name(c.source));

    /* Compose one frame so the interface layer is actually linked and
     * its footprint shows up in the size report. Nothing displays it
     * yet — the 2 bpp scanline expander is phase 5e. */
    frank_video_mode_t opened = ui_video_open(c.mode);
    ui_surface_t      *surf   = ui_video_surface();

    ui_desktop_t desk = {
        .board_name = d.board ? d.board->name : "unknown",
        .mcu_name   = frank_mcu_class_name(d.mcu),
        .video_name = frank_video_mode_name(c.mode),
        .unit_serial = "-",
        .manual_note = d.board ? d.board->manual_note : NULL,
    };
    desk.video_name = frank_video_mode_name(opened);
    ui_desktop_draw(surf, &desk, ui_desktop_menus(), 320, 240, true);
    ui_video_present();
    printf("[ui] %dx%d 2bpp, %u bytes, backend %s\n",
           UI_SCREEN_W, UI_SCREEN_H, (unsigned)UI_FB_BYTES,
           ui_video_current() ? ui_video_current()->name : "none");

    while (true) tight_loop_contents();
}
