#include "platform/Surface.h"

#include <cmath>
#include <cstddef>

namespace bb {
namespace {

// Shared clip: returns false when nothing of the source rect lands on dst.
struct ClipRect {
    int X0, Y0, X1, Y1;
};
bool Clip(int dstW, int dstH, int srcW, int srcH, int dx, int dy,
          ClipRect& out) {
    if (srcW <= 0 || srcH <= 0) return false;
    out.X0 = dx < 0 ? -dx : 0;
    out.Y0 = dy < 0 ? -dy : 0;
    out.X1 = (dx + srcW > dstW) ? dstW - dx : srcW;
    out.Y1 = (dy + srcH > dstH) ? dstH - dy : srcH;
    return out.X0 < out.X1 && out.Y0 < out.Y1;
}

}  // namespace

void Surface::Copy(const uint16_t* src, int srcW, int srcH, int dx, int dy) {
    ClipRect c;
    if (!src || !Clip(m_Width, m_Height, srcW, srcH, dx, dy, c)) return;
    for (int sy = c.Y0; sy < c.Y1; ++sy) {
        const uint16_t* srow = src + size_t(sy) * srcW;
        uint16_t* drow = m_Pixels.data() + size_t(dy + sy) * m_Width + dx;
        for (int sx = c.X0; sx < c.X1; ++sx) drow[sx] = srow[sx];
    }
}

void Surface::Blit(const uint16_t* src, int srcW, int srcH, int dx, int dy,
                   int alpha) {
    ClipRect c;
    if (!src || alpha <= 0 || !Clip(m_Width, m_Height, srcW, srcH, dx, dy, c))
        return;
    if (alpha > 15) alpha = 15;
    // Two loops rather than one with a test in it. Sprites are mostly empty
    // space -- a 75x75 perk effect is a circle in a square -- and the clear
    // pixels must cost *nothing*: not a blend, and not a store either. Writing
    // the destination back over itself is free on a desktop and is not on a
    // console whose framebuffer is not in ordinary cached memory.
    for (int sy = c.Y0; sy < c.Y1; ++sy) {
        const uint16_t* srow = src + size_t(sy) * srcW;
        uint16_t* drow = m_Pixels.data() + size_t(dy + sy) * m_Width + dx;
        if (alpha == 15) {
            for (int sx = c.X0; sx < c.X1; ++sx) {
                const uint16_t px = srow[sx];
                const int a = (px >> 12) & 0xF;
                if (a == 0) continue;
                drow[sx] = a == 0xF ? uint16_t(0xF000u | (px & 0x0FFF))
                                    : BlendArgb4444(px, drow[sx]);
            }
        } else {
            for (int sx = c.X0; sx < c.X1; ++sx) {
                const uint16_t px = srow[sx];
                const int a = (px >> 12) & 0xF;
                if (a == 0) continue;
                drow[sx] = BlendArgb4444(
                    uint16_t((px & 0x0FFF) | (DivBy15(a * alpha) << 12)),
                    drow[sx]);
            }
        }
    }
}

void Surface::BlitAdditive(const uint16_t* src, int srcW, int srcH, int dx,
                           int dy) {
    ClipRect c;
    if (!src || !Clip(m_Width, m_Height, srcW, srcH, dx, dy, c)) return;
    for (int sy = c.Y0; sy < c.Y1; ++sy) {
        const uint16_t* srow = src + size_t(sy) * srcW;
        uint16_t* drow = m_Pixels.data() + size_t(dy + sy) * m_Width + dx;
        for (int sx = c.X0; sx < c.X1; ++sx) {
            const uint16_t px = srow[sx];
            if ((px & 0x0FFFu) == 0) continue;
            // 0x100b4bdc, channel by channel: add, and clamp each on its own.
            const uint16_t d = drow[sx];
            unsigned r = (px & 0x0F00u) + (d & 0x0F00u);
            unsigned g = (px & 0x00F0u) + (d & 0x00F0u);
            unsigned b = (px & 0x000Fu) + (d & 0x000Fu);
            if (r > 0x0F00u) r = 0x0F00u;
            if (g > 0x00F0u) g = 0x00F0u;
            if (b > 0x000Fu) b = 0x000Fu;
            drow[sx] = uint16_t(0xF000u | r | g | b);
        }
    }
}

namespace {

// Intersect a destination rect with the surface and an optional extra clip.
bool ClipDest(int dstW, int dstH, const Surface::Rect* clip, int& x0, int& y0,
              int& x1, int& y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dstW) x1 = dstW;
    if (y1 > dstH) y1 = dstH;
    if (clip) {
        if (x0 < clip->X0) x0 = clip->X0;
        if (y0 < clip->Y0) y0 = clip->Y0;
        if (x1 > clip->X1) x1 = clip->X1;
        if (y1 > clip->Y1) y1 = clip->Y1;
    }
    return x0 < x1 && y0 < y1;
}

}  // namespace

