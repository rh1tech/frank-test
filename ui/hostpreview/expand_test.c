/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * expand_test.c — prove the scanline expander on the host.
 *
 * The expander is the one piece of the video path whose bugs are
 * silent: a transposed pixel pair does not crash, it produces an image
 * that looks almost right. Verifying it needs a monitor, a board and a
 * careful eye — or this, which needs none of them.
 *
 * It runs ui_expand2bpp_row() over a real composed frame, decodes the
 * RGB565 words back to palette indices, and compares against the
 * framebuffer they came from. Every pixel must match, in order. If the
 * pair packing were transposed, alternate columns would swap and this
 * would fail on the first row with fine detail in it.
 *
 * It also writes the decoded image, so a human can confirm that "the
 * bytes agree" and "the picture is right" are the same claim.
 */

#include "../ui_desktop.h"
#include "../ui_expand4bpp.h"
#include "../ui_palette.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define palette ui_palette_rgb888

static uint8_t  fb[UI_FB_BYTES];
static uint32_t line[UI_SCREEN_W / 2];
static uint32_t lut[256];

extern void preview_fill_desktop(ui_surface_t *s);   /* from preview.c */

int main(void) {
    ui_surface_t s;
    ui_surface_init(&s, fb, UI_SCREEN_W, UI_SCREEN_H);
    preview_fill_desktop(&s);

    ui_expand4bpp_build(lut, palette);

    /* RGB565 of each palette entry, to map decoded words back to an
     * index. The palette has no duplicate 565 values, which is what
     * makes this reversible — assert it rather than assume it. */
    uint16_t p565[UI_PALETTE_LEN];
    for (int i = 0; i < UI_PALETTE_LEN; i++) p565[i] = ui_rgb888_to_rgb565(palette[i]);
    for (int i = 0; i < UI_PALETTE_LEN; i++)
        for (int j = i + 1; j < UI_PALETTE_LEN; j++)
            if (p565[i] == p565[j]) {
                fprintf(stderr, "palette entries %d and %d collide in RGB565\n", i, j);
                return 1;
            }

    long mismatches = 0;
    long first_bad_row = -1;

    FILE *out = fopen("expanded.ppm", "wb");
    fprintf(out, "P6\n%d %d\n255\n", UI_SCREEN_W, UI_SCREEN_H);

    for (int y = 0; y < UI_SCREEN_H; y++) {
        ui_expand4bpp_row(line, fb + (size_t)y * UI_STRIDE, UI_STRIDE, lut);

        for (int x = 0; x < UI_SCREEN_W; x++) {
            /* Low half-word is the left pixel of each word. */
            uint16_t px = (x & 1) ? (uint16_t)(line[x / 2] >> 16)
                                  : (uint16_t)(line[x / 2] & 0xFFFFu);

            int idx = -1;
            for (int i = 0; i < UI_PALETTE_LEN; i++) if (px == p565[i]) idx = i;

            if (idx < 0 || idx != ui_pget(&s, x, y)) {
                if (first_bad_row < 0) first_bad_row = y;
                mismatches++;
            }

            uint32_t c = palette[idx < 0 ? 0 : idx];
            uint8_t rgb[3] = { (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF };
            fwrite(rgb, 1, 3, out);
        }
    }
    fclose(out);

    if (mismatches) {
        printf("FAIL  %ld pixels differ, first bad row %ld\n",
               mismatches, first_bad_row);
        return 1;
    }

    printf("PASS  %d x %d pixels round-tripped through the expander\n",
           UI_SCREEN_W, UI_SCREEN_H);
    return 0;
}
