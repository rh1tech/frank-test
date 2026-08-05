/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * report.h — write the run to the SD card.
 *
 * One text file per board, named for its DS2401 serial where it has one,
 * appended to rather than overwritten so a unit tested twice shows both
 * runs. See report.c for why this brings in a filesystem when the tests
 * deliberately do not.
 */
#ifndef REPORT_H
#define REPORT_H

#include "detect.h"
#include "registry.h"

typedef enum {
    REPORT_OK = 0,
    REPORT_NO_BOARD,     /* nothing identified, so nothing to name it */
    REPORT_NO_CARD,      /* no socket, no card, or pins not an SPI quartet */
    REPORT_NO_FS,        /* card is there and carries no filesystem */
    REPORT_NO_WRITE,     /* mounted, would not take the file */
} report_result_t;

/* Writes and returns the filename in `name_out` on success. */
report_result_t report_write(const detect_result_t *d,
                             const registry_results_t *r,
                             char *name_out, unsigned name_len);

#endif /* REPORT_H */
