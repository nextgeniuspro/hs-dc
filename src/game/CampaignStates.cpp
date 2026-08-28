#include "game/CampaignStates.h"

#include <cstdio>

#include "game/Font.h"
#include "game/Game.h"
#include "game/MenuChrome.h"
#include "game/MenuList.h"
#include "game/MissionFlow.h"
#include "game/SaveGame.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/TextBox.h"
#include "game/TextureCache.h"
#include "game/Water.h"
#include "platform/Host.h"
#include "platform/Surface.h"

namespace bb {
namespace {

constexpr int kFrameMs = 20;

// Strings the setup screen shows (0x1009a6b0).
constexpr int kStrNewGame = 2113;
constexpr int kStrReady = 1656;
constexpr int kStrCommanderName = 2115;
constexpr int kStrPlayerColour = 2465;
constexpr int kStrNoName = 1562;   // "Commander must have a name."
// The three offered colours and their row ids; the engine stores `id - 10`.
constexpr int kColourIds[3] = {11, 13, 14};
constexpr int kColourStrings[3] = {1674, 1676, 1677};  // Red, Blue, Yellow

// Two limits, and the pixel one always bites first: 0x100b1490 accepts a
// character only while the name it has *already* measures under 0x3c pixels
// and is shorter than the buffer 0x1009a6b0 asked for. Measuring before the
// insert is what lets the last character overhang a little.
constexpr int kMaxNameLength = 0x20;
constexpr int kMaxNameWidth = 0x3c;

// Resource slots for the setup screen's materials.
constexpr uint16_t kSlotInputBg = 0xe1;    // wood, grey arrow, iron brackets
constexpr uint16_t kSlotShellBoard = 0xb2; // the ordinary wooden button
constexpr uint16_t kSlotSword = 0x2c;      // the selection cursor
constexpr uint16_t kSlotMarker = 0x31;     // the gold coin (resource 0x31)

// The name is written between the field's iron brackets, not against its left
// edge: 0x100b1cec draws it at x + 0x36, five pixels down.
constexpr int kFieldTextX = 0x36;
constexpr int kFieldTextY = 5;
// Where the coin sits on a colour row, and how fast it turns (0x100acdf4).
constexpr int kCoinX = 0x78;
constexpr int kCoinY = -1;
constexpr int kCoinFrames = 6;
constexpr uint32_t kCoinStepMs = 0x46;

const TcTexture::Image* FrameOf(const Texture* tex, int frame) {
    return tex ? tex->Frame(frame) : nullptr;
}

void BlitFrame(Surface& dst, const Texture* tex, int frame, int x, int y) {
    if (const TcTexture::Image* img = FrameOf(tex, frame))
        dst.Blit(img->Pixels.data(), img->Width, img->Height, x, y);
}

int TextureHeight(const Texture* tex) {
    const TcTexture::Image* img = FrameOf(tex, 0);
    return img ? img->Height : 0;
}

}  // namespace

int NewGameState::ColourOf(int row) {
    if (row < kRowRed || row > kRowYellow) return 0;
    return kColourIds[row - kRowRed] - 10;
}

int NewGameState::IdOf(int row) {
    switch (row) {
        case kRowReady: return 2;
        case kRowName: return 1;
        default: return kColourIds[row - kRowRed];
    }
}

// One running y through the whole page, exactly as the widget builds it: the
// title claims its text plus five, an item its board height minus one, a
// caption its slab minus nine, and the name field its whole texture
// (0x1008ca04, and 0x1009a6b0's `+= FUN_100b1fd0`).
NewGameState::Layout NewGameState::Measure() const {
    const Texture* board =
        m_Ctx.Textures.Register(kSlotShellBoard, "Data\\Menu\\shell_board.tc");
    const Texture* field =
        m_Ctx.Textures.Register(kSlotInputBg, "Data\\Menu\\input_bg.tc");
    const int itemH = TextureHeight(board) - 1;
    const int captionH = chrome::SubTopicHeight(m_Ctx);

    Layout l;
    int y = chrome::kListTop + chrome::TitleHeight(m_Ctx, m_Ctx.StringsRef.Get(kStrNewGame));
    l.Ready = y;              y += itemH;
    l.NameCaption = y;       y += captionH;
    l.Field = y;              y += TextureHeight(field);
    l.ColourCaption = y;     y += captionH;
    for (int i = 0; i < 3; ++i) { l.Colour[i] = y; y += itemH; }
    return l;
}

int NewGameState::RowY(const Layout& l, int row) const {
    switch (row) {
        case kRowReady: return l.Ready;
        case kRowName: return l.Field;
        default: return l.Colour[row - kRowRed];
    }
}

void NewGameState::Draw(Surface& dst) {
    TextureCache& tc = m_Ctx.Textures;
    const Font& small = m_Ctx.SmallFont;

    if (m_Ctx.WaterRef.Valid()) m_Ctx.WaterRef.Draw(dst);
    else dst.Fill(0xF000u);

    const Layout l = Measure();

    // The input field goes down before the list (0x1009a6b0 draws it, then
    // calls the widget), so the caption above it can hang its rings over it.
    const Texture* field = tc.Register(kSlotInputBg, "Data\\Menu\\input_bg.tc");
    BlitFrame(dst, field, 0, 0, l.Field);
    std::string shown = m_Name;
    if (m_Editing && (m_Ctx.HostRef.TickCount() / 400) % 2 == 0) shown += "_";
    small.Draw(dst, shown, kFieldTextX, l.Field + kFieldTextY);

    const Texture* board =
        tc.Register(kSlotShellBoard, "Data\\Menu\\shell_board.tc");
    auto item = [&](int row, const std::string& label) {
        const int y = RowY(l, row);
        BlitFrame(dst, board, IdOf(row) % MenuList::kBoardFrames,
                  chrome::kItemX, y);
        small.Draw(dst, label, chrome::kItemX + MenuList::kTextX,
                   y + MenuList::kTextY);
    };

    item(kRowReady, m_Ctx.StringsRef.Get(kStrReady));

    const Texture* coin = tc.Register(kSlotMarker, "Data\\icons\\marker.tc");
    for (int i = 0; i < 3; ++i) {
        const int row = kRowRed + i;
        item(row, m_Ctx.StringsRef.Get(kColourStrings[i]));
        const bool chosen = ColourOf(row) == m_Ctx.CampaignData.Colour;
        BlitFrame(dst, coin, chosen ? m_CoinFrame : 0,
                  chrome::kItemX + kCoinX, l.Colour[i] + kCoinY);
    }

    // The sword, one frame every 30 ms. While the field is being typed into
    // the original keeps it on the field's row (0x1009a6b0's edit branch).
    const Texture* sword = tc.Register(kSlotSword, "Data\\Menu\\sword.tc");
    if (sword && !sword->Frames.empty()) {
        const int frames = int(sword->Frames.size());
        const int f = int(m_Ctx.HostRef.TickCount() / MenuList::kCursorFrameMs) % frames;
        const int y = m_Editing ? l.Field : RowY(l, m_Selected);
        BlitFrame(dst, sword, f, chrome::kItemX - MenuList::kCursorOffset,
                  y - MenuList::kCursorOffset);
    }

    // Last: the captions, then the title. Text rows are drawn over the items
    // (0x1007c898 after 0x1003a880), which is the whole reason the stone
    // slabs' rings appear to hang in front of the boards below them.
    chrome::DrawSubTopic(m_Ctx, dst, m_Ctx.StringsRef.Get(kStrCommanderName),
                         l.NameCaption);
    chrome::DrawSubTopic(m_Ctx, dst, m_Ctx.StringsRef.Get(kStrPlayerColour),
                         l.ColourCaption);
    chrome::DrawTitle(m_Ctx, dst, m_Ctx.StringsRef.Get(kStrNewGame), m_TitleFrame);
}

void NewGameState::Run(StateMachine& sm) {
    Host& host = m_Ctx.HostRef;
    if (m_Name.empty()) m_Name = m_Ctx.CampaignData.Commander;
    m_TitleFrame = chrome::PickTitleFrame(m_Ctx);
    m_CoinSince = host.TickCount();
    host.FlushKeys();
    host.PollText();

    while (!host.QuitRequested()) {
        // The coin beside the current colour keeps flipping until it settles
        // on its last frame.
        const uint32_t now = host.TickCount();
        if (now - m_CoinSince > kCoinStepMs) {
            m_CoinSince = now;
            if (m_CoinFrame < kCoinFrames - 1) ++m_CoinFrame;
        }

        if (m_Editing) {
            // The keyboard stands in for the original's multi-tap keypad.
            for (char c : host.PollText()) {
                if (c == '\b') {
                    if (!m_Name.empty()) m_Name.pop_back();
                } else if (c >= 0x20 &&
                           m_Ctx.SmallFont.Width(m_Name) < kMaxNameWidth &&
                           int(m_Name.size()) < kMaxNameLength) {
                    m_Name += c;
                }
            }
            if (host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kBack) ||
                host.KeyPressed(Key::kSoftRight)) {
                m_Editing = false;
                m_Ctx.CampaignData.Commander = m_Name;
            }
        } else {
            if (host.KeyPressed(Key::kUp) && m_Selected > 0) --m_Selected;
            if (host.KeyPressed(Key::kDown) && m_Selected < kRowCount - 1)
                ++m_Selected;
            if (host.KeyPressed(Key::kSelect)) {
                if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(SoundManager::kSoundEnter);
                if (m_Selected == kRowName) {
                    m_Editing = true;
                    host.PollText();
                } else if (m_Selected >= kRowRed && m_Selected <= kRowYellow) {
                    // Picking a colour restarts its coin (0x100ad060).
                    m_Ctx.CampaignData.Colour = ColourOf(m_Selected);
                    m_CoinFrame = 1;
                    m_CoinSince = host.TickCount();
                } else {
                    // Ready. A nameless commander gets told off and stays put
                    // (0x1009a6b0 builds a box around string 1562).
                    m_Ctx.CampaignData.Commander = m_Name;
                    if (m_Name.empty()) {
                        TextBox box(m_Ctx, TextBox::kOkOk);
                        box.Title(m_Ctx.StringsRef.Get(kStrNewGame));
                        box.Text(m_Ctx.StringsRef.Get(kStrNoName));
                        box.Run();
                        host.FlushKeys();
                        continue;
                    }
                    sm.Change(std::make_unique<PerkSelectState>(m_Ctx));
                    return;
                }
            }
            if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight)) {
                if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(SoundManager::kSoundCancel);
                sm.Back();
                return;
            }
        }

        Draw(host.Screen());
        host.Flip();
        if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
        host.Sleep(kFrameMs);
    }
    if (host.QuitRequested()) sm.Quit();
}

