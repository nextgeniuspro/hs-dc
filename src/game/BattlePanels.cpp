#include "game/BattlePanels.h"

#include <string>

#include "game/BattleData.h"
#include "game/BattleField.h"
#include "game/Font.h"
#include "game/Strings.h"
#include "game/TextureCache.h"
#include "shim/Log.h"

namespace bb {
namespace {

// Resource slots, in the order the startup loader registers them.
constexpr uint16_t kSlotFlags = 0xbc;        // Data\icons\flags.tc
constexpr uint16_t kSlotCommanderBg = 0xca;  // Data\Menu\commander_bg.tc
constexpr uint16_t kSlotPerkMeter = 0xba;    // Data\Menu\perkmeter.tc
constexpr uint16_t kSlotPerkPointer = 0xbb;  // Data\Menu\perkpointer.tc
constexpr uint16_t kSlotTerrainBg = 0xcb;    // Data\Menu\terrain_bg.tc
constexpr uint16_t kSlotTerrainIcons = 0x52; // Data\icons\terrain_small.tc
constexpr uint16_t kSlotPropBg = 0x56;       // Data\Menu\bv_prop_bg.tc
constexpr uint16_t kSlotUnitBg = 0xb7;       // Data\Menu\bv_square.tc
constexpr uint16_t kSlotDefense = 0x34;      // Data\icons\defense.tc
constexpr uint16_t kSlotAmmo = 0x35;
constexpr uint16_t kSlotHealth = 0x36;
constexpr uint16_t kSlotMoney = 0x37;
constexpr uint16_t kSlotRations = 0x38;
constexpr uint16_t kSlotCapture = 0x39;

// The small unit portraits, resource slots 0x61..0x78, listed here in unit
// type order the way 0x10054770's switch maps them. Two of the names are the
// artists' rather than the rules': the Wagon's sheet is `chariot` and the
// Heavy Transport's is `hut`. The three loaded rowing boats reuse the empty
// one's, exactly as that switch does.
const char* const kUnitIcons[kUnitTypeCount] = {
    "Data\\icons\\units_small\\swordmen.tc",        // 0x61
    "Data\\icons\\units_small\\pistoleer.tc",       // 0x62
    "Data\\icons\\units_small\\musketeer.tc",       // 0x63
    "Data\\icons\\units_small\\scout.tc",           // 0x64
    "Data\\icons\\units_small\\light-cavalry.tc",   // 0x65
    "Data\\icons\\units_small\\heavy-cavalry.tc",   // 0x66
    "Data\\icons\\units_small\\mortar.tc",          // 0x67
    "Data\\icons\\units_small\\cannon.tc",          // 0x68
    "Data\\icons\\units_small\\scorch-cannon.tc",   // 0x69
    "Data\\icons\\units_small\\chariot.tc",         // 0x70, the Wagon
    "Data\\icons\\units_small\\rowing-boat.tc",     // 0x73
    "Data\\icons\\units_small\\sloop.tc",           // 0x71
    "Data\\icons\\units_small\\hut.tc",             // 0x78, Transport-Heavy
    "Data\\icons\\units_small\\galley.tc",          // 0x72
    "Data\\icons\\units_small\\hidsu.tc",           // 0x76
    "Data\\icons\\units_small\\mothership.tc",      // 0x75
    "Data\\icons\\units_small\\man-o-war.tc",       // 0x74
    "Data\\icons\\units_small\\cannon-tower.tc",    // 0x77
    "Data\\icons\\units_small\\rowing-boat.tc",
    "Data\\icons\\units_small\\rowing-boat.tc",
    "Data\\icons\\units_small\\rowing-boat.tc",
};

// Where the cursor has to be for a panel to give way. The engine reads the
// cursor's screen pixel position and compares against these (0x1009b494).
constexpr int kTopLeftX = 0x45;      // 69
constexpr int kTopLeftY = 0x27;      // 39
constexpr int kBottomLeftX = 0x31;   // 49
constexpr int kBottomLeftY = 0x8d;   // 141
constexpr int kBottomRightX = 0x65;  // 101
constexpr int kBottomRightYBrowse = 0x83;    // 131
constexpr int kBottomRightYOrdering = 0x97;  // 151

// Panel origins.
constexpr int kPlayerHomeX = 0;
constexpr int kPlayerAwayX = 0x5d;   // 93
constexpr int kCellHomeX = 0;
constexpr int kCellBesideUnitX = 0x41;   // 65, when the unit panel is at 107
constexpr int kCellRightX = 0x87;        // 135, when there is no unit panel
constexpr int kUnitHomeX = 0x6b;     // 107
constexpr int kUnitAwayX = 0x25;     // 37, tucked beside the cell preview

// The unit panel's rows hang off this baseline (0x100520b4's `param_4`).
constexpr int kUnitBaseY = 0xb4;     // 180
// The number in an icon-and-value pair ends this far right of the icon, and
// three rows lower (0x100543ec).
constexpr int kValueRight = 0xf + 0x12;
constexpr int kValueDrop = 3;

// The gap between defence shields, and the narrower one a property's five
// need (0x10052008).
constexpr int kShieldStep = 8;
constexpr int kShieldStepTight = 6;

// Terrain that shows its hit points in place of shields: BreakableWall.
constexpr int kBreakableWall = 10;

// What stands in for a number the player has not scouted (0x10112ac8).
constexpr const char* kUnknown = "--";

void Blit(Surface& dst, const Texture* tex, int frame, int x, int y) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(frame);
    // The engine's blitter falls back to frame 0 for an out-of-range index
    // (0x100b3754's first two lines), and the flag table relies on it.
    if (!img) img = tex->Frame(0);
    if (!img) return;
    dst.Blit(img->Pixels.data(), img->Width, img->Height, x, y);
}

void BlitOwned(Surface& dst, const Texture* tex, int frame, int x, int y,
               const BattleRenderer& renderer, int owner) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(frame);
    if (!img) img = tex->Frame(0);
    if (!img) return;
    const uint16_t* lut = renderer.OwnerLut(owner);
    if (!lut) return;
    dst.BlitIndexed(img->Pixels.data(), img->Width, img->Height, x, y, lut,
                    renderer.LutSize());
}

