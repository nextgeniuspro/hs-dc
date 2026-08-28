#include "game/BattleRenderer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "game/ConfigFile.h"
#include "game/FilePack.hpp"
#include "game/Palette.h"
#include "game/TextureCache.h"
#include "shim/Log.h"

namespace bb {
namespace {

constexpr const char* kPaletteColPath = "Data\\paletteCol.pal";
constexpr int kOwnerColours = 5;      // neutral plus four players
constexpr int kOwnerPatchFirst = 256; // the entries paletteCol.pal replaces
constexpr int kOwnerPatchCount = 16;

// The strip layout. Columns 0-7 are fill tiles; 8-11 are the edge art.
constexpr int kEdge = 8;                  // band thickness (tileRenderer+0x3c)
constexpr int kFar = BattleRenderer::kTile - kEdge;   // 12 (+0x40)
constexpr int kColVertical = 8 * BattleRenderer::kTile;    // left+right bands
constexpr int kColHorizontal = 9 * BattleRenderer::kTile;  // top+bottom bands
constexpr int kColFrame = 10 * BattleRenderer::kTile;      // all four
constexpr int kColCorners = 11 * BattleRenderer::kTile;    // the four corners

// Edge bits, as 0x100b7e80 ORs them in. The low nibble says which edges are
// exposed; the high nibble marks the corners those edges already cover.
constexpr uint8_t kEdgeUp = 0x32;      // low 2, corners TL|TR
constexpr uint8_t kEdgeDown = 0xC8;    // low 8, corners BR|BL
constexpr uint8_t kEdgeRight = 0x64;   // low 4, corners TR|BR
constexpr uint8_t kEdgeLeft = 0x91;    // low 1, corners TL|BL
constexpr uint8_t kLowLeft = 1, kLowUp = 2, kLowRight = 4, kLowDown = 8;
constexpr uint8_t kCornerTL = 0x10, kCornerTR = 0x20;
constexpr uint8_t kCornerBR = 0x40, kCornerBL = 0x80;

// The overlay colours (0x100490a4 fills renderer+0x8a with four bright/dark
// pairs; the checkerboard alternates the two per cell, and the strip is the
// pair's index + 4 in the tile renderer's table). The table runs
//
//   kind 0  0xff0 / 0x880  yellow  strip 4 = range_mov   a unit's reach
//   kind 1  0xf00 / 0x800  red     strip 5 = range_att   its firing arc
//   kind 2  0xf00 / 0x800  red     strip 6 = range_att   the range preview
//   kind 3  0x0f0 / 0x080  green   strip 7 = range_mov   where to put down
//
// -- strips 6 and 7 being straight copies of 5 and 4 (0x100b6980), so the last
// two kinds are the first two's art in another colour.
constexpr uint16_t kMoveBright = 0x0FF0, kMoveDark = 0x0880;
constexpr uint16_t kAttackBright = 0x0F00, kAttackDark = 0x0800;
constexpr uint16_t kPreviewBright = 0x0F00, kPreviewDark = 0x0800;
constexpr uint16_t kPlaceBright = 0x00F0, kPlaceDark = 0x0080;
// The thrown-rations numbers, all from 0x1004a3a0 and 0x1009cdc0/0x1009ce70.
constexpr int kSupplyParticles = 2;
constexpr int kSupplyLife = 0x24;
constexpr int kSupplyKick = 180000;      // subtracted from a 16-bit random
constexpr int kSupplyGravity = 0x2c00;
constexpr int kOverlayAlpha = 4;   // 0x100b8678 blends src at 4/15

constexpr int kMistFrames = 64;
constexpr int kMistFrameMs = 80;
constexpr int kBuildingFrameMs = 200;
constexpr int kBuildingFrames = 5;
// The four cursor corners breathe one pixel in and out. 0x10065718 steps the
// offset every 66 ms while the cursor is the attack one and every 100 ms
// otherwise, which is nearly all the time.
constexpr int kCursorStepMs = 100;
constexpr int kCursorAttackStepMs = 66;
constexpr int kSeaFrameMs = 40;

const UnitAnimation kNoAnimation;

std::string Unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        out.push_back(s[i]);
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\\') ++i;
    }
    return out;
}

}  // namespace

bool UnitAnimation::Clip::Empty() const {
    for (const auto& d : Dir)
        if (!d.empty()) return false;
    return true;
}

int UnitAnimation::Clip::Frame(int direction, int step) const {
    const std::vector<int>* list = nullptr;
    if (direction >= 0 && direction < 16 && !Dir[direction].empty())
        list = &Dir[direction];
    if (!list) {
        for (const auto& d : Dir)
            if (!d.empty()) {
                list = &d;
                break;
            }
    }
    if (!list || list->empty()) return -1;
    const int n = int(list->size());
    return (*list)[std::size_t(((step % n) + n) % n)];
}

const UnitAnimation& BattleRenderer::Animation(int type) const {
    return type >= 0 && type < kUnitTypeCount ? m_Anims[type] : kNoAnimation;
}

// 0x100b76ec, applied to `terrain - 3`. Written out per terrain because the
// original's chain of sequential compares on a mutating value is much harder
// to read than the answer it produces.
int BattleRenderer::Tier(int terrain) {
    switch (terrain) {
        case NdLevel::kDeepWater: return -3;
        case NdLevel::kDeepWaterWithMist: return -2;
        case NdLevel::kShallowWater:
        case NdLevel::kShallowWaterWithMist:
        case NdLevel::kShallowWaterWithRocks:
        case NdLevel::kBridge: return 1;
        case NdLevel::kBeach: return 2;
        case NdLevel::kPlain:
        case NdLevel::kForest:
        case NdLevel::kMountain:
        case NdLevel::kWall:
        case NdLevel::kBreakableWall:
        case NdLevel::kRoad: return 3;
        case NdLevel::kDocksTerrain:
        case NdLevel::kShipyardTerrain: return -3;
        default: return terrain - 3;   // Dam, which no shipped level uses
    }
}

// 0x10142998, indexed by in * 4 + out.
int BattleRenderer::RouteFrame(int in, int out) {
    static const int kJoin[16] = {0, 3, -1, 2, 4, 1, 2, -1,
                                  -1, 5, 0, 4, 5, -1, 3, 1};
    if (in < 0 || in > 3 || out < 0 || out > 3) return -1;
    return kJoin[in * 4 + out];
}

int BattleRenderer::RouteHead(int in) {
    static const int kHead[4] = {6, 8, 7, 9};
    return (in >= 0 && in < 4) ? kHead[in] : 6;
}

int BattleRenderer::RouteTail(int out) {
    static const int kTail[4] = {10, 12, 11, 13};
    return (out >= 0 && out < 4) ? kTail[out] : 10;
}

uint8_t BattleRenderer::ShoreFlags(int x, int y, int level) const {
    if (x < 0 || y < 0 || x >= m_ShoreW || y >= m_ShoreH) return 0;
    if (level < 0 || level > 3) return 0;
    return m_Shore[std::size_t(y) * m_ShoreW + x].Flags[level];
}

