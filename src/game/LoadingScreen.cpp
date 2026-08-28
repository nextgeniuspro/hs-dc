#include "game/LoadingScreen.h"

#include "game/Font.h"
#include "game/Game.h"
#include "game/SoundManager.h"
#include "game/Strings.h"
#include "game/TextureCache.h"
#include "platform/Host.h"
#include "platform/Surface.h"

namespace bb {

LoadingScreen::LoadingScreen(GameContext& ctx) : m_Ctx(ctx) {
    m_Loader = ctx.Textures.Load("Data\\loadscreen\\loader.tc");
    m_Board = ctx.Textures.Register(0x32, "Data\\Menu\\fullboard.tc");
}

void LoadingScreen::DrawFrame(int frame) {
    Surface& s = m_Ctx.HostRef.Screen();
    if (m_Board) {
        if (const TcTexture::Image* b = m_Board->Frame(0))
            s.Copy(b->Pixels.data(), b->Width, b->Height, 0, 0);
    }
    const std::string& text = m_Ctx.StringsRef.Get(kStringId);
    const Font& big = m_Ctx.BigFont;
    big.Draw(s, text, Surface::kWidth / 2 - big.Width(text) / 2, kTextY);
    if (m_Loader) {
        // The bar sits twenty pixels down (the original passes 0x140000 in
        // 16.16), and the frame is the progress step.
        if (const TcTexture::Image* f = m_Loader->Frame(frame))
            s.Blit(f->Pixels.data(), f->Width, f->Height, 0, 20);
    }
    m_Ctx.HostRef.Flip();
    // A loading screen is put up over whatever was playing -- the chart's
    // music, on the way into a mission -- and it is the only thing running
    // while it is up.
    if (m_Ctx.Sound) m_Ctx.Sound->Pump(m_Ctx.HostRef);
}

void LoadingScreen::Step(int percent) {
    if (percent < 0) percent = 0;
    const int target = int(float(percent) / kPercentPerFrame);
    if (target <= m_Frame) return;
    for (; m_Frame <= target; ++m_Frame) {
        DrawFrame(m_Frame < kFrames ? m_Frame : kFrames - 1);
        if (m_Frame != target) m_Ctx.HostRef.Sleep(kStepMs);
        if (m_Ctx.HostRef.QuitRequested()) break;
    }
    m_Frame = target;
}

}  // namespace bb
