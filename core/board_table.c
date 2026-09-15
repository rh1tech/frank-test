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
 *  1. GP22 is the tape input on every board that has one, except the
 *     Core 2U revision (GP45) and frank_next (GP44, and a BC850 doing
 *     the squaring rather than a CD4069).
 *     On frank, megafrank and minifrank the CD4069 output does
 *     not reach the MCU directly — a config switch bridges it onto GP22
 *     (JP1 6<->7, S1 9<->3, S2 1<->4 respectively). The pin is therefore
 *     only the tape input when the operator has closed that switch, which
 *     is what CAP_TAPE_DIP_GATED records.
 *
 *  2. Every audio mux, USB mux and amplifier-shutdown line in the fleet
 *     is driven by a physical switch or jumper, not by a GPIO. Firmware
 *     cannot select those paths. The tests can only verify whichever
 *     path the switch currently selects, and the report has to tell the
 *     operator which switch to move. See `manual_note`.
 *
 *     There used to be one exception, frank_next's ESP UART mux on GP30.
 *     The second design keeps the 74HC4052 and takes the GPIO away: S0
 *     is now JP2, a two-way DIP that also selects where the USB-C port
 *     goes. So the rule is now without exceptions.
 *
 *  3. ESP CH_PD and RST go to buttons on every board that carries a
 *     companion; no GPIO reaches them. So the ESP has to be detected by
 *     talking to it (AT/OK) rather than by rebooting it and watching for
 *     a ROM banner.
 *
 *     The first FRANK Next was the exception — an ESP32 whose CHIP_PU
 *     and GPIO0 were on GP38/GP39 — and the second is not: its ESP8266EX
 *     takes reset from S5 and from the CH343P's DTR/RTS pair through a
 *     UMH3N, which is the usual auto-download arrangement and reaches no
 *     GPIO of ours.
 *
 *     That still holds, but it was written as "no GPIO reaches any of the
 *     ESP's control pins", which is wider than the netlists support.
 *     oldskoolfrank brings GPIO2 and GPIO0 back to GP23 and GP24. It
 *     changes nothing about resetting the part - CH_PD there is not
 *     connected to anything at all and RST is on SW3, so the straps
 *     cannot be made to take effect - but the two pins are wired, they
 *     carry pull-ups, and those pull-ups are what tells this board apart
 *     from frank_pga. See sig_oldskoolfrank.
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
    /* Bare pads, and the only thing left that separates this board from
     * oldskoolfrank — which brings its ESP-01S strapping pins back here
     * through 1K. Everything above is now identical on the two: see
     * sig_oldskoolfrank for why GP0-3 stopped being a discriminator. */
    { 23, PINSIG_FLOAT }, { 24, PINSIG_FLOAT },
    { 28, PINSIG_FLOAT }, { 29, PINSIG_FLOAT },/* no I2C                 */
    { 30, PINSIG_FLOAT },                      /* no 1-Wire              */
    /* The PGA2350's own PSRAM chip select, pulled up inside the module.
     * This said FLOAT on the reasoning that the carrier fits no PSRAM,
     * which is true and is not what the pin reads: a Pimoroni PGA2350
     * brings 8 MB on GP47 exactly as a Pico Plus 2 does. Measured high
     * on hardware. */
    { 47, PINSIG_HIGH },
};

static const pinsig_entry_t sig_megafrank[] = {
    { 0, PINSIG_HIGH }, { 2, PINSIG_HIGH },
    { 28, PINSIG_HIGH }, { 29, PINSIG_HIGH },  /* I2C, 4.7K              */
    { 30, PINSIG_HIGH },                       /* DS2401, 4.7K           */
    { 47, PINSIG_HIGH },                       /* the module's PSRAM CS  */
};

