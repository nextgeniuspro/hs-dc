#include "game/BattleScreen.h"

#include <algorithm>

#include "game/BattleAi.h"
#include "game/BattleInfo.h"
#include "game/CardPicker.h"
#include "game/FilePack.hpp"
#include "game/Font.h"
#include "game/MenuChrome.h"
#include "game/MenuScreen.h"
#include "game/MenuScreens.h"
#include "game/Game.h"
#include "game/MissionDatabase.h"
#include "game/Palette.h"
#include "game/Settings.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/TextBox.h"
#include "game/TextureCache.h"
#include "platform/Host.h"
#include "platform/Storage.h"
#include "shim/Log.h"

namespace bb {
namespace {

// Actions, in the order the popup lists them (0x100a4a34's tail). The engine's
// own ids are in the comments; ours only have to be distinct.
enum Action {
    kActAttack,        //  7
    kActCapture,       //  e
    kActLoad,          //  9
    kActJoin,          //  8
    kActSupply,        //  f
    kActWait,          // 10
    kActCancelOrder,   // 12 -- the row appended to *every* action popup
    // The battle menu's own actions carry the engine's ids from 0x1004eec8:
    // Perks 1, End turn 2, Save 3, Options 4, End current game 5, Pause 6.
    kActPerks,
    kActEndTurn,
    kActSave,
    kActOptions,
    kActQuit,          // "End current game"
    kActSettings,      // the row the original labels "Pause" or "Game settings"
    // And the Options submenu's own, in the order 0x1005ac84 adds them.
    kActObjectives,
    kActCommanderInfo,
    kActBattleInfo,
    kActSurrender,
    kActMap,
};

// The perk rows carry their perk id, offset so it cannot collide with an
// action. The engine uses `perk + 1` for the same reason (0x100d1cf0).
constexpr int kActPerkBase = 100;
// "None", the row a commander with no perks gets (0x100d1de0).
constexpr int kStrNone = 1573;
// "Perks", the board's own title (0x100d1ddc).
constexpr int kStrPerksTitle = 1655;

// One row per occupied cargo slot (engine ids 10..13), so the id carries the
// slot the way the original's does. Building has no row here at all: it is a
// screen of its own (see BuildMenu).
constexpr int kUnloadActionBase = 50;

// Which of `shell_board.tc`'s three grains the surrender board's first answer
// wears: its own widget virtual returns 32 (0x1006160c).
constexpr int kSurrenderGrain = 32;


}  // namespace

bool BattleSession::Load(GameContext& ctx, const std::string& levelPath) {
    m_Path = levelPath;
    if (m_Name.empty()) m_Name = PrettyMissionName(levelPath);
    const bool data = m_Data.Load(ctx.Pack);
    const bool art = m_Renderer.Load(ctx.Pack, ctx.Textures, ctx.PaletteRef);
    const bool level = data && m_Field.Load(ctx.Pack, m_Data, levelPath);
    // Who sits where is the mission table's business, not the level's: the
    // level chunk's own human/computer bytes are editor leftovers the engine
    // never reads (see BattleField::Build).
    if (level) {
        m_Field.Seat(m_HumanSeats, m_Teams);
        m_Renderer.SetField(m_Field);
    }
    m_Ready = data && art && level;
    return m_Ready;
}

// --- helpers ---------------------------------------------------------------

std::string BattleScreen::Text(int id) const {
    const std::string& s = m_Ctx.StringsRef.Get(id);
    return s.empty() ? std::string() : s;
}

// Put the info panels away and restart the clock that brings them back.
void BattleScreen::DuckPanels() {
    m_CursorStillSince = m_Ctx.HostRef.TickCount();
    m_Panels.ResetSlide();
}

void BattleScreen::CentreOn(int x, int y) {
    m_View.CamX = x * BattleRenderer::kTile + BattleRenderer::kTile / 2 -
                  Surface::kWidth / 2;
    m_View.CamY = y * BattleRenderer::kTile + BattleRenderer::kTile / 2 -
                  Surface::kHeight / 2;
    BattleRenderer::ClampCamera(m_Session->Field(), Surface::kWidth,
                                Surface::kHeight, m_View.CamX, m_View.CamY);
}

// Basker Confusion, from the other side of it: while an enemy is running the
// perk the d-pad lies. Regular turns every direction round; Master sends the
// cursor somewhere else entirely, which is the difference between "inverted"
// and "randomised".
void BattleScreen::Steer(int dx, int dy) {
    const BattleField& f = m_Session->Field();
    if (f.EnemyPerkActive(kPerkBaskerConfusion, m_Human)) {
        if (f.EnemyPerkActive(kPerkBaskerConfusion, m_Human, true)) {
            // A small linear congruential step, so a press is unpredictable
            // but the game stays deterministic for a test to drive.
            m_Confusion = m_Confusion * 1103515245u + 12345u;
            const int quarters = int((m_Confusion >> 16) & 3u);
            for (int i = 0; i < quarters; ++i) {
                const int t = dx;
                dx = -dy;
                dy = t;
            }
        } else {
            dx = -dx;
            dy = -dy;
        }
    }
    MoveCursor(dx, dy);
}

void BattleScreen::MoveCursor(int dx, int dy) {
    BattleField& f = m_Session->Field();
    const int nx = m_View.CursorX + dx, ny = m_View.CursorY + dy;
    if (!f.InBounds(nx, ny)) return;
    m_View.CursorX = nx;
    m_View.CursorY = ny;
    // Walking the cursor ducks the info panels; they come back when it has
    // been still for a moment. The engine restamps the same timestamp the key
    // repeat runs off (0x100cdcc0's tail), so the two can never disagree.
    DuckPanels();

    // And it ticks. There are two cursor samples and two states that play
    // them: the free cursor (0x100cdcc0) plays `cursor_move` on every step it
    // manages, and the state that walks a unit's order about (0x10097cf8)
    // plays `cursor_move_unit` instead. The port has one cursor and the mode
    // tells the two apart.
    if (m_Ctx.Sound) {
        const bool ordering = m_Mode == Mode::kMove || m_Mode == Mode::kTarget ||
                              m_Mode == Mode::kUnloadWhere;
        m_Ctx.Sound->PlayBattle(ordering ? SoundManager::kSoundCursorUnit
                                        : SoundManager::kSoundCursor);
    }

    // Keep the cursor a tile inside the view before scrolling, which is what
    // the original's edge arrows are marking.
    const int margin = BattleRenderer::kTile;
    const int sx = BattleRenderer::ScreenX(nx, m_View);
    const int sy = BattleRenderer::ScreenY(ny, m_View);
    if (sx < margin) m_View.CamX -= margin - sx;
    if (sx + BattleRenderer::kTile > Surface::kWidth - margin)
        m_View.CamX += sx + BattleRenderer::kTile - (Surface::kWidth - margin);
    if (sy < margin) m_View.CamY -= margin - sy;
    const int viewH = Surface::kHeight;
    if (sy + BattleRenderer::kTile > viewH - margin)
        m_View.CamY += sy + BattleRenderer::kTile - (viewH - margin);
    BattleRenderer::ClampCamera(f, Surface::kWidth, viewH, m_View.CamX,
                                m_View.CamY);

    if (m_Mode == Mode::kMove && m_Selected >= 0) {
        m_Session->Field().PathTo(m_Selected, m_View.CursorX, m_View.CursorY,
                                 m_View.Path);
        if (m_Reach[std::size_t(ny) * f.Width() + nx] < 0) m_View.Path.clear();
    }
}

// --- menus -----------------------------------------------------------------

bool BattleScreen::MenuOpen() const { return m_MenuOpen; }

// The ring of squares the unit could shoot into from where it stands. The
// engine paints this red while the order is being given, so you can see which
// way a unit reaches before committing to Attack.
void BattleScreen::BuildAttackRange(int unitIndex, std::vector<uint8_t>& out) {
    BattleField& f = m_Session->Field();
    out.assign(f.Cells().size(), 0);
    const BattleField::Unit* u = f.UnitByIndex(unitIndex);
    if (!u) return;
    const UnitAttrs& a = f.Data().Unit(u->Type);
    for (int y = 0; y < f.Height(); ++y) {
        for (int x = 0; x < f.Width(); ++x) {
            const int d = std::abs(x - u->X) + std::abs(y - u->Y);
            if (d >= a.MinRange && d <= a.MaxRange)
                out[std::size_t(y) * f.Width() + x] = 1;
        }
    }
}

void BattleScreen::OpenActionMenu() {
    BattleField& f = m_Session->Field();
    m_View.PlaceRange = nullptr;
    m_Menu = MenuList{};
    m_Menu.Load(m_Ctx.Textures, m_Ctx.SmallFont, 0);
    m_MenuOpen = true;

    std::vector<Target> targets;
    BuildTargetList(targets);
    m_View.AttackRange = nullptr;
    if (!targets.empty()) {
        m_Menu.Add(Text(BattleData::kStrAttack), kActAttack);
        BuildAttackRange(m_Selected, m_Targets);
        m_View.AttackRange = &m_Targets;
    }
    if (f.CanCapture(m_Selected)) m_Menu.Add(Text(BattleData::kStrCapture), kActCapture);

    // Load and Join: a friendly unit standing where we are.
    const BattleField::Unit* u = f.UnitByIndex(m_Selected);
    bool canLoad = false, canJoin = false;
    for (int i = 0; i < int(f.Units().size()); ++i) {
        const BattleField::Unit& t = f.Units()[std::size_t(i)];
        if (!t.Alive || i == m_Selected) continue;
        if (t.X != u->X || t.Y != u->Y) continue;
        if (f.CanLoad(m_Selected, i)) canLoad = true;
        if (f.CanJoin(m_Selected, i)) canJoin = true;
    }
    if (canLoad) m_Menu.Add(Text(BattleData::kStrLoad), kActLoad);
    if (canJoin) m_Menu.Add(Text(BattleData::kStrJoin), kActJoin);
    // Supply and Wait are both suppressed once Load or Join is on offer: both of
    // those consume the unit, so there is nothing left to wait for (`bVar4` and
    // `bVar1` in 0x100a4a34 are each `!load && !join`).
    if (f.CanSupply(m_Selected) && !canLoad && !canJoin)
        m_Menu.Add(Text(BattleData::kStrSupply), kActSupply);
    if (!canLoad && !canJoin) m_Menu.Add(Text(BattleData::kStrWait), kActWait);

    // One row per cargo slot that has somewhere to be put down, labelled
    // "Unload <unit>" -- the original concatenates string 1708, a space and the
    // passenger's name.
    for (int slot = 0; slot < int(u->Cargo.size()); ++slot) {
        if (!UnloadPossible(m_Selected, slot)) continue;
        const BattleField::Unit& p = f.Units()[std::size_t(u->Cargo[std::size_t(slot)])];
        m_Menu.Add(Text(BattleData::kStrUnload) + " " +
                      Text(BattleData::UnitStringId(p.Type)),
                  kUnloadActionBase + slot);
    }

    // Always last, on every action popup.
    m_Menu.Add(Text(BattleData::kStrCancelOrder), kActCancelOrder);
    m_Mode = Mode::kAction;
}

// The four squares 0x100a4a34 tries, in its order: left, right, up, down
// (dx {-1,1,0,0}, dy {0,0,-1,1}).
static const int kUnloadDx[4] = {-1, 1, 0, 0};
static const int kUnloadDy[4] = {0, 0, -1, 1};

// Is there anywhere next to the transport this passenger could be put down?
bool BattleScreen::UnloadPossible(int transport, int slot) const {
    const BattleField& f = m_Session->Field();
    const BattleField::Unit* t = f.UnitByIndex(transport);
    if (!t || slot < 0 || slot >= int(t->Cargo.size())) return false;
    for (int d = 0; d < 4; ++d)
        if (f.CanUnload(transport, slot, t->X + kUnloadDx[d], t->Y + kUnloadDy[d]))
            return true;
    return false;
}

// Picking "Unload <unit>" does not hand back a free cursor: the engine has
// already worked out which squares will take the passenger (it builds a 5x5
// grid per cargo slot in 0x100a4a34 and marks the legal cells), paints them
// with the green overlay, and drops the cursor on one of them. All the player
// has to do is choose between them.
void BattleScreen::BeginUnloadPlacement() {
    BattleField& f = m_Session->Field();
    m_Place.assign(f.Cells().size(), 0);
    const BattleField::Unit* t = f.UnitByIndex(m_Selected);
    int firstX = -1, firstY = -1;
    if (t) {
        for (int d = 0; d < 4; ++d) {
            const int x = t->X + kUnloadDx[d], y = t->Y + kUnloadDy[d];
            if (!f.InBounds(x, y)) continue;
            if (!f.CanUnload(m_Selected, m_UnloadSlot, x, y)) continue;
            m_Place[std::size_t(y) * f.Width() + x] = 1;
            if (firstX < 0) {
                firstX = x;
                firstY = y;
            }
        }
    }
    if (firstX < 0) {
        // Nothing to choose from; the row should not have been offered.
        OpenActionMenu();
        return;
    }
    m_View.MoveRange = nullptr;
    m_View.AttackRange = nullptr;
    m_View.Path.clear();
    m_View.PlaceRange = &m_Place;
    m_View.CursorX = firstX;
    m_View.CursorY = firstY;
    CentreOn(firstX, firstY);
    DuckPanels();
    m_Mode = Mode::kUnloadWhere;
}

bool BattleScreen::AttackTargetNow() {
    if (m_Mode != Mode::kTarget || m_TargetList.empty()) return false;
    const int i = std::clamp(m_TargetIndex, 0, int(m_TargetList.size()) - 1);
    CommitAttack(m_TargetList[std::size_t(i)]);
    return true;
}

const MenuList& BattleScreen::OrderUnitTo(int unit, int x, int y) {
    BattleField& f = m_Session->Field();
    const BattleField::Unit* u = f.UnitByIndex(unit);
    m_Selected = unit;
    m_FromX = u ? u->X : 0;
    m_FromY = u ? u->Y : 0;
    m_FromMovement = u ? u->Movement : 0;
    m_View.SelectedUnit = unit;
    f.Reachable(unit, m_Reach);
    m_View.MoveRange = &m_Reach;
    m_Mode = Mode::kMove;
    // The player points the cursor at the destination before confirming, and
    // the route follows the cursor; do the same so the state after the move
    // matches what the screen would really be holding.
    m_View.CursorX = x;
    m_View.CursorY = y;
    f.PathTo(unit, x, y, m_View.Path);
    if (u && (x != u->X || y != u->Y)) f.MoveUnit(unit, x, y);
    m_View.MoveRange = nullptr;
    m_View.Path.clear();
    OpenActionMenu();
    return m_Menu;
}

// The battle menu, row for row as 0x1004eec8 builds it. Perks, Save and Pause
// name screens that are not ported (the perks board, the save flow, the pause
// state 0x100ce0c8 pushes), so they sit disabled rather than doing nothing.
void BattleScreen::OpenBattleMenu() {
    m_Menu = MenuList{};
    m_Menu.Load(m_Ctx.Textures, m_Ctx.SmallFont, 0);
    m_MenuOpen = true;
    // Live exactly when one of the commander's perks could go off.
    m_Menu.Add(Text(BattleData::kStrPerks), kActPerks,
              m_Session->Field().AnyPerkUsable(m_Human));
    // Saving is offered exactly when there is somewhere to save to. A host
    // with no storage -- the headless test host, a kiosk build -- gets the
    // row greyed out rather than a button that fails when pressed. On a
    // console a card in the machine counts as somewhere: pressing the row is
    // what puts the card picker up (CardPicker.h).
    m_Menu.Add(Text(BattleData::kStrSave), kActSave, CanSave(m_Ctx));
    m_Menu.Add(Text(BattleData::kStrOptions), kActOptions);
    // The row the original calls "Pause" on a single machine and "Game
    // settings" on a networked one (0x1004eec8 picks the label off the game
    // type). Pausing a game that nobody else is waiting on does nothing worth
    // having, so the port keeps the other half: the row opens the settings
    // screens, and wears the string the original wrote for exactly that.
    m_Menu.Add(Text(BattleData::kStrGameSettings), kActSettings);
    m_Menu.Add(Text(BattleData::kStrEndCurrentGame), kActQuit);
    m_Menu.Add(Text(BattleData::kStrEndTurn), kActEndTurn);
    m_Mode = Mode::kMenu;
}

// The perks board (0x100d1cf0, the state 0x100d1cb8 builds and the battle
// menu's first row pushes). One row per perk the commander carries, in perk
// order, and a row is live only while the bar can pay for it -- which is the
// Regular price, sixty per cent, because that is the cheaper of the two.
void BattleScreen::OpenPerkMenu() {
    const BattleField& f = m_Session->Field();
    m_Menu = MenuList{};
    m_Menu.Load(m_Ctx.Textures, m_Ctx.SmallFont, 0);
    m_MenuOpen = true;
    int shown = 0;
    for (int p = 0; p < kPerkCount; ++p) {
        if (!f.HasPerk(m_Human, p)) continue;
        ++shown;
        m_Menu.Add(Text(Campaign::kPerkNameBase + p), kActPerkBase + p,
                  f.PerkUsable(m_Human, p, false));
    }
    // A commander with nothing to spend gets the same greyed-out "None" the
    // original shows rather than an empty board.
    if (shown == 0) m_Menu.Add(Text(kStrNone), 0, false);
    m_MenuTitleFrame = chrome::PickTitleFrame(m_Ctx);
    m_Mode = Mode::kPerks;
}

// 0x10047b70: the level is not a choice -- a full bar buys the Master version
// -- and the whole bar goes either way. Then the animation, over every unit
// the perk reached.
void BattleScreen::UsePerk(int perk) {
    BattleField& f = m_Session->Field();
    bool master = false;
    std::vector<int> touched;
    if (!f.UsePerk(m_Human, perk, &master, &touched)) return;

    if (m_Ctx.Sound) m_Ctx.Sound->PlayPerk(perk);

    const PerkDef& def = PerkInfo(perk);
    const int lv = master ? 1 : 0;
    // Four perks have a sheet of their own for their Master version; the rest
    // play the Regular one at either level (0x100a0044 asks whether there is a
    // second sheet before it reaches for one).
    const int sheet = def.Sheet[lv] >= 0 ? def.Sheet[lv] : def.Sheet[0];
    const int blend = def.Sheet[lv] >= 0 ? def.Blend[lv] : def.Blend[0];

    std::vector<PerkFlash::Cell> cells;
    for (int i : touched) {
        const BattleField::Unit* u = f.UnitByIndex(i);
        if (!u || u->Carrier >= 0) continue;
        cells.push_back({u->X, u->Y});
    }
    if (!cells.empty()) {
        Host& host = m_Ctx.HostRef;
        // The sheet is asked for, but the effect runs either way: PerkFlash
        // washes the blocks with a plain tint when it has no artwork, so a
        // machine that could not spare 350 KB for the frames still shows which
        // units the perk reached.
        const bool art = sheet >= 0 && m_PerkFlash.Load(m_Ctx.Textures, sheet);
        LogDebug("perk: %s at %s, %zu unit%s, sheet %d %s, blend %d\n",
                 Text(Campaign::kPerkNameBase + perk).c_str(),
                 master ? "master" : "regular", cells.size(),
                 cells.size() == 1 ? "" : "s", sheet,
                 art ? "playing" : "unavailable", blend);
        // Nothing on the map moves while the effect plays, so the map is drawn
        // *once* and copied back under each frame of it. Rebuilding the whole
        // battlefield thirty times over -- terrain, buildings, every unit --
        // to put a sprite on top of it is most of the cost of the animation on
        // a machine that rasterises in software, and none of it is work the
        // picture needs.
        Surface backdrop;
        backdrop.Fill(0xF000);
        m_Session->Renderer().Draw(backdrop, m_Session->Field(), m_View);
        m_PerkFlash.Begin(std::move(cells), blend);
        while (m_PerkFlash.Active() && !host.QuitRequested()) {
            Surface& screen = host.Screen();
            screen.Copy(backdrop.Pixels(), backdrop.Width(), backdrop.Height(),
                        0, 0);
            m_PerkFlash.Step(screen, m_View.CamX, m_View.CamY);
            host.Flip();
            if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
            host.Sleep(kFrameMs);
        }
        // And give the frames back. Ten sheets are three and a half megabytes
        // between them; a battle that saw several perks used to hold every one
        // it had played until it ended.
        m_PerkFlash.Unload();
    }

    // The script gets its turn, and whatever the perk killed is buried.
    FireDestroyTriggers();
    TriggerRunner::Context tc;
    tc.Player = m_Human;
    FireTrigger(TriggerRunner::Event::kPerkUse, tc);
    PumpDialogue();
}

// The Options submenu (0x1005ac84): this is where Surrender lives -- the
// tutorial even says so (string 51027). Every row it adds is here except
// "Break up team", which the original only adds when the game type is one of
// the three team types -- a Bluetooth game between several machines, whose row
// sends the other seats a message (0x1005ade4 case 5) rather than opening
// anything. There is no such game type here, so there is no such row.
//
// Surrender is the one row 0x1005ac84 conditions, on `the seat playing is this
// LocalPlayer's own`. The original has one LocalPlayer per human seat and only
// the one whose turn it is has a menu open, so that test is always true where
// it is asked; here there is one screen and the same is true of it, so the row
// is simply live.
void BattleScreen::OpenOptionsMenu() {
    m_Menu = MenuList{};
    m_Menu.Load(m_Ctx.Textures, m_Ctx.SmallFont, 0);
    m_MenuOpen = true;
    m_Menu.Add(Text(BattleData::kStrMissionObjectives), kActObjectives);
    m_Menu.Add(Text(BattleData::kStrCommanderInfo), kActCommanderInfo);
    m_Menu.Add(Text(BattleData::kStrBattleInfo), kActBattleInfo);
    m_Menu.Add(Text(BattleData::kStrSurrender), kActSurrender);
    m_Menu.Add(Text(BattleData::kStrMap), kActMap);
    m_Mode = Mode::kOptions;
}

// Building a unit is a screen of its own, not a row of wooden boards: see
// BuildMenu. It is modal, exactly as the engine's is -- a LocalPlayer state
// pushed over the map -- so this runs it to its answer and acts on it here.
void BattleScreen::OpenProduceMenu(int propertyIndex) {
    BattleField& f = m_Session->Field();
    m_ProduceProperty = propertyIndex;
    std::vector<int> types;
    f.Producible(propertyIndex, types);
    if (types.empty()) return;
    const int type = m_Build.Run(m_Ctx, f, m_Session->Renderer(), m_Panels, m_View,
                                propertyIndex);
    if (type >= 0) {
        const int made = f.Produce(propertyIndex, type);
        if (made >= 0 && m_Ctx.Sound)
            m_Ctx.Sound->PlayMenu(SoundManager::kSoundEnter);
        FireDestroyTriggers();
    }
    DuckPanels();
    m_Ctx.HostRef.FlushKeys();
    m_Mode = Mode::kBrowse;
}

// --- actions ---------------------------------------------------------------

// Putting the unit back where it started. Going through MoveUnit is wrong: it
// charges the return trip against what is left of the budget, and entry costs
// are not symmetric, so a unit that walked off a road onto a mountain could not
// always walk back.
void BattleScreen::RewindMove() {
    BattleField& f = m_Session->Field();
    if (m_Selected >= 0)
        f.ReturnUnit(m_Selected, m_FromX, m_FromY, m_FromMovement);
    f.RecomputeVision();
    m_View.AttackRange = nullptr;
}

// Cancel from the action popup -- the row itself or the back key, which the
// engine funnels into one branch (`case -1: case 0x12:` in 0x100a5614). It
// undoes the move and then calls LocalPlayer::statePop, so the state that
// pushed the popup becomes current again: the unit is still picked up, its
// movement range is still drawn, and the cursor has not moved, so the route to
// it comes straight back. You can confirm the same square again or walk the
// cursor somewhere else. Only a second cancel, from there, lets the unit go.
void BattleScreen::CancelOrder() {
    if (m_Selected < 0) {
        Deselect();
        return;
    }
    RewindMove();
    BattleField& f = m_Session->Field();
    f.Reachable(m_Selected, m_Reach);
    m_View.MoveRange = &m_Reach;
    m_View.SelectedUnit = m_Selected;
    m_Mode = Mode::kMove;
    // The route was thrown away when the move was committed; the cursor is
    // still on the destination, so ask for it again.
    f.PathTo(m_Selected, m_View.CursorX, m_View.CursorY, m_View.Path);
    const std::size_t i = std::size_t(m_View.CursorY) * f.Width() + m_View.CursorX;
    if (i >= m_Reach.size() || m_Reach[i] < 0) m_View.Path.clear();
}

// The keypad's 3 and 1, which are what a turn with a dozen men on the board is
// actually played with: they walk the cursor round the seat's own units,
// skipping whatever has already had its go. 0x1009b028 asks the battle for the
// unit after (or before) the current one and keeps asking until it finds one
// that is alive and not finished, giving up when it comes back to where it
// started -- so the last unit with something left to do simply keeps the
// cursor rather than the key doing nothing.
void BattleScreen::CycleUnit(int step) {
    BattleField& f = m_Session->Field();
    const int seat = f.CurrentPlayer();
    // In the order they appear in the level, which is the order the engine's
    // per-seat list keeps them in.
    std::vector<int> mine;
    for (int i = 0; i < int(f.Units().size()); ++i) {
        const BattleField::Unit& u = f.Units()[std::size_t(i)];
        // A passenger is not on the board to be walked to; its transport is.
        if (u.Alive && u.Owner == seat && !u.Done && u.Carrier < 0)
            mine.push_back(i);
    }
    if (mine.empty()) return;

    // Where in that list the cursor is standing now. On a square that is not
    // one of them -- open ground, an enemy -- the walk starts from the
    // beginning or the end, so that one press lands on a unit either way.
    const int here = f.At(m_View.CursorX, m_View.CursorY).Unit;
    int at = -1;
    for (int i = 0; i < int(mine.size()); ++i)
        if (mine[std::size_t(i)] == here) at = i;
    const int n = int(mine.size());
    const int next = at < 0 ? (step > 0 ? 0 : n - 1) : (at + step + n) % n;

    const BattleField::Unit& u = f.Units()[std::size_t(mine[std::size_t(next)])];
    m_View.CursorX = u.X;
    m_View.CursorY = u.Y;
    CentreOn(u.X, u.Y);
    DuckPanels();
    if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(SoundManager::kSoundMove);
}

// The keypad's 4, held. What is shown depends on what the unit could do: a
// cannon tower cannot move at all, so 0x1009ad88 reaches for its firing arc
// instead of a movement range that would only ever be its own square.
void BattleScreen::UpdateRangePreview() {
    BattleField& f = m_Session->Field();
    const bool want = m_Mode == Mode::kBrowse && !m_MenuOpen &&
                      !m_Dialogue.Active() && m_Ctx.HostRef.KeyHeld(Key::kRange);
    int unit = -1;
    if (want && f.Visible(m_View.Viewer, m_View.CursorX, m_View.CursorY))
        unit = f.At(m_View.CursorX, m_View.CursorY).Unit;
    if (unit >= 0) {
        const BattleField::Unit* u = f.UnitByIndex(unit);
        if (!u || !u->Alive || u->Carrier >= 0) unit = -1;
    }
    if (unit == m_PreviewUnit) return;
    m_PreviewUnit = unit;
    // Its own overlay slot, and a red one: 0x1009ad88 hands the region to the
    // renderer as kind 2 whichever branch it took, so the preview never wears
    // the yellow an order in progress does.
    m_View.PreviewReach = nullptr;
    m_View.PreviewTargets = nullptr;
    if (unit < 0) return;
    const BattleField::Unit& u = *f.UnitByIndex(unit);
    if (f.Data().Unit(u.Type).UnitClass == kClassStationary) {
        BuildAttackRange(unit, m_PreviewTargets);
        m_View.PreviewTargets = &m_PreviewTargets;
    } else {
        f.Reachable(unit, m_PreviewReach);
        m_View.PreviewReach = &m_PreviewReach;
    }
}

// Letting the unit go: back to a free cursor with nothing selected.
void BattleScreen::Deselect() {
    RewindMove();
    m_View.Path.clear();
    m_View.MoveRange = nullptr;
    m_View.PlaceRange = nullptr;
    m_View.SelectedUnit = -1;
    m_Selected = -1;
    m_Mode = Mode::kBrowse;
}

void BattleScreen::BuildTargetList(std::vector<Target>& out) const {
    out.clear();
    if (m_Selected < 0) return;
    BattleField& f = m_Session->Field();
    std::vector<int> units;
    f.AttackTargets(m_Selected, units);
    for (const int i : units) {
        const BattleField::Unit& e = f.Units()[std::size_t(i)];
        out.push_back(Target{i, e.X, e.Y});
    }
    std::vector<int> walls;
    f.ObstacleTargets(m_Selected, walls);
    for (const int cell : walls)
        out.push_back(Target{-1, cell % f.Width(), cell / f.Width()});
}

void BattleScreen::CommitAttack(const Target& target) {
    BattleField& f = m_Session->Field();
    // Announce the attack before it lands: the campaign's second trigger is
    // "the first time player one attacks player two". A wall has no owner, so
    // there is nobody to name as the other side.
    {
        const BattleField::Unit* a = f.UnitByIndex(m_Selected);
        const BattleField::Unit* d =
            target.Unit >= 0 ? f.UnitByIndex(target.Unit) : nullptr;
        TriggerRunner::Context tc;
        tc.Unit = m_Selected;
        tc.Target = target.Unit;
        tc.Player = a ? a->Owner : 0;
        tc.Other = d ? d->Owner : 0;
        tc.X = target.X;
        tc.Y = target.Y;
        FireTrigger(TriggerRunner::Event::kAttack, tc);
    }
    bool landed = false;
    if (target.Unit >= 0) {
        landed = f.Attack(m_Selected, target.Unit).Valid;
    } else {
        landed = f.AttackObstacle(m_Selected, target.X, target.Y).Valid;
    }
    if (landed && m_Ctx.Sound)
        m_Ctx.Sound->PlayMenu(SoundManager::kSoundEnter);
    FireDestroyTriggers();
    m_View.MoveRange = nullptr;
    m_View.AttackRange = nullptr;
    m_View.Path.clear();
    m_Selected = -1;
    m_Mode = Mode::kBrowse;
}

void BattleScreen::PerformAction(int action) {
    BattleField& f = m_Session->Field();
    // The perks board's rows, which sit above the unload ones.
    if (action >= kActPerkBase && action < kActPerkBase + kPerkCount) {
        UsePerk(action - kActPerkBase);
        m_Mode = Mode::kBrowse;
        return;
    }
    if (action >= kUnloadActionBase) {
        m_UnloadSlot = action - kUnloadActionBase;
        BeginUnloadPlacement();
        return;
    }

    switch (action) {
        case kActAttack: {
            BuildTargetList(m_TargetList);
            if (m_TargetList.empty()) {
                m_Mode = Mode::kAction;
                return;
            }
            m_TargetIndex = 0;
            BuildAttackRange(m_Selected, m_Targets);
            m_View.AttackRange = &m_Targets;
            m_View.MoveRange = nullptr;
            m_View.CursorX = m_TargetList[0].X;
            m_View.CursorY = m_TargetList[0].Y;
            m_Mode = Mode::kTarget;
            return;
        }
        case kActCapture:
            f.Capture(m_Selected);
            break;
        case kActSupply:
            f.Supply(m_Selected);
            break;
        case kActLoad: {
            const BattleField::Unit* u = f.UnitByIndex(m_Selected);
            for (int i = 0; i < int(f.Units().size()); ++i) {
                const BattleField::Unit& t = f.Units()[std::size_t(i)];
                if (i == m_Selected || !t.Alive) continue;
                if (t.X == u->X && t.Y == u->Y && f.CanLoad(m_Selected, i)) {
                    f.LoadUnit(m_Selected, i);
                    break;
                }
            }
            break;
        }
        case kActJoin: {
            const BattleField::Unit* u = f.UnitByIndex(m_Selected);
            for (int i = 0; i < int(f.Units().size()); ++i) {
                const BattleField::Unit& t = f.Units()[std::size_t(i)];
                if (i == m_Selected || !t.Alive) continue;
                if (t.X == u->X && t.Y == u->Y && f.CanJoin(m_Selected, i)) {
                    f.Join(m_Selected, i);
                    break;
                }
            }
            break;
        }
        case kActWait:
            f.Wait(m_Selected);
            break;
        case kActCancelOrder:
            CancelOrder();
            return;
        case kActEndTurn:
            m_View.MoveRange = nullptr;
            m_View.AttackRange = nullptr;
            m_Selected = -1;
            AdvanceTurn();
            return;
        case kActPerks:
            OpenPerkMenu();
            return;
        case kActOptions:
            OpenOptionsMenu();
            return;
        case kActSave:
            SaveNow();
            m_Mode = Mode::kBrowse;
            return;
        case kActSettings: {
            // The front end's Settings tree, run on a state machine of its own
            // so that Sound settings, Language, Key configuration and the two
            // confirmations underneath it all work exactly as they do from the
            // main menu. It ends when Ready or Back empties that machine.
            //
            // The attract reel is held off for the duration: these pages arm
            // it as they open, and a battle left standing on one is not idle.
            const bool attract = m_Ctx.Attract;
            m_Ctx.Attract = false;
            StateMachine settings;
            settings.Run(std::make_unique<SettingsState>(m_Ctx));
            m_Ctx.Attract = attract;
            if (m_Ctx.HostRef.QuitRequested()) {
                m_Finished = true;
                m_Outcome = Outcome::kQuit;
                return;
            }
            m_Ctx.HostRef.FlushKeys();
            // Back to the battle menu the row was chosen from -- and rebuilt,
            // because Language may have just changed every label on it.
            OpenBattleMenu();
            return;
        }
        // The Options submenu's four boards. Each is a state pushed over the
        // battle in the original, so nothing under it moves while it is up and
        // backing out of it returns to the submenu that opened it -- which is
        // what running it modally and reopening the menu afterwards comes to.
        case kActObjectives:
        case kActCommanderInfo:
        case kActBattleInfo:
        case kActMap: {
            bool alive = true;
            if (action == kActObjectives) {
                alive = ShowMissionObjectives(m_Ctx, *m_Session);
            } else if (action == kActCommanderInfo) {
                // The book opens on the seat whose turn it is, which is the
                // seat that opened the menu.
                alive = ShowCommanderInfo(m_Ctx, *m_Session, m_View.Viewer);
            } else if (action == kActBattleInfo) {
                alive = ShowBattleInfo(m_Ctx, *m_Session);
            } else {
                alive = ShowBattleMap(m_Ctx, *m_Session, m_View);
            }
            if (!alive) {
                m_Finished = true;
                m_Outcome = Outcome::kQuit;
                return;
            }
            m_Ctx.HostRef.FlushKeys();
            OpenOptionsMenu();
            return;
        }
        case kActSurrender: {
            // Surrender asks first (0x10061444): its own name on the plank,
            // "Do you really want to surrender?" under it, and Yes before No.
            const Confirmed answer =
                RunConfirm(m_Ctx, BattleData::kStrSurrender,
                           BattleData::kStrSurrenderAsk, true, kSurrenderGrain);
            if (answer == Confirmed::kQuit) {
                m_Finished = true;
                m_Outcome = Outcome::kQuit;
                return;
            }
            m_Ctx.HostRef.FlushKeys();
            if (answer == Confirmed::kYes) {
                // Surrender ends the battle as a loss.
                m_Finished = true;
                m_Outcome = Outcome::kLost;
                m_Mode = Mode::kBrowse;
                return;
            }
            // No clears the menus and puts the player back on the map; backing
            // out of the board goes one level, to the submenu that opened it.
            if (answer == Confirmed::kBacked) {
                OpenOptionsMenu();
            } else {
                m_MenuOpen = false;
                m_Mode = Mode::kBrowse;
            }
            return;
        }
        case kActQuit: {
            // "End current game" asks first (string 1618, the dialog
            // 0x1004f094's case 5 raises) and leaves the battle unresolved --
            // the engine's third outcome (0x1008b028 returns 3 and the travel
            // map treats it as "come back later"). Here it ends the whole
            // sitting: the screens between the battle and the main menu are
            // unwound, which is what the row's name promises.
            TextBox ask(m_Ctx, TextBox::kOkCancel);
            ask.Text(Text(1618));
            if (ask.Run() == TextBox::kConfirmed) {
                m_Finished = true;
                m_Outcome = Outcome::kAbandoned;
            }
            m_Mode = Mode::kBrowse;
            return;
        }
        default:
            break;
    }

    // Whatever the action did -- captured, loaded, landed, waited -- and
    // whatever it killed, in that order.
    FireDestroyTriggers();

    m_View.MoveRange = nullptr;
    m_View.AttackRange = nullptr;
    m_View.Path.clear();
    m_Selected = -1;
    m_Mode = Mode::kBrowse;
}

// --- turns -----------------------------------------------------------------

// 0x100cad50 and 0x1004c84c both end a battle the same way: OnDefeat for the
// seats outside the winning mask, then OnVictory for the ones inside it, then
// the result. A seat already put out by 0x10041cbc has had its OnDefeat, so it
// is not told twice.
void BattleScreen::SettleBattle(uint32_t winners) {
    if (m_Finished || m_Settled) return;
    m_Settled = true;
    winners &= TriggerRunner::kSeatMask;
    const uint32_t losers = ~winners & TriggerRunner::kSeatMask & ~m_Defeated;
    if (losers != 0) {
        m_Defeated |= losers;
        TriggerRunner::Context tc;
        tc.Mask = losers;
        FireTrigger(TriggerRunner::Event::kDefeat, tc);
    }
    if (winners != 0) {
        TriggerRunner::Context tc;
        tc.Mask = winners;
        FireTrigger(TriggerRunner::Event::kVictory, tc);
    }
    Finish(LocalSeats() & winners ? Outcome::kWon : Outcome::kLost);
}

void BattleScreen::Finish(Outcome outcome) {
    if (m_Finished) return;
    m_Outcome = outcome;
    m_Mode = Mode::kOver;
    // Not `m_Dialogue.Clear()`. The mission's last line belongs to the trigger
    // that just ended the battle -- SP6's fort falls, Crimson Bob says his
    // piece, and only then is it won -- so it has to be read out before the
    // result board takes over.
    PlayOutDialogue();
    m_Finished = true;
}

void BattleScreen::PlayOutDialogue() {
    Host& host = m_Ctx.HostRef;
    if (!m_Dialogue.Active()) ShowNextDialogue();
    while (m_Dialogue.Active() && !host.QuitRequested()) {
        HandleInput();
        Frame();
    }
    m_Dialogue.Clear();
}


// Ending a turn, with the two events that hang off it. The engine's rotation
// (0x100428b8) counts to five and treats the wrap through zero as the end of a
// round, and `System::OnRoundChange` is what six of the campaign's win and
// lose triggers watch -- including every turn limit, which is written as a
// script condition (`getRound() >= n`) rather than enforced by the engine.
void BattleScreen::AdvanceTurn() {
    BattleField& f = m_Session->Field();
    TriggerRunner::Context tc;
    tc.Player = f.CurrentPlayer();
    FireTrigger(TriggerRunner::Event::kEndTurn, tc);
    if (m_Finished) return;
    f.EndTurn();
    if (f.Round() != m_LastRound) {
        m_LastRound = f.Round();
        FireTrigger(TriggerRunner::Event::kRoundChange, {});
        if (m_Finished) return;
    }
    BeginPlayerTurn();
}

void BattleScreen::BeginPlayerTurn() {
    BattleField& f = m_Session->Field();
    // Turn upkeep can kill too -- a unit that began its turn starving.
    FireDestroyTriggers();
    m_View.Viewer = f.CurrentPlayer();
    // The chime that goes with the turn banner. 0x100875f8's case 4 picks
    // between two samples on whether the seat about to move is the one at the
    // keyboard -- `turn_start_player` for you, `turn_start_other` for anyone
    // else -- and plays it just before it pushes the card.
    if (m_Ctx.Sound)
        m_Ctx.Sound->PlayBattle(f.CurrentPlayer() == m_Human
                                   ? SoundManager::kSoundTurnStartPlayer
                                   : SoundManager::kSoundTurnStartOther);
    // The panels are down for the first fraction of a second of each turn and
    // then slide in; the engine stamps the same clock when the state activates
    // (0x100cdc00) as it does on every cursor move.
    DuckPanels();
    if (f.Winner() != 0) {
        SettleBattle(WinningSeats());
        return;
    }
    // Put the cursor on the first unit that can still act.
    for (const BattleField::Unit& u : f.Units()) {
        if (u.Alive && u.Owner == f.CurrentPlayer() && !u.Done && u.Carrier < 0) {
            m_View.CursorX = u.X;
            m_View.CursorY = u.Y;
            break;
        }
    }
    CentreOn(m_View.CursorX, m_View.CursorY);
    m_Mode = Mode::kBrowse;

    // Before anything can be moved, the board that says whose turn it is
    // (0x100cc460) -- for whoever is about to move, computer or not. That is
    // what the two titles are for: "You are next" for the player at the
    // keyboard and "Next Commander" for everybody else.
    //
    // The card is built by a *controller*, and it labels its own player "You"
    // (0x100cc460 compares the seat against the one the controller belongs
    // to). A mission with an ally hands the keyboard two seats, so on the
    // ally's turn the card is theirs and it is the ally who reads as "You".
    // The computer has no card of its own, so its turn is announced from the
    // seat the player actually holds.
    const BattleField::Player& next =
        f.Players()[std::size_t(f.CurrentPlayer())];
    m_TurnCard.Build(m_Ctx, f, next.Computer ? m_Human : f.CurrentPlayer());
    if (!m_TurnCard.Run(m_Ctx, f, [this](Surface& s) {
            s.Fill(0xF000);
            m_Session->Renderer().Draw(s, m_Session->Field(), m_View);
        })) {
        m_Finished = true;
        m_Outcome = Outcome::kQuit;
        return;
    }
    DuckPanels();

    // Then the mission script gets its turn: this is where the campaign's
    // conversation happens. It has to finish before anything else does --
    // before the player can move, and before the computer starts thinking.
    TriggerRunner::Context tc;
    tc.Player = f.CurrentPlayer();
    FireTrigger(TriggerRunner::Event::kBeginTurn, tc);
    PumpDialogue();
}

// 0x1007d410, once a frame, with the battlefield still drawn underneath --
// the engine's version is a pushed state, so the map is visible but frozen.
void BattleScreen::PlayCaptureHoist(int cellX, int cellY, int propertyType,
                                    int colour, int before, int after) {
    if (!m_Hoist.Ready()) return;
    // 0x100875f8 only raises it for a player the viewer is watching
    // (0x10089574: the current seat is mine, or I can see what it is doing).
    // Otherwise the computer taking a building on the far side of the fog
    // would stop the game to show a panel about nothing.
    const BattleField& field = m_Session->Field();
    if (field.FogEnabled() && !field.Visible(m_View.Viewer, cellX, cellY))
        return;
    Host& host = m_Ctx.HostRef;
    BattleRenderer& renderer = m_Session->Renderer();
    // The panel wants the square in view; the engine's is centred on it and
    // then pushed back inside the screen, which reads oddly if the square is
    // off the map's edge of the camera.
    CentreOn(cellX, cellY);
    m_Hoist.Begin(cellX, cellY, propertyType, colour, before, after);
    // The map is frozen under the panel, so it is drawn once and copied back
    // each frame rather than rebuilt -- see the note in UsePerk.
    Surface backdrop;
    backdrop.Fill(0xF000);
    renderer.Draw(backdrop, m_Session->Field(), m_View);
    while (m_Hoist.Active() && !host.QuitRequested()) {
        Surface& screen = host.Screen();
        screen.Copy(backdrop.Pixels(), backdrop.Width(), backdrop.Height(), 0,
                    0);
        m_Hoist.Step(screen, renderer, m_View.CamX, m_View.CamY);
        host.Flip();
        if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
        host.Sleep(kFrameMs);
    }
}

// The cutaway an attack plays. 0x100875f8's case 0xf pushes it as a state, so
// nothing under it moves; here it is a modal loop for the same reason.
//
// The three gates are the original's, as far as they are known -- see the note
// at the top of FightAnimation.h. The third one there is a virtual on the
// current LocalPlayer state that has not been identified, and this stands in
// for it with the fog test the capture board already uses.
// What this battle's commanders shout, and only theirs.
//
// The engine reads all five voice-over banks at startup (0x10050660) and keeps
// them for the whole run, which it can afford because a Spanc streams its .spc
// off the card a block at a time. This port decodes to PCM up front, so five
// banks would be five megabytes standing idle -- more than a Dreamcast has to
// spare once a battle's three megabytes of artwork are up. Loading only the
// nations actually seated costs one or two, and BattleScreen::Run hands them
// back when the battle ends.
void BattleScreen::LoadVoiceBanks() {
    if (!m_Ctx.Sound) return;
    const BattleField& f = m_Session->Field();
    bool wanted[SoundManager::kNationCount] = {};
    for (int p = 1; p <= BattleField::kMaxPlayers; ++p) {
        if (!f.Players()[std::size_t(p)].Present) continue;
        const int n = f.Nationality(p);
        if (n >= 0 && n < SoundManager::kNationCount) wanted[n] = true;
    }
    m_Ctx.Sound->LoadVoiceBanks(wanted);
    // What the battle's audio costs, said out loud -- the console is the
    // machine it matters on, and the worst level in the game is SP16, which
    // seats three nationalities and so asks for three of these banks.
    LogDebug("audio: %zu KB of sound resident\n", m_Ctx.Sound->Bytes() / 1024);
}

// The nation `player`'s men shout in, or -1 if that seat has no commander --
// every seat of a multiplayer map, and every seat when the field was built
// without a pak. A seat with no nation simply says nothing.
int BattleScreen::VoiceOf(int player) const {
    return m_Session->Field().Nationality(player);
}

void BattleScreen::LoadFightAnimation() {
    if (m_FightLoaded) return;
    m_FightLoaded = true;
    m_Fight.Load(m_Ctx.Textures, m_Ctx.Pack, m_Ctx.SmallFont);
    m_Fight.SetSound(m_Ctx.Sound);
    // What a unit sounds like: the battle bank, and the table that says which
    // of its samples belongs to which unit.
    if (m_Ctx.Sound) {
        m_Ctx.Sound->LoadBank(SoundManager::kBankBattle,
                             SoundManager::kBattleBankPath);
        m_Ctx.Sound->LoadUnitSounds(SoundManager::kUnitSoundPath);
    }
}

void BattleScreen::PlayFightAnimation(const BattleField::ScriptEvent& e) {
    LoadFightAnimation();
    if (!m_Fight.Ready()) return;
    const BattleField& f = m_Session->Field();
    const BattleField::Unit* a = f.UnitByIndex(e.Unit);
    const BattleField::Unit* d = f.UnitByIndex(e.Other);
    if (!a || !d) return;
    const bool visible = !f.FogEnabled() ||
                         f.Visible(m_View.Viewer, e.X, e.Y) ||
                         f.Visible(m_View.Viewer, e.FromX, e.FromY);
    if (!FightAnimation::Wanted(m_Ctx.SettingsRef.FightAnimation,
                                f.Data().Unit(a->Type).UnitClass,
                                f.Data().Unit(d->Type).UnitClass, visible)) {
        // No cutaway, so the shot is heard on the map instead. That is the
        // other half of 0x100875f8's case 0xf: the branch that does *not*
        // push the scene plays the attacker's own weapon sound where it
        // stands, and only the branch that does leaves it to the scene.
        if (m_Ctx.Sound)
            m_Ctx.Sound->PlayUnit(a->Type, SoundManager::kUnitAttack);
        return;
    }

    // What each side is standing on, and the cover it gives.
    const auto stage = [&f](const BattleField::Unit& u, int hpBefore,
                            int hpAfter, FightAnimation::Combatant& out) {
        out.Type = u.Type;
        // The seat's *colour*, not its number: two seats can want the same one
        // and the field settles that when the battle starts.
        out.Colour = f.Colour(u.Owner);
        out.Terrain = f.At(u.X, u.Y).Terrain;
        out.HPBefore = hpBefore;
        out.HPAfter = hpAfter;
        const BattleField::Property* p = f.PropertyAt(u.X, u.Y);
        out.Property = p ? p->Type : -1;
        out.Shields = p ? f.Data().Property(p->Type).Shield
                        : f.Data().Terrain(out.Terrain).Shield;
        if (u.Owner >= 1 && u.Owner <= BattleField::kMaxPlayers)
            out.Commander = f.Players()[std::size_t(u.Owner)].Name;
        // And which army shouts for this side, which the scene needs from
        // both of them: whose men call out is the attacker's business, but
        // what they call depends on who they are facing.
        out.Nationality = f.Nationality(u.Owner);
    };

    FightAnimation::Params p;
    stage(*a, e.AttackerHPBefore, e.AttackerHPAfter, p.Attacker);
    stage(*d, e.DefenderHPBefore, e.DefenderHPAfter, p.Defender);
    p.AttackerX = e.FromX;
    p.AttackerY = e.FromY;
    p.DefenderX = e.X;
    p.DefenderY = e.Y;
    p.Countered = e.Countered;

    Host& host = m_Ctx.HostRef;
    host.FlushKeys();
    m_Fight.Begin(p, m_Session->Renderer());
    LogDebug("fight: unit %d vs %d, %d squares, %s%s\n", a->Type, d->Type,
             m_Fight.Distance(),
             m_Fight.AttackerSide() == 0 ? "attacker left" : "attacker right",
             p.Countered ? ", countered" : "");
    while (m_Fight.Active() && !host.QuitRequested()) {
        Surface& screen = host.Screen();
        screen.Fill(0xF000);
        m_Fight.Step(screen);
        host.Flip();
        if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
        // The two gaming keys cut it short (0x1006f8bc asks for exactly those
        // and then clears the scene's alive flag).
        if (host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kSoftLeft) ||
            host.KeyPressed(Key::kSoftRight) || host.KeyPressed(Key::kBack))
            m_Fight.Skip();
        host.Sleep(kFrameMs);
    }
    host.FlushKeys();
}

