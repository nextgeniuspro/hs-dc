// SDL2 entry point for the Blackbeard port.
//
// Stands in for the Symbian app shell: instead of the launcher -> app factory
// -> document -> AppUi -> control -> one-shot-timer chain the original goes
// through, this opens a window and calls RunGame() directly. Everything after
// that point is the ported game.
//
//   blackbeard                               play the boot flow
//   blackbeard <data.pak>                    ...from a pak somewhere else
//   blackbeard --import <data.pak>           take a copy and print where it
//                                            went; no window, no game
//   blackbeard <data.pak> --texture <path>   single-texture viewer (Left/Right)
//   blackbeard <data.pak> --cutscene <name>  play one cutscene, e.g. 01-Intro
//   blackbeard <data.pak> --battle <name>    play one battle, e.g. SP1
//   blackbeard <data.pak> --fight <spec>     one cutaway fight, e.g. 0:7
//   blackbeard <data.pak> --menu             skip the splashes and the intro
//   blackbeard <data.pak> --travel           straight onto the travel chart
//   blackbeard <data.pak> --scale N          screen zoom (default 3); the
//                                            window is 16:9 around it
//   blackbeard <data.pak> --window WxH       an explicit window size instead
//   blackbeard <data.pak> --verbose          log every asset, bank and screen
//
// The pak is optional and usually left off: a build that has been run before
// has a copy of its own, and one that has not asks for it (ImportScreen.h).
// Naming one on the command line is for playing a second copy without
// importing it -- which is what every flag above is really for too.
//
// The window is resizable and F11 (or Alt+Enter) makes it fullscreen. The
// screen keeps its 176x208 shape whatever the window does; the space either
// side of it is the device frame's.
//
// Environment:
//   BB_TEXTURE=Data\...\foo.tc   same as --texture
//   BB_CUTSCENE=01-Intro         same as --cutscene
//   BB_BATTLE=SP1                same as --battle
//   BB_FIGHT=0:7                 same as --fight
//   BB_MENU=1                    same as --menu
//   BB_TRAVEL=1                  same as --travel
//   BB_SCALE=4                   same as --scale
//   BB_FRAMES=/path/to/frames    where the device frame art lives
//   BB_WINDOW=1280x1024          same as --window
//   BB_LANG=EN|FR|IT|GE|SP       which localised Nokia logo to show
//   BB_FLIPS=N                   quit after N presented frames (smoke tests)
//   BB_SHOT=out.bmp              save the final presented frame
//   BB_VERBOSE=1                 same as --verbose
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL.h>

#include "game/Boot.h"
#include "game/Cutscene.h"
#include "game/FilePack.hpp"
#include "game/Font.h"
#include "game/Game.h"
#include "game/Palette.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/Settings.h"
#include "game/Water.h"
#include "game/TextureCache.h"
#include "platform/DataFiles.h"
#include "platform/ImportScreen.h"
#include "platform/SdlHost.h"
#include "shim/Log.h"
#include "shim/Resources.h"

namespace {

// "1280x1024" -- an explicit window size, for a display the default 16:9 suits
// badly and for looking at the presentation in a shape you would otherwise
// have to drag the window into.
void ParseSize(const char* s, int& w, int& h) {
    if (!s) return;
    const char* x = std::strpbrk(s, "xX*");
    if (!x) return;
    const int pw = std::atoi(s);
    const int ph = std::atoi(x + 1);
    if (pw > 0 && ph > 0) {
        w = pw;
        h = ph;
    }
}

// The directory part of a path, or "." if there is none.
std::string DirOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".")
                                      : path.substr(0, slash);
}

// Where a port asset -- the soft-key icons, the device frame art -- might be.
// These are the port's own files rather than the game's, so they are not in
// data.pak and something has to go looking: an explicit override first, then
// beside the data this run was given, then beside the binary, and last the
// repo's own assets/.
//
// Anchored on the binary rather than on the working directory. The two are the
// same run -- `./build/blackbeard data.pak` from `port/` and `./blackbeard
// data.pak` from `port/build/` -- and only the second used to find anything,
// which is the whole reason the device frames looked like a console-only
// feature: nothing was missing, nobody was looking in the right place. A frame
// that is not found is not an error (see OpenFrames), so it failed silently,
// taking the settings screen's Frame row with it.
std::vector<std::string> AssetPaths(const std::string& name, const char* envVar,
                                    const std::string& pakPath) {
    std::vector<std::string> out;
    if (const char* env = std::getenv(envVar)) out.push_back(env);
    out.push_back(DirOf(pakPath) + "/" + name);
    // The directory the executable is in, whichever one that is. It can fail
    // -- on a platform SDL has no answer for -- and then this run has only the
    // relative paths below, which is what it had before.
    if (char* base = SDL_GetBasePath()) {
        const std::string dir = base;  // SDL guarantees a trailing separator
        SDL_free(base);
        out.push_back(dir + name);                    // installed beside the app
        out.push_back(dir + "assets/" + name);        // ...or in its assets/
        out.push_back(dir + "../assets/" + name);     // port/build/blackbeard
        out.push_back(dir + "../../assets/" + name);  // a multi-config build
    }
    // Working-directory paths last: a repo checkout with the binary somewhere
    // else entirely still finds its art if it is run from the right place.
    out.push_back("port/assets/" + name);
    out.push_back("assets/" + name);
    out.push_back("../assets/" + name);
    return out;
}

