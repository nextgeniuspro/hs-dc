// SaveGame — what a saved game contains, and the file it becomes.
//
// The original's shape, which this keeps: one save file per *game type*, not
// a list of numbered slots. 0x10078184 picks the name from the type --
// `tutorial-game.nds`, `hotseat-game.nds`, or the campaign's own name out of
// resource slot 0x1c -- and refuses outright for the types that have no name
// ("Game saving not supported for this game type!"). Settings are not part of
// it: they go to their own file, byte by byte, in 0x100796f0. So there are
// three game slots and one settings slot here, and saving twice overwrites.
//
// That is worth keeping for its own sake, but it matters twice over for a
// memory card. A fixed, tiny set of slots is a fixed, tiny block budget: three
// game saves and the settings come to about a quarter of a VMU and cannot grow
// past it however long the campaign runs. A numbered-slot design would let a
// player fill the card and then be unable to save at all.
//
// **What one save holds**, matching what 0x1007834c writes into a single file:
// the travel engine (the campaign -- commander, colour, perks, and where the
// voyage has got to) and, when the save is taken mid-battle, the battle engine
// as well. Loading a mid-battle save rebuilds the level from the pak and puts
// the snapshot back on top of it, so the blob never carries terrain.
//
// **The file.** A sixteen-byte header and a payload:
//
//     "BBSV"           magic
//     u8  version      kVersion; anything else is refused, not guessed at
//     u8  kind         0 game, 1 settings
//     u8  flags        bit 0: the payload is deflated
//     u8  reserved
//     u32 rawSize     payload length once inflated
//     u32 crc32        of the payload exactly as stored
//
// The CRC is not ceremony. Flash cells go bad, and a save that has rotted must
// read as *broken* rather than as a battlefield with a unit standing inside a
// mountain. Deflate is there because the card bills in 512-byte blocks and a
// battle snapshot roughly halves; if it fails to shrink, the payload is stored
// raw and the flag says so.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/BattleField.h"
#include "game/Campaign.h"
#include "game/TriggerRunner.h"

namespace bb {

class Settings;
class Storage;

// The three save files the original has, by the game type that owns them.
enum class SaveKind { kCampaign, kTutorial, kHotSeat };

// Twelve characters, uppercase and underscore: what a Dreamcast VMU's
// directory can hold and what its file manager can render. See Storage.h.
const char* SlotName(SaveKind kind);
constexpr const char* kSettingsSlot = "HSSETTINGS";

// A battle caught in the middle. `level` is the pak path the field was built
// from; everything else is what has happened to it since.
struct SavedBattle {
    std::string Level;      // "Data\\Levels\\sp1.ndl"
    std::string Name;       // what the objectives panel calls it
    std::string Mission;    // MissionDatabase key ("SP1"); empty for skirmishes
    int Viewer = 1;         // which seat is the human one
    // A random sea battle rather than a place on the chart. The two are
    // bracketed differently -- an encounter has no cutscenes, no briefing and
    // no victory board -- so a resumed save has to know which it is coming
    // back into.
    bool Encounter = false;
    BattleField::Snapshot Field;
    TriggerRunner::State Triggers;
};

struct SavedGame {
    Campaign CampaignData;
    // True when the save was taken from the battle menu rather than between
    // missions. Loading one drops straight back into the battle.
    bool InBattle = false;
    SavedBattle Battle;
};

// Why a save or load did not happen, in the terms the player is owed.
enum class SaveStatus {
    kOk,
    kNoStorage,    // this host has nowhere to put a save
    kTooBig,       // will not fit the slot's budget -- the card is the limit
    kWriteFailed,
    kMissing,      // nothing saved in that slot
    kCorrupt,      // wrong magic, version, length or checksum
};

// A line to show the player. Short, because it goes in a TextBox over the map.
const char* SaveStatusText(SaveStatus s);

// --- the file ---------------------------------------------------------------

// Current format version. Bump on any layout change; old saves are then
// refused as kCorrupt rather than misread.
//
// With one deliberate exception, which the settings file relies on: a purely
// *optional* field, appended after everything else and read back only if
// SaveReader::Remaining() says it is there, is compatible in the direction
// that matters and does not need a bump. That exception is worth having
// because this byte is shared by both kinds -- bumping it to admit one new
// setting would refuse every campaign, tutorial and hot-seat save too.
constexpr uint8_t kSaveVersion = 6;

std::vector<uint8_t> EncodeGame(const SavedGame& game);
bool DecodeGame(const std::vector<uint8_t>& blob, SavedGame& out);

// --- slots ------------------------------------------------------------------

SaveStatus WriteGame(Storage& store, SaveKind kind, const SavedGame& game);
SaveStatus ReadGame(Storage& store, SaveKind kind, SavedGame& out);
bool HasGame(Storage& store, SaveKind kind);

// Everything this game owns, for "Erase game data" (settings included, which
// is what that entry does in the original -- it is the whole of the game's
// footprint, not just the campaign).
void EraseAll(Storage& store);

// --- settings ---------------------------------------------------------------
//
// Their own slot, as in the original: a settings file survives erasing a
// campaign and a campaign survives changing the volume.
SaveStatus WriteSettings(Storage& store, const Settings& settings);
SaveStatus ReadSettings(Storage& store, Settings& out);

}  // namespace bb