bool BattleRenderer::Load(FilePack& pack, TextureCache& textures,
                          const Palette& palette) {
    // A battle's artwork is a battle's own -- 3 MB of tiles, buildings and
    // twenty-one unit sheets -- and the chart it is fought over does not want
    // any of it back. Loading through the set is what gives it up when this
    // renderer goes, which is when the battle ends.
    m_Claims.emplace(textures);
    TextureSet& tex = *m_Claims;
    // One ripple tank serves both waters: the shallow sea adopts the deep
    // sea's field (0x100d79bc), so the two textures refract the same heights
    // and ripple in unison. Warmed together after linking, as 0x100b6750's
    // 64-step loop does.
    m_SeaDeep.Init(tex.Load("Data\\Battle\\gfx\\tiles\\water2.tc"), 0);
    m_SeaShallow.Init(tex.Load("Data\\Battle\\gfx\\tiles\\water.tc"), 0);
    m_SeaShallow.AdoptField(m_SeaDeep);
    for (int i = 0; i < 64; ++i) {
        m_SeaDeep.Step();
        m_SeaShallow.Step();
    }
    m_WaterEdge = tex.Load("Data\\Battle\\gfx\\tiles\\water_0.tc");
    m_Grass[0] = tex.Load("Data\\Battle\\gfx\\tiles\\grass_0.tc");
    m_Grass[1] = tex.Load("Data\\Battle\\gfx\\tiles\\grass_1.tc");
    m_Wall[0] = tex.Load("Data\\Battle\\gfx\\tiles\\wall_0.tc");
    m_Wall[1] = tex.Load("Data\\Battle\\gfx\\tiles\\wall_1.tc");
    m_Road = tex.Load("Data\\Battle\\gfx\\tiles\\road_0.tc");
    m_Nature = tex.Load("Data\\Battle\\gfx\\nature.tc");
    m_Bridge = tex.Load("Data\\Battle\\gfx\\tiles\\bridge.tc");
    m_Mist = tex.Load("Data\\Battle\\gfx\\tiles\\mist.tc");
    m_Fog = tex.Load("Data\\Battle\\gfx\\tiles\\fog.tc");
    m_RangeMove = tex.Load("Data\\Battle\\gfx\\tiles\\range_mov.tc");
    m_RangeAttack = tex.Load("Data\\Battle\\gfx\\tiles\\range_att.tc");
    m_Cursor = tex.Load("Data\\cursor\\cursor.tc");
    m_AttackCursor = tex.Load("Data\\cursor\\attack_cursor.tc");
    m_Route = tex.Load("Data\\Menu\\routearrow.tc");
    m_Health = tex.Load("Data\\Battle\\gfx\\icon\\health.tc");
    m_States = tex.Load("Data\\Battle\\gfx\\icon\\states.tc");
    m_SupplyTex = tex.Load("Data\\Battle\\gfx\\part\\supply.tc");

    // Resource slots 0x42..0x4a, in the order 0x10065c94's switch wants them.
    static const char* const kIconPaths[10] = {
        "Data\\cursor\\attack.tc",   "Data\\cursor\\attack.tc",
        "Data\\cursor\\build.tc",    "Data\\cursor\\cancel.tc",
        "Data\\cursor\\capture.tc",  "Data\\cursor\\invalid.tc",
        "Data\\cursor\\join.tc",     "Data\\cursor\\load.tc",
        "Data\\cursor\\selection.tc", "Data\\cursor\\unload.tc",
    };
    for (int i = 0; i < 10; ++i) m_Icons[i] = tex.Load(kIconPaths[i]);

    // The eight pieces of the frame the map screen draws round its overview,
    // in the order 0x100490a4 loads them: the four corners and the four edges
    // that tile between them.
    static const char* const kMapFramePaths[8] = {
        "Data\\Battle\\gfx\\map\\UL.tc", "Data\\Battle\\gfx\\map\\U.tc",
        "Data\\Battle\\gfx\\map\\UR.tc", "Data\\Battle\\gfx\\map\\L.tc",
        "Data\\Battle\\gfx\\map\\R.tc",  "Data\\Battle\\gfx\\map\\DL.tc",
        "Data\\Battle\\gfx\\map\\D.tc",  "Data\\Battle\\gfx\\map\\DR.tc",
    };
    for (int i = 0; i < 8; ++i) m_MapFrame[i] = tex.Load(kMapFramePaths[i]);

    // Buildings and units keep their palette indices; the owner colour is
    // applied at blit time.
    m_Buildings = tex.LoadIndexed("Data\\Battle\\gfx\\buildings.tc");
    for (int i = 0; i < 6; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "Data\\Battle\\gfx\\special\\%d.tc",
                      9 + i);
        m_Special[i] = tex.LoadIndexed(path);
    }

    for (int t = 0; t < kUnitTypeCount; ++t) {
        char path[64];
        std::snprintf(path, sizeof(path), "Data\\Battle\\unit\\%d.dat", t);
        auto stream = pack.Open(path);
        if (!stream) continue;
        const auto& bytes = stream->Data();
        const std::string text(bytes.begin(), bytes.end());
        UnitAnimation& a = m_Anims[t];
        // `filename` sits above the first section, so ConfigFile (which only
        // keeps entries inside one) cannot see it.
        const std::size_t at = text.find("filename");
        if (at != std::string::npos) {
            const std::size_t eol = text.find('\n', at);
            std::string line = text.substr(at + 8, eol - at - 8);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
                line.erase(line.begin());
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                     line.back() == '\t'))
                line.pop_back();
            a.Sheet = Unescape(line);
        }
        ConfigFile cfg;
        cfg.Parse(text);
        for (const auto& section : cfg.Sections()) {
            UnitAnimation::Clip* clip = nullptr;
            if (section.Name == "attack") clip = &a.Attack;
            else if (section.Name == "walk") clip = &a.Walk;
            else if (section.Name == "idle") clip = &a.Idle;
            else if (section.Name == "sleep") clip = &a.Sleep;
            if (!clip) continue;
            clip->Framerate = section.GetInt("framerate", 5);
            for (const auto& e : section.Entries) {
                if (e.Key.compare(0, 3, "dir") != 0) continue;
                const int d = std::atoi(e.Key.c_str() + 3);
                if (d < 0 || d >= 16) continue;
                const char* p = e.Value.c_str();
                while (*p) {
                    while (*p == ' ' || *p == '\t') ++p;
                    if (!*p) break;
                    clip->Dir[d].push_back(std::atoi(p));
                    while (*p && *p != ' ' && *p != '\t') ++p;
                }
            }
        }
        a.Valid = !a.Sheet.empty();
        if (a.Valid) m_Units[t] = tex.LoadIndexed(a.Sheet);
    }

    // The owner colour table: five sets of sixteen entries that replace the
    // top of the shared palette.
    Palette colours;
    if (auto stream = pack.Open(kPaletteColPath)) {
        FileInputStream s = *stream;
        colours.Load(s);
    }
    m_LutStride = palette.Size();
    if (m_LutStride == 0) m_LutStride = 272;
    m_OwnerLut.assign(std::size_t(kOwnerColours) * kBoostLevels * m_LutStride,
                      0);
    for (int owner = 0; owner < kOwnerColours; ++owner) {
        uint16_t* lut = &m_OwnerLut[std::size_t(owner) * m_LutStride];
        for (std::size_t i = 0; i < m_LutStride; ++i) lut[i] = palette.Rgb444(i);
        for (int i = 0; i < kOwnerPatchCount; ++i) {
            const std::size_t dst = std::size_t(kOwnerPatchFirst + i);
            if (dst >= m_LutStride) break;
            lut[dst] = colours.Rgb444(std::size_t(owner * kOwnerPatchCount + i));
        }
        // And the brightened copies, one step of three per level. Adding to
        // every channel and clamping is the same arithmetic the effect sheets
        // are composited with, so a boosted unit and the flash that started it
        // agree about what "brighter" means.
        for (int level = 1; level < kBoostLevels; ++level) {
            uint16_t* up = &m_OwnerLut[(std::size_t(level) * kOwnerColours +
                                        std::size_t(owner)) *
                                       m_LutStride];
            for (std::size_t i = 0; i < m_LutStride; ++i) {
                const uint16_t c = lut[i];
                unsigned r = ((c >> 8) & 0xF) + unsigned(level);
                unsigned g = ((c >> 4) & 0xF) + unsigned(level);
                unsigned b = (c & 0xF) + unsigned(level);
                if (r > 15) r = 15;
                if (g > 15) g = 15;
                if (b > 15) b = 15;
                up[i] = uint16_t((c & 0xF000u) | (r << 8) | (g << 4) | b);
            }
        }
    }

    int sheets = 0;
    for (const Texture* t : m_Units)
        if (t) ++sheets;
    m_Ready = m_Grass[1] && m_Road && m_Nature && m_Buildings;
    const bool sea = m_SeaDeep.Valid() && m_SeaShallow.Valid();
    if (!m_Ready || !sea)
        LogError("battle: renderer %s, %d unit sheets, sea %s\n",
                 m_Ready ? "ready" : "INCOMPLETE", sheets,
                 sea ? "animated" : "flat");
    else
        LogDebug("battle: renderer ready, %d unit sheets\n", sheets);
    return m_Ready;
}

