// TriggerRunner — runs a level's mission scripts while the battle plays.
//
// Each trigger is events + conditions + actions (see TriggerScript.h). The
// battle raises named events as things happen; a trigger whose event matches
// and whose conditions all hold runs its actions in order. Almost every
// dialogue trigger finishes with `System::DisableTrigger(Current trigger)`, so
// a line is said once.
//
// **This is also where a mission is won and lost.** Only two of the campaign's
// levels end by elimination alone; the other sixty-four end because a trigger
// said so, and the shipped scripts spell out eight ways to get there --
// reaching a region with enough men, sinking the last of somebody's boats,
// holding a named set of buildings, flattening a castle, loading a wagon onto
// a ship, running out of rounds, running out of units, or somebody being
// eliminated. `Player::Win` and `Player::Lose` are the only verbs that finish
// a battle, and both take a *seat mask*, not a seat: `Win(Human player)` on a
// mission that seats two of them names both.
//
// The pieces that make that work, all from the binary:
//
//   * **`variable(N)` is a handle on one of the editor's objects, not a
//     value.** For a unit or a property it is matched against the object's own
//     id (0x100c8018 compares `arg.value == object->id()`), which the level
//     file carries in the top half of each placement word. For a region it is
//     the four-corner box the editor wrote beside it. For a trigger it is that
//     trigger's id.
//   * **A player argument resolves to a bitmask** (0x100c7c20): "Player 1" is
//     `1 << 1`, "Human player 2" is one over the *nth human seat*, "Human
//     player" is every human seat at once, "Any player" is all of them.
//   * **`equal` on two of those masks is an intersection test, not a
//     comparison** (0x100f0e48: when the left value is enum-typed it resolves
//     both sides and returns `(a & b) != 0`). That is what lets SP16 ask
//     whether six named forts are all held by "Human player" when three
//     different seats are human.
//   * `System::getNumberOfUnits(type, player, region)` counts inside a box,
//     and the box argument is load-bearing: SP5 and SP14 both win on "three of
//     my men are standing in this rectangle", and ignoring the region turns
//     that into "three of my men are alive", which is true from the first
//     frame.
//   * `Unit::OnRegionEnter` fires on the *path*, not the destination
//     (0x100c8fa4 takes the route's first and last point and wants the first
//     outside the box and the last inside).
//
// Coverage: the whole campaign uses 20 event verbs, 15 action verbs and four
// condition forms. Everything that decides an outcome, carries the story, or
// changes state runs. What is recognised and skipped is the computer player's
// steering (`AI::*`) and the decorations (`UI::ShowExplosion`,
// `UI::HighlightRegion`, `UI::HightlightUnit`), and every skip is logged once
// so the gap is visible rather than silent.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "game/TriggerScript.h"

namespace bb {

class BattleField;
class NdLevel;

// A line of dialogue a trigger asked for.
struct DialogueLine {
    int TextID = 0;
    int SpeakerID = 0;
    // ShowDialog's third argument, which is the panel's position: 0 rises
    // from the bottom of the screen, 1 drops in from the top (0x100a6dac
    // hands it straight to the panel as its `mode`).
    int Flags = 0;
    std::string Sound;   // the .spc queued alongside it, if any
};

class TriggerRunner {
public:
    // Every seat is a bit, one-based, exactly as 0x100c7c20 builds them.
    static constexpr uint32_t kSeatMask = 0x1eu;   // seats 1..4

    // The events the battle raises. Named as the scripts name them.
    enum class Event {
        kBeginTurn,        // Player::OnBeginTurn(player)
        kEndTurn,          // Player::OnEndTurn(player)
        kRoundChange,      // System::OnRoundChange()
        kAttack,           // Unit::OnAttack(unit, player, target, owner, region)
        kUnitDestroy,      // Unit::OnDestroy(unit, player)
        kUnitMove,         // Unit::OnMove(unit, player, region)
        kUnitPreMove,      // Player::OnUnitPreMove(player, unit, region)
        kRegionEnter,      // Unit::OnRegionEnter(unit, player, region)
        kCaptureStart,     // Property::OnCaptureStart(prop, owner, unit, by, region)
        kCaptureCompleted, // Property::OnCaptureCompleted(same shape)
        kUnitLoad,         // Unit::OnLoad(cargo, player, transport, region)
        kUnitUnload,       // Unit::OnUnload(cargo, player, transport, region)
        kUnitJoin,
        kUnitWait,
        kUnitSupply,
        kUnitAmbush,
        kUnitSelect,       // Unit::OnSelect(unit, player, region)
        kPropertySelect,   // Property::OnSelect(prop, player, region)
        kPropertyBuild,    // Property::OnUnitBuild(prop, owner, unit, player, region)
        kHealthChange,     // Special::OnHealthChange(pointOrVariable)
        kDefeat,           // Player::OnDefeat(player) -- carries a seat mask
        kVictory,          // Player::OnVictory(player) -- ditto
        kPerkUse,
        kBattleBegin,
        kBattleEnd,
    };

    // What an event carries. Unused members stay at their defaults.
    struct Context {
        int Player = 0;       // whose turn / who acted / a capture's old owner
        int Other = 0;        // the defender's owner, or the capturing player
        int Unit = -1;        // unit index
        int Target = -1;      // the other unit (defender, transport, cargo)
        int Property = -1;    // property index
        int X = 0, Y = 0;
        int FromX = 0, FromY = 0;   // where a move set out from
        // Player::OnDefeat and Player::OnVictory are raised with a *mask* of
        // seats, not one seat: 0x100c8d58 matches when the argument's mask and
        // the event's overlap. Player::Win posts both at once, one for the
        // winners and one for everybody else.
        uint32_t Mask = 0;
    };

