// BattleRenderer — draws a BattleField, using the game's own art.
//
// Port of the engine's BattleFieldRenderer::render (0x1004b018) and the tile
// renderer under it (0x100b6ed8 / 0x100b7194 / 0x100b82d8). The layout is
// 20x20 tiles with 40x40 sprites centred on them, so units and buildings
// overhang their cell; the ground goes down first, then the mist, then rows
// top to bottom with the buildings in a row before the units in it, which is
// what gives the correct overlap.
//
// Where the art comes from (0x100b6750 builds this table):
//
//   deep / shallow water   `tiles\water2.tc` and `tiles\water.tc`, 32x32 --
//                          not tiles but the *source* for two ripple tanks;
//                          see SeaSurface.h
//   beach                  `tiles\grass_0.tc`, a 240x20 strip
//   plain, forest, mountain `tiles\grass_1.tc`, same shape
//   wall / breakable / road `tiles\wall_0.tc`, `wall_1.tc`, `road_0.tc`,
//                          320x20 strips of sixteen tiles
//   trees, rocks           `nature.tc`, frames 0-2 forest, 3-5 mountain,
//                          6 the rocks in shallow water
//   bridges                `tiles\bridge.tc`, one frame per orientation
//   mist                   `tiles\mist.tc`, 64 frames cycled per cell
//
// The map data carries which piece to use: the terrain word's high byte is the
// column in the strip, so nothing is auto-tiled -- the level editor baked that
// in. But **only columns 0-7 are fill tiles**. Columns 8-11 of every strip are
// its shoreline art: 8 = left and right bands, 9 = top and bottom, 10 = all
// four, 11 = the four 8x8 corner pieces. That is the blending between terrain
// heights, and without it every coastline is a hard edge.
//
// Which of them to draw is worked out at load time (0x100b7e80). Each terrain
// maps to a *tier* (0x100b76ec): deep water -3, deep water with mist -2,
// anything shallow or a bridge 1, beach 2, and all dry land 3. A cell whose
// neighbour stands on a higher tier records that neighbour's edge at level
// `tier - 1`, and level N is drawn from texture N -- 0 water_0, 1 grass_0,
// 2 grass_1. So the shallows put a soft rim on the deep water beside them, the
// beach puts sand on the shallows, and the grass puts a fringe on the beach.
//
// Buildings and unit sprites are *indexed* textures. The engine recolours them
// for each player by overwriting palette entries 256-271 from
// `Data\paletteCol.pal` (0x1005a164, 0x100d12d0), sixteen entries per owner
// with slot 0 for neutral, so this keeps the pixels as indices and resolves
// them through a per-owner lookup at blit time.
//
// Unit frames come from `Data\Battle\unit\<type>.dat`: a sprite sheet name and
// `attack`/`walk`/`idle`/`sleep` animations, each listing frame numbers per
// sixteenth of a circle. Direction 0 is north and they run anticlockwise --
// 4 west, 8 south, 12 east -- read off the Sloop's bowsprit. A unit that has
// already acted shows its `sleep` frame, which is why the last frame of every
// sheet is a greyed-out pose.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/BattleData.h"
#include "game/BattleField.h"
#include "game/SeaSurface.h"
#include "game/TextureCache.h"
#include "platform/Surface.h"

namespace bb {

class FilePack;
class Font;
class Palette;
class TextureCache;
struct Texture;

// One unit type's animation table, read from `Data\Battle\unit\<n>.dat`.
struct UnitAnimation {
    struct Clip {
        int Framerate = 5;
        // Frame lists per direction; empty where the file has no `dirN`.
        std::vector<int> Dir[16];
        bool Empty() const;
        // The frame for `dir` at `step`, falling back to any direction the
        // file did define (idle and sleep only define one).
        int Frame(int dir, int step) const;
    };
    std::string Sheet;
    Clip Attack, Walk, Idle, Sleep;
    bool Valid = false;
};

class BattleRenderer {
public:
    static constexpr int kTile = 20;
    static constexpr int kSpriteSize = 40;
    // The engine's own draw offsets: buildings at (-9, -13) from the tile's
    // top-left (0x10059d54), units centred (0x100d12d0), mist at (-7, -9)
    // (0x1004b018 passes x-1/y+1 into 0x10059e94, which subtracts 6 and 10).
    static constexpr int kBuildingOffsetX = -9;
    static constexpr int kBuildingOffsetY = -13;
    static constexpr int kMistOffsetX = -7;
    static constexpr int kMistOffsetY = -9;

