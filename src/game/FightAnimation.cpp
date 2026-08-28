#include "game/FightAnimation.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "game/BattleData.h"
#include "game/BattleField.h"
#include "game/ConfigFile.h"
#include "game/FilePack.hpp"
#include "game/Font.h"
#include "game/NdLevel.h"
#include "game/SoundManager.h"
#include "game/TcTexture.h"

namespace bb {
namespace {

constexpr int kW = Surface::kWidth;
constexpr int kH = Surface::kHeight;

constexpr const char* kUiNamePath = "Data\\Fight\\gfx\\FA_UI_name.tc";
constexpr const char* kUiLeftPath = "Data\\Fight\\gfx\\FA_UI_left.tc";
constexpr const char* kUiRightPath = "Data\\Fight\\gfx\\FA_UI_right.tc";
constexpr const char* kShieldPath = "Data\\icons\\defense.tc";
constexpr const char* kSmallShotPath =
    "Data\\Fight\\gfx\\part\\smallprojectile.tc";
constexpr const char* kBigShotPath = "Data\\Fight\\gfx\\part\\bigprojectile.tc";
constexpr const char* kBurstLeftPath = "Data\\Fight\\gfx\\part\\burst-left.tc";
constexpr const char* kBurstRightPath = "Data\\Fight\\gfx\\part\\burst-right.tc";
constexpr const char* kCloudPath = "Data\\Battle\\gfx\\part\\fade-cloud.tc";
constexpr const char* kFlamePath = "Data\\Battle\\gfx\\part\\boost-flame.tc";

// The panel layout at rest, from 0x1006e510. `slide` is the offset every one
// of them shares, in pixels, and it walks from -88 to zero.
constexpr int kPlateY = 12;          // the two planks under the name
constexpr int kNameLeftX = 2, kNameRightX = 108;
constexpr int kHpLeftX = 22, kHpRightX = 162, kHpY = 10;
constexpr int kShieldPlateLeftX = 36, kShieldPlateRightX = 108;
constexpr int kHpPlateLeftX = 2, kHpPlateRightX = 142;
// Where the shields sit on their plank: centred, six apart, four rows proud.
constexpr int kShieldStep = 6, kShieldBias = 9, kShieldY = -4;

// The beach pieces, when exactly one side is at sea and they are close enough
// to share a shore (0x1006bb4c's tail). Each is 104 tall and drawn twice.
constexpr int kBeachStep = 104;

// The engine's own integer square root (0x1008c170) is only used here to turn
// a map offset into a distance in squares, so a plain one will do.
int IntSqrt(int v) {
    if (v <= 0) return 0;
    int r = 0, bit = 1 << 30;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

// --- the staging rules -----------------------------------------------------

// 0x1006efc8: `ceil(hp * 5 / maxHp)`, clamped to the five slots.
int FightAnimation::MenFor(int hp) {
    if (hp <= 0) return 0;
    const int n = (hp * kMen + kMaxHitPoints - 1) / kMaxHitPoints;
    return n > kMen ? kMen : n;
}

// 0x1006d51c.
int FightAnimation::SpriteKind(int unitType) {
    switch (unitType) {
        case kUnitSwordsman:
        case kUnitScout:
            return 0;
        case kUnitRowingBoat:
        case kUnitRowingBoatSwordsman:
        case kUnitRowingBoatPistoleer:
        case kUnitRowingBoatMusketeer:
            return 3;
        case kUnitSloop:
        case kUnitTransportHeavy:
        case kUnitGalley:
        case kUnitHidsu:
        case kUnitMothership:
        case kUnitManOWar:
        case kUnitCannonTower:
            return 1;
        default:
            return 2;
    }
}

// 0x1006d494.
int FightAnimation::AttackStyle(int unitType) {
    switch (unitType) {
        case kUnitSwordsman:
        case kUnitPistoleer:
        case kUnitMusketeer:
        case kUnitScout:
        case kUnitCavalryLight:
        case kUnitCavalryHeavy:
            return 3;
        case kUnitMortar:
        case kUnitGalley:
        case kUnitHidsu:
        case kUnitCannonTower:
            return 1;
        case kUnitScorchCannon:
            return 2;
        default:
            return 0;
    }
}

// 0x1006d2bc. A building wins over the ground it stands on, and the two
// waterside ones ask whether the unit floats: a ship fights *at* the quay, in
// front of the dock artwork over open water, and a landsman fights on it.
int FightAnimation::BackgroundFor(int terrain, int property, bool floats) {
    switch (property) {
        case kPropDocks: return floats ? kBgSea : 7;
        case kPropShipyard: return floats ? kBgSea : 8;
        case kPropHeadquarters: return 6;
        case kPropGarrison: return 10;
        case kPropVillage: return 9;
        default: break;
    }
    switch (terrain) {
        case NdLevel::kDeepWater:
        case NdLevel::kShallowWater:
            return kBgSea;
        case NdLevel::kDeepWaterWithMist:
        case NdLevel::kShallowWaterWithMist:
            return kBgMist;
        case NdLevel::kMountain: return 2;
        case NdLevel::kBeach: return 3;
        case NdLevel::kForest: return 4;
        case NdLevel::kRoad:
        case NdLevel::kBridge:
            return 5;
        default: return 1;
    }
}

// 0x100875f8's case 0xf, as far as it is known.
bool FightAnimation::Wanted(bool settingOn, int attackerClass,
                            int defenderClass, bool visible) {
    if (!settingOn) return false;
    // A Cannon Tower has no fight artwork at all -- there is no
    // `Data\Fight\gfx\unit\17-*.tc` -- which is the same thing the engine says
    // by refusing the scene when the defender is STATIONARY_COMBAT. The port
    // asks about the attacker too, because a tower shooting first would have
    // nothing to draw either.
    if (attackerClass == kClassStationary || defenderClass == kClassStationary)
        return false;
    return visible;
}

// 0x1006bb4c: the attacker takes the left when it is *above* the defender,
// and failing that when it is to its left. Everything else puts it on the
// right, which is why a fight looks the way the map does.
bool FightAnimation::AttackerOnLeft(int ax, int ay, int dx, int dy) {
    if (ay - dy < 0) return true;
    return ax - dx < 0;
}

int FightAnimation::DistanceBetween(int ax, int ay, int dx, int dy) {
    const int x = (ax - dx) * 4, y = (ay - dy) * 4;
    return (IntSqrt(x * x + y * y) + 3) >> 2;
}

// --- loading ---------------------------------------------------------------

bool FightAnimation::Load(TextureCache& cache, FilePack& pack,
                          const Font& font) {
    m_Cache = &cache;
    m_Pack = &pack;
    m_Font = &font;
    m_Base.emplace(cache);
    TextureSet& t = *m_Base;
    m_UIName = t.Load(kUiNamePath);
    m_UILeft = t.Load(kUiLeftPath);
    m_UIRight = t.Load(kUiRightPath);
    m_Shield = t.Load(kShieldPath);
    m_SmallShot = t.Load(kSmallShotPath);
    m_BigShot = t.Load(kBigShotPath);
    m_Burst[0] = t.Load(kBurstLeftPath);
    m_Burst[1] = t.Load(kBurstRightPath);
    m_Cloud = t.Load(kCloudPath);
    m_Flame = t.Load(kFlamePath);
    if (const Texture* w = t.Load(kWaterPath)) {
        m_SeaReady = m_Sea.Init(w, kWaterGridShift, kWaterDropRadius,
                               kWaterDropStrength, kWaterDropInterval);
        if (m_SeaReady) m_Sea.SetShifts(kWaterRefractShift, kWaterShadeShift);
    }
    m_Ready = m_UIName != nullptr && m_UILeft != nullptr && m_UIRight != nullptr;
    return m_Ready;
}

// `Data\Fight\gfx\unit\<type>.dat`, the same brace format the battlefield's
// unit scripts use. Only `dir0` is ever present -- the scene is a side view,
// so there is one direction -- and the fourth clip is spelled `death` here and
// `sleep` on the map; 0x100306d4 accepts either.
bool FightAnimation::LoadScript(int type, FightUnitScript& out) {
    out = FightUnitScript{};
    if (!m_Pack) return false;
    char path[64];
    std::snprintf(path, sizeof(path), "Data\\Fight\\gfx\\unit\\%d.dat", type);
    auto stream = m_Pack->Open(path);
    if (!stream) return false;
    const auto& bytes = stream->Data();
    const std::string text(bytes.begin(), bytes.end());

    // The shoot offsets sit above the first section, where ConfigFile cannot
    // see them -- it only keeps entries inside one.
    const auto scalar = [&text](const char* key, int fallback) {
        const std::size_t at = text.find(key);
        if (at == std::string::npos) return fallback;
        return std::atoi(text.c_str() + at + std::char_traits<char>::length(key));
    };
    // `shootx-1` and friends also appear in the ship files (one pair per
    // damage stage) and the engine reads none of them, so neither does this:
    // searching for "shootx0" cannot match "shootx-1".
    out.ShootX[0] = scalar("shootx0", 0);
    out.ShootY[0] = scalar("shooty0", 0);
    out.ShootX[1] = scalar("shootx1", 0);
    out.ShootY[1] = scalar("shooty1", 0);

    ConfigFile cfg;
    cfg.Parse(text);
    for (const auto& section : cfg.Sections()) {
        FightUnitScript::Clip* clip = nullptr;
        if (section.Name == "attack") clip = &out.Attack;
        else if (section.Name == "walk") clip = &out.Walk;
        else if (section.Name == "idle") clip = &out.Idle;
        else if (section.Name == "death" || section.Name == "sleep")
            clip = &out.Death;
        if (!clip) continue;
        clip->Framerate = std::max(1, section.GetInt("framerate", 5));
        for (const auto& e : section.Entries) {
            if (e.Key != "dir0") continue;
            const char* p = e.Value.c_str();
            while (*p) {
                while (*p == ' ' || *p == '\t') ++p;
                if (!*p) break;
                clip->Frames.push_back(std::atoi(p));
                while (*p && *p != ' ' && *p != '\t') ++p;
            }
        }
    }
    // 0x100309c0 gives a missing section a two-frame stand-in rather than
    // leaving a hole, so a Wagon with no `attack` still has something to play.
    for (FightUnitScript::Clip* c :
         {&out.Idle, &out.Walk, &out.Attack, &out.Death})
        if (c->Empty()) c->Frames.assign({0, 1});
    out.Valid = true;
    return true;
}

const Texture* FightAnimation::LoadSheet(int side, int type, int damage) {
    // 0x1006d9a0: a big ship's sheet is its *damage stage* and never mirrors;
    // everyone else has a left-facing and a right-facing sheet.
    const int n = m_Side[side].Kind == 1 ? Clamp(damage, 0, 5) : side;
    char path[64];
    std::snprintf(path, sizeof(path), "Data\\Fight\\gfx\\unit\\%d-%d.tc", type,
                  n);
    return m_Fight->LoadIndexed(path);
}

// --- staging ---------------------------------------------------------------

void FightAnimation::Begin(const Params& p, const BattleRenderer& renderer) {
    if (!m_Ready) return;
    m_Renderer = &renderer;
    m_Fight.emplace(*m_Cache);

    m_AttackerSide = AttackerOnLeft(p.AttackerX, p.AttackerY, p.DefenderX,
                                    p.DefenderY)
                         ? 0
                         : 1;
    m_DefenderSide = 1 - m_AttackerSide;
    m_Distance = DistanceBetween(p.AttackerX, p.AttackerY, p.DefenderX,
                                p.DefenderY);
    m_Countered = p.Countered;
    m_Walkers = 0;
    m_Parts.clear();
    m_Slide = kSlideStart;
    m_SlideSpeed = kSlideSpeed;
    m_Started = false;
    m_Finished = false;
    m_MistScroll = 0;
    m_Frame = 0;
    m_Linger = 0;
    m_HitSounded = false;
    m_Shaking = false;
    m_ShotJumped = false;
    m_MistNear = m_MistFar = nullptr;
    m_BeachWater = m_BeachSurf = m_Quay = nullptr;
    m_QuayX = 0;

    const Combatant* who[2];
    who[m_AttackerSide] = &p.Attacker;
    who[m_DefenderSide] = &p.Defender;

    // The defender's background is worked out first and the attacker's
    // second, which is what decides whose quay is on screen when both are at
    // one: 0x1006d2bc frees the last overlay it made before making another.
    for (int pass = 0; pass < 2; ++pass) {
        const int s = pass == 0 ? m_DefenderSide : m_AttackerSide;
        const Combatant& c = *who[s];
        Side& sd = m_Side[s];
        sd = Side{};
        sd.Type = c.Type;
        sd.Colour = c.Colour;
        sd.Kind = SpriteKind(c.Type);
        sd.Style = AttackStyle(c.Type);
        sd.Shields = c.Shields;
        sd.Commander = c.Commander;
        sd.Men = MenFor(c.HPBefore);
        sd.MenAfter = MenFor(c.HPAfter);
        sd.HPShown = c.HPBefore;
        sd.HPTarget = c.HPBefore;
        sd.HPFinal = std::max(0, c.HPAfter);
        sd.Pending = 0;
        const bool floats = BattleField::IsSeaUnit(c.Type);
        sd.Bg = BackgroundFor(c.Terrain, c.Property, floats);
        if (sd.Bg == kBgSea &&
            (c.Property == kPropDocks || c.Property == kPropShipyard)) {
            const char* left = c.Property == kPropDocks
                                   ? "Data\\Fight\\gfx\\docks-left.tc"
                                   : "Data\\Fight\\gfx\\shipyard-left.tc";
            const char* right = c.Property == kPropDocks
                                    ? "Data\\Fight\\gfx\\docks-right.tc"
                                    : "Data\\Fight\\gfx\\shipyard-right.tc";
            m_Quay = m_Fight->Load(s == 0 ? left : right);
            m_QuayX = 0;
            if (s != 0 && m_Quay && m_Quay->Valid())
                m_QuayX = kW - m_Quay->Frame(0)->Width;
        }
    }

    // Mist is bg 11, which is not a picture: it means "open sea, with two
    // layers of fog over it" (0x1006bb4c). Both sides misty and only the near
    // layers are used, one from each side.
    if (m_Side[0].Bg == kBgMist || m_Side[1].Bg == kBgMist) {
        if (m_Side[0].Bg == kBgMist && m_Side[1].Bg == kBgMist) {
            m_MistNear = m_Fight->Load("Data\\Fight\\gfx\\mist-right.tc");
            m_MistFar = m_Fight->Load("Data\\Fight\\gfx\\mist-left.tc");
            m_MistNearX = kMistNearRightX;
            m_MistFarX = kMistNearLeftX;
            // Both layers are near ones here, so both scroll the same way.
            m_MistBothNear = true;
            m_Side[0].Bg = kBgSea;
            m_Side[1].Bg = kBgSea;
        } else if (m_Side[0].Bg == kBgMist) {
            m_MistNear = m_Fight->Load("Data\\Fight\\gfx\\mist-left.tc");
            m_MistFar = m_Fight->Load("Data\\Fight\\gfx\\mist-left2.tc");
            m_MistNearX = kMistNearLeftX;
            m_MistFarX = kMistFarLeftX;
            m_Side[0].Bg = kBgSea;
        } else {
            m_MistNear = m_Fight->Load("Data\\Fight\\gfx\\mist-right.tc");
            m_MistFar = m_Fight->Load("Data\\Fight\\gfx\\mist-right2.tc");
            m_MistNearX = kMistNearRightX;
            m_MistFarX = kMistFarRightX;
            m_Side[1].Bg = kBgSea;
        }
    }

    const bool leftSea = m_Side[0].Bg == kBgSea;
    const bool rightSea = m_Side[1].Bg == kBgSea;
    m_SeaSide = leftSea && rightSea ? Sea::kBoth
                : leftSea            ? Sea::kLeft
                : rightSea           ? Sea::kRight
                                      : Sea::kNone;

    BuildBackground();

    // The shore, when one side is at sea and they are close enough to be
    // standing on the same beach.
    if (m_Distance < 2 && (m_SeaSide == Sea::kLeft || m_SeaSide == Sea::kRight)) {
        const bool left = m_SeaSide == Sea::kLeft;
        m_BeachWater = m_Fight->Load(left
                                        ? "Data\\Fight\\gfx\\bg\\beach-water-left.tc"
                                        : "Data\\Fight\\gfx\\bg\\beach-water-right.tc");
        m_BeachWaterX = left ? 26 : 90;
        m_BeachSurf = m_Fight->Load(left ? "Data\\Fight\\gfx\\bg\\beach-left-w.tc"
                                        : "Data\\Fight\\gfx\\bg\\beach-right-w.tc");
        m_BeachSurfX = left ? 63 : 85;
        // The far bank is not animated at all, so it is stamped into the
        // background once instead of being drawn every frame.
        if (const Texture* bank = m_Fight->Load(
                left ? "Data\\Fight\\gfx\\bg\\beach-right.tc"
                     : "Data\\Fight\\gfx\\bg\\beach-left.tc")) {
            const int bx = left ? 87 : 61;
            for (int y = 0; y < kH; y += kBeachStep) StampBg(bank, bx, y);
        }
    }

    for (int s = 0; s < 2; ++s) {
        Side& sd = m_Side[s];
        LoadScript(sd.Type, sd.Script);
        sd.Sheet = LoadSheet(s, sd.Type, kMen - sd.Men);
        if (sd.Kind == 1) sd.SheetHurt = LoadSheet(s, sd.Type, kMen - sd.MenAfter);
        // Men standing in the sea are drawn only down to row 44 of their
        // sixty: the water takes their legs (0x1006d9a0 sets the sprite's own
        // cut at 0x2c for a side that is at sea, ships and boats excepted).
        sd.Wading = (m_SeaSide == Sea::kBoth ||
                     (m_SeaSide == Sea::kLeft && s == 0) ||
                     (m_SeaSide == Sea::kRight && s == 1)) &&
                    sd.Kind != 1 && sd.Kind != 3;
        PlaceMen(s);
    }

    SetStyle(m_Side[m_AttackerSide].Style);
    m_Side[m_AttackerSide].Pending = m_Side[m_AttackerSide].Men;
    m_Active = true;
    m_Alive = true;

    // The two sounds the scene opens with, both from the tail of 0x1006bb4c.
    if (m_Sound) {
        // The bed: misc[16], `generic_battle_good1.spc`, which misc.dat labels
        // "FIGHT ANIM BG". It runs under the whole cutaway and 0x1006ced4
        // fades it out when the scene comes down. This is the music the port
        // was missing in the battle scene -- the fight had its hits but no
        // score.
        m_Sound->PlayBattle(SoundManager::kSoundFightScene);

        // And the attacker's men shout. Which line depends on who they are
        // shouting at: a fight between two commanders of the same nationality
        // draws from the nine generic calls, and one across nationalities
        // rolls twelve and takes the rival-specific block for the top three.
        // That is the arithmetic at the end of 0x1006bb4c, and it is why each
        // nation's bank carries three lines for each of the other four.
        const int atk = p.Attacker.Nationality;
        const int def = p.Defender.Nationality;
        if (atk >= 0) {
            int line;
            if (atk == def || def < 0) {
                line = int(Random() % SoundManager::kVoiceGenericAttackCount);
            } else {
                const int roll = int(Random() % 12u);
                line = roll < SoundManager::kVoiceGenericAttackCount
                           ? roll
                           : SoundManager::AttackVoice(atk, def, roll - 9);
            }
            if (line >= 0) m_Sound->PlayVoice(atk, line);
        }
    }
}

// 0x1006f10c and 0x1006f520. Every `bg\<n>.tc` is a full screen of artwork
// whose *alpha nibble* is a blend map rather than transparency: opaque for the
// left ninety columns or so, fading to nothing across the middle, with real
// terrain colour underneath the whole way. Laying one over the other through
// that map is what makes two landscapes meet in a dithered seam.
void FightAnimation::BuildBackground() {
    m_Bg.assign(std::size_t(kW) * kH, 0);
    m_BgCovered.assign(std::size_t(kW) * kH, 0);
    m_SeaColMin = 0;
    m_SeaColMax = kW - 1;
    if (m_SeaSide == Sea::kBoth) return;   // nothing but sea, no picture at all

    const auto loadBg = [&](int n) {
        char path[64];
        std::snprintf(path, sizeof(path), "Data\\Fight\\gfx\\bg\\%d.tc", n);
        return m_Fight->Load(path);
    };
    const auto pixels = [](const Texture* t) -> const uint16_t* {
        if (!t || !t->Valid()) return nullptr;
        const TcTexture::Image* img = t->Frame(0);
        if (!img || img->Width != kW || img->Height != kH) return nullptr;
        return img->Pixels.data();
    };

    const Texture* base = nullptr;
    const Texture* over = nullptr;
    bool invert = false;
    bool hard = false;
    bool opaque = false;   // is every pixel of the result covered

    if (m_SeaSide == Sea::kNone) {
        opaque = true;
        if (m_Side[0].Bg == m_Side[1].Bg) {
            // One landscape, used as it stands. Road against road has its own
            // picture rather than the plain one.
            base = m_Side[0].Bg == 5
                       ? m_Fight->Load("Data\\fight\\gfx\\bg\\5-0.tc")
                       : loadBg(m_Side[1].Bg);
            if (const uint16_t* src = pixels(base)) {
                for (std::size_t i = 0; i < m_Bg.size(); ++i) {
                    m_Bg[i] = uint16_t(0xF000u | (src[i] & 0x0FFFu));
                    m_BgCovered[i] = 1;
                }
            }
            return;
        }
        base = loadBg(m_Side[1].Bg);
        over = loadBg(m_Side[0].Bg);
        hard = m_Distance >= 2;
    } else {
        // One side is sea: the land goes over an empty screen, and the sea
        // shows through wherever the map left nothing. The split is always the
        // hard one here -- 0x1006f10c hands the compositor `distance + 1`.
        const int land = m_SeaSide == Sea::kLeft ? m_Side[1].Bg : m_Side[0].Bg;
        over = loadBg(land);
        invert = m_SeaSide == Sea::kLeft;
        hard = true;
    }

    const uint16_t* src = pixels(over);
    const uint16_t* dst = pixels(base);
    if (dst)
        for (std::size_t i = 0; i < m_Bg.size(); ++i) m_Bg[i] = dst[i];
    if (!src) {
        // No overlay to composite; keep whatever the base gave us.
        for (std::size_t i = 0; i < m_Bg.size(); ++i)
            if (opaque) {
                m_Bg[i] = uint16_t(0xF000u | (m_Bg[i] & 0x0FFFu));
                m_BgCovered[i] = 1;
            }
        return;
    }

    int minX = kW - 1, maxX = 0;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            int a = hard ? (x > kW / 2 ? 0 : 15) : ((src[i] >> 12) & 0xF);
            if (invert) a = 15 - a;
            if (a != 15) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
            }
            // The engine squares the weight before it blends -- `(a * (a <<
            // 12)) >> 16` -- so the seam falls off faster than the map says
            // and never quite reaches fifteen.
            const int w = (a * a) >> 4;
            const uint16_t s = src[i], d = m_Bg[i];
            const int r = DivBy15(((s >> 8) & 0xF) * w + ((d >> 8) & 0xF) * (15 - w));
            const int g = DivBy15(((s >> 4) & 0xF) * w + ((d >> 4) & 0xF) * (15 - w));
            const int b = DivBy15((s & 0xF) * w + (d & 0xF) * (15 - w));
            const bool covered = opaque || a != 0;
            m_Bg[i] = uint16_t((covered ? 0xF000u : 0u) | uint16_t(r << 8) |
                              uint16_t(g << 4) | uint16_t(b));
            m_BgCovered[i] = covered ? 1 : 0;
        }
    }
    if (minX <= maxX) {
        m_SeaColMin = minX;
        m_SeaColMax = maxX;
    }
}

