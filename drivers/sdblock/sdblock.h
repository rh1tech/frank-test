/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * sdblock.h — an SD card as numbered blocks, for FatFs to sit on.
 *
 * Separate from tests_sd.c, which probes the socket and is deliberately
 * bit-banged: a test that moves forty bytes has no use for a peripheral
 * and cannot leave one half-configured behind it. Writing a report is
 * the opposite job — kilobytes, and speed matters — so this uses the SPI
 * block, exactly as the throughput test does.
 *
 * The two do not run at once. The tests own the pins while they run; a
 * report is written afterwards, from the menu, and hands them back when
 * it is done.
 *
 * Initialisation is bit-banged even here. A card has to be clocked into
 * SPI mode at under 400 kHz before it will speak at all, and the SPI
 * block's slowest divider does not reach that far down from a 252 MHz
 * system clock.
 */
#ifndef SDBLOCK_H
#define SDBLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "board_desc.h"

/* Claim the socket and bring the card up. False when there are no pins,
 * no card, or the pins are not an SPI quartet. */
bool sdblock_init(const frank_pins_t *pins);

/* Give the pins back as plain inputs. */
void sdblock_release(void);

bool sdblock_read(uint32_t lba, uint8_t *buf, uint32_t count);
bool sdblock_write(uint32_t lba, const uint8_t *buf, uint32_t count);

/* Capacity in 512-byte sectors, or zero when no card is up. */
uint32_t sdblock_sectors(void);

#endif /* SDBLOCK_H */