bb::Language ParseLanguage(const char* s) {
    if (!s) return bb::Language::kEn;
    if (!std::strcmp(s, "FR")) return bb::Language::kFr;
    if (!std::strcmp(s, "IT")) return bb::Language::kIt;
    if (!std::strcmp(s, "GE")) return bb::Language::kGe;
    if (!std::strcmp(s, "SP")) return bb::Language::kSp;
    return bb::Language::kEn;
}

// Single-texture viewer: the tool that proved the decoder out, kept because it
// stays the fastest way to eyeball a newly named asset.
void ViewTexture(bb::SdlHost& host, bb::TextureCache& textures,
                 const std::string& path) {
    const bb::Texture* tex = textures.Load(path);
    if (!tex) return;
    std::printf("texture: %s  %dx%d, %zu frame(s)%s\n", path.c_str(), tex->Width,
                tex->Height, tex->Frames.size(),
                tex->Complete ? "" : "  (some frames undecodable)");
    int index = 0;
    while (!host.QuitRequested()) {
        bb::Surface& screen = host.Screen();
        screen.Fill(0x1112u);  // dark slate, so alpha edges show
        if (const auto* f = tex->Frame(index)) {
            screen.Blit(f->Pixels.data(), f->Width, f->Height,
                        (screen.Width() - f->Width) / 2,
                        (screen.Height() - f->Height) / 2);
        }
        host.Flip();
        const int n = static_cast<int>(tex->Frames.size());
        if (n > 1) {
            if (host.KeyPressed(bb::Key::kRight)) index = (index + 1) % n;
            if (host.KeyPressed(bb::Key::kLeft)) index = (index + n - 1) % n;
        }
        host.Sleep(16);
    }
}

// The caustics layer on its own, with nothing drawn over it. Handy for judging
// the ripple and the fish, which are otherwise mostly hidden behind menus.
void ViewWater(bb::SdlHost& host, bb::TextureCache& textures) {
    bb::Water water;
    if (!water.Load(textures)) return;
    while (!host.QuitRequested()) {
        water.Draw(host.Screen());
        host.Flip();
        host.Sleep(20);
    }
}

// One cutscene on its own, with only the assets it needs loaded. There are 31
// of them and every one goes through the same player, so this is how to look
// at any of them without playing to that point in the campaign.
void ViewCutscene(bb::SdlHost& host, bb::FilePack& pack,
                  bb::TextureCache& textures, bb::Language lang,
                  const std::string& name) {
    bb::Strings strings;
    strings.Load(pack, lang);
    bb::Font font;
    font.Load(textures, "Data\\font-small.tc", 11);
    // The subtitle panel comes out of the startup loader's texture set.
    textures.LoadStartupTextures();
    bb::SoundManager sound(pack);
    sound.Open(host);
    bb::PlayCutscene(host, pack, textures, strings, font, name, &sound);
}

const char* const kUsage =
    "usage: blackbeard [data.pak] [--import <data.pak>] [--texture <asset.tc>] "
    "[--cutscene <name>] [--battle <mission>] [--fight <spec>] [--menu] "
    "[--travel] [--scale N] [--window WxH] [--verbose]\n";

}  // namespace

