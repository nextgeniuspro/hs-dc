#include "game/MenuChrome.h"

#include "game/Font.h"
#include "game/Game.h"
#include "game/TcTexture.h"
#include "game/TextureCache.h"
#include "platform/Host.h"
#include "platform/Surface.h"

namespace bb {
namespace chrome {
namespace {

constexpr uint16_t kSlotHeaderBg = 0xd1;   // Data\Menu\header_bg.tc
constexpr uint16_t kSlotSubTopic = 0xdf;   // Data\Menu\subtopic.tc
constexpr uint16_t kSlot3Rows = 0xe0;      // Data\Menu\3rows.tc
constexpr uint16_t kSlotScrollBg = 0xb4;   // Data\Menu\scroll_bg.tc
constexpr uint16_t kSlotScrollUp = 0x2a;   // Data\Menu\scrollup.tc
constexpr uint16_t kSlotScrollDown = 0x2b; // Data\Menu\scrolldown.tc

const Texture* HeaderBg(GameContext& ctx) {
    return ctx.Textures.Register(kSlotHeaderBg, "Data\\Menu\\header_bg.tc");
}

const Texture* SubTopic(GameContext& ctx) {
    return ctx.Textures.Register(kSlotSubTopic, "Data\\Menu\\subtopic.tc");
}

const Texture* ThreeRows(GameContext& ctx) {
    return ctx.Textures.Register(kSlot3Rows, "Data\\Menu\\3rows.tc");
}

}  // namespace

int PickTitleFrame(GameContext& ctx) {
    const Texture* tex = HeaderBg(ctx);
    if (!tex || tex->Frames.empty()) return 0;
    return int(ctx.HostRef.TickCount() % tex->Frames.size());
}

int TitleHeight(GameContext& ctx, const std::string& title) {
    if (title.empty()) return 0;
    return ctx.BigFont.Height() + kTitleExtra;
}

void DrawTitle(GameContext& ctx, Surface& dst, const std::string& title,
               int frame) {
    if (title.empty()) return;
    const Texture* tex = HeaderBg(ctx);
    if (tex) {
        if (const TcTexture::Image* img = tex->Frame(frame))
            dst.Blit(img->Pixels.data(), img->Width, img->Height, 0, 0);
    }
    ctx.BigFont.Draw(dst, title, kTitleTextX, kTitleTextY);
}

int SubTopicHeight(GameContext& ctx) {
    const Texture* tex = SubTopic(ctx);
    const TcTexture::Image* img = tex ? tex->Frame(0) : nullptr;
    if (!img) return 0;
    return img->Height + kSubTopicDrop - kSubTopicShort;
}

void DrawSubTopic(GameContext& ctx, Surface& dst, const std::string& text,
                  int y) {
    const Texture* tex = SubTopic(ctx);
    if (tex) {
        if (const TcTexture::Image* img = tex->Frame(0))
            dst.Blit(img->Pixels.data(), img->Width, img->Height, 0,
                     y + kSubTopicDrop);
    }
    // The caption row is the one thing on these pages drawn left of the item
    // column: 0x1008c378 case 8 takes five off the running x before it starts.
    ctx.SmallFont.Draw(dst, text, kItemX - kSubTopicDrop + kSubTopicTextX,
                        y + kSubTopicTextY);
}

int StonePlankHeight(GameContext& ctx) {
    const Texture* tex = ThreeRows(ctx);
    const TcTexture::Image* img = tex ? tex->Frame(0) : nullptr;
    if (!img) return 0;
    return img->Height - kStonePlankShort;
}

void DrawStonePlank(GameContext& ctx, Surface& dst, const std::string& text,
                    int y) {
    const Texture* tex = ThreeRows(ctx);
    const TcTexture::Image* img = tex ? tex->Frame(0) : nullptr;
    if (img)
        dst.Blit(img->Pixels.data(), img->Width, img->Height, 0,
                 y + kStonePlankDrop);
    // Wrapped, not centred: the row hands its text to the wrapping draw, and
    // the plank is three lines deep because questions run to three lines.
    const int width = (img ? img->Width : Surface::kWidth) -
                      kStonePlankTextX - kStonePlankWrapInset;
    int ty = y + kStonePlankTextY;
    for (const std::string& line : WrapText(ctx.SmallFont, text, width)) {
        ctx.SmallFont.Draw(dst, line, kStonePlankTextX, ty);
        ty += ctx.SmallFont.Height();
    }
}

void DrawScrollPlank(TextureCache& textures, Surface& dst, int scroll,
                     int most) {
    const Texture* bg =
        textures.Register(kSlotScrollBg, "Data\\Menu\\scroll_bg.tc");
    const Texture* up =
        textures.Register(kSlotScrollUp, "Data\\Menu\\scrollup.tc");
    const Texture* down =
        textures.Register(kSlotScrollDown, "Data\\Menu\\scrolldown.tc");
    if (bg) {
        if (const TcTexture::Image* img = bg->Frame(0))
            dst.Blit(img->Pixels.data(), img->Width, img->Height, kScrollBgX,
                     kScrollBgY);
    }
    // ratio = scroll * 10 / most, clamped to 0..10 -- and the engine's own
    // guard makes an empty range read as "all the way down".
    int ratio = most > 0 ? scroll * 10 / most : 10;
    if (ratio > 10) ratio = 10;
    if (ratio < 0) ratio = 0;
    if (up) {
        if (const TcTexture::Image* img = up->Frame(0))
            dst.Blit(img->Pixels.data(), img->Width, img->Height, kScrollArrowX,
                     kScrollUpY, ratio + kScrollDim);
    }
    if (down) {
        if (const TcTexture::Image* img = down->Frame(0))
            dst.Blit(img->Pixels.data(), img->Width, img->Height, kScrollArrowX,
                     kScrollDownY, kScrollBright - ratio);
    }
}

void DrawSoftKeys(GameContext& ctx, Surface& dst, const std::string& left,
                  const std::string& right) {
    if (left.empty() && right.empty()) return;
    const bool pad = ctx.HostRef.ActiveInput() == InputDevice::kGamepad;
    const Texture* confirm = ctx.Textures.Load(
        pad ? "Data\\icons\\pad_a.tc" : "Data\\icons\\key_space.tc");
    // An Ok/Ok board confirms from either corner, so both show the confirm
    // key; only a corner that actually cancels gets the cancel key.
    const Texture* cancel =
        right == left ? confirm
                      : ctx.Textures.Load(pad ? "Data\\icons\\pad_b.tc"
                                              : "Data\\icons\\key_esc.tc");
    const Texture* leftSb =
        ctx.Textures.Register(0xb0, "Data\\Menu\\left_sb.tc");
    const Texture* rightSb =
        ctx.Textures.Register(0xb1, "Data\\Menu\\right_sb.tc");
    Font& font = ctx.SmallFont;
    auto corner = [&](const std::string& text, const Texture* plank,
                      const Texture* icon, int px, bool rightSide) {
        if (text.empty()) return;
        int plankW = 56;
        if (plank && plank->Valid()) {
            plankW = plank->Width;
            if (const TcTexture::Image* img = plank->Frame(0))
                dst.Blit(img->Pixels.data(), img->Width, img->Height, px,
                         kSoftY);
        }
        const int tw = font.Width(text);
        const int tx = rightSide ? px + kSoftTextRight - tw : px + kSoftTextX;
        font.Draw(dst, text, tx, kSoftY + kSoftTextY);
        if (!icon || !icon->Valid()) return;
        const TcTexture::Image* img = icon->Frame(0);
        if (!img) return;
        // The key sits centred over its plank's visible part, just above it.
        const int visible = rightSide ? Surface::kWidth - px : plankW + px;
        int ix = rightSide ? px + (visible - img->Width) / 2
                            : (visible - img->Width) / 2;
        if (ix < 1) ix = 1;
        if (ix + img->Width > Surface::kWidth - 1)
            ix = Surface::kWidth - 1 - img->Width;
        dst.Blit(img->Pixels.data(), img->Width, img->Height, ix,
                 kSoftY - img->Height - 1);
    };
    corner(left, leftSb, confirm, kSoftLeftX, false);
    corner(right, rightSb, cancel, kSoftRightX, true);
}

}  // namespace chrome
}  // namespace bb
