#include "game/Water.h"

#include <cmath>

#include "game/TextureCache.h"
#include "platform/Surface.h"
#include "shim/Log.h"

namespace bb {
namespace {

constexpr int kAngles = 1024;  // the engine's sine table (0x1011e43c)
constexpr int kQuarter = kAngles / 4;

// 16.16 sine, amplitude 65536 — the table's own scale, checked against its
// first entries (0, 402, 804, ... = 65536 * sin(2*pi*i/1024)).
const int32_t* SineTable() {
    static int32_t table[kAngles];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < kAngles; ++i)
            table[i] = static_cast<int32_t>(
                std::lround(65536.0 * std::sin(2.0 * 3.14159265358979323846 * i / kAngles)));
        built = true;
    }
    return table;
}

int32_t Sin(int a) { return SineTable()[a & (kAngles - 1)]; }
int32_t Cos(int a) { return SineTable()[(a + kQuarter) & (kAngles - 1)]; }

}  // namespace

bool Water::Load(TextureCache& textures) {
    m_Water = textures.Load("Data\\Menu\\water128.tc");
    m_Fish = textures.Load("Data\\Menu\\fish.tc");
    if (!m_Water || !m_Water->Valid()) {
        LogError("water: Data\\Menu\\water128.tc unavailable\n");
        m_Water = nullptr;
        return false;
    }
    const TcTexture::Image* img = m_Water->Frame(0);
    if (!img || img->Width != kTex || img->Height != kTex) {
        LogError("water: source is %dx%d, expected %dx%d\n",
                 img ? img->Width : 0, img ? img->Height : 0, kTex, kTex);
        return false;
    }
    m_Src = img->Pixels.data();

    m_Field.Init(kGridShift, kDropRadius, kDropStrength, kDampingShift);
    m_Out.assign(size_t(kTex) * kTex, 0);

    // Turn rates 32 and 19 (`i * -0xd + 0x20`).
    for (int i = 0; i < 2; ++i) m_FishState[i].Turn = 0x20 - i * 0xd;

    // The water's own flock, home at the origin (0x100d8c50 hands the ctor
    // the point (0, 0)); its members start there and the wander spreads them.
    m_Flock = Flock{};
    return true;
}

void Water::Splash(int gx, int gy) { m_Field.Splash(gx, gy); }

void Water::RenderCaustics() {
    // The original also offsets the whole field by a scroll accumulator that
    // advances by sim+0x6c / +0x70 each frame. The menu sets both deltas to
    // zero (FUN_100d83e0(sim, 0, 0)), so the accumulators never move and the
    // terms are dropped here rather than carried as dead arithmetic.
    ++m_Frame;
    const int32_t* h = m_Field.Heights();
    // Only one parity of rows is refreshed per frame; the other keeps last
    // frame's pixels. That interlace is the original's, not an optimisation
    // added here.
    int rowBase = int(m_Frame & 1) << kTexShift;

    for (int y = int(m_Frame & 1); y < Surface::kHeight; y += 2) {
        const int row = (y & kGridMask) << kGridShift;
        const int rowDown = ((y + 1) & kGridMask) << kGridShift;
        for (int x = 0; x < Surface::kWidth; ++x) {
            const int ux = x & kGridMask;
            const int32_t h0 = h[ux + row];
            const int32_t dy = h0 - h[ux + rowDown];
            int shade = dy >> kShadeShift;
            if (shade == 6) shade = 4;  // the original's own special case
            const int32_t dx = h0 - h[((x + 1) & kGridMask) + row];

            const int tx = (x + (dx >> kRefractShift)) & kTexMask;
            const int ty = (y + (dy >> kRefractShift)) & kTexMask;
            const uint16_t c = m_Src[tx + (ty << kTexShift)];

            int r = int(c & 0x0F00) - shade * 0x100;
            int g = int(c & 0x00F0) - shade * 0x10;
            int b = int(c & 0x000F) - shade;
            if (r > 0xFFF) r = 0xFFF;
            if (r < 0x100) r = 0x100;
            if (g > 0xFE) g = 0xFF;
            if (g < 0x10) g = 0x10;
            if (b > 0xE) b = 0xF;
            uint16_t px = static_cast<uint16_t>(r) | static_cast<uint16_t>(g);
            if (b >= 0) px |= static_cast<uint16_t>(b);
            m_Out[rowBase + x] = px;
        }
        // Two source rows per step, because y advances by two.
        rowBase += kTex * 2;
    }
}

