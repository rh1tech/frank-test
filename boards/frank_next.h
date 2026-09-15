// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------
//
// FRANK Next — master half (U3, RP2350B in QFN-80).
//
// A header of its own rather than frank_b.h, for the same reason
// frank_xt8086.h is one: frank_b.h puts the console on GP0/GP1, and on
// this board GP0 is the microSD's CMD line and GP1 its CLK. A shared
// header would have every printf drive the card's command line.
//
// The rest of frank_b.h is wrong here too — its default SPI is the
// microSD at GP4-7, which on this board is SDIO data rather than SPI,
// and its WS2812 is GP46, which here is the console TX. The package,
// the flash part and the flash size are all these two agree on.
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// https://github.com/rh1tech/frank-test
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _BOARDS_FRANK_NEXT_H
#define _BOARDS_FRANK_NEXT_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define FRANK_NEXT

// --- RP2350 VARIANT ---
// RP2350B: 48 GPIOs, so PIO needs the movable 32-pin window.
#define PICO_RP2350A 0

pico_board_cmake_set_default(PICO_PIO_USE_GPIO_BASE, 1)
#ifndef PICO_PIO_USE_GPIO_BASE
#define PICO_PIO_USE_GPIO_BASE 1
#endif

// --- UART (J2 header: pin 1 = TX, pin 2 = GND, pin 3 = RX) ---
// GP46/GP45, still UART0 — the peripheral's pin map repeats every
// sixteen GPIOs in blocks of four, so GP44-47 lands back on UART0.
//
// GP45 is UART0 RX at the usual FUNCSEL 2, but GP46 is UART0 *CTS*
// there; its TX lives at FUNCSEL 11, the B package's alternate. Nothing
// here has to say so, because the SDK picks per pin — UART_FUNCSEL_NUM()
// chooses the alternate for any GPIO with bit 1 set, which is exactly
// the CTS/RTS half of each block of four. Code that reaches for
// GPIO_FUNC_UART directly on this pin gets flow control and a console
// that never transmits.
//
// The same two pins also reach the ESP8266 through the U26 mux, so the
// console and the companion processor are the same UART on this board.
// JP2 decides whether the ESP hears the RP or the CH343P; it does not
// disconnect the RP either way.
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 46
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 45
#endif

// --- LED ---
// Neither LED is on this half. LD1 (plain blue) and LD2 (WS2812B) both
// hang off the slave, on its GP26 and GP27 — see frank_next_slave.h.

// --- SPI ---
// No default. The microSD is SDIO 4-bit: CMD on GP0, CLK on GP1, DAT0-2
// on GP4-6 and CD/DAT3 on GP7, driven by PIO rather than by the SPI
// peripheral. Declaring an SPI here would name pins that carry a
// different protocol.

// --- I2C (TLV320DAC3100 at 0x18, DS3231MZ at 0x68) ---
// I2C1 on GP2/GP3. The firmware bit-bangs this bus rather than using the
// peripheral, but the mapping is declared so the numbers are in one
// place with the rest.
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 1
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 2
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 3
#endif

// --- FLASH (U2, W25Q128JVPIQ, 16 MB) ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
