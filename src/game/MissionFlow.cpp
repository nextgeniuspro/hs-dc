#include "game/MissionFlow.h"

#include <cstdio>
#include <memory>
#include <string>

#include "game/BattleScreen.h"
#include "game/Cutscene.h"
#include "game/Font.h"
#include "game/Game.h"
#include "game/LoadingScreen.h"
#include "game/MissionDatabase.h"
#include "game/SaveGame.h"
#include "game/SoundManager.h"
#include "game/StateMachine.h"
#include "game/Strings.h"
#include "game/TextBox.h"
#include "game/TextureCache.h"
#include "game/TravelMap.h"
#include "game/TravelMenu.h"
#include "game/TravelWorld.h"
#include "platform/Host.h"
#include "platform/Storage.h"
#include "platform/Surface.h"
#include "shim/Log.h"

namespace bb {
namespace {

// Strings the result and info boards use.
constexpr int kStrMissionFailed = 2126;
constexpr int kStrMissionSuccess = 2127;
constexpr int kStrBattleInfo = 2058;
// The board a random sea battle opens with, and its two soft keys 2123 /
// 2124 -- which is what TextBox mode 1 exists for.
constexpr int kStrEncounter = 2122;
// The board the chart raises when a place is picked (0x100bc09c).
constexpr int kStrOpenMission = 2141;
constexpr int kStrWins = 62202;
constexpr int kStrLoses = 62203;
// The labels the info board lists, in 0x1008a9c8's order.
constexpr int kStrSkillPoints = 2325;   // "Skill points gained", winner only
constexpr int kStrTimeUsed = 2330;      // leads the original's list
constexpr int kStrTurnsPlayed = 2326;
constexpr int kStrGold = 2327;
constexpr int kStrPropsCaptured = 2328;
constexpr int kStrPropsLost = 1719;
constexpr int kStrUnitsDestroyed = 1716;
constexpr int kStrUnitsLost = 1717;
constexpr int kStrUnitsBuilt = 1715;
// The two pictures the result board leads with, as inline image tags.
constexpr const char* kVictoryTag = "<0 Data\\icons\\victory.tc>";
constexpr const char* kDefeatTag = "<0 Data\\icons\\Defeated.tc>";
// The label and its value are joined by a literal ": " -- 0x100b9dd0 returns
// that, or " : " in French, and nothing else.
const char* Sep(GameContext& ctx) {
    return ctx.CurrentLanguage == Language::kFr ? " : " : ": ";
}

void PlayScene(GameContext& ctx, const std::string& name) {
    if (name.empty()) return;
    LoadingScreen load(ctx);
    load.Step(40);
    load.Finish();
    PlayCutscene(ctx.HostRef, ctx.Pack, ctx.Textures, ctx.StringsRef, ctx.SmallFont,
                 name, ctx.Sound, ctx.CampaignData.Commander);
}

// The music a briefing board plays over, and puts away when it closes.
//
// 0x1008a1a0's shape exactly: duck everything to an eighth of the music
// volume, start the briefing track at the full music volume, and on the way
// out ramp it back down to nothing at `musicVolume / 32` a block (floored at
// one, which is 0x1008c2a8). A fade that reaches zero stops the voice, so this
// is also what takes the track off the mixer.
//
// Ducking is not undone here, and that is the original's behaviour rather than
// an oversight: the next screen to be built asks for its own music and sets
// the volume then. See SoundManager::StartMusic.
class BriefingMusic {
public:
    BriefingMusic(GameContext& ctx, SoundManager::MenuSound track)
        : m_Sound(ctx.Sound) {
        if (!m_Sound) return;
        m_FadeStep = m_Sound->MusicVolume() / 32;
        if (m_FadeStep < 1) m_FadeStep = 1;
        m_Sound->Duck(m_Sound->DuckedMusicVolume());
        m_Sound->StartMusic(SoundManager::kBankMenu, static_cast<int>(track));
    }
    ~BriefingMusic() {
        if (m_Sound) m_Sound->FadeMusic(m_FadeStep, 0);
    }
    BriefingMusic(const BriefingMusic&) = delete;
    BriefingMusic& operator=(const BriefingMusic&) = delete;

private:
    SoundManager* m_Sound = nullptr;
    int m_FadeStep = 1;
};

// The briefing (0x1008a1a0): the mission's name over its description, with Ok
// and Cancel. Cancel backs out without playing -- except at Broken
// Tranquility, whose key the function compares against the literal "SP1" and
// gives mode 0: both soft keys say Ok, the result is thrown away, and the
// first battle of a fresh voyage cannot be declined.
bool ShowBriefing(GameContext& ctx, const MissionDatabase::Mission& m) {
    const bool first = m.Key == "SP1";
    // The board has a piece of music of its own, and it is the reason
    // menu.dat carries entries 8 and 9 -- the file labels them "Mission
    // briefing" and "Mission briefing encounter". 0x1008a1a0 picks between
    // them on the mission *key*, not on whether the board can be declined: the
    // encounter track is for keys beginning "MPS", and every real mission --
    // Broken Tranquility with its two Ok keys included -- gets this one. Both
    // name a travel-bank track, so on the chart they cost nothing extra:
    // decoded sounds are shared by path.
    BriefingMusic music(ctx, SoundManager::kSoundBriefingMusic);
    TextBox box(ctx, first ? TextBox::kOkOk : TextBox::kOkCancel);
    box.Title(ctx.StringsRef.Get(kMissionNameBase + m.Index));
    box.Text(ctx.StringsRef.Get(kMissionBriefBase + m.Index));
    const int r = box.Run();
    if (r == TextBox::kQuit) return false;
    return first || r == TextBox::kConfirmed;
}

// The win/lose board (0x1008a770): a title, the picture, and the mission's own
// closing line. Mode 12 is Next / Exit.
int ShowResult(GameContext& ctx, const MissionDatabase::Mission& m, bool won) {
    TextBox box(ctx, TextBox::kNextExit);
    box.Title(ctx.StringsRef.Get(won ? kStrMissionSuccess : kStrMissionFailed));
    box.Text(won ? kVictoryTag : kDefeatTag);
    box.Text(ctx.StringsRef.Get((won ? kMissionVictoryBase : kMissionDefeatBase) +
                             m.Index));
    // 0x1008a770 plays the misc bank's own fanfare here -- `mission_success`
    // or `mission_failed`, its indices 0 and 1 -- not a menu blip. The port
    // used the blip because the misc bank's first two entries had no caller.
    if (ctx.Sound)
        ctx.Sound->PlayBattle(won ? SoundManager::kSoundMissionSuccess
                                  : SoundManager::kSoundMissionFailed);
    return box.Run();
}

// One "Battle info" board per player (0x1008a9c8): who won, then the tally.
// The last player's board gets Exit / Back instead of Next / Back, which is
// how the original knows it is the end of the run.
int ShowBattleInfo(GameContext& ctx, const BattleField& field, int winner,
                   int skillPoints) {
    const auto& players = field.Players();
    int seats = 0;
    for (int p = 1; p <= BattleField::kMaxPlayers; ++p)
        if (players[std::size_t(p)].Present) ++seats;

    const std::string sep = Sep(ctx);
    int shown = 0;
    for (int p = 1; p <= BattleField::kMaxPlayers; ++p) {
        const BattleField::Player& pl = players[std::size_t(p)];
        if (!pl.Present) continue;
        ++shown;
        const bool last = shown == seats;
        const BattleField::Player::StatBlock& st = pl.Stats;
        TextBox box(ctx, last ? 6 : 5);   // Exit/Back, else Next/Back
        box.Title(ctx.StringsRef.Get(kStrBattleInfo));
        // A side wins, not a seat, so an ally's board says "wins" too --
        // 0x1008b028 settles the battle by testing seats against the winning
        // team's mask rather than against one another.
        const bool onWinningSide = field.SameTeam(p, winner);
        box.Text(pl.Name + " " +
                 ctx.StringsRef.Get(onWinningSide ? kStrWins : kStrLoses));
        // Only the first board carries the skill points, and only when there
        // are any to award (0x1008a9c8 guards it with `param_4 && i == 0`).
        // What the place is worth is its own `<skillpoints>`, which is what
        // the chart will have to spend.
        if (shown == 1 && onWinningSide && skillPoints > 0)
            box.Text(ctx.StringsRef.Get(kStrSkillPoints) + sep +
                     std::to_string(skillPoints));
        // The engine shows elapsed / limit for both time and turns; a campaign
        // battle has no turn limit, so only the left half is real and the port
        // leaves the slash off. The clock is the battle's own -- see
        // BattleField::Elapsed, which a mid-battle save carries.
        {
            const int s = field.ElapsedSeconds();
            char clock[16];
            std::snprintf(clock, sizeof(clock), "%d:%02d", s / 60, s % 60);
            box.Text(ctx.StringsRef.Get(kStrTimeUsed) + sep + clock);
        }
        box.Text(ctx.StringsRef.Get(kStrTurnsPlayed) + sep +
                 std::to_string(field.Round()));
        box.Text(ctx.StringsRef.Get(kStrGold) + sep +
                 std::to_string(st.GoldCollected));
        box.Text(ctx.StringsRef.Get(kStrPropsCaptured) + sep +
                 std::to_string(st.PropertiesCaptured));
        box.Text(ctx.StringsRef.Get(kStrPropsLost) + sep +
                 std::to_string(st.PropertiesLost));
        box.Text(ctx.StringsRef.Get(kStrUnitsDestroyed) + sep +
                 std::to_string(st.UnitsDestroyed));
        box.Text(ctx.StringsRef.Get(kStrUnitsLost) + sep +
                 std::to_string(st.UnitsLost));
        box.Text(ctx.StringsRef.Get(kStrUnitsBuilt) + sep +
                 std::to_string(st.UnitsBuilt));
        const int r = box.Run();
        if (r == TextBox::kQuit) return r;
        // Back on any board returns to the result screen, which is why the
        // original loops the two together.
        if (r == TextBox::kCancelled) return r;
    }
    return TextBox::kConfirmed;
}

}  // namespace

void CheckpointCampaign(GameContext& ctx) {
    Storage* store = ctx.HostRef.Saves();
    if (!store) return;
    SavedGame game;
    game.CampaignData = ctx.CampaignData;
    game.InBattle = false;
    const SaveStatus s = WriteGame(*store, SaveKind::kCampaign, game);
    if (s != SaveStatus::kOk)
        LogError("save: campaign checkpoint failed -- %s\n", SaveStatusText(s));
}

MissionResult RunMission(GameContext& ctx, const std::string& key,
                         const SavedBattle* resume) {
    MissionDatabase db;
    if (!db.Load(ctx.Pack)) return MissionResult::kUnavailable;
    const MissionDatabase::Mission* m = db.ByKey(key);
    if (!m) {
        LogError("mission: no table entry for '%s'\n", key.c_str());
        return MissionResult::kUnavailable;
    }
    TravelWorld world;
    world.Load(ctx.Pack);
    const TravelWorld::Location* node = world.ByKey(key);

    // 1 and 2, the cutscene and the briefing, only when the mission is
    // starting. A save picked up from the battle menu has been through both
    // already, and replaying them would make loading feel like restarting.
    if (!resume) {
        if (node) PlayScene(ctx, node->Before);
        if (ctx.HostRef.QuitRequested()) return MissionResult::kQuit;

        if (!ShowBriefing(ctx, *m)) return MissionResult::kDeclined;
        if (ctx.HostRef.QuitRequested()) return MissionResult::kQuit;
    }

    // 3. The battle, behind a loading board.
    LoadingScreen load(ctx);
    load.Step(20);
    auto session = std::make_unique<BattleSession>();
    session->SetName(ctx.StringsRef.Get(kMissionNameBase + m->Index));
    // A campaign battle saves into the campaign's own file, and remembers
    // which place on the chart it is, so a resumed save knows where it left
    // off (0x10078184 picks the file from the game type in the same way).
    session->SetSaveKind(SaveKind::kCampaign);
    session->SetMissionKey(m->Key, m->Index);
    session->SetSeating(m->HumanSeats(), m->Teams);
    load.Step(50);
    if (!session->Load(ctx, m->File)) {
        LogError("mission: '%s' failed to load\n", m->File.c_str());
        return MissionResult::kUnavailable;
    }
    // The commander the player made on the New game screen takes the first
    // seat: their name replaces whatever the level file called player one, and
    // their colour replaces the seat's default palette.
    session->Field().SetName(1, ctx.CampaignData.Commander);
    session->Field().SetColour(1, ctx.CampaignData.Colour);
    // And the perks the skill chart bought: they are the commander's, so they
    // come to whichever seat the commander is sitting in.
    session->Field().SetPerks(1, ctx.CampaignData.Perks);
    load.Finish();

    BattleScreen screen(ctx, std::move(session));
    screen.SetViewer(1);
    if (resume) screen.Resume(*resume);
    {
        // The screen is written as a state; run it on a scratch machine so its
        // Back() request lands somewhere harmless.
        StateMachine scratch;
        screen.Run(scratch);
    }
    const BattleScreen::Outcome outcome = screen.Result();
    if (ctx.HostRef.QuitRequested()) return MissionResult::kQuit;
    if (outcome == BattleScreen::Outcome::kQuit) return MissionResult::kQuit;
    // Ended from the battle menu: no result board and no closing cutscene for a
    // battle that was never settled -- straight out to the main menu.
    if (outcome == BattleScreen::Outcome::kAbandoned)
        return MissionResult::kAbandoned;

    const bool won = outcome == BattleScreen::Outcome::kWon;
    // What the info board says the win was worth. The chart is what pays it
    // out (RunCampaign), so this is only the announcement.
    const int reward = node ? node->Skill : 0;

    // 4 and 5. The result board and the per-player info boards, looped: Back
    // out of the info boards and the result comes up again.
    for (;;) {
        const int r = ShowResult(ctx, *m, won);
        if (r == TextBox::kQuit) return MissionResult::kQuit;
        const int info = ShowBattleInfo(ctx, screen.Field(),
                                        screen.Field().Winner(), reward);
        if (info == TextBox::kQuit) return MissionResult::kQuit;
        if (info != TextBox::kCancelled) break;
    }

    // 6. The closing cutscene.
    if (node) PlayScene(ctx, won ? node->Complete : node->Fail);
    return won ? MissionResult::kWon : MissionResult::kLost;
}

namespace {

// The "Encounter" board (title 2122, Accept / Retreat). 0x1008a1a0's branch
// for keys starting "MPS": the skirmish's *name* as the first line -- "Doomed
// Gulf" -- an empty spacer row, then the 10300-series briefing, which opens on
// its own `<- data\icons\Intercept.tc>` picture of the stranger's ship and the
// lookout's report. The 10200-series blurb with the map thumbnail belongs to
// the multiplayer select screen, not here.
bool ShowEncounter(GameContext& ctx, const MissionDatabase::Mission& m) {
    // The other of the two briefing tracks -- this is the branch 0x1008a1a0
    // takes for a key beginning "MPS", which is what an encounter's is.
    BriefingMusic music(ctx, SoundManager::kSoundBriefingEncounterMusic);
    TextBox box(ctx, TextBox::kAcceptRetreat);
    box.Title(ctx.StringsRef.Get(kStrEncounter));
    box.Text(ctx.StringsRef.Get(kMissionNameBase + m.Index));
    box.Add(TextBox::kTextLow, "");
    box.Text(ctx.StringsRef.Get(kMissionBriefBase + m.Index));
    if (ctx.Sound) ctx.Sound->PlayMenu(SoundManager::kSoundEnter);
    return box.Run() == TextBox::kConfirmed;
}

// A sea battle: no cutscenes, no briefing, straight to the fight and then the
// same result boards a mission gets.
MissionResult RunEncounter(GameContext& ctx, const std::string& key,
                           const SavedBattle* resume = nullptr) {
    MissionDatabase db;
    if (!db.Load(ctx.Pack)) return MissionResult::kUnavailable;
    const MissionDatabase::Mission* m = db.ByKey(key);
    if (!m) {
        LogError("encounter: no table entry for '%s'\n", key.c_str());
        return MissionResult::kUnavailable;
    }
    // The Accept / Retreat board is the decision to fight, and it has already
    // been made in a save taken mid-encounter.
    if (!resume) {
        if (!ShowEncounter(ctx, *m)) return MissionResult::kDeclined;
        if (ctx.HostRef.QuitRequested()) return MissionResult::kQuit;
    }

    LoadingScreen load(ctx);
    load.Step(20);
    auto session = std::make_unique<BattleSession>();
    session->SetName(ctx.StringsRef.Get(kStrEncounter));
    // An encounter happens during the voyage, so it belongs in the campaign's
    // file like any other campaign battle -- flagged, because it is bracketed
    // differently on the way out.
    session->SetSaveKind(SaveKind::kCampaign);
    session->SetMissionKey(m->Key, m->Index);
    session->SetEncounter(true);
    session->SetSeating(m->HumanSeats(), m->Teams);
    load.Step(50);
    if (!session->Load(ctx, m->File)) {
        LogError("encounter: '%s' failed to load\n", m->File.c_str());
        return MissionResult::kUnavailable;
    }
    session->Field().SetName(1, ctx.CampaignData.Commander);
    session->Field().SetColour(1, ctx.CampaignData.Colour);
    // And the perks the skill chart bought: they are the commander's, so they
    // come to whichever seat the commander is sitting in.
    session->Field().SetPerks(1, ctx.CampaignData.Perks);
    load.Finish();

    BattleScreen screen(ctx, std::move(session));
    screen.SetViewer(1);
    if (resume) screen.Resume(*resume);
    {
        StateMachine scratch;
        screen.Run(scratch);
    }
    if (ctx.HostRef.QuitRequested()) return MissionResult::kQuit;
    const BattleScreen::Outcome outcome = screen.Result();
    if (outcome == BattleScreen::Outcome::kQuit) return MissionResult::kQuit;
    if (outcome == BattleScreen::Outcome::kAbandoned)
        return MissionResult::kAbandoned;
    const bool won = outcome == BattleScreen::Outcome::kWon;
    // An encounter is worth its own `<skillpoints>` like anywhere else on the
    // chart; the world's entry for it is where that number lives.
    int reward = 0;
    {
        TravelWorld world;
        if (world.Load(ctx.Pack)) {
            for (const TravelWorld::Encounter& e : world.Encounters())
                if (e.Key == key) reward = e.Skill;
        }
    }
    const int info = ShowBattleInfo(ctx, screen.Field(),
                                    screen.Field().Winner(), reward);
    if (info == TextBox::kQuit) return MissionResult::kQuit;
    return won ? MissionResult::kWon : MissionResult::kLost;
}

// The chart is not on screen while a battle is being fought on top of it, and
// its artwork is 4.9 MB of islands, sea and fold-out map -- more than the
// battle's own 3 MB. On a desktop that is merely untidy; on a Dreamcast the
// two together are 13.6 MB of a 16 MB machine and the game dies where the
// second one loads. So the chart goes back to the texture cache for the
// duration of a fight and is read again on the way out.
//
// The map object itself survives: a voyage is a position, a route and a
// camera, none of which is a picture.
class ChartAway {
public:
    explicit ChartAway(TravelMap& map) : m_Map(map) { m_Map.Unload(); }
    ~ChartAway() { m_Map.Load(); }