/* This said "oldskoolfrank drives PS/2 through bare 1K series resistors
 * with no pull-up to 3V3, so GP0..3 float where frank_pga's are held
 * high", and expected all four to float. Every part of that is wrong,
 * and it was wrong when it was written rather than overtaken by a board
 * revision — the netlist has not changed.
 *
 * R10-R13 are 1K to +3V3, not series resistors into the connector. The
 * generator prints one hop, so `GPIO0 -> R10[1K].pin1` says nothing
 * about where R10's other end goes, and it was read as though it went to
 * the MDC-036. It goes to the rail. All four pins are held high, exactly
 * as frank_pga's are through its 10K.
 *
 * The cost was not four wrong entries, it was the identification: with
 * GP0-3 high and everything else in the old signature agreeing, a real
 * oldskoolfrank matched frank_pga's whole signature and only three of
 * its own seven. It reported itself as a FRANK PGA, confidently, and
 * would have gone on doing so.
 *
 * What actually separates the two is the ESP-01S strapping. Both boards
 * carry an ESP-01S on the same UART, but oldskoolfrank alone brings
 * GPIO2 and GPIO0 back to the MCU — GP23 and GP24, through 1K, each with
 * a 10K pull-up to 3V3 of its own (R1/R3 and R2/R4). Both read high, and
 * they do so with no module fitted, because those four resistors are on
 * the carrier rather than on the ESP. frank_pga leaves both pins
 * unconnected. */
static const pinsig_entry_t sig_oldskoolfrank[] = {
    { 0, PINSIG_HIGH }, { 1, PINSIG_HIGH },
    { 2, PINSIG_HIGH }, { 3, PINSIG_HIGH },    /* R10-R13, 1K to 3V3     */
    { 23, PINSIG_HIGH }, { 24, PINSIG_HIGH },  /* ESP GPIO2/GPIO0, 1K+10K*/
    { 28, PINSIG_FLOAT }, { 30, PINSIG_FLOAT },
    { 47, PINSIG_HIGH },                       /* the module's PSRAM CS  */
};

static const pinsig_entry_t sig_core2_master[] = {
    { 47, PINSIG_HIGH },                       /* PSRAM CE, 10K          */
    { 43, PINSIG_HIGH },                       /* R3 10K — the famously  */
                                               /* unconnected slave RUN  */
    { 2, PINSIG_FLOAT }, { 3, PINSIG_FLOAT },
    { 23, PINSIG_FLOAT }, { 28, PINSIG_FLOAT },
};

/* xt8086, and the constraint that shapes it: every pin below GP30 is
 * wired to a real 8086, so none of them may appear here.
 *
 * pinsig.c's second phase drives a pad low for ten microseconds to tell
 * a pull-up from a floating pin. Doing that to AD0-15 is contention with
 * a CPU that may be driving them, and doing it to GP28 or GP29 is
 * poking that CPU's reset and clock. The signature is therefore drawn
 * entirely from GP36 upward, which is enough on its own: eleven pins,
 * and no other RP2350B descriptor matches more than eight of them. */
static const pinsig_entry_t sig_xt8086[] = {
    { 36, PINSIG_FLOAT }, { 37, PINSIG_FLOAT },  /* VGA HS/VS, no pulls   */
    { 38, PINSIG_HIGH  }, { 39, PINSIG_HIGH  },  /* I2C to the DS3231, 4K7*/
    { 40, PINSIG_FLOAT }, { 41, PINSIG_FLOAT },  /* microSD on SPI1, and  */
    { 42, PINSIG_FLOAT }, { 43, PINSIG_FLOAT },  /* not a pull-up on it   */
    { 44, PINSIG_FLOAT }, { 45, PINSIG_FLOAT },  /* the UART header       */
    { 47, PINSIG_HIGH  },                        /* PSRAM CE, 10K         */
};

/* The FRANK Next master. It shares the whole of its link — GP20-42, two
 * eight-bit buses and their handshakes — with the Core 2 master, and
 * those pins carry no pull resistors on either board, so none of them
 * can tell the two apart. What does is everything below GP20: the Core 2
 * master's microSD is SPI on GP4-7 with no pulls and its I2C does not
 * exist, while this board's is SDIO with a 10K on every line but the
 * clock, and its I2C sits on GP2/GP3 behind 4K7.
 *
 * GP1 is in here as a float on purpose. It is the only SD line without a
 * pull-up — the card does not need one on CLK — and stating that is what
 * makes the five highs either side of it evidence of an SDIO card socket
 * rather than of a pull-up field. */
