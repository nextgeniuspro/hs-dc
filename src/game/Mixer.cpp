#include "game/Mixer.h"

#include "game/SpcAudio.h"

namespace bb {
namespace {

// 0x100964d4's constants. The magic multiply is the compiler's own division
// idiom; it is reproduced verbatim rather than rewritten as a rational, so
// the knee bends exactly where the original's does.
constexpr int64_t kKneeMagic = 962184001;
constexpr int kKneeScale = 0xeb7;

}  // namespace

int16_t Mixer::Limit(int v) {
    if (v > kLimit) {
        const int t = (v - kLimit) * kKneeScale;
        v = static_cast<int>((kKneeMagic * t) >> 45) - (t >> 31) + kLimit;
    } else if (v < -kLimit) {
        const int t = (-kLimit - v) * kKneeScale;
        v = -(static_cast<int>((kKneeMagic * t) >> 45) - (t >> 31) + kLimit);
    }
    // DELIBERATE DIVERGENCE. The knee only gains about a tenth on the
    // overshoot, so it keeps two full-scale voices inside 16-bit range but not
    // four: 4 * 32767 comes out at 39514. The original stores that straight
    // into a short and wraps it to a loud negative click. Clamping is strictly
    // better and cannot be told apart anywhere the original stayed in range.
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return static_cast<int16_t>(v);
}

Mixer::Voice* Mixer::Find(Handle h) {
    if (h == kNoHandle) return nullptr;
    for (Voice& v : m_Voices)
        if (v.ID == h && v.Sound) return &v;
    return nullptr;
}

const Mixer::Voice* Mixer::Find(Handle h) const {
    return const_cast<Mixer*>(this)->Find(h);
}

// 0x10096880: take a free slot, else steal the oldest -- stopping it first,
// which is what "XXX removing %s" in the original's log is announcing.
Mixer::Handle Mixer::Play(const SpcSound& sound, int volume, bool loop) {
    if (!sound.Valid()) return kNoHandle;
    if (volume < 0) volume = 0;

    Voice* slot = nullptr;
    for (Voice& v : m_Voices) {
        if (!v.Sound) {
            slot = &v;
            break;
        }
    }
    if (!slot) {
        slot = &m_Voices[0];
        for (Voice& v : m_Voices)
            if (v.Age < slot->Age) slot = &v;
    }

    slot->Sound = &sound;
    slot->Pos = 0;
    slot->Volume = volume;
    slot->Loop = loop;
    slot->ID = m_NextHandle++;
    slot->Age = m_NextAge++;
    slot->Fading = false;
    slot->FadeLeft = kFadeBlock;
    // Whatever this slot was playing before, the block it has in hand belongs
    // to that sound and not this one.
    slot->BlockIndex = kNoBlock;
    return slot->ID;
}

void Mixer::Stop(Handle h) {
    if (Voice* v = Find(h)) {
        v->Sound = nullptr;
        v->ID = kNoHandle;
    }
}

void Mixer::StopAll() {
    for (Voice& v : m_Voices) {
        v.Sound = nullptr;
        v.ID = kNoHandle;
    }
}

void Mixer::StopSource(const SpcSound& sound) {
    for (Voice& v : m_Voices) {
        if (v.Sound != &sound) continue;
        v.Sound = nullptr;
        v.ID = kNoHandle;
    }
}

// 0x100ac708 clears the fading flag as well as writing the volume, so setting
// a volume outright always wins over a ramp that is still running.
void Mixer::SetVolume(Handle h, int volume) {
    if (Voice* v = Find(h)) {
        v->Volume = volume < 0 ? 0 : volume;
        v->Fading = false;
    }
}

// 0x100ac6d0: remember where we are heading and how fast, and let the block
// loop get there. `step` is per block, not per second.
void Mixer::Fade(Handle h, int step, int target) {
    Voice* v = Find(h);
    if (!v) return;
    if (step < 1) step = 1;
    v->Fading = true;
    v->FadeStep = step;
    v->FadeTarget = target < 0 ? 0 : target;
    v->FadeLeft = kFadeBlock;
}

void Mixer::FadeSource(const SpcSound& sound, int step, int target) {
    if (step < 1) step = 1;
    for (Voice& v : m_Voices) {
        if (v.Sound != &sound) continue;
        v.Fading = true;
        v.FadeStep = step;
        v.FadeTarget = target < 0 ? 0 : target;
        v.FadeLeft = kFadeBlock;
    }
}

void Mixer::SetAllVolumes(int volume) {
    for (Voice& v : m_Voices) {
        if (!v.Sound) continue;
        v.Volume = volume < 0 ? 0 : volume;
        v.Fading = false;
    }
}

// The ramp out of 0x100ac46c, verbatim: close the gap by a step, snap and
// finish once the remainder is inside one, and treat arriving at silence as a
// stop rather than as a voice sitting there at zero.
bool Mixer::StepFade(Voice& v) {
    if (v.Volume < v.FadeTarget - v.FadeStep) {
        v.Volume += v.FadeStep;
    } else if (v.FadeTarget + v.FadeStep < v.Volume) {
        v.Volume -= v.FadeStep;
    } else {
        v.Volume = v.FadeTarget;
        v.Fading = false;
    }
    if (v.Volume == 0) {
        v.Sound = nullptr;
        v.ID = kNoHandle;
        v.Fading = false;
        return false;
    }
    return true;
}

bool Mixer::Playing(Handle h) const { return Find(h) != nullptr; }

int Mixer::ActiveVoices() const {
    int n = 0;
    for (const Voice& v : m_Voices)
        if (v.Sound) ++n;
    return n;
}

// The one block of a voice's sound that is decoded at any moment. A sound is
// held compressed, so this is where the samples actually come from -- the
// equivalent of 0x100ac46c pulling the next block when the sample loop has
// used up the last.
bool Mixer::FetchBlock(Voice& v, std::size_t pos) {
    const std::size_t bs = v.Sound->BlockSamples();
    if (bs == 0) return false;
    const std::size_t want = pos / bs;
    if (want == v.BlockIndex && !v.Block.empty()) return true;
    if (v.Block.size() != bs) v.Block.assign(bs, 0);
    if (v.Sound->DecodeBlock(want, v.Block.data()) == 0) return false;
    v.BlockIndex = want;
    return true;
}

// 0x1009659c: clear the accumulator, let every voice add itself, then hand
// the block to the limiter.
void Mixer::Render(int16_t* out, int samples) {
    if (!out || samples <= 0) return;
    m_Accum.assign(std::size_t(samples), 0);

    for (Voice& v : m_Voices) {
        if (!v.Sound) continue;
        const std::size_t count = v.Sound->Count();
        for (int i = 0; i < samples; ++i) {
            // The ramp advances on block boundaries, which is where the
            // original's lives -- and a ramp that reaches zero takes the voice
            // with it, so this has to be able to end the voice mid-block.
            if (v.Fading && v.FadeLeft <= 0) {
                v.FadeLeft = kFadeBlock;
                if (!StepFade(v)) break;
            }
            --v.FadeLeft;
            if (v.Pos >= count) {
                if (!v.Loop) {
                    // A voice that runs out frees its slot, as the original
                    // does when the per-voice render returns false.
                    v.Sound = nullptr;
                    v.ID = kNoHandle;
                    break;
                }
                v.Pos = 0;
            }
            // Pull the block this sample is in, if it is not the one already
            // in hand. Sequential playback crosses a boundary once every 2048
            // samples and loops back to block zero, so this is one decode per
            // quarter second per voice.
            if (!FetchBlock(v, v.Pos)) {
                v.Sound = nullptr;
                v.ID = kNoHandle;
                break;
            }
            const std::size_t at = v.Pos % v.Sound->BlockSamples();
            m_Accum[std::size_t(i)] += (int(v.Block[at]) * v.Volume) >> 8;
            ++v.Pos;
        }
    }

    for (int i = 0; i < samples; ++i) out[i] = Limit(m_Accum[std::size_t(i)]);
}

}  // namespace bb
