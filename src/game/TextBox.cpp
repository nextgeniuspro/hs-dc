#include "game/TextBox.h"

#include <cstdlib>

#include "game/Font.h"
#include "game/Game.h"
#include "game/MenuChrome.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/TextureCache.h"
#include "game/Water.h"
#include "platform/Host.h"

namespace bb {
namespace {

// Resource slots the row backgrounds come from.
constexpr uint16_t kSlotFullBoard = 0x32;  // Data\Menu\fullboard.tc
constexpr uint16_t kSlotHeaderBg = 0xd1;   // Data\Menu\header_bg.tc
constexpr uint16_t kSlotSubTopic = 0xdf;   // Data\Menu\subtopic.tc
constexpr uint16_t kSlotStatusBg = 0xe2;   // Data\Menu\status_bg.tc
constexpr uint16_t kSlot3Rows = 0xe0;      // Data\Menu\3rows.tc

// The wrap engine clips three pixels in from the surface's right edge
// (0x10072724 subtracts 3 from the clip rectangle).
constexpr int kWrapInset = 3;

// Soft-key label pairs per mode, straight out of 0x10065ef0's switch. -1 means
// the mode leaves that key blank.
struct KeyPair { int Mode, Left, Right; };
const KeyPair kSoftKeys[] = {
    {0,  2116, 2116},    // Ok / Ok
    {1,  2123, 2124},    // Accept / Retreat
    {2,    -1,   -1},    // no keys
    {3,  2116, 62148},   // Ok / Cancel
    {4,  62146, 62147},  // Next / Back
    {5,  62146, 62147},
    {6,  62145, 62147},  // Exit / Back
    {7,  62143, 62144},  // Next page / Exit help
    {8,  62145, 62145},
    {9,    -1, 62148},
    {10, 2116, 62147},   // Ok / Back
    {11, 62146, 62147},
    {12, 62146, 62145},  // Next / Exit
    {13, 62145, 62147},
    {14, 1672, 62148},   // Select / Cancel
    {15, 71133, 62148},  // Buy / Cancel
};

}  // namespace

TextBox::TextBox(GameContext& ctx, int mode) : m_Ctx(ctx), m_Mode(mode) {
    m_Board = ctx.Textures.Register(kSlotFullBoard, "Data\\Menu\\fullboard.tc");
    m_HeaderBg = ctx.Textures.Register(kSlotHeaderBg, "Data\\Menu\\header_bg.tc");
    m_Subtopic = ctx.Textures.Register(kSlotSubTopic, "Data\\Menu\\subtopic.tc");
    m_StatusBg = ctx.Textures.Register(kSlotStatusBg, "Data\\Menu\\status_bg.tc");
    m_ThreeRows = ctx.Textures.Register(kSlot3Rows, "Data\\Menu\\3rows.tc");
    for (const KeyPair& k : kSoftKeys) {
        if (k.Mode != mode) continue;
        if (k.Left > 0) m_LeftKey = ctx.StringsRef.Get(k.Left);
        if (k.Right > 0) m_RightKey = ctx.StringsRef.Get(k.Right);
        break;
    }
    // The title plank picks one of its four frames from the clock, so two
    // dialogs in a row don't show the same grain (0x1008c378 case 6).
    if (m_HeaderBg && !m_HeaderBg->Frames.empty())
        m_HeaderFrame = int(ctx.HostRef.TickCount() % m_HeaderBg->Frames.size());
}

std::string TextBox::Substitute(const GameContext& ctx,
                                const std::string& text) {
    // 0x10066268 copies the line a character at a time and, on `[`, gathers up
    // to `]` and appends the commander's name when the tag reads `player`.
    // Note it appends the name *and then keeps copying*, so the brackets stay
    // in the output -- the shipped strings are the tag on its own, and the
    // port drops the tag instead of reproducing that.
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '[') {
            out += text[i];
            continue;
        }
        const std::size_t end = text.find(']', i);
        if (end == std::string::npos) {
            out += text[i];
            continue;
        }
        const std::string tag = text.substr(i + 1, end - i - 1);
        if (tag == "player") out += ctx.CampaignData.Commander;
        i = end;
    }
    return out;
}

void TextBox::Add(int kind, const std::string& text) {
    if (kind != kPlain) {
        AddRaw(kind, text, kind == kSubTopic || kind == kCentred ? 0 : kItemX);
        return;
    }
    // A body line: substitute the commander's name, then split out any
    // `<x path.tc>` image tags into rows of their own (0x1007c334).
    const std::string line = Substitute(m_Ctx, text);
    std::size_t at = 0;
    while (at < line.size()) {
        const std::size_t open = line.find('<', at);
        if (open == std::string::npos) break;
        const std::size_t close = line.find('>', open);
        if (close == std::string::npos) break;
        if (open > at) AddRaw(kPlain, line.substr(at, open - at), kItemX);
        // `<x path>`: x is a column, or `-` to centre the image.
        const std::string tag = line.substr(open + 1, close - open - 1);
        const std::size_t space = tag.find(' ');
        if (space != std::string::npos) {
            const std::string where = tag.substr(0, space);
            // The string table writes a single backslash as `\\`, the same as
            // it does inside a line break, so a path picked out of a shipped
            // string arrives doubled: `data\\maps\\mp1.tc`.
            std::string path;
            for (std::size_t k = space + 1; k < tag.size(); ++k) {
                path.push_back(tag[k]);
                if (tag[k] == '\\' && k + 1 < tag.size() && tag[k + 1] == '\\')
                    ++k;
            }
            int x = where == "-" ? -1 : std::atoi(where.c_str());
            if (x < 0) {
                const Texture* t = m_Ctx.Textures.Load(path);
                x = t ? (Surface::kWidth / 2 - t->Width / 2) : 0;
            }
            AddRaw(kImage, path, x);
        }
        at = close + 1;
    }
    if (at < line.size()) AddRaw(kPlain, line.substr(at), kItemX);
    if (line.empty()) AddRaw(kPlain, line, kItemX);
}

