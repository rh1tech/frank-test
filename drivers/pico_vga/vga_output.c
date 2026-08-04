/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * vga_output.c — the raw-HSTX scanout.
 *
 * Structurally a stripped copy of drivers/pico_hdmi/video_output.c: two
 * DMA channels chained to each other, feeding the HSTX FIFO, with an
 * interrupt at the end of every transfer that posts whatever the next
 * scanline needs. What is missing is everything HDMI-specific — TMDS
 * encoding, data islands, audio, infoframes — because analogue VGA has
 * none of it. A line is sync levels and pixel levels and nothing else.
 *
 *
 * REGISTER SETUP
 *
 * The HSTX configuration is taken from DispHSTX's disphstx_vga.c, which
 * is the reference implementation for driving VGA out of this peripheral:
 *
 *     csr          CLKDIV 5, N_SHIFTS 5, SHIFT 16, EXPAND_EN, EN
 *     expand_shift ENC_N_SHIFTS 5, ENC_SHIFT 0, RAW_N_SHIFTS 5, RAW_SHIFT 0
 *     bit[0..7]    B0 B1 G0 G1 R0 R1 HS VS, at shift positions
 *                  0, 2, 4, 6, 8, 10, and 12/13 for the syncs
 *
 * Five shifts of 16 bits per pixel, one word popped per pixel. See
 * vga_output.h for why the word carries its 16-bit pattern twice.
 *
 * Note what is *not* different from the HDMI path: clk_hstx. HDMI needs
 * 126 MHz because TMDS is ten bits per pixel at two bits per lane per
 * clock; VGA needs 126 MHz because it takes five clocks per pixel at
 * 25.2 MHz. The same number for unrelated reasons, which is convenient
 * and worth not mistaking for a shared cause.
 */

#include "vga_output.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "pico/stdlib.h"

#include <string.h>

/* HSTX command expander opcodes, in the top nibble-and-a-bit of a word.
 * Same encoding the HDMI driver uses. */
#define HSTX_CMD_RAW        (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT (0x1u << 12)
#define HSTX_CMD_NOP        (0xfu << 12)

#define DMACH_PING 0
#define DMACH_PONG 1

/* HSTX drives GP12..GP19 and nothing else — the peripheral is hard-wired
 * to those pads, which is why every Murmulator-lineage board puts its
 * video connector there. */
#define VGA_PIN_BASE 12

volatile uint32_t vga_frame_count;

static uint32_t line_buffer[VGA_ACTIVE_WORDS] __attribute__((aligned(4)));

static uint32_t v_scanline = 2;
static bool     vactive_cmdlist_posted = false;
static bool     dma_pong = false;

static vga_scanline_cb_t scanline_callback;
static vga_vsync_cb_t    vsync_callback;

/* ------------------------------------------------------------------ */
/* Command lists                                                       */
/* ------------------------------------------------------------------ */

/* Blanking levels: black picture, syncs at their idle level, with the
 * pulse cut into the middle of the line. */
/* One pixel per word, so a blanking word is just the byte.
 *
 * Packing four pixels per word and letting the expander unpack them
 * (RAW_N_SHIFTS 4, RAW_SHIFT 8) worked but ran the frame at 78.5 Hz
 * instead of 60: the command counts and the expansion factor interact,
 * and which of the two a count is measured in was not something the
 * numbers would settle. At 1:1 there is nothing to get wrong — a count
 * is pixels, is output words, is input words. It costs 2.5 KB of line
 * buffer and four table lookups per word instead of one, which on this
 * budget is nothing. */
#define W(p)     ((uint32_t)(uint8_t)(p))
#define W_IDLE   W(VGA_SYNC_IDLE)   /* HS and VS both idle high */
#define W_HSYNC  W(VGA_BIT_VS)      /* HS low, VS still idle    */
#define W_VSYNC  W(VGA_BIT_HS)      /* VS low, HS still idle    */
#define W_BOTH   W(0)               /* both pulses coincide     */

