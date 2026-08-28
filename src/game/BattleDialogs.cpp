#include "game/BattleDialogs.h"

#include <algorithm>
#include <map>
#include <vector>

#include "game/BattleField.h"
#include "game/Font.h"
#include "game/Game.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/TextBox.h"
#include "game/TextureCache.h"
#include "platform/Host.h"

namespace bb {
namespace {

constexpr const char* kPlankSmall = "Data\\Menu\\Plank-26-player.tc";
constexpr const char* kPlankBig = "Data\\Menu\\Plank-27-playerbig.tc";
constexpr const char* kPlankVersus = "Data\\Menu\\Plank-28-versus.tc";
constexpr const char* kFlagsBig = "Data\\icons\\flags.tc";
constexpr const char* kFlagsMed = "Data\\icons\\flags-med.tc";
constexpr const char* kFlagsSmall = "Data\\icons\\flags-small.tc";

// The dialogue panel's furniture, all from the startup loader's slots.
constexpr uint16_t kSlotFullBoard = 0x32;   // Data\Menu\fullboard.tc
constexpr uint16_t kSlotEdge = 0xc0;        // Data\anim\sub-bga.tc
constexpr uint16_t kSlotPlate = 0xdb;       // Data\anim\sub-name.tc
constexpr uint16_t kSlotPlateEnd = 0xdc;    // Data\anim\sub-name-end.tc
constexpr uint16_t kSlotPlateStart = 0xdd;  // Data\anim\sub-name-start.tc
constexpr uint16_t kSlotPlateTop = 0xde;    // Data\anim\sub-name-top.tc
// The other name bar: 176x15, one piece, the full width of the screen. The
// startup loader has always fetched it and nothing had ever drawn it -- the
// cutscene subtitle class does not know about it, because it is the *battle*
// dialogue's, and only when the panel comes down from the top (0x100a7044
// stores it at panel+0xcc, 0x100a729c draws it in the `mode != 0` branch).
constexpr uint16_t kSlotPlateWide = 0xea;   // Data\anim\sub-name-secondary.tc

void Blit(Surface& dst, const Texture* tex, int frame, int x, int y,
          const Surface::Rect* clip = nullptr) {
    if (!tex) return;
    const TcTexture::Image* img = tex->Frame(frame);
    if (!img) img = tex->Frame(0);
    if (!img) return;
    if (clip) {
        dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0,
                       img->Width, img->Height, x, y, clip);
    } else {
        dst.Blit(img->Pixels.data(), img->Width, img->Height, x, y);
    }
}

int TexWidth(const Texture* tex) {
    const TcTexture::Image* img = tex ? tex->Frame(0) : nullptr;
    return img ? img->Width : 0;
}

int TexHeight(const Texture* tex) {
    const TcTexture::Image* img = tex ? tex->Frame(0) : nullptr;
    return img ? img->Height : 0;
}

// 0x1008c344: -1, 0 or 1.
int Sign(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

}  // namespace

void TurnCard::Build(GameContext& ctx, const BattleField& field, int viewer) {
    m_Rows.clear();
    m_Seats.clear();
    const int current = field.CurrentPlayer();
    m_Title = ctx.StringsRef.Get(current == viewer ? kStrYouAreNext
                                               : kStrNextCommander);
    // The engine's round counter is zero-based and the card adds one; the
    // port's already counts from one, so it prints as it stands. (A timed
    // game appends " / <limit>"; campaign missions have no limit.)
    m_TurnLine = ctx.StringsRef.Get(kStrTurn) + " " + std::to_string(field.Round());

    // Every seated player, starting from whoever is about to move and going
    // round the table (0x100cc460 walks `(current + i - 1) % 4 + 1`).
    for (int i = 0; i < BattleField::kMaxPlayers; ++i) {
        const int slot = (current + i - 1) % BattleField::kMaxPlayers + 1;
        const BattleField::Player& p = field.Players()[std::size_t(slot)];
        if (!p.Present) continue;
        int label = kStrEnemy;
        if (slot == viewer) label = kStrYou;
        else if (field.SameTeam(slot, viewer)) label = kStrFriendly;
        // The engine glues the name and the label with a literal backslash-n
        // and lets the wrap engine break it (0x100cc8a0).
        m_Rows.push_back(p.Name + "\\n" + ctx.StringsRef.Get(label));
        m_Seats.push_back({slot, m_Rows.back()});
    }
}

// The bottom plank: one badge per player, grouped by team, with "vs" between
// the groups (0x100ccd70's tail).
void TurnCard::DrawSides(GameContext& ctx, const BattleField& field,
                         Surface& dst, int y) const {
    const Texture* smallFlags = ctx.Textures.Load(kFlagsSmall);
    if (!smallFlags) return;

    // Teams in the order their first member is seated. A player the mission
    // put on no team has an id of their own rather than a blank one, so a
    // free-for-all falls out of the same grouping as four separate sides.
    std::vector<std::vector<int>> sides;
    std::map<int, int> byTeam;
    for (int slot = 1; slot <= BattleField::kMaxPlayers; ++slot) {
        const BattleField::Player& p = field.Players()[std::size_t(slot)];
        if (!p.Present) continue;
        auto it = byTeam.find(p.Team);
        if (it != byTeam.end()) {
            sides[std::size_t(it->second)].push_back(slot);
        } else {
            byTeam[p.Team] = int(sides.size());
            sides.push_back({slot});
        }
    }
    if (sides.empty()) return;

    const int column = kBadgeSpan / int(sides.size());
    int x = column / 2 - int(sides.size()) * column / 2 + kBadgeLeft;
    for (std::size_t s = 0; s < sides.size(); ++s) {
        // Three or more on a side and the column starts ten pixels left, so
        // the second stack still fits.
        int dx = sides[s].size() < 3 ? 0 : -10;
        int dy = 0;
        int row = 0;
        for (int slot : sides[s]) {
            const BattleField::Player& p = field.Players()[std::size_t(slot)];
            const int frame = (p.Alive ? 0 : 4) + field.Colour(slot) - 1;
            Blit(dst, smallFlags, frame, x + dx - 5,
                 y + kBadgeY + row * 10 + dy);
            if (++row > 1) { row = 0; dx = 10; dy = 5; }
        }
        if (s + 1 < sides.size()) {
            ctx.SmallFont.Draw(dst, ctx.StringsRef.Get(kStrVersus),
                                x + column / 2 - 4, y + kBadgeY);
        }
        x += column;
    }
}

void TurnCard::Draw(GameContext& ctx, const BattleField& field,
                    Surface& dst) const {
    const Font& small = ctx.SmallFont;
    const Font& big = ctx.BigFont;
    const Texture* bigPlank = ctx.Textures.Load(kPlankBig);
    const Texture* plank = ctx.Textures.Load(kPlankSmall);
    const Texture* header = ctx.Textures.Load(kPlankVersus);
    const Texture* flagsBig = ctx.Textures.Load(kFlagsBig);
    const Texture* flagsMed = ctx.Textures.Load(kFlagsMed);

    // The header plank carries the two lines of title, both centred on x=88.
    Blit(dst, header, 0, kPlankX, kHeaderY);
    big.Draw(dst, m_Title, Surface::kWidth / 2 - big.Width(m_Title) / 2, kTitleY);
    small.Draw(dst, m_TurnLine,
               Surface::kWidth / 2 - small.Width(m_TurnLine) / 2, kTurnY);

    int textY = kListY;
    int plankY = kPlankY;
    for (std::size_t i = 0; i < m_Seats.size(); ++i) {
        const int slot = m_Seats[i].Slot;
        const int colour = field.Colour(slot);
        if (i == 0) {
            Blit(dst, bigPlank, 0, kPlankX, kListY);
            // flags.tc has four frames and is indexed by the colour 1..4, so
            // colour four runs off the end and falls back to frame 0.
            Blit(dst, flagsBig, colour, kFlagX, plankY);
        } else {
            Blit(dst, plank, 0, kPlankX, plankY);
            Blit(dst, flagsMed, (colour - 1) & 3, kFlagX, plankY);
        }
        const int x = i == 0 ? kFirstX : kX;
        int ly = textY;
        for (const std::string& l :
             WrapText(small, m_Seats[i].Text, Surface::kWidth - x - 3)) {
            small.Draw(dst, l, x, ly);
            ly += small.Height();
        }
        const int drop = i == 0 ? kFirstDrop : kDrop;
        textY += drop;
        plankY += drop;
    }

    Blit(dst, header, 0, kPlankX, plankY + 1);
    DrawSides(ctx, field, dst, plankY);
    // No soft keys: 0x100cc460 builds its board with mode 2, the blank pair.
    // Any key dismisses it, and the original labels none of them.
}

bool TurnCard::Run(GameContext& ctx, const BattleField& field,
                   const std::function<void(Surface&)>& backdrop) {
    Host& host = ctx.HostRef;
    host.FlushKeys();
    while (!host.QuitRequested()) {
        if (backdrop) backdrop(host.Screen());
        Draw(ctx, field, host.Screen());
        host.Flip();
        // The card is modal and it is raised right after the turn chime, so
        // without this the chime is cut off by its own banner.
        if (ctx.Sound) ctx.Sound->Pump(host);
        host.Sleep(20);
        if (host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kSoftLeft) ||
            host.KeyPressed(Key::kBack) || host.KeyPressed(Key::kSoftRight))
            return true;
    }
    return false;
}

