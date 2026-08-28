#include "game/SeaSurface.h"

#include "game/TextureCache.h"
#include "shim/Log.h"

namespace bb {

bool SeaSurface::Init(const Texture* source, int warmup) {
    return Init(source, kGridShift, kDropRadius, kDropStrength, kDropInterval,
                0, 0, warmup);
}

bool SeaSurface::Init(const Texture* source, int gridShift, int dropRadius,
                      int dropStrength, int dropInterval, int windX,
                      int windY, int warmup) {
    m_Src = nullptr;
    m_Shared = nullptr;
    m_Size = 1 << gridShift;
    m_Shift = gridShift;
    m_Interval = dropInterval > 0 ? dropInterval : kDropInterval;
    m_WindVx = windX;
    m_WindVy = windY;
    m_WindX = m_WindY = 0;
    if (!source || !source->Valid()) return false;
    const TcTexture::Image* img = source->Frame(0);
    if (!img || img->Width != m_Size || img->Height != m_Size) {
        LogError("sea: source is %dx%d, expected %dx%d\n",
                 img ? img->Width : 0, img ? img->Height : 0, m_Size, m_Size);
        return false;
    }
    m_Src = img->Pixels.data();
    m_Out.assign(std::size_t(m_Size) * m_Size, 0);
    m_Field.Init(m_Shift, dropRadius, dropStrength);
    // Pre-run the sim so the sea is already moving on the first frame. A
    // surface that is going to adopt another's field passes zero and is
    // warmed by its owner's loop instead.
    for (int i = 0; i < warmup; ++i) Step();
    return true;
}

// 0x100d8148, with the grid and the source the same size so the wrap masks
// coincide. It refreshes only one parity of rows per call, alternating; that
// interlace is the original's, and on a 32x32 tile it is what keeps a droplet
// ring from reading as a hard circle.
void SeaSurface::Refract() {
    ++m_Frame;
    const int32_t* h = Heights();
    const int mask = Mask();
    // An eighth of each accumulator, which is how far the ripple pattern has
    // crawled: the height field is read that much further on and the source
    // texture that much further back.
    const int wx = m_WindX >> 3, wy = m_WindY >> 3;
    for (int y = int(m_Frame & 1); y < m_Size; y += 2) {
        const int row = ((y + wy) & mask) << m_Shift;
        const int rowDown = ((y + wy + 1) & mask) << m_Shift;
        for (int x = 0; x < m_Size; ++x) {
            const int gx = (x + wx) & mask;
            const int32_t h0 = h[gx + row];
            const int32_t dy = h0 - h[gx + rowDown];
            int shade = dy >> m_ShadeShift;
            if (shade == 6) shade = 4;  // the original's own special case
            const int32_t dx = h0 - h[((gx + 1) & mask) + row];

            const int tx = (x + (dx >> m_RefractShift) - wx) & mask;
            const int ty = (y + (dy >> m_RefractShift) - wy) & mask;
            const uint16_t c = m_Src[tx + (ty << m_Shift)];

            int r = int(c & 0x0F00) - shade * 0x100;
            int g = int(c & 0x00F0) - shade * 0x10;
            int b = int(c & 0x000F) - shade;
            if (r > 0xFFF) r = 0xFFF;
            if (r < 0x100) r = 0x100;
            if (g > 0xFE) g = 0xFF;
            if (g < 0x10) g = 0x10;
            if (b > 0xE) b = 0xF;
            uint16_t px = uint16_t(0xF000u) | uint16_t(r) | uint16_t(g);
            if (b >= 0) px |= uint16_t(b);
            m_Out[(std::size_t(y) << m_Shift) + std::size_t(x)] = px;
        }
    }
}

// 0x100d7ee4. The caller passes the object's own frame counter, and the whole
// simulation -- buffer swap, droplet, propagation -- happens only when it is
// even. The refraction runs every call. Stepping the water every frame instead
// pumps in twice the energy and the droplet rings stop being subtle.
void SeaSurface::Step() {
    if (!m_Src) return;
    m_WindX -= m_WindVx;
    m_WindY -= m_WindVy;
    // A surface running on an adopted field neither drips nor propagates --
    // 0x100d79bc clears the enable flag along with the buffers -- it only
    // refracts its own texture through the owner's heights.
    if (!m_Shared && (m_Frame & 1) == 0) {
        m_Field.Swap();
        if (m_Frame % uint32_t(m_Interval) == 0) {
            m_Rng = m_Rng * 1103515245u + 12345u;
            const int gx = int(m_Rng >> 16) & Mask();
            m_Rng = m_Rng * 1103515245u + 12345u;
            const int gy = int(m_Rng >> 16) & Mask();
            m_Field.Splash(gx, gy);
        }
        m_Field.Propagate();
    }
    Refract();
}

}  // namespace bb
