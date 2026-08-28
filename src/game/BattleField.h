// BattleField — the battle simulation: the grid, the units on it, the
// buildings, the players, and the rules that move them around.
//
// This is the engine's `BattleField` (0x10040000-0x1004c000), built from an
// `NdLevel` the way 0x1008438c does it: the terrain grid is handed over whole
// (0x100404cc), then the unit and property layers are walked and turned into
// objects (0x1004057c, 0x10040808).
//
// Players are numbered 1..4 and 0 means "nobody", exactly as the level file
// and the turn rotation (0x100428b8) do -- the rotation counts to five and
// treats the wrap through zero as the end of a round.
//
// Hit points run 0..100 and are shown as ten bars. The engine converts with a
// 100-entry table at 0x1011a6c8 (0x10077994); the table is `max(1, (hp+5)/10)`
// capped at 10, and that same number is how many capture points a unit
// removes in a turn, so it is worth having in one place.
//
// Rules recovered from the binary, rather than assumed from the genre:
//
//   * A property only pays out if the friendly unit standing on it is at full
//     health, rations *and* ammunition. Otherwise the turn is spent
//     resupplying that unit and the building earns nothing (0x100421c0).
//   * Capturing needs ammunition but does not spend it (0x10077834). A
//     building is worth 20 points and a healthy unit removes 10, so every
//     capture takes two turns and a Swordsman -- which carries one round and
//     has an ammunition consume rate of zero -- could never finish one if the
//     action charged for itself.
//   * A unit that begins its turn with no rations loses 20 hit points.
//   * Damage is `attackPower / 100 * (200 - defensePower)`, at least 1
//     (0x10076db8). Both powers are `value * hp + terrainOrBuildingBonus`,
//     rounded up, where the value comes from the 21x21 matrices in
//     `ndUnitAttributes.ini`.
//   * The counter-attack happens only if the defender survived, is in its own
//     minimum..maximum range, can attack that unit type, still has
//     ammunition, and has `isAbleToCounterAttack`.
//   * Seven missions seat an ally, and an ally is not a softer enemy: you
//     cannot shoot one or take their buildings, you can walk through their
//     army and resupply it, and their victory is yours. The engine spells
//     each of those out against the owner's *team* rather than their seat
//     (0x100a4a34's capture gate, 0x100479e8's supply, the `~teamMask` it
//     hands the path finder, 0x1004c84c's winner), and so does this.
//
// Which of those seats you personally play is a separate question, answered
// by the mission table rather than by the level -- see Seat().
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/BattleData.h"
#include "game/NdLevel.h"
#include "game/Perks.h"

namespace bb {

class FilePack;

class BattleField {
public:
    static constexpr int kMaxPlayers = 4;   // slots 1..4
    static constexpr int kMaxHitPoints = 100;
    static constexpr int kStarveDamage = 20;
    static constexpr int kBaseRepair = 10;
    static constexpr int kNoOne = 0;
    // A seat the mission table puts on no team still gets an id, so that
    // "same team" never has to mean "both zero". 0x1004ff0c returns
    // `0x100 + seat` when its search falls off the end; the port keeps the
    // number so a save written by either reading means the same thing.
    static constexpr int kLoneTeam = 0x100;
    // 0x10059278 writes this into every property it builds.
    static constexpr int kPropertyHitPoints = 200;
    // The three baked unit-category masks the engine keeps (DAT_10076698,
    // DAT_10076684, and the literal 0x3c000 returned by 0x10076688). The
    // trigger language exposes them as "Land unit" / "Sea unit" / "Deep sea
    // unit" and the unload rule uses the sea pair.
    static constexpr uint32_t kLandUnitMask = 0x000203ffu;
    static constexpr uint32_t kSeaUnitMask = 0x001e3c00u;
    static constexpr uint32_t kDeepSeaUnitMask = 0x0003c000u;
    // Terrain ids the unload rule names. Docks and Shipyard are the two the
    // level loader stamps over the terrain under those buildings (0x10040a9c),
    // which is why they never appear in a level file.
    static constexpr int kTerrainBeach = 5;
    static constexpr int kTerrainDocks = 16;
    static constexpr int kTerrainShipyard = 17;
    static bool IsSeaUnit(int type);

