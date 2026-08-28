// CellBoard — the board the info key raises over the square under the cursor.
//
// A LocalPlayer state of its own: ctor 0x1007f350, activate 0x1007f3a0, tick
// 0x1007f554, key 0x1007f470, draw 0x1007f728. It is *pushed* over the battle
// and its draw renders the battlefield first, so this is an overlay on the map
// rather than a board of its own -- nothing else in the port works that way
// except the overview.
//
// **Three pages, and the info key walks them.** 0x1007f3a0 opens on the unit
// if the viewer can see one standing there, and otherwise on the ground; then
// 0x1007f470 turns unit -> ground on a second press and closes on a third.
// Every other key closes it at once.
//
//   unit      two cards. `build_bg.tc` carries the portrait in the owner's
//             colours, the unit's name, and six figures down the right edge --
//             price, movement, vision, health, rations, ammunition -- plus its
//             attack range, the icon for what it is *for* (capture, close,
//             indirect, transport, stationary, production) and the icon for
//             what it *is* (infantry, artillery, cavalry, gun tower, small sea
//             vessel, large ship). `stats_bg.tc` is the attack/defence table:
//             two bars per unit group, from `Data\attack_defense.txt`. Both are
//             the build screen's own cards -- the engine draws both screens
//             through 0x100530f8 and 0x100535f4 -- with the live unit's
//             numbers rather than the table's.
//
//   ground    `info_bg.tc` with the big terrain or building picture, its name,
//             its defence shields, and what it costs each of the five movement
//             families to enter: foot, horse, wheels, deep-sea ship, small
//             ship (0x10052628 reads unit types 1, 4, 9, 13 and 10 for the
//             costs and 1, 4, 9, 16 and 10 for whether they may enter at all).
//             A family that cannot set foot there gets `amount.tc` in place of
//             a number. A building adds what it pays a turn.
//
// **The description panel** is `bv_text_bg.tc` at x = 100, and it is what the
// board is really for. Every terrain, building and unit has eight pre-wrapped
// lines in the string table at `LongStringId * 1000 + 0..7` -- deep water is
// string 15, so 115000 is "All ships", 115001 "can travel", and so on. Nothing
// wraps at runtime; the translators wrapped it.
//
// **And every stat explains itself.** The d-pad walks a highlight over the
// name and each figure in turn (ten stops on the unit page, seven on terrain,
// eight on a building), and the panel swaps the description for that stat's
// own note -- 601000 "Producing a new unit of this type costs this much"
// through 616000 "How much gold the property produces per turn". The highlight
// repeats every 160 ms while a direction is held.
#pragma once

#include <cstdint>
#include <string>

#include "game/BattleRenderer.h"
#include "platform/Surface.h"

namespace bb {

class BattleField;
class BattleSession;
struct GameContext;
struct Texture;

// The attack/defence table (0x100535f4), which is not the cell board's alone:
// the build screen puts the same card under its own portrait, and the engine
// draws both through this one function. Six rows, one per unit group --
// infantry, artillery, cavalry, gun towers, small sea vessels, large ships --
// and *two* columns of bars, attack on the left under `UnitClass\attack.tc`
// and defence on the right under `defense.tc`, both out of
// `Data\attack_defense.txt`. A zero is not a short bar but `amount.tc`, the
// little red cross.
//
// Not drawn: the `bonus_bar.tc` underlay the engine puts behind a bar whose
// seat has a commander skill lifting it (0x1003686c / 0x100368cc). The port's
// attribute tables carry the flat figures only.
namespace cards {

// Where the card's parts sit, relative to its own top-left corner.
inline constexpr int kGroupIconX = -7;
inline constexpr int kGroupFirstY = 7;
inline constexpr int kRowStep = 12;
inline constexpr int kBarFirstY = 0xc;
inline constexpr int kAttackHeaderX = 0x1c, kAttackBarX = 0x21;
inline constexpr int kDefenceHeaderX = 0x3e, kDefenceBarX = 0x43;
// 0x10053fa4 draws a row's icon six pixels above the line it labels.
inline constexpr int kHeaderY = 2 - 6;
inline constexpr int kGroups = 6;

void DrawStatsCard(Surface& dst, GameContext& ctx, const BattleField& field,
                   int type, int x, int y);

}  // namespace cards

class CellBoard {
public:
    // Which page is up. The numbers are 0x1007f3a0's own.
    enum Page { kNone = 0, kUnit = 1, kTerrain = 2, kProperty = 3 };

