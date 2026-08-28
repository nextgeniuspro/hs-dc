// Game — the top-level object the app shell hands control to.
//
// The original reaches this point by a route that is pure Symbian ceremony: the
// launcher loads main.dll, calls its single export (an app factory), which
// builds a CEikApplication -> CEikDocument -> CAknAppUi -> CCoeControl chain.
// The control's ConstructL (0x1000fa54) creates the engine object, the 176x208
// CWsBitmap the game draws into, and a one-shot 15 ms CPeriodic; that timer's
// callback (0x1000fcf0) is where the game actually starts, and it does not
// return until the game is over -- at which point it tears down and calls
// User::Exit. In other words RedLynx kept a conventional blocking main loop and
// hung it off a timer so Symbian's framework would finish initialising first.
//
// None of that scaffolding is worth porting. What matters is what the callback
// does, and that is this class: open the pak, load resources, show the boot
// screens, then run the state machine until it finishes.
#pragma once

#include <string>

#include "game/Boot.h"
#include "game/Campaign.h"

namespace bb {

class FilePack;
class Font;
class Host;
class Palette;
class Settings;
class SoundManager;
class Strings;
class TextureCache;
class Water;

// What every state needs. Passed by reference; states hold no ownership.
// These mirror the engine's resource slots, which every screen looks up through
// the engine object: 0xd7 strings, 0x22 / 0xc2 fonts, 0xb9 the water layer.
struct GameContext {
    Host& HostRef;
    FilePack& Pack;
    // Engine resource slot 0x1f: the shared 272-entry palette. The battle
    // screens need it directly, because buildings and unit sprites are
    // recoloured per player by patching its top sixteen entries.
    Palette& PaletteRef;
    TextureCache& Textures;
    Strings& StringsRef;
    Font& SmallFont;
    Font& BigFont;
    Water& WaterRef;
    Settings& SettingsRef;
    Language CurrentLanguage = Language::kEn;
    // Engine resource slot 0xd6. Null runs the game silent, which is what a
    // host with no audio device gives you.
    SoundManager* Sound = nullptr;
    // The single-player game in progress: commander name, colour and perk.
    // The engine parks the commander in resource slot 0xc1, and every screen
    // that writes `[player]` reads the name back out of there.
    Campaign CampaignData;
    // Whether a page left idle may run the attract reel. Every front-end page
    // arms it as it opens (0x100393b0) and that is right for the front end,
    // but the settings screens are also reachable from *inside* a battle now,
    // and a film starting over a game in progress is not a screen saver, it is
    // a bug. The battle clears this for as long as it is borrowing them.
    bool Attract = true;
};

// Load resources, show the boot screens, and run the screen flow to completion.
// Returns when the game is over (or the host asked to quit).
//
// `skipIntro` loads the same resources but goes straight to the main menu,
// past the six splashes and the intro cutscene. Purely a development
// convenience -- there is no such path in the original.
void RunGame(GameContext& ctx, bool skipIntro = false);

// Straight onto the travel chart, with a voyage that has just won its opening
// mission -- the perk, the skill points and the log page Broken Tranquility
// pays out. The `--travel` flag, and the same kind of development convenience
// `--battle` is: the front end reaches the chart through New game.
bool RunTravel(GameContext& ctx);

// One cutaway fight, staged from `attacker:defender[:terrainA:terrainD
// [:distance[:propertyA:propertyD]]]` and replayed until the window closes.
// The `--fight` flag: a development convenience for looking at a scene the
// game only reaches by playing to an attack.
bool RunFight(GameContext& ctx, const std::string& spec);

// Straight into one battle, skipping the boot screens and the front end.
// `mission` may be a mission key ("SP1"), a level stem, or a pak path. Returns
// false if no such battle exists. This is the `--battle` flag: the front end
// reaches the same screen through Single-player game / Hot Seat.
bool RunBattle(GameContext& ctx, const std::string& mission);

}  // namespace bb
