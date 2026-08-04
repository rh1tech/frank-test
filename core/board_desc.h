/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * board_desc.h — one descriptor per board: pins, capabilities, and the
 * passive pin signature the detector expects to see.
 *
 * Every pin number in board_table.c was extracted from the KiCad netlist
 * (kicad-cli sch export netlist), not from documentation. tools/gpio2.py
 * regenerates the evidence; docs/pinouts.txt is its output.
 *
 * Absent pins are PIN_NC (-1) rather than 0, because GPIO0 is a real and
 * frequently used pin. Anything that indexes a pin must check.
 */
#ifndef BOARD_DESC_H
#define BOARD_DESC_H

#include "frank_caps.h"

#define PIN_NC  (-1)

/* ------------------------------------------------------------------ */
/* Pin map                                                             */
/* ------------------------------------------------------------------ */

/* The fleet shares four conventions, which is what makes one firmware
 * plausible at all and why these fields are so often identical:
 *
 *   GP4-7    microSD, always DAT0 / CD-DAT3 / CLK / CMD in that order
 *   GP9-11   I2S, always DATA / SCLK-BCLK / LRCK-WCLK
 *   GP12-19  video, always CLKN CLKP D0N D0P D1N D1P D2N D2P — and on
 *            the VGA boards the same eight pins carry the resistor
 *            ladder RGB and the syncs
 *   GP0/1    UART0 wherever a debug header exists
 *
 * This matches Murmulator 2.0, which is why the frank-msx drivers in
 * drivers/ drop in unmodified.
 */
typedef struct {
    /* Console UART */
    int8_t uart_tx, uart_rx;

    /* PS/2 through a TXS0104 level shifter (or bare series resistors on
     * oldskoolfrank). Order is fixed across the fleet: A1/A2 carry the
     * keyboard, A3/A4 the mouse. */
    int8_t ps2_kb_clk, ps2_kb_dat;
    int8_t ps2_ms_clk, ps2_ms_dat;

    /* microSD. dat1/dat2 are only wired on the 4-bit board. */
    int8_t sd_dat0, sd_cs, sd_clk, sd_cmd;
    int8_t sd_dat1, sd_dat2;

    /* I2S. sclk = i2s_clk_base, lrck = i2s_clk_base + 1. mclk is only
     * present where the codec needs a master clock. */
    int8_t i2s_data, i2s_clk_base, i2s_mclk;

    /* Video. The eight pins are always contiguous from here. */
    int8_t video_base;

    /* PSRAM. Either a QMI chip select, or — on megafrank alone — a
     * bit-banged SPI trio, which is a different probe entirely. */
    int8_t psram_cs;
    /* Bit-banged SPI PSRAM (megafrank only). Four wires, not three: the
     * part's SO comes back on its own pin, via the S10 switch. It was
     * modelled as half-duplex on one SIO line at first, which cannot
     * work and duly did not. */
    int8_t psram_soft_sclk, psram_soft_mosi, psram_soft_miso;

    /* Status LED. Exactly one of these is set. */
    int8_t led_ws2812, led_plain;

    /* Companion processors.
     *
     * esp_chip_pu / esp_gpio0 are PIN_NC on every ESP-01S board: those
     * lines go to buttons, not to GPIOs. Only frank_next can reset its
     * companion in firmware. Detection has to account for that — see
     * detect.c. */
    int8_t esp_uart_tx, esp_uart_rx;
    int8_t esp_chip_pu, esp_gpio0;
    int8_t esp_spi_miso, esp_spi_cs, esp_spi_sck, esp_spi_mosi;
    int8_t esp_hs, esp_ready;
    int8_t esp_mux_sel;          /* 74HC4052 S0, frank_next only */

    /* Gamepads. NES/SNES shift registers share latch+clock; DB9 is the
     * Atari-style direct-wired variant on oldskoolfrank. */
    int8_t pad_latch, pad_clk, pad_d1, pad_d2;

    /* Tape input, via a CD4069 used as a squaring comparator. GP22 on
     * every board that has it except core2u (GP45). On frank, megafrank
     * and minifrank the connection is gated by a config DIP — the pin is
     * only the tape input when the switch is closed. */
    int8_t tape_in;

    /* I2C — DS3231 RTC and, on frank_next, the TLV320 codec. */
    int8_t i2c_sda, i2c_scl;

    /* DS2401 silicon serial number. The only per-unit identity in the
     * fleet that does not depend on a probe guessing right. */
    int8_t onewire;

    /* PIO soft USB host. dm = dp + 1 on both boards that have it. */
    int8_t pio_usb_dp;

    /* TurboSound: two AYs behind a pair of 74HC595s. On megafrank these
     * are the same pins as I2S, arbitrated by the audio mux. */
    int8_t ay_rclk, ay_srclk, ay_ser;

    /* Config DIP / jumper block, where one is readable. */
    int8_t dip;

    /* Inter-processor link (core2 / core2u only). Both buses share the
     * layout the PIO programs rely on: clk == data_base + 8,
     * valid == data_base + 9. */
    int8_t link_a_data, link_b_data;
    int8_t link_fs, link_db_out, link_db_in;
} frank_pins_t;