void BattleScreen::PumpDialogue() {
    Host& host = m_Ctx.HostRef;
    while (m_Dialogue.Active() && !host.QuitRequested() && !m_Finished) {
        HandleInput();
        Frame();
    }
}

SavedBattle BattleScreen::Snapshot() const {
    SavedBattle b;
    b.Level = m_Session->Path();
    b.Name = m_Session->Name();
    b.Mission = m_Session->MissionKey();
    b.Viewer = m_Human;
    b.Encounter = m_Session->IsEncounter();
    b.Field = m_Session->Field().Save();
    b.Triggers = m_Triggers.Save();
    return b;
}

// The battle menu's Save row. One write, one answer -- the original puts the
// same two outcomes up as dialogs (0x1007834c logs "Game saved successfully!"
// or "Save game file open failed!"), and a memory card is slow enough that
// saying nothing would read as a hang.
void BattleScreen::SaveNow() {
    // Which card, on a machine where that is a question. A player who chose to
    // play without one at the start meets the picker here, which is the whole
    // point of asking again: declining was a decision, not a lock.
    Storage* store = SaveTarget(m_Ctx);
    if (!store) {
        if (m_Ctx.HostRef.QuitRequested()) return;
        // The picker has already had this conversation; a second board saying
        // there is nowhere to save would be telling them what they just said.
        if (AsksForCard(m_Ctx)) return;
        TextBox none(m_Ctx, TextBox::kOkOk);
        none.Text(SaveStatusText(SaveStatus::kNoStorage));
        none.Run();
        return;
    }
    SavedGame game;
    game.CampaignData = m_Ctx.CampaignData;
    game.InBattle = true;
    game.Battle = Snapshot();
    const SaveStatus status = WriteGame(*store, m_Session->Kind(), game);
    TextBox box(m_Ctx, TextBox::kOkOk);
    box.Text(SaveStatusText(status));
    box.Run();
}

