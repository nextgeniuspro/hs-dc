// FightAnimation — the cutaway scene an attack plays, and the one big piece of
// the battle the port was still missing.
//
// When one unit attacks another the engine does not just subtract hit points on
// the map: LocalPlayer's event handler pushes a state of its own (the attack
// event, case 0xf of 0x100875f8, builds it through 0x1006f774 -> 0x1006bb4c)
// and the battlefield stops underneath it while the two sides shoot it out in
// side view. Then it pops and the map comes back with the losses already
// applied.
//
// **The gate.** 0x100875f8 case 0xf plays the scene only when all three of
// these hold, and otherwise falls through to the on-map projectile:
//
//   * the settings byte at resource 0x18 +0 is set -- that is the "Fight
//     animation on/off" row this port already has in Settings;
//   * the defender is not STATIONARY_COMBAT (the Cannon Tower). It has no
//     fight artwork at all -- `Data\Fight\gfx\unit\17-*.tc` does not exist --
//     which is the same statement made twice;
//   * a virtual on the current LocalPlayer state (its vtable +0x3c) says yes.
//     That one is not identified; the port uses "the fight is somewhere the
//     viewer can see", which is the test the capture board already applies and
//     covers the same ground -- a computer player trading blows behind the fog
//     does not stop the game to show a scene about nothing.
//
// **The stage.** 176x208, split down the middle, attacker on one side and
// defender on the other. Which side each takes is decided from the map: the
// attacker is on the LEFT when it is *above* the defender, or failing that
// when it is to its *left* (0x1006bb4c compares dy then dx). `distance` is the
// straight-line distance in squares, computed in quarters and rounded up, and
// it changes the staging: a fight at distance 1 shares one blended landscape,
// a fight at distance 2 or more is a hard split with a black line down the
// join and the shot flying between the halves.
//
// **The landscape** is one full-screen `Data\Fight\gfx\bg\<n>.tc` per side,
// chosen by what each unit is standing on (0x1006d2bc):
//
//     property  Docks(0)      7, or the docks overlay if the unit floats
//               Headquarters  6
//               Garrison      10
//               Shipyard(3)   8, or the shipyard overlay if the unit floats
//               Village       9
//     terrain   deep/shallow water        0  -- open sea
//               either "with mist"        11 -- sea, plus the mist layers
//               mountain(8)               2
//               beach(5)                  3
//               forest(7)                 4
//               road(11)/bridge(12)       5
//               anything else             1  -- plain
//
// Every one of those images carries its own **blend map in the alpha nibble**:
// opaque for the left ~90 columns, ramping to nothing across the middle. The
// RGB is real artwork the whole way across, which is what makes the composite
// work -- 0x1006f520 lays the left side's image over the right side's using
// that nibble as the weight, so the two terrains meet in a soft, dithered
// seam. Same terrain both sides and there is nothing to composite, so the
// image is used as it is (0x1006f10c's "Same terrain" path); both sides on
// bg 5 has its own picture, `bg\5-0.tc`.
//
// **The sea is a real ripple tank**, not a texture: when either side is water
// the scene builds the travel map's simulation over `Data\travel\gfx\water.tc`
// (0x1006f3f0 -- 64x64, radius 7, and refraction/shade shifts 6 and 4) and
// tiles it across the columns the composite left transparent. 0x1006f520
// records that span as it goes, which is why the class carries a min and max
// column rather than assuming half the screen.
//
// **The men.** Each side fields up to five, one per twenty hit points --
// `ceil(hp * 5 / 100)`, clamped (0x1006efc8) -- standing at five fixed
// positions and drawn back to front. Sprites come from
// `Data\Fight\gfx\unit\<type>-<n>.tc` with the animation script in
// `Data\Fight\gfx\unit\<type>.dat`, the same brace format as the battlefield's
// own unit files but with only `dir0` (this is a side view) and with a
// `death` section where the map's files have `sleep`. What `<n>` means depends
// on the unit (0x1006d51c):
//
//     kind 0  Swordsman, Scout          `<type>-<side>.tc`, and they *walk in*
//     kind 1  Sloop .. Cannon Tower     `<type>-<5 - men>.tc`: six damage
//                                       stages of one big ship, no mirror
//     kind 3  the rowing boats          `<type>-<side>.tc`
//     kind 2  everything else           `<type>-<side>.tc`, stands and shoots
//
// Only the attacking side moves. Kind 0 walks 38 pixels toward the middle and
// swings when it arrives; everyone else attacks from where they stand. A side
// that has already closed the distance does not close it again on the
// counter-attack -- 0x1006d71c keeps a count of walkers and logs "No need to
// walk...".
//
// **What comes out of the barrel** is picked per unit type by 0x1006d494:
//
//     style 0  small projectile, 4 shots (8 at range)   speed 4 / 8
//     style 1  big projectile,   2 shots (4 at range)   speed 2 / 4
//     style 2  big projectile plus a flame trail        the Scorch Cannon
//     style 3  a muzzle burst at the target             swords and firearms
//
// A projectile that lands, or a burst that finishes, is one hit; so is a
// walker's swing ending. When the attacker has no hits left in flight the
// defender loses however many men the damage cost, they run off the edge, and
// the scene either swaps sides for the counter-attack or ends.
//
// **The screen kicks when a shot lands** -- but only for a shot: 0x1006de80
// arms the shake on the first hit of a round and skips it entirely for attack
// style 3, so swords and firearms leave the picture alone and the three that
// throw something do not. Each style brings its own pair of numbers, amplitude
// and decay (0x1006c9a4 writes them into +0x160/+0x164): a musket ball is not
// a mortar. See ScreenShake.h; the panels are drawn *after* it, so they are
// the one thing on screen that stays still.
//
// **The panels** slide in from off-screen and decelerate (offset -88, speed
// 8.53, x29/32 a frame). Each side gets its commander's name plate
// (`FA_UI_name.tc`), a hit point plank (`FA_UI_left.tc`) whose number ticks
// down to the new value, and a defence plank (`FA_UI_right.tc`) carrying one
// `Data\icons\defense.tc` shield per point of cover.
//
// **It ends** when the exchange is over and the last particle has gone, or
// after 300 frames, or the moment the player presses a key (0x1006f8bc ->
// 0x10030368 just clears the alive flag).
//
// Not ported, and deliberately: the drifting `128.tc` haze over open water,
// the turn timer (Bluetooth games only), the perk plaque above each commander,
// and the commanders' battle chatter -- the scene picks a random voiceover
// line by seat, and the port has no voiceover bank.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/BattleRenderer.h"
#include "game/ScreenShake.h"
#include "game/SeaSurface.h"
#include "game/TextureCache.h"
#include "platform/Surface.h"

