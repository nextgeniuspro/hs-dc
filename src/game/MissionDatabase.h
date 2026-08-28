// MissionDatabase — the list of battles the game can start.
//
// Port of the engine's MissionDatabase (0x10095964 looks a mission up by
// index, 0x100959ec by name). It reads four brace-format tables:
//
//     Data\mission_data.txt                       the 17 campaign missions
//     Data\sub_mission_data.txt                    8 optional side missions
//     Data\multiplayer_short_mission_data.txt     22 short skirmish maps
//     Data\multiplayer_normal_mission_data.txt    12 full-size skirmish maps
//     Data\tutorial_data.txt                       7 tutorials
//
// Each entry names its `.ndl` level, how many players it seats, a style, and
// a mission index; some carry a turn limit or a `TEAMS` bitmask that groups
// players onto the same side.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bb {

class FilePack;

class MissionDatabase {
public:
    enum Style { kSingle, kSub, kShort, kNormal, kTutorial, kStyleCount };

    struct Mission {
        std::string Key;        // "SP1", "MPS4", "TUT2"
        std::string File;       // "Data\Levels\....ndl"
        std::string Name;       // the level file's own stem, tidied up
        int Players = 2;
        int Index = 0;          // MISSION_INDEX
        int TurnLimit = 0;     // 0 = no limit
        // The `TEAMS` list, in file order: one seat bitmask per team, bit `n`
        // meaning seat `n`. A team's *id* is its position here plus one, which
        // is how 0x1004ff0c numbers them -- it searches from index one, over
        // an array whose zeroth entry the loader (0x10095438) leaves zero.
        // Every shipped mission that has the key declares exactly one team.
        std::vector<uint32_t> Teams;
        Style GameStyle = kSingle;

        // How many seats the local player takes, counting from one.
        //
        // 0x1004f708 builds the seat list as `players - ai` local players
        // followed by `ai` computer ones, where `ai` is one everywhere except
        // "SUB5", which the constructor names outright. So a three-seat
        // mission with an ally is not you plus an allied AI: seats one and two
        // are *both* yours, and only the last seat thinks for itself. The
        // level files agree -- their (otherwise unread) player chunk marks
        // exactly those seats human in 43 of the 44 single-player levels.
        //
        // Multiplayer is seated from its setup screen instead of by that
        // constructor, and the port has no such screen, so those keep one.
        int HumanSeats() const;
    };

    bool Load(FilePack& pack);
    const std::vector<Mission>& All() const { return m_Missions; }
    std::vector<const Mission*> ByStyle(Style style) const;
    const Mission* ByIndex(int index) const;
    const Mission* ByKey(const std::string& key) const;
    // Accepts a level stem ("sp1_broken_tranquility"), a key ("SP1"), or a
    // full pak path -- what the --battle flag is given.
    const Mission* Find(const std::string& text) const;

private:
    bool LoadFile(FilePack& pack, const std::string& path, Style style);

    std::vector<Mission> m_Missions;
};

// "sp1_broken_tranquility" -> "Broken Tranquility".
std::string PrettyMissionName(const std::string& levelPath);

}  // namespace bb