// A clipped blit, for the meter's filled and empty halves.
void BlitClippedX(Surface& dst, const Texture* tex, int frame, int x, int y,
                  int clipX0, int clipX1) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(frame);
    if (!img) img = tex->Frame(0);
    if (!img) return;
    const Surface::Rect clip{clipX0, 0, clipX1, dst.Height()};
    dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0,
                   img->Width, img->Height, x, y, &clip);
}

}  // namespace

bool BattlePanels::Load(TextureCache& cache, const Font& font) {
    // The panels' portraits are a battle's, not the front end's: twenty-one
    // unit icons and the property set, released when the battle is over.
    m_Claims.emplace(cache);
    TextureSet& textures = *m_Claims;
    m_Font = &font;
    m_Flags = textures.Register(kSlotFlags, "Data\\icons\\flags.tc");
    m_CommanderBg =
        textures.Register(kSlotCommanderBg, "Data\\Menu\\commander_bg.tc");
    m_PerkMeter = textures.Register(kSlotPerkMeter, "Data\\Menu\\perkmeter.tc");
    m_PerkPointer =
        textures.Register(kSlotPerkPointer, "Data\\Menu\\perkpointer.tc");
    m_TerrainBg = textures.Register(kSlotTerrainBg, "Data\\Menu\\terrain_bg.tc");
    m_TerrainIcons =
        textures.Register(kSlotTerrainIcons, "Data\\icons\\terrain_small.tc");
    m_PropBg = textures.Register(kSlotPropBg, "Data\\Menu\\bv_prop_bg.tc");
    m_UnitBg = textures.Register(kSlotUnitBg, "Data\\Menu\\bv_square.tc");
    m_IconDefense = textures.Register(kSlotDefense, "Data\\icons\\defense.tc");
    m_IconAmmo = textures.Register(kSlotAmmo, "Data\\icons\\ammo.tc");
    m_IconHealth = textures.Register(kSlotHealth, "Data\\icons\\health.tc");
    m_IconMoney = textures.Register(kSlotMoney, "Data\\icons\\money.tc");
    m_IconRations = textures.Register(kSlotRations, "Data\\icons\\rations.tc");
    m_IconCapture = textures.Register(kSlotCapture, "Data\\icons\\capture.tc");

    // The building and unit portraits (slots 0x54 and 0x61..0x78) keep their
    // palette indices: they are recoloured per owner, the same way the map's
    // sprites are.
    m_PropIcons = textures.LoadIndexed("Data\\icons\\prop_small.tc");
    for (int t = 0; t < kUnitTypeCount; ++t)
        m_UnitIcons[t] = textures.LoadIndexed(kUnitIcons[t]);

    m_Ready = m_Flags && m_CommanderBg && m_PerkMeter && m_TerrainBg &&
             m_TerrainIcons && m_PropBg && m_PropIcons && m_UnitBg;
    if (!m_Ready) LogError("battle: info panels INCOMPLETE\n");
    return m_Ready;
}