    // Which action the cursor is offering; picks the icon in its fourth
    // corner (0x10065c94's switch over resource slots 0x42..0x4a).
    enum CursorMode {
        kCursorAttackA = 0, kCursorAttackB = 1, kCursorBuild = 2,
        kCursorCancel = 3, kCursorCapture = 4, kCursorInvalid = 5,
        kCursorJoin = 6, kCursorLoad = 7, kCursorSelect = 8,
        kCursorUnload = 9, kCursorAllCorners = 10, kCursorHidden = 11,
    };

    // Walk directions, as `Data\Battle\unit\<n>.dat` numbers them.
    enum Facing { kFaceUp = 0, kFaceLeft = 4, kFaceDown = 8, kFaceRight = 12 };

    // What to draw on top of the field this frame.
    struct View {
        int CamX = 0, CamY = 0;     // pixels, top-left of the viewport
        int CursorX = 0, CursorY = 0;
        int CursorMode = kCursorSelect;
        int Viewer = 1;               // whose fog of war applies
        int SelectedUnit = -1;
        // Per-cell move cost (-1 = out of reach); null hides the overlay.
        const std::vector<int>* MoveRange = nullptr;
        // Per-cell flag for the attack overlay; null hides it.
        const std::vector<uint8_t>* AttackRange = nullptr;
        // Per-cell flag for the *placement* overlay -- the green squares a
        // passenger may be put down on. The engine keeps two overlay layers
        // with a colour index each (renderer+0x78/+0x7c, kinds at +0x80), and
        // the table at +0x8a runs yellow, red, red, **green**; the green one
        // is what the unload and build-here states raise.
        const std::vector<uint8_t>* PlaceRange = nullptr;
        // The range key's preview, which is the table's *third* pair -- red,
        // like the attack overlay and unlike the yellow one a unit in hand
        // wears. 0x1009ad88 raises it as kind 2 down both of its branches, so
        // it stays red whether it is showing where the unit could go or, for
        // one that cannot move, where it could shoot. Only one of the two is
        // ever set.
        const std::vector<int>* PreviewReach = nullptr;    // move costs
        const std::vector<uint8_t>* PreviewTargets = nullptr;  // firing arc
        // The route the cursor has picked out, drawn as the red arrow.
        std::vector<BattleField::Step> Path;
        // A unit walking between two squares: drawn at `MovingPx/py` (world
        // pixels) with its `walk` clip instead of at its grid position.
        int MovingUnit = -1;
        int MovingPx = 0, MovingPy = 0;
        int MovingDir = kFaceDown;
        uint32_t Ticks = 0;           // milliseconds, for the animations
        Surface::Rect Clip{0, 0, Surface::kWidth, Surface::kHeight};
    };

    bool Load(FilePack& pack, TextureCache& textures, const Palette& palette);
    bool Ready() const { return m_Ready; }

    // Work out the shoreline art for this map. Call once after the field is
    // built; cheap enough to call again if the terrain ever changes.
    void SetField(const BattleField& field);

    void Draw(Surface& dst, const BattleField& field, const View& view);

    // --- the overview ---------------------------------------------------
    //
    // The battle menu's Options submenu ends with a Map row, and what it opens
    // is this (0x1004a4c8): the whole board at five pixels a tile, centred on
    // the screen, inside a frame made of eight wooden pieces -- four corners
    // and four edges tiled thirty-two pixels at a time. It is drawn *over* the
    // battlefield rather than over a board, which is why the map state renders
    // the field first and never touches `fullboard.tc`.
    //
    // Each square is a flat colour taken from its terrain, halved where the
    // viewer has not seen it -- `(c >> 1) & 0x777`, one bit off each nibble.
    // Rocks are a two-colour dither, and the two kinds of wall a rectangle
    // with a lighter rim. A building is a three-by-three block of its owner's
    // colour inside a rounded black outline; a unit is a black three-by-three
    // with a single coloured pixel at its heart, and only where the viewer can
    // see it.
    void DrawMinimap(Surface& dst, const BattleField& field, int viewer) const;
    // Five pixels a square, and how far the corner pieces stand outside it.
    static constexpr int kMinimapTile = 5;
    static constexpr int kMinimapFrame = 6;
    // How wide one edge piece is before it repeats.
    static constexpr int kMinimapFrameStep = 32;
    // Where the overview's top-left square lands, for a caller that wants to
    // put something else on the same grid.
    static int MinimapOriginX(const BattleField& field);
    static int MinimapOriginY(const BattleField& field);

