// ScreenShake — the kick a framebuffer takes when something explodes.
//
// A little class of its own in the engine: built at 0x100e708c, armed at
// 0x100e712c, and applied one frame at a time by 0x100e714c. Two things
// construct one — the `.lap` cutscene player's effect kind 2 (0x10002204's
// third case), which no shipped cutscene ever schedules, and the cutaway fight
// scene, which sets one off the first time a shot lands.
//
// **Two sine waves at rates that do not divide each other.** The horizontal
// phase advances 128 of the engine's 1024-step turn every frame and the
// vertical 90, starting at 0 and 511. Eight frames to the horizontal cycle and
// about eleven and a half to the vertical, so they never line up and the
// picture wanders instead of sliding along one axis or tracing an ellipse.
//
// **The amplitude is the remaining energy, in whole pixels**, and the energy
// loses a fixed amount every frame — so the shake decays linearly and retires
// the moment it runs out rather than being given a length up front. The fight
// scene passes a different pair per weapon: a musket ball is not a mortar.
//
// **The vacated edge is not cleared.** Each row is copied over itself through
// a one-row scratch buffer, only `width - |dx|` pixels of it are written, and
// the rows the picture moved off keep whatever was already there. That smear
// down one side is what the effect looks like on the device, and clearing it
// would be a different effect.
#pragma once

#include <cstdint>
#include <vector>

namespace bb {

class Surface;

class ScreenShake {
public:
    // 0x100e714c's two phase steps, out of 1024 to the turn.
    static constexpr int kStepX = 0x80;
    static constexpr int kStepY = 0x5A;
    // Where the vertical phase starts, in both the constructor and the arming
    // call (0x100e70d4 and 0x100e7148 are both 511).
    static constexpr int kPhaseYStart = 511;

    // Arm it. `energy` is the starting amplitude in 16.16 pixels and `decay`
    // is what comes off it each frame, so the shake lasts `energy / decay`
    // frames and starts `energy >> 16` pixels wide.
    void Begin(int decay, int energy);
    bool Active() const { return m_Energy > 0; }

    // Displace `dst` and decay by one frame. Returns true once the energy has
    // run out -- which is what 0x100e714c returns, and what the fight scene
    // clears its own "shaking" flag on.
    bool Step(Surface& dst);

    // What this frame's displacement would be, without applying it. The test
    // seam: the wander is the whole character of the effect and it is much
    // easier to assert on two numbers than on a screen.
    void Offset(int* dx, int* dy) const;

private:
    void Displace(Surface& dst, int dx, int dy);

    int m_PhaseX = 0;
    int m_PhaseY = kPhaseYStart;
    int m_Energy = 0;
    int m_Decay = 0;
    std::vector<uint16_t> m_Row;   // the one-row scratch buffer, 0x160 bytes
};

}  // namespace bb
