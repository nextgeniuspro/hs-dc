// SeaSurface — the animated sea a battle is fought on.
//
// The battlefield's water is not a scrolling texture: it is the same ripple
// tank and refraction pass the menu background uses, at a quarter of the size.
// The tile renderer builds two of them (0x100b6750):
//
//     deep water     Data\Battle\gfx\tiles\water2.tc
//     shallow water  Data\Battle\gfx\tiles\water.tc
//
// both 32x32, each wrapped in `FUN_100d7a0c(sim, 4, 5, 10, 0x10, 0x20, 0x20,
// 0x20)` -- drop radius 4, strength 5, one droplet every sixteenth frame, a
// 32x32 grid over a 32x32 source. But only the deep sea actually simulates:
// 0x100d79bc frees the shallow sim's height buffers, hands it the deep
// sim's, and clears its enable flag, so the shallow water is the same
// heights refracted through the other texture -- one tank, two skins,
// rippling in unison. The result is a 32x32 buffer regenerated every frame
// and tiled across the map with a plain `& 31` wrap (0x100b853c copies it
// row by row, opaque -- there is no blending on the sea itself).
//
// That is what makes the water read as caustics rather than as something
// sliding: nothing translates, the light moves.
#pragma once

#include <cstdint>
#include <vector>

#include "game/RippleField.h"

namespace bb {

struct Texture;

class SeaSurface {
public:
    // 0x100d7a0c's arguments for the battlefield.
    static constexpr int kGridShift = 5;      // 32x32 field
    static constexpr int kSize = 1 << kGridShift;
    static constexpr int kMask = kSize - 1;
    static constexpr int kDropRadius = 4;
    static constexpr int kDropStrength = 5;
    static constexpr int kDropInterval = 16;
    // The refraction and shade shifts the constructor leaves in place
    // (sim+0x74 = 7, sim+0x78 = 4, read back at 0x100d8148).
    static constexpr int kRefractShift = 7;
    static constexpr int kShadeShift = 4;

    // Init with the battlefield's parameters: `source` must be kSize square.
    // `warmup` pre-runs the sim so the water is already moving on the first
    // frame: the battle's tile renderer takes 64 steps (0x100b6750's loop),
    // the travel map 32 (0x100d9ed0's).
    bool Init(const Texture* source, int warmup = 64);
    // The same simulation at another size, which is what the travel map's sea
    // is: `<watertexture radius="7" height="4" density="10" interval="16"
    // windx="2" windy="2">` over a 64x64 `Data\travel\gfx\water.tc`
    // (0x100d9ed0 reads the attributes and passes 0x40 for the grid).
    // `source` must be (1 << gridShift) square.
    //
    // **Wind** is what makes the travel map's water read as moving where the
    // battlefield's only shimmers. Each step subtracts the wind from a pair
    // of accumulators (0x100d7ee4), and the refraction takes an eighth of
    // those: the height field is read that much further along while the
    // source texture is read that much further back, so the ripples crawl
    // across the surface. Zero wind is the battlefield's sea.
    bool Init(const Texture* source, int gridShift, int dropRadius,
              int dropStrength, int dropInterval, int windX = 0,
              int windY = 0, int warmup = 64);
    bool Valid() const { return m_Src != nullptr; }

    // How hard the ripple bends the light and how deep its shadows go
    // (0x100d83e0, sim+0x74 and +0x78). The battlefield and the travel map
    // leave the constructor's 7 and 4 alone; the fight animation's sea sets
    // 6 and 4, which is a visibly stronger refraction. Safe to call after
    // Init -- the shifts only shape the output pixels, never the heights, so
    // a warm-up run under the old pair is not wasted.
    void SetShifts(int refract, int shade) {
        m_RefractShift = refract;
        m_ShadeShift = shade;
    }

    // Refract `owner`'s ripple field instead of simulating one of our own.
    // This is 0x100d79bc: the battle's shallow sea frees its buffers, adopts
    // the deep sea's, and its enable flag drops -- it never drips or
    // propagates again, it only refracts its own texture through the shared
    // heights. That is why deep and shallow water ripple in unison.
    void AdoptField(const SeaSurface& owner) { m_Shared = &owner; }

    // Advance one frame and regenerate the tile. The ripple itself only moves
    // on every other call -- see the note in the .cpp.
    void Step();

    // Size() x Size() ARGB4444, opaque.
    const uint16_t* Pixels() const { return m_Out.data(); }
    int Size() const { return m_Size; }
    int Shift() const { return m_Shift; }
    int Mask() const { return m_Size - 1; }

    // The ripple field itself. The travel map reads one value per row out of
    // it -- column zero of the row -- and shoves an island's surf sideways by
    // that much, which is how a coastline breaks (0x100d87c0).
    const int32_t* Heights() const {
        return m_Shared ? m_Shared->Heights() : m_Field.Heights();
    }

private:
    void Refract();

    RippleField m_Field;
    const SeaSurface* m_Shared = nullptr;   // the field we adopted, if any
    const uint16_t* m_Src = nullptr;
    std::vector<uint16_t> m_Out;
    int m_Size = kSize;
    int m_Shift = kGridShift;
    int m_Interval = kDropInterval;
    int m_RefractShift = kRefractShift;
    int m_ShadeShift = kShadeShift;
    // Wind velocity, and the accumulators it is subtracted from.
    int m_WindVx = 0, m_WindVy = 0;
    int m_WindX = 0, m_WindY = 0;
    // The engine's own frame counter, which drives both the half-rate
    // stepping and the refraction's row parity.
    uint32_t m_Frame = 0;
    uint32_t m_Rng = 0x2545f491;
};

}  // namespace bb
