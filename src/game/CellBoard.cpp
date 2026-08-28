#include "game/CellBoard.h"

#include <algorithm>
#include <string>

#include "game/BattleData.h"
#include "game/BattleField.h"
#include "game/BattlePanels.h"
#include "game/BattleScreen.h"
#include "game/BuildMenu.h"
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

// The engine's own slot numbers, so the board asks for artwork the way the
// original does.
constexpr uint16_t kSlotUnitBg = 0xc6;      // build_bg.tc
constexpr uint16_t kSlotStatsBg = 0xc5;
constexpr uint16_t kSlotGroundBg = 0xc4;    // info_bg.tc
constexpr uint16_t kSlotTextBg = 0xb8;      // bv_text_bg.tc
constexpr uint16_t kSlotSelectName = 0xc9;
constexpr uint16_t kSlotSelectSmall = 0xc7;
constexpr uint16_t kSlotShield = 0x34;
constexpr uint16_t kSlotAmount = 0x4d;
constexpr uint16_t kSlotAdBar = 0xbd;
constexpr uint16_t kSlotAttack = 0xa0;
constexpr uint16_t kSlotDefence = 0xa1;
constexpr uint16_t kSlotCargoBg = 0xe3;
constexpr uint16_t kSlotTerrainLarge = 0x51;
constexpr uint16_t kSlotPropLarge = 0x53;
constexpr uint16_t kSlotRangeClose = 0xa2;
constexpr uint16_t kSlotRangeFar = 0xa3;
constexpr uint16_t kSlotFamilyBase = 0x3d;  // foot, horse, carriage, sails, row
constexpr uint16_t kSlotGroupBase = 0x9a;
constexpr uint16_t kSlotRoleBase = 0xa4;

void Blit(Surface& dst, const Texture* tex, int frame, int x, int y) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(frame);
    if (!img) img = tex->Frame(0);
    if (!img) return;
    dst.Blit(img->Pixels.data(), img->Width, img->Height, x, y);
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

// The selection sprites all breathe the same way: a counter that runs up to
// the frame count and back down (0x10053f04's `__modsi3`).
int PingPongFrame(uint32_t tick, int frames) {
    if (frames <= 1) return 0;
    const int span = frames * 2;
    int f = int(tick % uint32_t(span));
    if (f >= frames) f = span - (f + 1);
    return f;
}

// Which of the six role icons a unit wears (0x100530f8's first switch): the
// two transport-ish classes share the production icon and the two
// indirect-ish ones share indirect.
int RoleIcon(int unitClass) {
    switch (unitClass) {
        case kClassDirectCapture: return 0;  // 0xa4 capture
        case kClassDirect: return 1;         // 0xa5 close
        case kClassIndirect:
        case kClassStationary: return 2;     // 0xa6 indirect
        case kClassTransport:
        case kClassProduction: return 5;     // 0xa9 production
        default: return -1;
    }
}

}  // namespace

// The cost column prices one unit type and the passability column asks
// another. That is the engine's, not a slip of the port's: 0x10052628 reads
// the cost table at 1, 4, 9, 13 and 10 and the movement masks at 1, 4, 9, 16
// and 10. The sails column therefore prices a Mortar Boat and asks a
// Man-o-War, which is the stricter of the two -- a Man-o-War cannot enter
// shallow water and the Mortar Boat can.
const int CellBoard::kFamilyCostUnit[CellBoard::kFamilies] = {
    kUnitPistoleer,     // foot
    kUnitCavalryLight,  // horse
    kUnitWagon,         // wheels
    kUnitGalley,        // deep-sea ship
    kUnitRowingBoat,    // small ship
};
const int CellBoard::kFamilyEnterUnit[CellBoard::kFamilies] = {
    kUnitPistoleer, kUnitCavalryLight, kUnitWagon, kUnitManOWar,
    kUnitRowingBoat,
};

