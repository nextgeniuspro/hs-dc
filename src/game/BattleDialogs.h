// The two things that appear over the battlefield and take the keys away.
//
// **The turn card** (0x100cc460, drawn by 0x100ccbcc and 0x100ccd70) opens
// every turn before you can move. It is four bare planks with things written
// on them, and nothing else:
//
//   `Plank-28-versus.tc`     168x32  at (5, 4)    "You are next" (2065) or
//                                                 "Next Commander" (2064) in
//                                                 the big font at y=4, and
//                                                 "Turn <n>" (2069) at y=19
//   `Plank-27-playerbig.tc`  168x45  at (5, 38)   whoever is about to move:
//                                                 `flags.tc` at (10, 39) and
//                                                 their name over You /
//                                                 Friendly / Enemy at x=95
//   `Plank-26-player.tc`     168x23  at (5, y)    one per other seated player,
//                                                 with `flags-med.tc` at x=10
//                                                 and the text at x=60
//   `Plank-28-versus.tc`     again,  at (5, y+1)  the sides: each team's
//                                                 `flags-small.tc` badges with
//                                                 "vs" (1721) between them
//
// The planks are spaced by their own heights: 45 after the big one, 24 after
// each small one, which leaves a hairline of battlefield between them.
//
// The flag frame is the player's *colour*, not their seat -- the campaign lets
// you choose one on the New game screen, and the name on the big plank is the
// commander name typed there, not the one the level file carries.
//
// **The dialogue box** is what `UI::ShowDialog` puts up (0x100a7044): the same
// parchment furniture the cutscene subtitles use, over the live battlefield.
// It is *not* modal in the usual sense -- the cursor still walks the map
// underneath while it is up, and only the select key belongs to the box. It
// slides in from off-screen, rests, and slides back the way it came, easing a
// quarter of the remaining distance per frame (0x100a729c).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "platform/Surface.h"

namespace bb {

class BattleField;
class Font;
struct GameContext;
struct Texture;

// The board that names whose turn it is.
class TurnCard {
public:
    static constexpr int kStrYouAreNext = 2065;
    static constexpr int kStrNextCommander = 2064;
    static constexpr int kStrTurn = 2069;
    static constexpr int kStrEnemy = 2066;
    static constexpr int kStrFriendly = 2067;
    static constexpr int kStrYou = 2068;
    static constexpr int kStrVersus = 1721;
    // Where everything sits (0x100cc460 / 0x100ccd70's own coordinates).
    static constexpr int kPlankX = 5;
    static constexpr int kHeaderY = 4;
    static constexpr int kTitleY = 4;
    static constexpr int kTurnY = 0x13;
    static constexpr int kListY = 0x26;      // first row's text
    static constexpr int kPlankY = 0x27;     // first row's plank
    static constexpr int kFirstDrop = 0x2d;
    static constexpr int kDrop = 0x18;
    static constexpr int kFirstX = 0x5f;
    static constexpr int kX = 0x3c;
    static constexpr int kFlagX = 0xa;
    static constexpr int kBadgeSpan = 0xaa;  // the versus row's usable width
    static constexpr int kBadgeLeft = 0x52;
    static constexpr int kBadgeY = 6;

    // Build the card for the player about to move. `viewer` is the human.
    void Build(GameContext& ctx, const BattleField& field, int viewer);
    void Draw(GameContext& ctx, const BattleField& field, Surface& dst) const;
    // Run it until dismissed. `backdrop` repaints whatever is behind it --
    // the battlefield, which the original keeps drawing underneath. Returns
    // false if the host wants to quit.
    bool Run(GameContext& ctx, const BattleField& field,
             const std::function<void(Surface&)>& backdrop);

    // For tests.
    const std::string& Title() const { return m_Title; }
    const std::string& TurnLine() const { return m_TurnLine; }
    const std::vector<std::string>& Rows() const { return m_Rows; }

private:
    struct Seat {
        int Slot = 0;
        std::string Text;
    };
    void DrawSides(GameContext& ctx, const BattleField& field, Surface& dst,
                   int y) const;

    std::string m_Title;
    std::string m_TurnLine;
    std::vector<std::string> m_Rows;
    std::vector<Seat> m_Seats;
};

// One line of scripted conversation, on the parchment panel, over the map.
class BattleDialogue {
public:
    // Where the panel comes to rest and how it gets there (0x100a7044).
    static constexpr int kTextX = 3;
    static constexpr int kBodyPad = 6;    // added to the wrapped text's height
    static constexpr int kPlateTextX = 3;
    static constexpr int kEase = 2;       // move (target - y) >> 2 per frame
    // The name plate's own offsets inside the panel.
    static constexpr int kPlateGap = 2;

    enum Phase { kEntering, kResting, kLeaving, kGone };

    // `position` is ShowDialog's third argument: 0 rises from the bottom,
    // 1 drops in from the top.
    void Set(GameContext& ctx, int textID, int speakerID, int position = 0);
    bool Active() const { return m_Phase != kGone; }
    // Ask it to leave. It is only really gone once Update() says so.
    void Dismiss();
    void Clear() { m_Phase = kGone; }
    // One frame of the slide. Returns false once the panel has left.
    bool Update(GameContext& ctx);
    void Draw(GameContext& ctx, Surface& dst) const;

    int Phase() const { return m_Phase; }
    int Top() const { return m_Y; }
    const std::string& Speaker() const { return m_Speaker; }
    const std::string& Line() const { return m_Line; }

private:
    void Measure(GameContext& ctx);

    int m_Phase = kGone;
    int m_Position = 0;
    std::string m_Speaker;
    std::string m_Line;
    std::vector<std::string> m_Lines;
    int m_BodyH = 0;    // the parchment's height
    int m_PlateH = 0;   // the name plate's, zero when nobody is named
    int m_Y = 0;         // the parchment's top edge
    int m_StartY = 0;
    int m_RestY = 0;
};

}  // namespace bb
