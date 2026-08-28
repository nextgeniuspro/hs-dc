#include "game/TravelMap.h"

#include <algorithm>
#include <cmath>
#include <queue>

#include "game/Game.h"
#include "game/SoundManager.h"
#include "game/TextureCache.h"
#include "platform/Host.h"

namespace bb {
namespace {

constexpr int kHalfW = Surface::kWidth / 2;    // 88
constexpr int kHalfH = Surface::kHeight / 2;   // 104

// The chart's own layout (0x100da688): the world is squeezed into 152x176
// pixels at (15, 13), or twice that when only the opening quadrant is known.
constexpr int kChartX = 15, kChartY = 13;
constexpr int kChartW = 152, kChartH = 176;

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int Hypot(int dx, int dy) {
    return int(std::lround(std::sqrt(double(dx) * dx + double(dy) * dy)));
}

// Shortest signed way round from `from` to `to` on a 1024-unit circle.
int TurnDelta(int from, int to) {
    int d = (to - from) & 0x3FF;
    if (d > 512) d -= 1024;
    return d;
}

// Does the segment ab cross the segment cd? Standard orientation test; used
// against every island edge, so it has to be cheap and exact in integers.
int64_t Cross(int ax, int ay, int bx, int by, int cx, int cy) {
    return int64_t(bx - ax) * (cy - ay) - int64_t(by - ay) * (cx - ax);
}

bool SegmentsCross(TravelWorld::Point a, TravelWorld::Point b,
                   TravelWorld::Point c, TravelWorld::Point d) {
    const int64_t d1 = Cross(c.X, c.Y, d.X, d.Y, a.X, a.Y);
    const int64_t d2 = Cross(c.X, c.Y, d.X, d.Y, b.X, b.Y);
    const int64_t d3 = Cross(a.X, a.Y, b.X, b.Y, c.X, c.Y);
    const int64_t d4 = Cross(a.X, a.Y, b.X, b.Y, d.X, d.Y);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

}  // namespace

int TravelMap::HeadingOf(int dx, int dy) {
    if (dx == 0 && dy == 0) return 0;
    // Straight out of the engine's own routine (0x1008be18), whose degenerate
    // case is the whole answer: with dy zero it returns **254 for dx positive
    // and 764 for dx negative**. So east is a quarter turn and west is three,
    // the ratio it divides is dx over dy, and the heading is `atan2(dx, dy)`
    // with **south at zero**: S 0, E 256, N 512, W 768.
    //
    // Which also settles what that little flag on the spar is. At heading 256
    // -- due east -- ship.tc's frame shows the flag pointing *west*, so it is
    // the stern ensign, not a bowsprit, and the ship's bow is the blunt-
    // looking end. Reading it as the bow puts the ship exactly backwards.
    const double a = std::atan2(double(dx), double(dy));
    int h = int(std::lround(a * 1024.0 / (2.0 * 3.14159265358979323846)));
    return h & 0x3FF;
}

TravelMap::TravelMap(GameContext& ctx, const TravelWorld& world,
                     TravelState& state)
    : m_Ctx(ctx), m_World(world), m_State(state), m_Claims(ctx.Textures) {
    PlaceShip({state.ShipX, state.ShipY});
    m_Heading = m_WantHeading = state.Heading;
    m_CursorX = m_ShipX;
    m_CursorY = m_ShipY;
    m_CamX = m_ShipX;
    m_CamY = m_ShipY;
    m_AnchorX = m_ShipX;
    m_AnchorY = m_ShipY;
}

// No complaint of its own when the art is not there: the cache says so once
// per path, and the chart asks for the same icon once per location that wears
// it -- which is how one missing file used to fill six lines.
const Texture* TravelMap::Tex(const std::string& path) {
    return m_Claims.Load(path);
}

void TravelMap::Unload() {
    if (m_Claims.Empty()) return;
    // The chart's music goes with its pictures. 0x100d9264 stops the track it
    // chose (`if (track >= 0) stop(track, bank 5)`) as the state comes down,
    // and the bank itself is a megabyte of decoded music that nothing else
    // wants -- on a console that is worth more than a battle's artwork.
    if (m_Ctx.Sound) {
        m_Ctx.Sound->StopMusic();
        m_Ctx.Sound->UnloadBank(SoundManager::kBankTravel);
    }
    m_Claims.ReleaseAll();
    // Every pointer below came out of the cache and may now be freed memory,
    // so the map goes back to the state it was in before Load(): nothing to
    // draw, and Load() to call before it is drawn again.
    m_Haze = m_Gull = m_GullShadow = m_Ship = m_ShipRefl = nullptr;
    m_RouteDot = m_Dest = m_Flag = m_Pointer = nullptr;
    m_OverSea = m_OverLocation = m_Marker = nullptr;
    for (const Texture*& chart : m_Chart) chart = nullptr;
    m_Islands.clear();
    m_LocIcons.clear();
    m_Sea = SeaSurface{};
}

bool TravelMap::Load() {
    const TravelWorld::Water& w = m_World.WaterProps();
    const Texture* water = Tex(w.Texture);
    // The engine hands the ripple tank the texture's own size as the grid
    // (0x100d9ed0 passes 0x40 and water.tc is 64 square).
    int shift = 6;
    if (water && water->Valid()) {
        shift = 0;
        while ((1 << shift) < water->Width) ++shift;
    }
    // 0x100d9ed0 warms the travel sea with 32 steps, half the battle's 64.
    m_Sea.Init(water, shift, w.Radius, w.Height, w.Interval, w.Windx, w.Windy,
              32);
    BuildLanes();

    // The five flocks the engine scatters over the map (0x100d95e4). They
    // start at rest (0x100716f4 zeroes the velocities): the wander goal in
    // Flock::Step is what spins them up, within a second of the camera
    // arriving.
    static const Point kHomes[kFlockCount] = {
        {0, -380}, {0, -80}, {110, 130}, {375, 185}, {280, -60}};
    m_Flocks.clear();
    for (const Point& home : kHomes) {
        Flock f;
        f.Home = {home.X, home.Y};
        f.Scatter(m_Rng);
        m_Flocks.push_back(f);
    }

    // The two gulls the world-loader hangs off its particle pool
    // (0x100d95e4's tail): spawned on the ship, headings zero, and the pool
    // slot's parity is which way each one is condemned to turn (0x1000d208
    // reads bit 0 of the slot index the pool stamped on the particle).
    for (int i = 0; i < kGullCount; ++i) {
        m_Gulls[i] = Gull{};
        m_Gulls[i].X = m_ShipX;
        m_Gulls[i].Y = m_ShipY;
        m_Gulls[i].Clockwise = (i & 1) != 0;
    }

    m_Haze = Tex("Data\\travel\\256.tc");
    m_Gull = Tex("Data\\travel\\gfx\\seagull.tc");
    m_GullShadow = Tex("Data\\travel\\gfx\\seagull_shadow.tc");
    m_Ship = Tex("Data\\travel\\gfx\\ship.tc");
    m_ShipRefl = Tex("Data\\travel\\gfx\\ship-refl.tc");
    m_RouteDot = Tex("Data\\travel\\gfx\\route.tc");
    m_Dest = Tex("Data\\travel\\gfx\\adestination.tc");
    m_Flag = Tex("Data\\travel\\gfx\\locationflag.tc");
    m_Pointer = Tex("Data\\travel\\gfx\\pointer-arrow.tc");
    m_OverSea = Tex("Data\\travel\\gfx\\amove-to.tc");
    m_OverLocation = Tex("Data\\travel\\gfx\\amission.tc");
    m_Marker = Tex("Data\\travel\\gfx\\Map-Marker-01.tc");
    for (int i = 0; i < 4; ++i)
        m_Chart[i] = Tex("Data\\travel\\gfx\\map" + std::to_string(i + 1) + ".tc");
    // Every island, up front and kept: they are the map, and they are blitted
    // twice a frame, so asking the cache for them by name each time would take
    // a claim per island per frame -- artwork that could then never be given
    // back. Six locations name `mission.tc` or `skull.tc`, which are not in
    // the pak under any name -- dangling references in world.xml, so those
    // places show only their flag.
    m_Islands.clear();
    m_Islands.reserve(m_World.Islands().size());
    for (const TravelWorld::Island& is : m_World.Islands())
        m_Islands.push_back(Tex(is.Texture));
    m_LocIcons.clear();
    m_LocIcons.reserve(m_World.Locations().size());
    for (const TravelWorld::Location& loc : m_World.Locations())
        m_LocIcons.push_back(loc.Texture.empty() ? nullptr : Tex(loc.Texture));

    // The chart's own bank, and one of its three tracks. The engine re-rolls
    // the choice every time the map is built or returned to (0x100d95e4,
    // 0x100dabb4 and the tail of 0x100dc2b8 all do `rand()%3` into the sound
    // player's `track` field), so the voyage does not play the same piece over
    // and over -- and since MissionFlow unloads and reloads the map around
    // every mission, doing it here is doing it at all three of those points.
    // It comes in from silence: 0x100dcbdc plays at volume 0 and 0x100f1774
    // ramps it up at 0x10 a block.
    if (m_Ctx.Sound) {
        m_Ctx.Sound->LoadBank(SoundManager::kBankTravel,
                             SoundManager::kTravelBankPath);
        m_Rng = m_Rng * 1103515245u + 12345u;
        m_MusicTrack =
            int((m_Rng >> 16) % unsigned(SoundManager::kTravelMusicCount));
        m_Ctx.Sound->StartMusic(SoundManager::kBankTravel,
                               SoundManager::kTravelMusicFirst + m_MusicTrack,
                               kMusicFadeStep);
    }
    return m_Sea.Valid() && m_Ship && m_Ship->Valid();
}

void TravelMap::PlaceShip(Point p) {
    m_ShipX = p.X << 16;
    m_ShipY = p.Y << 16;
    m_VelX = m_VelY = 0;
    m_Route.clear();
    m_Leg = 0;
}

void TravelMap::MoveCursor(Point p) {
    m_CursorX = Clamp(p.X, TravelWorld::kMinX, TravelWorld::kMaxX) << 16;
    m_CursorY = Clamp(p.Y, TravelWorld::kMinY, TravelWorld::kMaxY) << 16;
    m_Follow = false;
}

bool TravelMap::IsOpen(int id) const {
    return std::find(m_State.Open.begin(), m_State.Open.end(), id) !=
           m_State.Open.end();
}

bool TravelMap::IsDone(int id) const {
    return std::find(m_State.Done.begin(), m_State.Done.end(), id) !=
           m_State.Done.end();
}

// 0x100ddbe0: which quadrant is `p` in, and has it been charted? A point
// outside the world is uncharted outright -- unlike AreaOf, which files it
// under the opening quadrant for the encounter counters' sake.
bool TravelMap::Charted(Point p) const {
    const int qx = (p.X - TravelWorld::kMinX) * 2 /
                   (TravelWorld::kMaxX - TravelWorld::kMinX);
    const int qy = (p.Y - TravelWorld::kMinY) * 2 /
                   (TravelWorld::kMaxY - TravelWorld::kMinY);
    if (qx < 0 || qx > 1 || qy < 0 || qy > 1) return false;
    return m_State.Known[qx + qy * 2];
}

// The union of the charted quadrants, in 16.16 (0x100dbce0's loop): each set
// flag stretches the box over its quarter of the world.
void TravelMap::KnownBox(int& minx, int& miny, int& maxx, int& maxy) const {
    minx = miny = 10000 << 16;
    maxx = maxy = -(10000 << 16);
    const int wx = TravelWorld::kMinX << 16, wy = TravelWorld::kMinY << 16;
    const int hx = ((TravelWorld::kMaxX - TravelWorld::kMinX) << 16) / 2;
    const int hy = ((TravelWorld::kMaxY - TravelWorld::kMinY) << 16) / 2;
    for (int qy = 0; qy < 2; ++qy) {
        for (int qx = 0; qx < 2; ++qx) {
            if (!m_State.Known[qx + qy * 2]) continue;
            minx = std::min(minx, wx + qx * hx);
            maxx = std::max(maxx, wx + qx * hx + hx);
            miny = std::min(miny, wy + qy * hy);
            maxy = std::max(maxy, wy + qy * hy + hy);
        }
    }
}

// An island's hull is stored in texture coordinates, and an island hangs from
// its `<position>` by the texture's top-left corner rather than its middle --
// which is how the harbours, forts and villages come out standing on land and
// the ship's berth comes out at sea. So a hull vertex is simply `pos + vertex`.
bool TravelMap::IslandHull(int index, std::vector<Point>& hull) const {
    hull.clear();
    const TravelWorld::Island& is = m_World.Islands()[std::size_t(index)];
    if (is.Polygon.size() < 3) return false;
    hull.reserve(is.Polygon.size());
    for (const Point& v : is.Polygon)
        hull.push_back({v.X + is.Pos.X, v.Y + is.Pos.Y});
    return true;
}

int TravelMap::IslandUnder(Point p) const {
    std::vector<Point> hull;
    for (int i = 0; i < int(m_World.Islands().size()); ++i) {
        if (!IslandHull(i, hull)) continue;
        bool inside = false;
        const std::size_t n = hull.size();
        for (std::size_t a = 0, b = n - 1; a < n; b = a++) {
            if ((hull[a].Y > p.Y) == (hull[b].Y > p.Y)) continue;
            const double x = double(hull[b].X - hull[a].X) * (p.Y - hull[a].Y) /
                                 double(hull[b].Y - hull[a].Y) +
                             hull[a].X;
            if (p.X < x) inside = !inside;
        }
        if (inside) return i;
    }
    return -1;
}

bool TravelMap::OnLand(Point p) const { return IslandUnder(p) >= 0; }

bool TravelMap::ClearWater(Point a, Point b) const {
    std::vector<Point> hull;
    for (int i = 0; i < int(m_World.Islands().size()); ++i) {
        if (!IslandHull(i, hull)) continue;
        const std::size_t n = hull.size();
        for (std::size_t p = 0, q = n - 1; p < n; q = p++)
            if (SegmentsCross(a, b, hull[p], hull[q])) return false;
    }
    return true;
}

// The lane list is a set of undirected edges given by their endpoints, so the
// nodes are whatever distinct points appear in it. Built once.
void TravelMap::BuildLanes() {
    m_LaneNodes.clear();
    m_LaneAdj.clear();
    auto index = [&](Point p) {
        for (std::size_t i = 0; i < m_LaneNodes.size(); ++i)
            if (m_LaneNodes[i].X == p.X && m_LaneNodes[i].Y == p.Y) return int(i);
        m_LaneNodes.push_back(p);
        m_LaneAdj.emplace_back();
        return int(m_LaneNodes.size()) - 1;
    };
    for (const TravelWorld::Lane& l : m_World.Lanes()) {
        const int a = index(l.A), b = index(l.B);
        if (a == b) continue;
        m_LaneAdj[std::size_t(a)].push_back(b);
        m_LaneAdj[std::size_t(b)].push_back(a);
    }
}

int TravelMap::NearestLane(Point p, bool needClear, bool wetOnly) const {
    int best = -1, bestD = 1 << 28;
    for (std::size_t i = 0; i < m_LaneNodes.size(); ++i) {
        const int d = Hypot(m_LaneNodes[i].X - p.X, m_LaneNodes[i].Y - p.Y);
        if (d >= bestD) continue;
        if (wetOnly && OnLand(m_LaneNodes[i])) continue;
        if (needClear && !ClearWater(p, m_LaneNodes[i])) continue;
        bestD = d;
        best = int(i);
    }
    return best;
}

// A harbour, a fort or a village stands *on* its island, so a route to one can
// never actually reach it. The engine does not sail over the coast to get
// there: it drags the target back out along the line from the nearest sea lane
// until it is ten pixels clear of the land (0x100deb1c). Arriving is a
// proximity test, so stopping short still opens the mission.
TravelMap::Point TravelMap::OffLand(Point p, int* usedLane) const {
    if (usedLane) *usedLane = -1;
    if (!OnLand(p)) return p;
    // The nearest lane node is not necessarily in the water: the moorings sit
    // inside the bays, well within a coarse hull. Drag toward the nearest one
    // that is afloat, or there is nowhere dry to stop short of.
    const int lane = NearestLane(p, false, true);
    if (lane < 0) return p;
    if (usedLane) *usedLane = lane;
    const Point a = m_LaneNodes[std::size_t(lane)];
    const int span = Hypot(a.X - p.X, a.Y - p.Y);
    if (span <= 0) return a;
    auto along = [&](int t) {
        return Point{p.X + (a.X - p.X) * t / span, p.Y + (a.Y - p.Y) * t / span};
    };
    for (int t = 1; t <= span; ++t) {
        if (OnLand(along(t))) continue;
        // Ten more pixels of daylight -- but only as much of it as stays wet,
        // because a berth can be a narrow channel with the next shore right
        // behind it. Never past the lane node either.
        for (int u = (t + 10 < span ? t + 10 : span); u > t; --u) {
            const Point q = along(u);
            if (!OnLand(q) && ClearWater(q, a)) return q;
        }
        return along(t);
    }
    return a;
}

bool TravelMap::PlotRoute(Point from, Point to, std::vector<Point>& out) const {
    out.clear();
    int berth = -1;
    const Point target = OffLand(to, &berth);
    if (ClearWater(from, target)) {
        out.push_back(target);
        return true;
    }
    if (m_LaneNodes.empty()) return false;

    // Enter the network at the nearest node reachable in a straight line, and
    // leave it at the one the target was dragged out toward -- that lane is
    // the berth, and going anywhere else for the last hop is what put the
    // ship across the coast.
    int start = NearestLane(from, true);
    if (start < 0) start = NearestLane(from, false);
    int goal = berth;
    if (goal < 0) goal = NearestLane(target, true);
    if (goal < 0) goal = NearestLane(target, false);
    if (start < 0 || goal < 0) return false;

    const int kInf = 1 << 29;
    std::vector<int> dist(m_LaneNodes.size(), kInf), prev(m_LaneNodes.size(), -1);
    using Item = std::pair<int, int>;  // cost, node
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    dist[std::size_t(start)] = 0;
    open.push({0, start});
    while (!open.empty()) {
        const Item cur = open.top();
        open.pop();
        if (cur.first != dist[std::size_t(cur.second)]) continue;
        if (cur.second == goal) break;
        const Point here = m_LaneNodes[std::size_t(cur.second)];
        for (int n : m_LaneAdj[std::size_t(cur.second)]) {
            const Point there = m_LaneNodes[std::size_t(n)];
            const int alt = cur.first + Hypot(there.X - here.X, there.Y - here.Y);
            if (alt >= dist[std::size_t(n)]) continue;
            dist[std::size_t(n)] = alt;
            prev[std::size_t(n)] = cur.second;
            open.push({alt, n});
        }
    }
    if (dist[std::size_t(goal)] == kInf) return false;

    std::vector<Point> back;
    for (int n = goal; n >= 0; n = prev[std::size_t(n)])
        back.push_back(m_LaneNodes[std::size_t(n)]);
    out.assign(back.rbegin(), back.rend());
    // Drop the leading nodes the ship can already see past: the graph is
    // coarse, and without this a short hop doubles back to join it. This is
    // the engine's own line-of-sight pass (0x100deb1c's two smoothing loops),
    // kept to the ends where it matters most.
    while (out.size() > 1 && ClearWater(from, out[1])) out.erase(out.begin());
    while (out.size() > 1 && ClearWater(out[out.size() - 2], target)) out.pop_back();
    out.push_back(target);
    return true;
}

// 0x100dc2b8's tail. Eight directions, thirty-two pixels each, tried from a
// random start until one is at sea and inside the world; the ship then *sails*
// there rather than jumping (0x100bca4c asks the path finder for a route and
// queues it), so the retreat reads as a retreat.
//
// **The latch stays set.** 0x10095270 sets it before it calls the launcher and
// nothing in the launcher clears it -- only 0x10094290 does, once the ship is
// genuinely outside the twenty-four pixel box, and that test does not run
// while the mission is on screen. Clearing it here instead would fire the
// arrival again on the very next tick, before the ship had moved a pixel: back
// into the cutscene, back into the briefing, cancel, and round again. The
// `place` argument is kept because the caller names what is being backed away
// from, and it reads better than a bare push.
bool TravelMap::PushOffFrom(Point place) {
    (void)place;
    static const int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int kDy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    constexpr int kStep = 32;
    const Point from = Ship();
    m_Rng = m_Rng * 1103515245u + 12345u;
    const int start = int((m_Rng >> 16) & 7);
    for (int i = 0; i < 8; ++i) {
        const int k = (i + start) % 8;
        const Point to{from.X + kDx[k] * kStep, from.Y + kDy[k] * kStep};
        if (to.X < TravelWorld::kMinX || to.X > TravelWorld::kMaxX) continue;
        if (to.Y < TravelWorld::kMinY || to.Y > TravelWorld::kMaxY) continue;
        if (OnLand(to)) continue;
        if (SailTo(to)) return true;
    }
    return false;
}

// 0x100de698: one of three stings when a course is set, never the same one
// twice running -- the engine rolls `rand()%3` and, if it matches the last, it
// takes the next along and wraps. At the sfx volume, not the music one, even
// though travel.dat marks the whole bank `music 1`.
void TravelMap::PlayMoveSting() {
    if (!m_Ctx.Sound) return;
    m_Rng = m_Rng * 1103515245u + 12345u;
    int n = int((m_Rng >> 16) % unsigned(SoundManager::kTravelMoveCount));
    if (n == m_MoveSting && ++n >= SoundManager::kTravelMoveCount) n = 0;
    m_MoveSting = n;
    m_Ctx.Sound->Play(SoundManager::kBankTravel,
                     SoundManager::kTravelMoveFirst + n);
}

bool TravelMap::SailTo(Point p) {
    std::vector<Point> route;
    if (!PlotRoute(Ship(), p, route)) return false;
    m_Route = std::move(route);
    m_Leg = 0;
    m_Follow = true;
    return true;
}

const TravelWorld::Location* TravelMap::LocationAt(Point p) const {
    for (const TravelWorld::Location& loc : m_World.Locations()) {
        if (loc.Key.empty() || !IsOpen(loc.ID) || IsDone(loc.ID)) continue;
        if (std::abs(loc.Pos.X - p.X) >= kReach) continue;
        if (std::abs(loc.Pos.Y - p.Y) >= kReach) continue;
        return &loc;
    }
    return nullptr;
}

// 0x100bc35c's steering: turn toward the waypoint at a fixed rate, and only
// put on speed once you are pointing roughly at it.
void TravelMap::Steer() {
    const int d = TurnDelta(m_Heading, m_WantHeading);
    if (d > kTurnRate)
        m_Heading += kTurnRate;
    else if (d < -kTurnRate)
        m_Heading -= kTurnRate;
    else
        m_Heading = m_WantHeading;
    m_Heading &= 0x3FF;
}

TravelMap::Event TravelMap::Sail() {
    if (m_Leg >= int(m_Route.size())) {
        // Nothing to chase: coast to a stop.
        m_VelX = m_VelX * 2 / 3;
        m_VelY = m_VelY * 2 / 3;
    } else {
        const Point wp = m_Route[std::size_t(m_Leg)];
        const int dx = (wp.X << 16) - m_ShipX, dy = (wp.Y << 16) - m_ShipY;
        m_WantHeading = HeadingOf(dx >> 16, dy >> 16);
        const int dist = Hypot(dx >> 16, dy >> 16);
        if (std::abs(TurnDelta(m_Heading, m_WantHeading)) < kFaceTolerance) {
            if (dist < kArrive) {
                ++m_Leg;
            } else {
                // v = (v - s) * 5/6 + s, which settles on exactly s.
                const int sx = int((int64_t(dx) * kCruise) / dist);
                const int sy = int((int64_t(dy) * kCruise) / dist);
                m_VelX = (m_VelX - sx) * 5 / 6 + sx;
                m_VelY = (m_VelY - sy) * 5 / 6 + sy;
            }
        } else {
            m_VelX = m_VelX * 2 / 3;
            m_VelY = m_VelY * 2 / 3;
        }
    }
    Steer();

    m_ShipX += m_VelX;
    m_ShipY += m_VelY;
    const Point now = Ship();

    // Distance sailed, in the engine's units: |v| >> 4, times four, and one
    // unit of the encounter counter per 65536 of it.
    const int speed = Hypot(m_VelX >> 4, m_VelY >> 4) * 4;
    if (speed > 0) m_Travelled += speed;
    Event e = Event::kNone;
    while (m_Travelled > (1 << 16)) {
        m_Travelled -= 1 << 16;
        const Event r = RollEncounters(now);
        if (r != Event::kNone) e = r;
    }

    if (m_Leg >= int(m_Route.size()) && !m_Route.empty()) {
        m_Route.clear();
        m_Leg = 0;
    }
    return e;
}

// Arriving is not finishing a route: the engine gives every location a
// per-frame proximity test (0x10094290) -- within twenty-four pixels on both
// axes of a discovered place -- with a latch so it fires once per approach and
// not again until the ship has left. Sailing *past* somewhere opens it too.
TravelMap::Event TravelMap::CheckArrival() {
    const Point s = Ship();
    Event e = Event::kNone;
    for (const TravelWorld::Location& loc : m_World.Locations()) {
        if (loc.Key.empty()) continue;
        const bool near = std::abs(loc.Pos.X - s.X) < kReach &&
                          std::abs(loc.Pos.Y - s.Y) < kReach;
        const auto held = std::find(m_Latched.begin(), m_Latched.end(), loc.ID);
        if (!near) {
            if (held != m_Latched.end()) m_Latched.erase(held);
            continue;
        }
        if (held != m_Latched.end()) continue;
        m_Latched.push_back(loc.ID);
        if (!IsOpen(loc.ID) || IsDone(loc.ID)) continue;
        m_Arrived = &loc;
        e = Event::kArrived;
    }
    return e;
}

TravelMap::Event TravelMap::RollEncounters(Point at) {
    const int area = TravelWorld::AreaOf(at);
    if (area < 0 || area >= TravelWorld::kAreas) return Event::kNone;
    if (!m_State.Known[area]) return Event::kNone;
    const int sailed = m_State.Sailed[area];
    for (const TravelWorld::Encounter& e : m_World.Encounters()) {
        if (e.Area != area) continue;
        if (std::find(m_State.Spent.begin(), m_State.Spent.end(), e.ID) !=
            m_State.Spent.end())
            continue;
        if (sailed < e.Distance) continue;
        if (e.Rate <= 0 || sailed % e.Rate != 0) continue;
        m_Rng = m_Rng * 1103515245u + 12345u;
        if (int((m_Rng >> 16) % 100) >= e.Chance) continue;
        m_State.Spent.push_back(e.ID);
        m_Ambush = &e;
        break;
    }
    // The counter advances whatever happened, exactly as 0x100ddc6c does it --
    // otherwise a fired encounter would leave the next unit sitting on the
    // same multiple of `rate` and every other one in the quadrant would roll
    // again immediately.
    ++m_State.Sailed[area];
    return m_Ambush ? Event::kAmbush : Event::kNone;
}

// The flocks' flight -- the rules live in Flock::Step, shared with the menu
// water's own flock. Only the wiring is here: the ship that scares them, the
// camera that gates them, and the map's one random stream.
void TravelMap::StepFlocks() {
    const Point ship = Ship();
    for (Flock& f : m_Flocks)
        f.Step(ship.X, ship.Y, m_CamX >> 16, m_CamY >> 16, m_Rng);
}

// The gulls' flight, 0x1000d208 term for term. The steering never aims: each
// tick the bird takes the raw difference between the heading to the ship and
// its own -- unwrapped, so a bird just past the ship sees a huge number -- and
// turns by a 32nd of it in the one direction its slot parity allows. That is
// the whole algorithm, and it produces two lazy opposite orbits that
// straighten out whenever a bird happens to line up on the ship. Speed is
// always two pixels a tick along the heading; the pool then integrates the
// position (0x100e2b60's tail). The frame block is the heading's sixteenth,
// sixteen frames each: flapping cycles all sixteen off the counter, and a
// bird pointed within 0x41 of the ship holds frame 9, the glide.
void TravelMap::StepGulls() {
    for (Gull& g : m_Gulls) {
        ++g.Flap;
        const int want =
            HeadingOf((m_ShipX - g.X) >> 16, (m_ShipY - g.Y) >> 16);
        const int d = std::abs(want - g.Heading);
        g.Heading = (g.Heading + (g.Clockwise ? d >> 5 : -(d >> 5))) & 0x3FF;
        // The engine reads its 1024-entry 16.16 sine table at the heading and
        // a quarter-turn on (0x1008c12c / 0x1008c14c) and doubles it: with
        // south at zero, x runs on the sine and y on the cosine.
        const double a = g.Heading * (2.0 * 3.14159265358979323846 / 1024.0);
        g.Vx = int(std::lround(std::sin(a) * kGullSpeed));
        g.Vy = int(std::lround(std::cos(a) * kGullSpeed));
        const int base = (g.Heading >> 2) & 0xFFF0;
        g.Frame = base + (d < kGullGlide ? 9 : (g.Flap & 0xF));
        g.X += g.Vx;
        g.Y += g.Vy;
    }
}

// 0x1000d2d4: the shadow rides the sea five right and twenty-five below the
// gull's sea-level point, at alpha 5 like the ship's reflection; the bird
// itself flies twenty-five above it. Drawn after the ship, over everything.
void TravelMap::DrawGulls(Surface& dst, Point origin) {
    for (const Gull& g : m_Gulls) {
        const int x = (g.X >> 16) - origin.X;
        const int y = (g.Y >> 16) - origin.Y;
        if (m_GullShadow && m_GullShadow->Valid()) {
            const int frames = int(m_GullShadow->Frames.size());
            if (const TcTexture::Image* img =
                    m_GullShadow->Frame(frames > 0 ? g.Frame % frames : 0))
                dst.Blit(img->Pixels.data(), img->Width, img->Height,
                         x + kGullShadowX, y + kGullHover, kGullShadowAlpha);
        }
        if (m_Gull && m_Gull->Valid()) {
            const int frames = int(m_Gull->Frames.size());
            if (const TcTexture::Image* img =
                    m_Gull->Frame(frames > 0 ? g.Frame % frames : 0))
                dst.Blit(img->Pixels.data(), img->Width, img->Height, x,
                         y - kGullHover);
        }
    }
}

void TravelMap::DrawFlocks(Surface& dst, Point origin) {
    for (const Flock& f : m_Flocks) f.Draw(dst, origin.X, origin.Y);
}

TravelMap::Event TravelMap::Tick() {
    Host& host = m_Ctx.HostRef;
    ++m_Frame;
    m_Arrived = nullptr;
    m_Ambush = nullptr;
    m_Selected = nullptr;

    // The menu key. 0x100db3e0's key 1 does two things with it and which one
    // depends on the ship: under way it is the order to heave to, and only a
    // ship standing still raises the menu.
    if (host.KeyPressed(Key::kSoftLeft)) {
        if (Sailing()) {
            m_Route.clear();
            m_Leg = 0;
        } else {
            return Event::kMenu;
        }
    }
    if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight)) {
        if (Sailing()) {
            m_Route.clear();
            m_Leg = 0;
        } else {
            return Event::kLeave;
        }
    }
    if (m_ChartOpen) return Event::kNone;