bool CellBoard::Load(GameContext& ctx) {
    TextureCache& t = ctx.Textures;
    m_UnitBg = t.Register(kSlotUnitBg, "Data\\Menu\\build_bg.tc");
    m_GroundBg = t.Register(kSlotGroundBg, "Data\\Menu\\info_bg.tc");
    m_TextBg = t.Register(kSlotTextBg, "Data\\Menu\\bv_text_bg.tc");
    m_SelectName = t.Register(kSlotSelectName, "Data\\Menu\\select_name.tc");
    m_SelectSmall = t.Register(kSlotSelectSmall, "Data\\Menu\\select_small.tc");
    m_Shield = t.Register(kSlotShield, "Data\\icons\\defense.tc");
    m_Amount = t.Register(kSlotAmount, "Data\\icons\\amount.tc");
    // The attack table's own artwork, brought up here rather than on the frame
    // it is first drawn on: a console with a disc in it should not stop to
    // decode a sprite mid-turn.
    t.Register(kSlotStatsBg, "Data\\Menu\\stats_bg.tc");
    t.Register(kSlotAdBar, "Data\\icons\\ad_bar.tc");
    t.Register(kSlotAttack, "Data\\icons\\unit_class\\attack.tc");
    t.Register(kSlotDefence, "Data\\icons\\unit_class\\defense.tc");
    m_CargoBg = t.Register(kSlotCargoBg, "Data\\Menu\\cargo_icon_bg.tc");
    m_TerrainLarge = t.Register(kSlotTerrainLarge, "Data\\icons\\terrain_large.tc");
    m_PropLarge = t.Register(kSlotPropLarge, "Data\\icons\\prop_large.tc");
    m_RangeClose = t.Register(kSlotRangeClose, "Data\\icons\\range_close.tc");
    m_RangeFar = t.Register(kSlotRangeFar, "Data\\icons\\range_indirect.tc");

    static const char* const kFigurePaths[6] = {
        "Data\\icons\\money.tc",   "Data\\icons\\movement.tc",
        "Data\\icons\\vision.tc",  "Data\\icons\\health.tc",
        "Data\\icons\\rations.tc", "Data\\icons\\ammo.tc",
    };
    static const uint16_t kFigureSlots[6] = {0x37, 0x3b, 0x3a, 0x36, 0x38, 0x35};
    for (int i = 0; i < 6; ++i)
        m_Figures[i] = t.Register(kFigureSlots[i], kFigurePaths[i]);

    static const char* const kFamilyPaths[kFamilies] = {
        "Data\\icons\\unit_type\\foot.tc",
        "Data\\icons\\unit_type\\horse.tc",
        "Data\\icons\\unit_type\\carriage.tc",
        "Data\\icons\\unit_type\\sails.tc",
        "Data\\icons\\unit_type\\rowing.tc",
    };
    for (int i = 0; i < kFamilies; ++i)
        m_Families[i] = t.Register(uint16_t(kSlotFamilyBase + i), kFamilyPaths[i]);

    static const char* const kGroupPaths[6] = {
        "Data\\icons\\unit_class\\infantry.tc",
        "Data\\icons\\unit_class\\artillery.tc",
        "Data\\icons\\unit_class\\cavalry.tc",
        "Data\\icons\\unit_class\\cannon_towers.tc",
        "Data\\icons\\unit_class\\small_sea.tc",
        "Data\\icons\\unit_class\\large_ships.tc",
    };
    for (int i = 0; i < 6; ++i)
        m_Groups[i] = t.Register(uint16_t(kSlotGroupBase + i), kGroupPaths[i]);

    static const char* const kRolePaths[6] = {
        "Data\\icons\\combat_type\\capture.tc",
        "Data\\icons\\combat_type\\close.tc",
        "Data\\icons\\combat_type\\indirect.tc",
        "Data\\icons\\combat_type\\transport.tc",
        "Data\\icons\\combat_type\\stationary.tc",
        "Data\\icons\\combat_type\\production.tc",
    };
    for (int i = 0; i < 6; ++i)
        m_Roles[i] = t.Register(uint16_t(kSlotRoleBase + i), kRolePaths[i]);

    for (int type = 0; type < kUnitTypeCount; ++type) {
        const std::string small = BuildMenu::IconPath("small", type);
        const std::string large = BuildMenu::IconPath("large", type);
        // Both sets stay palette-indexed: the owner's sixteen colours go over
        // the top at draw time, which only works on indices.
        if (!small.empty()) m_SmallIcon[type] = ctx.Textures.LoadIndexed(small);
        if (!large.empty()) m_LargeIcon[type] = ctx.Textures.LoadIndexed(large);
    }

    m_Ready = m_UnitBg && m_GroundBg && m_TextBg;
    if (!m_Ready) LogError("cell board: art missing\n");
    return m_Ready;
}

