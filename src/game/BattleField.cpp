#include "game/BattleField.h"

#include <algorithm>
#include <cstdio>
#include <queue>

#include "game/Commanders.h"
#include "game/FilePack.hpp"
#include "shim/Log.h"

namespace bb {
namespace {

constexpr int kDx[4] = {0, 1, 0, -1};
constexpr int kDy[4] = {-1, 0, 1, 0};

int Manhattan(int ax, int ay, int bx, int by) {
    return std::abs(ax - bx) + std::abs(ay - by);
}

}  // namespace

// 0x10077994's table, which is `max(1, (hp + 5) / 10)` capped at ten for
// every one of its hundred entries. Kept as the formula rather than the table
// because the table is exactly this and nothing else.
int BattleField::HealthBar(int hp) {
    if (hp < 1) hp = 1;
    if (hp > kMaxHitPoints) hp = kMaxHitPoints;
    const int bar = (hp + 5) / 10;
    return bar < 1 ? 1 : (bar > 10 ? 10 : bar);
}

bool BattleField::Load(FilePack& pack, const BattleData& data,
                       const std::string& path) {
    NdLevel level;
    if (!level.Load(pack, path)) return false;
    if (!Build(level, data)) return false;
    // 0x1003bd34 loads the level and then, in the same breath, gives every
    // seat the commander its player name spells: the character id the info
    // boards are keyed by, and the one perk that commander brought. The
    // campaign overwrites the player's own seat with the voyage's perks
    // afterwards (MissionFlow), which is the order the original uses too.
    for (int p = 1; p <= kMaxPlayers; ++p) {
        CommanderDef def;
        if (!commanders::Load(pack, m_Players[std::size_t(p)].Name, def))
            continue;
        m_Character[p] = def.Type;
        m_Nationality[p] = def.Nationality;
        if (!def.Perks.empty()) SetPerks(p, def.Perks);
    }
    return true;
}

int BattleField::Character(int player) const {
    return player >= 1 && player <= kMaxPlayers ? m_Character[player] : -1;
}

void BattleField::SetCharacter(int player, int type) {
    if (player >= 1 && player <= kMaxPlayers) m_Character[player] = type;
}

int BattleField::Nationality(int player) const {
    return player >= 1 && player <= kMaxPlayers ? m_Nationality[player] : -1;
}

bool BattleField::Build(const NdLevel& level, const BattleData& data) {
    if (!level.Valid() || !data.Loaded()) return false;
    m_Data = &data;
    m_Level = level;
    m_Width = level.Width();
    m_Height = level.Height();
    m_Cells.assign(std::size_t(m_Width) * m_Height, Cell{});
    m_Units.clear();
    m_Properties.clear();
    m_Players.assign(kMaxPlayers + 1, Player{});
    m_Current = 1;
    m_Round = 0;
    m_Turn = 0;
    m_NextID = 1;

    for (std::size_t i = 0; i < m_Cells.size(); ++i) {
        m_Cells[i].Terrain = level.Tiles()[i].Terrain;
        m_Cells[i].Variant = level.Tiles()[i].Variant;
        m_Cells[i].TerrainHP = data.Terrain(m_Cells[i].Terrain).MaxHitPoints;
    }

    // The file numbers players from one; slot zero is "nobody". Eighteen of
    // the sixty-six levels carry an *empty* player chunk and four name nobody
    // at all -- the campaign seats those from the mission table and the
    // commander the player picked -- so who is playing is worked out from who
    // owns something on the map, and the chunk only supplies names.
    //
    // Only names. The chunk's human/computer byte is editor metadata the
    // engine never reads: 0x100834bc parses it into level+0xd4 and nothing in
    // the binary looks at that array again, while 0x1008438c -- the whole of
    // building a battle from a level -- takes the terrain, the units, the
    // buildings, the regions and the triggers and no players at all. Trusting
    // it would hand SP15 to the computer outright, which is the one level
    // whose flags disagree with the seating the game actually uses.
    for (std::size_t i = 0; i < level.Players().size() && i < kMaxPlayers; ++i) {
        Player& p = m_Players[i + 1];
        p.Name = level.Players()[i].Name;
        p.Present = !p.Name.empty();
    }
    // Until a mission table says otherwise: seat one plays, the rest think,
    // and nobody is anybody's ally.
    for (int i = 1; i <= kMaxPlayers; ++i) {
        m_Players[std::size_t(i)].Computer = i != 1;
        m_Players[std::size_t(i)].Team = kLoneTeam + i;
    }

    // 0x10040a9c. Docks and shipyards *replace* the terrain under them with
    // their own type, which is why ids 16 and 17 never appear in a level file
    // and why a boat can sail into one: the movement mask has a bit for each.
    // Everything else claims a width x height footprint, each cell recording
    // which piece of the building it is -- that is how the multi-tile castles
    // pick their sprite frame.
    for (const auto& e : level.Properties()) {
        const int type = int(e.Type) - 1;
        if (type < 0 || type >= kPropertyTypeCount) continue;
        Property p;
        p.ID = e.ID;
        p.Type = type;
        p.Owner = e.Owner;
        p.X = e.X;
        p.Y = e.Y;
        p.CapturePoints = data.Property(type).MaxCapturePoints;
        m_Properties.push_back(p);
        const int index = int(m_Properties.size()) - 1;
        Cell& origin = m_Cells[std::size_t(e.Y) * m_Width + e.X];
        origin.Property = index;
        if (type == kPropDocks) {
            origin.Terrain = NdLevel::kDocksTerrain;
        } else if (type == kPropShipyard) {
            origin.Terrain = NdLevel::kShipyardTerrain;
        } else {
            const PropertyAttrs& a = data.Property(type);
            for (int dy = 0; dy < a.Height; ++dy) {
                for (int dx = 0; dx < a.Width; ++dx) {
                    const int cx = e.X + dx, cy = e.Y + dy;
                    if (!InBounds(cx, cy)) continue;
                    Cell& c = m_Cells[std::size_t(cy) * m_Width + cx];
                    c.Property = index;
                    c.Piece = uint8_t(a.Width * dy + dx);
                }
            }
        }
        origin.TerrainHP = data.Terrain(origin.Terrain).MaxHitPoints;
    }

    // 0x1004057c: a boat-with-passenger type creates two units, the passenger
    // taking the next id and riding in the boat.
    for (const auto& e : level.Units()) {
        const int type = int(e.Type) - 1;
        if (type < 0 || type >= kUnitTypeCount) continue;
        if (e.Owner < 1 || e.Owner > kMaxPlayers) continue;
        const int boat = AddUnit(type, e.Owner, e.X, e.Y, e.ID);
        if (boat < 0) continue;
        m_Units[std::size_t(boat)].Facing = e.Facing;
        const int passenger = NdLevel::PassengerOf(e.Type);
        if (passenger > 0) {
            const int rider = AddUnit(passenger - 1, e.Owner, e.X, e.Y, e.ID + 1);
            if (rider >= 0) {
                m_Units[std::size_t(rider)].Carrier = boat;
                m_Units[std::size_t(boat)].Cargo.push_back(rider);
                // The rider is inside the boat, so it does not hold the cell.
                SetCell(e.X, e.Y, boat);
            }
        }
    }

    for (Unit& u : m_Units) {
        const TerrainAttrs& t = data.Terrain(m_Cells[std::size_t(u.Y) * m_Width + u.X].Terrain);
        u.Hidden = t.CanHide;
    }

    // Anyone who owns a unit or a building is in the battle, named or not.
    for (const Unit& u : m_Units)
        if (u.Owner >= 1 && u.Owner <= kMaxPlayers)
            m_Players[std::size_t(u.Owner)].Present = true;
    for (const Property& p : m_Properties)
        if (p.Owner >= 1 && p.Owner <= kMaxPlayers)
            m_Players[std::size_t(p.Owner)].Present = true;
    int seated = 0;
    for (int i = 1; i <= kMaxPlayers; ++i) {
        Player& p = m_Players[std::size_t(i)];
        if (!p.Present) continue;
        if (p.Name.empty()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "Player %d", i);
            p.Name = buf;
        }
        p.Alive = true;
        ++seated;
    }

    m_Visible.assign(std::size_t(kMaxPlayers + 1) * m_Cells.size(), 1);
    LogDebug("battle: %s %dx%d, %zu units, %zu buildings, %zu triggers, "
             "%d players\n",
             level.Path().c_str(), m_Width, m_Height, m_Units.size(),
             m_Properties.size(), level.Triggers().size(), seated);
    return true;
}

int BattleField::AddUnit(int type, int owner, int x, int y, int id) {
    if (!InBounds(x, y)) return -1;
    const UnitAttrs& a = m_Data->Unit(type);
    Unit u;
    u.ID = id;
    u.Type = type;
    u.Owner = owner;
    u.X = x;
    u.Y = y;
    u.HP = kMaxHitPoints;
    u.Movement = a.MaxMovement;
    u.Rations = a.MaxRations;
    u.Ammo = a.MaxAmmo;
    m_Units.push_back(u);
    const int index = int(m_Units.size()) - 1;
    if (id >= m_NextID) m_NextID = id + 1;
    SetCell(x, y, index);
    return index;
}

void BattleField::SetCell(int x, int y, int unitIndex) {
    m_Cells[std::size_t(y) * m_Width + x].Unit = unitIndex;
}

const BattleField::Unit* BattleField::UnitAt(int x, int y) const {
    if (!InBounds(x, y)) return nullptr;
    const int i = At(x, y).Unit;
    return i >= 0 ? &m_Units[std::size_t(i)] : nullptr;
}

const BattleField::Property* BattleField::PropertyAt(int x, int y) const {
    if (!InBounds(x, y)) return nullptr;
    const int i = At(x, y).Property;
    return i >= 0 ? &m_Properties[std::size_t(i)] : nullptr;
}

int BattleField::AttackBonusAt(int x, int y) const {
    // 0x10076944 prefers the building's bonus over the terrain's.
    if (const Property* p = PropertyAt(x, y))
        return m_Data->Property(p->Type).AttackBonus;
    return m_Data->Terrain(At(x, y).Terrain).AttackBonus;
}

int BattleField::DefenseBonusAt(int x, int y) const {
    if (const Property* p = PropertyAt(x, y))
        return m_Data->Property(p->Type).DefenseBonus;
    return m_Data->Terrain(At(x, y).Terrain).DefenseBonus;
}

// 0x10076bfc. The matrix entry is 8.8 fixed; multiply by hit points, add the
// terrain or building bonus (also 8.8), then round *up* to a whole number.
int BattleField::Power(int selfType, int otherType, int hp, int x, int y,
                       bool attacking) const {
    return Power(selfType, otherType, hp, x, y, attacking, kNoOne);
}

// `owner` is whose unit this is, which only the perks care about: 0x10077a4c
// and 0x10077ba0 add the sum of every active perk's attack or defence
// percentage to the terrain's own bonus before the matrix value is scaled.
int BattleField::Power(int selfType, int otherType, int hp, int x, int y,
                       bool attacking, int owner) const {
    if (otherType < 0 || otherType >= kUnitTypeCount) return 0;
    // Flaming Fandango: "all indirect units function as Scorch Guns", which is
    // the Scorch Cannon's own row of the matrix against ships.
    int rowType = selfType;
    if (attacking && owner != kNoOne && selfType != kUnitScorchCannon &&
        m_Data->Unit(selfType).UnitClass == kClassIndirect &&
        IsSeaUnit(otherType) && PerkActive(kPerkFlamingFandango, owner))
        rowType = kUnitScorchCannon;
    const UnitAttrs& a = m_Data->Unit(rowType);
    int v = (attacking ? a.Attack[otherType] : a.Defense[otherType]) * hp;
    if (owner != kNoOne) {
        const int pct = PerkBonus(
            attacking ? PerkStat::kAttack : PerkStat::kDefence, owner,
            selfType);
        if (pct != 0) v += v * pct / 100;
    }
    v += TerrainCombatBonus(x, y, attacking, owner) << 8;
    int whole = v >> 8;
    if (v & 0xff) ++whole;
    return whole;
}