void Water::StepFish(int i) {
    Fish& f = m_FishState[i];
    f.X += Sin(f.Heading) * 4;
    f.Y += Cos(f.Heading) * 4;

    // Only fish 0 wanders: within sixteen pixels of its goal on both axes
    // (0x100d8aa8's threshold is 0xFFFFF) it picks a fresh random point on
    // the screen. Fish 1 never retargets on its own -- its goal is planted on
    // fish 0's position by the caller, so it chases.
    if (i == 0) {
        const int32_t dx = f.TargetX - f.X;
        const int32_t dy = f.TargetY - f.Y;
        if (std::abs(dx) <= kFishReach && std::abs(dy) <= kFishReach) {
            m_Rng = m_Rng * 1103515245u + 12345u;
            f.TargetX = int32_t((m_Rng >> 16) % Surface::kWidth) << 16;
            m_Rng = m_Rng * 1103515245u + 12345u;
            f.TargetY = int32_t((m_Rng >> 16) % Surface::kHeight) << 16;
        }
    }

    // Steer toward the target, but only once off by more than 0x10 units.
    const double want = std::atan2(double(f.TargetX - f.X), double(f.TargetY - f.Y));
    int desired = int(std::lround(want * kAngles / (2.0 * 3.14159265358979323846)));
    desired &= kAngles - 1;

    int cw = desired - f.Heading;
    if (cw < 0) cw += kAngles;
    int ccw = f.Heading - desired;
    if (ccw < 0) ccw += kAngles;
    if (std::abs(f.Heading - desired) > 0x10)
        f.Heading += (cw < ccw) ? f.Turn : -f.Turn;
    if (f.Heading < 0) f.Heading += kAngles;
    if (f.Heading > kAngles - 1) f.Heading -= kAngles;
}

void Water::Draw(Surface& screen) {
    if (!m_Src) return;

    // Step: swap buffers, add a droplet on the interval, propagate, refract.
    m_Field.Swap();
    if (m_Frame % kDropInterval == 0) {
        m_Rng = m_Rng * 1103515245u + 12345u;
        const int gx = int(m_Rng >> 16) & kGridMask;
        m_Rng = m_Rng * 1103515245u + 12345u;
        const int gy = int(m_Rng >> 16) & kGridMask;
        m_Field.Splash(gx, gy);
    }
    m_Field.Propagate();
    RenderCaustics();

    // The original Mem::Copy's each row straight over the screen.
    for (int y = 0; y < Surface::kHeight; ++y) {
        const uint16_t* s = m_Out.data() + size_t(y) * kTex;
        uint16_t* d = screen.Pixels() + size_t(y) * screen.Width();
        for (int x = 0; x < Surface::kWidth; ++x) d[x] = s[x];
    }

    // The flock's specks mill over the water, under the fish (0x100d8ed0
    // steps and draws it between the sea copy and the fish). No ship swims
    // the menu, so nothing ever scares them; the camera sits on their home.
    m_Flock.Step(10000, 10000, m_Flock.Home.X, m_Flock.Home.Y, m_Rng);
    m_Flock.Draw(screen, kFlockOriginX, kFlockOriginY);

    if (!m_Fish || !m_Fish->Valid()) return;
    // Step fish 0, plant its position as fish 1's goal (the shoaling,
    // 0x100d8ed0's two stores between the step calls), step fish 1, then
    // draw both -- ghostly, at the texture's alpha 8.
    StepFish(0);
    m_FishState[1].TargetX = m_FishState[0].X;
    m_FishState[1].TargetY = m_FishState[0].Y;
    StepFish(1);
    for (int i = 0; i < 2; ++i) {
        const int frame = (m_FishState[i].Heading >> 5) % int(m_Fish->Frames.size());
        if (const TcTexture::Image* g = m_Fish->Frame(frame)) {
            screen.Blit(g->Pixels.data(), g->Width, g->Height,
                        m_FishState[i].X >> 16, m_FishState[i].Y >> 16,
                        kFishAlpha);
        }
    }
}

}  // namespace bb