void BattleScreen::RunAiTurn() {
    BattleField& f = m_Session->Field();
    BattleAi ai;
    Host& host = m_Ctx.HostRef;
    int guard = 0;
    while (!host.QuitRequested() && !m_Finished && guard++ < 500) {
        // Walk the unit rather than teleporting it: the computer's moves read
        // exactly like the player's, which is the only way to follow what it
        // is doing.
        const bool acted = ai.Step(
            f, f.CurrentPlayer(),
            [this](int unit, const std::vector<BattleField::Step>& path) {
                AnimateMove(unit, path);
            });
        if (!acted) break;
        // Whatever fell -- to the attack, or to the player's counterattack --
        // raises its Unit::OnDestroy before anything is said about it.
        FireDestroyTriggers();
        // Anything the move set off gets said before the next one starts.
        PumpDialogue();
        const uint32_t until = host.TickCount() + kAiStepMs;
        while (host.TickCount() < until && !host.QuitRequested()) Frame();
    }
    if (m_Finished) return;
    AdvanceTurn();
}

// --- drawing ---------------------------------------------------------------

// While a list is open the engine labels the two soft keys (its style resource
// names string 1672 "Select" and 1670 "Cancel"). Drawn through the shared
// chrome: each label on its own little plank, the physical key above it.
void BattleScreen::DrawSoftKeys(Surface& dst) {
    chrome::DrawSoftKeys(m_Ctx, dst, Text(1672), Text(BattleData::kStrCancel));
}

