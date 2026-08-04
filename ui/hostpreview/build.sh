#!/bin/bash
#
# FRANK universal test firmware
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# build.sh — render the interface on the host and check the scanline
# expander, neither of which needs a board.
#
#   ./build.sh            build both, render, run the test
#   ./preview out.ppm 2   render at 2x for a closer look
#   ./preview out.ppm 1 3 render with menu 3 (Video) pulled down
#
set -euo pipefail
cd "$(dirname "$0")"

SRC="../ui_gfx.c ../ui_icons.c ../ui_window.c ../ui_menu.c ../ui_desktop.c
     ../../master/src/ui_font.c"
INC="-I.. -I../../master/src"

cc -O1 -Wall $INC -o preview preview.c $SRC
cc -O1 -Wall -Wno-unused-variable -Wno-unused-function -DPREVIEW_NO_MAIN $INC \
   -o expand_test expand_test.c preview.c $SRC

./preview desktop.ppm 1
./preview menu.ppm    1 3

# The expander is the one piece of the video path whose bugs are silent:
# a transposed pixel pair does not crash, it produces an image that looks
# almost right. Verify it here rather than on a monitor.
./expand_test

if command -v sips >/dev/null 2>&1; then
    for f in desktop menu expanded; do
        [ -f "$f.ppm" ] && sips -s format png "$f.ppm" --out "$f.png" >/dev/null
    done
    echo "wrote desktop.png menu.png expanded.png"
fi