    struct Cell {
        uint8_t Terrain = 0;
        uint8_t Variant = 0;
        int TerrainHP = 0;     // breakable terrain; see the note in the cpp
        int Property = -1;      // index into Properties(), or -1
        int Unit = -1;          // index into Units(), or -1
        // Which piece of a multi-tile building this cell is, row-major inside
        // the footprint. Zero for everything one tile across.
        uint8_t Piece = 0;
    };

    struct Unit {
        int ID = 0;             // the level editor's id; triggers use it
        int Type = 0;           // 0-based UnitType
        int Owner = 0;          // 1..4
        int X = 0, Y = 0;
        int HP = kMaxHitPoints;
        int Movement = 0;
        int Rations = 0;
        int Ammo = 0;
        int Facing = 0;         // Cannon Tower only
        bool Done = false;      // has acted this turn
        // Whether this unit has been walked somewhere this turn (unit+0x50,
        // set by the path move 0x100417ec and cleared by the begin-turn
        // upkeep 0x10042140). It is what stops an indirect-combat unit
        // shooting on the turn it repositions: 0x10092aa8 refuses the attack
        // outright when `class == INDIRECT_COMBAT && moved`, and the action
        // popup hides the row for the same reason (0x100a4a34's
        // `class != 2 || !moved`). Mortars, Cannons, Scorch Cannons, the
        // Galley -- the mortar boat -- and the H.I.D.S.U. are all that class.
        bool Moved = false;
        // Halfway through taking a building (unit+0x54, set by the capture
        // action 0x10046b14 and cleared when the points run out, 0x10041e64).
        // It is what puts the little flag badge over the unit.
        bool Capturing = false;
        bool Hidden = false;    // standing in cover
        bool Alive = true;
        int Carrier = -1;       // transport carrying this unit, or -1
        std::vector<int> Cargo; // units aboard
    };

    struct Property {
        int ID = 0;
        int Type = 0;           // 0-based PropertyType
        int Owner = 0;          // 0 = neutral
        int X = 0, Y = 0;
        int CapturePoints = 0;
        bool BeingCaptured = false;
        // What `Special::getHealthOfSpecial` reads. The constructor
        // (0x10059278) writes a flat 200 into property+0x38 -- it is not in
        // `ndPropertyAttributes.ini` and does not vary by type -- and three
        // campaign missions end when a named castle's reaches zero.
        int HP = kPropertyHitPoints;
    };

    struct Player {
        std::string Name;
        bool Computer = false;
        bool Alive = false;
        bool Present = false;
        int Cash = 0;
        // Which side this seat fights on. Never zero once the field is built:
        // a seat on no declared team gets kLoneTeam + its own number. See
        // Seat() for where the mission table's TEAMS list comes in.
        int Team = 0;
        // Which of the four palettes this player wears, 1..4. It defaults to
        // the seat but need not match it: the campaign lets you pick, and
        // 0x1003c06c reconciles the choices by handing anyone whose colour is
        // already taken the first free one. Everything with a colour on it --
        // unit and building sprites, the flags on the turn card -- goes
        // through this, not through the seat number.
        int Colour = 0;
        // The commander-power meter the status panel draws, out of
        // kMaxPerkPoints. A battle opens with it *empty*: 0x1005d840, the
        // reset every Player runs through when it is built, writes zero into
        // it. (The four hundred at 0x1005dbac belongs to the "Wilhelm" default
        // commander, which is only used to fill an unnamed seat.) It fills from
        // there -- see AddPerkPoints and the turn income in BeginTurn.
        int PerkPoints = 0;
        // How much of it may still be gained this turn. The engine resets the
        // allowance whenever a perk is spent (0x10047b70 writes 0x100f2140's
        // answer into the commander) and counts gains against it, so no single
        // rout can refill the bar outright.
        int PerkAllowance = kPerkGainPerTurn;
        // Which perks this commander brought, by perk id. A campaign
        // commander's are the campaign's; a skirmish seat has none unless a
        // test gives it some.
        std::vector<bool> Perks;
        // What the Battle info board reports afterwards. The engine keeps
        // these on a separate statistics object indexed by seat (0x10051438);
        // here they live with the player, which comes to the same thing.
        struct StatBlock {
            int UnitsBuilt = 0;
            int UnitsDestroyed = 0;   // of somebody else's
            int UnitsLost = 0;
            int PropertiesCaptured = 0;
            int PropertiesLost = 0;
            int GoldCollected = 0;    // income, not the treasury
            // Headquarters lost, counted apart from the rest. The engine keeps
            // properties-lost as an array indexed by building type
            // (0x10051314's case 6) and the capture handler reads exactly the
            // headquarters slot: losing your last one only ends you if one of
            // them was *taken*, so a side that never had a headquarters is not
            // finished off when its village changes hands.
            int HeadquartersLost = 0;
        } Stats;
    };

