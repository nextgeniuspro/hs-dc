// LapAnimation — RedLynx ".lap" cutscene format (loader at 0x10000334).
//
// Every cutscene in the game is one `Data\anim\<name>.lap` plus four sidecar
// text files, and they are all the same thing: a handful of still paintings
// moved around by a keyframed camera. There is no video anywhere in the game.
//
//   Data\anim\<name>.lap        this file -- sprites, keyframes, effects
//   Data\anim\sub\<name>.dat    who speaks and which string id they say
//   Data\anim\sub\<name>.sub    when each subtitle appears, and for how long
//   Data\anim\snd\<name>.dat    the sound bank to load
//   Data\anim\snd\<name>.snd    the sound cue timeline
//
// Container (little-endian throughout, fixed point is 16.16 unless noted):
//
//   u16 signature, u16 ~signature       version, as major.minor in the bytes
//   Header, 28 bytes:
//     u32 fileSize        always 0 in the shipped files
//     u16 duration        length in frames
//     u16 fps             frames per second (21 in every shipped cutscene)
//     u16 viewAreaX, viewAreaY, viewAreaWid, viewAreaHei
//     u16 imageAmount, audioAmount, objectAmount, spriteAmount, fxAmount
//     u8  compression     0 in every shipped file
//   spriteAmount x 48-byte sprite records
//   imageAmount  x u16-length-prefixed texture paths (no terminator)
//   objectAmount x { 8-byte header, keyframeAmount x 36-byte keyframes }
//   fxAmount     x 16-byte effect records
//   u16-length-prefixed name of the animation to chain to ("" = none)
//
// Verified by parsing all 32 shipped cutscenes: every one is consumed exactly,
// with zero bytes left over.
//
// The coordinate system is the one thing that will trip you up: positions are
// the *centre* of the sprite, x grows right but **y grows up from the bottom**
// of the view area (the renderer draws at `viewAreaHei - y`, 0x100027c8).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bb {

class FilePack;

class LapAnimation {
public:
    static constexpr int32_t kOne = 1 << 16;  // 1.0 in 16.16

    // Which sprite field an object animates. 0x10001148 picks the target by
    // *byte offset* into the sprite object, which is how the last two channels
    // come out where they do -- and why the exporter's field names are
    // misleading: what the format calls a sprite's "frame" is its rotation,
    // and what it calls its rotation and scale are the two halves of the
    // cel-animation counter. Channel 4 exists but no shipped cutscene uses it.
    enum Channel : uint8_t {
        kChanPos = 0,        // 2 values: centre x, y      (+0x1c)
        kChanRotation = 1,   // 1 value: degrees, 16.16    (+0x2c)
        kChanSize = 2,       // 2 values: on-screen w, h   (+0x24)
        kChanAlpha = 3,      // 1 value: 0..255            (+0x30)
        kChanCell = 4,       // 2 values: cel counter and step (+0x34/+0x38)
    };

    // How to get from the previous keyframe to this one (0x10001210). The
    // counters the debug overlay prints call 0 "Anim: Linear" and 1
    // "Anim: Splines"; 2 holds the previous value and snaps on the exact frame.
    enum Interp : int32_t { kLinear = 0, kSpline = 1, kStep = 2 };

    // Which image class the engine instantiates for a sprite (0x10002538).
    // The debug log names them "Basic", "Adv" and "Zoom".
    enum SpriteKind : uint8_t {
        kBasic = 0,     // blitted 1:1; every kBasic sprite's size is its
                        // texture's own size
        kAdvanced = 1,  // transformable, stored square-padded
        kZoom = 2,      // transformable, stored linearly for fast scaling
    };

    // 16-byte effect record. Only kinds 0 and 3 appear in shipped cutscenes.
    enum FxKind : int32_t {
        kFxFade = 0,      // cross-fade the whole screen to/from `colour`
        kFxUnknown1 = 1,  // 0x100e6c08; 6 uses, none in a reachable cutscene
        kFxUnknown2 = 2,  // 0x100e708c; 1 use
        kFxOptimize = 3,  // toggles the device's half-screen update hack
    };

    struct Fx {
        int32_t Time = 0;    // frame it fires on
        int32_t Kind = 0;
        int32_t Param = 0;   // kFxFade: -1.0 fades in, +1.0 fades out
        int32_t Colour = 0;  // kFxFade: the ARGB4444 colour faded to/from
    };

    struct Keyframe {
        int32_t Interp = kLinear;
        int32_t Time = 0;      // in frames
        int32_t Tension = 0;   // the three spline shape controls; only
        int32_t Bias = 0;      // `tension` is ever non-zero in a shipped
        int32_t Continuity = 0;  // cutscene, and only once
        int32_t EaseIn = 0;   // fraction of the segment spent accelerating
        int32_t EaseOut = 0;  // ... and decelerating
        int32_t Values[2] = {0, 0};
        // Hermite tangents, derived from the neighbours by Prepare().
        int32_t OutTangent[2] = {0, 0};
        int32_t InTangent[2] = {0, 0};
    };

