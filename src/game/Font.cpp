#include "game/Font.h"

#include "game/TextureCache.h"
#include "platform/Surface.h"

namespace bb {
namespace {

// Rightmost column holding a non-zero pixel, or -1 if the glyph is blank.
// The original tests the whole 16-bit word rather than just the alpha nibble
// (0x1007298c); transparent pixels in these textures are 0x0000, so it is the
// same test, and matching it keeps the measured widths identical.
int RightmostInk(const TcTexture::Image& g) {
    int maxX = -1;
    for (int y = 0; y < g.Height; ++y) {
        const uint16_t* row = g.Pixels.data() + size_t(y) * g.Width;
        for (int x = g.Width - 1; x > maxX; --x) {
            if (row[x] != 0) {
                maxX = x;
                break;
            }
        }
    }
    return maxX;
}

}  // namespace

bool Font::Load(TextureCache& textures, const std::string& path,
                int16_t lineHeight) {
    m_Tex = textures.Load(path);
    if (!m_Tex || !m_Tex->Valid()) {
        m_Tex = nullptr;
        return false;
    }
    m_LineHeight = lineHeight != 0 ? lineHeight
                                    : static_cast<int16_t>(m_Tex->Height);

    m_Widths.resize(m_Tex->Frames.size());
    for (size_t i = 0; i < m_Tex->Frames.size(); ++i) {
        const TcTexture::Image* g = m_Tex->Frame(static_cast<int>(i));
        // A glyph whose ink starts and ends in column 0 counts as blank too --
        // the original's `if (maxX == 0)` makes no distinction, so neither do we.
        const int maxX = g ? RightmostInk(*g) : -1;
        const int base = maxX <= 0 ? kBlankWidth : maxX;
        m_Widths[i] = static_cast<int16_t>(base + kSpacing);
    }
    return true;
}

int Font::Advance(unsigned char c) const {
    const int idx = (c - kFirstChar) & 0xFF;
    if (idx >= static_cast<int>(m_Widths.size())) return kBlankWidth;
    return m_Widths[idx];
}

int Font::Width(const std::string& text) const {
    if (!m_Tex) return 0;
    int w = 0;
    for (char c : text) w += Advance(static_cast<unsigned char>(c));
    return w;
}

void Font::Draw(Surface& dst, const std::string& text, int x, int y) const {
    if (!m_Tex) return;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const int idx = (c - kFirstChar) & 0xFF;
        if (idx < static_cast<int>(m_Widths.size())) {
            if (const TcTexture::Image* g = m_Tex->Frame(idx))
                dst.Blit(g->Pixels.data(), g->Width, g->Height, x, y);
        }
        x += Advance(c);
    }
}

void Font::DrawTinted(Surface& dst, const std::string& text, int x, int y,
                      uint16_t tint) const {
    if (!m_Tex) return;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const int idx = (c - kFirstChar) & 0xFF;
        if (idx < static_cast<int>(m_Widths.size())) {
            if (const TcTexture::Image* g = m_Tex->Frame(idx))
                dst.BlitTinted(g->Pixels.data(), g->Width, g->Height, x, y,
                               tint);
        }
        x += Advance(c);
    }
}

void Font::DrawNumber(Surface& dst, int value, int rightX, int y) const {
    if (!m_Tex) return;
    if (value < 0) value = 0;
    // Digit by digit from the least significant, exactly as 0x1007256c does:
    // step left by the glyph's advance, draw, divide by ten, stop at zero.
    // Eight digits is the original's own loop bound.
    for (int i = 0; i < 8; ++i) {
        const int digit = value % 10;
        const unsigned char c = static_cast<unsigned char>('0' + digit);
        rightX -= Advance(c);
        const int idx = (c - kFirstChar) & 0xFF;
        if (idx < static_cast<int>(m_Widths.size())) {
            if (const TcTexture::Image* g = m_Tex->Frame(idx))
                dst.Blit(g->Pixels.data(), g->Width, g->Height, rightX, y);
        }
        value /= 10;
        if (value == 0) return;
    }
}

std::vector<std::string> WrapText(const Font& font, const std::string& text,
                                  int maxWidth) {
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0) return lines;

    std::string line;
    size_t i = 0;
    while (i <= text.size()) {
        size_t space = text.find(' ', i);
        // A literal backslash-n breaks the line whatever the width says: the
        // engine's wrap loop tests for that pair explicitly (0x10072724) and
        // the mission scripts' dialogue uses it.
        //
        // Every one of the 705 breaks in the shipped string table is written
        // with *two* backslashes, and nothing unescapes them, so the engine
        // matches the pair starting at the second one and draws the first as a
        // glyph -- there is a real backslash at the end of every wrapped line
        // in the original. The port swallows the whole run instead. That is a
        // deliberate divergence, and the only one in this file.
        const size_t brk = text.find("\\n", i);
        bool hard = brk != std::string::npos && (space == std::string::npos || brk < space);
        size_t wordEnd = space;
        if (hard) {
            space = brk;
            wordEnd = brk;
            while (wordEnd > i && text[wordEnd - 1] == '\\') --wordEnd;
        }
        const std::string word = text.substr(i, wordEnd == std::string::npos
                                                    ? std::string::npos
                                                    : wordEnd - i);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && font.Width(candidate) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        if (space == std::string::npos) break;
        if (hard) {
            lines.push_back(line);
            line.clear();
            i = space + 2;
            continue;
        }
        i = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

}  // namespace bb
