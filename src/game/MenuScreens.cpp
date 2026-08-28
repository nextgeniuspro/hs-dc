#include "game/MenuScreens.h"

#include <cstring>

#include "game/BattleScreen.h"
#include "game/CampaignStates.h"
#include "game/CardPicker.h"
#include "game/Font.h"
#include "game/Game.h"
#include "game/MenuIds.h"
#include "game/MenuList.h"
#include "game/MissionDatabase.h"
#include "game/SaveGame.h"
#include "game/Settings.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "platform/Host.h"
#include "platform/Storage.h"
#include "shim/Log.h"

namespace bb {
namespace {

// Leaves that go on into game code the port hasn't reached. Saying so beats a
// dead button, and each one names what is actually missing.
constexpr const char* kNoHelpPages =
    "Help pages are not ported yet.";
constexpr const char* kNoCredits =
    "The credits screen is not ported yet.";

// Language order matches the binary's table: 0 EN, 1 FR, 2 IT, 3 GE, 4 SP.
struct LanguageEntry {
    int ID;
    Language Lang;
};
constexpr LanguageEntry kLanguages[] = {
    {ids::kLangEnglish, Language::kEn}, {ids::kLangFrench, Language::kFr},
    {ids::kLangItalian, Language::kIt}, {ids::kLangGerman, Language::kGe},
    {ids::kLangSpanish, Language::kSp},
};

// Short name for a bound key. The action names are long ("Secondary gaming
// Key") and the original gives each its own text row above the button rather
// than sharing a line, so these stay terse to fit both on one board.
const char* KeyName(Key k) {
    switch (k) {
        case Key::kUp: return "Up";
        case Key::kDown: return "Dn";
        case Key::kLeft: return "Lf";
        case Key::kRight: return "Rt";
        case Key::kSelect: return "5";
        case Key::kSoftLeft: return "7";
        case Key::kSoftRight: return "#";
        case Key::kBack: return "C";
        // The five battlefield keys, by the digit the keypad had them on
        // (0x10079070's table).
        case Key::kInfo: return "6";
        case Key::kNextUnit: return "3";
        case Key::kPrevUnit: return "1";
        case Key::kRange: return "4";
        case Key::kMap: return "2";
        case Key::kCount: break;
    }
    return "-";
}

// "Off" reads better than an empty row, and it is what the settings list shows
// against the Frame entry when no frame is on.
constexpr const char* kFrameOff = "Off";

// The label for whatever frame is up now. Asked of the host rather than
// matched against the setting, because a settings file can name a frame whose
// art is not installed -- that keeps its name in the file, but there is
// nothing to show for it here.
std::string FrameLabel(GameContext& ctx) {
    const char* id = ctx.HostRef.Frame();
    for (int i = 0; i < ctx.HostRef.FrameCount(); ++i)
        if (std::strcmp(ctx.HostRef.FrameId(i), id) == 0)
            return ctx.HostRef.FrameLabel(i);
    return kFrameOff;
}

void PushNotice(GameContext& ctx, StateMachine& sm, int titleID,
                const char* message) {
    sm.Push(std::make_unique<NoticeState>(ctx, titleID, message));
}

// Settings go to their own file, as they do in the original (0x100796f0
// writes settings.cfg, separately from any saved game). Written once, on the
// way out of the settings screen, rather than on every keystroke: the target
// is flash on a memory card, where a write costs an erase cycle and a slider
// dragged from 0 to 5 would otherwise cost six of them.
void PersistSettings(GameContext& ctx) {
    Storage* store = ctx.HostRef.Saves();
    if (!store) return;
    const SaveStatus s = WriteSettings(*store, ctx.SettingsRef);
    if (s != SaveStatus::kOk)
        LogError("settings: not saved -- %s\n", SaveStatusText(s));
}

}  // namespace

// --- Single-player game (0x100a79b8) ----------------------------------------

std::string SinglePlayerState::Title() const {
    return m_Ctx.StringsRef.Get(ids::kSinglePlayerTitle);
}

void SinglePlayerState::Build(MenuList& list) {
    // Greyed out when there is nothing to load, which is also how it reads on
    // a host with no storage at all. On a host that keeps saves on memory
    // cards the row stays live while there is a card that might hold one --
    // which of them does is what the picker is for. See CardPicker.h.
    const bool haveSave = CanLoad(m_Ctx, SaveKind::kCampaign);
    list.Add(m_Ctx.StringsRef.Get(ids::kLoadGame), ids::kLoadGame, haveSave);
    list.Add(m_Ctx.StringsRef.Get(ids::kNewGame), ids::kNewGame);
#ifdef BB_DEV
    // Not one of the original's rows: it is the port's way into any
    // single-player map without playing the campaign chain to reach it, which
    // is scaffolding rather than game. A build someone plays should offer the
    // game, so it is compiled out unless BB_DEV is on (see CMakeLists.txt).
    list.Add(m_Ctx.StringsRef.Get(ids::kMissionBrowser), ids::kMissionBrowser);
#endif
    list.Add(m_Ctx.StringsRef.Get(ids::kTutorial), ids::kTutorial);
}

bool SinglePlayerState::OnChosen(int id, StateMachine& sm) {
    switch (id) {
        // Load game: the campaign's own slot, the only one this entry can
        // mean. A save taken mid-battle carries the battle with it and the
        // campaign state drops straight back into it; one taken between
        // missions comes up on the chart.
        case ids::kLoadGame: {
            // Which card, on a machine that has more than one place to look.
            // No "play without one" here: there is nothing to load without a
            // card, so the only answers are a card or Cancel.
            if (AsksForCard(m_Ctx)) {
                switch (PickCard(m_Ctx, /*allowNone=*/false)) {
                    case CardChoice::kQuit:
                        sm.Quit();
                        return true;
                    case CardChoice::kPicked:
                        break;
                    default:
                        // Backed out: stay here, and rebuild, since a card may
                        // have come or gone while the board was up.
                        return false;
                }
            }
            Storage* store = m_Ctx.HostRef.Saves();
            if (!store) {
                PushNotice(m_Ctx, sm, id,
                           SaveStatusText(SaveStatus::kNoStorage));
                break;
            }
            SavedGame game;
            const SaveStatus status =
                ReadGame(*store, SaveKind::kCampaign, game);
            if (status != SaveStatus::kOk) {
                PushNotice(m_Ctx, sm, id, SaveStatusText(status));
                break;
            }
            m_Ctx.CampaignData = game.CampaignData;
            std::unique_ptr<SavedBattle> resume;
            if (game.InBattle)
                resume = std::make_unique<SavedBattle>(game.Battle);
            sm.Push(std::make_unique<CampaignState>(m_Ctx, std::move(resume)));
            break;
        }
        // The campaign proper: name a commander, take a perk, and the mission
        // chain follows (0x100a79b8's case 2).
        case ids::kNewGame:
            // Where this campaign is going to live, asked before it starts
            // rather than at the first checkpoint -- a player who has no card
            // should find that out now, not after winning a mission. Declining
            // is a real answer: the game runs and saves nothing until they ask
            // for a card from the Save row.
            if (AsksForCard(m_Ctx)) {
                switch (PickCard(m_Ctx, /*allowNone=*/true)) {
                    case CardChoice::kQuit:
                        sm.Quit();
                        return true;
                    case CardChoice::kCancelled:
                        return false;
                    default:
                        break;  // a card, or deliberately none
                }
            }
            sm.Push(std::make_unique<NewGameState>(m_Ctx));
            break;
#ifdef BB_DEV
        // Not in the original's tree -- the port keeps a way to jump straight
        // into any single-player map, which the campaign chain does not give.
        // Development builds only; see Build().
        case ids::kMissionBrowser:
            sm.Push(std::make_unique<MissionSelectState>(
                m_Ctx, MissionDatabase::kSingle, ids::kNewGame));
            break;
#endif
        case ids::kTutorial:
            sm.Push(std::make_unique<MissionSelectState>(
                m_Ctx, MissionDatabase::kTutorial, ids::kTutorial));
            break;
        default: return false;
    }
    return true;
}

// --- Multiplayer game (0x10099818) ------------------------------------------

std::string MultiplayerState::Title() const {
    return m_Ctx.StringsRef.Get(ids::kMultiplayerTitle);
}

void MultiplayerState::Build(MenuList& list) {
    // Hot Seat only. Bluetooth needs a stack the port doesn't have, and the
    // N-Gage Arena service no longer exists.
    list.Add(m_Ctx.StringsRef.Get(ids::kHotSeat), ids::kHotSeat);
}

bool MultiplayerState::OnChosen(int id, StateMachine& sm) {
    if (id == ids::kHotSeat) {
        // Hot Seat is the local multiplayer mode; its maps are the short
        // skirmish table.
        sm.Push(std::make_unique<MissionSelectState>(
            m_Ctx, MissionDatabase::kShort, ids::kHotSeat));
        return true;
    }
    return false;
}

// --- Settings (0x100a6598) ---------------------------------------------------

std::string SettingsState::Title() const {
    return m_Ctx.StringsRef.Get(ids::kSettings);
}

void SettingsState::Build(MenuList& list) {
    Settings& s = m_Ctx.SettingsRef;
    list.Add(m_Ctx.StringsRef.Get(ids::kReady), ids::kReady);
    list.Add(m_Ctx.StringsRef.Get(ids::kSoundSettings), ids::kSoundSettings);
    // (Bluetooth name and the Bluetooth on/off toggle sit here in the original.)
    list.Add(m_Ctx.StringsRef.Get(ids::kLanguage), ids::kLanguage);
    list.Add(m_Ctx.StringsRef.Get(ids::kKeyConfiguration), ids::kKeyConfiguration);
    list.Add(m_Ctx.StringsRef.Get(ids::kResetSettings), ids::kResetSettings);
    // One entry, two labels: it names the action, not the state.
    list.Add(m_Ctx.StringsRef.Get(s.FightAnimation ? ids::kFightAnimationOff
                                                : ids::kFightAnimationOn),
             ids::kFightAnimationOn);
    // Only when this host has frames to choose between -- there is no row on
    // the Dreamcast or in the tests. Below the toggles and above Erase game
    // data: a preference among the preferences, clear of the row that wipes
    // everything.
    if (m_Ctx.HostRef.FrameCount() > 0)
        list.Add("Frame", kFrameItem, /*enabled=*/true, FrameLabel(m_Ctx));
    list.Add(m_Ctx.StringsRef.Get(ids::kEraseGameData), ids::kEraseGameData);
}

bool SettingsState::OnChosen(int id, StateMachine& sm) {
    switch (id) {
        case ids::kReady:
            // Everything the sub-screens changed comes back through here, so
            // this one write covers sound, language and key bindings too.
            PersistSettings(m_Ctx);
            sm.Back();
            return true;
        case ids::kSoundSettings:
            sm.Push(std::make_unique<SoundSettingsState>(m_Ctx));
            return true;
        case ids::kLanguage:
            sm.Push(std::make_unique<LanguageState>(m_Ctx));
            return true;
        case ids::kKeyConfiguration:
            sm.Push(std::make_unique<KeyConfigState>(m_Ctx));
            return true;
        case ids::kResetSettings: {
            Settings& s = m_Ctx.SettingsRef;
            sm.Push(std::make_unique<ConfirmState>(
                m_Ctx, ids::kResetSettings, ids::kConfirmResetSettings,
                /*yesFirst=*/false, [&s](bool yes) {
                    if (yes) s.Reset();
                }));
            return true;
        }
        case ids::kFightAnimationOn:
            m_Ctx.SettingsRef.FightAnimation = !m_Ctx.SettingsRef.FightAnimation;
            return false;  // stay, relabel
        case kFrameItem:
            sm.Push(std::make_unique<FrameState>(m_Ctx));
            return true;
        case ids::kEraseGameData: {
            // Everything this game owns, settings included -- the entry is
            // "Erase game data", not "erase the campaign", and on a memory
            // card it is the only way to get the blocks back.
            GameContext& ctx = m_Ctx;
            sm.Push(std::make_unique<ConfirmState>(
                m_Ctx, ids::kEraseGameData, ids::kConfirmEraseData,
                /*yesFirst=*/false, [&ctx](bool yes) {
                    if (!yes) return;
                    if (Storage* store = ctx.HostRef.Saves()) EraseAll(*store);
                    ctx.CampaignData = Campaign{};
                }));
            return true;
        }
        default:
            return false;
    }
}

// Back is the other way out of the settings screen, and it has to persist
// just as Ready does -- otherwise which key you left by would decide whether
// your settings survived.
void SettingsState::OnBack(StateMachine& sm) {
    PersistSettings(m_Ctx);
    MenuScreenState::OnBack(sm);
}

// --- Sound settings (0x100aafdc) --------------------------------------------

std::string SoundSettingsState::Title() const {
    return m_Ctx.StringsRef.Get(ids::kSoundSettings);
}

void SoundSettingsState::Build(MenuList& list) {
    Settings& s = m_Ctx.SettingsRef;
    list.Add(m_Ctx.StringsRef.Get(ids::kReady), ids::kReady);
    list.Add(m_Ctx.StringsRef.Get(s.SoundOn ? ids::kSoundOff : ids::kSoundOn),
             ids::kSoundOn);
    // (Mute when in call sits here in the original -- dropped, see MenuIds.h.)
    list.AddSlider(m_Ctx.StringsRef.Get(ids::kCutSceneVolume), ids::kCutSceneVolume,
                   s.CutsceneVolume, Settings::kVolumeMax);
    list.AddSlider(m_Ctx.StringsRef.Get(ids::kMusicVolume), ids::kMusicVolume,
                   s.MusicVolume, Settings::kVolumeMax);
    list.AddSlider(m_Ctx.StringsRef.Get(ids::kSfxVolume), ids::kSfxVolume,
                   s.SfxVolume, Settings::kVolumeMax);
}

void SoundSettingsState::OnSliderChanged(int id, int value) {
    Settings& s = m_Ctx.SettingsRef;
    if (id == ids::kCutSceneVolume) s.CutsceneVolume = value;
    if (id == ids::kMusicVolume) s.MusicVolume = value;
    if (id == ids::kSfxVolume) s.SfxVolume = value;
    // The music slider is heard as it moves, not when the screen is left.
    // 0x100aafdc does the same three things this does, on the same slider:
    // starts the theme when the bar comes up off zero, sets the volume of
    // whatever is playing when it moves, and lets it go when it reaches zero.
    if (id == ids::kMusicVolume) ApplyMusicVolume();
}

// Shared by the slider and the master on/off row, because turning the sound
// back on has to bring the theme with it.
void SoundSettingsState::ApplyMusicVolume() {
    if (!m_Ctx.Sound) return;
    const Settings& s = m_Ctx.SettingsRef;
    if (!s.SoundOn || s.MusicVolume <= 0) {
        m_Ctx.Sound->StopMusic();
        return;
    }
    // Idempotent: this is a volume change when the theme is already up, and
    // starts it when it is not.
    m_Ctx.Sound->StartMusic(SoundManager::kBankMenu,
                           SoundManager::kSoundMenuMusic);
}

bool SoundSettingsState::OnChosen(int id, StateMachine& sm) {
    switch (id) {
        case ids::kReady:
            sm.Back();
            return true;
        case ids::kSoundOn:
            m_Ctx.SettingsRef.SoundOn = !m_Ctx.SettingsRef.SoundOn;
            ApplyMusicVolume();
            return false;
        default:
            return false;
    }
}

// --- Language (0x100a5b10) ---------------------------------------------------

std::string LanguageState::Title() const {
    return m_Ctx.StringsRef.Get(ids::kLanguage);
}

void LanguageState::Build(MenuList& list) {
    for (const LanguageEntry& e : kLanguages)
        list.Add(m_Ctx.StringsRef.Get(e.ID), e.ID);
}

bool LanguageState::OnChosen(int id, StateMachine& sm) {
    for (const LanguageEntry& e : kLanguages) {
        if (e.ID != id) continue;
        m_Ctx.SettingsRef.CurrentLanguage = e.Lang;
        m_Ctx.CurrentLanguage = e.Lang;
        // Reload the table so every screen switches at once, exactly as the
        // original does when the language changes.
        if (!m_Ctx.StringsRef.Load(m_Ctx.Pack, e.Lang))
            LogError("language: failed to load %s\n",
                     Strings::PathFor(e.Lang).c_str());
        sm.Back();
        return true;
    }
    return false;
}

// --- Device frame (the port's own screen) ------------------------------------

std::string FrameState::Title() const { return "Frame"; }

void FrameState::Build(MenuList& list) {
    list.Add(kFrameOff, kOffItem);
    for (int i = 0; i < m_Ctx.HostRef.FrameCount(); ++i)
        list.Add(m_Ctx.HostRef.FrameLabel(i), kFirstFrame + i);
}

bool FrameState::OnChosen(int id, StateMachine& sm) {
    const int index = id == kOffItem ? -1 : id - kFirstFrame;
    if (id != kOffItem && (index < 0 || index >= m_Ctx.HostRef.FrameCount()))
        return false;
    m_Ctx.SettingsRef.Frame = index < 0 ? "" : m_Ctx.HostRef.FrameId(index);
    // Straight to the host, so the choice is already drawn behind this screen
    // by the time it pops. Writing it out waits for the settings screen to be
    // left, along with everything else.
    m_Ctx.HostRef.SetFrame(m_Ctx.SettingsRef.Frame.c_str());
    sm.Back();
    return true;
}

// --- Key configuration (0x10081f40) -----------------------------------------

std::string KeyConfigState::Title() const {
    return m_Ctx.StringsRef.Get(ids::kKeyConfiguration);
}

void KeyConfigState::Build(MenuList& list) {
    list.Add(m_Ctx.StringsRef.Get(ids::kReadyKeys), ids::kReadyKeys);
    list.Add(m_Ctx.StringsRef.Get(ids::kResetKeyConfig), ids::kResetKeyConfig);
    // The eleven bindable actions, ids 1506..1516, each showing its key.
    for (int i = 0; i < ids::kKeyActionCount; ++i) {
        const int id = ids::kFirstKeyAction + i;
        list.Add(m_Ctx.StringsRef.Get(id), id, /*enabled=*/true,
                 KeyName(m_Ctx.SettingsRef.Binding(
                     static_cast<Settings::Action>(i))));
    }
}

bool KeyConfigState::OnChosen(int id, StateMachine& sm) {
    if (id == ids::kReadyKeys) {
        sm.Back();
        return true;
    }
    if (id == ids::kResetKeyConfig) {
        Settings& s = m_Ctx.SettingsRef;
        sm.Push(std::make_unique<ConfirmState>(
            m_Ctx, ids::kResetKeyConfig, ids::kConfirmResetKeys,
            /*yesFirst=*/false, [&s](bool yes) {
                if (yes) s.ResetKeys();
            }));
        return true;
    }
    // Rebinding needs a "press a key" capture screen (string 1533) and, per the
    // plan, gamepad support alongside it. Not built yet, so say so.
    PushNotice(m_Ctx, sm, ids::kKeyConfiguration,
               "Rebinding is not ported yet.");
    return true;
}

// --- Help (0x1007cb04) -------------------------------------------------------

std::string HelpState::Title() const {
    return m_Ctx.StringsRef.Get(ids::kHelpTitle);
}

void HelpState::Build(MenuList& list) {
    list.Add(m_Ctx.StringsRef.Get(ids::kHelpBattleView), ids::kHelpBattleView);
    list.Add(m_Ctx.StringsRef.Get(ids::kHelpTravelView), ids::kHelpTravelView);
    list.Add(m_Ctx.StringsRef.Get(ids::kHelpCredits), ids::kHelpCredits);
}

bool HelpState::OnChosen(int id, StateMachine& sm) {
    // Battle view and Travel view page through help text; Credits is its own
    // screen. Both need renderers the port doesn't have yet.
    PushNotice(m_Ctx, sm, id,
               id == ids::kHelpCredits ? kNoCredits : kNoHelpPages);
    return true;
}

// --- Mission select (the port's own screen; see the header) -----------------

std::string MissionSelectState::Title() const {
    return m_Ctx.StringsRef.Get(m_TitleID);
}

// Which of the original's three save files a battle picked here belongs in.
// The tutorial has its own (`tutorial-game.nds`); everything else is a one-off
// battle outside the campaign and shares the hot-seat file
// (`hotseat-game.nds`), including the port's own mission browser, which the
// original has no equivalent of.
SaveKind MissionSelectState::KindOf(int style) {
    return style == MissionDatabase::kTutorial ? SaveKind::kTutorial
                                               : SaveKind::kHotSeat;
}

void MissionSelectState::Build(MenuList& list) {
    // A battle saved from this mode is offered back at the top of its own
    // list. Without this the battle menu's Save row would write a file that
    // nothing could ever read.
    if (CanLoad(m_Ctx, KindOf(m_Style)))
        list.Add(m_Ctx.StringsRef.Get(ids::kLoadGame), kResumeItem);

    MissionDatabase db;
    if (!db.Load(m_Ctx.Pack)) return;
    const auto missions =
        db.ByStyle(static_cast<MissionDatabase::Style>(m_Style));
    for (std::size_t i = 0; i < missions.size(); ++i)
        list.Add(missions[i]->Name, kItemBase + int(i));
}

bool MissionSelectState::OnChosen(int id, StateMachine& sm) {
    if (id == kResumeItem) {
        // The same question the campaign's Load asks, for the same reason.
        if (AsksForCard(m_Ctx)) {
            switch (PickCard(m_Ctx, /*allowNone=*/false)) {
                case CardChoice::kQuit:
                    sm.Quit();
                    return true;
                case CardChoice::kPicked:
                    break;
                default:
                    return false;
            }
        }
        Storage* store = m_Ctx.HostRef.Saves();
        SavedGame game;
        const SaveStatus status =
            store ? ReadGame(*store, KindOf(m_Style), game)
                  : SaveStatus::kNoStorage;
        if (status != SaveStatus::kOk || !game.InBattle) {
            PushNotice(m_Ctx, sm, m_TitleID,
                       status == SaveStatus::kOk
                           ? SaveStatusText(SaveStatus::kCorrupt)
                           : SaveStatusText(status));
            return true;
        }
        auto session = std::make_unique<BattleSession>();
        session->SetName(game.Battle.Name);
        session->SetSaveKind(KindOf(m_Style));
        session->SetMissionKey(game.Battle.Mission);
        // The snapshot carries the seating, but the board underneath it is
        // rebuilt from the pak first, so seat it from the table as well --
        // that is what a save of a battle the table no longer lists falls
        // back to, and it costs one lookup.
        MissionDatabase resumeDb;
        if (resumeDb.Load(m_Ctx.Pack)) {
            if (const MissionDatabase::Mission* m =
                    resumeDb.ByKey(game.Battle.Mission)) {
                session->SetSeating(m->HumanSeats(), m->Teams);
                session->SetMissionKey(m->Key, m->Index);
            }
        }
        if (!session->Load(m_Ctx, game.Battle.Level)) {
            PushNotice(m_Ctx, sm, m_TitleID, "That battle failed to load.");
            return true;
        }
        auto screen = std::make_unique<BattleScreen>(m_Ctx, std::move(session));
        screen->Resume(game.Battle);
        sm.Push(std::move(screen));
        return true;
    }

    if (id < kItemBase) return false;
    MissionDatabase db;
    if (!db.Load(m_Ctx.Pack)) return false;
    const auto missions =
        db.ByStyle(static_cast<MissionDatabase::Style>(m_Style));
    const std::size_t index = std::size_t(id - kItemBase);
    if (index >= missions.size()) return false;

    auto session = std::make_unique<BattleSession>();
    session->SetName(missions[index]->Name);
    session->SetSaveKind(KindOf(m_Style));
    session->SetMissionKey(missions[index]->Key, missions[index]->Index);
    session->SetSeating(missions[index]->HumanSeats(), missions[index]->Teams);
    if (!session->Load(m_Ctx, missions[index]->File)) {
        PushNotice(m_Ctx, sm, m_TitleID, "That battle failed to load.");
        return true;
    }
    sm.Push(std::make_unique<BattleScreen>(m_Ctx, std::move(session)));
    return true;
}

}  // namespace bb