CellBoard::Page CellBoard::PageFor(const BattleField& field, int cellX,
                                   int cellY, int viewer) {
    if (!field.InBounds(cellX, cellY)) return kNone;
    // A unit behind the fog is not there as far as this board is concerned:
    // 0x1007f3a0 asks the same visibility question before it reaches for one.
    const bool seen = field.Visible(viewer, cellX, cellY);
    const BattleField::Cell& c = field.At(cellX, cellY);
    if (seen && c.Unit >= 0) {
        const BattleField::Unit* u = field.UnitByIndex(c.Unit);
        if (u && u->Alive && u->Carrier < 0) return kUnit;
    }
    return c.Property >= 0 ? kProperty : kTerrain;
}

int CellBoard::StopsOn(Page page) {
    switch (page) {
        case kUnit: return kUnitStops;
        case kTerrain: return kTerrainStops;
        case kProperty: return kPropertyStops;
        default: return 0;
    }
}

int CellBoard::NoteFor(Page page, int slot) {
    if (slot <= 0) return 0;  // the subject's own description
    if (page == kUnit) {
        switch (slot) {
            case 1: return kNoteCost;
            case 2: return kNoteMovement;
            case 3: return kNoteVision;
            case 4: return kNoteHealth;
            case 5: return kNoteRations;
            case 6: return kNoteAmmo;
            case 7: return kNoteRange;
            // The role icon says what the unit is *for* and the group icon
            // what it counts as in a fight; 0x100530f8 hands them 609 and 608
            // in that order.
            case 8: return kNoteRole;
            case 9: return kNoteGroup;
            default: return 0;
        }
    }
    if (slot == 1) return kNoteShields;
    if (slot >= 2 && slot <= 6) return kNoteFootCost + (slot - 2);
    if (slot == 7 && page == kProperty) return kNoteIncome;
    return 0;
}

std::string CellBoard::Description(const GameContext& ctx, int longID) {
    std::string out;
    if (longID <= 0) return out;
    for (int i = 0; i < kDescLines; ++i) {
        const std::string& line = ctx.StringsRef.Get(longID * kDescScale + i);
        if (line.empty()) continue;
        if (!out.empty()) out += '\n';
        out += line;
    }
    return out;
}

// --- drawing ----------------------------------------------------------------

void CellBoard::DrawRow(Surface& dst, GameContext& ctx, int x, int y,
                        const std::string& text, const Texture* icon,
                        bool selected, uint32_t tick) const {
    // 0x1005435c: the icon first, then the value fifteen pixels past it.
    int tx = x;
    if (icon) {
        Blit(dst, icon, 0, x, y);
        tx += kRowIconWidth;
    }
    if (!text.empty())
        ctx.SmallFont.Draw(dst, text, tx + kRowTextInset, y + kRowTextDrop);
    if (!selected || !m_SelectSmall) return;
    Blit(dst, m_SelectSmall, PingPongFrame(tick, int(m_SelectSmall->Frames.size())),
         x, y);
}

