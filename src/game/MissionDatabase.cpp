#include "game/MissionDatabase.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "game/ConfigFile.h"
#include "game/FilePack.hpp"
#include "shim/Log.h"

namespace bb {
namespace {

struct Source {
    const char* Path;
    MissionDatabase::Style GameStyle;
};

const Source kSources[] = {
    {"Data\\mission_data.txt", MissionDatabase::kSingle},
    {"Data\\sub_mission_data.txt", MissionDatabase::kSub},
    {"Data\\multiplayer_short_mission_data.txt", MissionDatabase::kShort},
    {"Data\\multiplayer_normal_mission_data.txt", MissionDatabase::kNormal},
    {"Data\\tutorial_data.txt", MissionDatabase::kTutorial},
};

// The tables write a single backslash as `\\`.
std::string Unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        out.push_back(s[i]);
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\\') ++i;
    }
    return out;
}

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

// The level stems are `sp1_broken_tranquility` or `mp12-s2p_invasion_ii`, so
// the title is whatever follows the first underscore, with underscores turned
// back into spaces and each word capitalised.
std::string PrettyMissionName(const std::string& levelPath) {
    std::size_t slash = levelPath.find_last_of("\\/");
    std::string stem = slash == std::string::npos ? levelPath
                                                  : levelPath.substr(slash + 1);
    const std::size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    const std::size_t us = stem.find('_');
    if (us != std::string::npos) stem = stem.substr(us + 1);
    for (char& c : stem)
        if (c == '_') c = ' ';
    bool start = true;
    for (char& c : stem) {
        if (start) c = char(std::toupper(static_cast<unsigned char>(c)));
        start = (c == ' ' || c == '-');
    }
    return stem;
}

// The one mission the seating constructor names: 0x1004f708 compares the key
// against the literal at 0x101123a4 and gives that battle a second computer
// seat. SUB5 is a three-hander with no TEAMS line -- two enemies who are not
// allied with each other either.
constexpr const char* kTwoComputerSeats = "SUB5";

int MissionDatabase::Mission::HumanSeats() const {
    if (GameStyle == kShort || GameStyle == kNormal) return 1;
    const int ai = Key == kTwoComputerSeats ? 2 : 1;
    return std::max(1, Players - ai);
}

bool MissionDatabase::LoadFile(FilePack& pack, const std::string& path,
                               Style style) {
    ConfigFile cfg;
    if (!cfg.Load(pack, path)) {
        LogError("missions: '%s' not in the pak\n", path.c_str());
        return false;
    }
    for (const auto& s : cfg.Sections()) {
        Mission m;
        m.Key = s.Name;
        m.File = Unescape(s.Get("FILE"));
        if (m.File.empty()) continue;
        m.Name = PrettyMissionName(m.File);
        m.Players = s.GetInt("NUMBER_OF_PLAYERS", 2);
        m.Index = s.GetInt("MISSION_INDEX");
        m.TurnLimit = s.GetInt("TURNLIMIT");
        for (const std::string& mask : s.GetList("TEAMS"))
            m.Teams.push_back(uint32_t(ConfigFile::ParseInt(mask)));
        m.GameStyle = style;
        m_Missions.push_back(std::move(m));
    }
    return true;
}

bool MissionDatabase::Load(FilePack& pack) {
    m_Missions.clear();
    bool ok = true;
    for (const Source& s : kSources) ok = LoadFile(pack, s.Path, s.GameStyle) && ok;
    LogDebug("missions: %zu battles\n", m_Missions.size());
    return ok && !m_Missions.empty();
}

std::vector<const MissionDatabase::Mission*> MissionDatabase::ByStyle(
    Style style) const {
    std::vector<const Mission*> out;
    for (const Mission& m : m_Missions)
        if (m.GameStyle == style) out.push_back(&m);
    return out;
}

const MissionDatabase::Mission* MissionDatabase::ByIndex(int index) const {
    for (const Mission& m : m_Missions)
        if (m.Index == index) return &m;
    return nullptr;
}

const MissionDatabase::Mission* MissionDatabase::ByKey(
    const std::string& key) const {
    for (const Mission& m : m_Missions)
        if (Lower(m.Key) == Lower(key)) return &m;
    return nullptr;
}

const MissionDatabase::Mission* MissionDatabase::Find(
    const std::string& text) const {
    if (const Mission* m = ByKey(text)) return m;
    const std::string want = Lower(text);
    for (const Mission& m : m_Missions) {
        const std::string file = Lower(m.File);
        if (file == want) return &m;
        if (file.find(want) != std::string::npos) return &m;
        if (Lower(m.Name) == want) return &m;
    }
    return nullptr;
}

}  // namespace bb