// The ground's own contribution, and the two perks that rewrite it: Tactical
// Genius lifts the bare blocks up to what a forest or a mist would give (and
// at Master level to what a village gives), and Keen Sight does the same for
// the open sea.
int BattleField::TerrainCombatBonus(int x, int y, bool attacking,
                                    int owner) const {
    const int own = attacking ? AttackBonusAt(x, y) : DefenseBonusAt(x, y);
    if (owner == kNoOne) return own;
    if (PropertyAt(x, y)) return own;

    const int terrain = At(x, y).Terrain;
    auto bonusOf = [&](int t) {
        const TerrainAttrs& ta = m_Data->Terrain(t);
        return attacking ? ta.AttackBonus : ta.DefenseBonus;
    };
    int floor = own;
    if (PerkActive(kPerkTacticalGenius, owner)) {
        if (PerkActive(kPerkTacticalGenius, owner, /*masterOnly=*/true)) {
            const PropertyAttrs& village = m_Data->Property(kPropVillage);
            floor = std::max(floor, attacking ? village.AttackBonus
                                              : village.DefenseBonus);
        } else {
            floor = std::max(floor, bonusOf(NdLevel::kForest));
        }
    }
    if (PerkActive(kPerkKeenSight, owner, /*masterOnly=*/true) &&
        IsWaterTerrain(terrain))
        floor = std::max(floor, bonusOf(NdLevel::kDeepWaterWithMist));
    return floor;
}

int BattleField::MoveCost(int unitType, int x, int y) const {
    return MoveCost(unitType, x, y, kNoOne);
}

// Keen Sight: "no ships suffer movement penalties on mist blocks", so for a
// side running it the mist costs what the water under it costs.
int BattleField::MoveCost(int unitType, int x, int y, int owner) const {
    int terrain = At(x, y).Terrain;
    if (owner != kNoOne && IsSeaUnit(unitType) &&
        PerkActive(kPerkKeenSight, owner)) {
        if (terrain == NdLevel::kDeepWaterWithMist)
            terrain = NdLevel::kDeepWater;
        else if (terrain == NdLevel::kShallowWaterWithMist)
            terrain = NdLevel::kShallowWater;
    }
    const int cost = m_Data->Terrain(terrain).MovementCost[unitType];
    return cost > 0 ? cost : 1;
}

bool BattleField::CanEnter(int unitType, int x, int y) const {
    if (!InBounds(x, y)) return false;
    const int terrain = At(x, y).Terrain;
    return (m_Data->Unit(unitType).MovementMask & (1u << terrain)) != 0;
}

// Dijkstra over movement points. Enemy units block; friendly ones -- an
// ally's as much as your own -- can be passed through but not stopped on.
// 0x100a4a34 hands the engine's path finder `~teamMask`, the seats that are
// *not* on the moving unit's side, as the set that blocks.
void BattleField::Reachable(int unitIndex, std::vector<int>& out) const {
    out.assign(m_Cells.size(), -1);
    const Unit* u = UnitByIndex(unitIndex);
    if (!u || !u->Alive || u->Carrier >= 0) return;
    const UnitAttrs& a = AttrsOf(*u);
    if (a.UnitClass == kClassStationary) {
        out[std::size_t(u->Y) * m_Width + u->X] = 0;
        return;
    }

    std::vector<int> best(m_Cells.size(), 0x3fffffff);
    using Node = std::pair<int, int>;  // (cost, cell)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    const std::size_t start = std::size_t(u->Y) * m_Width + u->X;
    best[start] = 0;
    open.emplace(0, int(start));
    while (!open.empty()) {
        const auto [cost, cell] = open.top();
        open.pop();
        if (cost > best[std::size_t(cell)]) continue;
        const int cx = cell % m_Width, cy = cell / m_Width;
        const int occupant = m_Cells[std::size_t(cell)].Unit;
        if (occupant < 0 || occupant == unitIndex ||
            SameTeam(m_Units[std::size_t(occupant)].Owner, u->Owner))
            out[std::size_t(cell)] = cost;
        // Stopping is blocked by any occupant that is not this unit; passing
        // through is blocked only by an enemy.
        if (occupant >= 0 && occupant != unitIndex &&
            !SameTeam(m_Units[std::size_t(occupant)].Owner, u->Owner))
            continue;
        if (occupant >= 0 && occupant != unitIndex) out[std::size_t(cell)] = -1;
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + kDx[d], ny = cy + kDy[d];
            if (!CanEnter(u->Type, nx, ny)) continue;
            const int step = cost + MoveCost(u->Type, nx, ny, u->Owner);
            if (step > u->Movement) continue;
            const std::size_t ni = std::size_t(ny) * m_Width + nx;
            if (step >= best[ni]) continue;
            best[ni] = step;
            open.emplace(step, int(ni));
        }
    }
    // A unit may always stay where it is.
    out[start] = 0;
}

bool BattleField::PathTo(int unitIndex, int x, int y,
                         std::vector<Step>& out) const {
    out.clear();
    const Unit* u = UnitByIndex(unitIndex);
    if (!u || !InBounds(x, y)) return false;
    std::vector<int> cost;
    Reachable(unitIndex, cost);
    if (cost[std::size_t(y) * m_Width + x] < 0) return false;

    // Walk back down the cost field; every step must drop by exactly the
    // cost of the cell we are leaving.
    int cx = x, cy = y;
    out.push_back({cx, cy});
    while (cx != u->X || cy != u->Y) {
        const int here = cost[std::size_t(cy) * m_Width + cx];
        bool moved = false;
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + kDx[d], ny = cy + kDy[d];
            if (!InBounds(nx, ny)) continue;
            const int there = cost[std::size_t(ny) * m_Width + nx];
            if (there < 0) continue;
            if (there + MoveCost(u->Type, cx, cy, u->Owner) == here) {
                cx = nx;
                cy = ny;
                out.push_back({cx, cy});
                moved = true;
                break;
            }
        }
        if (!moved) {
            // The cost field is only filled where the unit may *stop*; a
            // friendly-occupied cell it passed through shows as -1, so fall
            // back to a plain breadth-first walk through passable cells.
            out.clear();
            std::vector<int> from(m_Cells.size(), -1);
            std::vector<int> dist(m_Cells.size(), -1);
            std::vector<int> q{int(std::size_t(u->Y) * m_Width + u->X)};
            dist[std::size_t(q[0])] = 0;
            for (std::size_t head = 0; head < q.size(); ++head) {
                const int cell = q[head];
                const int px = cell % m_Width, py = cell / m_Width;
                for (int d = 0; d < 4; ++d) {
                    const int nx = px + kDx[d], ny = py + kDy[d];
                    if (!CanEnter(u->Type, nx, ny)) continue;
                    const std::size_t ni = std::size_t(ny) * m_Width + nx;
                    if (dist[ni] >= 0) continue;
                    const int occupant = m_Cells[ni].Unit;
                    if (occupant >= 0 && occupant != unitIndex &&
                        !SameTeam(m_Units[std::size_t(occupant)].Owner, u->Owner))
                        continue;
                    dist[ni] = dist[std::size_t(cell)] + 1;
                    from[ni] = cell;
                    q.push_back(int(ni));
                }
            }
            std::size_t at = std::size_t(y) * m_Width + x;
            if (dist[at] < 0) return false;
            while (true) {
                out.push_back({int(at % m_Width), int(at / m_Width)});
                if (from[at] < 0) break;
                at = std::size_t(from[at]);
            }
            std::reverse(out.begin(), out.end());
            return true;
        }
    }
    std::reverse(out.begin(), out.end());
    return true;
}

void BattleField::PlaceUnit(int index, int x, int y) {
    Unit& u = m_Units[std::size_t(index)];
    if (m_Cells[std::size_t(u.Y) * m_Width + u.X].Unit == index)
        SetCell(u.X, u.Y, -1);
    u.X = x;
    u.Y = y;
    SetCell(x, y, index);
    u.Hidden = m_Data->Terrain(At(x, y).Terrain).CanHide;
}

bool BattleField::MoveUnit(int unitIndex, int x, int y) {
    Unit* u = MutableUnit(unitIndex);
    if (!u || !u->Alive || u->Done || u->Carrier >= 0) return false;
    std::vector<int> cost;
    Reachable(unitIndex, cost);
    const int c = cost[std::size_t(y) * m_Width + x];
    if (c < 0) return false;
    if (At(x, y).Unit >= 0 && At(x, y).Unit != unitIndex) return false;
    ScriptEvent e;
    e.Kind = ScriptEvent::kMove;
    e.Unit = unitIndex;
    e.Player = u->Owner;
    e.FromX = u->X;
    e.FromY = u->Y;
    e.X = x;
    e.Y = y;
    // 0x100417ec stamps unit+0x50 at the end of every path it walks. Only a
    // real change of square counts here: ordering a unit to stand where it
    // already is issues no move command in the original either (0x10097ef4
    // only queues one when the destination differs), and an indirect unit
    // that stays put has to keep its shot.
    if (u->X != x || u->Y != y) {
        u->Moved = true;
        // Walking off a building gives up whatever progress was made on it:
        // the badge goes with the man, and so do the points. What is given up
        // is written down first, because the very next thing the popup may be
        // told is that the order is cancelled.
        RecordCaptureUndo(unitIndex);
        AbandonCapture(*u);
    }
    PlaceUnit(unitIndex, x, y);
    u->Movement -= c;
    RecomputeVision();
    // Unit::OnMove and Unit::OnRegionEnter both hang off the *path*, not the
    // destination: 0x100c8fa4 asks for its first and last point and fires only
    // when the unit started outside the region and finished inside it.
    RaiseEvent(e);
    return true;
}

bool BattleField::ReturnUnit(int unitIndex, int x, int y, int movement) {
    Unit* u = MutableUnit(unitIndex);
    if (!u || !u->Alive || !InBounds(x, y)) return false;
    const int occupant = At(x, y).Unit;
    if (occupant >= 0 && occupant != unitIndex) return false;
    PlaceUnit(unitIndex, x, y);
    u->Movement = movement;
    u->Done = false;
    // Cancelling an order puts the unit back as it was in every respect: the
    // engine's reset (0x100a47f4) clears unit+0x50 through FUN_100cef50 for
    // exactly this reason, so an indirect unit that was walked and then sent
    // home can still fire from where it started.
    u->Moved = false;
    // Including the capture the move gave up: the man picks his flag back up
    // and the building goes back to standing where it stood. The property has
    // to be the same one he stepped off, or this is some other unit's return
    // trip to a square that happens to hold a building.
    if (m_CaptureUndo.Unit == unitIndex && m_CaptureUndo.Property >= 0 &&
        At(x, y).Property == m_CaptureUndo.Property) {
        Property& p = m_Properties[std::size_t(m_CaptureUndo.Property)];
        p.CapturePoints = m_CaptureUndo.Points;
        p.BeingCaptured = m_CaptureUndo.BeingCaptured;
        u->Capturing = true;
    }
    m_CaptureUndo = CaptureUndo{};
    RecomputeVision();
    return true;
}

