// Surface — the game's own render target, in the engine's native pixel format.
//
// The original allocates one 73216-byte buffer (DAT_1008d734 = 176*208*2) and
// hands it out from System::screen() (engine vtable +0x50). Every draw path in
// main.dll composites 16-bit ARGB4444 into it, then System::flip() (+0x54)
// pushes it to the window server -- the whole reason main.dll needs only two
// BITGDI imports.
//
// So the port renders in ARGB4444 too, and only the host backend converts. That
// keeps decompiled draw code byte-comparable against the original, and leaves
// the 32-bit conversion as a single well-defined step in Framebuffer.
#pragma once

#include <cstdint>
#include <vector>

namespace bb {

// Source-over composite of one ARGB4444 pixel. Alpha is the top nibble; the
// engine's blend helper (FUN_100b4a50) masks 0x0FFF and indexes a 16-entry LUT
// per nibble, which is this same c*a + d*(15-a) over 15 arithmetic.
// Divide by fifteen without dividing. Every blend below wants `x / 15` for a
// numerator no bigger than 15*15 + 15*15, and `(x * 4370) >> 16` is exactly
// that over the whole of that range -- there is a test that walks all of it.
//
// This is not micro-optimisation for its own sake: the SH-4 in a Dreamcast has
// no integer divide instruction, so each `/ 15` is a call into libgcc, and a
// blend that did three of them per pixel cost more than the rest of the frame
// put together whenever anything large and translucent was drawn.
inline int DivBy15(int x) { return (x * 4370) >> 16; }

inline uint16_t BlendArgb4444(uint16_t src, uint16_t dst) {
    const int a = (src >> 12) & 0xF;
    if (a == 0) return dst;
    if (a == 0xF) return static_cast<uint16_t>(0xF000u | (src & 0x0FFF));
    const int inv = 15 - a;
    const int r = DivBy15(((src >> 8) & 0xF) * a + ((dst >> 8) & 0xF) * inv);
    const int g = DivBy15(((src >> 4) & 0xF) * a + ((dst >> 4) & 0xF) * inv);
    const int b = DivBy15((src & 0xF) * a + (dst & 0xF) * inv);
    return static_cast<uint16_t>(0xF000u | (r << 8) | (g << 4) | b);
}

class Surface {
public:
    // Half-open destination rectangle, for draws that must stay clear of
    // something else on screen (the cutscene player keeps its artwork above
    // the subtitle bar this way, as the engine does at 0x10081874).
    struct Rect {
        int X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
    };

    static constexpr int kWidth = 176;   // N-Gage screen
    static constexpr int kHeight = 208;
    static constexpr int kSizeBytes = kWidth * kHeight * 2;  // == 73216

    Surface() : Surface(kWidth, kHeight) {}
    Surface(int w, int h) : m_Width(w), m_Height(h), m_Pixels(size_t(w) * h, 0) {}

    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    uint16_t* Pixels() { return m_Pixels.data(); }
    const uint16_t* Pixels() const { return m_Pixels.data(); }

    void Fill(uint16_t argb4444) {
        for (auto& p : m_Pixels) p = argb4444;
    }

    // Overwrite, ignoring alpha -- for full-screen backgrounds, which is how the
    // splash images land (the loader draws them into a fresh buffer at 0,0).
    void Copy(const uint16_t* src, int srcW, int srcH, int dx, int dy);

    // Source-over blit, clipped. Mirrors the engine's three span paths:
    // skip (a==0), opaque copy (a==15), blend (everything else). `alpha` is an
    // extra 0..15 fade over each pixel's own, which is the engine's per-image
    // alpha (texture vtable +0x18): the ship's reflection is drawn at 5, and
    // so is each scroll arrow when its direction is exhausted.
    void Blit(const uint16_t* src, int srcW, int srcH, int dx, int dy,
              int alpha = 15);

    // Source-*added*, clipped: the engine's blitter mode 5, whose span routine
    // is 0x100b4bdc. Each channel is added to what is already there and
    // clamped, the source's alpha is **ignored** entirely, and what comes out
    // is opaque. That last part is the whole character of it -- a perk's
    // artwork is a dim smudge whose alpha ramps from three to ten, and
    // weighting the add by that alpha is the difference between a flash and a
    // stain.
    //
    // The routine adds every pixel in the span, clear ones included; this one
    // skips them, which is the same answer because every clear pixel in the
    // sheets that use this mode is black, and much less work.
    void BlitAdditive(const uint16_t* src, int srcW, int srcH, int dx, int dy);

    // Source-over blit of a sub-rectangle, which is how the battlefield reads
    // its tile strips: `grass_1.tc` is one 240x20 image holding twelve 20x20
    // tiles side by side and the map data picks the column.
    void BlitRegion(const uint16_t* src, int srcW, int srcH, int sx, int sy,
                    int sw, int sh, int dx, int dy, const Rect* clip = nullptr);

