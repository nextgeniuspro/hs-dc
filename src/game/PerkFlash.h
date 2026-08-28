// PerkFlash — what a perk looks like going off.
//
// The original keeps ten sprite sheets for this, `Data\Battle\gfx\perk\0.tc`
// through `9.tc`, and picks one per perk out of the table 0x1009fed0 fills in;
// 0x1009dad8 loads it by patching the digit into the one path it keeps. They
// are all 75x75, eleven to thirty-one frames, and the sheet is played *over
// every unit the perk touched* -- 0x1004a160 walks the list the apply
// collected and starts one at each -- so a heal shows over your own army and a
// poison over theirs.
//
// The table also carries a *blitter mode* per perk and level, and the two it
// uses are not the same thing at all. 0x100b3424's eleven-way jump table hands
// each mode a span routine, and:
//
//   mode 4 (0x100b4acc)  the ordinary alpha blend, through the engine's
//                        premultiplied lookup table;
//   mode 5 (0x100b4bdc)  add each channel to what is already there and clamp
//                        it, ignoring the source's alpha completely.
//
// Mode 5 is the one that "adjusts the colour" of what is under it -- the
// affected unit visibly brightens, which is how the game says that unit is the
// one that got the boost. Weighting that add by the sheet's own alpha, which
// ramps from three to ten, turns the flash into a smudge. The two enemy-facing
// perks play their animation on the enemy's units instead of yours.
//
// One sheet frame per game frame: the sheets run 11 to 31 frames, which at the
// battle's own 40 ms is between half a second and a second and a quarter.
//
// The noise is the perk's own entry in `Data\Battle\sfx\perks.dat`, a bank of
// twenty-six sounds over five `perk_effect` samples. That is the sound
// manager's business, not this class's; see BattleScreen.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "game/TextureCache.h"
#include "platform/Surface.h"

namespace bb {

class BattleRenderer;
class TextureCache;
struct Texture;

class PerkFlash {
public:
    // The two modes the perk table asks for.
    static constexpr int kBlendAlpha = 4;
    static constexpr int kBlendAdd = 5;

    struct Cell {
        int X = 0, Y = 0;
    };

    // How long the plain tint runs when there is no sheet to play.
    static constexpr int kFallbackFrames = 24;

    // Load `sheet` (0..9). Loading the sheet already held is free, which
    // matters because a perk used twice running should not reload 350 KB of
    // decoded frames. False when the sheet could not be decoded -- which is a
    // real possibility on a console with sixteen megabytes and a battle
    // already in memory, and is why Begin() runs without one.
    bool Load(TextureCache& cache, int sheet);
    // Give the sheet back. The battle does this when it ends.
    void Unload();
    bool Ready() const { return m_Sheet != nullptr; }

    // Start playing over these cells, in the perk table's blend mode. Runs
    // with or without a sheet: without one the cells are washed with a plain
    // tint instead, because saying *which units the perk reached* is the point
    // of the effect and must not depend on a texture load.
    void Begin(std::vector<Cell> cells, int blend);
    // How many frames this run will take.
    int Frames() const;
    bool Active() const { return m_Active; }

    // Draw this frame and advance. Returns false once the last frame is past.
    bool Step(Surface& dst, int camX, int camY);

private:
    void DrawTint(Surface& dst, int px, int py, int strength) const;

    std::optional<TextureSet> m_Claims;
    const Texture* m_Sheet = nullptr;
    int m_Loaded = -1;

    bool m_Active = false;
    std::vector<Cell> m_Cells;
    int m_Frame = 0;
    int m_Blend = 5;
};

}  // namespace bb