void FightAnimation::StampBg(const Texture* t, int x, int y) {
    if (!t || !t->Valid()) return;
    const TcTexture::Image* img = t->Frame(0);
    if (!img) return;
    for (int sy = 0; sy < img->Height; ++sy) {
        const int py = y + sy;
        if (py < 0 || py >= kH) continue;
        for (int sx = 0; sx < img->Width; ++sx) {
            const int px = x + sx;
            if (px < 0 || px >= kW) continue;
            const uint16_t c = img->Pixels[std::size_t(sy) * img->Width + sx];
            if ((c >> 12) == 0) continue;
            const std::size_t i = std::size_t(py) * kW + px;
            m_Bg[i] = BlendArgb4444(c, m_Bg[i]);
            m_BgCovered[i] = 1;
        }
    }
}

// 0x1006d690 and 0x1006d864: five fixed stations, mirrored for the right, and
// each man given a little wobble of his own so they do not move as one.
void FightAnimation::PlaceMen(int s) {
    Side& sd = m_Side[s];
    const TcTexture::Image* img =
        sd.Sheet && sd.Sheet->Valid() ? sd.Sheet->Frame(0) : nullptr;
    sd.ShootDx = (sd.Script.ShootX[s] << 16) -
                  ((img ? img->Width : 0) << 15);
    sd.ShootDy = (sd.Script.ShootY[s] << 16) -
                  ((img ? img->Height : 0) << 15);
    for (int i = 0; i < kMen; ++i) {
        Man& m = sd.Squad[i];
        m = Man{};
        int x = kManX[i];
        if (s == 0) {
            if (sd.Kind == 1) x -= kShipNudge;
        } else {
            x = kW - x;
            if (sd.Kind == 1) x += kShipNudge;
        }
        m.X = x << 16;
        m.Y = kManY[i] << 16;
        m.TargetX = m.X;
        m.DriftY = int(Random() & 0x7FFF) - 0x4000;
        m.SpeedX = int(Random() & 0x7FFF) + 0x14000;
        m.Timer = kDeathFrames;
        m.AnimState = Man::State::kIdle;
        m.Step = int(Random() & 7);
        m.Frame = ClipFrame(sd.Script.Idle, m.Step);
    }
}