// A capture that stops before it is finished leaves nothing behind. The
// progress belongs to the man making it -- the engine keeps the flag on the
// *unit*, at unit+0x54, and there is nowhere for it to live once he is killed
// or walks away -- so the building takes its points back with him.
//
// Left out, the counter is just a number sitting on the building for whoever
// comes next: an enemy who got a neutral village halfway and was shot off it
// would hand the other half to the first friendly unit to step up. Nothing
// else sweeps that up, either. BeginTurn's upkeep only ever visits the turn
// player's *own* buildings, and a building being fought over is by definition
// not one of them until the fight is settled -- a neutral one never is.
void BattleField::AbandonCapture(Unit& u) {
    if (!u.Capturing) return;
    u.Capturing = false;
    if (!InBounds(u.X, u.Y)) return;
    const int i = At(u.X, u.Y).Property;
    if (i < 0) return;
    Property& p = m_Properties[std::size_t(i)];
    p.CapturePoints = m_Data->Property(p.Type).MaxCapturePoints;
    p.BeingCaptured = false;
}

void BattleField::RecordCaptureUndo(int unitIndex) {
    m_CaptureUndo = CaptureUndo{};
    const Unit& u = m_Units[std::size_t(unitIndex)];
    if (!u.Capturing || !InBounds(u.X, u.Y)) return;
    const int i = At(u.X, u.Y).Property;
    if (i < 0) return;
    const Property& p = m_Properties[std::size_t(i)];
    m_CaptureUndo.Unit = unitIndex;
    m_CaptureUndo.Property = i;
    m_CaptureUndo.Points = p.CapturePoints;
    m_CaptureUndo.BeingCaptured = p.BeingCaptured;
}

bool BattleField::CanAttack(int attacker, int defender) const {
    const Unit* a = UnitByIndex(attacker);
    const Unit* d = UnitByIndex(defender);
    if (!a || !d || !a->Alive || !d->Alive) return false;
    // Not your own army and not your ally's: 0x100985ac only puts the attack
    // icon under the cursor when the two owners' teams differ, and the blast
    // radius (0x100553xx) steps over anything sharing the firer's team.
    if (SameTeam(a->Owner, d->Owner)) return false;
    if (a->Carrier >= 0 || d->Carrier >= 0) return false;
    const UnitAttrs& aa = AttrsOf(*a);
    if ((aa.AttackCapability & (1u << d->Type)) == 0) return false;
    if (a->Ammo < 1) return false;
    // Artillery fires or it moves, never both in a turn. 0x10092aa8 refuses
    // the shot outright -- `class != INDIRECT_COMBAT || !unit->hasMoved()` --
    // and 0x100a4a34 keeps the Attack row off the popup for the same reason.
    // The class covers the Mortar, the Cannon, the Scorch Cannon, the Galley
    // (the mortar boat) and the H.I.D.S.U.
    if (aa.UnitClass == kClassIndirect && a->Moved) return false;
    const int dist = Manhattan(a->X, a->Y, d->X, d->Y);
    return dist >= aa.MinRange && dist <= aa.MaxRange;
}

void BattleField::AttackTargets(int unitIndex, std::vector<int>& out) const {
    out.clear();
    const Unit* a = UnitByIndex(unitIndex);
    if (!a) return;
    for (int i = 0; i < int(m_Units.size()); ++i) {
        if (!CanAttack(unitIndex, i)) continue;
        if (m_Fog && !Visible(a->Owner, m_Units[std::size_t(i)].X,
                             m_Units[std::size_t(i)].Y))
            continue;
        out.push_back(i);
    }
}

// 0x10076db8, transcribed. The float divide is the original's, not a
// convenience: `attackPower / 100.0f * (200 - defensePower)` truncated is not
// the same as the integer `attackPower * (200 - defensePower) / 100` for
// every input, and the engine rounds the way it rounds.
BattleField::CombatResult BattleField::Preview(int attacker,
                                               int defender) const {
    CombatResult r;
    if (!CanAttack(attacker, defender)) return r;
    const Unit& a = m_Units[std::size_t(attacker)];
    const Unit& d = m_Units[std::size_t(defender)];
    const UnitAttrs& aa = m_Data->Unit(a.Type);
    const UnitAttrs& da = m_Data->Unit(d.Type);

    r.Valid = true;
    r.Attacker = attacker;
    r.Defender = defender;
    r.AttackerHPBefore = r.AttackerHP = a.HP;
    r.DefenderHPBefore = r.DefenderHP = d.HP;
    r.AttackerAmmo = a.Ammo;
    r.DefenderAmmo = d.Ammo;

    const int atk = Power(a.Type, d.Type, a.HP, a.X, a.Y, true, a.Owner);
    const int def = Power(d.Type, a.Type, d.HP, d.X, d.Y, false, d.Owner);
    r.Damage = int(float(atk) / 100.0f * float(200 - def));
    if (r.Damage < 1) r.Damage = 1;
    r.AttackerAmmo -= std::max(0, aa.AmmoRate);
    r.DefenderHP = d.HP - r.Damage;
    if (r.DefenderHP <= 0) {
        r.DefenderHP = 0;
        r.DefenderDied = true;
    }

    const int dist = Manhattan(a.X, a.Y, d.X, d.Y);
    if (!r.DefenderDied && da.CounterAttack && r.DefenderAmmo > 0 &&
        dist >= da.MinRange && dist <= da.MaxRange &&
        (da.AttackCapability & (1u << a.Type)) != 0) {
        r.Countered = true;
        const int catk =
            Power(d.Type, a.Type, r.DefenderHP, d.X, d.Y, true, d.Owner);
        const int cdef =
            Power(a.Type, d.Type, r.AttackerHP, a.X, a.Y, false, a.Owner);
        r.CounterDamage = int(float(catk) / 100.0f * float(200 - cdef));
        if (r.CounterDamage < 1) r.CounterDamage = 1;
        r.DefenderAmmo -= std::max(0, da.AmmoRate);
        r.AttackerHP = a.HP - r.CounterDamage;
        if (r.AttackerHP <= 0) {
            r.AttackerHP = 0;
            r.AttackerDied = true;
        }
    }
    return r;
}

// --- obstacles ---------------------------------------------------------------

// The two kinds. A breakable wall is *terrain* (`isBreakable` in
// ndTerrainAttributes.ini marks exactly one type, and the fences on
// `tiles\wall_1.tc` are what it draws); a castle is a *property*, and only the
// last two of the five -- 0x100597ac is the whole rule, `12 <= type <= 13`.
bool BattleField::IsBreakable(int x, int y) const {
    if (!InBounds(x, y)) return false;
    return m_Data->Terrain(At(x, y).Terrain).Breakable;
}

bool BattleField::IsCastle(int propertyType) {
    return propertyType == kPropSpecialCastle4 ||
           propertyType == kPropSpecialCastle5;
}

int BattleField::ObstacleAt(int x, int y) const {
    if (!InBounds(x, y)) return -1;
    const int i = At(x, y).Property;
    if (i < 0) return -1;
    return IsCastle(m_Properties[std::size_t(i)].Type) ? i : -1;
}

bool BattleField::IsObstacle(int x, int y) const {
    if (IsBreakable(x, y) && At(x, y).TerrainHP > 0) return true;
    const int i = ObstacleAt(x, y);
    return i >= 0 && m_Properties[std::size_t(i)].HP > 0;
}

// What `Special::getHealthOfSpecial` reads off a square: the wall's own hit
// points where there is a wall, the castle's where there is a castle, and
// otherwise the tile's untouched hundred -- which is 0x100770f0's own order,
// `isBreakable ? terrain : property`.
int BattleField::ObstacleHealth(int x, int y) const {
    if (!InBounds(x, y)) return 0;
    if (IsBreakable(x, y)) return At(x, y).TerrainHP;
    const int i = ObstacleAt(x, y);
    return i >= 0 ? m_Properties[std::size_t(i)].HP : At(x, y).TerrainHP;
}

// 0x10092e58 for a wall, 0x10092cdc for a castle. A shorter list than
// CanAttack's either way: there is no type to look up in the capability mask,
// so the engine asks only whether the unit can attack *anything at all*
// (`attrs->attackCapability != 0`). The one clause the castle adds is that it
// has to belong to somebody else.
bool BattleField::CanAttackObstacle(int unitIndex, int x, int y) const {
    const Unit* u = UnitByIndex(unitIndex);
    if (!u || !u->Alive || u->Carrier >= 0) return false;
    if (!IsObstacle(x, y)) return false;
    // A square with somebody standing on it is a fight, not demolition: the
    // blast loop at 0x100471f8 only reaches the terrain where `cell.unit` is
    // empty, and the single-target path is offered on the same terms.
    if (At(x, y).Unit >= 0) return false;
    const int castle = ObstacleAt(x, y);
    if (castle >= 0 &&
        SameTeam(m_Properties[std::size_t(castle)].Owner, u->Owner))
        return false;
    const UnitAttrs& a = AttrsOf(*u);
    if (a.AttackCapability == 0) return false;
    if (u->Ammo < 1) return false;
    // The Cannon Tower is the only unit in the game with a blast radius, and
    // 0x10092e58 refuses it this action outright -- its shells reach walls
    // through the splash instead.
    if (a.BlastRadius != 0) return false;
    if (a.UnitClass == kClassIndirect && u->Moved) return false;
    if (m_Fog && !Visible(u->Owner, x, y)) return false;
    const int dist = Manhattan(u->X, u->Y, x, y);
    return dist >= a.MinRange && dist <= a.MaxRange;
}

void BattleField::ObstacleTargets(int unitIndex, std::vector<int>& out) const {
    out.clear();
    const Unit* u = UnitByIndex(unitIndex);
    if (!u) return;
    const UnitAttrs& a = AttrsOf(*u);
    if (a.AttackCapability == 0 || a.BlastRadius != 0) return;
    for (int y = 0; y < m_Height; ++y)
        for (int x = 0; x < m_Width; ++x) {
            if (Manhattan(u->X, u->Y, x, y) > a.MaxRange) continue;
            if (CanAttackObstacle(unitIndex, x, y))
                out.push_back(y * m_Width + x);
        }
}

// 0x100770f0. The same formula a fight uses, with the obstacle standing in as
// a Cannon Tower on both sides of it: the firer's attack row is read at column
// 0x11, and the obstacle's defence is the Cannon Tower's row read at the
// firer's column, scaled by whatever hit points it has left. The defence bonus
// comes off the *terrain* even under a castle, because the engine hands
// 0x10076944 no owner and it falls through to the tile's own numbers.
BattleField::ObstacleResult BattleField::PreviewObstacleAttack(int unitIndex,
                                                               int x,
                                                               int y) const {
    ObstacleResult r;
    if (!CanAttackObstacle(unitIndex, x, y)) return r;
    const Unit& u = m_Units[std::size_t(unitIndex)];
    r.Valid = true;
    r.X = x;
    r.Y = y;
    r.HPBefore = r.HPAfter = ObstacleHealth(x, y);

    const int atk =
        Power(u.Type, kUnitCannonTower, u.HP, u.X, u.Y, true, u.Owner);
    const UnitAttrs& wall = m_Data->Unit(kUnitCannonTower);
    int def = wall.Defense[u.Type] * r.HPBefore;
    def += m_Data->Terrain(At(x, y).Terrain).DefenseBonus << 8;
    int whole = def >> 8;
    if (def & 0xff) ++whole;
    r.Damage = int(float(atk) / 100.0f * float(200 - whole));
    if (r.Damage < 1) r.Damage = 1;
    r.HPAfter = r.HPBefore - r.Damage;
    if (r.HPAfter <= 0) {
        r.HPAfter = 0;
        r.Destroyed = true;
    }
    return r;
}

// 0x100475cc for a wall and 0x10041094 for a castle: both leave open ground.
// The castle's footprint goes back to Plain a tile at a time and the building
// stops being on the map at all -- the entry stays in the list, ownerless and
// at zero, because that is what a `Special::OnHealthChange` trigger reads it
// through.
void BattleField::FlattenCell(int x, int y) {
    Cell& c = m_Cells[std::size_t(y) * m_Width + x];
    c.Terrain = NdLevel::kPlain;
    c.Variant = 0;
}

