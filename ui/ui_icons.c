/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_icons.c — the artwork.
 *
 * '#' is ink, anything else is paper. Every icon is 16 rows of 16
 * characters; the packer asserts nothing, but a short row simply ends up
 * blank on the right, which is visible immediately.
 */

#include "ui_icons.h"

#include <string.h>

#define W UI_ICON_W
#define H UI_ICON_H
#define ROW_BYTES 2

typedef const char *art_t[H];

/* ---- subsystems ---------------------------------------------------- */

static const art_t art_chip = {
    "                ",
    "   #  #  #  #   ",
    "  ############  ",
    "  #          #  ",
    "###          ###",
    "  #  ######  #  ",
    "###  #    #  ###",
    "  #  #    #  #  ",
    "###  #    #  ###",
    "  #  ######  #  ",
    "###          ###",
    "  #          #  ",
    "  ############  ",
    "   #  #  #  #   ",
    "                ",
    "                ",
};

static const art_t art_flash = {
    "                ",
    "  ############  ",
    "  #          #  ",
    "  # ######## #  ",
    "  # #      # #  ",
    "  # #  ##  # #  ",
    "  # #  ##  # #  ",
    "  # #      # #  ",
    "  # ######## #  ",
    "  #          #  ",
    "  # ## ## ## #  ",
    "  #          #  ",
    "  ############  ",
    "   ##      ##   ",
    "                ",
    "                ",
};

static const art_t art_ram = {
    "                ",
    " ############## ",
    " #            # ",
    " # ##  ##  ## # ",
    " # ##  ##  ## # ",
    " #            # ",
    " # ##  ##  ## # ",
    " # ##  ##  ## # ",
    " #            # ",
    " # ##  ##  ## # ",
    " # ##  ##  ## # ",
    " ############## ",
    "  # #  #  # #   ",
    "                ",
    "                ",
    "                ",
};

static const art_t art_disk = {
    "                ",
    "  ############  ",
    "  #   ####   #  ",
    "  #   #  #   #  ",
    "  #   #  #   #  ",
    "  #   #  #   #  ",
    "  #          #  ",
    "  #          #  ",
    "  ############  ",
    "  #          #  ",
    "  #  ######  #  ",
    "  #  #    #  #  ",
    "  #  #    #  #  ",
    "  #  ######  #  ",
    "  ############  ",
    "                ",
};

static const art_t art_display = {
    "                ",
    " ############## ",
    " #............# ",
    " #.##########.# ",
    " #.#        #.# ",
    " #.#        #.# ",
    " #.#        #.# ",
    " #.#        #.# ",
    " #.##########.# ",
    " #............# ",
    " ############## ",
    "      ####      ",
    "      ####      ",
    "   ##########   ",
    "   ##########   ",
    "                ",
};

static const art_t art_speaker = {
    "                ",
    "        ##      ",
    "       ###      ",
    "      ####   #  ",
    "  #####  #  # # ",
    "  #      # #  # ",
    "  #      # # #  ",
    "  #      # # #  ",
    "  #      # #  # ",
    "  #####  #  # # ",
    "      ####   #  ",
    "       ###      ",
    "        ##      ",
    "                ",
    "                ",
    "                ",
};

static const art_t art_keyboard = {
    "                ",
    "                ",
    " ############## ",
    " #            # ",
    " # # # # # #  # ",
    " #            # ",
    " # # # # # #  # ",
    " #            # ",
    " #  ########  # ",
    " #            # ",
    " ############## ",
    "                ",
    "                ",
    "                ",
    "                ",
    "                ",
};

static const art_t art_mouse = {
    "                ",
    "     ######     ",
    "    ##    ##    ",
    "   ##  ##  ##   ",
    "   #   ##   #   ",
    "   #   ##   #   ",
    "   ##########   ",
    "   #        #   ",
    "   #        #   ",
    "   #        #   ",
    "   ##      ##   ",
    "    ########    ",
    "                ",
    "                ",
    "                ",
    "                ",
};