namespace bb {

class FilePack;
class Font;
class SoundManager;

// One unit type's fight script, from `Data\Fight\gfx\unit\<type>.dat`.
//
// The engine reads it with the same animation-control class the battlefield
// uses (0x100306d4, pointed at `Data\Fight\gfx\unit\` instead of
// `Data\Battle\unit\`), so the section names are the same four slots -- and
// `death` lands in the one the map's files call `sleep`, which is why the
// parser accepts either name for it. A missing section falls back to a
// two-frame clip (0x100309c0), so a Wagon with no `attack` still animates.
struct FightUnitScript {
    struct Clip {
        int Framerate = 5;
        std::vector<int> Frames;
        bool Empty() const { return Frames.empty(); }
    };
    Clip Idle, Walk, Attack, Death;
    // Where the shot leaves the sprite, relative to its centre, for a unit
    // standing on the left (0) and on the right (1) of the screen. The files
    // give these as `shootx0`/`shooty0`/`shootx1`/`shooty1` in sprite
    // coordinates; Load turns them into offsets from the centre.
    int ShootX[2] = {0, 0};
    int ShootY[2] = {0, 0};
    bool Valid = false;
};

class FightAnimation {
public:
    static constexpr int kMen = 5;              // men a side can field
    static constexpr int kMaxHitPoints = 100;   // what five men are worth

    // Where the five stand, in pixels, for a unit on the left; the right side
    // mirrors about the screen (0x1006d690, in 16.16).
    static constexpr int kManX[kMen] = {52, 38, 38, 52, 52};
    static constexpr int kManY[kMen] = {117, 93, 141, 69, 165};
    // Back to front, so the ones further up the screen are drawn first.
    static constexpr int kDrawOrder[kMen] = {3, 1, 0, 2, 4};
    // A big ship is not five men in a line; its sprite is nudged toward its
    // own edge instead (0x1006d864).
    static constexpr int kShipNudge = 8;
    // How far a walker closes before it swings (0x1006d71c).
    static constexpr int kWalkIn = 38;
    // Where the dead walk off to.
    static constexpr int kExitLeft = -32, kExitRight = 208;
    // Frames a man takes to fall (0x1006d864 / 0x1006dd58 both write 0x10).
    static constexpr int kDeathFrames = 16;

