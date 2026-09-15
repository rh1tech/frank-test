/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tests_esp.c — the companion 8266, socketed or soldered down.
 *
 *
 * WHAT IS BEING TESTED
 *
 * That the companion answers over the UART. On most of the fleet that
 * is an ESP-01S in a socket; on frank_next it is an ESP8266EX soldered
 * to the board. The test cannot tell the difference and does not need
 * to — it asks, and either something answers or nothing does.
 *
 * Two firmwares are recognised, because both turn up in the field.
 *
 * frank-netcard, whose protocol is a small AT dialect at 115200 8N1:
 *
 *   AT        -> OK
 *   AT+VER    -> +VER:frank-netcard,1.0.0
 *                OK
 *   AT+HEAP   -> +HEAP:38240
 *                OK
 *
 * A pass proves the socket, both directions of the UART, the module's
 * 3V3 rail and that its firmware is running. That is the whole of what
 * this can honestly claim: nothing here touches WiFi, because a board
 * that cannot see an access point is not a faulty board.
 *
 * And Espressif's stock AT firmware, which answers AT but has no AT+VER
 * at all. It reports itself through AT+GMR instead:
 *
 *   AT+GMR    -> AT version:1.7.6.0(Jan 24 2022 08:56:02)
 *                SDK version:3.0.6-dev(072755c)
 *                compile time:Jun 17 2024 07:38:00
 *                Bin version(Wroom 02):1.7.6
 *                OK
 *
 * So AT+VER is asked first and AT+GMR second, and whichever answers
 * names the firmware in the row. A module that answers AT and neither of
 * those still passes: the socket and both directions of the UART are
 * proven either way, and that is what this test is for.
 *
 * The version and free heap are reported because they are free — the
 * module volunteers them.
 *
 *
 * WHY AN EMPTY SOCKET IS NOT A FAILURE
 *
 * Most of these boards ship without a module. Silence means "nothing
 * answered", which is exactly what an empty socket sounds like, and
 * calling that a fault would send someone looking for a defect that is
 * not there. It reports "could not run" instead. That does mean a dead
 * module and an empty socket are indistinguishable from here, and the
 * row says so rather than guessing.
 *
 * On frank_next the part is soldered down, so there is no empty socket
 * to explain a silence — but there is a switch. JP2's UART_SEL gives the
 * 8266 to the CH343P instead of to us, and from here that looks exactly
 * the same as an absent module. The row cannot tell them apart either;
 * the board's manual_note is what says to check the switch.
 *
 *
 * WHY THERE IS NO RESET
 *
 * CH_PU and GPIO0 go to buttons on every board in the fleet, not to
 * GPIOs — see the note in board_desc.h. So the module cannot be
 * power-cycled or held in bootloader from firmware, and the test has to
 * work with whatever state it is already in. That is why it opens with a
 * bare AT rather than a reset: if the module is up, AT answers
 * immediately, and if it is wedged there is nothing this can do about
 * it.
 *
 * The first FRANK Next was the exception, with an ESP32 on SPI and real
 * reset lines. Its replacement is not: the 8266 there takes reset from a
 * button and from the CH343P's DTR/RTS pair, which is the arrangement
 * that lets a host flash it over USB and reaches nothing of ours.
 *
 *
 * THE THREE PIN PAIRS
 *
 * GP20/21 on FRANK and miniFRANK, GP38/39 on FRANK PGA and MegaFRANK,
 * GP46/45 on frank_next. The first two are UART1 and the third is UART0,
 * and no two of them take the same function number — see the note on
 * esp_uart_open() for why, and why the rule that used to be written here
 * was the wrong generalisation of two examples.
 *
 *
 * THE SHARED PINS
 *
 * On FRANK and miniFRANK the ESP UART sits on GP20/21, which are also
 * the NES gamepad's clock and latch, and on miniFRANK GP20 is the PIO-USB
 * D+ as well. Talking to the module therefore clocks the gamepad shift
 * registers, which is harmless — they are read-only and nothing latches
 * a result — but it does mean the two cannot be in use at once. The test
 * hands the pins back as GPIO inputs when it finishes so the gamepad
 * dialog finds them as it expects.
 *
 * frank_next shares them with something less forgiving: its ESP hangs
 * off the console UART, through the same mux that can give those pins to
 * the CH343P instead. So this test borrows the console for the length of
 * the exchange and gives it back, rather than handing the pins over as
 * inputs — which on that board would end the run's logging.
 */

