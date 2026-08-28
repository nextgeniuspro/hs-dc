#include "game/BattleInfo.h"

#include <string>
#include <vector>

#include "game/BattleField.h"
#include "game/BattleScreen.h"
#include "game/Campaign.h"
#include "game/Commanders.h"
#include "game/Game.h"
#include "game/MenuChrome.h"
#include "game/MissionFlow.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/TextBox.h"
#include "platform/Host.h"
#include "platform/Surface.h"

namespace bb {
namespace {

// The boards' own titles.
constexpr int kStrMissionObjectives = 2057;
constexpr int kStrBattleInfo = 2058;
constexpr int kStrCommanderInfo = 1579;
// The objectives line: the same MISSION_INDEX the name is keyed by, a hundred
// higher (0x100ae5ac reads `mission + 0x2774`).
constexpr int kMissionObjectivesBase = 10100;
// Battle info's labels, in 0x100ae5ac's order.
constexpr int kStrRoundsPlayed = 1711;
constexpr int kStrTimeElapsed = 1712;
constexpr int kStrUnits = 1713;
constexpr int kStrProperties = 1714;
constexpr int kStrUnitsBuilt = 1715;
constexpr int kStrUnitsDestroyed = 1716;
constexpr int kStrUnitsLost = 1717;
constexpr int kStrPropsCaptured = 1718;
constexpr int kStrPropsLost = 1719;
constexpr int kStrGoldCollected = 1720;
// Commander info's two headings and the word both boards fall back to.
constexpr int kStrSkills = 1722;
constexpr int kStrPerks = 1655;
constexpr int kStrNone = 1573;
// The soft keys the two paged books use: Next/Exit on the first page,
// Next/Back in the middle, Exit/Back on the last (0x10065ef0 cases 0xc, 0xb
// and 0xd).
constexpr int kPageFirst = TextBox::kNextExit;
constexpr int kPageMiddle = 11;
constexpr int kPageLast = 13;

constexpr const char* kMoneyIcon = "Data\\icons\\money.tc";
constexpr const char* kFlagIcon = "Data\\icons\\flags-small.tc";
// Where the text sits on a line with a picture beside it: the money line
// indents to 20 (0x100ae5ac) and the commander's name to 30 (0x1010105c).
constexpr int kMoneyTextX = 0x14;
constexpr int kNameTextX = 0x1e;

// The label and its value are joined by a literal ": " -- 0x100b9dd0 returns
// that, or " : " in French, and nothing else.
std::string Sep(const GameContext& ctx) {
    return ctx.CurrentLanguage == Language::kFr ? " : " : ": ";
}

std::string Row(const GameContext& ctx, int label, int value) {
    return ctx.StringsRef.Get(label) + Sep(ctx) + std::to_string(value);
}

// Minutes and seconds, zero-padded, the way 0x1008a91c formats them.
std::string Clock(int seconds) {
    if (seconds < 0) seconds = 0;
    const int m = seconds / 60, s = seconds % 60;
    return std::to_string(m) + ":" + (s < 10 ? "0" : "") + std::to_string(s);
}

// The seats a book pages through, in seat order.
std::vector<int> Seats(const BattleField& f) {
    std::vector<int> out;
    for (int p = 1; p <= BattleField::kMaxPlayers; ++p)
        if (f.Players()[std::size_t(p)].Present) out.push_back(p);
    return out;
}

// Which soft keys a page of `count` shows.
int PageMode(int index, int count) {
    if (index <= 0) return kPageFirst;
    return index + 1 >= count ? kPageLast : kPageMiddle;
}

// What to call a seat. A commander with a name string is called by it --
// 0x1005f9f8 prefers `5100 + type` over the name the level file wrote -- and
// the player's own commander is string 5113, the literal `[player]`, which
// TextBox substitutes for the name on the New game screen.
std::string SeatName(const GameContext& ctx, const BattleField& f, int seat) {
    const int id = commanders::NameString(f.Character(seat));
    if (id != 0 && !ctx.StringsRef.Get(id).empty())
        return TextBox::Substitute(ctx, ctx.StringsRef.Get(id));
    return f.Players()[std::size_t(seat)].Name;
}

// How many buildings a seat holds. The engine sums its statistics object's
// per-type array (0x10051438 case 4); counting the board comes to the same
// number and does not need the array.
int PropertyCount(const BattleField& f, int seat) {
    int n = 0;
    for (const BattleField::Property& p : f.Properties())
        if (p.Owner == seat) ++n;
    return n;
}

// Everything the perk section needs, for both the "none" case and the rows.
void AddPerks(GameContext& ctx, TextBox& box, const BattleField& f, int seat) {
    // A big-font heading that claims no height, followed by the blank whose
    // fifteen pixels it borrows -- see TextBox.h.
    box.Add(TextBox::kHeading, ctx.StringsRef.Get(kStrPerks));
    box.Add(TextBox::kTextLow, "");
    int shown = 0;
    for (int p = 0; p < Campaign::kPerkCount; ++p) {
        if (!f.HasPerk(seat, p)) continue;
        ++shown;
        box.Text(ctx.StringsRef.Get(Campaign::kPerkNameBase + p) + Sep(ctx));
        box.Text(ctx.StringsRef.Get(Campaign::kPerkDescBase + p));
        box.Add(TextBox::kTextLow, "");
    }
    if (shown == 0) box.Text(ctx.StringsRef.Get(kStrNone));
}

// The skill chart as a sentence, which is what the board shows in place of a
// canned summary for the one commander whose skills the player picked.
void AddSkills(GameContext& ctx, TextBox& box) {
    std::string line = ctx.StringsRef.Get(kStrSkills) + Sep(ctx);
    int shown = 0;
    // Slot zero is the chart's inert "initial state" row and is never bought;
    // 0x1010105c starts its walk at one for that reason.
    for (int s = 1; s < Campaign::kSkillSlots; ++s) {
        if (!ctx.CampaignData.HasSkill(s)) continue;
        if (shown++ > 0) line += ", ";
        line += ctx.StringsRef.Get(Campaign::kSkillNameBase + s);
    }
    if (shown == 0) line += ctx.StringsRef.Get(kStrNone);
    box.Text(line);
    box.Add(TextBox::kTextLow, "");
}

// --- the pages -------------------------------------------------------------

void BuildBattleTally(GameContext& ctx, TextBox& box, const BattleField& f) {
    box.Title(ctx.StringsRef.Get(kStrBattleInfo));
    box.Text(Row(ctx, kStrRoundsPlayed, f.Round()));
    box.Text(ctx.StringsRef.Get(kStrTimeElapsed) + Sep(ctx) +
             Clock(f.ElapsedSeconds()));
}

void BuildSeatTally(GameContext& ctx, TextBox& box, const BattleField& f,
                    int seat) {
    const BattleField::Player& pl = f.Players()[std::size_t(seat)];
    box.Title(ctx.StringsRef.Get(kStrBattleInfo));
    box.Text(SeatName(ctx, f, seat));
    // The treasury, with what the buildings will pay next turn in brackets.
    box.AddIconLine(kMoneyIcon, 0,
                    std::to_string(pl.Cash) + " (" +
                        std::to_string(f.Income(seat)) + ")",
                    kMoneyTextX);
    box.Text(Row(ctx, kStrUnits, f.CountUnits(seat)));
    box.Text(Row(ctx, kStrProperties, PropertyCount(f, seat)));
    box.Text(Row(ctx, kStrUnitsBuilt, pl.Stats.UnitsBuilt));
    box.Text(Row(ctx, kStrUnitsDestroyed, pl.Stats.UnitsDestroyed));
    box.Text(Row(ctx, kStrUnitsLost, pl.Stats.UnitsLost));
    box.Text(Row(ctx, kStrPropsCaptured, pl.Stats.PropertiesCaptured));
    box.Text(Row(ctx, kStrPropsLost, pl.Stats.PropertiesLost));
    box.Text(Row(ctx, kStrGoldCollected, pl.Stats.GoldCollected));
}

void BuildCommanderPage(GameContext& ctx, TextBox& box, const BattleField& f,
                        int seat) {
    const int type = f.Character(seat);
    const std::vector<int> seats = Seats(f);
    int position = 1;
    for (std::size_t i = 0; i < seats.size(); ++i)
        if (seats[i] == seat) position = int(i) + 1;

    box.Title(ctx.StringsRef.Get(kStrCommanderInfo));
    // The name, with the seat's flag beside it and its place in the book after
    // it: "Black Barlow (2/3)".
    box.AddIconLine(kFlagIcon, f.Colour(seat) - 1,
                    SeatName(ctx, f, seat) + " (" + std::to_string(position) +
                        "/" + std::to_string(int(seats.size())) + ")",
                    kNameTextX);
    box.Add(TextBox::kTextLow, "");

    // The portrait and the paragraph under it, which are one string.
    const int bio = commanders::BioString(type);
    if (bio != 0 && !ctx.StringsRef.Get(bio).empty()) {
        box.Text(ctx.StringsRef.Get(bio));
        box.Add(TextBox::kTextLow, "");
    }
    // Then either what this commander's skills do to their army, or -- for the
    // seat the player holds, whose slot is the `[player]` placeholder -- the
    // skill chart itself.
    const bool team = f.Team(seat) < BattleField::kLoneTeam;
    const int bonus = commanders::BonusString(type, team);
    if (bonus != 0 && !ctx.StringsRef.Get(bonus).empty()) {
        box.Text(ctx.StringsRef.Get(bonus));
        box.Add(TextBox::kTextLow, "");
    } else {
        AddSkills(ctx, box);
    }
    AddPerks(ctx, box, f, seat);
}

// Run a book of pages. `build` fills one; the soft keys walk it.
template <typename Build>
bool RunBook(GameContext& ctx, int count, int start, Build build) {
    if (count <= 0) return true;
    int page = start < 0 ? 0 : (start >= count ? count - 1 : start);
    for (;;) {
        TextBox box(ctx, PageMode(page, count));
        build(box, page);
        const int r = box.Run();
        if (r == TextBox::kQuit) return false;
        if (ctx.Sound)
            ctx.Sound->PlayMenu(r == TextBox::kConfirmed
                                    ? SoundManager::kSoundEnter
                                    : SoundManager::kSoundCancel);
        if (r == TextBox::kConfirmed) {
            // The left key is Next until the last page, where it says Exit.
            if (page + 1 >= count) return true;
            ++page;
        } else {
            // And the right key is Exit on the first page and Back after it.
            if (page == 0) return true;
            --page;
        }
    }
}

}  // namespace

bool ShowMissionObjectives(GameContext& ctx, const BattleSession& session) {
    TextBox box(ctx, TextBox::kOkOk);
    box.Title(ctx.StringsRef.Get(kStrMissionObjectives));
    const int n = session.MissionIndex();
    // A battle that is not a table entry -- a skirmish, or one started from
    // the --battle flag -- has no per-mission strings. It still gets the
    // board, with the level's own title on it and nothing under it, rather
    // than a row that cannot be opened.
    const std::string name = n > 0 ? ctx.StringsRef.Get(kMissionNameBase + n)
                                   : std::string();
    box.Text(name.empty() ? session.Name() : name);
    box.Add(TextBox::kTextLow, "");
    if (n > 0) box.Text(ctx.StringsRef.Get(kMissionObjectivesBase + n));
    return box.Run() != TextBox::kQuit;
}

bool ShowBattleInfo(GameContext& ctx, const BattleSession& session) {
    const BattleField& f = session.Field();
    const std::vector<int> seats = Seats(f);
    // The battle's own page, then one per seat.
    return RunBook(ctx, int(seats.size()) + 1, 0,
                   [&](TextBox& box, int page) {
                       if (page == 0) {
                           BuildBattleTally(ctx, box, f);
                       } else {
                           BuildSeatTally(ctx, box, f,
                                          seats[std::size_t(page - 1)]);
                       }
                   });
}

void BuildCommanderBoard(GameContext& ctx, TextBox& box,
                         const BattleSession& session, int seat) {
    BuildCommanderPage(ctx, box, session.Field(), seat);
}

bool ShowCommanderInfo(GameContext& ctx, const BattleSession& session,
                       int viewer) {
    const BattleField& f = session.Field();
    const std::vector<int> seats = Seats(f);
    int start = 0;
    for (std::size_t i = 0; i < seats.size(); ++i)
        if (seats[i] == viewer) start = int(i);
    return RunBook(ctx, int(seats.size()), start,
                   [&](TextBox& box, int page) {
                       BuildCommanderPage(ctx, box, f,
                                          seats[std::size_t(page)]);
                   });
}

bool ShowBattleMap(GameContext& ctx, BattleSession& session,
                   const BattleRenderer::View& view) {
    Host& host = ctx.HostRef;
    host.FlushKeys();
    // Mode 0's two soft keys, which is all the empty TextBox the engine puts
    // over this screen contributes: the board itself is never drawn, or the
    // overview it is there to caption would be under it.
    const std::string ok = ctx.StringsRef.Get(2116);
    BattleRenderer::View v = view;
    for (;;) {
        if (host.QuitRequested()) return false;
        Surface& screen = host.Screen();
        screen.Fill(0xF000);
        v.Ticks = host.TickCount();
        session.Renderer().Draw(screen, session.Field(), v);
        session.Renderer().DrawMinimap(screen, session.Field(), v.Viewer);
        chrome::DrawSoftKeys(ctx, screen, ok, ok);
        host.Flip();
        if (ctx.Sound) ctx.Sound->Pump(host);
        host.Sleep(BattleScreen::kFrameMs);
        // Any soft key closes it (0x100a78d0 pops on either, and 0x100a7868
        // on the select and back keys the pad maps to them).
        if (host.KeyPressed(Key::kSoftLeft) || host.KeyPressed(Key::kSoftRight) ||
            host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kBack)) {
            if (ctx.Sound) ctx.Sound->PlayMenu(SoundManager::kSoundCancel);
            host.FlushKeys();
            return true;
        }
    }
}

}  // namespace bb
