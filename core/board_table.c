/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * board_table.c — the descriptors.
 *
 * Source of truth: the KiCad netlists, exported with
 *
 *     kicad-cli sch export netlist --format kicadsexpr -o b.net b.kicad_sch
 *
 * and joined pin-to-net by tools/parse.py + tools/gpio2.py. docs/pinouts.txt
 * is that output; every number below can be found in it. Do not "tidy" a
 * pin here without regenerating that file — the whole point of the
 * generator is that the table cannot quietly drift from the schematic.
 *
 * Three findings from the extraction are worth stating up front, because
 * they contradict what the board diagrams suggest:
 *
 *  1. GP22 is the tape input on every board that has one, except core2u
 *     (GP45). On frank, megafrank and minifrank the CD4069 output does
 *     not reach the MCU directly — a config switch bridges it onto GP22
 *     (JP1 6<->7, S1 9<->3, S2 1<->4 respectively). The pin is therefore
 *     only the tape input when the operator has closed that switch, which
 *     is what CAP_TAPE_DIP_GATED records.
 *
 *  2. Every audio mux, USB mux and amplifier-shutdown line in the fleet
 *     is driven by a physical switch or jumper, not by a GPIO — the sole
 *     exception being frank_next's ESP UART mux on GP30. Firmware cannot
 *     select those paths. The tests can only verify whichever path the
 *     switch currently selects, and the report has to tell the operator
 *     which switch to move. See `manual_note`.
 *
 *  3. ESP-01S CH_PD and RST go to buttons on every board that carries
 *     one; no GPIO reaches them. The "pulse reset and watch for the ROM
 *     banner" probe only works on frank_next. Elsewhere the ESP has to be
 *     detected by talking to it (AT/OK) rather than by rebooting it.
 */

#include "board_desc.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Passive signatures                                                  */
/* ------------------------------------------------------------------ */

/* Only the pins that separate a board from the others in its MCU class.
 * Adding pins the whole class shares would dilute the Hamming margin
 * without adding information. */

/* --- RP2350A class ------------------------------------------------- */

/* GP8 is the PSRAM chip select on all three standalone A-class boards
 * (10K pull-up), and unused on frank and on the core2 slave. That single
 * pin splits the class in half before anything else is looked at. */
static const pinsig_entry_t sig_microfrank[] = {
    { 0, PINSIG_FLOAT }, { 2, PINSIG_FLOAT }, { 3, PINSIG_FLOAT },
    { 8, PINSIG_HIGH  }, { 20, PINSIG_FLOAT }, { 26, PINSIG_FLOAT },
};

static const pinsig_entry_t sig_minifrank[] = {
    { 2, PINSIG_HIGH }, { 3, PINSIG_HIGH },   /* TXS0104 A-side, 10K   */
    { 8, PINSIG_HIGH },                        /* PSRAM CE               */
    { 20, PINSIG_HIGH },                       /* ESP-01S URXD idles high*/
};

static const pinsig_entry_t sig_zerofrank[] = {
    { 2, PINSIG_FLOAT }, { 3, PINSIG_FLOAT },
    { 8, PINSIG_HIGH },
    { 20, PINSIG_HIGH },                       /* PIO-USB D+, 1.5K to 3V3*/
};

/* frank: PS/2 pull-ups present, but no PSRAM. */
static const pinsig_entry_t sig_frank_a[] = {
    { 2, PINSIG_HIGH }, { 3, PINSIG_HIGH },
    { 8, PINSIG_FLOAT },
    { 22, PINSIG_DONTCARE },                   /* depends on the DIP     */
};

/* The core2/core2u slave: PSRAM on GP0 rather than GP8, and the link
 * buses on GP1..GP23 carry no pull resistors at all. Tier 1's PSRAM
 * probe on CS 0 is what actually confirms this — the signature alone
 * cannot separate it from `frank`, since both show GP8 floating. */
static const pinsig_entry_t sig_core2_slave[] = {
    { 0, PINSIG_HIGH  },                       /* PSRAM CE, 10K          */
    { 4, PINSIG_FLOAT }, { 5, PINSIG_FLOAT },
    { 6, PINSIG_FLOAT }, { 7, PINSIG_FLOAT },
    { 8, PINSIG_FLOAT },
};

/* --- RP2350B class ------------------------------------------------- */

