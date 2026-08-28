// AudioKeepalive — keeps the sound stream running across a call that blocks
// the game thread.
//
// This machine's audio is pulled rather than pushed: the AICA plays out of a
// buffer KOS refills from `snd_stream_poll`, and the samples that go into it
// are mixed by the game itself (SoundManager::Pump). Both ends run on the game
// thread -- Flip() and Sleep() turn the crank -- which is right for as long as
// the game thread keeps coming back round.
//
// Writing a save is where it does not. A memory card is flash on the far end
// of the maple bus: KOS buffers the file in RAM and commits it in `fs_close`,
// a 512-byte block at a time, each block a bus exchange plus the card's own
// write cycle. Seventeen blocks is a second or two inside one call. Nothing
// polls the stream for the whole of it, so the AICA reaches the end of the
// last buffer it was given and plays it again, and again -- which is the
// stutter a player hears when they save.
//
// So for as long as one of these is alive, a KOS thread turns the crank
// instead: mix, poll, sleep a few milliseconds, repeat. That works because the
// game thread is *asleep* in the driver the entire time -- KOS's VMU code
// waits on genwait and thd_sleep, both of which yield the CPU -- so the two
// threads never reach the mixer at once, and the guard joins its thread before
// it returns, which hands the mixer back before the game can touch it again.
//
// It is deliberately not a general audio thread. The mixer has no locking and
// wants none: it is called from every screen in the game. This borrows it for
// the length of one call that the game thread is known to be blocked inside,
// and gives it straight back.
#pragma once

#include <functional>

namespace bb {

class AudioKeepalive {
public:
    // One turn of the crank: mix what the game owes and hand it to the AICA.
    // KosHost::PumpAudio is the one that matters; an empty function is fine
    // and means no thread is started at all, which is what a host with no
    // sound open gets.
    using Tick = std::function<void()>;

    // `tick` is borrowed, not copied: a guard is a local in the call it is
    // protecting and never outlives the object holding the function.
    explicit AudioKeepalive(const Tick& tick, int periodMs = kPeriodMs);
    ~AudioKeepalive();

    AudioKeepalive(const AudioKeepalive&) = delete;
    AudioKeepalive& operator=(const AudioKeepalive&) = delete;

    // Whether a thread is actually running. False when there was nothing to
    // call or the thread could not be created -- in which case the caller
    // simply blocks the way it always did.
    bool Running() const { return m_Thread != nullptr; }

    // Twelve turns inside the AICA's 256 ms buffer and twenty inside the
    // 128 ms the mixer keeps ahead: often enough that neither can run dry,
    // rare enough that the thread costs nothing next to a card write.
    static constexpr int kPeriodMs = 10;

private:
    static void* Run(void* self);

    const Tick* m_Tick;
    int m_PeriodMs;
    // Written by the game thread in the destructor, read by the keepalive
    // thread; volatile rather than atomic because this is one CPU with no
    // store buffer to flush, and the only thing that has to survive the
    // compiler is the load in the loop condition.
    volatile bool m_Stop = false;
    void* m_Thread = nullptr;  // kthread_t*, kept opaque as KosHost keeps its
                              // pvr pointers -- so this header stays portable
};

}  // namespace bb