    // The meter's full scale and the mark two thirds along it, both fixed in
    // the binary: 0x1005db70 returns 400 and 0x1005db78 takes 60% of it.
    static constexpr int kMaxPerkPoints = 400;
    static constexpr int kPerkThreshold = kMaxPerkPoints * 60 / 100;
    // And what may be gained between two spends: a third of the pool plus one
    // (0x100f2140), which is 134.
    static constexpr int kPerkGainPerTurn = kMaxPerkPoints / 3 + 1;
    // What a turn is worth on its own (0x100425a0, run from the begin-turn
    // upkeep): fifty, or seventy for the commander whose army is worth more
    // than every other on the board. That is the *whole* army's list price,
    // summed, and the comparison is `>=` -- a tie counts as being matched, so
    // the seventy goes only to a side that is strictly ahead.
    static constexpr int kPerkTurnIncome = 50;
    static constexpr int kPerkTurnIncomeAhead = 70;

    // A perk that has gone off and is still running. The engine keeps these in
    // a list on the battle engine (0x100a0124 adds, 0x100a0150 counts them
    // down at their owner's turn) and every rule that can be modified sums the
    // list (0x100a0330).
    struct ActivePerk {
        int Perk = -1;
        int Seat = 0;
        bool Master = false;
        // Turns still to run. Counted down at the owner's turn; the perk goes
        // when it would drop below zero.
        int Turns = 0;
    };

    // What one attack did. Mirrors the struct 0x10076db8 fills in, so the UI
    // can show before/after without recomputing anything.
    struct CombatResult {
        bool Valid = false;
        int Attacker = -1, Defender = -1;
        int AttackerHPBefore = 0, DefenderHPBefore = 0;
        int AttackerHP = 0, DefenderHP = 0;
        int AttackerAmmo = 0, DefenderAmmo = 0;
        int Damage = 0, CounterDamage = 0;
        bool Countered = false;
        bool AttackerDied = false, DefenderDied = false;
    };

    struct Step {
        int X = 0, Y = 0;
    };

    // What shooting at an obstacle did. There is no counter-attack and no
    // ammunition spent, so it is a much smaller answer than a fight.
    struct ObstacleResult {
        bool Valid = false;
        int X = 0, Y = 0;
        int HPBefore = 0, HPAfter = 0;
        int Damage = 0;
        bool Destroyed = false;
    };

