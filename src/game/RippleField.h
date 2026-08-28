// RippleField — the 2D ripple tank the engine's water effect is built on.
//
// One class in the original (constructed at 0x100d7a0c) serves both the menu
// background and the sea in a battle; only its parameters differ:
//
//     menu    FUN_100d7a0c(sim, 7, 5, 10, 0x10, 0x40, 0x100, 0x100)
//     battle  FUN_100d7a0c(sim, 4, 5, 10, 0x10, 0x20, 0x020, 0x020)
//
// -- drop radius, drop strength, ?, drop interval, grid size, source width,
// source height. So the battle sea is the same simulation at a quarter of the
// menu's grid, refracting a 32x32 tile instead of a 256x256 painting.
//
// The field itself is the classic scheme (0x100d800c): a double-buffered
// integer height grid, propagated as `new = (sum of eight neighbours >> 2) -
// old` and damped by `v - (v >> 10)`, with one random droplet every sixteenth
// frame. The droplet brush is a cone, `strength * (radius - distance)`, built
// once (0x100d7ca8).
//
// One quirk is reproduced rather than tidied: the brush is indexed with a
// stride of `radius` while spanning `2 * radius` columns, so its cells overlap.
// It is almost certainly a bug in the original, but it is part of how the water
// looks.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace bb {

class RippleField {
public:
    // `gridShift` sets the size: the field is (1 << gridShift) square.
    void Init(int gridShift, int dropRadius, int dropStrength,
              int dampingShift = 10) {
        m_Shift = gridShift;
        m_Size = 1 << gridShift;
        m_Mask = m_Size - 1;
        m_Radius = dropRadius;
        m_Strength = dropStrength;
        m_Damping = dampingShift;
        m_Grid[0].assign(std::size_t(m_Size) * m_Size, 0);
        m_Grid[1].assign(std::size_t(m_Size) * m_Size, 0);
        BuildBrush();
    }

    int Size() const { return m_Size; }
    int Shift() const { return m_Shift; }
    int Mask() const { return m_Mask; }
    const int32_t* Heights() const { return m_Grid[m_Cur].data(); }

    void Swap() { m_Cur ^= 1; }

    void Splash(int gx, int gy) {
        const int r = m_Radius;
        const int x0 = (gx < r) ? 1 - gx : -r;
        const int x1 = (gx > m_Size - (r + 1)) ? m_Size - (gx + 1) : r;
        const int y0 = (gy < r) ? 1 - gy : -r;
        const int y1 = (gy > m_Size - (r + 1)) ? m_Size - (gy + 1) : r;

        int32_t* dst = m_Grid[m_Cur ^ 1].data();
        int brushRow = r * (y0 + r);
        for (int dy = y0; dy < y1; ++dy) {
            for (int dx = x0; dx < x1; ++dx) {
                if (dx * dx + dy * dy < r * r)
                    dst[(gx + dx) + ((gy + dy) << m_Shift)] += m_Brush[std::size_t(dx + r + brushRow)];
            }
            brushRow += r;
        }
    }

    void Propagate() {
        const int32_t* cur = m_Grid[m_Cur].data();
        int32_t* other = m_Grid[m_Cur ^ 1].data();
        for (int y = 0; y < m_Size; ++y) {
            const int up = ((y + m_Size - 1) & m_Mask) << m_Shift;
            const int down = ((y + 1) & m_Mask) << m_Shift;
            const int row = y << m_Shift;
            for (int x = 0; x < m_Size; ++x) {
                const int xr = (x + 1) & m_Mask;
                const int xl = (x + m_Size - 1) & m_Mask;
                // Eight neighbours, no centre, halved twice.
                int32_t v = (cur[xl + up] + cur[x + up] + cur[xr + up] +
                             cur[xl + row] + cur[xr + row] + cur[xl + down] +
                             cur[x + down] + cur[xr + down]) >> 2;
                v -= other[x + row];
                other[x + row] = v - (v >> m_Damping);
            }
        }
    }

private:
    void BuildBrush() {
        const int r = m_Radius;
        // The original allocates radius*radius*16 bytes and indexes with a
        // stride of `radius` over a 2*radius span, so cells accumulate.
        m_Brush.assign(std::size_t(r) * r * 4, 0);
        for (int b = -r; b < r; ++b) {
            for (int a = -r; a < r; ++a) {
                const int d2 = a * a + b * b;
                if (d2 >= r * r) continue;
                const int idx = r * (a + r) + b + r;
                m_Brush[std::size_t(idx)] +=
                    int32_t(m_Strength * (r - std::sqrt(double(d2))));
            }
        }
    }

    std::vector<int32_t> m_Grid[2];
    std::vector<int32_t> m_Brush;
    int m_Shift = 6, m_Size = 64, m_Mask = 63;
    int m_Radius = 7, m_Strength = 5, m_Damping = 10;
    int m_Cur = 0;
};

}  // namespace bb