/* ------------------------------------------------------------------ */
/* Expected passive signature                                          */
/* ------------------------------------------------------------------ */

/* What a pad looks like with nothing driving it. See pinsig.c for how
 * these are measured — notably why the internal pull-down is not used. */
typedef enum {
    PINSIG_DONTCARE = 0,   /* not part of this board's signature */
    PINSIG_FLOAT,          /* no external load                   */
    PINSIG_HIGH,           /* external pull-up wins              */
    PINSIG_LOW,            /* external pull-down or load to GND  */
} pinsig_t;

#define PINSIG_MAX_PINS 48

typedef struct {
    uint8_t  pin;
    pinsig_t expect;
} pinsig_entry_t;

/* ------------------------------------------------------------------ */
/* Descriptor                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    frank_board_id_t   id;
    const char        *name;
    const char        *slug;
    frank_mcu_class_t  mcu;
    frank_role_t       role;
    frank_caps_t       caps;
    frank_pins_t       pins;

    /* Tier 2 fingerprint: the pins whose passive state distinguishes
     * this board from the others in its MCU class, and nothing more.
     * Listing pins that every board in the class shares would only dilute
     * the Hamming margin. */
    const pinsig_entry_t *sig;
    uint8_t               sig_len;

    /* Expected flash size in bytes, 0 if the flash lives on a socketed
     * module and therefore is not a property of the board. */
    uint32_t flash_bytes;

    /* Expected PSRAM size, 0 if none fitted. */
    uint32_t psram_bytes;

    /* Free text for the report: what the operator has to do by hand
     * because it is a switch and not a GPIO. NULL if nothing. */
    const char *manual_note;
} frank_board_desc_t;

/* The table, and lookups over it. */
extern const frank_board_desc_t frank_board_table[];
extern const unsigned           frank_board_table_len;

const frank_board_desc_t *frank_board_desc(frank_board_id_t id, frank_role_t role);

/* The descriptor to use when detection cannot name the board.
 *
 * Not a guess at which board this is — a statement of what every board
 * in the fleet has in common (1.3): video on GP12-19, microSD on GP4-7,
 * I2S on GP9-11, console UART on GP0/1. Those conventions hold across
 * all thirteen descriptors, which is what makes it safe to drive them
 * before knowing which one is under us.
 *
 * It exists because of a circularity found on real hardware: the dialog
 * that asks "core2 or core2u?" needs a screen, and bringing the screen
 * up needed the answer. Something has to be able to draw before the
 * board is known, and this is the least-assuming thing that can. */
const frank_board_desc_t *frank_board_fallback(void);

/* Descriptors that could be running on this MCU class. Used by the
 * detector to bound its search — and by the build to decide which
 * drivers must be linked in. */
unsigned frank_boards_for_mcu(frank_mcu_class_t mcu,
                              const frank_board_desc_t **out, unsigned max);

#endif /* BOARD_DESC_H */