void BattlePanels::Step(uint32_t sinceMoveMs) {
    if (sinceMoveMs < kAppearMs) {
        m_Slide = kSlideStart;
        m_Visible = false;
        return;
    }
    m_Visible = true;
    m_Slide = m_Slide > 0 ? m_Slide - kSlideStep : 0;
    if (m_Slide < 0) m_Slide = 0;

    // The meter's overflow phase walks 0..7 and back down again.
    if (m_BlinkUp) {
        if (++m_Blink > kBlinkPeriod) m_BlinkUp = false;
    } else if (--m_Blink == 0) {
        m_BlinkUp = true;
    }
}

int BattlePanels::PlayerPanelX(int cursorSx, int cursorSy) {
    const bool inTheWay = cursorSy <= kTopLeftY && cursorSx <= kTopLeftX;
    return inTheWay ? kPlayerAwayX : kPlayerHomeX;
}

int BattlePanels::CellPanelX(int cursorSx, int cursorSy, bool unitHere,
                             Layout layout) {
    const bool inTheWay =
        cursorSy >= kBottomLeftY && cursorSx <= kBottomLeftX;
    if (!inTheWay) return kCellHomeX;
    // With a unit panel on the right there is only the middle to move to;
    // without one the preview goes all the way over. While a unit is being
    // ordered the engine only ever uses the middle slot (0x100973c8).
    if (layout == Layout::kOrdering || unitHere) return kCellBesideUnitX;
    return kCellRightX;
}

int BattlePanels::UnitPanelX(int cursorSx, int cursorSy, Layout layout) {
    const int limit = layout == Layout::kOrdering ? kBottomRightYOrdering
                                                  : kBottomRightYBrowse;
    const bool clear = cursorSx < kBottomRightX || cursorSy < limit;
    return clear ? kUnitHomeX : kUnitAwayX;
}

void BattlePanels::DrawValue(Surface& dst, const Texture* icon, int value,
                             int x, int y) const {
    Blit(dst, icon, 0, x, y);
    if (m_Font) m_Font->DrawNumber(dst, value, x + kValueRight, y + kValueDrop);
}

int BattlePanels::ShieldX(int index, int count, int x) {
    // Five shields -- what every building has -- are packed two pixels tighter
    // and the row starts two pixels further left, so they still fit the board.
    int step = kShieldStep;
    if (count == 5) {
        step = kShieldStepTight;
        x -= 2;
    }
    return x + 0xb - count * (step / 2) + index * step;
}

void BattlePanels::DrawShields(Surface& dst, int x, int y, int count) const {
    for (int i = 0; i < count; ++i)
        Blit(dst, m_IconDefense, 0, ShieldX(i, count, x), y + 0xa4);
}

void BattlePanels::DrawPlayer(Surface& dst, const BattleField& field, int x,
                              int y) const {
    const int owner = field.CurrentPlayer();
    if (owner < 1 || owner > BattleField::kMaxPlayers) return;
    const BattleField::Player& p = field.Players()[std::size_t(owner)];

    // The flag frame is the player's *colour*, 1..4 -- not the seat, because
    // the campaign lets you choose. `flags.tc` has only four frames, so colour
    // four falls off the end and the blitter's fallback lands it on frame 0.
    // That is not an accident: the frames run yellow, red, black, blue, and
    // strings 1674..1677 name the four colours red, black, blue, yellow, which
    // is exactly the sequence 1, 2, 3, 0.
    Blit(dst, m_Flags, field.Colour(owner), x + 3, y);
    Blit(dst, m_CommanderBg, 0, x + 7, y + 2);
    if (m_Font) m_Font->Draw(dst, p.Name, x + 9, y);

    // The commander-power meter: the filled part in a "charging", "ready" or
    // flashing frame, the rest in frame 0, split by a clip (0x10052458).
    const int mx = x + 9, my = y + 14;
    const int value = p.PerkPoints;
    const int fill = mx + (value * 0x34) / BattleField::kMaxPerkPoints + 8;
    int frame = 1, pointer = 1;
    if (value >= BattleField::kPerkThreshold) {
        pointer = 2;
        frame = value >= BattleField::kMaxPerkPoints ? 2 + m_Blink / 2 : 2;
    }
    BlitClippedX(dst, m_PerkMeter, frame, mx, my, 0, fill);
    BlitClippedX(dst, m_PerkMeter, 0, mx, my, fill, dst.Width());
    Blit(dst, m_PerkPointer, pointer, fill - 1, my + 3);
    Blit(dst, m_PerkPointer, 0, mx + 0x26, my + 3);

    // The treasury, in gold, with the turn's income after it when there is any.
    std::string money = std::to_string(p.Cash);
    const int income = field.Income(owner);
    if (income > 0) money += " +" + std::to_string(income);
    Blit(dst, m_IconMoney, 0, x + 5, y + 20);
    if (m_Font) m_Font->DrawTinted(dst, money, x + 25, y + 24, kMoneyColour);
}

