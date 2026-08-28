// MenuList — the engine's list-of-buttons widget (constructed at 0x10038f90).
//
// Each entry is not drawn as text on a background: the widget bakes a little
// surface per item (0x1008cbd4), stamping a wooden board texture into it and
// then drawing the label on top. The board comes from a style triple stored in
// engine resource 0xad, `{textX, textY, styleIndex}` = `{20, 2, 0}`, and style
// 0 selects `Data\Menu\shell_board.tc` — 176x20 with **three frames**. The
// frame is picked by `itemIndex % 3`, so the wood grain varies down the list
// instead of tiling identically.
//
// Items sit at x = 5 (resource 0xaa's fourth word) and stack with a pitch of
// board height - 1. Disabled items get their surface desaturated in place:
// alpha kept, RGB replaced by the channel average.
//
// The selection marker is `Data\Menu\sword.tc`, a 32x32 30-frame animation
// drawn at the selected item's origin minus (5, 5), advancing one frame every
// 30 ms (0x1003a788).
//
// Lists taller than the screen scroll, keeping the selection inside the same
// margins the original uses, and get the two-arrow plank at the bottom middle
// (chrome::DrawScrollPlank). Not ported from the original widget: the
// selection's slide animation and the soft-key bar.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/Surface.h"

namespace bb {

class Font;
class Host;
class TextureCache;
struct Texture;

class MenuList {
public:
    // Layout constants recovered from the binary.
    static constexpr int kItemX = 5;        // resource 0xaa word 3
    static constexpr int kTextX = 20;       // resource 0xad word 0
    static constexpr int kTextY = 2;        // resource 0xad word 1
    static constexpr int kBoardFrames = 3;  // shell_board.tc frame count
    static constexpr int kCursorOffset = 5; // sword drawn at item - (5, 5)
    static constexpr uint32_t kCursorFrameMs = 30;
    static constexpr int kScrollMarginTop = 24;     // resource 0xaa word 2
    static constexpr int kScrollMarginBottom = 17;  // resource 0xaa word 1

    // `firstIndex` is the index the original passes for the first entry (the
    // main menu starts at 4), because it feeds the `% 3` board choice.
    bool Load(TextureCache& textures, const Font& font, int firstIndex);

    // Add an entry. `id` is returned by Chosen() when it is picked.
    // `RightLabel` is drawn right-aligned on the same board -- the key
    // configuration screen pairs an action name with its current key that way,
    // where the original uses a separate text row above each button.
    void Add(const std::string& label, int id, bool enabled = true,
             const std::string& rightLabel = {});

    // Add a caption: the grey stone slab (`subtopic.tc`) with the small font
    // on it, the engine's kind-8 text row. It shares the list's running y --
    // which is the whole point, because a caption is what divides one section
    // of a page from the next -- but the cursor cannot land on it, since in
    // the original text rows are a second list the selection never indexes.
    void AddCaption(const std::string& text);

    // Add a volume row. The original composes these from `volume.tc` (a wooden
    // ramp, 176x35) with `volume_tab.tc` (12x16) sliding along it, and the
    // slider widget caps the level at 5. Left/Right adjust it in place rather
    // than confirming, so `Adjust` reports whether the press was consumed.
    void AddSlider(const std::string& label, int id, int value, int max);
    bool IsSlider(int index) const;
    int SliderValue(int index) const;
    // Returns true and updates the value if the selected row is a slider.
    bool Adjust(int delta);

    int Count() const { return static_cast<int>(m_Items.size()); }
    const std::string& Label(int index) const;
    int IdAt(int index) const;
    int Selected() const { return m_Selected; }
    void SetSelected(int i);
    int SelectedId() const;

    // Total height of the list as laid out.
    int Height() const;

    // Move the highlight, wrapping. Skips captions, which are not items at
    // all; disabled entries it does land on, because the original lets the
    // cursor sit on them and simply refuses to confirm them.
    void MoveUp();
    void MoveDown();

    // True if the currently selected entry can be confirmed.
    bool SelectedEnabled() const;

    // Whether the sword is drawn at all. A page where the list is the only
    // thing to look at always wants it; the card picker does not, because the
    // selection there can be on the strip of cards above the list instead, and
    // two cursors on one screen would be two selections. Off, the rows are
    // still drawn -- what goes is the marker saying which one you are on.
    void ShowCursor(bool on) { m_CursorShown = on; }

    // Draw the list with its top at `y`, scrolling if it is taller than the
    // space below `y`. `nowMs` drives the cursor animation.
    void Draw(Surface& dst, int y, uint32_t nowMs);

    // True if the last Draw had to scroll (there is content off-screen).
    bool Scrolled() const { return m_Scroll != 0 || m_Overflow; }

private:
    struct Item {
        std::string Label;
        std::string RightLabel;
        int ID = 0;
        bool Enabled = true;
        bool Slider = false;
        bool Caption = false;
        int Value = 0;
        int Max = 0;
        Surface Canvas{1, 1};  // baked board (or ramp, or slab) + label
    };

    void Bake(Item& item, int index);
    void BakeCaption(Item& item);
    // How much running y a row claims. A button claims its height less one; a
    // caption claims nine less than it occupies, so the slab's iron rings hang
    // over whatever is drawn below it (0x1008ca04).
    int Advance(const Item& item) const;
    bool Landable(int index) const;
    // Put the cursor on the first row it may sit on, if it is not on one.
    void SettleSelection();

    TextureCache* m_Textures = nullptr;
    const Font* m_Font = nullptr;
    const Texture* m_Board = nullptr;
    const Texture* m_Cursor = nullptr;
    const Texture* m_Ramp = nullptr;
    const Texture* m_Tab = nullptr;
    std::vector<Item> m_Items;
    int m_FirstIndex = 0;
    int m_Selected = 0;
    bool m_CursorShown = true;
    int m_CursorFrame = 0;
    uint32_t m_CursorLastMs = 0;
    int m_Scroll = 0;
    bool m_Overflow = false;
};

}  // namespace bb
