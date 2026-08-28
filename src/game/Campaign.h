// Campaign — the state a single-player game carries between screens.
//
// The engine spreads this over three objects: a Commander (0x1005fa50, 0x78
// bytes, name at offset 0 and a 30-byte perk array at +0x6c), a Player
// (0x1005d538, 0x60 bytes, colour at +0x30) and the TravelEngineCore that owns
// them. `New game` fills the first two in (0x1007fc4c) and parks the commander
// in resource slot 0xc1, which is where every screen afterwards finds the name
// to substitute for `[player]`.
//
// The port keeps the same three facts in one place, because that is all the
// screens actually read, plus the travel map's own state -- which is what
// decides which mission comes next (see TravelWorld.h).
#pragma once

#include <string>
#include <vector>

#include "game/TravelWorld.h"

namespace bb {

struct Campaign {
    // The engine's own default, set when `New game` builds the screen
    // (0x100a7bcc) and again by the Commander's own constructor (0x1005dbac).
    static constexpr const char* kDefaultName = "Wilhelm";
    // The perk array is 30 slots wide (0x1007fc4c clears 0x1e of them), though
    // only 26 have names.
    static constexpr int kPerkSlots = 30;
    static constexpr int kPerkCount = 26;
    // Perk name and description string ids (0x100a9c5c / 0x100a9fec).
    static constexpr int kPerkNameBase = 3000;
    static constexpr int kPerkDescBase = 3200;
    // The three the opening screen offers (0x1007f9a4).
    static constexpr int kFirstPerks[3] = {0x12, 0x14, 0x17};
    // Skills are the other half of the skill chart: 37 slots (0x100a7c7c
    // reserves 0x25 of them), named at 4000 + n and described at 4100 + n
    // (0x100a9c10, 0x100a9c34). Slot 0 is the table's inert "initial state"
    // row -- it has no prerequisites, which under the engine's own rule means
    // it can never be bought, and nothing lists it as a prerequisite except
    // the perk the opening screen gives away.
    static constexpr int kSkillSlots = 37;
    static constexpr int kSkillNameBase = 4000;
    static constexpr int kSkillDescBase = 4100;

    std::string Commander = kDefaultName;
    // The player's palette row, 1..4. The colour rows on the setup screen
    // carry ids 11, 13 and 14 and the engine takes `id - 10`, so black (2) is
    // never offered -- it belongs to the enemy.
    int Colour = 1;
    std::vector<bool> Perks = std::vector<bool>(kPerkSlots, false);
    std::vector<bool> Skills = std::vector<bool>(kSkillSlots, false);
    // What the skill chart spends. Every place on the chart is worth some
    // (`<skillpoints>` in world.xml) and finishing it pays out.
    int SkillPoints = 0;
    // The captain's log, as string ids in the order the pages were written.
    // The engine keeps the same list on the travel core (+0x108) and adds to
    // it from four places: a mission won, an encounter won, a perk taken, and
    // the two entries the last stretch of the campaign writes by hand.
    std::vector<int> Log;
    // The voyage: where the ship is, what is open, what has been sailed. This
    // is what decides which mission comes next -- there is no counter walking
    // the campaign table any more, because the map is the campaign.
    TravelState Travel;

    void ChoosePerk(int perk) {
        Perks.assign(kPerkSlots, false);
        if (perk >= 0 && perk < kPerkSlots) Perks[std::size_t(perk)] = true;
    }

    bool HasPerk(int perk) const {
        return perk >= 0 && std::size_t(perk) < Perks.size() &&
               Perks[std::size_t(perk)];
    }
    bool HasSkill(int skill) const {
        return skill >= 0 && std::size_t(skill) < Skills.size() &&
               Skills[std::size_t(skill)];
    }

    // Write a page, unless that page is already in the book. 0x10091798 walks
    // the list looking for the same id before it adds anything, so a place
    // sailed back to does not write its entry twice.
    void AddLogPage(int stringID) {
        if (stringID <= 0) return;
        for (int id : Log)
            if (id == stringID) return;
        Log.push_back(stringID);
    }
};

}  // namespace bb
