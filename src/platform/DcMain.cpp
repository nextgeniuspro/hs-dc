// Dreamcast entry point for the Blackbeard port.
//
// Where the data comes from, in order:
//
//   /cd/DATA.PAK    the disc -- how a player runs it
//   /pc/data.pak    the host, over dcload/dc-tool -- how a developer runs it,
//                   with no disc to burn between builds
//   /sd/DATA.PAK    an SD card on the serial port -- how a player runs a disc
//   /sd/hs/DATA.PAK built without the game data (scripts/dc-cdi.sh --no-pak),
//                   and a second way to develop without burning anything
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <arch/arch.h>
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
#include "platform/BootScreen.h"
#include "platform/DcSdCard.h"
#include "platform/KosHost.h"
#include "shim/Log.h"
#include "shim/Resources.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

namespace {

// Both spellings of every candidate: ISO9660 hands names back uppercase, a
// host filesystem over dcload does not.
const char* const kDiscPakPaths[] = {
    "/cd/DATA.PAK", "/cd/data.pak", "/pc/data.pak", "/pc/DATA.PAK",
};
const char* const kCardPakPaths[] = {
    "/sd/DATA.PAK", "/sd/data.pak", "/sd/hs/DATA.PAK", "/sd/hs/data.pak",
};
const char* const kIconPaths[] = {
    "/cd/ICONS.PAK", "/cd/icons.pak", "/pc/icons.pak", "/sd/ICONS.PAK",
};
const char* const kFramePaths[] = {
    "/cd/FRAMES.DCF", "/cd/frames.dcf", "/pc/frames.dcf", "/sd/FRAMES.DCF",
};

// What the console's file manager lists for each of the four files this game
// can put on a card, and which of the two icons goes beside it.
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

const char* MountData(bb::Resources& resources) {
    for (const char* path : kDiscPakPaths) {
        if (resources.MountPak(path, "/")) return path;
    }
    if (!bb::MountSdCard()) return nullptr;
    for (const char* path : kCardPakPaths) {
        if (resources.MountPak(path, "/")) return path;
    }
    bb::UnmountSdCard();
    return nullptr;
}

bool AskForData(bb::KosHost& host) {
    bb::LogError("no game data; asking for a copy of data.pak\n");

    const std::string frameWas = host.Frame();
    if (frameWas.empty() && host.FrameCount() > 0)
        host.SetFrame(host.FrameId(0));

    bb::Surface& screen = host.Screen();
    bb::DrawBootChrome(screen, "NO GAME DATA", bb::kBootHeading);
    bb::DrawBootBody(screen, bb::kBootBodyY,
                     "This disc ships without game data. It needs the "
                     "data.pak from your own copy of High Seize."
                     "\n\n"
                     "Put it on an SD card, as /sd/data.pak"
                     " or /sd/hs/data.pak.",
                     bb::kBootInk);
    bb::DrawBootKeys(screen, "Press A to look again", "Press B to quit");

    host.FlushKeys();
    for (;;) {
        host.Flip();
        host.Sleep(16);
        if (host.KeyPressed(bb::Key::kSelect)) return true;
        if (host.KeyPressed(bb::Key::kBack)) return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    dbglog_set_level(bb::LogEnabled(bb::LogLevel::kDebug) ? DBG_KDEBUG
                                                          : DBG_ERROR);

    bb::LogInfo("blackbeard: built %s %s\n", __DATE__, __TIME__);
#ifdef BB_DEV
    arch_set_exit_path(ARCH_EXIT_RETURN);
#else
    arch_set_exit_path(ARCH_EXIT_MENU);
#endif


    bb::KosHost host;
    if (!host.Init()) return 1;

    for (const char* path : kFramePaths) {
        host.OpenFrames(path);
        if (host.FrameCount() > 0) break;
    }

    bb::Resources resources;
    const char* pakPath = MountData(resources);
    while (!pakPath) {
        if (!AskForData(host)) return 1;
        pakPath = MountData(resources);
    }
    bb::LogInfo("pak: %s\n", pakPath);
    for (const char* path : kIconPaths) {
        if (resources.MountPak(path, "/")) break;
    }

    bb::FilePack pack(resources);

    bb::Palette palette;
    if (auto palStream = pack.Open("Data\\palette.pal")) palette.Load(*palStream);
    bb::TextureCache textures(pack, palette);

    host.SetSaveLabels(SaveLabels(pack));
    host.OpenSaves();
    std::string cards;
    for (int bay = 0; bay < host.SaveBayCount(); ++bay) {
        if (!host.SaveBayReady(bay)) continue;
        if (!cards.empty()) cards += ", ";
        cards += host.SaveBayLabel(bay);
    }
    bb::LogInfo("card: %s\n", cards.empty() ? "none" : cards.c_str());

    bb::Strings strings;
    bb::Font smallFont, bigFont;
    bb::Water water;
    bb::Settings settings;
    const bb::Language lang = SystemLanguage();
    settings.CurrentLanguage = lang;
    bb::SoundManager sound(pack);
    sound.Open(host);

    host.SetMemoryProbe([&textures, &sound](std::size_t& tex, std::size_t& snd) {
        tex = textures.Bytes();
        snd = sound.Bytes();
    });


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
