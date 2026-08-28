#!/usr/bin/env bash
#
# Build the Dreamcast binary: build-dc/blackbeard.elf.
#
#   scripts/dc-build.sh                 release build
#   scripts/dc-build.sh -DCMAKE_BUILD_TYPE=Debug
#   BB_DC_BUILD=/tmp/dc scripts/dc-build.sh
#
# Any extra arguments are passed through to the CMake configure step. The
# resulting ELF is what scripts/dc-cdi.sh turns into a disc image, and what
# `dc-tool -x` uploads to a console over a coder's cable.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$here")"

# shellcheck disable=SC1091
. "$here/dc-env.sh"

build="${BB_DC_BUILD:-$repo/build-dc}"
config="${BB_DC_CONFIG:-Release}"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

echo "dc-build: KOS_BASE=$KOS_BASE"
echo "dc-build: KOS_PORTS=$KOS_PORTS"
echo "dc-build: build=$build ($config)"

cmake -S "$repo" -B "$build" \
    -DCMAKE_TOOLCHAIN_FILE="$KOS_CMAKE_TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE="$config" \
    "$@"

cmake --build "$build" -j"$jobs"
