#!/usr/bin/env bash
#
# Build a bootable Dreamcast disc image: build-dc/blackbeard.cdi.
#
#   scripts/dc-cdi.sh                          find data.pak in the repo
#   scripts/dc-cdi.sh --pak /path/to/data.pak
#   scripts/dc-cdi.sh --out ~/blackbeard.cdi
#   scripts/dc-cdi.sh --iso                    also dump the data track
#   scripts/dc-cdi.sh --pad                    pad the track out for CD-R
#
# The disc holds four things: the game binary as 1ST_READ.BIN (mkdcdisc makes
# it from the ELF, scrambling and all), the original DATA.PAK, the port's
# little ICONS.PAK of soft-key hints, and FRAMES.DCF -- the device frames cut
# for this screen by mkframes.py, so the console decodes no images at all. The pak stays whole and compressed --
# 25 MB of one file the game seeks around in, exactly as it did on the phone's
# flash -- so nothing about the data layout changes for the console.
#
# .cdi is DiscJuggler's format: what emulators load and what burning software
# writes to a CD-R. mkdcdisc lays out the data track and adds the IP.BIN boot
# header.
#
# Padding is off unless --pad is given. mkdcdisc pads to the full 700 MB by
# default, which puts the game out at the disc's edge where a real drive reads
# fastest -- worth it for a CD-R, and 700 MB of nothing to move around for an
# emulator, which reads a file. Padded, this image is ~708 MB; unpadded, ~32.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(dirname "$here")"

pak=""
icons=""
out=""
name="Blackbeard"
author="RedLynx"
dump_iso=0
build_first=1
pad=0

while [ $# -gt 0 ]; do
    case "$1" in
        --pak) pak="$2"; shift 2 ;;
        --icons) icons="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --name) name="$2"; shift 2 ;;
        --iso) dump_iso=1; shift ;;
        --pad) pad=1; shift ;;
        --no-build) build_first=0; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "dc-cdi: unknown argument $1" >&2; exit 2 ;;
    esac
done

# shellcheck disable=SC1091
. "$here/dc-env.sh"

build="${BB_DC_BUILD:-$repo/build-dc}"
elf="$build/blackbeard.elf"
out="${out:-$build/blackbeard.cdi}"

if [ ! -f "$elf" ] && [ "$build_first" -eq 1 ]; then
    echo "dc-cdi: no binary yet, building it"
    "$here/dc-build.sh"
fi
if [ ! -f "$elf" ]; then
    echo "dc-cdi: no $elf -- run scripts/dc-build.sh first" >&2
    exit 1
fi

# The pak is the game the port reads; it is not in the repository, so this
# looks where an extracted N-Gage install puts it and takes a path otherwise.
if [ -z "$pak" ]; then
    for candidate in \
        "${BB_PAK:-}" \
        "$repo/data.pak"; do
        [ -n "$candidate" ] && [ -f "$candidate" ] && { pak="$candidate"; break; }
    done
fi
if [ -z "$pak" ] || [ ! -f "$pak" ]; then
    echo "dc-cdi: no data.pak found -- pass --pak /path/to/data.pak" >&2
    exit 1
fi
# The console's own soft-key icons: the same names, drawn as a Dreamcast pad
# rather than an Xbox one (tools/makeicons.py --style dc). The game asks for
# `pad_a.tc` and the pak it was given decides whose button that is, so nothing
# in the game knows which machine it is running on.
if [ -z "$icons" ]; then
    icons="$repo/assets/icons-dc.pak"
    [ -f "$icons" ] || icons="$repo/assets/icons.pak"
fi

if ! command -v mkdcdisc >/dev/null 2>&1; then
    echo "dc-cdi: mkdcdisc not on PATH (looked in $DC_TOOLS_BASE)" >&2
    echo "dc-cdi: get it from https://gitlab.com/simulant/mkdcdisc" >&2
    exit 1
fi

# The disc root, assembled fresh each time so a renamed or removed asset
# cannot linger in an image from a previous build. ISO9660 hands names back
# uppercase, so they go on uppercase.
stage="$build/disc"
rm -rf "$stage"
mkdir -p "$stage"
cp "$pak" "$stage/DATA.PAK"
[ -f "$icons" ] && cp "$icons" "$stage/ICONS.PAK"

# The device frames, cut for a 640x480 screen. Needs Pillow; without it the
# disc simply carries no frames and the sides of the screen stay black.
if [ -d "$repo/assets/frames" ]; then
    if python3 -c "import PIL" >/dev/null 2>&1; then
        python3 "$here/mkframes.py" "$repo/assets/frames" "$stage/FRAMES.DCF"
    else
        echo "dc-cdi: no Pillow, skipping device frames (pip install pillow)"
    fi
fi

echo "dc-cdi: binary $elf"
echo "dc-cdi: data   $pak ($(du -h "$pak" | cut -f1))"
echo "dc-cdi: output $out"

args=(-e "$elf" -o "$out" -n "$name" -a "$author" -D "$stage" -v 2)
[ "$dump_iso" -eq 1 ] && args+=(-I)
[ "$pad" -eq 0 ] && args+=(-N)

mkdcdisc "${args[@]}"

echo "dc-cdi: wrote $out ($(du -h "$out" | cut -f1))"
echo "dc-cdi: run it with an emulator, or burn it to CD-R"
