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
#include "video_mode.h"

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

    /* A bare PWM pin into a speaker. Mono, no DAC, no mux — see
     * CAP_AUDIO_PWM. Set only where the board has no I2S at all; the
     * boards that do have I2S reach PWM through the audio mux on the
     * I2S pins instead. */
    int8_t speaker;

    /* Where I2S comes out when the PCM5122 hat is fitted, which is not
     * where it comes out otherwise: the hat occupies its own pins and
     * the onboard pair above goes to whatever the board already had.
     * Same convention - sclk = base, lrck = base + 1. */
    int8_t pcm_i2s_data, pcm_i2s_clk_base;

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
     * esp_chip_pu is PIN_NC on every ESP-01S board: CH_PD goes to a
     * button, or on oldskoolfrank to nothing at all. Only frank_next can
     * reset its companion in firmware, and detection has to account for
     * that — see detect.c.
     *
     * esp_gpio0 and esp_gpio2 are a separate question and the answer is
     * not the same. This said they were PIN_NC on every ESP-01S board
     * too, which was true of the boards looked at when it was written
     * and is not true of oldskoolfrank: both its strapping pins come
     * back to GP24 and GP23 through 1K, each with a 10K pull-up of its
     * own. Reaching the straps is still not the same as being able to
     * reset the part — without CH_PD or RST there is no way to make it
     * sample them — so this remains a record of what is wired rather
     * than a capability. */
    int8_t esp_uart_tx, esp_uart_rx;
    int8_t esp_chip_pu, esp_gpio0, esp_gpio2;
    int8_t esp_spi_miso, esp_spi_cs, esp_spi_sck, esp_spi_mosi;
    int8_t esp_hs, esp_ready;
    int8_t esp_mux_sel;          /* 74HC4052 S0, frank_next only */

    /* Gamepads. NES/SNES shift registers share latch+clock; DB9 is the
     * Atari-style direct-wired variant on oldskoolfrank. */
    int8_t pad_latch, pad_clk, pad_d1, pad_d2;

    /* Tape input, via a CD4069 used as a squaring comparator. GP22 on
     * every board that has it except the Core 2U revision (GP45). On frank, megafrank
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

    /* The host CPU bus, as one contiguous run.
     *
     * On xt8086 the RP2350 is the whole chipset for a real 8086: GP0-15
     * are AD0-15, GP16-19 are A16-19, GP20-26 are ALE, RD, WR, INTA,
     * M/IO, BHE and INTR, GP27 is the D input of the READY flip-flop,
     * GP28 is RESET and GP29 is the CPU's clock. Thirty pins, no gaps.
     *
     * Named as a run rather than as thirty fields because nothing here
     * addresses those lines individually — what the firmware needs to
     * know is which pins belong to the CPU and must not be driven by
     * anything else. tests_board.c's short sweep reads exactly this;
     * without it, that test would drive a live CPU's address bus one
     * pair at a time. The per-line detail lives in docs/pinouts.txt,
     * where it came from. */
    int8_t  host_bus_base;
    uint8_t host_bus_len;

    /* The three lines inside that run which firmware may drive on
     * purpose: the CPU's clock, its reset, and the wait-state flip-flop.
     *
     * With no clock the 8086 executes nothing and its bus stays in the
     * high-impedance state it powers up in, which is why this firmware
     * leaves all three alone and the board tests as a peripheral
     * carrier. They are recorded so that a test which does want to start
     * the CPU has the pins from the netlist rather than from memory. */
    int8_t  host_clk, host_reset, host_ready;

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

    /* True only when the MCU arrives on a Pico-form-factor module, whose
     * header breaks out GP0-GP22 and GP26-GP28 and nothing else: GP23 is
     * the module's SMPS power-save control, GP24 its VBUS sense, GP25 its
     * LED, and GP29 upward never leave the module.
     *
     * Not the same question as "is the flash on the module", which is
     * what this was first inferred from and is wrong: a PGA2350 carries
     * its own flash too and exposes all 48 GPIOs. FRANK PGA, MegaFRANK
     * and OldSkoolFRANK all use pins above GP28, so of the socketed
     * boards only FRANK is one of these. */
    bool               pico_socket;

    /* True when the MCU arrives on a module that brings its own flash
     * and PSRAM, so finding memory says nothing about the carrier.
     *
     * Split out of pico_socket, which was answering two questions at
     * once and only one of them correctly. pico_socket means "the header
     * breaks out GP0-22 and GP26-28 and nothing else", which the short
     * scan reads and which is false for a PGA2350 carrier — a PGA2350
     * exposes all 48 GPIOs. But a Pimoroni PGA2350 carries 8 MB of PSRAM
     * on GP47 inside the module, exactly as a Pico Plus 2 does, so the
     * "PSRAM answered on a board that declares none" veto has to exempt
     * it for the same reason.
     *
     * Keying that veto on pico_socket alone is how a real OldSkoolFRANK
     * came to report itself as a FRANK: its module's PSRAM vetoed
     * oldskoolfrank, frank_pga and every other PGA descriptor, and FRANK
     * — the one board exempt — was the only thing left standing. It then
     * opened HDMI on a board jumpered for VGA.
     *
     * The module's memory is still tested: detection measures what is
     * actually on the bus and registry_detected_caps() turns that into a
     * capability, which is the same route by which a Plus 2's PSRAM gets
     * exercised in a FRANK socket. */
    bool               module_memory;

    frank_pins_t       pins;

    /* Which output to bring up when nobody has said otherwise.
     *
     * VIDEO_AUTO — the zero, so a descriptor that says nothing gets the
     * fleet rule — means "HDMI if this board has it, else whatever it
     * does have". That rule is right for every board whose connectors
     * are distinguishable, and wrong for oldskoolfrank, where HDMI and
     * VGA are the same eight pins and which one is live is decided by
     * three solder jumpers. The detector cannot see a solder jumper, so
     * on that board its HDMI verdict is not a measurement — it is the
     * fleet rule wearing a measurement's clothes, and a board that ships
     * jumpered for VGA came up on a socket with nothing in it.
     *
     * Set this only where the board itself knows better than the probe.
     * It outranks autodetect and is outranked by every deliberate
     * choice: the boot window, a mode the running firmware asked for
     * before restarting, and -DFRANK_VIDEO. */
    frank_video_mode_t video_default;

    /* This board's I2S DAC calls the two halves of the frame the other
     * way round, so the firmware has to swap them to be believed.
     *
     * Not a wiring fact — the netlist was traced end to end and
     * oldskoolfrank's is conventional: U7's IOL reaches the jack's tip
     * through U6B and IOR reaches the ring through U6A, and on a TRS
     * jack tip is left. The difference is the part. The fleet's TDA1387
     * is I2S, where word select low selects the left channel; the
     * TDA1545A takes the older simultaneous/EIAJ convention, where it
     * selects the right. Same bitstream, opposite speakers.
     *
     * Applied where the frame is composed rather than by rewriting
     * i2s_tone()'s packing, which is correct for I2S and was got wrong
     * once already — see the comment there for how the halves were
     * established. Swapping the labels at the source keeps that
     * reasoning true and confines the exception to the board that has
     * the part. */
    bool               i2s_lr_swapped;

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
 * that asks which board this is needs a screen, and bringing the screen
 * up needed the answer. Something has to be able to draw before the
 * board is known, and this is the least-assuming thing that can. */
const frank_board_desc_t *frank_board_fallback(void);

/* Descriptors that could be running on this MCU class. Used by the
 * detector to bound its search — and by the build to decide which
 * drivers must be linked in. */
unsigned frank_boards_for_mcu(frank_mcu_class_t mcu,
                              const frank_board_desc_t **out, unsigned max);

#endif /* BOARD_DESC_H */
