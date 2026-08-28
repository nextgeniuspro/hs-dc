// Dreamcast entry point for the Blackbeard port.
//
// The console's equivalent of SdlMain.cpp, and just as short: KallistiOS has
// already brought up the machine by the time main() is called, so this finds
// the data, builds the same objects the desktop build does, and calls
// RunGame(). Everything after that point is the ported game, byte for byte the
// same code the desktop runs.
//
// There is no command line on a Dreamcast, so the development flags SdlMain
// carries (--battle, --cutscene, --texture) have no counterpart here. What the
// console offers instead is its own configuration: the language the game runs
// in comes from the system settings in flash, so a Japanese-region machine set
// to French gets the French text without the player choosing anything.
//
// Where the data comes from, in order:
//
//   /cd/DATA.PAK    the disc -- how a player runs it
//   /pc/data.pak    the host, over dcload/dc-tool -- how a developer runs it,
//                   with no disc to burn between builds
//   /sd/DATA.PAK    an SD card on the serial port, for the same reason
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <arch/arch.h>
#include <dc/biosfont.h>
#include <kos/dbglog.h>
#include <dc/flashrom.h>
#include <dc/video.h>
#include <kos/init.h>
#include <kos/thread.h>

#include "game/FileInputStream.hpp"
#include "game/FilePack.hpp"
#include "game/Font.h"
#include "game/Game.h"
#include "game/SaveGame.h"
#include "game/Palette.h"
#include "game/Settings.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/TextureCache.h"
#include "game/Water.h"
#include "platform/KosHost.h"
#include "shim/Log.h"
#include "shim/Resources.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

