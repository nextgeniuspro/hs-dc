// BattleInfo — the four boards behind the battle menu's Options row.
//
// Options (0x1005ac84) lists six rows and hands each to a state of its own
// (0x1005ade4's switch): Mission objectives, Commander info, Battle info,
// Surrender, Break up team and Map. Surrender is a decision, not a screen, and
// belongs to BattleScreen; Break up team is a Bluetooth message a team-game
// seat sends its allies, and the row is only added when the game type is one
// of the three team types, which the port has no way to be in. The other four
// are here.
//
// **Mission objectives** (0x100ae318 with page 0). The mission's name over its
// objectives -- strings `10000 + n` and `10100 + n`, keyed by the table's
// MISSION_INDEX, the same n the briefing uses. Ok on both soft keys.
//
// **Battle info** (page 1 onwards). A book: the first page is the battle's own
// tally -- rounds played and time elapsed -- and after it comes one page per
// seat, with that seat's treasury and the eight counters the statistics object
// keeps (0x10051438's eight cases). Next walks forward, Back walks back, and
// the soft keys say so: the first page offers Next/Exit, the middle ones
// Next/Back and the last Exit/Back, which is TextBox modes 12, 11 and 13.
//
// **Commander info** (0x10066e84, laid out by 0x1010105c). One page per seat:
// who they are, what they look like, what their skills do to their army, and
// every perk they carry with its description. The same three-mode paging.
//
// For the seat the player holds -- character 13, Player Stevenson, whose
// bonus strings are the `[player]` placeholder and an empty line -- the board
// shows the *skill chart* instead of a canned summary, because that is the one
// commander whose skills the player chose.
//
// **Map** (0x100a77c0). The whole board at five pixels a tile, framed, over
// the battlefield itself. Any soft key closes it. The drawing is
// BattleRenderer::DrawMinimap; what is here is the loop around it.
//
// All four are modal, because the states that raise them are pushed on top of
// the battle and nothing underneath ticks while they are up.
#pragma once

#include "game/BattleRenderer.h"

namespace bb {

class BattleSession;
class TextBox;
struct GameContext;

// Each returns false when the host asked to quit while the board was up, so
// the caller can unwind instead of drawing another frame.
bool ShowMissionObjectives(GameContext& ctx, const BattleSession& session);
bool ShowBattleInfo(GameContext& ctx, const BattleSession& session);
// `viewer` is the seat at the keyboard: its own page opens first, exactly as
// the engine's does (0x1005ade4 case 3 asks the battle for the current seat).
bool ShowCommanderInfo(GameContext& ctx, const BattleSession& session,
                       int viewer);
// The overview is drawn over the battlefield, so it needs the view the caller
// was already looking at: same camera, same cursor, same fog.
bool ShowBattleMap(GameContext& ctx, BattleSession& session,
                   const BattleRenderer::View& view);

// Test seam. The boards above are modal, which makes what is *on* them
// awkward to assert on; this fills a box with one seat's commander page
// exactly as the board does, without running it.
void BuildCommanderBoard(GameContext& ctx, TextBox& box,
                         const BattleSession& session, int seat);

}  // namespace bb