void BattleField::RazeProperty(int propertyIndex) {
    Property& p = m_Properties[std::size_t(propertyIndex)];
    const PropertyAttrs& a = m_Data->Property(p.Type);
    for (int dy = 0; dy < a.Height; ++dy)
        for (int dx = 0; dx < a.Width; ++dx) {
            const int cx = p.X + dx, cy = p.Y + dy;
            if (!InBounds(cx, cy)) continue;
            Cell& c = m_Cells[std::size_t(cy) * m_Width + cx];
            if (c.Property != propertyIndex) continue;
            c.Property = -1;
            c.Piece = 0;
            FlattenCell(cx, cy);
        }
    p.Owner = 0;
    p.HP = 0;
    p.BeingCaptured = false;
}

BattleField::ObstacleResult BattleField::AttackObstacle(int unitIndex, int x,
                                                        int y) {
    const ObstacleResult r = PreviewObstacleAttack(unitIndex, x, y);
    if (!r.Valid) return r;
    const int castle = ObstacleAt(x, y);
    const bool wall = IsBreakable(x, y);
    if (wall) m_Cells[std::size_t(y) * m_Width + x].TerrainHP = r.HPAfter;
    if (castle >= 0) m_Properties[std::size_t(castle)].HP = r.HPAfter;
    // 0x1004702c ends the firer's turn and hands its ammunition back
    // unchanged. Knocking a fence down really is free.
    m_Units[std::size_t(unitIndex)].Done = true;
    // The event goes out *before* the rubble is cleared, because in the engine
    // there is no gap between the two: 0x100400c8 hands a message straight to
    // the trigger database and every other listener in the same call, so the
    // `Special::OnHealthChange` trigger has already run -- and read the zero --
    // by the time 0x100475cc swaps the tile out. The port queues its events for
    // the screen to drain a moment later, so the reading has to still be there
    // when it does. That is also why a flattened square is left *at* zero
    // rather than taking Plain's fresh hundred the way 0x100b0d94 gives it:
    // every one of the eleven shipped health triggers asks
    // `getHealthOfSpecial(...) <= 0`, SP6 is won by it, and nothing in the game
    // reads the hit points of a square that cannot be broken.
    ScriptEvent e;
    e.Kind = ScriptEvent::kHealthChange;
    e.Unit = unitIndex;
    e.Player = m_Units[std::size_t(unitIndex)].Owner;
    e.Property = castle;
    // A castle answers on every tile of its footprint, but the editor wrote
    // one point beside its handle and that is the building's own corner, so
    // that is the square the event names -- otherwise shooting the far end of
    // SP17's keep would say nothing the script recognised.
    e.X = castle >= 0 ? m_Properties[std::size_t(castle)].X : x;
    e.Y = castle >= 0 ? m_Properties[std::size_t(castle)].Y : y;
    e.Before = r.HPBefore;
    e.Value = r.HPAfter;
    RaiseEvent(e);
    if (r.Destroyed) {
        if (castle >= 0) RazeProperty(castle);
        else if (wall) FlattenCell(x, y);
    }
    RecomputeVision();
    return r;
}

BattleField::CombatResult BattleField::Attack(int attacker, int defender) {
    CombatResult r = Preview(attacker, defender);
    if (!r.Valid) return r;
    Unit& a = m_Units[std::size_t(attacker)];
    Unit& d = m_Units[std::size_t(defender)];
    // Announce the exchange before it is applied, so the cutaway can open on
    // the position as it stood. The engine's own attack event carries the same
    // before/after pair (0x10076db8's result struct, which is what
    // 0x1006bb4c reads its hit points and its counter-attack flag out of).
    {
        ScriptEvent e;
        e.Kind = ScriptEvent::kFight;
        e.Unit = attacker;
        e.Other = defender;
        e.Player = a.Owner;
        e.X = d.X;
        e.Y = d.Y;
        e.FromX = a.X;
        e.FromY = a.Y;
        e.AttackerHPBefore = r.AttackerHPBefore;
        e.AttackerHPAfter = r.AttackerHP;
        e.DefenderHPBefore = r.DefenderHPBefore;
        e.DefenderHPAfter = r.DefenderHP;
        e.Countered = r.Countered;
        RaiseEvent(e);
    }
    a.Ammo = r.AttackerAmmo;
    d.Ammo = r.DefenderAmmo;
    a.HP = r.AttackerHP;
    d.HP = r.DefenderHP;
    a.Done = true;
    if (r.DefenderDied) {
        ++m_Players[std::size_t(a.Owner)].Stats.UnitsDestroyed;
        ++m_Players[std::size_t(d.Owner)].Stats.UnitsLost;
        RemoveUnit(defender);
    }
    if (r.AttackerDied) {
        ++m_Players[std::size_t(d.Owner)].Stats.UnitsDestroyed;
        ++m_Players[std::size_t(a.Owner)].Stats.UnitsLost;
        RemoveUnit(attacker);
    }
    RecomputeVision();
    return r;
}

void BattleField::RemoveUnit(int index) {
    Unit& u = m_Units[std::size_t(index)];
    if (!u.Alive) return;
    for (int c : u.Cargo) {
        if (c >= 0 && c < int(m_Units.size())) {
            if (m_Units[std::size_t(c)].Alive) m_Deaths.push_back(c);
            m_Units[std::size_t(c)].Alive = false;
            m_Units[std::size_t(c)].Carrier = -1;
        }
    }
    u.Cargo.clear();
    // Killed on the building he was taking: the flag comes down and the
    // building is whole again. This is the case that made the rule worth
    // writing down -- an enemy destroyed mid-capture used to leave his half
    // of the work standing there for the other side to finish.
    AbandonCapture(u);
    m_Deaths.push_back(index);
    // A commander's bar fills from their losses: 0x10046cac hands each side
    // the price of whatever it just lost (0x10076db8's two price fields).
    AddPerkPoints(u.Owner, AttrsOf(u).Cost);
    if (u.Carrier >= 0) {
        Unit& t = m_Units[std::size_t(u.Carrier)];
        t.Cargo.erase(std::remove(t.Cargo.begin(), t.Cargo.end(), index),
                      t.Cargo.end());
        u.Carrier = -1;
    } else if (m_Cells[std::size_t(u.Y) * m_Width + u.X].Unit == index) {
        SetCell(u.X, u.Y, -1);
    }
    u.Alive = false;
    // 0x10041378's tail: with the unit gone, count what its owner has left --
    // cargo included, which is why the count walks the units and not the map
    // -- and if the answer is nothing, that side is finished. Owning a
    // shipyard does not save you; the engine asks about units and only units.
    const int owner = u.Owner;
    if (owner >= 1 && owner <= kMaxPlayers && CountUnits(owner) == 0)
        Eliminate(owner);
}

int BattleField::CountUnits(int player) const {
    int n = 0;
    for (const Unit& u : m_Units)
        if (u.Alive && u.Owner == player) ++n;
    return n;
}

int BattleField::CountProperties(int player, int type) const {
    int n = 0;
    for (const Property& p : m_Properties)
        if (p.Owner == player && p.Type == type) ++n;
    return n;
}

// 0x10041cbc.
void BattleField::Eliminate(int player) {
    if (player < 1 || player > kMaxPlayers) return;
    Player& pl = m_Players[std::size_t(player)];
    if (!pl.Present || !pl.Alive) return;
    pl.Alive = false;

    // The engine settles the battle before it tidies up, and skips the tidying
    // when the answer is already in: there is no point emptying a board that
    // nobody is going to look at again. Doing it the other way round would
    // fire a wave of Unit::OnDestroy triggers after the mission was over.
    if (Winner() != 0) return;

    for (int i = 0; i < int(m_Units.size()); ++i)
        if (m_Units[std::size_t(i)].Alive &&
            m_Units[std::size_t(i)].Owner == player)
            RemoveUnit(i);
    for (Property& p : m_Properties) {
        if (p.Owner != player) continue;
        // A headquarters is worth too much to leave lying about, so it is
        // demoted to a village first (0x10041cbc: `setType(4)` when the type
        // is 1) and only then let go.
        if (p.Type == kPropHeadquarters) {
            p.Type = kPropVillage;
            p.CapturePoints = m_Data->Property(p.Type).MaxCapturePoints;
        }
        p.Owner = 0;
        p.BeingCaptured = false;
    }
}

bool BattleField::CanCapture(int unitIndex) const {
    const Unit* u = UnitByIndex(unitIndex);
    if (!u || !u->Alive || u->Carrier >= 0) return false;
    const Property* p = PropertyAt(u->X, u->Y);
    // An ally's building is not a prize. The action builder's capture gate
    // (0x100a4a34) ends by comparing the property owner's team against the
    // unit owner's and striking the row out when they match.
    if (!p || SameTeam(p->Owner, u->Owner)) return false;
    const UnitAttrs& a = AttrsOf(*u);
    if ((a.CaptureCapability & (1u << p->Type)) == 0) return false;
    // 0x10077834 gives up before touching the building when the unit is out of
    // ammunition, so an empty unit cannot make progress. It does not *spend*
    // any: the engine never calls the ammunition setter on this path. The
    // original still offers the action and lets the turn be wasted; the port
    // hides it instead, which is the same rule with the dead end closed.
    return u->Ammo > 0;
}

bool BattleField::Capture(int unitIndex) {
    if (!CanCapture(unitIndex)) return false;
    Unit& u = m_Units[std::size_t(unitIndex)];
    const int propertyIndex = At(u.X, u.Y).Property;
    Property& p = m_Properties[std::size_t(propertyIndex)];
    // Plundering Blitz at Master level: "your units capture in one turn, even
    // if they're damaged", so a wounded soldier counts as a whole one.
    const int rate =
        PerkActive(kPerkPlunderingBlitz, u.Owner, /*masterOnly=*/true)
            ? m_Data->Property(p.Type).MaxCapturePoints
            : HealthBar(u.HP);
    // Progress belongs to the man making it, so a unit that is not already at
    // work on this building starts it whole whatever the counter says.
    // AbandonCapture normally has the points back long before this; the rule
    // is stated here as well so that no way into a capture -- an unload, a
    // script putting a unit down, anything the port grows later -- can inherit
    // somebody else's half-finished job.
    if (!u.Capturing) {
        p.CapturePoints = m_Data->Property(p.Type).MaxCapturePoints;
        p.BeingCaptured = false;
    }
    const bool first = !p.BeingCaptured;
    const int was = p.Owner;
    const int wasPoints = p.CapturePoints;
    p.CapturePoints = std::max(0, p.CapturePoints - rate);
    p.BeingCaptured = true;
    u.Capturing = true;
    u.Done = true;
    ScriptEvent e;
    e.Unit = unitIndex;
    e.Property = propertyIndex;
    e.Player = was;          // the owner the script's fourth argument names
    e.Other = u.Owner;
    e.X = p.X;
    e.Y = p.Y;
    // What the flag-raising board is told: the building's kind, the taker's
    // colour, and how many capture points stood there before and after.
    ScriptEvent hoist = e;
    hoist.Kind = ScriptEvent::kCaptureProgress;
    hoist.Value = p.Type;
    hoist.Before = Colour(u.Owner);
    hoist.FromX = wasPoints;
    hoist.FromY = p.CapturePoints;
    if (p.CapturePoints > 0) {
        if (first) {
            e.Kind = ScriptEvent::kCaptureStart;
            RaiseEvent(e);
        }
        RaiseEvent(hoist);
        return false;
    }
    if (p.Owner >= 1 && p.Owner <= kMaxPlayers) {
        ++m_Players[std::size_t(p.Owner)].Stats.PropertiesLost;
        if (p.Type == kPropHeadquarters)
            ++m_Players[std::size_t(p.Owner)].Stats.HeadquartersLost;
    }
    ++m_Players[std::size_t(u.Owner)].Stats.PropertiesCaptured;
    p.Owner = u.Owner;
    p.CapturePoints = m_Data->Property(p.Type).MaxCapturePoints;
    p.BeingCaptured = false;
    u.Capturing = false;
    if (first) {
        e.Kind = ScriptEvent::kCaptureStart;
        RaiseEvent(e);
    }
    e.Kind = ScriptEvent::kCaptureCompleted;
    RaiseEvent(e);
    RaiseEvent(hoist);
    // The other way a side is finished: its headquarters changed hands and it
    // has none left (0x10041e64's tail). Both halves are needed. The count
    // alone would end anyone holding a village on a map with no headquarters
    // at all, and the "one was taken" tally alone would end a side that still
    // has a second one standing. The check comes after the capture has been
    // announced, so a script watching for Property::OnCaptureCompleted hears
    // about the building before it hears about the funeral.
    if (was >= 1 && was <= kMaxPlayers &&
        m_Players[std::size_t(was)].Stats.HeadquartersLost > 0 &&
        CountProperties(was, kPropHeadquarters) == 0)
        Eliminate(was);
    return true;
}

