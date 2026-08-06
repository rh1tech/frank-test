/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tlv320.c — bringing up frank_next's TLV320DAC3100.
 *
 * See tlv320.h for what is proven here and what is not. In short: the
 * bus layer is shared with everything else on these boards, and the
 * register sequence is from the datasheet and has never been heard.
 *
 *
 * THE CLOCK ARITHMETIC, SHOWN
 *
 * Worth writing down, because these five numbers are the whole
 * configuration and a reader checking this against the datasheet should
 * not have to rederive them.
 *
 * The PIO sends 16-bit stereo, so BCLK is 32 x fs. At 44.1 kHz that is
 * 1.4112 MHz, and that is the PLL's reference.
 *
 *     PLL_CLK     = BCLK x R x J.D / P
 *     DAC_CLK     = PLL_CLK / NDAC
 *     DAC_MOD_CLK = DAC_CLK / MDAC
 *     fs          = DAC_MOD_CLK / DOSR
 *
 * Working backwards from 44.1 kHz with the usual DOSR of 128:
 *
 *     PLL_CLK = 44100 x 128 x MDAC x NDAC
 *             = 44100 x 128 x 3 x 5
 *             = 84.672 MHz          (inside the 80-110 MHz the part wants)
 *
 * and against a 1.4112 MHz reference that needs
 *
 *     R x J.D / P = 84.672 / 1.4112 = 60.0 exactly
 *
 * so R = 1, P = 1, J = 60, D = 0. The fractional part being zero is
 * luck worth having: D is the field most easily got wrong, and here it
 * is simply not used.
 */

#include "tlv320.h"

#include "i2c_bb.h"

#include "pico/stdlib.h"

#define TLV320_ADDR 0x18

/* Page 0 — clocking, the serial interface and the digital DAC. */
#define P0_PAGE_SELECT      0x00
#define P0_RESET            0x01
#define P0_CLOCK_MUX        0x04   /* PLL_CLKIN and CODEC_CLKIN sources  */
#define P0_PLL_P_R          0x05
#define P0_PLL_J            0x06
#define P0_PLL_D_MSB        0x07
#define P0_PLL_D_LSB        0x08
#define P0_NDAC             0x0B
#define P0_MDAC             0x0C
#define P0_DOSR_MSB         0x0D
#define P0_DOSR_LSB         0x0E
#define P0_IFACE_CTRL1      0x1B
#define P0_DAC_DATA_PATH    0x3F
#define P0_DAC_MUTE         0x40
#define P0_DAC_VOL_L        0x41
#define P0_DAC_VOL_R        0x42

/* Page 1 — the analogue side: routing, volume, drivers. Addresses and
 * every value below are from SLAS671C tables 6-99 to 6-109. */
#define P1_HP_DRIVERS       0x1F
#define P1_HP_POP           0x21
#define P1_OUT_MIXER_ROUTE  0x23
#define P1_HPL_ANALOG_VOL   0x24
#define P1_HPR_ANALOG_VOL   0x25
#define P1_HPL_DRIVER       0x28
#define P1_HPR_DRIVER       0x29

static bool wr(unsigned sda, unsigned scl, uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = { reg, val };
    return i2c_bb_write(sda, scl, TLV320_ADDR, buf, sizeof(buf));
}

static bool page(unsigned sda, unsigned scl, uint8_t n) {
    return wr(sda, scl, P0_PAGE_SELECT, n);
}

bool tlv320_detect(unsigned sda, unsigned scl) {
    i2c_bb_init(sda, scl);
    const bool found = i2c_bb_probe(sda, scl, TLV320_ADDR);
    i2c_bb_release(sda, scl);
    return found;
}

