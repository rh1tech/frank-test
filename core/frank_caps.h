/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * frank_caps.h — board identity and capability vocabulary.
 *
 * Every test in tests/ declares the capabilities it needs; the runner
 * compares that against the detected board's mask and skips what the
 * board does not have. The distinction the whole design rests on:
 *
 *     n/a   the board has no such hardware       (not a defect)
 *     fail  the hardware is there and wrong      (a defect)
 *     no-run the test could not complete          (unknown)
 *
 * Conflating the first two is how a test suite stops being believed.
 */
#ifndef FRANK_CAPS_H
#define FRANK_CAPS_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Boards                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    FRANK_BOARD_UNKNOWN = 0,

    FRANK_BOARD_FRANK,            /* Pico 2 module carrier              */
    FRANK_BOARD_FRANK_PGA,        /* PGA2350 carrier                    */
    FRANK_BOARD_MEGAFRANK,        /* PGA2350 + TurboSound + RTC + PSRAM */
    FRANK_BOARD_MICROFRANK,       /* RP2350A, HDMI only                 */
    FRANK_BOARD_MINIFRANK,        /* RP2350A, HDMI + VGA                */
    FRANK_BOARD_ZEROFRANK,        /* RP2350A, HDMI + PIO-USB            */
    FRANK_BOARD_OLDSKOOLFRANK,    /* PGA2350, TDA1545A, DB9             */
    FRANK_BOARD_CORE2,            /* RP2350B + RP2350A                  */
    FRANK_BOARD_CORE2U,           /* as CORE2, + tape + USB hub/mux     */
    FRANK_BOARD_NEXT,             /* RP2350B + ESP32                    */
    FRANK_BOARD_Z0PA,             /* Waveshare RP2350-PiZero            */

    FRANK_BOARD_COUNT
} frank_board_id_t;

/* Which of the two binaries this board belongs to.
 *
 * Two, not three. `hecate` and `frank`'s RP2040-Zero are the fleet's only
 * RP2040s, both are PS/2-to-USB adapters with no video hardware, and both
 * are exercised through the PS/2 tests of whatever board they are plugged
 * into — so neither needs firmware of its own. See PLAN.md 1.4. */
typedef enum {
    FRANK_MCU_RP2350A = 0,        /* QFN-60, 30 GPIOs */
    FRANK_MCU_RP2350B,            /* QFN-80 / PGA2350, 48 GPIOs */
    FRANK_MCU_ANY,                /* descriptor is valid on more than one */
} frank_mcu_class_t;

/* A board can present more than one MCU to the firmware. The role says
 * which half this image is running on. */
typedef enum {
    FRANK_ROLE_SINGLE = 0,
    FRANK_ROLE_MASTER,
    FRANK_ROLE_SLAVE,
} frank_role_t;

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

typedef uint64_t frank_caps_t;

#define CAP_NONE                 0ull

/* Memory */
#define CAP_PSRAM_QMI            (1ull <<  0)  /* PSRAM on the QMI CS pin      */
#define CAP_PSRAM_SOFTSPI        (1ull <<  1)  /* bit-banged SPI PSRAM (mega)  */

/* Storage */
#define CAP_SD                   (1ull <<  2)  /* microSD on SPI               */
#define CAP_SD_4BIT              (1ull <<  3)  /* SDIO 4-bit wiring (next)     */

/* Video — see video_detect.h. All three drive the same eight GPIOs. */
#define CAP_VIDEO_HDMI           (1ull <<  4)
#define CAP_VIDEO_VGA            (1ull <<  5)
#define CAP_VIDEO_COMPOSITE      (1ull <<  6)

/* Audio */
#define CAP_AUDIO_I2S            (1ull <<  7)  /* TDA1387 / TDA1545A           */
#define CAP_AUDIO_CODEC_I2C      (1ull <<  8)  /* TLV320DAC3100, needs I2C init*/
#define CAP_AUDIO_AMP            (1ull <<  9)  /* PAM8403 — switch-controlled  */
#define CAP_AUDIO_MUX            (1ull << 10)  /* 74HC4052 — switch-controlled */
#define CAP_TURBOSOUND           (1ull << 11)  /* 2x AY via 74HC595            */

/* USB */
#define CAP_USB_DEVICE           (1ull << 12)
#define CAP_USB_HOST             (1ull << 13)
#define CAP_USB_HUB              (1ull << 14)
#define CAP_USB_MUX              (1ull << 15)  /* switch-controlled            */
#define CAP_PIO_USB              (1ull << 16)  /* soft host on ordinary GPIOs  */

/* Human input */
#define CAP_PS2                  (1ull << 17)
#define CAP_GAMEPAD_NES          (1ull << 18)
#define CAP_GAMEPAD_DB9          (1ull << 19)

/* Misc peripherals */
#define CAP_TAPE_IN              (1ull << 20)  /* CD4069 comparator on GP22    */
#define CAP_TAPE_DIP_GATED       (1ull << 21)  /* ...only when the DIP is on   */
#define CAP_RTC_DS3231           (1ull << 22)
#define CAP_ONEWIRE_DS2401       (1ull << 23)
#define CAP_I2C                  (1ull << 24)

/* Companion processors */
#define CAP_ESP01                (1ull << 25)  /* ESP-01S on a UART            */
#define CAP_ESP32_SPI            (1ull << 26)  /* ESP32-D0WD over SPI          */
#define CAP_LINK                 (1ull << 27)  /* the dual-RP2350 parallel link*/

/* Indicators and controls */
#define CAP_LED_WS2812           (1ull << 28)
#define CAP_LED_PLAIN            (1ull << 29)
#define CAP_DIPSWITCH            (1ull << 30)

/* This board can take the PCM5122 audio hat.
 *
 * Deliberately not CAP_AUDIO_CODEC_I2C, which means a codec is soldered
 * to the board and which detection vetoes a board for claiming when
 * nothing answers on the bus. The hat is an accessory: most units do not
 * have one fitted, and claiming it as present ruled Z0pa out of its own
 * identification. This says only that the connector and the pins exist,
 * which is true whether or not anything is plugged into them. */
#define CAP_AUDIO_PCM5122        (1ull << 31)

/* Any video output at all — convenience mask, not a bit of its own. */
#define CAP_VIDEO_ANY  (CAP_VIDEO_HDMI | CAP_VIDEO_VGA | CAP_VIDEO_COMPOSITE)

static inline bool frank_has_cap(frank_caps_t have, frank_caps_t want) {
    return (have & want) == want;
}

static inline bool frank_has_any(frank_caps_t have, frank_caps_t want) {
    return (have & want) != 0ull;
}

const char *frank_board_name(frank_board_id_t id);
const char *frank_board_slug(frank_board_id_t id);   /* for `board set <x>` */
const char *frank_mcu_class_name(frank_mcu_class_t c);
const char *frank_role_name(frank_role_t r);

/* Parse a slug back to an id. Returns FRANK_BOARD_UNKNOWN on no match. */
frank_board_id_t frank_board_from_slug(const char *slug);

#endif /* FRANK_CAPS_H */