static const pinsig_entry_t sig_next[] = {
    { 0, PINSIG_HIGH  },                       /* SD CMD, 10K            */
    { 1, PINSIG_FLOAT },                       /* SD CLK, deliberately not*/
    { 4, PINSIG_HIGH  }, { 5, PINSIG_HIGH  },
    { 6, PINSIG_HIGH  }, { 7, PINSIG_HIGH  },  /* SD DAT0-2, CD/DAT3, 10K*/
    { 43, PINSIG_HIGH },                       /* slave RUN, 10K — and on*/
                                               /* this board it is wired */
    { 45, PINSIG_HIGH },                       /* ESP mux 1Y1, 10K       */
    { 47, PINSIG_HIGH },                       /* PSRAM CE, 10K          */
};

/* Three entries were here — GP2, GP3 and GP44 — and the reasons for
 * removing them are not the reasons first written here.
 *
 * GP44 was simply wrong. It expected high, from reading "R48 is 1K to
 * 3V3" and stopping there, which is the mistake sig_oldskoolfrank
 * records at length. There is a BC850 on that collector and with nothing
 * in the tape jack it rests conducting: measured low over SWD. It is
 * left out rather than flipped, because what it reads with a lead
 * plugged in has not been measured either.
 *
 * GP2 and GP3 expected high, which is right for a working board — 4K7 to
 * 3V3, and nothing else on the net. They are out because of what happens
 * when that is not true. On the first FRANK Next brought up here both
 * pins were shorted to ground; with them in the signature the board
 * scored 9 of 12 and was still identified, but a second fault would have
 * taken it below the line and turned a specific, diagnosable failure —
 * "the I2C bus is dead" — into "unidentified board", which is the least
 * useful thing this firmware can say.
 *
 * A signature should be drawn from pins whose idle state is a property
 * of the board rather than of its health. The SD pull-up field is that;
 * a bus with parts hanging off it is not.
 *
 * pinsig_score() also declines to probe a descriptor's I2C pins now, for
 * an unrelated reason that stands on its own: phase B drives a pad low
 * for ten microseconds, and doing that to SDA while SCL is high is an
 * I2C START. */

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
    .speaker = NC, \
    .pcm_i2s_data = NC, .pcm_i2s_clk_base = NC, \
    .video_base = NC, \
    .psram_cs = NC, .psram_soft_sclk = NC, \
    .psram_soft_mosi = NC, .psram_soft_miso = NC, \
    .led_ws2812 = NC, .led_plain = NC, \
    .esp_uart_tx = NC, .esp_uart_rx = NC, .esp_chip_pu = NC, \
    .esp_gpio0 = NC, .esp_gpio2 = NC, \
    .pad_latch = NC, .pad_clk = NC, .pad_d1 = NC, .pad_d2 = NC, \
    .tape_in = NC, \
    .i2c_sda = NC, .i2c_scl = NC, \
    .onewire = NC, \
    .pio_usb_dp = NC, \
    .ay_rclk = NC, .ay_srclk = NC, .ay_ser = NC, \
    .dip = NC, \
    .host_bus_base = NC, .host_bus_len = 0, \
    .host_clk = NC, .host_reset = NC, .host_ready = NC, \
    .link_a_data = NC, .link_b_data = NC, \
    .link_fs = NC, .link_db_out = NC, .link_db_in = NC, \
    .link_run = NC

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
    .pico_socket = true, .module_memory = true,
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
    /* A Pimoroni PGA2350, which brings its own flash and 8 MB of PSRAM
     * on GP47. Not pico_socket: a PGA2350 exposes all 48 GPIOs. */
    .module_memory = true,
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
    /* PGA2350: the QMI PSRAM found on GP47 is the module's. This board's
     * own PSRAM is the bit-banged SPI part on GP31-34. */
    .module_memory = true,
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
    /* PGA2350, so the 8 MB that answers on GP47 belongs to the module
     * and not to this carrier. Without saying so, that PSRAM vetoed this
     * descriptor out of its own identification and the board reported
     * itself as a FRANK. */
    .module_memory = true,
    /* HDMI and VGA are the same eight pins, and which socket they reach
     * is decided by three solder jumpers.
     *
     * GP12-19 each drive a 220R resistor. On the HDMI side the eight
     * legs are four differential pairs; JP3, JP4 and JP5 short each
     * colour pair's two legs together, which turns them into one node
     * feeding one pin of the DSUB-15 — blue on GP12/13, green on GP14/15,
     * red on GP16/17, with GP18 and GP19 going straight to HSYNC and
     * VSYNC. That is the fleet's ladder order, B0 B1 G0 G1 R0 R1 HS VS,
     * so the HSTX VGA backend drives it unmodified.
     *
     * The two resistors per colour are equal rather than weighted, so
     * the four codes give three levels and not four: 01 and 10 are the
     * same voltage. Worth knowing when the greys look coarser here than
     * on megafrank, which uses a real 2R/R ladder.
     *
     * Both outputs are declared, because either can be jumpered and an
     * operator can still ask for the other by holding H at boot. Which
     * one comes up unasked is video_default below. */
    .caps = CAP_VIDEO_HDMI | CAP_VIDEO_VGA
          | CAP_AUDIO_I2S | CAP_AUDIO_AMP
          | CAP_SD | CAP_PS2 | CAP_GAMEPAD_DB9 | CAP_TAPE_IN
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_ESP01 | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_UART01, PINS_PS2_TXS, PINS_SD_SPI,
              PINS_I2S_TDA, PINS_PAD_NES,
              .video_base = 12, .led_plain = 25,
              .esp_uart_tx = 38, .esp_uart_rx = 39,
              /* The one board whose ESP-01S strapping pins reach the
               * MCU. Recorded so the short sweep leaves them alone and
               * so the signature's claim on them has a stated source;
               * nothing drives them, because with CH_PD unconnected and
               * RST on SW3 there is no way to make the part sample
               * them. */
              .esp_gpio0 = 24, .esp_gpio2 = 23,
              .tape_in = 22 },
    SIG(sig_oldskoolfrank),
    /* VGA unless somebody says otherwise. The detector cannot see a
     * solder jumper, so on this board alone the descriptor knows better
     * than the probe - see board_desc.h and video_select.h. */
    .video_default = VIDEO_VGA,
    /* The TDA1545A calls word-select high the left channel where I2S
     * calls it the right, so left and right came out of the sides they
     * are not named after. Heard on the bench; the board's own wiring
     * from the DAC to the jack was traced and is conventional. */
    .i2s_lr_swapped = true,
    .flash_bytes = 0, .psram_bytes = 0,
    .manual_note = "Video is chosen by solder jumpers JP3/JP4/JP5, not by "
                   "firmware: closed is VGA on J11, open is HDMI on J10. "
                   "VGA is what comes up unasked; hold H at boot for the "
                   "other. The DAC is a TDA1545A, not a TDA1387, into an "
                   "LM358 rather than a PAM8403. S2 selects the USB mux.",
},



