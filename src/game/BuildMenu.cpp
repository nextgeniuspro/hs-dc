#include "game/BuildMenu.h"

#include <algorithm>

#include "game/BattleData.h"
#include "game/BattleField.h"
#include "game/BattlePanels.h"
#include "game/BattleRenderer.h"
#include "game/CellBoard.h"
#include "game/Font.h"
#include "game/Game.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/TcTexture.h"
#include "game/TextureCache.h"
#include "platform/Host.h"
#include "shim/Log.h"

namespace bb {
namespace {

// The resource slots the build screen uses, as the engine numbers them.
constexpr uint16_t kSlotMoney = 0x37;
constexpr uint16_t kSlotSelectBig = 0xc8;
constexpr uint16_t kSlotRowBg = 0xc3;
constexpr uint16_t kSlotInfoBg = 0xc6;
constexpr uint16_t kSlotStatsBg = 0xc5;
constexpr uint16_t kSlotScrollUp = 0xcc;
constexpr uint16_t kSlotScrollDown = 0xcd;

// The six figures down the portrait's right edge, in the order 0x100530f8
// lists them, and the icon each wears.
constexpr uint16_t kFigureSlots[6] = {
    0x37,  // money -- the price
    0x3b,  // movement
    0x3a,  // vision
    0x36,  // health
    0x38,  // rations
    0x35,  // ammunition
};

// The small and large icon sets have eighteen pictures for twenty-one types:
// the three loaded rowing boats share the rowing boat's. 0x10054480 spells the
// mapping out; the small set (0x61..) follows the same shape one base lower.
int IconOf(int unitType) {
    switch (unitType) {
        case 0: case 1: case 2: case 3: case 4:
        case 5: case 6: case 7: case 8:
            return unitType;                 // swordsman .. scorch cannon
        case 9: return 9;                     // wagon -> chariot
        case 10: case 18: case 19: case 20:
            return 12;                        // every rowing boat
        case 11: return 10;                   // sloop
        case 12: return 17;                   // heavy transport -> hut
        case 13: return 11;                   // galley
        case 14: return 15;                   // H.I.D.S.U.
        case 15: return 14;                   // mothership
        case 16: return 13;                   // man-o-war
        case 17: return 16;                   // cannon tower
        default: return -1;
    }
}

std::string IconFile(const char* set, int icon) {
    static const char* const kNames[18] = {
        "swordmen",  "pistoleer",  "musketeer",   "scout",
        "light-cavalry", "heavy-cavalry", "mortar", "cannon",
        "scorch-cannon", "chariot",  "sloop",      "galley",
        "rowing-boat", "man-o-war", "mothership", "hidsu",
        "cannon-tower", "hut",
    };
    if (icon < 0 || icon >= 18) return std::string();
    return std::string("Data\\icons\\units_") + set + "\\" + kNames[icon] +
           ".tc";
}

void Blit(Surface& dst, const Texture* tex, int frame, int x, int y) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(frame);
    if (!img) img = tex->Frame(0);
    if (!img) return;
    dst.Blit(img->Pixels.data(), img->Width, img->Height, x, y);
}

// The two-lamp scrollbar draws both arrows always and varies their alpha.
void BlitAlpha(Surface& dst, const Texture* tex, int x, int y, int alpha) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(0);
    if (!img) return;
    dst.Blit(img->Pixels.data(), img->Width, img->Height, x, y,
             std::clamp(alpha, 0, 15));
}

void BlitOwned(Surface& dst, const Texture* tex, int frame, int x, int y,
               const BattleRenderer& renderer, int colour) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(frame);
    if (!img) img = tex->Frame(0);
    if (!img) return;
    const uint16_t* lut = renderer.OwnerLut(colour);
    if (!lut) return;
    dst.BlitIndexed(img->Pixels.data(), img->Width, img->Height, x, y, lut,
                    renderer.LutSize());
}

// The same alpha ping-pong the selection sprites use everywhere: a counter
// that runs up to the frame count and back down (0x10053f04's `__modsi3`).
int PingPongFrame(uint32_t tick, int frames) {
    if (frames <= 1) return 0;
    const int span = frames * 2;
    int f = int(tick % uint32_t(span));
    if (f >= frames) f = span - (f + 1);
    return f;
}

constexpr uint16_t kMoneyColour = 0xFFF0;

}  // namespace

int BuildMenu::GroupOf(int unitType) { return BattleData::GroupOf(unitType); }

std::string BuildMenu::IconPath(const char* set, int unitType) {
    return IconFile(set, IconOf(unitType));
}

