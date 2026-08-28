// BattleScreen — the in-battle interface and turn loop.
//
// The engine splits this between LocalPlayer (which owns the cursor and the
// action menus, 0x100873b0 / 0x100875f8) and BattleField (which owns the
// rules). The port keeps that split: everything here is input, presentation
// and sequencing, and every rule lives in BattleField.
//
// The flow is the genre's, and the engine's: move the cursor, press select on
// one of your own units to see where it can go, move it, pick an action from
// the list that appears, and when every unit is done end the turn from the
// battle menu on the left soft key. Strings come from the game's own table
// (1701 Attack, 1702 Capture, 1707 Wait, 2055 End turn, ...), so the screen
// speaks whichever language the front end is set to.
//
// A BattleSession bundles what a battle needs -- the attribute tables, the
// renderer and the field -- so the dev flag and the menus can both start one.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "game/BattleData.h"
#include "game/BattleField.h"
#include "game/BattleDialogs.h"
#include "game/BattlePanels.h"
#include "game/BattleRenderer.h"
#include "game/BuildMenu.h"
#include "game/CaptureHoist.h"
#include "game/CellBoard.h"
#include "game/FightAnimation.h"
#include "game/PerkFlash.h"
#include "game/MenuList.h"
#include "game/SaveGame.h"
#include "game/TriggerRunner.h"
#include "game/StateMachine.h"

namespace bb {

struct GameContext;
class Font;

// Everything a battle needs, loaded once.
class BattleSession {
public:
    bool Load(GameContext& ctx, const std::string& levelPath);
    bool Ready() const { return m_Ready; }

    // What the objectives panel calls this battle. Set from the mission table
    // where there is one; falls back to the level file's own title.
    void SetName(std::string name) { m_Name = std::move(name); }
    const std::string& Name() const { return m_Name; }

    // Which of the original's three save files this battle belongs in, and the
    // mission table key that names it. 0x10078184 picks the file from the game
    // type in exactly this way; the key is what a resumed campaign needs in
    // order to carry on from the right place on the chart.
    void SetSaveKind(SaveKind kind) { m_Kind = kind; }
    SaveKind Kind() const { return m_Kind; }
    // `index` is the table's MISSION_INDEX, which every per-mission string is
    // keyed by -- the name at 10000 + n and the objectives the Options
    // submenu shows at 10100 + n. Zero when the battle is not a table entry.
    void SetMissionKey(std::string key, int index = 0) {
        m_Mission = std::move(key);
        m_MissionIndex = index;
    }
    const std::string& MissionKey() const { return m_Mission; }
    int MissionIndex() const { return m_MissionIndex; }
    // A random sea battle rather than a place on the chart; see SavedBattle.
    void SetEncounter(bool on) { m_Encounter = on; }
    bool IsEncounter() const { return m_Encounter; }

    // The mission table's two seating facts, applied by Load() once the level
    // has built the board: how many seats the player takes, counting from
    // one, and the TEAMS masks that say who is fighting alongside whom. Both
    // come off a MissionDatabase::Mission -- `m.HumanSeats()` and `m.teams`.
    // A battle started from a bare level path gets the defaults: one seat, no
    // alliances.
    void SetSeating(int humanSeats, std::vector<uint32_t> teams) {
        m_HumanSeats = humanSeats;
        m_Teams = std::move(teams);
    }

    BattleData& Data() { return m_Data; }
    BattleRenderer& Renderer() { return m_Renderer; }
    BattleField& Field() { return m_Field; }
    const BattleField& Field() const { return m_Field; }
    const std::string& Path() const { return m_Path; }

private:
    BattleData m_Data;
    BattleRenderer m_Renderer;
    BattleField m_Field;
    std::string m_Path;
    std::string m_Name;
    std::string m_Mission;
    int m_MissionIndex = 0;
    int m_HumanSeats = 1;
    std::vector<uint32_t> m_Teams;
    SaveKind m_Kind = SaveKind::kHotSeat;
    bool m_Encounter = false;
    bool m_Ready = false;
};

class BattleScreen : public GameState {
public:
    BattleScreen(GameContext& ctx, std::unique_ptr<BattleSession> session)
        : m_Ctx(ctx), m_Session(std::move(session)) {}

    void Run(StateMachine& sm) override;
    const char* Name() const override { return "Battle"; }

    // Carry on a battle that was saved from the battle menu instead of
    // starting a new one. The session must already be loaded from
    // `saved.level`; the snapshot goes on top of it, and the opening -- the
    // first turn's upkeep, the battle-begin trigger -- is not replayed,
    // because it already happened before the save was taken.
    void Resume(const SavedBattle& saved) {
        m_Resume = saved;
        m_Resuming = true;
    }

