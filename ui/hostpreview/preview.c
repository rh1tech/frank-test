/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * preview.c — render the interface on the host, to a PPM.
 *
 * The UI layer has no SDK dependency for exactly this reason. Designing
 * a pixel interface by flashing a board and squinting at a monitor is
 * slow enough that you stop iterating, and it shows in the result. This
 * builds in under a second and produces something you can look at.
 *
 *     cc -o preview preview.c ../ui_*.c ../../master/src/ui_font.c
 *     ./preview desktop.ppm
 *
 * The palette comes from ui_palette.h, the same header the display
 * backends use, so the preview cannot drift into being a nice picture
 * of a different program.
 */

#include "../ui_desktop.h"
#include "../ui_palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t fb[UI_FB_BYTES];

static void write_ppm(const char *path, const ui_surface_t *s, int scale) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }

    fprintf(f, "P6\n%d %d\n255\n", s->w * scale, s->h * scale);
    for (int y = 0; y < s->h; y++)
        for (int sy = 0; sy < scale; sy++)
            for (int x = 0; x < s->w; x++) {
                uint32_t c = ui_palette_rgb888[ui_pget(s, x, y)];
                uint8_t rgb[3] = { (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF };
                for (int sx = 0; sx < scale; sx++) fwrite(rgb, 1, 3, f);
            }
    fclose(f);
}

/* A plausible mid-run state on a miniFRANK: some tests done, one
 * running, the ones this board has no hardware for marked n/a, and one
 * genuine failure. Fake data, but shaped exactly like the real thing so
 * the layout is being judged against what it will actually hold. */
static const ui_test_row_t rows[] = {
    { ICON_CHIP,     "Silicon",          "RP2350A rev 2, 252 MHz", TEST_PASS, 0 },
    { ICON_FLASH,    "Flash",            "EF4018  16 MB",          TEST_PASS, 0 },
    { ICON_FLASH,    "Flash read",       "31.8 MiB/s",             TEST_PASS, 0 },
    { ICON_FLASH,    "Flash CRC32",      "1A2B3C4D",               TEST_PASS, 0 },
    { ICON_RAM,      "PSRAM",            "8 MB on GP8",            TEST_PASS, 0 },
    { ICON_RAM,      "PSRAM sweep",      "w 12.9  r 30.1 MiB/s",   TEST_PASS, 0 },
    { ICON_DISK,     "microSD",          "29.7 GB  FAT32",         TEST_PASS, 0 },
    { ICON_DISPLAY,  "Video detect",     "VGA monitor",            TEST_PASS, 0 },
    { ICON_DISPLAY,  "Video output",     "640x480 @ 60",           TEST_PASS, 0 },
    { ICON_SPEAKER,  "Audio I2S",        NULL,                     TEST_RUNNING, 620 },
    { ICON_SPEAKER,  "Audio mux",        "switch S2-2",            TEST_NORUN, 0 },
    { ICON_KEYBOARD, "PS/2 keyboard",    "set 2, BAT ok",          TEST_PASS, 0 },
    { ICON_MOUSE,    "PS/2 mouse",       "no response",            TEST_FAIL, 0 },
    { ICON_KEYBOARD, "USB keyboard",     "no device",              TEST_NORUN, 0 },
    { ICON_MOUSE,    "USB mouse",        "no device",              TEST_NORUN, 0 },
    { ICON_USB,      "USB hub",          NULL,                     TEST_PENDING, 0 },
    { ICON_GAMEPAD,  "Gamepad",          NULL,                     TEST_PENDING, 0 },
    { ICON_CASSETTE, "Tape in",          "switch S2-1",            TEST_NORUN, 0 },
    { ICON_CLOCK,    "RTC",              "not fitted",             TEST_NA, 0 },
    { ICON_LINK,     "Processor link",   "not fitted",             TEST_NA, 0 },
    { ICON_CHIP_SMALL, "ESP-01S",        NULL,                     TEST_PENDING, 0 },
};

/* Compose the sample scene. Shared with expand_test.c so the expander
 * is verified against a real frame rather than a synthetic pattern. */
void preview_fill_desktop(ui_surface_t *s) {
    ui_desktop_t d = {
        .board_name  = "miniFRANK",
        .mcu_name    = "RP2350A  QFN-60",
        .video_name  = "VGA",
        .unit_serial = "E6614C31",
        .rows        = rows,
        .row_count   = (int)(sizeof(rows) / sizeof(rows[0])),
        .first_visible = 0,
        .selected      = 12,
        .passed = 11, .failed = 1, .na = 2, .remaining = 5,
    };
    ui_menubar_t mb = *ui_desktop_menus();
    mb.open = -1; mb.highlight = -1;
    ui_desktop_draw(s, &d, &mb, 300, 190, true);
}

/* expand_test.c links this file for preview_fill_desktop() and brings
 * its own main. */
#ifndef PREVIEW_NO_MAIN
int main(int argc, char **argv) {
    const char *out   = argc > 1 ? argv[1] : "desktop.ppm";
    const int   scale = argc > 2 ? atoi(argv[2]) : 1;
    const int   menu  = argc > 3 ? atoi(argv[3]) : -1;

    int bad = ui_icons_validate();
    if (bad) {
        fprintf(stderr, "%d icon art rows are not %d characters long\n",
                bad, UI_ICON_W);
        return 1;
    }

    ui_surface_t s;
    ui_surface_init(&s, fb, UI_SCREEN_W, UI_SCREEN_H);

    ui_desktop_t d = {
        .board_name  = "miniFRANK",
        .mcu_name    = "RP2350A  QFN-60",
        .video_name  = "VGA",
        .unit_serial = "E6614C31",
        .rows        = rows,
        .row_count   = (int)(sizeof(rows) / sizeof(rows[0])),
        .first_visible = 0,
        .selected      = 12,
        .passed = 11, .failed = 1, .na = 2, .remaining = 5,
    };

    ui_menubar_t mb = *ui_desktop_menus();
    mb.open      = menu;
    mb.highlight = (menu >= 0) ? 4 : -1;

    ui_desktop_draw(&s, &d, &mb, 300, 190, true);
    write_ppm(out, &s, scale);

    printf("wrote %s (%dx%d, scale %d)\n", out,
           UI_SCREEN_W * scale, UI_SCREEN_H * scale, scale);
    return 0;
}
#endif /* PREVIEW_NO_MAIN */
