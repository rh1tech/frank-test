/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * i2c_bb.h — a software I2C master.
 *
 * Bit-banged rather than hardware I2C on purpose. Detection has to probe
 * the bus before we know what board this is, which means before we can
 * be sure the pins map onto a controller instance the way the descriptor
 * claims. A software master works on any pin pair and cannot leave a
 * peripheral half-configured behind it. It is also slow, which is the
 * right trade for a bus with 4.7K pull-ups and unknown capacitance.
 *
 * This was detect.c's private code until the bus scan and the RTC test
 * needed to read registers as well as probe for an ack. Same routines,
 * one copy.
 *
 * There is no clock stretching and no arbitration. Every part on these
 * boards is a simple slave, and a master that handled the general case
 * would be more code than the thing it is testing.
 */
#ifndef I2C_BB_H
#define I2C_BB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Claim the pair as open-drain with pull-ups. Call before anything else,
 * and i2c_bb_release() when finished so the pins go back to inputs. */
void i2c_bb_init(unsigned sda, unsigned scl);
void i2c_bb_release(unsigned sda, unsigned scl);

/* True when a slave acknowledged its address. */
bool i2c_bb_probe(unsigned sda, unsigned scl, uint8_t addr7);

/* Write `len` bytes. True only if every byte was acknowledged. */
bool i2c_bb_write(unsigned sda, unsigned scl, uint8_t addr7,
                  const uint8_t *data, size_t len);

/* Set the register pointer, then read `len` bytes from it — the repeated
 * start that every register-mapped part on these boards expects. */
bool i2c_bb_read_regs(unsigned sda, unsigned scl, uint8_t addr7,
                      uint8_t reg, uint8_t *out, size_t len);

#endif /* I2C_BB_H */