// 0x1006c9a4: what the attacker throws, how fast, and how far past the
// defender it has to fly before the scene follows it across.
void FightAnimation::SetStyle(int style) {
    m_Style = style;
    m_Spread = 0;
    m_ShotJumped = false;
    // The default pair, which style 3 keeps and never uses (0x1006bb4c writes
    // these before 0x1006c9a4 gets a chance to override them).
    m_ShakeDecay = 12000;
    m_ShakeEnergy = 450000;
    switch (style) {
        case 0:
            m_ShakeDecay = 8000;
            m_ShakeEnergy = 250000;
            m_ShotSpeed = 4;
            if (m_Distance >= 2) {
                m_ShotSpeed = 8;
                m_Spread = 0x78;
            }
            break;
        case 1:
            m_ShakeDecay = 20000;
            m_ShakeEnergy = 600000;
            m_ShotSpeed = 2;
            if (m_Distance >= 2) {
                m_ShotSpeed = 4;
                m_Spread = 0x50;
            }
            break;
        case 2:
            m_ShotSpeed = 2;
            if (m_Distance >= 2) {
                m_ShotSpeed = 4;
                m_Spread = 0x78;
            }
            break;
        default:
            m_ShotSpeed = 4;
            break;
    }
    m_ShotHalf = m_AttackerSide;
    m_ShotJumpX = m_AttackerSide == 0 ? kW / 2 + (m_Spread >> 1)
                                       : kW / 2 - (m_Spread >> 1);
}

