#include "game/Subtitles.h"

#include "game/ConfigFile.h"
#include "game/FilePack.hpp"
#include "game/Font.h"
#include "game/ResourceTable.h"
#include "game/Strings.h"
#include "game/TextureCache.h"
#include "shim/Log.h"

namespace bb {
namespace {

// The panel slides at a fixed 4 px per frame (0x100e7688).
constexpr int kSlideStep = 4;

// Text insets inside the two scratch panels, from the draw calls the engine
// makes into them: the message at (8, -1), the speaker's name at (4, -1). The
// -1 is real -- the glyph cells have a blank top row, so the text sits flush
// with the top of the parchment.
constexpr int kTextX = 8;
constexpr int kTextY = -1;
constexpr int kNameX = 4;
constexpr int kNameY = -1;

// The word-wrap limit is the panel width less 3 (0x10072724 passes
// `clipRight - 3` to the line breaker).
constexpr int kWrapMargin = 3;

// The name plate is sized to its text plus a fixed pad (0x100e7688).
constexpr int kPlatePad = 16;

// String 5113 is "[player]", the placeholder that stands in for whatever the
// player named their captain. It sits in the middle of the alphabetical
// talker roster (Little Marcel, Pierre Du Blezac, [player], Rodriguez...) and
// is by far the most-used speaker: 112 of the 300-odd lines in the game.
//
// The engine compares the *resolved* string against this literal and swaps in
// resource 0xc1, the name from the player's profile (0x100e7bd4). With no
// profile it substitutes an empty string and the plate comes out blank, so
// that is what SetPlayerName's default does too -- except that an empty name
// suppresses the plate entirely here rather than drawing an empty one.
constexpr const char* kPlayerToken = "[player]";

void DrawTexture(Surface& dst, const Texture* tex, int x, int y, int frame = 0) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(frame);
    if (!img) return;
    dst.Blit(img->Pixels.data(), img->Width, img->Height, x, y);
}

// Blit only the leftmost `width` columns -- how the engine narrows the name
// plate and its highlight to fit the speaker's name (0x10081874).
void DrawClipped(Surface& dst, const uint16_t* src, int srcW, int srcH, int x,
                 int y, int width) {
    if (!src || width <= 0) return;
    const int w = width < srcW ? width : srcW;
    for (int row = 0; row < srcH; ++row)
        dst.Blit(src + std::size_t(row) * srcW, w, 1, x, y + row);
}

void DrawTextureClipped(Surface& dst, const Texture* tex, int x, int y,
                        int width) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(0);
    if (!img) return;
    DrawClipped(dst, img->Pixels.data(), img->Width, img->Height, x, y, width);
}

}  // namespace

bool Subtitles::Init(TextureCache& textures, const Font& font) {
    m_Font = &font;
    m_Bg = textures.Slot(0xbf);
    m_Edge = textures.Slot(0xc0);
    m_Plate = textures.Slot(0xdb);
    m_PlateEnd = textures.Slot(0xdc);
    m_PlateStart = textures.Slot(0xdd);
    m_PlateTop = textures.Slot(0xde);

    if (!m_Bg || !m_Plate) {
        LogError("subtitles: panel textures missing\n");
        return false;
    }
    m_BgScratch = Surface(m_Bg->Width, m_Bg->Height);
    m_PlateScratch = Surface(m_Plate->Width, m_Plate->Height);
    Reset();
    return true;
}

bool Subtitles::Load(FilePack& pack, const Strings& strings,
                     const std::string& name) {
    m_Lines.clear();

    // Who says what. Each section carries two string ids: `talker` names the
    // speaker, `row` is the line itself.
    ConfigFile text;
    if (!text.Load(pack, "Data\\anim\\sub\\" + name + ".dat")) {
        Reset();
        return false;
    }
    for (const auto& section : text.Sections()) {
        Line line;
        line.Text = strings.Get(section.GetInt("row", -1));
        if (section.Has("talker")) {
            line.Talker = strings.Get(section.GetInt("talker", -1));
            if (line.Talker == kPlayerToken) line.Talker = m_PlayerName;
        }
        m_Lines.push_back(std::move(line));
    }

    // When, and for how long. Matched to the above by position, which is what
    // the engine does: it walks both lists with one index.
    ConfigFile timing;
    if (timing.Load(pack, "Data\\anim\\sub\\" + name + ".sub")) {
        const std::size_t n = timing.Count() < m_Lines.size() ? timing.Count()
                                                             : m_Lines.size();
        for (std::size_t i = 0; i < n; ++i) {
            m_Lines[i].Time = timing.Sections()[i].GetInt("time");
            m_Lines[i].Delay = timing.Sections()[i].GetInt("delay");
        }
    }

    Reset();
    return true;
}

