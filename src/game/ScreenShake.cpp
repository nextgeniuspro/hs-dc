#include "game/ScreenShake.h"

#include <algorithm>
#include <cmath>

#include "platform/Surface.h"

namespace bb {
namespace {

constexpr int kAngles = 1024;  // the engine's sine table (0x1011e43c)

// 16.16 sine over a 1024-step turn, the table's own scale. Kept local for the
// same reason Water.cpp and TravelMap.cpp keep their own: it is ten lines and
// a header shared between four files that each want it slightly differently
// would be the larger thing.
int32_t Sin(int a) {
    static int32_t table[kAngles];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < kAngles; ++i)
            table[i] = static_cast<int32_t>(std::lround(
                65536.0 * std::sin(2.0 * 3.14159265358979323846 * i / kAngles)));
        built = true;
    }
    return table[a & (kAngles - 1)];
}

}  // namespace

// 0x100e712c. The phases are restarted along with the energy, so every shake
// begins with the same first kick rather than picking up wherever the last one
// happened to stop.
void ScreenShake::Begin(int decay, int energy) {
    m_PhaseX = 0;
    m_PhaseY = kPhaseYStart;
    m_Decay = decay;
    m_Energy = energy;
}

void ScreenShake::Offset(int* dx, int* dy) const {
    // The engine reads the amplitude as the *signed short* at +0x12, which is
    // the top half of the energy word: whole pixels, and it falls to zero as
    // the energy drains.
    const int amp = static_cast<int16_t>(m_Energy >> 16);
    if (dx)
        *dx = static_cast<int>((static_cast<int64_t>(Sin(m_PhaseX)) * amp) >> 16);
    if (dy)
        *dy = static_cast<int>((static_cast<int64_t>(Sin(m_PhaseY)) * amp) >> 16);
}

// 0x100e714c.
bool ScreenShake::Step(Surface& dst) {
    // Spent before it is used, so the last frame of a shake is already
    // smaller than the one before it and the effect never ends on a jolt.
    m_Energy -= m_Decay;
    if (m_Energy < 1) {
        m_Energy = 0;
        return true;
    }
    m_PhaseX += kStepX;
    m_PhaseY += kStepY;
    int dx = 0, dy = 0;
    Offset(&dx, &dy);
    if (dx != 0 || dy != 0) Displace(dst, dx, dy);
    return false;
}

// The row-by-row copy. Which end it starts from depends on which way the
// picture is going, because the rows are moved in place: going down it has to
// walk up, or it would overwrite the rows it has not read yet.
void ScreenShake::Displace(Surface& dst, int dx, int dy) {
    const int w = dst.Width(), h = dst.Height();
    if (w <= 0 || h <= 0) return;
    if (dx <= -w || dx >= w || dy <= -h || dy >= h) return;
    m_Row.resize(static_cast<std::size_t>(w));

    uint16_t* p = dst.Pixels();
    int count = h, stride = w;
    const int shift = dy * w;   // where a row lands, relative to where it was
    if (dy < 0) {
        // Moving up: read from `-dy` rows down and walk forward. The bottom
        // `-dy` rows have nothing to be filled from and keep what they had.
        count = h + dy;
        p += static_cast<std::size_t>(-dy) * w;
    } else if (dy > 0) {
        count = h - dy;
        stride = -w;
        p += static_cast<std::size_t>(h - 1 - dy) * w;
    }
    for (int i = 0; i < count; ++i) {
        std::copy_n(p, w, m_Row.data());
        if (dx < 0)
            std::copy_n(m_Row.data() - dx, w + dx, p + shift);
        else
            std::copy_n(m_Row.data(), w - dx, p + shift + dx);
        p += stride;
    }
}

}  // namespace bb