// --- scripted dialogue ------------------------------------------------------

void BattleDialogue::Measure(GameContext& ctx) {
    const Font& small = ctx.SmallFont;
    // The wrap runs from x=3 to three pixels short of the screen's right edge,
    // which is what 0x10072724 does with the target's clip rectangle.
    m_Lines = WrapText(small, m_Line, Surface::kWidth - kTextX - 3);
    if (m_Lines.empty()) m_Lines.push_back(std::string());
    m_BodyH = int(m_Lines.size()) * small.Height() + kBodyPad;

    m_PlateH = 0;
    if (!m_Speaker.empty()) {
        TextureCache& tc = ctx.Textures;
        const int a = TexHeight(tc.Register(kSlotPlateStart,
                                            "Data\\anim\\sub-name-start.tc"));
        const int b = TexHeight(tc.Register(kSlotPlate, "Data\\anim\\sub-name.tc"));
        const int c = TexHeight(tc.Register(kSlotPlateEnd,
                                            "Data\\anim\\sub-name-end.tc"));
        m_PlateH = std::max(std::max(a, b), c);
    }
    if (m_Position == 0) {
        // It rises from below the screen and stops with the parchment flush
        // against the bottom edge.
        m_StartY = Surface::kHeight + m_PlateH;
        m_RestY = Surface::kHeight - m_BodyH;
    } else {
        // The other way up: in from above, resting just below its own plate.
        m_StartY = -(m_PlateH + m_BodyH);
        m_RestY = m_PlateH - kPlateGap;
    }
}

