/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sdblock.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <string.h>

#define CMD0    0
#define CMD8    8
#define CMD9    9
#define CMD12   12
#define CMD17   17
#define CMD18   18
#define CMD24   24
#define CMD55   55
#define CMD58   58
#define ACMD41  41

#define SPI_HZ  12500000u

static struct {
    spi_inst_t *spi;
    uint        cs, clk, mosi, miso;
    bool        up;
    bool        block_addressed;
    uint32_t    sectors;
} s;

/* ---- the slow path, for bring-up only ---------------------------- */

/* A card will not answer above 400 kHz until it is in SPI mode, and the
 * SPI block cannot divide a 252 MHz clock that far down. So the opening
 * exchange is banged by hand and the block takes over afterwards. */
static uint8_t bb_xfer(uint8_t out) {
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        gpio_put(s.mosi, (out >> i) & 1u);
        busy_wait_us_32(1);
        gpio_put(s.clk, 1);
        busy_wait_us_32(1);
        in = (uint8_t)((in << 1) | (gpio_get(s.miso) ? 1u : 0u));
        gpio_put(s.clk, 0);
    }
    return in;
}

static void bb_pins(void) {
    gpio_init(s.cs);   gpio_set_dir(s.cs, GPIO_OUT);   gpio_put(s.cs, 1);
    gpio_init(s.clk);  gpio_set_dir(s.clk, GPIO_OUT);  gpio_put(s.clk, 0);
    gpio_init(s.mosi); gpio_set_dir(s.mosi, GPIO_OUT); gpio_put(s.mosi, 1);
    gpio_init(s.miso); gpio_set_dir(s.miso, GPIO_IN);  gpio_pull_up(s.miso);
}

static uint8_t bb_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    bb_xfer(0xFF);
    bb_xfer((uint8_t)(0x40u | cmd));
    bb_xfer((uint8_t)(arg >> 24)); bb_xfer((uint8_t)(arg >> 16));
    bb_xfer((uint8_t)(arg >> 8));  bb_xfer((uint8_t)arg);
    bb_xfer(crc);

    uint8_t r = 0xFF;
    for (int i = 0; i < 16 && (r & 0x80u); i++) r = bb_xfer(0xFF);
    return r;
}

/* ---- the fast path ------------------------------------------------ */

static uint8_t hw_xfer(uint8_t out) {
    uint8_t in = 0xFF;
    spi_write_read_blocking(s.spi, &out, &in, 1);
    return in;
}

static uint8_t hw_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    hw_xfer(0xFF);
    hw_xfer((uint8_t)(0x40u | cmd));
    hw_xfer((uint8_t)(arg >> 24)); hw_xfer((uint8_t)(arg >> 16));
    hw_xfer((uint8_t)(arg >> 8));  hw_xfer((uint8_t)arg);
    hw_xfer(crc);

    uint8_t r = 0xFF;
    for (int i = 0; i < 16 && (r & 0x80u); i++) r = hw_xfer(0xFF);
    return r;
}

static bool hw_rx_block(uint8_t *buf, unsigned len) {
    uint8_t tok = 0xFF;
    for (int i = 0; i < 40000 && tok == 0xFF; i++) tok = hw_xfer(0xFF);
    if (tok != 0xFE) return false;

    spi_read_blocking(s.spi, 0xFF, buf, len);
    hw_xfer(0xFF); hw_xfer(0xFF);            /* CRC, unused in SPI mode */
    return true;
}

static bool hw_tx_block(const uint8_t *buf, unsigned len, uint8_t token) {
    hw_xfer(0xFF);
    hw_xfer(token);
    spi_write_blocking(s.spi, buf, len);
    hw_xfer(0xFF); hw_xfer(0xFF);

    if ((hw_xfer(0xFF) & 0x1Fu) != 0x05u) return false;

    /* The card holds the line low while it commits. A worn one can take
     * a surprising while over this. */
    for (unsigned i = 0; i < 500000u; i++)
        if (hw_xfer(0xFF) != 0x00u) return true;
    return false;
}

/* ---- capacity ----------------------------------------------------- */

static uint32_t sectors_from_csd(const uint8_t *csd) {
    if ((csd[0] >> 6) == 1) {                       /* CSD v2: SDHC/SDXC */
        const uint32_t c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16) |
                                ((uint32_t)csd[8] << 8) | csd[9];
        return (c_size + 1u) * 1024u;
    }
    /* v1 encodes a size and a multiplier, in bytes. */
    const uint32_t c_size = ((uint32_t)(csd[6] & 0x03u) << 10) |
                            ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
    const uint32_t mult   = (uint32_t)(((csd[9] & 0x03u) << 1) |
                                       (csd[10] >> 7)) + 2u;
    const uint32_t rdblen = csd[5] & 0x0Fu;
    return ((c_size + 1u) << mult) * (1u << rdblen) / 512u;
}

/* ---- public ------------------------------------------------------- */

