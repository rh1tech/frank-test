#
# FRANK universal test firmware — core library
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# https://github.com/rh1tech/frank-test
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Board identification, video selection and the persisted settings
# record. Deliberately free of any dependency on a video driver, a USB
# stack or the link: those differ per target, and the core has to be
# linkable into all three MCU-class binaries unchanged.

add_library(frank_core STATIC
    board_table.c
    pinsig.c
    settings.c
    detect.c
    video_detect.c
    attest.c
    oc_request.c
    i2c_bb.c
    video_select.c
    video_request.c

    ${CMAKE_CURRENT_LIST_DIR}/../common/mem_test.c
    ${CMAKE_CURRENT_LIST_DIR}/../common/link_proto.c
)

target_sources(frank_core PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../drivers/psram_init.c)

target_include_directories(frank_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../common
    ${CMAKE_CURRENT_LIST_DIR}/../drivers
    # registry.h describes its results in the interface's own row type,
    # so the two share a header. The dependency is one way: nothing in
    # ui/ knows about tests.
    ${CMAKE_CURRENT_LIST_DIR}/../ui
    ${CMAKE_CURRENT_LIST_DIR}/../master/src
)

target_compile_options(frank_core PRIVATE
    -O2
    -Wall
    -Wno-unused-parameter
    -Wno-unused-function
)

target_link_libraries(frank_core
    pico_stdlib
    hardware_clocks
    hardware_flash
    hardware_gpio
    hardware_sync
    hardware_xip_cache
)