const uint16_t* BattleRenderer::OwnerLut(int owner) const {
    return OwnerLut(owner, 0);
}

const uint16_t* BattleRenderer::OwnerLut(int owner, int boost) const {
    if (m_OwnerLut.empty()) return nullptr;
    if (owner < 0 || owner >= kOwnerColours) owner = 0;
    if (boost < 0 || boost >= kBoostLevels) boost = 0;
    return &m_OwnerLut[(std::size_t(boost) * kOwnerColours +
                        std::size_t(owner)) *
                       m_LutStride];
}

// Up and back down again, a step every eighth of a second, so a boosted unit
// pulses about once a second.
int BattleRenderer::BoostStep(uint32_t ticks) {
    static constexpr int kRamp[] = {0, 1, 2, 3, 3, 2, 1, 0};
    return kRamp[(ticks / 125u) % (sizeof kRamp / sizeof *kRamp)];
}

void BattleRenderer::ClampCamera(const BattleField& f, int viewW, int viewH,
                                 int& camX, int& camY) {
    const int mapW = f.Width() * kTile;
    const int mapH = f.Height() * kTile;
    if (mapW <= viewW) camX = -(viewW - mapW) / 2;
    else if (camX < 0) camX = 0;
    else if (camX > mapW - viewW) camX = mapW - viewW;
    if (mapH <= viewH) camY = -(viewH - mapH) / 2;
    else if (camY < 0) camY = 0;
    else if (camY > mapH - viewH) camY = mapH - viewH;
}

// 0x100b7e80. A cell records the edges of every neighbour that stands on a
// higher tier, at that tier's level, plus the tier of each diagonal.
void BattleRenderer::SetField(const BattleField& f) {
    m_ShoreW = f.Width();
    m_ShoreH = f.Height();
    m_Shore.assign(std::size_t(m_ShoreW) * m_ShoreH, ShoreCell{});
    auto tierAt = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= m_ShoreW || y >= m_ShoreH) return -3;
        return Tier(f.At(x, y).Terrain);
    };
    for (int y = 0; y < m_ShoreH; ++y) {
        for (int x = 0; x < m_ShoreW; ++x) {
            ShoreCell& c = m_Shore[std::size_t(y) * m_ShoreW + x];
            const int self = std::max(0, tierAt(x, y));
            struct { int Tier; uint8_t Bits; } sides[4] = {
                {tierAt(x, y - 1), kEdgeUp},
                {tierAt(x, y + 1), kEdgeDown},
                {tierAt(x + 1, y), kEdgeRight},
                {tierAt(x - 1, y), kEdgeLeft},
            };
            for (const auto& s : sides)
                if (self < s.Tier && s.Tier < 5 && s.Tier >= 1)
                    c.Flags[s.Tier - 1] |= s.Bits;
            c.Diag[0] = int8_t(tierAt(x - 1, y - 1) - 1);
            c.Diag[1] = int8_t(tierAt(x - 1, y + 1) - 1);
            c.Diag[2] = int8_t(tierAt(x + 1, y - 1) - 1);
            c.Diag[3] = int8_t(tierAt(x + 1, y + 1) - 1);
        }
    }
}

// 0x100b853c: the generated 32x32 tile, wrapped over world coordinates so the
// sea is continuous across cell boundaries. Opaque -- the original Mem::Copy's
// it straight in.
void BattleRenderer::DrawWater(Surface& dst, int sx, int sy, bool deep,
                               const View& v) const {
    const SeaSurface& sea = deep ? m_SeaDeep : m_SeaShallow;
    if (!sea.Valid()) {
        dst.FillRect(sx, sy, kTile, kTile, deep ? 0xF114 : 0xF247, &v.Clip);
        return;
    }
    const uint16_t* src = sea.Pixels();
    const int worldX = sx + v.CamX, worldY = sy + v.CamY;
    for (int y = 0; y < kTile; ++y) {
        const int py = sy + y;
        if (py < v.Clip.Y0 || py >= v.Clip.Y1) continue;
        const int row = ((worldY + y) & SeaSurface::kMask) << SeaSurface::kGridShift;
        uint16_t* drow = dst.Pixels() + std::size_t(py) * dst.Width();
        for (int x = 0; x < kTile; ++x) {
            const int px = sx + x;
            if (px < v.Clip.X0 || px >= v.Clip.X1) continue;
            drow[px] = src[row + ((worldX + x) & SeaSurface::kMask)];
        }
    }
}

// The base tile is column `variant` of the strip, straight (0x100b82d8 offsets
// the source by `variant * tileWidth` and nothing else). The 240-wide strips
// keep their last four columns for the shoreline art and their terrain never
// uses a variant above 7; the 320-wide road and wall strips are sixteen fill
// tiles and use all of them, so there is no clamp to apply here.
void BattleRenderer::DrawStrip(Surface& dst, const Texture* strip, int column,
                               int sx, int sy, const View& v) const {
    const TcTexture::Image* img = strip ? strip->Frame(0) : nullptr;
    if (!img) return;
    const int columns = img->Width / kTile;
    if (columns <= 0) return;
    if (column < 0 || column >= columns) column = 0;
    dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, column * kTile,
                   0, kTile, kTile, sx, sy, &v.Clip);
}

