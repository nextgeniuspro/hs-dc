// TravelMenu — the board the chart raises when the menu key is pressed.
//
// The travel view is one function in the original (0x100ba3a0) and it is a
// small state machine of its own, kept in the word at +0x1b8: 0 sails the map,
// 1 has this menu up, 3 the Open missions list, 4 the Log book, 5 a board over
// either of them. The map is redrawn under the menu and stops being *ticked*,
// so the sea freezes with the chart still visible behind the buttons.
//
// The menu is seven rows, added with the engine's own indices 6..12 and no
// title plank at all:
//
//   6  Open missions   (2109)  where the voyage may still go
//   7  Log book        (2110)  the captain's journal
//   8  Skill chart     (2105)  spend what the missions paid out
//   9  Save            (2106)
//  10  Map             (2107)  the fold-out chart
//  11  Game settings   (72015) -- the original's "Pause" (2108); see below
//  12  End current game (2140)
//
// **Open missions** lists every place that is open and unfinished, labelled
// with the mission's name, and a place picked out of it raises the same "Open
// mission" board (2141) the chart raises when you point at it. Ok does *not*
// set sail here: 0x100bb698's branch moves the pointer onto the place and
// hands the map back, because the list is a way of finding somewhere, not a
// way of going there. Cancel returns to the list.
//
// **Log book** is the captain's journal: one page per entry, labelled "Page n"
// (2125), and each opens the page. The entries themselves are string ids the
// campaign collects as it goes -- see Campaign::AddLogPage.
//
// **Skill chart** is a screen of its own (0x100a8888): Ready, the points you
// have, the three groups -- Land, Support, Sea -- and the perks you own. Each
// group opens a page (0x100a8b94) of what you have in it, what you can buy,
// and what is next, and buying is a Buy/Cancel board over the cost and the
// description. What may be bought is SkillTree's business.
//
// **Save**, **Map** and **End current game** are one board each.
//
// **Game settings** is where the original's Pause row was. Pause is the
// phone's kind of pause: the game stops and asks whether you want out (string
// 1619), which is the same branch the engine takes when the app loses focus --
// there is nothing else a suspended phone game can offer, and nothing worth
// having on a machine nobody else is waiting on. So the row does here what the
// battle menu's row of the same index does (BattleScreen's kActSettings): it
// opens the front end's Settings tree, wearing the label the engine wrote for
// exactly that (72015, the networked half of 0x1004eec8's pair).
//
// Not ported: the engine's file-manager dialog behind Save (this port has one
// campaign slot, not a directory of named files) and the memory-card space
// check, whose 32000-byte threshold belongs to a game deck.
#pragma once

#include <string>

#include "game/SkillTree.h"
#include "game/TravelWorld.h"

namespace bb {

class MenuList;
class TravelMap;
struct GameContext;

class TravelMenu {
public:
    // Why the menu gave the chart back.
    enum class Result {
        kClosed,     // back to sailing
        kMoveTo,     // Open missions: put the pointer on Target()
        kAbandoned,  // "End current game"
        kQuit,       // the host wants out
    };

    TravelMenu(GameContext& ctx, TravelMap& map, const TravelWorld& world);

    // Run the menu and everything under it. Blocks until it closes.
    Result Run();

    // Where Open missions pointed, valid when Run() returned kMoveTo.
    TravelWorld::Point Target() const { return m_Target; }

private:
    // What RunPage returns instead of a row id.
    static constexpr int kBack = -1;
    static constexpr int kQuitting = -2;

    // One list over the frozen chart. `title` empty draws no plank, which is
    // what the travel menu itself does.
    int RunPage(MenuList& list, const std::string& title,
                bool closeOnMenuKey = false);

    Result OpenMissions();
    Result LogBook();
    Result SkillChart();
    Result SkillCategory(int group);
    Result Chart();
    Result Save();
    Result GameSettings();
    Result EndGame();

    // The Buy/Cancel and Ok boards the skill chart's rows raise.
    int ShowEntry(const std::string& name, const std::string& description,
                  int cost, bool forSale);

    std::string Text(int id) const;
    void Blip(int which) const;

    GameContext& m_Ctx;
    TravelMap& m_Map;
    const TravelWorld& m_World;
    SkillTree m_Tree;
    bool m_TreeReady = false;
    TravelWorld::Point m_Target{};
    // Where the cursor was left on each page. The engine keeps the widgets
    // themselves alive across a board -- the menu remembers its row in +0x1c0
    // and hands it back when the board closes -- and rebuilding the list from
    // a remembered index comes to the same thing.
    int m_MenuRow = 0;
    int m_MissionsRow = 0;
    int m_LogRow = 0;
    int m_ChartRow = 0;
    int m_CategoryRow = 0;
};

}  // namespace bb
