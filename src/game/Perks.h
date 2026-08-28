// Perks — the commander's twenty-six special powers, and the table that says
// what each one does.
//
// **The bar.** Every commander carries a pool of perk points (0x1005db70: four
// hundred of them, and a battle starts with it empty) and spends the *whole*
// pool
// to use a perk. What you get for it depends on how full the bar was: sixty
// per cent buys the Regular version, and only a full bar buys the Master one
// (0x10077fa4 is asked twice, once with each level, and 0x10047b70 takes the
// better answer). Either way the bar goes back to zero.
//
// The bar fills two ways. A turn is worth fifty on its own, or seventy to the
// commander whose army is worth more than every other on the board
// (0x100425a0). And a loss is worth what was lost: when a unit dies its owner
// gains that unit's *price* (0x10046cac hands 0x10076db8's two price fields to
// 0x1005d940, one per side). Both are charged against a ceiling of a third of
// the pool plus one -- 134 points -- a turn, so no single massacre refills it.
//
// **What a perk is.** One class each in the original (0x1009fa24 is a switch
// that news twenty-six of them over a common base at 0x1009f8dc), and the base
// is a small interface the battle rules consult:
//
//   * an *apply*, run once when the perk goes off -- the damage, the healing,
//     the gold, the ammo;
//   * a *duration* in turns, or -2 for a perk that is nothing but its apply
//     (0x1009f918 stores the answer, and 0x100a0150 counts it down at the
//     owner's turn and drops the perk at zero);
//   * up to seven *modifier* hooks -- movement, vision, attack, defence,
//     price, income -- which 0x100a0330 sums over every active perk whenever
//     the rules want one of those numbers.
//
// That is what this file is: the same interface as data. A perk's numbers are
// the ones its modifier slots return (Super Soldiers' +30/+10 is literally
// 0x100afeec returning 0x1e and 0x100aff44 returning 10), and the units it
// reaches are the group its slots test for.
//
// **The animation** is one of ten sheets, `Data\Battle\gfx\perk\0.tc` through
// `9.tc` -- 0x1009dad8 builds the path by patching the digit -- played over
// every unit the perk touched, with the perk's own sound out of
// `Data\Battle\sfx\perks.dat`. Four perks have a second sheet for their
// Master version; the rest reuse the first. The table's other number is the
// blitter mode to draw it in, and both modes it uses are additive: see
// PerkFlash.
#pragma once

#include <string>

namespace bb {

class BattleData;

// Which units a perk's modifiers reach. The original asks the unit table
// (0x10076790 returns the infantry mask 7, 0x100766bc tests for cavalry);
// these are the same groups by name.
enum class PerkTargets {
    kNone,
    kAll,
    kInfantry,     // Swordsman, Pistoleer, Musketeer
    kCavalry,      // Scout and both Cavalry
    kIndirect,     // Mortar, Cannon, Scorch Gun and the sea's indirect pair
    kShips,        // everything that swims
    kRowboats,     // the four rowing boats
    kTransports,   // Wagon and Transport-Heavy
};

// The seven things a perk can change, in the order 0x100a0330 numbers them.
enum class PerkStat {
    kMovement = 0,
    kVision = 1,
    kAttack = 2,
    kDefence = 3,
    kPrice = 5,
    kIncome = 6,
};

struct PerkDef {
    // Turns the perk stays active for, or kInstant when it is nothing but its
    // apply. Zero means it lasts until the owner's next turn comes round.
    static constexpr int kInstant = -2;
    int Duration = kInstant;

    // Per level: 0 Regular, 1 Master.
    PerkTargets Targets[2] = {PerkTargets::kNone, PerkTargets::kNone};
    int Move[2] = {0, 0};      // whole movement points
    int Vision[2] = {0, 0};    // whole blocks
    int Attack[2] = {0, 0};    // per cent
    int Defence[2] = {0, 0};
    int Price[2] = {0, 0};     // per cent, negative is cheaper
    int Income[2] = {0, 0};

    // The Master version's own tricks, which are not modifiers: an enemy
    // penalty, or a rule the battle asks about by name.
    int EnemyMove[2] = {0, 0};        // Horse Whisperer, Flaming Fandango
    PerkTargets EnemyTargets[2] = {PerkTargets::kNone, PerkTargets::kNone};

    // The animation: which of the ten sheets, and which blitter mode
    // 0x1009fed0's table asks for it to be drawn in. -1 is a level with no
    // sheet of its own, which plays the Regular one. The modes the perks use
    // are four and five, and both are additive -- see PerkFlash.
    int Sheet[2] = {-1, -1};
    int Blend[2] = {5, 5};
    // The two enemy-facing perks play their animation over *enemy* units
    // (0x100a0088's byte, set for Plundering Blitz and Vermin Infestation).
    bool OverEnemy = false;

    // Granting the perk hands its units a movement point there and then, so a
    // buff bought mid-turn can be used in the same turn (the +1 loop that ends
    // most of the applies).
    bool RefreshMovement = false;
};

// The twenty-six perks, by the ids the perk table and the string table use.
// Named where a rule elsewhere has to ask for one by name.
enum PerkId {
    kPerkHorseWhisperer = 0,
    kPerkPlunderingBlitz = 1,
    kPerkSupremeSpyGlasses = 2,
    kPerkFlamingFandango = 3,
    kPerkFitOfRage = 4,
    kPerkTechnologyBreak = 5,
    kPerkGutsOfGold = 6,
    kPerkSuperiorSupply = 7,
    kPerkVerminInfestation = 8,
    kPerkPoison = 9,
    kPerkDoctorsOrders = 10,
    kPerkEnforcedAction = 11,
    kPerkRumDelivery = 12,
    kPerkBlackSpot = 13,
    kPerkGambling = 14,
    kPerkSmoothSailing = 15,
    kPerkTacticalGenius = 16,
    kPerkBaskerConfusion = 17,
    kPerkSuperSoldiers = 18,
    kPerkForTheCause = 19,
    kPerkHoardUp = 20,
    kPerkGoldenAge = 21,
    kPerkSupremeLogistics = 22,
    kPerkBlazingRowboats = 23,
    kPerkKeenSight = 24,
    kPerkSecondWind = 25,
};
constexpr int kPerkCount = 26;
const PerkDef& PerkInfo(int perk);

// Does `unitType` fall in `group`? Needs the unit table for the classes and
// the movement masks -- what swims and what does not is not a list in the
// original either.
bool PerkReaches(const BattleData& data, PerkTargets group, int unitType);

// `Data\Battle\gfx\perk\<n>.tc`, the sheet `sheet` names.
std::string PerkSheetPath(int sheet);

// The sound bank every perk's noise comes out of, and the entry for one perk.
constexpr const char* kPerkSoundBank = "Data\\Battle\\sfx\\perks.dat";
int PerkSound(int perk);

}  // namespace bb