    // The pointer is not stepped, it is driven: a direction key adds to a
    // velocity that decays to four fifths every tick, so it settles on four
    // times the acceleration rather than one (0x100db3e0). Holding the run
    // key doubles the acceleration *and* snaps the camera onto the pointer;
    // pressing two directions at once scales each to two thirds, so a
    // diagonal is not faster than a straight line.
    const bool left = host.KeyHeld(Key::kLeft), right = host.KeyHeld(Key::kRight);
    const bool up = host.KeyHeld(Key::kUp), down = host.KeyHeld(Key::kDown);
    int accel = kCursorSpeed;
    if ((left || right) && (up || down)) accel = accel * 2 / 3;
    if (!(left || right || up || down)) {
        m_VcursorX = m_VcursorX * 3 / 4;
        m_VcursorY = m_VcursorY * 3 / 4;
    } else {
        m_Follow = false;
        if (left) m_VcursorX -= accel;
        if (right) m_VcursorX += accel;
        if (up) m_VcursorY -= accel;
        if (down) m_VcursorY += accel;
    }
    // The pointer will not walk into a quadrant that has not been charted:
    // 0x100dbbac tries each axis on its own against where the velocity would
    // land it, and stops the one that crosses the line. This -- not the world
    // edge -- is what bounds the view for most of the campaign.
    {
        const Point c = Cursor();
        if (!Charted({(m_CursorX + m_VcursorX) >> 16, c.Y})) m_VcursorX = 0;
        if (!Charted({c.X, (m_CursorY + m_VcursorY) >> 16})) m_VcursorY = 0;
    }
    m_CursorX += m_VcursorX;
    m_CursorY += m_VcursorY;
    // And it stops twenty pixels inside the world besides.
    m_CursorX = Clamp(m_CursorX >> 16, TravelWorld::kMinX + kEdgeInset,
                      TravelWorld::kMaxX - kEdgeInset)
                << 16;
    m_CursorY = Clamp(m_CursorY >> 16, TravelWorld::kMinY + kEdgeInset,
                      TravelWorld::kMaxY - kEdgeInset)
                << 16;
    m_VcursorX = m_VcursorX * 4 / 5;
    m_VcursorY = m_VcursorY * 4 / 5;

