// BuildMenu — the screen a shipyard or a headquarters opens.
//
// This is not the wooden list every other menu in the game uses. Building a
// unit gets a screen of its own (LocalPlayer state, ctor 0x1005a90c, frame
// 0x1005aaf0) with art nothing else touches, and it is laid out in two
// columns:
//
//   left    the commander header at (0, 0) -- flag, name, power meter and
//           treasury, exactly the one the status panel draws (0x10052264) --
//           and under it a scrolling list of what this building can make.
//           Each row is its own 71x26 board, `Data\Menu\build_item_bg.tc`:
//           **frame 0 when you can afford it and frame 1, a grey stone slab,
//           when you cannot**. The row carries the unit's small icon in the
//           player's colours, its name, and its price under a coin.
//
//   right   two cards about whatever the cursor is on. The upper one
//           (`build_bg.tc` at (75, 3)) is the unit's portrait -- the large
//           icon, its name, and six figures down the right edge: price,
//           movement, vision, health, rations and ammunition. The lower one
//           (`stats_bg.tc` at (75, 100)) is its attack/defence table: six
//           rows, one per unit group -- infantry, artillery, cavalry, cannon
//           towers, small sea vessels, large ships -- each two bars out of
//           `Data\attack_defense.txt`. Both cards belong to the cell board as
//           much as to this screen; the table is cards::DrawStatsCard.
//
// The list's own numbers (0x1005a52c): rows start at y = 60 and step 26, the
// selection is `select_big.tc` at (0, row - 5) with a ping-ponged frame, the
// window scrolls over `count * 26 - 108` pixels, and the two arrows sit at
// (25, 50) and (25, 190) with their *alpha* saying which way there is more --
// the same two-lamp scrollbar the front end uses.
#pragma once

#include <string>
#include <vector>

#include "game/BattleRenderer.h"
#include "platform/Surface.h"

namespace bb {

class BattleField;
class BattlePanels;
struct GameContext;
struct Texture;

class BuildMenu {
public:
    // Everything from 0x1005a52c and 0x100530f8 / 0x100535f4.
    static constexpr int kRowX = 5;
    static constexpr int kRowTop = 0x3c;      // 60
    static constexpr int kRowStep = 0x1a;     // 26
    static constexpr int kWindow = 0x6c;      // 108 px of list before it scrolls
    static constexpr int kArrowX = 0x19;      // 25
    static constexpr int kArrowUpY = 0x32;    // 50
    static constexpr int kArrowDownY = 0xbe;  // 190
    // Inside a row.
    static constexpr int kRowIconX = -4, kRowIconY = -5;
    static constexpr int kRowCoinX = 28, kRowCoinY = 6;
    static constexpr int kRowNameX = 0x22, kRowNameY = -1;
    static constexpr int kRowCostX = 0x2e, kRowCostY = 11;
    // The two cards.
    static constexpr int kCardX = 0x4b;       // 75
    static constexpr int kInfoY = 3;
    static constexpr int kStatsY = 100;
    static constexpr int kPortraitX = 4, kPortraitY = 0xc;
    static constexpr int kFigureX = 0x3f;     // from the card's own left edge
    static constexpr int kFigureFirstY = -5;
    static constexpr int kFigureStep = 12;
    // How many unit groups the attack table has. Everything else about that
    // card lives with the card, in cards:: -- two screens draw it.
    static constexpr int kGroups = 6;

    // Which of the six groups the stats card files a unit under (0x100369c8).
    static int GroupOf(int unitType);

    // Where a unit's picture lives, in the `small` or the `large` set. There
    // are eighteen pictures for twenty-one types -- the three loaded rowing
    // boats share the empty one's (0x10054480) -- and an empty string means
    // the type has none. Shared with the cell board, which draws the same
    // portrait from the same two sets.
    static std::string IconPath(const char* set, int unitType);

    bool Load(GameContext& ctx);
    bool Ready() const { return m_Ready; }

    // Run the screen for `propertyIndex`. Returns the unit type to build, or
    // -1 if the player backed out. `panels` is only borrowed for its commander
    // header, which is the same one the status panel draws.
    int Run(GameContext& ctx, BattleField& field, BattleRenderer& renderer,
            BattlePanels& panels, const BattleRenderer::View& view,
            int propertyIndex);

    // What the list would show, for tests: one entry per producible type in
    // ascending order, with the price and whether it is affordable.
    struct Row {
        int Type = 0;
        int Cost = 0;
        bool Affordable = false;
    };
    static std::vector<Row> Rows(const BattleField& field, int propertyIndex);

private:
    void Draw(Surface& dst, GameContext& ctx, const BattleField& field,
              BattleRenderer& renderer, BattlePanels& panels,
              const BattleRenderer::View& view, const std::vector<Row>& rows,
              int cursor, int scroll, uint32_t tick) const;
    void DrawRow(Surface& dst, GameContext& ctx, const BattleField& field,
                 const BattleRenderer& renderer, const Row& row, int y) const;
    void DrawInfoCard(Surface& dst, GameContext& ctx, const BattleField& field,
                      const BattleRenderer& renderer, int type,
                      int colour) const;
    void DrawStatsCard(Surface& dst, GameContext& ctx,
                       const BattleField& field, int type) const;

    bool m_Ready = false;
    const Texture* m_RowBg = nullptr;
    const Texture* m_InfoBg = nullptr;
    const Texture* m_StatsBg = nullptr;
    const Texture* m_Select = nullptr;
    const Texture* m_ScrollUp = nullptr;
    const Texture* m_ScrollDown = nullptr;
    const Texture* m_Coin = nullptr;
    // The six stat icons the portrait lists, in the order it lists them. The
    // attack table's own artwork belongs to cards::DrawStatsCard.
    const Texture* m_Figures[6] = {};
    // Slots 0x61.. and 0x81.., one per unit type; 18 pictures for 21 types.
    const Texture* m_SmallIcon[32] = {};
    const Texture* m_LargeIcon[32] = {};
};

}  // namespace bb