void Subtitles::SetLines(std::vector<Line> lines) {
    m_Lines = std::move(lines);
    Reset();
}

// 0x100e7590. The panel parks one plate-height below the bottom of the screen
// so even the name plate is out of sight.
void Subtitles::Reset() {
    m_Next = 0;
    m_Current = -1;
    m_Delay = 0;
    m_PlateWidth = 0;
    const int lineHeight = m_Font ? m_Font->Height() : 0;
    m_TargetY = Surface::kHeight - 2 * lineHeight;
    m_Y = Surface::kHeight + (m_Plate ? m_Plate->Height : 0);
}

void Subtitles::Present(const Line& line) {
    if (!m_Font || !m_Bg) return;

    // The message, wrapped onto the parchment.
    m_BgScratch.Fill(0);
    DrawTexture(m_BgScratch, m_Bg, 0, 0);
    const int wrap = m_BgScratch.Width() - kWrapMargin - kTextX;
    const std::vector<std::string> wrapped = WrapText(*m_Font, line.Text, wrap);
    int y = kTextY;
    for (const std::string& row : wrapped) {
        m_Font->Draw(m_BgScratch, row, kTextX, y);
        y += m_Font->Height();
    }

    // The speaker's name, on its plate.
    m_PlateWidth = 0;
    if (!line.Talker.empty() && m_Plate) {
        m_PlateScratch.Fill(0);
        DrawTexture(m_PlateScratch, m_Plate, 0, 0);
        m_Font->Draw(m_PlateScratch, line.Talker, kNameX, kNameY);
        m_PlateWidth = kNameX + m_Font->Width(line.Talker) + kPlatePad;
    }

    // Rise far enough to show every line, but always at least two lines'
    // worth, so a one-line message does not leave a sliver of parchment.
    const int textHeight = static_cast<int>(wrapped.size()) * m_Font->Height();
    const int twoLines = Surface::kHeight - 2 * m_Font->Height();
    m_TargetY = Surface::kHeight - textHeight;
    if (twoLines < m_TargetY) m_TargetY = twoLines;
}

void Subtitles::Update(int frame) {
    if (m_Next < static_cast<int>(m_Lines.size()) &&
        m_Lines[m_Next].Time <= frame) {
        m_Current = m_Next++;
        m_Delay = m_Lines[m_Current].Delay;
        Present(m_Lines[m_Current]);
    }

    // The engine reads the counter, then decrements it, so a line with delay 0
    // is already expired on the frame it appears.
    const int remaining = m_Delay--;
    const int parked = Surface::kHeight + (m_Plate ? m_Plate->Height : 0);
    if (remaining < 1) {
        if (m_Y > parked) return;  // already all the way off; stop moving
        m_Y += kSlideStep;         // expired: slide back off the bottom
    } else if (m_Y > m_TargetY) {
        m_Y -= kSlideStep;  // still rising
    } else if (m_TargetY - kSlideStep > m_Y) {
        m_Y += kSlideStep;  // overshot a taller previous line; settle back down
    }
}

void Subtitles::Draw(Surface& dst) const {
    if (!m_Bg || m_Current < 0) return;
    if (m_Y > Surface::kHeight + (m_Plate ? m_Plate->Height : 0)) return;

    if (!m_Lines[m_Current].Talker.empty() && m_Plate) {
        // The plate sits above the parchment: left cap, then the name plate
        // and its highlight, then the right cap at the name's end.
        const int top = m_Y - (m_Plate->Height + 8);
        const int cap = m_PlateStart ? m_PlateStart->Width : 0;
        DrawTexture(dst, m_PlateStart, 0, top);
        DrawClipped(dst, m_PlateScratch.Pixels(), m_PlateScratch.Width(),
                    m_PlateScratch.Height(), cap - 1, top + 4, m_PlateWidth);
        DrawTextureClipped(dst, m_PlateTop, cap - 1, top + 1, m_PlateWidth);
        DrawTexture(dst, m_PlateEnd, m_PlateWidth, top);
    }

    if (m_Edge) {
        const TcTexture::Image* img = m_Edge->Frame(0);
        if (img) dst.Blit(img->Pixels.data(), img->Width, img->Height, 0,
                          m_Y - (img->Height + 2));
    }
    dst.Blit(m_BgScratch.Pixels(), m_BgScratch.Width(), m_BgScratch.Height(), 0,
             m_Y - 2);
}

}  // namespace bb