void BattleScreen::FireDestroyTriggers() {
    BattleField& f = m_Session->Field();
    // Whatever the rules did first -- the walk, the capture, the landing --
    // and then whatever it killed. The engine's order is the order the events
    // were pushed onto the battle's queue, and a capture that finishes a
    // mission has to be heard before the unit that died taking it.
    FireFieldEvents();
    std::vector<int> dead;
    f.TakeDeaths(dead);
    for (int i : dead) {
        const BattleField::Unit* u = f.UnitByIndex(i);
        if (!u) continue;
        // 0x100875f8's cases 0xd and 0x36 both play misc[15] as a unit goes
        // down -- whether it was shot or crushed under a collapsing keep.
        if (m_Ctx.Sound)
            m_Ctx.Sound->PlayBattle(SoundManager::kSoundUnitDestroyed);
        TriggerRunner::Context tc;
        tc.Unit = i;
        tc.Player = u->Owner;
        tc.X = u->X;
        tc.Y = u->Y;
        FireTrigger(TriggerRunner::Event::kUnitDestroy, tc);
        // A seat with nothing left is eliminated, and 0x1005d8b0 posts its
        // Player::OnDefeat the moment that happens. Thirteen of the campaign's
        // win/lose triggers hang off exactly that.
        FireDefeats();
    }
    FireDefeats();
}