bool BuildMenu::Load(GameContext& ctx) {
    TextureCache& t = ctx.Textures;
    m_RowBg = t.Register(kSlotRowBg, "Data\\Menu\\build_item_bg.tc");
    m_InfoBg = t.Register(kSlotInfoBg, "Data\\Menu\\build_bg.tc");
    m_StatsBg = t.Register(kSlotStatsBg, "Data\\Menu\\stats_bg.tc");
    m_Select = t.Register(kSlotSelectBig, "Data\\Menu\\select_big.tc");
    m_ScrollUp = t.Register(kSlotScrollUp, "Data\\Menu\\buildscrollup.tc");
    m_ScrollDown = t.Register(kSlotScrollDown, "Data\\Menu\\buildscrolldown.tc");
    m_Coin = t.Register(kSlotMoney, "Data\\icons\\money.tc");

    static const char* const kFigurePaths[6] = {
        "Data\\icons\\money.tc",    "Data\\icons\\movement.tc",
        "Data\\icons\\vision.tc",   "Data\\icons\\health.tc",
        "Data\\icons\\rations.tc",  "Data\\icons\\ammo.tc",
    };
    for (int i = 0; i < 6; ++i)
        m_Figures[i] = t.Register(kFigureSlots[i], kFigurePaths[i]);

    for (int type = 0; type < kUnitTypeCount; ++type) {
        const int icon = IconOf(type);
        if (icon < 0) continue;
        // Both sets are palette-indexed: the engine copies sixteen entries of
        // paletteCol.pal over the top of the shared palette before drawing one
        // (0x1005a164, 0x10054480), which only works while the pixels are
        // still indices.
        m_SmallIcon[type] = t.LoadIndexed(IconFile("small", icon));
        m_LargeIcon[type] = t.LoadIndexed(IconFile("large", icon));
    }

    m_Ready = m_RowBg && m_InfoBg && m_StatsBg;
    if (!m_Ready) LogError("build menu: art missing\n");
    return m_Ready;
}

std::vector<BuildMenu::Row> BuildMenu::Rows(const BattleField& field,
                                            int propertyIndex) {
    std::vector<Row> rows;
    if (propertyIndex < 0 ||
        propertyIndex >= int(field.Properties().size()))
        return rows;
    const BattleField::Property& p =
        field.Properties()[std::size_t(propertyIndex)];
    std::vector<int> types;
    field.Producible(propertyIndex, types);
    const int purse = p.Owner >= 1 && p.Owner <= BattleField::kMaxPlayers
                          ? field.Players()[std::size_t(p.Owner)].Cash
                          : 0;
    for (int type : types) {
        Row r;
        r.Type = type;
        // What it costs *this* seat: Golden Age is a discount on the table's
        // price, and the row has to show what will actually be charged.
        r.Cost = field.UnitPrice(p.Owner, type);
        // 0x1005a90c enables a row when the price is *at most* the purse.
        r.Affordable = r.Cost <= purse;
        rows.push_back(r);
    }
    return rows;
}

void BuildMenu::DrawRow(Surface& dst, GameContext& ctx,
                        const BattleField& field,
                        const BattleRenderer& renderer, const Row& row,
                        int y) const {
    // Frame 1 is the grey stone slab the engine puts under a row you cannot
    // pay for (0x1005a164 hands `cost > purse` straight to the blit as its
    // frame index).
    Blit(dst, m_RowBg, row.Affordable ? 0 : 1, kRowX, y);
    const int colour = field.Colour(field.CurrentPlayer());
    BlitOwned(dst, m_SmallIcon[row.Type], 0, kRowX + kRowIconX, y + kRowIconY,
              renderer, colour);
    Blit(dst, m_Coin, 0, kRowX + kRowCoinX, y + kRowCoinY);
    // 0x1005a164 draws both into the row's own 71x26 surface: the name at
    // (0x22, -1), a pixel above its top edge, and the price at (0x2e, 11) in
    // gold, level with the coin.
    const Font& f = ctx.SmallFont;
    f.Draw(dst, ctx.StringsRef.Get(BattleData::UnitStringId(row.Type)),
           kRowX + kRowNameX, y + kRowNameY);
    f.DrawTinted(dst, std::to_string(row.Cost), kRowX + kRowCostX,
                 y + kRowCostY, kMoneyColour);
}

// 0x100530f8: the portrait, its name, and six figures down the right edge.
void BuildMenu::DrawInfoCard(Surface& dst, GameContext& ctx,
                             const BattleField& field,
                             const BattleRenderer& renderer, int type,
                             int colour) const {
    Blit(dst, m_InfoBg, 0, kCardX, kInfoY);
    BlitOwned(dst, m_LargeIcon[type], 0, kCardX + kPortraitX,
              kInfoY + kPortraitY, renderer, colour);
    const Font& f = ctx.SmallFont;
    f.Draw(dst, ctx.StringsRef.Get(BattleData::UnitStringId(type)), kCardX,
           kInfoY - 2);

    const UnitAttrs& a = field.Data().Unit(type);
    // The fourth figure is blank: 0x10052fcc passes an empty string where the
    // health icon sits, because every unit starts at full health.
    const std::string values[6] = {
        std::to_string(a.Cost),      std::to_string(a.MaxMovement),
        std::to_string(a.Vision),    std::string(),
        std::to_string(a.MaxRations), std::to_string(a.MaxAmmo),
    };
    for (int i = 0; i < 6; ++i) {
        const int x = kCardX + kFigureX;
        const int y = kInfoY + kFigureFirstY + i * kFigureStep;
        Blit(dst, m_Figures[i], 0, x, y);
        if (!values[i].empty()) f.Draw(dst, values[i], x + 15 + 4, y + 3);
    }
}

