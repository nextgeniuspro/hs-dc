// BattleData — the three attribute tables every battle is built from.
//
// `Data\Battle\ndUnitAttributes.ini`, `ndPropertyAttributes.ini` and
// `ndTerrainAttributes.ini`, read by 0x100765dc into the tables at
// 0x10075878 (units), 0x100760c8 (properties) and 0x1007621c (terrain).
//
// The ids are *not* the order the files list things in. The engine keeps one
// name->id table (0x10072ec4) that every enum in the game resolves through,
// and the ini sections are looked up by name against it -- so `Beach` is
// terrain 5 even though it is the first section in the file. Those tables are
// transcribed here from the binary, which is also where the unit order comes
// from: `Rowing-boat` is unit 10 and `Sloop` 11, the reverse of the order the
// ini declares them in.
//
// The damage matrices live in the same file, as `AttackValues` and
// `DefenseValues`: 21 rows of 21 floats, both indexed [attacker][defender].
// The engine stores them as 8.8 fixed point (0x1007564c multiplies by 256 and
// truncates), and so does this, so the rounding matches.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bb {

class FilePack;

// Unit type ids, 0-based, from 0x10072ec4. A level file stores them 1-based.
enum UnitType : int {
    kUnitSwordsman = 0,
    kUnitPistoleer = 1,
    kUnitMusketeer = 2,
    kUnitScout = 3,
    kUnitCavalryLight = 4,
    kUnitCavalryHeavy = 5,
    kUnitMortar = 6,
    kUnitCannon = 7,
    kUnitScorchCannon = 8,
    kUnitWagon = 9,
    kUnitRowingBoat = 10,
    kUnitSloop = 11,
    kUnitTransportHeavy = 12,
    kUnitGalley = 13,
    kUnitHidsu = 14,
    kUnitMothership = 15,
    kUnitManOWar = 16,
    kUnitCannonTower = 17,
    kUnitRowingBoatSwordsman = 18,
    kUnitRowingBoatPistoleer = 19,
    kUnitRowingBoatMusketeer = 20,
    kUnitTypeCount = 21,
};

enum PropertyType : int {
    kPropDocks = 0,
    kPropHeadquarters = 1,
    kPropGarrison = 2,
    kPropShipyard = 3,
    kPropVillage = 4,
    kPropSpecialVillage1 = 5,
    kPropSpecialCastle1 = 9,
    // The last two castles are the only buildings a unit can shoot at
    // (0x100597ac: `12 <= type <= 13`), which is how SP13 and SP17 are won.
    kPropSpecialCastle4 = 12,
    kPropSpecialCastle5 = 13,
    kPropertyTypeCount = 14,
};

// The `class` field of a unit, from the same table.
enum UnitClass : int {
    kClassDirectCapture = 0,
    kClassDirect = 1,
    kClassIndirect = 2,
    kClassTransport = 3,
    kClassStationary = 4,
    kClassProduction = 5,
};

struct TerrainAttrs {
    std::string Name;
    int AttackBonus = 0;
    int DefenseBonus = 0;
    int VisionBonus = 0;
    bool CanHide = false;
    bool Breakable = false;
    int MaxHitPoints = 100;
    int Shield = 0;
    // Cost to enter, per unit type. 0 means the ini had no entry.
    int MovementCost[kUnitTypeCount] = {};
    bool Valid = false;
};

struct PropertyAttrs {
    std::string Name;
    int AttackBonus = 0;
    int DefenseBonus = 0;
    uint32_t CanProduce = 0;   // bit per unit type
    int CashRate = 0;
    int MaxCapturePoints = 20;
    int Width = 1, Height = 1;
    int Shield = 0;
    bool Valid = false;
};

struct UnitAttrs {
    std::string Name;           // `internalName`, the id table's key
    int UnitClass = kClassDirect;
    int Cost = 0;
    int Vision = 0;
    int MaxMovement = 0;
    int MaxRations = 0;
    int MaxAmmo = 0;
    int RationRate = 0;
    int AmmoRate = 0;
    int MinRange = 1;
    int MaxRange = 1;
    int BlastRadius = 0;
    int Capacity = 0;
    uint32_t MovementMask = 0;      // bit per terrain id
    uint32_t AttackCapability = 0;  // bit per unit type
    uint32_t SupplyCapability = 0;
    uint32_t ProductionCapability = 0;
    uint32_t LoadingCapability = 0;
    uint32_t CaptureCapability = 0;
    bool CounterAttack = false;
    // 8.8 fixed, indexed by the *other* unit's type.
    int Attack[kUnitTypeCount] = {};
    int Defense[kUnitTypeCount] = {};
    bool Valid = false;
};

class BattleData {
public:
    static constexpr int kTerrainCount = 18;
    static constexpr const char* kUnitIni = "Data\\Battle\\ndUnitAttributes.ini";
    static constexpr const char* kPropertyIni =
        "Data\\Battle\\ndPropertyAttributes.ini";
    static constexpr const char* kTerrainIni =
        "Data\\Battle\\ndTerrainAttributes.ini";
    // The build screen's own table: how good each unit is, on a scale of
    // fifteen, against each of the six groups the screen sorts units into.
    // These are not derived from the 21x21 damage matrices -- they are a
    // separate hand-written file the engine reads at startup into a table it
    // hands out by (type, group) (0x100366b0, 0x10036840, 0x10036854).
    static constexpr const char* kAttackDefenseTxt = "Data\\attack_defense.txt";
    static constexpr int kGroupCount = 6;