static const pinsig_entry_t sig_z0pa[] = {
    /* Measured on the board rather than inferred from what each pin is
     * for. The first attempt was written from murmnes' pin assignments,
     * which say what a pin does and nothing about how it idles - it
     * scored below core2, the board came up unidentified, and video
     * opened on HSTX pins that go nowhere here.
     *
     * What actually distinguishes it: everything from GP28 up idles
     * high, and everything below GP28 floats. A core2 master matches
     * five of its own six entries on this board and disagrees only at
     * GP28, which is why the two tied. */
    { 47, PINSIG_HIGH },                       /* PSRAM CE               */
    { 43, PINSIG_HIGH },                       /* SD CS                  */
    { 28, PINSIG_HIGH },                       /* the one core2 calls    */
                                               /* float - the tiebreak   */
    { 30, PINSIG_HIGH }, { 31, PINSIG_HIGH },  /* SD on SPI1             */
    { 32, PINSIG_HIGH }, { 35, PINSIG_HIGH },  /* the HDMI block, which  */
    { 39, PINSIG_HIGH },                       /* is where core2 floats  */
    { 12, PINSIG_FLOAT },                      /* and where core2's own  */
                                               /* video sits             */
    { 46, PINSIG_FLOAT },
};

static const pinsig_entry_t sig_frank_pga[] = {
    { 0, PINSIG_HIGH }, { 1, PINSIG_HIGH },
    { 2, PINSIG_HIGH }, { 3, PINSIG_HIGH },    /* TXS0104 A-side, 10K    */
    { 28, PINSIG_FLOAT }, { 29, PINSIG_FLOAT },/* no I2C                 */
    { 30, PINSIG_FLOAT },                      /* no 1-Wire              */
    { 47, PINSIG_FLOAT },                      /* no PSRAM               */
};

static const pinsig_entry_t sig_megafrank[] = {
    { 0, PINSIG_HIGH }, { 2, PINSIG_HIGH },
    { 28, PINSIG_HIGH }, { 29, PINSIG_HIGH },  /* I2C, 4.7K              */
    { 30, PINSIG_HIGH },                       /* DS2401, 4.7K           */
    { 47, PINSIG_FLOAT },
};

/* oldskoolfrank drives PS/2 through bare 1K series resistors with no
 * pull-up to 3V3, so GP0..3 float where frank_pga's are held high. */
static const pinsig_entry_t sig_oldskoolfrank[] = {
    { 0, PINSIG_FLOAT }, { 1, PINSIG_FLOAT },
    { 2, PINSIG_FLOAT }, { 3, PINSIG_FLOAT },
    { 28, PINSIG_FLOAT }, { 30, PINSIG_FLOAT },
    { 47, PINSIG_FLOAT },
};

static const pinsig_entry_t sig_core2_master[] = {
    { 47, PINSIG_HIGH },                       /* PSRAM CE, 10K          */
    { 43, PINSIG_HIGH },                       /* R3 10K — the famously  */
                                               /* unconnected slave RUN  */
    { 2, PINSIG_FLOAT }, { 3, PINSIG_FLOAT },
    { 23, PINSIG_FLOAT }, { 28, PINSIG_FLOAT },
};

static const pinsig_entry_t sig_next[] = {
    { 47, PINSIG_HIGH },                       /* PSRAM CE               */
    { 3, PINSIG_HIGH }, { 4, PINSIG_HIGH },
    { 5, PINSIG_HIGH }, { 6, PINSIG_HIGH },
    { 7, PINSIG_HIGH },                        /* SD 4-bit, 10K each      */
    { 23, PINSIG_HIGH },                       /* DS2401, 4.7K            */
    { 28, PINSIG_HIGH }, { 29, PINSIG_HIGH },  /* I2C, 4.7K               */
    { 38, PINSIG_HIGH }, { 39, PINSIG_HIGH },  /* ESP32 CHIP_PU / GPIO0   */
};

/* ------------------------------------------------------------------ */
/* Descriptors                                                         */
/* ------------------------------------------------------------------ */

#define NC PIN_NC
#define SIG(x) .sig = (x), .sig_len = (uint8_t)(sizeof(x) / sizeof((x)[0]))

/* Shorthands for the conventions shared across the fleet, so a board
 * that follows them says so instead of repeating six numbers. */
#define PINS_SD_SPI    .sd_dat0 = 4, .sd_cs = 5, .sd_clk = 6, .sd_cmd = 7, \
                       .sd_dat1 = NC, .sd_dat2 = NC