bool tlv320_init(unsigned sda, unsigned scl) {
    i2c_bb_init(sda, scl);

    bool ok = page(sda, scl, 0);
    if (!ok) { i2c_bb_release(sda, scl); return false; }

    /* Software reset, and the millisecond the part needs to finish it.
     * Everything below assumes reset defaults. */
    ok = ok && wr(sda, scl, P0_RESET, 0x01);
    sleep_ms(2);

    /* PLL from BCLK, codec from the PLL.
     *
     * bits[3:2] PLL_CLKIN = 01 (BCLK), bits[1:0] CODEC_CLKIN = 11 (PLL).
     * This is the line that makes the unused MCLK pin irrelevant. */
    ok = ok && wr(sda, scl, P0_CLOCK_MUX, 0x07);

    /* PLL on, P = 1, R = 1. bit7 powers it up, bits[6:4] are P, [3:0] R,
     * and P = 1 is encoded as 1 rather than 0. */
    ok = ok && wr(sda, scl, P0_PLL_P_R, 0x91);
    ok = ok && wr(sda, scl, P0_PLL_J, 60);
    ok = ok && wr(sda, scl, P0_PLL_D_MSB, 0);
    ok = ok && wr(sda, scl, P0_PLL_D_LSB, 0);
    sleep_ms(10);                       /* PLL lock */

    /* Dividers, each with its power-up bit set. See the arithmetic at
     * the top of the file. */
    ok = ok && wr(sda, scl, P0_NDAC, 0x80 | 5);
    ok = ok && wr(sda, scl, P0_MDAC, 0x80 | 3);
    ok = ok && wr(sda, scl, P0_DOSR_MSB, 0);
    ok = ok && wr(sda, scl, P0_DOSR_LSB, 128);

    /* I2S, 16-bit, codec is the clock slave — which it must be, because
     * the PIO is the master and there is nothing here to arbitrate. All
     * four of those are the reset state; written anyway, because the
     * sequence should not depend on a reset having been believed. */
    ok = ok && wr(sda, scl, P0_IFACE_CTRL1, 0x00);

    /* ---- analogue ---- */
    ok = ok && page(sda, scl, 1);

    /* De-pop before power, because it governs the power-on ramp that
     * the next write starts.
     *
     * D7=1 powers the DAC down only after the amplifiers, D6-D3=0011 is
     * a 1.53 ms driver power-on and D2-D1=11 a 3.9 ms ramp step. The
     * reset value asks for 304 ms, which would swallow the first third
     * of a one-second melody and read as a dead channel. */
    ok = ok && wr(sda, scl, P1_HP_POP, 0x9E);

    /* HPL and HPR powered, common mode 1.35 V. D2 is reserved and must
     * be written as 1 — leaving it clear is the difference between a
     * driver that comes up and one that acknowledges and does not. */
    ok = ok && wr(sda, scl, P1_HP_DRIVERS, 0xC4);
    sleep_ms(5);

    /* The signal path, in order: each DAC into its channel mixer
     * (D7-D6 = 01, D3-D2 = 01), each mixer through the analogue volume
     * control and on to its headphone driver (D7 = 1, gain 0 = 0 dB).
     *
     * The alternative is routing each DAC straight at its driver
     * (D7-D6 = 10), which works and skips the analogue volume entirely
     * — so the two volume writes below would then be doing nothing. */
    ok = ok && wr(sda, scl, P1_OUT_MIXER_ROUTE, 0x44);
    ok = ok && wr(sda, scl, P1_HPL_ANALOG_VOL, 0x80);
    ok = ok && wr(sda, scl, P1_HPR_ANALOG_VOL, 0x80);

    /* Drivers at 0 dB and unmuted. D2 unmutes, D1 is reserved and
     * written as 1; a zero here is a driver that is powered, routed and
     * silent, which is the most convincing way to fail. */
    ok = ok && wr(sda, scl, P1_HPL_DRIVER, 0x06);
    ok = ok && wr(sda, scl, P1_HPR_DRIVER, 0x06);

    /* ---- digital, last ---- */
    ok = ok && page(sda, scl, 0);

    /* Both DAC channels on, each taking its own side of the stream.
     * bits[7:6] power L and R, [5:4] and [3:2] pick the data path. */
    ok = ok && wr(sda, scl, P0_DAC_DATA_PATH, 0xD4);
    ok = ok && wr(sda, scl, P0_DAC_VOL_L, 0x00);        /* 0 dB          */
    ok = ok && wr(sda, scl, P0_DAC_VOL_R, 0x00);

    /* Unmute last, so nothing is audible until everything upstream of
     * it is settled. */
    ok = ok && wr(sda, scl, P0_DAC_MUTE, 0x00);

    i2c_bb_release(sda, scl);
    return ok;
}

bool tlv320_quiet(unsigned sda, unsigned scl) {
    i2c_bb_init(sda, scl);

    /* Mute the digital side first, then take the analogue drivers down.
     * The other order leaves the drivers live over a muting transient. */
    bool ok = page(sda, scl, 0);
    ok = ok && wr(sda, scl, P0_DAC_MUTE, 0x0C);         /* mute L and R  */
    ok = ok && wr(sda, scl, P0_DAC_DATA_PATH, 0x00);    /* DACs off      */

    ok = ok && page(sda, scl, 1);
    ok = ok && wr(sda, scl, P1_HPL_DRIVER, 0x02);       /* mute, keep D1 */
    ok = ok && wr(sda, scl, P1_HPR_DRIVER, 0x02);
    ok = ok && wr(sda, scl, P1_HP_DRIVERS, 0x04);       /* drivers off   */

    i2c_bb_release(sda, scl);
    return ok;
}