    if (host.KeyPressed(Key::kSelect)) {
        const Point c = Cursor();
        if (const TravelWorld::Location* loc = LocationAt(c)) {
            // Picking a place does not set sail: it opens the "Open mission"
            // board first (0x100bc09c builds a TextBox in Ok/Cancel mode with
            // string 2141 over the location's own travel blurb), and only Ok
            // orders the ship.
            m_Selected = loc;
            return Event::kSelected;
        }
        if (!OnLand(c) && SailTo(c)) PlayMoveSting();
    }

    Event e = Sail();
    if (e == Event::kNone) e = CheckArrival();

    // The pointer rides along until the player takes hold of it -- but it
    // will not follow the ship into uncharted water (0x100ddbe0 guards the
    // snap), so an opening leg through an unknown quadrant leaves the view
    // behind at the border.
    if (m_Follow && Charted(Ship())) {
        m_CursorX = m_ShipX;
        m_CursorY = m_ShipY;
    }
    // The camera eases an eighth of the way to the pointer each tick, then
    // 0x100dbce0 holds it twice over: the screen may show at most sixteen
    // pixels past the charted quadrants, and never anything past the world.
    m_CamX += (m_CursorX - m_CamX) >> 3;
    m_CamY += (m_CursorY - m_CamY) >> 3;
    {
        int kminx, kminy, kmaxx, kmaxy;
        KnownBox(kminx, kminy, kmaxx, kmaxy);
        const int mx = kKnownMargin << 16;
        const int hw = kHalfW << 16, hh = kHalfH << 16;
        if (m_CamX - hw < kminx - mx) m_CamX = kminx - mx + hw;
        if (m_CamY - hh < kminy - mx) m_CamY = kminy - mx + hh;
        if (kmaxx + mx < m_CamX + hw) m_CamX = kmaxx + mx - hw;
        if (kmaxy + mx < m_CamY + hh) m_CamY = kmaxy + mx - hh;
        if (m_CamX - hw < TravelWorld::kMinX << 16)
            m_CamX = (TravelWorld::kMinX << 16) + hw;
        if (m_CamY - hh < TravelWorld::kMinY << 16)
            m_CamY = (TravelWorld::kMinY << 16) + hh;
        if ((TravelWorld::kMaxX << 16) < m_CamX + hw)
            m_CamX = (TravelWorld::kMaxX << 16) - hw;
        if ((TravelWorld::kMaxY << 16) < m_CamY + hh)
            m_CamY = (TravelWorld::kMaxY << 16) - hh;
    }