void CellBoard::DrawName(Surface& dst, GameContext& ctx, int x, int y,
                         const std::string& text, bool selected,
                         uint32_t tick) const {
    ctx.SmallFont.Draw(dst, text, x, y + kNameY);
    if (!selected || !m_SelectName) return;
    // 0x10053f04 puts the wider box four pixels up and to the left of the
    // name it frames.
    Blit(dst, m_SelectName, PingPongFrame(tick, int(m_SelectName->Frames.size())),
         x - 4, y - 4);
}

void CellBoard::DrawLines(Surface& dst, GameContext& ctx, int base,
                          int y) const {
    Blit(dst, m_TextBg, 0, kTextX, y);
    const Font& f = ctx.SmallFont;
    int ly = y;
    for (int i = 0; i < kDescLines; ++i) {
        const std::string& line = ctx.StringsRef.Get(base + i);
        // A blank line still takes its height: the translators laid the eight
        // out as a block and a run of them is deliberate spacing.
        if (!line.empty())
            f.Draw(dst, line, kTextX + kTextInset, ly);
        ly += f.Height();
    }
}

void CellBoard::DrawDescription(Surface& dst, GameContext& ctx, int longID,
                                int y) const {
    if (longID <= 0) {
        Blit(dst, m_TextBg, 0, kTextX, y);
        return;
    }
    DrawLines(dst, ctx, longID * kDescScale, y);
}

void CellBoard::DrawNote(Surface& dst, GameContext& ctx, int note,
                         int y) const {
    DrawLines(dst, ctx, note * kDescScale, y);
}

// 0x100530f8, with 0x10052d4c's live numbers rather than 0x10052fcc's table
// ones -- the difference between "what a Mortar is" and "what this Mortar has
// left".
void CellBoard::DrawUnitCard(Surface& dst, GameContext& ctx,
                             const BattleField& field,
                             const BattleRenderer& renderer, int unit, int x,
                             int y, int slot, uint32_t tick) const {
    const BattleField::Unit* u = field.UnitByIndex(unit);
    if (!u) return;
    const UnitAttrs& a = field.Data().Unit(u->Type);
    const int colour = field.Colour(u->Owner);

    Blit(dst, m_UnitBg, 0, x, y);
    BlitOwned(dst, m_LargeIcon[u->Type], 0, x + kPortraitX, y + kPortraitY,
              renderer, colour);
    DrawName(dst, ctx, x, y, ctx.StringsRef.Get(BattleData::UnitStringId(u->Type)),
             slot == 0, tick);

    const std::string values[6] = {
        std::to_string(a.Cost),     std::to_string(a.MaxMovement),
        std::to_string(a.Vision),   std::to_string(u->HP),
        std::to_string(u->Rations), std::to_string(u->Ammo),
    };
    for (int i = 0; i < 6; ++i)
        DrawRow(dst, ctx, x + kUnitFigureX,
                y + kUnitFigureFirstY + i * kRowStep, values[i], m_Figures[i],
                slot == i + 1, tick);

    // The attack range: one number for a unit that fights where it stands,
    // "min - max" for one that shoots over other squares, with the icon that
    // says which.
    const bool close = a.MinRange == 1 && a.MaxRange == 1;
    const std::string range =
        close ? std::string("1")
              : std::to_string(a.MinRange) + " - " + std::to_string(a.MaxRange);
    DrawRow(dst, ctx, x + kRangeTextX, y + kRangeIconY, std::string(),
            close ? m_RangeClose : m_RangeFar, slot == 7, tick);
    // The number itself sits on the row below its icon, unindented -- it is
    // the one value the card writes without an icon beside it (0x100542a0).
    ctx.SmallFont.Draw(dst, range, x + kRangeTextX + kRowTextInset,
                        y + kClassIconY + kRowTextDrop);

    // The two class icons sit on the bottom row, and they carry no value of
    // their own -- the picture is the answer.
    const int role = RoleIcon(a.UnitClass);
    DrawRow(dst, ctx, x + kRoleIconX, y + kClassIconY, std::string(),
            role >= 0 ? m_Roles[role] : nullptr, slot == 8, tick);
    const int group = BattleData::GroupOf(u->Type);
    DrawRow(dst, ctx, x + kGroupIconX, y + kClassIconY, std::string(),
            group >= 0 && group < 6 ? m_Groups[group] : nullptr, slot == 9, tick);

    // What it is carrying, in its own little tray (0x10052d4c's tail). One
    // passenger sits fourteen pixels further in than two do.
    const int aboard = int(u->Cargo.size());
    if (aboard <= 0) return;
    Blit(dst, m_CargoBg, 0, x + kCargoBgX, y + kCargoBgY);
    for (int i = 0; i < aboard; ++i) {
        const BattleField::Unit* c = field.UnitByIndex(u->Cargo[std::size_t(i)]);
        if (!c) continue;
        int cx = x + i * kCargoIconStep - 2;
        if (aboard == 1) cx += 0xe;
        BlitOwned(dst, m_SmallIcon[c->Type], 0, cx, y + kCargoIconY, renderer,
                  colour);
    }
}