bool BattleField::CanSupply(int unitIndex) const {
    const Unit* u = UnitByIndex(unitIndex);
    if (!u || !u->Alive || u->Carrier >= 0) return false;
    if (AttrsOf(*u).SupplyCapability == 0) return false;
    // A wagon works for the whole side, not just its own seat: both the
    // action (0x100479e8) and the row that offers it (0x10093b3c) accept a
    // neighbour whose owner is on the supplier's team.
    for (int d = 0; d < 4; ++d) {
        const Unit* n = UnitAt(u->X + kDx[d], u->Y + kDy[d]);
        if (n && n->Alive && SameTeam(n->Owner, u->Owner) &&
            (AttrsOf(*u).SupplyCapability & (1u << n->Type)) != 0)
            return true;
    }
    return false;
}

int BattleField::Supply(int unitIndex) {
    if (!CanSupply(unitIndex)) return 0;
    Unit& u = m_Units[std::size_t(unitIndex)];
    const uint32_t mask = AttrsOf(u).SupplyCapability;
    int n = 0;
    for (int d = 0; d < 4; ++d) {
        const int nx = u.X + kDx[d], ny = u.Y + kDy[d];
        if (!InBounds(nx, ny)) continue;
        const int i = At(nx, ny).Unit;
        if (i < 0) continue;
        Unit& t = m_Units[std::size_t(i)];
        if (!t.Alive || !SameTeam(t.Owner, u.Owner)) continue;
        if ((mask & (1u << t.Type)) == 0) continue;
        const UnitAttrs& ta = AttrsOf(t);
        t.Rations = ta.MaxRations;
        t.Ammo = ta.MaxAmmo;
        ++n;
        ScriptEvent e;
        e.Kind = ScriptEvent::kSupply;
        e.Unit = unitIndex;
        e.Other = i;
        e.Player = u.Owner;
        e.X = nx;
        e.Y = ny;
        RaiseEvent(e);
    }
    u.Done = true;
    return n;
}

bool BattleField::CanLoad(int unitIndex, int transport) const {
    const Unit* u = UnitByIndex(unitIndex);
    const Unit* t = UnitByIndex(transport);
    if (!u || !t || !u->Alive || !t->Alive) return false;
    // 0x100a4a34 tests neither owner here, and does not need to: the only way
    // to reach a transport's square is to be allowed to walk onto it, which
    // the path finder already limits to the mover's own side. Spelling it out
    // in terms of the team says the same thing without relying on the caller.
    if (!SameTeam(u->Owner, t->Owner) || unitIndex == transport) return false;
    if (u->Carrier >= 0) return false;
    const UnitAttrs& ta = AttrsOf(*t);
    if (int(t->Cargo.size()) >= ta.Capacity) return false;
    return (ta.LoadingCapability & (1u << u->Type)) != 0;
}

bool BattleField::LoadUnit(int unitIndex, int transport) {
    if (!CanLoad(unitIndex, transport)) return false;
    Unit& u = m_Units[std::size_t(unitIndex)];
    // Climbing aboard is walking away as far as the building is concerned. A
    // passenger boards from the square it is standing on, so it can be a
    // capturer who never had to move to do it.
    AbandonCapture(u);
    if (m_Cells[std::size_t(u.Y) * m_Width + u.X].Unit == unitIndex)
        SetCell(u.X, u.Y, -1);
    u.Carrier = transport;
    u.X = m_Units[std::size_t(transport)].X;
    u.Y = m_Units[std::size_t(transport)].Y;
    u.Done = true;
    m_Units[std::size_t(transport)].Cargo.push_back(unitIndex);
    RecomputeVision();
    ScriptEvent e;
    e.Kind = ScriptEvent::kLoad;
    e.Unit = unitIndex;        // the cargo: the script's first argument
    e.Other = transport;
    e.Player = u.Owner;
    e.X = u.X;
    e.Y = u.Y;
    RaiseEvent(e);
    return true;
}

// Is `type` one of the things that floats? The engine keeps three baked
// masks and "sea" for this purpose is the union of the sea and deep-sea ones
// (0x10076884 = isDeepSea || isSea, values 0x3c000 and 0x1e3c00). Both
// include the Cannon Tower, which can be planted on land or in water.
bool BattleField::IsSeaUnit(int type) {
    if (type < 0 || type >= 32) return false;
    return ((kSeaUnitMask | kDeepSeaUnitMask) >> type) & 1u;
}

// The five water blocks the level format numbers first, mist and rocks
// included (NdLevel's terrain ids 0..4).
bool BattleField::IsWaterTerrain(int terrain) {
    return terrain >= NdLevel::kDeepWater &&
           terrain <= NdLevel::kShallowWaterWithRocks;
}

// Where a passenger may be put down. This is 0x100a4a34's unload loop, which
// is stricter than "anywhere the passenger could stand":
//
//   * only the four orthogonal neighbours (dx {-1,1,0,0}, dy {0,0,-1,1});
//   * the square must be free of units;
//   * the passenger's own movementMask must accept the terrain; and
//   * **if the transport is a sea unit, the square must be Beach** -- unless
//     the transport is itself sitting on Docks or a Shipyard, in which case
//     any square its passenger can stand on will do.
//
// That last clause is the one the port was missing: without it a rowing boat
// could put a swordsman down on any coastal plain, and the campaign's landings
// stop being landings.
bool BattleField::CanUnload(int transport, int slot, int x, int y) const {
    const Unit* t = UnitByIndex(transport);
    if (!t || !t->Alive || slot < 0 || slot >= int(t->Cargo.size())) return false;
    const Unit* c = UnitByIndex(t->Cargo[std::size_t(slot)]);
    if (!c) return false;
    if (Manhattan(t->X, t->Y, x, y) != 1) return false;
    if (!CanEnter(c->Type, x, y)) return false;
    if (At(x, y).Unit >= 0) return false;
    if (IsSeaUnit(t->Type)) {
        const int here = At(t->X, t->Y).Terrain;
        const bool moored = here == kTerrainDocks || here == kTerrainShipyard;
        if (!moored && At(x, y).Terrain != kTerrainBeach) return false;
    }
    return true;
}

bool BattleField::Unload(int transport, int slot, int x, int y) {
    if (!CanUnload(transport, slot, x, y)) return false;
    Unit& t = m_Units[std::size_t(transport)];
    const int index = t.Cargo[std::size_t(slot)];
    t.Cargo.erase(t.Cargo.begin() + slot);
    Unit& u = m_Units[std::size_t(index)];
    u.Carrier = -1;
    u.X = x;
    u.Y = y;
    SetCell(x, y, index);
    u.Hidden = m_Data->Terrain(At(x, y).Terrain).CanHide;
    // Supreme Logistics: "units unloading from transport units do not become
    // inactive", so the passenger keeps its turn. The transport has still
    // spent its own.
    u.Done = !PerkActive(kPerkSupremeLogistics, u.Owner);
    if (!u.Done) u.Movement = AttrsOf(u).MaxMovement +
                              PerkBonus(PerkStat::kMovement, u.Owner, u.Type);
    t.Done = true;
    RecomputeVision();
    ScriptEvent e;
    e.Kind = ScriptEvent::kUnload;
    e.Unit = transport;    // 0x100c9604 takes the owner off the transport
    e.Other = index;
    e.Player = t.Owner;
    e.X = x;
    e.Y = y;
    RaiseEvent(e);
    return true;
}

bool BattleField::CanJoin(int unitIndex, int other) const {
    const Unit* a = UnitByIndex(unitIndex);
    const Unit* b = UnitByIndex(other);
    if (!a || !b || unitIndex == other) return false;
    if (!a->Alive || !b->Alive) return false;
    if (!SameTeam(a->Owner, b->Owner) || a->Type != b->Type) return false;
    return b->HP < kMaxHitPoints;
}

bool BattleField::Join(int unitIndex, int other) {
    if (!CanJoin(unitIndex, other)) return false;
    Unit& a = m_Units[std::size_t(unitIndex)];
    Unit& b = m_Units[std::size_t(other)];
    const UnitAttrs& attrs = AttrsOf(b);
    b.HP = std::min(kMaxHitPoints, b.HP + a.HP);
    b.Rations = std::min(attrs.MaxRations, b.Rations + a.Rations);
    b.Ammo = std::min(attrs.MaxAmmo, b.Ammo + a.Ammo);
    b.Done = true;
    RemoveUnit(unitIndex);
    RecomputeVision();
    ScriptEvent e;
    e.Kind = ScriptEvent::kJoin;
    e.Unit = unitIndex;
    e.Other = other;
    e.Player = b.Owner;
    e.X = b.X;
    e.Y = b.Y;
    RaiseEvent(e);
    return true;
}

int BattleField::CreateUnit(int type, int owner, int x, int y) {
    if (!InBounds(x, y) || At(x, y).Unit >= 0) return -1;
    if (owner < 1 || owner > kMaxPlayers) return -1;
    const int index = AddUnit(type, owner, x, y, m_NextID);
    if (index >= 0) RecomputeVision();
    return index;
}

void BattleField::DamageUnit(int index, int amount) {
    Unit* u = MutableUnit(index);
    if (!u || !u->Alive) return;
    u->HP -= amount;
    if (u->HP <= 0) {
        u->HP = 0;
        RemoveUnit(index);
        RecomputeVision();
    }
}

void BattleField::Wait(int unitIndex) {
    Unit* u = MutableUnit(unitIndex);
    if (!u) return;
    u->Done = true;
    ScriptEvent e;
    e.Kind = ScriptEvent::kWait;
    e.Unit = unitIndex;
    e.Player = u->Owner;
    e.X = u->X;
    e.Y = u->Y;
    RaiseEvent(e);
}

void BattleField::Producible(int propertyIndex, std::vector<int>& types) const {
    types.clear();
    if (propertyIndex < 0 || propertyIndex >= int(m_Properties.size())) return;
    const Property& p = m_Properties[std::size_t(propertyIndex)];
    const uint32_t mask = m_Data->Property(p.Type).CanProduce;
    for (int t = 0; t < kUnitTypeCount; ++t)
        if (mask & (1u << t)) types.push_back(t);
}

// The four clauses of 0x1009b028's property branch, in its order: there is a
// building here, it is mine, it can make something, and nothing is standing on
// it.
bool BattleField::CanBuildAt(int propertyIndex) const {
    if (propertyIndex < 0 || propertyIndex >= int(m_Properties.size()))
        return false;
    const Property& p = m_Properties[std::size_t(propertyIndex)];
    if (p.Owner != m_Current) return false;
    if (m_Data->Property(p.Type).CanProduce == 0) return false;
    return At(p.X, p.Y).Unit < 0;
}

