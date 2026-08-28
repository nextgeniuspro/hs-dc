// The first two screens of the game flow.
//
// The state machine's initial state is created at 0x1008a0b4 and its Run() is
// at 0x1007ffc8: it plays the "01-Intro" cutscene, then changes to the main
// menu (0x1008b764, built around Data\Menu\high_seize.tc -- the title logo; the
// game shipped as "High Seize" in some regions).
//
// The screens below the main menu live in MenuScreens.h.
#pragma once

#include "game/MenuScreen.h"
#include "game/StateMachine.h"

namespace bb {

class Cutscene;
struct GameContext;
struct Texture;

// The animation the intro state plays, named in the binary at 0x1011d1d4.
inline constexpr const char* kIntroCutscene = "01-Intro";

// Plays the intro cutscene, then hands over to the main menu.
class IntroState : public GameState {
public:
    // `preloaded` is the cutscene the boot sequence already read while the
    // title splash was up, which is where the original loads it. Passing
    // nullptr makes the state load it itself.
    explicit IntroState(GameContext& ctx, Cutscene* preloaded = nullptr)
        : m_Ctx(ctx), m_Preloaded(preloaded) {}
    void Run(StateMachine& sm) override;
    const char* Name() const override { return "Intro"; }

private:
    GameContext& m_Ctx;
    Cutscene* m_Preloaded = nullptr;
};

class MainMenuState : public MenuScreenState {
public:
    explicit MainMenuState(GameContext& ctx) : MenuScreenState(ctx) {}
    const char* Name() const override { return "MainMenu"; }

    // Which item the user chose last, for tests. -1 if none.
    int Chosen() const { return m_Chosen; }

protected:
    // No title: the main menu shows the logo instead.
    bool OnEnter(StateMachine& sm) override;
    void DrawHeader(Surface& screen, int listTop) const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
    void OnBack(StateMachine& sm) override;
    int ListTop() const override;

private:
    // "Do you want to quit?", from either the Quit row or Back.
    void AskQuit(StateMachine& sm);

    int m_Chosen = -1;
    bool m_QuitRequested = false;
};

}  // namespace bb
