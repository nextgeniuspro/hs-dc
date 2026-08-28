// MenuScreen — the shape every menu screen in the game shares.
//
// Each screen is its own state, and each one does the same three things: build
// a list of items, run the widget until one is chosen, then decide where that
// leads (0x1008b764, 0x100a79b8, 0x100a6598 and the rest are all this shape).
// The frame loop draws the water first, then the screen's title as a big-font
// text element, then the items.
//
// Subclasses supply the three parts that actually differ: the title, the items,
// and the dispatch. Items are rebuilt whenever the screen is re-entered or a
// toggle changes, which is how the original gets labels like "Sound on" /
// "Sound off" out of a single entry.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "game/StateMachine.h"

namespace bb {

class MenuList;
class Surface;
struct GameContext;

class MenuScreenState : public GameState {
public:
    explicit MenuScreenState(GameContext& ctx) : m_Ctx(ctx) {}

    void Run(StateMachine& sm) final;

protected:
    // Title drawn above the list. Empty means no title (the main menu draws its
    // logo instead).
    virtual std::string Title() const { return {}; }

    // Draw anything above the list that isn't a title — the main menu's logo.
    // `y` is where the list will start.
    virtual void DrawHeader(Surface& screen, int listTop) const {}

    // Called every time the screen is entered, including when a screen pushed
    // on top of it pops back. Return true if a transition was requested and the
    // screen should not run -- that is how a confirmation's answer takes effect
    // on the screen that asked for it.
    virtual bool OnEnter(StateMachine& sm) { return false; }

    // Populate the list. Called again after every toggle.
    virtual void Build(MenuList& list) = 0;

    // A row was confirmed. Return false to stay on this screen (a toggle), true
    // once a transition has been requested or the screen is finished.
    virtual bool OnChosen(int id, StateMachine& sm) = 0;

    // Called when Back / the right soft key is pressed. Default returns to the
    // previous screen.
    virtual void OnBack(StateMachine& sm);

    // Where the list starts. Defaults to just below the title.
    virtual int ListTop() const;

    // A slider row moved. Persist the new value.
    virtual void OnSliderChanged(int id, int value) {}

    GameContext& m_Ctx;

private:
    int m_Selected = 0;  // survives rebuilds
};

// A yes/no question over the usual furniture. `yesFirst` matters: the quit
// prompt lists Yes then No (0x10060554) while the reset prompts list No then
// Yes (0x1006125c), and getting it backwards is how you delete a save by
// muscle memory.
class ConfirmState : public MenuScreenState {
public:
    using Callback = std::function<void(bool)>;

    ConfirmState(GameContext& ctx, int titleID, int questionID, bool yesFirst,
                 Callback onAnswer)
        : MenuScreenState(ctx), m_TitleID(titleID), m_QuestionID(questionID),
          m_YesFirst(yesFirst), m_OnAnswer(std::move(onAnswer)) {}

    const char* Name() const override { return "Confirm"; }

protected:
    std::string Title() const override;
    void DrawHeader(Surface& screen, int listTop) const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
    int ListTop() const override;

private:
    int m_TitleID;
    int m_QuestionID;
    bool m_YesFirst;
    Callback m_OnAnswer;
};

// What a confirmation board came back with.
//
// The two "no"s are not the same answer, and the battle's Surrender board is
// where that shows: 0x10061524 sends the surrender on Yes, and on *No* calls
// LocalPlayer::stateClearAndSet — which throws the whole menu stack away and
// puts the player back on the map — while backing out with the right soft key
// calls statePop, one level, which lands back on the Options submenu that
// opened it.
enum class Confirmed { kYes, kNo, kBacked, kQuit };

// The same board as ConfirmState, run modally instead of pushed onto the state
// machine, for a caller that owns its own loop. The battle needs one: its
// Surrender board is a LocalPlayer state (0x10061444), not a front-end page,
// and it has to tell all three answers apart.
//
// `firstIndex` is what the widget's own virtual hands 0x10038f90 — it picks
// which of `shell_board.tc`'s three grains the first row wears. The front end's
// confirmations pass 100 (0x10060554) and the surrender board 32 (0x1006160c).
Confirmed RunConfirm(GameContext& ctx, int titleID, int questionID,
                     bool yesFirst, int firstIndex);

// A screen that just says something and waits — used where the original goes on
// into game code the port hasn't reached yet, so the flow stays honest instead
// of silently doing nothing.
class NoticeState : public MenuScreenState {
public:
    NoticeState(GameContext& ctx, int titleID, std::string message)
        : MenuScreenState(ctx), m_TitleID(titleID), m_Message(std::move(message)) {}

    const char* Name() const override { return "Notice"; }

protected:
    std::string Title() const override;
    void DrawHeader(Surface& screen, int listTop) const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
    int ListTop() const override;

private:
    int m_TitleID;
    std::string m_Message;
};

}  // namespace bb