// 0x1006dc8c / 0x1006d71c. Only the attacking side does anything; the other
// stands and takes it.
void FightAnimation::BeginSide(int s) {
    Side& sd = m_Side[s];
    int kind = sd.Kind;
    // A side that has already walked in does not walk in again on the
    // counter-attack: "No need to walk...".
    if (kind == 0 && m_Walkers > 0) kind = 2;
    m_AttackSounded = false;
    for (int i = 0; i < kMen; ++i) {
        if (i >= sd.Men) continue;
        if (s != m_AttackerSide) {
            SetManState(s, i, Man::State::kIdle);
            continue;
        }
        if (kind == 0) {
            SetManState(s, i, Man::State::kWalk);
            sd.Squad[i].TargetX += (s == 0 ? kWalkIn : -kWalkIn) << 16;
            ++m_Walkers;
        } else {
            SetManState(s, i, Man::State::kAttack);
            // One report for the volley, not five (0x1006d71c keeps its own
            // flag for exactly this).
            if (!m_AttackSounded && m_Sound) {
                m_Sound->PlayUnit(sd.Type, SoundManager::kUnitAttack);
                m_AttackSounded = true;
            }
        }
    }
}

void FightAnimation::StartRound() {
    m_Started = true;
    BeginSide(0);
    BeginSide(1);
}

