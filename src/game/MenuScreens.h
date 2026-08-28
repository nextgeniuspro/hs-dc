// The game's menu screens, as the original lays them out.
//
// Recovered with tools/ghidra_menutree.py, which walks each screen's item list
// and follows the state each item constructs. The original tree, with the
// address of each screen's Run():
//
//   Main menu                 0x1008b764
//     Single-player game      0x100a79b8   Load game / New game / Tutorial
//     Multiplayer game        0x10099818   Hot Seat / Bluetooth / N-Gage Arena
//     N-Gage Arena            (launches the Arena client)
//     Settings                0x100a6598   Ready / Sound settings / Bluetooth
//                                          name / Turn Bluetooth on|off /
//                                          Language / Key configuration /
//                                          Reset settings / Turn Fight
//                                          Animation on|off / Erase game data
//       Sound settings        0x100aafdc   Ready / Sound on|off / Mute when in
//                                          call on|off / three volume ramps
//                                          (mute is dropped, see below)
//       Language              0x100a5b10   English / French / Italian /
//                                          German / Spanish
//       Key configuration     0x10081f40   Ready / Reset key configuration /
//                                          eleven bindable actions
//       Reset settings        0x1006125c   No / Yes
//     Help                    0x1007cb04   Battle view / Travel view / Credits
//     Quit                    0x10060554   Yes / No
//
// Five entries are deliberately dropped from the port, all of them dead
// off-device rather than unimplemented: N-Gage Arena (the service is gone) in
// both the main and multiplayer menus, the Bluetooth name and on/off entries
// (the port has no Bluetooth stack), and "Mute when in call" (there are no
// calls to mute for). Everything else is here.
//
// Two more are present but not offered, and the difference from a dropped row
// is that these can come back:
//
//   * **Multiplayer game** is drawn greyed out (MenuStates.cpp). Dropping the
//     two Bluetooth modes and the Arena leaves Hot Seat, which is not
//     finished, so the row is disabled -- the screen under it still builds and
//     re-enabling it is one argument to `MenuList::Add`.
//   * **Open mission** is the port's own row rather than the original's: it
//     reaches any single-player map without playing the campaign chain to get
//     there. Compiled out unless `BB_DEV` is defined, because it is
//     scaffolding and not game. See CMakeLists.txt.
#pragma once

#include "game/MenuScreen.h"
#include "game/SaveGame.h"

namespace bb {

class SinglePlayerState : public MenuScreenState {
public:
    using MenuScreenState::MenuScreenState;
    const char* Name() const override { return "SinglePlayer"; }

protected:
    std::string Title() const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
};

class MultiplayerState : public MenuScreenState {
public:
    using MenuScreenState::MenuScreenState;
    const char* Name() const override { return "Multiplayer"; }

protected:
    std::string Title() const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
};

class SettingsState : public MenuScreenState {
public:
    using MenuScreenState::MenuScreenState;
    const char* Name() const override { return "Settings"; }

protected:
    std::string Title() const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
    // Leaving by Back writes the settings file, exactly as Ready does.
    void OnBack(StateMachine& sm) override;

public:
    // The original's table has no string for any of this -- it had a screen
    // the size of its window and nothing to put around it -- so these are the
    // port's own ids, kept clear of the localisation range the way
    // MissionBrowserState's are, and their labels are written out.
    static constexpr int kFrameItem = 890000;
};

// Which device frame the host draws either side of the screen. One row per
// installed frame plus "Off", in the shape of the language screen: pick one,
// it takes effect at once, and the list pops back to Settings.
class FrameState : public MenuScreenState {
public:
    using MenuScreenState::MenuScreenState;
    const char* Name() const override { return "Frame"; }

    static constexpr int kOffItem = 890001;
    // Plus the frame's index in the host's list.
    static constexpr int kFirstFrame = 890002;

protected:
    std::string Title() const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
};

class SoundSettingsState : public MenuScreenState {
public:
    using MenuScreenState::MenuScreenState;
    const char* Name() const override { return "SoundSettings"; }

protected:
    std::string Title() const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
    void OnSliderChanged(int id, int value) override;

private:
    // Make the music slider and the master switch heard at once rather than
    // when the screen closes -- start the theme, set its volume, or let it go.
    void ApplyMusicVolume();
};

class LanguageState : public MenuScreenState {
public:
    using MenuScreenState::MenuScreenState;
    const char* Name() const override { return "Language"; }

protected:
    std::string Title() const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
};

class KeyConfigState : public MenuScreenState {
public:
    using MenuScreenState::MenuScreenState;
    const char* Name() const override { return "KeyConfig"; }

protected:
    std::string Title() const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
};

class HelpState : public MenuScreenState {
public:
    using MenuScreenState::MenuScreenState;
    const char* Name() const override { return "Help"; }

protected:
    std::string Title() const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;
};

// Pick a battle. The original reaches its levels through the travel map for
// the campaign and through a map browser for skirmishes; neither is ported
// yet, so this lists one of `MissionDatabase`'s four tables directly and goes
// straight into the battle. It is the port's screen, not the original's, and
// it is the seam the travel map will replace.
class MissionSelectState : public MenuScreenState {
public:
    MissionSelectState(GameContext& ctx, int style, int titleID)
        : MenuScreenState(ctx), m_Style(style), m_TitleID(titleID) {}
    const char* Name() const override { return "MissionSelect"; }

    // Item ids are this plus the mission's position in the list.
    static constexpr int kItemBase = 900000;
    // The row that picks up a battle saved from this mode, offered above the
    // list when there is one.
    static constexpr int kResumeItem = 899999;

    // Which save file a battle picked here belongs in.
    static SaveKind KindOf(int style);

protected:
    std::string Title() const override;
    void Build(MenuList& list) override;
    bool OnChosen(int id, StateMachine& sm) override;

private:
    int m_Style;
    int m_TitleID;
};

}  // namespace bb