static spi_inst_t *spi_for(uint miso, uint cs, uint clk, uint mosi) {
    if ((miso & 3u) != 0u || (cs & 3u) != 1u ||
        (clk  & 3u) != 2u || (mosi & 3u) != 3u) return NULL;
    if ((miso / 8u) != (clk / 8u) || (clk / 8u) != (mosi / 8u)) return NULL;
    return ((clk / 8u) & 1u) ? spi1 : spi0;
}

bool sdblock_init(const frank_pins_t *pins) {
    memset(&s, 0, sizeof(s));

    if (!pins || pins->sd_cs == PIN_NC || pins->sd_clk == PIN_NC ||
        pins->sd_cmd == PIN_NC || pins->sd_dat0 == PIN_NC) return false;

    s.cs   = (uint)pins->sd_cs;
    s.clk  = (uint)pins->sd_clk;
    s.mosi = (uint)pins->sd_cmd;
    s.miso = (uint)pins->sd_dat0;
    s.spi  = spi_for(s.miso, s.cs, s.clk, s.mosi);
    if (!s.spi) return false;

    bb_pins();

    /* At least 74 clocks with CS high is how a card is told to use SPI
     * rather than its native interface. */
    gpio_put(s.cs, 1);
    for (int i = 0; i < 12; i++) bb_xfer(0xFF);

    gpio_put(s.cs, 0);
    if (bb_cmd(CMD0, 0, 0x95u) != 0x01u) { gpio_put(s.cs, 1); return false; }

    const uint8_t r8 = bb_cmd(CMD8, 0x1AAu, 0x87u);
    bool v2 = (r8 == 0x01u);
    if (v2) for (int i = 0; i < 4; i++) bb_xfer(0xFF);

    for (int i = 0; i < 20000; i++) {
        bb_cmd(CMD55, 0, 0x65u);
        if (bb_cmd(ACMD41, v2 ? 0x40000000u : 0u, 0x77u) == 0x00u) break;
        busy_wait_us_32(200);
    }

    if (v2 && bb_cmd(CMD58, 0, 0xFDu) == 0x00u) {
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = bb_xfer(0xFF);
        s.block_addressed = (ocr[0] & 0x40u) != 0u;
    }

    /* Hand the bus to the peripheral for everything after this. */
    gpio_put(s.cs, 1);
    spi_init(s.spi, SPI_HZ);
    spi_set_format(s.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(s.clk,  GPIO_FUNC_SPI);
    gpio_set_function(s.mosi, GPIO_FUNC_SPI);
    gpio_set_function(s.miso, GPIO_FUNC_SPI);

    gpio_put(s.cs, 0);
    uint8_t csd[16];
    const bool got = (hw_cmd(CMD9, 0, 0xFFu) == 0x00u) && hw_rx_block(csd, 16);
    gpio_put(s.cs, 1);
    if (!got) { sdblock_release(); return false; }

    s.sectors = sectors_from_csd(csd);
    s.up = true;
    return true;
}

void sdblock_release(void) {
    if (s.spi) spi_deinit(s.spi);
    if (s.clk) {
        gpio_init(s.clk);  gpio_set_dir(s.clk,  GPIO_IN);
        gpio_init(s.mosi); gpio_set_dir(s.mosi, GPIO_IN);
        gpio_init(s.miso); gpio_set_dir(s.miso, GPIO_IN);
        gpio_init(s.cs);   gpio_set_dir(s.cs,   GPIO_IN);
    }
    memset(&s, 0, sizeof(s));
}

uint32_t sdblock_sectors(void) { return s.up ? s.sectors : 0u; }

bool sdblock_read(uint32_t lba, uint8_t *buf, uint32_t count) {
    if (!s.up || !buf || !count) return false;

    const uint32_t arg = s.block_addressed ? lba : lba * 512u;
    bool ok = true;

    gpio_put(s.cs, 0);
    if (count == 1u) {
        ok = (hw_cmd(CMD17, arg, 0xFFu) == 0x00u) && hw_rx_block(buf, 512);
    } else if (hw_cmd(CMD18, arg, 0xFFu) == 0x00u) {
        for (uint32_t i = 0; i < count && ok; i++)
            ok = hw_rx_block(buf + i * 512u, 512);
        hw_cmd(CMD12, 0, 0xFFu);
        for (int i = 0; i < 8; i++) hw_xfer(0xFF);
    } else {
        ok = false;
    }
    gpio_put(s.cs, 1);
    hw_xfer(0xFF);
    return ok;
}

bool sdblock_write(uint32_t lba, const uint8_t *buf, uint32_t count) {
    if (!s.up || !buf || !count) return false;

    bool ok = true;
    gpio_put(s.cs, 0);

    /* One block at a time. A multi-block write needs the pre-erase
     * ACMD23 and a stop token to be worth anything, and a report is a
     * few kilobytes - the difference would not be visible. */
    for (uint32_t i = 0; i < count && ok; i++) {
        const uint32_t lb  = lba + i;
        const uint32_t arg = s.block_addressed ? lb : lb * 512u;
        ok = (hw_cmd(CMD24, arg, 0xFFu) == 0x00u) &&
             hw_tx_block(buf + i * 512u, 512, 0xFEu);
    }

    gpio_put(s.cs, 1);
    hw_xfer(0xFF);
    return ok;
}