    // Source-over blit where each source pixel is `alpha << 12 | index` and
    // the colour comes from `lut`. Buildings and unit sprites are indexed and
    // the engine recolours them per player by overwriting sixteen palette
    // entries (0x1005a164), so the pixels have to stay unresolved until here.
    void BlitIndexed(const uint16_t* src, int srcW, int srcH, int dx, int dy,
                     const uint16_t* lut, int lutCount,
                     const Rect* clip = nullptr);

    // Source-over blit where every *fully opaque* source pixel is replaced by
    // `tint`. That is the engine's blend mode 9 (FUN_100b4e7c's run type 1),
    // which takes the flat colour from the texture's own field at +0x2c;
    // partly transparent pixels still blend in their own colour, so an
    // antialiased glyph keeps its soft edge while its body is recoloured. The
    // battlefield's money readout is drawn this way, in gold.
    void BlitTinted(const uint16_t* src, int srcW, int srcH, int dx, int dy,
                    uint16_t tint, const Rect* clip = nullptr);

    // Source-over fill of a destination rectangle.
    void FillRect(int x, int y, int w, int h, uint16_t argb4444,
                  const Rect* clip = nullptr);

    // Source-over blit of `src` stretched to dw x dh at (dx, dy), with the
    // source stepped in 16.16 the way the engine's Zoom image class does.
    // `bilinear` picks between the two samplers the cutscene sprite records
    // select (0x100007a8); `alpha` is an extra 0..15 fade applied on top of
    // each pixel's own; `clip`, when given, further limits the destination.
    void BlitScaled(const uint16_t* src, int srcW, int srcH, int dx, int dy,
                    int dw, int dh, bool bilinear = false, int alpha = 15,
                    const Rect* clip = nullptr);

    // Where the destination rectangle's three defining corners land in the
    // source, in 16.16 texels: under its top-left, its top-right and its
    // bottom-left. Everything between is interpolated, so this covers scaling,
    // rotation and any shear of the two together -- which is exactly the shape
    // the engine's transformable image class keeps (0x100b2690 reads six
    // words at +0x98 and divides them into per-pixel steps).
    struct Affine {
        int32_t U0 = 0, V0 = 0;
        int32_t U1 = 0, V1 = 0;
        int32_t U2 = 0, V2 = 0;
    };

    // The whole source over the whole destination, unturned: the corners span
    // exactly [0, w] x [0, h], because the engine builds them as the image's
    // own extent either side of its centre (0x100b2354 stores -w/2 and +w/2,
    // and 0x100b5d48 centres the image in its padded square, so the two cancel
    // to 0 and w). Span w and not w-1 is the whole ball game: it is what makes
    // an unturned 1:1 draw step exactly one texel and land on whole texels, so
    // it comes back byte-for-byte instead of being resampled every frame.
    static Affine Stretch(int srcW, int srcH);

    // The same for an image the engine stores *linearly* rather than padded --
    // its "Zoom" class. That path throws the corners away and substitutes its
    // own (0x100b276c), which are inset by a texel on every side: [1, w-1] x
    // [1, h-1]. So a Zoom sprite is very slightly cropped and magnified even at
    // its own size, and cannot turn at all.
    static Affine StretchLinear(int srcW, int srcH);

    // Stretch, turned about the source's centre. `angle` is in the engine's
    // own units: **1024 to a full turn**, clockwise, which is what the sine
    // table at 0x1011e43c is indexed by (0x100b2d2c).
    static Affine Rotate(int srcW, int srcH, int angle);

    // Source-over affine blit. The destination position *and extent* are 16.16
    // and both matter: the per-pixel source step is computed against the
    // fractional extent, so a slow zoom moves the sampling smoothly even
    // though the rectangle it covers only gains a whole column now and then.
    // Getting that wrong is what makes a zoom look like it is stretching one
    // axis at a time. Samples that fall outside the source are skipped rather
    // than clamped, so a turned image does not smear its edge pixels.
    //
    // `bilinear` follows the engine's filtered span (0x100b2ea0) closely: the
    // weights are eighths of a percent rather than sixteenths, and one of the
    // four is tipped by 1/16 on a checkerboard, which is what stops a
    // truncating blend from banding a smooth gradient into flat patches.
    // Two things it does that the port does not: it leaves the alpha nibble
    // at zero (the port interpolates alpha, so soft edges stay soft), and it
    // overwrites rather than blending (the port blends, which only shows on
    // sprites that fade -- and no filtered sprite in any shipped cutscene
    // does).
    void BlitAffine(const uint16_t* src, int srcW, int srcH, int32_t dx16,
                    int32_t dy16, int32_t dw16, int32_t dh16, const Affine& uv,
                    bool bilinear = false, int alpha = 15,
                    const Rect* clip = nullptr);

    // Source-over line, Bresenham. The travel map's flocks are drawn this way
    // and nothing else in the game is (0x100a2680).
    void Line(int x0, int y0, int x1, int y1, uint16_t argb4444);

    // Scale every pixel's colour toward black; 0 = black, 16 = unchanged.
    // The boot fade (FUN_1008d320) steps this 0..15 over 16 frames of 20ms.
    void FadeStep(int step);

private:
    int m_Width;
    int m_Height;
    std::vector<uint16_t> m_Pixels;
};

}  // namespace bb