    // Load all three. Returns false if any is missing from the pak.
    bool Load(FilePack& pack);
    bool Loaded() const { return m_Loaded; }

    const UnitAttrs& Unit(int type) const;
    // 0..15, or 0 when the type or group is out of range. Group order is the
    // file's own: infantry, artillery, cavalry, cannon towers, small sea
    // vessels, large ships.
    int AttackVersus(int type, int group) const;
    int DefenceVersus(int type, int group) const;
    const PropertyAttrs& Property(int type) const;
    const TerrainAttrs& Terrain(int type) const;

    // Name -> id for each enum, as the engine's shared table resolves them.
    static int UnitId(const std::string& name);
    static int PropertyId(const std::string& name);
    static int TerrainId(const std::string& name);
    static int ClassId(const std::string& name);
    // Which of the six groups a unit is filed under on the build screen
    // (0x100369c8). Not the same thing as its combat class.
    static int GroupOf(int unitType);

    // Internal names, for logs and for matching the data files.
    static const char* UnitName(int type);
    static const char* TerrainName(int type);
    static const char* PropertyName(int type);

    // Localisation ids for the battle screens. The string table lists these in
    // its own display order rather than the engine's id order -- unit 10 is
    // the Rowing-boat but string 51 is the Sloop -- so the tables are matched
    // by name. `long` ids are the same list a hundred higher, which the info
    // panels use where there is room for the full name.
    static int UnitStringId(int type, bool longName = false);
    static int TerrainStringId(int type, bool longName = false);
    static int PropertyStringId(int type, bool longName = false);

    // The abbreviated unit name ("Swrd", "MorB", "GTow") the bottom-right
    // panel puts on its board, where the full name would not fit. These are
    // the same list two hundred higher -- 0x10054654 spells the mapping out
    // case by case and it agrees with `UnitStringId` + 200 everywhere except
    // the three loaded rowing boats, which all borrow the Rowboat's.
    static int ShortUnitStringId(int type);

    // Battle UI strings, by their real ids.
    enum StringId : int {
        kStrHp = 71,
        kStrAmmo = 72,
        kStrMove = 73,
        kStrCost = 74,
        kStrCapturePoints = 75,
        kStrProductivity = 76,
        kStrVision = 78,
        kStrRations = 79,
        kStrAttackRanges = 80,
        kStrCombatClass = 501,   // .. 505, in UnitClass order
        kStrDamage = 1700,
        kStrAttack = 1701,
        kStrCapture = 1702,
        kStrLoad = 1703,
        kStrJoin = 1704,
        kStrSupply = 1705,
        kStrBuildUnit = 1706,
        kStrWait = 1707,
        kStrUnload = 1708,
        kStrObjectives = 1709,
        kStrCancel = 1670,       // the right soft key's label
        // The action popup's own last row is a *different* string with the same
        // text: 0x100a5564 holds 62148, past the 1701..1708 table.
        kStrCancelOrder = 62148,
        kStrColourRed = 1674,     // .. 1677: red, black, blue, yellow
        kStrBattleMenu = 2050,
        // The battle menu rows, in the order 0x1004eec8 adds them, and the
        // Options submenu it opens (0x1005ac84).
        kStrPerks = 2051,
        kStrSave = 2052,
        kStrOptions = 2053,
        // One row, two labels, chosen by the game type (0x1004eec8): "Pause"
        // ordinarily, and "Game settings" in game types 5 and 6 -- the
        // networked ones, where pausing is not a thing one machine gets to do.
        // Both are in the string table in all five languages.
        kStrPause = 2054,
        kStrGameSettings = 72015,
        kStrEndCurrentGame = 2140,
        kStrEndTurn = 2055,
        kStrMissionObjectives = 2057,
        kStrCommanderInfo = 1579,
        kStrBattleInfo = 2058,
        kStrSurrender = 2059,
        // The board that row opens (0x10061444): its title is the row's own
        // name, its question this, and its two answers 2001 / 2002.
        kStrSurrenderAsk = 72016,
        kStrBreakUpTeam = 2060,
        kStrMap = 2107,
        kStrTurn = 2069,
    };

private:
    bool LoadUnits(FilePack& pack);
    bool LoadProperties(FilePack& pack);
    bool LoadTerrain(FilePack& pack);
    bool LoadAttackDefense(FilePack& pack);

    std::vector<UnitAttrs> m_Units;
    std::vector<PropertyAttrs> m_Properties;
    std::vector<TerrainAttrs> m_Terrain;
    // [type][group], both halves of Data\attack_defense.txt.
    int m_AttackGroup[kUnitTypeCount][kGroupCount] = {};
    int m_DefenceGroup[kUnitTypeCount][kGroupCount] = {};
    bool m_Loaded = false;
};

}  // namespace bb