    // Something that happened, for the mission scripts to hear about.
    //
    // The engine raises these from inside the rules -- every action handler
    // ends by pushing an event object onto the battle's own queue with
    // 0x100400c8 -- which is why a computer player's capture fires exactly the
    // triggers a human player's does. The port keeps that shape: the field
    // appends, and the screen drains and translates. Doing it in the screen's
    // action handlers instead would leave the AI's turn silent.
    struct ScriptEvent {
        enum ActionKind {
            kMove,              // unit walked; from_* is where it set out
            kCaptureStart,      // property, unit, player = the old owner
            kCaptureCompleted,  // ditto, once the points ran out
            // Every capture action, finished or not: what the flag-raising
            // board needs. `value` is the building kind, `before` the
            // capturing player's colour, and FromX/FromY the capture points
            // before and after -- which is how far the flag has to climb.
            kCaptureProgress,
            kLoad,              // unit boarded other
            kUnload,            // unit put other down at x,y
            kJoin,
            kSupply,
            kWait,
            kBuild,             // property produced unit
            kSelect,            // a unit or property was picked up
            kHealthChange,      // a cell's or property's hit points moved
            kAmbush,            // a move stopped early on a hidden enemy
            // A commander set a perk off. `value` is the perk, `before` is 1
            // for the Master version, and `player` whose it was.
            kPerkUse,
            // Two units traded blows. `unit` is the attacker and `other` the
            // defender, `x`/`y` is the defender's square and `FromX`/`FromY`
            // the attacker's, and the four hit point fields below say what it
            // cost. This is what the cutaway fight scene is built from, which
            // is why it is an event and not a return value: the computer's
            // attacks have to raise it too, and the AI drives the field
            // directly.
            kFight,
            // The commander-power bar has just climbed past one of its two
            // marks (0x1005d940 posts message 0x34 when it does). `player` is
            // whose bar it is and `value` is 1 for the full mark -- the one
            // that unlocks the Master perks -- and 0 for the sixty-percent
            // one. Only the seat at the keyboard is told about it: 0x100875f8
            // gates the case on the local-player test, which is why the misc
            // bank's `enemy_perk` samples have no caller anywhere.
            kPerkReady,
        };
        ActionKind Kind = kMove;
        int Unit = -1;
        int Other = -1;
        int Property = -1;
        int Player = 0;
        int X = 0, Y = 0;
        int FromX = 0, FromY = 0;
        int Value = 0, Before = 0;
        // kFight only. Read before the losses are applied, so the scene can
        // open on the position as it was and play the damage out.
        int AttackerHPBefore = 0, AttackerHPAfter = 0;
        int DefenderHPBefore = 0, DefenderHPAfter = 0;
        bool Countered = false;
    };
    void TakeEvents(std::vector<ScriptEvent>& out) {
        out = std::move(m_Events);
        m_Events.clear();
    }
    void RaiseEvent(const ScriptEvent& e) { m_Events.push_back(e); }

    // A battle in progress, reduced to the part a save has to carry.
    //
    // The engine has the same idea and the same name (`BattleField::save`,
    // 0x1004340c, whose log lines enumerate map data, buildings, units,
    // objects, the perk pool, the random seed, regions and statistics). What
    // is *not* here is the point of it: the terrain grid, the building
    // footprints, which cell each piece of a castle is -- all of that comes
    // back from the level file, so a save names the level and stores only what
    // the battle has since changed. That is what keeps a mid-battle save a few
    // kilobytes instead of a few hundred, which is what makes it fit on a
    // memory card.
    //
    // `Restore` therefore only works on a field already built from the same
    // level; it repairs the derived state (which unit is standing on which
    // cell, and what each player can see) rather than trusting the blob for
    // it, so a save cannot describe an impossible board.
    struct Snapshot {
        std::vector<Unit> Units;
        std::vector<Property> Properties;
        std::vector<Player> Players;    // slot 0 unused, exactly as the field
        // One per cell. This is also how a *flattened* obstacle survives a
        // save with no field of its own: the level file puts the wall back on
        // the way in, and a breakable square whose hit points are zero is one
        // that has already come down, so Restore knocks it over again. The
        // same reading finishes a castle, whose hit points ride along on the
        // property.
        std::vector<int> TerrainHP;
        // The perks still running, which are as much a part of the position as
        // the units: a battle reloaded in the middle of a Fit of Rage should
        // still be in one.
        std::vector<ActivePerk> Perks;
        int Current = 1, Round = 0, Turn = 0, NextID = 1;
        bool Fog = false;
        // How long the battle has been played, in milliseconds. The engine
        // keeps the same number on its statistics object (0x100512d0's slot 2,
        // which the Battle info board reads as "Time elapsed") and a battle
        // picked up from a save carries on counting from where it left off.
        int ElapsedMs = 0;
    };

    bool Load(FilePack& pack, const BattleData& data, const std::string& path);
    bool Build(const NdLevel& level, const BattleData& data);

    bool Valid() const { return m_Width > 0; }
    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    bool InBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < m_Width && y < m_Height;
    }
    const Cell& At(int x, int y) const {
        return m_Cells[std::size_t(y) * m_Width + x];
    }
    const std::vector<Cell>& Cells() const { return m_Cells; }
    const std::vector<Unit>& Units() const { return m_Units; }
    const std::vector<Property>& Properties() const { return m_Properties; }
    const std::vector<Player>& Players() const { return m_Players; }
    const NdLevel& Level() const { return m_Level; }
    const BattleData& Data() const { return *m_Data; }

