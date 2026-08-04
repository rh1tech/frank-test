#
# FRANK universal test firmware — core self-test target
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Smallest thing that links the core and exercises every entry point, so
# that "does it build and link for this MCU class" is answerable without
# a board attached. Not a product target.

cmake_minimum_required(VERSION 3.13)
set(FW_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(PICO_BOARD_HEADER_DIRS ${FW_ROOT}/boards CACHE PATH "" FORCE)

# Overridable so the same target can prove the core builds for all three
# MCU classes, which is the point of having it.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "frank_core2u_master" CACHE STRING "" FORCE)
endif()

include(${FW_ROOT}/pico_sdk_import.cmake)
project(frank-core-selftest C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(${FW_ROOT}/core core)
add_subdirectory(${FW_ROOT}/drivers/pico_hdmi pico_hdmi)
add_subdirectory(${FW_ROOT}/ui   ui)

add_executable(frank-core-selftest main.c)
# pico_hdmi is an INTERFACE library: its sources compile into whoever
# links it, so it has to be linked by the executable rather than by
# frank_ui_video, which only wants its headers.
target_link_libraries(frank-core-selftest
    frank_core frank_ui frank_ui_video pico_hdmi pico_stdlib)
# Pairs with set_sys_clock_khz(252000) in main.c: clk_hstx = clk_sys/2 =
# 126 MHz, which is what 640x480@60 TMDS needs. Changing either without
# the other produces a picture-free HDMI output that still reports
# success — see the comment in main().
target_compile_definitions(frank-core-selftest PRIVATE MODE_HSTX_CLK_DIV=2)

target_compile_options(frank-core-selftest PRIVATE -O2 -Wall)
# Bring-up console.
#
# The product firmware runs USB as a HID *host* so keyboards and mice can
# be tested, which means it cannot also enumerate as a serial device —
# its console is UART. This binary tests neither, so it can have CDC, and
# for a first power-on with nothing but a USB cable attached that is the
# difference between a report and a blank screen.
#
# Default ON here for exactly that reason. Turn it off to mirror the
# product's UART-only routing.
option(BRINGUP_USB_CDC "USB CDC console on the self-test binary" ON)

if(BRINGUP_USB_CDC)
    pico_enable_stdio_uart(frank-core-selftest 1)
    pico_enable_stdio_usb(frank-core-selftest  1)
    target_compile_definitions(frank-core-selftest PRIVATE
        # Emit stdout even when the host has not raised DTR: a bring-up
        # binary that discards its own log because the terminal attached a
        # second late is useless exactly when it matters.
        PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000)
else()
    pico_enable_stdio_uart(frank-core-selftest 1)
    pico_enable_stdio_usb(frank-core-selftest  0)
endif()

pico_add_extra_outputs(frank-core-selftest)