/* ---------------------------------------------------------------- */
/* One descriptor for Core 2 and Core 2U.
 *
 * They share a pin map exactly. The U revision adds a tape input on
 * GP45 and a USB hub behind a mux, and none of that is visible from the
 * master: GP45 is a bare pad on one and a CMOS input on the other,
 * about 5 pF apart, which no probe here can measure. So the detector
 * used to tie, report AMBIGUOUS, and decline to run anything until an
 * operator chose from a menu — on every board of the type, to separate
 * two boards whose tests are otherwise identical.
 *
 * The union is declared rather than the intersection, and that is a
 * deliberate trade. On a Core 2U everything is covered. On the older
 * non-U revision the tape and USB-hub rows are asking after hardware
 * that is not fitted, so they will not pass — which is the wrong answer
 * in the smaller direction, and much cheaper than making every operator
 * of both revisions answer a question before the rig will do anything.
 * The note below says so on screen. */
{
    .id = FRANK_BOARD_CORE2, .name = "FRANK Core 2",
    .slug = "core2",
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
    .manual_note = "Covers Core 2 and Core 2U. Tape (GP45), the USB hub and "
                   "the mux (S5) are on the U revision only - on a plain "
                   "Core 2 those rows have nothing to find. GP43 is drawn to "
                   "the slave RUN pin but the net does not exist on either "
                   "revision; FS (GP40) is the reboot request instead.",
},