#define PINS_I2S_TDA   .i2s_data = 9, .i2s_clk_base = 10, .i2s_mclk = NC
#define PINS_PS2_TXS   .ps2_ms_clk = 0, .ps2_ms_dat = 1, \
                       .ps2_kb_clk = 2, .ps2_kb_dat = 3
/* Clock is GP20 and latch is GP21, not the other way round.
 *
 * They were swapped here, and the symptom was a gamepad dialog where no
 * button ever registered: the 4021 got a latch pulse where it expected
 * clocks, so the data line never presented changing bits.
 *
 * The netlist alone cannot settle this — it only shows GP20 on J16/J17
 * pin 4 and GP21 on pin 3, with each port's data returning on pin 2
 * through R119/R120 to GP26/GP27. Which of the two shared lines carries
 * the clock is a Murmulator GP2 convention, and murmnes' board_m2.h is
 * working firmware against it: NESPAD_CLK_PIN 20, NESPAD_LATCH_PIN 21. */
#define PINS_PAD_NES   .pad_clk = 20, .pad_latch = 21, .pad_d1 = 26, .pad_d2 = 27
#define PINS_UART01    .uart_tx = 0, .uart_rx = 1

/* Fields not mentioned are zero-initialised, which for a pin means
 * GPIO0 — so every descriptor below sets the ones it does not use to NC
 * through this default block. C99 designated initialisers apply the
 * later value, so listing the defaults first is safe and keeps each
 * board's entry to only what is true about it. */
#define PINS_NONE \
    .uart_tx = NC, .uart_rx = NC, \
    .ps2_kb_clk = NC, .ps2_kb_dat = NC, .ps2_ms_clk = NC, .ps2_ms_dat = NC, \
    .sd_dat0 = NC, .sd_cs = NC, .sd_clk = NC, .sd_cmd = NC, \
    .sd_dat1 = NC, .sd_dat2 = NC, \
    .i2s_data = NC, .i2s_clk_base = NC, .i2s_mclk = NC, \
    .video_base = NC, \
    .psram_cs = NC, .psram_soft_sclk = NC, \
    .psram_soft_mosi = NC, .psram_soft_miso = NC, \
    .led_ws2812 = NC, .led_plain = NC, \
    .esp_uart_tx = NC, .esp_uart_rx = NC, .esp_chip_pu = NC, .esp_gpio0 = NC, \
    .esp_spi_miso = NC, .esp_spi_cs = NC, .esp_spi_sck = NC, \
    .esp_spi_mosi = NC, .esp_hs = NC, .esp_ready = NC, .esp_mux_sel = NC, \
    .pad_latch = NC, .pad_clk = NC, .pad_d1 = NC, .pad_d2 = NC, \
    .tape_in = NC, \
    .i2c_sda = NC, .i2c_scl = NC, \
    .onewire = NC, \
    .pio_usb_dp = NC, \
    .ay_rclk = NC, .ay_srclk = NC, .ay_ser = NC, \
    .dip = NC, \
    .link_a_data = NC, .link_b_data = NC, \
    .link_fs = NC, .link_db_out = NC, .link_db_in = NC

/* PINS_NONE sets every pin to NC and each board then overrides the ones
 * it uses. That is the point of the idiom, so silence the warning that
 * exists to catch it happening by accident. */
#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Winitializer-overrides"
#else
#pragma GCC diagnostic ignored "-Woverride-init"
#endif

const frank_board_desc_t frank_board_table[] = {

/* ---------------------------------------------------------------- */
{
    .id = FRANK_BOARD_FRANK, .name = "FRANK", .slug = "frank",
    /* Either package, because the socket decides.
     *
     * This said RP2350A on the reasoning that only a Pico 2 is ever
     * fitted. A Pimoroni Pico Plus 2 is an RP2350B in the same
     * footprint, and one is fitted, so the assumption was simply wrong.
     * The board is whatever module is in the socket, which is exactly
     * what FRANK_MCU_ANY is for. */
    .mcu = FRANK_MCU_ANY,
    .role = FRANK_ROLE_SINGLE,
    .pico_socket = true,
    .caps = CAP_VIDEO_HDMI | CAP_VIDEO_VGA | CAP_VIDEO_COMPOSITE
          | CAP_AUDIO_I2S | CAP_AUDIO_AMP | CAP_AUDIO_MUX
          | CAP_SD | CAP_PS2 | CAP_GAMEPAD_NES
          | CAP_TAPE_IN | CAP_TAPE_DIP_GATED | CAP_DIPSWITCH
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB
          | CAP_ESP01 | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_UART01, PINS_PS2_TXS, PINS_SD_SPI,
              PINS_I2S_TDA, PINS_PAD_NES,
              .video_base = 12, .led_plain = 25,
              .esp_uart_tx = 20, .esp_uart_rx = 21,
              .tape_in = 22, .dip = 22 },
    SIG(sig_frank_a),
    .flash_bytes = 0,                   /* lives on the module */
    .psram_bytes = 0,
    .manual_note = "JP1: pin6-7 closes tape onto GP22; pin1 selects the "
                   "audio mux; pin4 un-shuts the PAM8403.",
},