static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | VGA_H_FRONT_PORCH, W_IDLE,
    HSTX_CMD_RAW_REPEAT | VGA_H_SYNC_WIDTH,  W_HSYNC,
    HSTX_CMD_RAW_REPEAT | (VGA_H_BACK_PORCH + VGA_H_ACTIVE_PIXELS), W_IDLE,
};

static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | VGA_H_FRONT_PORCH, W_VSYNC,
    HSTX_CMD_RAW_REPEAT | VGA_H_SYNC_WIDTH,  W_BOTH,
    HSTX_CMD_RAW_REPEAT | (VGA_H_BACK_PORCH + VGA_H_ACTIVE_PIXELS), W_VSYNC,
};

/* An active line is posted in two transfers: this list, which ends with
 * a RAW command announcing 640 pixels, and then the 640 words that
 * carry them.
 * That is why the interrupt handler has a "cmdlist posted" flag — the
 * scanline only advances on the second of the pair. */
static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | VGA_H_FRONT_PORCH, W_IDLE,
    HSTX_CMD_RAW_REPEAT | VGA_H_SYNC_WIDTH,  W_HSYNC,
    HSTX_CMD_RAW_REPEAT | VGA_H_BACK_PORCH,  W_IDLE,
    HSTX_CMD_RAW | VGA_H_ACTIVE_PIXELS,
};

/* ------------------------------------------------------------------ */
/* Interrupt                                                           */
/* ------------------------------------------------------------------ */

static inline void __scratch_x("") post(dma_channel_hw_t *ch,
                                        const uint32_t *src, uint32_t words) {
    ch->read_addr      = (uintptr_t)src;
    ch->transfer_count = words;
}