namespace cards {

// 0x100535f4. Two columns, because the card answers two questions: what this
// unit does to each group, and what each group does to it.
void DrawStatsCard(Surface& dst, GameContext& ctx, const BattleField& field,
                   int type, int x, int y) {
    TextureCache& t = ctx.Textures;
    Blit(dst, t.Register(kSlotStatsBg, "Data\\Menu\\stats_bg.tc"), 0, x, y);

    static const char* const kGroupPaths[kGroups] = {
        "Data\\icons\\unit_class\\infantry.tc",
        "Data\\icons\\unit_class\\artillery.tc",
        "Data\\icons\\unit_class\\cavalry.tc",
        "Data\\icons\\unit_class\\cannon_towers.tc",
        "Data\\icons\\unit_class\\small_sea.tc",
        "Data\\icons\\unit_class\\large_ships.tc",
    };
    for (int i = 0; i < kGroups; ++i)
        Blit(dst, t.Register(uint16_t(kSlotGroupBase + i), kGroupPaths[i]), 0,
             x + kGroupIconX, y + kGroupFirstY + i * kRowStep);

    Blit(dst, t.Register(kSlotAttack, "Data\\icons\\unit_class\\attack.tc"), 0,
         x + kAttackHeaderX, y + kHeaderY);
    Blit(dst, t.Register(kSlotDefence, "Data\\icons\\unit_class\\defense.tc"), 0,
         x + kDefenceHeaderX, y + kHeaderY);

    const Texture* bar = t.Register(kSlotAdBar, "Data\\icons\\ad_bar.tc");
    const Texture* none = t.Register(kSlotAmount, "Data\\icons\\amount.tc");
    for (int i = 0; i < kGroups; ++i) {
        const int by = y + kBarFirstY + i * kRowStep;
        const int values[2] = {field.Data().AttackVersus(type, i),
                               field.Data().DefenceVersus(type, i)};
        const int xs[2] = {x + kAttackBarX, x + kDefenceBarX};
        for (int c = 0; c < 2; ++c) {
            // Zero is not a short bar but a different sprite (0x10053d68).
            if (values[c] == 0) Blit(dst, none, 0, xs[c], by);
            else Blit(dst, bar, values[c], xs[c], by);
        }
    }
}

}  // namespace cards