    m_Sea.Step();
    StepFlocks();
    StepGulls();

    m_State.ShipX = m_ShipX >> 16;
    m_State.ShipY = m_ShipY >> 16;
    m_State.Heading = m_Heading;
    return e;
}

void TravelMap::DrawSea(Surface& dst, Point origin) {
    if (!m_Sea.Valid()) {
        dst.Fill(0xF135);
        return;
    }
    const int size = m_Sea.Size();
    const int ox = ((origin.X % size) + size) % size;
    const int oy = ((origin.Y % size) + size) % size;
    for (int y = -oy; y < Surface::kHeight; y += size)
        for (int x = -ox; x < Surface::kWidth; x += size)
            dst.Copy(m_Sea.Pixels(), size, size, x, y);
}

// The shallow water round a coast, blended and shoved sideways a row at a time
// by the ripple field (0x100d87c0's first pass). One displacement per row,
// taken from column zero of that row of the tank and divided by 64; the surf
// run carries four columns of slop either side so the shove stays inside it.
void TravelMap::DrawSurf(Surface& dst, const Texture* t, Point pos,
                         Point origin) {
    if (!t || !t->Water) return;
    const TcTexture::Image* img = t->Surf(0);
    if (!img) return;
    const int dx = pos.X - origin.X, dy = pos.Y - origin.Y;
    if (dx >= dst.Width() || dy >= dst.Height() || dx + img->Width < 0 ||
        dy + img->Height < 0)
        return;
    const int32_t* heights = m_Sea.Heights();
    const int mask = m_Sea.Mask(), shift = m_Sea.Shift();
    for (int y = 0; y < img->Height; ++y) {
        const int py = dy + y;
        if (py < 0 || py >= dst.Height()) continue;
        const int wave =
            heights ? int(heights[std::size_t((y & mask) << shift)] >> kSurfShift)
                    : 0;
        const int shove = Clamp(wave, -TcTexture::kSurfShift, TcTexture::kSurfShift);
        const uint16_t* srow = img->Pixels.data() + std::size_t(y) * img->Width;
        uint16_t* drow = dst.Pixels() + std::size_t(py) * dst.Width();
        for (int x = 0; x < img->Width; ++x) {
            const int sx = x - shove;
            if (sx < 0 || sx >= img->Width) continue;
            const int px = dx + x;
            if (px < 0 || px >= dst.Width()) continue;
            const uint16_t c = srow[sx];
            if (c) drow[px] = BlendArgb4444(c, drow[px]);
        }
    }
}

