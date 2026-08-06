/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * mem_test.h — flash and PSRAM exercises shared by both MCUs.
 *
 * Both halves of the board carry the same parts (W25Q128 16 MB QSPI
 * flash + ESP-PSRAM64H 8 MB PSRAM), so one implementation covers both;
 * the slave ships its results back to the master over the link and the
 * master renders them side by side.
 *
 * ORDERING CONSTRAINT: mem_test_flash_identify() briefly takes the QMI
 * out of XIP mode to issue a raw 0x9F. It must therefore run while the
 * other core is parked — on the master that means before graphics_init()
 * launches core 1 for HSTX scanout. The throughput and pattern tests
 * below are plain loads/stores through the XIP windows and are safe to
 * run at any time.
 */
#ifndef MEM_TEST_H
#define MEM_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "link_proto.h"

/* Read the flash JEDEC ID (0x9F) and the 64-bit unique ID.
 *
 * Reads the part once and answers from a cache ever after. Both values
 * are properties of the silicon and cannot change while it is powered,
 * so a repeat read could only return the same answer — and the read is
 * the dangerous part. See mem_test_flash_prime().
 *
 * The first call must run with the other core parked. */
void mem_test_flash_identify(uint32_t *jedec_id, uint8_t unique_id[8]);

/* Do that first read now, while the clock is still the one the board
 * started at, and throw the answer away.
 *
 * THIS IS NOT AN OPTIMISATION. flash_do_cmd() leaves XIP and returns
 * through the bootrom's flash_enter_cmd_xip(), which installs the
 * bootrom's own XIP configuration rather than the one boot2 set up.
 * At the clock the board boots at, what it installs works. At 378 or
 * 504 MHz it does not: the next instruction fetch comes back wrong and
 * the core takes a HardFault with CFSR and HFSR both zero — no faulting
 * address, because nothing dereferenced anything.
 *
 * Measured on a Core 2U master: at 504 MHz the board printed
 * "[boot] settings" and died. Skipping the settings read moved the
 * death to "[boot] detect", which issues the same command. Every call
 * site was fatal in turn, which is what made it clear the call was the
 * fault rather than any one caller.
 *
 * So main() calls this before it raises the clock, and every later
 * caller — settings_offset(), detect_run() — gets the cached answer and
 * never touches the QMI again. Overclocking works because nothing
 * re-enters XIP once the clock is up.
 *
 * Must run with the other core parked, and before any clock change. */
void mem_test_flash_prime(void);

/* Capacity implied by the JEDEC ID's third byte (2^N bytes). Returns 0
 * when the ID looks implausible. */
uint32_t mem_test_flash_capacity(uint32_t jedec_id);

/* Bring up PSRAM on `cs_pin` and work out how much is actually there by
 * looking for address aliasing. Returns 0 if no PSRAM responded. */
uint32_t mem_test_psram_probe(uint32_t cs_pin);

/* Sequential read throughput of the flash XIP window, in KiB/s. Reads
 * `bytes` starting at the base of XIP; the region is far larger than
 * the 8 KiB XIP cache so this measures the QMI, not the cache. */
uint32_t mem_test_flash_read_kbps(uint32_t bytes);

/* CRC-32 over the first `bytes` of the flash XIP window. Two runs on
 * the same firmware must agree — a mismatch means unstable QSPI timing. */
uint32_t mem_test_flash_crc(uint32_t bytes);

/* Full write-then-verify sweep of `psram_bytes` with an LFSR pattern,
 * timing both passes. `bit_errors` counts mismatching bytes. Returns
 * true when every byte read back matched. */
bool mem_test_psram_sweep(uint32_t psram_bytes,
                          uint32_t *write_kbps, uint32_t *read_kbps,
                          uint32_t *byte_errors);

/* Run the whole battery and fill in a result struct for the link. */
void mem_test_run_all(link_mem_result_t *out,
                      uint32_t flash_bytes, uint32_t psram_bytes);

#endif /* MEM_TEST_H */