void Surface::BlitRegion(const uint16_t* src, int srcW, int srcH, int sx,
                         int sy, int sw, int sh, int dx, int dy,
                         const Rect* clip) {
    if (!src || sw <= 0 || sh <= 0) return;
    int x0 = dx, y0 = dy, x1 = dx + sw, y1 = dy + sh;
    if (!ClipDest(m_Width, m_Height, clip, x0, y0, x1, y1)) return;
    for (int y = y0; y < y1; ++y) {
        const int v = sy + (y - dy);
        if (v < 0 || v >= srcH) continue;
        const uint16_t* srow = src + std::size_t(v) * srcW;
        uint16_t* drow = m_Pixels.data() + std::size_t(y) * m_Width;
        for (int x = x0; x < x1; ++x) {
            const int u = sx + (x - dx);
            if (u < 0 || u >= srcW) continue;
            drow[x] = BlendArgb4444(srow[u], drow[x]);
        }
    }
}

void Surface::BlitIndexed(const uint16_t* src, int srcW, int srcH, int dx,
                          int dy, const uint16_t* lut, int lutCount,
                          const Rect* clip) {
    if (!src || !lut || srcW <= 0 || srcH <= 0) return;
    int x0 = dx, y0 = dy, x1 = dx + srcW, y1 = dy + srcH;
    if (!ClipDest(m_Width, m_Height, clip, x0, y0, x1, y1)) return;
    for (int y = y0; y < y1; ++y) {
        const uint16_t* srow = src + std::size_t(y - dy) * srcW;
        uint16_t* drow = m_Pixels.data() + std::size_t(y) * m_Width;
        for (int x = x0; x < x1; ++x) {
            const uint16_t p = srow[x - dx];
            const uint16_t a = p & 0xF000;
            if (a == 0) continue;
            const int index = p & 0x0FFF;
            const uint16_t colour =
                index < lutCount ? uint16_t(lut[index] & 0x0FFF) : 0;
            drow[x] = BlendArgb4444(uint16_t(a | colour), drow[x]);
        }
    }
}

void Surface::BlitTinted(const uint16_t* src, int srcW, int srcH, int dx,
                         int dy, uint16_t tint, const Rect* clip) {
    if (!src || srcW <= 0 || srcH <= 0) return;
    int x0 = dx, y0 = dy, x1 = dx + srcW, y1 = dy + srcH;
    if (!ClipDest(m_Width, m_Height, clip, x0, y0, x1, y1)) return;
    for (int y = y0; y < y1; ++y) {
        const uint16_t* srow = src + std::size_t(y - dy) * srcW;
        uint16_t* drow = m_Pixels.data() + std::size_t(y) * m_Width;
        for (int x = x0; x < x1; ++x) {
            const uint16_t p = srow[x - dx];
            if ((p & 0xF000) == 0xF000) drow[x] = tint;
            else drow[x] = BlendArgb4444(p, drow[x]);
        }
    }
}

void Surface::FillRect(int x0i, int y0i, int w, int h, uint16_t argb,
                       const Rect* clip) {
    int x0 = x0i, y0 = y0i, x1 = x0i + w, y1 = y0i + h;
    if (!ClipDest(m_Width, m_Height, clip, x0, y0, x1, y1)) return;
    for (int y = y0; y < y1; ++y) {
        uint16_t* drow = m_Pixels.data() + std::size_t(y) * m_Width;
        for (int x = x0; x < x1; ++x) drow[x] = BlendArgb4444(argb, drow[x]);
    }
}

namespace {

// Weighted average of four ARGB4444 pixels, with the weights in 0..256ths of
// the 16.16 fraction, exactly as the engine's filtered span keeps them
// (0x100b2ea0). `bias` is added to the top-left weight: the engine passes 1/16
// of a whole weight on alternate pixels, so the truncating shift dithers
// instead of banding a gradient into flat steps.
//
// Alpha is averaged along with the colours, which the engine does not bother
// with -- it writes a zero alpha nibble, having nothing behind the sprite to
// blend against. Averaging keeps a cut-out's soft edge soft here.
uint16_t Sample4(uint16_t p00, uint16_t p10, uint16_t p01, uint16_t p11, int fu,
                 int fv, int bias) {
    const int w00 = (256 - fu) * (256 - fv) + bias, w10 = fu * (256 - fv);
    const int w01 = (256 - fu) * fv, w11 = fu * fv;
    int out = 0;
    for (int shift = 0; shift <= 12; shift += 4) {
        const int v = (((p00 >> shift) & 0xF) * w00 + ((p10 >> shift) & 0xF) * w10 +
                       ((p01 >> shift) & 0xF) * w01 + ((p11 >> shift) & 0xF) * w11) >>
                      16;
        out |= (v > 0xF ? 0xF : v) << shift;
    }
    return static_cast<uint16_t>(out);
}

}  // namespace

