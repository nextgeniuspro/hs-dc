#include "game/CardPicker.h"

#include <string>
#include <vector>

#include "game/Font.h"
#include "game/Game.h"
#include "game/MenuChrome.h"
#include "game/MenuList.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/TextBox.h"
#include "game/TextureCache.h"
#include "game/Water.h"
#include "platform/Host.h"
#include "platform/Storage.h"
#include "platform/Surface.h"

namespace bb {
namespace {

// The same twenty milliseconds every other modal board in the port paces
// itself at.
constexpr int kFrameMs = 20;

// Strings the game already ships, so the board comes out in whatever language
// is running: 1616 "Save game" and 1614 "Don't save". The prose between them
// is the port's own and is written out, as it is on every other screen the
// original does not have (see MenuScreens.cpp).
constexpr int kStrSaveGame = 1616;
constexpr int kStrDontSave = 1614;
constexpr int kStrOk = 2116;
constexpr int kStrCancel = 62148;

constexpr const char* kAskPick = "Choose a memory card.";
constexpr const char* kAskNone =
    "No memory card. Put one in a controller, or play without saving.";
constexpr const char* kAskNoneLoad =
    "No memory card. Put one in a controller to load a game.";
constexpr const char* kConfirmNone =
    "Play without a memory card? Nothing you do will be saved.";

// The sprite, and the strip it is laid out in. Four bays across a 176-pixel
// screen at a pitch of 40 leaves the outer two eight pixels clear of the
// edges, and a 24-wide card centred in each.
constexpr const char* kCardIcon = "Data\\icons\\vmu.tc";
constexpr int kSlotPitch = 40;
constexpr int kFallbackW = 24, kFallbackH = 32;  // no icons.pak: draw a slab
constexpr int kLetterGap = 2;
constexpr int kBlockGap = 5;
// A bay with no card is dimmed rather than dropped, the way the scroll plank
// dims the arrow with nowhere to go.
constexpr int kEmptyAlpha = chrome::kScrollDim;

int BayCount(const GameContext& ctx) { return ctx.HostRef.SaveBayCount(); }

bool AnyBayReady(const GameContext& ctx) {
    for (int i = 0; i < BayCount(ctx); ++i)
        if (ctx.HostRef.SaveBayReady(i)) return true;
    return false;
}

// The next bay with a card in it, walking `step` at a time and wrapping. -1
// when there are none at all, which is what leaves the strip with nothing lit.
int NextReadyBay(const GameContext& ctx, int from, int step) {
    const int n = BayCount(ctx);
    if (n <= 0) return -1;
    for (int i = 1; i <= n; ++i) {
        const int bay = ((from + i * step) % n + n) % n;
        if (ctx.HostRef.SaveBayReady(bay)) return bay;
    }
    return ctx.HostRef.SaveBayReady(from) ? from : -1;
}

// Where the cursor opens: on the card already in use if it is still there,
// otherwise on the first one that could be.
int OpeningBay(const GameContext& ctx) {
    const int current = ctx.HostRef.SaveBay();
    if (current >= 0 && ctx.HostRef.SaveBayReady(current)) return current;
    for (int i = 0; i < BayCount(ctx); ++i)
        if (ctx.HostRef.SaveBayReady(i)) return i;
    return 0;
}

// One card, at the top-left of its slot. `lit` picks the sprite's second
// frame -- the same card with its screen on and a ring round it -- and
// `ready` decides the alpha.
void DrawCard(Surface& dst, const Texture* icon, int x, int y, bool ready,
              bool lit) {
    const int alpha = ready ? 15 : kEmptyAlpha;
    if (icon && icon->Valid()) {
        // A sprite with only one frame in it still works: the cursor then
        // shows in the row below and nothing here lies about it.
        const TcTexture::Image* img =
            icon->Frame(lit && icon->Frames.size() > 1 ? 1 : 0);
        if (img) {
            dst.Blit(img->Pixels.data(), img->Width, img->Height, x, y, alpha);
            return;
        }
    }
    // No icons.pak -- which the disc always carries, but a developer running
    // over dcload may not have mounted. A slab with a screen on it keeps the
    // board usable rather than leaving four blank gaps.
    const uint16_t body = static_cast<uint16_t>(alpha << 12 | 0xEEE);
    const uint16_t screen = static_cast<uint16_t>(alpha << 12 | (lit ? 0xAFD
                                                                     : 0x798));
    dst.FillRect(x, y, kFallbackW, kFallbackH, body);
    dst.FillRect(x + 4, y + 4, kFallbackW - 8, kFallbackH / 2, screen);
}

// The question behind the "Don't save" row. On the same board as every other
// question in the game, and asked once -- the point is that the player meant
// it, not that they should be nagged.
int AskWithoutCard(GameContext& ctx) {
    TextBox box(ctx, TextBox::kOkCancel);
    box.Title(ctx.StringsRef.Get(kStrSaveGame));
    box.Text(kConfirmNone);
    return box.Run();
}

}  // namespace

bool AsksForCard(const GameContext& ctx) { return ctx.HostRef.SaveBayCount() > 0; }

bool CanSave(const GameContext& ctx) {
    if (ctx.HostRef.Saves() != nullptr) return true;
    return AsksForCard(ctx) && AnyBayReady(ctx);
}

bool CanLoad(GameContext& ctx, SaveKind kind) {
    if (Storage* store = ctx.HostRef.Saves())
        if (HasGame(*store, kind)) return true;
    return AsksForCard(ctx) && AnyBayReady(ctx);
}

CardChoice PickCard(GameContext& ctx, bool allowNone) {
    Host& host = ctx.HostRef;
    if (!AsksForCard(ctx)) {
        // Nothing to choose between. Answer for the host rather than putting
        // an empty board up: this is the desktop and the tests.
        return host.Saves() ? CardChoice::kPicked : CardChoice::kNone;
    }

    // The card sprite belongs to this board and goes back with it. Everything
    // else on screen is furniture the startup set already holds.
    TextureSet art(ctx.Textures);
    const Texture* icon = art.Load(kCardIcon);

    const std::string title = ctx.StringsRef.Get(kStrSaveGame);
    const int titleFrame = chrome::PickTitleFrame(ctx);
    const std::string ok = ctx.StringsRef.Get(kStrOk);
    const std::string cancel = ctx.StringsRef.Get(kStrCancel);

    MenuList list;
    if (allowNone) {
        list.Load(ctx.Textures, ctx.SmallFont, 0);
        list.Add(ctx.StringsRef.Get(kStrDontSave), 0);
    }

    const int cardW = icon && icon->Valid() ? icon->Width : kFallbackW;
    const int cardH = icon && icon->Valid() ? icon->Height : kFallbackH;
    const int bays = BayCount(ctx);
    const int stripX = (Surface::kWidth - bays * kSlotPitch) / 2;

    int bay = OpeningBay(ctx);
    // Which of the two places the selection is. It starts on the strip unless
    // there is nothing there to land on, in which case the row is the only
    // answer the board can take.
    bool onRow = allowNone && !AnyBayReady(ctx);

    host.FlushKeys();
    auto blip = [&](SoundManager::MenuSound which) {
        if (ctx.Sound) ctx.Sound->PlayMenu(which);
    };

    while (!host.QuitRequested()) {
        const bool any = AnyBayReady(ctx);
        // A card pulled out from under the cursor drops the selection onto the
        // row, if there is one; a card pushed in while the board is up is
        // landable at once.
        if (!onRow && !any && allowNone) onRow = true;
        if (onRow && !allowNone) onRow = false;

        if (host.KeyPressed(Key::kUp) && onRow && any) {
            onRow = false;
            // The bay the cursor left may have been emptied since.
            if (!host.SaveBayReady(bay)) bay = OpeningBay(ctx);
            blip(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kDown) && !onRow && allowNone) {
            onRow = true;
            blip(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kLeft) && !onRow && any) {
            bay = NextReadyBay(ctx, bay, -1);
            blip(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kRight) && !onRow && any) {
            bay = NextReadyBay(ctx, bay, +1);
            blip(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight)) {
            blip(SoundManager::kSoundCancel);
            if (ctx.Sound) ctx.Sound->Pump(host);
            return CardChoice::kCancelled;
        }
        // The left soft key is labelled Ok on this board, so it confirms --
        // the same pair of keys a TextBox takes, and for the same reason: the
        // corner says so.
        if (host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kSoftLeft)) {
            if (onRow) {
                blip(SoundManager::kSoundEnter);
                if (ctx.Sound) ctx.Sound->Pump(host);
                const int answer = AskWithoutCard(ctx);
                if (answer == TextBox::kQuit) return CardChoice::kQuit;
                if (answer == TextBox::kConfirmed) {
                    host.SetSaveBay(-1);
                    return CardChoice::kNone;
                }
                host.FlushKeys();
            } else if (bay >= 0 && host.SaveBayReady(bay)) {
                blip(SoundManager::kSoundEnter);
                if (ctx.Sound) ctx.Sound->Pump(host);
                // False means the card went away between being drawn and being
                // chosen. Say nothing and stay: the strip has already redrawn
                // that bay empty, which is the honest answer.
                if (host.SetSaveBay(bay)) return CardChoice::kPicked;
            }
        }

        // --- draw ------------------------------------------------------------
        //
        // The water, not the wooden board a TextBox uses. This page has a
        // wooden button on it, and a wooden button on a wooden board is
        // invisible -- which is exactly why every page in the game that offers
        // one, the confirmation boards included (0x1006125c and the rest),
        // sits on the caustics layer instead.
        Surface& screen = host.Screen();
        if (ctx.WaterRef.Valid())
            ctx.WaterRef.Draw(screen);
        else
            screen.Fill(0xF000u);

        int y = chrome::kListTop + chrome::TitleHeight(ctx, title);
        const char* ask = any ? kAskPick
                              : (allowNone ? kAskNone : kAskNoneLoad);
        for (const std::string& line :
             WrapText(ctx.SmallFont, ask,
                      Surface::kWidth - 2 * chrome::kItemX)) {
            ctx.SmallFont.Draw(screen, line, chrome::kItemX, y);
            y += ctx.SmallFont.Height();
        }

        y += kBlockGap;
        for (int i = 0; i < bays; ++i) {
            const int cx = stripX + i * kSlotPitch + kSlotPitch / 2;
            const bool ready = host.SaveBayReady(i);
            DrawCard(screen, icon, cx - cardW / 2, y, ready,
                     !onRow && i == bay && ready);
            const std::string label = host.SaveBayLabel(i);
            ctx.SmallFont.Draw(screen, label,
                                cx - ctx.SmallFont.Width(label) / 2,
                                y + cardH + kLetterGap);
        }
        y += cardH + kLetterGap + ctx.SmallFont.Height() + kBlockGap;

        if (allowNone) {
            list.ShowCursor(onRow);
            list.Draw(screen, y, host.TickCount());
        }
        chrome::DrawTitle(ctx, screen, title, titleFrame);
        chrome::DrawSoftKeys(ctx, screen, ok, cancel);

        host.Flip();
        if (ctx.Sound) ctx.Sound->Pump(host);
        host.Sleep(kFrameMs);
    }
    return CardChoice::kQuit;
}

Storage* SaveTarget(GameContext& ctx) {
    if (Storage* store = ctx.HostRef.Saves()) return store;
    if (!AsksForCard(ctx)) return nullptr;  // this host simply has nowhere
    PickCard(ctx, /*allowNone=*/true);
    return ctx.HostRef.Saves();
}

}  // namespace bb
