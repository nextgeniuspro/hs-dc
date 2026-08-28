#include "game/Boot.h"

#include "game/TextureCache.h"
#include "platform/Host.h"
#include "platform/Surface.h"
#include "shim/Log.h"

namespace bb {
namespace {

// The engine copies this 5 x 0x24 table onto the stack and indexes it by the
// language byte; the paths are the binary's, at 0x10140a83.
constexpr const char* kNokiaLogos[] = {
    "Data\\loadscreen\\Nokia_logo_EN.tc",
    "Data\\loadscreen\\Nokia_logo_FR.tc",
    "Data\\loadscreen\\Nokia_logo_IT.tc",
    "Data\\loadscreen\\Nokia_logo_GE.tc",
    "Data\\loadscreen\\Nokia_logo_SP.tc",
};
constexpr const char* kEsrbSplash = "Data\\loadscreen\\splash_esrb_start.tc";

// A second localised table, same shape, at 0x101413d9 -- the "published by
// N-Gage" screen.
constexpr const char* kPublisherSplashes[] = {
    "Data\\loadscreen\\Nokia_splash_EN.tc",
    "Data\\loadscreen\\Nokia_splash_FR.tc",
    "Data\\loadscreen\\Nokia_splash_IT.tc",
    "Data\\loadscreen\\Nokia_splash_GE.tc",
    "Data\\loadscreen\\Nokia_splash_SP.tc",
};
constexpr const char* kDeveloperSplash = "Data\\loadscreen\\splash_rl.tc";
constexpr const char* kRatingSplash = "Data\\loadscreen\\splash_oflc.tc";
constexpr const char* kTitleSplash = "Data\\loadscreen\\splash_hs.tc";

// FUN_1008d320: 16 frames, 20 ms apart. The fade object advances one alpha
// level (0x10000 in 16.16) per frame and stops once it reaches 15, so the last
// couple of frames show the target unblended.
constexpr int kFadeFrames = 16;
constexpr int kFadeFrameMs = 20;

// Every held splash waits the same 1799 ms, polled every 10 ms. The four hold
// constants in the binary (DAT_1008db5c, DAT_1008e2e4, DAT_1008e3b0,
// DAT_1008e578) are separate but all 0x707.
constexpr uint32_t kSplashHoldMs = 1799;
constexpr int kSplashPollMs = 10;

// DELIBERATE DIVERGENCES, both stand-ins for work the device did and this
// machine does not. Neither number is in the binary: on hardware they were
// emergent load times, so they can't be recovered by reading it. These are the
// two knobs to turn if the pacing feels wrong.
//
// The Nokia logo gets no timer at all -- 0x1008d644 fades it, calls
// tickCount(), throws the result away, and moves on. What kept it on screen was
// the work that follows: setting the language and building the string table,
// which means inflating and parsing a 190 KB file of 2659 entries on a 104 MHz
// ARM9. A short beat stands in for that.
constexpr uint32_t kLogoDwellMs = 250;

// The title splash gets no timer either. Nothing redraws the screen between
// the splash sequence ending and the intro cutscene's first frame, so what
// kept it up was simply the cutscene loading: constructing the animation
// player and pulling "01-Intro" -- eight paintings, several of them 200x300 --
// off MMC. That load is passed in here, so on a slow machine it fills the
// dwell by itself and this only tops it up.
constexpr uint32_t kTitleDwellMs = 700;

// Cross-fade to `path`, then hold for `holdMs` -- in that order, which is the
// order the loader uses: the fade returns, *then* it reads the clock and waits.
// Any work done inside `afterFade` counts toward the wait, so on the device
// loading and waiting overlapped.
void ShowSplash(Host& host, TextureCache& textures, const char* path,
                uint32_t holdMs,
                const std::function<void()>& afterFade = nullptr) {
    const Texture* tex = textures.Load(path);
    if (!tex || !tex->Valid()) {
        LogError("boot: '%s' unavailable, skipping\n", path);
        return;
    }
    Surface image;
    DrawFullScreen(image, *tex);
    CrossFade(host, image);
    if (host.QuitRequested()) return;

    const uint32_t shown = host.TickCount();
    if (afterFade) afterFade();
    while (host.TickCount() - shown <= holdMs) {
        if (host.QuitRequested()) return;
        host.Sleep(kSplashPollMs);
    }
}

}  // namespace

const char* NokiaLogoPath(Language lang) {
    const int i = static_cast<int>(lang);
    const int n = static_cast<int>(sizeof(kNokiaLogos) / sizeof(kNokiaLogos[0]));
    return kNokiaLogos[(i >= 0 && i < n) ? i : 0];
}

const char* PublisherSplashPath(Language lang) {
    const int i = static_cast<int>(lang);
    const int n = static_cast<int>(sizeof(kPublisherSplashes) /
                                   sizeof(kPublisherSplashes[0]));
    return kPublisherSplashes[(i >= 0 && i < n) ? i : 0];
}

void DrawFullScreen(Surface& dst, const Texture& tex) {
    const TcTexture::Image* frame = tex.Frame(0);
    if (!frame) return;
    const int x = (dst.Width() - frame->Width) / 2;
    const int y = (dst.Height() - frame->Height) / 2;
    dst.Fill(0xF000u);  // opaque black behind anything smaller than the screen
    dst.Copy(frame->Pixels.data(), frame->Width, frame->Height, x, y);
}

void CrossFade(Host& host, const Surface& target) {
    Surface& screen = host.Screen();
    const Surface from = screen;  // what the fade starts from
    const size_t n = size_t(Surface::kWidth) * Surface::kHeight;

    for (int f = 0; f < kFadeFrames; ++f) {
        // Alpha climbs 1..14 and then saturates; at saturation the engine's
        // fade bails out and leaves the freshly copied target in place.
        const int alpha = f + 1;
        const uint16_t* src = from.Pixels();
        const uint16_t* dstSrc = target.Pixels();
        uint16_t* out = screen.Pixels();

        if (alpha >= 15) {
            for (size_t i = 0; i < n; ++i) out[i] = dstSrc[i];
        } else {
            const int inv = 15 - alpha;
            for (size_t i = 0; i < n; ++i) {
                const uint16_t a = dstSrc[i], b = src[i];
                const int r = (((a >> 8) & 0xF) * alpha + ((b >> 8) & 0xF) * inv) / 15;
                const int g = (((a >> 4) & 0xF) * alpha + ((b >> 4) & 0xF) * inv) / 15;
                const int bl = ((a & 0xF) * alpha + (b & 0xF) * inv) / 15;
                out[i] = static_cast<uint16_t>(0xF000u | (r << 8) | (g << 4) | bl);
            }
        }
        host.Flip();
        host.Sleep(kFadeFrameMs);
        if (host.QuitRequested()) return;
    }
}

void RunBootScreens(Host& host, TextureCache& textures, Language lang,
                    const BootLoad& load) {
    // The font is already loaded by the time the loader clears the screen.
    if (load.Fonts) load.Fonts();

    host.Screen().Fill(0xF000u);  // the engine memsets the buffer first
    host.Flip();

    // 1. Nokia logo. No hold of its own -- the string table loading behind it
    //    is what kept it up. See kLogoDwellMs.
    ShowSplash(host, textures, NokiaLogoPath(lang), kLogoDwellMs, load.Strings);
    if (host.QuitRequested()) return;

    // 2. ESRB rating. Everything else loads behind it: the sound bank, the 76
    //    menu textures, the big font, the mission tables. On the device that
    //    was most of the loading time, which is why the remaining splashes
    //    come after it rather than all six running together.
    ShowSplash(host, textures, kEsrbSplash, kSplashHoldMs, load.Rest);
    if (host.QuitRequested()) return;

    // 3-5. Published by N-Gage, RedLynx, and the OFLC rating.
    for (const char* path :
         {PublisherSplashPath(lang), kDeveloperSplash, kRatingSplash}) {
        ShowSplash(host, textures, path, kSplashHoldMs);
        if (host.QuitRequested()) return;
    }

    // 6. The title. No hold in the original either -- the intro cutscene
    //    loading behind it is what kept it up. See kTitleDwellMs.
    //
    //
    // The original also stamps a version line into this image before drawing
    // it -- 0x1008f6c8 reads `game_id` and `build_id` out of the application
    // directory and 0x1007242c draws the first, clipped to 18 characters, at
    // (82, 190). Neither file is present in a retail MMC dump, so the strings
    // come back empty and nothing is drawn; the port leaves it out for that
    // reason rather than having missed it.
    ShowSplash(host, textures, kTitleSplash, kTitleDwellMs, load.Cutscene);
}

}  // namespace bb
