#include "platform/AudioKeepalive.h"

#include <kos/thread.h>

#include "shim/Log.h"

namespace bb {

AudioKeepalive::AudioKeepalive(const Tick& tick, int periodMs)
    : m_Tick(&tick), m_PeriodMs(periodMs > 0 ? periodMs : 1) {
    if (!tick) return;  // nothing to call: the caller blocks as it always did
    // Everything the thread reads is set above, because thd_create can hand it
    // the CPU before it returns.
    m_Thread = thd_create(false, &AudioKeepalive::Run, this);
    if (!m_Thread) LogError("audio: no keepalive thread; sound will stall\n");
}

AudioKeepalive::~AudioKeepalive() {
    if (!m_Thread) return;
    m_Stop = true;
    // Joined, not detached: the mixer goes back to belonging to the game
    // thread at this line and not a moment later.
    thd_join(static_cast<kthread_t*>(m_Thread), nullptr);
}

void* AudioKeepalive::Run(void* arg) {
    AudioKeepalive* self = static_cast<AudioKeepalive*>(arg);
    while (!self->m_Stop) {
        (*self->m_Tick)();
        thd_sleep(static_cast<unsigned>(self->m_PeriodMs));
    }
    return nullptr;
}

}  // namespace bb
