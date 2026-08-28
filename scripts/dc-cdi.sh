#!/usr/bin/env bash
#
# Build a bootable Dreamcast disc image: build-dc/blackbeard.cdi.
#
#   scripts/dc-cdi.sh                          find data.pak in the repo
#   scripts/dc-cdi.sh --pak /path/to/data.pak
#   scripts/dc-cdi.sh --no-pak                 leave the game data off
#   scripts/dc-cdi.sh --out ~/blackbeard.cdi
#   scripts/dc-cdi.sh --iso                    also dump the data track
#   scripts/dc-cdi.sh --pad                    pad the track out for CD-R
#
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
no_pak=0

while [ $# -gt 0 ]; do
    case "$1" in
        --pak) pak="$2"; shift 2 ;;
        --no-pak) no_pak=1; shift ;;
        --icons) icons="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --name) name="$2"; shift 2 ;;
        --iso) dump_iso=1; shift ;;
        --pad) pad=1; shift ;;
        --no-build) build_first=0; shift ;;
        -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
        *) echo "dc-cdi: unknown argument $1" >&2; exit 2 ;;
    esac
done

# Asking for both is a contradiction, and guessing which one was meant is how
# somebody ends up handing out a disc with the game data on it.
if [ "$no_pak" -eq 1 ] && [ -n "$pak" ]; then
    echo "dc-cdi: --pak and --no-pak ask for opposite discs" >&2
    exit 2
fi

# shellcheck disable=SC1091
. "$here/dc-env.sh"

build="${BB_DC_BUILD:-$repo/build-dc}"
elf="$build/blackbeard.elf"
if [ "$no_pak" -eq 1 ]; then
    out="${out:-$build/blackbeard-sd.cdi}"
else
    out="${out:-$build/blackbeard.cdi}"
fi

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
if [ "$no_pak" -eq 0 ]; then
    if [ -z "$pak" ]; then
        for candidate in \
            "${BB_PAK:-}" \
            "$repo/data.pak"; do
            [ -n "$candidate" ] && [ -f "$candidate" ] && { pak="$candidate"; break; }
        done
    fi
    if [ -z "$pak" ] || [ ! -f "$pak" ]; then
        echo "dc-cdi: no data.pak found -- pass --pak /path/to/data.pak," >&2
        echo "dc-cdi: or --no-pak for a disc that reads one off an SD card" >&2
        exit 1
    fi
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
[ "$no_pak" -eq 0 ] && cp "$pak" "$stage/DATA.PAK"
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
if [ "$no_pak" -eq 1 ]; then
    echo "dc-cdi: data   none -- the disc reads it off an SD card"
else
    echo "dc-cdi: data   $pak ($(du -h "$pak" | cut -f1))"
fi
echo "dc-cdi: output $out"

args=(-e "$elf" -o "$out" -n "$name" -a "$author" -D "$stage" -v 2)
[ "$dump_iso" -eq 1 ] && args+=(-I)
[ "$pad" -eq 0 ] && args+=(-N)

mkdcdisc "${args[@]}"

echo "dc-cdi: wrote $out ($(du -h "$out" | cut -f1))"
echo "dc-cdi: run it with an emulator, or burn it to CD-R"
if [ "$no_pak" -eq 1 ]; then
    echo "dc-cdi: this disc has no game data on it. Whoever plays it needs"
    echo "dc-cdi: their own data.pak on an SD card in the serial port, as"
    echo "dc-cdi:   /data.pak   or   /hs/data.pak"
    echo "dc-cdi: FAT-formatted. Either case of the name will do."
fi
