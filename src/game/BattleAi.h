// BattleAi — the computer player's turn.
//
// The original's AI is a scoring pass over every unit and every square it can
// reach (the `AI` and `WorldPathFinder` classes in RedLynx's assert strings);
// this is the same shape, written from the rules rather than transcribed,
// because the scoring weights are not recoverable from the binary without the
// kind of live tracing the port does not have yet. It plays the game properly
// -- captures, attacks the target it profits most from, keeps supply units
// working, builds what it can afford -- but it is the port's judgement, not
// RedLynx's, and it is marked as such.
//
// One call to Step() performs one visible action, so the screen can pace the
// computer's turn instead of resolving it between two frames.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "game/BattleField.h"

namespace bb {

class BattleAi {
public:
    // Told about a unit that has just walked, with the route it took. The move
    // is already committed when this runs, exactly as it is for the player's
    // own moves, so all the screen has to do is play the walk.
    using MoveWatcher =
        std::function<void(int unit, const std::vector<BattleField::Step>&)>;

    // Do one thing for `player`. Returns false when the turn is over.
    bool Step(BattleField& field, int player, const MoveWatcher& walked = {});

    // What the last Step did, for the log and the on-screen ticker.
    const std::string& LastAction() const { return m_Last; }

private:
    struct Plan {
        int Unit = -1;
        int MoveX = 0, MoveY = 0;
        int Target = -1;         // unit to attack, or -1
        bool Capture = false;
        bool Supply = false;
        int UnloadSlot = -1;    // passenger to put ashore
        int UnloadX = 0, UnloadY = 0;
        // An obstacle to knock down, and where it stands. Worth doing when
        // there is nothing better: several maps fence the two sides apart, and
        // an army that will not touch a fence sits behind it for the whole
        // battle.
        bool Smash = false;
        int SmashX = 0, SmashY = 0;
        int Score = 0;
    };

    bool PlanUnit(BattleField& field, int unit, Plan& out) const;
    bool Produce(BattleField& field, int player);

    std::string m_Last;
};

}  // namespace bb