#include "registry.h"

#include "board_desc.h"
#include "detect.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdio_uart.h"
#include "ui_input.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

/* frank-netcard's default. AT+BAUD can move it, but a module that has
 * been moved and not moved back is not something this can discover, and
 * guessing at rates would turn a clean "nothing answered" into a slow
 * one. */
#define ESP_BAUD 115200u

/* Generous: the module answers a bare AT in single-digit milliseconds,
 * and AT+VER is no slower. This is sized for "is anything there at all",
 * not for the reply. */
#define ESP_REPLY_MS 400u

typedef struct {
    int tx, rx;
    uart_inst_t *uart;
    bool         is_console;     /* the pins are the console's own */
} esp_pins_t;

/* Which UART a pin belongs to.
 *
 * The peripheral's map goes in blocks of four and repeats every sixteen
 * GPIOs: 0-3 UART0, 4-7 UART1, 8-11 UART1, 12-15 UART0, and again from
 * GP16, GP32. That is one XOR of two bits, and writing it as one is
 * better than a table of twelve rows that has to be extended the next
 * time a board uses a pin nobody has used yet. */
static uart_inst_t *uart_for_pin(int gpio) {
    return (((gpio >> 2) ^ (gpio >> 3)) & 1) ? uart1 : uart0;
}

static bool pins_from(const frank_pins_t *p, esp_pins_t *out) {
    if (p->esp_uart_tx == PIN_NC || p->esp_uart_rx == PIN_NC) return false;
    out->tx   = p->esp_uart_tx;
    out->rx   = p->esp_uart_rx;
    out->uart = uart_for_pin(out->tx);

    /* Only true on frank_next, where there is one UART for both jobs. */
    out->is_console = (out->uart == uart_default) &&
                      (out->tx == PICO_DEFAULT_UART_TX_PIN) &&
                      (out->rx == PICO_DEFAULT_UART_RX_PIN);
    return true;
}

/* Each block of four is TX, RX, CTS, RTS at FUNCSEL 2 — and on the
 * RP2350, TX, RX again at FUNCSEL 11 for the CTS/RTS half. So a pair is
 * usable if it sits in one block with an even TX and an odd RX, and the
 * SDK's UART_FUNCSEL_NUM() picks the right function for each pin.
 *
 * This used to be "GP20/21 take 2, anything above GP31 takes 11", which
 * was true of the two pairs that existed when it was written and is not
 * a rule: it is 11 for GP38/39 because both have bit 1 set, not because
 * they are high pins. frank_next's GP46/45 straddles the two halves —
 * TX at 11, RX at 2 — and a blanket 11 would have configured its RX as
 * an RTS output driving the mux. */