void __scratch_x("") vga_dma_irq_handler(void) {
    const uint32_t ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1U << ch_num;
    dma_pong = !dma_pong;

    const uint32_t vs_start = VGA_V_ACTIVE_LINES + VGA_V_FRONT_PORCH;
    const uint32_t vs_end   = vs_start + VGA_V_SYNC_WIDTH;

    if (v_scanline >= vs_start && v_scanline < vs_end) {
        post(ch, vblank_line_vsync_on, count_of(vblank_line_vsync_on));
    } else if (v_scanline < VGA_V_ACTIVE_LINES) {
        if (!vactive_cmdlist_posted) {
            if (scanline_callback) scanline_callback(v_scanline, line_buffer);
            post(ch, vactive_line, count_of(vactive_line));
            vactive_cmdlist_posted = true;
        } else {
            post(ch, line_buffer, VGA_ACTIVE_WORDS);
            vactive_cmdlist_posted = false;
        }
    } else {
        post(ch, vblank_line_vsync_off, count_of(vblank_line_vsync_off));
    }

    if (!vactive_cmdlist_posted) {
        v_scanline++;
        if (v_scanline >= VGA_V_TOTAL_LINES) {
            v_scanline = 0;
            vga_frame_count++;
            if (vsync_callback) vsync_callback();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

void vga_output_init(void) {
    /* clk_hstx = 126 MHz, five clocks per 25.2 MHz pixel.
     *
     * The pixel clock is NOT reachable directly, and that constraint is
     * the whole reason this driver spends five clocks on each pixel:
     * CLOCKS_CLK_HSTX_DIV_INT is two bits wide — mask 0x00030000 — so
     * clk_hstx can only be clk_sys divided by 1, 2 or 3. Asking for
     * 25.2 MHz from a 252 MHz system clock silently wrote a divider of
     * 10 & 3 = 2 and produced 126 MHz, which scanned out a picture at
     * five times the correct line rate and no monitor would look at.
     *
     * Sourced from the PLL rather than clk_sys, matching the call
     * ui_video_hstx.c makes — which looks like belt-and-braces next to
     * video_output_init()'s own clk_sys version and is the one actually
     * relied upon. */
    clock_configure(clk_hstx, 0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    clock_get_hz(clk_sys), 126000000);

    dma_channel_claim(DMACH_PING);
    dma_channel_claim(DMACH_PONG);
}

void vga_output_set_scanline_callback(vga_scanline_cb_t cb) { scanline_callback = cb; }
void vga_output_set_vsync_callback(vga_vsync_cb_t cb)       { vsync_callback = cb; }

/* Lane i takes bit i, for both halves of the clock.
 *
 * HSTX is double data rate and SEL_P/SEL_N pick what each half emits.
 * An analogue level must not change mid-pixel, so both point at the same
 * bit — which is also what makes a pixel exactly one clock rather than
 * the five TMDS needs. */
#define SEL_SAME(b) ((uint32_t)(b) << HSTX_CTRL_BIT0_SEL_P_LSB | \
                     (uint32_t)(b) << HSTX_CTRL_BIT0_SEL_N_LSB)

void vga_output_core1_run(void) {
    /* N_SHIFTS 1, not 5.
     *
     * This field is how many *output* words the command expander makes
     * from each *input* word, rotating RAW_SHIFT bits between them —
     * with RAW_SHIFT 0 that is plain horizontal duplication. DispHSTX
     * uses 5 because its VGA modes scale a 320-wide framebuffer up to
     * 640; this scans out 640 real pixels, so it must be 1.
     *
     * At 5 the picture never appeared and the reason was not visually
     * obvious: a `RAW | 640` command consumed only 128 input words while
     * the DMA pushed 640, so the expander started reading pixels as
     * commands, the FIFO stopped draining, and the whole chain jammed
     * three scanlines in. The symptom over the probe was a DMA channel
     * whose registers never changed between reads. */
    hstx_ctrl_hw->expand_shift =
        1u << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        0u << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB    |
        1u << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0u << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    /* SHIFT 0, N_SHIFTS 5: hold the word still for five clocks, then
     * take the next one. Five clocks at 126 MHz is one 25.2 MHz pixel.
     *
     * Rotating is what the field is for and it is exactly wrong here.
     * DispHSTX rotates 16 bits five times, relying on a 32-bit register
     * wrapping to land back on the same halves; on this silicon that
     * asks for 80 bits out of 32 and the peripheral simply stops — FIFO
     * full, DREQ never re-asserted, DMA transfer count frozen. Zero is
     * both legal and what is actually wanted: the lane selectors point
     * at fixed bits and nothing needs to move underneath them.
     *
     * The HDMI path is SHIFT 2 / N_SHIFTS 5, which is ten bits — one
     * TMDS symbol — and stays inside the register for the same reason. */
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS |
                        5u << HSTX_CTRL_CSR_CLKDIV_LSB   |
                        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
                        0u << HSTX_CTRL_CSR_SHIFT_LSB    |
                        HSTX_CTRL_CSR_EN_BITS;

    for (int i = 0; i < 8; i++)
        hstx_ctrl_hw->bit[i] = SEL_SAME(i);

    for (int i = VGA_PIN_BASE; i < VGA_PIN_BASE + 8; i++)
        gpio_set_function(i, 0);           /* function 0 is HSTX on RP2350 */

    dma_channel_config c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PING, &c, &hstx_fifo_hw->fifo,
                          vblank_line_vsync_off,
                          count_of(vblank_line_vsync_off), false);

    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PONG, &c, &hstx_fifo_hw->fifo,
                          vblank_line_vsync_off,
                          count_of(vblank_line_vsync_off), false);

    dma_hw->ints0 = (1U << DMACH_PING) | (1U << DMACH_PONG);
    dma_hw->inte0 = (1U << DMACH_PING) | (1U << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, vga_dma_irq_handler);
    irq_set_priority(DMA_IRQ_0, 0);
    irq_set_enabled(DMA_IRQ_0, true);

    /* The scanout must win every arbitration it enters: a DMA that
     * arrives late is a line that tears, and nothing else on this chip
     * has a deadline that tight. */
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
                            BUSCTRL_BUS_PRIORITY_DMA_R_BITS |
                            BUSCTRL_BUS_PRIORITY_PROC1_BITS;

    dma_channel_start(DMACH_PING);

    while (true) tight_loop_contents();
}