void TravelMap::Draw(Surface& dst) {
    if (m_ChartOpen) {
        DrawChart(dst);
        return;
    }
    const Point origin{(m_CamX >> 16) - kHalfW, (m_CamY >> 16) - kHalfH};
    DrawSea(dst, origin);

    // Two anchors. Scenery -- islands, location icons, the pointer -- hangs
    // from its position by the top-left corner; anything that moves is centred
    // on it, because the engine writes that `- w/2` out by hand every time
    // (0x100bc7a8 for the ship, 0x100dbf2c for the flag).
    auto pin = [&](const Texture* t, int frame, int wx, int wy, int alpha = 15) {
        if (!t || !t->Valid()) return;
        const TcTexture::Image* img = t->Frame(frame);
        if (!img) return;
        dst.Blit(img->Pixels.data(), img->Width, img->Height, wx - origin.X,
                 wy - origin.Y, alpha);
    };
    auto blit = [&](const Texture* t, int frame, int wx, int wy, int alpha = 15) {
        if (!t || !t->Valid()) return;
        const TcTexture::Image* img = t->Frame(frame);
        if (!img) return;
        dst.Blit(img->Pixels.data(), img->Width, img->Height,
                 wx - origin.X - img->Width / 2, wy - origin.Y - img->Height / 2,
                 alpha);
    };

    // Islands, surf first. 0x100dabb4 makes two passes with the haze between
    // them, so the shallow water goes under it and the land over.
    for (std::size_t i = 0; i < m_World.Islands().size(); ++i)
        DrawSurf(dst, i < m_Islands.size() ? m_Islands[i] : nullptr,
                 m_World.Islands()[i].Pos, origin);

    DrawFlocks(dst, origin);

    // The haze drifts against the camera at two thirds its speed, pinned to
    // wherever the voyage was picked up: 0x100dabb4 works out
    // `anchor + (cam - anchor)/3 - cam`, which is that, and then backs off an
    // eighth of the texture's width and height.
    if (m_Haze && m_Haze->Valid()) {
        const TcTexture::Image* img = m_Haze->Frame(0);
        if (img) {
            const int hx = m_AnchorX >> 16, hy = m_AnchorY >> 16;
            const int cx = m_CamX >> 16, cy = m_CamY >> 16;
            dst.Blit(img->Pixels.data(), img->Width, img->Height,
                     hx + (cx - hx) / 3 - cx - img->Width / 8,
                     hy + (cy - hy) / 3 - cy - img->Height / 8);
        }
    }

    for (std::size_t i = 0; i < m_World.Islands().size(); ++i) {
        const TravelWorld::Island& is = m_World.Islands()[i];
        const Texture* t = i < m_Islands.size() ? m_Islands[i] : nullptr;
        if (!t || !t->Valid()) continue;
        // The land layer is copied, not blended (0x100d87c0's second pass is a
        // straight Mem__Copy); Blit does the same for an opaque pixel and
        // leaves the sea alone where the layer is empty.
        pin(t, 0, is.Pos.X, is.Pos.Y);
    }

    for (std::size_t i = 0; i < m_World.Locations().size(); ++i) {
        const TravelWorld::Location& loc = m_World.Locations()[i];
        if (loc.Texture.empty()) continue;
        const bool known = IsOpen(loc.ID) || IsDone(loc.ID);
        if (!known) continue;
        // A location's icon is centred on the place it marks -- 0x100943c8
        // takes half its width and height off before drawing it. Islands are
        // not; they hang from their top-left corner.
        const Texture* icon = i < m_LocIcons.size() ? m_LocIcons[i] : nullptr;
        blit(icon, 0, loc.Pos.X, loc.Pos.Y);
        if (!IsDone(loc.ID) && !loc.Key.empty() && m_Flag && m_Flag->Valid()) {
            // The flag hangs fifteen up and left of the place it marks, and
            // drops another thirty and half its own width when there is no
            // icon under it to plant it in (0x100dbf2c's two offsets).
            const int frames = int(m_Flag->Frames.size());
            const bool bare = !icon || !icon->Valid();
            const int fw = m_Flag->Width;
            pin(m_Flag, frames > 0 ? int(m_Frame % uint32_t(frames)) : 0,
                loc.Pos.X - 15 - (bare ? fw / 2 : 0),
                loc.Pos.Y - 15 - (bare ? 30 : 0));
        }
    }

    // The route, as a dotted guide. The engine puts a dot on every third point
    // of its own path and that path is dense; the port's is a handful of
    // sea-lane corners, so the dots are stepped along the legs instead.
    //
    // Stepped from the *destination* backwards, which matters: measured from
    // there, every dot sits at a fixed spot in the world and the ship eats
    // them one by one as it goes. Measured from the ship they would slide
    // along with it, and the guide would look like it was being fed into the
    // target rather than consumed.
    if (m_Leg < int(m_Route.size())) {
        std::vector<Point> line;
        line.push_back(Ship());
        for (int i = m_Leg; i < int(m_Route.size()); ++i)
            line.push_back(m_Route[std::size_t(i)]);
        int carry = 0;
        for (std::size_t i = line.size() - 1; i > 0; --i) {
            const Point head = line[i], tail = line[i - 1];
            const int span = Hypot(tail.X - head.X, tail.Y - head.Y);
            if (span <= 0) continue;
            for (int d = kDotSpacing - carry; d < span; d += kDotSpacing) {
                blit(m_RouteDot, 0, head.X + (tail.X - head.X) * d / span,
                     head.Y + (tail.Y - head.Y) * d / span + 4);
            }
            carry = (span + carry) % kDotSpacing;
        }
        blit(m_Dest, 0, m_Route.back().X, m_Route.back().Y + 4);
    }

    const int shipFrame = m_Heading >> 5;   // 32 headings
    blit(m_ShipRefl, shipFrame, m_ShipX >> 16, (m_ShipY >> 16) + 16, 5);
    if (m_Ship && m_Ship->Valid()) {
        const int frames = int(m_Ship->Frames.size());
        int f = shipFrame * 2 + int((m_Frame >> 3) & 1);
        if (frames > 0) f %= frames;
        blit(m_Ship, f, m_ShipX >> 16, m_ShipY >> 16);
    }

    // The gulls go over the ship and the land both: their pool is drawn last
    // of the world layers in 0x100dabb4, just before the pointer.
    DrawGulls(dst, origin);

    // While the pointer is riding the ship there is nothing to point at, so
    // the engine leaves it off entirely (0x100dabb4 guards the whole call with
    // its follow flag, and 0x100daf3c hides it again while 5 is held).
    if (!m_Follow && !m_Ctx.HostRef.KeyHeld(Key::kSelect)) {
        const Point c = Cursor();
        pin(m_Pointer, 0, c.X, c.Y);
        pin(LocationAt(c) ? m_OverLocation : m_OverSea, 0, c.X + 8, c.Y + 8);
    }
}

