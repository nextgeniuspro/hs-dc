#include "game/MenuList.h"

#include "game/Font.h"
#include "game/MenuChrome.h"
#include "game/ResourceTable.h"
#include "game/TextureCache.h"
#include "platform/Surface.h"
#include "shim/Log.h"

namespace bb {
namespace {

constexpr uint16_t kBoardSlot = 0xb2;   // Data\Menu\shell_board.tc
constexpr uint16_t kCursorSlot = 0x2c;  // Data\Menu\sword.tc
constexpr uint16_t kRampSlot = 0x26;    // Data\Menu\volume.tc
constexpr uint16_t kTabSlot = 0x27;     // Data\Menu\volume_tab.tc
constexpr uint16_t kSubTopicSlot = 0xdf;  // Data\Menu\subtopic.tc

// Where the volume tab travels along the ramp. The slider widget's own track
// geometry (0x100d6000) isn't recovered, so these frame it inside the 176x35
// artwork, whose wedge runs from the left edge to about five sixths across.
constexpr int kTrackLeft = 14;
constexpr int kTrackRight = 150;
constexpr int kTabY = 12;

}  // namespace

bool MenuList::Load(TextureCache& textures, const Font& font, int firstIndex) {
    m_Textures = &textures;
    m_Font = &font;
    m_FirstIndex = firstIndex;
    m_Board = textures.Register(kBoardSlot, "Data\\Menu\\shell_board.tc");
    m_Cursor = textures.Register(kCursorSlot, "Data\\Menu\\sword.tc");
    m_Ramp = textures.Register(kRampSlot, "Data\\Menu\\volume.tc");
    m_Tab = textures.Register(kTabSlot, "Data\\Menu\\volume_tab.tc");
    if (!m_Board || !m_Board->Valid()) {
        LogError("menu: Data\\Menu\\shell_board.tc unavailable\n");
        return false;
    }
    return true;
}

void MenuList::Add(const std::string& label, int id, bool enabled,
                   const std::string& rightLabel) {
    Item item;
    item.Label = label;
    item.RightLabel = rightLabel;
    item.ID = id;
    item.Enabled = enabled;
    const int index = m_FirstIndex + static_cast<int>(m_Items.size());
    m_Items.push_back(std::move(item));
    Bake(m_Items.back(), index);
    SettleSelection();
}

void MenuList::AddCaption(const std::string& text) {
    Item item;
    item.Label = text;
    item.ID = -1;
    item.Enabled = false;
    item.Caption = true;
    m_Items.push_back(std::move(item));
    BakeCaption(m_Items.back());
    SettleSelection();
}

void MenuList::AddSlider(const std::string& label, int id, int value, int max) {
    Item item;
    item.Label = label;
    item.ID = id;
    item.Slider = true;
    item.Value = value;
    item.Max = max > 0 ? max : 1;
    const int index = m_FirstIndex + static_cast<int>(m_Items.size());
    m_Items.push_back(std::move(item));
    Bake(m_Items.back(), index);
    SettleSelection();
}

bool MenuList::Landable(int index) const {
    return index >= 0 && index < Count() && !m_Items[std::size_t(index)].Caption;
}

void MenuList::SettleSelection() {
    if (Landable(m_Selected)) return;
    for (int i = 0; i < Count(); ++i) {
        if (Landable(i)) {
            m_Selected = i;
            return;
        }
    }
    m_Selected = 0;
}

int MenuList::Advance(const Item& item) const {
    return item.Canvas.Height() -
           (item.Caption ? chrome::kSubTopicShort : 1);
}

bool MenuList::IsSlider(int index) const {
    return index >= 0 && index < Count() && m_Items[index].Slider;
}

int MenuList::SliderValue(int index) const {
    return IsSlider(index) ? m_Items[index].Value : 0;
}

bool MenuList::Adjust(int delta) {
    if (m_Items.empty() || !m_Items[m_Selected].Slider) return false;
    Item& item = m_Items[m_Selected];
    int v = item.Value + delta;
    if (v < 0) v = 0;
    if (v > item.Max) v = item.Max;
    if (v == item.Value) return true;  // consumed, just already at the end
    item.Value = v;
    Bake(item, m_FirstIndex + m_Selected);
    return true;
}

void MenuList::SetSelected(int i) {
    if (m_Items.empty()) return;
    m_Selected = ((i % Count()) + Count()) % Count();
    // A remembered index that has landed on a caption after a rebuild walks
    // forward to the next row rather than sitting somewhere the cursor cannot
    // be drawn.
    for (int n = 0; n < Count() && !Landable(m_Selected); ++n)
        m_Selected = (m_Selected + 1) % Count();
    SettleSelection();
}

void MenuList::Bake(Item& item, int index) {
    if (!m_Font) return;
    // Sliders use the wooden ramp instead of a board; everything else takes
    // frame `index % 3`, so consecutive buttons don't repeat the same grain.
    const Texture* base = item.Slider ? m_Ramp : m_Board;
    if (!base || !base->Valid()) return;
    const TcTexture::Image* src =
        item.Slider ? base->Frame(0) : base->Frame(index % kBoardFrames);
    if (!src) return;

    // Stamp the board in, then draw the label on top of it.
    item.Canvas = Surface(src->Width, src->Height);
    item.Canvas.Copy(src->Pixels.data(), src->Width, src->Height, 0, 0);

    if (!item.Enabled) {
        // Desaturate: keep alpha, set every channel to the channel average.
        // The original does exactly this in place (0x1008cbd4).
        uint16_t* px = item.Canvas.Pixels();
        const int n = item.Canvas.Width() * item.Canvas.Height();
        for (int i = 0; i < n; ++i) {
            const uint16_t p = px[i];
            const int avg = (((p >> 8) & 0xF) + ((p >> 4) & 0xF) + (p & 0xF)) / 3;
            px[i] = static_cast<uint16_t>((p & 0xF000) |
                                          static_cast<uint16_t>(avg * 0x111));
        }
    }

    if (item.RightLabel.empty()) {
        m_Font->Draw(item.Canvas, item.Label, kTextX, kTextY);
    } else {
        // The right label wins the space it needs; the left one is clipped to
        // what remains, so a long action name cannot run over its key.
        const int rightW = m_Font->Width(item.RightLabel);
        const int rightX = item.Canvas.Width() - rightW - kTextX / 2;
        m_Font->Draw(item.Canvas, item.RightLabel, rightX, kTextY);

        std::string left = item.Label;
        const int room = rightX - kTextX - kTextX / 2;
        while (!left.empty() && m_Font->Width(left) > room) left.pop_back();
        m_Font->Draw(item.Canvas, left, kTextX, kTextY);
    }

    if (item.Slider && m_Tab && m_Tab->Valid()) {
        if (const TcTexture::Image* t = m_Tab->Frame(0)) {
            const int span = kTrackRight - kTrackLeft;
            const int x = kTrackLeft + (span * item.Value) / item.Max;
            item.Canvas.Blit(t->Pixels.data(), t->Width, t->Height, x, kTabY);
        }
    }
}

// The kind-8 row, baked rather than drawn straight to the screen so it can
// scroll with the rest of the list: the slab five pixels down inside a surface
// five taller than itself, with the small font at (23, 6) inside the slab.
void MenuList::BakeCaption(Item& item) {
    if (!m_Font || !m_Textures) return;
    const Texture* slab =
        m_Textures->Register(kSubTopicSlot, "Data\\Menu\\subtopic.tc");
    const TcTexture::Image* img = slab ? slab->Frame(0) : nullptr;
    if (!img) return;
    item.Canvas = Surface(img->Width, img->Height + chrome::kSubTopicDrop);
    item.Canvas.Fill(0x0000u);
    item.Canvas.Blit(img->Pixels.data(), img->Width, img->Height, 0,
                      chrome::kSubTopicDrop);
    m_Font->Draw(item.Canvas, item.Label, chrome::kSubTopicTextX,
                chrome::kSubTopicDrop + chrome::kSubTopicTextY);
}

const std::string& MenuList::Label(int index) const {
    static const std::string kNone;
    if (index < 0 || index >= Count()) return kNone;
    return m_Items[std::size_t(index)].Label;
}

int MenuList::IdAt(int index) const {
    if (index < 0 || index >= Count()) return -1;
    return m_Items[std::size_t(index)].ID;
}

int MenuList::SelectedId() const {
    if (m_Items.empty()) return -1;
    return m_Items[m_Selected].ID;
}

bool MenuList::SelectedEnabled() const {
    return !m_Items.empty() && m_Items[m_Selected].Enabled;
}

int MenuList::Height() const {
    int h = 0;
    for (const Item& item : m_Items) h += Advance(item);
    return h;
}

void MenuList::MoveUp() {
    if (m_Items.empty()) return;
    for (int n = 0; n < Count(); ++n) {
        m_Selected = (m_Selected + Count() - 1) % Count();
        if (Landable(m_Selected)) return;
    }
}

void MenuList::MoveDown() {
    if (m_Items.empty()) return;
    for (int n = 0; n < Count(); ++n) {
        m_Selected = (m_Selected + 1) % Count();
        if (Landable(m_Selected)) return;
    }
}

void MenuList::Draw(Surface& dst, int y, uint32_t nowMs) {
    if (m_Items.empty()) return;

    // Each row advances by its own height minus one: the engine's item height
    // (0x1008ca04 returns surface height - 1) plus resource 0xaa's zero
    // spacing. Rows are not all the same height -- a volume ramp is 35px
    // against a button's 20.
    //
    // Lists longer than the screen scroll. The original keeps the selection
    // inside a window with margins of 24 above and 17 below (resource 0xaa
    // words 2 and 1, used by 0x1008cea0) and eases the offset toward its
    // target; the port clamps to the same window without the easing.
    const int view = dst.Height() - y;
    const int total = Height();
    m_Overflow = total > view;

    int selectedTop = 0, selectedH = 0, running = 0;
    for (int i = 0; i < Count(); ++i) {
        if (i == m_Selected) {
            selectedTop = running;
            selectedH = m_Items[i].Canvas.Height();
        }
        running += Advance(m_Items[i]);
    }

    if (!m_Overflow) {
        m_Scroll = 0;
    } else {
        if (selectedTop - m_Scroll < kScrollMarginTop)
            m_Scroll = selectedTop - kScrollMarginTop;
        if (selectedTop + selectedH - m_Scroll > view - kScrollMarginBottom)
            m_Scroll = selectedTop + selectedH - view + kScrollMarginBottom;
        if (m_Scroll > total - view) m_Scroll = total - view;
        if (m_Scroll < 0) m_Scroll = 0;
    }

    int itemY = y - m_Scroll;
    int selectedY = y;
    for (int i = 0; i < Count(); ++i) {
        const Item& item = m_Items[i];
        // A caption ignores the item x: its slab is drawn against the left
        // edge, which is what lets its rings overhang the boards below.
        dst.Blit(item.Canvas.Pixels(), item.Canvas.Width(),
                 item.Canvas.Height(), item.Caption ? 0 : kItemX, itemY);
        if (i == m_Selected) selectedY = itemY;
        itemY += Advance(item);
    }

    // The plank at the bottom, when there is more list than screen.
    if (m_Overflow && m_Textures)
        chrome::DrawScrollPlank(*m_Textures, dst, m_Scroll, total - view);

    if (!m_CursorShown || !m_Cursor || !m_Cursor->Valid()) return;
    // One frame every 30 ms, cycling the whole animation (0x1003a788).
    const int frames = static_cast<int>(m_Cursor->Frames.size());
    if (nowMs - m_CursorLastMs > kCursorFrameMs) {
        m_CursorFrame = (m_CursorFrame + 1) % frames;
        m_CursorLastMs = nowMs;
    }
    if (const TcTexture::Image* c = m_Cursor->Frame(m_CursorFrame)) {
        dst.Blit(c->Pixels.data(), c->Width, c->Height, kItemX - kCursorOffset,
                 selectedY - kCursorOffset);
    }
}

}  // namespace bb
