#include "game/Settings.h"

namespace bb {

void Settings::Reset() {
    SoundOn = true;
    FightAnimation = true;
    CutsceneVolume = kVolumeMax;
    MusicVolume = kVolumeMax;
    SfxVolume = kVolumeMax;
    Frame = kDefaultFrame;
    // Language is deliberately not reset: it is chosen from its own screen and
    // resetting it would strand a player who cannot read the default.
}

void Settings::ResetKeys() {
    // The N-Gage defaults, in the screen's action order. Gamepad support will
    // add a second table alongside this one rather than replacing it.
    m_Bindings = {
        Key::kSelect,   // Primary gaming Key       -- 5
        Key::kSoftLeft, // Secondary gaming Key     -- 7
        Key::kInfo,     // Info Key                 -- 6
        Key::kNextUnit, // Next selection Key       -- 3
        Key::kPrevUnit, // Previous selection Key   -- 1
        Key::kRange,    // Display range Key        -- 4
        Key::kMap,      // Display map Key          -- 2
        Key::kUp, Key::kDown, Key::kLeft, Key::kRight,
    };
}

}  // namespace bb