namespace {

// Both spellings of every candidate: ISO9660 hands names back uppercase, a
// host filesystem over dcload does not.
const char* const kPakPaths[] = {
    "/cd/DATA.PAK",  "/cd/data.pak",  "/pc/data.pak",
    "/pc/DATA.PAK",  "/sd/DATA.PAK",  "/sd/data.pak", 
    "/sd/hs/DATA.PAK", "/sd/hs/data.pak"
};
const char* const kIconPaths[] = {
    "/cd/ICONS.PAK", "/cd/icons.pak", "/pc/icons.pak", "/sd/ICONS.PAK",
};
const char* const kFramePaths[] = {
    "/cd/FRAMES.DCF", "/cd/frames.dcf", "/pc/frames.dcf", "/sd/FRAMES.DCF",
};

// What the console's file manager lists for each of the four files this game
// can put on a card, and which of the two icons goes beside it.
//
// The short line is what the manager's list shows, so it has to tell one of
// these apart from the next -- four rows all reading "Blackbeard" is how a
// player deletes their campaign meaning to delete a tutorial. Sixteen
// characters each, cut to fit by VmuStorage; ASCII, because the BIOS renders
// its own font and the game's accented languages have no business in it.
struct SlotLabel {
    bb::SaveKind Kind;
    bool IsSettings;
    const char* DescShort;
    const char* DescLong;
};
const SlotLabel kSlotLabels[] = {
    {bb::SaveKind::kCampaign, false, "HS", "HS Campaign"},
    {bb::SaveKind::kTutorial, false, "HS tut", "HS Tutorial"},
    {bb::SaveKind::kHotSeat, false, "HS 2P", "HS Hot Seat"},
    {bb::SaveKind::kCampaign, true, "HS opt", "HS Settings"},
};

// The two icons, in the console's own format (see VmuStorage.h). Read straight
// out of the icon pak and handed on untouched.
constexpr const char* kSaveIconPath = "Data\\icons\\save.icn";
constexpr const char* kOptsIconPath = "Data\\icons\\opts.icn";

bool ReadIcon(bb::FilePack& pack, const char* path, bb::VmsLabel& into) {
    auto stream = pack.Open(path);
    if (!stream || !stream->IsOpen()) return false;
    std::vector<uint8_t> blob(stream->Size());
    if (stream->Read(blob.data(), stream->Size()) != stream->Size())
        return false;
    return into.SetIcon(blob.data(), blob.size());
}

// Build the table the memory card writes its headers from. A missing icon pak
// is not an error: the saves then wear the plain fallback header they wore
// before there were any icons, and the file manager shows them as blank tiles.
bb::VmuStorage::Labels SaveLabels(bb::FilePack& pack) {
    bb::VmsLabel save, opts;
    const bool haveSave = ReadIcon(pack, kSaveIconPath, save);
    const bool haveOpts = ReadIcon(pack, kOptsIconPath, opts);
    if (!haveSave || !haveOpts)
        bb::LogError("card icons: save %s, settings %s\n",
                     haveSave ? "ok" : "missing",
                     haveOpts ? "ok" : "missing");

    bb::VmuStorage::Labels labels;
    for (const SlotLabel& s : kSlotLabels) {
        bb::VmsLabel label = s.IsSettings ? opts : save;
        label.DescShort = s.DescShort;
        label.DescLong = s.DescLong;
        labels[s.IsSettings ? bb::kSettingsSlot : bb::SlotName(s.Kind)] =
            std::move(label);
    }
    return labels;
}

// The console's own language setting, mapped onto the five the game ships.
// Anything else -- Japanese, or a machine whose flash cannot be read -- gets
// English, which is what the disc's default text is.
bb::Language SystemLanguage() {
    flashrom_syscfg_t cfg{};
    if (flashrom_get_syscfg(&cfg) < 0) return bb::Language::kEn;
    switch (cfg.language) {
        case FLASHROM_LANG_GERMAN:  return bb::Language::kGe;
        case FLASHROM_LANG_FRENCH:  return bb::Language::kFr;
        case FLASHROM_LANG_SPANISH: return bb::Language::kSp;
        case FLASHROM_LANG_ITALIAN: return bb::Language::kIt;
        default:                    return bb::Language::kEn;
    }
}

// Nothing to play: say so on the television rather than exiting to a black
// screen, since a console gives the player nowhere else to look. The BIOS font
// is the only text this program can draw -- the game's own fonts are in the
// pak that is missing -- and it draws into the game's screen like everything
// else, so the panel goes out through the same PVR path a frame does.
//
// Its glyphs are 12x24, so a 176-pixel screen holds fourteen characters. Each
// line is written short rather than wrapped: a bfont run does not stop at the
// end of a row, it walks straight into the next one.
[[noreturn]] void Fatal(bb::KosHost& host,
                        std::initializer_list<const char*> lines) {
    for (const char* line : lines) bb::LogError("%s\n", line);

    bb::Surface& screen = host.Screen();
    screen.Fill(0xf000);
    int y = 60;
    for (const char* line : lines) {
        bfont_draw_str(screen.Pixels() + y * screen.Width() + 4,
                       screen.Width(), 1, line);
        y += 26;
    }
    for (;;) {
        host.Flip();
        host.Sleep(100);
    }
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // KallistiOS has a log of its own, and it says the same things every boot:
    // the video mode it just set, that the drive noticed a disc, that a save
    // was rounded up to a whole block. None of it is news, and all of it goes
    // down the same serial line the game's own output does. Errors still come
    // through -- those are worth a line -- and a verbose build gets the lot.
    //
    // Set here rather than later because the first of those messages comes out
    // of KosHost::Init a few lines below.
    dbglog_set_level(bb::LogEnabled(bb::LogLevel::kDebug) ? DBG_KDEBUG
                                                          : DBG_ERROR);

    // First line out, so a log always says which binary produced it. Three
    // memory faults into this port, "are you running the build I fixed?" is a
    // question worth being able to answer from the log alone.
    bb::LogInfo("blackbeard: built %s %s\n", __DATE__, __TIME__);

    // Where the console goes when the game is over -- when Quit is confirmed,
    // or when anything below returns early. Set before the first of those can
    // happen, because `main` returning is what runs it.
    //
    // KOS's default is ARCH_EXIT_RETURN: hand control back to whatever loaded
    // the binary. Over dc-tool that is the loader still sitting on the other
    // end of the serial or BBA link, so quitting drops straight back to a
    // prompt and the next build goes down the wire without power-cycling the
    // machine -- which is the whole reason to develop that way.
    //
    // Off a disc there is no loader to return to, and a player who chose Quit
    // has asked to stop playing: ARCH_EXIT_MENU puts them in the console's own
    // menu, where the disc can be swapped and the card they just saved to can
    // be browsed. So this follows the same switch the development-only menu
    // rows do -- a build meant for a machine on a desk, or one meant for a
    // television.
#ifdef BB_DEV
    arch_set_exit_path(ARCH_EXIT_RETURN);
#else
    arch_set_exit_path(ARCH_EXIT_MENU);
#endif

    // The host comes up first: it owns the screen, which is where anything
    // that goes wrong from here has to be said.
    bb::KosHost host;
    if (!host.Init()) return 1;

    bb::Resources resources;
    bool mounted = false;
    for (const char* path : kPakPaths) {
        if (resources.MountPak(path, "/")) {
            bb::LogInfo("pak: %s\n", path);
            mounted = true;
            break;
        }
    }
    if (!mounted) {
        Fatal(host, {"NO GAME DATA", "", "Put DATA.PAK", "on the disc",
                     "beside", "1ST_READ.BIN"});
    }
    // The soft-key hint icons ship in their own little pak (tools/makeicons.py).
    // Missing is fine -- the labels just go without icons.
    for (const char* path : kIconPaths) {
        if (resources.MountPak(path, "/")) break;
    }

    bb::FilePack pack(resources);

    bb::Palette palette;
    if (auto palStream = pack.Open("Data\\palette.pal")) palette.Load(*palStream);
    bb::TextureCache textures(pack, palette);

    // A memory card in any slot takes the saves at boot, which is what lets
    // the settings file be read before anything is drawn. The *game* saves are
    // not left to that: starting or loading a game puts the card picker up and
    // the player says which port they mean (game/CardPicker.h). Which cards
    // are in the machine is the first thing worth knowing when a player says
    // their game did not keep, so it stays in a quiet log -- as one line
    // naming the ports that have one, rather than four naming the empties too.
    host.SetSaveLabels(SaveLabels(pack));
    host.OpenSaves();
    std::string cards;
    for (int bay = 0; bay < host.SaveBayCount(); ++bay) {
        if (!host.SaveBayReady(bay)) continue;
        if (!cards.empty()) cards += ", ";
        cards += host.SaveBayLabel(bay);
    }
    bb::LogInfo("card: %s\n", cards.empty() ? "none" : cards.c_str());
    // The device frames drawn either side of the field. Missing is fine: the
    // sides then stay black and the settings list loses its Frame row. Which
    // one is showing comes from the settings, and RunGame applies that.
    for (const char* path : kFramePaths) {
        host.OpenFrames(path);
        if (host.FrameCount() > 0) break;
    }

    bb::Strings strings;
    bb::Font smallFont, bigFont;
    bb::Water water;
    bb::Settings settings;
    const bb::Language lang = SystemLanguage();
    settings.CurrentLanguage = lang;
    bb::SoundManager sound(pack);
    sound.Open(host);

    // Let the host's memory line say how much of the heap the game can account
    // for. The host does not know what a texture is; this is the entry point
    // telling it who to ask.
    host.SetMemoryProbe([&textures, &sound](std::size_t& tex, std::size_t& snd) {
        tex = textures.Bytes();
        snd = sound.Bytes();
    });

    // And where the samples come from, for the times the game loop is not the
    // one asking. The game pumps the mixer itself every frame; a memory card
    // write blocks that loop for a second or two, and this is what lets the
    // host go on mixing through it (platform/AudioKeepalive.h). The same
    // arrangement as the memory line above: the host does not know what a
    // sound is, so the entry point tells it who to ask.
    host.SetAudioPump([&host, &sound] { sound.Pump(host); });

    bb::GameContext ctx{
        host, 
        pack, 
        palette, 
        textures,
        strings, 
        smallFont, 
        bigFont, 
        water,
        settings, 
        lang, 
        &sound
    };
    bb::RunGame(ctx);
    return 0;
}
