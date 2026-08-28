# KallistiOS environment for the Dreamcast build. Sourced, not run.
#
# Everything the SH4 toolchain needs comes out of KOS's own environ.sh -- the
# compiler prefix, the include and library paths, the CMake toolchain file. Two
# things this adds:
#
#   * KOS_PORTS. A stock environ.sh leaves it empty, and zlib -- which the pak
#     reader needs, and which every Dreamcast install has under kos-ports --
#     lives there. Empty also makes KOS's own CMake toolchain file fail
#     outright: it calls file(REAL_PATH) on it.
#   * DC_TOOLS_BASE on the PATH, so mkdcdisc is findable by name.
#
# Override BB_KOS_ENVIRON to point at a different install.
BB_KOS_ENVIRON="${BB_KOS_ENVIRON:-/opt/toolchains/dc/kos/environ.sh}"

if [ ! -f "$BB_KOS_ENVIRON" ]; then
    echo "dc-env: no KallistiOS environment at $BB_KOS_ENVIRON" >&2
    echo "dc-env: set BB_KOS_ENVIRON to your kos/environ.sh" >&2
    return 1 2>/dev/null || exit 1
fi

# environ.sh reads optional variables that may be unset, so `set -u` -- which
# the calling scripts run with -- has to stand down for the duration.
case $- in
    *u*) bb_restore_u=1 ;;
    *)   bb_restore_u=0 ;;
esac
set +u
# "pristine" is a console rather than a NAOMI board; KOS's CMake toolchain
# file insists the variable exists either way.
: "${KOS_SUBARCH:=pristine}"
export KOS_SUBARCH

# shellcheck disable=SC1090
. "$BB_KOS_ENVIRON"

if [ "$bb_restore_u" = 1 ]; then set -u; fi
unset bb_restore_u

# `:=` covers both unset and set-but-empty, which is how environ.sh ships it.
: "${KOS_PORTS:=${KOS_BASE}/../kos-ports}"
export KOS_PORTS
: "${DC_TOOLS_BASE:=/opt/toolchains/dc/bin}"
export DC_TOOLS_BASE
: "${KOS_CMAKE_TOOLCHAIN:=${KOS_BASE}/utils/cmake/kallistios.toolchain.cmake}"
export KOS_CMAKE_TOOLCHAIN
export PATH="${DC_TOOLS_BASE}:${PATH}"

if [ ! -f "$KOS_BASE/lib/$KOS_ARCH/libkallisti.a" ]; then
    echo "dc-env: KallistiOS is not built ($KOS_BASE/lib/$KOS_ARCH/libkallisti.a missing)" >&2
    echo "dc-env: build it with 'make' in $KOS_BASE" >&2
    return 1 2>/dev/null || exit 1
fi
