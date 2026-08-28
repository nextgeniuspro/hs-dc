// TC texture container — RedLynx ".tc" format (Blackbeard, N-Gage).
//
// Reverse-engineered from the loader FUN_100b5750 / header parser FUN_100b62c0
// (debug strings label the fields) and the blitter FUN_100b537c.
//
// Layout:
//   Header (12 bytes, LE): u16 format, u16 width, u16 height, u16 frames,
//                          u32 dataSize
//   then `frames` records, each: u32 recordByteSize, then recordByteSize bytes.
//   dataSize == sum over frames of (4 + recordByteSize).
//
// format: 8 = "non-lossy" (the only one the original supports; 0 = lossy and
//   0x10 = ARGB16 are rejected by its own header check).
//
// Frame payload is a per-row RLE over 16-bit LE words, recovered from the draw
// routine FUN_100b3754. Pixels are ARGB4444 (the blend helper FUN_100b4a50
// masks 0x0FFF and uses bits 12..15 as the alpha nibble):
//
//   per row:  ctrl = W[p++];  ctrl == 0xFFFF -> row is fully transparent
//     loop:   A = ctrl & 0xFF   transparent pixels to skip (no data)
//             B = ctrl >> 8     B literal pixel words follow
//             C = W[p++]        C more pixel words follow
//             x += A + B + C;   until x >= width, a segment with B == C == 0
//                               closes the row, or next ctrl == 0xFFFF
//
// A frame may end with one padding word (0x0000, or 0xCDCD — MSVC's
// uninitialised-heap fill left by the original Windows packing tool).
//
// The one trap is that `A` is a byte, so a transparent run longer than that is
// split across segments carrying the maximum — 254 — and a maximum-length gap
// is indistinguishable from the pure-skip segment that ends a row. See Decode().
//
// **Water frames.** Fifty-six frames refuse that grammar: all 22 travel-map
// islands, both `-W` beach halves and every frame of `ship-refl.tc`. They are
// the same grammar with **eight extra words after each non-empty B run** --
// four columns of slop either side of it. Nothing generic reads them, and for
// a long time that looked like a broken exporter; it is not. These are the
// sprites the engine draws *through* the water (0x100d87c0, 0x100d84c8), and
// that routine reads run 1 at `run + 4 + (rippleHeight >> 6)` words, shoving
// it sideways by up to four texels per row. The slop is what keeps that shove
// inside the run.
//
// The two runs are not one image, either:
//
//   run 1  the **surf**: the shallow water round a coast. Alpha-blended, and
//          displaced per row by the ripple field, which is what makes a
//          coastline shimmer instead of sitting there as an outline.
//   run 2  the **land**: copied straight, no blending.
//
// The engine draws them in two passes with the parallax haze in between. So
// `A` and `B`+`C` still add up to the width and every row still comes out
// exactly `width` across -- the layers interleave, they do not overlap.
//
// Decode() tries the strict grammar first and falls back to this one, which is
// safe because no frame parses both ways; pass `surf` to keep the two layers
// apart, or leave it off to flatten them the way a still picture wants.
//
// Verified two ways. Statically, over all named .tc assets (3202 frames): 3146
// consume their record exactly under the strict grammar and the other 56 under
// the padded one. Dynamically, against the original running under EKA2L1 with
// its decoders breakpointed: 254,230 pixels across 6 textures, zero mismatches.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bb {

class FileInputStream;

class TcTexture {
public:
    enum Format : uint16_t {
        kLossy = 0,      // 4x4 block codec; rejected by the original's own
                         // header check and unused by every shipped asset
        kNonLossy = 8,   // the per-row ARGB4444 RLE below -- the only one used
        kArgb16 = 0x10,  // unsupported by the original engine
    };

    struct Frame {
        std::vector<uint8_t> Data;  // raw record bytes (the RLE word stream)
    };

    enum class DecodeStatus {
        kOk,
        kBadFrameIndex,
        kUnsupportedStream,  // stream doesn't fit the known grammar
    };

    // Decoded frame: ARGB4444, row-major, width*height entries. 0 = transparent.
    struct Image {
        uint16_t Width = 0;
        uint16_t Height = 0;
        std::vector<uint16_t> Pixels;
    };

    // Parse a .tc from a stream (positioned at the start). Returns false on a
    // malformed or unsupported-format header.
    bool Parse(FileInputStream& in);

    uint16_t Format() const { return m_Format; }
    uint16_t Width() const { return m_Width; }
    uint16_t Height() const { return m_Height; }
    uint16_t FrameCount() const { return static_cast<uint16_t>(m_Frames.size()); }
    const Frame& GetFrame(uint16_t i) const { return m_Frames[i]; }
    bool Supported() const { return m_Format == kNonLossy; }

    // The slop either side of a water frame's first run, and how far into it
    // the unshifted pixels start.
    static constexpr int kSlackWords = 8;
    static constexpr int kSlackLeft = 4;
    // How far the ripple field may shove a surf run, in texels either way.
    static constexpr int kSurfShift = kSlackLeft;

    // Expand one frame's RLE stream into ARGB4444 pixels. Tries the strict
    // grammar, then the water one. Given `surf`, a water frame's two layers
    // are kept apart -- run 1 in `surf`, run 2 in `out`; without it they are
    // flattened into `out`. An ordinary frame ignores `surf` and leaves it
    // empty, because its two runs are just one picture.
    DecodeStatus Decode(uint16_t frame, Image& out, Image* surf = nullptr) const;

    // True when the frame only parses as a water frame, so it has a surf
    // layer worth asking for.
    bool IsWaterFrame(uint16_t frame) const;

    // One attempt, with `slack` extra words assumed around each non-empty B
    // run. `slack == 0` is the strict grammar.
    DecodeStatus DecodeVariant(uint16_t frame, Image& out, int slack,
                               Image* surf = nullptr) const;

    // ARGB4444 -> 8-bit RGBA components (nibble * 17 spreads 0..15 to 0..255).
    static void ToRgba8(uint16_t argb4444, uint8_t rgba[4]) {
        rgba[0] = static_cast<uint8_t>(((argb4444 >> 8) & 0xF) * 17);
        rgba[1] = static_cast<uint8_t>(((argb4444 >> 4) & 0xF) * 17);
        rgba[2] = static_cast<uint8_t>((argb4444 & 0xF) * 17);
        rgba[3] = static_cast<uint8_t>(((argb4444 >> 12) & 0xF) * 17);
    }

private:
    uint16_t m_Format = 0;
    uint16_t m_Width = 0;
    uint16_t m_Height = 0;
    uint16_t m_DeclaredFrames = 0;
    std::vector<Frame> m_Frames;
};

}  // namespace bb