{
    /* One entry for three boards' slave halves, for the reason the Core
     * 2 and Core 2U masters are one entry: they are the same chip wired
     * the same way, and asking the operator to choose between them is
     * asking a question with no answer.
     *
     * FRANK Next's slave joined them. It is pin-identical — PSRAM on
     * GP0, the two buses on GP1-GP23, UART on GP24/25, LD1 on GP26 — and
     * everything that makes a FRANK Next a FRANK Next is on the other
     * chip, which this one cannot see. Given its own descriptor it
     * scored exactly what this one scores, on the same six pins, and
     * turned every Core 2 slave into a tie the operator had to break by
     * hand.
     *
     * The cost is LD2, in the usual direction: it exists on FRANK Next
     * and on neither Core 2, so the WS2812 row has nothing to find on
     * two boards out of three. The manual note says so. */
    .id = FRANK_BOARD_CORE2, .name = "Core 2 / FRANK Next (slave)",
    .slug = "core2-slave",
    .mcu = FRANK_MCU_RP2350A, .role = FRANK_ROLE_SLAVE,
    .caps = CAP_PSRAM_QMI | CAP_LINK | CAP_USB_DEVICE
          | CAP_LED_PLAIN | CAP_LED_WS2812,
    .pins = { PINS_NONE,
              .uart_tx = 24, .uart_rx = 25,
              .led_plain = 26, .led_ws2812 = 27, .psram_cs = 0,
              .link_a_data = 1, .link_b_data = 11,
              .link_fs = 21, .link_db_in = 22, .link_db_out = 23 },
    SIG(sig_core2_slave),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = "Covers the Core 2, Core 2U and FRANK Next slaves, "
                   "which are pin-identical and cannot be told apart from "
                   "this half. LD2 (WS2812B, GP27) is on FRANK Next only - "
                   "on either Core 2 that row has nothing to light.",
},

