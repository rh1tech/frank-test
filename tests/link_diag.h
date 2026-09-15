/*
 * FRANK Core 2 — dual-RP2350 test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * diag_link.h — the master's half of the inter-processor test.
 *
 * Brings the link up, interrogates the slave, sweeps the wire rate and
 * writes the results straight into the console report.
 */
#ifndef LINK_DIAG_H
#define LINK_DIAG_H

#include <stdbool.h>
#include <stdint.h>

#include "link_proto.h"

typedef struct {
    bool               contacted;      /* slave answered HELLO           */
    link_node_info_t   slave_info;
    bool               slave_mem_valid;
    link_mem_result_t  slave_mem;

    /* Per-divider sweep results. */
    struct {
        uint16_t clkdiv_q88;           /* divider in 8.8 fixed point     */
        uint32_t m2s_bytes_per_s;
        uint32_t s2m_bytes_per_s;
        uint32_t duplex_bytes_per_s;
        uint32_t byte_errors;          /* bytes that came back wrong     */
        bool     verify_ran;           /* the verified pass completed    */
        bool     ok;
    } sweep[4];
    int  sweep_count;

    uint32_t rtt_ns;                   /* 128-byte control round trip    */
    uint32_t best_bytes_per_s;         /* fastest error-free divider     */
    bool     all_passed;
} diag_link_result_t;

/* Claim PIO/DMA and configure the master's link pins. Call once.
 *
 * `slave_run_pin` is the GPIO that reaches the other chip's RUN, or -1
 * where the board has no such net — which is every board but FRANK Next.
 * Taken as an argument rather than from a board header because it is the
 * one link pin that is not the same on every board with a link: the rest
 * are identical across the Core 2, the Core 2U and FRANK Next, and this
 * one either exists or does not. */
void diag_link_init(int slave_run_pin);

/* Run the whole sequence: handshake, remote self-test, rate sweep,
 * verified transfers, latency. Renders progress into the console log
 * and the results into the fixed report rows as it goes. */
void diag_link_run(diag_link_result_t *out);

/* Repaint the link table from a previous run (used after a redraw). */


/* Cheap "is the slave there yet?" probe for the idle loop.
 *
 * A slave that boots late, is reflashed, or reboots on its own has to be
 * picked up by the master noticing. Uses a short doorbell timeout so a
 * genuinely absent slave costs a blink rather than a stalled foreground,
 * and escalates as the failures accumulate: first an FS request asking a
 * wedged slave to reboot itself, then — where the board has the pin — a
 * RUN pulse, which is the only thing that reaches a slave whose
 * interrupts are off.
 *
 * Returns true when the slave answered — the caller should then re-run
 * the full diagnostic. */
bool diag_link_try_reconnect(void);

/* Ask the slave to reboot over the wire and prove that it did.
 *
 * Confirms the slave is answering, pulses FS, then watches for it to
 * stop answering and come back. "Stopped answering" is the part that
 * matters: without it a slave that simply never rebooted is
 * indistinguishable from one that rebooted too fast to notice.
 *
 * Returns true only if the slave went away and returned. */
bool diag_link_reset_slave(void);

/* Why a hardware reset attempt ended. Not a bool, because the four ways
 * this fails want four different things said about them — and one of
 * them, HELD_ALIVE, is a specific diagnosis rather than a disappointment:
 * the slave went on answering while its RUN pin was held low, so the pin
 * this firmware is driving does not reach it. That is the Core 2's
 * missing net, reported from the master rather than traced by hand. */
typedef enum {
    DIAG_RESET_OK = 0,        /* held down, released, came back          */
    DIAG_RESET_NO_PIN,        /* the board has no RUN net                */
    DIAG_RESET_ABSENT,        /* nothing was answering to begin with     */
    DIAG_RESET_HELD_ALIVE,    /* answered while RUN was held low         */
    DIAG_RESET_NO_RETURN,     /* went down and did not come back         */
} diag_reset_t;

/* Pull the slave's RUN low, prove it went quiet *while held*, release,
 * and wait for it to come back.
 *
 * Probing during the hold is what separates this from the FS test. A
 * slave that reboots when asked proves its firmware is listening; a
 * slave that stops answering because a pin is held low proves the pin
 * arrives, which is a different claim about different copper and the one
 * that matters when the firmware is the thing that has stopped working.
 *
 * Takes several seconds: a RUN reset is a cold boot, so the slave does
 * not know it was asked and runs its full console-attach window on the
 * way up. See the note on the timeout in the implementation. */
diag_reset_t diag_link_hard_reset_slave(void);

#endif /* LINK_DIAG_H */
