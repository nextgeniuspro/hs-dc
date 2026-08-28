#include "game/PerkFlash.h"

#include "game/BattleRenderer.h"
#include "game/Perks.h"
#include "game/TcTexture.h"
#include "game/TextureCache.h"
#include "shim/Log.h"

namespace bb {
namespace {

// The tint the fallback washes over an affected block, and how far into the
// animation it peaks. The colour is the warm one the sheets themselves build
// to, so a machine that cannot spare the sheet still shows the same idea.
constexpr int kTintR = 7, kTintG = 5, kTintB = 2;

}  // namespace

bool PerkFlash::Load(TextureCache& cache, int sheet) {
    if (sheet < 0 || sheet > 9) return false;
    if (sheet == m_Loaded && Ready()) return true;
    m_Claims.emplace(cache);
    const Texture* t = m_Claims->Load(PerkSheetPath(sheet));
    // A texture that came back but decoded to nothing is not a texture. It
    // used to pass this test, set the animation running, and then be dropped
    // by the first Step -- an effect that fired and showed nothing at all.
    m_Sheet = t && t->Valid() && !t->Frames.empty() ? t : nullptr;
    m_Loaded = m_Sheet ? sheet : -1;
    if (!m_Sheet)
        LogError("perk: sheet %d unavailable; drawing the plain tint\n", sheet);
    return Ready();
}

void PerkFlash::Unload() {
    m_Claims.reset();
    m_Sheet = nullptr;
    m_Loaded = -1;
    m_Active = false;
}

int PerkFlash::Frames() const {
    return m_Sheet ? int(m_Sheet->Frames.size()) : kFallbackFrames;
}

void PerkFlash::Begin(std::vector<Cell> cells, int blend) {
    if (cells.empty()) return;
    m_Cells = std::move(cells);
    m_Blend = blend;
    m_Frame = 0;
    // Playing with no sheet is not nothing: the point of the effect is to say
    // which units the perk reached, and that has to survive a sheet the
    // machine could not spare the memory to decode.
    m_Active = true;
}

// The plain tint, for when there is no sheet: the whole block washed with the
// same warm colour, ramping up and back down over the animation so it reads as
// the unit changing colour rather than a rectangle appearing on it.
void PerkFlash::DrawTint(Surface& dst, int px, int py, int strength) const {
    if (strength <= 0) return;
    const int tile = BattleRenderer::kTile;
    const uint16_t tint = uint16_t(0xF000u |
                                   ((kTintR * strength / 15) << 8) |
                                   ((kTintG * strength / 15) << 4) |
                                   (kTintB * strength / 15));
    for (int y = 0; y < tile; ++y) {
        const int dy = py + y;
        if (dy < 0 || dy >= dst.Height()) continue;
        for (int x = 0; x < tile; ++x) {
            const int dx = px + x;
            if (dx < 0 || dx >= dst.Width()) continue;
            uint16_t* p = &dst.Pixels()[std::size_t(dy) * dst.Width() + dx];
            unsigned r = (*p & 0x0F00u) + (tint & 0x0F00u);
            unsigned g = (*p & 0x00F0u) + (tint & 0x00F0u);
            unsigned b = (*p & 0x000Fu) + (tint & 0x000Fu);
            if (r > 0x0F00u) r = 0x0F00u;
            if (g > 0x00F0u) g = 0x00F0u;
            if (b > 0x000Fu) b = 0x000Fu;
            *p = uint16_t(0xF000u | r | g | b);
        }
    }
}

bool PerkFlash::Step(Surface& dst, int camX, int camY) {
    if (!m_Active) return false;
    const int frames = Frames();
    const TcTexture::Image* img =
        m_Sheet ? m_Sheet->Frame(m_Frame % frames) : nullptr;
    // Without a sheet: a triangle wave over the animation, so the wash grows
    // and fades instead of snapping on.
    const int half = frames / 2 + 1;
    const int strength =
        img ? 0 : 15 * (m_Frame < half ? m_Frame : frames - m_Frame) / half;

    for (const Cell& c : m_Cells) {
        const int tile = BattleRenderer::kTile;
        const int cx = c.X * tile + tile / 2 - camX;
        const int cy = c.Y * tile + tile / 2 - camY;
        if (!img) {
            DrawTint(dst, cx - tile / 2, cy - tile / 2, strength);
            continue;
        }
        // Centred on the block, the way the engine starts one at each
        // affected unit's own position.
        const int px = cx - img->Width / 2;
        const int py = cy - img->Height / 2;
        // A perk that reaches a whole army puts one of these 75x75 sheets over
        // every unit in it, and compositing them is the most expensive thing a
        // frame can be asked to do. The ones that would land off screen are
        // not composited at all.
        if (px >= dst.Width() || py >= dst.Height() || px + img->Width <= 0 ||
            py + img->Height <= 0)
            continue;
        if (m_Blend == kBlendAlpha)
            dst.Blit(img->Pixels.data(), img->Width, img->Height, px, py);
        else
            dst.BlitAdditive(img->Pixels.data(), img->Width, img->Height, px,
                             py);
    }
    if (++m_Frame >= frames) {
        m_Active = false;
        return false;
    }
    return true;
}

}  // namespace bb