// Turn what the field recorded into script events. Doing it here rather than
// in the action handlers is what makes the computer player's turn raise the
// same triggers a human's does: the AI drives BattleField directly.
void BattleScreen::FireFieldEvents() {
    BattleField& f = m_Session->Field();
    std::vector<BattleField::ScriptEvent> events;
    f.TakeEvents(events);
    for (const BattleField::ScriptEvent& e : events) {
        TriggerRunner::Context tc;
        tc.Unit = e.Unit;
        tc.Target = e.Other;
        tc.Property = e.Property;
        tc.Player = e.Player;
        tc.Other = e.Other;
        tc.X = e.X;
        tc.Y = e.Y;
        tc.FromX = e.FromX;
        tc.FromY = e.FromY;
        switch (e.Kind) {
            case BattleField::ScriptEvent::kMove:
                FireTrigger(TriggerRunner::Event::kUnitMove, tc);
                FireTrigger(TriggerRunner::Event::kRegionEnter, tc);
                break;
            case BattleField::ScriptEvent::kCaptureStart:
                tc.Other = e.Other;
                FireTrigger(TriggerRunner::Event::kCaptureStart, tc);
                break;
            case BattleField::ScriptEvent::kPerkUse:
                // Raised by UsePerk, which fires the trigger itself once the
                // animation is over -- the script's line about it should not
                // arrive underneath the flash.
                break;
            case BattleField::ScriptEvent::kCaptureCompleted:
                // A flag going up is worth a cheer: 0x100875f8's case 0x15
                // plays one of the capturing commander's six `capture_vo`
                // lines, picked at random out of the block that starts at
                // index 9 of their nation's bank.
                if (m_Ctx.Sound) {
                    const BattleField::Unit* u = f.UnitByIndex(e.Unit);
                    const int nation = u ? VoiceOf(u->Owner) : -1;
                    if (nation >= 0) {
                        m_SoundRng = m_SoundRng * 1103515245u + 12345u;
                        const int n =
                            int((m_SoundRng >> 16) %
                                unsigned(SoundManager::kVoiceCaptureCount));
                        m_Ctx.Sound->PlayVoice(
                            nation, SoundManager::kVoiceCaptureFirst + n);
                    }
                }
                FireTrigger(TriggerRunner::Event::kCaptureCompleted, tc);
                break;
            case BattleField::ScriptEvent::kCaptureProgress:
                // The flag going up the pole. It is raised for every capture
                // action, finished or not: what changes is how far the flag
                // climbs (0x100875f8's case 0x15 hands the board the capture
                // points before and after).
                PlayCaptureHoist(e.X, e.Y, e.Value, e.Before, e.FromX,
                                 e.FromY);
                break;
            case BattleField::ScriptEvent::kFight:
                // The cutaway. Raised by the rules rather than by the attack
                // handler, so the computer's attacks show it too. The
                // Unit::OnAttack trigger is not fired from here: CommitAttack
                // raises it before the blow lands, which is what the campaign's
                // "the first time player one attacks player two" wants.
                PlayFightAnimation(e);
                break;
            case BattleField::ScriptEvent::kLoad:
                if (m_Ctx.Sound)
                    m_Ctx.Sound->PlayBattle(SoundManager::kSoundLoad);
                FireTrigger(TriggerRunner::Event::kUnitLoad, tc);
                break;
            case BattleField::ScriptEvent::kUnload:
                if (m_Ctx.Sound)
                    m_Ctx.Sound->PlayBattle(SoundManager::kSoundUnload);
                FireTrigger(TriggerRunner::Event::kUnitUnload, tc);
                break;
            case BattleField::ScriptEvent::kJoin:
                FireTrigger(TriggerRunner::Event::kUnitJoin, tc);
                break;
            case BattleField::ScriptEvent::kSupply:
                // The rations thrown over the unit that just got them
                // (0x100875f8's case 0x1b), and the sound that goes with it.
                m_Session->Renderer().EmitSupply(e.X, e.Y);
                if (m_Ctx.Sound)
                    m_Ctx.Sound->PlayBattle(SoundManager::kSoundSupply);
                FireTrigger(TriggerRunner::Event::kUnitSupply, tc);
                break;
            case BattleField::ScriptEvent::kWait:
                FireTrigger(TriggerRunner::Event::kUnitWait, tc);
                break;
            case BattleField::ScriptEvent::kBuild:
                // "Swordsmen, ready!" -- 0x100875f8's case 0xc plays the
                // producing commander's own `<unit>_produced_vo1`, which is
                // why every nation's bank carries twenty-one of them in unit
                // order starting at index 27.
                if (m_Ctx.Sound) {
                    const BattleField::Unit* u = f.UnitByIndex(e.Unit);
                    const int nation = u ? VoiceOf(u->Owner) : -1;
                    if (nation >= 0 && u)
                        m_Ctx.Sound->PlayVoice(
                            nation,
                            SoundManager::kVoiceProducedFirst + u->Type);
                }
                FireTrigger(TriggerRunner::Event::kPropertyBuild, tc);
                break;
            case BattleField::ScriptEvent::kSelect:
                FireTrigger(e.Property >= 0
                                ? TriggerRunner::Event::kPropertySelect
                                : TriggerRunner::Event::kUnitSelect,
                            tc);
                break;
            case BattleField::ScriptEvent::kHealthChange:
                FireTrigger(TriggerRunner::Event::kHealthChange, tc);
                break;
            case BattleField::ScriptEvent::kAmbush:
                FireTrigger(TriggerRunner::Event::kUnitAmbush, tc);
                break;
            case BattleField::ScriptEvent::kPerkReady:
                // Only your own bar is worth telling you about; 0x100875f8's
                // case 0x34 opens with the local-player test and there is no
                // script event behind it.
                if (m_Ctx.Sound && e.Player == m_Human)
                    m_Ctx.Sound->PlayBattle(
                        e.Value ? SoundManager::kSoundSuperPerkReady
                                : SoundManager::kSoundPerkReady);
                break;
        }
    }
}

