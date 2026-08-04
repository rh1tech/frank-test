/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "settings.h"

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
 * PICO_FLASH_SIZE_BYTES comes from the board header, so a board with a
 * 4 MB part gets its own last sector rather than an address off the end. */
#define SETTINGS_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_ADDR    ((const uint8_t *)(XIP_BASE + SETTINGS_OFFSET))

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
    if (stored.version > FRANKID_VERSION)  return false;   /* written by a newer build */
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