void BattleDialogue::Set(GameContext& ctx, int textID, int speakerID,
                         int position) {
    m_Position = position;
    // Both are ordinary string ids, and the speaker is often 5113 -- literally
    // "[player]" -- so it goes through the same substitution the boards use.
    m_Speaker = TextBox::Substitute(ctx, ctx.StringsRef.Get(speakerID));
    m_Line = TextBox::Substitute(ctx, ctx.StringsRef.Get(textID));
    Measure(ctx);
    m_Y = m_StartY;
    m_Phase = kEntering;
}

void BattleDialogue::Dismiss() {
    if (m_Phase == kEntering || m_Phase == kResting) m_Phase = kLeaving;
}

bool BattleDialogue::Update(GameContext& ctx) {
    (void)ctx;
    if (m_Phase == kGone) return false;
    // Both slides ease by a quarter of what is left, never less than a pixel
    // (0x100a729c). Leaving runs the same sum with the sign flipped, so the
    // panel accelerates back out the way it came in.
    if (m_Phase == kEntering) {
        int d = m_RestY - m_Y;
        if (d < 0) d += 3;
        d >>= 2;
        if (d == 0) d = Sign(m_RestY - m_Y);
        m_Y += d;
        if (m_Y == m_RestY) m_Phase = kResting;
    } else if (m_Phase == kLeaving) {
        int d = m_RestY - m_Y;
        if (d < 0) d += 3;
        d >>= 2;
        if (d == 0) d = -Sign(m_StartY - m_Y);
        m_Y -= d;
        if (std::abs(m_Y - m_RestY) >= m_PlateH + m_BodyH) {
            m_Phase = kGone;
            return false;
        }
    }
    return true;
}

