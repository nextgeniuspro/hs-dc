// TravelMap — the sea between the missions.
//
// The campaign is not a list of battles: it is a chart of the Caribbean that
// the player sails around, with the next mission waiting at a harbour, a fort
// or a village somewhere on it. The engine calls this the travel view
// (0x100ba3a0 runs the screen, 0x100db150 ticks it, 0x100dabb4 draws it) and
// hangs it off TravelEngineCore, which owns the world (see TravelWorld.h), the
// ship and every board the map can raise.
//
// **What is on screen.** 0x100dabb4, in order:
//
//   1  the sea: one 64x64 ripple tank refracting `water.tc`, tiled four across
//      and five down against the world grid, so the light moves but nothing
//      slides
//   2  the islands. An island hangs from its `<position>` by the texture's
//      *top-left*, not its middle -- which is what puts the harbours and
//      forts on land and the player's berth at sea -- and its hull polygon
//      is in the same frame
//   3  `Data\travel\256.tc`, a 256-square white haze that drifts against the
//      camera at two thirds its speed, pinned to where the voyage was picked
//      up -- the one parallax layer on the map
//   4  the locations: a 48x48 icon each, pinned the same way, with
//      `locationflag.tc` (15 frames) planted fifteen up and left of the ones
//      that are open -- and another (w/2, 30) further when the place has no
//      icon to plant it in, which six of them do not: they name `mission.tc`
//      or `skull.tc` and neither is in the pak
//   5  the ship: its route as a line of `route.tc` dots stepped back from
//      `adestination.tc` on the target -- so they stand still in the world
//      and the ship eats them -- its reflection drawn *through* the water at
//      alpha 5 and sixteen pixels lower, then the hull itself
//   6  the pointer, and over a reachable location `amission.tc` instead of
//      `amove-to.tc`
//
// **The ship** (0x100bc35c) is not steered, it is sent. Heading is in the
// engine's usual 1024-to-a-turn units, and 0x1008be18 pins where they start:
// with dy zero it returns 254 for dx positive and 764 for dx negative, so
// **south is zero and east is a quarter turn** -- `atan2(dx, dy)`. Frame =
// heading/32 * 2, plus one bit of bob from a frame counter. The little flag
// on `ship.tc`'s spar is the **stern** ensign, not a bowsprit: at due east
// the frame flies it west. Read it as the bow and the ship sails backwards. It turns at most
// 16 units a tick, and only accelerates once it is within 128 of the heading
// it wants; otherwise it slows to two thirds. The approach to cruising speed
// is `v = (v - s) * 5/6 + s` with `s` two pixels a tick, so it converges on
// exactly two and gets there smoothly. A waypoint is reached at eleven pixels.
//
// **Picking a place** does not set sail. It raises the "Open mission" board
// (0x100bc09c: a TextBox in Ok/Cancel mode, string 2141 over the location's
// own `<desc>` blurb) and only Ok orders the ship. **Arriving** is not
// finishing a route either: every location runs a proximity test each frame
// (0x10094290, twenty-four pixels on both axes) with a latch, so sailing
// *past* somewhere opens it and stopping short of a coast still counts.
//
// **Where a route may go.** Never over land. A harbour stands on its island,
// so the target is dragged back out to the water beside it before anything is
// plotted (0x100deb1c does the same, ten pixels clear); the legs between are
// the world's own sea lanes, which the engine trusts and does not test --
// just as well, because the lane into a dock runs alongside the coast.
//
// **Encounters** (0x100ddc6c). Every pixel sailed adds one to a counter kept
// per quadrant of the map -- and only if that quadrant has been discovered, so
// the opening leg is safe. Each of the world's twelve encounters names a
// quadrant, a distance that must be behind you, a rate and a percentage: once
// past the distance, every `rate` units it rolls `chance` out of a hundred.
// Three per quadrant, each good once.
//
// **Keys.** The d-pad drives the pointer -- it adds to a velocity that decays
// to four fifths a tick, so it settles at four times the acceleration, and a
// diagonal is scaled to two thirds so it is no faster than a straight line.
// The pointer stops twenty pixels inside the world -- but well before that it
// runs into the edge of the *charted* sea: the map is split into four
// quadrants, and 0x100dbbac stops whichever axis of the pointer's velocity
// would carry it into one that has not been charted yet. The camera is held
// harder still: 0x100dbce0 clamps it so the screen shows at most sixteen
// pixels past the charted quadrants, then keeps it inside the world outright.
// A fresh voyage knows only the opening quadrant, so the view after Broken
// Tranquility is a quarter of the chart, and it grows as missions open the
// rest. (The engine's run key, which doubles the acceleration and pushes the
// camera directly, is not ported: there is no key free for it.) The pointer
// otherwise rides along with the ship -- though it will not follow it into
// uncharted water (0x100ddbe0); select
// sails to it, or into the location under it; the left soft key raises the
// menu (0x100db3e0's key 1, which sets the travel core's +0xa6 for the screen
// above to notice) or, with the ship under way, heaves to; back stops the
// ship, or leaves the map if it is already still. The fold-out chart
// (`map1..4.tc` and a marker per known location) has the engine's own key 6 to
// itself on the device; here it is the menu's Map row, because there is no key
// spare for it.
//
// **The seagulls** are not the flocks. Two gulls live in an 8-slot
// ParticlePool (0x100e2c60) owned by a manager the world-loader builds at
// +0x90 (0x1000cfe8), and they are real sprites: `seagull.tc`, 21x21 with 256
// frames -- sixteen headings by sixteen flap frames -- plus a 256-frame
// `seagull_shadow.tc` drawn at alpha 5. Each is spawned on the ship
// (0x1000d1c4) and steered every tick (0x1000d208) by the strangest little
// rule in the game: take the *raw, unwrapped* difference between the heading
// to the ship and your own, and turn by a 32nd of it -- one gull always
// clockwise, the other always anticlockwise, by the parity of its pool slot.
// The result is a pair of loose, opposite orbits round the ship that
// straighten into a glide (frame 9 of the block) whenever the bird happens to
// point within 0x41 units of it, and flap (the counter's low four bits)
// otherwise. Speed is two pixels a tick, always. The gull is drawn 25 pixels
// above its sea-level point and its shadow 25 below it and 5 right
// (0x1000d2d4), after the ship, so they fly over everything.
//
// **The flocks** (0x100716f4, 0x100718cc, 0x10071e98) are five groups of ten,
// scattered thirty pixels round fixed points and flying as boids -- but boids
// that never settle, which is the whole trick. Every other tick each member
// gathers on its neighbours, shoves off the close ones, blends a fifth of the
// way toward the flock's average velocity, and then steers at a point the
// flock re-rolls at random near its home every update -- so there is always
// somewhere new to be, and they mill instead of parking. The ship scatters
// anything it sails within sixteen pixels of. A flock only lives while the
// camera is within two hundred pixels of its home. Not one of them is a
// sprite: each member is a short streak drawn straight to the screen, and its
// colour comes out of its own speed -- a slow one is a dark speck on the
// water and a fast one is a white dash, which is what makes some read as
// birds and some as fish.
//
// The menu the engine hangs off this screen -- open missions, log book, skill
// chart, save, map, pause -- is TravelMenu.h; this file only raises the event
// that asks for it. Not ported: the wake particles.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/Flock.h"
#include "game/SeaSurface.h"
#include "game/TextureCache.h"
#include "game/TravelWorld.h"
#include "platform/Surface.h"