/* ---------------------------------------------------------------- */
{
    .id = FRANK_BOARD_FRANK_PGA, .name = "FRANK PGA", .slug = "pga",
    .mcu = FRANK_MCU_RP2350B, .role = FRANK_ROLE_SINGLE,
    .caps = CAP_VIDEO_HDMI | CAP_VIDEO_VGA | CAP_VIDEO_COMPOSITE
          | CAP_AUDIO_I2S | CAP_AUDIO_AMP | CAP_AUDIO_MUX
          | CAP_SD | CAP_PS2 | CAP_GAMEPAD_NES | CAP_TAPE_IN
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_ESP01 | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_UART01, PINS_PS2_TXS, PINS_SD_SPI,
              PINS_I2S_TDA, PINS_PAD_NES,
              .video_base = 12, .led_plain = 25,
              .esp_uart_tx = 38, .esp_uart_rx = 39,
              .tape_in = 22 },
    SIG(sig_frank_pga),
    .flash_bytes = 0, .psram_bytes = 0,
    .manual_note = "S1 selects the USB mux; JP1 selects the audio mux and "
                   "the PAM8403 shutdown. Neither is reachable from firmware.",
},

/* ---------------------------------------------------------------- */
{
    .id = FRANK_BOARD_MEGAFRANK, .name = "MegaFRANK", .slug = "mega",
    .mcu = FRANK_MCU_RP2350B, .role = FRANK_ROLE_SINGLE,
    .caps = CAP_VIDEO_HDMI | CAP_VIDEO_VGA | CAP_VIDEO_COMPOSITE
          | CAP_AUDIO_I2S | CAP_AUDIO_AMP | CAP_AUDIO_MUX | CAP_TURBOSOUND
          | CAP_SD | CAP_PS2 | CAP_GAMEPAD_NES
          | CAP_TAPE_IN | CAP_TAPE_DIP_GATED | CAP_DIPSWITCH
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_ESP01 | CAP_LED_PLAIN
          | CAP_PSRAM_SOFTSPI | CAP_I2C | CAP_RTC_DS3231 | CAP_ONEWIRE_DS2401,
    .pins = { PINS_NONE, PINS_UART01, PINS_PS2_TXS, PINS_SD_SPI,
              PINS_I2S_TDA, PINS_PAD_NES,
              .video_base = 12, .led_plain = 25,
              .esp_uart_tx = 38, .esp_uart_rx = 39,
              .tape_in = 22, .dip = 22,
              .psram_soft_sclk = 32, .psram_soft_mosi = 33,
              .psram_soft_miso = 34,   /* via S10 */
              .psram_cs = 31,
              .i2c_sda = 28, .i2c_scl = 29, .onewire = 30,
              /* The AY latch shares the I2S pins; which one reaches the
               * amplifier is decided by the audio mux, i.e. by S1. */
              .ay_rclk = 9, .ay_srclk = 10, .ay_ser = 11 },
    SIG(sig_megafrank),
    .flash_bytes = 0, .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = "S1-1 and S1-2 are the two SELECT BITS of the audio mux, "
                   "not two enables: 10=TDA/I2S, 01=TurboSound, 00=PWM, "
                   "11=GROUND (silence). Turning both on is silence. "
                   "S1-6 un-shuts the PAM8403; S1 9-3 closes tape onto GP22. "
                   "S10 connects the SPI PSRAM's SO to GP34 - with it off "
                   "the part cannot answer. S9 selects the USB mux.",
},

/* ---------------------------------------------------------------- */
{
    .id = FRANK_BOARD_MICROFRANK, .name = "microFRANK", .slug = "micro",
    .mcu = FRANK_MCU_RP2350A, .role = FRANK_ROLE_SINGLE,
    .caps = CAP_VIDEO_HDMI | CAP_AUDIO_I2S | CAP_SD | CAP_PSRAM_QMI
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_UART01, PINS_SD_SPI, PINS_I2S_TDA,
              .video_base = 12, .led_plain = 25, .psram_cs = 8 },
    SIG(sig_microfrank),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = "S1 selects the USB mux (device vs host).",
},