void BattleDialogue::Draw(GameContext& ctx, Surface& dst) const {
    if (m_Phase == kGone) return;
    TextureCache& tc = ctx.Textures;
    const Font& small = ctx.SmallFont;

    const Texture* board = tc.Register(kSlotFullBoard, "Data\\Menu\\fullboard.tc");
    const Texture* edge = tc.Register(kSlotEdge, "Data\\anim\\sub-bga.tc");

    // The parchment is the full-screen board clipped to the band the text
    // needs, with the torn edge above it and below it.
    Surface::Rect band;
    band.X0 = 0;
    band.Y0 = m_Y;
    band.X1 = Surface::kWidth;
    band.Y1 = m_Y + m_BodyH;
    if (band.Y1 > Surface::kHeight) band.Y1 = Surface::kHeight;

    if (!m_Speaker.empty()) {
        const Texture* plate = tc.Register(kSlotPlate, "Data\\anim\\sub-name.tc");
        const Texture* start =
            tc.Register(kSlotPlateStart, "Data\\anim\\sub-name-start.tc");
        const int startW = TexWidth(start);
        const int nameW = small.Width(m_Speaker) + kBodyPad;
        const int plateHh = TexHeight(plate);

        if (m_Position == 0) {
            const Texture* end =
                tc.Register(kSlotPlateEnd, "Data\\anim\\sub-name-end.tc");
            const Texture* top =
                tc.Register(kSlotPlateTop, "Data\\anim\\sub-name-top.tc");
            const int plateW = TexWidth(plate);
            const int topH = TexHeight(top);

            // Four pieces: a strip clipped to exactly the name's width with a
            // cap at each end, so the plate looks tailored to whoever is
            // speaking.
            Surface::Rect plateClip;
            plateClip.X0 = 0;
            plateClip.Y0 = 0;
            plateClip.X1 = startW + nameW;
            plateClip.Y1 = Surface::kHeight;
            Surface::Rect topClip = plateClip;
            topClip.X0 = startW - kPlateGap;

            Blit(dst, plate, 0, startW + nameW - plateW + kPlateGap,
                 m_Y - plateHh - kPlateGap, &plateClip);
            Blit(dst, top, 0, startW + nameW - plateW + kPlateGap,
                 m_Y - plateHh - topH - kPlateGap, &topClip);
            Blit(dst, start, 0, 0, m_Y - TexHeight(start));
            Blit(dst, end, 0, startW + nameW, m_Y - TexHeight(end) - kPlateGap);
        } else {
            // Coming down from the top it is one board across the whole
            // screen, sitting on the parchment's upper edge -- no caps, no
            // tailoring, because there is nothing beside it to tail off into.
            const Texture* wide =
                tc.Register(kSlotPlateWide, "Data\\anim\\sub-name-secondary.tc");
            Blit(dst, wide, 0, 0, m_Y - TexHeight(wide));
        }
        // The name itself goes in the same place either way: 0x100a729c writes
        // it outside the branch, off the narrow plate's measurements.
        small.Draw(dst, m_Speaker, startW + kPlateTextX,
                   m_Y - plateHh - kPlateGap - 1);
    }

    // Only the parchment is clipped to the band (0x100a729c pushes the clip on
    // the board's own texture object). The two torn edges sit *outside* it --
    // the top one bridges the gap up to the name plate, which is exactly what
    // clipping it away used to leave missing.
    Blit(dst, edge, 0, 0, m_Y - TexHeight(edge));
    Blit(dst, board, 0, 0, m_Y, &band);
    Blit(dst, edge, 1, 0, m_Y + m_BodyH);

    int ly = m_Y;
    for (const std::string& l : m_Lines) {
        small.Draw(dst, l, kTextX, ly);
        ly += small.Height();
    }
}

}  // namespace bb