    // Where a tile lands on screen for a given camera.
    static int ScreenX(int tileX, const View& v) { return tileX * kTile - v.CamX; }
    static int ScreenY(int tileY, const View& v) { return tileY * kTile - v.CamY; }

    // Clamp a camera so the map stays under the viewport, centring when the
    // map is smaller than the view.
    static void ClampCamera(const BattleField& field, int viewW, int viewH,
                            int& camX, int& camY);

    // The terrain height tier the shoreline logic groups by (0x100b76ec).
    static int Tier(int terrain);

    // --- particles -----------------------------------------------------
    //
    // The engine keeps several particle pools on the battle renderer, each a
    // fixed array of thirty-two with its own sprite and its own draw
    // (0x100e2c60 builds the pool, 0x100e2a94 steps and draws one particle).
    // The port carries the one the player actually sees on the map: the meat
    // a resupplied unit tosses up.
    //
    // Resupplying throws **two** of `Data\Battle\gfx\part\supply.tc` -- a
    // chicken leg, 13x10, one frame -- from the middle of the unit's square
    // (0x1004a3a0 spawns them at cell + (10, 10) with a life of 0x24 ticks).
    // Each gets a random sideways drift of at most half a pixel a tick and an
    // upward kick of one and three-quarter to two and three-quarter pixels,
    // and gains 0x2c00 of downward velocity every frame it is drawn, so they
    // arc up and fall back.
    void EmitSupply(int cellX, int cellY);
    bool ParticlesBusy() const { return !m_Supply.empty(); }

    // Which frame of `states.tc` belongs over this unit, or -1 for none. Four
    // kinds share six frames; see the note on the implementation. Public
    // because it is the only part of the badge that can be asserted on without
    // reading pixels, and because the choice it keeps is per unit.
    int StatusBadge(const BattleField& f, const BattleField::Unit& u,
                    int index);

    // The route arrow's pieces (0x100973c8 and the table at 0x10142998).
    // `in`/`out` are the direction of travel into and out of a square:
    // 0 up, 1 right, 2 down, 3 left. RouteFrame returns -1 for a reversal,
    // which a path never contains.
    static int RouteFrame(int in, int out);
    static int RouteHead(int in);
    static int RouteTail(int out);

    // The shoreline edge bits for one cell at one level, for tests and for
    // eyeballing a map's coastline without rendering it.
    uint8_t ShoreFlags(int x, int y, int level) const;

    const UnitAnimation& Animation(int type) const;

    // The palette an owner's indexed sprites resolve through, and how many
    // entries it has. The info panels draw the same recoloured unit and
    // building icons the map does, so they need the same table.
    const uint16_t* OwnerLut(int owner) const;
    // The same palette, brightened. A unit that a perk is boosting is drawn
    // through one of these instead of its plain one, stepping up and back down
    // so that the boost reads as the unit changing colour rather than as a
    // different-coloured unit. Level 0 is the plain palette.
    static constexpr int kBoostLevels = 4;
    const uint16_t* OwnerLut(int owner, int boost) const;
    // Which brightening a boosted unit is showing at this moment.
    static int BoostStep(uint32_t ticks);
    int LutSize() const { return int(m_LutStride); }

private:
    // Everything this renderer loaded, given back when the battle ends. Held
    // as an optional because the cache only arrives with Load(), and the
    // pointers below are only valid while it does.
    std::optional<TextureSet> m_Claims;

    // Per-cell shoreline record (0x100b7e80): edge bits for each of four
    // levels, plus the tier of each diagonal neighbour, less one.
    struct ShoreCell {
        uint8_t Flags[4] = {0, 0, 0, 0};
        int8_t Diag[4] = {0, 0, 0, 0};   // up-left, down-left, up-right, down-right
    };
    // The same shape for a region outline (0x100b77a0): which edges of this
    // cell face outside the set, and whether each diagonal is outside.
    struct RegionCell {
        uint8_t Flags = 0;
        int8_t Diag[4] = {0, 0, 0, 0};
        bool Inside = false;
    };