int main(int argc, char** argv) {
    std::string pakArg;
    std::string importPath;
    std::string texturePath;
    std::string cutscene;
    std::string battle;
    std::string fight;
    bool waterOnly = false;
    bool skipIntro = false;
    bool travel = false;
    int scale = 3;
    int winW = 0, winH = 0;
    // The pak is the one argument with no flag in front of it, and it is
    // optional: a build that has been run before already has a copy of its
    // own (platform/DataFiles.h).
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            if (pakArg.empty()) pakArg = argv[i];
            else bb::LogError("ignoring extra argument: %s\n", argv[i]);
        } else if (!std::strcmp(argv[i], "--help") ||
                   !std::strcmp(argv[i], "-h")) {
            std::printf("%s", kUsage);
            return 0;
        } else if (!std::strcmp(argv[i], "--import") && i + 1 < argc)
            importPath = argv[++i];
        else if (!std::strcmp(argv[i], "--texture") && i + 1 < argc)
            texturePath = argv[++i];
        else if (!std::strcmp(argv[i], "--cutscene") && i + 1 < argc)
            cutscene = argv[++i];
        else if (!std::strcmp(argv[i], "--battle") && i + 1 < argc)
            battle = argv[++i];
        else if (!std::strcmp(argv[i], "--fight") && i + 1 < argc)
            fight = argv[++i];
        else if (!std::strcmp(argv[i], "--menu"))
            skipIntro = true;
        else if (!std::strcmp(argv[i], "--travel"))
            travel = true;
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--water"))
            waterOnly = true;
        else if (!std::strcmp(argv[i], "--window") && i + 1 < argc)
            ParseSize(argv[++i], winW, winH);
        else if (!std::strcmp(argv[i], "--verbose") ||
                 !std::strcmp(argv[i], "-v"))
            bb::SetLogLevel(bb::LogLevel::kDebug);
    }
    if (std::getenv("BB_VERBOSE")) bb::SetLogLevel(bb::LogLevel::kDebug);
    if (texturePath.empty()) {
        if (const char* env = std::getenv("BB_TEXTURE")) texturePath = env;
    }
    if (cutscene.empty()) {
        if (const char* env = std::getenv("BB_CUTSCENE")) cutscene = env;
    }
    if (battle.empty()) {
        if (const char* env = std::getenv("BB_BATTLE")) battle = env;
    }
    if (!skipIntro && std::getenv("BB_MENU")) skipIntro = true;
    if (!travel && std::getenv("BB_TRAVEL")) travel = true;
    if (const char* env = std::getenv("BB_SCALE")) scale = std::atoi(env);
    if (scale < 1) scale = 1;
    if (const char* env = std::getenv("BB_WINDOW")) ParseSize(env, winW, winH);

    // `--import <data.pak>`: what the import screen does, without the screen.
    // For a script that is setting a machine up rather than a player sitting
    // at one -- and it is how the copy itself is tested, since a drag onto a
    // window is not something a test can perform.
    if (!importPath.empty()) {
        if (const std::string why = bb::PakProblem(importPath); !why.empty()) {
            bb::LogError("import: %s %s\n", importPath.c_str(), why.c_str());
            return 1;
        }
        std::string error;
        const std::string landed = bb::ImportPak(importPath, nullptr, error);
        if (landed.empty()) {
            bb::LogError("import: %s\n", error.c_str());
            return 1;
        }
        std::printf("%s\n", landed.c_str());
        return 0;
    }

    // Which data this run plays, decided before anything is opened.
    //
    // A pak named on the command line (or in BB_PAK) is taken at its word and
    // a bad one is fatal: a script that passed a path wants to be told it was
    // wrong, not shown a window asking for another. A run that names nothing
    // searches the usual places, and if the search comes up empty the import
    // screen asks the player for their copy -- which is the ordinary state of
    // a build someone has just downloaded, since none ships with data.
    std::string pakPath;
    {
        const char* named = !pakArg.empty() ? pakArg.c_str()
                                            : std::getenv("BB_PAK");
        if (named && *named) {
            if (const std::string why = bb::PakProblem(named); !why.empty()) {
                bb::LogError("pak: %s %s\n", named, why.c_str());
                return 1;
            }
            pakPath = named;
        } else {
            pakPath = bb::FindPak();
        }
    }

    // The window comes up before the data does now, because the screen that
    // asks for the data is drawn in it.
    bb::SdlHost host;
    if (!host.Init(scale, winW, winH)) return 1;
    if (const char* flips = std::getenv("BB_FLIPS"))
        host.SetFlipLimit(std::atol(flips));
    // The device frames -- the picture of the phone drawn either side of the
    // screen -- are looked for the same way the icons are. Missing is fine,
    // the sides just go black; said out loud all the same, because a frame
    // nobody can find and a frame nobody installed look identical on screen.
    //
    // Looked for before the pak rather than after it: the import screen wears
    // one too, and the candidates that find anything are anchored on the
    // binary, not on a pak this run may not have yet.
    {
        bool found = false;
        for (const std::string& c : AssetPaths("frames", "BB_FRAMES", pakPath)) {
            host.OpenFrames(c);
            if (host.FrameCount() == 0) continue;
            bb::LogDebug("frames: %s (%d)\n", c.c_str(), host.FrameCount());
            found = true;
            break;
        }
        if (!found) bb::LogDebug("frames: none found, sides stay black\n");
    }

    // BB_SHOT saves whatever was last presented, wherever the run ended --
    // including the import screen, which is otherwise the one screen in the
    // port no smoke test could photograph.
    const auto saveShot = [&host] {
        if (const char* shot = std::getenv("BB_SHOT")) {
            std::printf("screenshot: %s (%s)\n", shot,
                        host.SaveScreenshot(shot) ? "ok" : "failed");
        }
    };

    if (pakPath.empty()) {
        pakPath = bb::RunImportScreen(host);
        if (pakPath.empty()) {  // they closed the window instead
            saveShot();
            return 0;
        }
    }

    bb::Resources resources;
    if (!resources.MountPak(pakPath, "/")) {
        bb::LogError("pak: failed to open %s\n", pakPath.c_str());
        return 1;
    }
    // Which data this run is playing: the one line worth having in a quiet log,
    // for the same reason the console build stamps its build date.
    bb::LogInfo("pak: %s\n", pakPath.c_str());
    // The soft-key hint icons ship in their own little pak (built by
    // tools/makeicons.py). Missing is fine -- the labels just go without icons.
    for (const std::string& c : AssetPaths("icons.pak", "BB_ICONS", pakPath)) {
        if (!resources.MountPak(c, "/")) continue;
        bb::LogDebug("icons: %s\n", c.c_str());
        break;
    }
    bb::FilePack pack(resources);

    bb::Palette palette;
    if (auto palStream = pack.Open("Data\\palette.pal")) {
        if (palette.Load(*palStream))
            bb::LogDebug("palette: %zu entries\n", palette.Size());
    }
    bb::TextureCache textures(pack, palette);

    // Saved games sit beside the data, which is where a player would look for
    // them -- and for an imported copy that is the port's own directory, which
    // is somewhere a player can always write. BB_SAVES moves them, which is
    // what the smoke tests use so a test run cannot overwrite a real campaign.
    {
        std::string saves;
        if (const char* env = std::getenv("BB_SAVES")) {
            saves = env;
        } else {
            saves = DirOf(pakPath) + "/saves";
        }
        host.OpenSaves(saves);
        bb::LogDebug("saves: %s\n", saves.c_str());
    }
    // BB_KEYS="40:select,80:softleft": inject key presses at given flips, so a
    // smoke test can walk through screens no env flag jumps to.
    if (const char* keys = std::getenv("BB_KEYS")) {
        static const std::pair<const char*, bb::Key> kNames[] = {
            {"up", bb::Key::kUp},           {"down", bb::Key::kDown},
            {"left", bb::Key::kLeft},       {"right", bb::Key::kRight},
            {"select", bb::Key::kSelect},   {"softleft", bb::Key::kSoftLeft},
            {"softright", bb::Key::kSoftRight}, {"back", bb::Key::kBack},
            {"info", bb::Key::kInfo},       {"nextunit", bb::Key::kNextUnit},
            {"prevunit", bb::Key::kPrevUnit},
            {"range", bb::Key::kRange},     {"map", bb::Key::kMap},
        };
        std::string spec = keys;
        std::size_t at = 0;
        while (at < spec.size()) {
            std::size_t end = spec.find(',', at);
            if (end == std::string::npos) end = spec.size();
            const std::string item = spec.substr(at, end - at);
            const std::size_t colon = item.find(':');
            if (colon != std::string::npos) {
                const long flip = std::atol(item.substr(0, colon).c_str());
                const std::string name = item.substr(colon + 1);
                for (const auto& [n, k] : kNames)
                    if (name == n) host.ScheduleKey(flip, k);
            }
            at = end + 1;
        }
    }

    if (waterOnly) {
        ViewWater(host, textures);
    } else if (!texturePath.empty()) {
        ViewTexture(host, textures, texturePath);
    } else if (!cutscene.empty()) {
        ViewCutscene(host, pack, textures, ParseLanguage(std::getenv("BB_LANG")),
                     cutscene);
    } else {
        bb::Strings strings;
        bb::Font smallFont, bigFont;
        bb::Water water;
        bb::Settings settings;
        const bb::Language lang = ParseLanguage(std::getenv("BB_LANG"));
        settings.CurrentLanguage = lang;
        bb::SoundManager sound(pack);
        sound.Open(host);
        bb::GameContext ctx{host,    pack,       palette,  textures,
                            strings, smallFont, bigFont, water,
                            settings, lang,      &sound};
        if (!fight.empty())
            bb::RunFight(ctx, fight);
        else if (!battle.empty())
            bb::RunBattle(ctx, battle);
        else if (travel)
            bb::RunTravel(ctx);
        else
            bb::RunGame(ctx, skipIntro);
    }

    saveShot();
    return 0;
}