    const Unit* UnitAt(int x, int y) const;
    const Property* PropertyAt(int x, int y) const;
    Unit* MutableUnit(int i) {
        return i >= 0 && i < int(m_Units.size()) ? &m_Units[std::size_t(i)] : nullptr;
    }
    const Unit* UnitByIndex(int i) const {
        return i >= 0 && i < int(m_Units.size()) ? &m_Units[std::size_t(i)] : nullptr;
    }
    const UnitAttrs& AttrsOf(const Unit& u) const { return m_Data->Unit(u.Type); }

    // Turn state.
    int CurrentPlayer() const { return m_Current; }
    int Round() const { return m_Round; }
    int Turn() const { return m_Turn; }
    void StartBattle();          // seats player 1 and runs their begin-turn
    // Who is at the keyboard, one seat at a time. Seat() below sets all four
    // from a mission table entry, which is where every battle the port can
    // start gets it from; this is the override for anything that wants to
    // change its mind afterwards.
    void SetComputer(int player, bool computer);

    // Sides. `Seat` applies a mission table entry: the first `humanSeats`
    // seats are played from the keyboard and the rest by the computer, and
    // `teamMasks` is that entry's TEAMS list -- one seat bitmask per team,
    // bit `n` meaning seat `n`, team id being the mask's position plus one
    // (0x1004ff0c). A seat no mask claims keeps its lone id, so a
    // free-for-all needs no special case anywhere: SameTeam is a comparison.
    void Seat(int humanSeats, const std::vector<uint32_t>& teamMasks);
    int Team(int player) const;
    bool SameTeam(int a, int b) const;
    // Which commander is behind a seat, as a character id -- the `<type>` of
    // the `Data\commanders\*.xml` the level's player name names. -1 when the
    // seat has none, which is every seat of a multiplayer map (their player
    // chunk is empty) and every seat when the field was built without a pak
    // to read the files from. See Commanders.h.
    int Character(int player) const;
    void SetCharacter(int player, int type);
    // Which of the five recorded armies this seat's men shout in -- the
    // `<nationality>` of the same file, as a SoundManager::Nation. -1 where
    // there is no commander, and then the seat simply has no voice.
    int Nationality(int player) const;

    // How long this battle has been played, in milliseconds, and the same in
    // whole seconds -- which is what the Battle info board shows. The field
    // does not run a clock of its own: the screen stamps it every frame, so
    // that the number survives into a save and comes back with it.
    int Elapsed() const { return m_ElapsedMs; }
    int ElapsedSeconds() const { return m_ElapsedMs / 1000; }
    void SetElapsed(int ms) { m_ElapsedMs = ms < 0 ? 0 : ms; }

    // What the campaign sets before the battle opens: the commander's own name
    // and the colour chosen on the New game screen. A colour of zero means
    // "whatever the seat number is".
    void SetName(int player, const std::string& name);
    void SetColour(int player, int colour);
    // The palette a player wears. Always 1..4, and never shared.
    int Colour(int player) const;
    void EndTurn();              // rotates to the next living player
    // Whether this seat is still in the battle. It is a *latch*, not a tally:
    // the engine's `player+0x38` starts set and is only ever cleared, by
    // 0x10041cbc. See Eliminate for the two things that clear it.
    bool PlayerAlive(int p) const;
    // 0x10041cbc. Put a seat out of the battle: no more turns, Player::OnDefeat
    // for the scripts, and -- unless the battle is already decided -- its units
    // killed and its buildings handed back to nobody, a headquarters becoming
    // a village on the way. Calling it twice does nothing the second time,
    // which is what stops the wipe recursing through RemoveUnit.
    void Eliminate(int player);
    // What the two elimination rules count: 0x10044e3c walks the board for a
    // seat's units (cargo included) and 0x10044eb0 for its buildings of one
    // kind.
    int CountUnits(int player) const;
    int CountProperties(int player, int type) const;
    // A seat on the last side still standing, or 0 while more than one side
    // is. It is a *side*, not a seat, that wins: 0x1004c84c settles a battle
    // by turning the surviving player's team into a seat mask, and 0x1008b028
    // calls it a win if any of the local seats is in it. So callers must ask
    // SameTeam(Winner(), theirSeat) rather than compare seat numbers.
    int Winner() const;
    // What this player's buildings pay at the start of their turn. The engine
    // keeps this as a field on the player, recomputed whenever a building
    // changes hands (0x10042040 sums it and stores it through 0x1005daa4);
    // the port derives it on demand, which comes to the same number. The
    // status panel shows it beside the treasury.
    int Income(int player) const;
    // Set a player's treasury outright, which a mission script can do
    // (Player::SetAmountOfGold).
    void SetCash(int player, int amount);

