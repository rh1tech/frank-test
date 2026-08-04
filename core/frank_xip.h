/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * frank_xip.h — the XIP windows.
 *
 * These are properties of the chip, not of the board, which is why they
 * live here rather than in a board header: CS0 is flash and CS1 is PSRAM
 * on every RP2350 in the fleet, whatever is soldered to them.
 *
 * The 0x14000000 / 0x15000000 aliases bypass the XIP cache. That matters
 * for the PSRAM presence and size probes and is not a micro-optimisation:
 * through the cached window a write followed by a read of the same
 * address is answered out of the 8 KiB cache, so a missing chip looks
 * present and an address that aliases back onto offset 0 looks like it
 * does not. Throughput measurements deliberately use the cached window,
 * because that is how real code reaches these devices.
 */
#ifndef FRANK_XIP_H
#define FRANK_XIP_H

#define FRANK_XIP_FLASH_BASE         0x10000000u
#define FRANK_XIP_PSRAM_BASE         0x11000000u
#define FRANK_XIP_FLASH_NOCACHE_BASE 0x14000000u
#define FRANK_XIP_PSRAM_NOCACHE_BASE 0x15000000u

#endif /* FRANK_XIP_H */