/* ---------------------------------------------------------------- */
{
    .id = FRANK_BOARD_MINIFRANK, .name = "miniFRANK", .slug = "mini",
    .mcu = FRANK_MCU_RP2350A, .role = FRANK_ROLE_SINGLE,
    .caps = CAP_VIDEO_HDMI | CAP_VIDEO_VGA
          | CAP_AUDIO_I2S | CAP_AUDIO_MUX
          | CAP_SD | CAP_PSRAM_QMI | CAP_PS2 | CAP_GAMEPAD_NES
          | CAP_TAPE_IN | CAP_TAPE_DIP_GATED | CAP_DIPSWITCH
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_ESP01 | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_UART01, PINS_PS2_TXS, PINS_SD_SPI,
              PINS_I2S_TDA, PINS_PAD_NES,
              .video_base = 12, .led_plain = 25, .psram_cs = 8,
              .esp_uart_tx = 20, .esp_uart_rx = 21,
              .tape_in = 22, .dip = 22 },
    SIG(sig_minifrank),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = "S2: 1-4 closes tape onto GP22; 2 selects the audio mux. "
                   "S1 selects the USB mux. J13 carries I2S off-board to a "
                   "TurboSound daughtercard.",
},

/* ---------------------------------------------------------------- */
{
    .id = FRANK_BOARD_ZEROFRANK, .name = "zeroFRANK", .slug = "zero",
    .mcu = FRANK_MCU_RP2350A, .role = FRANK_ROLE_SINGLE,
    .caps = CAP_VIDEO_HDMI | CAP_AUDIO_I2S | CAP_SD | CAP_PSRAM_QMI
          | CAP_USB_DEVICE | CAP_PIO_USB | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_UART01, PINS_SD_SPI, PINS_I2S_TDA,
              .video_base = 12, .led_plain = 25, .psram_cs = 8,
              .pio_usb_dp = 20 },
    SIG(sig_zerofrank),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = NULL,
},

/* ---------------------------------------------------------------- */
{
    .id = FRANK_BOARD_OLDSKOOLFRANK, .name = "OldSkoolFRANK",
    .slug = "oldskool",
    .mcu = FRANK_MCU_RP2350B, .role = FRANK_ROLE_SINGLE,
    /* HDMI and VGA are selected by solder jumpers JP3/JP4/JP5, so only
     * one of them is fitted on any given board. Both are declared; the
     * detector reports what is electrically present and says that a sink
     * on the unfitted connector is invisible to it. */
    .caps = CAP_VIDEO_HDMI | CAP_VIDEO_VGA
          | CAP_AUDIO_I2S | CAP_AUDIO_AMP
          | CAP_SD | CAP_PS2 | CAP_GAMEPAD_DB9 | CAP_TAPE_IN
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_ESP01 | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_UART01, PINS_PS2_TXS, PINS_SD_SPI,
              PINS_I2S_TDA, PINS_PAD_NES,
              .video_base = 12, .led_plain = 25,
              .esp_uart_tx = 38, .esp_uart_rx = 39,
              .tape_in = 22 },
    SIG(sig_oldskoolfrank),
    .flash_bytes = 0, .psram_bytes = 0,
    .manual_note = "Video output is chosen by solder jumpers JP3/JP4/JP5, "
                   "not by firmware. The DAC is a TDA1545A, not a TDA1387. "
                   "S2 selects the USB mux.",
},



/* ---------------------------------------------------------------- */
/* core2 and core2u share a pin map exactly; core2u adds tape on GP45.
 * That single pin is the only electrical difference visible from the
 * master, and it is a bare pad versus a CMOS input — roughly 5 pF. The
 * detector reports the pair as ambiguous rather than guessing. */
{
    .id = FRANK_BOARD_CORE2, .name = "FRANK Core 2",
    .slug = "core2",
    .mcu = FRANK_MCU_RP2350B, .role = FRANK_ROLE_MASTER,
    .caps = CAP_VIDEO_HDMI | CAP_AUDIO_I2S | CAP_SD | CAP_PSRAM_QMI
          | CAP_LINK | CAP_USB_DEVICE | CAP_USB_HOST | CAP_LED_WS2812,
    .pins = { PINS_NONE, PINS_UART01, PINS_SD_SPI, PINS_I2S_TDA,
              .video_base = 12, .led_ws2812 = 46, .psram_cs = 47,
              .link_a_data = 20, .link_b_data = 30,
              .link_fs = 40, .link_db_out = 41, .link_db_in = 42 },
    SIG(sig_core2_master),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = "GP43 is drawn to the slave RUN pin but the net does not "
                   "exist on this revision; the master cannot reset the slave "
                   "in hardware. FS (GP40) is used as a reboot request instead.",
},

