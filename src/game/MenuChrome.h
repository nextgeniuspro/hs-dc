// MenuChrome — the furniture every list screen in the game is built out of.
//
// The engine has one list widget (0x10038f90) and every front-end page is that
// widget filled with two kinds of row: *items*, which are wooden buttons you
// can land on, and *text rows* (0x1008c378), which are captions you cannot.
// Both share one running y, so a caption pushes the buttons below it down.
//
// Geometry comes from layout resource **0xaa**, four words wide:
//
//   word 0  0   spacing added between rows
//   word 1  17  where the running y starts
//   word 2  24  bottom margin used when the list scrolls
//   word 3  5   the x every row is drawn at
//
// and the item style from resource **0xad** = `{20, 2, 0}`: label at (20, 2)
// on style 0, `shell_board.tc`.
//
// The two text rows this header covers:
//
//   kind 6, the page title. `Data\Menu\header_bg.tc` is a 176x48 plank with
//   four different trinkets nailed to its right end -- a compass, a skull, a
//   cutlass, a coil of rope -- and 0x1008c378 picks one **at random** every
//   time a page is built. The row ignores the running x and y and pins itself
//   to (0, 0); the big font goes on at (5, 0). It only *claims* the height of
//   its text plus five (0x1008ca04), so the first button lands at y=41, right
//   where the plank's ragged bottom edge fades out.
//
//   kind 8, a caption. `Data\Menu\subtopic.tc` is a grey stone slab hung on two
//   iron rings, drawn five pixels down inside a row five taller than itself,
//   with the small font at (23, 6). The row claims nine pixels *less* than it
//   occupies, so the rings always hang over whatever is drawn below.
//
//   kind 10, a question. `Data\Menu\3rows.tc` is the same slab three lines
//   deep -- 176x50 -- drawn two pixels down with the small font *wrapped*
//   inside it at (11, 2) (0x1008c378 case 10 hands the text to 0x10072724,
//   the wrapping draw, rather than to the single-line one the other rows use).
//   It claims nine pixels less than it occupies, like the caption, so its rings
//   hang over the first button under it. This is what every confirmation board
//   in the game puts its question on -- the quit prompt (0x10060554), the reset
//   prompts, and the battle's Surrender board (0x10061444).
//
// **The scroll plank** (0x1003a8e4). Any page with more content than screen
// gets a little 30x25 board nailed to the bottom middle with an arrow above
// and an arrow below it. Neither arrow ever disappears: both are drawn every
// frame, and it is their *alpha* that says which way there is still content to
// go. The engine takes `ratio = scroll * 10 / maximum` clamped to 0..10 and
// draws the up arrow at `ratio + 5` and the down arrow at `15 - ratio`, so at
// the top the up arrow is a fifth as bright as the down one and at the bottom
// the pair have swapped. It is a scrollbar drawn as two lamps.
#pragma once

#include <string>

namespace bb {

class Surface;
class TextureCache;
struct GameContext;

namespace chrome {

// The plank and its two arrows, at the coordinates 0x1003a8e4 draws them.
constexpr int kScrollBgX = 0x48, kScrollBgY = 0xb7;   // 72, 183
constexpr int kScrollArrowX = 0x4f;                   // 79
constexpr int kScrollUpY = 0xbc, kScrollDownY = 0xc7; // 188, 199
constexpr int kScrollDim = 5, kScrollBright = 15;

// Draw the plank. `scroll` is how far the content has travelled and `most` how
// far it can; passing `most <= 0` still draws it with the arrows at their
// extremes, which is what the engine does when the range is zero.
void DrawScrollPlank(TextureCache& textures, Surface& dst, int scroll,
                     int most);

// Layout resource 0xaa.
constexpr int kListTop = 17;
constexpr int kItemX = 5;

// Row kind 6.
constexpr int kTitleTextX = 5;
constexpr int kTitleTextY = 0;
constexpr int kTitleExtra = 5;

// Row kind 8.
constexpr int kSubTopicDrop = 5;
constexpr int kSubTopicTextX = 0x17;
constexpr int kSubTopicTextY = 6;
constexpr int kSubTopicShort = 9;

// Which trinket the plank shows. Call once per page and keep the answer, or
// the plank flickers between frames.
int PickTitleFrame(GameContext& ctx);

// How much running y a title row claims. Zero for an empty title.
int TitleHeight(GameContext& ctx, const std::string& title);
void DrawTitle(GameContext& ctx, Surface& dst, const std::string& title,
               int frame);

// How much running y a caption claims, and how to draw one at `y`.
int SubTopicHeight(GameContext& ctx);
void DrawSubTopic(GameContext& ctx, Surface& dst, const std::string& text,
                  int y);

// Row kind 10.
constexpr int kStonePlankDrop = 2;
constexpr int kStonePlankTextX = 0xb;
constexpr int kStonePlankTextY = 2;
constexpr int kStonePlankShort = 9;
// The wrap engine breaks a line three pixels in from the right edge of the
// surface it is drawing into (0x10072724), which here is the plank's own.
constexpr int kStonePlankWrapInset = 3;

// The three-line stone plank a question goes on. Its height does not depend on
// the question: the plank is one picture and the row claims the same nine
// pixels less than it however many lines land on it.
int StonePlankHeight(GameContext& ctx);
void DrawStonePlank(GameContext& ctx, Surface& dst, const std::string& text,
                    int y);

// The soft-key bar: each label sits on its own little wooden plank --
// `leftSb.tc` / `rightSb.tc`, 56x17, resource slots 0xb0/0xb1 -- at the
// coordinates 0x1003a8e4 draws them: the left plank at (-1, 191), the right
// at (123, 191), its right end clipped off the screen. The widget bakes the
// label onto the plank (0x10038f90's tail): the left one at (2, 3), the
// right one right-aligned to x = 47. Above each, this port adds the physical
// key that triggers it -- a keyboard keycap (Space / Esc) or a pad face
// button (A / B) by whatever the player last touched; on an Ok/Ok board both
// corners show the confirm key. The icons come from icons.pak
// (tools/makeicons.py), mounted next to data.pak; if it is not there the
// planks stand alone. An empty label leaves its corner blank.
constexpr int kSoftLeftX = -1, kSoftRightX = 0x7b, kSoftY = 0xbf;
constexpr int kSoftTextX = 2, kSoftTextRight = 0x2f, kSoftTextY = 3;
void DrawSoftKeys(GameContext& ctx, Surface& dst, const std::string& left,
                  const std::string& right);

}  // namespace chrome
}  // namespace bb