/* ---------------------------------------------------------------- */
/* FRANK Next, second design, and almost nothing of the first survives.
 *
 * It was one RP2350B with an ESP32 on SPI; it is now the Core 2's
 * arrangement — an RP2350B master and an RP2350A slave across two
 * eight-bit buses — carrying the FRANK Next peripherals. Video on
 * GP12-19, the TLV320DAC3100 and the DS3231 are the only things that
 * stayed on the pins they were on. The SD moved, the I2C moved, the I2S
 * master clock moved, the tape input moved, the LEDs moved to the other
 * chip, the DS2401 and the gamepad ports are gone, the single PSRAM
 * became one per MCU, and the ESP32 became an ESP8266EX on a UART.
 *
 * The old descriptor is not kept alongside this one. Both designs are
 * called FRANK Next, both would answer to the same name in a dialog, and
 * the fleet has no first-design boards left to test. */
{
    .id = FRANK_BOARD_NEXT, .name = "FRANK Next", .slug = "next",
    .mcu = FRANK_MCU_RP2350B, .role = FRANK_ROLE_MASTER,
    .caps = CAP_VIDEO_HDMI
          | CAP_AUDIO_I2S | CAP_AUDIO_CODEC_I2C
          | CAP_SD | CAP_SD_4BIT | CAP_PSRAM_QMI
          | CAP_I2C | CAP_RTC_DS3231
          | CAP_TAPE_IN | CAP_LINK | CAP_LINK_RUN
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_ESP01,
    .pins = { PINS_NONE,
              /* GP46/GP45 (J2), because GP0/GP1 are the SD card's CMD
               * and CLK here. GP46 is UART0 TX only at the B package's
               * alternate function — see boards/frank_next.h. */
              .uart_tx = 46, .uart_rx = 45,
              /* SDIO 4-bit, and not on the GP4-7 SPI convention: CMD and
               * CLK sit below the data lines rather than among them. */
              .sd_cmd = 0, .sd_clk = 1, .sd_dat0 = 4,
              .sd_dat1 = 5, .sd_dat2 = 6, .sd_cs = 7,
              /* TLV320DAC3100: BCLK/WCLK/DIN, not the TDA's
               * DATA/SCLK/LRCK order. Hence the explicit fields. */
              .i2s_clk_base = 9, .i2s_data = 11, .i2s_mclk = 8,
              .video_base = 12, .psram_cs = 47,
              .i2c_sda = 2, .i2c_scl = 3,
              /* A BC850 squaring the tape input, where the rest of the
               * fleet uses a CD4069. */
              .tape_in = 44,
              /* The console UART's own pins. The 8266 hangs off the same
               * pair through U26, so the two cannot talk at once and
               * neither can be reached while JP2 gives the mux to the
               * CH343P. */
              .esp_uart_tx = 46, .esp_uart_rx = 45,
              .link_a_data = 20, .link_b_data = 30,
              .link_fs = 40, .link_db_out = 41, .link_db_in = 42,
              /* A real net here, unlike on the Core 2, where the same
               * pin is drawn to the slave's RUN and connected to
               * nothing. The reset test still goes through FS: that
               * path is the one both boards have, and it proves the
               * slave's firmware is listening rather than only that its
               * RUN pin works. Recorded so that nothing drives it by
               * accident — see the field's note in board_desc.h. */
              .link_run = 43 },
    SIG(sig_next),
    .flash_bytes = 16u * 1024u * 1024u,
    .psram_bytes = 8u * 1024u * 1024u,
    .manual_note = "JP2 is two switches: UART_SEL gives the ESP8266 to "
                   "this chip or to the CH343P, and USB_SEL gives the "
                   "USB-C port to the CH343P or to the slave. The ESP row "
                   "can only pass with UART_SEL on the RP side. S7 "
                   "selects the master's USB between the hub and the "
                   "second port.",
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
          | CAP_AUDIO_I2S | CAP_AUDIO_PCM5122 | CAP_I2C
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_LED_PLAIN,
    .pins = { PINS_NONE, PINS_UART01,
              /* SD is on SPI1: pins above GP29 have no SPI0 mapping. */
              .sd_clk = 30, .sd_cmd = 31, .sd_dat0 = 40, .sd_cs = 43,
              .ps2_kb_clk = 14, .ps2_kb_dat = 15,
              .pad_clk = 4, .pad_latch = 5, .pad_d1 = 7, .pad_d2 = 8,
              /* The onboard I2S pair, not the PCM5122 hat's. */
              .i2s_data = 10, .i2s_clk_base = 11, .i2s_mclk = NC,
              /* The hat's, which are different pins entirely - data on
               * GP21, BCK GP18, LRCK GP19, and no master clock at all.
               * From SpeccyP's WS_ZERO2 board file, which drives this
               * hat on this board and moves PS/2 to GP14/15 to make room
               * for it - which is where this descriptor already has it. */
              .pcm_i2s_data = 21, .pcm_i2s_clk_base = 18,
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

/* ---------------------------------------------------------------- */
{
    /* xt8086, and the first board in the fleet where the RP2350 is not
     * the computer.
     *
     * U5 is a real 8086 in minimum mode and the RP2350B is its entire
     * chipset: clock generator, wait-state logic, memory and every
     * peripheral. That takes GP0-29, which is why nothing this firmware
     * normally expects is where it normally is — microSD moves to
     * GP40-43, the console to GP44/45, I2C to GP38/39 and video to
     * GP30-37.
     *
     * VIDEO IS VGA, AND ONLY VGA.
     *
     * There are two connectors and one signal. GP30-37 drive a resistor
     * ladder — 800R/400R per channel, two bits each, syncs on GP36/37 —
     * which reaches the DSUB-15 at J11 directly and also feeds U16, an
     * MS9288A that converts that analogue VGA back up to TMDS for the
     * HDMI socket at J12. So the HDMI connector carries VGA that has
     * been through a converter, and the firmware has no HDMI path to
     * offer: CAP_VIDEO_HDMI is not declared, and video_detect.c's
     * "a VGA-only board is the one case where the verdict still stands"
     * applies here and nowhere else.
     *
     * The pin order is the fleet's, one octave up: B0 B1 G0 G1 R0 R1 HS
     * VS from video_base, which is the same byte the HSTX VGA backend
     * builds. What is different is the base. HSTX is hard-wired to
     * GP12-19 on this silicon and those pins are AD12-A19 here, so the
     * hardware backend cannot serve this board and must not try — see
     * ui_video_vga.c, which now refuses any base but 12. The PIO VGA
     * backend takes it instead, exactly as PIO HDMI took the PiZero, and
     * like that path it scans 320x240 and so draws the text page rather
     * than the desktop.
     *
     * AUDIO IS ONE PIN.
     *
     * GP46 through JP1/JP2/JP3/JP4 to the buzzer, the 3.5 mm jack, or
     * the MS9288A's analogue input so the converted HDMI carries sound.
     * No DAC, no mux, no second channel: CAP_AUDIO_PWM. */
    .id = FRANK_BOARD_XT8086, .name = "xt8086", .slug = "xt8086",
    .mcu = FRANK_MCU_RP2350B, .role = FRANK_ROLE_SINGLE,
    .caps = CAP_VIDEO_VGA
          | CAP_AUDIO_PWM
          | CAP_SD | CAP_PSRAM_QMI
          | CAP_I2C | CAP_RTC_DS3231
          | CAP_USB_DEVICE | CAP_USB_HOST | CAP_USB_HUB | CAP_USB_MUX
          | CAP_HOST_8086,
    .pins = { PINS_NONE,
              /* J1, and not GP0/1: those are AD0 and AD1. */
              .uart_tx = 44, .uart_rx = 45,
              /* SPI1 — GP40-43 have no SPI0 mapping. Same DAT0 / CD-DAT3
               * / CLK / CMD order as the rest of the fleet, four pins
               * higher up. */
              .sd_dat0 = 40, .sd_cs = 41, .sd_clk = 42, .sd_cmd = 43,
              .video_base = 30,
              .speaker = 46,
              .psram_cs = 47,
              .i2c_sda = 38, .i2c_scl = 39,
              /* GP0-29 belong to U5. Declared as a run so the short
               * sweep skips them; the individual lines are in
               * docs/pinouts.txt. */
              .host_bus_base = 0, .host_bus_len = 30,
              .host_clk = 29, .host_reset = 28, .host_ready = 27 },
    SIG(sig_xt8086),
    .flash_bytes = 16u * 1024u * 1024u,      /* U7, W25Q128JVPIQ  */
    .psram_bytes = 8u * 1024u * 1024u,       /* U8, ESP-PSRAM64H  */
    .manual_note = "The 8086 (U5) has no clock unless firmware makes one on "
                   "GP29, so it executes nothing and its bus stays high-Z - "
                   "which is what lets the rig test the rest of the board. "
                   "Nothing here starts it. S2 grounds the VGA ladder's 75R "
                   "terminations: with it open the picture is roughly double "
                   "level and the HDMI converter sees the same. S1 selects "
                   "the USB mux. JP1-JP4 send GP46 to the HDMI converter, "
                   "the jack, or the buzzer - one at a time. LD1 is on "
                   "GP25/BHE and belongs to the CPU, not to the firmware.",
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