// 0x100da688: the whole world squeezed onto one of four painted charts, with
// a marker on every location that has been found. The page is chosen by which
// quadrants are known, in the engine's own priority: the opening one loses to
// every other, and the top-right wins outright.
void TravelMap::DrawChart(Surface& dst) {
    int page = 0;
    if (m_State.Known[0]) page = 1;
    if (m_State.Known[2]) page = 2;
    if (m_State.Known[3]) page = 3;
    if (m_State.Known[1]) page = 4;
    if (page < 1) page = 1;
    const Texture* sheet = m_Chart[page - 1];
    if (sheet && sheet->Valid()) {
        if (const TcTexture::Image* img = sheet->Frame(0))
            dst.Copy(img->Pixels.data(), img->Width, img->Height, 0, 0);
    } else {
        dst.Fill(0xF000);
    }
    // Only the opening chart is drawn at double scale, which is what makes one
    // quadrant fill the sheet.
    const int scale = page == 1 ? 2 : 1;
    const int spanX = kChartW * scale, spanY = kChartH * scale;
    auto place = [&](Point p, const Texture* t, int frame) {
        if (!t || !t->Valid()) return;
        const TcTexture::Image* img = t->Frame(frame);
        if (!img) return;
        const int x = kChartX +
                      int(int64_t(p.X - TravelWorld::kMinX) * spanX /
                          (TravelWorld::kMaxX - TravelWorld::kMinX)) -
                      img->Width / 2;
        const int y = kChartY +
                      int(int64_t(p.Y - TravelWorld::kMinY) * spanY /
                          (TravelWorld::kMaxY - TravelWorld::kMinY)) -
                      img->Height / 2;
        dst.Blit(img->Pixels.data(), img->Width, img->Height, x, y);
    };
    for (const TravelWorld::Location& loc : m_World.Locations()) {
        if (loc.Key.empty() || !(IsOpen(loc.ID) || IsDone(loc.ID))) continue;
        place(loc.Pos, m_Marker, 0);
    }
    place(Ship(), m_Pointer, 0);
}