// The edge bands and corner pieces for one cell, out of one strip's columns
// 8-11. The original picks the fewest rectangles that cover the exposed edges
// (sixteen little functions at 0x100b8a58..0x100b9130); this partitions the
// tile into a 3x3 grid instead and sources each part from the column that
// handler would have used -- the same pixels from the same art, just more and
// smaller blits, which costs nothing off-device.
void BattleRenderer::DrawEdgeTile(Surface& dst, const Texture* strip,
                                  uint8_t flags, const int8_t diag[4], int match,
                                  int sx, int sy, const View& v) const {
    const TcTexture::Image* img = strip ? strip->Frame(0) : nullptr;
    if (!img || img->Width < kColCorners + kTile) return;
    const uint16_t* px = img->Pixels.data();
    const bool left = (flags & kLowLeft) != 0;
    const bool up = (flags & kLowUp) != 0;
    const bool right = (flags & kLowRight) != 0;
    const bool down = (flags & kLowDown) != 0;

    auto part = [&](int col, int ox, int oy, int w, int h) {
        dst.BlitRegion(px, img->Width, img->Height, col + ox, oy, w, h, sx + ox,
                       sy + oy, &v.Clip);
    };
    // Corners of the 3x3 partition: a straight run comes from its own band's
    // column, a meeting of two from the all-edges column.
    if (left || up) part(left && up ? kColFrame : (left ? kColVertical : kColHorizontal), 0, 0, kEdge, kEdge);
    if (right || up) part(right && up ? kColFrame : (right ? kColVertical : kColHorizontal), kFar, 0, kEdge, kEdge);
    if (left || down) part(left && down ? kColFrame : (left ? kColVertical : kColHorizontal), 0, kFar, kEdge, kEdge);
    if (right || down) part(right && down ? kColFrame : (right ? kColVertical : kColHorizontal), kFar, kFar, kEdge, kEdge);
    // The middles of each band.
    if (up) part(kColHorizontal, kEdge, 0, kFar - kEdge, kEdge);
    if (down) part(kColHorizontal, kEdge, kFar, kFar - kEdge, kEdge);
    if (left) part(kColVertical, 0, kEdge, kEdge, kFar - kEdge);
    if (right) part(kColVertical, kFar, kEdge, kEdge, kFar - kEdge);

    // A corner piece goes in only where a diagonal is exposed and neither
    // adjacent edge already covered that corner.
    const uint8_t clear = uint8_t(flags ^ 0xF0);
    struct { int Diag; uint8_t Bit; int Dx, Dy; } corners[4] = {
        {0, kCornerTL, 0, 0},      // up-left
        {2, kCornerTR, kFar, 0},   // up-right
        {1, kCornerBL, 0, kFar},   // down-left
        {3, kCornerBR, kFar, kFar},
    };
    for (const auto& c : corners) {
        if (diag[c.Diag] != match || !(clear & c.Bit)) continue;
        dst.BlitRegion(px, img->Width, img->Height, kColCorners + c.Dx, c.Dy,
                       kEdge, kEdge, sx + c.Dx, sy + c.Dy, &v.Clip);
    }
}

void BattleRenderer::DrawGround(Surface& dst, const BattleField& f,
                                const View& v) {
    const int x0 = (v.CamX < 0 ? 0 : v.CamX / kTile);
    const int y0 = (v.CamY < 0 ? 0 : v.CamY / kTile);
    const int x1 = std::min(f.Width(), (v.CamX + v.Clip.X1) / kTile + 1);
    const int y1 = std::min(f.Height(), (v.CamY + v.Clip.Y1) / kTile + 1);
    const bool haveShore = m_ShoreW == f.Width() && m_ShoreH == f.Height();

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const BattleField::Cell& c = f.At(x, y);
            const int sx = ScreenX(x, v), sy = ScreenY(y, v);
            const int variant = c.Variant;
            const int kind = c.Terrain;
            const bool water = kind <= NdLevel::kShallowWaterWithRocks;

            switch (kind) {
                case NdLevel::kDeepWater:
                case NdLevel::kDeepWaterWithMist:
                    DrawWater(dst, sx, sy, true, v);
                    break;
                case NdLevel::kShallowWater:
                case NdLevel::kShallowWaterWithMist:
                    DrawWater(dst, sx, sy, false, v);
                    break;
                case NdLevel::kShallowWaterWithRocks: {
                    DrawWater(dst, sx, sy, false, v);
                    const TcTexture::Image* n = m_Nature ? m_Nature->Frame(6) : nullptr;
                    if (n)
                        dst.Blit(n->Pixels.data(), n->Width, n->Height, sx - 10,
                                 sy - 10);
                    break;
                }
                case NdLevel::kBeach:
                    DrawStrip(dst, m_Grass[0], variant, sx, sy, v);
                    break;
                case NdLevel::kPlain:
                    DrawStrip(dst, m_Grass[1], variant, sx, sy, v);
                    break;
                case NdLevel::kForest:
                case NdLevel::kMountain: {
                    DrawStrip(dst, m_Grass[1], variant, sx, sy, v);
                    const int frame = (kind == NdLevel::kForest ? 0 : 3) + variant;
                    const TcTexture::Image* n = m_Nature ? m_Nature->Frame(frame) : nullptr;
                    if (n)
                        dst.Blit(n->Pixels.data(), n->Width, n->Height, sx - 10,
                                 sy - 10);
                    break;
                }
                case NdLevel::kWall:
                    DrawStrip(dst, m_Wall[0], variant, sx, sy, v);
                    break;
                case NdLevel::kBreakableWall: {
                    DrawStrip(dst, m_Wall[1], variant, sx, sy, v);
                    // A fence that has been shot at wears the same health
                    // badge a wounded unit does. 0x1004b018 spells the test
                    // out: terrain type ten, the cell in sight, and the bar
                    // short of ten, drawn eleven pixels in from the tile's
                    // corner (a unit's sits at the centre line instead).
                    const int bar = BattleField::HealthBar(c.TerrainHP);
                    if (bar < 10 && m_Health &&
                        f.Visible(v.Viewer, x, y)) {
                        const TcTexture::Image* h = m_Health->Frame(bar - 1);
                        if (h)
                            dst.BlitRegion(h->Pixels.data(), h->Width, h->Height,
                                           0, 0, h->Width, h->Height, sx + 11,
                                           sy + 11, &v.Clip);
                    }
                    break;
                }
                case NdLevel::kRoad:
                    DrawStrip(dst, m_Road, variant, sx, sy, v);
                    break;
                case NdLevel::kBridge: {
                    DrawWater(dst, sx, sy, false, v);
                    const TcTexture::Image* b =
                        m_Bridge ? m_Bridge->Frame(variant) : nullptr;
                    if (b) dst.Blit(b->Pixels.data(), b->Width, b->Height, sx, sy);
                    break;
                }
                default:
                    DrawStrip(dst, m_Grass[1], variant, sx, sy, v);
                    break;
            }

            if (!haveShore) continue;
            const ShoreCell& s = m_Shore[std::size_t(y) * m_ShoreW + x];
            // Level 0 -- the shallow-water rim -- only ever lands on deep
            // water (0x100b6ed8 runs it for kinds 0 and 1 alone).
            if (kind <= NdLevel::kDeepWaterWithMist)
                DrawEdgeTile(dst, m_WaterEdge, s.Flags[0], s.Diag, 0, sx, sy, v);
            // 0x100b7194 runs from `height - 3` to 2, so water gets the beach
            // and grass rims and the beach gets the grass one.
            if (water) {
                DrawEdgeTile(dst, m_Grass[0], s.Flags[1], s.Diag, 1, sx, sy, v);
                DrawEdgeTile(dst, m_Grass[1], s.Flags[2], s.Diag, 2, sx, sy, v);
            } else if (kind == NdLevel::kBeach) {
                DrawEdgeTile(dst, m_Grass[1], s.Flags[2], s.Diag, 2, sx, sy, v);
            }
        }
    }
}