static const art_t art_gamepad = {
    "                ",
    "                ",
    "  ############  ",
    " ##          ## ",
    " #   ##    #  # ",
    " # ######    ## ",
    " #   ##   #   # ",
    " #        ##  # ",
    " ##          ## ",
    "  ############  ",
    "                ",
    "                ",
    "                ",
    "                ",
    "                ",
    "                ",
};

static const art_t art_cassette = {
    "                ",
    "                ",
    " ############## ",
    " #            # ",
    " #  ###  ###   #",
    " # ##### #####  ",
    " # ## ##  ## #  ",
    " #  ###  ###   #",
    " #            # ",
    " #  ########  # ",
    " ############## ",
    "  #          #  ",
    "                ",
    "                ",
    "                ",
    "                ",
};

static const art_t art_clock = {
    "                ",
    "     ######     ",
    "   ##      ##   ",
    "  #    ##    #  ",
    " #     ##     # ",
    " #     ##     # ",
    "#      ##      #",
    "#      #####   #",
    "#              #",
    " #            # ",
    " #            # ",
    "  #          #  ",
    "   ##      ##   ",
    "     ######     ",
    "                ",
    "                ",
};

static const art_t art_usb = {
    "                ",
    "       ##       ",
    "      ####      ",
    "       ##       ",
    "       ##       ",
    "   #########    ",
    "   #   ##  #    ",
    "   #   ##  #    ",
    "  ###  ##  #    ",
    "  ###  ## ###   ",
    "       ## ###   ",
    "       ##       ",
    "      ####      ",
    "      ####      ",
    "                ",
    "                ",
};

static const art_t art_link = {
    "                ",
    "  ####    ####  ",
    "  #  #    #  #  ",
    "  #  ######  #  ",
    "  #  #    #  #  ",
    "  ####    ####  ",
    "                ",
    "   ##########   ",
    "                ",
    "  ####    ####  ",
    "  #  #    #  #  ",
    "  #  ######  #  ",
    "  #  #    #  #  ",
    "  ####    ####  ",
    "                ",
    "                ",
};

static const art_t art_chip_small = {
    "                ",
    "                ",
    "    #  #  #     ",
    "   ##########   ",
    "   #        #   ",
    " ###        ### ",
    "   #   ##   #   ",
    " ###   ##   ### ",
    "   #        #   ",
    " ###        ### ",
    "   #        #   ",
    "   ##########   ",
    "    #  #  #     ",
    "                ",
    "                ",
    "                ",
};

/* ---- status glyphs ------------------------------------------------- */

static const art_t art_tick = {
    "                ",
    "                ",
    "             ## ",
    "            ### ",
    "           ###  ",
    "          ###   ",
    "  ##     ###    ",
    "  ###   ###     ",
    "   ### ###      ",
    "    #####       ",
    "     ###        ",
    "      #         ",
    "                ",
    "                ",
    "                ",
    "                ",
};

static const art_t art_cross = {
    "                ",
    "                ",
    "  ##        ##  ",
    "  ###      ###  ",
    "   ###    ###   ",
    "    ###  ###    ",
    "     ######     ",
    "      ####      ",
    "     ######     ",
    "    ###  ###    ",
    "   ###    ###   ",
    "  ###      ###  ",
    "  ##        ##  ",
    "                ",
    "                ",
    "                ",
};

static const art_t art_dash = {
    "                ",
    "                ",
    "                ",
    "                ",
    "                ",
    "                ",
    "                ",
    "   ##########   ",
    "   ##########   ",
    "                ",
    "                ",
    "                ",
    "                ",
    "                ",
    "                ",
    "                ",
};

static const art_t art_query = {
    "                ",
    "     ######     ",
    "    ##    ##    ",
    "    ##    ##    ",
    "          ##    ",
    "         ##     ",
    "        ##      ",
    "       ##       ",
    "       ##       ",
    "                ",
    "       ##       ",
    "       ##       ",
    "                ",
    "                ",
    "                ",
    "                ",
};

