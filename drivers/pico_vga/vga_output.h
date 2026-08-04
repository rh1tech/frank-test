/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * vga_output.h — 640x480 analogue VGA from the HSTX, in raw mode.
 *
 * Same eight pins as HDMI, completely different use of them. HDMI shifts
 * TMDS symbols down three differential pairs;
 * this shifts a plain parallel byte to eight single-ended pins, six of
 * which drive a resistor ladder and two of which are the syncs.
 *
 * Every symbol here is prefixed vga_ so this and pico_hdmi can be linked
 * into the same image. Only one may be initialised — they both want DMA
 * 0/1 and DMA_IRQ_0 — which ui_video.c guarantees by opening exactly one
 * backend per boot.
 */
#ifndef VGA_OUTPUT_H
#define VGA_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

/* 640x480 @ 60 Hz, VESA. Pixel clock 25.175 MHz, reached as 126 MHz over
 * five HSTX clocks per pixel — clk_hstx cannot be divided down to the
 * pixel rate directly, see vga_output_init(). */
#define VGA_H_FRONT_PORCH   16
#define VGA_H_SYNC_WIDTH    96
#define VGA_H_BACK_PORCH    48
#define VGA_H_ACTIVE_PIXELS 640

#define VGA_V_FRONT_PORCH   10
#define VGA_V_SYNC_WIDTH    2
#define VGA_V_BACK_PORCH    33
#define VGA_V_ACTIVE_LINES  480

#define VGA_H_TOTAL_PIXELS \
    (VGA_H_FRONT_PORCH + VGA_H_SYNC_WIDTH + VGA_H_BACK_PORCH + VGA_H_ACTIVE_PIXELS)
#define VGA_V_TOTAL_LINES \
    (VGA_V_FRONT_PORCH + VGA_V_SYNC_WIDTH + VGA_V_BACK_PORCH + VGA_V_ACTIVE_LINES)

/* ------------------------------------------------------------------ */
/* The pixel byte                                                      */
/* ------------------------------------------------------------------ */

/*
 * One byte per pixel, four pixels per 32-bit word, leftmost in the low
 * byte. Lane i takes bit i and the word is held still for five HSTX
 * clocks, which at 126 MHz is one 25.2 MHz pixel.
 *
 *      7   6   5   4   3   2   1   0
 *     VS  HS  R1  R0  G1  G0  B1  B0
 *
 * Confirmed twice over: murmnes' pico_vga_hstx/hstx_pins.h documents
 * exactly this byte ("GPIO 12 -> B0 (bit 0) ... GPIO 19 -> VS (bit 7)"),
 * and megafrank's netlist agrees — J20 pin 1 (red) is fed by R125 from
 * GP17 and R128 from GP16, pin 2 (green) by R126/R129 from GP15/GP14,
 * pin 3 (blue) by R127/R130 from GP13/GP12, with the syncs on GP18/GP19
 * through R131/R132.
 *
 * An earlier version packed one pixel per word with its 16-bit pattern
 * duplicated, following DispHSTX's arrangement of rotating 16 bits five
 * times and relying on a 32-bit register wrapping back onto the same
 * halves. That asks for 80 bits out of 32, and on this silicon the
 * peripheral simply stopped: FIFO full, DREQ never re-asserted, DMA
 * transfer count frozen. Holding the word still instead — CSR SHIFT 0 —
 * gets the same five clocks with nothing moving underneath the lane
 * selectors, and fits four pixels in a word rather than one.
 */
#define VGA_BIT_B0 0x01u
#define VGA_BIT_B1 0x02u
#define VGA_BIT_G0 0x04u
#define VGA_BIT_G1 0x08u
#define VGA_BIT_R0 0x10u
#define VGA_BIT_R1 0x20u
#define VGA_BIT_HS 0x40u
#define VGA_BIT_VS 0x80u

/* 640x480@60 has negative sync on both lines, so the bit is HIGH except
 * during the pulse. Encoded as levels rather than via the HSTX INV bit,
 * because a list of words that reads the way a scope would is easier to
 * check than one that depends on a register set somewhere else. */
#define VGA_SYNC_IDLE (VGA_BIT_HS | VGA_BIT_VS)

/* One pixel per word: the byte sits in bits 0-7 and the rest is
 * ignored, because the lane selectors only ever look at bits 0-7. */
#define VGA_PIXELS_PER_WORD 1
#define VGA_ACTIVE_WORDS    VGA_H_ACTIVE_PIXELS

/* r/g/b are 2 bits each, bit 0 being the low-order ladder leg. */
static inline uint8_t vga_pixel_byte(unsigned r, unsigned g, unsigned b,
                                     uint8_t sync) {
    uint8_t p = sync;
    if (b & 1u) p |= VGA_BIT_B0;
    if (b & 2u) p |= VGA_BIT_B1;
    if (g & 1u) p |= VGA_BIT_G0;
    if (g & 2u) p |= VGA_BIT_G1;
    if (r & 1u) p |= VGA_BIT_R0;
    if (r & 2u) p |= VGA_BIT_R1;
    return p;
}

/* The pixel byte in its word. Only bits 0-7 are ever looked at — the
 * lane selectors point there and nowhere else. */
static inline uint32_t vga_word(uint8_t p) { return (uint32_t)p; }

/* ------------------------------------------------------------------ */
/* Interface — deliberately the same shape as pico_hdmi's              */
/* ------------------------------------------------------------------ */

extern volatile uint32_t vga_frame_count;

/* Fill one active line: VGA_ACTIVE_WORDS words of four packed pixels,
 * leftmost in the low byte, each built with VGA_SYNC_IDLE. */
typedef void (*vga_scanline_cb_t)(uint32_t active_line, uint32_t *line_buffer);
typedef void (*vga_vsync_cb_t)(void);

void vga_output_init(void);
void vga_output_set_scanline_callback(vga_scanline_cb_t cb);
void vga_output_set_vsync_callback(vga_vsync_cb_t cb);

/* Never returns. Runs on core 1. */
void vga_output_core1_run(void);

#endif /* VGA_OUTPUT_H */