void Surface::BlitScaled(const uint16_t* src, int srcW, int srcH, int dx,
                         int dy, int dw, int dh, bool bilinear, int alpha,
                         const Rect* clip) {
    if (!src || srcW <= 0 || srcH <= 0 || dw <= 0 || dh <= 0 || alpha <= 0)
        return;

    int x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > m_Width) x1 = m_Width;
    if (y1 > m_Height) y1 = m_Height;
    if (clip) {
        if (x0 < clip->X0) x0 = clip->X0;
        if (y0 < clip->Y0) y0 = clip->Y0;
        if (x1 > clip->X1) x1 = clip->X1;
        if (y1 > clip->Y1) y1 = clip->Y1;
    }
    if (x0 >= x1 || y0 >= y1) return;

    // Source position stepped in 16.16, starting at the source's own origin --
    // no half-pixel centring, so the first destination column always samples
    // source column 0 and the walk never runs off either end.
    const int64_t stepU = (int64_t(srcW) << 16) / dw;
    const int64_t stepV = (int64_t(srcH) << 16) / dh;

    for (int y = y0; y < y1; ++y) {
        const int64_t vv = (int64_t(y) - dy) * stepV;
        int sy = static_cast<int>(vv >> 16);
        if (sy >= srcH) sy = srcH - 1;
        const int fv = static_cast<int>((vv >> 8) & 0xFF);
        uint16_t* drow = m_Pixels.data() + size_t(y) * m_Width;
        for (int x = x0; x < x1; ++x) {
            const int64_t uu = (int64_t(x) - dx) * stepU;
            int sx = static_cast<int>(uu >> 16);
            if (sx >= srcW) sx = srcW - 1;
            uint16_t px;
            if (bilinear) {
                const int fu = static_cast<int>((uu >> 8) & 0xFF);
                const int cx = sx + 1 < srcW ? sx + 1 : sx;
                const int cy = sy + 1 < srcH ? sy + 1 : sy;
                const uint16_t* r0 = src + size_t(sy) * srcW;
                const uint16_t* r1 = src + size_t(cy) * srcW;
                px = Sample4(r0[sx], r0[cx], r1[sx], r1[cx], fu, fv,
                             ((x - dx + y - dy) & 1) << 12);
            } else {
                px = src[size_t(sy) * srcW + sx];
            }
            if (alpha < 15) {
                const int a = DivBy15(((px >> 12) & 0xF) * alpha);
                px = static_cast<uint16_t>((px & 0x0FFF) | (a << 12));
            }
            drow[x] = BlendArgb4444(px, drow[x]);
        }
    }
}

Surface::Affine Surface::Stretch(int srcW, int srcH) {
    Affine a;
    a.U1 = int32_t(srcW) << 16;
    a.V2 = int32_t(srcH) << 16;
    return a;
}

Surface::Affine Surface::StretchLinear(int srcW, int srcH) {
    Affine a;
    a.U0 = a.V0 = a.U2 = a.V1 = 1 << 16;
    a.U1 = int32_t(srcW - 1) << 16;
    a.V2 = int32_t(srcH - 1) << 16;
    return a;
}

Surface::Affine Surface::Rotate(int srcW, int srcH, int angle) {
    angle &= 0x3FF;
    if (angle == 0) return Stretch(srcW, srcH);
    // The engine keeps a 1024-entry sine table in 16.16 and reads cosine from
    // 256 entries further along (0x100b2d2c). Computing it comes to the same
    // numbers and saves carrying the table.
    const double turn = double(angle) * 2.0 * 3.14159265358979323846 / 1024.0;
    const int64_t sn = std::lround(std::sin(turn) * 65536.0);
    const int64_t cs = std::lround(std::cos(turn) * 65536.0);
    // The corners the engine turns are whole texels either side of the centre
    // (0x100b2354 keeps them as -w/2 and +w/2 and 0x100b2d2c drops their
    // fraction), and it turns them about that centre. Subtracting the low
    // corner afterwards puts the result back in the image's own coordinates,
    // where an angle of zero comes out as exactly Stretch.
    const int32_t x0 = -((srcW + 1) >> 1), x1 = srcW >> 1;
    const int32_t y0 = -((srcH + 1) >> 1), y1 = srcH >> 1;
    const int32_t corner[3][2] = {{x0, y0}, {x1, y0}, {x0, y1}};
    Affine a;
    int32_t* p[3][2] = {{&a.U0, &a.V0}, {&a.U1, &a.V1}, {&a.U2, &a.V2}};
    for (int i = 0; i < 3; ++i) {
        const int32_t x = corner[i][0], y = corner[i][1];
        *p[i][0] = int32_t(cs * x - sn * y) - (x0 << 16);
        *p[i][1] = int32_t(sn * x + cs * y) - (y0 << 16);
    }
    return a;
}