TravelMap::Event TravelMap::Run() {
    Host& host = m_Ctx.HostRef;
    host.FlushKeys();
    m_LastTick = host.TickCount();
    for (;;) {
        if (host.QuitRequested()) return Event::kQuit;
        const uint32_t now = host.TickCount();
        // Only whole slices, and never a free one: 0x100ba3a0 divides the
        // elapsed time by 31 and runs that many ticks, capped at 32. Ticking
        // once per pass round the loop instead runs the whole map at whatever
        // rate the host draws at, which off-device is three times too fast.
        // Nothing is dropped by not ticking: a keypress stays pending until
        // something consumes it.
        int steps = int((now - m_LastTick) / kTickMs);
        if (steps > 32) steps = 32;
        m_LastTick += uint32_t(steps) * kTickMs;
        for (int i = 0; i < steps; ++i) {
            const Event e = Tick();
            if (e != Event::kNone) {
                Draw(host.Screen());
                host.Flip();
                return e;
            }
        }
        Draw(host.Screen());
        host.Flip();
        // Every loop that holds the screen has to keep the mixer fed -- the
        // host only ever gets the blocks somebody renders for it. The chart
        // was the one loop that did not, so its music started and then sat
        // there unread: a voice nothing pumps is a silent voice.
        if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
        host.Sleep(10);
    }
}