    // What the battle menu's Save row writes, and what a resumed battle
    // restores. Public because the mission flow saves at its own checkpoints
    // too, not just when the player asks.
    SavedBattle Snapshot() const;

    // How long the computer waits between its moves, so a human can follow
    // what it is doing.
    static constexpr int kAiStepMs = 240;
    static constexpr int kFrameMs = 40;
    // How long a turn banner stays up. The original's battlefield has no
    // permanent panel -- its several info panels are a separate screen the
    // port has not reached -- so the only thing drawn over the map here is a
    // short-lived line saying whose turn it is or what an attack did.
    // Where a list sits. The battle popup is the same widget as a front-end
    // screen with no title, and those start at y = 6 (MenuScreen's kTitleY).
    static constexpr int kMenuTop = 6;
    // How long a unit takes to cross one square when it walks. The original
    // paces this from the unit's own `walk` clip; a flat rate reads the same
    // and keeps the turn moving.
    static constexpr int kWalkMsPerTile = 110;

    // Test seams. The action popup is otherwise only reachable by driving the
    // whole screen through a scripted host, which makes its contents awkward to
    // assert on.
    //
    // `OrderUnitTo` is exactly what the screen does when you confirm a
    // destination: remember where the unit set out from, walk it there, open the
    // popup. Pass the unit's own square to order it in place.
    const MenuList& OrderUnitTo(int unit, int x, int y);
    void TakeAction(int id) { PerformAction(id); }
    // Confirm whatever the Attack order is currently pointing at -- what
    // pressing Select does once the target cursor is up. False when the screen
    // is not choosing a target. This is the only way to reach the cutaway
    // fight scene without a scripted host walking the whole popup.
    bool AttackTargetNow();
    const BattleField& Field() const { return m_Session->Field(); }
    BattleField& MutableField() { return m_Session->Field(); }
    const BattleRenderer::View& View() const { return m_View; }
    int SelectedUnit() const { return m_Selected; }
    BattlePanels& Panels() { return m_Panels; }
    void MoveCursorBy(int dx, int dy) { MoveCursor(dx, dy); }
    // The two keypad keys that are otherwise only reachable through a host
    // that holds a button down: walking between your own units, and the reach
    // preview asking the host whether its key is held this instant.
    void CycleUnitBy(int step) { CycleUnit(step); }
    void PollRangePreview() { UpdateRangePreview(); }
    // The same, through whatever an enemy's Basker Confusion is doing to the
    // controls. Test seam as well as the screen's own path.
    void SteerBy(int dx, int dy) { Steer(dx, dy); }

    // How the battle ended, for the mission flow that has to decide between
    // the victory and the defeat board. The engine's runner reads the same
    // three outcomes off the battle core (0x1008b028's `iVar5`: 1 won,
    // 2 quit, 3 surrendered).
    //
    // kQuit and kAbandoned are both "the battle stopped without a result", and
    // they part company above this class: kQuit is the host wanting the process
    // gone, kAbandoned is the player choosing "End current game" and expecting
    // the main menu.
    enum class Outcome { kUnfinished, kWon, kLost, kQuit, kAbandoned };
    Outcome Result() const { return m_Outcome; }
    // Who is at the keyboard. The campaign always seats player one.
    void SetViewer(int player) { m_Human = player; }

private:
    enum class Mode {
        kBrowse,        // free cursor
        kMove,          // a unit is picked; showing where it can go
        kAction,        // the action list, after the move
        kTarget,        // choosing an attack target
        kUnloadWhere,   // choosing where to drop a passenger
        kMenu,          // the battle menu
        kOptions,       // its Options submenu, where Surrender lives
        kPerks,         // the commander's perks, over the map
        kOver,          // someone won
    };

