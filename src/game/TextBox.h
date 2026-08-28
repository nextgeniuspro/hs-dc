// TextBox — the engine's modal message board (constructed at 0x10065ef0).
//
// One widget carries most of the game's prose: the perk explanation, the
// mission briefing, the win/lose screen, the "you are next" turn card and
// every line of in-battle dialogue a mission script throws up. It is the same
// list widget the menus use (0x10038f90) over layout resource **0xac** instead
// of 0xaa, filled with *text rows* rather than buttons, drawn over
// `Data\Menu\fullboard.tc` and scrolled with up/down.
//
// **Rows** come from 0x1008c378's switch on a kind:
//
//   2   an image, loaded from a path and drawn at a given x
//   3   text, five pixels lower than it would otherwise sit
//   5   plain text on no background at all
//   6   a title: `Data\Menu\header_bg.tc` (176x48, four frames picked at
//       random) with the big font at (5, 0)
//   7   a heading: the same as 5, but in the *big* font (0x1008c83c reaches
//       for slot 0xc2 rather than 0x22) -- and it claims no running y at all,
//       because 0x1008ca04 has no case for it. That is not an oversight to
//       correct: the one screen that uses it, the commander board, always
//       follows the heading with a kind-3 blank, and the blank's fifteen
//       pixels are exactly the room the big font needs.
//   8   `Data\Menu\subtopic.tc` with the small font at (23, 6), five higher
//   9   `Data\Menu\status_bg.tc` with the small font at (47, 9)
//   10  `Data\Menu\3rows.tc` with the small font centred at (11, 2)
//
// **Markup.** A kind-5 line is scanned twice. `[player]` is replaced by the
// commander's name (resource 0xc1) -- the mission scripts write speaker names
// that way, and string 5113 *is* literally "[player]". And `<x path.tc>` (with
// `-` for "centre it") splits the line and inserts an image row: the win and
// lose screens are one of these plus a sentence.
//
// **Soft keys** are chosen by a mode number, which 0x10065ef0 turns into a
// pair of string ids stored in the layout resource. The modes this port uses:
// 0 Ok/Ok, 1 Accept/Retreat, 3 Ok/Cancel, 10 Ok/Back, 12 Next/Exit.
//
// **Scrolling.** A board taller than the screen scrolls one pixel every eight
// milliseconds while up or down is held, and gets the little plank of two
// arrows at the bottom middle that says which way there is still text to go --
// see chrome::DrawScrollPlank in MenuChrome.h.
//
// Not ported: the pointer-driven scroll bars (0xf1/0xf2, a touch device the
// N-Gage never was) and the acceleration that makes holding key 5 scroll three
// pixels a tick instead of one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/Surface.h"

namespace bb {

struct GameContext;
struct Texture;

class TextBox {
public:
    // Row kinds, numbered as the engine numbers them.
    enum Kind {
        kImage = 2,
        kTextLow = 3,
        kPlain = 5,
        kTitle = 6,
        kHeading = 7,
        kSubTopic = 8,
        kStatus = 9,
        kCentred = 10,
    };

    // Which pair of soft-key labels to show (0x10065ef0's switch).
    enum Mode {
        kOkOk = 0,
        kAcceptRetreat = 1,
        kNone = 2,
        kOkCancel = 3,
        kOkBack = 10,
        kNextExit = 12,
        kBuyCancel = 15,   // the skill chart's purchase board
    };

    // What Run() returns.
    enum Result { kQuit = -2, kCancelled = -1, kRunning = 0, kConfirmed = 1 };

    // Layout, from resource 0xac and the row builders.
    static constexpr int kItemX = 5;          // resource 0xac word 3
    static constexpr int kListTop = 17;       // word 1: where the rows start
    static constexpr int kMarginTop = 24;     // word 2: the scroll's stop
    static constexpr int kTitleTextX = 5;     // 0x1008c378 case 6
    static constexpr int kTitleTextY = 0;
    static constexpr int kSubTopicTextX = 0x17;
    static constexpr int kSubTopicTextY = 6;
    static constexpr int kStatusTextX = 0x2f;
    static constexpr int kStatusTextY = 9;
    static constexpr int kScrollPerTick = 1;  // pixels per 8 ms
    static constexpr uint32_t kScrollTickMs = 8;

    explicit TextBox(GameContext& ctx, int mode = kOkOk);

    // Add a row. `Text` runs the markup: it substitutes the commander's name
    // for `[player]` and splits `<x path>` out into image rows.
    void Add(int kind, const std::string& text);
    void Text(const std::string& line) { Add(kPlain, line); }
    void Title(const std::string& line) { Add(kTitle, line); }

    // A line with a small picture at its left: one frame of `path` at x = 0
    // and the text at `textX`, both on one row. The engine builds these out
    // of two rows -- an image row pinned four pixels above the running y and a
    // text row indented past it (0x100ae5ac's money line, 0x10066f10's flag)
    // -- which comes to the same picture and needs a second draw path to say
    // so. Baking one row is the same result with the machinery already here.
    void AddIconLine(const std::string& path, int frame,
                     const std::string& text, int textX);

    // Run modally until a soft key is pressed. Returns Result.
    int Run();

    // One frame: draw and poll. Returns Result; kRunning means keep going.
    // Split out so a caller that owns its own loop (the battle screen, which
    // keeps drawing the map behind its dialogue) can drive it.
    int Step(Surface& dst);

    // Draw without polling, for a caller that wants the box over its own art.
    void Draw(Surface& dst);

    bool Empty() const { return m_Rows.empty(); }
    int RowCount() const { return static_cast<int>(m_Rows.size()); }
    // The text a row was built from, for tests.
    const std::string& RowText(int i) const;
    int RowKind(int i) const;
    // Total height of the laid-out rows.
    int Height() const;
    // How far the content can scroll; zero or less means it all fits, and the
    // scroll plank stays off.
    int ScrollLimit() const;
    int Scroll() const { return m_Scroll; }

    // The two soft-key labels this mode shows. Empty means no key.
    int Mode() const { return m_Mode; }
    const std::string& LeftKey() const { return m_LeftKey; }
    const std::string& RightKey() const { return m_RightKey; }

    // `[player]` -> the commander's name, as 0x10066268 does it. Public
    // because the trigger runner formats speaker names the same way.
    static std::string Substitute(const GameContext& ctx,
                                  const std::string& text);

private:
    struct Row {
        int Kind = kPlain;
        std::string Text;
        Surface Canvas{1, 1};  // baked background + text, empty for plain
        int X = 0;              // where the row is drawn
        int Height = 0;
        bool Plain = false;     // drawn as text straight onto the screen
        bool Big = false;       // ... in the big font: a kind-7 heading
    };

    void Bake(Row& row);
    void AddRaw(int kind, const std::string& text, int x);

    GameContext& m_Ctx;
    int m_Mode = kOkOk;
    std::string m_LeftKey, m_RightKey;
    std::vector<Row> m_Rows;
    int m_Scroll = 0;
    uint32_t m_LastScrollMs = 0;
    const Texture* m_Board = nullptr;       // fullboard.tc, slot 0x32
    const Texture* m_HeaderBg = nullptr;   // 0xd1
    const Texture* m_Subtopic = nullptr;    // 0xdf
    const Texture* m_StatusBg = nullptr;   // 0xe2
    const Texture* m_ThreeRows = nullptr;  // 0xe0
    int m_HeaderFrame = 0;
};

}  // namespace bb
