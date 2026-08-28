#include "game/TravelMenu.h"

#include <memory>
#include <string>
#include <vector>

#include "game/Campaign.h"
#include "game/CardPicker.h"
#include "game/Font.h"
#include "game/Game.h"
#include "game/MenuChrome.h"
#include "game/MenuList.h"
#include "game/MenuScreens.h"
#include "game/SaveGame.h"
#include "game/SoundManager.h"
#include "game/StateMachine.h"
#include "game/Strings.h"
#include "game/TextBox.h"
#include "game/TextureCache.h"
#include "game/TravelMap.h"
#include "platform/Host.h"
#include "platform/Storage.h"
#include "platform/Surface.h"
#include "shim/Log.h"

namespace bb {
namespace {

constexpr int kFrameMs = 20;

// The seven rows, with the engine's own indices (0x100ba3a0 adds them 6..12).
// The index is not decoration: the widget picks each board's wood grain from
// it, so starting the list anywhere else changes how the menu looks.
enum Row {
    kRowOpenMissions = 6,
    kRowLogBook = 7,
    kRowSkillChart = 8,
    kRowSave = 9,
    kRowMap = 10,
    kRowSettings = 11,
    kRowEndGame = 12,
};

// Strings, all resolved from the literal pools of the functions named above.
constexpr int kStrNoData = 2303;          // shown as "<No data>"
constexpr int kStrOpenMissions = 2109;
constexpr int kStrLogBook = 2110;
constexpr int kStrSkillChartRow = 2105;   // the menu row
constexpr int kStrSkillChart = 2208;      // the screen's own title
constexpr int kStrSave = 2106;
constexpr int kStrMap = 2107;
// The row the original labels "Pause" here (2108). It carries the battle
// menu's other label instead, for the reason BattleScreen gives at its own
// settings row: pausing a game nobody else is waiting on buys nothing, and the
// string the engine wrote for that row on a networked machine says exactly
// what the port does with it.
constexpr int kStrGameSettings = 72015;
constexpr int kStrEndCurrentGame = 2140;
constexpr int kStrOpenMission = 2141;     // the board over a place
constexpr int kStrPage = 2125;
constexpr int kStrReady = 2209;
constexpr int kStrLand = 1650;
constexpr int kStrSupport = 1651;
constexpr int kStrSea = 1652;
constexpr int kStrPerks = 1655;
constexpr int kStrNone = 1573;
constexpr int kStrPointsAvailable = 2481;
constexpr int kStrYouHave = 1657;         // "Skills and perks you have"
constexpr int kStrAvailable = 1659;       // "Available to buy"
constexpr int kStrNotEnough = 71134;      // "Not enough skill points"
constexpr int kStrNextAvailable = 1582;   // "Next available skill / perk"
constexpr int kStrCost = 1661;
constexpr int kStrSaveGame = 1616;
constexpr int kStrOverwrite = 1613;       // "Overwrite a saved game?"
constexpr int kStrEndGameTitle = 1612;
constexpr int kStrEndGameAsk = 1618;      // "Do you want to end current game?"

// The skill chart's row ids. The engine crams four meanings into two bases and
// tells them apart by which loop added the row; these are four bases, one per
// meaning, which comes to the same thing and reads.
constexpr int kIdReady = 1;
constexpr int kIdGroupBase = 10;          // + 1..3
constexpr int kOwnedSkill = 50000;
constexpr int kOwnedPerk = 60000;
constexpr int kBuySkill = 70000;
constexpr int kBuyPerk = 80000;
constexpr int kLockedSkill = 90000;
constexpr int kLockedPerk = 100000;

// The label and its value are joined by a literal ": " -- 0x100b9dd0 returns
// that, or " : " in French.
const char* Sep(const GameContext& ctx) {
    return ctx.CurrentLanguage == Language::kFr ? " : " : ": ";
}

int GroupOfPerks(int skillGroup) { return skillGroup + 3; }

}  // namespace

TravelMenu::TravelMenu(GameContext& ctx, TravelMap& map,
                       const TravelWorld& world)
    : m_Ctx(ctx), m_Map(map), m_World(world) {}

std::string TravelMenu::Text(int id) const { return m_Ctx.StringsRef.Get(id); }

void TravelMenu::Blip(int which) const {
    if (m_Ctx.Sound)
        m_Ctx.Sound->PlayMenu(static_cast<SoundManager::MenuSound>(which));
}

// --- the list loop -----------------------------------------------------------

int TravelMenu::RunPage(MenuList& list, const std::string& title,
                        bool closeOnMenuKey) {
    Host& host = m_Ctx.HostRef;
    host.FlushKeys();
    const int titleFrame = chrome::PickTitleFrame(m_Ctx);
    const int top = chrome::kListTop + chrome::TitleHeight(m_Ctx, title);

    while (!host.QuitRequested()) {
        if (host.KeyPressed(Key::kUp)) {
            list.MoveUp();
            Blip(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kDown)) {
            list.MoveDown();
            Blip(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight) ||
            (closeOnMenuKey && host.KeyPressed(Key::kSoftLeft))) {
            Blip(SoundManager::kSoundCancel);
            if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
            return kBack;
        }
        if (host.KeyPressed(Key::kSelect) && list.SelectedEnabled()) {
            Blip(SoundManager::kSoundEnter);
            if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
            return list.SelectedId();
        }

        // The chart underneath, still drawn and no longer ticking: the sea
        // holds its ripple and the ship stands where it was.
        Surface& screen = host.Screen();
        m_Map.Draw(screen);
        list.Draw(screen, top, host.TickCount());
        chrome::DrawTitle(m_Ctx, screen, title, titleFrame);
        host.Flip();
        if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
        host.Sleep(kFrameMs);
    }
    return kQuitting;
}

// --- the menu itself ---------------------------------------------------------

TravelMenu::Result TravelMenu::Run() {
    for (;;) {
        MenuList list;
        list.Load(m_Ctx.Textures, m_Ctx.SmallFont, kRowOpenMissions);
        list.Add(Text(kStrOpenMissions), kRowOpenMissions);
        list.Add(Text(kStrLogBook), kRowLogBook);
        list.Add(Text(kStrSkillChartRow), kRowSkillChart);
        // Saving is offered exactly when there is somewhere to save to, the
        // same rule the battle menu uses -- and on a console, when there is a
        // card that could become somewhere, since the row is where a player
        // who declined one at the start comes back to change their mind.
        list.Add(Text(kStrSave), kRowSave, CanSave(m_Ctx));
        list.Add(Text(kStrMap), kRowMap);
        list.Add(Text(kStrGameSettings), kRowSettings);
        list.Add(Text(kStrEndCurrentGame), kRowEndGame);

        list.SetSelected(m_MenuRow);
        const int chosen = RunPage(list, std::string(), /*closeOnMenuKey=*/true);
        m_MenuRow = list.Selected();
        if (chosen == kQuitting) return Result::kQuit;
        if (chosen == kBack) return Result::kClosed;

        Result r = Result::kClosed;
        switch (chosen) {
            case kRowOpenMissions: r = OpenMissions(); break;
            case kRowLogBook: r = LogBook(); break;
            case kRowSkillChart: r = SkillChart(); break;
            case kRowSave: r = Save(); break;
            case kRowMap: r = Chart(); break;
            case kRowSettings: r = GameSettings(); break;
            case kRowEndGame: r = EndGame(); break;
            default: break;
        }
        // kClosed from a submenu means it backed out into this menu; anything
        // else is the whole screen finishing.
        if (r != Result::kClosed) return r;
    }
}

// --- open missions -----------------------------------------------------------

TravelMenu::Result TravelMenu::OpenMissions() {
    for (;;) {
        std::vector<const TravelWorld::Location*> places;
        for (const TravelWorld::Location& loc : m_World.Locations()) {
            if (loc.Key.empty() || loc.Desc <= 0) continue;
            if (!m_Map.IsOpen(loc.ID) || m_Map.IsDone(loc.ID)) continue;
            places.push_back(&loc);
        }

        MenuList list;
        list.Load(m_Ctx.Textures, m_Ctx.SmallFont, 0);
        for (std::size_t i = 0; i < places.size(); ++i) {
            // The row is labelled with the mission's *name*: 0x100baefc adds
            // the blurb id and looks the string up two hundred lower, which is
            // the 10000-series name.
            list.Add(Text(places[i]->Desc - 200), int(i));
        }
        if (places.empty())
            list.Add("<" + Text(kStrNoData) + ">", -1, /*enabled=*/false);

        list.SetSelected(m_MissionsRow);
        const int chosen = RunPage(list, Text(kStrOpenMissions));
        m_MissionsRow = list.Selected();
        if (chosen == kQuitting) return Result::kQuit;
        if (chosen == kBack) return Result::kClosed;
        if (chosen < 0 || std::size_t(chosen) >= places.size()) continue;

        const TravelWorld::Location* loc = places[std::size_t(chosen)];
        TextBox box(m_Ctx, TextBox::kOkCancel);
        box.Title(Text(kStrOpenMission));
        box.Text(Text(loc->Desc));
        const int r = box.Run();
        if (r == TextBox::kQuit) return Result::kQuit;
        // Ok does not sail: it puts the pointer on the place and gives the
        // chart back, which is what 0x100bb698's branch does with it.
        if (r == TextBox::kConfirmed) {
            m_Target = loc->Pos;
            return Result::kMoveTo;
        }
    }
}

// --- log book ----------------------------------------------------------------

TravelMenu::Result TravelMenu::LogBook() {
    const std::vector<int>& pages = m_Ctx.CampaignData.Log;
    for (;;) {
        MenuList list;
        list.Load(m_Ctx.Textures, m_Ctx.SmallFont, 0);
        for (std::size_t i = 0; i < pages.size(); ++i)
            list.Add(Text(kStrPage) + " " + std::to_string(i + 1), int(i));
        if (pages.empty())
            list.Add("<" + Text(kStrNoData) + ">", -1, /*enabled=*/false);

        list.SetSelected(m_LogRow);
        const int chosen = RunPage(list, Text(kStrLogBook));
        m_LogRow = list.Selected();
        if (chosen == kQuitting) return Result::kQuit;
        if (chosen == kBack) return Result::kClosed;
        if (chosen < 0 || std::size_t(chosen) >= pages.size()) continue;

        TextBox box(m_Ctx, TextBox::kOkOk);
        box.Title(Text(kStrPage) + " " + std::to_string(chosen + 1));
        box.Text(Text(pages[std::size_t(chosen)]));
        if (box.Run() == TextBox::kQuit) return Result::kQuit;
    }
}

// --- skill chart -------------------------------------------------------------

// The board a row raises: the name, what it costs, and what it does. For sale
// it is mode 15 (Buy / Cancel) and the answer is acted on; otherwise mode 0,
// which is the same board with nothing to decide.
int TravelMenu::ShowEntry(const std::string& name,
                          const std::string& description, int cost,
                          bool forSale) {
    TextBox box(m_Ctx, forSale ? TextBox::kBuyCancel : TextBox::kOkOk);
    box.Title(name);
    box.Text(Text(kStrCost) + Sep(m_Ctx) + std::to_string(cost));
    box.Text(description);
    return box.Run();
}

TravelMenu::Result TravelMenu::SkillChart() {
    if (!m_TreeReady) {
        m_TreeReady = m_Tree.Load(m_Ctx.Pack);
        if (!m_TreeReady)
            LogError("skills: the chart has no data to sell\n");
    }
    const Campaign& c = m_Ctx.CampaignData;

    for (;;) {
        MenuList list;
        list.Load(m_Ctx.Textures, m_Ctx.SmallFont, 4);
        list.Add(Text(kStrReady), kIdReady);
        list.AddCaption(Text(kStrPointsAvailable) + Sep(m_Ctx) +
                        std::to_string(c.SkillPoints));

        // Each group carries the count of skills taken in it, which is what
        // 0x100a8888 draws beside the three rows every frame.
        const int names[3] = {kStrLand, kStrSupport, kStrSea};
        for (int g = 1; g <= 3; ++g) {
            int taken = 0;
            for (int i = 1; i < Campaign::kSkillSlots; ++i)
                if (c.HasSkill(i) && m_Tree.Skill(i).Group == g) ++taken;
            list.Add(Text(names[g - 1]), kIdGroupBase + g, true,
                     std::to_string(taken));
        }

        list.AddCaption(Text(kStrPerks));
        int perks = 0;
        for (int i = 0; i < Campaign::kPerkSlots; ++i) {
            if (!c.HasPerk(i)) continue;
            ++perks;
            list.Add(Text(Campaign::kPerkNameBase + i), kOwnedPerk + i);
        }
        if (perks == 0) list.Add(Text(kStrNone), -1, /*enabled=*/false);

        list.SetSelected(m_ChartRow);
        const int chosen = RunPage(list, Text(kStrSkillChart));
        m_ChartRow = list.Selected();
        if (chosen == kQuitting) return Result::kQuit;
        if (chosen == kBack || chosen == kIdReady) return Result::kClosed;

        if (chosen > kIdGroupBase && chosen <= kIdGroupBase + 3) {
            const Result r = SkillCategory(chosen - kIdGroupBase);
            if (r != Result::kClosed) return r;
            continue;
        }
        if (chosen >= kOwnedPerk && chosen < kOwnedPerk + Campaign::kPerkSlots) {
            const int perk = chosen - kOwnedPerk;
            if (ShowEntry(Text(Campaign::kPerkNameBase + perk),
                          Text(Campaign::kPerkDescBase + perk),
                          m_Tree.Perk(perk).Cost, false) == TextBox::kQuit)
                return Result::kQuit;
        }
    }
}

TravelMenu::Result TravelMenu::SkillCategory(int group) {
    Campaign& c = m_Ctx.CampaignData;
    const int perkGroup = GroupOfPerks(group);
    const int titles[3] = {kStrLand, kStrSupport, kStrSea};
    const std::string title = Text(titles[group - 1]);
    m_CategoryRow = 0;

    for (;;) {
        MenuList list;
        list.Load(m_Ctx.Textures, m_Ctx.SmallFont, 4);
        list.Add(Text(kStrReady), kIdReady);
        list.AddCaption(Text(kStrPointsAvailable) + Sep(m_Ctx) +
                        std::to_string(c.SkillPoints));

        // What you have.
        list.AddCaption(Text(kStrYouHave));
        int have = 0;
        for (int i = 1; i < Campaign::kSkillSlots; ++i) {
            if (!c.HasSkill(i) || m_Tree.Skill(i).Group != group) continue;
            ++have;
            list.Add(Text(Campaign::kSkillNameBase + i), kOwnedSkill + i);
        }
        for (int i = 0; i < Campaign::kPerkSlots; ++i) {
            if (!c.HasPerk(i) || m_Tree.Perk(i).Group != perkGroup) continue;
            ++have;
            list.Add(Text(Campaign::kPerkNameBase + i), kOwnedPerk + i);
        }
        if (have == 0) list.Add(Text(kStrNone), -1, /*enabled=*/false);

        // What you can afford right now.
        list.AddCaption(Text(kStrAvailable));
        int canBuy = 0;
        for (int i = 1; i < Campaign::kSkillSlots; ++i) {
            const SkillTree::Node& n = m_Tree.Skill(i);
            if (n.Group != group || !m_Tree.SkillAvailable(i, c)) continue;
            if (n.Cost > c.SkillPoints) continue;
            ++canBuy;
            list.Add(Text(Campaign::kSkillNameBase + i), kBuySkill + i, true,
                     std::to_string(n.Cost));
        }
        for (int i = 0; i < Campaign::kPerkSlots; ++i) {
            const SkillTree::Node& n = m_Tree.Perk(i);
            if (n.Group != perkGroup || !m_Tree.PerkAvailable(i, c)) continue;
            if (n.Cost > c.SkillPoints) continue;
            ++canBuy;
            list.Add(Text(Campaign::kPerkNameBase + i), kBuyPerk + i, true,
                     std::to_string(n.Cost));
        }
        if (canBuy == 0) list.Add(Text(kStrNotEnough), -1, /*enabled=*/false);

        // And what is waiting behind it: everything in the group that is not
        // yours yet and cannot be bought today -- too dear, or still locked --
        // minus whatever a choice already made has shut out for good.
        list.AddCaption(Text(kStrNextAvailable));
        int locked = 0;
        for (int i = 1; i < Campaign::kSkillSlots; ++i) {
            const SkillTree::Node& n = m_Tree.Skill(i);
            if (!n.Listed || n.Group != group || c.HasSkill(i)) continue;
            if (m_Tree.SkillAvailable(i, c) && n.Cost <= c.SkillPoints) continue;
            if (m_Tree.SkillLockedOut(i, c)) continue;
            ++locked;
            list.Add(Text(Campaign::kSkillNameBase + i), kLockedSkill + i, true,
                     std::to_string(n.Cost));
        }
        for (int i = 0; i < Campaign::kPerkSlots; ++i) {
            const SkillTree::Node& n = m_Tree.Perk(i);
            if (!n.Listed || n.Group != perkGroup || c.HasPerk(i)) continue;
            if (m_Tree.PerkAvailable(i, c) && n.Cost <= c.SkillPoints) continue;
            if (m_Tree.PerkLockedOut(i, c)) continue;
            ++locked;
            list.Add(Text(Campaign::kPerkNameBase + i), kLockedPerk + i, true,
                     std::to_string(n.Cost));
        }
        if (locked == 0) list.Add(Text(kStrNone), -1, /*enabled=*/false);

        list.SetSelected(m_CategoryRow);
        const int chosen = RunPage(list, title);
        m_CategoryRow = list.Selected();
        if (chosen == kQuitting) return Result::kQuit;
        if (chosen == kBack || chosen == kIdReady) return Result::kClosed;

        // A skill or a perk, owned, for sale, or out of reach: the same board
        // either way, and only the middle one can be answered.
        int answer = TextBox::kCancelled;
        if (chosen >= kLockedPerk) {
            const int i = chosen - kLockedPerk;
            answer = ShowEntry(Text(Campaign::kPerkNameBase + i),
                               Text(Campaign::kPerkDescBase + i),
                               m_Tree.Perk(i).Cost, false);
        } else if (chosen >= kLockedSkill) {
            const int i = chosen - kLockedSkill;
            answer = ShowEntry(Text(Campaign::kSkillNameBase + i),
                               Text(Campaign::kSkillDescBase + i),
                               m_Tree.Skill(i).Cost, false);
        } else if (chosen >= kBuyPerk) {
            const int i = chosen - kBuyPerk;
            answer = ShowEntry(Text(Campaign::kPerkNameBase + i),
                               Text(Campaign::kPerkDescBase + i),
                               m_Tree.Perk(i).Cost, true);
            if (answer == TextBox::kConfirmed) m_Tree.BuyPerk(i, c);
        } else if (chosen >= kBuySkill) {
            const int i = chosen - kBuySkill;
            answer = ShowEntry(Text(Campaign::kSkillNameBase + i),
                               Text(Campaign::kSkillDescBase + i),
                               m_Tree.Skill(i).Cost, true);
            if (answer == TextBox::kConfirmed) m_Tree.BuySkill(i, c);
        } else if (chosen >= kOwnedPerk) {
            const int i = chosen - kOwnedPerk;
            answer = ShowEntry(Text(Campaign::kPerkNameBase + i),
                               Text(Campaign::kPerkDescBase + i),
                               m_Tree.Perk(i).Cost, false);
        } else if (chosen >= kOwnedSkill) {
            const int i = chosen - kOwnedSkill;
            answer = ShowEntry(Text(Campaign::kSkillNameBase + i),
                               Text(Campaign::kSkillDescBase + i),
                               m_Tree.Skill(i).Cost, false);
        }
        if (answer == TextBox::kQuit) return Result::kQuit;
    }
}

// --- the fold-out chart ------------------------------------------------------

TravelMenu::Result TravelMenu::Chart() {
    Host& host = m_Ctx.HostRef;
    m_Map.SetChartOpen(true);
    host.FlushKeys();
    while (!host.QuitRequested()) {
        if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight) ||
            host.KeyPressed(Key::kSoftLeft) || host.KeyPressed(Key::kSelect))
            break;
        m_Map.Draw(host.Screen());
        host.Flip();
        if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
        host.Sleep(kFrameMs);
    }
    m_Map.SetChartOpen(false);
    if (host.QuitRequested()) return Result::kQuit;
    Blip(SoundManager::kSoundCancel);
    // 0x100db6e8: a chart put away from the menu row comes back to the menu.
    return Result::kClosed;
}

