// BattlePanels — the three info panels the battlefield draws over the map.
//
// The engine has no permanent HUD frame: the map fills the whole 176x208
// screen and three little boards float on top of it, drawn last by whichever
// LocalPlayer state is current. Their layout is spelled out in three
// functions, and the state that calls them decides where they go:
//
//   0x10052264  player status, top-left  -- flag, commander board and name,
//               the commander-power meter, and the treasury
//   0x10051b64  cell preview, bottom-left -- the terrain (or the building)
//               under the cursor, with its defence shields
//   0x100520b4  unit status, bottom-right -- the unit under the cursor: its
//               short name, portrait, hit points, rations and ammunition
//
// **They move out of the cursor's way.** Each panel has a home corner and an
// alternate, and the state picks between them from the cursor's *screen*
// position (0x1009b494 reads the cursor object's pixel coordinates at +0x10
// and +0x14, the same numbers that place the cursor's own corner sprites):
//
//   player status   x = 0, or 93 when the cursor is in the top-left corner
//                   (screen y <= 39 and x <= 69)
//   cell preview    x = 0, or -- when the cursor is bottom-left (y >= 141 and
//                   x <= 49) -- 65 if a unit is standing there and 135 if not
//   unit status     x = 107, or 37 when the cursor is bottom-right, which
//                   parks it right beside the cell preview
//
// The threshold for "bottom-right" differs by state: 131 while browsing
// (0x1009b494), 151 once a unit is picked up and you are choosing where to
// send it (0x100973c8). That second state also drops the player panel and the
// cell preview's 135 fallback, which is what Layout distinguishes.
//
// **They get out of the way while the cursor is moving.** One timestamp on the
// LocalPlayer state (+0x54) drives this: it is stamped when the turn starts
// (0x100cdc00) and again on *every* direction key (0x100cdcc0's tail, which
// also doubles it as the key-repeat clock). Until 421 ms after the last of
// those, nothing is drawn; then the panels appear 70 pixels out of place and
// close that gap 8 pixels a frame -- the bottom pair sliding up from below the
// screen, the top one down from above it. So they duck away the moment you
// start walking the cursor around and settle back once you stop.
//
// Not ported: the turn timer under the player panel (0x100ce17c, `turntime_bv`
// plus a number), which only appears in timed Bluetooth games; and property
// hit points, which the two destructible castle types would show in place of
// capture points (0x100597ac picks that branch) but which the port's model
// does not carry. The commander-power meter is drawn from
// `BattleField::Player::PerkPoints`, and nothing feeds that yet -- the
// commander perk system as a whole is not ported -- so it reads empty.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "game/BattleRenderer.h"
#include "game/TextureCache.h"
#include "platform/Surface.h"

namespace bb {

class BattleData;
class BattleField;
class Font;
class Strings;
class TextureCache;
struct Texture;

class BattlePanels {
public:
    // The slide-in (0x1009b494). Nothing is drawn until `kAppearMs` after the
    // cursor last moved; the offset then walks from `kSlideStart` to zero,
    // `kSlideStep` a frame.
    static constexpr uint32_t kAppearMs = 0x1a5;   // 421
    static constexpr int kSlideStart = 0x46;       // 70
    static constexpr int kSlideStep = 8;

    // The meter's overflow animation cycles a counter 0..7 and back, one step
    // per frame, and uses half of it as a frame number (0x1009b494).
    static constexpr int kBlinkPeriod = 7;

    // Gold, the colour 0x10052264 puts the font into for the treasury.
    static constexpr uint16_t kMoneyColour = 0xFFF0;

    // Which state is drawing. `kOrdering` is every state that has a unit
    // picked up -- choosing a destination, a target or an unload square.
    enum class Layout { kBrowse, kOrdering };

    bool Load(TextureCache& cache, const Font& font);
    bool Ready() const { return m_Ready; }

    // Duck the panels and rewind the slide. The engine does this by restamping
    // its one timestamp, which the turn start and every cursor move both do.
    void ResetSlide() { m_Slide = kSlideStart; m_Visible = false; }

    // Once per drawn frame. `sinceMoveMs` is how long it has been since the
    // cursor last moved (or the turn began), which is what gates the panels
    // coming back up.
    void Step(uint32_t sinceMoveMs);

    // How far out of place the panels are this frame, and whether they are up
    // at all yet.
    int Slide() const { return m_Slide; }
    bool Visible() const { return m_Visible; }

    void Draw(Surface& dst, const BattleField& field, const Strings& strings,
              const BattleRenderer& renderer, const BattleRenderer::View& view,
              Layout layout) const;

    // Where each panel's left edge lands, given the cursor's screen position.
    // Split out so the placement rules can be tested without a screen.
    static int PlayerPanelX(int cursorSx, int cursorSy);
    static int CellPanelX(int cursorSx, int cursorSy, bool unitHere,
                          Layout layout);
    static int UnitPanelX(int cursorSx, int cursorSy, Layout layout);

    // Where the `index`th of `count` defence shields goes on a cell panel
    // whose left edge is `x` (0x10052008). The row is centred on the board,
    // and a building's five are packed two pixels tighter than a terrain's
    // one to four so that they still fit.
    static int ShieldX(int index, int count, int x);

    // The commander header -- flag, name, power meter, treasury -- which the
    // build screen puts in its own top-left corner (0x10052264 draws it there
    // with x = y = 0) as well as the status panel using it.
    void DrawPlayer(Surface& dst, const BattleField& field, int x, int y) const;

private:
    void DrawCell(Surface& dst, const BattleField& field,
                  const BattleRenderer& renderer, int cellX, int cellY,
                  bool known, int x, int slide) const;
    void DrawUnit(Surface& dst, const BattleField& field,
                  const BattleRenderer& renderer, const Strings& strings,
                  int unitIndex, int x, int y) const;
    // One `Data\icons\<name>.tc` badge and the value beside it, the pairing
    // 0x100543ec makes: the icon at (x, y) and the number's right edge 33
    // pixels along.
    void DrawValue(Surface& dst, const Texture* icon, int value, int x,
                   int y) const;
    void DrawShields(Surface& dst, int x, int y, int count) const;

    const Font* m_Font = nullptr;
    bool m_Ready = false;
    int m_Slide = kSlideStart;
    bool m_Visible = false;
    int m_Blink = 0;
    bool m_BlinkUp = true;

    // Everything below came from here, and goes back when the battle does.
    std::optional<TextureSet> m_Claims;

    const Texture* m_Flags = nullptr;          // 0xbc
    const Texture* m_CommanderBg = nullptr;   // 0xca
    const Texture* m_PerkMeter = nullptr;     // 0xba
    const Texture* m_PerkPointer = nullptr;   // 0xbb
    const Texture* m_TerrainBg = nullptr;     // 0xcb
    const Texture* m_TerrainIcons = nullptr;  // 0x52
    const Texture* m_PropBg = nullptr;        // 0x56
    const Texture* m_PropIcons = nullptr;     // 0x54, indexed
    const Texture* m_UnitBg = nullptr;        // 0xb7
    const Texture* m_UnitIcons[kUnitTypeCount] = {};  // 0x61..0x78, indexed
    const Texture* m_IconDefense = nullptr;   // 0x34
    const Texture* m_IconAmmo = nullptr;      // 0x35
    const Texture* m_IconHealth = nullptr;    // 0x36
    const Texture* m_IconMoney = nullptr;     // 0x37
    const Texture* m_IconRations = nullptr;   // 0x38
    const Texture* m_IconCapture = nullptr;   // 0x39
};

}  // namespace bb
