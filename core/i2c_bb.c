/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "i2c_bb.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define I2C_DELAY_US 5

static void pin_release(unsigned pin) {   /* open-drain high */
    gpio_set_dir(pin, GPIO_IN);
}
static void pin_drive_low(unsigned pin) {
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_OUT);
}

void i2c_bb_init(unsigned sda, unsigned scl) {
    gpio_init(sda); gpio_init(scl);
    gpio_put(sda, 0); gpio_put(scl, 0);
    pin_release(sda); pin_release(scl);
    gpio_pull_up(sda); gpio_pull_up(scl);
    busy_wait_us_32(I2C_DELAY_US * 4);
}

void i2c_bb_release(unsigned sda, unsigned scl) {
    gpio_disable_pulls(sda); gpio_disable_pulls(scl);
    gpio_set_dir(sda, GPIO_IN); gpio_set_dir(scl, GPIO_IN);
}

static void bb_start(unsigned sda, unsigned scl) {
    pin_release(sda);   busy_wait_us_32(I2C_DELAY_US);
    pin_release(scl);   busy_wait_us_32(I2C_DELAY_US);
    pin_drive_low(sda); busy_wait_us_32(I2C_DELAY_US);
    pin_drive_low(scl); busy_wait_us_32(I2C_DELAY_US);
}

/* SDA must already be low, which it is after a byte or an ACK. */
static void bb_restart(unsigned sda, unsigned scl) {
    pin_release(sda);   busy_wait_us_32(I2C_DELAY_US);
    pin_release(scl);   busy_wait_us_32(I2C_DELAY_US);
    pin_drive_low(sda); busy_wait_us_32(I2C_DELAY_US);
    pin_drive_low(scl); busy_wait_us_32(I2C_DELAY_US);
}

static void bb_stop(unsigned sda, unsigned scl) {
    pin_drive_low(sda); busy_wait_us_32(I2C_DELAY_US);
    pin_release(scl);   busy_wait_us_32(I2C_DELAY_US);
    pin_release(sda);   busy_wait_us_32(I2C_DELAY_US);
}

/* Returns true if the slave pulled SDA low for the ACK bit. */
static bool bb_write_byte(unsigned sda, unsigned scl, uint8_t v) {
    for (int i = 7; i >= 0; i--) {
        if (v & (1u << i)) pin_release(sda); else pin_drive_low(sda);
        busy_wait_us_32(I2C_DELAY_US);
        pin_release(scl);   busy_wait_us_32(I2C_DELAY_US);
        pin_drive_low(scl); busy_wait_us_32(I2C_DELAY_US);
    }
    pin_release(sda);       busy_wait_us_32(I2C_DELAY_US);
    pin_release(scl);       busy_wait_us_32(I2C_DELAY_US);
    const bool ack = !gpio_get(sda);
    pin_drive_low(scl);     busy_wait_us_32(I2C_DELAY_US);
    return ack;
}

/* The master acknowledges every byte but the last, which is how the
 * slave is told to stop driving the bus. */
static uint8_t bb_read_byte(unsigned sda, unsigned scl, bool ack) {
    uint8_t v = 0;

    pin_release(sda);
    for (int i = 7; i >= 0; i--) {
        busy_wait_us_32(I2C_DELAY_US);
        pin_release(scl);   busy_wait_us_32(I2C_DELAY_US);
        if (gpio_get(sda)) v |= (uint8_t)(1u << i);
        pin_drive_low(scl); busy_wait_us_32(I2C_DELAY_US);
    }

    if (ack) pin_drive_low(sda); else pin_release(sda);
    busy_wait_us_32(I2C_DELAY_US);
    pin_release(scl);   busy_wait_us_32(I2C_DELAY_US);
    pin_drive_low(scl); busy_wait_us_32(I2C_DELAY_US);
    pin_drive_low(sda);
    return v;
}

bool i2c_bb_probe(unsigned sda, unsigned scl, uint8_t addr7) {
    bb_start(sda, scl);
    const bool ack = bb_write_byte(sda, scl, (uint8_t)(addr7 << 1));  /* write */
    bb_stop(sda, scl);
    return ack;
}

bool i2c_bb_write(unsigned sda, unsigned scl, uint8_t addr7,
                  const uint8_t *data, size_t len) {
    bb_start(sda, scl);
    bool ok = bb_write_byte(sda, scl, (uint8_t)(addr7 << 1));
    for (size_t i = 0; ok && i < len; i++)
        ok = bb_write_byte(sda, scl, data[i]);
    bb_stop(sda, scl);
    return ok;
}

bool i2c_bb_read_regs(unsigned sda, unsigned scl, uint8_t addr7,
                      uint8_t reg, uint8_t *out, size_t len) {
    if (!out || len == 0) return false;

    bb_start(sda, scl);
    bool ok = bb_write_byte(sda, scl, (uint8_t)(addr7 << 1));
    if (ok) ok = bb_write_byte(sda, scl, reg);
    if (!ok) { bb_stop(sda, scl); return false; }

    bb_restart(sda, scl);
    ok = bb_write_byte(sda, scl, (uint8_t)((addr7 << 1) | 1u));   /* read */
    if (!ok) { bb_stop(sda, scl); return false; }

    for (size_t i = 0; i < len; i++)
        out[i] = bb_read_byte(sda, scl, i + 1 < len);

    bb_stop(sda, scl);
    return true;
}
