// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------
//
// The single-chip RP2350A FRANK boards: miniFRANK, microFRANK,
// zeroFRANK. One header rather than three, because they agree on
// everything the SDK needs to know — package, flash part, flash size and
// console pins — and differ only in peripherals, which the board table
// describes and this cannot.
//
// Not for the boards that socket a Pico module (FRANK, FRANK PGA,
// MegaFRANK, OldSkoolFRANK): those take whatever package is in the
// socket, so build them for the module that is fitted.
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// https://github.com/rh1tech/frank-test
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _BOARDS_FRANK_A_H
#define _BOARDS_FRANK_A_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define FRANK_A

// --- RP2350 VARIANT ---
// The A package: 30 GPIOs, so nothing above GP29 exists and PIO reaches
// every pin without a base window.
#define PICO_RP2350A 1

// --- UART ---
// GP0/GP1, which is the fleet convention. Note these are also the PS/2
// mouse pins on every board that has PS/2, and the mouse wins — see
// app/main.c. The console survives only where there is no PS/2 port.
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- LED ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// --- FLASH ---
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
