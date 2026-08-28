// MissionFlow — everything that happens around a campaign battle.
//
// The campaign is not a list: the player sails the travel map (TravelMap.h)
// and a mission starts because the ship arrived somewhere. This file is what
// happens once it has.
//
// The engine splits that between the travel map's launcher (0x100dc2b8) and
// the battle runner (0x1008b028), and between them they do:
//
//   1  the location's `animationbefore` cutscene, if it has one
//   2  the briefing: a TextBox in mode 3 (Ok / Cancel) whose title is the
//      mission's name and whose body is its description (0x1008a1a0). Cancel
//      backs out without playing.
//   3  the battle
//   4  "Mission successful!" or "Mission failed" over the victory/defeat
//      picture and the mission's own closing line (0x1008a770, mode 12)
//   5  one "Battle info" board per player: who won, and the tally of turns,
//      gold, properties and units (0x1008a9c8). Backing out of these returns
//      to step 4, which is why the original loops the two together.
//   6  `animationcomplete` (or `animationfail`) on the way out
//
// Steps 1, 3 and 6 each get a loading board on the way in, which is what
// System::showLoading is for.
//
// Per-mission text lives in the main string table at a base derived from the
// table's MISSION_INDEX: name at 10000 + n, the travel blurb at 10200 + n, the
// briefing at 10300 + n, the defeat line at 10500 + n and the victory line at
// 10600 + n.
#pragma once

#include <string>

namespace bb {

class StateMachine;
struct GameContext;
struct SavedBattle;

// Where a mission ended.
enum class MissionResult {
    kWon,
    kLost,
    kQuit,        // the host wants the game gone
    kAbandoned,   // "End current game": back to the main menu
    kDeclined,    // backed out of the briefing
    kUnavailable, // the level or its table entry is missing
};

// String id bases, keyed by the table's MISSION_INDEX.
constexpr int kMissionNameBase = 10000;
constexpr int kMissionBlurbBase = 10200;
constexpr int kMissionBriefBase = 10300;
constexpr int kMissionDefeatBase = 10500;
constexpr int kMissionVictoryBase = 10600;

// Run one campaign mission end to end. `key` is a MissionDatabase key ("SP1").
//
// `resume` restarts a battle that was saved from the battle menu. The run-up
// -- the location's cutscene and the briefing -- is skipped, because it
// already happened before the save was taken; everything after the battle is
// the same, so a resumed mission still gets its result and info boards.
MissionResult RunMission(GameContext& ctx, const std::string& key,
                         const SavedBattle* resume = nullptr);

// The campaign: the travel map, and whatever sailing around it turns up.
// Reaching an open location runs its mission and, on a win, opens the places
// that were waiting on it; a random encounter opens the "Encounter" board and,
// if it is accepted, a skirmish. Blocks until the player leaves the map.
//
// A new commander never sees the map first. `<playerstart>` is twelve pixels
// from SP1 and SP1 is the only place the world opens with, so the voyage
// begins already moored at Broken Tranquility and it plays straight away --
// New game, `02-Caribbean`, the battle, `03-CrocoGreg`, and only then the
// chart.
//
// `resume` is a mid-battle save being picked up again: the battle runs first
// and the chart follows it, exactly as if the player had just arrived there.
void RunCampaign(GameContext& ctx, StateMachine& sm,
                 const SavedBattle* resume = nullptr);

// Write the campaign as it stands into its own slot, if this host has one.
// Called at the points the voyage actually moves on -- a mission settled, a
// place opened, the chart put away -- and nowhere else: a memory card is
// flash, so writes are slow and the erase cycles are finite. Quiet on success;
// a failure is logged, because a checkpoint the player did not ask for should
// not interrupt them with a dialog.
//
// "If this host has one" now covers a second case, and deliberately: a player
// on a console who was offered a card and said no leaves `Host::Saves()` null,
// and this writes nothing at all rather than picking a card for them. Their
// game is saved when they ask for it from a Save row and not before. See
// game/CardPicker.h.
void CheckpointCampaign(GameContext& ctx);

}  // namespace bb