    // The panels' slide-in, 16.16 (0x1006c1d4 / 0x1006c1d8 and 0x1006e510).
    static constexpr int kSlideStart = -(88 << 16);
    static constexpr int kSlideSpeed = 0x88800;
    static constexpr int kSlideDecayNum = 29, kSlideDecayDen = 32;

    // The mist layers: three copies of each, 104 apart, scrolling half a pixel
    // a frame and wrapping (0x1006e960).
    static constexpr int kMistStep = 104;
    static constexpr int kMistScroll = 0x8000;
    static constexpr int kMistNearLeftX = -50, kMistNearRightX = 96;
    static constexpr int kMistFarLeftX = -36, kMistFarRightX = 82;

    // The whole thing is over after this many frames whatever else is
    // happening, and lingers this long after the last hit point ticks.
    static constexpr int kMaxFrames = 300;
    static constexpr int kLinger = 10;

    // The sea, as 0x1006f3f0 builds it: the travel map's tank over the travel
    // map's water, with the refraction and shade shifts overridden to 6 and 4.
    static constexpr const char* kWaterPath = "Data\\travel\\gfx\\water.tc";
    static constexpr int kWaterGridShift = 6;
    static constexpr int kWaterDropRadius = 7;
    static constexpr int kWaterDropStrength = 5;
    static constexpr int kWaterDropInterval = 16;
    static constexpr int kWaterRefractShift = 6;
    static constexpr int kWaterShadeShift = 4;

    // The bg index 0x1006d2bc gives sea, and the one it gives mist -- which is
    // not a file but a marker the constructor turns into sea plus two mist
    // layers.
    static constexpr int kBgSea = 0;
    static constexpr int kBgMist = 11;

    // Which side of the screen is water: none, the left, the right, or both.
    enum class Sea { kNone, kLeft, kRight, kBoth };

    // What the caller knows about one of the two combatants.
    struct Combatant {
        int Type = 0;            // UnitType
        int Colour = 1;          // owner, for the palette swap
        int Terrain = 0;         // NdLevel terrain id under the unit
        int Property = -1;       // PropertyType there, or -1
        int HPBefore = 0;
        int HPAfter = 0;
        int Shields = 0;         // cover, one defence icon each
        std::string Commander;   // the name on the plate
        // Which army this side's men shout for (SoundManager::Nation), or -1
        // for a seat with no commander -- and then they fight in silence.
        int Nationality = -1;
    };

    struct Params {
        Combatant Attacker, Defender;
        int AttackerX = 0, AttackerY = 0;   // map squares, for the staging
        int DefenderX = 0, DefenderY = 0;
        bool Countered = false;               // is there a second round
    };

    // Load what every fight needs: the panels, the shields, the particles and
    // the sea. The per-fight artwork -- backgrounds and unit sheets -- is
    // loaded and given back by Begin/End, because a battle cannot afford to
    // hold twenty-one unit sheets at once.
    bool Load(TextureCache& cache, FilePack& pack, const Font& font);
    // Where the noises come from. Null runs the scene silent; the caller owns
    // it, and is responsible for having the battle bank and the unit sound
    // table loaded.
    void SetSound(SoundManager* sound) { m_Sound = sound; }
    bool Ready() const { return m_Ready; }

    // Stage a fight. `renderer` supplies the owner palettes.
    void Begin(const Params& p, const BattleRenderer& renderer);
    bool Active() const { return m_Active; }

    // Draw one frame and advance; false once the scene is over.
    bool Step(Surface& dst);
    // A key press ends it there and then (0x10030368).
    void Skip() { m_Alive = false; }

    // How fast the scene's background music is taken off when it ends:
    // 0x1006ced4's ramp step, in volume units per decoded block.
    static constexpr int kSceneFadeStep = 0x20;

    // Which of the two sides the attacker took, for the tests.
    int AttackerSide() const { return m_AttackerSide; }
    int DefenderSide() const { return m_DefenderSide; }
    Sea Water() const { return m_SeaSide; }
    int Distance() const { return m_Distance; }
    int Background(int side) const { return m_Side[side].Bg; }
    int MenAt(int side) const { return m_Side[side].Men; }