    // --- perks ---------------------------------------------------------------
    //
    // See Perks.h for what each one does. The rules read the modifiers through
    // PerkBonus and ask about the flag-like ones with PerkActive; everything
    // else is UsePerk's business.

    // Give this seat its commander's perks (the campaign's, for the seat the
    // player holds). Anything outside 0..25 is ignored.
    void SetPerks(int player, const std::vector<bool>& perks);
    bool HasPerk(int player, int perk) const;
    // May this seat use `perk` at this level right now? 0x10077fa4: the perk
    // is one of theirs, and the bar is at least sixty per cent full for the
    // Regular version or completely full for the Master one.
    bool PerkUsable(int player, int perk, bool master) const;
    // Whether *any* of the seat's perks could go off, which is what decides
    // whether the battle menu's Perks row is live.
    bool AnyPerkUsable(int player) const;

    // Set the perk off. The level is not a choice: a full bar buys Master and
    // anything less buys Regular (0x10047b70). Returns false if it could not
    // be used at all. `outMaster` reports which version went off, and
    // `outTouched` the units the animation should play over.
    bool UsePerk(int player, int perk, bool* outMaster = nullptr,
                 std::vector<int>* outTouched = nullptr);

    // What this seat's army is worth on the board, and the best any other seat
    // can show. The turn's perk income is decided by the two.
    int ArmyValue(int player) const;
    int BestRivalArmyValue(int player) const;

    // Points, and the two events crossing a threshold raises.
    void AddPerkPoints(int player, int amount);
    void SetPerkPoints(int player, int amount);

    const std::vector<ActivePerk>& ActivePerks() const { return m_ActivePerks; }
    // The sum of every active perk's modifier for this seat's unit type, the
    // way 0x100a0330 sums them. Percentages for attack, defence, price and
    // income; whole blocks for movement and vision.
    int PerkBonus(PerkStat stat, int player, int unitType) const;
    // Is `perk` running for `player`? `masterOnly` asks for the Master
    // version, which is how the rules that only the Master version changes
    // are written (0x100a0500's level mask).
    bool PerkActive(int perk, int player, bool masterOnly = false) const;
    // Is any *enemy* of `player` running `perk`?
    bool EnemyPerkActive(int perk, int player, bool masterOnly = false) const;
    // Is a perk giving this unit something right now? The renderer asks so it
    // can mark the unit: a buff that lasts three turns with nothing on screen
    // to show for it is a buff the player has to remember rather than see.
    bool UnitBoosted(const Unit& u) const;
    // What a unit costs this seat, which Golden Age makes cheaper. The build
    // screen shows this rather than the table's own price.
    int UnitPrice(int player, int unitType) const;

    // Movement. `Reachable` fills a width*height array with the cost to enter
    // each cell, or -1 where the unit cannot stand.
    void Reachable(int unitIndex, std::vector<int>& out) const;
    bool PathTo(int unitIndex, int x, int y, std::vector<Step>& out) const;
    bool MoveUnit(int unitIndex, int x, int y);
    // Put a unit back where it started with the movement points it had, with
    // no reachability check: undoing a move is not itself a move, and the
    // return trip can cost more than the outward one because movement costs
    // are charged on the square being entered.
    bool ReturnUnit(int unitIndex, int x, int y, int movement);

    // Attacking. `AttackTargets` lists the unit indices this unit could hit
    // standing where it is now.
    void AttackTargets(int unitIndex, std::vector<int>& out) const;
    bool CanAttack(int attacker, int defender) const;
    CombatResult Attack(int attacker, int defender);
    // The numbers the UI shows before committing: what the attack would do.
    CombatResult Preview(int attacker, int defender) const;

