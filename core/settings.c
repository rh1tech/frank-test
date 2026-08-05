/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "settings.h"

#include "mem_test.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <stddef.h>
#include <string.h>

/* The last sector of the configured flash.
 *
 * Deliberately not "the sector after the binary": that address moves
 * every time the firmware grows, which would throw away the board's
 * identity on the one operation — reflashing — that it exists to
 * survive. The last sector is a fixed address for a given part, and the
 * only thing that could collide with it is a filesystem we do not have.
 *
 * The size is asked of the part rather than taken from the board
 * header, and that distinction matters.
 *
 * PICO_FLASH_SIZE_BYTES is a build-time constant, and one image serves
 * every board of a given package - so the 48-GPIO image says 16 MB
 * whatever it is running on. The Waveshare PiZero carries 4 MB, and a
 * FRANK socket takes whichever module is pushed into it. On any of
 * those, the last sector of the *declared* size is past the end of the
 * *actual* part: reads return whatever the address wraps to and a write
 * lands somewhere nobody chose.
 *
 * So the JEDEC capacity byte decides, read once and cached. It falls
 * back to the header only if the part will not identify itself, which
 * is the case where nothing better is available.
 *
 * Reading it drops the QMI out of XIP, so it must happen with the other
 * core parked. That is why it is cached rather than read per call: the
 * first call is main()'s early settings_load, before video wakes core 1,
 * and every later one - saving a board choice from a menu, with the
 * scanout running - gets the cached answer and touches nothing. */
static uint32_t settings_offset(void) {
    static uint32_t cached;
    if (cached) return cached;

    uint32_t jedec = 0;
    uint8_t  uid[8];
    mem_test_flash_identify(&jedec, uid);

    const uint32_t bytes = mem_test_flash_capacity(jedec);
    cached = (bytes ? bytes : (uint32_t)PICO_FLASH_SIZE_BYTES) - FLASH_SECTOR_SIZE;
    return cached;
}

#define SETTINGS_OFFSET  settings_offset()
#define SETTINGS_ADDR    ((const uint8_t *)(XIP_BASE + settings_offset()))

/* CRC-32 (IEEE, reflected) computed the slow way. This runs twice per
 * boot at most, over 32 bytes; a table would cost more flash than the
 * cycles are worth. */
static uint32_t crc32_of(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static uint32_t record_crc(const frank_settings_t *s) {
    return crc32_of(s, offsetof(frank_settings_t, crc32));
}

bool settings_load(frank_settings_t *out) {
    memset(out, 0, sizeof(*out));
    out->magic   = FRANKID_MAGIC;
    out->version = FRANKID_VERSION;
    out->board   = FRANK_BOARD_UNKNOWN;
    out->role    = FRANK_ROLE_SINGLE;
    out->video   = VIDEO_AUTO;

    frank_settings_t stored;
    memcpy(&stored, SETTINGS_ADDR, sizeof(stored));

    if (stored.magic != FRANKID_MAGIC)     return false;
    /* Any version but ours, in either direction. Newer means a build we
     * cannot read; older means ids that have since been renumbered. */
    if (stored.version != FRANKID_VERSION)  return false;
    if (stored.crc32 != record_crc(&stored)) return false;
    if (stored.board >= FRANK_BOARD_COUNT)   return false;

    *out = stored;
    return true;
}

bool settings_save(const frank_settings_t *in) {
    /* One flash page, zero-padded. flash_range_program insists on
     * FLASH_PAGE_SIZE multiples and the record is far smaller. */
    static uint8_t page[FLASH_PAGE_SIZE];
    frank_settings_t s = *in;

    s.magic   = FRANKID_MAGIC;
    s.version = FRANKID_VERSION;
    s.crc32   = record_crc(&s);

    memset(page, 0xFF, sizeof(page));
    memcpy(page, &s, sizeof(s));

    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(irq);

    /* Read back. A settings write that failed silently would make the
     * next boot ask the same question with no explanation, which is
     * exactly the kind of fault that gets blamed on the operator. */
    return memcmp(SETTINGS_ADDR, &s, sizeof(s)) == 0;
}

bool settings_set_console(bool keep) {
    frank_settings_t s;
    settings_load(&s);            /* defaults on failure, which is fine */
    s.console = keep ? 1u : 0u;
    return settings_save(&s);
}

bool settings_set_board(frank_board_id_t id, frank_role_t role) {
    frank_settings_t s;
    settings_load(&s);            /* defaults on failure, which is fine */
    s.board = (uint16_t)id;
    s.role  = (uint16_t)role;
    return settings_save(&s);
}


bool settings_clear(void) {
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);

    frank_settings_t s;
    return !settings_load(&s);    /* success means it no longer validates */
}
