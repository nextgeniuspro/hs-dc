#include "game/TriggerRunner.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "game/BattleData.h"
#include "game/BattleField.h"
#include "game/NdLevel.h"
#include "shim/Log.h"

namespace bb {
namespace {

// Verbs that are recognised but do nothing: the ones that only steer the
// computer player, and the purely decorative ones.
const char* const kKnownUnimplemented[] = {
    "AI::SpecificHold",    "AI::SpecificBuildUnits", "AI::GeneralCapture",
    "AI::LoadUnit",        "AI::UnloadUnit",         "UI::ShowExplosion",
    "UI::HighlightRegion", "UI::HightlightUnit",
};

bool IsKnownUnimplemented(const std::string& name) {
    for (const char* v : kKnownUnimplemented)
        if (name == v) return true;
    return false;
}

}  // namespace

const char* TriggerEventName(TriggerRunner::Event e) {
    switch (e) {
        case TriggerRunner::Event::kBeginTurn: return "Player::OnBeginTurn";
        case TriggerRunner::Event::kEndTurn: return "Player::OnEndTurn";
        case TriggerRunner::Event::kRoundChange: return "System::OnRoundChange";
        case TriggerRunner::Event::kAttack: return "Unit::OnAttack";
        case TriggerRunner::Event::kUnitDestroy: return "Unit::OnDestroy";
        case TriggerRunner::Event::kUnitMove: return "Unit::OnMove";
        case TriggerRunner::Event::kUnitPreMove: return "Player::OnUnitPreMove";
        case TriggerRunner::Event::kRegionEnter: return "Unit::OnRegionEnter";
        case TriggerRunner::Event::kCaptureStart:
            return "Property::OnCaptureStart";
        case TriggerRunner::Event::kCaptureCompleted:
            return "Property::OnCaptureCompleted";
        case TriggerRunner::Event::kUnitLoad: return "Unit::OnLoad";
        case TriggerRunner::Event::kUnitUnload: return "Unit::OnUnload";
        case TriggerRunner::Event::kUnitJoin: return "Unit::OnJoin";
        case TriggerRunner::Event::kUnitWait: return "Unit::OnWait";
        case TriggerRunner::Event::kUnitSupply: return "Unit::OnSupply";
        case TriggerRunner::Event::kUnitAmbush: return "Unit::OnAmbush";
        case TriggerRunner::Event::kUnitSelect: return "Unit::OnSelect";
        case TriggerRunner::Event::kPropertySelect: return "Property::OnSelect";
        case TriggerRunner::Event::kPropertyBuild:
            return "Property::OnUnitBuild";
        case TriggerRunner::Event::kHealthChange:
            return "Special::OnHealthChange";
        case TriggerRunner::Event::kDefeat: return "Player::OnDefeat";
        case TriggerRunner::Event::kVictory: return "Player::OnVictory";
        case TriggerRunner::Event::kPerkUse: return "Player::OnPerkUse";
        case TriggerRunner::Event::kBattleBegin: return "System::OnBattleBegin";
        case TriggerRunner::Event::kBattleEnd: return "System::OnBattleEnd";
    }
    return "";
}

void TriggerRunner::Load(const NdLevel& level) {
    m_Level = &level;
    m_Triggers.clear();
    m_Dialogue.clear();
    m_Variables.clear();
    m_Unhandled.clear();
    m_Winners = 0;
    m_TurnLimit = 0;
    m_FocusX = m_FocusY = -1;
    int bad = 0;
    for (const NdLevel::Trigger& src : level.Triggers()) {
        Trigger t;
        t.ID = src.ID;
        auto parse = [&bad](const std::vector<std::string>& in,
                            std::vector<ScriptCall>& out) {
            for (const std::string& line : in) {
                ScriptCall c;
                if (ParseScriptCall(line, c)) out.push_back(std::move(c));
                else ++bad;
            }
        };
        parse(src.Events, t.Events);
        parse(src.Conditions, t.Conditions);
        parse(src.Actions, t.Actions);
        m_Triggers.push_back(std::move(t));
    }
    // Boolean variables start at whatever the editor wrote beside them.
    for (const NdLevel::Param& p : level.Params()) {
        const NdLevel::Variable* v = level.ScriptVariable(int(p.Target));
        if (v && v->Kind == NdLevel::Variable::kBoolean)
            m_Variables[int(p.Target)] = v->Flag ? 1 : 0;
    }
    if (bad)
        LogError("triggers: %zu scripts, some lines did not parse\n",
                 m_Triggers.size());
    else
        LogDebug("triggers: %zu scripts\n", m_Triggers.size());
}

void TriggerRunner::Note(const std::string& verb) {
    if (std::find(m_Unhandled.begin(), m_Unhandled.end(), verb) !=
        m_Unhandled.end())
        return;
    m_Unhandled.push_back(verb);
    LogDebug("triggers: '%s' %s\n", verb.c_str(),
             IsKnownUnimplemented(verb) ? "recognised, does nothing"
                                        : "not implemented");
}

int TriggerRunner::Variable(int id) const {
    const auto it = m_Variables.find(id);
    return it == m_Variables.end() ? 0 : it->second;
}

// --- argument resolution ----------------------------------------------------

// 0x100c7c20. Every player argument is a seat bitmask, one-based, and the
// wildcards are all-ones rather than a special case.
uint32_t TriggerRunner::PlayerMask(const ScriptArg& arg,
                                   const BattleField& field) const {
    const PlayerRef ref = PlayerRef::Parse(arg.Text);
    const auto nth = [&field](bool computer, int n) -> uint32_t {
        int seen = 0;
        for (int i = 1; i <= BattleField::kMaxPlayers; ++i) {
            const BattleField::Player& p = field.Players()[std::size_t(i)];
            if (!p.Present || p.Computer != computer) continue;
            if (++seen == n) return 1u << i;
        }
        return 0;
    };
    const auto all = [&field](bool computer) -> uint32_t {
        uint32_t m = 0;
        for (int i = 1; i <= BattleField::kMaxPlayers; ++i) {
            const BattleField::Player& p = field.Players()[std::size_t(i)];
            if (p.Present && p.Computer == computer) m |= 1u << i;
        }
        return m;
    };
    switch (ref.Kind) {
        case PlayerRef::kAbsolute:
            return ref.Index >= 0 && ref.Index < 32 ? 1u << ref.Index : 0;
        case PlayerRef::kHuman: return nth(false, ref.Index);
        case PlayerRef::kComputer: return nth(true, ref.Index);
        case PlayerRef::kAnyHuman: return all(false);
        case PlayerRef::kAnyComputer: return all(true);
        // "Current player" (token 0x4e) falls through 0x100c7c20's switch to
        // its default and comes back as -1, the same as "Any player". Kept as
        // the binary has it; no shipped script uses it in an argument slot.
        case PlayerRef::kCurrent:
        case PlayerRef::kAny:
        default:
            return 0xffffffffu;
    }
}

// 0x100c7b48. A preset unit token is either one of the three baked category
// masks or a single type; a `variable(N)` is the named unit's own type.
uint32_t TriggerRunner::UnitTypeMask(const ScriptArg& arg,
                                     const BattleField& field) const {
    if (arg.IsVariable()) {
        const int i = UnitByVariable(arg.Number(), field);
        const BattleField::Unit* u = field.UnitByIndex(i);
        return u ? 1u << u->Type : 0u;
    }
    const std::string& n = arg.Text;
    if (n.empty() || n == "Any unit") return 0xffffffffu;
    if (n == "Land unit") return BattleField::kLandUnitMask;
    if (n == "Sea unit") return BattleField::kSeaUnitMask;
    if (n == "Deep sea unit") return BattleField::kDeepSeaUnitMask;
    // "Rowing-boat-Swordmen" is the editor's own spelling of
    // "Rowing-boat-Swordsman"; the engine registers both against one id.
    const int id = BattleData::UnitId(
        n == "Rowing-boat-Swordmen" ? "Rowing-boat-Swordsman" : n);
    return id >= 0 && id < 32 ? 1u << id : 0u;
}

uint32_t TriggerRunner::PropertyTypeMask(const ScriptArg& arg) const {
    const std::string& n = arg.Text;
    if (n.empty() || n == "Any property") return 0xffffffffu;
    const int id = BattleData::PropertyId(n);
    return id >= 0 && id < 32 ? 1u << id : 0u;
}

// 0x100c8018: a preset matches by type mask, a handle by object id.
bool TriggerRunner::UnitMatches(const ScriptArg& arg, int unitIndex,
                                const BattleField& field) const {
    const BattleField::Unit* u = field.UnitByIndex(unitIndex);
    if (!u) return false;
    if (arg.IsVariable()) return u->ID == arg.Number();
    return (UnitTypeMask(arg, field) >> u->Type) & 1u;
}

bool TriggerRunner::PropertyMatches(const ScriptArg& arg, int propertyIndex,
                                    const BattleField& field) const {
    if (propertyIndex < 0 ||
        propertyIndex >= int(field.Properties().size()))
        return false;
    const BattleField::Property& p =
        field.Properties()[std::size_t(propertyIndex)];
    if (arg.IsVariable()) return p.ID == arg.Number();
    return (PropertyTypeMask(arg) >> p.Type) & 1u;
}

int TriggerRunner::UnitByVariable(int id, const BattleField& field) const {
    for (int i = 0; i < int(field.Units().size()); ++i)
        if (field.Units()[std::size_t(i)].ID == id) return i;
    return -1;
}

int TriggerRunner::PropertyByVariable(int id, const BattleField& field) const {
    for (int i = 0; i < int(field.Properties().size()); ++i)
        if (field.Properties()[std::size_t(i)].ID == id) return i;
    return -1;
}

// 0x100c7e24. "Any region" is the whole board; a handle is the editor's box.
TriggerRunner::Box TriggerRunner::Region(const ScriptArg& arg,
                                         const BattleField& field) const {
    Box b;
    if (arg.IsVariable()) {
        const NdLevel::Variable* v =
            m_Level ? m_Level->ScriptVariable(arg.Number()) : nullptr;
        if (v && v->Kind == NdLevel::Variable::kRegion) {
            b.X1 = std::min(v->X1, v->X2);
            b.Y1 = std::min(v->Y1, v->Y2);
            b.X2 = std::max(v->X1, v->X2);
            b.Y2 = std::max(v->Y1, v->Y2);
            return b;
        }
        // A handle the level did not describe would silently make every
        // region test fail, which is exactly how a mission stops being
        // winnable. Say so once rather than answering "empty".
        const_cast<TriggerRunner*>(this)->Note("region variable " +
                                               arg.Text + " unresolved");
        return b;
    }
    if (arg.Text == "Any region" || arg.Text.empty()) {
        b.X2 = field.Width() - 1;
        b.Y2 = field.Height() - 1;
    }
    return b;
}

// 0x100cb124. A point reads the cell's hit points -- which is what a
// breakable wall carries -- and a handle reads the named property's own.
int TriggerRunner::HealthOf(const ScriptArg& arg,
                            const BattleField& field) const {
    // A health reading is about a *square* either way. The editor writes a
    // point beside every handle, so `variable(48)` -- SP13's castle -- and
    // `const(PointType("6,5"))` -- SP6's wall -- both come down to a cell, and
    // the field answers with whatever is standing on it.
    if (arg.IsVariable()) {
        const NdLevel::Variable* v =
            m_Level ? m_Level->ScriptVariable(arg.Number()) : nullptr;
        if (v && v->HasPoint && field.InBounds(v->X, v->Y))
            return field.ObstacleHealth(v->X, v->Y);
        const int i = PropertyByVariable(arg.Number(), field);
        if (i < 0) return 0;
        return field.Properties()[std::size_t(i)].HP;
    }
    int x = 0, y = 0;
    if (std::sscanf(arg.Text.c_str(), "%d,%d", &x, &y) != 2) return 0;
    if (!field.InBounds(x, y)) return 0;
    return field.ObstacleHealth(x, y);
}

// --- evaluation -------------------------------------------------------------

TriggerRunner::Value TriggerRunner::Evaluate(const ScriptArg& arg,
                                             const BattleField& field) const {
    Value v;
    if (arg.IsCall()) return Evaluate(*arg.Call, field);
    if (arg.IsVariable()) {
        v.Kind = Value::kHandle;
        v.I = arg.Number();
        return v;
    }
    if (arg.Type == "PlayerType") {
        v.Kind = Value::kEnum;
        v.Mask = PlayerMask(arg, field);
        return v;
    }
    if (arg.Type == "BooleanType") {
        v.Kind = Value::kBool;
        v.I = (arg.Text == "true" || arg.Text == "1") ? 1 : 0;
        return v;
    }
    v.I = arg.Number();
    return v;
}

TriggerRunner::Value TriggerRunner::Evaluate(const ScriptCall& call,
                                             const BattleField& field) const {
    Value out;
    const std::string verb = call.Verb();
    if (verb == "getTurn") {
        out.I = field.Turn();
        return out;
    }
    if (verb == "getRound") {
        out.I = field.Round();
        return out;
    }
    if (verb == "getNumberOfUnits") {
        // (unit, player, region). Every one of those three filters decides a
        // campaign objective somewhere: SP2 counts each rowing-boat variant
        // on its own, SP5 counts men inside a beachhead.
        const ScriptArg* ua = Arg(call, 0);
        const ScriptArg* pa = Arg(call, 1);
        const ScriptArg* ra = Arg(call, 2);
        const uint32_t players = pa ? PlayerMask(*pa, field) : 0xffffffffu;
        const Box box = ra ? Region(*ra, field)
                           : Box{0, 0, field.Width() - 1, field.Height() - 1};
        int n = 0;
        for (int i = 0; i < int(field.Units().size()); ++i) {
            const BattleField::Unit& u = field.Units()[std::size_t(i)];
            if (!u.Alive) continue;
            if (u.Owner < 0 || u.Owner > 31) continue;
            if (((players >> u.Owner) & 1u) == 0) continue;
            if (!box.Contains(u.X, u.Y)) continue;
            if (ua && !UnitMatches(*ua, i, field)) continue;
            ++n;
        }
        out.I = n;
        return out;
    }
    if (verb == "getNumberOfProperties") {
        const ScriptArg* qa = Arg(call, 0);
        const ScriptArg* pa = Arg(call, 1);
        const ScriptArg* ra = Arg(call, 2);
        const uint32_t players = pa ? PlayerMask(*pa, field) : 0xffffffffu;
        const Box box = ra ? Region(*ra, field)
                           : Box{0, 0, field.Width() - 1, field.Height() - 1};
        int n = 0;
        for (int i = 0; i < int(field.Properties().size()); ++i) {
            const BattleField::Property& p =
                field.Properties()[std::size_t(i)];
            if (p.Owner < 0 || p.Owner > 31) continue;
            if (((players >> p.Owner) & 1u) == 0) continue;
            if (!box.Contains(p.X, p.Y)) continue;
            if (qa && !PropertyMatches(*qa, i, field)) continue;
            ++n;
        }
        out.I = n;
        return out;
    }
    if (verb == "getOwnerOfProperty") {
        // 0x100c87e0 answers with the owner's *token* (seat + 0x3f) and marks
        // the result enum-typed, so the `equal` beside it is an overlap test.
        out.Kind = Value::kEnum;
        out.Mask = 0;
        if (const ScriptArg* qa = Arg(call, 0)) {
            for (int i = 0; i < int(field.Properties().size()); ++i) {
                if (!PropertyMatches(*qa, i, field)) continue;
                const int owner = field.Properties()[std::size_t(i)].Owner;
                if (owner >= 0 && owner < 32) out.Mask |= 1u << owner;
                break;
            }
        }
        return out;
    }
    if (verb == "getHealthOfSpecial") {
        if (const ScriptArg* a = Arg(call, 0)) out.I = HealthOf(*a, field);
        return out;
    }
    if (verb == "getAmountOfGold") {
        if (const ScriptArg* pa = Arg(call, 0)) {
            const uint32_t m = PlayerMask(*pa, field);
            for (int i = 1; i <= BattleField::kMaxPlayers; ++i)
                if ((m >> i) & 1u) {
                    out.I = field.Players()[std::size_t(i)].Cash;
                    break;
                }
        }
        return out;
    }
    if (verb == "getAmountOfPerkPower") {
        if (const ScriptArg* pa = Arg(call, 0)) {
            const uint32_t m = PlayerMask(*pa, field);
            for (int i = 1; i <= BattleField::kMaxPlayers; ++i)
                if ((m >> i) & 1u) {
                    out.I = field.Players()[std::size_t(i)].PerkPoints;
                    break;
                }
        }
        return out;
    }
    if (verb == "getVariable" || verb == "variable") {
        if (const ScriptArg* a = Arg(call, 0)) out.I = Variable(a->Number());
        return out;
    }
    if (verb == "getX" || verb == "getY") {
        if (const ScriptArg* a = Arg(call, 0)) {
            int x = 0, y = 0;
            if (std::sscanf(a->Text.c_str(), "%d,%d", &x, &y) == 2)
                out.I = verb == "getX" ? x : y;
        }
        return out;
    }
    if (call.Args.size() >= 2) {
        const int a = Evaluate(call.Args[0], field).I;
        const int b = Evaluate(call.Args[1], field).I;
        if (verb == "add") { out.I = a + b; return out; }
        if (verb == "subtract") { out.I = a - b; return out; }
        if (verb == "multiply") { out.I = a * b; return out; }
        if (verb == "divide") { out.I = b ? a / b : 0; return out; }
        if (verb == "mod") { out.I = b ? a % b : 0; return out; }
        if (verb == "max") { out.I = a > b ? a : b; return out; }
        if (verb == "min") { out.I = a < b ? a : b; return out; }
        if (verb == "getManhattanDistance") { out.I = 0; return out; }
    }
    if (call.Args.size() == 1) {
        const int a = Evaluate(call.Args[0], field).I;
        if (verb == "abs") { out.I = a < 0 ? -a : a; return out; }
        if (verb == "sign") { out.I = a < 0 ? -1 : (a > 0 ? 1 : 0); return out; }
        // getRandomNumber(n) is used once, for a line of flavour dialogue.
        if (verb == "getRandomNumber") { out.I = 0; return out; }
    }
    const_cast<TriggerRunner*>(this)->Note(call.Name);
    return out;
}

// 0x100f0e48. Three shapes, and only the last is arithmetic.
bool TriggerRunner::Compare(const std::string& verb, const Value& a,
                            const Value& b) const {
    if (a.Kind == Value::kHandle && b.Kind == Value::kBool)
        return (Variable(a.I) != 0) == (b.I != 0);
    if (a.Kind == Value::kEnum || b.Kind == Value::kEnum) {
        // Only equality is defined on a pair of masks, and it is an overlap
        // test: everything else returns false.
        if (verb != "equal") return false;
        const uint32_t ma = a.Kind == Value::kEnum ? a.Mask : uint32_t(a.I);
        const uint32_t mb = b.Kind == Value::kEnum ? b.Mask : uint32_t(b.I);
        return (ma & mb) != 0;
    }
    const int x = a.Kind == Value::kHandle ? Variable(a.I) : a.I;
    const int y = b.Kind == Value::kHandle ? Variable(b.I) : b.I;
    if (verb == "equal") return x == y;
    if (verb == "notequal") return x != y;
    if (verb == "less") return x < y;
    if (verb == "greater") return x > y;
    if (verb == "lessorequal") return x <= y;
    if (verb == "greaterorequal") return x >= y;
    return true;
}

bool TriggerRunner::ConditionsHold(const Trigger& t,
                                   const BattleField& field) const {
    for (const ScriptCall& c : t.Conditions) {
        const std::string verb = c.Verb();
        if (verb == "or" || verb == "and") {
            const bool wantAll = verb == "and";
            bool any = false, all = true;
            for (const ScriptArg& a : c.Args) {
                if (!a.IsCall()) continue;
                // A nested comparison, not a value: recurse through one
                // synthetic trigger so the same code path handles it.
                Trigger sub;
                sub.Conditions.push_back(*a.Call);
                const bool ok = ConditionsHold(sub, field);
                any = any || ok;
                all = all && ok;
            }
            if (wantAll ? !all : !any) return false;
            continue;
        }
        if (c.Args.size() < 2) continue;
        if (!Compare(verb, Evaluate(c.Args[0], field),
                     Evaluate(c.Args[1], field)))
            return false;
    }
    return true;
}

// --- event matching ---------------------------------------------------------

bool TriggerRunner::Matches(const Trigger& t, Event event, const Context& ctx,
                            const BattleField& field) const {
    const std::string want = TriggerEventName(event);
    for (const ScriptCall& e : t.Events) {
        if (e.Name != want) continue;
        const auto inRegion = [&](std::size_t i, int x, int y) {
            const ScriptArg* a = Arg(e, i);
            return !a || Region(*a, field).Contains(x, y);
        };
        const auto ownerIn = [&](std::size_t i, int seat) {
            const ScriptArg* a = Arg(e, i);
            if (!a) return true;
            if (seat < 0 || seat > 31) return false;
            return ((PlayerMask(*a, field) >> seat) & 1u) != 0;
        };
        // A filter only vetoes when there is something to test it against.
        // The engine's handlers always have both objects in hand; a caller
        // that raises an event without one (a test, or an action the port
        // does not carry a second unit through) should not be silenced by an
        // "Any unit" argument.
        const auto unitOk = [&](std::size_t i, int index) {
            const ScriptArg* a = Arg(e, i);
            return !a || index < 0 || UnitMatches(*a, index, field);
        };
        const auto propertyOk = [&](std::size_t i, int index) {
            const ScriptArg* a = Arg(e, i);
            return !a || index < 0 || PropertyMatches(*a, index, field);
        };
        switch (event) {
            case Event::kBeginTurn:
            case Event::kEndTurn:
            case Event::kPerkUse:
                if (ownerIn(0, ctx.Player)) return true;
                break;
            case Event::kDefeat:
            case Event::kVictory: {
                // 0x100c8d58: the event carries a mask, and a trigger fires
                // when its own mask overlaps.
                const ScriptArg* a = Arg(e, 0);
                const uint32_t wantMask = a ? PlayerMask(*a, field)
                                             : 0xffffffffu;
                uint32_t got = ctx.Mask;
                if (got == 0 && ctx.Player >= 0 && ctx.Player < 32)
                    got = 1u << ctx.Player;
                if ((wantMask & got) != 0) return true;
                break;
            }
            case Event::kAttack:
                // (attacker unit, attacker player, target unit, target player,
                // region).
                if (e.Args.size() >= 4) {
                    if (ownerIn(1, ctx.Player) && ownerIn(3, ctx.Other) &&
                        unitOk(0, ctx.Unit) && unitOk(2, ctx.Target) &&
                        inRegion(4, ctx.X, ctx.Y))
                        return true;
                } else {
                    return true;
                }
                break;
            case Event::kUnitDestroy:
                if (unitOk(0, ctx.Unit) && ownerIn(1, ctx.Player))
                    return true;
                break;
            // (unit, player, region), the commonest shape of all.
            case Event::kUnitMove:
            case Event::kUnitWait:
            case Event::kUnitSelect:
            case Event::kUnitJoin:
            case Event::kUnitAmbush:
                if (unitOk(0, ctx.Unit) && ownerIn(1, ctx.Player) &&
                    inRegion(2, ctx.X, ctx.Y))
                    return true;
                break;
            case Event::kUnitPreMove:
                // (player, unit, region) -- the player comes first here.
                if (ownerIn(0, ctx.Player) && unitOk(1, ctx.Unit) &&
                    inRegion(2, ctx.X, ctx.Y))
                    return true;
                break;
            case Event::kRegionEnter: {
                // 0x100c8fa4: out of the box at the start of the route and
                // inside it at the end. Sitting still in a region does not
                // fire it, and neither does crossing one.
                const ScriptArg* ra = Arg(e, 2);
                if (!unitOk(0, ctx.Unit)) break;
                if (!ownerIn(1, ctx.Player)) break;
                if (!ra) break;
                const Box box = Region(*ra, field);
                if (!box.Contains(ctx.FromX, ctx.FromY) &&
                    box.Contains(ctx.X, ctx.Y))
                    return true;
                break;
            }
            case Event::kUnitSupply:
                // (supplier, player, supplied, region)
                if (unitOk(0, ctx.Unit) && ownerIn(1, ctx.Player) &&
                    unitOk(2, ctx.Target) && inRegion(3, ctx.X, ctx.Y))
                    return true;
                break;
            case Event::kUnitLoad:
                // (cargo, player, transport, region); the player is the
                // cargo's owner (0x100c94bc).
                if (unitOk(0, ctx.Unit) && ownerIn(1, ctx.Player) &&
                    unitOk(2, ctx.Target) && inRegion(3, ctx.X, ctx.Y))
                    return true;
                break;
            case Event::kUnitUnload:
                // (cargo, player, transport, region), but 0x100c9604 reads the
                // owner off the *transport*: ctx.unit is the transport and
                // ctx.target the passenger.
                if (unitOk(0, ctx.Target) && ownerIn(1, ctx.Player) &&
                    unitOk(2, ctx.Unit) && inRegion(3, ctx.X, ctx.Y))
                    return true;
                break;
            case Event::kCaptureStart:
            case Event::kCaptureCompleted:
            case Event::kPropertyBuild:
                // (property, its owner before, unit, that unit's owner, region)
                if (propertyOk(0, ctx.Property) && ownerIn(1, ctx.Player) &&
                    unitOk(2, ctx.Unit) && ownerIn(3, ctx.Other) &&
                    inRegion(4, ctx.X, ctx.Y))
                    return true;
                break;
            case Event::kPropertySelect:
                if (propertyOk(0, ctx.Property) && ownerIn(1, ctx.Player) &&
                    inRegion(2, ctx.X, ctx.Y))
                    return true;
                break;
            case Event::kHealthChange: {
                // The argument names a square and the event says which square
                // changed. A handle carries its point beside it, so both forms
                // reduce to the same comparison; the property number is only
                // the fallback for a handle the editor left without one.
                const ScriptArg* a = Arg(e, 0);
                if (!a) return true;
                if (a->IsVariable()) {
                    const NdLevel::Variable* v =
                        m_Level ? m_Level->ScriptVariable(a->Number()) : nullptr;
                    if (v && v->HasPoint) {
                        if (v->X == ctx.X && v->Y == ctx.Y) return true;
                        break;
                    }
                    const int i = PropertyByVariable(a->Number(), field);
                    if (i >= 0 && i == ctx.Property) return true;
                    break;
                }
                int x = 0, y = 0;
                if (std::sscanf(a->Text.c_str(), "%d,%d", &x, &y) == 2 &&
                    x == ctx.X && y == ctx.Y)
                    return true;
                break;
            }
            default:
                return true;
        }
    }
    return false;
}

// --- actions ----------------------------------------------------------------

void TriggerRunner::RunActions(Trigger& t, BattleField& field) {
    for (const ScriptCall& a : t.Actions) {
        const std::string verb = a.Verb();
        if (verb == "ShowDialog") {
            DialogueLine line;
            if (a.Args.size() >= 1) line.TextID = a.Args[0].Number();
            if (a.Args.size() >= 2) line.SpeakerID = a.Args[1].Number();
            if (a.Args.size() >= 3) line.Flags = a.Args[2].Number();
            // A PlaySound immediately before it belongs with this line.
            if (!m_PendingSound.empty()) {
                line.Sound = m_PendingSound;
                m_PendingSound.clear();
            }
            m_Dialogue.push_back(std::move(line));
        } else if (verb == "PlaySound") {
            if (!a.Args.empty()) m_PendingSound = a.Args[0].Text;
        } else if (verb == "DisableTrigger" || verb == "EnableTrigger") {
            const bool on = verb == "EnableTrigger";
            const ScriptArg* who = Arg(a, 0);
            if (!who || who->Text == "Current trigger") {
                t.Enabled = on;
            } else if (who->Text == "All triggers") {
                for (Trigger& other : m_Triggers) other.Enabled = on;
            } else {
                // A handle names the trigger whose id it is.
                const int id = who->Number();
                for (Trigger& other : m_Triggers)
                    if (other.ID == id) other.Enabled = on;
            }
        } else if (verb == "Win" || verb == "Lose") {
            // 0x100cad50 / 0x100cadd4: both write a winners mask into the
            // battle result, and Lose writes the complement.
            const uint32_t m =
                a.Args.empty() ? 0u : PlayerMask(a.Args[0], field);
            m_Winners |= (verb == "Win" ? m : ~m) & kSeatMask;
        } else if (verb == "SetVariable") {
            if (a.Args.size() >= 2 && a.Args[0].IsVariable())
                m_Variables[a.Args[0].Number()] = Evaluate(a.Args[1], field).I;
        } else if (verb == "SetTurnLimit") {
            if (!a.Args.empty()) m_TurnLimit = Evaluate(a.Args[0], field).I;
        } else if (verb == "SetAmountOfGold" || verb == "SetPerkPower") {
            if (a.Args.size() >= 2) {
                const int v = Evaluate(a.Args[1], field).I;
                const uint32_t m = PlayerMask(a.Args[0], field);
                for (int i = 1; i <= BattleField::kMaxPlayers; ++i) {
                    if (((m >> i) & 1u) == 0) continue;
                    if (verb == "SetAmountOfGold") field.SetCash(i, v);
                }
            }
        } else if (verb == "CreateSingleUnit") {
            // (type, where, owner). The `where` is a handle on an editor
            // object, so it is the point the level wrote beside it.
            if (a.Args.size() >= 3 && m_Level) {
                const int type = BattleData::UnitId(a.Args[0].Text);
                const NdLevel::Variable* v =
                    a.Args[1].IsVariable()
                        ? m_Level->ScriptVariable(a.Args[1].Number())
                        : nullptr;
                const uint32_t m = PlayerMask(a.Args[2], field);
                int owner = 0;
                for (int i = 1; i <= BattleField::kMaxPlayers; ++i)
                    if ((m >> i) & 1u) { owner = i; break; }
                if (type >= 0 && v && v->HasPoint && owner)
                    field.CreateUnit(type, owner, v->X, v->Y);
            }
        } else if (verb == "damageUnit") {
            // (type, player, unit handle, amount)
            if (a.Args.size() >= 4) {
                const int amount = Evaluate(a.Args[3], field).I;
                const int i = a.Args[2].IsVariable()
                                  ? UnitByVariable(a.Args[2].Number(), field)
                                  : -1;
                if (i >= 0) field.DamageUnit(i, amount);
            }
        } else if (verb == "CameraFocus") {
            if (!a.Args.empty())
                std::sscanf(a.Args[0].Text.c_str(), "%d,%d", &m_FocusX,
                            &m_FocusY);
        } else if (verb == "EnableFogOfWar" || verb == "DisableFogOfWar") {
            field.SetFog(verb == "EnableFogOfWar");
        } else if (verb == "Delay" || verb == "UnselectUnit" ||
                   verb == "UnselectProperty" || verb == "CancelMove" ||
                   verb == "EnableMenuItem" || verb == "SetAttribute") {
            // Presentation and input plumbing with no separate state in the
            // port: the popup is dismissed by the dialogue queue anyway, and
            // the battle menu has no items a script needs to grey out.
        } else if (IsKnownUnimplemented(a.Name)) {
            Note(a.Name);
        } else {
            Note(a.Name);
        }
    }
}

void TriggerRunner::Fire(Event event, const Context& ctx, BattleField& field) {
    for (Trigger& t : m_Triggers) {
        if (!t.Enabled) continue;
        if (!Matches(t, event, ctx, field)) continue;
        if (!ConditionsHold(t, field)) continue;
        RunActions(t, field);
    }
    m_PendingSound.clear();
}

void TriggerRunner::PopDialogue() {
    if (!m_Dialogue.empty()) m_Dialogue.erase(m_Dialogue.begin());
}

TriggerRunner::State TriggerRunner::Save() const {
    State s;
    s.Enabled.reserve(m_Triggers.size());
    for (const Trigger& t : m_Triggers) s.Enabled.push_back(t.Enabled ? 1 : 0);
    s.Variables.assign(m_Variables.begin(), m_Variables.end());
    s.Winners = m_Winners;
    s.TurnLimit = m_TurnLimit;
    return s;
}

bool TriggerRunner::Restore(const State& s) {
    // One flag per trigger, in the order Load() read them. A different count
    // means a different level, and applying it would silently re-arm or
    // silence the wrong lines.
    if (s.Enabled.size() != m_Triggers.size()) return false;
    for (std::size_t i = 0; i < m_Triggers.size(); ++i)
        m_Triggers[i].Enabled = s.Enabled[i] != 0;
    m_Variables.clear();
    for (const auto& [id, value] : s.Variables) m_Variables[id] = value;
    m_Winners = s.Winners;
    m_TurnLimit = s.TurnLimit;
    m_Dialogue.clear();
    m_PendingSound.clear();
    m_FocusX = m_FocusY = -1;
    return true;
}

}  // namespace bb