namespace bb {

struct GameContext;
struct Texture;

class TravelMap {
public:
    using Point = TravelWorld::Point;

    // Why the map gave control back.
    enum class Event {
        kNone,
        kQuit,      // the host wants out
        kLeave,     // the player left the map
        kArrived,   // the ship reached an open location
        kAmbush,    // an encounter fired
        kSelected,  // a place was picked: raise its "Open mission" board
        kMenu,      // the menu key, with the ship standing still
    };

    // The engine's own numbers (0x100bc35c, 0x100db3e0, 0x100ba3a0).
    static constexpr int kTickMs = 31;        // one simulation slice
    static constexpr int kTurnRate = 16;      // heading units a tick
    static constexpr int kCruise = 2;         // pixels a tick, once up to speed
    static constexpr int kArrive = 11;        // pixels: a waypoint is reached
    static constexpr int kFaceTolerance = 128;  // turn before you accelerate
    // The pointer's acceleration, not its speed: it settles on four times
    // this, because the velocity decays to four fifths a tick.
    static constexpr int kCursorSpeed = 0x19000;   // 16.16 a tick
    static constexpr int kEdgeInset = 20;     // how far inside the world the
                                              // pointer stops
    static constexpr int kKnownMargin = 16;   // how far past the charted
                                              // quadrants the screen may show
    static constexpr int kReach = 24;         // pixels: the pointer's grab,
                                              // and how near counts as arrived
    static constexpr int kTile = 64;          // the sea tile
    static constexpr int kDotSpacing = 12;    // pixels between route dots
    static constexpr int kSurfShift = 6;      // ripple height -> surf texels
    // The five flocks the world loader scatters (0x100d95e4); the boid rules
    // themselves live in Flock.h, shared with the menu water's own flock.
    static constexpr int kFlockCount = 5;
    static constexpr int kFlockSize = Flock::kSize;
    static constexpr int kFlockScatter = Flock::kScatter;
    // The seagulls (0x1000d084 spawns exactly two).
    static constexpr int kGullCount = 2;
    static constexpr int kGullSpeed = 2 << 16;   // 0x1000d208: sin/cos << 1
    static constexpr int kGullGlide = 0x41;      // aligned this well? glide
    static constexpr int kGullHover = 25;        // drawn this far above the sea
    static constexpr int kGullShadowX = 5;       // and the shadow this far off
    // How fast the chart's music comes up out of silence: 0x100f1774's ramp
    // step, in volume units per decoded block.
    static constexpr int kMusicFadeStep = 0x10;
    static constexpr int kGullShadowAlpha = 5;