    // The staging rules, exposed because they are the part worth testing
    // without a screen.
    //
    // How many men `hp` is worth (0x1006efc8).
    static int MenFor(int hp);
    // The sprite kind (0x1006d51c) and the projectile style (0x1006d494).
    static int SpriteKind(int unitType);
    static int AttackStyle(int unitType);
    // The background index for one side (0x1006d2bc); `floats` is whether the
    // unit is a sea unit, which is what decides between a dockside fight and
    // a fight in front of the docks.
    static int BackgroundFor(int terrain, int property, bool floats);
    // Does this attack get a scene at all? The gate at the top of
    // FightAnimation.h, in one place: the player's setting, then the two
    // fights the engine will not stage.
    //
    // `visible` is the port's stand-in for the unidentified virtual -- the
    // fight has to be somewhere the viewer can see, or there is nobody to
    // show it to.
    static bool Wanted(bool settingOn, int attackerClass, int defenderClass,
                       bool visible);

    // Is the attacker on the left (0x1006bb4c)?
    static bool AttackerOnLeft(int ax, int ay, int dx, int dy);
    // Squares between them, rounded up the way the constructor does it.
    static int DistanceBetween(int ax, int ay, int dx, int dy);

private:
    // One man, or one ship. Mirrors the 0x24-byte record 0x1006d864 builds.
    struct Man {
        enum class State { kWalk, kAttack, kIdle, kDying };
        State AnimState = State::kIdle;
        int X = 0, Y = 0;        // 16.16
        int TargetX = 0;        // where a walk is heading
        int DriftY = 0;         // 16.16 per frame, the wobble
        int SpeedX = 0;         // 16.16 per frame
        int Timer = 0;           // attack ticks left, or frames left to fall
        int Frame = 0;           // sheet frame
        int Step = 0;            // position in the clip
        int Tick = 0;            // sub-steps toward the next clip frame
    };

    struct Side {
        int Type = 0;
        int Colour = 1;
        int Kind = 2;            // SpriteKind
        int Style = 3;           // AttackStyle
        int Bg = 1;
        int Men = 0;             // now
        int MenAfter = 0;       // once this round's damage lands
        int Pending = 0;         // hits still owed, or men still falling
        int HPShown = 0;        // the number on the plank, 16.16-ish
        int HPTarget = 0;
        int HPFinal = 0;
        int Shields = 0;
        std::string Commander;
        const Texture* Sheet = nullptr;      // the men, or the ship
        const Texture* SheetHurt = nullptr; // a ship's next damage stage
        FightUnitScript Script;
        // The muzzle, as an offset from the sprite's centre: the script gives
        // it in sprite coordinates and 0x1006da84 subtracts half the frame.
        int ShootDx = 0, ShootDy = 0;
        // Standing in the sea, so the sprite is cut off at the waterline.
        bool Wading = false;
        Man Squad[kMen];
    };

    // --- setup ---
    bool LoadScript(int type, FightUnitScript& out);
    const Texture* LoadSheet(int side, int type, int damage);
    void BuildBackground();
    void StampBg(const Texture* t, int x, int y);
    void PlaceMen(int side);
    void BeginSide(int side);
    void StartRound();
    void EndRound();          // the counter-attack, or the end
    void SetStyle(int style);

    // --- per frame ---
    static int ClipFrame(const FightUnitScript::Clip& c, int step);
    static const FightUnitScript::Clip& ClipFor(const Side& sd, Man::State st);
    void StepMan(int side, int i);
    void SetManState(int side, int i, Man::State state);
    void SpawnShot(int x0, int y0, int x1, int y1);
    void RegisterHit(int x16, int y16);
    void SettleRound();
    void Explode(int x16, int y16);
    void Blit(Surface& dst, const Texture* t, int frame, int x, int y,
              const Surface::Rect* clip = nullptr);
    void DrawShoved(Surface& dst, const Texture* t, int x, int y);
    void DrawSide(Surface& dst, int side);
    void DrawPanels(Surface& dst);
    void DrawParticles(Surface& dst);
    void StepParticles();
    uint32_t Random();

    // A projectile, a muzzle burst, a cloud or a flame. The engine keeps an
    // emitter per kind over one pool; the port keeps one list with a kind on
    // it, because the differences are two lines each.
    struct Particle {
        enum class ParticleKind { kShot, kBurst, kCloud, kFlame };
        ParticleKind Kind = ParticleKind::kShot;
        int X = 0, Y = 0;        // 16.16
        int Vx = 0, Vy = 0;
        int Arc = 0;             // 16.16 lift above the line, for a shot
        int GroundY = 0;        // where its shadow sits
        int Life = 0;
        int Life0 = 0;           // what it started with, for the arc
        int Frame = 0;
        int TargetX = 0;        // where a burst says the hit landed
        int Half = 0;            // which half of the screen it is drawn in
        bool Hits = false;       // does its end count as a hit
        bool Trailing = false;   // the Scorch Cannon's flame
    };

