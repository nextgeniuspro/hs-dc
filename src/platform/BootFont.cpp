#include "platform/BootFont.h"

#include "platform/Surface.h"

namespace bb {
namespace {

constexpr int kInkW = 5;  // inked columns per glyph; the sixth is the gap
constexpr int kInkH = 7;  // inked rows; the eighth is the line gap

constexpr char kFirstChar = ' ';  // 0x20
constexpr char kLastChar = '~';   // 0x7E

// Printable ASCII, seven rows of five columns each, laid out as seven adjacent
// string literals that the compiler joins into one thirty-five character
// string. '#' is ink and anything else is clear, so the table reads as what it
// draws -- read a line in five-character groups and the glyph is there.
//
// Seven rows is one short of what this really wants. Capitals and digits take
// all of them, lowercase sits in the bottom five, and the five letters with a
// descender have to find room for it inside the same seven -- so g, j, p, q
// and y carry a bowl one row shallower than o's and spend the row they saved
// below the baseline. That is worth the asymmetry: a `g` drawn level with the
// digits is a `9`, which is not an academic complaint about a font whose whole
// job is spelling out filesystem paths.
const char* const kGlyphs[] = {
    /*   */ "....." "....." "....." "....." "....." "....." ".....",
    /* ! */ "..#.." "..#.." "..#.." "..#.." "..#.." "....." "..#..",
    /* " */ ".#.#." ".#.#." "....." "....." "....." "....." ".....",
    /* # */ ".#.#." ".#.#." "#####" ".#.#." "#####" ".#.#." ".#.#.",
    /* $ */ "..#.." ".####" "#.#.." ".###." "..#.#" "####." "..#..",
    /* % */ "##..." "##..#" "...#." "..#.." ".#..." "#..##" "...##",
    /* & */ ".##.." "#..#." "#.#.." ".#..." "#.#.#" "#..#." ".##.#",
    /* ' */ "..#.." "..#.." "....." "....." "....." "....." ".....",
    /* ( */ "...#." "..#.." ".#..." ".#..." ".#..." "..#.." "...#.",
    /* ) */ ".#..." "..#.." "...#." "...#." "...#." "..#.." ".#...",
    /* * */ "....." "#.#.#" ".###." "#####" ".###." "#.#.#" ".....",
    /* + */ "....." "..#.." "..#.." "#####" "..#.." "..#.." ".....",
    /* , */ "....." "....." "....." "....." ".##.." "..#.." ".#...",
    /* - */ "....." "....." "....." "#####" "....." "....." ".....",
    /* . */ "....." "....." "....." "....." "....." ".##.." ".##..",
    /* / */ "....." "....#" "...#." "..#.." ".#..." "#...." ".....",
    /* 0 */ ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###.",
    /* 1 */ "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###.",
    /* 2 */ ".###." "#...#" "....#" "...#." "..#.." ".#..." "#####",
    /* 3 */ "#####" "...#." "..##." "....#" "....#" "#...#" ".###.",
    /* 4 */ "...#." "..##." ".#.#." "#..#." "#####" "...#." "...#.",
    /* 5 */ "#####" "#...." "####." "....#" "....#" "#...#" ".###.",
    /* 6 */ "..##." ".#..." "#...." "####." "#...#" "#...#" ".###.",
    /* 7 */ "#####" "....#" "...#." "..#.." ".#..." ".#..." ".#...",
    /* 8 */ ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###.",
    /* 9 */ ".###." "#...#" "#...#" ".####" "....#" "...#." ".##..",
    /* : */ "....." ".##.." ".##.." "....." ".##.." ".##.." ".....",
    /* ; */ "....." ".##.." ".##.." "....." ".##.." "..#.." ".#...",
    /* < */ "...#." "..#.." ".#..." "#...." ".#..." "..#.." "...#.",
    /* = */ "....." "....." "#####" "....." "#####" "....." ".....",
    /* > */ ".#..." "..#.." "...#." "....#" "...#." "..#.." ".#...",
    /* ? */ ".###." "#...#" "....#" "...#." "..#.." "....." "..#..",
    /* @ */ ".###." "#...#" "#.###" "#.#.#" "#.###" "#...." ".###.",
    /* A */ "..#.." ".#.#." "#...#" "#...#" "#####" "#...#" "#...#",
    /* B */ "####." "#...#" "#...#" "####." "#...#" "#...#" "####.",
    /* C */ ".###." "#...#" "#...." "#...." "#...." "#...#" ".###.",
    /* D */ "###.." "#..#." "#...#" "#...#" "#...#" "#..#." "###..",
    /* E */ "#####" "#...." "#...." "####." "#...." "#...." "#####",
    /* F */ "#####" "#...." "#...." "####." "#...." "#...." "#....",
    /* G */ ".###." "#...#" "#...." "#.###" "#...#" "#...#" ".####",
    /* H */ "#...#" "#...#" "#...#" "#####" "#...#" "#...#" "#...#",
    /* I */ ".###." "..#.." "..#.." "..#.." "..#.." "..#.." ".###.",
    /* J */ "..###" "...#." "...#." "...#." "...#." "#..#." ".##..",
    /* K */ "#...#" "#..#." "#.#.." "##..." "#.#.." "#..#." "#...#",
    /* L */ "#...." "#...." "#...." "#...." "#...." "#...." "#####",
    /* M */ "#...#" "##.##" "#.#.#" "#.#.#" "#...#" "#...#" "#...#",
    /* N */ "#...#" "#...#" "##..#" "#.#.#" "#..##" "#...#" "#...#",
    /* O */ ".###." "#...#" "#...#" "#...#" "#...#" "#...#" ".###.",
    /* P */ "####." "#...#" "#...#" "####." "#...." "#...." "#....",
    /* Q */ ".###." "#...#" "#...#" "#...#" "#.#.#" "#..#." ".##.#",
    /* R */ "####." "#...#" "#...#" "####." "#.#.." "#..#." "#...#",
    /* S */ ".####" "#...." "#...." ".###." "....#" "....#" "####.",
    /* T */ "#####" "..#.." "..#.." "..#.." "..#.." "..#.." "..#..",
    /* U */ "#...#" "#...#" "#...#" "#...#" "#...#" "#...#" ".###.",
    /* V */ "#...#" "#...#" "#...#" "#...#" "#...#" ".#.#." "..#..",
    /* W */ "#...#" "#...#" "#...#" "#.#.#" "#.#.#" "##.##" "#...#",
    /* X */ "#...#" "#...#" ".#.#." "..#.." ".#.#." "#...#" "#...#",
    /* Y */ "#...#" "#...#" ".#.#." "..#.." "..#.." "..#.." "..#..",
    /* Z */ "#####" "....#" "...#." "..#.." ".#..." "#...." "#####",
    /* [ */ ".###." ".#..." ".#..." ".#..." ".#..." ".#..." ".###.",
    /* \ */ "....." "#...." ".#..." "..#.." "...#." "....#" ".....",
    /* ] */ ".###." "...#." "...#." "...#." "...#." "...#." ".###.",
    /* ^ */ "..#.." ".#.#." "#...#" "....." "....." "....." ".....",
    /* _ */ "....." "....." "....." "....." "....." "....." "#####",
    /* ` */ ".#..." "..#.." "....." "....." "....." "....." ".....",
    /* a */ "....." "....." ".###." "....#" ".####" "#...#" ".####",
    /* b */ "#...." "#...." "####." "#...#" "#...#" "#...#" "####.",
    /* c */ "....." "....." ".###." "#...#" "#...." "#...#" ".###.",
    /* d */ "....#" "....#" ".####" "#...#" "#...#" "#...#" ".####",
    /* e */ "....." "....." ".###." "#...#" "#####" "#...." ".###.",
    /* f */ "..##." ".#..#" ".#..." "####." ".#..." ".#..." ".#...",
    /* g */ "....." "....." ".####" "#...#" ".####" "....#" ".###.",
    /* h */ "#...." "#...." "####." "#...#" "#...#" "#...#" "#...#",
    /* i */ "..#.." "....." ".##.." "..#.." "..#.." "..#.." ".###.",
    /* j */ "...#." "....." "..##." "...#." "...#." "#..#." ".##..",
    /* k */ "#...." "#...." "#..#." "#.#.." "##..." "#.#.." "#..#.",
    /* l */ ".##.." "..#.." "..#.." "..#.." "..#.." "..#.." ".###.",
    /* m */ "....." "....." "##.#." "#.#.#" "#.#.#" "#...#" "#...#",
    /* n */ "....." "....." "####." "#...#" "#...#" "#...#" "#...#",
    /* o */ "....." "....." ".###." "#...#" "#...#" "#...#" ".###.",
    /* p */ "....." "....." "####." "#...#" "####." "#...." "#....",
    /* q */ "....." "....." ".####" "#...#" ".####" "....#" "....#",
    /* r */ "....." "....." "#.##." "##..#" "#...." "#...." "#....",
    /* s */ "....." "....." ".####" "#...." ".###." "....#" "####.",
    /* t */ ".#..." ".#..." "####." ".#..." ".#..." ".#..#" "..##.",
    /* u */ "....." "....." "#...#" "#...#" "#...#" "#..##" ".##.#",
    /* v */ "....." "....." "#...#" "#...#" "#...#" ".#.#." "..#..",
    /* w */ "....." "....." "#...#" "#...#" "#.#.#" "#.#.#" ".#.#.",
    /* x */ "....." "....." "#...#" ".#.#." "..#.." ".#.#." "#...#",
    /* y */ "....." "....." "#...#" "#...#" ".#.#." "..#.." ".#...",
    /* z */ "....." "....." "#####" "...#." "..#.." ".#..." "#####",
    /* { */ "...##" "..#.." "..#.." ".##.." "..#.." "..#.." "...##",
    /* | */ "..#.." "..#.." "..#.." "..#.." "..#.." "..#.." "..#..",
    /* } */ "##..." "..#.." "..#.." "..##." "..#.." "..#.." "##...",
    /* ~ */ "....." "....." ".##.#" "#..#." "....." "....." ".....",
};
static_assert(sizeof(kGlyphs) / sizeof(kGlyphs[0]) ==
                  static_cast<std::size_t>(kLastChar - kFirstChar + 1),
              "one glyph per printable ASCII character");

// What a character the table has no glyph for draws as. A hollow box rather
// than a blank: a path with an accent in it should look like a path the font
// cannot spell, not like a path with a hole in it.
const char* const kMissingGlyph = "#####"
                                  "#...#"
                                  "#...#"
                                  "#...#"
                                  "#...#"
                                  "#...#"
                                  "#####";

const char* GlyphFor(char c) {
    if (c < kFirstChar || c > kLastChar) return kMissingGlyph;
    return kGlyphs[static_cast<unsigned char>(c) - kFirstChar];
}

}  // namespace

int DrawBootText(Surface& dst, int x, int y, const std::string& text,
                 uint16_t colour, int scale) {
    if (scale < 1) scale = 1;
    for (const char c : text) {
        const char* glyph = GlyphFor(c);
        for (int row = 0; row < kInkH; ++row) {
            for (int col = 0; col < kInkW; ++col) {
                if (glyph[row * kInkW + col] != '#') continue;
                dst.FillRect(x + col * scale, y + row * scale, scale, scale,
                             colour);
            }
        }
        x += kBootGlyphW * scale;
    }
    return x;
}

void DrawBootTextCentred(Surface& dst, int y, const std::string& text,
                         uint16_t colour, int scale) {
    const int w = BootTextWidth(text, scale);
    // The trailing gap is part of the width but not part of what is seen, so
    // half of it is given back -- otherwise every centred line sits a pixel or
    // three to the left of where it looks like it should.
    const int ink = w - (text.empty() ? 0 : scale);
    DrawBootText(dst, (dst.Width() - ink) / 2, y, text, colour, scale);
}

int BootTextWidth(const std::string& text, int scale) {
    if (scale < 1) scale = 1;
    return static_cast<int>(text.size()) * kBootGlyphW * scale;
}

std::vector<std::string> WrapBootText(const std::string& text, int columns) {
    std::vector<std::string> lines;
    if (columns < 1) columns = 1;
    std::string line;
    // Where in `line` the run of characters since the last space begins. A
    // word that overflows is moved down whole; a word longer than the line is
    // cut, because there is nowhere else for it to go.
    std::size_t wordStart = 0;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
            wordStart = 0;
            continue;
        }
        line.push_back(c);
        if (c == ' ') wordStart = line.size();
        if (static_cast<int>(line.size()) <= columns) continue;
        if (wordStart == 0) {  // one long word: break it where it ran out
            lines.push_back(line.substr(0, line.size() - 1));
            line = line.substr(line.size() - 1);
        } else {
            std::string carried = line.substr(wordStart);
            line.resize(wordStart);
            while (!line.empty() && line.back() == ' ') line.pop_back();
            lines.push_back(line);
            line = std::move(carried);
        }
        wordStart = 0;
    }
    lines.push_back(line);
    return lines;
}

}  // namespace bb