    struct Object {
        uint16_t Sprite = 0;
        uint8_t Channel = kChanPos;
        uint16_t ValueCount = 0;  // how many of Keyframe::values are live
        std::vector<Keyframe> Keys;
    };

    struct Sprite {
        uint16_t Image = 0;
        uint8_t Kind = kBasic;
        uint8_t Filter = 1;  // 0 selects bilinear sampling, 1 nearest
        int32_t X = 0, Y = 0;          // centre, y measured up from the bottom
        int32_t Width = 0, Height = 0;  // on-screen size in pixels
        // Degrees, 16.16. Only kAdvanced and kZoom sprites can turn: the plain
        // texture class's setRotation is `bx lr` (0x100ffe20), so a kBasic
        // sprite's value is simply ignored. The port does not rotate at all
        // yet, which is the one thing a cutscene can ask for and not get.
        int32_t Rotation = 0;
        int32_t Alpha = 255 * kOne;
        // The cel counter's seed and its step, in 16.16 frames per rendered
        // frame (0x100e7398, 0x100e73c4). The seed is shifted left another
        // sixteen places on load, which in practice always overflows to zero;
        // the step is what actually animates a multi-frame texture, and it is
        // 1.0 unless the exporter was told otherwise -- the three gulls in
        // 02-Caribbean beat their wings at 0.4, 0.2 and 0.3 frames a tick.
        int32_t CellSeed = 0;
        int32_t CellStep = kOne;
        int32_t PivotX = 0, PivotY = 0;  // origin offset inside the texture
    };

    // Live per-sprite values, rewritten every frame by Evaluate().
    struct SpriteState {
        bool Visible = false;
        int32_t X = 0, Y = 0, Width = 0, Height = 0;
        int32_t Rotation = 0, Alpha = 255 * kOne;
        int32_t Cell = 0, CellStep = kOne;
    };

    // Read "Data\anim\<name>.lap" from the pak. Returns false if it is missing
    // or malformed; a partially read file leaves the object empty.
    bool Load(FilePack& pack, const std::string& name);

    // "Data\anim\<name>.lap" -- exposed because the sidecars are built the
    // same way, from the same name.
    static std::string PathFor(const std::string& name);

    bool Valid() const { return !m_Sprites.empty(); }
    int Duration() const { return m_Duration; }
    int Fps() const { return m_Fps; }
    int ViewWidth() const { return m_ViewW; }
    int ViewHeight() const { return m_ViewH; }
    const std::string& Next() const { return m_Next; }

    const std::vector<Sprite>& Sprites() const { return m_Sprites; }
    const std::vector<std::string>& Images() const { return m_Images; }
    const std::vector<Object>& Objects() const { return m_Objects; }
    const std::vector<Fx>& Effects() const { return m_Fx; }

    // Rewind to frame 0: reset every sprite to its record's values and put
    // every object's keyframe cursor back to the start.
    void Reset();

    // Advance every object to `frame` and rewrite the sprite states. Must be
    // called once per frame in order, as the original does -- objects carry a
    // cursor that only moves forward.
    void Evaluate(int frame);

    const std::vector<SpriteState>& States() const { return m_States; }

    // Advance one sprite's cel counter and return the frame to draw, exactly
    // as 0x100e73c4 does: add the step, wrap to zero the moment the integer
    // part reaches the texture's frame count, and take the integer part. Call
    // it once per rendered frame per *visible* sprite, in draw order.
    int NextCell(std::size_t sprite, int frameCount);

    // --- interpolation internals, exposed for tests -----------------------

    // 0x10001a7c. Reshapes a 0..1.0 segment position so the first `EaseOut`
    // of it accelerates and the last `EaseIn` decelerates.
    static int32_t Ease(int32_t t, int32_t easeOut, int32_t easeIn);

    // 0x10001494. Cubic Hermite basis at `t`: weights for the previous value,
    // the next value, the previous out-tangent and the next in-tangent.
    static void HermiteBasis(int32_t t, int32_t out[4]);

private:
    // 0x100014e8. Derive every spline keyframe's tangents from its neighbours.
    static void Prepare(Object& obj);

    int m_Duration = 0;
    int m_Fps = 0;
    int m_ViewX = 0, m_ViewY = 0, m_ViewW = 0, m_ViewH = 0;
    std::string m_Next;
    std::vector<Sprite> m_Sprites;
    std::vector<std::string> m_Images;
    std::vector<Object> m_Objects;
    std::vector<Fx> m_Fx;

    // Playback state.
    std::vector<SpriteState> m_States;
    std::vector<int> m_Cursors;  // one keyframe cursor per object
};

}  // namespace bb