    TravelMap(GameContext& ctx, const TravelWorld& world, TravelState& state);

    // Load the map's textures and start the water. False if anything vital is
    // missing from the pak.
    bool Load();

    // Give the chart's artwork back and forget it. The map object survives --
    // the voyage, the route and the camera are model, not pixels -- and Load()
    // brings the pictures back.
    //
    // The campaign does this around every mission, and on a 16 MB console it
    // is the difference between playing and an out-of-memory abort: the chart
    // is a megabyte of islands and sea that nobody can see while a battle is
    // being fought on top of it, and the battle wants three of its own.
    void Unload();

    // Block until something happens.
    Event Run();

    // One simulation slice: input, ship, encounters. Does not draw.
    Event Tick();

    void Draw(Surface& dst);

    const TravelWorld::Location* Arrived() const { return m_Arrived; }
    const TravelWorld::Encounter* Ambush() const { return m_Ambush; }
    const TravelWorld::Location* Selected() const { return m_Selected; }

    // --- model, exposed for the campaign driver and for tests ---------------

    void PlaceShip(Point p);
    Point Ship() const { return {m_ShipX >> 16, m_ShipY >> 16}; }
    Point Cursor() const { return {m_CursorX >> 16, m_CursorY >> 16}; }
    // Where the view is centred, for tests.
    int CameraX() const { return m_CamX >> 16; }
    int CameraY() const { return m_CamY >> 16; }
    void MoveCursor(Point p);
    bool Sailing() const { return m_Leg < int(m_Route.size()); }
    // The fold-out chart. It is not a key on this screen any more: the menu's
    // Map row raises it (0x100ba3a0's index 10 sets the same flag), and Draw()
    // shows it instead of the sea while it is up.
    void SetChartOpen(bool open) { m_ChartOpen = open; }
    bool ChartOpen() const { return m_ChartOpen; }
    const std::vector<Point>& Route() const { return m_Route; }

    // Send the ship to `p`, routing around land. False if there is no way.
    bool SailTo(Point p);
    // The sting a confirmed course plays, chosen so it is never the last one.
    void PlayMoveSting();
    // Push off after a mission that was declined or lost.
    //
    // The engine does this at the tail of 0x100dc2b8 whenever the result is
    // not a win and the place was a location rather than an encounter: it
    // takes eight compass offsets of thirty-two pixels (the table at
    // 0x1014b3d0, ±1 in each axis), starts at a random one of them, and sails
    // to the first that lands in open water inside the charted world.
    //
    // It is not decoration. Arriving is a latched proximity test at
    // twenty-four pixels (0x10094290), so a ship left sitting on the place it
    // just backed out of can never trigger it again; thirty-two pixels is far
    // enough to clear the latch, and the route back in re-opens the briefing.
    // Without it the player has to steer away and return by hand.
    bool PushOffFrom(Point place);
    // The route from `from` to `to`: the straight line if the sea is clear,
    // otherwise a walk of the world's sea lanes.
    bool PlotRoute(Point from, Point to, std::vector<Point>& out) const;

    bool OnLand(Point p) const;
    // Index of the island `p` stands on, or -1.
    int IslandUnder(Point p) const;
    // Nothing but water between a and b.
    bool ClearWater(Point a, Point b) const;
    // `p` dragged out to open water if it is on land, ten pixels clear.
    // `usedLane` receives the sea-lane node it was dragged toward, which is
    // the berth a route to it has to leave the network at.
    Point OffLand(Point p, int* usedLane = nullptr) const;

    // An open, unfinished location within `kReach` of `p`, or null.
    const TravelWorld::Location* LocationAt(Point p) const;
    bool IsOpen(int id) const;
    bool IsDone(int id) const;

    // Heading in engine units for a step, anticlockwise from north.
    static int HeadingOf(int dx, int dy);