// Mist is 37x37 and overhangs its cell, so it cannot go down inside the ground
// loop -- the next tile's opaque base would clip it. The original draws it in
// the same pass as the buildings, after the whole ground layer.
void BattleRenderer::DrawMist(Surface& dst, const BattleField& f,
                              const View& v) const {
    if (!m_Mist) return;
    const int x0 = (v.CamX < 0 ? 0 : v.CamX / kTile);
    const int y0 = (v.CamY < 0 ? 0 : v.CamY / kTile);
    const int x1 = std::min(f.Width(), (v.CamX + v.Clip.X1) / kTile + 1);
    const int y1 = std::min(f.Height(), (v.CamY + v.Clip.Y1) / kTile + 1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const int t = f.At(x, y).Terrain;
            if (t != NdLevel::kDeepWaterWithMist &&
                t != NdLevel::kShallowWaterWithMist)
                continue;
            // Per-cell phase, so the sea does not pulse in unison.
            const int phase =
                ((x + y) * 8 + int(v.Ticks / kMistFrameMs)) & (kMistFrames - 1);
            const TcTexture::Image* m = m_Mist->Frame(phase);
            if (m)
                dst.BlitRegion(m->Pixels.data(), m->Width, m->Height, 0, 0,
                               m->Width, m->Height, ScreenX(x, v) + kMistOffsetX,
                               ScreenY(y, v) + kMistOffsetY, &v.Clip);
        }
    }
}

void BattleRenderer::DrawPropertyCell(Surface& dst, const BattleField& f, int x,
                                      int y, const View& v) const {
    const BattleField::Cell& cell = f.At(x, y);
    if (cell.Property < 0) return;
    const BattleField::Property& p = f.Properties()[std::size_t(cell.Property)];
    const int sx = ScreenX(x, v) + kBuildingOffsetX;
    const int sy = ScreenY(y, v) + kBuildingOffsetY;
    const Texture* tex = nullptr;
    int frame = 0;
    if (p.Type >= kPropSpecialCastle1 && p.Type < kPropertyTypeCount) {
        tex = m_Special[p.Type - kPropSpecialCastle1];
        frame = cell.Piece;
    } else {
        tex = m_Buildings;
        frame = p.Type * kBuildingFrames +
                int(v.Ticks / kBuildingFrameMs) % kBuildingFrames;
    }
    const TcTexture::Image* img = tex ? tex->Frame(frame) : nullptr;
    if (!img) return;
    if (!tex->Indexed) {
        dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0,
                       img->Width, img->Height, sx, sy, &v.Clip);
        return;
    }
    const uint16_t* lut = OwnerLut(f.Colour(p.Owner));
    if (!lut) return;
    dst.BlitIndexed(img->Pixels.data(), img->Width, img->Height, sx, sy, lut,
                    int(m_LutStride), &v.Clip);
}

void BattleRenderer::DrawUnit(Surface& dst, const BattleField& f,
                              const BattleField::Unit& u, const View& v) {
    const int index = int(&u - f.Units().data());
    const Texture* sheet = m_Units[u.Type];
    if (!sheet) return;
    const UnitAnimation& a = m_Anims[u.Type];

    const bool walking = index == v.MovingUnit;
    const UnitAnimation::Clip& clip =
        walking ? a.Walk : (u.Done ? a.Sleep : a.Idle);
    const int rate = clip.Framerate > 0 ? clip.Framerate : 5;
    const int step = int(v.Ticks * uint32_t(rate) / 1000u);
    int frame = clip.Frame(walking ? v.MovingDir : kFaceDown, step);
    if (frame < 0) frame = a.Sleep.Frame(0, 0);
    const TcTexture::Image* img = frame >= 0 ? sheet->Frame(frame) : nullptr;
    if (!img) return;

    const int cellX = walking ? v.MovingPx - v.CamX : ScreenX(u.X, v);
    const int cellY = walking ? v.MovingPy - v.CamY : ScreenY(u.Y, v);
    const int sx = cellX + kTile / 2 - img->Width / 2;
    const int sy = cellY + kTile / 2 - img->Height / 2;
    // A unit a perk is boosting wears a brighter palette, and the brightness
    // moves, which is what says the boost is still running rather than that
    // the unit is simply painted differently.
    const int boost = f.UnitBoosted(u) ? BoostStep(v.Ticks) : 0;
    const uint16_t* lut = OwnerLut(f.Colour(u.Owner), boost);
    if (!lut) return;
    dst.BlitIndexed(img->Pixels.data(), img->Width, img->Height, sx, sy, lut,
                    int(m_LutStride), &v.Clip);

    // 0x100d1438 draws both badges off the sprite's own anchor, which is the
    // centre of the cell: the health bar one pixel right of it and the status
    // icon eight left, both on the centre line. Each is 8x8.
    const int badgeY = cellY + kTile / 2;
    const int bar = BattleField::HealthBar(u.HP);
    if (bar < 10 && m_Health) {
        const TcTexture::Image* h = m_Health->Frame(bar - 1);
        if (h)
            dst.BlitRegion(h->Pixels.data(), h->Width, h->Height, 0, 0, h->Width,
                           h->Height, cellX + kTile / 2 + 1, badgeY, &v.Clip);
    }
    const int badge = StatusBadge(f, u, index);
    if (badge >= 0 && m_States) {
        if (const TcTexture::Image* s = m_States->Frame(badge))
            dst.BlitRegion(s->Pixels.data(), s->Width, s->Height, 0, 0,
                           s->Width, s->Height, cellX + kTile / 2 - 8, badgeY,
                           &v.Clip);
    }
}

// 0x1004a3a0. Two rations, from the middle of the square.
void BattleRenderer::EmitSupply(int cellX, int cellY) {
    for (int i = 0; i < kSupplyParticles; ++i) {
        Particle p;
        p.X = (cellX * kTile + kTile / 2) << 16;
        p.Y = (cellY * kTile + kTile / 2) << 16;
        m_ParticleRng = m_ParticleRng * 1103515245u + 12345u;
        p.Vx = int(m_ParticleRng & 0xffffu) - 0x8000;
        m_ParticleRng = m_ParticleRng * 1103515245u + 12345u;
        p.Vy = int(m_ParticleRng & 0xffffu) - kSupplyKick;
        p.Life = kSupplyLife;
        m_Supply.push_back(p);
    }
}

// 0x100e2a94, once per particle: draw it, then either retire it or advance
// it. Gravity is added by the draw itself (0x1009ce70), which is why the arc
// only steepens on the frames the particle is actually on screen.
void BattleRenderer::DrawParticles(Surface& dst, const View& v) {
    const TcTexture::Image* img = m_SupplyTex ? m_SupplyTex->Frame(0) : nullptr;
    for (std::size_t i = 0; i < m_Supply.size();) {
        Particle& p = m_Supply[i];
        p.Vy += kSupplyGravity;
        if (img) {
            const int sx = (p.X >> 16) - img->Width / 2 - v.CamX;
            const int sy = (p.Y >> 16) - img->Height / 2 - v.CamY;
            dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0,
                           img->Width, img->Height, sx, sy, &v.Clip);
        }
        if (--p.Life < 0) {
            m_Supply.erase(m_Supply.begin() + long(i));
            continue;
        }
        p.X += p.Vx;
        p.Y += p.Vy;
        ++i;
    }
}

