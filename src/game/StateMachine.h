// MenuStateMachine — port of the engine's screen-flow driver (main.dll).
//
// Original: constructed at 0x1008d448 (28-byte object), driven by the run loop
// at 0x1008f448, with the four transitions at 0x1008f3f8 / f358 / f38c / f3ac.
// Its layout was
//     +0x00 current state   +0x04 pending action   +0x08 pending next state
//     +0x0c stack of suspended states
// and the loop is:
//
//     m_current = initial;
//     do {
//         m_action = kQuit;                 // default if the state asks for nothing
//         log("MenuStateMachine: STACK SIZE %d", stack.size());
//         m_current->Run();                 // BLOCKS until the state wants out
//         switch (m_action) { ... }
//     } while (m_current);
//
// The important part is that Run() blocks: each state owns its own loop and
// yields through Host::Sleep(). Transitions are *requests* recorded during
// Run() and applied only after it returns, so a state can safely ask for a
// transition and then finish its frame -- it is never deleted underneath
// itself. Anything that changes here changes the game's screen flow, so the
// four cases below mirror the original exactly, including "a state that returns
// without asking for anything ends the game" (case 0 clears the stack).
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace bb {

class StateMachine;

// Base for every screen. In the original the vptr sits at object offset +0xc
// and Run() is vtable slot 3; Name() (slot 6) exists purely for the state
// tracing the engine logs on every transition, and is kept for the same reason.
class GameState {
public:
    virtual ~GameState() = default;

    // Runs the screen to completion. Call one of the StateMachine transition
    // requests before returning; returning without one quits the game.
    virtual void Run(StateMachine& sm) = 0;

    virtual const char* Name() const = 0;
};

class StateMachine {
public:
    StateMachine() = default;
    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;

    // Drive `initial` and everything it transitions to, until a state quits or
    // Back() empties the stack. Returns when the flow is finished.
    void Run(std::unique_ptr<GameState> initial);

    // --- transition requests, callable from inside GameState::Run() ---------

    // Replace the current state; the current one is destroyed (original: action
    // 1, requested by FUN_1008f4f8). This is the ordinary "go to that screen".
    void Change(std::unique_ptr<GameState> next);

    // Suspend the current state on the stack and switch to `next` (action 2).
    // Back() later resumes it -- how submenus and pause screens work.
    void Push(std::unique_ptr<GameState> next);

    // Destroy the current state and resume the one below (action 3). With an
    // empty stack the flow ends.
    void Back();

    // Unwind to the state at the bottom of the stack -- the main menu, which is
    // what IntroState changed into before anything was pushed on top of it.
    // Everything between here and there is destroyed. This is what "End current
    // game" asks for: the battle and the screens that led to it are gone, and
    // the player is back where a new game starts from. With an empty stack
    // there is nothing to unwind to, so it ends the flow like Back() does.
    //
    // Not one of the original's four actions -- the engine unwinds the same way
    // by returning a result code up through each screen's own loop.
    void Home();

    // End the flow: destroy the current state and discard the stack (action 0,
    // also the default when a state requests nothing).
    void Quit();

    std::size_t StackSize() const { return m_Stack.size(); }
    const GameState* Current() const { return m_Current.get(); }

    // Every state the machine entered, in order. Trace-only, mirroring the
    // engine's own transition logging; tests assert on it. Copied rather than
    // held by pointer so it stays valid after the states are destroyed.
    const std::vector<std::string>& Trace() const { return m_Trace; }

private:
    enum class Action { kQuit = 0, kChange = 1, kPush = 2, kBack = 3, kHome = 4 };

    Action m_Action = Action::kQuit;
    std::unique_ptr<GameState> m_Current;
    std::unique_ptr<GameState> m_Next;
    std::vector<std::unique_ptr<GameState>> m_Stack;
    std::vector<std::string> m_Trace;
};

}  // namespace bb