// 0x1006dcd4: swap sides for the counter-attack, or stop.
void FightAnimation::EndRound() {
    if (!m_Countered) {
        m_Finished = true;
        return;
    }
    m_Countered = false;
    m_HitSounded = false;
    m_AttackSounded = false;
    std::swap(m_AttackerSide, m_DefenderSide);
    SetStyle(m_Side[m_AttackerSide].Style);
    BeginSide(m_AttackerSide);
    m_Side[m_AttackerSide].Pending = m_Side[m_AttackerSide].Men;
}

// --- the men ---------------------------------------------------------------

int FightAnimation::ClipFrame(const FightUnitScript::Clip& c, int step) {
    if (c.Frames.empty()) return 0;
    const int n = int(c.Frames.size());
    return c.Frames[std::size_t(((step % n) + n) % n)];
}

const FightUnitScript::Clip& FightAnimation::ClipFor(const Side& sd,
                                                     Man::State st) {
    switch (st) {
        case Man::State::kWalk: return sd.Script.Walk;
        case Man::State::kAttack: return sd.Script.Attack;
        case Man::State::kDying: return sd.Script.Death;
        default: return sd.Script.Idle;
    }
}

// 0x1006e204. Changing to the attack state is also what fires the shot.
void FightAnimation::SetManState(int s, int i, Man::State st) {
    Side& sd = m_Side[s];
    Man& m = sd.Squad[i];
    if (m.AnimState == st) return;
    m.AnimState = st;
    m.Tick = 0;
    m.Step = st == Man::State::kIdle ? int(Random() & 7) : 0;
    m.Frame = ClipFrame(ClipFor(sd, st), m.Step);
    if (st != Man::State::kAttack) return;

    m.Timer = int(sd.Script.Attack.Frames.size()) * 4;
    if (s != m_AttackerSide) return;
    // A walker's swing is the hit; nothing leaves the sprite.
    if (sd.Kind == 0) return;

    const Side& def = m_Side[m_DefenderSide];
    const int dx = def.Squad[0].X - sd.Squad[0].X;
    int lead = (m_Spread + 15) << 16;
    if (dx < 0) lead = -lead;
    const int aim = dx + lead;
    const int mx = m.X + sd.ShootDx;
    const int my = m.Y + sd.ShootDy;
    if (m_Style != 3) {
        SpawnShot(mx, my, m.X + aim, m.Y);
    } else {
        // A muzzle flash that plays out where it was fired and calls the hit
        // in at the far end of its own length (0x1009cbb0).
        const int sign = m_AttackerSide * 2 - 1;
        const int reach = std::abs(aim) - (1 << 16);
        Particle p;
        p.Kind = Particle::ParticleKind::kBurst;
        p.X = mx;
        p.Y = my;
        p.Vx = 0;
        p.Vy = 0;
        p.GroundY = my;
        p.TargetX = mx - reach * sign;
        const Texture* tex = m_Burst[m_AttackerSide];
        const int frames = tex && tex->Valid() ? int(tex->Frames.size()) : 3;
        p.Life = std::max(1, frames - 2);
        p.Hits = true;
        m_Parts.push_back(p);
    }
}

// 0x1009ddc0: a ballistic shot, aimed by angle and given exactly enough life
// to cover the distance at the style's speed.
void FightAnimation::SpawnShot(int x0, int y0, int x1, int y1) {
    const int dx = x1 - x0, dy = y1 - y0;
    const int dist = IntSqrt((dx >> 10) * (dx >> 10) + (dy >> 10) * (dy >> 10));
    const int life = std::max(1, (dist >> 6) / std::max(1, m_ShotSpeed));
    Particle p;
    p.Kind = Particle::ParticleKind::kShot;
    p.X = x0;
    p.Y = y0;
    p.GroundY = y1;
    p.Life = life;
    p.Life0 = life;
    p.Vx = life > 0 ? dx / life : dx;
    p.Vy = life > 0 ? dy / life : dy;
    p.Frame = 1;
    p.Hits = true;
    p.Trailing = m_Style == 2;
    p.Half = m_ShotHalf;
    m_Parts.push_back(p);
}

