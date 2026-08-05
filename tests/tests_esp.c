/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tests_esp.c — the ESP-01S socket.
 *
 *
 * WHAT IS BEING TESTED
 *
 * That a module in the socket answers over the UART. Two firmwares are
 * recognised, because both turn up in the field.
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
 *
 * WHY THERE IS NO RESET
 *
 * CH_PU and GPIO0 go to buttons on every ESP-01S board in the fleet, not
 * to GPIOs — see the note in board_desc.h. So the module cannot be
 * power-cycled or held in bootloader from firmware, and the test has to
 * work with whatever state it is already in. That is why it opens with a
 * bare AT rather than a reset: if the module is up, AT answers
 * immediately, and if it is wedged there is nothing this can do about
 * it. frank_next is the exception and is not this test — it carries an
 * ESP32 on SPI with real reset lines.
 *
 *
 * THE TWO PIN PAIRS
 *
 * GP20/21 on FRANK and miniFRANK, GP38/39 on FRANK PGA and MegaFRANK.
 * Both are UART1, but not by the same function number: GP20/21 are
 * UART1 TX/RX at FUNCSEL 2, while at FUNCSEL 2 GP38/39 are UART1 *CTS
 * and RTS* — TX/RX live at FUNCSEL 0x0b instead. So the SDK's
 * GPIO_FUNC_UART, which is 2, silently wires up flow control on half the
 * fleet and the module never hears a byte. The function is chosen per
 * pin below.
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
 */

#include "registry.h"

#include "board_desc.h"
#include "detect.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
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

/* UART1 on both pin pairs. UART0 is the console, and on the boards with
 * PS/2 it is the mouse. */
#define ESP_UART uart1

/* FUNCSEL 0x0b — UART1 TX/RX on the GP38/39 pair. The SDK has no name
 * for it because it is specific to the high pins on the B package. */
#define GPIO_FUNC_UART_HIGH 11u

typedef struct {
    int tx, rx;
} esp_pins_t;

static bool pins_from(const frank_pins_t *p, esp_pins_t *out) {
    if (p->esp_uart_tx == PIN_NC || p->esp_uart_rx == PIN_NC) return false;
    out->tx = p->esp_uart_tx;
    out->rx = p->esp_uart_rx;
    return true;
}

/* GP20/21 take FUNCSEL 2; GP38/39 take 0x0b. Anything else is a
 * descriptor this code has not been told about, and it says so rather
 * than picking one and producing a silent failure. */
static bool esp_uart_open(const esp_pins_t *p) {
    uint32_t fn_tx, fn_rx;

    if (p->tx <= 31 && p->rx <= 31) {
        fn_tx = fn_rx = GPIO_FUNC_UART;
    } else if (p->tx >= 32 && p->rx >= 32) {
        fn_tx = fn_rx = GPIO_FUNC_UART_HIGH;
    } else {
        return false;
    }

    uart_init(ESP_UART, ESP_BAUD);
    uart_set_format(ESP_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(ESP_UART, false, false);   /* the ESP-01S has none */
    uart_set_fifo_enabled(ESP_UART, true);

    gpio_set_function((uint)p->tx, (gpio_function_t)fn_tx);
    gpio_set_function((uint)p->rx, (gpio_function_t)fn_rx);
    return true;
}

/* Back to plain inputs. The gamepad reader expects to find its clock and
 * latch as GPIOs, and leaving them driven by a UART would make its next
 * read nonsense. */
static void esp_uart_close(const esp_pins_t *p) {
    uart_deinit(ESP_UART);
    gpio_init((uint)p->tx);
    gpio_set_dir((uint)p->tx, GPIO_IN);
    gpio_init((uint)p->rx);
    gpio_set_dir((uint)p->rx, GPIO_IN);
}

static void esp_drain(void) {
    while (uart_is_readable(ESP_UART)) (void)uart_getc(ESP_UART);
}

static void esp_send(const char *cmd) {
    uart_puts(ESP_UART, cmd);
    uart_puts(ESP_UART, "\r\n");
}

/* Collect reply lines until one of them is OK or ERROR, or until the
 * module has been quiet for long enough that it is not going to answer.
 *
 * Everything before the terminator is kept in `buf` so the caller can
 * pick out a +TAG: line. Async events can arrive at any time — the
 * protocol says so — which is the other reason this gathers rather than
 * matching the first line. */
static bool esp_collect(char *buf, unsigned len, unsigned timeout_ms) {
    unsigned n = 0;
    buf[0] = '\0';

    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        if (!uart_is_readable(ESP_UART)) {
            tight_loop_contents();
            continue;
        }
        const char c = (char)uart_getc(ESP_UART);
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
        snprintf(detail, len, "GP%d/%d is not a UART1 pair", p.tx, p.rx);
        return TEST_NORUN;
    }

    char buf[192];

    /* A module that has just been powered emits +READY of its own accord,
     * and anything still in the FIFO from that would be read as the reply
     * to the first command. */
    esp_drain();

    if (progress) progress(200, "AT");
    esp_send("AT");
    const bool answered = esp_collect(buf, sizeof buf, ESP_REPLY_MS);

    if (!answered || !strstr(buf, "OK")) {
        esp_uart_close(&p);
        /* Silence and a dead module look identical from here, and most
         * of these boards ship with an empty socket. */
        if (answered) snprintf(detail, len, "replied, but no OK to AT");
        else          snprintf(detail, len, "no answer on GP%d/%d - fitted?",
                               p.tx, p.rx);
        return TEST_NORUN;
    }

    /* It is talking. Everything from here is detail, and a module that
     * answers AT but not AT+VER is running something other than
     * frank-netcard — worth reporting as a pass with a caveat rather
     * than a failure, because the socket and the UART are both proven. */
    char ver[48] = "", heap[24] = "";

    if (progress) progress(500, "AT+VER");
    esp_drain();
    esp_send("AT+VER");
    if (esp_collect(buf, sizeof buf, ESP_REPLY_MS)) esp_tag(buf, "+VER:", ver, sizeof ver);

    if (ver[0]) {
        /* frank-netcard. It has a heap query too, and free heap is worth
         * knowing before anything opens a TLS socket. */
        if (progress) progress(800, "AT+HEAP");
        esp_drain();
        esp_send("AT+HEAP");
        if (esp_collect(buf, sizeof buf, ESP_REPLY_MS))
            esp_tag(buf, "+HEAP:", heap, sizeof heap);
    } else {
        /* Espressif's stock AT firmware. AT+GMR answers with four
         * unprefixed lines; the first is the one worth the row's width. */
        if (progress) progress(800, "AT+GMR");
        esp_drain();
        esp_send("AT+GMR");
        if (esp_collect(buf, sizeof buf, ESP_REPLY_MS))
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
    { "ESP-01", ICON_CHIP_SMALL, CAP_ESP01, 0, t_esp01 },
};

const unsigned frank_tests_esp_len =
    sizeof(frank_tests_esp) / sizeof(frank_tests_esp[0]);