    // How many stops the highlight has on each page (0x1007f554's three
    // clamps): name + six figures + range + the two class icons on a unit,
    // name + shields + five movement families on terrain, and one more for a
    // building's income.
    static constexpr int kUnitStops = 10;
    static constexpr int kTerrainStops = 7;
    static constexpr int kPropertyStops = 8;

    // The key-repeat the highlight walks at.
    static constexpr uint32_t kRepeatMs = 0xa0;  // 160

    // Where the cards land. 0x1007f728 keeps them off the cursor: with the
    // cursor in the top half of the screen the unit's card goes to the bottom
    // and its attack table above it, and the other way round below. The attack
    // table also steps right when the cursor is over on the left.
    static constexpr int kCursorSplitY = 100;
    static constexpr int kCursorSplitX = 0x50;   // 80
    static constexpr int kUnitCardLowY = 0x6f;   // 111
    static constexpr int kStatsHighY = 0x14;     // 20
    static constexpr int kStatsLowY = 100;
    static constexpr int kStatsDodgeX = 0x4a;    // 74
    static constexpr int kGroundCardLowY = 0x73; // 115
    // The description panel's own corner, and how many lines it takes.
    static constexpr int kTextX = 100;
    static constexpr int kTextInset = 2;
    static constexpr int kDescLines = 8;

    // Inside a card. The unit card's figures sit one pixel further right than
    // the ground card's rows, and start a pixel higher (0x100530f8 against
    // 0x10052628).
    static constexpr int kPortraitX = 4, kPortraitY = 0xc;
    static constexpr int kNameY = -2;
    static constexpr int kUnitFigureX = 0x3f, kUnitFigureFirstY = -5;
    static constexpr int kGroundRowX = 0x3e, kGroundRowFirstY = -4;
    static constexpr int kRowStep = 12;
    static constexpr int kShieldX = 0x41;
    static constexpr int kRangeIconY = 0x43;
    static constexpr int kClassIconY = 0x4e;
    static constexpr int kRoleIconX = -4;
    static constexpr int kGroupIconX = 0x1b;
    static constexpr int kRangeTextX = 0x3e;
    static constexpr int kCargoBgX = 3, kCargoBgY = 0x35;
    static constexpr int kCargoIconY = 0x30, kCargoIconStep = 0x18;
    // What a row's icon and its value cost horizontally (0x1005435c).
    static constexpr int kRowIconWidth = 0xf;
    static constexpr int kRowTextInset = 4, kRowTextDrop = 3;
    // Where the crossed-out marker goes when a family cannot enter at all
    // (0x1005410c).
    static constexpr int kNoValueX = 0x1a, kNoValueY = 6;

    // The string table's two families: a description is the subject's *long*
    // name id times a thousand, and a stat's note is one of 601..616 the same
    // way.
    static constexpr int kDescScale = 1000;
    static constexpr int kNoteCost = 601;
    static constexpr int kNoteMovement = 602;
    static constexpr int kNoteVision = 603;
    static constexpr int kNoteHealth = 604;
    static constexpr int kNoteRations = 605;
    static constexpr int kNoteAmmo = 606;
    static constexpr int kNoteRange = 607;
    static constexpr int kNoteGroup = 608;
    static constexpr int kNoteRole = 609;
    static constexpr int kNoteShields = 610;
    static constexpr int kNoteFootCost = 611;   // .. 615, in family order
    static constexpr int kNoteIncome = 616;

