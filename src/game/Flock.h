// Flock — the engine's boid specks (0x100716f4, 0x100718cc, 0x10071e98).
//
// One class serves two screens. The travel map scatters five flocks over the
// sea; the main menu keeps one more, built right inside the water object
// (0x100d8c50) with its home at (0, 0) and drawn with origin (-100, -128), so
// it mills over the middle of the menu's water. Not one member is a sprite:
// each is a short streak drawn straight to the screen, coloured by its own
// speed -- a slow one is a dark speck on the water and a fast one a white
// dash, which is what makes some read as birds and some as fish.
//
// The trick that keeps them airborne is the goal: every update the flock
// re-rolls a random point in a thirty-pixel box by its home and every member
// steers at a blend of that and the crowd's centre, so there is always
// somewhere new to be. A port that let them reach equilibrium showed an
// empty sea.
#pragma once

#include <cstdint>

namespace bb {

class Surface;

struct Flock {
    // 0x100716f4's numbers.
    static constexpr int kSize = 10;      // members per flock
    static constexpr int kScatter = 30;   // seeded this far round home
    static constexpr int kGather = 16;    // pool neighbours inside this
    static constexpr int kSpread = 12;    // shove off anyone inside this
    static constexpr int kWander = 30;    // the random goal's box at home
    static constexpr int kScare = 16;     // the ship scatters inside this
    static constexpr int kRange = 300;    // drawn no further off than this
    static constexpr int kStepGate = 200; // stepped only this near the camera

    // A member: position and velocity, both 16.16.
    struct Boid {
        int X = 0, Y = 0, Vx = 0, Vy = 0;
    };

    struct Home {
        int X = 0, Y = 0;
    } Home;
    Boid Member[kSize];
    // 0x100718cc's own tick counter: the full update runs every other step.
    int Counter = 0;

    // Seed the members round home, velocities zero (0x100716f4 zeroes them;
    // the world loader scatters, 0x100d95e4).
    void Scatter(uint32_t& rng);

    // One tick: gathers, spreads, wanders, and flees `ship`. Skipped in full
    // when the camera is further than kStepGate from home, and the real work
    // runs only on the counter's odd beats.
    void Step(int shipX, int shipY, int camX, int camY, uint32_t& rng);

    // Draw the streaks. Culled outright past kRange from `origin` (the view's
    // top-left corner, which is what 0x10071e98 is handed).
    void Draw(Surface& dst, int originX, int originY) const;
};

}  // namespace bb