// Player::OnDefeat, raised once per seat that has just run out of everything.
void BattleScreen::FireDefeats() {
    BattleField& f = m_Session->Field();
    for (int i = 1; i <= BattleField::kMaxPlayers; ++i) {
        if (!f.Players()[std::size_t(i)].Present) continue;
        if (f.PlayerAlive(i)) continue;
        if (m_Defeated & (1u << i)) continue;
        m_Defeated |= 1u << i;
        TriggerRunner::Context tc;
        tc.Player = i;
        tc.Mask = 1u << i;
        FireTrigger(TriggerRunner::Event::kDefeat, tc);
    }
}

// Raise a mission-script event; if it queued any dialogue, put the first line
// up straight away.
void BattleScreen::FireTrigger(TriggerRunner::Event event,
                               const TriggerRunner::Context& tc) {
    if (!m_Triggers.Loaded()) return;
    m_Triggers.Fire(event, tc, m_Session->Field());
    if (!m_Dialogue.Active()) ShowNextDialogue();
    // A trigger asked for the camera.
    if (m_Triggers.FocusX() >= 0) {
        m_View.CursorX = m_Triggers.FocusX();
        m_View.CursorY = m_Triggers.FocusY();
        CentreOn(m_View.CursorX, m_View.CursorY);
        m_Triggers.ClearFocus();
    }
    // A script can end the battle outright (Player::Win / Player::Lose). Both
    // record a *seat mask*, and 0x1008b028 calls it a win when any locally
    // played seat is in it -- which is how an ally's scripted victory becomes
    // yours without anyone having to compare teams.
    const uint32_t winners = m_Triggers.Winners();
    if (winners != 0) SettleBattle(winners);
}