// --- perks ------------------------------------------------------------------

bool PerkSelectState::Explain(GameContext& ctx, int perk) {
    // Mode 10 is Ok / Back, which is exactly the pair the perk board shows.
    TextBox box(ctx, TextBox::kOkBack);
    box.Title(ctx.StringsRef.Get(Campaign::kPerkNameBase + perk));
    box.Text(ctx.StringsRef.Get(Campaign::kPerkDescBase + perk));
    return box.Run() == TextBox::kConfirmed;
}

// A tiny list screen: the three perks, each of which explains itself before it
// is taken. The original rebuilds the whole widget after a refusal
// (0x1007f9a4's do-while), which is what the loop here does.
void PerkSelectState::Run(StateMachine& sm) {
    Host& host = m_Ctx.HostRef;
    MenuList list;
    // 0x1007f9a4 adds the three perks with ids 2, 3 and 4, and the id is what
    // picks each board's grain.
    list.Load(m_Ctx.Textures, m_Ctx.SmallFont, 2);
    for (int p : Campaign::kFirstPerks)
        list.Add(m_Ctx.StringsRef.Get(Campaign::kPerkNameBase + p), p);
    host.FlushKeys();

    const std::string title = m_Ctx.StringsRef.Get(kStrNewGame);
    // String 1665 is the caption over the three boards -- a kind-8 row, so it
    // is on the same stone slab the setup screen's labels use.
    const std::string prompt = m_Ctx.StringsRef.Get(1665);
    const int titleFrame = chrome::PickTitleFrame(m_Ctx);
    const int captionY = chrome::kListTop + chrome::TitleHeight(m_Ctx, title);
    const int top = captionY + chrome::SubTopicHeight(m_Ctx);

    while (!host.QuitRequested()) {
        if (host.KeyPressed(Key::kUp)) list.MoveUp();
        if (host.KeyPressed(Key::kDown)) list.MoveDown();
        if (host.KeyPressed(Key::kSelect)) {
            const int perk = list.SelectedId();
            if (m_Ctx.Sound) m_Ctx.Sound->PlayMenu(SoundManager::kSoundEnter);
            if (Explain(m_Ctx, perk)) {
                m_Ctx.CampaignData.ChoosePerk(perk);
                sm.Change(std::make_unique<CampaignState>(m_Ctx));
                return;
            }
            host.FlushKeys();
        }
        if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight)) {
            sm.Back();
            return;
        }

        Surface& screen = host.Screen();
        if (m_Ctx.WaterRef.Valid()) m_Ctx.WaterRef.Draw(screen);
        else screen.Fill(0xF000u);
        list.Draw(screen, top, host.TickCount());
        chrome::DrawSubTopic(m_Ctx, screen, prompt, captionY);
        chrome::DrawTitle(m_Ctx, screen, title, titleFrame);
        host.Flip();
        if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
        host.Sleep(kFrameMs);
    }
    if (host.QuitRequested()) sm.Quit();
}

// --- the campaign itself ----------------------------------------------------

CampaignState::CampaignState(GameContext& ctx,
                             std::unique_ptr<SavedBattle> resume)
    : m_Ctx(ctx), m_Resume(std::move(resume)) {}

CampaignState::~CampaignState() = default;

void CampaignState::Run(StateMachine& sm) {
    RunCampaign(m_Ctx, sm, m_Resume.get());
}

}  // namespace bb
