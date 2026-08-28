#!/usr/bin/env python3
"""Build FRAMES.DCF -- the device frames, cut for a Dreamcast's screen.

The desktop build loads `assets/frames/<id>/{left,right}.png` and scales them
per window; a console has one window and it never changes, so all of that can
happen here instead and the disc can carry pixels the PVR draws directly.

What this does, per frame half:

  * scales it to the height of the game field (480, the full screen height),
    keeping its aspect -- the same `screen_h * art_w / art_h` the desktop's
    ScreenLayout.h does, so both builds show the art at the same size;
  * crops it to the strip that is actually visible: the halves are butted
    against the field's edges and run off the sides of the screen, so only the
    117 columns next to the field survive -- the left half's rightmost, the
    right half's leftmost. Art narrower than that is kept whole and the rest
    of the side stays black, which is what the desktop does too;
  * composites it over black and writes RGB565. The alpha is spent here rather
    than carried: everything behind these is black, so a silhouette blended
    against black is the same picture with none of the cost. RGB565 also keeps
    a photograph out of the four-bits-a-channel banding the pak's own textures
    live with.

Rows are padded out to a power-of-two width, because that is what the PVR
wants a texture to be and what lets the console upload a half in one transfer
(the store queues move 32 bytes at a time, and 117 pixels is not a multiple of
them). The padding is never drawn: the visible width is recorded separately
and the quad's texture coordinates stop there.

File format (little-endian):

    "BBDF"  u16 version=1  u16 count  u16 height  u16 side
    count x { char id[16], char label[24], u16 left_w, u16 right_w,
              u16 tex_w, u16 pad, u32 left_offset, u32 right_offset }
                                                        -- 56 bytes each
    then the pixel blobs, tex_w*height RGB565 each, at the offsets above.

Usage: mkframes.py <frames-dir> <out.dcf>
"""

import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - a build-time convenience, not a test
    sys.exit("mkframes: needs Pillow (pip install pillow)")

# The console's screen and the game's, and therefore the space either side.
SCREEN_W, SCREEN_H = 640, 480
GAME_W, GAME_H = 176, 208
FIELD_W = SCREEN_H * GAME_W // GAME_H          # 406, aspect kept
SIDE = (SCREEN_W - FIELD_W) // 2               # 117 each side

ID_MAX, LABEL_MAX, RECORD = 16, 24, 56


def pot(n):
    """The power of two a texture of `n` pixels across has to live in."""
    size = 8
    while size < n:
        size *= 2
    return size


def read_manifest(path):
    """The same `<id> TAB <label>` manifest the desktop reads, in its order."""
    frames = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t", 1) if "\t" in line else line.split(None, 1)
        ident = parts[0].strip()
        label = parts[1].strip() if len(parts) > 1 else ident
        frames.append((ident, label))
    return frames


def to_rgb565(img, tex_w):
    """Composite over black, pad each row to `tex_w`, pack to RGB565 bytes."""
    img = img.convert("RGBA")
    black = Image.new("RGBA", (tex_w, img.height), (0, 0, 0, 255))
    black.paste(img, (0, 0), img)
    raw = black.convert("RGB").tobytes()
    out = bytearray(len(raw) // 3 * 2)
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        struct.pack_into("<H", out, i // 3 * 2,
                         ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    return bytes(out)


def cut(path, keep_right):
    """One half, scaled to the field's height and cropped to the visible strip.

    `keep_right` selects which edge meets the game: the left half of a device
    meets it with its right column, the right half with its left one.
    """
    img = Image.open(path)
    w = SCREEN_H * img.width // img.height
    if w <= 0:
        return 0, b""
    img = img.resize((w, SCREEN_H), Image.LANCZOS)
    if w > SIDE:
        img = img.crop((w - SIDE, 0, w, SCREEN_H) if keep_right
                       else (0, 0, SIDE, SCREEN_H))
        w = SIDE
    return w, img


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__.strip().splitlines()[-1])
    src, out = Path(argv[1]), Path(argv[2])

    manifest = src / "frames.txt"
    if not manifest.is_file():
        sys.exit(f"mkframes: no manifest at {manifest}")

    records, blobs, offset = [], [], 0
    for ident, label in read_manifest(manifest):
        left_png, right_png = src / ident / "left.png", src / ident / "right.png"
        if not left_png.is_file() or not right_png.is_file():
            print(f"mkframes: {ident}: art missing, skipped")
            continue
        left_w, left_img = cut(left_png, keep_right=True)
        right_w, right_img = cut(right_png, keep_right=False)
        # Both halves share one texture width, so the console can keep one
        # texture per side and swap frames into them.
        tex_w = pot(max(left_w, right_w))
        left = to_rgb565(left_img, tex_w)
        right = to_rgb565(right_img, tex_w)
        records.append((ident, label, left_w, right_w, tex_w, len(left),
                        len(right)))
        blobs += [left, right]
        print(f"mkframes: {ident:<12} {left_w}x{SCREEN_H} + {right_w}x{SCREEN_H}"
              f"  in {tex_w}px textures  {(len(left) + len(right)) // 1024} KB")

    if not records:
        sys.exit("mkframes: no frames to write")

    body = 12 + RECORD * len(records)
    header = struct.pack("<4sHHHH", b"BBDF", 1, len(records), SCREEN_H, SIDE)
    table, offset = b"", body
    for ident, label, left_w, right_w, tex_w, left_len, right_len in records:
        table += struct.pack(
            f"<{ID_MAX}s{LABEL_MAX}sHHHHII",
            ident.encode()[:ID_MAX - 1], label.encode()[:LABEL_MAX - 1],
            left_w, right_w, tex_w, 0, offset, offset + left_len)
        offset += left_len + right_len

    out.write_bytes(header + table + b"".join(blobs))
    print(f"mkframes: {out} ({out.stat().st_size // 1024} KB, "
          f"{len(records)} frames)")


if __name__ == "__main__":
    main(sys.argv)