// The seats this machine is playing. 0x1004f708 marks them by leaving
// `player+0x18` at zero, and 0x1008b028 reads exactly that to decide whether
// the battle that just ended was won or lost.
uint32_t BattleScreen::WinningSeats() const {
    const BattleField& f = m_Session->Field();
    const int w = f.Winner();
    if (w == 0) return 0;
    uint32_t m = 0;
    for (int i = 1; i <= BattleField::kMaxPlayers; ++i)
        if (f.Players()[std::size_t(i)].Present && f.SameTeam(w, i))
            m |= 1u << i;
    return m;
}

uint32_t BattleScreen::LocalSeats() const {
    const BattleField& f = m_Session->Field();
    uint32_t m = 0;
    for (int i = 1; i <= BattleField::kMaxPlayers; ++i) {
        const BattleField::Player& p = f.Players()[std::size_t(i)];
        if (p.Present && !p.Computer) m |= 1u << i;
    }
    // A battle with no human seat at all (an autoplay harness) still has to
    // answer the question; fall back to whoever the viewer is.
    if (m == 0 && m_Human >= 1 && m_Human <= BattleField::kMaxPlayers)
        m = 1u << m_Human;
    return m;
}

void BattleScreen::ShowNextDialogue() {
    if (!m_Triggers.HasDialogue()) {
        m_Dialogue.Clear();
        return;
    }
    const DialogueLine& line = m_Triggers.Dialogue().front();
    m_Dialogue.Set(m_Ctx, line.TextID, line.SpeakerID, line.Flags);
    if (m_Ctx.Sound && !line.Sound.empty()) m_Ctx.Sound->PlayMenu(SoundManager::kSoundMove);
}

// The three info boards. Which layout applies is just whether a unit is in
// hand: the engine's browse state (0x1009b494) shows the player's own board
// and slides everything in, while every state that has picked a unit up
// (0x100973c8, 0x10036cfc) drops it and shifts the thresholds.
void BattleScreen::DrawPanels(Surface& dst) {
    if (m_Mode == Mode::kOver || MenuOpen()) return;
    // The dialogue panel takes the bottom of the screen and the engine's
    // dialogue state is a *pushed* state, so none of the three panel-drawing
    // states runs while it is up.
    if (m_Dialogue.Active()) return;
    // Only the human player's states draw them: the computer's turn runs
    // through a different Player subclass entirely, and an action popup is a
    // pushed state that does not call any of the three (they have exactly the
    // three callers listed above).
    const BattleField& field = m_Session->Field();
    if (field.Players()[std::size_t(field.CurrentPlayer())].Computer) return;
    const BattlePanels::Layout layout = m_Mode == Mode::kBrowse
                                            ? BattlePanels::Layout::kBrowse
                                            : BattlePanels::Layout::kOrdering;
    m_Panels.Step(m_Ctx.HostRef.TickCount() - m_CursorStillSince);
    m_Panels.Draw(dst, m_Session->Field(), m_Ctx.StringsRef, m_Session->Renderer(),
                 m_View, layout);
}

void BattleScreen::DrawMenu(Surface& dst) {
    if (!m_MenuOpen || m_Menu.Count() == 0) return;
    // The perks board is the one battle popup with a title plank, which its
    // state adds as a kind-6 row (0x100d1cf0's first call).
    const bool titled = m_Mode == Mode::kPerks;
    const std::string title = titled ? Text(kStrPerksTitle) : std::string();
    const int top = titled ? chrome::kListTop + chrome::TitleHeight(m_Ctx, title)
                           : kMenuTop;
    m_Menu.Draw(dst, top, m_Ctx.HostRef.TickCount());
    if (titled) chrome::DrawTitle(m_Ctx, dst, title, m_MenuTitleFrame);
    DrawSoftKeys(dst);
}

// Which action icon the cursor shows in its spare corner. The engine drives
// this from what selecting would do (0x10065c94's mode argument), and shows
// four plain triangles when selecting would do nothing.
int BattleScreen::CursorMode() const {
    if (m_Mode == Mode::kOver) return BattleRenderer::kCursorHidden;
    // Picking an attack target turns the whole cursor red.
    if (m_Mode == Mode::kTarget) return BattleRenderer::kCursorAttackA;
    BattleField& f = m_Session->Field();
    if (m_Mode == Mode::kUnloadWhere) return BattleRenderer::kCursorUnload;
    if (m_Mode == Mode::kMove) {
        if (m_Reach.empty()) return BattleRenderer::kCursorAllCorners;
        const std::size_t i =
            std::size_t(m_View.CursorY) * f.Width() + m_View.CursorX;
        return m_Reach[i] >= 0 ? BattleRenderer::kCursorSelect
                              : BattleRenderer::kCursorAllCorners;
    }
    // Otherwise the cursor is four triangles, and only turns its bottom-right
    // one into the arrow over something you can actually pick up -- or into
    // the hammer over a building you could build from. 0x1009b494 works out
    // exactly the two cases, in this order, and falls back to the triangles.
    const BattleField::Cell& c = f.At(m_View.CursorX, m_View.CursorY);
    if (c.Unit >= 0) {
        const BattleField::Unit* u = f.UnitByIndex(c.Unit);
        if (u && u->Owner == f.CurrentPlayer() && !u->Done)
            return BattleRenderer::kCursorSelect;
    }
    if (c.Property >= 0 && f.CanBuildAt(c.Property))
        return BattleRenderer::kCursorBuild;
    return BattleRenderer::kCursorAllCorners;
}

// Walk the unit square by square so a move reads as a move. The renderer takes
// a pixel position and the direction of travel and plays the `walk` clip.
void BattleScreen::AnimateMove(int unit,
                               const std::vector<BattleField::Step>& path) {
    if (path.size() < 2) return;
    Host& host = m_Ctx.HostRef;
    // What this unit sounds like moving -- hooves, wheels, oars. 0x100875f8's
    // case 8 plays it once for the whole walk, out of the battle bank through
    // the `walk` column of the units.dat table. Several units have no entry
    // there (a swordsman makes no noise) and PlayUnit is silent for those.
    if (m_Ctx.Sound) {
        if (const BattleField::Unit* u = m_Session->Field().UnitByIndex(unit))
            m_Ctx.Sound->PlayUnit(u->Type, SoundManager::kUnitWalk);
    }
    m_View.MovingUnit = unit;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const BattleField::Step& a = path[i];
        const BattleField::Step& b = path[i + 1];
        m_View.MovingDir = b.Y < a.Y   ? BattleRenderer::kFaceUp
                           : b.Y > a.Y ? BattleRenderer::kFaceDown
                           : b.X < a.X ? BattleRenderer::kFaceLeft
                                       : BattleRenderer::kFaceRight;
        const uint32_t start = host.TickCount();
        for (;;) {
            const uint32_t t = host.TickCount() - start;
            if (t >= uint32_t(kWalkMsPerTile) || host.QuitRequested()) break;
            const int num = int(t), den = kWalkMsPerTile;
            m_View.MovingPx = (a.X * BattleRenderer::kTile * (den - num) +
                               b.X * BattleRenderer::kTile * num) / den;
            m_View.MovingPy = (a.Y * BattleRenderer::kTile * (den - num) +
                               b.Y * BattleRenderer::kTile * num) / den;
            Frame();
        }
    }
    m_View.MovingUnit = -1;
}

void BattleScreen::Frame() {
    Host& host = m_Ctx.HostRef;
    Surface& screen = host.Screen();
    screen.Fill(0xF000);
    m_View.Ticks = host.TickCount() - m_StartTicks;
    // What the Battle info board calls "Time elapsed", stamped where the
    // battle is actually being played rather than counted by the rules: a
    // battle picked up from a save carries on from the number it was saved
    // with (`m_ElapsedBase`), and the boards and dialogs that stop the screen
    // do not stop the clock, which is what the original's does too.
    m_Session->Field().SetElapsed(m_ElapsedBase +
                                 int(host.TickCount() - m_StartTicks));
    // The original's battlefield viewport is the whole 176x208 screen
    // (0x100490a4 passes 0xb0 x 0xd0 to the renderer's init) and the panels
    // are drawn over it.
    m_View.Clip = {0, 0, Surface::kWidth, Surface::kHeight};
    m_View.CursorMode = CursorMode();
    m_Session->Renderer().Draw(screen, m_Session->Field(), m_View);
    DrawPanels(screen);
    // The panel eases into place, holds, then eases back out the way it came.
    // When it has finished leaving, the next queued line comes up.
    if (m_Dialogue.Active() && !m_Dialogue.Update(m_Ctx)) {
        ShowNextDialogue();
        // Popping the dialogue state re-enters the browse state, which stamps
        // the panel clock afresh (0x100cdc00) -- so the panels slide back in
        // rather than snapping.
        if (!m_Dialogue.Active()) DuckPanels();
    }
    m_Dialogue.Draw(m_Ctx, screen);
    DrawMenu(screen);
    // The overview, while its key is held. It goes on last and over
    // everything, which is where 0x1009b494 puts it: the very tail of the
    // browse state's draw, after the three info panels it hides. And only
    // there -- the browse state is the one state that draws it, so a unit in
    // hand or a popup up means no overview, the same as on the phone.
    if (m_Mode == Mode::kBrowse && !MenuOpen() && !m_Dialogue.Active() &&
        host.KeyHeld(Key::kMap))
        m_Session->Renderer().DrawMinimap(screen, m_Session->Field(),
                                         m_View.Viewer);
    host.Flip();
    if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
    host.Sleep(kFrameMs);
}

// --- input -----------------------------------------------------------------

