// Settings — what the settings screens read and write.
//
// The original keeps these on the object in engine resource slot 0x18 (the one
// the startup loader creates at 0x10078f50 and whose +0x10 holds the language
// index). The settings screens toggle bytes on it directly: +0x04 mute-when-in-
// call (dropped here), +0x05 sound on, +0x06 a volume index, +0x09 a flag the
// main menu sets.
//
// Volumes run 0..5 — the slider widget (0x100d6000) sets its maximum to 5.
//
// The eleven bindable actions are the ones the key configuration screen lists,
// ids 1506..1516; the port keeps the same order so a saved binding table would
// map straight across.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "game/Boot.h"  // Language
#include "platform/Host.h"

namespace bb {

class Settings {
public:
    static constexpr int kVolumeMax = 5;  // the slider's own maximum
    static constexpr int kActionCount = 11;

    // The device frame drawn either side of the screen (see Host::SetFrame).
    // The port's own setting -- the original had a screen the size of its
    // window and nothing to put around it.
    static constexpr int kFrameIdMax = 16;
    static constexpr const char* kDefaultFrame = "ngage-qd";

    // In the order the key configuration screen lists them (string ids
    // 1506..1516): primary, secondary, info, next, previous, range, map, then
    // the four directions.
    enum Action : int {
        kPrimary = 0, kSecondary, kInfo, kNextSelection, kPrevSelection,
        kDisplayRange, kDisplayMap, kUp, kDown, kLeft, kRight,
    };

    bool SoundOn = true;
    bool FightAnimation = true;
    // The original also carries a mute-when-in-call flag at +0x04. There are no
    // calls to be interrupted by off-device, so the port drops both the setting
    // and its menu entry.
    int CutsceneVolume = kVolumeMax;
    int MusicVolume = kVolumeMax;
    int SfxVolume = kVolumeMax;
    Language CurrentLanguage = Language::kEn;
    // Which frame, by id; "" is none. The id rather than a position in the
    // host's list, because that position means a different frame the moment
    // one is installed or removed and this is what goes in the settings file.
    std::string Frame = kDefaultFrame;

    Key Binding(Action a) const { return m_Bindings[static_cast<int>(a)]; }
    void Bind(Action a, Key k) { m_Bindings[static_cast<int>(a)] = k; }

    // Everything back to defaults, as "Reset settings" does. Key bindings are
    // separate -- the original resets those from their own menu entry.
    void Reset();
    void ResetKeys();

    Settings() { ResetKeys(); }

private:
    std::array<Key, kActionCount> m_Bindings{};
};

}  // namespace bb
