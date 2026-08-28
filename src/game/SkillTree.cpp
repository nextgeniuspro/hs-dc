#include "game/SkillTree.h"

#include "game/ConfigFile.h"
#include "game/FilePack.hpp"
#include "shim/Log.h"

namespace bb {
namespace {

const SkillTree::Node kMissing;

std::vector<int> IntList(const ConfigFile::Section& s, const char* key) {
    std::vector<int> out;
    for (const std::string& field : s.GetList(key))
        out.push_back(ConfigFile::ParseInt(field, -1));
    return out;
}

}  // namespace

bool SkillTree::LoadOne(FilePack& pack, const std::string& path,
                        std::vector<Node>& out) {
    ConfigFile cfg;
    if (!cfg.Load(pack, path)) {
        LogError("skills: '%s' not in the pak\n", path.c_str());
        return false;
    }
    for (const auto& s : cfg.Sections()) {
        // The section name is the id, and the files number them from zero with
        // no gaps -- but an id the array cannot hold is skipped rather than
        // grown into, because the array width is the engine's, not the file's.
        const int id = ConfigFile::ParseInt(s.Name, -1);
        if (id < 0 || std::size_t(id) >= out.size()) continue;
        Node& n = out[std::size_t(id)];
        n.ID = id;
        n.Cost = s.GetInt("COST", 0);
        n.Group = s.GetInt("GROUP", 0);
        n.Disable = s.GetInt("DISABLE", -1);
        n.Exclude = s.GetInt("EXCLUDE", -1);
        n.Skills = IntList(s, "SKILLS");
        n.Perks = IntList(s, "PERKS");
        n.Listed = true;
    }
    return true;
}

bool SkillTree::Load(FilePack& pack, bool multiplayer) {
    m_Skills.assign(Campaign::kSkillSlots, Node{});
    m_Perks.assign(Campaign::kPerkSlots, Node{});
    const bool a = LoadOne(pack, multiplayer ? kMpSkillPath : kSkillPath, m_Skills);
    const bool b = LoadOne(pack, multiplayer ? kMpPerkPath : kPerkPath, m_Perks);
    return a && b;
}

const SkillTree::Node& SkillTree::Skill(int id) const {
    if (id < 0 || std::size_t(id) >= m_Skills.size()) return kMissing;
    return m_Skills[std::size_t(id)];
}

const SkillTree::Node& SkillTree::Perk(int id) const {
    if (id < 0 || std::size_t(id) >= m_Perks.size()) return kMissing;
    return m_Perks[std::size_t(id)];
}

// Rule 2: an entry you already have that names this one shuts it out for good.
bool SkillTree::Blocked(const std::vector<Node>& table,
                        const std::vector<bool>& owned, int id) const {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (!table[i].Listed) continue;
        if (i >= owned.size() || !owned[i]) continue;
        if (table[i].Disable == id || table[i].Exclude == id) return true;
    }
    return false;
}

bool SkillTree::SkillUnlocked(int id, const Campaign& c) const {
    const Node& n = Skill(id);
    for (int s : n.Skills)
        if (c.HasSkill(s)) return true;
    for (int p : n.Perks)
        if (c.HasPerk(p)) return true;
    return false;
}

bool SkillTree::PerkUnlocked(int id, const Campaign& c) const {
    const Node& n = Perk(id);
    for (int s : n.Skills)
        if (c.HasSkill(s)) return true;
    for (int p : n.Perks)
        if (c.HasPerk(p)) return true;
    return false;
}

bool SkillTree::SkillLockedOut(int id, const Campaign& c) const {
    return Blocked(m_Skills, c.Skills, id);
}

bool SkillTree::PerkLockedOut(int id, const Campaign& c) const {
    return Blocked(m_Perks, c.Perks, id);
}

bool SkillTree::SkillAvailable(int id, const Campaign& c) const {
    if (!Skill(id).Listed || c.HasSkill(id)) return false;
    if (Blocked(m_Skills, c.Skills, id)) return false;
    return SkillUnlocked(id, c);
}

bool SkillTree::PerkAvailable(int id, const Campaign& c) const {
    if (!Perk(id).Listed || c.HasPerk(id)) return false;
    if (Blocked(m_Perks, c.Perks, id)) return false;
    return PerkUnlocked(id, c);
}

bool SkillTree::BuySkill(int id, Campaign& c) const {
    const Node& n = Skill(id);
    if (!SkillAvailable(id, c) || n.Cost > c.SkillPoints) return false;
    c.SkillPoints -= n.Cost;
    c.Skills[std::size_t(id)] = true;
    if (n.Exclude >= 0 && std::size_t(n.Exclude) < c.Skills.size())
        c.Skills[std::size_t(n.Exclude)] = false;
    return true;
}

bool SkillTree::BuyPerk(int id, Campaign& c) const {
    const Node& n = Perk(id);
    if (!PerkAvailable(id, c) || n.Cost > c.SkillPoints) return false;
    c.SkillPoints -= n.Cost;
    c.Perks[std::size_t(id)] = true;
    if (n.Exclude >= 0 && std::size_t(n.Exclude) < c.Perks.size())
        c.Perks[std::size_t(n.Exclude)] = false;
    return true;
}

}  // namespace bb