void BattleScreen::HandleInput() {
    Host& host = m_Ctx.HostRef;
    BattleField& f = m_Session->Field();

    // Held rather than pressed, and it belongs to no one mode: asked first so
    // that it comes down the moment anything else takes the screen.
    UpdateRangePreview();

    // A line of scripted dialogue takes the select key and nothing else: the
    // cursor still walks the map underneath while it is up.
    if (m_Dialogue.Active()) {
        if (host.KeyPressed(Key::kUp)) Steer(0, -1);
        if (host.KeyPressed(Key::kDown)) Steer(0, 1);
        if (host.KeyPressed(Key::kLeft)) Steer(-1, 0);
        if (host.KeyPressed(Key::kRight)) Steer(1, 0);
        const bool takesKeys = m_Dialogue.Phase() != BattleDialogue::kLeaving;
        if (takesKeys &&
            (host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kSoftLeft) ||
             host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight))) {
            // Acknowledging a line starts it sliding away; the next one is
            // raised once it has gone (0x100a729c's state 2).
            m_Triggers.PopDialogue();
            m_Dialogue.Dismiss();
        }
        return;
    }

    if (m_MenuOpen) {
        if (host.KeyPressed(Key::kUp)) {
            m_Menu.MoveUp();
            if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kDown)) {
            m_Menu.MoveDown();
            if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kSelect) && m_Menu.SelectedEnabled()) {
            if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(SoundManager::kSoundEnter);
            const int id = m_Menu.SelectedId();
            m_MenuOpen = false;
            PerformAction(id);
        }
        if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight)) {
            if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(SoundManager::kSoundCancel);
            m_MenuOpen = false;
            // The engine gives the back key and the popup's own Cancel row the
            // same branch, so this drops back to picking a destination too.
            if (m_Mode == Mode::kAction) {
                CancelOrder();
            } else if (m_Mode == Mode::kOptions || m_Mode == Mode::kPerks) {
                // The submenu is a pushed state; backing out of it pops to
                // the battle menu, not to the map.
                OpenBattleMenu();
            } else {
                m_Mode = Mode::kBrowse;
            }
        }
        return;
    }

    // Choosing a target moves between the units in range, not over the map:
    // there is nothing to point at on an empty square.
    if (m_Mode == Mode::kTarget) {
        const int n = int(m_TargetList.size());
        int step = 0;
        if (host.KeyPressed(Key::kRight) || host.KeyPressed(Key::kDown)) step = 1;
        if (host.KeyPressed(Key::kLeft) || host.KeyPressed(Key::kUp)) step = -1;
        if (step != 0 && n > 0) {
            m_TargetIndex = (m_TargetIndex + step + n) % n;
            const Target& t = m_TargetList[std::size_t(m_TargetIndex)];
            CentreOn(t.X, t.Y);
            m_View.CursorX = t.X;
            m_View.CursorY = t.Y;
            DuckPanels();
            if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kSelect) && n > 0)
            CommitAttack(m_TargetList[std::size_t(m_TargetIndex)]);
        if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight))
            OpenActionMenu();
        return;
    }

    if (host.KeyPressed(Key::kUp)) Steer(0, -1);
    if (host.KeyPressed(Key::kDown)) Steer(0, 1);
    if (host.KeyPressed(Key::kLeft)) Steer(-1, 0);
    if (host.KeyPressed(Key::kRight)) Steer(1, 0);

    if (m_Mode == Mode::kUnloadWhere) {
        if (host.KeyPressed(Key::kSelect)) {
            if (f.Unload(m_Selected, m_UnloadSlot, m_View.CursorX, m_View.CursorY)) {
                m_View.MoveRange = nullptr;
                m_View.PlaceRange = nullptr;
                m_Selected = -1;
                m_Mode = Mode::kBrowse;
                // SP14 wins on Unit::OnUnload as well as OnRegionEnter -- a
                // landing party put straight onto the objective counts.
                FireDestroyTriggers();
            }
        }
        if (host.KeyPressed(Key::kBack)) OpenActionMenu();
        return;
    }

    if (host.KeyPressed(Key::kSoftLeft)) {
        if (m_Mode == Mode::kMove) Deselect();
        OpenBattleMenu();
        return;
    }

    // The three browse-state keys the phone kept on its numeric keypad. They
    // are read where the engine reads them (0x1009b028, 0x1009b494): with a
    // free cursor, not while a unit is in hand.
    if (m_Mode == Mode::kBrowse) {
        if (host.KeyPressed(Key::kNextUnit)) CycleUnit(1);
        if (host.KeyPressed(Key::kPrevUnit)) CycleUnit(-1);
        if (host.KeyPressed(Key::kInfo)) {
            // 0x1009b494 plays misc[11] -- `info_sound`, which exists for
            // exactly this key -- before it pushes the board.
            if (m_Ctx.Sound) m_Ctx.Sound->PlayBattle(SoundManager::kSoundInfo);
            // The board is modal, as the state the engine pushes for it is.
            if (!m_CellBoard.Run(m_Ctx, *m_Session, m_View)) {
                m_Finished = true;
                m_Outcome = Outcome::kQuit;
                return;
            }
            DuckPanels();
            host.FlushKeys();
            return;
        }
    }

    if (m_Mode == Mode::kBrowse && host.KeyPressed(Key::kSelect)) {
        const int here = f.At(m_View.CursorX, m_View.CursorY).Unit;
        const BattleField::Unit* u = here >= 0 ? f.UnitByIndex(here) : nullptr;
        if (u && u->Owner == f.CurrentPlayer() && !u->Done) {
            m_Selected = here;
            m_FromX = u->X;
            m_FromY = u->Y;
            m_FromMovement = u->Movement;
            f.Reachable(m_Selected, m_Reach);
            m_View.MoveRange = &m_Reach;
            m_View.Path.clear();
            m_View.SelectedUnit = m_Selected;
            m_Mode = Mode::kMove;
            // Picking a unit up is not a menu press: 0x1009b028 plays the misc
            // bank's `unit_select`, which is a different noise and the reason
            // that entry exists.
            if (m_Ctx.Sound)
                m_Ctx.Sound->PlayBattle(SoundManager::kSoundUnitSelect);
            return;
        }
        // A building only answers the select key when it could actually build
        // -- 0x1009b028 returns without pushing a state otherwise, and the
        // clause that matters here is that the square is empty. Capturing a
        // shipyard leaves your man standing on it, and until he walks off it
        // the yard is his footing, not a workshop.
        const int prop = f.At(m_View.CursorX, m_View.CursorY).Property;
        if (prop >= 0 && f.CanBuildAt(prop)) {
            OpenProduceMenu(prop);
            return;
        }
        // Confirming an empty square does nothing; the battle menu is on the
        // left soft key alone.
        return;
    }

    if (m_Mode == Mode::kMove) {
        if (host.KeyPressed(Key::kSelect)) {
            const std::size_t i =
                std::size_t(m_View.CursorY) * f.Width() + m_View.CursorX;
            std::vector<BattleField::Step> route = m_View.Path;
            if (m_Reach[i] >= 0 && f.MoveUnit(m_Selected, m_View.CursorX,
                                             m_View.CursorY)) {
                m_View.MoveRange = nullptr;
                m_View.Path.clear();
                AnimateMove(m_Selected, route);
                // Unit::OnMove and Unit::OnRegionEnter land here, before the
                // action popup: SP5's and SP12's objectives are "get there",
                // and getting there is the whole move, not what you do next.
                FireDestroyTriggers();
                if (m_Finished) return;
                OpenActionMenu();
            }
        }
        if (host.KeyPressed(Key::kBack)) Deselect();
    }

}

void BattleScreen::Run(StateMachine& sm) {
    Host& host = m_Ctx.HostRef;
    host.FlushKeys();
    m_StartTicks = host.TickCount();

    if (!m_Session || !m_Session->Ready() || !m_Session->Field().Valid()) {
        sm.Back();
        return;
    }
    m_Panels.Load(m_Ctx.Textures, m_Ctx.SmallFont);
    m_Hoist.Load(m_Ctx.Textures);
    // The panels, the parts and the sea the cutaway needs whatever the fight
    // turns out to be. Its backgrounds and unit sheets are loaded per fight
    // and given back at the end of one -- a battle cannot hold twenty-one
    // sheets of men at once.
    LoadFightAnimation();
    // The perk noises, and the seat's own perks. A commander with none is the
    // usual case in a skirmish, and then the bank is the only cost.
    if (m_Ctx.Sound)
        m_Ctx.Sound->LoadBank(SoundManager::kBankPerks,
                             SoundManager::kPerkBankPath);
    m_Build.Load(m_Ctx);
    // Whatever is still playing goes quiet but does not stop: 0x1008b028 opens
    // by calling 0x10050dbc with a divisor of eight, which sets every live
    // voice to an eighth of the music volume. That is what leaves the front
    // end's theme running underneath a battle instead of cutting it -- the
    // engine never stops it, and neither does this.
    if (m_Ctx.Sound) m_Ctx.Sound->Duck(m_Ctx.Sound->DuckedMusicVolume());
    m_CellBoard.Load(m_Ctx);
    BattleField& f = m_Session->Field();
    LoadVoiceBanks();
    m_Triggers.Load(f.Level());
    if (m_Resuming) {
        // The level has just been rebuilt from the pak; the save supplies
        // everything that has happened to it since. If either half refuses the
        // snapshot the two disagree about the map, and going on would put
        // units on squares that are not there.
        if (!f.Restore(m_Resume.Field) || !m_Triggers.Restore(m_Resume.Triggers)) {
            LogError("battle: saved game does not fit %s\n",
                     m_Session->Path().c_str());
            sm.Back();
            return;
        }
        m_Human = m_Resume.Viewer;
        m_ElapsedBase = f.Elapsed();
        // No StartBattle and no battle-begin trigger: the battle already
        // began, and replaying either would hand out a second turn's income
        // and say the opening lines again.
        BeginPlayerTurn();
    } else {
        f.StartBattle();
        FireTrigger(TriggerRunner::Event::kBattleBegin, {});
        BeginPlayerTurn();
    }

    while (!host.QuitRequested() && !m_Finished) {
        // A decided battle does not wait to be dismissed: the original drops
        // straight through to the result board (0x1008b028).
        if (f.Winner() != 0 && m_Mode != Mode::kOver)
            SettleBattle(WinningSeats());
        if (m_Mode != Mode::kOver &&
            f.Players()[std::size_t(f.CurrentPlayer())].Computer) {
            RunAiTurn();
            continue;
        }
        HandleInput();
        // Nothing is drawn after the host has asked to go: an input handler
        // that blocked -- a perk animation, a line of dialogue -- has already
        // noticed, and one more frame over it only hides where it stopped.
        if (host.QuitRequested()) break;
        Frame();
    }
    // Every way out of the loop goes past here, so this is where the battle's
    // audio goes back: the voice-over banks are about a megabyte each and the
    // perk bank a third of one, and none of it is wanted once the fighting has
    // stopped. (The perk bank was being held for the rest of the run; its
    // header always said it was given back at the end of a battle, and now it
    // is.)
    if (m_Ctx.Sound) {
        m_Ctx.Sound->UnloadVoiceBanks();
        m_Ctx.Sound->UnloadBank(SoundManager::kBankPerks);
    }
    if (host.QuitRequested()) {
        m_Outcome = Outcome::kQuit;
        sm.Quit();
        return;
    }
    // A skirmish is pushed straight onto the real machine, so it is this
    // screen that unwinds to the main menu. A campaign battle runs on a
    // scratch machine (MissionFlow) and the request is dropped there; the
    // outcome is what carries the same decision out to RunCampaign.
    if (m_Outcome == Outcome::kAbandoned) {
        sm.Home();
        return;
    }
    sm.Back();
}

}  // namespace bb