    ChartAway(const ChartAway&) = delete;
    ChartAway& operator=(const ChartAway&) = delete;

private:
    TravelMap& m_Map;
};

}  // namespace

void RunCampaign(GameContext& ctx, StateMachine& sm,
                 const SavedBattle* resume) {
    TravelWorld world;
    if (!world.Load(ctx.Pack)) {
        sm.Back();
        return;
    }
    TravelState& state = ctx.CampaignData.Travel;
    const bool fresh = !resume && !state.Started;
    if (fresh) ResetTravelState(world, state);

    TravelMap map(ctx, world, state);

    // What a place is worth when it falls: the skill points the chart spends
    // and the page the captain writes about it. 0x100dc2b8 pays both out on a
    // win and neither otherwise, and the log dedupes itself.
    auto payOut = [&ctx](int skill, int log) {
        ctx.CampaignData.SkillPoints += skill;
        ctx.CampaignData.AddLogPage(log);
    };

    // The two endings the voyage cannot carry on from, and they are not the
    // same door: kQuit is the host wanting the game gone, kAbandoned is "End
    // current game" from the battle menu, which unwinds to the main menu with
    // the game still running. True means the campaign is over either way.
    auto ended = [&](MissionResult r) {
        if (r == MissionResult::kQuit) {
            sm.Quit();
            return true;
        }
        if (r == MissionResult::kAbandoned) {
            sm.Home();
            return true;
        }
        return false;
    };

    // A save taken in the middle of a battle goes back into that battle before
    // the chart is unrolled -- the voyage is exactly where it was, and the
    // fighting is what was interrupted.
    if (resume) {
        const MissionResult r =
            resume->Encounter ? RunEncounter(ctx, resume->Mission, resume)
                              : RunMission(ctx, resume->Mission, resume);
        if (ended(r)) return;
        if (r == MissionResult::kWon && !resume->Encounter) {
            if (const TravelWorld::Location* loc = world.ByKey(resume->Mission)) {
                CompleteLocation(world, state, loc->ID);
                payOut(loc->Skill, loc->Log);
            }
        }
        // The battle is settled either way, so the slot must stop describing
        // one: otherwise quitting now would drop the player back into a fight
        // they have already finished.
        CheckpointCampaign(ctx);
    }

    // The first mission is not sailed to. `<playerstart>` is twelve pixels
    // from SP1, which is the only place the world opens with, so a new
    // commander is already moored at Broken Tranquility -- and the original
    // runs it before the chart is ever unrolled: New game, the Caribbean
    // cutscene, the battle, Croco Greg, and only then the map.
    if (fresh) {
        if (const TravelWorld::Location* here = map.LocationAt(map.Ship())) {
            const MissionResult r = RunMission(ctx, here->Key);
            if (ended(r)) return;
            if (r == MissionResult::kWon) {
                CompleteLocation(world, state, here->ID);
                payOut(here->Skill, here->Log);
            }
            CheckpointCampaign(ctx);
        }
    }
    if (ctx.HostRef.QuitRequested()) {
        sm.Quit();
        return;
    }

    if (!map.Load()) {
        LogError("travel: the map's own art is missing; not sailing\n");
        sm.Back();
        return;
    }

    while (!ctx.HostRef.QuitRequested()) {
        const TravelMap::Event e = map.Run();
        if (e == TravelMap::Event::kQuit) {
            sm.Quit();
            return;
        }
        if (e == TravelMap::Event::kLeave) break;

        // The menu key, with everything that hangs off it (TravelMenu.h). It
        // draws over the chart and gives it back, except for the two answers
        // that end the voyage.
        if (e == TravelMap::Event::kMenu) {
            TravelMenu menu(ctx, map, world);
            const TravelMenu::Result r = menu.Run();
            if (r == TravelMenu::Result::kQuit) {
                sm.Quit();
                return;
            }
            if (r == TravelMenu::Result::kAbandoned) {
                // Where the voyage got to is worth keeping even now: the
                // player asked to stop playing, not to lose the campaign.
                CheckpointCampaign(ctx);
                sm.Home();
                return;
            }
            // "Open missions" answers by putting the pointer somewhere.
            if (r == TravelMenu::Result::kMoveTo) map.MoveCursor(menu.Target());
            continue;
        }

        // Picking a place on the chart asks before it sails: title 2141 "Open
        // mission" over the location's own travel blurb, Ok or Cancel
        // (0x100bc09c). Ok orders the ship; arriving is what starts the
        // battle.
        if (e == TravelMap::Event::kSelected) {
            const TravelWorld::Location* loc = map.Selected();
            if (!loc) continue;
            TextBox box(ctx, TextBox::kOkCancel);
            box.Title(ctx.StringsRef.Get(kStrOpenMission));
            box.Text(ctx.StringsRef.Get(loc->Desc));
            const int r = box.Run();
            if (r == TextBox::kQuit) {
                sm.Quit();
                return;
            }
            if (r == TextBox::kConfirmed) map.SailTo(loc->Pos);
            continue;
        }

        if (e == TravelMap::Event::kAmbush) {
            const TravelWorld::Encounter* enc = map.Ambush();
            if (!enc) continue;
            MissionResult r = MissionResult::kQuit;
            {
                ChartAway away(map);
                r = RunEncounter(ctx, enc->Key);
            }
            if (ended(r)) return;
            if (r == MissionResult::kWon) payOut(enc->Skill, enc->Log);
            // The encounter is spent and the ship has sailed to get here, so
            // the chart has moved on whether the fight was won or not.
            CheckpointCampaign(ctx);
            continue;
        }

        if (e != TravelMap::Event::kArrived) continue;
        const TravelWorld::Location* loc = map.Arrived();
        if (!loc) continue;
        MissionResult r = MissionResult::kQuit;
        {
            ChartAway away(map);
            r = RunMission(ctx, loc->Key);
        }
        if (ended(r)) return;
        if (r == MissionResult::kWon) {
            CompleteLocation(world, state, loc->ID);
            payOut(loc->Skill, loc->Log);
        } else {
            // A loss or a declined briefing leaves the place open -- and the
            // ship standing on it, where the latched proximity test can never
            // fire again. The engine pushes off thirty-two pixels in a random
            // free direction for exactly this reason (0x100dc2b8's tail), so
            // sailing back in re-opens the briefing.
            map.PushOffFrom(loc->Pos);
        }
        // Still worth a checkpoint either way -- the ship moved to get here.
        CheckpointCampaign(ctx);
    }
    if (ctx.HostRef.QuitRequested()) {
        sm.Quit();
        return;
    }
    // Leaving the chart for the main menu is the last chance to record where
    // the voyage got to.
    CheckpointCampaign(ctx);
    sm.Back();
}

}  // namespace bb