bool BattleField::CanProduce(int propertyIndex, int type) const {
    if (!CanBuildAt(propertyIndex)) return false;
    const Property& p = m_Properties[std::size_t(propertyIndex)];
    if ((m_Data->Property(p.Type).CanProduce & (1u << type)) == 0) return false;
    if (!CanEnter(type, p.X, p.Y)) return false;
    return m_Players[std::size_t(p.Owner)].Cash >= UnitPrice(p.Owner, type);
}

int BattleField::Produce(int propertyIndex, int type) {
    if (!CanProduce(propertyIndex, type)) return -1;
    const Property& p = m_Properties[std::size_t(propertyIndex)];
    m_Players[std::size_t(p.Owner)].Cash -= UnitPrice(p.Owner, type);
    const int index = AddUnit(type, p.Owner, p.X, p.Y, m_NextID);
    if (index >= 0) {
        ++m_Players[std::size_t(p.Owner)].Stats.UnitsBuilt;
        m_Units[std::size_t(index)].Done = true;
        m_Units[std::size_t(index)].Movement = 0;
        ScriptEvent e;
        e.Kind = ScriptEvent::kBuild;
        e.Unit = index;
        e.Property = propertyIndex;
        e.Player = p.Owner;
        e.X = p.X;
        e.Y = p.Y;
        RaiseEvent(e);
    }
    RecomputeVision();
    return index;
}

// --- turns ------------------------------------------------------------------

void BattleField::SetComputer(int player, bool computer) {
    if (player >= 1 && player <= kMaxPlayers)
        m_Players[std::size_t(player)].Computer = computer;
}

// 0x1004f708 seats the list, 0x1004ff0c reads a seat's team out of the mission
// table's masks, and 0x1003c06c copies that team onto the player before it
// builds anyone a controller. The three together are this.
void BattleField::Seat(int humanSeats, const std::vector<uint32_t>& teamMasks) {
    for (int p = 1; p <= kMaxPlayers; ++p) {
        Player& pl = m_Players[std::size_t(p)];
        pl.Computer = p > humanSeats;
        // The first mask claiming this seat names its team; the search starts
        // at one because the engine's array reserves index zero, and a seat no
        // mask claims falls through to an id of its own.
        pl.Team = kLoneTeam + p;
        for (std::size_t i = 0; i < teamMasks.size(); ++i) {
            if ((teamMasks[i] & (uint32_t(1) << p)) == 0) continue;
            pl.Team = int(i) + 1;
            break;
        }
    }
}

int BattleField::Team(int player) const {
    if (player < 1 || player > kMaxPlayers) return kNoOne;
    return m_Players[std::size_t(player)].Team;
}

bool BattleField::SameTeam(int a, int b) const {
    // Seat zero is nobody, and nobody is on nobody's side -- a neutral
    // building has to stay capturable.
    if (a < 1 || a > kMaxPlayers || b < 1 || b > kMaxPlayers) return false;
    return m_Players[std::size_t(a)].Team == m_Players[std::size_t(b)].Team;
}

void BattleField::SetName(int player, const std::string& name) {
    if (player >= 1 && player <= kMaxPlayers && !name.empty())
        m_Players[std::size_t(player)].Name = name;
}

void BattleField::SetColour(int player, int colour) {
    if (player >= 1 && player <= kMaxPlayers)
        m_Players[std::size_t(player)].Colour = colour;
}

int BattleField::Colour(int player) const {
    if (player < 1 || player > kMaxPlayers) return 0;
    const int c = m_Players[std::size_t(player)].Colour;
    return c >= 1 && c <= kMaxPlayers ? c : player;
}

// 0x1003c06c: every seat wants a colour, defaulting to its own number, and the
// first one to ask for a taken colour is given the lowest free one instead.
// Seats nobody is sitting in are skipped, so an empty slot does not eat a
// colour on its way past (the engine's loop asks its player list for seat `n`
// and does nothing when there is no such player).
void BattleField::AssignColours() {
    bool taken[kMaxPlayers + 1] = {false, false, false, false, false};
    for (int p = 1; p <= kMaxPlayers; ++p) {
        Player& pl = m_Players[std::size_t(p)];
        if (!pl.Present) continue;
        int want = pl.Colour >= 1 && pl.Colour <= kMaxPlayers ? pl.Colour : p;
        if (taken[want]) {
            want = p;
            for (int c = 1; c <= kMaxPlayers && taken[want]; ++c)
                if (!taken[c]) want = c;
        }
        taken[want] = true;
        pl.Colour = want;
    }
    ReserveBlackForTheComputer();
}

// 0x1003bd34, run straight after the dedupe above and easy to miss because it
// is buried in the battle's own init rather than beside it.
//
// **Black belongs to the computer.** Any seat that is played from the keyboard
// and came out of the dedupe wearing colour 2 trades colours with a computer
// seat. In a two-player mission this changes nothing -- you are seat one and
// red, the enemy is seat two and black -- which is why the New game screen
// offers red, blue and yellow and never black. But the moment a mission seats
// an *ally*, the ally is seat two and the plain dedupe would dress your own
// side in the enemy's colour and the enemy in blue. SP5 Misplaced Zeal is
// exactly that mission, and the swap is what puts the escaping crew in blue
// and Governor Grondman's Dutch in black.
void BattleField::ReserveBlackForTheComputer() {
    constexpr int kBlack = 2;
    for (int a = 1; a <= kMaxPlayers; ++a) {
        Player& pa = m_Players[std::size_t(a)];
        if (!pa.Present || pa.Computer || pa.Colour != kBlack) continue;
        for (int b = 1; b <= kMaxPlayers; ++b) {
            if (b == a) continue;
            Player& pb = m_Players[std::size_t(b)];
            if (!pb.Present || !pb.Computer) continue;
            std::swap(pa.Colour, pb.Colour);
        }
    }
}

void BattleField::StartBattle() {
    AssignColours();
    m_Current = 1;
    m_Round = 1;
    m_Turn = 1;
    while (m_Current <= kMaxPlayers && !m_Players[std::size_t(m_Current)].Alive)
        ++m_Current;
    if (m_Current > kMaxPlayers) m_Current = 1;
    RecomputeVision();
    BeginTurn(m_Current);
}

// 0x100428b8: walk to the next living player, counting a round each time the
// index wraps through zero.
void BattleField::EndTurn() {
    FinishTurn(m_Current);
    int next = m_Current;
    for (int i = 0; i < kMaxPlayers + 1; ++i) {
        next = (next + 1) % (kMaxPlayers + 1);
        if (next == 0) {
            ++m_Round;
            continue;
        }
        if (m_Players[std::size_t(next)].Alive) break;
    }
    m_Current = next;
    ++m_Turn;
    RecomputeVision();
    BeginTurn(m_Current);
}

// 0x100421c0. The order matters: buildings resupply or pay out, then units
// eat, then everything is woken up.
void BattleField::BeginTurn(int player) {
    if (player < 1 || player > kMaxPlayers) return;
    // A perk of this seat's is a turn older, and this turn's allowance for
    // filling the bar back up is fresh.
    ExpirePerks(player);
    m_Players[std::size_t(player)].PerkAllowance = kPerkGainPerTurn;
    // What the turn itself pays into the bar (0x100425a0).
    AddPerkPoints(player, ArmyValue(player) > BestRivalArmyValue(player)
                              ? kPerkTurnIncomeAhead
                              : kPerkTurnIncome);
    int income = 0;
    for (Property& p : m_Properties) {
        if (p.Owner != player) continue;
        if (!p.BeingCaptured)
            p.CapturePoints = m_Data->Property(p.Type).MaxCapturePoints;
        p.BeingCaptured = false;

        const int i = At(p.X, p.Y).Unit;
        Unit* u = i >= 0 ? &m_Units[std::size_t(i)] : nullptr;
        if (!u || u->Owner != player) {
            income += m_Data->Property(p.Type).CashRate;
            continue;
        }
        const UnitAttrs& a = AttrsOf(*u);
        if (u->HP >= kMaxHitPoints && u->Rations >= a.MaxRations &&
            u->Ammo >= a.MaxAmmo) {
            income += m_Data->Property(p.Type).CashRate;
            continue;
        }
        u->HP = std::min(kMaxHitPoints, u->HP + kBaseRepair);
        u->Rations = a.MaxRations;
        u->Ammo = a.MaxAmmo;
    }

    for (Unit& u : m_Units) {
        if (!u.Alive || u.Owner != player) continue;
        const UnitAttrs& a = AttrsOf(u);
        if (u.Rations == 0) u.HP -= kStarveDamage;
        u.Rations = std::max(0, u.Rations - a.RationRate);
        u.Done = false;
        u.Moved = false;
        // Whatever a perk of this side's is worth to this kind of unit is
        // part of what it starts the turn with (0x10077e54 adds the sum to
        // the table's own movement).
        u.Movement =
            std::max(0, a.MaxMovement +
                            PerkBonus(PerkStat::kMovement, player, u.Type));
    }
    // A unit that starved to death goes now, after the whole sweep, so the
    // loop above is not iterating a vector it is also shrinking.
    for (int i = 0; i < int(m_Units.size()); ++i)
        if (m_Units[std::size_t(i)].Alive && m_Units[std::size_t(i)].HP <= 0)
            RemoveUnit(i);

    m_Players[std::size_t(player)].Cash += income;
    m_Players[std::size_t(player)].Stats.GoldCollected += income;
    RecomputeVision();
}

// 0x100426a8: a building with an enemy unit standing on it that could capture
// is marked, so next turn's begin-turn knows not to reset its progress.
void BattleField::FinishTurn(int player) {
    for (Property& p : m_Properties) {
        const int i = At(p.X, p.Y).Unit;
        if (i < 0) continue;
        const Unit& u = m_Units[std::size_t(i)];
        if (!u.Alive || u.Owner != player || SameTeam(u.Owner, p.Owner))
            continue;
        if ((AttrsOf(u).CaptureCapability & (1u << p.Type)) == 0) continue;
        if (p.CapturePoints < m_Data->Property(p.Type).MaxCapturePoints)
            p.BeingCaptured = true;
    }
    for (Unit& u : m_Units)
        if (u.Owner == player) u.Done = true;
}

// The latch, not a tally. Working it out from what is on the board would get
// the two rules the wrong way round: a side with a shipyard and no men is out,
// and a side that has been eliminated stays out even if a building it once
// owned is neutral again.
bool BattleField::PlayerAlive(int p) const {
    if (p < 1 || p > kMaxPlayers) return false;
    const Player& pl = m_Players[std::size_t(p)];
    return pl.Present && pl.Alive;
}

// The battle is over when one *side* is left, not one seat -- an ally still
// standing beside you is not a battle still running.
int BattleField::Winner() const {
    int team = 0, last = 0;
    for (int p = 1; p <= kMaxPlayers; ++p) {
        if (!m_Players[std::size_t(p)].Present) continue;
        if (!PlayerAlive(p)) continue;
        const int t = m_Players[std::size_t(p)].Team;
        if (last != 0 && t != team) return 0;
        team = t;
        last = p;
    }
    return last;
}

void BattleField::SetCash(int player, int amount) {
    if (player >= 1 && player <= kMaxPlayers)
        m_Players[std::size_t(player)].Cash = amount;
}

int BattleField::Income(int player) const {
    int income = 0;
    for (const Property& p : m_Properties)
        if (p.Owner == player) income += m_Data->Property(p.Type).CashRate;
    // Plundering Blitz: while an enemy is running it, this seat earns nothing.
    // 0x10050660's income sum is short-circuited by the same test.
    if (EnemyPerkActive(kPerkPlunderingBlitz, player)) return 0;
    const int bonus = PerkBonus(PerkStat::kIncome, player, 0);
    if (bonus != 0) income += income * bonus / 100;
    return income;
}

