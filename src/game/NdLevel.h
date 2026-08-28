// NdLevel — RedLynx's `.ndl` level container: the map, its units and
// buildings, the mission's regions, and its trigger scripts.
//
// Every battle in the game is one of these. `Data\mission_data.txt` and its
// three siblings name 66 of them (17 campaign + 8 sub-missions + 34
// multiplayer + 7 tutorials); the format is identical for all of them, so a
// loader that reads one reads them all.
//
// Loader at 0x1008427c (magic and version) into 0x10083fbc (the chunk loop).
//
// Layout, all little-endian:
//
//     "NDL"                       3 bytes
//     u16 version                 must be 0x0100
//     u32 flags                   15 in every shipped file
//     u32 pad, u8 pad
//     u16 width, u16 height       in tiles
//     u8, u8                      1 and 4 in every shipped file
//     chunk*                      until end of file
//
// A chunk is `u32 size; u32 mask; u16 subtype; u8 kind;` followed by
// `size - 11` bytes of payload -- `size` counts its own 11-byte header, which
// is how the loader skips a chunk it has no handler for. Handlers are looked
// up by (mask, kind, subtype); the seven every shipped file carries are:
//
//     0x1       terrain      u16 per tile, row-major
//     0x10      properties   u32 per tile, non-zero = a building
//     0x100     units        u32 per tile, non-zero = a unit
//     0x1000    triggers     mission script and its parameters
//     0x10000   regions      named areas the triggers test against
//     0x100000  (skipped)    the loader reads and discards these records
//     0x1000000 players      up to four slots: name and human/computer
//
// The terrain word is `(variant << 8) | (type + 1)`; the loader subtracts one
// and splits the halves (0x100404cc). A property or unit word packs
// `type | owner << 8 | id << 16`, with the top two bits carrying a Cannon
// Tower's facing -- the only unit that has one.
//
// Unit types 18-20 are a rowing boat with a soldier already aboard. The
// loader creates *two* units for them (0x1004057c): the boat, and a
// Swordsman/Pistoleer/Musketeer loaded into it, taking the next free id.
// That is reproduced here rather than in the battle model, because the file
// format is where the fiction lives.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bb {

class FilePack;

class NdLevel {
public:
    // Terrain ids, from the engine's own name->id table at 0x10072ec4. The
    // same order is the bit order of a unit's `movementMask`.
    enum Terrain : uint8_t {
        kDeepWater = 0,
        kDeepWaterWithMist = 1,
        kShallowWater = 2,
        kShallowWaterWithMist = 3,
        kShallowWaterWithRocks = 4,
        kBeach = 5,
        kPlain = 6,
        kForest = 7,
        kMountain = 8,
        kWall = 9,
        kBreakableWall = 10,
        kRoad = 11,
        kBridge = 12,
        kDam = 13,
        kDocksTerrain = 16,
        kShipyardTerrain = 17,
        kTerrainCount = 18,
    };

    struct Tile {
        uint8_t Terrain = kDeepWater;
        uint8_t Variant = 0;   // which of the type's graphics to draw
    };

    // A building or a unit as the file places it. `id` is the editor's own
    // instance number, which the trigger scripts refer to.
    struct Placement {
        uint8_t X = 0, Y = 0;
        uint8_t Type = 0;      // 1-based in the file, kept 1-based here
        uint8_t Owner = 0;     // 0 = neutral, 1..4 = a player slot
        uint16_t ID = 0;
        uint8_t Facing = 0;    // Cannon Tower only
    };

    // An area the mission scripts test against: a bounding box plus a mask of
    // which cells inside it belong.
    struct Region {
        int Index = 0;
        uint8_t Kind = 0, Flags = 0;
        uint8_t X = 0, Y = 0, W = 0, H = 0;
        std::vector<uint8_t> Mask;
        bool Contains(int cx, int cy) const {
            const int dx = cx - X, dy = cy - Y;
            if (dx < 0 || dy < 0 || dx >= W || dy >= H) return false;
            return Mask[std::size_t(dy) * W + dx] != 0;
        }
    };

