// CardPicker — which memory card the game saves to.
//
// This is the port's own screen, and the only one in it that exists because of
// the hardware rather than because the original had one. The N-Gage has a
// filesystem, one of it, always there: the engine's file server hangs off the
// engine object at +0x41c and 0x1007834c writes through it without anybody
// choosing anything. A Dreamcast is the other way round. Its storage is
// removable, there may be none of it or four of it, and which card a player
// wants their campaign on is not something the machine can work out.
//
// So on a host that reports card bays (Host::SaveBayCount), the game asks.
//
// **When it asks.** Three moments, and no others:
//
//   * **New game.** Before a campaign is started, so the first checkpoint
//     between missions has somewhere to go. `allowNone` is on here: a player
//     is entitled to play without a card, and is asked to confirm it once --
//     the board says plainly that nothing will be kept -- rather than
//     discovering it three missions later.
//   * **Load game.** Before a save is read, since the game cannot know which
//     card it is on until it is told. `allowNone` is off: there is nothing to
//     do here without a card, so the only answers are a card or Cancel.
//   * **Save.** From the chart's menu or the battle menu, whenever there is
//     nowhere to write -- which is exactly the case for the player who
//     declined at the start. This is what makes declining a decision they can
//     change their mind about, and it is why nothing else ever auto-picks a
//     card on their behalf: the between-mission checkpoint writes to what they
//     chose, or does not write at all.
//
// **What "no card" means afterwards** is precisely `Host::Saves() == nullptr`,
// which every save path in the game already handles: the Save rows grey out,
// `CheckpointCampaign` returns without writing, and the settings file stops
// being kept as well. That last one is a real consequence rather than an
// oversight -- a player who has told the machine not to write to their card
// has told it about all of it.
//
// **What it looks like.** The standard modal furniture, so it belongs to the
// same game as everything else: `Data\Menu\fullboard.tc` behind it, the title
// plank across the top, the small font for the prose and the soft-key planks
// in the corners. What it adds is a strip of four cards across the middle --
// one per controller port, the letter printed on the machine under each -- and
// under that a wooden row for the "Don't save" answer, which is a real string
// in the game's table (1614) and comes out in whatever language is running.
//
// A bay with a card in it is drawn at full alpha, a bay without at a fifth of
// it -- the same trick the scroll plank plays with its two arrows, where a
// direction with nowhere to go is dimmed rather than hidden. The card under
// the cursor swaps to the sprite's second frame, which is the same card with
// its screen lit and a ring round it (tools/makeicons.py). So a glance says
// which ports hold cards and which one is about to be written to, with one
// blit per card and no second cursor on the screen.
//
// The strip is re-read every frame, so a card pushed in while the board is up
// lights up where it stands.
#pragma once

#include "game/SaveGame.h"

namespace bb {

class Storage;
struct GameContext;

// What the player answered.
enum class CardChoice {
    kPicked,     // a card is chosen; ctx.host.Saves() is now that card
    kNone,       // they chose to play without one; Saves() is null
    kCancelled,  // they backed out without deciding anything
    kQuit,       // the host asked to exit while the board was up
};

// Does this host make the player choose? False on the desktop and in the
// tests, where there is one place saves go and nothing to pick between.
bool AsksForCard(const GameContext& ctx);

// Put the board up and run it modally. `allowNone` offers the "Don't save"
// row and the question behind it.
CardChoice PickCard(GameContext& ctx, bool allowNone);

// The save-time front door: whatever card is already chosen, or the board, or
// nothing. Null means there is nowhere to write and the player either has no
// card or has just said no to using one; callers check
// `ctx.host.QuitRequested()` afterwards to tell a refusal from an exit.
Storage* SaveTarget(GameContext& ctx);

// Whether a Save row should be live: there is somewhere to save, or a bay that
// could hold somewhere.
bool CanSave(const GameContext& ctx);

// Whether a Load row should be live for `kind`. A host that asks about cards
// cannot know what is on the ones it has not mounted, so the row stays live
// while there is any card at all and the board sorts out which -- a card with
// nothing on it then says so, which is a better answer than a greyed row that
// cannot explain itself.
bool CanLoad(GameContext& ctx, SaveKind kind);

}  // namespace bb