void TextBox::AddIconLine(const std::string& path, int frame,
                          const std::string& text, int textX) {
    const Texture* tex = m_Ctx.Textures.Load(path);
    const TcTexture::Image* img = tex ? tex->Frame(frame) : nullptr;
    if (!img) {
        // No picture is no reason to lose the number.
        AddRaw(kPlain, text, textX);
        return;
    }
    const Font& small = m_Ctx.SmallFont;
    Row row;
    row.Kind = kImage;   // a baked surface, drawn as-is
    row.Text = text;
    row.X = 0;
    const int height = img->Height > small.Height() ? img->Height
                                                    : small.Height();
    row.Canvas = Surface(Surface::kWidth, height);
    row.Canvas.Blit(img->Pixels.data(), img->Width, img->Height, 0,
                     (height - img->Height) / 2);
    small.Draw(row.Canvas, Substitute(m_Ctx, text), textX,
               (height - small.Height()) / 2);
    row.Height = height - 1;
    m_Rows.push_back(std::move(row));
}

void TextBox::AddRaw(int kind, const std::string& text, int x) {
    Row row;
    row.Kind = kind;
    row.Text = text;
    row.X = x;
    Bake(row);
    m_Rows.push_back(std::move(row));
}

void TextBox::Bake(Row& row) {
    const Font& small = m_Ctx.SmallFont;
    const Font& big = m_Ctx.BigFont;
    switch (row.Kind) {
        case kImage: {
            const Texture* tex = m_Ctx.Textures.Load(row.Text);
            const TcTexture::Image* img = tex ? tex->Frame(0) : nullptr;
            if (!img) return;
            row.Canvas = Surface(img->Width, img->Height);
            row.Canvas.Copy(img->Pixels.data(), img->Width, img->Height, 0, 0);
            row.Height = img->Height - 1;
            return;
        }
        case kTitle: {
            const TcTexture::Image* bg =
                m_HeaderBg ? m_HeaderBg->Frame(m_HeaderFrame) : nullptr;
            if (!bg) return;
            row.X = 0;
            row.Canvas = Surface(bg->Width, bg->Height);
            row.Canvas.Copy(bg->Pixels.data(), bg->Width, bg->Height, 0, 0);
            const auto lines = WrapText(
                big, row.Text, row.Canvas.Width() - kTitleTextX - kWrapInset);
            int y = kTitleTextY;
            for (const std::string& l : lines) {
                big.Draw(row.Canvas, l, kTitleTextX, y);
                y += big.Height();
            }
            // The plank is taller than the words on it; the row only claims
            // the height of the text plus five (0x1008ca04 case 6).
            row.Height = int(lines.size()) * big.Height() + 5;
            return;
        }
        case kSubTopic:
        case kStatus:
        case kCentred: {
            const Texture* tex = row.Kind == kSubTopic  ? m_Subtopic
                                 : row.Kind == kStatus  ? m_StatusBg
                                                        : m_ThreeRows;
            const TcTexture::Image* bg = tex ? tex->Frame(0) : nullptr;
            if (!bg) return;
            const int extra = row.Kind == kSubTopic ? 5 : 0;
            row.Canvas = Surface(bg->Width, bg->Height + extra);
            row.Canvas.Copy(bg->Pixels.data(), bg->Width, bg->Height, 0,
                             row.Kind == kSubTopic  ? 5
                             : row.Kind == kCentred ? 2
                                                    : 0);
            if (row.Kind == kCentred) {
                const int w = small.Width(row.Text);
                small.Draw(row.Canvas, row.Text,
                           (row.Canvas.Width() - w) / 2, 2);
            } else {
                const int tx = row.Kind == kSubTopic ? kSubTopicTextX
                                                     : kStatusTextX;
                const int ty = row.Kind == kSubTopic ? kSubTopicTextY
                                                     : kStatusTextY;
                small.Draw(row.Canvas, row.Text, tx, ty);
            }
            row.Height = row.Canvas.Height() - (row.Kind == kStatus ? 8 : 9);
            return;
        }
        case kHeading: {
            // The big font, straight onto the screen, claiming nothing of the
            // running y -- see the note in the header.
            row.Plain = true;
            row.Big = true;
            row.Height = 0;
            return;
        }
        default: {
            // Plain text, wrapped, with no background at all.
            row.Plain = true;
            if (row.Kind == kTextLow) row.X += 5;
            const auto lines =
                WrapText(small, row.Text, Surface::kWidth - row.X - kWrapInset);
            row.Height = row.Kind == kTextLow
                             ? 15
                             : int(lines.size()) * small.Height() + 2;
            return;
        }
    }
}