    std::optional<TextureSet> m_Base;   // what every fight uses
    std::optional<TextureSet> m_Fight;  // what this fight uses

    TextureCache* m_Cache = nullptr;
    FilePack* m_Pack = nullptr;
    const Font* m_Font = nullptr;
    SoundManager* m_Sound = nullptr;
    const BattleRenderer* m_Renderer = nullptr;
    bool m_Ready = false;

    // The panels and the parts.
    const Texture* m_UIName = nullptr;
    const Texture* m_UILeft = nullptr;
    const Texture* m_UIRight = nullptr;
    const Texture* m_Shield = nullptr;
    const Texture* m_SmallShot = nullptr;
    const Texture* m_BigShot = nullptr;
    const Texture* m_Burst[2] = {nullptr, nullptr};   // left, right
    const Texture* m_Cloud = nullptr;
    const Texture* m_Flame = nullptr;

    // This fight's own art.
    const Texture* m_MistNear = nullptr;
    const Texture* m_MistFar = nullptr;
    int m_MistNearX = 0, m_MistFarX = 0;
    // Mist on both sides is two *near* layers, one from each shore, so they
    // scroll together instead of at a quarter speed.
    bool m_MistBothNear = false;
    const Texture* m_BeachWater = nullptr;   // the animated strip
    const Texture* m_BeachSurf = nullptr;    // the one the sea shoves
    int m_BeachWaterX = 0, m_BeachSurfX = 0;
    const Texture* m_Quay = nullptr;          // docks or shipyard, one side
    int m_QuayX = 0;

    SeaSurface m_Sea;
    bool m_SeaReady = false;

    std::vector<uint16_t> m_Bg;     // 176x208, the two landscapes composited
    // Which of those pixels the landscape actually covers. The engine leaves
    // this in the alpha nibble and draws the picture through it; the port
    // keeps it beside the pixels because "covered" and "opaque" are the same
    // question here and a byte is cheaper to test than a shift.
    std::vector<uint8_t> m_BgCovered;
    int m_SeaColMin = 0, m_SeaColMax = Surface::kWidth - 1;

    Side m_Side[2];
    int m_AttackerSide = 0, m_DefenderSide = 1;
    int m_Distance = 1;
    Sea m_SeaSide = Sea::kNone;
    bool m_Countered = false;
    int m_Walkers = 0;              // 0x1006bb4c's +0xf8

    // The kick the current style makes, as 0x1006c9a4 sets it up
    // (this+0x160 is the decay and +0x164 the starting energy). Style 3 --
    // swords and firearms -- never arms it.
    ScreenShake m_Shake;
    int m_ShakeDecay = 0;
    int m_ShakeEnergy = 0;
    bool m_Shaking = false;         // 0x1006bb4c's +0x15c

    // The projectile style's spread and speed (0x1006c9a4): how far the shot
    // has to fly past the defender, and how fast.
    int m_Spread = 0;
    int m_ShotSpeed = 4;
    int m_Style = 3;
    // The half of the screen shots are drawn in while they cross, and the
    // pixel column at which the scene jumps to the other half.
    int m_ShotHalf = 0;
    int m_ShotJumpX = 0;
    bool m_ShotJumped = false;

    std::vector<Particle> m_Parts;
    // Puffs a burning shell leaves behind, held back until the walk over
    // `m_Parts` is finished rather than appended into the container being
    // iterated.
    std::vector<Particle> m_Trail;
    int m_Slide = kSlideStart;
    int m_SlideSpeed = kSlideSpeed;
    bool m_Started = false;         // has the slide-in finished
    int m_MistScroll = 0;
    int m_Frame = 0;
    int m_Linger = 0;
    bool m_Active = false;
    bool m_Alive = false;
    bool m_Finished = false;        // the exchange is done; wait for the dust
    bool m_HitSounded = false;     // one hit noise a round (0x1006bb4c +0x141)
    bool m_AttackSounded = false;  // and one report for the volley
    uint32_t m_Rng = 0x2545f491u;
};

}  // namespace bb
