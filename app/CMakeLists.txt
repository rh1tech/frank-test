#
# FRANK universal test firmware — application
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-3.0-or-later
#
cmake_minimum_required(VERSION 3.13)
set(FW_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(PICO_BOARD_HEADER_DIRS ${FW_ROOT}/boards CACHE PATH "" FORCE)
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "frank_core2_master" CACHE STRING "" FORCE)
endif()

# Version, from version.txt at the repo root: "MAJOR MINOR", rendered as
# MAJOR.MM. One file is the single source of truth for the banner, the
# About box and the release artefact names — release.sh writes it and
# everything else reads it, so a release cannot ship a binary whose
# reported version disagrees with its filename.
file(READ ${FW_ROOT}/version.txt _frank_ver_raw)
string(STRIP "${_frank_ver_raw}" _frank_ver_raw)
string(REPLACE " " ";" _frank_ver_list "${_frank_ver_raw}")
list(GET _frank_ver_list 0 _frank_major)
list(GET _frank_ver_list 1 _frank_minor)
string(LENGTH "${_frank_minor}" _frank_minor_len)
if(_frank_minor_len LESS 2)
    set(_frank_minor "0${_frank_minor}")
endif()
set(FRANK_VERSION "${_frank_major}.${_frank_minor}")
message(STATUS "FRANK test firmware version ${FRANK_VERSION}")

include(${FW_ROOT}/pico_sdk_import.cmake)
project(frank-test C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(${FW_ROOT}/drivers/pico_hdmi pico_hdmi)
add_subdirectory(${FW_ROOT}/core core)
add_subdirectory(${FW_ROOT}/ui   ui)

add_executable(frank-test
    main.c
    dlg_audio.c
    dlg_nespad.c
    dlg_led.c
    dlg_ps2.c
    dlg_tape.c
    ${FW_ROOT}/core/registry.c
    ${FW_ROOT}/tests/tests_memory.c
    ${FW_ROOT}/tests/tests_board.c
    ${FW_ROOT}/tests/tests_psram_spi.c
    ${FW_ROOT}/tests/tests_sd.c
    ${FW_ROOT}/tests/tests_esp.c
    ${FW_ROOT}/tests/tests_audio.c
    ${FW_ROOT}/tests/tests_link.c
    ${FW_ROOT}/tests/link_diag.c

    # The link stack: two PIO state machines, the wire protocol and the
    # session layer. Shared verbatim with the core2 firmwares.
    ${FW_ROOT}/common/link_bus.c
    ${FW_ROOT}/common/link_session.c

    # NES/SNES pad reader, from murmnes, moved onto pio2 — pio0 carries
    # the inter-processor link and pio1 the I2S audio.
    ${FW_ROOT}/drivers/nespad/nespad.c

    # PS/2 keyboard and mouse, vendored from cabal by way of frank-quest.
    ${FW_ROOT}/drivers/ps2/ps2.c

    # Analogue VGA out of the HSTX, raw mode. Shares GP12-19 and the two
    # DMA channels with pico_hdmi; only one of the two is ever brought up.
    ${FW_ROOT}/drivers/pico_vga/vga_output.c

    # Software composite PAL/NTSC, taken verbatim from frank-msx's
    # drivers/tv (itself from murmnes). See the compile flags below —
    # both -include shims are load-bearing, not tidiness.
    ${FW_ROOT}/drivers/tv/tv-software.c
    ${FW_ROOT}/drivers/tv/graphics.c
)

# The TV driver's public symbols collide with ours, and its scanline ISR
# calls memset() fifteen times per line at 30 kHz — which on RP2350 is a
# flash-resident memset and thrashes the XIP cache badly enough to
# starve core 0. tv_rename.h fixes the first, tv_mem.h the second. The
# -fno-* flags stop GCC's loop-idiom pass turning the hand-inlined SRAM
# fills back into calls to the very memset tv_mem.h is avoiding.
set_source_files_properties(
    ${FW_ROOT}/drivers/tv/tv-software.c
    ${FW_ROOT}/drivers/tv/graphics.c
    PROPERTIES
    COMPILE_FLAGS
    "-include ${FW_ROOT}/drivers/tv/tv_rename.h -include ${FW_ROOT}/drivers/tv/tv_mem.h -Wno-all -Wno-error -Wno-incompatible-pointer-types -fno-tree-loop-distribute-patterns -fno-builtin-memset -fno-builtin-memcpy"
)

pico_generate_pio_header(frank-test ${FW_ROOT}/common/link_bus.pio)
pico_generate_pio_header(frank-test ${FW_ROOT}/drivers/audio_i2s.pio)
pico_generate_pio_header(frank-test ${FW_ROOT}/drivers/ps2/ps2.pio)

target_include_directories(frank-test PRIVATE
    ${FW_ROOT}/tests
    ${FW_ROOT}/drivers/nespad
    ${FW_ROOT}/drivers/ps2
    ${FW_ROOT}/drivers/tv)

# Pairs with set_sys_clock_khz(252000) in main(): clk_hstx = clk_sys/2 =
# 126 MHz, which is what 640x480@60 TMDS needs. ui_video_hstx.c also
# forces it, so this is belt and braces rather than the only guard.
target_compile_definitions(frank-test PRIVATE
    MODE_HSTX_CLK_DIV=2
    FIRMWARE_VERSION="${FRANK_VERSION}"
)

target_compile_options(frank-test PRIVATE -O2 -Wall -Wno-unused-parameter)

# usbhid is an INTERFACE library whose sources must compile into the
# executable, so it is linked here rather than by frank_ui_video — and
# only when it exists, since UI_INPUT_USB_HID=OFF skips creating it.
if(TARGET usbhid)
    set(_hid usbhid)
endif()

target_link_libraries(frank-test
    frank_core frank_ui frank_ui_video pico_hdmi ${_hid}
    pico_stdlib hardware_pio hardware_dma hardware_pwm)

# UART only. The USB controller is now a HID *host* so keyboards and mice
# can be tested, and it cannot simultaneously enumerate as a CDC device.
# The cost is real — a board with no UART header broken out gives no log
# over a bare USB cable — and it is the right trade for a rig whose job
# includes testing the input devices.
# UART always. USB CDC only when the HID host is off — the controller is
# one or the other, never both. A board with no debug probe attached has
# no other console, so bring-up builds turn HID off and get the log back.
pico_enable_stdio_uart(frank-test 1)
if(UI_INPUT_USB_HID)
    pico_enable_stdio_usb(frank-test 0)
else()
    pico_enable_stdio_usb(frank-test 1)
    target_compile_definitions(frank-test PRIVATE
        PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=20000)
endif()

pico_add_extra_outputs(frank-test)

# Bring-up escape hatch: open HDMI directly, skipping video selection.
# Off unless asked for; see app/main.c.
if(FRANK_FORCE_BOARD)
    target_compile_definitions(frank-test PRIVATE FRANK_FORCE_BOARD=${FRANK_FORCE_BOARD})
endif()

if(FRANK_FORCE_VIDEO)
    target_compile_definitions(frank-test PRIVATE FRANK_FORCE_VIDEO=${FRANK_FORCE_VIDEO})
endif()

# Bring-up: hold the boot until a USB console attaches, so the log of an
# early failure is not lost before the host enumerates. Milliseconds.
if(FRANK_CONSOLE_WAIT)
    target_compile_definitions(frank-test PRIVATE
        PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=${FRANK_CONSOLE_WAIT})
endif()
