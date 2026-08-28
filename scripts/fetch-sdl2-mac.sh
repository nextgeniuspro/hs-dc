#!/bin/sh
# Fetch the official universal SDL2.framework, for the macOS app bundle.
#
# The desktop build normally links whatever SDL2 the machine has -- Homebrew's,
# usually -- and that is right for developing on it and wrong for shipping it.
# A Homebrew SDL2 is one architecture, lives at an absolute path (/usr/local on
# an Intel Mac, /opt/homebrew on an Apple Silicon one), and on a machine
# without Homebrew is not there at all. A binary linked against it does not
# travel: hand an Intel-built one to an M4 and dyld cannot even find the
# library, never mind run the x86_64 code in it.
#
# libsdl.org's own release is a *universal* framework -- arm64 and x86_64 in
# one file -- with an install name of @rpath/..., which is exactly what an
# embedded framework needs. So the bundle links this instead:
#
#   scripts/fetch-sdl2-mac.sh                 puts it in third_party/mac/
#   cmake -S . -B build-mac -DBB_MAC_BUNDLE=ON
#   cmake --build build-mac -j                -> build-mac/Blackbeard.app
#
# It is not committed -- eleven megabytes of somebody else's binary has no
# business in this repo, which holds no binaries at all -- so this fetches it,
# and says what it verified.
set -eu

VERSION="${BB_SDL2_VERSION:-2.32.10}"
DEST="$(cd "$(dirname "$0")/.." && pwd)/third_party/mac"
DMG_URL="https://github.com/libsdl-org/SDL/releases/download/release-${VERSION}/SDL2-${VERSION}.dmg"

if [ -d "$DEST/SDL2.framework" ] && [ "${1:-}" != "--force" ]; then
    echo "fetch-sdl2-mac: already at $DEST/SDL2.framework (--force to replace)"
    exit 0
fi

WORK="$(mktemp -d)"
# The disk image is always detached and the scratch directory always removed,
# including on the way out of a failure -- a mounted volume left behind is the
# one piece of litter a script like this can leave that a person has to clear
# up by hand.
MOUNT=""
cleanup() {
    [ -n "$MOUNT" ] && hdiutil detach "$MOUNT" -quiet 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

echo "fetch-sdl2-mac: downloading SDL2 $VERSION"
curl -fsSL "$DMG_URL" -o "$WORK/SDL2.dmg"

MOUNT="$WORK/mnt"
mkdir -p "$MOUNT"
hdiutil attach "$WORK/SDL2.dmg" -mountpoint "$MOUNT" -nobrowse -quiet -readonly

mkdir -p "$DEST"
rm -rf "$DEST/SDL2.framework"
# -R, not -r: a framework is a bundle of symlinks around one real version, and
# following them would flatten it into something the linker does not recognise.
cp -R "$MOUNT/SDL2.framework" "$DEST/SDL2.framework"

BINARY="$DEST/SDL2.framework/Versions/A/SDL2"
echo "fetch-sdl2-mac: $DEST/SDL2.framework"
echo "fetch-sdl2-mac: $(lipo -archs "$BINARY")"
echo "fetch-sdl2-mac: install name $(otool -D "$BINARY" | tail -1)"

# Both slices, or the bundle is not universal however it is built. Said here
# rather than discovered later, when the only symptom is an app that will not
# open on somebody else's Mac.
case "$(lipo -archs "$BINARY")" in
    *arm64*x86_64* | *x86_64*arm64*) ;;
    *) echo "fetch-sdl2-mac: NOT universal -- expected arm64 and x86_64" >&2
       exit 1 ;;
esac