// 0x100535f4, which the cell board draws too -- see cards::DrawStatsCard.
void BuildMenu::DrawStatsCard(Surface& dst, GameContext& ctx,
                              const BattleField& field, int type) const {
    cards::DrawStatsCard(dst, ctx, field, type, kCardX, kStatsY);
}

void BuildMenu::Draw(Surface& dst, GameContext& ctx, const BattleField& field,
                     BattleRenderer& renderer, BattlePanels& panels,
                     const BattleRenderer::View& view,
                     const std::vector<Row>& rows, int cursor, int scroll,
                     uint32_t tick) const {
    // The engine's build screen is a *pushed* state and clears nothing, so
    // whatever the map left on the screen shows through the gaps between the
    // boards. Redrawing it is the same picture and does not depend on the
    // previous frame still being there.
    dst.Fill(0xF000);
    renderer.Draw(dst, field, view);
    panels.DrawPlayer(dst, field, 0, 0);

    for (int i = 0; i < int(rows.size()); ++i) {
        const int y = kRowTop + scroll + i * kRowStep;
        if (y + kRowStep < 0 || y > Surface::kHeight) continue;
        DrawRow(dst, ctx, field, renderer, rows[std::size_t(i)], y);
        if (i == cursor && m_Select) {
            const int frame =
                PingPongFrame(tick, int(m_Select->Frames.size()));
            Blit(dst, m_Select, frame, 0, y - 5);
        }
    }

    // The two-lamp scrollbar: both arrows are always drawn, and it is their
    // alpha that says which way there is more (0x1005a52c).
    const int span = int(rows.size()) * kRowStep - kWindow;
    if (span > 0) {
        const int ratio = std::clamp((-scroll * 10) / span, 0, 10);
        BlitAlpha(dst, m_ScrollUp, kArrowX, kArrowUpY, ratio + 5);
        BlitAlpha(dst, m_ScrollDown, kArrowX, kArrowDownY, 0xf - ratio);
    }

    if (cursor >= 0 && cursor < int(rows.size())) {
        const int type = rows[std::size_t(cursor)].Type;
        DrawInfoCard(dst, ctx, field, renderer, type,
                     field.Colour(field.CurrentPlayer()));
        DrawStatsCard(dst, ctx, field, type);
    }
}

int BuildMenu::Run(GameContext& ctx, BattleField& field,
                   BattleRenderer& renderer, BattlePanels& panels,
                   const BattleRenderer::View& view, int propertyIndex) {
    if (!m_Ready) return -1;
    const std::vector<Row> rows = Rows(field, propertyIndex);
    if (rows.empty()) return -1;

    Host& host = ctx.HostRef;
    host.FlushKeys();
    int cursor = 0;
    int scroll = 0;
    uint32_t tick = 0;
    const int span = std::max(0, int(rows.size()) * kRowStep - kWindow);
    while (!host.QuitRequested()) {
        // Keep the highlighted row inside the window, which is what the
        // engine's scroll accumulator settles on.
        const int want = std::clamp(
            -(cursor * kRowStep - (kWindow - kRowStep) / 2), -span, 0);
        if (scroll < want) scroll = std::min(want, scroll + 4);
        if (scroll > want) scroll = std::max(want, scroll - 4);

        Draw(host.Screen(), ctx, field, renderer, panels, view, rows, cursor,
             scroll, tick);
        host.Flip();
        if (ctx.Sound) ctx.Sound->Pump(host);
        host.Sleep(20);
        ++tick;

        const int n = int(rows.size());
        if (host.KeyPressed(Key::kUp) && n > 0) {
            cursor = (cursor + n - 1) % n;
            if (ctx.Sound) ctx.Sound->PlayMenu(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kDown) && n > 0) {
            cursor = (cursor + 1) % n;
            if (ctx.Sound) ctx.Sound->PlayMenu(SoundManager::kSoundMove);
        }
        if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight)) {
            if (ctx.Sound) ctx.Sound->PlayMenu(SoundManager::kSoundCancel);
            return -1;
        }
        if (host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kSoftLeft)) {
            const Row& r = rows[std::size_t(cursor)];
            if (!r.Affordable) {
                if (ctx.Sound) ctx.Sound->PlayMenu(SoundManager::kSoundCancel);
                continue;
            }
            if (ctx.Sound) ctx.Sound->PlayMenu(SoundManager::kSoundEnter);
            return r.Type;
        }
    }
    return -1;
}

}  // namespace bb
