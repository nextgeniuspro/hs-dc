// Water — the animated caustics layer every menu screen draws behind itself.
//
// Created at 0x100d8c50 and stored in engine resource slot 0xb9; the menu frame
// loop (0x1003aa84) fetches it and calls 0x100d8ed0 *before* drawing anything
// else, so the whole UI floats on moving water.
//
// It is a classic 2D ripple tank driving a refraction pass:
//
//   * a 64x64 double-buffered integer height field -- `RippleField`, which is
//     shared with the battlefield's sea because the engine uses one class for
//     both (see RippleField.h for the parameters each passes).
//   * the refraction pass (0x100d8148): for each pixel, take the height
//     gradient, use it to offset the lookup into the 256x256 `water128.tc`
//     source, and darken by the vertical gradient. That is what makes the light
//     ripple across the surface.
//
// One detail worth keeping rather than tidying up: the refraction pass updates
// only every other row per frame, alternating parity — the original halving its
// cost on a 104 MHz ARM9, and visible as a faint shimmer.
//
// Two fish swim over the top (0x100d8aa8): 32 rotation frames of `fish.tc`,
// picked by heading, drawn at alpha 8 so they read as shapes *under* the
// water. Only the first fish wanders: it steers between random points on the
// screen, re-aiming within sixteen pixels of one. The second fish's target is
// overwritten with the first fish's position every frame (0x100d8ed0 copies
// it between the two step calls), so it forever chases the leader -- that is
// the shoaling.
//
// And a flock: the water object builds one boid flock of its own
// (0x100d8c50), home (0, 0), stepped every frame and drawn with origin
// (-100, -128), so its specks mill over the middle of the menu. See Flock.h.
#pragma once

#include <cstdint>
#include <vector>

#include "game/Flock.h"
#include "game/RippleField.h"

namespace bb {

class Surface;
class TextureCache;
struct Texture;

class Water {
public:
    // Grid and source-texture geometry, as constructed at 0x100d8c50 via
    // FUN_100d7a0c(sim, 7, 5, 10, 0x10, 0x40, 0x100, 0x100).
    static constexpr int kGrid = 64;          // ripple field is kGrid x kGrid
    static constexpr int kGridShift = 6;      // log2(kGrid)
    static constexpr int kGridMask = kGrid - 1;
    static constexpr int kDropRadius = 7;
    static constexpr int kDropStrength = 5;
    static constexpr int kDampingShift = 10;
    static constexpr int kDropInterval = 16;  // frames between droplets
    static constexpr int kTex = 256;          // water128.tc is 256x256
    static constexpr int kTexShift = 8;
    static constexpr int kTexMask = kTex - 1;
    static constexpr int kRefractShift = 3;   // sim+0x74
    static constexpr int kShadeShift = 5;     // sim+0x78
    static constexpr int kFishAlpha = 8;      // fish.tc's draw mode, 0x100d8c50
    // The retarget box: within this of the goal on both axes, pick anew.
    static constexpr int kFishReach = 0xFFFFF;
    // Where the flock's world hangs on the screen (0x100d8ed0's origin).
    static constexpr int kFlockOriginX = -100;
    static constexpr int kFlockOriginY = -128;

    bool Load(TextureCache& textures);
    bool Valid() const { return m_Src != nullptr; }

    // Step the simulation and paint the result over `screen`, fish included.
    // Overwrites every pixel, exactly as the original's Mem::Copy does.
    void Draw(Surface& screen);

    // Drop a ripple at a grid cell, e.g. to react to input.
    void Splash(int gx, int gy);

    uint32_t Frame() const { return m_Frame; }

private:
    void RenderCaustics();
    void StepFish(int i);

    const Texture* m_Water = nullptr;
    const Texture* m_Fish = nullptr;
    const uint16_t* m_Src = nullptr;  // 256x256 source pixels

    RippleField m_Field;
    std::vector<uint16_t> m_Out;  // kTex x kTex, only the visible part is written
    Flock m_Flock;
    uint32_t m_Frame = 0;
    uint32_t m_Rng = 0x13579bdfu;

    // Fish state, in 16.16 fixed point. Initial values from 0x100d8c50.
    struct Fish {
        int32_t Heading = 0;  // 0..1023, indexes the sine table
        int32_t X = 0x200000;
        int32_t Y = 0x300000;
        int32_t Turn = 0;
        int32_t TargetX = 0x2c0000;
        int32_t TargetY = 0x100000;
    };
    Fish m_FishState[2];
};

}  // namespace bb