// --- perks ------------------------------------------------------------------

void BattleField::SetPerks(int player, const std::vector<bool>& perks) {
    if (player < 1 || player > kMaxPlayers) return;
    Player& p = m_Players[std::size_t(player)];
    p.Perks.assign(kPerkCount, false);
    for (int i = 0; i < kPerkCount && std::size_t(i) < perks.size(); ++i)
        p.Perks[std::size_t(i)] = perks[std::size_t(i)];
}

bool BattleField::HasPerk(int player, int perk) const {
    if (player < 1 || player > kMaxPlayers) return false;
    if (perk < 0 || perk >= kPerkCount) return false;
    const std::vector<bool>& mine = m_Players[std::size_t(player)].Perks;
    return std::size_t(perk) < mine.size() && mine[std::size_t(perk)];
}

// 0x10077fa4. Sixty per cent of the pool buys the Regular version and only a
// full pool buys the Master one.
bool BattleField::PerkUsable(int player, int perk, bool master) const {
    if (!HasPerk(player, perk)) return false;
    const int need = master ? kMaxPerkPoints : kPerkThreshold;
    return m_Players[std::size_t(player)].PerkPoints >= need;
}

bool BattleField::AnyPerkUsable(int player) const {
    for (int i = 0; i < kPerkCount; ++i)
        if (PerkUsable(player, i, false)) return true;
    return false;
}

void BattleField::SetPerkPoints(int player, int amount) {
    if (player < 1 || player > kMaxPlayers) return;
    Player& p = m_Players[std::size_t(player)];
    const int before = p.PerkPoints;
    p.PerkPoints = std::max(0, std::min(kMaxPerkPoints, amount));
    const int after = p.PerkPoints;

    // 0x1005d940's tail. The bar announces itself when it crosses *upward*
    // through either of its two marks -- the sixty-percent one that makes the
    // ordinary perks affordable and the full one that unlocks the Master
    // versions -- and the flag it carries says which. Crossing downward (a
    // perk being spent) says nothing, which is why both halves of each test
    // are needed rather than a plain `after >= mark`.
    const bool crossedFull =
        (before >= kMaxPerkPoints) != (after >= kMaxPerkPoints) &&
        after >= kMaxPerkPoints;
    const bool crossedLow =
        (before >= kPerkThreshold) != (after >= kPerkThreshold) &&
        after >= kPerkThreshold;
    if (crossedFull || crossedLow) {
        ScriptEvent e;
        e.Kind = ScriptEvent::kPerkReady;
        e.Player = player;
        e.Value = crossedFull ? 1 : 0;
        RaiseEvent(e);
    }
}

// 0x1005d940. A gain is charged against what is left of this turn's allowance,
// and the pool is capped at four hundred either way.
void BattleField::AddPerkPoints(int player, int amount) {
    if (player < 1 || player > kMaxPlayers) return;
    Player& p = m_Players[std::size_t(player)];
    if (amount > 0) {
        const int room = std::max(0, p.PerkAllowance);
        amount = std::min(amount, room);
        p.PerkAllowance = room - amount;
    }
    SetPerkPoints(player, p.PerkPoints + amount);
}

// What a side's army is worth: the list price of everything it still has on
// the board (0x10042610 sums each unit's own price). It is the measure the
// turn's perk income is decided by.
int BattleField::ArmyValue(int player) const {
    int total = 0;
    for (const Unit& u : m_Units)
        if (u.Alive && u.Owner == player) total += AttrsOf(u).Cost;
    return total;
}

// The best any *other* seat can show. Seats that were never in the battle do
// not count, which is what 0x100425a0's null check is for.
int BattleField::BestRivalArmyValue(int player) const {
    int best = 0;
    for (int p = 1; p <= kMaxPlayers; ++p) {
        if (p == player || !m_Players[std::size_t(p)].Present) continue;
        best = std::max(best, ArmyValue(p));
    }
    return best;
}

int BattleField::PerkBonus(PerkStat stat, int player, int unitType) const {
    int total = 0;
    for (const ActivePerk& a : m_ActivePerks) {
        const PerkDef& def = PerkInfo(a.Perk);
        const int lv = a.Master ? 1 : 0;
        const bool mine = SameTeam(a.Seat, player);
        // A perk helps its own side and, for the two that carry one, hurts
        // everybody else (0x1007d7f0 answers +1 to the owner's units and -1 to
        // the enemy's).
        if (mine) {
            if (!PerkReaches(*m_Data, def.Targets[lv], unitType)) continue;
            switch (stat) {
                case PerkStat::kMovement: total += def.Move[lv]; break;
                case PerkStat::kVision: total += def.Vision[lv]; break;
                case PerkStat::kAttack: total += def.Attack[lv]; break;
                case PerkStat::kDefence: total += def.Defence[lv]; break;
                case PerkStat::kPrice: total += def.Price[lv]; break;
                case PerkStat::kIncome: total += def.Income[lv]; break;
            }
        } else if (stat == PerkStat::kMovement && def.EnemyMove[lv] != 0 &&
                   PerkReaches(*m_Data, def.EnemyTargets[lv], unitType)) {
            total += def.EnemyMove[lv];
        }
    }
    return total;
}

// Anything a perk is adding to this unit -- a block of movement or vision, a
// percentage of attack or defence -- counts as a boost. Asking it this way
// means the mark follows the rules rather than a second list that could
// disagree with them: the units that light up are exactly the units getting
// something.
bool BattleField::UnitBoosted(const Unit& u) const {
    if (!u.Alive || m_ActivePerks.empty()) return false;
    static constexpr PerkStat kStats[] = {PerkStat::kMovement, PerkStat::kVision,
                                          PerkStat::kAttack, PerkStat::kDefence};
    for (PerkStat stat : kStats)
        if (PerkBonus(stat, u.Owner, u.Type) != 0) return true;
    return false;
}

bool BattleField::PerkActive(int perk, int player, bool masterOnly) const {
    for (const ActivePerk& a : m_ActivePerks)
        if (a.Perk == perk && SameTeam(a.Seat, player) &&
            (!masterOnly || a.Master))
            return true;
    return false;
}

bool BattleField::EnemyPerkActive(int perk, int player, bool masterOnly) const {
    for (const ActivePerk& a : m_ActivePerks)
        if (a.Perk == perk && !SameTeam(a.Seat, player) &&
            (!masterOnly || a.Master))
            return true;
    return false;
}

bool BattleField::UsePerk(int player, int perk, bool* outMaster,
                          std::vector<int>* outTouched) {
    if (!PerkUsable(player, perk, false)) return false;
    // The level is not the player's to choose: a full bar buys the Master
    // version and anything less buys the Regular one (0x10047b70 asks
    // 0x10077fa4 for Master and takes whatever it says).
    const bool master = PerkUsable(player, perk, true);
    if (outMaster) *outMaster = master;

    std::vector<int> touched;
    ApplyPerk(player, perk, master, touched);
    if (outTouched) *outTouched = touched;

    const PerkDef& def = PerkInfo(perk);
    if (def.Duration != PerkDef::kInstant) {
        ActivePerk a;
        a.Perk = perk;
        a.Seat = player;
        a.Master = master;
        a.Turns = def.Duration;
        m_ActivePerks.push_back(a);
        // A buff bought in the middle of a turn is worth having *this* turn,
        // so its units are handed the movement point the modifier gives them.
        if (def.RefreshMovement) {
            const int lv = master ? 1 : 0;
            for (Unit& u : m_Units) {
                if (!u.Alive || !SameTeam(u.Owner, player)) continue;
                if (!PerkReaches(*m_Data, def.Targets[lv], u.Type)) continue;
                u.Movement += std::max(0, def.Move[lv]);
            }
        }
    }

    // The whole pool, spent, and a fresh allowance to fill it with.
    Player& p = m_Players[std::size_t(player)];
    p.PerkPoints = 0;
    p.PerkAllowance = kPerkGainPerTurn;

    ScriptEvent e;
    e.Kind = ScriptEvent::kPerkUse;
    e.Player = player;
    e.Value = perk;
    e.Before = master ? 1 : 0;
    RaiseEvent(e);
    RecomputeVision();
    return true;
}

