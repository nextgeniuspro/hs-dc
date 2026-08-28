// TriggerScript — the mission scripts stored in every `.ndl` level.
//
// The level file carries three lists of strings per trigger and the engine
// hands them to TriggerCompiler::compileEvents / compileConditions /
// compileActions (0x100c3070). Each string is one call in a tiny language:
//
//   Player::OnBeginTurn(preset(PlayerType("Player 1")))
//   equal(function(System::getNumberOfUnits(preset(UnitType("Any unit")),
//         preset(PlayerType("Player 2")),preset(RegionType("Any region")))),
//         const(IntegerType("1")))
//   UI::ShowDialog(const(IntegerType("20001")),const(IntegerType("5111")),
//                  const(IntegerType("0")))
//
// So: a name, then arguments that are each `preset(Type("text"))`,
// `const(Type("text"))` or `function(Call(...))`. The compiler resolves each
// name to a handler through a string→function map it builds at startup; the
// whole vocabulary is 75 entries and this header carries the ones the shipped
// campaign actually uses.
//
// A trigger fires when one of its events matches, every condition holds, and
// then it runs its actions in order. `System::DisableTrigger(Current trigger)`
// is how a one-shot line stops repeating, and nearly every dialogue trigger
// ends with it.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace bb {

struct ScriptCall;

// One argument: a literal, a nested call, or a reference to one of the
// editor's own objects.
//
// The compiled form is a two-word slot `{type, value}` and the *type* is what
// every handler branches on (0x100c8018, 0x100c7b48, 0x100c7e24 all switch on
// the first word): **7 = a preset token**, **6 = an object id**, 4 = a region
// literal, 3 = a point, 1 = an integer, 0 = a boolean. So `variable(N)` is not
// a fourth flavour of literal -- it is a *handle*, and what it resolves to
// depends on where it is used: a unit or property is found by matching N
// against the placement id the level file stores in the top half of its word,
// a region is the box the editor wrote beside it, and a trigger is the trigger
// whose id is N.
struct ScriptArg {
    // "preset", "const", "function", "variable", or empty for a bare token.
    std::string Kind;
    // "PlayerType", "IntegerType", "UnitType", "RegionType", "StringType"...
    std::string Type;
    std::string Text;
    std::shared_ptr<ScriptCall> Call;

    bool IsCall() const { return Call != nullptr; }
    bool IsVariable() const { return Kind == "variable"; }
    bool IsPreset() const { return Kind == "preset"; }
    // The text as a number; 0 if it isn't one. For a variable that is the
    // object id it names.
    int Number() const;
};

// Which seat a `PlayerType` argument names. The editor's vocabulary (the
// table at 0x1014829c) has four forms: an absolute seat, the nth human seat,
// the nth computer seat, and the wildcards.
struct PlayerRef {
    enum PlayerKind { kAny, kAbsolute, kHuman, kComputer, kCurrent, kAnyHuman,
                kAnyComputer };
    PlayerKind Kind = kAny;
    int Index = 0;   // 1-based, for kAbsolute / kHuman / kComputer

    static PlayerRef Parse(const std::string& text);
};

struct ScriptCall {
    std::string Name;          // "Player::OnBeginTurn"
    std::vector<ScriptArg> Args;

    // The part after "::", or the whole name if there is no namespace.
    std::string Verb() const;
    std::string Space() const;
};

// Parse one script line. Returns false if it is not a call at all.
bool ParseScriptCall(const std::string& text, ScriptCall& out);

}  // namespace bb
