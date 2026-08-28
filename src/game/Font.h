// Font — port of the engine's bitmap font (constructor at 0x10072220).
//
// A font is just a .tc texture with **one frame per glyph**, starting at '!'
// (0x21) and running 223 frames to 0xFF, so it covers Latin-1 including the
// trademark sign the menu needs. The shipped fonts are `Data\font-small.tc`
// (13x15 cells, 11px line height) and `Data\font-big.tc` (15x19, 15px).
//
// Advance widths are not stored anywhere: the original **measures them from
// the pixels at load time** (0x100722b0). For each glyph it draws the frame
// into a scratch buffer and takes the rightmost non-transparent column
// (0x1007298c), then adds a spacing adjustment of -1. The glyphs are
// anti-aliased, so their outer columns are nearly transparent edge pixels and
// the -1 makes neighbouring glyphs share them -- that is kerning, not a bug.
// Glyphs with no ink at all (space) fall back to a fixed width of 5.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bb {

class Surface;
class TextureCache;
struct Texture;

class Font {
public:
    // Defaults are the values the original passes for both shipped fonts.
    static constexpr uint8_t kFirstChar = 0x21;  // '!' -- frame 0
    static constexpr int16_t kSpacing = -1;      // advance = maxInkX + this
    static constexpr int16_t kBlankWidth = 5;    // glyphs with no ink, and
                                                 // characters with no frame

    // Load and measure. `lineHeight` of 0 means "use the texture height", as
    // the original does when the argument is zero.
    bool Load(TextureCache& textures, const std::string& path,
              int16_t lineHeight = 0);

    bool Valid() const { return m_Tex != nullptr; }
    int Height() const { return m_LineHeight; }

    // Total advance of `text`, matching the engine's stringWidth (0x10072940):
    // characters outside the glyph range advance by kBlankWidth.
    int Width(const std::string& text) const;

    // Draw at (x, y) -- the top-left of the glyph *cell*, not the baseline.
    // The ink sits a couple of rows down inside the cell, as in the original.
    void Draw(Surface& dst, const std::string& text, int x, int y) const;

    // The same, but recolouring the solid part of every glyph. The original
    // does this by putting the font texture into blend mode 9 and setting its
    // colour field, which the battlefield's money readout uses (0x10052264
    // stores 0xFFF0, gold, then puts the font back into mode 4).
    void DrawTinted(Surface& dst, const std::string& text, int x, int y,
                    uint16_t tint) const;

    // Draw a non-negative number *right-aligned*: `rightX` is where the last
    // digit ends, and each glyph is placed by stepping left through its own
    // advance. The engine has this as its own font method (0x1007256c) and
    // every panel that shows a value calls it, so a three-digit hit point
    // total and a one-digit ammo count share a right edge.
    void DrawNumber(Surface& dst, int value, int rightX, int y) const;

    // Advance of a single character; useful for carets and layout.
    int Advance(unsigned char c) const;

private:
    const Texture* m_Tex = nullptr;
    int16_t m_LineHeight = 0;
    std::vector<int16_t> m_Widths;  // one per glyph frame
};

// Split `text` into lines that each fit `maxWidth`, breaking on spaces. The
// original's screens use pre-wrapped strings from the localisation file; the
// port wraps at runtime so a longer translation cannot run off the screen.
std::vector<std::string> WrapText(const Font& font, const std::string& text,
                                  int maxWidth);

}  // namespace bb
