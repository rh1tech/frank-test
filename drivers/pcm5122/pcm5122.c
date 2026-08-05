/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * pcm5122.c — bringing up the audio hat's DAC.
 *
 * Ported from SpeccyP's drivers/pcm5122, which drives this hat on this
 * board. Two differences, both deliberate.
 *
 * It is bit-banged rather than driven from an I2C peripheral. SpeccyP
 * owns the machine and can claim i2c1 for the whole run; this firmware
 * shares the bus with the detection scan and with whatever a board
 * hangs off the same two pins, and claiming the peripheral for a dialog
 * that runs for ten seconds would take it away from everything else. A
 * handful of register writes does not need the hardware.
 *
 * And the address is checked with a probe rather than a read. SpeccyP
 * reads a byte to see whether anything is there, which works, but this
 * firmware already has one definition of "something acknowledged at
 * this address" and detection uses it for every other part on the bus.
 */

#include "pcm5122.h"

#include "i2c_bb.h"

#include "pico/stdlib.h"

/* The hat is strapped to the low address. */
#define PCM5122_ADDR 0x4C

/* Page 0 registers. Named rather than numbered because the sequence
 * below is otherwise unreadable. */
#define REG_PAGE_SELECT     0
#define REG_RESET           1
#define REG_STANDBY         2
#define REG_MUTE            3
#define REG_PLL_REF        13
#define REG_CLK_ERR_DETECT 37
#define REG_DVOL_L         61
#define REG_DVOL_R         62

static bool wr(unsigned sda, unsigned scl, uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = { reg, val };
    return i2c_bb_write(sda, scl, PCM5122_ADDR, buf, sizeof(buf));
}

bool pcm5122_detect(unsigned sda, unsigned scl) {
    i2c_bb_init(sda, scl);
    const bool found = i2c_bb_probe(sda, scl, PCM5122_ADDR);
    i2c_bb_release(sda, scl);
    return found;
}

bool pcm5122_init(unsigned sda, unsigned scl) {
    i2c_bb_init(sda, scl);

    if (!i2c_bb_probe(sda, scl, PCM5122_ADDR)) {
        i2c_bb_release(sda, scl);
        return false;
    }

    bool ok = true;
    ok &= wr(sda, scl, REG_PAGE_SELECT, 0x00);

    /* Reset both the registers and the modules. The hat may have been
     * left mid-configuration by whatever ran before this. */
    ok &= wr(sda, scl, REG_RESET, 0x11);
    sleep_ms(100);

    /* The two writes that matter, and the two the datasheet's defaults
     * get wrong for this wiring.
     *
     * The hat brings out data, bit clock and word clock and no system
     * clock at all, so the PLL is referenced to BCK, and the clock
     * error detector is told to ignore a missing SCK. Left at their
     * defaults the DAC waits for a master clock that never arrives:
     * every write is acknowledged, nothing is audible, and there is no
     * error anywhere to explain it. */
    ok &= wr(sda, scl, REG_PLL_REF, 0x10);
    ok &= wr(sda, scl, REG_CLK_ERR_DETECT, 0x08);

    ok &= wr(sda, scl, REG_STANDBY, 0x00);
    ok &= wr(sda, scl, REG_MUTE, 0x00);

    /* 0 dB on both channels. The melody is a square wave, which is
     * already loud for its amplitude, and this hat drives headphones. */
    ok &= wr(sda, scl, REG_DVOL_L, 0x30);
    ok &= wr(sda, scl, REG_DVOL_R, 0x30);

    i2c_bb_release(sda, scl);
    return ok;
}

bool pcm5122_quiet(unsigned sda, unsigned scl) {
    i2c_bb_init(sda, scl);

    bool ok = i2c_bb_probe(sda, scl, PCM5122_ADDR);
    if (ok) {
        ok &= wr(sda, scl, REG_PAGE_SELECT, 0x00);
        ok &= wr(sda, scl, REG_MUTE, 0x11);      /* both channels */
        ok &= wr(sda, scl, REG_STANDBY, 0x10);
    }

    i2c_bb_release(sda, scl);
    return ok;
}
