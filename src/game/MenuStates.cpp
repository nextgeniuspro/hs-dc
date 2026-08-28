#include "game/MenuStates.h"

#include <cstdio>

#include "game/Cutscene.h"
#include "game/Font.h"
#include "game/Game.h"
#include "game/MenuIds.h"
#include "game/MenuList.h"
#include "game/MenuScreens.h"
#include "game/Strings.h"
#include "game/TextureCache.h"
#include "platform/Host.h"
#include "platform/Surface.h"

namespace bb {
namespace {

// Where the list starts. The original derives this from the page's layout
// cursor: the 176x48 title logo, then a 20px gap (0x1008b764 adds 0x14 to
// page+0x1c), then a 15px text line. The page element layout isn't reversed
// yet, so this is that arithmetic rather than a recovered constant.
constexpr int kLogoY = 0;
constexpr int kMainListTop = 83;

}  // namespace

void IntroState::Run(StateMachine& sm) {
    // The original builds an AnimationPlayer here, plays "01-Intro"
    // (0x10001f14), tears it down and only then changes to the main menu --
    // and it checks for a quit request twice, so closing the window during the
    // cutscene ends the game rather than dropping into the menu.
    const bool played =
        m_Preloaded ? m_Preloaded->Play(m_Ctx.HostRef)
                   : PlayCutscene(m_Ctx.HostRef, m_Ctx.Pack, m_Ctx.Textures,
                                  m_Ctx.StringsRef, m_Ctx.SmallFont, kIntroCutscene,
                                  m_Ctx.Sound, m_Ctx.CampaignData.Commander);
    // "Tears it down" is the part that matters on a small machine. A cutscene
    // played through PlayCutscene is torn down by leaving this function; the
    // preloaded intro is not, because RunGame builds it before the boot
    // screens and then runs the whole state machine from inside it -- so it is
    // told to let its 1.1 MB of artwork go here instead.
    if (m_Preloaded) m_Preloaded->Unload();
    if (!played) {
        sm.Quit();
        return;
    }
    sm.Change(std::make_unique<MainMenuState>(m_Ctx));
}

int MainMenuState::ListTop() const { return kMainListTop; }

bool MainMenuState::OnEnter(StateMachine& sm) {
    // Set by the quit confirmation, which has already popped back to here.
    if (!m_QuitRequested) return false;
    sm.Quit();
    return true;
}

void MainMenuState::DrawHeader(Surface& screen, int) const {
    const Texture* logo = m_Ctx.Textures.Load("Data\\Menu\\high_seize.tc");
    if (!logo) return;
    if (const TcTexture::Image* f = logo->Frame(0)) {
        screen.Blit(f->Pixels.data(), f->Width, f->Height,
                    (screen.Width() - f->Width) / 2, kLogoY);
    }
}

void MainMenuState::Build(MenuList& list) {
    list.Add(m_Ctx.StringsRef.Get(ids::kSinglePlayer), ids::kSinglePlayer);
    // Greyed out: the port has no multiplayer to offer yet. Two of the three
    // entries behind this row were dead off-device from the start -- Bluetooth
    // needs a stack the port does not have, and the N-Gage Arena service no
    // longer exists -- and Hot Seat, which is what that leaves, is not
    // finished.
    //
    // Disabled rather than dropped, and the difference is deliberate. The rows
    // the port *drops* are the ones that can never come back; this one can, so
    // it keeps its place in the original's menu, the screen behind it still
    // builds, and turning it back on is this one argument. A player also
    // learns more from a row they can see is off than from a menu that is
    // quietly shorter than the one on the box.
    list.Add(m_Ctx.StringsRef.Get(ids::kMultiplayer), ids::kMultiplayer,
             /*enabled=*/false);
    // N-Gage Arena sits here in the original; the service is gone.
    list.Add(m_Ctx.StringsRef.Get(ids::kSettings), ids::kSettings);
    list.Add(m_Ctx.StringsRef.Get(ids::kHelp), ids::kHelp);
    list.Add(m_Ctx.StringsRef.Get(ids::kQuit), ids::kQuit);
}

// "Do you want to quit?" -- Yes first here, unlike the reset prompts
// (0x10060554 vs 0x1006125c). The answer is remembered rather than acted on:
// the confirmation is a pushed state, so it has already popped back to the main
// menu by the time it is known, and OnEnter is what reads it.
void MainMenuState::AskQuit(StateMachine& sm) {
    sm.Push(std::make_unique<ConfirmState>(
        m_Ctx, ids::kQuit, ids::kConfirmQuit, /*yesFirst=*/true,
        [this](bool yes) { m_QuitRequested = yes; }));
}

void MainMenuState::OnBack(StateMachine& sm) {
    // There is nothing below the main menu, so Back is the way out of the game
    // -- and it asks the same question the Quit row does.
    AskQuit(sm);
}

bool MainMenuState::OnChosen(int id, StateMachine& sm) {
    m_Chosen = id;
    switch (id) {
        case ids::kSinglePlayer:
            sm.Push(std::make_unique<SinglePlayerState>(m_Ctx));
            return true;
        // Unreachable while the row above is disabled -- the widget refuses to
        // confirm a row it has greyed out -- and kept for exactly that reason:
        // the way back is to enable the row, not to write this again.
        case ids::kMultiplayer:
            sm.Push(std::make_unique<MultiplayerState>(m_Ctx));
            return true;
        case ids::kSettings:
            sm.Push(std::make_unique<SettingsState>(m_Ctx));
            return true;
        case ids::kHelp:
            sm.Push(std::make_unique<HelpState>(m_Ctx));
            return true;
        case ids::kQuit:
            AskQuit(sm);
            return true;
        default:
            return false;
    }
}

}  // namespace bb