const std::string& TextBox::RowText(int i) const {
    static const std::string kNone;
    if (i < 0 || i >= RowCount()) return kNone;
    return m_Rows[std::size_t(i)].Text;
}

int TextBox::RowKind(int i) const {
    if (i < 0 || i >= RowCount()) return -1;
    return m_Rows[std::size_t(i)].Kind;
}

int TextBox::Height() const {
    int h = 0;
    for (const Row& r : m_Rows) h += r.Height;
    return h;
}

// 0x1007c818: the furthest the list can travel is its running y less the
// screen minus the layout's bottom margin. Zero or below means it all fits.
int TextBox::ScrollLimit() const {
    return kListTop + Height() - (Surface::kHeight - kMarginTop);
}

void TextBox::Draw(Surface& dst) {
    if (m_Board) {
        if (const TcTexture::Image* b = m_Board->Frame(0))
            dst.Blit(b->Pixels.data(), b->Width, b->Height, 0, 0);
    }
    const Font& small = m_Ctx.SmallFont;
    // The running y starts at resource 0xac's second word, not at the top of
    // the board; only the title row ignores it and pins itself to (0, 0).
    // 0x1007c898 draws every row but the title, then the title last, so the
    // plank always sits over whatever the first body row put down.
    int y = kListTop - m_Scroll;
    const Row* title = nullptr;
    int titleY = 0;
    for (const Row& r : m_Rows) {
        if (r.Kind == kTitle) {
            // A title row ignores the running y and pins itself to the top of
            // the board (0x1008c378 case 6 zeroes both coordinates), but it
            // still claims its share of the running y.
            title = &r;
            titleY = -m_Scroll;
            y += r.Height;
            continue;
        }
        const int ry = y;
        if (r.Plain) {
            const Font& font = r.Big ? m_Ctx.BigFont : small;
            const auto lines = WrapText(font, r.Text,
                                        Surface::kWidth - r.X - kWrapInset);
            int ly = ry;
            for (const std::string& l : lines) {
                font.Draw(dst, l, r.X, ly);
                ly += font.Height();
            }
        } else if (r.Canvas.Width() > 1) {
            dst.Blit(r.Canvas.Pixels(), r.Canvas.Width(),
                     r.Canvas.Height(), r.X, ry);
        }
        y += r.Height;
    }
    if (title && title->Canvas.Width() > 1) {
        dst.Blit(title->Canvas.Pixels(), title->Canvas.Width(),
                 title->Canvas.Height(), title->X, titleY);
    }
    // The plank with the two arrows, whenever there is more prose than board.
    const int most = ScrollLimit();
    if (most > 0) chrome::DrawScrollPlank(m_Ctx.Textures, dst, m_Scroll, most);
    // The mode's two soft-key labels in the bottom corners, where the
    // device's keys sat -- with the physical key that stands in for each
    // drawn above, since a desktop has no keys under the screen.
    chrome::DrawSoftKeys(m_Ctx, dst, m_LeftKey, m_RightKey);
}

int TextBox::Step(Surface& dst) {
    Host& host = m_Ctx.HostRef;
    // Scroll one pixel per eight milliseconds while a direction is held, which
    // is what 0x100662c8 does with the tick counter.
    const uint32_t now = host.TickCount();
    if (m_LastScrollMs == 0) m_LastScrollMs = now;
    const int steps = int((now - m_LastScrollMs) / kScrollTickMs);
    if (steps > 0) {
        m_LastScrollMs += uint32_t(steps) * kScrollTickMs;
        // 0x1007c818: the furthest the list can go is its running y less
        // (208 - 24), the layout's bottom margin.
        const int limit = ScrollLimit();
        if (host.KeyHeld(Key::kDown)) m_Scroll += steps * kScrollPerTick;
        if (host.KeyHeld(Key::kUp)) m_Scroll -= steps * kScrollPerTick;
        if (m_Scroll > limit) m_Scroll = limit;
        if (m_Scroll < 0) m_Scroll = 0;
    }
    Draw(dst);
    if (host.QuitRequested()) return kQuit;
    if (host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kSoftLeft))
        return kConfirmed;
    if (host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight))
        return kCancelled;
    return kRunning;
}

int TextBox::Run() {
    Host& host = m_Ctx.HostRef;
    host.FlushKeys();
    for (;;) {
        const int r = Step(host.Screen());
        host.Flip();
        // A board is modal, so for as long as one is up this loop is the only
        // thing that can feed the mixer. The briefing board has a piece of
        // music of its own, and every result and objectives board is raised
        // over something already playing.
        if (m_Ctx.Sound) m_Ctx.Sound->Pump(host);
        host.Sleep(20);
        if (r != kRunning) return r;
        if (host.QuitRequested()) return kQuit;
    }
}

}  // namespace bb
