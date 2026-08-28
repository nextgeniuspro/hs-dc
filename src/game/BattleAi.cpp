#include "game/BattleAi.h"

#include <algorithm>
#include <climits>
#include <cstdio>

#include "game/BattleField.h"

namespace bb {
namespace {

int Manhattan(int ax, int ay, int bx, int by) {
    return std::abs(ax - bx) + std::abs(ay - by);
}

// How much a unit is worth killing, and how much it hurts to lose one.
int Worth(const BattleField& f, const BattleField::Unit& u) {
    return f.Data().Unit(u.Type).Cost * BattleField::HealthBar(u.HP) / 10;
}

// Weights. These are the port's, not the original's -- see the header.
constexpr int kCaptureBonus = 400;
constexpr int kHqCaptureBonus = 900;
constexpr int kKillBonus = 250;
constexpr int kApproachWeight = 6;
constexpr int kSupplyScore = 150;
constexpr int kUnloadScore = 500;
// Below everything that advances the battle, above wandering. A fence is only
// ever in the way, so knocking it down is worth a turn when there is nothing
// else to do with one -- and never worth crossing the map for.
constexpr int kSmashScore = 60;

}  // namespace

bool BattleAi::PlanUnit(BattleField& field, int unit, Plan& out) const {
    const BattleField::Unit* u = field.UnitByIndex(unit);
    if (!u || !u->Alive || u->Done || u->Carrier >= 0) return false;
    const UnitAttrs& attrs = field.Data().Unit(u->Type);

    std::vector<int> reach;
    field.Reachable(unit, reach);

    out = Plan{};
    out.Unit = unit;
    out.MoveX = u->X;
    out.MoveY = u->Y;
    // Walking toward the objective scores negatively (it is minus the distance
    // left), so the floor has to be below every real score, not zero.
    out.Score = INT_MIN;

    // Where is the nearest thing worth walking toward? Capturable buildings
    // first for anything that can capture, otherwise the nearest enemy.
    int goalX = -1, goalY = -1, goalDist = 1 << 20;
    const bool canCapture = attrs.CaptureCapability != 0;
    if (canCapture) {
        for (const BattleField::Property& p : field.Properties()) {
            if (field.SameTeam(p.Owner, u->Owner)) continue;
            if ((attrs.CaptureCapability & (1u << p.Type)) == 0) continue;
            const int d = Manhattan(u->X, u->Y, p.X, p.Y);
            if (d < goalDist) {
                goalDist = d;
                goalX = p.X;
                goalY = p.Y;
            }
        }
    }
    if (goalX < 0) {
        for (const BattleField::Unit& e : field.Units()) {
            if (!e.Alive || field.SameTeam(e.Owner, u->Owner) ||
                e.Carrier >= 0)
                continue;
            const int d = Manhattan(u->X, u->Y, e.X, e.Y);
            if (d < goalDist) {
                goalDist = d;
                goalX = e.X;
                goalY = e.Y;
            }
        }
    }

    const int w = field.Width(), h = field.Height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (reach[std::size_t(y) * w + x] < 0) continue;

            // Closing on the objective is the baseline score, so a unit with
            // nothing to do still walks somewhere useful.
            int base = 0;
            if (goalX >= 0)
                base = -Manhattan(x, y, goalX, goalY) * kApproachWeight;

            // Capturing where it stands.
            const BattleField::Property* p = field.PropertyAt(x, y);
            if (p && !field.SameTeam(p->Owner, u->Owner) && u->Ammo > 0 &&
                (attrs.CaptureCapability & (1u << p->Type)) != 0) {
                const int bonus =
                    p->Type == kPropHeadquarters ? kHqCaptureBonus : kCaptureBonus;
                const int score = base + bonus +
                                  (field.Data().Property(p->Type).CanProduce ? 200 : 0);
                if (score > out.Score) {
                    out.Score = score;
                    out.MoveX = x;
                    out.MoveY = y;
                    out.Target = -1;
                    out.Capture = true;
                    out.Supply = false;
                    out.UnloadSlot = -1;
                    out.Smash = false;
                }
            }

            // Attacking from here. The field's own preview does the maths, so
            // the AI cannot disagree with what the player would be shown --
            // but it has to ask about a hypothetical position, so try each
            // enemy in range by hand.
            for (int t = 0; t < int(field.Units().size()); ++t) {
                const BattleField::Unit& e = field.Units()[std::size_t(t)];
                if (!e.Alive || field.SameTeam(e.Owner, u->Owner) ||
                    e.Carrier >= 0)
                    continue;
                const int dist = Manhattan(x, y, e.X, e.Y);
                if (dist < attrs.MinRange || dist > attrs.MaxRange) continue;
                if ((attrs.AttackCapability & (1u << e.Type)) == 0) continue;
                if (u->Ammo < 1) continue;
                // An indirect unit cannot fire after moving; the port treats
                // MinRange > 1 as indirect, which is what the data means.
                if (attrs.MinRange > 1 && (x != u->X || y != u->Y)) continue;

                const int score = base + kKillBonus + Worth(field, e) * 3;
                if (score > out.Score) {
                    out.Score = score;
                    out.MoveX = x;
                    out.MoveY = y;
                    out.Target = t;
                    out.Capture = false;
                    out.Supply = false;
                    out.UnloadSlot = -1;
                    out.Smash = false;
                }
            }

            // Knocking a fence down. Only from where the unit already is if
            // it is artillery, same as a shot at a unit, and only when the
            // square is genuinely in range from here.
            if (attrs.AttackCapability != 0 && attrs.BlastRadius == 0 &&
                u->Ammo > 0 && !(attrs.MinRange > 1 && (x != u->X || y != u->Y))) {
                for (int dy = -attrs.MaxRange; dy <= attrs.MaxRange; ++dy) {
                    for (int dx = -attrs.MaxRange; dx <= attrs.MaxRange; ++dx) {
                        const int dist = std::abs(dx) + std::abs(dy);
                        if (dist < attrs.MinRange || dist > attrs.MaxRange)
                            continue;
                        const int wx = x + dx, wy = y + dy;
                        if (!field.IsObstacle(wx, wy)) continue;
                        if (field.At(wx, wy).Unit >= 0) continue;
                        const int score = base + kSmashScore;
                        if (score > out.Score) {
                            out.Score = score;
                            out.MoveX = x;
                            out.MoveY = y;
                            out.Target = -1;
                            out.Capture = false;
                            out.Supply = false;
                            out.UnloadSlot = -1;
                            out.Smash = true;
                            out.SmashX = wx;
                            out.SmashY = wy;
                        }
                    }
                }
            }

            // Supplying, for the units that can.
            if (attrs.SupplyCapability != 0) {
                static const int dx[4] = {0, 1, 0, -1};
                static const int dy[4] = {-1, 0, 1, 0};
                for (int d = 0; d < 4; ++d) {
                    const BattleField::Unit* n = field.UnitAt(x + dx[d], y + dy[d]);
                    if (!n || !n->Alive || !field.SameTeam(n->Owner, u->Owner))
                        continue;
                    if ((attrs.SupplyCapability & (1u << n->Type)) == 0) continue;
                    const UnitAttrs& na = field.Data().Unit(n->Type);
                    if (n->Rations >= na.MaxRations && n->Ammo >= na.MaxAmmo)
                        continue;
                    const int score = base + kSupplyScore;
                    if (score > out.Score) {
                        out.Score = score;
                        out.MoveX = x;
                        out.MoveY = y;
                        out.Target = -1;
                        out.Capture = false;
                        out.Supply = true;
                        out.UnloadSlot = -1;
                        out.Smash = false;
                    }
                    break;
                }
            }

            // Putting a passenger ashore. Several maps start with soldiers
            // already in rowing boats, so a transport that never unloads is a
            // transport that never does anything.
            if (!u->Cargo.empty()) {
                static const int ux[4] = {0, 1, 0, -1};
                static const int uy[4] = {-1, 0, 1, 0};
                for (std::size_t slot = 0; slot < u->Cargo.size(); ++slot) {
                    const BattleField::Unit* c =
                        field.UnitByIndex(u->Cargo[slot]);
                    if (!c) continue;
                    for (int d = 0; d < 4; ++d) {
                        const int nx = x + ux[d], ny = y + uy[d];
                        if (!field.InBounds(nx, ny)) continue;
                        if (!field.CanEnter(c->Type, nx, ny)) continue;
                        if (field.At(nx, ny).Unit >= 0) continue;
                        const int score = base + kUnloadScore;
                        if (score > out.Score) {
                            out.Score = score;
                            out.MoveX = x;
                            out.MoveY = y;
                            out.Target = -1;
                            out.Capture = false;
                            out.Supply = false;
                            out.UnloadSlot = int(slot);
                            out.Smash = false;
                            out.UnloadX = nx;
                            out.UnloadY = ny;
                        }
                    }
                }
            }

            // Plain movement.
            if (base > out.Score) {
                out.Score = base;
                out.MoveX = x;
                out.MoveY = y;
                out.Target = -1;
                out.Capture = false;
                out.Supply = false;
                out.UnloadSlot = -1;
                out.Smash = false;
            }
        }
    }
    return out.Score > INT_MIN;
}