void Surface::BlitAffine(const uint16_t* src, int srcW, int srcH,
                         int32_t dx16, int32_t dy16, int32_t dw16, int32_t dh16,
                         const Affine& uv, bool bilinear, int alpha,
                         const Rect* clip) {
    if (!src || srcW <= 0 || srcH <= 0 || alpha <= 0) return;
    // Whole destination pixels, and the sub-pixel remainder the engine keeps
    // at +0x74/+0x78 to bias the first sample.
    const int dx = dx16 >> 16, dy = dy16 >> 16;
    const int32_t fracX = dx16 & 0xFFFF, fracY = dy16 & 0xFFFF;
    const int dw = dw16 >> 16, dh = dh16 >> 16;
    if (dw <= 0 || dh <= 0) return;

    // Sixteenths of a pixel: dividing by this rather than by the rounded pixel
    // count is what keeps the step continuous as the extent creeps.
    const int64_t spanX = dw16 >> 12;
    const int64_t spanY = dh16 >> 12;
    if (spanX <= 0 || spanY <= 0) return;
    const int32_t duDx = int32_t((int64_t(uv.U1 - uv.U0) * 16) / spanX);
    const int32_t dvDx = int32_t((int64_t(uv.V1 - uv.V0) * 16) / spanX);
    const int32_t duDy = int32_t((int64_t(uv.U2 - uv.U0) * 16) / spanY);
    const int32_t dvDy = int32_t((int64_t(uv.V2 - uv.V0) * 16) / spanY);

    int x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > m_Width) x1 = m_Width;
    if (y1 > m_Height) y1 = m_Height;
    if (clip) {
        if (x0 < clip->X0) x0 = clip->X0;
        if (y0 < clip->Y0) y0 = clip->Y0;
        if (x1 > clip->X1) x1 = clip->X1;
        if (y1 > clip->Y1) y1 = clip->Y1;
    }
    if (x0 >= x1 || y0 >= y1) return;

    for (int y = y0; y < y1; ++y) {
        const int row = y - dy;
        int32_t u = uv.U0 + duDy * row + duDx * (x0 - dx) - fracX;
        int32_t v = uv.V0 + dvDy * row + dvDx * (x0 - dx) - fracY;
        uint16_t* drow = m_Pixels.data() + std::size_t(y) * m_Width;
        for (int x = x0; x < x1; ++x, u += duDx, v += dvDx) {
            const int sx = u >> 16;
            const int sy = v >> 16;
            if (sx < 0 || sy < 0 || sx >= srcW || sy >= srcH) continue;
            uint16_t px;
            if (bilinear) {
                const int cx = sx + 1 < srcW ? sx + 1 : sx;
                const int cy = sy + 1 < srcH ? sy + 1 : sy;
                const uint16_t* r0 = src + std::size_t(sy) * srcW;
                const uint16_t* r1 = src + std::size_t(cy) * srcW;
                // Eight-bit weights, and the checkerboard nudge the engine
                // adds to the top-left one. Row parity counts from the
                // sprite's own top edge and column parity from the clipped
                // one, as the engine's two counters do (0x100b2690).
                const int fu = (u >> 8) & 0xFF, fv = (v >> 8) & 0xFF;
                const int odd = ((x - x0) + (y - dy)) & 1;
                px = Sample4(r0[sx], r0[cx], r1[sx], r1[cx], fu, fv,
                             odd << 12);
            } else {
                px = src[std::size_t(sy) * srcW + sx];
            }
            if (alpha < 15) {
                const int a = DivBy15(((px >> 12) & 0xF) * alpha);
                px = static_cast<uint16_t>((px & 0x0FFF) | (a << 12));
            }
            drow[x] = BlendArgb4444(px, drow[x]);
        }
    }
}

void Surface::Line(int x0, int y0, int x1, int y1, uint16_t argb4444) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < m_Width && y0 < m_Height) {
            uint16_t& d = m_Pixels[std::size_t(y0) * m_Width + x0];
            d = BlendArgb4444(argb4444, d);
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Surface::FadeStep(int step) {
    if (step >= 16) return;
    if (step <= 0) {
        Fill(0xF000u);
        return;
    }
    for (auto& p : m_Pixels) {
        const int r = (((p >> 8) & 0xF) * step) / 16;
        const int g = (((p >> 4) & 0xF) * step) / 16;
        const int b = ((p & 0xF) * step) / 16;
        p = static_cast<uint16_t>((p & 0xF000u) | (r << 8) | (g << 4) | b);
    }
}

}  // namespace bb