    // Obstacles: the fences and gates a level drops on the map. They are not
    // units and not buildings -- they are *terrain* with hit points, the one
    // type ndTerrainAttributes.ini marks `isBreakable`, and nineteen of the
    // sixty-six levels use them. Twelve missions have no other use for a
    // soldier's first turn than knocking one down, and SP6 is won by it: its
    // only objective is `Special::OnHealthChange(6,5)` falling to zero, which
    // is the wall at that square.
    //
    // The engine gives them their own action rather than folding them into an
    // attack, because almost nothing about a fight applies: it does not shoot
    // back, it costs no ammunition (0x100770f0 copies the firer's rounds
    // through untouched), and the damage is looked up against **the Cannon
    // Tower's row** in both tables -- 0x100770f0 passes 0x11 as the other
    // side's type going out and coming back. A broken wall becomes Plain
    // (0x100475cc: `setTerrain(cell, 6, 0)`).
    // The other kind is a castle -- SpecialCastle4 and SpecialCastle5, and
    // only those two (0x100597ac is the whole rule). It is a building rather
    // than terrain, so its hit points ride on the property, but everything
    // else about shooting it is the same, and it is how SP13 and SP17 end.
    bool IsObstacle(int x, int y) const;
    int ObstacleHealth(int x, int y) const;
    bool CanAttackObstacle(int unitIndex, int x, int y) const;
    // The squares this unit could shoot at, packed as `y * width + x`.
    void ObstacleTargets(int unitIndex, std::vector<int>& out) const;
    ObstacleResult PreviewObstacleAttack(int unitIndex, int x, int y) const;
    ObstacleResult AttackObstacle(int unitIndex, int x, int y);
    bool IsBreakable(int x, int y) const;
    static bool IsCastle(int propertyType);

    // Other unit actions.
    bool CanCapture(int unitIndex) const;
    bool Capture(int unitIndex);          // returns true if it completed
    bool CanSupply(int unitIndex) const;
    int Supply(int unitIndex);            // number of units resupplied
    bool CanLoad(int unitIndex, int transport) const;
    bool LoadUnit(int unitIndex, int transport);
    bool CanUnload(int transport, int slot, int x, int y) const;
    bool Unload(int transport, int slot, int x, int y);
    bool CanJoin(int unitIndex, int other) const;
    bool Join(int unitIndex, int other);
    void Wait(int unitIndex);

    // Destroy a unit outright, crediting no one -- the mission scripts' kill,
    // and what the tests use to stage a battlefield state.
    void KillUnit(int index) { RemoveUnit(index); }
    // What `Unit::CreateSingleUnit` and `Unit::damageUnit` do. Both are
    // scripted rather than played: no cost is charged, no movement is spent,
    // and damage that reaches zero removes the unit through the same path
    // combat does, so its Unit::OnDestroy is raised as usual.
    int CreateUnit(int type, int owner, int x, int y);
    void DamageUnit(int index, int amount);

    // The units destroyed since the last call, in the order they fell. The
    // battle screen drains this to raise Unit::OnDestroy for each one --
    // whoever caused the death, combat or script alike. The entries stay
    // valid indices: a dead unit keeps its type and owner.
    void TakeDeaths(std::vector<int>& out) {
        out = std::move(m_Deaths);
        m_Deaths.clear();
    }

    // Production.
    void Producible(int propertyIndex, std::vector<int>& types) const;
    // Whether the build screen may be raised over this building at all, which
    // is a question about the *square*, not about any one unit type. The
    // engine asks it twice with the same four clauses -- once to decide the
    // cursor's picture (0x1009b494) and once to decide what the select key
    // does (0x1009b028) -- and the load-bearing clause is the last one:
    // `if (*piVar6 != 0) return;`, the cell already holds a unit. A building
    // whose square is occupied is locked, and the one that most often locks it
    // is the man who just finished capturing it.
    bool CanBuildAt(int propertyIndex) const;
    bool CanProduce(int propertyIndex, int type) const;
    int Produce(int propertyIndex, int type);   // new unit index, or -1

    // Fog of war. `Visible` is what `player` can see; it is recomputed
    // whenever a unit moves or a turn changes.
    bool FogEnabled() const { return m_Fog; }
    void SetFog(bool on);
    bool Visible(int player, int x, int y) const;
    void RecomputeVision();