// --- save --------------------------------------------------------------------

TravelMenu::Result TravelMenu::Save() {
    // On a console this is where the card gets chosen, if it has not been:
    // the row is live whenever a card is in the machine, and the picker turns
    // that into somewhere to write. Null afterwards means the player looked at
    // the board and said no, which needs no further comment from us.
    Storage* store = SaveTarget(m_Ctx);
    if (m_Ctx.HostRef.QuitRequested()) return Result::kQuit;
    if (!store) return Result::kClosed;

    if (HasGame(*store, SaveKind::kCampaign)) {
        TextBox ask(m_Ctx, TextBox::kOkCancel);
        ask.Title(Text(kStrSaveGame));
        ask.Text(Text(kStrOverwrite));
        const int r = ask.Run();
        if (r == TextBox::kQuit) return Result::kQuit;
        if (r != TextBox::kConfirmed) return Result::kClosed;
    }

    SavedGame game;
    game.CampaignData = m_Ctx.CampaignData;
    game.InBattle = false;
    const SaveStatus status = WriteGame(*store, SaveKind::kCampaign, game);
    TextBox done(m_Ctx, TextBox::kOkOk);
    done.Title(Text(kStrSave));
    done.Text(SaveStatusText(status));
    if (done.Run() == TextBox::kQuit) return Result::kQuit;
    return Result::kClosed;
}