{
    .id = FRANK_BOARD_CORE2, .name = "FRANK Core 2 (slave)",
    .slug = "core2-slave",
    .mcu = FRANK_MCU_RP2350A, .role = FRANK_ROLE_SLAVE,
    .caps = CAP_PSRAM_QMI | CAP_LINK | CAP_USB_DEVICE | CAP_LED_PLAIN,
    .pins = { PINS_NONE,
              .uart_tx = 24, .uart_rx = 25,
              .led_plain = 26, .psram_cs = 0,
              .link_a_data = 1, .link_b_data = 11,
              .link_fs = 21, .link_db_in = 22, .link_db_out = 23 },
    SIG(sig_core2_slave),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = NULL,
},

/* ---------------------------------------------------------------- */
{
    .id = FRANK_BOARD_CORE2U, .name = "FRANK Core 2U",
    .slug = "core2u",
    .mcu = FRANK_MCU_RP2350B, .role = FRANK_ROLE_MASTER,
    .caps = CAP_VIDEO_HDMI | CAP_AUDIO_I2S | CAP_SD | CAP_PSRAM_QMI
          | CAP_LINK | CAP_TAPE_IN
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_LED_WS2812,
    .pins = { PINS_NONE, PINS_UART01, PINS_SD_SPI, PINS_I2S_TDA,
              .video_base = 12, .led_ws2812 = 46, .psram_cs = 47,
              .tape_in = 45,
              .link_a_data = 20, .link_b_data = 30,
              .link_fs = 40, .link_db_out = 41, .link_db_in = 42 },
    SIG(sig_core2_master),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = "S5 selects the USB mux. GP43/slave-RUN is unconnected on "
                   "this revision, as on core2.",
},

{
    .id = FRANK_BOARD_CORE2U, .name = "FRANK Core 2U (slave)",
    .slug = "core2u-slave",
    .mcu = FRANK_MCU_RP2350A, .role = FRANK_ROLE_SLAVE,
    .caps = CAP_PSRAM_QMI | CAP_LINK | CAP_USB_DEVICE | CAP_LED_PLAIN,
    .pins = { PINS_NONE,
              .uart_tx = 24, .uart_rx = 25,
              .led_plain = 26, .psram_cs = 0,
              .link_a_data = 1, .link_b_data = 11,
              .link_fs = 21, .link_db_in = 22, .link_db_out = 23 },
    SIG(sig_core2_slave),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = NULL,
},


/* ---------------------------------------------------------------- */
{
    .id = FRANK_BOARD_NEXT, .name = "FRANK Next", .slug = "next",
    .mcu = FRANK_MCU_RP2350B, .role = FRANK_ROLE_SINGLE,
    .caps = CAP_VIDEO_HDMI
          | CAP_AUDIO_I2S | CAP_AUDIO_CODEC_I2C
          | CAP_SD | CAP_SD_4BIT | CAP_PSRAM_QMI
          | CAP_I2C | CAP_RTC_DS3231 | CAP_ONEWIRE_DS2401
          | CAP_GAMEPAD_NES | CAP_TAPE_IN
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_ESP32_SPI | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_PAD_NES,
              /* The console UART moved to GP40/41 (J2) because GP0/1 are
               * multiplexed onto the ESP32 by U18. */
              .uart_tx = 40, .uart_rx = 41,
              /* SDIO 4-bit: the one board that does not follow the
               * GP4-7 SPI convention. */
              .sd_clk = 2, .sd_cmd = 3, .sd_dat0 = 4,
              .sd_dat1 = 5, .sd_dat2 = 6, .sd_cs = 7,
              /* TLV320DAC3100: BCLK/WCLK/DIN, not the TDA's
               * DATA/SCLK/LRCK order. Hence the explicit fields. */
              .i2s_clk_base = 9, .i2s_data = 11, .i2s_mclk = 24,
              .video_base = 12, .led_plain = 25, .psram_cs = 47,
              .i2c_sda = 28, .i2c_scl = 29, .onewire = 23,
              .tape_in = 22,
              .esp_uart_tx = 1, .esp_uart_rx = 0, .esp_mux_sel = 30,
              .esp_spi_miso = 32, .esp_spi_cs = 33, .esp_spi_sck = 34,
              .esp_spi_mosi = 35, .esp_hs = 36, .esp_ready = 37,
              .esp_chip_pu = 38, .esp_gpio0 = 39 },
    SIG(sig_next),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = "The only board whose companion processor can be reset "
                   "from firmware (GP38 CHIP_PU, GP39 GPIO0). S5/JP1 select "
                   "the USB mux.",
},

