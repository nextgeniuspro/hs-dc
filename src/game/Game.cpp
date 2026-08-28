#include "game/Game.h"

#include <cstdio>
#include <memory>

#include "game/BattleScreen.h"
#include "game/CampaignStates.h"
#include "game/Cutscene.h"
#include "game/FightAnimation.h"
#include "game/NdLevel.h"
#include "game/Settings.h"
#include "game/SoundManager.h"
#include "game/Font.h"
#include "game/MenuStates.h"
#include "game/MissionDatabase.h"
#include "game/ResourceTable.h"
#include "game/SaveGame.h"
#include "game/StateMachine.h"
#include "game/Strings.h"
#include "game/TextureCache.h"
#include "game/TravelMap.h"
#include "game/TravelWorld.h"
#include "game/Water.h"
#include "platform/Host.h"
#include "platform/Storage.h"
#include "shim/Log.h"

namespace bb {

namespace {

// The startup loader interleaves its work with the splash screens; these three
// run at the points it does. See Boot.h for why the split matters.

// Slot 0x22, before anything is shown (0x1008d588).
void LoadFont(GameContext& ctx) {
    if (!ctx.SmallFont.Load(ctx.Textures, "Data\\font-small.tc", 11))
        LogError("fonts: small font failed to load\n");
}

// Slot 0xd7, behind the Nokia logo. Every screen's UI text comes from here by
// numeric id -- and on the device, inflating and parsing it is what gave the
// logo its dwell.
void LoadStrings(GameContext& ctx) {
    if (ctx.StringsRef.Load(ctx.Pack, ctx.CurrentLanguage))
        LogDebug("strings: %zu entries from %s\n", ctx.StringsRef.Count(),
                 Strings::PathFor(ctx.CurrentLanguage).c_str());
}

// Everything else, behind the ESRB screen.
void LoadRest(GameContext& ctx) {
    int attempted = 0;
    const int loaded = ctx.Textures.LoadStartupTextures(&attempted);
    if (loaded != attempted)
        LogError("resources: %d of %d startup textures decoded completely\n",
                 loaded, attempted);
    else
        LogDebug("resources: all %d startup textures decoded\n", loaded);

    if (!ctx.BigFont.Load(ctx.Textures, "Data\\font-big.tc", 15))
        LogError("fonts: big font failed to load\n");

    // Slot 0xb9. The original builds this once the intro cutscene player is
    // torn down (0x1008cef8), together with the second batch of menu textures;
    // from then on every menu screen draws it first.
    if (ctx.WaterRef.Load(ctx.Textures))
        LogDebug("water: caustics layer ready (%dx%d source)\n", Water::kTex,
                 Water::kTex);

    // Slot 0xd6. The startup loader creates the sound manager here and loads
    // the UI bank; its "Menu: Start sound %d" log is this step.
    if (ctx.Sound) {
        ctx.Sound->SetSettings(&ctx.SettingsRef);
        ctx.Sound->LoadBank(SoundManager::kBankMenu,
                            SoundManager::kMenuBankPath);
        ctx.Sound->LoadBank(SoundManager::kBankBattleMisc,
                            SoundManager::kBattleMiscBankPath);
    }

    // Everything above is what the engine keeps in resource slots for the rest
    // of the run -- the menu set, both fonts, the water. From here on a
    // texture belongs to whichever scene asked for it, and goes when that
    // scene does. See TextureCache.h.
    ctx.Textures.MarkBase();
}

// Settings are their own file in the original -- 0x100796f0 writes
// `settings.cfg` and nothing else touches it -- and they are read back before
// anything is drawn, so the language the player chose is the language the boot
// screens come up in. A missing file is a first run, not an error.
void LoadSettings(GameContext& ctx) {
    Storage* store = ctx.HostRef.Saves();
    if (store && ReadSettings(*store, ctx.SettingsRef) == SaveStatus::kOk) {
        ctx.CurrentLanguage = ctx.SettingsRef.CurrentLanguage;
        LogDebug("settings: restored (language %d)\n", int(ctx.CurrentLanguage));
    }
    // Outside the branch on purpose: a first run has no file to restore and
    // still wants its default frame up before the first splash draws. The
    // host quietly ignores a frame it does not have installed.
    ctx.HostRef.SetFrame(ctx.SettingsRef.Frame.c_str());
}

}  // namespace

bool RunBattle(GameContext& ctx, const std::string& mission) {
    // The battle needs the same resources the front end loads, minus the
    // splash screens: fonts and strings for its panels, textures for the map.
    LoadSettings(ctx);
    LoadFont(ctx);
    LoadStrings(ctx);
    LoadRest(ctx);

    MissionDatabase db;
    db.Load(ctx.Pack);
    const MissionDatabase::Mission* m = db.Find(mission);
    const std::string path = m ? m->File : mission;
    if (m)
        LogDebug("battle: %s -- %s, %d players\n", m->Key.c_str(),
                 m->Name.c_str(), m->Players);

    auto session = std::make_unique<BattleSession>();
    if (m) {
        session->SetName(m->Name);
        session->SetSeating(m->HumanSeats(), m->Teams);
        session->SetMissionKey(m->Key, m->Index);
    }
    if (!session->Load(ctx, path)) {
        LogError("battle: could not start '%s'\n", mission.c_str());
        return false;
    }
    // A battle started from the flag has no commander behind it. It takes
    // whatever perks the context carries, and a context with none -- which is
    // what `--battle` on its own gives -- is handed the three the New game
    // screen offers, so the perks board has something in it to try.
    std::vector<bool> perks = ctx.CampaignData.Perks;
    bool any = false;
    for (bool p : perks) any = any || p;
    if (!any) {
        perks.assign(Campaign::kPerkSlots, false);
        for (int p : Campaign::kFirstPerks) perks[std::size_t(p)] = true;
    }
    session->Field().SetPerks(1, perks);

    StateMachine sm;
    sm.Run(std::make_unique<BattleScreen>(ctx, std::move(session)));
    return true;
}

// One cutaway fight on its own, staged from a description rather than from a
// battle. There are 21 unit types over 18 terrains and the scene stages itself
// differently for nearly all of them -- who walks in, what flies, whether
// there is a sea to composite -- and playing to an attack every time is no way
// to look at that. `--fight`, and only that.
bool RunFight(GameContext& ctx, const std::string& spec) {
    LoadSettings(ctx);
    LoadFont(ctx);
    LoadStrings(ctx);
    LoadRest(ctx);

    // attacker:defender[:terrainA:terrainD[:distance[:propertyA:propertyD]]]
    int v[7] = {kUnitSwordsman, kUnitPistoleer, NdLevel::kPlain,
                NdLevel::kPlain, 1, -1, -1};
    {
        std::size_t at = 0;
        for (int i = 0; i < 7 && at <= spec.size(); ++i) {
            std::size_t end = spec.find(':', at);
            if (end == std::string::npos) end = spec.size();
            if (end > at) v[i] = std::atoi(spec.substr(at, end - at).c_str());
            if (end == spec.size()) break;
            at = end + 1;
        }
    }

    BattleRenderer renderer;
    if (!renderer.Load(ctx.Pack, ctx.Textures, ctx.PaletteRef)) {
        LogError("fight: the battle artwork failed to load\n");
        return false;
    }
    FightAnimation fight;
    if (!fight.Load(ctx.Textures, ctx.Pack, ctx.SmallFont)) {
        LogError("fight: the scene's own artwork failed to load\n");
        return false;
    }
    if (ctx.Sound) {
        ctx.Sound->LoadBank(SoundManager::kBankBattle,
                            SoundManager::kBattleBankPath);
        ctx.Sound->LoadUnitSounds(SoundManager::kUnitSoundPath);
        // The scene shouts as well as bangs, so the flag stages two nations to
        // hear the cross-nation lines rather than the generic ones.
        bool voices[SoundManager::kNationCount] = {};
        voices[SoundManager::kNationEnglish] = true;
        voices[SoundManager::kNationSpanish] = true;
        ctx.Sound->LoadVoiceBanks(voices);
        fight.SetSound(ctx.Sound);
    }

    FightAnimation::Params p;
    p.Attacker.Type = v[0];
    p.Attacker.Colour = 1;
    p.Attacker.Terrain = v[2];
    p.Attacker.Property = v[5];
    p.Attacker.HPBefore = 100;
    p.Attacker.HPAfter = 74;
    p.Attacker.Shields = 2;
    p.Attacker.Commander = "Blackbeard";
    p.Attacker.Nationality = SoundManager::kNationEnglish;
    p.Defender.Type = v[1];
    // Blue rather than the second seat's black, which is hard to make out
    // against half the backgrounds in a still.
    p.Defender.Colour = 3;
    p.Defender.Terrain = v[3];
    p.Defender.Property = v[6];
    p.Defender.HPBefore = 100;
    p.Defender.HPAfter = 38;
    p.Defender.Shields = 3;
    p.Defender.Commander = "Ramirez";
    p.Defender.Nationality = SoundManager::kNationSpanish;
    p.AttackerX = 0;
    p.AttackerY = 0;
    p.DefenderX = v[4];
    p.DefenderY = 0;
    p.Countered = true;

    Host& host = ctx.HostRef;
    LogDebug("fight: unit %d vs %d, terrain %d/%d, %d squares apart\n", v[0],
             v[1], v[2], v[3], v[4]);
    while (!host.QuitRequested()) {
        fight.Begin(p, renderer);
        LogDebug("fight: attacker on the %s, backgrounds %d/%d, %d vs %d men\n",
                 fight.AttackerSide() == 0 ? "left" : "right",
                 fight.Background(0), fight.Background(1), fight.MenAt(0),
                 fight.MenAt(1));
        while (fight.Active() && !host.QuitRequested()) {
            Surface& screen = host.Screen();
            screen.Fill(0xF000);
            fight.Step(screen);
            host.Flip();
            if (ctx.Sound) ctx.Sound->Pump(host);
            host.Sleep(40);
        }
    }
    return true;
}

bool RunTravel(GameContext& ctx) {
    LoadSettings(ctx);
    LoadFont(ctx);
    LoadStrings(ctx);
    LoadRest(ctx);

    TravelWorld world;
    if (!world.Load(ctx.Pack)) {
        LogError("travel: the world failed to load\n");
        return false;
    }
    // A voyage that has just won its first mission: the opening perk the New
    // game screen hands out, and what Broken Tranquility pays for falling --
    // its skill points and its page of the log. Marking the state started is
    // what puts the chart up first instead of running SP1 again.
    ResetTravelState(world, ctx.CampaignData.Travel);
    ctx.CampaignData.ChoosePerk(Campaign::kFirstPerks[0]);
    if (const TravelWorld::Location* first = world.ByKey("SP1")) {
        CompleteLocation(world, ctx.CampaignData.Travel, first->ID);
        ctx.CampaignData.SkillPoints += first->Skill;
        ctx.CampaignData.AddLogPage(first->Log);
    }

    StateMachine sm;
    sm.Run(std::make_unique<CampaignState>(ctx));
    return true;
}

void RunGame(GameContext& ctx, bool skipIntro) {
    // Before the first splash: a returning player's language decides which
    // localised Nokia logo comes up.
    LoadSettings(ctx);

    // The intro cutscene is read while the title splash is still up, because
    // nothing repaints the screen between the splash sequence and the
    // cutscene's first frame -- on the device that load *was* the title's
    // dwell (0x1007ffc8 runs straight after the loader returns).
    Cutscene intro(ctx.Pack, ctx.Textures, ctx.StringsRef, ctx.SmallFont);
    intro.SetSound(ctx.Sound);

    StateMachine sm;
    if (skipIntro) {
        // The same three loading steps the boot sequence interleaves with its
        // splashes, just without the splashes or the cutscene.
        LoadFont(ctx);
        LoadStrings(ctx);
        LoadRest(ctx);
        sm.Run(std::make_unique<MainMenuState>(ctx));
    } else {
        BootLoad load;
        load.Fonts = [&ctx] { LoadFont(ctx); };
        load.Strings = [&ctx] { LoadStrings(ctx); };
        load.Rest = [&ctx] { LoadRest(ctx); };
        load.Cutscene = [&intro] { intro.Load(kIntroCutscene); };
        RunBootScreens(ctx.HostRef, ctx.Textures, ctx.CurrentLanguage, load);
        if (ctx.HostRef.QuitRequested()) return;
        sm.Run(std::make_unique<IntroState>(ctx, intro.Valid() ? &intro : nullptr));
    }

    // Which screens the run passed through, in order -- the one line that says
    // how a session got where it did. Assembled only when it will be read.
    if (LogEnabled(LogLevel::kDebug)) {
        std::string flow;
        for (const auto& name : sm.Trace()) flow += " " + name;
        LogDebug("flow:%s\n", flow.c_str());
    }
}

}  // namespace bb
