// TravelWorld — the sea the campaign is sailed across (`Data\travel\world.xml`).
//
// One file describes the whole overworld, and the engine reads it once at
// startup (0x100d95e4 opens it, 0x100d9ad8 walks `<world><objects>` and hands
// each child to a parser by tag name):
//
//   `<island>`     a piece of land: a texture, a centre, and the polygon the
//                  ship may not sail into. 0x100da458.
//   `<mission>`    a place on the map. Most name a battle (`SP1`, `SUB3`) and
//                  carry the cutscenes that bracket it and the connections
//                  that open it; three name nothing and are scenery -- one of
//                  those is the player's start. 0x100da4c0.
//   `<encounter>`  a random sea battle, filed under one of four quadrants.
//                  0x100da198.
//
// plus `<properties><watertexture>` (the ripple tank's parameters) and
// `<pathing>`, a list of sea lanes given as pairs of points — the graph the
// WorldPathFinder routes the ship along.
//
// **Coordinates.** Positions are plain integers in world units, one to the
// pixel, and the engine hard-codes the extent after parsing rather than
// deriving it: x from -137 to 540, y from -414 to 382 (0x100d9ad8's tail).
// Quadrants are that box halved both ways, and an encounter's `area` indexes
// them in the order (top-left, top-right, bottom-left, bottom-right).
//
// **Per-mission text** hangs off the table's MISSION_INDEX rather than
// anything in this file: the travel blurb is `<desc>` and the captain's log
// entry `<log>`, both string ids, and they come out as 10200 + n and 10400 + n.
//
// The file is XML in the loosest sense — one tag per line, values as text
// between the tags, attributes only on `<vertex>`, `<point>`, `<connection>`
// and `<watertexture>` — so it is scanned rather than parsed.
#pragma once

#include <string>
#include <vector>

namespace bb {

class FilePack;

class TravelWorld {
public:
    struct Point {
        int X = 0, Y = 0;
    };

    struct Island {
        std::string Name;
        std::string Texture;         // "Data\travel\gfx\Island-05.tc"
        Point Pos;                   // centre, world units
        std::vector<Point> Polygon;  // hull, relative to the texture's corner
    };

    // What opens a location: the id of the place that has to be finished
    // first. Type 0 is the main chain, type 1 an extra prerequisite -- the
    // late missions list two or three and want all of them.
    struct Connection {
        int Type = 0;
        int ID = 0;
    };

    struct Location {
        int ID = -1;
        std::string Key;       // "SP1"; empty for the scenery nodes
        std::string Texture;
        Point Pos;
        bool Open = false;     // reachable before anything is finished
        bool Sub = false;      // <subbattle>: optional, off the main chain
        bool PlayerStart = false;
        int Desc = 0;          // travel blurb string id
        int Log = 0;           // captain's log string id
        int Skill = 0;         // skill points for finishing it
        std::string Before, Complete, Fail;   // cutscenes
        std::vector<Connection> Connections;
    };

    struct Encounter {
        int ID = 0;
        std::string Key;       // "MPS1" -- a short skirmish map
        int Area = 0;          // 0..3, the quadrant it can happen in
        int Distance = 0;      // units that must be sailed there first
        int Chance = 0;        // percent, rolled every `rate` units
        int Rate = 0;
        int Log = 0;
        int Skill = 0;
    };

    struct Lane {
        Point A, B;
    };

    struct Water {
        std::string Texture = "Data\\travel\\gfx\\water.tc";
        int Radius = 7, Height = 4, Density = 10, Interval = 16;
        int Windx = 2, Windy = 2;
    };

    static constexpr const char* kPath = "Data\\travel\\world.xml";

    // The extent 0x100d9ad8 writes over whatever the objects imply.
    static constexpr int kMinX = -137, kMinY = -414;
    static constexpr int kMaxX = 540, kMaxY = 382;
    static constexpr int kAreas = 4;

    // Which quadrant a point falls in, in the engine's order (0x100ddc6c
    // scans x fastest, so it is x + y * 2).
    static int AreaOf(Point p);

    bool Load(FilePack& pack);

    const std::vector<Island>& Islands() const { return m_Islands; }
    const std::vector<Location>& Locations() const { return m_Locations; }
    const std::vector<Encounter>& Encounters() const { return m_Encounters; }
    const std::vector<Lane>& Lanes() const { return m_Lanes; }
    const Water& WaterProps() const { return m_Water; }
    Point Start() const { return m_Start; }

    // The campaign missions in order, skipping the side missions and the
    // unnamed scenery nodes.
    std::vector<const Location*> Campaign() const;
    const Location* ByKey(const std::string& key) const;
    const Location* ById(int id) const;

private:
    std::vector<Island> m_Islands;
    std::vector<Location> m_Locations;
    std::vector<Encounter> m_Encounters;
    std::vector<Lane> m_Lanes;
    Water m_Water;
    Point m_Start;
};

// Where the voyage has got to. The engine keeps this on TravelEngineCore and
// writes it into the save under the tag `WLD01` (0x100dd40c): where the ship
// is, which locations are finished and which are open, how far has been sailed
// in each quadrant, and which encounters are used up. The port hangs it off
// Campaign, because a battle tears the map down and rebuilds it afterwards.
struct TravelState {
    bool Started = false;
    int ShipX = 0, ShipY = 0;
    int Heading = 0;                 // 0..1023, anticlockwise from north
    std::vector<int> Done;           // location ids finished
    std::vector<int> Open;           // location ids reachable now
    std::vector<int> Spent;          // encounter ids already used
    int Sailed[TravelWorld::kAreas] = {0, 0, 0, 0};
    // A quadrant is charted once something has been found in it, and only a
    // charted quadrant counts distance or throws encounters (0x100dd1cc).
    bool Known[TravelWorld::kAreas] = {false, false, false, false};
};

}  // namespace bb