    // Is `p` inside a charted quadrant (0x100ddbe0)? False outside the world.
    bool Charted(Point p) const;

    // The flocks as they fly, for tests.
    const std::vector<Flock>& Flocks() const { return m_Flocks; }

    // A seagull: position and velocity 16.16, heading in 1024-unit turns.
    struct Gull {
        int X = 0, Y = 0, Vx = 0, Vy = 0;
        int Heading = 0;
        int Flap = 0;      // 0x1000d208's counter; low four bits pick the frame
        int Frame = 0;
        bool Clockwise = false;   // the pool slot's parity
    };
    const Gull* Gulls() const { return m_Gulls; }

private:
    bool IslandHull(int index, std::vector<Point>& hull) const;
    // The bounding box of the charted quadrants, 16.16, as 0x100dbce0's loop
    // builds it before clamping the camera to it.
    void KnownBox(int& minx, int& miny, int& maxx, int& maxy) const;
    void BuildLanes();
    int NearestLane(Point p, bool needClear, bool wetOnly = false) const;
    void Steer();
    Event Sail();
    Event CheckArrival();
    Event RollEncounters(Point at);
    void DrawSea(Surface& dst, Point origin);
    void DrawSurf(Surface& dst, const Texture* t, Point pos, Point origin);
    void DrawFlocks(Surface& dst, Point origin);
    void StepFlocks();
    void StepGulls();
    void DrawGulls(Surface& dst, Point origin);
    void DrawChart(Surface& dst);
    const Texture* Tex(const std::string& path);

    GameContext& m_Ctx;
    const TravelWorld& m_World;
    TravelState& m_State;

    // What Load() took from the cache, given back by Unload().
    TextureSet m_Claims;

    SeaSurface m_Sea;
    // One per world island and one per location, in the world's order, null
    // where there is no art. Held rather than looked up per draw: an island is
    // blitted twice a frame, and a lookup by name would take a claim each time.
    std::vector<const Texture*> m_Islands;
    std::vector<const Texture*> m_LocIcons;
    const Texture* m_Haze = nullptr;
    const Texture* m_Ship = nullptr;
    const Texture* m_ShipRefl = nullptr;
    const Texture* m_RouteDot = nullptr;
    const Texture* m_Dest = nullptr;
    const Texture* m_Flag = nullptr;
    const Texture* m_Pointer = nullptr;
    const Texture* m_OverSea = nullptr;
    const Texture* m_OverLocation = nullptr;
    const Texture* m_Chart[4] = {nullptr, nullptr, nullptr, nullptr};
    const Texture* m_Marker = nullptr;
    const Texture* m_Gull = nullptr;
    const Texture* m_GullShadow = nullptr;

    // 16.16 throughout, like the engine.
    int m_ShipX = 0, m_ShipY = 0;
    int m_VelX = 0, m_VelY = 0;
    int m_CursorX = 0, m_CursorY = 0;
    int m_VcursorX = 0, m_VcursorY = 0;
    int m_CamX = 0, m_CamY = 0;
    int m_Heading = 0, m_WantHeading = 0;
    int m_Travelled = 0;    // 16.16 remainder of the distance counter
    int m_AnchorX = 0, m_AnchorY = 0;   // where the haze is pinned
    uint32_t m_Frame = 0;
    uint32_t m_Rng = 0x9e3779b9u;
    // Which of travel.dat's three tracks this visit to the chart drew, and
    // which confirm sting was heard last so the next one differs.
    int m_MusicTrack = 0;
    int m_MoveSting = -1;
    bool m_Follow = true;   // the pointer rides the ship until you move it
    bool m_ChartOpen = false;
    uint32_t m_LastTick = 0;

    std::vector<Point> m_Route;
    int m_Leg = 0;

    std::vector<Flock> m_Flocks;
    Gull m_Gulls[kGullCount];

    // The sea-lane graph, built once.
    std::vector<Point> m_LaneNodes;
    std::vector<std::vector<int>> m_LaneAdj;
    // Places the ship is currently standing at, so each opens once per
    // approach rather than every frame (the engine's own +0x41 latch).
    std::vector<int> m_Latched;

    const TravelWorld::Location* m_Arrived = nullptr;
    const TravelWorld::Encounter* m_Ambush = nullptr;
    const TravelWorld::Location* m_Selected = nullptr;
};

// Set a fresh game up: the ship at the player-start node, and whatever the
// world marks `<open>1</open>` reachable.
void ResetTravelState(const TravelWorld& world, TravelState& state);

// Mark `loc` finished and open whatever it unlocks: a location opens once
// every place its `<connections>` name has been finished.
void CompleteLocation(const TravelWorld& world, TravelState& state, int id);

}  // namespace bb
