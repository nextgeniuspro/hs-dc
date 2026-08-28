#include "game/MenuScreen.h"

#include <cstdio>

#include "game/Cutscene.h"
#include "game/SoundManager.h"
#include "game/Font.h"
#include "game/Game.h"
#include "game/MenuChrome.h"
#include "game/MenuIds.h"
#include "game/MenuList.h"
#include "game/Strings.h"
#include "game/TextureCache.h"
#include "game/Water.h"
#include "platform/Host.h"
#include "platform/Surface.h"

namespace bb {
namespace {

// The menu loop sleeps 0 in the original and runs as fast as the device
// allows; see the note in MenuStates.cpp about why the port paces it.
constexpr int kFrameMs = 20;

constexpr int kTitleX = chrome::kItemX;
constexpr int kTitleGap = 0;

// The question on a confirmation board. Every one of them puts it on the
// three-line stone plank -- `Data\Menu\3rows.tc`, the widget's row kind 10 --
// which is one picture whatever the question says, so the answers below it
// start in the same place on every board. Shared by the pushed screen and the
// modal one so the two cannot drift apart.
int QuestionHeight(GameContext& ctx, int questionID) {
    if (ctx.StringsRef.Get(questionID).empty()) return 0;
    return chrome::StonePlankHeight(ctx) + kTitleGap;
}

void DrawQuestion(GameContext& ctx, Surface& screen, int questionID, int y) {
    const std::string& text = ctx.StringsRef.Get(questionID);
    if (text.empty()) return;
    chrome::DrawStonePlank(ctx, screen, text, y);
}

void AddAnswers(const GameContext& ctx, MenuList& list, bool yesFirst) {
    if (yesFirst) {
        list.Add(ctx.StringsRef.Get(ids::kYes), ids::kYes);
        list.Add(ctx.StringsRef.Get(ids::kNo), ids::kNo);
    } else {
        list.Add(ctx.StringsRef.Get(ids::kNo), ids::kNo);
        list.Add(ctx.StringsRef.Get(ids::kYes), ids::kYes);
    }
}

}  // namespace

int MenuScreenState::ListTop() const {
    // The running y starts at 17 whether or not there is a title, and a title
    // row claims only the height of its words plus five -- the plank itself is
    // taller and the first button sits under its ragged edge.
    return chrome::kListTop + chrome::TitleHeight(m_Ctx, Title());
}

void MenuScreenState::OnBack(StateMachine& sm) { sm.Back(); }

void MenuScreenState::Run(StateMachine& sm) {
    Host& host = m_Ctx.HostRef;
    if (OnEnter(sm)) return;

    MenuList list;
    list.Load(m_Ctx.Textures, m_Ctx.SmallFont, 4);
    Build(list);
    list.SetSelected(m_Selected);

    host.FlushKeys();
    bool leaving = false;

    // Which trinket the title plank wears, chosen once per visit.
    const int titleFrame = chrome::PickTitleFrame(m_Ctx);

    // Every front-end page arms the attract reel as it opens (0x100393b0) --
    // unless the page is being borrowed by something that must not be
    // interrupted, which is what GameContext::attract says.
    AttractTimer attract;
    if (m_Ctx.Attract) attract.Arm(host);

    // And every one of them asks for the theme. The engine does this from the
    // list widget's own constructor -- 0x10038f90's last argument is a flag
    // meaning "start the music", and it is set for every front-end page and
    // for the in-battle popup too -- so the ask is per screen, not per state,
    // and all but the first are free. This is what keeps `menu_music.spc`
    // running unbroken across the whole front end without any screen owning
    // it, and it is the piece the port was missing: nothing ever started it.
    if (m_Ctx.Sound)
        m_Ctx.Sound->StartMusic(SoundManager::kBankMenu,
                               SoundManager::kSoundMenuMusic);

    while (!leaving && !host.QuitRequested()) {
        // Any key at all restarts the attract countdown, including ones this
        // screen does nothing with -- so the presses go through here.
        bool touched = false;
        auto pressed = [&](Key k) {
            const bool p = host.KeyPressed(k);
            touched = touched || p;
            return p;
        };

        // The UI bank, in the order menu.dat lists it.
        auto blip = [&](SoundManager::MenuSound which) {
            if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(which);
        };

        if (pressed(Key::kUp)) { list.MoveUp(); blip(SoundManager::kSoundMove); }
        if (pressed(Key::kDown)) { list.MoveDown(); blip(SoundManager::kSoundMove); }
        if (pressed(Key::kLeft) && list.Adjust(-1)) {
            OnSliderChanged(list.SelectedId(), list.SliderValue(list.Selected()));
            blip(SoundManager::kSoundAdjust);
        }
        if (pressed(Key::kRight) && list.Adjust(+1)) {
            OnSliderChanged(list.SelectedId(), list.SliderValue(list.Selected()));
            blip(SoundManager::kSoundAdjust);
        }
        pressed(Key::kSoftLeft);  // unused here, but it still counts as input
        if (pressed(Key::kBack) || pressed(Key::kSoftRight)) {
            blip(SoundManager::kSoundCancel);
            if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
            OnBack(sm);
            return;
        }
        if (pressed(Key::kSelect) && list.SelectedEnabled() &&
            !list.IsSlider(list.Selected())) {
            blip(SoundManager::kSoundEnter);
            m_Selected = list.Selected();
            if (OnChosen(list.SelectedId(), sm)) return;
            // Stayed put: a toggle changed the labels, so rebuild in place.
            MenuList rebuilt;
            rebuilt.Load(m_Ctx.Textures, m_Ctx.SmallFont, 4);
            Build(rebuilt);
            rebuilt.SetSelected(m_Selected);
            list = std::move(rebuilt);
            host.FlushKeys();
        }

        // 0x1003aa84 polls this every frame; it plays and returns here.
        if (touched)
            attract.Poke(host);
        else if (attract.Poll(host, m_Ctx.Pack, m_Ctx.Textures, m_Ctx.StringsRef,
                              m_Ctx.SmallFont, m_Ctx.Sound))
            continue;

        Surface& screen = host.Screen();
        if (m_Ctx.WaterRef.Valid())
            m_Ctx.WaterRef.Draw(screen);
        else
            screen.Fill(0xF000u);

        // Order matters, and it is the widget's: items, then the cursor, then
        // the text rows over both, and the title plank last of all
        // (0x1003a224 -> 0x1003a880, 0x1003a788, 0x1007c898 -- which holds the
        // kind-6 row back and draws it after everything else).
        const int top = ListTop();
        list.Draw(screen, top, host.TickCount());
        DrawHeader(screen, top);
        chrome::DrawTitle(m_Ctx, screen, Title(), titleFrame);

        host.Flip();
        if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
        host.Sleep(kFrameMs);
    }

    if (host.QuitRequested()) sm.Quit();
}

// --- confirmation -----------------------------------------------------------

std::string ConfirmState::Title() const { return m_Ctx.StringsRef.Get(m_TitleID); }

int ConfirmState::ListTop() const {
    // Leave room for the wrapped question between the title and the answers.
    return MenuScreenState::ListTop() + QuestionHeight(m_Ctx, m_QuestionID);
}

void ConfirmState::DrawHeader(Surface& screen, int) const {
    DrawQuestion(m_Ctx, screen, m_QuestionID, MenuScreenState::ListTop());
}

void ConfirmState::Build(MenuList& list) { AddAnswers(m_Ctx, list, m_YesFirst); }

bool ConfirmState::OnChosen(int id, StateMachine& sm) {
    if (m_OnAnswer) m_OnAnswer(id == ids::kYes);
    sm.Back();
    return true;
}

// The same board without the state machine under it. Everything about it is
// the pushed screen's -- the water, the title plank, the wrapped question, the
// two wooden answers -- except that it returns the answer instead of calling
// back with it, and that it does not arm the attract reel: 0x10061444 is a
// LocalPlayer state and never calls 0x100393b0, which is what would let the
// attract film start playing over a battle in progress.
Confirmed RunConfirm(GameContext& ctx, int titleID, int questionID,
                     bool yesFirst, int firstIndex) {
    Host& host = ctx.HostRef;
    const std::string title = ctx.StringsRef.Get(titleID);
    MenuList list;
    list.Load(ctx.Textures, ctx.SmallFont, firstIndex);
    AddAnswers(ctx, list, yesFirst);
    // 0x10039c48: the selection opens on the first row it can land on.
    list.SetSelected(0);

    const int titleFrame = chrome::PickTitleFrame(ctx);
    const int questionY = chrome::kListTop + chrome::TitleHeight(ctx, title);
    const int top = questionY + QuestionHeight(ctx, questionID);

    host.FlushKeys();
    auto blip = [&](SoundManager::MenuSound which) {
        if (ctx.Sound) ctx.Sound->PlayMenu(which);
    };
    while (!host.QuitRequested()) {
        if (host.KeyPressed(Key::kUp)) {
            list.MoveUp();
            blip(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kDown)) {
            list.MoveDown();
            blip(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight)) {
            blip(SoundManager::kSoundCancel);
            if (ctx.Sound) ctx.Sound->Pump(host);
            return Confirmed::kBacked;
        }
        if (host.KeyPressed(Key::kSelect) && list.SelectedEnabled()) {
            blip(SoundManager::kSoundEnter);
            if (ctx.Sound) ctx.Sound->Pump(host);
            return list.SelectedId() == ids::kYes ? Confirmed::kYes
                                                  : Confirmed::kNo;
        }

        Surface& screen = host.Screen();
        if (ctx.WaterRef.Valid())
            ctx.WaterRef.Draw(screen);
        else
            screen.Fill(0xF000u);
        list.Draw(screen, top, host.TickCount());
        DrawQuestion(ctx, screen, questionID, questionY);
        chrome::DrawTitle(ctx, screen, title, titleFrame);

        host.Flip();
        if (ctx.Sound) ctx.Sound->Pump(host);
        host.Sleep(kFrameMs);
    }
    return Confirmed::kQuit;
}

// --- notice -----------------------------------------------------------------

std::string NoticeState::Title() const { return m_Ctx.StringsRef.Get(m_TitleID); }

int NoticeState::ListTop() const {
    const auto lines =
        WrapText(m_Ctx.SmallFont, m_Message, Surface::kWidth - 2 * kTitleX);
    return MenuScreenState::ListTop() +
           static_cast<int>(lines.size()) * m_Ctx.SmallFont.Height() + kTitleGap;
}

void NoticeState::DrawHeader(Surface& screen, int) const {
    int y = MenuScreenState::ListTop();
    for (const std::string& line :
         WrapText(m_Ctx.SmallFont, m_Message, Surface::kWidth - 2 * kTitleX)) {
        m_Ctx.SmallFont.Draw(screen, line, kTitleX, y);
        y += m_Ctx.SmallFont.Height();
    }
}

void NoticeState::Build(MenuList& list) {
    list.Add(m_Ctx.StringsRef.Get(ids::kReady), ids::kReady);
}

bool NoticeState::OnChosen(int, StateMachine& sm) {
    sm.Back();
    return true;
}

}  // namespace bb