// Which of the six badges in `states.tc` belongs over this unit, or -1.
//
// The engine keeps four *kinds* (0x100d11c8) and gives each a frame of its
// own, with a red variant four frames along for the empty case:
//
//   0  rations  -- the crossed icon, yellow at one or two left, red at none
//   1  ammunition -- the same in a cross, and only for a unit that spends any
//                    (the gate is `ammunitionConsumeRate >= 1`)
//   2  cargo aboard -- the little crate
//   3  capturing -- the flag on a pole, which is the one that shows over a
//                   swordsman standing on somebody else's building
//
// Only one shows at a time and it is *sticky*: 0x100d124c starts its search
// at whichever kind was drawn last, runs to the end and wraps, and writes the
// answer back. So a badge holds until it stops applying and only then gives
// way to the next one round.
int BattleRenderer::StatusBadge(const BattleField& f,
                                const BattleField::Unit& u, int index) {
    const UnitAttrs& a = f.Data().Unit(u.Type);
    // Each kind: is it showing, and with how much left (-1 = it is a flag,
    // not a counter, so it never turns red).
    const auto value = [&](int kind) -> int {
        switch (kind) {
            case 0: return u.Rations <= 2 ? u.Rations : -2;
            case 1:
                if (a.AmmoRate < 1) return -2;
                return u.Ammo <= 2 ? u.Ammo : -2;
            case 2: return u.Cargo.empty() ? -2 : -1;
            case 3: return u.Capturing ? -1 : -2;
        }
        return -2;
    };
    if (index >= int(m_StatusKind.size()))
        m_StatusKind.resize(std::size_t(index) + 1, 0);
    const int from = m_StatusKind[std::size_t(index)];
    for (int i = 0; i < 4; ++i) {
        const int kind = (from + i) % 4;
        const int v = value(kind);
        if (v == -2) continue;
        m_StatusKind[std::size_t(index)] = uint8_t(kind);
        return v == 0 ? kind + 4 : kind;
    }
    return -1;
}

// 0x100b77a0: which edges of each cell in the set face outside it, and whether
// each diagonal is outside. Same record shape the shoreline uses.
void BattleRenderer::BuildRegion(const BattleField& f,
                                 std::vector<RegionCell>& out,
                                 const std::vector<int>* costs,
                                 const std::vector<uint8_t>* flags, bool fog,
                                 int viewer) const {
    const int w = f.Width(), h = f.Height();
    out.assign(std::size_t(w) * h, RegionCell{});
    auto inside = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        const std::size_t i = std::size_t(y) * w + x;
        if (fog) return !f.Visible(viewer, x, y);
        if (costs) return (*costs)[i] >= 0;
        if (flags) return (*flags)[i] != 0;
        return false;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            RegionCell& c = out[std::size_t(y) * w + x];
            c.Inside = inside(x, y);
            if (!c.Inside) continue;
            if (!inside(x, y - 1)) c.Flags |= kEdgeUp;
            if (!inside(x, y + 1)) c.Flags |= kEdgeDown;
            if (!inside(x + 1, y)) c.Flags |= kEdgeRight;
            if (!inside(x - 1, y)) c.Flags |= kEdgeLeft;
            c.Diag[0] = int8_t(inside(x - 1, y - 1) ? -1 : 0);
            c.Diag[1] = int8_t(inside(x - 1, y + 1) ? -1 : 0);
            c.Diag[2] = int8_t(inside(x + 1, y - 1) ? -1 : 0);
            c.Diag[3] = int8_t(inside(x + 1, y + 1) ? -1 : 0);
        }
    }
}

// A region is a checkerboard wash (0x100b8678 blends the colour at 4/15) plus
// an outline from its own strip. Fog darkens instead, halving every colour
// nibble (0x100b8844).
void BattleRenderer::DrawRegion(Surface& dst, const BattleField& f,
                                const View& v,
                                const std::vector<RegionCell>& cells,
                                const Texture* strip, uint16_t bright,
                                uint16_t dark, bool fog) {
    const int w = f.Width();
    const int x0 = (v.CamX < 0 ? 0 : v.CamX / kTile);
    const int y0 = (v.CamY < 0 ? 0 : v.CamY / kTile);
    const int x1 = std::min(f.Width(), (v.CamX + v.Clip.X1) / kTile + 1);
    const int y1 = std::min(f.Height(), (v.CamY + v.Clip.Y1) / kTile + 1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const RegionCell& c = cells[std::size_t(y) * w + x];
            if (!c.Inside) continue;
            const int sx = ScreenX(x, v), sy = ScreenY(y, v);
            if (fog) {
                const int px0 = std::max(sx, v.Clip.X0);
                const int py0 = std::max(sy, v.Clip.Y0);
                const int px1 = std::min(sx + kTile, v.Clip.X1);
                const int py1 = std::min(sy + kTile, v.Clip.Y1);
                for (int py = py0; py < py1; ++py) {
                    uint16_t* row = dst.Pixels() + std::size_t(py) * dst.Width();
                    for (int px = px0; px < px1; ++px)
                        row[px] = uint16_t(0xF000u | ((row[px] >> 1) & 0x777));
                }
            } else {
                const uint16_t colour = ((x + y) & 1) ? dark : bright;
                dst.FillRect(sx, sy, kTile, kTile,
                             uint16_t((kOverlayAlpha << 12) | colour), &v.Clip);
            }
            DrawEdgeTile(dst, strip, c.Flags, c.Diag, 0, sx, sy, v);
        }
    }
}

void BattleRenderer::DrawOverlays(Surface& dst, const BattleField& f,
                                  const View& v) {
    if (f.FogEnabled()) {
        BuildRegion(f, m_RegionScratch, nullptr, nullptr, true, v.Viewer);
        DrawRegion(dst, f, v, m_RegionScratch, m_Fog, 0, 0, true);
    }
    if (v.MoveRange) {
        BuildRegion(f, m_RegionScratch, v.MoveRange, nullptr, false, v.Viewer);
        DrawRegion(dst, f, v, m_RegionScratch, m_RangeMove, kMoveBright,
                   kMoveDark, false);
    }
    if (v.AttackRange) {
        BuildRegion(f, m_RegionScratch, nullptr, v.AttackRange, false, v.Viewer);
        DrawRegion(dst, f, v, m_RegionScratch, m_RangeAttack, kAttackBright,
                   kAttackDark, false);
    }
    if (v.PlaceRange) {
        BuildRegion(f, m_RegionScratch, nullptr, v.PlaceRange, false, v.Viewer);
        DrawRegion(dst, f, v, m_RegionScratch, m_RangeMove, kPlaceBright,
                   kPlaceDark, false);
    }
    if (v.PreviewReach || v.PreviewTargets) {
        BuildRegion(f, m_RegionScratch, v.PreviewReach, v.PreviewTargets,
                    false, v.Viewer);
        DrawRegion(dst, f, v, m_RegionScratch, m_RangeAttack, kPreviewBright,
                   kPreviewDark, false);
    }
}

// The red route arrow (0x100973c8). Fourteen pieces: 0 vertical, 1 horizontal,
// 2-5 the elbows, 6-9 the heads, 10-13 the tail caps.
void BattleRenderer::DrawRoute(Surface& dst, const View& v) const {
    const std::size_t n = v.Path.size();
    if (!m_Route || n < 2) return;
    auto blit = [&](int frame, const BattleField::Step& s) {
        const TcTexture::Image* img = m_Route->Frame(frame);
        if (!img) return;
        dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0,
                       img->Width, img->Height, ScreenX(s.X, v), ScreenY(s.Y, v),
                       &v.Clip);
    };
    // in and out are the direction of travel into and out of a square.
    auto dirOf = [](const BattleField::Step& a, const BattleField::Step& b) {
        if (b.Y < a.Y) return 0;              // up
        if (a.X < b.X) return 1;              // right
        if (a.Y < b.Y) return 2;              // down
        return 3;                             // left
    };
    for (std::size_t i = 1; i + 1 < n; ++i) {
        const int frame = RouteFrame(dirOf(v.Path[i - 1], v.Path[i]),
                                     dirOf(v.Path[i], v.Path[i + 1]));
        if (frame >= 0) blit(frame, v.Path[i]);
    }
    // The tail cap sits on the first square, facing the way the path leaves,
    // and the head on the last, pointing the way it arrived.
    blit(RouteTail(dirOf(v.Path[0], v.Path[1])), v.Path[0]);
    blit(RouteHead(dirOf(v.Path[n - 2], v.Path[n - 1])), v.Path[n - 1]);
}

