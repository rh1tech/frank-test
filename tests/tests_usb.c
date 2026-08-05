/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * tests_usb.c — the USB host, and the hub in front of it.
 *
 *
 * THE DATA WAS ALREADY THERE
 *
 * The interface depends on USB: it is how most of these boards get a
 * keyboard and a mouse. The host stack knew what had enumerated the
 * whole time and had no way to say so, so the one subsystem everything
 * else leans on was the one with no row of its own.
 *
 * It reports what is attached and what it is — vendor, product and kind
 * — which proves the connector, the host controller, the enumeration and
 * the device, in the sense that none of those can be true separately.
 *
 *
 * WHY AN EMPTY PORT IS NOT A FAILURE
 *
 * Nothing has to be plugged in. A rig driven from a PS/2 keyboard, or
 * over the serial console, is a perfectly ordinary way to use this
 * firmware, so an empty port reports "could not run" rather than
 * failing. It does mean a dead port and an empty one look the same from
 * here, which the row says rather than hides.
 *
 *
 * THE HUB PROVES ITSELF
 *
 * Core 2U puts a hub between the socket and the controller, so there is
 * no such thing as a device that enumerated without passing through it.
 * Anything at all in the inventory is therefore proof the hub powered
 * up, enumerated and passed a device along — which is a stronger result
 * than a row that only says "a hub is fitted", and it costs nothing
 * beyond noticing that it follows.
 *
 * The mux is a different matter and stays untestable: it is selected by
 * a switch nothing can read, so the firmware can say which side it sees
 * and never which side is chosen.
 */

#include "registry.h"

#include "board_desc.h"
#include "detect.h"
#include "usbhid.h"

#include <stdio.h>

/* The interface only builds the host when it is compiled in; without it
 * these rows would report an absence that is this firmware's doing
 * rather than the board's. */
#ifndef UI_INPUT_USB_HID
#define UI_INPUT_USB_HID 1
#endif

static const char *kind_name(uint8_t kind) {
    switch (kind) {
        case USBHID_KIND_KEYBOARD: return "keyboard";
        case USBHID_KIND_MOUSE:    return "mouse";
        default:                   return "HID";
    }
}

static ui_test_state_t t_usb_host(const detect_result_t *d, char *detail,
                                  unsigned len, test_progress_fn p) {
    (void)d; (void)p;

#if !UI_INPUT_USB_HID
    snprintf(detail, len, "host not built into this image");
    return TEST_NORUN;
#else
    const int n = usbhid_device_count();
    if (n <= 0) {
        snprintf(detail, len, "nothing enumerated - device fitted?");
        return TEST_NORUN;
    }

    /* The first device by name, and a count for the rest. A composite
     * keyboard mounts more than once, so the count is interfaces rather
     * than things you can hold, and the row says "interface" to avoid
     * claiming otherwise. */
    usbhid_device_t info;
    if (!usbhid_device_info(0, &info)) {
        snprintf(detail, len, "%d interface%s", n, n == 1 ? "" : "s");
        return TEST_PASS;
    }

    if (n == 1)
        snprintf(detail, len, "%04X:%04X %s", info.vid, info.pid,
                 kind_name(info.kind));
    else
        snprintf(detail, len, "%04X:%04X %s +%d more", info.vid, info.pid,
                 kind_name(info.kind), n - 1);
    return TEST_PASS;
#endif
}

/* Every device on this board is behind the hub, so any device at all is
 * the hub working. Nothing needs to interrogate it. */
static ui_test_state_t t_usb_hub(const detect_result_t *d, char *detail,
                                 unsigned len, test_progress_fn p) {
    (void)d; (void)p;

#if !UI_INPUT_USB_HID
    snprintf(detail, len, "host not built into this image");
    return TEST_NORUN;
#else
    const int n = usbhid_device_count();
    if (n <= 0) {
        snprintf(detail, len, "nothing through it yet - device fitted?");
        return TEST_NORUN;
    }
    snprintf(detail, len, "passed %d interface%s through", n, n == 1 ? "" : "s");
    return TEST_PASS;
#endif
}

const frank_test_t frank_tests_usb[] = {
    { "USB host", ICON_USB, CAP_USB_HOST, 0, t_usb_host },
    { "USB hub",  ICON_USB, CAP_USB_HUB,  0, t_usb_hub  },
};

const unsigned frank_tests_usb_len =
    sizeof(frank_tests_usb) / sizeof(frank_tests_usb[0]);
