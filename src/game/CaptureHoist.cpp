#include "game/CaptureHoist.h"

#include <algorithm>

#include "game/BattleRenderer.h"
#include "game/TcTexture.h"
#include "game/TextureCache.h"

namespace bb {
namespace {

// 0x1007d388: step and wrap.
void Wrap(uint32_t& counter, uint32_t limit, uint32_t step) {
    counter += step;
    if (counter >= limit) counter = 0;
}

// 0x1007d3a8: step and turn round at either end.
void PingPong(uint32_t& counter, int& dir, uint32_t limit, uint32_t step) {
    counter = dir == 1 ? counter + step : counter - step;
    if (counter >= limit) {
        dir = -1;
        counter -= step;
    }
    if (counter == 0) {
        dir = 1;
        counter += step;
    }
}

}  // namespace

bool CaptureHoist::Load(TextureCache& cache) {
    m_Claims.emplace(cache);
    TextureSet& textures = *m_Claims;
    m_Panel = textures.Load("Data\\fight\\gfx\\hoist\\panel.tc");
    m_Buildings = textures.Load("Data\\fight\\gfx\\hoist\\buildings.tc");
    // The two that wear the player's colours are palette-indexed, as the
    // engine's `FUN_100b5734(mgr, path, palette)` says by taking a palette.
    m_Soldier = textures.LoadIndexed("Data\\fight\\gfx\\hoist\\soldier.tc");
    m_Flag = textures.LoadIndexed("Data\\fight\\gfx\\hoist\\flag.tc");
    return Ready();
}

void CaptureHoist::Begin(int cellX, int cellY, int propertyType, int colour,
                         int pointsBefore, int pointsAfter) {
    if (!Ready()) return;
    m_Active = true;
    m_CellX = cellX;
    m_CellY = cellY;
    // Five frames for fourteen kinds of building; anything past the end takes
    // the last one, exactly as the constructor's own bounds check does.
    m_Building = propertyType >= 0 && propertyType < kBuildingFrames
                    ? propertyType
                    : kBuildingFrames - 1;
    m_Colour = colour;
    m_Rise = std::max(0, pointsBefore) * kPointRise;
    m_RiseTarget = std::max(0, pointsAfter) * kPointRise;
    m_Hold = kHold;
    m_SoldierStep = 0;
    m_SoldierDir = 1;
    m_FlagStep = 0;
}

bool CaptureHoist::Step(Surface& dst, const BattleRenderer& renderer,
                        int camX, int camY) {
    if (!m_Active || !Ready()) return false;
    const TcTexture::Image* board = m_Panel->Frame(0);
    if (!board) {
        m_Active = false;
        return false;
    }
    // The panel is centred on the square and then pushed back inside the
    // screen; the engine clamps to 176x208 with the panel's own size.
    int px = m_CellX * BattleRenderer::kTile + BattleRenderer::kTile / 2 -
             camX - board->Width / 2;
    int py = m_CellY * BattleRenderer::kTile + BattleRenderer::kTile / 2 -
             camY - board->Height / 2;
    px = std::clamp(px, 0, Surface::kWidth - board->Width);
    py = std::clamp(py, 0, Surface::kHeight - board->Height);

    const Surface::Rect clip{0, 0, Surface::kWidth, Surface::kHeight};
    const auto blit = [&](const TcTexture::Image* img, int x, int y) {
        if (img)
            dst.BlitRegion(img->Pixels.data(), img->Width, img->Height, 0, 0,
                           img->Width, img->Height, x, y, &clip);
    };
    const uint16_t* lut = renderer.OwnerLut(m_Colour);
    const auto blitOwned = [&](const TcTexture::Image* img, int x, int y) {
        if (img && lut)
            dst.BlitIndexed(img->Pixels.data(), img->Width, img->Height, x, y,
                            lut, renderer.LutSize(), &clip);
    };

    blit(board, px, py);
    blit(m_Buildings->Frame(m_Building), px + kBuildingX, py + kBuildingY);
    blitOwned(m_Soldier->Frame(int(m_SoldierStep >> 2)), px + kPoleX,
               py + kPoleY);
    blitOwned(m_Flag->Frame(int(m_FlagStep >> 2)), px + kPoleX,
               py + kPoleY + (m_Rise >> 16));

    Wrap(m_FlagStep, uint32_t(m_Flag->Frames.size()) << 2, 2);
    m_Rise -= kRiseSpeed;
    PingPong(m_SoldierStep, m_SoldierDir, uint32_t(m_Soldier->Frames.size()) << 2,
             2);
    if (m_Rise <= m_RiseTarget) {
        m_Rise = m_RiseTarget;
        if (--m_Hold <= 0) m_Active = false;
    }
    return m_Active;
}

}  // namespace bb
