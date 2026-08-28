// SkillTree — what the skill chart is allowed to sell, out of the game's own
// data files.
//
// The engine builds this at 0x100a7c7c: two arrays, 0x25 skills and 0x1e
// perks, filled by 0x100a7d58 from `Data\skills\skill_data.txt` and
// `Data\skills\perk_data.txt` (a hot-seat game reads the `mp_` pair instead).
// Both files are the ordinary brace format, one section per id:
//
//     18 {
//         COST 0
//         SKILLS {| 0 |}
//         PERKS  {| |}
//         GROUP   4
//         DISABLE 20
//         EXCLUDE 23
//     }
//
// GROUP is 1..3 for the three skill columns -- Land, Support, Sea -- and 4..6
// for the perks of the same three, which is why the chart's category pages ask
// for a skill group and a perk group together.
//
// **What may be bought** (0x100a8114 for a skill, 0x100a806c for a perk, and
// they are the same function twice):
//
//   1  you do not already have it;
//   2  nothing you *do* have names it in DISABLE or EXCLUDE -- and only the
//      same kind is consulted, so a perk never locks a skill out;
//   3  it is in the group being asked about;
//   4  you have at least one of the things it lists (0x100a81ac: any one of
//      SKILLS, or any one of PERKS, is enough).
//
// Rule 4 has a consequence worth stating, because it looks like a bug
// otherwise: an entry that lists nothing at all can never be bought. Skill 0
// is such an entry, and so the tree has exactly one way in -- the perk the New
// game screen hands out, which is named by the three cheapest land skills.
//
// Buying is 0x100a93ac / 0x100a953c: pay the cost, set the flag, and clear the
// EXCLUDE'd entry if there is one. DISABLE is not applied at purchase; it is
// read by rule 2 above, which is what makes the pair of them mutually
// exclusive for the rest of the voyage.
#pragma once

#include <string>
#include <vector>

#include "game/Campaign.h"

namespace bb {

class FilePack;

class SkillTree {
public:
    // The groups, as the data files number them.
    enum Group {
        kSkillLand = 1,
        kSkillSupport = 2,
        kSkillSea = 3,
        kPerkLand = 4,
        kPerkSupport = 5,
        kPerkSea = 6,
    };

    struct Node {
        int ID = 0;
        int Cost = 0;
        int Group = 0;
        int Disable = -1;
        int Exclude = -1;
        std::vector<int> Skills;  // any one of these unlocks it
        std::vector<int> Perks;
        bool Listed = false;      // the file had a record for this id
    };

    static constexpr const char* kSkillPath = "Data\\skills\\skill_data.txt";
    static constexpr const char* kPerkPath = "Data\\skills\\perk_data.txt";
    static constexpr const char* kMpSkillPath = "Data\\skills\\mp_skill_data.txt";
    static constexpr const char* kMpPerkPath = "Data\\skills\\mp_perk_data.txt";

    // Read both files. `multiplayer` picks the `mp_` pair, which is what a hot
    // seat game gets (0x100a7c7c's `param_2`). False if neither file is there.
    bool Load(FilePack& pack, bool multiplayer = false);

    const Node& Skill(int id) const;
    const Node& Perk(int id) const;

    // Rules 1, 2 and 4 above; the group is the caller's filter, because the
    // chart asks page by page.
    bool SkillAvailable(int id, const Campaign& c) const;
    bool PerkAvailable(int id, const Campaign& c) const;

    // Rule 4 on its own: something you own names it. An entry that is unlocked
    // but unaffordable is what the chart's "Next available skill / perk" list
    // is made of.
    bool SkillUnlocked(int id, const Campaign& c) const;
    bool PerkUnlocked(int id, const Campaign& c) const;

    // Rule 2 on its own: something you own has shut it out, and no amount of
    // points will open it again. The chart does not list these at all -- they
    // are not "next", they are gone.
    bool SkillLockedOut(int id, const Campaign& c) const;
    bool PerkLockedOut(int id, const Campaign& c) const;

    // Pay for one and take it. False if it could not be bought at all -- not
    // available, or not enough points.
    bool BuySkill(int id, Campaign& c) const;
    bool BuyPerk(int id, Campaign& c) const;

private:
    bool LoadOne(FilePack& pack, const std::string& path,
                 std::vector<Node>& out);
    bool Blocked(const std::vector<Node>& table, const std::vector<bool>& owned,
                 int id) const;

    std::vector<Node> m_Skills = std::vector<Node>(Campaign::kSkillSlots);
    std::vector<Node> m_Perks = std::vector<Node>(Campaign::kPerkSlots);
};

}  // namespace bb
