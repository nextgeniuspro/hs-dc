// The font the port has before it has the game's.
//
// Every glyph the game draws comes out of data.pak (game/Font.h), which is no
// use to the one screen that exists *because* the pak is missing. So the port
// carries a small font of its own, compiled in: five by seven, printable
// ASCII, no dependencies, available from the first instruction.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bb {

class Surface;

// The glyph cell, gap included: five inked columns and a blank sixth, seven
// inked rows and a blank eighth. So a 176-pixel screen holds 29 characters at
// scale 1, and text laid out in whole cells never needs to measure anything.
constexpr int kBootGlyphW = 6;
constexpr int kBootGlyphH = 8;

// Draw `text` with its top-left corner at (x, y), every pixel `scale` times as
// wide and tall. Anything outside printable ASCII draws as a hollow box, which
// is what a path with an accent in it should look like rather than a gap.
// Returns the x the run ended at, so pieces can be drawn one after another.
int DrawBootText(Surface& dst, int x, int y, const std::string& text,
                 uint16_t colour, int scale = 1);

// The same, centred across the surface's full width.
void DrawBootTextCentred(Surface& dst, int y, const std::string& text,
                         uint16_t colour, int scale = 1);

// What DrawBootText would advance by, trailing gap and all.
int BootTextWidth(const std::string& text, int scale = 1);

// Break `text` into lines of at most `columns` characters, at spaces where it
// can and mid-word where it cannot -- a filesystem path is one long word and
// still has to fit. Empty lines in the input are kept, since they are how the
// caller spaces its paragraphs.
std::vector<std::string> WrapBootText(const std::string& text, int columns);

}  // namespace bb
