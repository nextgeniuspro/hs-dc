// The single-player campaign's opening screens and its mission chain.
//
// `New game` on the single-player menu (item id 2 of 0x100a79b8) starts a
// three-step run-up before the first battle, and each step is its own state:
//
//   NewGameState     0x1009a6b0  name your commander and pick a colour
//   PerkSelectState  0x1007f9a4  one of three opening perks, each of which
//                                opens a TextBox to explain itself before it
//                                is taken (0x1007fbac, mode 10 = Ok / Back)
//   CampaignState    0x100bd23c  builds the commander and player (0x1007fc4c),
//                                then runs the campaign
//
// **The setup screen's materials.** The engine's list widget bakes a board
// behind every row and the board comes from a style index (0x1008cbd4): the
// two labels are kind-8 *text* rows on `Data\Menu\subtopic.tc`, a grey stone
// slab hung on iron rings, and the name field is the input widget's
// `Data\Menu\input_bg.tc`, wood with a grey arrow and a pair of iron brackets
// with the name written between them at x+0x36 (0x100b1cec). Everything you
// can actually pick -- the colours and Ready -- is the ordinary
// `shell_board.tc`. That contrast is deliberate: stone and iron mark the
// things that are not buttons.
//
// **Order matters.** 0x1009a6b0 adds Ready *first*, immediately under the
// title, and only then the name caption, the field, the colour caption and the
// three colours. Everything shares one running y that starts at 17, so the
// page comes out exactly 208 tall and never scrolls.
//
// The three colour rows carry ids 11, 13 and 14 and the screen stores `id -
// 10`, so the player can be red, blue or yellow; black is the enemy's and is
// never offered even though string 1675 names it. Whichever is current wears a
// gold coin (`Data\icons\marker.tc`, six frames) at x+0x78, and picking one
// restarts the coin's flip -- one frame every 0x46 ms up to frame 5.
//
// One deliberate difference: the original edits the name with multi-tap on the
// numeric keypad (0x100b112c). Off the device there is a keyboard, so the
// field takes typed characters through `Host::PollText` instead.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/StateMachine.h"

namespace bb {

class Surface;
struct GameContext;
struct SavedBattle;
struct Texture;

// Name your commander and choose a colour.
class NewGameState : public GameState {
public:
    explicit NewGameState(GameContext& ctx) : m_Ctx(ctx) {}
    void Run(StateMachine& sm) override;
    const char* Name() const override { return "NewGame"; }

    // The selectable rows, in the order the engine adds them: Ready is first.
    enum Row { kRowReady, kRowName, kRowRed, kRowBlue, kRowYellow, kRowCount };

    // The colour a row stands for -- the engine's `id - 10` (0x1009a6b0).
    static int ColourOf(int row);
    // The item id the engine gives a row, which also picks its board grain.
    static int IdOf(int row);

    // Test seams.
    int Selected() const { return m_Selected; }
    void SelectRow(int r) { m_Selected = r; }
    const std::string& NameField() const { return m_Name; }

private:
    struct Layout {
        int Ready = 0;
        int NameCaption = 0;
        int Field = 0;
        int ColourCaption = 0;
        int Colour[3] = {0, 0, 0};
    };
    Layout Measure() const;
    int RowY(const Layout& l, int row) const;
    void Draw(Surface& dst);

    GameContext& m_Ctx;
    std::string m_Name;
    int m_Selected = kRowReady;
    bool m_Editing = false;
    int m_TitleFrame = 0;
    // The coin beside the chosen colour: which frame it is on and when it last
    // turned (0x100acdf4's +0xc4 / +0xc8).
    int m_CoinFrame = 1;
    uint32_t m_CoinSince = 0;
};

// Pick one of the three opening perks.
class PerkSelectState : public GameState {
public:
    explicit PerkSelectState(GameContext& ctx) : m_Ctx(ctx) {}
    void Run(StateMachine& sm) override;
    const char* Name() const override { return "PerkSelect"; }

    // Show one perk's board and ask whether to take it. Returns true if the
    // player confirmed (0x1007fbac).
    static bool Explain(GameContext& ctx, int perk);

private:
    GameContext& m_Ctx;
};

// Runs the campaign: for now, the missions in order, then a placeholder for
// the travel map.
class CampaignState : public GameState {
public:
    explicit CampaignState(GameContext& ctx) : m_Ctx(ctx) {}
    // Carry on a game that was saved from the battle menu: the battle it was
    // saved in runs first, and the chart follows.
    CampaignState(GameContext& ctx, std::unique_ptr<SavedBattle> resume);
    // Out of line, because SavedBattle is only forward-declared here.
    ~CampaignState() override;

    void Run(StateMachine& sm) override;
    const char* Name() const override { return "Campaign"; }

private:
    GameContext& m_Ctx;
    std::unique_ptr<SavedBattle> m_Resume;
};

}  // namespace bb
