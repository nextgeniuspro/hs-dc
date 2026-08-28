// CaptureHoist — the little board that says a building is changing hands.
//
// Taking a building is not silent in the original. The moment the capture
// action lands, LocalPlayer pushes a state of its own (0x100875f8's case 0x15
// builds it through 0x1007d590 / 0x1007d024) and the map stops underneath it:
// a 50x100 plank panel appears over the building with a soldier at the foot of
// a flagpole, and the flag climbs the pole from where the building's capture
// points stood to where they stand now.
//
// Everything it draws (0x1007d410, one call a frame):
//
//   `Data\fight\gfx\hoist\panel.tc`      50x100, the plank, at the origin
//   `Data\fight\gfx\hoist\buildings.tc`  40x40 at (+12, +54), one frame per
//                                        building kind -- five of them, and
//                                        the ctor clamps anything higher to 4
//   `Data\fight\gfx\hoist\soldier.tc`    40x80 at (+4, +12), the capturing
//                                        player's colours, frame counter
//                                        ping-ponged two at a time
//   `Data\fight\gfx\hoist\flag.tc`       40x30 at (+4, +12 + rise), the same
//                                        colours, frame counter wrapped
//
// The rise is `capturePoints * 0x29999` in 16.16 -- 2.6 pixels a point -- and
// it closes on the new value at 0x14000 (1.25 px) a frame. When it arrives the
// panel holds for 32 frames and pops. Both frame counters are kept at four
// times the frame index, which is what makes the sprites run at a quarter of
// the panel's rate.
#pragma once

#include <cstdint>
#include <optional>

#include "game/TextureCache.h"
#include "platform/Surface.h"

namespace bb {

class BattleRenderer;
class TextureCache;
struct Texture;

class CaptureHoist {
public:
    // 0x29999: how far up the pole one capture point is, in 16.16.
    static constexpr int kPointRise = 0x29999;
    // 0x14000: how fast the flag closes the gap, per frame.
    static constexpr int kRiseSpeed = 0x14000;
    // 0x20: frames the finished panel is held before it pops.
    static constexpr int kHold = 0x20;
    // Where each piece sits inside the panel.
    static constexpr int kBuildingX = 12, kBuildingY = 54;
    static constexpr int kPoleX = 4, kPoleY = 12;
    static constexpr int kBuildingFrames = 5;

    bool Load(TextureCache& cache);
    bool Ready() const { return m_Panel != nullptr; }

    // Open the panel over the cell at (cellX, cellY) -- tile coordinates --
    // for a building of `propertyType` being taken by a player wearing
    // `colour`, with the capture points before and after the action.
    void Begin(int cellX, int cellY, int propertyType, int colour,
               int pointsBefore, int pointsAfter);
    bool Active() const { return m_Active; }

    // Draw this frame and advance. Returns false once the panel has closed.
    bool Step(Surface& dst, const BattleRenderer& renderer, int camX,
              int camY);

private:
    // The board's art, given back with the battle it belongs to.
    std::optional<TextureSet> m_Claims;

    const Texture* m_Panel = nullptr;
    const Texture* m_Buildings = nullptr;
    const Texture* m_Soldier = nullptr;
    const Texture* m_Flag = nullptr;

    bool m_Active = false;
    int m_CellX = 0, m_CellY = 0;
    int m_Building = 0;
    int m_Colour = 1;
    int m_Rise = 0;          // 16.16, current flag height above the rest
    int m_RiseTarget = 0;
    int m_Hold = kHold;
    // Both counters run at four times the frame they show.
    uint32_t m_SoldierStep = 0;
    int m_SoldierDir = 1;
    uint32_t m_FlagStep = 0;
};

}  // namespace bb