// --- game settings and the way out -------------------------------------------

TravelMenu::Result TravelMenu::GameSettings() {
    // The front end's Settings tree, run on a state machine of its own so that
    // Sound settings, Language, Key configuration and the two confirmations
    // underneath it all work exactly as they do from the main menu -- and from
    // the battle menu's row of the same name (BattleScreen's kActSettings).
    // It ends when Ready or Back empties that machine.
    //
    // The attract reel is held off for the duration: these pages arm it as
    // they open, and a chart left standing on one is not idle.
    const bool attract = m_Ctx.Attract;
    m_Ctx.Attract = false;
    StateMachine settings;
    settings.Run(std::make_unique<SettingsState>(m_Ctx));
    m_Ctx.Attract = attract;
    if (m_Ctx.HostRef.QuitRequested()) return Result::kQuit;
    m_Ctx.HostRef.FlushKeys();
    // Back to the menu the row was chosen from. Run()'s loop builds the list
    // afresh each time round, so Language having just changed every label on
    // it needs nothing further from us.
    return Result::kClosed;
}

TravelMenu::Result TravelMenu::EndGame() {
    TextBox box(m_Ctx, TextBox::kOkCancel);
    box.Title(Text(kStrEndGameTitle));
    box.Text(Text(kStrEndGameAsk));
    const int r = box.Run();
    if (r == TextBox::kQuit) return Result::kQuit;
    if (r == TextBox::kConfirmed) return Result::kAbandoned;
    return Result::kClosed;
}

}  // namespace bb