/* ---------------------------------------------------------------- */
{
    /* Waveshare RP2350-PiZero, "Z0pa" here. Not a FRANK board: a Pi
     * Zero form factor carrier that the fleet's firmware targets as
     * another platform, so the rig should be able to check one.
     *
     * Video is the thing to know about. Its HDMI connector is on
     * GP32-39, and HSTX on the RP2350 is fixed to GP12-19, so the HSTX
     * backends cannot drive it at all. It is served by the PIO HDMI
     * driver instead, vendored from murmnes, which already drives this
     * board - see ui/ui_video_pio_hdmi.c. The two HDMI backends tell
     * each other apart by video_base and refuse what they cannot serve.
     *
     * That path scans a 320x240 indexed framebuffer, so this board gets
     * the text page rather than the desktop, exactly as composite does.
     *
     * Pins from murmnes' board_z0.h, which is where this board is
     * already supported. */
    .id = FRANK_BOARD_Z0PA, .name = "Z0pa", .slug = "z0pa",
    .mcu = FRANK_MCU_RP2350B, .role = FRANK_ROLE_SINGLE,
    .caps = CAP_VIDEO_HDMI
          | CAP_PSRAM_QMI | CAP_SD | CAP_PS2 | CAP_GAMEPAD_NES
          | CAP_AUDIO_I2S | CAP_I2C
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_UART01,
              /* SD is on SPI1: pins above GP29 have no SPI0 mapping. */
              .sd_clk = 30, .sd_cmd = 31, .sd_dat0 = 40, .sd_cs = 43,
              .ps2_kb_clk = 14, .ps2_kb_dat = 15,
              .pad_clk = 4, .pad_latch = 5, .pad_d1 = 7, .pad_d2 = 8,
              /* The onboard I2S pair, not the PCM5122 hat's. */
              .i2s_data = 10, .i2s_clk_base = 11, .i2s_mclk = NC,
              /* I2C1 on GP2/GP3. It configures the PCM5122 on the audio
               * hat, which is why this board must NOT declare
               * CAP_AUDIO_CODEC_I2C: detection vetoes any board whose
               * codec claim disagrees with what answered on the bus, so
               * claiming a part that lives on an optional accessory
               * ruled this board out of its own identification on every
               * unit without the hat fitted - which is most of them. */
              .i2c_sda = 2, .i2c_scl = 3,
              /* The HDMI connector, which HSTX cannot reach - the PIO
               * backend takes this board on the strength of this pin
               * being above 31. */
              .video_base = 32,
              .psram_cs = 47, .led_plain = 25 },
    SIG(sig_z0pa),
    /* Zero, like the PSRAM below, because this board ships in more than
     * one configuration - 4 MB and 16 MB parts are both about - and the
     * Flash ID row reports a mismatch against whatever a descriptor
     * declares. Detection measures the part; a fixed figure here would
     * flag half the boards of this type as wrong. */
    .flash_bytes = 0,
    /* Left at zero rather than guessed. Detection measures what is
     * actually on CS and fills this in; the PiZero ships in variants
     * with and without PSRAM, and a descriptor claiming eight megabytes
     * would turn an absent part into a failure. */
    .psram_bytes = 0,
    .manual_note = "Waveshare RP2350-PiZero. HDMI is driven from the PIO "
                   "on GP32-39, not HSTX, and shows the text page rather "
                   "than the desktop - 320x240 is what that driver "
                   "scans. The PCM5122 audio hat, if fitted, sits on I2S "
                   "GP18/19 with I2C on GP2/GP3.",
},

};

#pragma GCC diagnostic pop

const unsigned frank_board_table_len =
    sizeof(frank_board_table) / sizeof(frank_board_table[0]);

/* ------------------------------------------------------------------ */
/* Fallback                                                            */
/* ------------------------------------------------------------------ */