// The one-shot half of a perk: what its `apply` does the moment it goes off.
// Each case is the body of one of the twenty-six classes 0x1009fa24 builds,
// with the numbers those bodies carry.
void BattleField::ApplyPerk(int player, int perk, bool master,
                            std::vector<int>& touched) {
    touched.clear();
    const PerkDef& def = PerkInfo(perk);
    const int lv = master ? 1 : 0;

    // The two lists every apply is written against: mine and theirs. An ally's
    // units are mine, the way every other rule in this file has it.
    auto mine = [&](int i) {
        return m_Units[std::size_t(i)].Alive &&
               SameTeam(m_Units[std::size_t(i)].Owner, player);
    };
    auto theirs = [&](int i) {
        const Unit& u = m_Units[std::size_t(i)];
        return u.Alive && u.Owner != kNoOne && !SameTeam(u.Owner, player);
    };
    auto each = [&](bool enemy, auto&& fn) {
        for (int i = 0; i < int(m_Units.size()); ++i) {
            if (enemy ? !theirs(i) : !mine(i)) continue;
            fn(m_Units[std::size_t(i)]);
            touched.push_back(i);
        }
    };
    // Damage that may not finish a unit off: "health cannot go below ten".
    auto wound = [&](Unit& u, int amount, int floor) {
        u.HP = std::max(floor, u.HP - amount);
    };

    switch (perk) {
        case kPerkSupremeSpyGlasses:
            // Nobody stays hidden (0x100ad344 clears the flag on every enemy);
            // the +1 vision and the Master version's lifted fog are the
            // modifier and the flag, read where the fog is recomputed.
            each(true, [](Unit& u) { u.Hidden = false; });
            break;

        case kPerkTechnologyBreak:
            // 0x100b0bf0: Pistoleers come back as Musketeers, and the Master
            // version does the same for the light cavalry.
            each(false, [&](Unit& u) {
                if (u.Type == kUnitPistoleer)
                    u.Type = kUnitMusketeer;
                else if (master && u.Type == kUnitCavalryLight)
                    u.Type = kUnitCavalryHeavy;
            });
            break;

        case kPerkGutsOfGold: {
            // 0x1007ca00: the treasury, divided by ten, capped at twenty --
            // thirty for the Master version -- taken off every enemy, and no
            // unit is killed by it.
            const int cap = master ? 30 : 20;
            const int hit =
                std::min(cap, m_Players[std::size_t(player)].Cash / 10);
            each(true, [&](Unit& u) { wound(u, hit, 10); });
            break;
        }

        case kPerkSuperiorSupply:
        case kPerkHoardUp:
            // 0x100afd4c: halfway to full, or all the way for Master.
            each(false, [&](Unit& u) {
                const UnitAttrs& a = AttrsOf(u);
                u.Rations = master ? a.MaxRations
                                   : std::min(a.MaxRations,
                                              (u.Rations + a.MaxRations) / 2);
                u.Ammo = master ? a.MaxAmmo
                                : std::min(a.MaxAmmo,
                                           (u.Ammo + a.MaxAmmo) / 2);
            });
            break;

        case kPerkVerminInfestation: {
            // 0x100d2acc: three quarters of the enemy's rations, or half.
            const int keep = master ? 50 : 75;
            each(true, [&](Unit& u) { u.Rations = u.Rations * keep / 100; });
            break;
        }

        case kPerkPoison: {
            const int hit = master ? 20 : 10;
            each(true, [&](Unit& u) { u.HP -= hit; });
            break;
        }

        case kPerkDoctorsOrders: {
            // 0x10068cec heals only what is hurt, so a whole army does not
            // light up the screen with animations that did nothing.
            const int heal = master ? 30 : 20;
            for (int i = 0; i < int(m_Units.size()); ++i) {
                if (!mine(i)) continue;
                Unit& u = m_Units[std::size_t(i)];
                if (u.HP >= kMaxHitPoints) continue;
                u.HP = std::min(kMaxHitPoints, u.HP + heal);
                touched.push_back(i);
            }
            break;
        }

        case kPerkEnforcedAction:
        case kPerkSecondWind:
            // 0x10069108 and 0x100a47f4 are the same body: a unit that has
            // already acted is woken up and given its movement back. Enforced
            // Action reaches every unit at Master level and only the ships
            // below it; Second Wind is the ships either way.
            for (int i = 0; i < int(m_Units.size()); ++i) {
                if (!mine(i)) continue;
                Unit& u = m_Units[std::size_t(i)];
                const bool all = perk == kPerkEnforcedAction && master;
                if (!all && !IsSeaUnit(u.Type)) continue;
                if (!u.Done && !u.Moved) continue;
                u.Done = false;
                u.Moved = false;
                u.Movement = AttrsOf(u).MaxMovement;
                touched.push_back(i);
            }
            break;

        case kPerkRumDelivery:
            // 0x100a40a4: the rum costs ten points of health all round, and
            // cannot kill.
            each(false, [&](Unit& u) { wound(u, 10, 10); });
            break;

        case kPerkBlackSpot: {
            // 0x10055200: every enemy standing next to one of my foot
            // soldiers. Fifty points, floored at ten -- or destroyed outright
            // at Master level.
            for (int i = 0; i < int(m_Units.size()); ++i) {
                if (!theirs(i)) continue;
                Unit& u = m_Units[std::size_t(i)];
                bool nextToInfantry = false;
                for (int d = 0; d < 4 && !nextToInfantry; ++d) {
                    const int nx = u.X + kDx[d], ny = u.Y + kDy[d];
                    if (!InBounds(nx, ny)) continue;
                    const int n = At(nx, ny).Unit;
                    if (n < 0) continue;
                    const Unit& mate = m_Units[std::size_t(n)];
                    if (!mate.Alive || !SameTeam(mate.Owner, player)) continue;
                    nextToInfantry =
                        PerkReaches(*m_Data, PerkTargets::kInfantry, mate.Type);
                }
                if (!nextToInfantry) continue;
                if (master)
                    u.HP = 0;
                else
                    wound(u, 50, 10);
                touched.push_back(i);
            }
            break;
        }

        case kPerkGambling: {
            // 0x10072e28: a quarter of every enemy commander's treasury, or
            // half, moved across.
            const int share = master ? 50 : 25;
            for (int p = 1; p <= kMaxPlayers; ++p) {
                if (p == player || SameTeam(p, player)) continue;
                if (!m_Players[std::size_t(p)].Present) continue;
                const int take = m_Players[std::size_t(p)].Cash * share / 100;
                m_Players[std::size_t(p)].Cash -= take;
                m_Players[std::size_t(player)].Cash += take;
            }
            break;
        }

        case kPerkForTheCause: {
            // 0x10072ba4: every village of mine that is standing empty turns
            // out a soldier, free. Master sends Pistoleers instead.
            const int type = master ? kUnitPistoleer : kUnitSwordsman;
            for (const Property& prop : m_Properties) {
                if (prop.Owner != player || prop.Type != kPropVillage) continue;
                if (At(prop.X, prop.Y).Unit >= 0) continue;
                const int index = AddUnit(type, player, prop.X, prop.Y,
                                          m_NextID++);
                if (index < 0) continue;
                // It arrives ready to act, which is the point of it.
                m_Units[std::size_t(index)].Done = false;
                touched.push_back(index);
                ScriptEvent e;
                e.Kind = ScriptEvent::kBuild;
                e.Unit = index;
                e.Player = player;
                e.X = prop.X;
                e.Y = prop.Y;
                RaiseEvent(e);
            }
            break;
        }

        default:
            // The rest are nothing but their modifiers, which the rules read
            // off the active list. Their animation still wants somewhere to
            // play, so the units the perk reaches are what it touched.
            if (def.Duration != PerkDef::kInstant) {
                const bool enemy = def.OverEnemy;
                const PerkTargets group =
                    enemy ? PerkTargets::kAll : def.Targets[lv];
                for (int i = 0; i < int(m_Units.size()); ++i) {
                    if (enemy ? !theirs(i) : !mine(i)) continue;
                    if (!enemy && group != PerkTargets::kAll &&
                        !PerkReaches(*m_Data, group, m_Units[std::size_t(i)].Type))
                        continue;
                    touched.push_back(i);
                }
            }
            break;
    }

    // Anything the perk killed goes now, after the sweep, so the loops above
    // are not iterating a vector they are also shrinking. RemoveUnit is what
    // pays the dead unit's price into its owner's bar.
    for (int i = int(m_Units.size()); i-- > 0;)
        if (m_Units[std::size_t(i)].Alive && m_Units[std::size_t(i)].HP <= 0)
            RemoveUnit(i);
}

// What a unit costs this seat today. 0x100779bc scales the table's price by
// the sum of the perk and skill modifiers, which for Golden Age is -50 or -75
// per cent.
int BattleField::UnitPrice(int player, int unitType) const {
    const int base = m_Data->Unit(unitType).Cost;
    const int pct = PerkBonus(PerkStat::kPrice, player, unitType);
    if (pct == 0) return base;
    return std::max(0, base + base * pct / 100);
}

// 0x100a0150, run for the seat whose turn is starting: every perk of theirs
// loses a turn, and the ones that have run out are dropped.
void BattleField::ExpirePerks(int player) {
    for (std::size_t i = m_ActivePerks.size(); i-- > 0;) {
        ActivePerk& a = m_ActivePerks[i];
        if (a.Seat != player) continue;
        if (a.Turns <= 0)
            m_ActivePerks.erase(m_ActivePerks.begin() + long(i));
        else
            --a.Turns;
    }
}

// --- saving -----------------------------------------------------------------

BattleField::Snapshot BattleField::Save() const {
    Snapshot s;
    s.Units = m_Units;
    s.Properties = m_Properties;
    s.Players = m_Players;
    s.TerrainHP.reserve(m_Cells.size());
    for (const Cell& c : m_Cells) s.TerrainHP.push_back(c.TerrainHP);
    s.Perks = m_ActivePerks;
    s.Current = m_Current;
    s.Round = m_Round;
    s.Turn = m_Turn;
    s.NextID = m_NextID;
    s.Fog = m_Fog;
    s.ElapsedMs = m_ElapsedMs;
    return s;
}

bool BattleField::Restore(const Snapshot& s) {
    // The board has to be the one this snapshot was taken on. A level of a
    // different size is the cheap tell, and the only one worth having here:
    // the save file carries the level's own path and the loader has already
    // rebuilt from it, so anything that gets this far is the right map.
    if (!Valid() || s.TerrainHP.size() != m_Cells.size()) return false;
    if (s.Players.size() != std::size_t(kMaxPlayers) + 1) return false;
    // Whose turn it is indexes the player table every frame of the battle, so
    // it has to be a seat that exists.
    if (s.Current < 1 || s.Current > kMaxPlayers) return false;
    // A unit index is a cross-reference -- carriers, cargo, the cell grid --
    // so every one of them has to be in range before any of it is applied.
    const int count = int(s.Units.size());
    for (const Unit& u : s.Units) {
        if (u.Carrier < -1 || u.Carrier >= count) return false;
        if (u.Owner < 0 || u.Owner > kMaxPlayers) return false;
        if (!InBounds(u.X, u.Y)) return false;
        for (const int c : u.Cargo)
            if (c < 0 || c >= count) return false;
    }

    m_Units = s.Units;
    m_Properties = s.Properties;
    m_Players = s.Players;
    m_ActivePerks = s.Perks;
    m_Current = s.Current;
    m_Round = s.Round;
    m_Turn = s.Turn;
    m_NextID = s.NextID;
    m_Fog = s.Fog;
    m_ElapsedMs = s.ElapsedMs < 0 ? 0 : s.ElapsedMs;
    m_Deaths.clear();

    for (std::size_t i = 0; i < m_Cells.size(); ++i) {
        m_Cells[i].TerrainHP = s.TerrainHP[i];
        m_Cells[i].Unit = -1;
    }
    // The level file has just put every wall and castle back up. Knock down
    // again whatever the battle had already flattened: a breakable square at
    // zero hit points, and a castle whose property reads zero. Nothing else
    // reaches zero -- the other terrains are not breakable and their hundred
    // is never touched.
    for (int y = 0; y < m_Height; ++y)
        for (int x = 0; x < m_Width; ++x)
            if (IsBreakable(x, y) && At(x, y).TerrainHP <= 0) FlattenCell(x, y);
    for (int i = 0; i < int(m_Properties.size()); ++i)
        if (IsCastle(m_Properties[std::size_t(i)].Type) &&
            m_Properties[std::size_t(i)].HP <= 0)
            RazeProperty(i);
    // Which unit holds which square is derived, not stored: a passenger is
    // inside its carrier and does not hold one, exactly as Build() has it for
    // the boat-with-soldier types.
    for (int i = 0; i < count; ++i) {
        const Unit& u = m_Units[std::size_t(i)];
        if (!u.Alive || u.Carrier >= 0) continue;
        SetCell(u.X, u.Y, i);
    }
    RecomputeVision();
    return true;
}

// --- fog of war -------------------------------------------------------------

void BattleField::SetFog(bool on) {
    m_Fog = on;
    RecomputeVision();
}

bool BattleField::Visible(int player, int x, int y) const {
    if (!m_Fog) return true;
    if (!InBounds(x, y) || player < 0 || player > kMaxPlayers) return true;
    return m_Visible[std::size_t(player) * m_Cells.size() +
                    std::size_t(y) * m_Width + x] != 0;
}

void BattleField::RecomputeVision() {
    m_Visible.assign(std::size_t(kMaxPlayers + 1) * m_Cells.size(), m_Fog ? 0 : 1);
    if (!m_Fog) return;
    // Scoped to the side rather than the seat: an ally's scouts report to you.
    // This one is the port's reading -- every other rule here is a team check
    // in the binary, but the engine's fog pass was not found, and nothing in
    // the port turns fog on yet.
    for (int p = 1; p <= kMaxPlayers; ++p) {
        uint8_t* v = &m_Visible[std::size_t(p) * m_Cells.size()];
        // Supreme Spy-Glasses at Master level lifts the fog outright while it
        // lasts -- 0x100d3024 skips its whole pass when 0x100a0500 says the
        // perk is running.
        if (PerkActive(kPerkSupremeSpyGlasses, p, /*masterOnly=*/true)) {
            for (std::size_t i = 0; i < m_Cells.size(); ++i) v[i] = 1;
            continue;
        }
        for (const Property& pr : m_Properties)
            if (SameTeam(pr.Owner, p)) v[std::size_t(pr.Y) * m_Width + pr.X] = 1;
        for (const Unit& u : m_Units) {
            if (!u.Alive || !SameTeam(u.Owner, p) || u.Carrier >= 0) continue;
            int range = AttrsOf(u).Vision +
                        m_Data->Terrain(At(u.X, u.Y).Terrain).VisionBonus +
                        PerkBonus(PerkStat::kVision, u.Owner, u.Type);
            if (range < 1) range = 1;
            for (int y = 0; y < m_Height; ++y) {
                for (int x = 0; x < m_Width; ++x) {
                    if (Manhattan(u.X, u.Y, x, y) > range) continue;
                    v[std::size_t(y) * m_Width + x] = 1;
                }
            }
        }
    }
}

}  // namespace bb