    void DrawGround(Surface& dst, const BattleField& f, const View& v);
    void DrawMist(Surface& dst, const BattleField& f, const View& v) const;
    void DrawWater(Surface& dst, int sx, int sy, bool deep, const View& v) const;
    void DrawStrip(Surface& dst, const Texture* strip, int column, int sx,
                   int sy, const View& v) const;
    void DrawEdgeTile(Surface& dst, const Texture* strip, uint8_t flags,
                      const int8_t diag[4], int match, int sx, int sy,
                      const View& v) const;
    void DrawPropertyCell(Surface& dst, const BattleField& f, int x, int y,
                          const View& v) const;
    void DrawUnit(Surface& dst, const BattleField& f,
                  const BattleField::Unit& u, const View& v);
    void DrawOverlays(Surface& dst, const BattleField& f, const View& v);
    void DrawRegion(Surface& dst, const BattleField& f, const View& v,
                    const std::vector<RegionCell>& cells, const Texture* strip,
                    uint16_t bright, uint16_t dark, bool fog);
    void DrawRoute(Surface& dst, const View& v) const;
    void DrawCursor(Surface& dst, const View& v) const;
    void BuildRegion(const BattleField& f, std::vector<RegionCell>& out,
                     const std::vector<int>* costs,
                     const std::vector<uint8_t>* flags, bool fog, int viewer) const;

    bool m_Ready = false;
    // Which badge each unit is currently showing, so the choice sticks the way
    // the engine's does (it keeps the index on the unit's animation object and
    // starts the next search there).
    std::vector<uint8_t> m_StatusKind;
    // One thrown ration. Position and velocity are 16.16, as the engine's are.
    struct Particle {
        int X = 0, Y = 0, Vx = 0, Vy = 0, Life = 0;
    };
    std::vector<Particle> m_Supply;
    uint32_t m_ParticleRng = 0x2545f491u;
    void DrawParticles(Surface& dst, const View& v);
    SeaSurface m_SeaDeep, m_SeaShallow;
    uint32_t m_SeaTicks = 0;
    const Texture* m_Grass[2] = {nullptr, nullptr};   // grass_0, grass_1
    const Texture* m_WaterEdge = nullptr;            // water_0, the level-0 rim
    const Texture* m_Wall[2] = {nullptr, nullptr};
    const Texture* m_Road = nullptr;
    const Texture* m_Nature = nullptr;
    const Texture* m_Bridge = nullptr;
    const Texture* m_Mist = nullptr;
    const Texture* m_Fog = nullptr;
    const Texture* m_RangeMove = nullptr;
    const Texture* m_RangeAttack = nullptr;
    const Texture* m_Buildings = nullptr;
    const Texture* m_Special[6] = {};                 // types 9..14
    const Texture* m_Units[kUnitTypeCount] = {};
    const Texture* m_Cursor = nullptr;                // cursor.tc, the triangles
    const Texture* m_AttackCursor = nullptr;
    const Texture* m_Icons[10] = {};                  // slots 0x42..0x4a
    const Texture* m_Route = nullptr;
    const Texture* m_Health = nullptr;
    // icon/states.tc, 8x8 x 6: ammo low, supplies low, cargo, capturing, and
    // the red out-of-ammo / out-of-supplies pair (frame + 4, 0x100d11c8).
    const Texture* m_States = nullptr;
    const Texture* m_SupplyTex = nullptr;   // the thrown rations
    // The overview's frame: UL, U, UR, L, R, DL, D, DR.
    const Texture* m_MapFrame[8] = {};
    UnitAnimation m_Anims[kUnitTypeCount];
    // 5 owner colours x 272 palette entries, RGB444.
    std::vector<uint16_t> m_OwnerLut;
    std::size_t m_LutStride = 0;
    // Scratch, kept between frames so a battle does not reallocate per frame.
    std::vector<ShoreCell> m_Shore;
    std::vector<RegionCell> m_RegionScratch;
    int m_ShoreW = 0, m_ShoreH = 0;
};

}  // namespace bb