// 0x10065718 and 0x10065c94. Four 8x8 triangles just outside the tile's
// corners, breathing one pixel in and out together; the corner nearest the
// screen edge is replaced by the action icon.
void BattleRenderer::DrawCursor(Surface& dst, const View& v) const {
    if (v.CursorMode == kCursorHidden) return;
    const int tx = ScreenX(v.CursorX, v), ty = ScreenY(v.CursorY, v);

    // The offset oscillates -1, 0, 1, 0, ... one step per interval.
    static const int kBreathe[4] = {-1, 0, 1, 0};
    const int period = v.CursorMode == kCursorAttackA ? kCursorAttackStepMs
                                                       : kCursorStepMs;
    const int anim = kBreathe[(v.Ticks / uint32_t(period)) & 3];

    // Which corner gives way to the icon, by where the cursor sits on screen.
    int iconCorner = 3;
    if (tx > 100) iconCorner = 2;
    if (ty > 0x82) iconCorner = (tx > 0x27) ? 1 : 0;
    if (ty < 0x28) iconCorner = (tx < 0x65) ? 3 : 2;

    // corner: 0 top-right, 1 top-left, 2 bottom-left, 3 bottom-right, each
    // with the triangle whose right angle points outward.
    struct Corner { int X, Y, Frame; };
    const Corner corners[4] = {
        {tx - anim + 20, ty + anim - 6, 1},
        {tx + anim - 3, ty + anim - 6, 0},
        {tx + anim - 3, ty - anim + 17, 3},
        {tx - anim + 20, ty - anim + 17, 2},
    };
    // Below mode 2 the cursor is an attack cursor and turns red.
    const Texture* marks =
        v.CursorMode < kCursorBuild ? m_AttackCursor : m_Cursor;
    for (int i = 0; i < 4; ++i) {
        if (i == iconCorner && v.CursorMode != kCursorAllCorners) continue;
        const TcTexture::Image* img = marks ? marks->Frame(corners[i].Frame) : nullptr;
        if (!img) continue;
        dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0,
                       img->Width, img->Height, corners[i].X, corners[i].Y,
                       &v.Clip);
    }

    if (v.CursorMode >= kCursorAllCorners) return;
    const Texture* icon = m_Icons[v.CursorMode];
    if (!icon) return;
    // Icon offsets and, for the selection arrow, which way it points.
    static const int kIconDx[4] = {0, -20, -20, 0};
    static const int kIconDy[4] = {-20, -20, 0, 0};
    static const int kIconFrame[4] = {3, 2, 1, 0};
    const int frame =
        v.CursorMode == kCursorSelect ? kIconFrame[iconCorner] : 0;
    const TcTexture::Image* img = icon->Frame(frame);
    if (!img) return;
    dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0, img->Width,
                   img->Height, tx + kIconDx[iconCorner] + 15,
                   ty + kIconDy[iconCorner] + 13, &v.Clip);
}

void BattleRenderer::Draw(Surface& dst, const BattleField& field,
                          const View& v) {
    if (!field.Valid()) return;
    // The sea advances on its own clock so it keeps moving at a steady rate
    // whatever the frame loop is doing.
    const uint32_t want = v.Ticks / kSeaFrameMs;
    for (uint32_t i = m_SeaTicks; i < want && i < m_SeaTicks + 8; ++i) {
        m_SeaDeep.Step();
        m_SeaShallow.Step();
    }
    m_SeaTicks = want;

    DrawGround(dst, field, v);
    DrawOverlays(dst, field, v);
    DrawRoute(dst, v);

    // Rows top to bottom: mist, then buildings, then the units in that row, so
    // a unit is covered by whatever stands in front of it.
    DrawMist(dst, field, v);
    const int y0 = (v.CamY < 0 ? 0 : v.CamY / kTile);
    const int y1 = std::min(field.Height(), (v.CamY + v.Clip.Y1) / kTile + 2);
    const int rx0 = (v.CamX < 0 ? 0 : v.CamX / kTile);
    const int rx1 = std::min(field.Width(), (v.CamX + v.Clip.X1) / kTile + 1);
    for (int y = y0; y < y1; ++y) {
        for (int x = rx0; x < rx1; ++x) DrawPropertyCell(dst, field, x, y, v);
        for (const BattleField::Unit& u : field.Units()) {
            const int index = &u - field.Units().data();
            if (!u.Alive || u.Carrier >= 0) continue;
            // A walking unit follows its pixel position, not its grid square.
            const int row =
                index == v.MovingUnit ? v.MovingPy / kTile : u.Y;
            if (row != y) continue;
            if (field.FogEnabled() && !field.Visible(v.Viewer, u.X, u.Y)) continue;
            DrawUnit(dst, field, u, v);
        }
    }
    // Particles ride over everything on the map but under the cursor, the way
    // the engine's pools do (0x1004b018 steps them after the unit rows).
    DrawParticles(dst, v);
    DrawCursor(dst, v);
}

// --- the overview ----------------------------------------------------------

namespace {

// Opaque, because the minimap's colours are the twelve-bit values the engine
// pokes straight into the framebuffer with no alpha nibble at all.
constexpr uint16_t Solid(int rgb444) {
    return uint16_t(0xF000u | uint16_t(rgb444));
}

// Half brightness without letting one nibble bleed into the next, which is
// what the engine's `(c >> 1) & 0x777` does at every one of its call sites.
constexpr uint16_t Dim(int rgb444) { return Solid((rgb444 >> 1) & 0x777); }

// The colour of each terrain on the overview, from 0x1004a4c8's switch. Note
// what it switches on: `terrain & 0xf`, so the two building terrains fold back
// onto the two waters -- which never shows, because a docks or a shipyard
// always has its own three-by-three block drawn over it.
constexpr int kMapDeepWater = 0x38a;
constexpr int kMapShallowWater = 0x7bc;
constexpr int kMapRocks = 0x888;      // dithered against the shallows
constexpr int kMapBeach = 0xdd8;
constexpr int kMapPlain = 0x570;
constexpr int kMapForest = 0x240;
constexpr int kMapMountain = 0x888;
constexpr int kMapWallFill = 0xdcc, kMapWallEdge = 0xedd;
constexpr int kMapBreakFill = 0xb95, kMapBreakEdge = 0xcb7;
constexpr int kMapRoad = 0x863;
constexpr int kMapOther = 0x4f4;      // the dam, and anything unaccounted for

// The five owner colours, straight out of 0x100490a4: nobody, then red,
// black, blue and yellow in the order the colour rows are numbered.
constexpr int kMapOwnerColours[5] = {0xeee, 0xf22, 0x555, 0x33f, 0xff0};

// The engine's two-colour fill (0x10048f3c): a checkerboard, one colour on
// each parity, which is how shallow water with rocks in it is drawn.
void DitherRect(Surface& dst, int x, int y, int w, int h, uint16_t a,
                uint16_t b, const Surface::Rect& clip) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            dst.FillRect(x + i, y + j, 1, 1, ((i + j) & 1) ? b : a, &clip);
}

