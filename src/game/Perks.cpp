#include "game/Perks.h"

#include "game/BattleData.h"
#include "game/BattleField.h"

namespace bb {
namespace {

using T = PerkTargets;

// One row per perk, in id order. The numbers are the ones the perks' own
// modifier slots return; the sheets and blend modes are 0x1009fed0's table.
//
// `duration` 0 means "until the owner's next turn", 1 "one turn beyond that",
// and kInstant that the perk is over as soon as it has been applied.
const PerkDef kPerks[kPerkCount] = {
    // 0 Horse Whisperer -- +1 to movement for Scouts and both Cavalry; Master
    // takes one off the enemy's.
    [] { PerkDef d; d.Duration = 1;
         d.Targets[0] = d.Targets[1] = T::kCavalry;
         d.Move[0] = d.Move[1] = 1;
         d.EnemyTargets[1] = T::kCavalry; d.EnemyMove[1] = -1;
         d.Sheet[0] = 6; d.Blend[0] = 5; d.RefreshMovement = true;
         return d; }(),
    // 1 Plundering Blitz -- the enemy earns nothing while it lasts. The income
    // hook is asked about the *enemy's* seat, so the modifier is -100 per cent
    // against everyone else (0x100a0500 case 1 short-circuits the engine's
    // income sum to zero, which is the same answer).
    [] { PerkDef d; d.Duration = 1;
         d.Sheet[0] = 5; d.Blend[0] = 4; d.OverEnemy = true;
         return d; }(),
    // 2 Supreme Spy-Glasses -- +1 vision, and the hidden are revealed; Master
    // lifts the fog outright for a round.
    [] { PerkDef d; d.Duration = 0;
         d.Targets[0] = d.Targets[1] = T::kAll;
         d.Vision[0] = d.Vision[1] = 1;
         d.Sheet[0] = 9; return d; }(),
    // 3 Flaming Fandango -- the indirect units burn like Scorch Guns; Master
    // slows the enemy's ships.
    [] { PerkDef d; d.Duration = 1;
         d.Targets[0] = d.Targets[1] = T::kIndirect;
         d.EnemyTargets[1] = T::kShips; d.EnemyMove[1] = -1;
         d.Sheet[0] = 1; return d; }(),
    // 4 Fit of Rage.
    [] { PerkDef d; d.Duration = 1;
         d.Targets[0] = T::kIndirect; d.Targets[1] = T::kAll;
         d.Move[0] = d.Move[1] = 1;
         d.Attack[0] = 50; d.Attack[1] = 75;
         d.Defence[0] = -10; d.Defence[1] = -10;
         d.Sheet[0] = 1; d.Blend[0] = 4;
         d.Sheet[1] = 3; d.Blend[1] = 4;
         d.RefreshMovement = true; return d; }(),
    // 5 Technology Break -- Pistoleers become Musketeers, and Master turns the
    // light cavalry heavy.
    [] { PerkDef d; d.Sheet[0] = 8; return d; }(),
    // 6 Guts of Gold.
    [] { PerkDef d; d.Sheet[0] = 2; d.Blend[0] = 4; return d; }(),
    // 7 Superior Supply.
    [] { PerkDef d; d.Sheet[0] = 7; d.Blend[0] = 4; return d; }(),
    // 8 Vermin Infestation.
    [] { PerkDef d; d.Sheet[0] = 7; d.Blend[0] = 4; d.OverEnemy = true;
         return d; }(),
    // 9 Poison.
    [] { PerkDef d; d.Sheet[0] = 2; return d; }(),
    // 10 Doctor's Orders.
    [] { PerkDef d; d.Sheet[0] = 4; return d; }(),
    // 11 Enforced Action -- the ships that have already moved may move again;
    // Master wakes everything.
    [] { PerkDef d; d.Sheet[0] = 0; return d; }(),
    // 12 Rum Delivery -- rum for the whole crew, and it costs them ten points
    // of health.
    [] { PerkDef d; d.Duration = 1;
         d.Targets[0] = d.Targets[1] = T::kAll;
         d.Attack[0] = 75; d.Attack[1] = 150;
         d.Defence[0] = d.Defence[1] = 75;
         d.Vision[0] = d.Vision[1] = -1;
         d.Sheet[0] = 2; d.Sheet[1] = 1; d.Blend[1] = 4;
         return d; }(),
    // 13 Black Spot.
    [] { PerkDef d; d.Sheet[0] = 2; d.Blend[0] = 4; return d; }(),
    // 14 Gambling.
    [] { PerkDef d; d.Sheet[0] = 5; d.Blend[0] = 4; return d; }(),
    // 15 Smooth Sailing.
    [] { PerkDef d; d.Duration = 0;
         d.Targets[0] = d.Targets[1] = T::kShips;
         d.Attack[0] = 20; d.Attack[1] = 30;
         d.Defence[0] = 20; d.Defence[1] = 30;
         d.Vision[0] = 1; d.Vision[1] = 2;
         d.Move[1] = 1;
         d.Sheet[0] = 8; d.Sheet[1] = 1; d.Blend[1] = 4;
         d.RefreshMovement = true; return d; }(),
    // 16 Tactical Genius -- the ground itself is kinder. No apply at all: the
    // terrain rules ask whether it is active.
    [] { PerkDef d; d.Duration = 1; d.Sheet[0] = 3; d.Blend[0] = 4;
         return d; }(),
    // 17 Basker Confusion -- one turn, and the only perk that does not last
    // three (0x100f1bc8 returns 1 where every other class returns 3).
    [] { PerkDef d; d.Duration = 0; d.Sheet[0] = 0; return d; }(),
    // 18 Super Soldiers.
    [] { PerkDef d; d.Duration = 1;
         d.Targets[0] = d.Targets[1] = T::kInfantry;
         d.Attack[0] = 30; d.Attack[1] = 50;
         d.Defence[0] = 10; d.Defence[1] = 20;
         d.Sheet[0] = 8; d.RefreshMovement = true; return d; }(),
    // 19 For the Cause -- every village turns out a soldier.
    [] { PerkDef d; d.Sheet[0] = 8; return d; }(),
    // 20 Hoard Up -- Superior Supply by another name.
    [] { PerkDef d; d.Sheet[0] = 7; return d; }(),
    // 21 Golden Age.
    [] { PerkDef d; d.Duration = 0;
         d.Targets[0] = d.Targets[1] = T::kAll;
         d.Price[0] = -50; d.Price[1] = -75;
         d.Sheet[0] = 5; return d; }(),
    // 22 Supreme Logistics -- a unit put ashore can still act; Master also
    // hurries the transports.
    [] { PerkDef d; d.Duration = 1;
         d.Targets[1] = T::kTransports; d.Move[1] = 1;
         d.Sheet[0] = 8; d.RefreshMovement = true; return d; }(),
    // 23 Blazing Rowboats.
    [] { PerkDef d; d.Duration = 1;
         d.Targets[0] = d.Targets[1] = T::kRowboats;
         d.Attack[0] = 10; d.Attack[1] = 35;
         d.Defence[0] = 10; d.Defence[1] = 25;
         d.Move[0] = d.Move[1] = 1;
         d.Sheet[0] = 8; d.RefreshMovement = true; return d; }(),
    // 24 Keen Sight -- the mist stops slowing the ships down, and they see
    // further; Master makes the open sea as good to fight from as the mist.
    [] { PerkDef d; d.Duration = 1;
         d.Targets[0] = d.Targets[1] = T::kShips;
         d.Vision[0] = d.Vision[1] = 2;
         d.Sheet[0] = 9; return d; }(),
    // 25 Second Wind -- the ships that have moved may move again; Master also
    // sharpens every sea unit's attack.
    [] { PerkDef d; d.Duration = 1;
         d.Targets[1] = T::kShips; d.Attack[1] = 20;
         d.Sheet[0] = 0; return d; }(),
};

const PerkDef kNoPerk;

}  // namespace

const PerkDef& PerkInfo(int perk) {
    if (perk < 0 || perk >= kPerkCount) return kNoPerk;
    return kPerks[perk];
}

bool PerkReaches(const BattleData& data, PerkTargets group, int unitType) {
    if (group == PerkTargets::kNone) return false;
    if (unitType < 0 || unitType >= kUnitTypeCount) return false;
    if (group == PerkTargets::kAll) return true;
    const UnitAttrs& a = data.Unit(unitType);
    switch (group) {
        case PerkTargets::kInfantry:
            // 0x10076790's mask, 7: the three foot soldiers.
            return unitType == kUnitSwordsman || unitType == kUnitPistoleer ||
                   unitType == kUnitMusketeer;
        case PerkTargets::kCavalry:
            return unitType == kUnitScout || unitType == kUnitCavalryLight ||
                   unitType == kUnitCavalryHeavy;
        case PerkTargets::kIndirect:
            return a.UnitClass == kClassIndirect;
        case PerkTargets::kShips:
            return BattleField::IsSeaUnit(unitType);
        case PerkTargets::kRowboats:
            return unitType == kUnitRowingBoat ||
                   unitType == kUnitRowingBoatSwordsman ||
                   unitType == kUnitRowingBoatPistoleer ||
                   unitType == kUnitRowingBoatMusketeer;
        case PerkTargets::kTransports:
            return a.Capacity > 0;
        default:
            return false;
    }
}

std::string PerkSheetPath(int sheet) {
    if (sheet < 0 || sheet > 9) return std::string();
    // 0x1009dad8 keeps one path and writes the digit into it.
    std::string path = "Data\\Battle\\gfx\\perk\\0.tc";
    path[path.size() - 4] = char('0' + sheet);
    return path;
}

int PerkSound(int perk) {
    return perk >= 0 && perk < kPerkCount ? perk : -1;
}

}  // namespace bb