    void Frame();
    // Raise a mission-script event and, if it produced any dialogue, show it.
    void FireTrigger(TriggerRunner::Event event,
                     const TriggerRunner::Context& ctx);
    // Drain everything the rules recorded since the last call and turn it
    // into mission-script events: first what the field did (a walk, a capture,
    // a landing), then Unit::OnDestroy for whatever fell, then
    // Player::OnDefeat for any seat that has just run out of everything. The
    // campaign's objectives hang off all three -- SP2 wins when the last of
    // the smugglers' rowing boats goes down, SP5 when three men reach the
    // beach, SP11 when three named forts are held at once.
    void FireDestroyTriggers();
    void FireFieldEvents();
    void FireDefeats();
    // The seats played from this machine, as a bitmask.
    uint32_t LocalSeats() const;
    // Every seat on the side that is left standing, as a bitmask. 0x1004c84c
    // reads the survivor's *team* and turns that into the mask it declares the
    // winner, which is how an ally who was wiped out halfway through still
    // ends the battle on the winning side.
    uint32_t WinningSeats() const;
    // Rotate to the next player: the end-of-turn and round-change events, then
    // the next seat's upkeep and its opening lines.
    void AdvanceTurn();
    // Take the next queued line and put it on screen.
    void ShowNextDialogue();
    // Hold everything else until the queue is empty. The engine's dialogue is
    // a state pushed on top of the battle (0x100a7044), so nothing underneath
    // is ticked while a line is up -- the turn does not begin, and the
    // computer does not move, until the last line has been acknowledged. The
    // cursor still walks, because that much the pushed state forwards.
    void PumpDialogue();
    // Run the capture board to its end, drawing the map behind it.
    void PlayCaptureHoist(int cellX, int cellY, int propertyType,
                          int colour, int before, int after);
    // Run the cutaway fight scene an attack sets off, if the settings and the
    // fog allow it. Everything else stops while it plays, exactly as the
    // pushed state does in the original.
    void PlayFightAnimation(const BattleField::ScriptEvent& e);
    // Bring up the scene's artwork and its sound bank. Called when the battle
    // starts, because a hitch at load is better than one mid-turn on a machine
    // with a disc in it -- and again on the first fight, so a screen driven
    // through its test seams rather than through Run() still has them.
    void LoadFightAnimation();
    // Read the voice-over banks for the nations seated in this battle, and
    // only those. See the definition for why the port does not read all five
    // the way the engine does.
    void LoadVoiceBanks();
    // Which nation a seat's men shout in, or -1 for a seat with no commander.
    int VoiceOf(int player) const;
    void DuckPanels();
    void DrawPanels(Surface& dst);
    void DrawMenu(Surface& dst);
    void DrawSoftKeys(Surface& dst);
    void HandleInput();
    bool MenuOpen() const;
    void OpenActionMenu();
    void OpenBattleMenu();
    void OpenOptionsMenu();
    // The commander's perks, as UsePerkMenuState lists them (0x100d1cf0): one
    // row per perk they carry, live only while the bar can pay for it.
    void OpenPerkMenu();
    // Set one off and show it: the animation over every unit it touched, the
    // perk's own noise, and the script's Player::OnPerkUse.
    void UsePerk(int perk);
    void OpenProduceMenu(int propertyIndex);
    void PerformAction(int action);
    // Something the Attack order can be pointed at: an enemy unit, or a square
    // with an obstacle standing on it. The engine keeps the two apart --
    // 0x10092aa8 validates a fight, 0x10092e58 a demolition -- but the popup
    // offers one Attack row and the target cursor walks both.
    struct Target {
        int Unit = -1;   // -1 means the obstacle at (x, y)
        int X = 0, Y = 0;
    };
    void CommitAttack(const Target& target);
    // Everything the selected unit could shoot at from where it stands:
    // enemies first, then obstacles, in map order.
    void BuildTargetList(std::vector<Target>& out) const;
    void RewindMove();
    // Back out of the action popup: the unit goes home but stays picked up,
    // with its range and route still drawn, ready to be sent somewhere else.
    void CancelOrder();
    // Back out of the selection itself: nothing picked up, free cursor.
    void Deselect();
    // Whether cargo slot `slot` has a legal square to be put down on, which is
    // what decides if it gets a row in the action popup.
    bool UnloadPossible(int transport, int slot) const;
    // Choosing "Unload <unit>": light the squares that will take the
    // passenger in green and put the cursor on one of them.
    void BeginUnloadPlacement();
    // Squares this unit could shoot at from where it stands, for the red
    // overlay. Empty when it cannot attack anything.
    void BuildAttackRange(int unitIndex, std::vector<uint8_t>& out);
    // Walk the cursor to the next (or previous) unit of the seat whose turn it
    // is that can still act -- the keypad's 3 and 1 (0x1009b028 walks the
    // seat's unit list with 0x10089110 and 0x10089018, skipping anything dead
    // or finished and stopping when it comes back where it started).
    void CycleUnit(int step);
    // The reach preview the range key holds up: while it is down, the unit
    // under the cursor shows where it could go -- or, for the one unit that
    // cannot go anywhere, where it could shoot. It is a state of its own in
    // the engine (0x1009ad88) and it drops the overlay the moment the key
    // comes up.
    void UpdateRangePreview();
    // Walk `unit` along `path`, drawing every frame, then leave it there.
    void AnimateMove(int unit, const std::vector<BattleField::Step>& path);
    int CursorMode() const;
    void MoveCursor(int dx, int dy);
    void Steer(int dx, int dy);
    void CentreOn(int x, int y);
    void RunAiTurn();
    void BeginPlayerTurn();
    // The battle menu's Save row. Writes the campaign and this battle into the
    // slot the session belongs to and tells the player what happened.
    void SaveNow();
    std::string Text(int id) const;
    // Declare the battle over for a mask of winning seats: Player::OnDefeat for
    // everyone else, Player::OnVictory for them, and then the result. Both ways
    // a battle can end go through here, because the engine's do -- the script's
    // `Player::Win` (0x100cad50) posts the same two messages that 0x1004c84c
    // posts when the last enemy is gone, and thirteen of the campaign's
    // missions keep their closing line on an `OnVictory` trigger for exactly
    // the second case.
    void SettleBattle(uint32_t winners);
    // Settle the battle and leave: the result board is next.
    void Finish(Outcome outcome);
    // Let whatever the last trigger queued be read before the result board
    // takes the screen. The line that ends a mission is written *by* the
    // trigger that ends it, so dropping the queue here would swallow it.
    void PlayOutDialogue();

