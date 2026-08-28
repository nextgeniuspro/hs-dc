#include "game/LapAnimation.h"

#include "game/FileInputStream.hpp"
#include "game/FilePack.hpp"
#include "shim/Log.h"

namespace bb {
namespace {

// The four sidecar loaders build their paths the same way (0x10002104): a
// directory prefix, the animation name, then an extension.
constexpr const char* kAnimDir = "Data\\anim\\";

int32_t ReadI32(FileInputStream& in) {
    return static_cast<int32_t>(in.ReadU32());
}

// Length-prefixed, not NUL-terminated -- the loader reads the u16, then
// exactly that many bytes, and appends its own terminator (0x10000860).
std::string ReadString(FileInputStream& in) {
    const uint16_t n = in.ReadU16();
    std::string s(n, '\0');
    if (n) in.Read(&s[0], n);
    return s;
}

int32_t Abs32(int32_t v) { return v < 0 ? -v : v; }

}  // namespace

std::string LapAnimation::PathFor(const std::string& name) {
    return std::string(kAnimDir) + name + ".lap";
}

bool LapAnimation::Load(FilePack& pack, const std::string& name) {
    m_Sprites.clear();
    m_Images.clear();
    m_Objects.clear();
    m_Fx.clear();
    m_Next.clear();

    const std::string path = PathFor(name);
    auto file = pack.Open(path);
    if (!file) {
        LogError("anim: '%s' not in the pak\n", path.c_str());
        return false;
    }
    FileInputStream& in = *file;

    // The signature is a version word followed by its own complement; the
    // loader rejects anything else ("ERROR: Illegal signature").
    const uint16_t sig = in.ReadU16();
    const uint16_t inv = in.ReadU16();
    if (sig != static_cast<uint16_t>(~inv)) {
        LogError("anim: '%s' has a bad signature (%04x/%04x)\n", path.c_str(),
                 sig, inv);
        return false;
    }

    in.ReadU32();  // fileSize; zero in every shipped file
    m_Duration = in.ReadU16();
    m_Fps = in.ReadU16();
    m_ViewX = in.ReadU16();
    m_ViewY = in.ReadU16();
    m_ViewW = in.ReadU16();
    m_ViewH = in.ReadU16();
    const uint16_t imageAmount = in.ReadU16();
    const uint16_t audioAmount = in.ReadU16();
    const uint16_t objectAmount = in.ReadU16();
    const uint16_t spriteAmount = in.ReadU16();
    const uint16_t fxAmount = in.ReadU16();
    in.ReadU8();   // compression; zero in every shipped file
    in.ReadU8();   // pad to the 28-byte header the loader reads in one go
    (void)audioAmount;  // always 0: sound lives in the .snd sidecars

    // Sprites (0x10000630): 48 bytes each.
    m_Sprites.resize(spriteAmount);
    for (Sprite& s : m_Sprites) {
        s.Image = in.ReadU16();
        s.Kind = in.ReadU8();
        in.ReadU8();
        s.X = ReadI32(in);
        s.Y = ReadI32(in);
        s.Width = ReadI32(in);
        s.Height = ReadI32(in);
        s.Rotation = ReadI32(in);
        s.Alpha = ReadI32(in);
        s.CellSeed = ReadI32(in);
        s.CellStep = ReadI32(in);
        s.PivotX = ReadI32(in);
        s.PivotY = ReadI32(in);
        s.Filter = in.ReadU8();
        in.ReadU8();
        in.ReadU16();  // three bytes of exporter stack garbage
    }

    // Image paths (0x100007e8).
    m_Images.reserve(imageAmount);
    for (uint16_t i = 0; i < imageAmount; ++i) m_Images.push_back(ReadString(in));

    // Objects and their keyframes (0x10000b50).
    m_Objects.resize(objectAmount);
    for (Object& obj : m_Objects) {
        obj.Sprite = in.ReadU16();
        obj.Channel = in.ReadU8();
        in.ReadU8();
        const uint16_t keyCount = in.ReadU16();
        obj.ValueCount = in.ReadU16();
        obj.Keys.resize(keyCount);
        for (Keyframe& k : obj.Keys) {
            k.Interp = ReadI32(in);
            k.Time = ReadI32(in);
            k.Tension = ReadI32(in);
            k.Bias = ReadI32(in);
            k.Continuity = ReadI32(in);
            k.EaseIn = ReadI32(in);
            k.EaseOut = ReadI32(in);
            k.Values[0] = ReadI32(in);
            k.Values[1] = ReadI32(in);
        }
        Prepare(obj);
    }

    // Effects (0x10000e38).
    m_Fx.resize(fxAmount);
    for (Fx& f : m_Fx) {
        f.Time = ReadI32(in);
        f.Kind = ReadI32(in);
        f.Param = ReadI32(in);
        f.Colour = ReadI32(in);
    }

    // The animation to play next (0x10000d64). Empty in every shipped file.
    m_Next = ReadString(in);

    if (in.Tell() > in.Size()) {
        LogError("anim: '%s' is truncated\n", path.c_str());
        m_Sprites.clear();
        return false;
    }
    Reset();
    return true;
}

void LapAnimation::Reset() {
    m_States.assign(m_Sprites.size(), SpriteState{});
    for (std::size_t i = 0; i < m_Sprites.size(); ++i) {
        const Sprite& s = m_Sprites[i];
        SpriteState& st = m_States[i];
        st.Visible = false;
        st.X = s.X;
        st.Y = s.Y;
        st.Width = s.Width;
        st.Height = s.Height;
        st.Rotation = s.Rotation;
        st.Alpha = s.Alpha;
        // 0x100e7398 stores the seed shifted a further sixteen places, which
        // for any value the exporter writes lands on zero. Reproduced rather
        // than tidied: it is what decides the phase every animation starts on.
        st.Cell = int32_t(uint32_t(s.CellSeed) << 16);
        st.CellStep = s.CellStep;
    }
    m_Cursors.assign(m_Objects.size(), 0);
}

// 0x10001a7c. `t` is how far through the segment we are, 0..1.0. The two ease
// values carve out an accelerating head and a decelerating tail; the middle is
// linear but steeper, so the total distance still works out to 1.0.
int32_t LapAnimation::Ease(int32_t t, int32_t easeOut, int32_t easeIn) {
    int32_t a = easeOut, b = easeIn;
    const int32_t sum = a + b;
    if (sum == 0 || t == 0 || t == kOne) return t;

    // Overlapping ramps get scaled down so they exactly fill the segment.
    if (sum > kOne) {
        a = ((a << 8) / sum) << 8;
        b = ((b << 8) / sum) << 8;
    }
    if (a == 0 && b == 0) return t;

    const int32_t k = 0x40000000 / ((2 * kOne - b) - a);
    int32_t slope = k << 2;
    int32_t u;
    if (t < a) {
        slope = (k << 10) / a;
        u = (t * slope) >> 8;
        slope = t;
    } else if (t >= kOne - b) {
        const int32_t rest = kOne - t;
        const int32_t s = (k << 10) / b;
        return kOne - (((rest >> 8) * ((rest * s) >> 8)) >> 8);
    } else {
        u = t * 2 - a;
    }
    return ((slope >> 8) * u) >> 8;
}

// 0x10001494. h00, h01, h10, h11 of the cubic Hermite basis, in 16.16.
void LapAnimation::HermiteBasis(int32_t t, int32_t out[4]) {
    const int32_t u = t >> 8;         // 8.8
    const int32_t u2 = u * u;         // 16.16
    const int32_t u3 = u * (u2 >> 8);
    const int32_t h = u3 * 2 - u2 * 3;
    out[0] = h + kOne;   // previous value
    out[1] = -h;         // next value
    out[2] = t - u2 * 2 + u3;  // previous out-tangent
    out[3] = u3 - u2;          // next in-tangent
}

// 0x100014e8 and the three helpers it calls. Tangents are derived once at load
// time, exactly as the original does after reading each object.
void LapAnimation::Prepare(Object& obj) {
    const int n = static_cast<int>(obj.Keys.size());
    const int vals = obj.ValueCount;
    if (n < 2 || vals <= 0) return;

    auto spline = [&](int i) { return obj.Keys[i].Interp == kSpline; };

    // Two keyframes with no interior neighbour: both tangents are just the
    // segment slope, scaled by tension (0x10001658).
    if (n == 2) {
        if (spline(0) && spline(1)) {
            for (int v = 0; v < vals; ++v) {
                const int32_t d = (obj.Keys[1].Values[v] - obj.Keys[0].Values[v]) >> 8;
                obj.Keys[0].OutTangent[v] = ((kOne - obj.Keys[0].Tension) >> 8) * d;
                obj.Keys[1].InTangent[v] = ((kOne - obj.Keys[1].Tension) >> 8) * d;
            }
        }
        return;
    }

    // Interior spline keyframes: Kochanek-Bartels, with the segment lengths
    // folded in so uneven spacing does not overshoot (0x10001820).
    const int32_t span = obj.Keys[n - 1].Time - obj.Keys[0].Time;
    for (int i = 1; i < n - 1; ++i) {
        if (!spline(i)) continue;
        Keyframe& k = obj.Keys[i];
        int32_t tPrev = obj.Keys[i - 1].Time;
        const int32_t tCur = k.Time;
        int32_t tNext = obj.Keys[i + 1].Time;
        if (tNext <= tPrev) tNext += span;
        if (tCur <= tPrev) tPrev -= span;

        const int32_t half = (tNext - tPrev) >> 1;
        if (half == 0) continue;
        const int32_t ra = ((tCur - tPrev) << 16) / half;
        const int32_t rb = ((tNext - tCur) << 16) / half;

        const int32_t bias = k.Bias;
        const int32_t absBias = Abs32(bias);
        const int32_t cont = kOne - k.Continuity;
        const int32_t tens = (kOne - k.Tension) >> 5;
        const int32_t c1 = (((kOne - bias) >> 4) * tens) >> 8;
        const int32_t c2 = (((2 * kOne - (kOne - bias)) >> 4) * tens) >> 8;
        const int32_t d1 = cont >> 8;
        const int32_t d2 = (2 * kOne - cont) >> 8;
        const int32_t e1 = (ra + absBias - (((ra >> 4) * (absBias >> 4)) >> 8)) >> 8;
        const int32_t e2 = (rb + absBias - (((rb >> 4) * (absBias >> 4)) >> 8)) >> 8;

        for (int v = 0; v < vals; ++v) {
            const int32_t p = (k.Values[v] - obj.Keys[i - 1].Values[v]) >> 4;
            const int32_t q = (obj.Keys[i + 1].Values[v] - k.Values[v]) >> 4;
            k.InTangent[v] = ((((q * ((e1 * ((d1 * c2) >> 8)) >> 12)) >> 4) +
                                ((p * ((e1 * ((d2 * c1) >> 8)) >> 12)) >> 4)) >> 4);
            k.OutTangent[v] = ((((q * ((e2 * ((d1 * c1) >> 8)) >> 12)) >> 4) +
                                 ((((e2 * ((d2 * c2) >> 8)) >> 12) * p) >> 4)) >> 4);
        }
    }

    // Where a spline meets a linear neighbour, the free end takes whatever
    // tangent makes the cubic pass through both points (0x100016fc/0x1000178c).
    auto outFromNext = [&](int i, int j) {
        Keyframe& a = obj.Keys[i];
        const Keyframe& b = obj.Keys[j];
        for (int v = 0; v < vals; ++v) {
            a.OutTangent[v] = ((kOne - a.Tension) >> 9) *
                               (((b.Values[v] - a.Values[v]) * 3 - b.InTangent[v]) >> 8);
        }
    };
    auto inFromPrev = [&](int i, int j) {
        const Keyframe& a = obj.Keys[i];
        Keyframe& b = obj.Keys[j];
        for (int v = 0; v < vals; ++v) {
            b.InTangent[v] = -((kOne - b.Tension) >> 9) *
                              (((a.Values[v] - b.Values[v]) * 3 + a.OutTangent[v]) >> 8);
        }
    };

    for (int i = 0; i < n - 1; ++i)
        if (!spline(i) && spline(i + 1)) outFromNext(i, i + 1);
    if (spline(0)) outFromNext(0, 1);
    for (int i = 1; i < n; ++i)
        if (!spline(i) && spline(i - 1)) inFromPrev(i - 1, i);
    if (spline(n - 1)) inFromPrev(n - 2, n - 1);
}

// 0x10001210, once per object per frame.
void LapAnimation::Evaluate(int frame) {
    for (std::size_t oi = 0; oi < m_Objects.size(); ++oi) {
        Object& obj = m_Objects[oi];
        int& cursor = m_Cursors[oi];
        const int n = static_cast<int>(obj.Keys.size());
        if (n == 0 || cursor >= n) continue;
        if (obj.Sprite >= m_States.size()) continue;
        SpriteState& st = m_States[obj.Sprite];

        // A sprite exists only between its first and last keyframe. That is
        // the whole visibility mechanism -- nothing else turns sprites on.
        if (frame < obj.Keys[0].Time) {
            st.Visible = false;
            continue;
        }
        if (frame >= obj.Keys[cursor].Time) {
            ++cursor;
            if (cursor >= n) {
                st.Visible = false;
                continue;
            }
        }
        st.Visible = true;

        const Keyframe& cur = obj.Keys[cursor];
        const Keyframe& prev = obj.Keys[cursor - 1];
        int32_t out[2] = {prev.Values[0], prev.Values[1]};

        if (cur.Interp == kStep) {
            // Holds the previous value and snaps on the exact frame.
            if (cur.Time != frame) continue;
            out[0] = cur.Values[0];
            out[1] = cur.Values[1];
        } else {
            const int32_t span = cur.Time - prev.Time;
            if (span == 0) continue;
            const int32_t raw = ((frame - prev.Time) << 16) / span;
            const int32_t t = Ease(raw, prev.EaseOut, cur.EaseIn);

            if (prev.Interp == kLinear && cur.Interp == kLinear) {
                for (int v = 0; v < obj.ValueCount; ++v) {
                    const int32_t a = prev.Values[v];
                    out[v] = a + (t >> 5) * ((cur.Values[v] - a) >> 11);
                }
            } else {
                int32_t w[4];
                HermiteBasis(t, w);
                for (int v = 0; v < obj.ValueCount; ++v) {
                    out[v] = (((w[1] >> 6) * (cur.Values[v] >> 6)) >> 4) +
                             (((w[0] >> 6) * (prev.Values[v] >> 6)) >> 4) +
                             (((prev.OutTangent[v] >> 6) * (w[2] >> 6)) >> 4) +
                             (((cur.InTangent[v] >> 6) * (w[3] >> 6)) >> 4);
                }
            }
        }

        switch (obj.Channel) {
            case kChanPos:
                st.X = out[0];
                st.Y = out[1];
                break;
            case kChanRotation:
                st.Rotation = out[0];
                break;
            case kChanSize:
                st.Width = out[0];
                st.Height = out[1];
                break;
            case kChanAlpha:
                st.Alpha = out[0];
                break;
            case kChanCell:
                st.Cell = out[0];
                st.CellStep = out[1];
                break;
            default:
                break;
        }
    }
}

int LapAnimation::NextCell(std::size_t sprite, int frameCount) {
    if (sprite >= m_States.size()) return 0;
    SpriteState& st = m_States[sprite];
    st.Cell += st.CellStep;
    if (frameCount <= 0 || (st.Cell >> 16) >= frameCount) st.Cell = 0;
    return int(st.Cell >> 16);
}

}  // namespace bb
