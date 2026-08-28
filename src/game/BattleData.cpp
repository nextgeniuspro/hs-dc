#include "game/BattleData.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "game/ConfigFile.h"
#include "game/FilePack.hpp"
#include "shim/Log.h"

namespace bb {
namespace {

// 0x10072ec4 registers every name the data files use against a shared id
// table. These are that table, in id order, so the index *is* the id.
const char* const kUnitNames[kUnitTypeCount] = {
    "Swordsman", "Pistoleer", "Musketeer", "Scout", "Cavalry-Light",
    "Cavalry-Heavy", "Mortar", "Cannon", "Scorch-Cannon", "Wagon",
    "Rowing-boat", "Sloop", "Transport-Heavy", "Galley", "H.I.D.S.U.",
    "Mothership", "Man-o-war", "Cannon-Tower", "Rowing-boat-Swordsman",
    "Rowing-boat-Pistoleer", "Rowing-boat-Musketeer",
};

const char* const kPropertyNames[kPropertyTypeCount] = {
    "Docks", "Headquarters", "Garrison", "Shipyard", "Village",
    "SpecialVillage1", "SpecialVillage2", "SpecialVillage3", "SpecialVillage4",
    "SpecialCastle1", "SpecialCastle2", "SpecialCastle3", "SpecialCastle4",
    "SpecialCastle5",
};

// Ids 14 and 15 are holes: the movement mask skips two bits between Dam and
// Docks, and the engine's table skips the same two.
const char* const kTerrainNames[BattleData::kTerrainCount] = {
    "DeepWater", "DeepWaterWithMist", "ShallowWater", "ShallowWaterWithMist",
    "ShallowWaterWithRocks", "Beach", "Plain", "Forest", "Mountain", "Wall",
    "BreakableWall", "Road", "Bridge", "Dam", "", "", "DocksTerrain",
    "ShipyardTerrain",
};

const char* const kClassNames[] = {
    "DIRECT_COMBAT_CAPTURE_UNIT", "DIRECT_COMBAT_UNIT", "INDIRECT_COMBAT_UNIT",
    "TRANSPORT_UNIT", "STATIONARY_UNIT", "PRODUCTION_UNIT",
};

int Lookup(const char* const* names, int count, const std::string& name) {
    for (int i = 0; i < count; ++i)
        if (name == names[i]) return i;
    return -1;
}

// The engine reads these as floats and stores 8.8 fixed (0x1007564c:
// multiply by 256.0f, then truncate toward zero).
int ToFixed88(const std::string& s) {
    return int(std::atof(s.c_str()) * 256.0);
}

const UnitAttrs kNoUnit;
const PropertyAttrs kNoProperty;
const TerrainAttrs kNoTerrain;

}  // namespace

int BattleData::UnitId(const std::string& n) {
    return Lookup(kUnitNames, kUnitTypeCount, n);
}
int BattleData::PropertyId(const std::string& n) {
    return Lookup(kPropertyNames, kPropertyTypeCount, n);
}
int BattleData::TerrainId(const std::string& n) {
    return n.empty() ? -1 : Lookup(kTerrainNames, kTerrainCount, n);
}
int BattleData::ClassId(const std::string& n) {
    return Lookup(kClassNames, 6, n);
}

const char* BattleData::UnitName(int t) {
    return t >= 0 && t < kUnitTypeCount ? kUnitNames[t] : "?";
}
const char* BattleData::PropertyName(int t) {
    return t >= 0 && t < kPropertyTypeCount ? kPropertyNames[t] : "?";
}
const char* BattleData::TerrainName(int t) {
    return t >= 0 && t < kTerrainCount ? kTerrainNames[t] : "?";
}

namespace {

// The string table's own order, matched to the engine's ids by name.
// Units: 41..61 short, 141..161 long. Terrain: 12..27 / 112..127.
// Properties: 81..94 / 181..194.
const int kUnitStrings[kUnitTypeCount] = {
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50,  // the first ten agree
    53,  // Rowing-boat  -> "Rowboat"
    51,  // Sloop
    58,  // Transport-Heavy -> "Cargo Ship"
    52,  // Galley -> "Mortar Boat"
    56,  // H.I.D.S.U. -> "Gunship"
    55,  // Mothership
    54,  // Man-o-war
    57,  // Cannon-Tower -> "Gun Tower"
    61, 59, 60,  // the three loaded rowboats
};

const int kTerrainStrings[BattleData::kTerrainCount] = {
    15,  // DeepWater
    16,  // DeepWaterWithMist
    13,  // ShallowWater
    14,  // ShallowWaterWithMist
    27,  // ShallowWaterWithRocks -> "Water rocks"
    17,  // Beach
    12,  // Plain
    19,  // Forest
    20,  // Mountain
    21,  // Wall
    22,  // BreakableWall
    23,  // Road
    24,  // Bridge
    25,  // Dam
    26, 26,
    84,  // DocksTerrain, which shares the building's name
    85,  // ShipyardTerrain
};

const int kPropertyStrings[kPropertyTypeCount] = {
    84,  // Docks
    81,  // Headquarters
    83,  // Garrison
    85,  // Shipyard
    82,  // Village
    86, 87, 88, 89,   // the four special villages
    90, 91, 92, 93, 94,  // Palace, Fortress, Prison, Hideout, Fortress
};

}  // namespace

int BattleData::UnitStringId(int t, bool longName) {
    if (t < 0 || t >= kUnitTypeCount) return 0;
    return kUnitStrings[t] + (longName ? 100 : 0);
}

int BattleData::ShortUnitStringId(int t) {
    if (t < 0 || t >= kUnitTypeCount) return 0;
    // A loaded rowing boat shows the empty one's name.
    if (t >= 18) t = 10;
    return kUnitStrings[t] + 200;
}

int BattleData::TerrainStringId(int t, bool longName) {
    if (t < 0 || t >= kTerrainCount) return 0;
    return kTerrainStrings[t] + (longName ? 100 : 0);
}

int BattleData::PropertyStringId(int t, bool longName) {
    if (t < 0 || t >= kPropertyTypeCount) return 0;
    return kPropertyStrings[t] + (longName ? 100 : 0);
}

const UnitAttrs& BattleData::Unit(int t) const {
    return t >= 0 && t < int(m_Units.size()) ? m_Units[std::size_t(t)] : kNoUnit;
}
const PropertyAttrs& BattleData::Property(int t) const {
    return t >= 0 && t < int(m_Properties.size()) ? m_Properties[std::size_t(t)]
                                                 : kNoProperty;
}
const TerrainAttrs& BattleData::Terrain(int t) const {
    return t >= 0 && t < int(m_Terrain.size()) ? m_Terrain[std::size_t(t)]
                                              : kNoTerrain;
}

bool BattleData::Load(FilePack& pack) {
    m_Units.assign(kUnitTypeCount, UnitAttrs{});
    m_Properties.assign(kPropertyTypeCount, PropertyAttrs{});
    m_Terrain.assign(kTerrainCount, TerrainAttrs{});
    // Deliberately not short-circuiting: all three should report.
    const bool units = LoadUnits(pack);
    const bool properties = LoadProperties(pack);
    const bool terrain = LoadTerrain(pack);
    const bool ad = LoadAttackDefense(pack);
    m_Loaded = units && properties && terrain && ad;
    return m_Loaded;
}

// 0x100366b0: one section per unit type, named by its number, each with an
// `attack` and a `defense` list of six values. Nothing derives these from the
// damage matrices -- they are the designers' own summary, and the build
// screen is the only thing that reads them.
bool BattleData::LoadAttackDefense(FilePack& pack) {
    ConfigFile cfg;
    if (!cfg.Load(pack, kAttackDefenseTxt)) {
        LogError("battle: %s missing\n", kAttackDefenseTxt);
        return false;
    }
    int n = 0;
    for (const auto& s : cfg.Sections()) {
        const int type = std::atoi(s.Name.c_str());
        if (type < 0 || type >= kUnitTypeCount) continue;
        const auto fill = [&](const char* key, int (&out)[kGroupCount]) {
            const std::vector<std::string>& list = s.GetList(key);
            for (int i = 0; i < kGroupCount && i < int(list.size()); ++i)
                out[i] = std::atoi(list[std::size_t(i)].c_str());
        };
        fill("attack", m_AttackGroup[type]);
        fill("defense", m_DefenceGroup[type]);
        ++n;
    }
    if (n == 0)
        LogError("battle: %s has no attack/defence rows\n", kAttackDefenseTxt);
    else
        LogDebug("battle: %d attack/defence rows\n", n);
    return n > 0;
}

int BattleData::AttackVersus(int type, int group) const {
    if (type < 0 || type >= kUnitTypeCount) return 0;
    if (group < 0 || group >= kGroupCount) return 0;
    return m_AttackGroup[type][group];
}

int BattleData::DefenceVersus(int type, int group) const {
    if (type < 0 || type >= kUnitTypeCount) return 0;
    if (group < 0 || group >= kGroupCount) return 0;
    return m_DefenceGroup[type][group];
}

// 0x100369c8, which is a plain switch and not the unit's combat class: the
// Wagon files under artillery, the Cannon Tower has a group to itself, and the
// three loaded rowing boats go with the small sea vessels.
int BattleData::GroupOf(int unitType) {
    switch (unitType) {
        case 0: case 1: case 2: return 0;             // infantry
        case 3: case 4: case 5: return 2;             // cavalry
        case 6: case 7: case 8: case 9: return 1;     // artillery
        case 10: case 11: case 13:
        case 18: case 19: case 20: return 4;          // small sea vessels
        case 12: case 14: case 15: case 16: return 5; // large ships
        case 17: return 3;                            // cannon towers
        default: return 0;
    }
}

bool BattleData::LoadUnits(FilePack& pack) {
    ConfigFile cfg;
    if (!cfg.Load(pack, kUnitIni)) {
        LogError("battle: %s missing\n", kUnitIni);
        return false;
    }
    int n = 0;
    for (const auto& s : cfg.Sections()) {
        if (s.Name == "AttackValues" || s.Name == "DefenseValues") continue;
        const int id = UnitId(s.Get("internalName"));
        if (id < 0) continue;
        UnitAttrs& u = m_Units[std::size_t(id)];
        u.Name = s.Get("internalName");
        u.UnitClass = ClassId(s.Get("class"));
        u.Cost = s.GetInt("cost");
        u.Vision = s.GetInt("vision");
        u.MaxMovement = s.GetInt("maxMovementPoints");
        u.MaxRations = s.GetInt("maxRations");
        u.MaxAmmo = s.GetInt("maxAmmunition");
        u.RationRate = s.GetInt("rationConsumeRate");
        u.AmmoRate = s.GetInt("ammunitionConsumeRate");
        u.MinRange = s.GetInt("minAttackRange", 1);
        u.MaxRange = s.GetInt("maxAttackRange", 1);
        u.BlastRadius = s.GetInt("attackBlastRadius");
        u.Capacity = s.GetInt("loadingCapacity");
        u.MovementMask = uint32_t(s.GetInt("movementMask"));
        u.AttackCapability = uint32_t(s.GetInt("attackCapability"));
        u.SupplyCapability = uint32_t(s.GetInt("supplyCapability"));
        u.ProductionCapability = uint32_t(s.GetInt("productionCapability"));
        u.LoadingCapability = uint32_t(s.GetInt("loadingCapability"));
        u.CaptureCapability = uint32_t(s.GetInt("captureCapability"));
        u.CounterAttack = s.GetBool("isAbleToCounterAttack");
        u.Valid = true;
        ++n;
    }

    // The two 21x21 matrices. Rows are keyed by internalName, so they resolve
    // through the same id table the unit sections did.
    for (const char* which : {"AttackValues", "DefenseValues"}) {
        const ConfigFile::Section* s = cfg.Find(which);
        if (!s) continue;
        const bool attack = std::string(which) == "AttackValues";
        for (const auto& row : s->Entries) {
            const int id = UnitId(row.Key);
            if (id < 0 || !row.IsList) continue;
            int* dst = attack ? m_Units[std::size_t(id)].Attack
                              : m_Units[std::size_t(id)].Defense;
            const int count = int(row.List.size()) < kUnitTypeCount
                                  ? int(row.List.size())
                                  : kUnitTypeCount;
            for (int i = 0; i < count; ++i) dst[i] = ToFixed88(row.List[std::size_t(i)]);
        }
    }
    if (n != kUnitTypeCount)
        LogError("battle: %s has %d of %d unit types\n", kUnitIni, n,
                 kUnitTypeCount);
    else
        LogDebug("battle: %d unit types\n", n);
    return n == kUnitTypeCount;
}

bool BattleData::LoadProperties(FilePack& pack) {
    ConfigFile cfg;
    if (!cfg.Load(pack, kPropertyIni)) {
        LogError("battle: %s missing\n", kPropertyIni);
        return false;
    }
    int n = 0;
    for (const auto& s : cfg.Sections()) {
        const int id = PropertyId(s.Name);
        if (id < 0) continue;
        PropertyAttrs& p = m_Properties[std::size_t(id)];
        p.Name = s.Name;
        p.AttackBonus = s.GetInt("attackBonus");
        p.DefenseBonus = s.GetInt("defenseBonus");
        p.CanProduce = uint32_t(s.GetInt("unitsAbleToProduce"));
        p.CashRate = s.GetInt("cashProduceRate");
        p.MaxCapturePoints = s.GetInt("maxCapturePoints", 20);
        p.Width = s.GetInt("width", 1);
        p.Height = s.GetInt("height", 1);
        p.Shield = s.GetInt("shield");
        p.Valid = true;
        ++n;
    }
    if (n != kPropertyTypeCount)
        LogError("battle: %s has %d of %d property types\n", kPropertyIni, n,
                 kPropertyTypeCount);
    else
        LogDebug("battle: %d property types\n", n);
    return n == kPropertyTypeCount;
}

bool BattleData::LoadTerrain(FilePack& pack) {
    ConfigFile cfg;
    if (!cfg.Load(pack, kTerrainIni)) {
        LogError("battle: %s missing\n", kTerrainIni);
        return false;
    }
    int n = 0;
    for (const auto& s : cfg.Sections()) {
        const int id = TerrainId(s.Name);
        if (id < 0) continue;
        TerrainAttrs& t = m_Terrain[std::size_t(id)];
        t.Name = s.Name;
        t.AttackBonus = s.GetInt("attackBonus");
        t.DefenseBonus = s.GetInt("defenseBonus");
        t.VisionBonus = s.GetInt("visionBonus");
        t.CanHide = s.GetBool("unitCanHide");
        t.Breakable = s.GetBool("isBreakable");
        t.MaxHitPoints = s.GetInt("maxHitPoints", 100);
        t.Shield = s.GetInt("shield");
        const auto& costs = s.GetList("movementCost");
        for (std::size_t i = 0; i < costs.size() && i < kUnitTypeCount; ++i)
            t.MovementCost[i] = std::atoi(costs[i].c_str());
        t.Valid = true;
        ++n;
    }
    // 14 + docks + shipyard; ids 14 and 15 are holes
    if (n != 16)
        LogError("battle: %s has %d of 16 terrain types\n", kTerrainIni, n);
    else
        LogDebug("battle: %d terrain types\n", n);
    return n == 16;
}

}  // namespace bb