/* ---- the mark ------------------------------------------------------
 *
 * A small heavy F, and nothing else.
 *
 * It began as an F inside a bolt outline, which at 16x16 read as a grey
 * blob with a smudge in it — there is not enough room for an outline and
 * a letter inside it. The plain F that replaced it filled the cell and
 * sat heavier than the menu titles beside it; a mark should anchor the
 * bar, not shout from it. This one is 8 rows in a 16-row cell.
 *
 * Deliberately not anyone else's logo. */
static const art_t art_frank = {
    "                ",
    "                ",
    "                ",
    "                ",
    "     ######     ",
    "     ######     ",
    "     ##         ",
    "     #####      ",
    "     #####      ",
    "     ##         ",
    "     ##         ",
    "     ##         ",
    "                ",
    "                ",
    "                ",
    "                ",
};

/* ------------------------------------------------------------------ */

static const art_t *const art_table[ICON_COUNT] = {
    [ICON_CHIP]       = &art_chip,
    [ICON_FLASH]      = &art_flash,
    [ICON_RAM]        = &art_ram,
    [ICON_DISK]       = &art_disk,
    [ICON_DISPLAY]    = &art_display,
    [ICON_SPEAKER]    = &art_speaker,
    [ICON_KEYBOARD]   = &art_keyboard,
    [ICON_MOUSE]      = &art_mouse,
    [ICON_GAMEPAD]    = &art_gamepad,
    [ICON_CASSETTE]   = &art_cassette,
    [ICON_CLOCK]      = &art_clock,
    [ICON_USB]        = &art_usb,
    [ICON_LINK]       = &art_link,
    [ICON_CHIP_SMALL] = &art_chip_small,
    [ICON_TICK]       = &art_tick,
    [ICON_CROSS]      = &art_cross,
    [ICON_DASH]       = &art_dash,
    [ICON_QUERY]      = &art_query,
    [ICON_FRANK]      = &art_frank,
};

static uint8_t     packed[ICON_COUNT][H * ROW_BYTES];
static ui_bitmap_t bitmaps[ICON_COUNT];
static bool        packed_done;

/* Every row must be exactly W characters. A short row silently leaves
 * the right of the icon blank and a long one silently loses its last
 * column, and both look like sloppy artwork rather than a bug — which is
 * how three of these shipped with 17-character rows. */
int ui_icons_validate(void) {
    int bad = 0;
    for (int i = 0; i < ICON_COUNT; i++) {
        const art_t *a = art_table[i];
        if (!a) continue;
        for (int y = 0; y < H; y++) {
            const char *row = (*a)[y];
            if (!row) { bad++; continue; }
            int n = 0;
            while (row[n]) n++;
            if (n != W) bad++;
        }
    }
    return bad;
}

void ui_icons_init(void) {
    if (packed_done) return;

    for (int i = 0; i < ICON_COUNT; i++) {
        memset(packed[i], 0, sizeof(packed[i]));

        const art_t *a = art_table[i];
        if (a) {
            for (int y = 0; y < H; y++) {
                const char *row = (*a)[y];
                if (!row) continue;
                for (int x = 0; x < W && row[x]; x++)
                    if (row[x] == '#')
                        packed[i][y * ROW_BYTES + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7));
            }
        }

        bitmaps[i].data   = packed[i];
        bitmaps[i].mask   = NULL;   /* icons draw ink only; see ui_blit_tinted */
        bitmaps[i].w      = W;
        bitmaps[i].h      = H;
        bitmaps[i].stride = ROW_BYTES;
    }
    packed_done = true;
}

const ui_bitmap_t *ui_icon(ui_icon_id_t id) {
    ui_icons_init();
    if (id < 0 || id >= ICON_COUNT) id = ICON_QUERY;
    return &bitmaps[id];
}