    GameContext& m_Ctx;
    std::unique_ptr<BattleSession> m_Session;
    BattleRenderer::View m_View;
    // The three info boards over the map. They duck out of the way while the
    // cursor is walking and slide back a fraction of a second after it stops,
    // so the screen remembers when it last moved.
    BattlePanels m_Panels;
    uint32_t m_CursorStillSince = 0;
    // The mission script, and the two things it can put over the map.
    TriggerRunner m_Triggers;
    BattleDialogue m_Dialogue;
    TurnCard m_TurnCard;
    // The flag-raising board a capture puts over the building. It is a state
    // in the engine, pushed on top of the battle, so nothing else moves while
    // it plays; here it is a modal loop for the same reason.
    CaptureHoist m_Hoist;
    PerkFlash m_PerkFlash;
    // The cutaway an attack plays, for the same reason: the engine pushes it
    // on top of the battle and the map stops underneath.
    FightAnimation m_Fight;
    bool m_FightLoaded = false;
    // Which trinket the perks board's title plank wears, picked once a visit.
    int m_MenuTitleFrame = 0;
    // The scrambler Basker Confusion's Master version steers with.
    uint32_t m_Confusion = 0x2545f491u;
    // A stream of its own for the sounds that pick between takes -- which of
    // six cheers a capture raises. Kept apart from `m_Confusion` so that
    // turning the sound off cannot change how the perk scrambles the d-pad.
    uint32_t m_SoundRng = 0x1f123bb5u;
    // The build screen, which is modal and has art of its own.
    BuildMenu m_Build;
    // The cell board the info key raises, likewise.
    CellBoard m_CellBoard;
    Mode m_Mode = Mode::kBrowse;
    // The action popup is the engine's own list widget: the battle menu is
    // built by the same class as every front-end screen (0x10038f90) with the
    // same style resource, so it is the same wooden boards and sword cursor.
    MenuList m_Menu;
    bool m_MenuOpen = false;
    int m_Selected = -1;          // unit being ordered
    int m_FromX = 0, m_FromY = 0, m_FromMovement = 0;
    int m_UnloadSlot = 0;
    int m_ProduceProperty = -1;
    std::vector<int> m_Reach;
    std::vector<uint8_t> m_Targets;   // squares in range, for the red overlay
    // The range key's preview, which is nobody's order: its own buffers and
    // its own overlay slot (the renderer's red kind 2), so that holding the
    // key over an enemy cannot disturb the yellow overlay belonging to a unit
    // the player has actually picked up.
    std::vector<int> m_PreviewReach;
    std::vector<uint8_t> m_PreviewTargets;
    int m_PreviewUnit = -1;          // whose reach is drawn, or -1
    std::vector<uint8_t> m_Place;     // squares a passenger may land on, green
    std::vector<Target> m_TargetList;
    int m_TargetIndex = 0;
    uint32_t m_StartTicks = 0;
    // How long this battle had already been played when this sitting began:
    // zero for a fresh one, the saved total for a resumed one.
    int m_ElapsedBase = 0;
    // Seats whose Player::OnDefeat has already been raised, so a seat that
    // stays eliminated does not raise it once per death afterwards.
    uint32_t m_Defeated = 0;
    // The round the last System::OnRoundChange was raised for. The engine
    // treats the rotation's wrap through zero as the end of a round
    // (0x100428b8) and six of the campaign's win/lose triggers fire on it.
    int m_LastRound = -1;
    // Whether the battle has already been declared over. The engine caches the
    // answer on the rules object (0x1004c84c's `param_2[2]`) and never asks a
    // second time; here it stops OnVictory being raised again by whatever the
    // closing dialogue's own frames go on to do.
    bool m_Settled = false;
    bool m_Finished = false;
    Outcome m_Outcome = Outcome::kUnfinished;
    int m_Human = 1;
    // A save this screen is being started from, rather than a fresh battle.
    SavedBattle m_Resume;
    bool m_Resuming = false;
};

}  // namespace bb