bool BattleAi::Produce(BattleField& field, int player) {
    for (int i = 0; i < int(field.Properties().size()); ++i) {
        const BattleField::Property& p = field.Properties()[std::size_t(i)];
        if (p.Owner != player) continue;
        std::vector<int> types;
        field.Producible(i, types);
        // The most expensive thing it can afford, but leave the cheap
        // capture-capable infantry in the mix so the AI keeps taking ground.
        int best = -1, bestCost = -1;
        for (int t : types) {
            if (!field.CanProduce(i, t)) continue;
            int cost = field.Data().Unit(t).Cost;
            if (field.Data().Unit(t).CaptureCapability != 0) cost += 15;
            if (cost > bestCost) {
                bestCost = cost;
                best = t;
            }
        }
        if (best < 0) continue;
        if (field.Produce(i, best) >= 0) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "builds %s",
                          BattleData::UnitName(best));
            m_Last = buf;
            return true;
        }
    }
    return false;
}

bool BattleAi::Step(BattleField& field, int player, const MoveWatcher& walked) {
    m_Last.clear();

    // Best plan across every unit still to act, so the AI does the most
    // valuable thing first rather than acting in array order.
    Plan best;
    best.Score = INT_MIN;
    for (int i = 0; i < int(field.Units().size()); ++i) {
        if (field.Units()[std::size_t(i)].Owner != player) continue;
        Plan plan;
        if (!PlanUnit(field, i, plan)) continue;
        if (plan.Score > best.Score) best = plan;
    }

    if (best.Unit >= 0) {
        const BattleField::Unit* u = field.UnitByIndex(best.Unit);
        const char* name = BattleData::UnitName(u->Type);
        char buf[128];
        if (best.MoveX != u->X || best.MoveY != u->Y) {
            // The route has to come off the field before the move, since after
            // it the unit is already standing at the far end.
            std::vector<BattleField::Step> route;
            if (walked) field.PathTo(best.Unit, best.MoveX, best.MoveY, route);
            field.MoveUnit(best.Unit, best.MoveX, best.MoveY);
            if (walked) walked(best.Unit, route);
        }
        if (best.Target >= 0 && field.CanAttack(best.Unit, best.Target)) {
            const BattleField::CombatResult r =
                field.Attack(best.Unit, best.Target);
            std::snprintf(buf, sizeof(buf), "%s attacks for %d", name, r.Damage);
        } else if (best.Smash &&
                   field.CanAttackObstacle(best.Unit, best.SmashX,
                                           best.SmashY)) {
            const BattleField::ObstacleResult r =
                field.AttackObstacle(best.Unit, best.SmashX, best.SmashY);
            std::snprintf(buf, sizeof(buf), "%s %s the obstacle", name,
                          r.Destroyed ? "breaks" : "hacks at");
        } else if (best.Capture && field.CanCapture(best.Unit)) {
            const bool done = field.Capture(best.Unit);
            std::snprintf(buf, sizeof(buf), "%s %s", name,
                          done ? "captures" : "is capturing");
        } else if (best.UnloadSlot >= 0 &&
                   field.Unload(best.Unit, best.UnloadSlot, best.UnloadX,
                                best.UnloadY)) {
            std::snprintf(buf, sizeof(buf), "%s unloads", name);
        } else if (best.Supply && field.CanSupply(best.Unit)) {
            field.Supply(best.Unit);
            std::snprintf(buf, sizeof(buf), "%s supplies", name);
        } else {
            field.Wait(best.Unit);
            std::snprintf(buf, sizeof(buf), "%s moves", name);
        }
        m_Last = buf;
        return true;
    }

    return Produce(field, player);
}

}  // namespace bb