// 0x1006e034 and 0x1006e46c.
void FightAnimation::StepMan(int s, int i) {
    Side& sd = m_Side[s];
    Man& m = sd.Squad[i];
    const FightUnitScript::Clip& clip = ClipFor(sd, m.AnimState);
    const auto animate = [&] {
        if (++m.Tick >= clip.Framerate) {
            m.Tick = 0;
            ++m.Step;
        }
        m.Frame = ClipFrame(clip, m.Step);
    };
    const auto travel = [&] {
        m.Y += m.DriftY;
        if (m.TargetX < m.X)
            m.X = std::max(m.TargetX, m.X - m.SpeedX);
        else if (m.X < m.TargetX)
            m.X = std::min(m.TargetX, m.X + m.SpeedX);
        else
            return true;   // arrived
        return false;
    };

    switch (m.AnimState) {
        case Man::State::kWalk:
            animate();
            if (travel()) SetManState(s, i, Man::State::kAttack);
            return;
        case Man::State::kAttack: {
            animate();
            if (m.Timer <= 0 || --m.Timer != 0) return;
            SetManState(s, i, Man::State::kIdle);
            if (s != m_AttackerSide || sd.Kind != 0) return;
            // The swing landed. 0x1006e034 puts the burst fifty pixels the
            // other side of the man who threw it.
            --sd.Pending;
            if (!m_HitSounded && m_Sound)
                m_Sound->PlayFightHit(SoundManager::kHitMelee);
            m_HitSounded = true;
            Explode(m.X + (m_AttackerSide == 0 ? (50 << 16) : -(50 << 16)), m.Y);
            SettleRound();
            return;
        }
        case Man::State::kDying: {
            if (m.Timer <= 0) return;
            --m.Timer;
            animate();
            travel();
            Side& d = m_Side[m_DefenderSide];
            if (m.Timer == 0) --d.Pending;
            if (d.Pending < 1) {
                d.Men = d.MenAfter;
                d.HPTarget = d.HPFinal;
                EndRound();
            }
            return;
        }
        default:
            animate();
            return;
    }
}

// 0x1006de80: a shot or a burst reached the other side.
void FightAnimation::RegisterHit(int x16, int y16) {
    Explode(x16, y16);
    Side& att = m_Side[m_AttackerSide];
    --att.Pending;
    // One hit noise a round, as the engine's own flag at +0x141 arranges.
    // Which of the three depends on what landed: a swing, a musket ball or a
    // cannon shot.
    if (!m_HitSounded && m_Sound) {
        m_Sound->PlayFightHit(m_Style == 3 ? SoundManager::kHitMelee
                             : m_Style == 0 ? SoundManager::kHitProjectile
                                           : SoundManager::kHitCannon);
    }
    m_HitSounded = true;
    // And the screen kicks -- once a round, and never for a muzzle burst.
    // A shake still running from the last round carries on rather than being
    // restarted, which is what the engine's own flag arranges.
    if (!m_Shaking && m_Style != 3) {
        m_Shake.Begin(m_ShakeDecay, m_ShakeEnergy);
        m_Shaking = true;
    }
    Side& def = m_Side[m_DefenderSide];
    if (def.Kind == 1) {
        def.Men = def.MenAfter;
        def.HPTarget = def.HPFinal;
    }
    SettleRound();
}

// 0x1006dd58: once the attacker has nothing left in flight, the defender pays.
void FightAnimation::SettleRound() {
    Side& att = m_Side[m_AttackerSide];
    if (att.Pending >= 1) return;
    Side& d = m_Side[m_DefenderSide];
    d.Pending = 0;
    if (d.Kind == 1) {
        d.Men = d.MenAfter;
        std::swap(d.Sheet, d.SheetHurt);
    }
    for (int i = d.MenAfter; i < d.Men; ++i) {
        ++d.Pending;
        SetManState(m_DefenderSide, i, Man::State::kDying);
        d.Squad[i].Timer = kDeathFrames;
        d.Squad[i].SpeedX = int(Random() & 0x8FFFF) | 0x80000;
        d.Squad[i].TargetX =
            (m_DefenderSide == 0 ? kExitLeft : kExitRight) << 16;
    }
    d.HPTarget = d.HPFinal;
    if (d.Pending == 0) EndRound();
}

// 0x1006e168: a grey cloud, and -- for anything but a swordsman's swing -- a
// flame added on top of it.
//
// The engine throws two more things in here that the port does not: a handful
// of sparks (0x1009e134) and five pieces of ballistic debris (0x1009cdc0, the
// same emitter the battlefield throws rations with). Both of their sheets are
// passed to constructors whose arguments the decompiler lost, so the port
// would be guessing at the artwork rather than porting it -- and the wrong
// sheet is worse than none, since frame zero of most of these is a shadow.
void FightAnimation::Explode(int x16, int y16) {
    Particle p;
    p.Kind = Particle::ParticleKind::kCloud;
    p.X = x16;
    p.Y = y16;
    p.Life = m_Cloud && m_Cloud->Valid() ? int(m_Cloud->Frames.size()) : 8;
    m_Parts.push_back(p);
    if (m_Side[m_AttackerSide].Kind == 0) return;
    p.Kind = Particle::ParticleKind::kFlame;
    p.Life = m_Flame && m_Flame->Valid() ? int(m_Flame->Frames.size()) : 8;
    m_Parts.push_back(p);
}

uint32_t FightAnimation::Random() {
    m_Rng = m_Rng * 1103515245u + 12345u;
    return m_Rng >> 8;
}

// --- the frame -------------------------------------------------------------