static bool esp_uart_open(const esp_pins_t *p) {
    if ((p->tx >> 2) != (p->rx >> 2)) return false;   /* not one block */
    if ((p->tx & 1) || !(p->rx & 1))  return false;   /* not TX and RX */

    /* Where this is also the console, take stdio off it first. Anything
     * printed mid-exchange would go out as an AT line the module has to
     * ignore, and its reply would land in our collect buffer. */
    if (p->is_console) {
        stdio_flush();
        stdio_set_driver_enabled(&stdio_uart, false);
    }

    uart_init(p->uart, ESP_BAUD);
    uart_set_format(p->uart, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(p->uart, false, false);    /* no 8266 module has any */
    uart_set_fifo_enabled(p->uart, true);

    gpio_set_function((uint)p->tx, UART_FUNCSEL_NUM(p->uart, p->tx));
    gpio_set_function((uint)p->rx, UART_FUNCSEL_NUM(p->uart, p->rx));
    return true;
}

/* Back to plain inputs. The gamepad reader expects to find its clock and
 * latch as GPIOs, and leaving them driven by a UART would make its next
 * read nonsense.
 *
 * Unless they are the console's, in which case handing them back as
 * inputs is how the log goes quiet for the rest of the run. */
/* Empty the receive path, and keep emptying it until the line has been
 * quiet for a spell.
 *
 * Draining once is not enough. esp_reply_collect() returns the moment it
 * sees OK or ERROR, so the rest of that line is still on the wire when
 * the caller moves on: at 115200 the tail of "ERROR:UNKNOWN_CMD\r\n"
 * takes about a millisecond to arrive. Bytes that land after a single
 * drain are the whole bug this guards against - see esp_uart_close().
 *
 * The cap is there because a module emitting async events could
 * otherwise hold this for ever, and a test that never returns is worse
 * than a stale byte. */
#define ESP_DRAIN_QUIET_US  3000
#define ESP_DRAIN_CAP_US   50000

static void esp_drain(uart_inst_t *u) {
    const absolute_time_t cap = make_timeout_time_us(ESP_DRAIN_CAP_US);
    absolute_time_t quiet = make_timeout_time_us(ESP_DRAIN_QUIET_US);

    while (absolute_time_diff_us(get_absolute_time(), quiet) > 0 &&
           absolute_time_diff_us(get_absolute_time(), cap) > 0) {
        if (uart_is_readable(u)) {
            (void)uart_getc(u);
            quiet = make_timeout_time_us(ESP_DRAIN_QUIET_US);
        }
    }
}

static void esp_uart_close(const esp_pins_t *p) {
    if (p->is_console) {
        /* Drain before stdio takes the pins back, or the tail of the
         * last reply becomes typing.
         *
         * On FRANK Next this UART is the console - GP46/GP45 through
         * U26 - so anything still unread here is handed to
         * ui_input_getkey(), which cannot tell a byte from a keypress.
         * 'R' is bound to Restart, so one stale reply rebooted the
         * board. It read as the link soak resetting it, because
         * run_all() does not poll the keyboard: the bytes sat in the
         * FIFO from this test until the run ended, thirty seconds of
         * soak later. */
        esp_drain(p->uart);
        stdio_uart_init();          /* re-opens the UART and the driver */
        return;
    }

    uart_deinit(p->uart);
    gpio_init((uint)p->tx);
    gpio_set_dir((uint)p->tx, GPIO_IN);
    gpio_init((uint)p->rx);
    gpio_set_dir((uint)p->rx, GPIO_IN);
}

static void esp_send(uart_inst_t *u, const char *cmd) {
    uart_puts(u, cmd);
    uart_puts(u, "\r\n");
}

/* Collect reply lines until one of them is OK or ERROR, or until the
 * module has been quiet for long enough that it is not going to answer.
 *
 * Everything before the terminator is kept in `buf` so the caller can
 * pick out a +TAG: line. Async events can arrive at any time — the
 * protocol says so — which is the other reason this gathers rather than
 * matching the first line. */
static bool esp_collect(uart_inst_t *u, char *buf, unsigned len,
                        unsigned timeout_ms) {
    unsigned n = 0;
    buf[0] = '\0';

    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        if (!uart_is_readable(u)) {
            tight_loop_contents();
            continue;
        }
        const char c = (char)uart_getc(u);
        if (n + 1 < len) { buf[n++] = c; buf[n] = '\0'; }

        /* Terminators, checked on every byte so the loop leaves as soon
         * as the exchange is over rather than waiting out the timeout. */
        if (n >= 4 && strstr(buf, "OK\r\n"))    return true;
        if (n >= 6 && strstr(buf, "ERROR"))     return true;
    }
    return n > 0;      /* something arrived, just not a terminator */
}