// And its bordered fill (0x10048fcc): `edge` round the top and both sides,
// `fill` inside, and the bottom row at half the edge colour so the block
// reads as a wall with a lit top and a shadow under it.
void WallRect(Surface& dst, int x, int y, int w, int h, uint16_t fill,
              int edge, const Surface::Rect& clip) {
    dst.FillRect(x, y, w, 1, Solid(edge), &clip);
    for (int j = 1; j < h - 1; ++j) {
        dst.FillRect(x, y + j, 1, 1, Solid(edge), &clip);
        if (w > 2) dst.FillRect(x + 1, y + j, w - 2, 1, fill, &clip);
        dst.FillRect(x + w - 1, y + j, 1, 1, Solid(edge), &clip);
    }
    dst.FillRect(x, y + h - 1, w, 1, Dim(edge), &clip);
}

// The rounded outline a building sits in (0x1004a448): the middle three
// pixels of the top and bottom rows and the outer columns of the three
// between them, so the corners of the five-by-five block are left open.
void RoundedOutline(Surface& dst, int x, int y, const Surface::Rect& clip) {
    const uint16_t black = Solid(0);
    dst.FillRect(x + 1, y, 3, 1, black, &clip);
    dst.FillRect(x + 1, y + 4, 3, 1, black, &clip);
    for (int j = 1; j <= 3; ++j) {
        dst.FillRect(x, y + j, 1, 1, black, &clip);
        dst.FillRect(x + 4, y + j, 1, 1, black, &clip);
    }
}

void Blit(Surface& dst, const Texture* tex, int x, int y,
          const Surface::Rect* clip) {
    const TcTexture::Image* img = tex ? tex->Frame(0) : nullptr;
    if (!img) return;
    dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0,
                   img->Width, img->Height, x, y, clip);
}

}  // namespace

int BattleRenderer::MinimapOriginX(const BattleField& field) {
    return (Surface::kWidth - field.Width() * kMinimapTile) >> 1;
}

int BattleRenderer::MinimapOriginY(const BattleField& field) {
    return (Surface::kHeight - field.Height() * kMinimapTile) >> 1;
}

void BattleRenderer::DrawMinimap(Surface& dst, const BattleField& field,
                                 int viewer) const {
    if (!field.Valid()) return;
    const int w5 = field.Width() * kMinimapTile;
    const int h5 = field.Height() * kMinimapTile;
    const int x0 = MinimapOriginX(field);
    const int y0 = MinimapOriginY(field);
    const Surface::Rect all{0, 0, Surface::kWidth, Surface::kHeight};

    // The frame. Corners first, then the edges tiled between them and clipped
    // to the board's extent, which is what 0x10081874 is setting up each time
    // it appears in the original.
    Blit(dst, m_MapFrame[0], x0 - kMinimapFrame, y0 - kMinimapFrame, &all);
    Blit(dst, m_MapFrame[2], x0 + w5, y0 - kMinimapFrame, &all);
    Blit(dst, m_MapFrame[5], x0 - kMinimapFrame, y0 + h5, &all);
    Blit(dst, m_MapFrame[7], x0 + w5, y0 + h5, &all);
    const Surface::Rect across{x0, 0, x0 + w5, Surface::kHeight};
    const Surface::Rect down{0, y0, Surface::kWidth, y0 + h5};
    for (int x = x0; x < x0 + w5; x += kMinimapFrameStep) {
        Blit(dst, m_MapFrame[1], x, y0 - kMinimapFrame, &across);
        Blit(dst, m_MapFrame[6], x, y0 + h5, &across);
    }
    for (int y = y0; y < y0 + h5; y += kMinimapFrameStep) {
        Blit(dst, m_MapFrame[3], x0 - kMinimapFrame, y, &down);
        Blit(dst, m_MapFrame[4], x0 + w5, y, &down);
    }

    // The ground.
    for (int y = 0; y < field.Height(); ++y) {
        for (int x = 0; x < field.Width(); ++x) {
            const bool seen = field.Visible(viewer, x, y);
            const int px = x0 + x * kMinimapTile;
            const int py = y0 + y * kMinimapTile;
            int flat = kMapOther;
            switch (field.At(x, y).Terrain & 0xf) {
                case NdLevel::kDeepWater: flat = kMapDeepWater; break;
                // Mist over water is drawn as water: the original puts a pale
                // block down for it and then covers that block completely with
                // the shallow colour, so what reaches the screen is the
                // shallows either way.
                case NdLevel::kDeepWaterWithMist:
                case NdLevel::kShallowWater:
                case NdLevel::kShallowWaterWithMist:
                    flat = kMapShallowWater;
                    break;
                case NdLevel::kShallowWaterWithRocks:
                    DitherRect(dst, px, py, kMinimapTile, kMinimapTile,
                               seen ? Solid(kMapShallowWater)
                                    : Dim(kMapShallowWater),
                               seen ? Solid(kMapRocks) : Dim(kMapRocks), all);
                    continue;
                case NdLevel::kBeach: flat = kMapBeach; break;
                case NdLevel::kPlain: flat = kMapPlain; break;
                case NdLevel::kForest: flat = kMapForest; break;
                case NdLevel::kMountain: flat = kMapMountain; break;
                case NdLevel::kWall:
                    WallRect(dst, px, py, kMinimapTile, kMinimapTile,
                             seen ? Solid(kMapWallFill) : Dim(kMapWallFill),
                             seen ? kMapWallEdge : (kMapWallEdge >> 1) & 0x777,
                             all);
                    continue;
                case NdLevel::kBreakableWall:
                    WallRect(dst, px, py, kMinimapTile, kMinimapTile,
                             seen ? Solid(kMapBreakFill) : Dim(kMapBreakFill),
                             seen ? kMapBreakEdge
                                  : (kMapBreakEdge >> 1) & 0x777,
                             all);
                    continue;
                case NdLevel::kRoad:
                case NdLevel::kBridge: flat = kMapRoad; break;
                default: flat = kMapOther; break;
            }
            dst.FillRect(px, py, kMinimapTile, kMinimapTile,
                         seen ? Solid(flat) : Dim(flat), &all);
        }
    }

    // The buildings, and then the units on top of them.
    for (int y = 0; y < field.Height(); ++y) {
        for (int x = 0; x < field.Width(); ++x) {
            const BattleField::Cell& c = field.At(x, y);
            const bool seen = field.Visible(viewer, x, y);
            const int px = x0 + x * kMinimapTile;
            const int py = y0 + y * kMinimapTile;
            if (c.Property >= 0) {
                const BattleField::Property& p =
                    field.Properties()[std::size_t(c.Property)];
                // Only the square the building was placed on gets a block;
                // the rest of a castle's footprint is left as terrain, which
                // is what the original does by keying off the cell's own
                // property pointer being the origin.
                if (p.X == x && p.Y == y) {
                    // A building nobody has seen keeps its shape but not its
                    // colours: the fog hides who holds it, not that it is
                    // there.
                    const int owner = seen ? p.Owner : 0;
                    const int index = owner >= 1 ? field.Colour(owner) : 0;
                    RoundedOutline(dst, px, py, all);
                    dst.FillRect(px + 1, py + 1, 3, 3,
                                 Solid(kMapOwnerColours[index]), &all);
                }
            }
            if (!seen || c.Unit < 0) continue;
            const BattleField::Unit* u = field.UnitByIndex(c.Unit);
            if (!u || !u->Alive) continue;
            const int index = u->Owner >= 1 ? field.Colour(u->Owner) : 0;
            dst.FillRect(px + 1, py + 1, 3, 3, Solid(0), &all);
            dst.FillRect(px + 2, py + 2, 1, 1,
                         Solid(kMapOwnerColours[index]), &all);
        }
    }
}

}  // namespace bb