bool FightAnimation::Step(Surface& dst) {
    if (!m_Active) return false;

    // The sea first, across the columns the composite left open.
    if (m_SeaSide != Sea::kNone && m_SeaReady) {
        m_Sea.Step();
        const int size = m_Sea.Size();
        const int first = m_SeaColMin / size;
        const int last = first + (m_SeaColMax - m_SeaColMin) / size;
        for (int row = 0; row * size < kH + size; ++row)
            for (int col = first; col <= last; ++col)
                dst.Copy(m_Sea.Pixels(), size, size, col * size, row * size);
    }

    // Then the landscape, skipping wherever it left the sea showing.
    if (m_SeaSide != Sea::kBoth && !m_Bg.empty()) {
        uint16_t* out = dst.Pixels();
        for (std::size_t i = 0; i < m_Bg.size(); ++i)
            if (m_BgCovered[i]) out[i] = m_Bg[i];
    }

    // The shore.
    if (m_BeachWater)
        for (int y = 0; y < kH; y += kBeachStep)
            Blit(dst, m_BeachWater, 0, m_BeachWaterX, y);
    if (m_BeachSurf)
        for (int y = 0; y < kH; y += kBeachStep)
            DrawShoved(dst, m_BeachSurf, m_BeachSurfX, y);

    // A fight at range is two places, not one, so the join is a hard line.
    if (m_Distance >= 2) {
        uint16_t* out = dst.Pixels();
        for (int y = 0; y < kH; ++y) out[std::size_t(y) * kW + kW / 2] = 0xF000;
    }

    if (m_Quay) Blit(dst, m_Quay, 0, m_QuayX, 0);

    // The far bank of mist, behind the men, drifting up at a quarter speed.
    // Mist on both sides is two *near* banks instead -- one shore each -- and
    // those go in front of the men with everything else.
    if (m_MistFar && !m_MistBothNear) {
        const int base = -(m_MistScroll >> 18);
        for (int i = 0; i < 3; ++i)
            Blit(dst, m_MistFar, 0, m_MistFarX, base + i * kMistStep);
    }

    DrawSide(dst, 0);
    DrawSide(dst, 1);

    // Once the leading shot is past the middle the scene follows it to the
    // other half (0x1006e960): every shot still in the air jumps back by the
    // spread and starts being drawn in the defender's half instead.
    if (m_Spread != 0 && !m_ShotJumped) {
        for (const Particle& p : m_Parts) {
            if (p.Kind != Particle::ParticleKind::kShot) continue;
            const int px = p.X >> 16;
            const bool past = m_AttackerSide == 0 ? px > m_ShotJumpX
                                                  : px < m_ShotJumpX;
            if (!past) continue;
            m_ShotJumped = true;
            break;
        }
        if (m_ShotJumped) {
            const int by = m_AttackerSide == 0 ? -(m_Spread << 16)
                                               : (m_Spread << 16);
            for (Particle& p : m_Parts)
                if (p.Kind == Particle::ParticleKind::kShot) {
                    p.X += by;
                    p.Half = m_DefenderSide;
                }
            m_ShotHalf = m_DefenderSide;
        }
    }

    DrawParticles(dst);
    StepParticles();

    // The near bank of mist, in front of everything, drifting down.
    {
        const int base = (m_MistScroll >> 16) - kMistStep;
        if (m_MistNear)
            for (int i = 0; i < 3; ++i)
                Blit(dst, m_MistNear, 0, m_MistNearX, base + i * kMistStep);
        if (m_MistFar && m_MistBothNear)
            for (int i = 0; i < 3; ++i)
                Blit(dst, m_MistFar, 0, m_MistFarX, base + i * kMistStep);
    }
    m_MistScroll += kMistScroll;
    if (m_MistScroll >= (kMistStep << 16)) m_MistScroll = 0;

    // The kick goes on last of all -- and *before* the panels, so the scene
    // moves under a plate that does not. It does not hold the scene open: the
    // engine's end check asks about the particles and the hit point counter
    // and not about this, so a shake still running when the exchange is over
    // is cut off with it.
    if (m_Shaking && m_Shake.Step(dst)) m_Shaking = false;

    DrawPanels(dst);

    // The panels have to be all the way in before anyone swings.
    if (!m_Started) {
        m_Slide += m_SlideSpeed;
        m_SlideSpeed = m_SlideSpeed * kSlideDecayNum / kSlideDecayDen;
        if (m_Slide >= 0) {
            m_Slide = 0;
            StartRound();
        }
    }

    if ((m_Finished || m_Frame > kMaxFrames) && m_Parts.empty() && m_Linger < 1)
        m_Alive = false;
    --m_Linger;
    ++m_Frame;
    if (!m_Alive) {
        m_Active = false;
        // 0x1006ced4 ramps the bed out at 0x20 a block as the state comes
        // down, rather than cutting it, so the last shot still rings on over
        // the map for a moment.
        if (m_Sound) m_Sound->FadeSound(SoundManager::kBankBattleMisc,
                                      SoundManager::kSoundFightScene,
                                      kSceneFadeStep, 0);
        m_Fight.reset();
        return false;
    }
    return true;
}

void FightAnimation::Blit(Surface& dst, const Texture* t, int frame, int x,
                          int y, const Surface::Rect* clip) {
    if (!t || !t->Valid()) return;
    const TcTexture::Image* img = t->Frame(frame);
    if (!img) return;
    dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0,
                   img->Width, img->Height, x, y, clip);
}

// 0x100d84c8: the surf round a coast, shoved sideways a row at a time by the
// ripple tank. One displacement per row, from column zero of that row.
void FightAnimation::DrawShoved(Surface& dst, const Texture* t, int x, int y) {
    if (!t || !t->Valid()) return;
    const TcTexture::Image* img = t->Water && t->Surf(0) ? t->Surf(0) : t->Frame(0);
    if (!img) return;
    const int32_t* heights = m_SeaReady ? m_Sea.Heights() : nullptr;
    const int mask = m_Sea.Mask(), shift = m_Sea.Shift();
    for (int sy = 0; sy < img->Height; ++sy) {
        const int py = y + sy;
        if (py < 0 || py >= kH) continue;
        const int wave =
            heights ? int(heights[std::size_t((sy & mask) << shift)] >> 6) : 0;
        const int shove = Clamp(wave, -TcTexture::kSurfShift, TcTexture::kSurfShift);
        uint16_t* row = dst.Pixels() + std::size_t(py) * kW;
        for (int sx = 0; sx < img->Width; ++sx) {
            const int px = x + sx;
            if (px < 0 || px >= kW) continue;
            const int tx = sx - shove;
            if (tx < 0 || tx >= img->Width) continue;
            const uint16_t c = img->Pixels[std::size_t(sy) * img->Width + tx];
            if ((c >> 12) == 0) continue;
            row[px] = BlendArgb4444(c, row[px]);
        }
    }
}

// 0x1006e848: back to front, and a big ship instead of five men.
void FightAnimation::DrawSide(Surface& dst, int s) {
    Side& sd = m_Side[s];
    // The men are stepped from in here, as the engine steps them from its own
    // draw (0x1006e848 calls the state machine and then blits the frame it
    // returns). Missing artwork must therefore not skip the stepping, or a
    // fight whose sheet failed to decode would stand still until the scene
    // timed out instead of playing through silently.
    const uint16_t* lut = m_Renderer ? m_Renderer->OwnerLut(sd.Colour) : nullptr;
    const bool haveArt = sd.Sheet && sd.Sheet->Valid() && lut;
    const auto draw = [&](Man& m) {
        if (!haveArt) return;
        const TcTexture::Image* img = sd.Sheet->Frame(m.Frame);
        if (!img) return;
        const int x = (m.X >> 16) - img->Width / 2;
        const int y = (m.Y >> 16) - img->Height / 2;
        Surface::Rect clip{0, 0, kW, kH};
        if (sd.Wading) clip.Y1 = std::min(kH, y + 0x2C);
        dst.BlitIndexed(img->Pixels.data(), img->Width, img->Height, x, y, lut,
                        m_Renderer->LutSize(), &clip);
    };
    if (sd.Kind == 1) {
        StepMan(s, 0);
        draw(sd.Squad[0]);
        return;
    }
    for (int k = 0; k < kMen; ++k) {
        const int i = kDrawOrder[k];
        if (i >= sd.Men) continue;
        StepMan(s, i);
        draw(sd.Squad[i]);
    }
}

