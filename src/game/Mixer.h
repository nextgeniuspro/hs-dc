// Mixer — the game's own software mixer (sound manager, resource slot 0xd6).
//
// main.dll imports exactly one function from MEDIACLIENTAUDIOSTREAM, which
// tells you the shape of this before you read a line of it: the game mixes
// everything itself and pushes one PCM stream at the OS. That makes it a
// clean thing to port -- the mixer is platform-independent and the host only
// has to swallow finished blocks.
//
// Reversed from 0x100963dc (construction), 0x10096880 (voice allocation),
// 0x1009659c (render a block) and 0x100964d4 (limit and convert).
//
// Shape, all of it the original's:
//
//   * Four voices. A fifth Play() steals the oldest, stopping it first.
//   * A block is 512 samples. At 8 kHz that is 64 ms, which is the latency
//     the game was built around.
//   * Voices accumulate into an int32 buffer, so they sum without clipping
//     against each other.
//   * The block is then soft-limited: anything past +-29000 is compressed
//     rather than clamped, so four loud voices distort instead of crackling.
//
// The port keeps all of that. What it does not keep is the original's trick
// of folding each voice's volume into its ADPCM codebook at block-load time
// -- decoded samples are stored at unity and scaled here instead, which is
// equivalent at constant volume and lets a change take effect immediately
// rather than up to 2048 samples later.
#pragma once

#include <cstdint>
#include <vector>

namespace bb {

class SpcSound;

class Mixer {
public:
    static constexpr int kRate = 8000;         // the only rate the game uses
    static constexpr int kBlockSamples = 512;  // 0x1009653c + 1
    static constexpr int kVoices = 4;          // 0x10096880 walks four slots
    static constexpr int kUnitVolume = 256;    // the mix shifts back down by 8
    static constexpr int kLimit = 29000;       // 0x10096530

    // A handle names one playing voice. Handles are never reused while the
    // voice is alive, so a stale one is simply inactive rather than wrong.
    using Handle = int;
    static constexpr Handle kNoHandle = -1;

    // Start `sound` at `volume` (0..kUnitVolume). Returns a handle, stealing
    // the oldest voice if all four are busy.
    Handle Play(const SpcSound& sound, int volume = kUnitVolume,
                bool loop = false);

    void Stop(Handle h);
    void StopAll();

    // How often a fade takes a step: one per decoded block. The original's
    // ramp lives in the codec, not the mixer -- 0x100ac46c moves the voice's
    // volume one `step` toward its target each time it loads a block, so the
    // rate is a block, never a frame. A block is `blockSamples` from the .spc
    // header, which is 2048 in every shipped file.
    static constexpr int kFadeBlock = 2048;

    // Walk `h`'s volume toward `target` by `step` every kFadeBlock samples.
    // Reaching zero stops the voice, exactly as 0x100ac46c does -- which is
    // what makes "fade to 0" a stop and not a silent voice holding a slot.
    void Fade(Handle h, int step, int target);
    // Set every live voice's volume, which is all 0x10096b10 does: it walks
    // the four slots and calls each one's SetVolume. The engine uses it to
    // duck whatever is playing when a battle or a briefing takes the screen.
    void SetAllVolumes(int volume);

    // Stop every voice playing `sound`. The sound manager calls this before it
    // frees decoded audio: a voice holds a bare pointer into it, and on a
    // console where a cutscene's music is a megabyte, freeing it the moment
    // the scene ends is the difference between playing on and running out.
    void StopSource(const SpcSound& sound);
    // Ramp every voice playing `sound`. The engine addresses a fade by (bank,
    // index) rather than by voice -- 0x100813d0 looks the Spanc up and ramps
    // it -- and since a Spanc is one object shared by every play of it, that
    // is the same thing as "whatever is playing this sample".
    void FadeSource(const SpcSound& sound, int step, int target);
    void SetVolume(Handle h, int volume);
    bool Playing(Handle h) const;
    int ActiveVoices() const;

    // Mix `samples` frames into `out` (signed 16-bit mono at kRate). Any
    // count works; the engine's own block is kBlockSamples.
    void Render(int16_t* out, int samples);

    // 0x100964d4's soft knee. Exposed for tests.
    static int16_t Limit(int v);

private:
    struct Voice {
        const SpcSound* Sound = nullptr;
        std::size_t Pos = 0;
        int Volume = kUnitVolume;
        bool Loop = false;
        Handle ID = kNoHandle;
        int Age = 0;  // 0x10096838's monotonic stamp, for stealing
        // The codec's ramp, hoisted here because this port decodes up front
        // and so has no block loader to hang it off. Semantics are 0x100ac46c's
        // to the letter, including that the step is taken per block rather
        // than per sample or per frame.
        bool Fading = false;
        int FadeStep = 0;
        int FadeTarget = 0;
        int FadeLeft = kFadeBlock;  // samples until the next step
        // The one block of this voice's sound that is currently decoded, and
        // which block it is. The sound itself is held compressed; a voice pulls
        // the block it needs and no more, which is what 0x100ac46c does when
        // the sample loop runs off the end of the last one.
        std::vector<int16_t> Block;
        std::size_t BlockIndex = kNoBlock;
    };

    static constexpr std::size_t kNoBlock = std::size_t(-1);
    // Make sure `v.block` holds the block containing `pos`. False if there is
    // nothing there to play.
    static bool FetchBlock(Voice& v, std::size_t pos);

    // One ramp step on `v`. Returns false when the fade reached zero and the
    // voice was stopped.
    bool StepFade(Voice& v);

    Voice* Find(Handle h);
    const Voice* Find(Handle h) const;

    Voice m_Voices[kVoices];
    std::vector<int32_t> m_Accum;
    Handle m_NextHandle = 1;
    int m_NextAge = 0;
};

}  // namespace bb