    // A mission script. The three lists are the compiler's three inputs
    // (0x100c3070 hands them to TriggerCompiler::compileEvents /
    // compileConditions / compileActions).
    struct Trigger {
        uint16_t ID = 0;
        std::vector<std::string> Events;
        std::vector<std::string> Conditions;
        std::vector<std::string> Actions;
    };

    // `TriggerType`/`PropertyType`/`PointType`... assignments the editor wrote
    // alongside the triggers. Kept verbatim; the engine only acts on three of
    // the keys and reads past the rest.
    struct Param {
        uint32_t Target = 0;
        std::vector<std::pair<std::string, std::string>> Attrs;
    };

    // A param seen the way the scripts see it: `variable(N)` names the param
    // whose `target` is N. The engine builds a record for exactly three of the
    // attribute keys (0x10083a94 compares against "BooleanType",
    // "PropertyType" and "RegionType" and reads past everything else), and a
    // region's value is its four comma-separated corners run through the same
    // tokeniser the point parser uses -- `x1,y1,x2,y2`, inclusive, and
    // normalised with min/max at every use.
    //
    // Unit, property and trigger handles need no record: they are matched
    // against the object's own id at runtime (0x100c8018 compares
    // `arg.value == object->id()`), and the level file already carries that id
    // in the top half of each placement word.
    struct Variable {
        enum VariableKind { kNone, kRegion, kBoolean, kProperty, kUnit, kTrigger,
                                  kObject };
        VariableKind Kind = kNone;
        int X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;   // kRegion
        bool Flag = false;                     // kBoolean
        int X = 0, Y = 0;    // the PointType every param carries
        bool HasPoint = false;
    };

    struct Player {
        bool Computer = false;
        std::string Name;
    };

    bool Load(FilePack& pack, const std::string& path);
    bool Parse(const std::vector<uint8_t>& data);

    bool Valid() const { return m_Width > 0 && m_Height > 0; }
    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    const std::string& Path() const { return m_Path; }

    const Tile& At(int x, int y) const { return m_Tiles[std::size_t(y) * m_Width + x]; }
    const std::vector<Tile>& Tiles() const { return m_Tiles; }
    const std::vector<Placement>& Properties() const { return m_Properties; }
    const std::vector<Placement>& Units() const { return m_Units; }
    const std::vector<Region>& Regions() const { return m_Regions; }
    const std::vector<Trigger>& Triggers() const { return m_Triggers; }
    const std::vector<Param>& Params() const { return m_Params; }
    // The param a script's `variable(id)` names, or null. Note that the id is
    // the param's `target` field, not its position: the editor leaves gaps.
    const Variable* ScriptVariable(int id) const;
    const std::vector<Player>& Players() const { return m_Players; }

    // Unit types 19-21 (1-based) are a boat with a passenger. Returns the
    // passenger's 1-based type, or 0 when `type` is an ordinary unit.
    static int PassengerOf(int type);

private:
    bool ReadTerrain(const uint8_t* p, std::size_t n);
    bool ReadPlacements(const uint8_t* p, std::size_t n,
                        std::vector<Placement>& out, bool units);
    bool ReadTriggers(const uint8_t* p, std::size_t n);
    bool ReadRegions(const uint8_t* p, std::size_t n);
    void NoteVariable(const Param& p);
    bool ReadPlayers(const uint8_t* p, std::size_t n);

    std::string m_Path;
    int m_Width = 0, m_Height = 0;
    std::vector<Tile> m_Tiles;
    std::vector<Placement> m_Properties;
    std::vector<Placement> m_Units;
    std::vector<Region> m_Regions;
    std::vector<Trigger> m_Triggers;
    std::vector<Param> m_Params;
    std::map<int, Variable> m_Variables;
    std::vector<Player> m_Players;
};

}  // namespace bb
