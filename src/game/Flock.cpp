#include "game/Flock.h"

#include <cmath>
#include <cstdlib>

#include "platform/Surface.h"

namespace bb {
namespace {

int Hypot(int dx, int dy) {
    return int(std::lround(std::sqrt(double(dx) * dx + double(dy) * dy)));
}

}  // namespace

void Flock::Scatter(uint32_t& rng) {
    for (Boid& b : Member) {
        rng = rng * 1103515245u + 12345u;
        b.X = (Home.X + int((rng >> 16) % 60) - kScatter) << 16;
        rng = rng * 1103515245u + 12345u;
        b.Y = (Home.Y + int((rng >> 16) % 60) - kScatter) << 16;
        b.Vx = b.Vy = 0;
    }
}

// The flight, 0x100718cc term for term. A flock only lives while the camera
// is within two hundred pixels of its home, and the full update runs every
// other step -- the counter parity is the flap rate.
void Flock::Step(int shipX, int shipY, int camX, int camY, uint32_t& rng) {
    if (std::abs(camX - Home.X) > kStepGate) return;
    if (std::abs(camY - Home.Y) > kStepGate) return;
    if ((++Counter & 1) == 0) return;
    for (int i = 0; i < kSize; ++i) {
        Boid& b = Member[i];
        // Pool everyone within sixteen pixels -- positions for cohesion,
        // velocities for alignment; the count includes the member itself
        // but the velocity sum does not, exactly as the engine has it.
        int px = b.X, py = b.Y, ax = 0, ay = 0, n = 1;
        int wx = b.Vx, wy = b.Vy;   // the working velocity
        for (int j = 0; j < kSize; ++j) {
            if (j == i) continue;
            const Boid& o = Member[j];
            const int dx = b.X - o.X, dy = b.Y - o.Y;
            const int d = Hypot(dx >> 16, dy >> 16);
            if (d < kGather) {
                px += o.X;
                py += o.Y;
                ax += o.Vx;
                ay += o.Vy;
                ++n;
            }
            // A shove off anyone within twelve, straight into the working
            // velocity.
            if (d < kSpread) {
                wx += dx / (d + 1);
                wy += dy / (d + 1);
            }
        }
        int cx = px / n, cy = py / n;
        ax /= n;
        ay /= n;
        // Alignment: keep four fifths of your own way, take a fifth of the
        // flock's.
        wx = ax + (wx - ax) * 4 / 5;
        wy = ay + (wy - ay) * 4 / 5;
        // The goal: the crowd's centre eased a tenth of the way onto a random
        // point in the box by home, re-rolled every update.
        rng = rng * 1103515245u + 12345u;
        const int tx = (Home.X - int((rng >> 16) % kWander)) << 16;
        rng = rng * 1103515245u + 12345u;
        const int ty = (Home.Y - int((rng >> 16) % kWander)) << 16;
        cx = tx + (cx - tx) * 9 / 10;
        cy = ty + (cy - ty) * 9 / 10;
        const int gx = cx - b.X, gy = cy - b.Y;
        const int gd = Hypot(gx >> 16, gy >> 16);
        wx += gx / (gd + 1);
        wy += gy / (gd + 1);
        // The ship scatters anything it comes within sixteen pixels of: the
        // working velocity is *replaced* by four times the unit away.
        const int sx = b.X - (shipX << 16), sy = b.Y - (shipY << 16);
        const int sd = Hypot(sx >> 16, sy >> 16);
        if (sd < kScare) {
            wx = (sx / (sd + 1)) << 2;
            wy = (sy / (sd + 1)) << 2;
        }
        // Damped to nine tenths (the engine's 18/20), then the stored
        // velocity eases half the way to it, and the member moves.
        wx = wx * 18 / 20;
        wy = wy * 18 / 20;
        b.Vx = wx + (b.Vx - wx) / 2;
        b.Vy = wy + (b.Vy - wy) / 2;
        b.X += b.Vx;
        b.Y += b.Vy;
    }
}

void Flock::Draw(Surface& dst, int originX, int originY) const {
    // Only when near the view, as 0x10071e98 does at 300 pixels -- measured
    // against the view's top-left corner, which is what it is handed.
    if (std::abs(Home.X - originX) > kRange) return;
    if (std::abs(Home.Y - originY) > kRange) return;
    for (const Boid& b : Member) {
        const int x0 = (b.X >> 16) - originX, y0 = (b.Y >> 16) - originY;
        // The colour keys off the *vertical* speed alone, squared -- so it
        // saturates within a pixel a tick -- and caps at twelve: alpha climbs
        // from 4 to 10 while the grey whitens under it.
        int speed = std::abs((b.Vy >> 14) * (b.Vy >> 14));
        if (speed > 12) speed = 12;
        // A two-pixel streak along the velocity; a member at rest is a
        // one-pixel diagonal tick.
        const int len = Hypot(b.Vx >> 15, b.Vy >> 15);
        const int x1 = len < 1 ? x0 + 1 : x0 + ((b.Vx << 2) / len >> 16);
        const int y1 = len < 1 ? y0 + 1 : y0 + ((b.Vy << 2) / len >> 16);
        const uint16_t c = uint16_t(
            ((speed >> 1) * 0x1001 + (speed + 3) * 0x110 + 0x4009) & 0xFFFF);
        dst.Line(x0, y0, x1, y1, c);
    }
}

}  // namespace bb
