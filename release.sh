#!/usr/bin/env bash
#
# FRANK universal test firmware
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# https://github.com/rh1tech/frank-test
# SPDX-License-Identifier: GPL-3.0-or-later
#
# release.sh — build the release UF2s and stamp the version.
#
# Two images come out of this, and both are needed to test a Core 2 or a
# Core 2U completely:
#
#   rp2350a  the test firmware for the 30-GPIO package
#   rp2350b  the test firmware for the 48-GPIO package
#   slave    the link peer. The inter-processor link cannot be tested from
#            one end: "Processor link" and "Slave reset" report no answer
#            unless the other chip is running this.
#
# The first two are the same program. Which one a board takes depends
# only on the silicon in it, and the README has the table.
#
# Usage: ./release.sh [VERSION]
#   VERSION   e.g. "1.00". Prompted with the next minor if omitted.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Locates the SDK, and rejects a stale PICO_SDK_PATH rather than trusting
# it — an exported path to a moved checkout is the usual cause of a
# CMake failure that looks like a broken build script.
# shellcheck source=sdk_env.sh
source ./sdk_env.sh

GREEN='\033[0;32m'; RED='\033[0;31m'; CYAN='\033[0;36m'; NC='\033[0m'

VERSION_FILE="version.txt"

# ---- work out the version -------------------------------------------
LAST_MAJOR=1; LAST_MINOR=0
if [[ -f "$VERSION_FILE" ]]; then
    read -r LAST_MAJOR LAST_MINOR < "$VERSION_FILE"
fi
NEXT_MAJOR=$((10#$LAST_MAJOR))
NEXT_MINOR=$((10#$LAST_MINOR + 1))
if [[ $NEXT_MINOR -ge 100 ]]; then NEXT_MAJOR=$((NEXT_MAJOR + 1)); NEXT_MINOR=0; fi
DEFAULT_VERSION="${NEXT_MAJOR}.$(printf '%02d' $NEXT_MINOR)"

if [[ $# -ge 1 ]]; then
    INPUT_VERSION="$1"
else
    read -r -p "Version [default: $DEFAULT_VERSION]: " INPUT_VERSION
    INPUT_VERSION="${INPUT_VERSION:-$DEFAULT_VERSION}"
fi

MAJOR="${INPUT_VERSION%%.*}"
MINOR="${INPUT_VERSION##*.}"
MAJOR=$((10#$MAJOR)); MINOR=$((10#$MINOR))
if [[ $MAJOR -lt 0 || $MINOR -lt 0 || $MINOR -ge 100 ]]; then
    echo -e "${RED}Version must be MAJOR.MM with MM in 00-99${NC}" >&2
    exit 1
fi

VERSION_DOT="${MAJOR}.$(printf '%02d' $MINOR)"
VERSION_US="${MAJOR}_$(printf '%02d' $MINOR)"

echo -e "${GREEN}Building release ${VERSION_DOT}${NC}"

# Written before the build, not after: the firmware reads this file at
# configure time to stamp its own banner, so the binary and the filename
# can never disagree about what they are.
echo "$MAJOR $MINOR" > "$VERSION_FILE"

RELEASE_DIR="$SCRIPT_DIR/release"
mkdir -p "$RELEASE_DIR"

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

SUCCEEDED=(); FAILED=()

build_one() {
    local label="$1" src="$2" builddir="$3" board="$4" target="$5" out="$6"

    echo ""
    echo "────────────────────────────────────────────────────────────"
    echo -e "${CYAN}Building: $out${NC}"

    rm -rf "$builddir"
    if cmake -S "$src" -B "$builddir" -DPICO_BOARD="$board" >/dev/null 2>&1 \
       && cmake --build "$builddir" -j"$JOBS" >/dev/null 2>&1 \
       && [[ -f "$builddir/${target}.uf2" ]]; then
        cp "$builddir/${target}.uf2" "$RELEASE_DIR/$out"
        echo -e "  ${GREEN}ok${NC} → release/$out"
        SUCCEEDED+=("$out")
    else
        echo -e "  ${RED}failed${NC}: $label"
        FAILED+=("$label")
    fi
}

# Three images, and the split is by silicon rather than by board.
#
# The test firmware is one program: it works out which board it is on at
# run time, so the only thing a build has to get right is the package.
# Every RP2350A target takes the same image and every RP2350B target
# takes the same image. Flashing the wrong one does not misbehave subtly
# — it hard-faults on the first access to a GPIO the package does not
# have — so the names say the package, not a board.
#
# The slave image is a different program: the link peer that the Core 2
# and Core 2U masters talk to. It is RP2350A, but it is listed separately
# because you flash it for a different reason.
build_one "rp2350a" app   app/build-rel-a   frank_a \
          frank-test           "frank-test_${VERSION_US}_rp2350a.uf2"
build_one "rp2350b" app   app/build-rel-b   frank_b \
          frank-test           "frank-test_${VERSION_US}_rp2350b.uf2"
build_one "slave"   slave slave/build-release frank_core2u_slave \
          frank-core2u-slave   "frank-test_${VERSION_US}_slave.uf2"

echo ""
echo "────────────────────────────────────────────────────────────"
for f in "${SUCCEEDED[@]:-}"; do [[ -n "$f" ]] && echo -e "  ${GREEN}✓${NC} $f"; done
for f in "${FAILED[@]:-}";    do [[ -n "$f" ]] && echo -e "  ${RED}✗${NC} $f"; done
[[ ${#FAILED[@]} -eq 0 ]] || exit 1

echo ""
echo -e "${GREEN}Release ${VERSION_DOT} in release/${NC}"
