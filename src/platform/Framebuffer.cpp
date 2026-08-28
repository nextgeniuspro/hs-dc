#include "platform/Framebuffer.h"

#include "platform/Surface.h"

// Reserved for framebuffer helpers that shouldn't be inlined (palette blits,
// scaling). Kept as a translation unit so the platform lib has a stable ABI
// as the decompiled BITGDI-equivalent draw paths land here.
namespace bb {

void Framebuffer::Blit(const uint16_t* src, int srcW, int srcH, int dx, int dy) {
    if (!src || srcW <= 0 || srcH <= 0) return;

    // Clip the source rect against the screen.
    const int x0 = dx < 0 ? -dx : 0;
    const int y0 = dy < 0 ? -dy : 0;
    const int x1 = (dx + srcW > kWidth) ? kWidth - dx : srcW;
    const int y1 = (dy + srcH > kHeight) ? kHeight - dy : srcH;

    for (int sy = y0; sy < y1; ++sy) {
        const uint16_t* srow = src + size_t(sy) * srcW;
        uint32_t* drow = m_Pixels.data() + size_t(dy + sy) * kWidth + dx;
        for (int sx = x0; sx < x1; ++sx) {
            const uint16_t p = srow[sx];
            const int a = (p >> 12) & 0xF;
            if (a == 0) continue;  // transparent

            // Expand each 4-bit channel to 8 bits (n * 17 maps 0..15 -> 0..255).
            const int r = ((p >> 8) & 0xF) * 17;
            const int g = ((p >> 4) & 0xF) * 17;
            const int b = (p & 0xF) * 17;

            if (a == 0xF) {
                drow[sx] = 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
                continue;
            }
            const uint32_t d = drow[sx];
            const int dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
            const int inv = 15 - a;
            const uint32_t outR = uint32_t((r * a + dr * inv) / 15);
            const uint32_t outG = uint32_t((g * a + dg * inv) / 15);
            const uint32_t outB = uint32_t((b * a + db * inv) / 15);
            drow[sx] = 0xFF000000u | (outR << 16) | (outG << 8) | outB;
        }
    }
}

void Framebuffer::CopyFrom(const Surface& src) {
    const int w = src.Width() < kWidth ? src.Width() : kWidth;
    const int h = src.Height() < kHeight ? src.Height() : kHeight;
    for (int y = 0; y < h; ++y) {
        const uint16_t* srow = src.Pixels() + size_t(y) * src.Width();
        uint32_t* drow = m_Pixels.data() + size_t(y) * kWidth;
        for (int x = 0; x < w; ++x) {
            const uint16_t p = srow[x];
            const uint32_t r = ((p >> 8) & 0xF) * 17;
            const uint32_t g = ((p >> 4) & 0xF) * 17;
            const uint32_t b = (p & 0xF) * 17;
            drow[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

void DrawTestPattern(Framebuffer& fb) {
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const uint8_t r = static_cast<uint8_t>(x * 255 / Framebuffer::kWidth);
            const uint8_t g = static_cast<uint8_t>(y * 255 / Framebuffer::kHeight);
            const uint8_t b = static_cast<uint8_t>((x ^ y) & 0xFF);
            fb.Put(x, y, 0xFF000000u | (r << 16) | (g << 8) | b);
        }
    }
}

}  // namespace bb
