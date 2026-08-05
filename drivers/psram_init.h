/*
 * frank-msx — fMSX for RP2350
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-msx
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PSRAM_INIT_H
#define PSRAM_INIT_H

#include "pico/stdlib.h"

void psram_init(uint cs_pin);

/* The ceiling the QMI divider is derived from. Defaults to the build's
 * PSRAM_MAX_FREQ_MHZ; the overclock dialog moves it. */
void psram_set_max_freq(unsigned mhz);

#endif