void FightAnimation::DrawParticles(Surface& dst) {
    for (const Particle& p : m_Parts) {
        const int x = p.X >> 16, y = p.Y >> 16;
        switch (p.Kind) {
            case Particle::ParticleKind::kShot: {
                const Texture* tex = m_Style == 0 ? m_SmallShot : m_BigShot;
                if (!tex || !tex->Valid()) break;
                // While the shot is crossing it belongs to one half of the
                // screen; the other half is the far bank and it must not be
                // drawn over it.
                Surface::Rect clip{p.Half == 0 ? 0 : kW / 2, 0,
                                   p.Half == 0 ? kW / 2 : kW, kH};
                if (m_Spread == 0) clip = Surface::Rect{0, 0, kW, kH};
                const TcTexture::Image* shadow = tex->Frame(0);
                if (shadow)
                    Blit(dst, tex, 0, x - shadow->Width / 2,
                         (p.GroundY >> 16) - shadow->Height / 2, &clip);
                const TcTexture::Image* img = tex->Frame(p.Frame);
                if (img)
                    Blit(dst, tex, p.Frame, x - img->Width / 2,
                         ((p.Y - p.Arc) >> 16) - img->Height / 2, &clip);
                break;
            }
            case Particle::ParticleKind::kBurst: {
                const Texture* tex = m_Burst[m_AttackerSide];
                const int f = p.Frame;
                if (!tex || !tex->Valid()) break;
                const TcTexture::Image* img = tex->Frame(f);
                if (img)
                    Blit(dst, tex, f, x - img->Width / 2, y - img->Height / 2);
                break;
            }
            case Particle::ParticleKind::kCloud:
            case Particle::ParticleKind::kFlame: {
                const Texture* tex =
                    p.Kind == Particle::ParticleKind::kCloud ? m_Cloud : m_Flame;
                if (!tex || !tex->Valid()) break;
                const TcTexture::Image* img = tex->Frame(p.Frame);
                if (!img) break;
                const int px = x - img->Width / 2, py = y - img->Height / 2;
                if (p.Kind == Particle::ParticleKind::kFlame)
                    dst.BlitAdditive(img->Pixels.data(), img->Width,
                                     img->Height, px, py);
                else
                    Blit(dst, tex, p.Frame, px, py);
                break;
            }
        }
    }
}

void FightAnimation::StepParticles() {
    std::vector<Particle> hits;
    m_Trail.clear();
    for (Particle& p : m_Parts) {
        switch (p.Kind) {
            case Particle::ParticleKind::kShot: {
                p.X += p.Vx;
                p.Y += p.Vy;
                // The lift above the straight line, a parabola over the
                // shot's life (0x1009decc).
                const int half = p.Life0 >> 1;
                const int t = p.Life - half;
                p.Arc = ((half * half - t * t) >> 3) << 16;
                if (const Texture* tex = m_Style == 0 ? m_SmallShot : m_BigShot) {
                    const int n = tex->Valid() ? int(tex->Frames.size()) : 1;
                    if (n > 1 && ++p.Frame >= n) p.Frame = 1;
                }
                // The Scorch Cannon's shell burns as it flies: 0x1006e204
                // hangs a flame emitter off the shot it just fired, and
                // 0x1006e960 keeps it on the shell as the scene follows it
                // across. One puff every other frame is what that looks like.
                if (p.Trailing && (p.Life & 1) == 0 && m_Flame &&
                    m_Flame->Valid()) {
                    Particle f;
                    f.Kind = Particle::ParticleKind::kFlame;
                    f.X = p.X;
                    f.Y = p.Y - p.Arc;
                    f.Life = int(m_Flame->Frames.size());
                    m_Trail.push_back(f);
                }
                break;
            }
            case Particle::ParticleKind::kBurst:
            case Particle::ParticleKind::kCloud:
            case Particle::ParticleKind::kFlame:
                ++p.Frame;
                break;
        }
        --p.Life;
    }
    for (Particle& t : m_Trail) m_Parts.push_back(t);
    m_Trail.clear();
    for (const Particle& p : m_Parts)
        if (p.Life <= 0 && p.Hits) hits.push_back(p);
    m_Parts.erase(std::remove_if(m_Parts.begin(), m_Parts.end(),
                                [](const Particle& p) { return p.Life <= 0; }),
                 m_Parts.end());
    for (const Particle& p : hits)
        RegisterHit(p.Kind == Particle::ParticleKind::kBurst ? p.TargetX : p.X, p.Y);
}

// 0x1006e510. Everything shares one offset that walks in from off-screen.
void FightAnimation::DrawPanels(Surface& dst) {
    const int o = m_Slide >> 16;      // pixels, -88 .. 0
    const int q = m_Slide >> 18;      // a quarter of it, for the two planks

    Blit(dst, m_UILeft, 0, kHpPlateLeftX + q, kPlateY);
    Blit(dst, m_UILeft, 0, kHpPlateRightX - q, kPlateY);
    if (m_Font && m_Font->Valid()) {
        m_Font->DrawNumber(dst, std::max(0, m_Side[0].HPShown), kHpLeftX + q,
                          kHpY);
        m_Font->DrawNumber(dst, std::max(0, m_Side[1].HPShown), kHpRightX - q,
                          kHpY);
    }
    Blit(dst, m_UIName, 0, kNameLeftX + o, 0);
    Blit(dst, m_UIName, 0, kNameRightX - o, 0);
    if (m_Font && m_Font->Valid()) {
        m_Font->Draw(dst, m_Side[0].Commander, kNameLeftX + o + 3, 0);
        m_Font->Draw(dst, m_Side[1].Commander, kNameRightX - o + 3, 0);
    }
    for (int s = 0; s < 2; ++s) {
        const int px = s == 0 ? kShieldPlateLeftX : kShieldPlateRightX;
        const int py = kPlateY + o;
        Blit(dst, m_UIRight, 0, px, py);
        const int n = std::max(0, m_Side[s].Shields);
        for (int i = 0; i < n; ++i)
            Blit(dst, m_Shield, 0,
                 px + kShieldBias - 3 * n + i * kShieldStep, py + kShieldY);
    }

    // The number walks down to the new value rather than snapping, and holds
    // the scene open while it does.
    for (Side& sd : m_Side) {
        if (sd.HPShown == sd.HPTarget) continue;
        sd.HPShown -= ((sd.HPShown - sd.HPTarget) >> 4) + 1;
        m_Linger = kLinger;
    }
}

}  // namespace bb
