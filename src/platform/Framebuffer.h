// Software framebuffer matching the N-Gage screen (176x208).
//
// The game software-renders into its own buffer and blits once per frame
// (main.dll imports only 2 BITGDI functions). The platform layer owns this
// buffer and presents it; decompiled rendering code will draw into Pixels().
#pragma once

#include <cstdint>
#include <vector>

namespace bb {

class Surface;

class Framebuffer {
public:
    static constexpr int kWidth = 176;
    static constexpr int kHeight = 208;

    Framebuffer() : m_Pixels(kWidth * kHeight, 0) {}

    // ARGB8888, row-major, kWidth stride.
    uint32_t* Pixels() { return m_Pixels.data(); }
    const uint32_t* Pixels() const { return m_Pixels.data(); }

    void Fill(uint32_t argb) {
        for (auto& p : m_Pixels) p = argb;
    }
    void Put(int x, int y, uint32_t argb) {
        if (x >= 0 && x < kWidth && y >= 0 && y < kHeight)
            m_Pixels[y * kWidth + x] = argb;
    }

    // Source-over blit of an ARGB4444 image (the .tc pixel format) at (dx, dy),
    // clipped to the screen. Alpha 0 pixels are skipped, 15 are copied, the
    // rest are blended -- mirroring the original engine's three span paths
    // (skip run / opaque run / blended run) in FUN_100b3754.
    void Blit(const uint16_t* src, int srcW, int srcH, int dx, int dy);

    // Convert the game's ARGB4444 screen to this buffer's ARGB8888, opaque.
    // This is the port's equivalent of the original's blit to the window
    // server, and the only place the two pixel formats meet.
    void CopyFrom(const Surface& src);

private:
    std::vector<uint32_t> m_Pixels;
};

// Diagnostic gradient used before any game content is wired up.
void DrawTestPattern(Framebuffer& fb);

}  // namespace bb