// 0x10052628.
void CellBoard::DrawGroundCard(Surface& dst, GameContext& ctx,
                               const BattleField& field,
                               const BattleRenderer& renderer, int cellX,
                               int cellY, int x, int y, int slot,
                               uint32_t tick) const {
    const BattleField::Cell& cell = field.At(cellX, cellY);
    const BattleField::Property* p = field.PropertyAt(cellX, cellY);
    const BattleData& data = field.Data();

    Blit(dst, m_GroundBg, 0, x, y);
    std::string name;
    int shields = 0;
    if (p) {
        BlitOwned(dst, m_PropLarge, p->Type, x + kPortraitX, y + kPortraitY,
                  renderer, field.Colour(p->Owner));
        name = ctx.StringsRef.Get(BattleData::PropertyStringId(p->Type));
        shields = data.Property(p->Type).Shield;
    } else {
        Blit(dst, m_TerrainLarge, cell.Terrain, x + kPortraitX, y + kPortraitY);
        name = ctx.StringsRef.Get(BattleData::TerrainStringId(cell.Terrain));
        shields = data.Terrain(cell.Terrain).Shield;
    }
    DrawName(dst, ctx, x, y, name, slot == 0, tick);

    // The defence row shows shields rather than a number, so it is an empty
    // row with the shields laid over it (0x10052008 centres them on the board
    // and packs a building's five two pixels tighter).
    DrawRow(dst, ctx, x + kGroundRowX, y + kGroundRowFirstY, std::string(),
            nullptr, slot == 1, tick);
    for (int i = 0; i < shields; ++i)
        Blit(dst, m_Shield, 0, BattlePanels::ShieldX(i, shields, x + kShieldX),
             y + kGroundRowFirstY);

    // A castle is not ground anyone walks on, so 0x10052628 crosses out all
    // five families over one rather than pricing them.
    const bool impassable = p && p->Type >= kPropSpecialCastle1;
    for (int i = 0; i < kFamilies; ++i) {
        const int costType = kFamilyCostUnit[i];
        const int enterType = kFamilyEnterUnit[i];
        const bool can = !impassable && field.CanEnter(enterType, cellX, cellY);
        const int rx = x + kGroundRowX;
        const int ry = y + kGroundRowFirstY + (i + 1) * kRowStep;
        DrawRow(dst, ctx, rx, ry,
                can ? std::to_string(field.MoveCost(costType, cellX, cellY))
                    : std::string(),
                m_Families[i], slot == i + 2, tick);
        // No number, but not nothing: the engine drops `amount.tc` where the
        // value would have gone (0x1005410c).
        if (!can) Blit(dst, m_Amount, 0, rx + kNoValueX, ry + kNoValueY);
    }

    if (!p) return;
    DrawRow(dst, ctx, x + kGroundRowX, y + kRangeIconY,
            std::to_string(data.Property(p->Type).CashRate), m_Figures[0],
            slot == 7, tick);
}

// --- the state --------------------------------------------------------------