/* Deliberately outside the table: this must never be a candidate the
 * detector can pick, only the thing the caller falls back to when it
 * picked nothing.
 *
 * Capabilities are the intersection of what the fleet reliably has, and
 * no more. No PSRAM (the CS pin differs), no PS/2 (three boards lack
 * it), no VGA or composite (five boards lack them, and claiming VGA
 * would make the detector probe a connector that may not exist). HDMI
 * is claimed because every board with any video at all has an HDMI
 * connector on GP12-19.
 *
 * VGA is claimed on exactly the same grounds, and used not to be, which
 * was an inconsistency rather than caution: across this fleet VGA is the
 * *same eight pins* through a resistor ladder — megafrank's DSUB-15 is
 * fed from GP12-17 via R125-R130 with the syncs on GP18/19 — so a board
 * unidentified enough to be offered HDMI is equally entitled to be
 * offered VGA. Refusing it meant holding V on an undecided board printed
 * "not wired on unknown" and silently came up in HDMI.
 *
 * Composite likewise, and for the third time the same argument: the
 * detector's own report says composite "drives the same pins and
 * presents the same 75R-to-ground load as VGA", which is precisely why
 * it cannot be detected — and precisely why refusing to offer it on an
 * unidentified board makes no sense either. */
static const frank_board_desc_t fallback_desc = {
    .id   = FRANK_BOARD_UNKNOWN,
    .name = "Unidentified board",
    .slug = "unknown",
    .mcu  = FRANK_MCU_ANY,
    .role = FRANK_ROLE_SINGLE,
    .caps = CAP_VIDEO_HDMI | CAP_VIDEO_VGA | CAP_VIDEO_COMPOSITE
          | CAP_SD | CAP_AUDIO_I2S | CAP_PS2 | CAP_USB_DEVICE,
    .pins = { PINS_NONE, PINS_UART01, PINS_PS2_TXS, PINS_SD_SPI,
              PINS_I2S_TDA, .video_base = 12 },
    .sig = NULL, .sig_len = 0,
    .flash_bytes = 0, .psram_bytes = 0,
    .manual_note = "SELECT A BOARD before testing:  Board > Set Board "
                   "(Alt+B).  Until then only the pin conventions common "
                   "to the whole fleet are assumed - video GP12-19, "
                   "PS/2 GP2-3, microSD GP4-7, I2S GP9-11 - enough to draw "
                   "this screen and not enough to test anything.",
};

const frank_board_desc_t *frank_board_fallback(void) { return &fallback_desc; }

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

const frank_board_desc_t *frank_board_desc(frank_board_id_t id,
                                           frank_role_t role) {
    for (unsigned i = 0; i < frank_board_table_len; i++) {
        if (frank_board_table[i].id == id &&
            frank_board_table[i].role == role)
            return &frank_board_table[i];
    }
    return NULL;
}

unsigned frank_boards_for_mcu(frank_mcu_class_t mcu,
                              const frank_board_desc_t **out, unsigned max) {
    unsigned n = 0;
    for (unsigned i = 0; i < frank_board_table_len && n < max; i++) {
        const frank_board_desc_t *d = &frank_board_table[i];
        if (d->mcu == mcu || d->mcu == FRANK_MCU_ANY)
            out[n++] = d;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

const char *frank_board_name(frank_board_id_t id) {
    for (unsigned i = 0; i < frank_board_table_len; i++)
        if (frank_board_table[i].id == id) return frank_board_table[i].name;
    return "unknown";
}

const char *frank_board_slug(frank_board_id_t id) {
    for (unsigned i = 0; i < frank_board_table_len; i++)
        if (frank_board_table[i].id == id) return frank_board_table[i].slug;
    return "unknown";
}

frank_board_id_t frank_board_from_slug(const char *slug) {
    if (!slug) return FRANK_BOARD_UNKNOWN;
    for (unsigned i = 0; i < frank_board_table_len; i++)
        if (strcmp(frank_board_table[i].slug, slug) == 0)
            return frank_board_table[i].id;
    return FRANK_BOARD_UNKNOWN;
}

const char *frank_mcu_class_name(frank_mcu_class_t c) {
    switch (c) {
        case FRANK_MCU_RP2350A: return "RP2350A";
        case FRANK_MCU_RP2350B: return "RP2350B";
        default:                return "any";
    }
}

const char *frank_role_name(frank_role_t r) {
    switch (r) {
        case FRANK_ROLE_MASTER: return "master";
        case FRANK_ROLE_SLAVE:  return "slave";
        default:                return "single";
    }
}