void BattlePanels::DrawCell(Surface& dst, const BattleField& field,
                            const BattleRenderer& renderer, int cellX,
                            int cellY, bool known, int x, int slide) const {
    const BattleField::Cell& cell = field.At(cellX, cellY);
    const int terrain = cell.Terrain;
    int shields = field.Data().Terrain(terrain).Shield;
    // Where the shields (or the wall's hit points) sit, which depends on which
    // of the two boards went down.
    int row = slide + 2;

    if (cell.Property < 0) {
        Blit(dst, m_TerrainBg, 0, x, slide + 0xaa);
        Blit(dst, m_TerrainIcons, terrain, x + 2, slide + 0xb7);
    } else {
        const BattleField::Property& p =
            field.Properties()[std::size_t(cell.Property)];
        shields = field.Data().Property(p.Type).Shield;
        Blit(dst, m_PropBg, 0, x, slide + 0x9c);
        // An unexplored building is drawn in the neutral colours.
        BlitOwned(dst, m_PropIcons, p.Type, x + 2, slide + 0x9e, renderer,
                  known ? field.Colour(p.Owner) : 0);
        // Capture progress. The two destructible castle types show hit points
        // here instead (0x100597ac picks that branch); the port's model has no
        // property hit points, so they get the same capture readout as
        // everything else.
        Blit(dst, m_IconCapture, 0, x + 1, slide + 0xbe);
        if (m_Font) {
            if (known)
                m_Font->DrawNumber(dst, p.CapturePoints, x + 0x19,
                                  slide + 0xbe);
            else
                m_Font->Draw(dst, kUnknown, x + 0xf, slide + 0xbe);
        }
        row = slide + 0xb;
    }

    if (terrain == kBreakableWall) {
        // A breakable wall trades its shields for a hit point count. Before
        // the square has been scouted the original draws an empty string with
        // no icon, so the board is simply blank there.
        if (known) {
            Blit(dst, m_IconHealth, 0, x - 6, row + 0xa4);
            if (m_Font)
                m_Font->Draw(dst, std::to_string(cell.TerrainHP), x + 13,
                            row + 0xa7);
        }
        return;
    }
    DrawShields(dst, x, row, shields);
}

void BattlePanels::DrawUnit(Surface& dst, const BattleField& field,
                            const BattleRenderer& renderer,
                            const Strings& strings, int unitIndex, int x,
                            int y) const {
    const BattleField::Unit* u = field.UnitByIndex(unitIndex);
    if (!u) return;

    Blit(dst, m_UnitBg, 0, x, y - 9);
    if (m_Font) {
        m_Font->Draw(dst, strings.Get(BattleData::ShortUnitStringId(u->Type)),
                    x + 2, y - 11);
    }
    if (u->Type >= 0 && u->Type < kUnitTypeCount) {
        BlitOwned(dst, m_UnitIcons[u->Type], 0, x - 3, y - 6, renderer,
                  field.Colour(u->Owner));
    }

    const int cx = x + 0x1d;
    DrawValue(dst, m_IconHealth, u->HP, cx, y - 0xd);
    DrawValue(dst, m_IconRations, u->Rations, cx, y - 2);
    DrawValue(dst, m_IconAmmo, u->Ammo, cx, y + 10);
}

void BattlePanels::Draw(Surface& dst, const BattleField& field,
                        const Strings& strings, const BattleRenderer& renderer,
                        const BattleRenderer::View& view, Layout layout) const {
    if (!m_Ready || !m_Visible) return;
    if (!field.InBounds(view.CursorX, view.CursorY)) return;

    const int sx = BattleRenderer::ScreenX(view.CursorX, view);
    const int sy = BattleRenderer::ScreenY(view.CursorY, view);
    const bool known = field.Visible(view.Viewer, view.CursorX, view.CursorY);
    const int unit = known ? field.At(view.CursorX, view.CursorY).Unit : -1;

    // Bottom-left, then bottom-right, then the top -- the order the engine
    // draws them in, which decides what overlaps what where they touch.
    DrawCell(dst, field, renderer, view.CursorX, view.CursorY, known,
             CellPanelX(sx, sy, unit >= 0, layout), m_Slide);
    if (unit >= 0) {
        DrawUnit(dst, field, renderer, strings, unit,
                 UnitPanelX(sx, sy, layout), m_Slide + kUnitBaseY);
    }
    // The player's own board only shows while the cursor is free; once a unit
    // is picked up the engine leaves the top of the screen to the map.
    if (layout == Layout::kBrowse)
        DrawPlayer(dst, field, PlayerPanelX(sx, sy), -m_Slide);
}

}  // namespace bb