void ResetTravelState(const TravelWorld& world, TravelState& state) {
    state = TravelState{};
    state.Started = true;
    const TravelWorld::Point start = world.Start();
    state.ShipX = start.X;
    state.ShipY = start.Y;
    for (const TravelWorld::Location& loc : world.Locations())
        if (loc.Open && !loc.Key.empty()) state.Open.push_back(loc.ID);
    // Whatever quadrants the opening locations sit in are already charted,
    // which is what lets encounters happen there at all (0x100dd1cc).
    for (int id : state.Open) {
        const TravelWorld::Location* loc = world.ById(id);
        if (loc) state.Known[TravelWorld::AreaOf(loc->Pos)] = true;
    }
    state.Known[TravelWorld::AreaOf(start)] = true;
}

void CompleteLocation(const TravelWorld& world, TravelState& state, int id) {
    if (std::find(state.Done.begin(), state.Done.end(), id) == state.Done.end())
        state.Done.push_back(id);
    for (const TravelWorld::Location& loc : world.Locations()) {
        if (loc.Connections.empty()) continue;
        if (std::find(state.Open.begin(), state.Open.end(), loc.ID) !=
            state.Open.end())
            continue;
        bool all = true;
        for (const TravelWorld::Connection& c : loc.Connections)
            if (std::find(state.Done.begin(), state.Done.end(), c.ID) ==
                state.Done.end())
                all = false;
        if (!all) continue;
        state.Open.push_back(loc.ID);
        state.Known[TravelWorld::AreaOf(loc.Pos)] = true;
    }
}

}  // namespace bb