    void Load(const NdLevel& level);
    bool Loaded() const { return !m_Triggers.empty(); }
    int Count() const { return static_cast<int>(m_Triggers.size()); }

    // Raise an event. Any trigger it fires appends to the queues below.
    void Fire(Event event, const Context& ctx, BattleField& field);

    // Dialogue the last Fire produced, oldest first. The battle screen shows
    // them one at a time and calls PopDialogue when each is dismissed.
    const std::vector<DialogueLine>& Dialogue() const { return m_Dialogue; }
    void PopDialogue();
    bool HasDialogue() const { return !m_Dialogue.empty(); }

    // Who a trigger declared the winner, as a seat mask. Zero while the
    // battle is still running. `Player::Lose(x)` records `~x`, exactly as
    // 0x100cadd4 does, so there is only ever one answer to store.
    uint32_t Winners() const { return m_Winners; }
    bool Finished() const { return m_Winners != 0; }

    // `System::SetTurnLimit`. Zero means the mission table's own limit stands.
    int TurnLimit() const { return m_TurnLimit; }

    // A camera move a trigger asked for, in cells; -1 when none is pending.
    int FocusX() const { return m_FocusX; }
    int FocusY() const { return m_FocusY; }
    void ClearFocus() { m_FocusX = m_FocusY = -1; }

    // Script variables (System::SetVariable / getVariable), keyed by the id a
    // `variable(N)` names.
    int Variable(int id) const;

    // Verbs seen but not implemented, for the log and for tests.
    const std::vector<std::string>& Unhandled() const { return m_Unhandled; }

    // What a saved battle has to carry from the mission script: which triggers
    // have already fired themselves off (almost every dialogue trigger ends
    // with `System::DisableTrigger(Current trigger)`, so this is what stops a
    // reloaded battle saying every line again), the script variables, and any
    // result a trigger has already declared.
    //
    // The dialogue queue is deliberately not here. The battle menu is only
    // reachable with the map idle and nothing on screen, so at the moment a
    // save is taken the queue is empty by construction; storing it would be
    // storing a state that cannot occur. The engine's own
    // `TriggerDatabase::save` (0x100c2760) logs the same three things --
    // triggers, trigger variables, bound triggers -- and no dialogue either.
    struct State {
        std::vector<uint8_t> Enabled;   // one per trigger, in load order
        std::vector<std::pair<int, int>> Variables;
        uint32_t Winners = 0;
        int TurnLimit = 0;
    };

    State Save() const;
    // False if the script does not match the one this state came from, which
    // means the level and the save disagree.
    bool Restore(const State& s);

private:
    struct Trigger {
        int ID = 0;
        bool Enabled = true;
        std::vector<ScriptCall> Events, Conditions, Actions;
    };

    // A compiled argument value, mirroring the engine's two-word slot: what a
    // condition compares depends on the *type*, not just the number.
    struct Value {
        enum ValueKind {
            kInt,       // an integer or a count
            kBool,      // preset(BooleanType(...))
            kEnum,      // a preset token resolved to a seat mask
            kHandle,    // variable(N): an object id, which may hold a boolean
        };
        ValueKind Kind = kInt;
        int I = 0;
        uint32_t Mask = 0;
    };

    // Inclusive box, already normalised.
    struct Box {
        int X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
        bool Contains(int x, int y) const {
            return x >= X1 && x <= X2 && y >= Y1 && y <= Y2;
        }
    };

    bool Matches(const Trigger& t, Event event, const Context& ctx,
                 const BattleField& field) const;
    bool ConditionsHold(const Trigger& t, const BattleField& field) const;
    void RunActions(Trigger& t, BattleField& field);

    Value Evaluate(const ScriptArg& arg, const BattleField& field) const;
    Value Evaluate(const ScriptCall& call, const BattleField& field) const;
    bool Compare(const std::string& verb, const Value& a, const Value& b) const;

    // Argument resolvers, each the engine's own routine.
    uint32_t PlayerMask(const ScriptArg& arg, const BattleField& field) const;
    uint32_t UnitTypeMask(const ScriptArg& arg, const BattleField& field) const;
    uint32_t PropertyTypeMask(const ScriptArg& arg) const;
    Box Region(const ScriptArg& arg, const BattleField& field) const;
    bool UnitMatches(const ScriptArg& arg, int unitIndex,
                     const BattleField& field) const;
    bool PropertyMatches(const ScriptArg& arg, int propertyIndex,
                         const BattleField& field) const;
    int UnitByVariable(int id, const BattleField& field) const;
    int PropertyByVariable(int id, const BattleField& field) const;
    // A `variable(N)` used where a health reading is wanted resolves to a
    // property; a `const(PointType)` to a cell. Both answer in hit points.
    int HealthOf(const ScriptArg& arg, const BattleField& field) const;

    const ScriptArg* Arg(const ScriptCall& c, std::size_t i) const {
        return i < c.Args.size() ? &c.Args[i] : nullptr;
    }
    void Note(const std::string& verb);

    const NdLevel* m_Level = nullptr;
    std::vector<Trigger> m_Triggers;
    std::vector<DialogueLine> m_Dialogue;
    std::map<int, int> m_Variables;
    std::vector<std::string> m_Unhandled;
    // A PlaySound is written just before the line it belongs to.
    std::string m_PendingSound;
    // The trigger currently running, so `System::DisableTrigger(Current
    // trigger)` and a nested `variable(N)` trigger handle mean the same thing.
    uint32_t m_Winners = 0;
    int m_TurnLimit = 0;
    int m_FocusX = -1, m_FocusY = -1;
};

// The event name a script uses for each Event, for matching and for tests.
const char* TriggerEventName(TriggerRunner::Event e);

}  // namespace bb
