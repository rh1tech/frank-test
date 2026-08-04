#
# FRANK universal test firmware — interface layer
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Two libraries, and the split is load-bearing.
#
#   frank_ui        pure C, no SDK, no hardware. ui/hostpreview compiles
#                   these same sources for the host and renders the
#                   interface to an image in under a second. Adding a
#                   pico_stdlib call here ends that, so don't.
#
#   frank_ui_video  the backends. Target-only by definition.

add_library(frank_ui STATIC
    ui_gfx.c
    ui_icons.c
    ui_window.c
    ui_menu.c
    ui_desktop.c
    ${CMAKE_CURRENT_LIST_DIR}/../master/src/ui_font.c
)

target_include_directories(frank_ui PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../master/src
)

target_compile_options(frank_ui PRIVATE -O2 -Wall -Wno-unused-parameter)

# ---------------------------------------------------------------------

add_library(frank_ui_video STATIC
    ui_video.c
    ui_video_vga.c
    ui_video_tv.c
    ui_cursor.c
    ui_input.c
)

# USB HID host: keyboard and mouse. The RP2350's native USB controller is
# either a host or a device, never both, so enabling this is what rules
# out a USB CDC console — the console is UART. That is the product
# direction anyway: these boards exist to talk to keyboards and mice, and
# a test rig that cannot test them is missing the point.
option(UI_INPUT_USB_HID "USB HID host for keyboard and mouse" ON)
if(UI_INPUT_USB_HID)
    # The driver gates its own sources on this, and warns that USB CDC
    # stdio must be off when it is set.
    set(USB_HID_ENABLED ON CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../drivers/usbhid usbhid)

    # Headers here; the sources are INTERFACE and must compile into the
    # executable, which links usbhid itself — the same arrangement as
    # pico_hdmi above, and for the same reason.
    target_compile_definitions(frank_ui_video PUBLIC UI_INPUT_USB_HID=1)
    target_include_directories(frank_ui_video PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../drivers/usbhid
        ${CMAKE_CURRENT_LIST_DIR}/../drivers)
else()
    target_compile_definitions(frank_ui_video PUBLIC UI_INPUT_USB_HID=0)
endif()

target_sources(frank_ui_video PRIVATE ui_video_hstx.c)

# Headers only. pico_hdmi is an INTERFACE library, so linking it here
# would compile its sources into this static library as well as into the
# executable that also links it — two copies of the scanout core and a
# duplicate-symbol link failure.
target_include_directories(frank_ui_video PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../drivers)

target_include_directories(frank_ui_video PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(frank_ui_video
    frank_ui
    frank_core
    pico_stdlib
    pico_multicore      # the HSTX scanout owns core 1
    hardware_clocks
    hardware_watchdog
)

target_compile_options(frank_ui_video PRIVATE -O2 -Wall -Wno-unused-parameter)