    // Saving. `Save` is a pure read; `Restore` returns false if the snapshot
    // does not fit this board, which is how a save from another level -- or a
    // damaged one -- is turned away instead of half-applied.
    Snapshot Save() const;
    bool Restore(const Snapshot& s);

    // Ten-bar health, and the same number the capture rule uses.
    static int HealthBar(int hp);

    // Terrain and building bonuses at a cell, as the combat code reads them.
    int AttackBonusAt(int x, int y) const;
    int DefenseBonusAt(int x, int y) const;
    int MoveCost(int unitType, int x, int y) const;
    // The same, for a unit whose side may be running Keen Sight.
    int MoveCost(int unitType, int x, int y, int owner) const;
    bool CanEnter(int unitType, int x, int y) const;

    // Is this terrain water? The perks that rewrite what the sea is worth need
    // to know, and so does the mist rule.
    static bool IsWaterTerrain(int terrain);

private:
    int Power(int selfType, int otherType, int hp, int x, int y,
              bool attacking) const;
    int Power(int selfType, int otherType, int hp, int x, int y,
              bool attacking, int owner) const;
    int TerrainCombatBonus(int x, int y, bool attacking, int owner) const;
    void AssignColours();
    void ReserveBlackForTheComputer();
    void BeginTurn(int player);
    void FinishTurn(int player);
    void RemoveUnit(int index);
    // A capture that stops before it finishes gives the building its points
    // back. See the definition for why the counter cannot be left standing.
    void AbandonCapture(Unit& u);
    // Write down what AbandonCapture is about to throw away, so that a move
    // cancelled a moment later can put it back.
    void RecordCaptureUndo(int unitIndex);
    void PlaceUnit(int index, int x, int y);
    void SetCell(int x, int y, int unitIndex);
    // The castle at this square, or -1. Every tile of its footprint answers.
    int ObstacleAt(int x, int y) const;
    // Clearing up after an obstacle: one square back to open ground, or a
    // whole castle's footprint plus the building itself.
    void FlattenCell(int x, int y);
    void RazeProperty(int propertyIndex);
    int AddUnit(int type, int owner, int x, int y, int id);
    void ResetVisionFor(int player);
    // The one-shot half of a perk: the damage, the healing, the gold. Fills
    // `touched` with the units it reached.
    void ApplyPerk(int player, int perk, bool master, std::vector<int>& touched);
    // Count the seat's perks down and drop the spent ones (0x100a0150).
    void ExpirePerks(int player);

    const BattleData* m_Data = nullptr;
    NdLevel m_Level;
    int m_Width = 0, m_Height = 0;
    std::vector<Cell> m_Cells;
    std::vector<Unit> m_Units;
    // Indices RemoveUnit appended, waiting for the screen to raise their
    // Unit::OnDestroy events.
    std::vector<int> m_Deaths;
    // What the rules did this action, for the mission script. See ScriptEvent.
    std::vector<ScriptEvent> m_Events;
    std::vector<Property> m_Properties;
    std::vector<Player> m_Players;   // index 0 unused, 1..4 real
    std::vector<uint8_t> m_Visible;  // (kMaxPlayers+1) * width * height
    int m_Current = 1;
    int m_Round = 0;
    int m_Turn = 0;
    int m_NextID = 1;
    bool m_Fog = false;
    int m_ElapsedMs = 0;
    // The commander behind each seat, by character id. Not part of a snapshot:
    // the level is rebuilt from the pak before one is applied, so the seats
    // have their commanders back before the save is read.
    // The capture the last move interrupted, kept only until that move is
    // either cancelled or built on. Cancelling an order is meant to cost
    // nothing, and walking a half-finished capturer off its building and
    // straight back on again is what cancelling one looks like from here.
    struct CaptureUndo {
        int Unit = -1;
        int Property = -1;
        int Points = 0;
        bool BeingCaptured = false;
    };
    CaptureUndo m_CaptureUndo;
    int m_Character[kMaxPlayers + 1] = {-1, -1, -1, -1, -1};
    // And which army each shouts for; same source, same lifetime.
    int m_Nationality[kMaxPlayers + 1] = {-1, -1, -1, -1, -1};
    std::vector<ActivePerk> m_ActivePerks;
};

}  // namespace bb