/* One line's worth of a +TAG: value, copied out without its prefix. */
static bool esp_tag(const char *buf, const char *tag, char *out, unsigned len) {
    const char *p = strstr(buf, tag);
    if (!p) return false;
    p += strlen(tag);

    unsigned i = 0;
    while (*p && *p != '\r' && *p != '\n' && i + 1 < len) out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

static ui_test_state_t t_esp01(const detect_result_t *d, char *detail,
                               unsigned len, test_progress_fn progress) {
    esp_pins_t p;
    if (!d->board || !pins_from(&d->board->pins, &p)) {
        snprintf(detail, len, "no ESP UART pins");
        return TEST_NORUN;
    }
    if (!esp_uart_open(&p)) {
        snprintf(detail, len, "GP%d/%d is not a UART TX/RX pair",
                 p.tx, p.rx);
        return TEST_NORUN;
    }

    char buf[192];

    /* A module that has just been powered emits +READY of its own accord,
     * and anything still in the FIFO from that would be read as the reply
     * to the first command. */
    esp_drain(p.uart);

    if (progress) progress(200, "AT");
    esp_send(p.uart, "AT");
    const bool answered = esp_collect(p.uart, buf, sizeof buf, ESP_REPLY_MS);

    if (!answered || !strstr(buf, "OK")) {
        esp_uart_close(&p);
        /* Silence and a dead module look identical from here, and most
         * of these boards ship with an empty socket. */
        if (answered) snprintf(detail, len, "replied, but no OK to AT");
        else          snprintf(detail, len, "no answer on GP%d/%d - fitted?",
                               p.tx, p.rx);
        return TEST_NORUN;
    }

    /* It is talking - and if these are the console's own pins, that is
     * the end of the console as an input device for this session.
     *
     * The two share one UART through U26, so every line the firmware
     * prints lands in the module's command parser, which answers each
     * one with "ERROR:UNKNOWN_CMD". Those replies come back up through
     * console_getkey(), which cannot tell a byte from a keypress, and
     * 'R' is the accelerator for Restart: the board reboots itself a
     * few seconds after any run that printed anything. It looked like
     * the link soak resetting the board, because run_all() does not
     * poll the keyboard and the flood was only read once it returned.
     *
     * Muting it here rather than for the whole board is deliberate: a
     * module only answers when JP2's UART_SEL gives the mux to this
     * side, and when it does not, a terminal on J2 is a real keyboard
     * and should keep working. */
    if (p.is_console) {
        ui_input_set_console_keys(false);
        printf("[esp] module answers on the console UART (GP%d/%d) - "
               "console keys off for this session\n", p.tx, p.rx);
    }

    /* Everything from here is detail, and a module that answers AT but
     * not AT+VER is running something other than frank-netcard — worth reporting as a pass with a caveat rather
     * than a failure, because the socket and the UART are both proven. */
    char ver[48] = "", heap[24] = "";

    if (progress) progress(500, "AT+VER");
    esp_drain(p.uart);
    esp_send(p.uart, "AT+VER");
    if (esp_collect(p.uart, buf, sizeof buf, ESP_REPLY_MS)) esp_tag(buf, "+VER:", ver, sizeof ver);

    if (ver[0]) {
        /* frank-netcard. It has a heap query too, and free heap is worth
         * knowing before anything opens a TLS socket. */
        if (progress) progress(800, "AT+HEAP");
        esp_drain(p.uart);
        esp_send(p.uart, "AT+HEAP");
        if (esp_collect(p.uart, buf, sizeof buf, ESP_REPLY_MS))
            esp_tag(buf, "+HEAP:", heap, sizeof heap);
    } else {
        /* Espressif's stock AT firmware. AT+GMR answers with four
         * unprefixed lines; the first is the one worth the row's width. */
        if (progress) progress(800, "AT+GMR");
        esp_drain(p.uart);
        esp_send(p.uart, "AT+GMR");
        if (esp_collect(p.uart, buf, sizeof buf, ESP_REPLY_MS))
            esp_tag(buf, "AT version:", ver, sizeof ver);
    }

    esp_uart_close(&p);
    if (progress) progress(1000, NULL);

    if (ver[0] && heap[0])  snprintf(detail, len, "%s, heap %s", ver, heap);
    else if (ver[0])        snprintf(detail, len, "%s", ver);
    else                    snprintf(detail, len, "answers AT, version unknown");

    return TEST_PASS;
}

const frank_test_t frank_tests_esp[] = {
    /* Named for the part rather than for the module, because on
     * frank_next there is no module. */
    { "ESP 8266", ICON_CHIP_SMALL, CAP_ESP01, 0, t_esp01 },
};

const unsigned frank_tests_esp_len =
    sizeof(frank_tests_esp) / sizeof(frank_tests_esp[0]);
