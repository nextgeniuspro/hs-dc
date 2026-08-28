#include "game/StateMachine.h"

namespace bb {

void StateMachine::Run(std::unique_ptr<GameState> initial) {
    m_Current = std::move(initial);
    m_Next.reset();
    if (m_Current) m_Trace.push_back(m_Current->Name());

    while (m_Current) {
        // A state that returns without requesting anything ends the flow --
        // that is the original's default, not an oversight.
        m_Action = Action::kQuit;

        m_Current->Run(*this);

        switch (m_Action) {
            case Action::kQuit:
                m_Current.reset();
                m_Stack.clear();
                break;

            case Action::kChange:
                m_Current = std::move(m_Next);
                break;

            case Action::kPush:
                m_Stack.push_back(std::move(m_Current));
                m_Current = std::move(m_Next);
                break;

            case Action::kBack:
                m_Current.reset();
                if (!m_Stack.empty()) {
                    m_Current = std::move(m_Stack.back());
                    m_Stack.pop_back();
                }
                break;

            case Action::kHome:
                m_Current.reset();
                if (!m_Stack.empty()) {
                    m_Current = std::move(m_Stack.front());
                    m_Stack.clear();
                }
                break;
        }
        m_Next.reset();
        if (m_Current) m_Trace.push_back(m_Current->Name());
    }
}

void StateMachine::Change(std::unique_ptr<GameState> next) {
    m_Action = Action::kChange;
    m_Next = std::move(next);
}

void StateMachine::Push(std::unique_ptr<GameState> next) {
    m_Action = Action::kPush;
    m_Next = std::move(next);
}

void StateMachine::Back() {
    m_Action = Action::kBack;
    m_Next.reset();
}

void StateMachine::Home() {
    m_Action = Action::kHome;
    m_Next.reset();
}

void StateMachine::Quit() {
    m_Action = Action::kQuit;
    m_Next.reset();
}

}  // namespace bb