    // The five movement families the ground page lists, as the unit type each
    // is read from: the cost comes off one type and whether the square can be
    // entered at all off another, which is the engine's own asymmetry -- the
    // sails column prices a Mortar Boat and asks a Man-o-War.
    static constexpr int kFamilies = 5;
    static const int kFamilyCostUnit[kFamilies];
    static const int kFamilyEnterUnit[kFamilies];

    bool Load(GameContext& ctx);
    bool Ready() const { return m_Ready; }

    // Run the board over the square the view's cursor is on. Returns false if
    // the host asked to quit while it was up.
    bool Run(GameContext& ctx, BattleSession& session,
             const BattleRenderer::View& view);

    // Which page a square opens on, given what `viewer` can see of it: kUnit
    // when a unit is standing there, kProperty over a building, kTerrain
    // otherwise.
    static Page PageFor(const BattleField& field, int cellX, int cellY,
                        int viewer);
    // How many stops that page's highlight has.
    static int StopsOn(Page page);
    // The string id of the note behind stop `slot` on `page`, or 0 for the
    // stop that shows the subject's own description instead.
    static int NoteFor(Page page, int slot);

    // Test seam: the eight lines the description panel would show for a
    // subject whose long name id is `longID`, joined by newlines.
    static std::string Description(const GameContext& ctx, int longID);

private:
    void DrawUnitCard(Surface& dst, GameContext& ctx, const BattleField& field,
                      const BattleRenderer& renderer, int unit, int x, int y,
                      int slot, uint32_t tick) const;
    void DrawGroundCard(Surface& dst, GameContext& ctx,
                        const BattleField& field,
                        const BattleRenderer& renderer, int cellX, int cellY,
                        int x, int y, int slot, uint32_t tick) const;
    // `bv_text_bg.tc` and the eight lines on it.
    void DrawDescription(Surface& dst, GameContext& ctx, int longID,
                         int y) const;
    void DrawNote(Surface& dst, GameContext& ctx, int note, int y) const;
    void DrawLines(Surface& dst, GameContext& ctx, int base, int y) const;
    // 0x100541dc: an icon, a value beside it, and the selection box over both
    // when this is the stop the highlight is on.
    void DrawRow(Surface& dst, GameContext& ctx, int x, int y,
                 const std::string& text, const Texture* icon, bool selected,
                 uint32_t tick) const;
    // 0x10053f04: the same for the name, which wears a wider box.
    void DrawName(Surface& dst, GameContext& ctx, int x, int y,
                  const std::string& text, bool selected, uint32_t tick) const;

    bool m_Ready = false;
    const Texture* m_UnitBg = nullptr;      // 0xc6 build_bg.tc
    const Texture* m_GroundBg = nullptr;    // 0xc4 info_bg.tc
    const Texture* m_TextBg = nullptr;      // 0xb8 bv_text_bg.tc
    const Texture* m_SelectName = nullptr;  // 0xc9
    const Texture* m_SelectSmall = nullptr; // 0xc7
    const Texture* m_Shield = nullptr;       // 0x34
    const Texture* m_Amount = nullptr;       // 0x4d
    const Texture* m_CargoBg = nullptr;     // 0xe3
    const Texture* m_TerrainLarge = nullptr;  // 0x51
    const Texture* m_PropLarge = nullptr;     // 0x53
    const Texture* m_Figures[6] = {};        // 0x37, 0x3b, 0x3a, 0x36, 0x38, 0x35
    const Texture* m_Families[kFamilies] = {};  // 0x3d .. 0x41
    const Texture* m_Groups[6] = {};         // 0x9a .. 0x9f
    const Texture* m_Roles[6] = {};          // 0xa4 .. 0xa9
    const Texture* m_RangeClose = nullptr;  // 0xa2
    const Texture* m_RangeFar = nullptr;    // 0xa3
    const Texture* m_SmallIcon[kUnitTypeCount] = {};
    const Texture* m_LargeIcon[kUnitTypeCount] = {};
};

}  // namespace bb