bool CellBoard::Run(GameContext& ctx, BattleSession& session,
                    const BattleRenderer::View& view) {
    Host& host = ctx.HostRef;
    if (!m_Ready) return true;
    const BattleField& field = session.Field();
    const int cx = view.CursorX, cy = view.CursorY;
    Page page = PageFor(field, cx, cy, view.Viewer);
    if (page == kNone) return true;

    host.FlushKeys();
    int slot = 0;
    uint32_t tick = 0;
    uint32_t lastStep = 0;

    while (!host.QuitRequested()) {
        const int stops = StopsOn(page);
        if (slot >= stops) slot = 0;
        if (slot < 0) slot = stops - 1;

        Surface& screen = host.Screen();
        screen.Fill(0xF000);
        // The board is a *pushed* state and the map goes on underneath it, so
        // the battlefield is drawn every frame exactly as the browse state
        // left it -- same camera, same cursor, same fog.
        BattleRenderer::View v = view;
        v.Ticks = host.TickCount();
        session.Renderer().Draw(screen, session.Field(), v);

        // Where the cards land depends on where the cursor is, so that
        // whatever the board is describing stays visible next to it.
        const int sx = BattleRenderer::ScreenX(cx, view);
        const int sy = BattleRenderer::ScreenY(cy, view);
        const int cardY = sy < kCursorSplitY
                               ? (page == kUnit ? kUnitCardLowY : kGroundCardLowY)
                               : 0;
        if (page == kUnit) {
            const int unit = field.At(cx, cy).Unit;
            const BattleField::Unit* u = field.UnitByIndex(unit);
            if (!u) return true;
            const int statsX = sx < kCursorSplitX ? kStatsDodgeX : 0;
            const int statsY = sy < kCursorSplitY ? kStatsHighY : kStatsLowY;
            DrawUnitCard(screen, ctx, field, session.Renderer(), unit, 0,
                         cardY, slot, tick);
            const int note = NoteFor(page, slot);
            if (note != 0) DrawNote(screen, ctx, note, cardY);
            else DrawDescription(screen, ctx,
                                 BattleData::UnitStringId(u->Type, true),
                                 cardY);
            // The attack table goes on last, and it is why it never lands on
            // the description panel: the two share the screen, not a corner.
            cards::DrawStatsCard(screen, ctx, field, u->Type, statsX,
                                 statsY);
        } else {
            DrawGroundCard(screen, ctx, field, session.Renderer(), cx, cy, 0,
                           cardY, slot, tick);
            const int note = NoteFor(page, slot);
            if (note != 0) {
                DrawNote(screen, ctx, note, cardY);
            } else {
                const BattleField::Property* p = field.PropertyAt(cx, cy);
                const int id =
                    p ? BattleData::PropertyStringId(p->Type, true)
                      : BattleData::TerrainStringId(field.At(cx, cy).Terrain,
                                                    true);
                DrawDescription(screen, ctx, id, cardY);
            }
        }

        host.Flip();
        if (ctx.Sound) ctx.Sound->Pump(host);
        host.Sleep(BattleScreen::kFrameMs);
        ++tick;
        if (host.QuitRequested()) return false;

        // The highlight walks on a held direction rather than on a press, and
        // it repeats every 160 ms (0x1007f554). Left and up step back, right
        // and down step on.
        const bool back = host.KeyHeld(Key::kLeft) || host.KeyHeld(Key::kUp);
        const bool forward = host.KeyHeld(Key::kRight) || host.KeyHeld(Key::kDown);
        const uint32_t now = host.TickCount();
        if (!back && !forward) {
            lastStep = 0;
        } else if (lastStep == 0 || now - lastStep >= kRepeatMs) {
            lastStep = now;
            if (back) --slot;
            if (forward) ++slot;
            if (ctx.Sound) ctx.Sound->PlayMenu(SoundManager::kSoundMove);
        }
        // Consume the presses those holds also produced, so that letting go
        // and pressing again does not step twice.
        host.KeyPressed(Key::kLeft);
        host.KeyPressed(Key::kRight);
        host.KeyPressed(Key::kUp);
        host.KeyPressed(Key::kDown);

        // The info key turns the page from the unit to the ground under it and
        // closes the board from there; every other key closes it outright
        // (0x1007f470).
        if (host.KeyPressed(Key::kInfo)) {
            if (page == kUnit) {
                page = field.At(cx, cy).Property >= 0 ? kProperty : kTerrain;
                slot = 0;
                lastStep = 0;
                continue;
            }
            break;
        }
        if (host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kBack) ||
            host.KeyPressed(Key::kSoftLeft) || host.KeyPressed(Key::kSoftRight) ||
            host.KeyPressed(Key::kNextUnit) || host.KeyPressed(Key::kPrevUnit) ||
            host.KeyPressed(Key::kMap) || host.KeyPressed(Key::kRange))
            break;
    }
    if (host.QuitRequested()) return false;
    if (ctx.Sound) ctx.Sound->PlayMenu(SoundManager::kSoundCancel);
    host.FlushKeys();
    return true;
}

}  // namespace bb
