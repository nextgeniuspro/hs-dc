// String ids for the menu tree, taken from the binary.
//
// Every screen looks its labels up by number through the localisation table, so
// these are the game's own ids and the same names appear in all five languages.
// They were recovered by walking the menu builders with tools/ghidra_menus.py
// and tools/ghidra_menutree.py — each screen's items are the ids it passes to
// the string lookup, in the order it adds them.
//
// A few entries the original has are deliberately absent from the port's
// screens -- all of them dead off-device rather than unimplemented. They're
// still listed here, marked, so it's clear they were found and dropped rather
// than missed.
#pragma once

namespace bb {
namespace ids {

// --- main menu (0x1008b764) -------------------------------------------------
constexpr int kSinglePlayer = 1667;
constexpr int kMultiplayer = 1668;
constexpr int kArena = 2320;  // REMOVED from the port: N-Gage Arena is dead
constexpr int kSettings = 1669;
constexpr int kHelp = 2321;
constexpr int kQuit = 2322;

// --- single-player (0x100a79b8) ---------------------------------------------
constexpr int kSinglePlayerTitle = 2111;
constexpr int kLoadGame = 2112;
constexpr int kNewGame = 2113;
// Not one of the original's ids: the port keeps a mission browser alongside
// the campaign, using string 2141 "Open mission" for its label. Its row is
// compiled in only under BB_DEV -- it is a way past the campaign chain rather
// than part of the game -- but the id stays here unconditionally, because a
// number that exists in one build and not another is worse than a dead one.
constexpr int kMissionBrowser = 2141;
constexpr int kTutorial = 2114;

// --- multiplayer (0x10099818) -----------------------------------------------
constexpr int kMultiplayerTitle = 2319;
constexpr int kHotSeat = 2101;
constexpr int kBluetooth = 2102;      // REMOVED from the port
constexpr int kArenaMultiplayer = 2103;  // REMOVED from the port

// --- settings (0x100a6598) --------------------------------------------------
constexpr int kReady = 2209;
constexpr int kSoundSettings = 1525;
constexpr int kBluetoothName = 2305;   // REMOVED from the port
constexpr int kBluetoothOn = 1538;     // REMOVED from the port
constexpr int kBluetoothOff = 1536;    // REMOVED from the port
constexpr int kLanguage = 1524;
constexpr int kKeyConfiguration = 1503;
constexpr int kResetSettings = 1540;
constexpr int kFightAnimationOn = 1544;   // "Turn Fight Animation on"
constexpr int kFightAnimationOff = 1542;  // "Turn Fight Animation off"
constexpr int kEraseGameData = 1545;

// --- sound settings (0x100aafdc) --------------------------------------------
constexpr int kSoundOn = 1527;
constexpr int kSoundOff = 1526;
// REMOVED from the port: there are no calls to be interrupted by off-device.
constexpr int kMuteInCallOn = 1528;
constexpr int kMuteInCallOff = 1529;
constexpr int kCutSceneVolume = 1530;
constexpr int kMusicVolume = 1531;
constexpr int kSfxVolume = 1532;

// --- language (0x100a5b10) --------------------------------------------------
constexpr int kLangEnglish = 62206;
constexpr int kLangFrench = 62207;
constexpr int kLangItalian = 62208;
constexpr int kLangGerman = 62209;
constexpr int kLangSpanish = 62210;

// --- key configuration (0x10081f40) -----------------------------------------
constexpr int kReadyKeys = 1656;
constexpr int kResetKeyConfig = 1505;
constexpr int kSelectKeyToSwitch = 1504;
// The eleven bindable actions are a contiguous run, 1506..1516, listed in the
// order the screen's loop adds them.
constexpr int kFirstKeyAction = 1506;
constexpr int kKeyActionCount = 11;

// --- help (0x1007cb04) ------------------------------------------------------
constexpr int kHelpTitle = 2145;
constexpr int kHelpBattleView = 2138;
constexpr int kHelpTravelView = 2139;
constexpr int kHelpCredits = 62140;

// --- confirmations and status text ------------------------------------------
constexpr int kYes = 2001;
constexpr int kNo = 2002;
constexpr int kCancel = 62148;
constexpr int kConfirmQuit = 1663;
constexpr int kConfirmEraseData = 1500;
constexpr int kConfirmResetSettings = 1501;
constexpr int kConfirmResetKeys = 1502;
constexpr int kFightAnimationIsOn = 1541;
constexpr int kFightAnimationIsOff = 1543;

}  // namespace ids
}  // namespace bb
