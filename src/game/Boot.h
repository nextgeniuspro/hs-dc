// Boot screens — port of the startup loader at 0x1008d478.
//
// Six full-screen images run before the intro cutscene, each drawn into a
// scratch buffer and cross-faded onto the screen over 16 frames of 20 ms
// (FUN_1008d320):
//
//   (font-small loads first, before anything is shown)
//   1. Nokia_logo_XX      localised; the string table loads behind it
//   2. splash_esrb_start  held 1799 ms; everything else loads behind it
//   3. Nokia_splash_XX    localised "published by N-Gage", held 1799 ms
//   4. splash_rl          RedLynx, held 1799 ms
//   5. splash_oflc        the PG rating, held 1799 ms
//   6. splash_hs          the title; the intro cutscene loads behind it
//
// The interleaving matters as much as the order. Only screens 2-5 have timers,
// and each one's hold starts *after* its fade finishes, not when it begins.
// Screens 1 and 6 have no timer at all: what kept them up was the loading that
// happened behind them, so the port passes that work in and stands in for what
// it no longer costs. Running the six back-to-back and loading before or after
// would look wrong even with the same images.
//
// In the original all of this lives inside the MenuStateMachine constructor.
// The port splits it into named functions -- same order, same assets, same
// timings, just not hidden in a constructor.
#pragma once

#include <functional>

namespace bb {

class Host;
class Surface;
class TextureCache;
struct Texture;

// Index into the localised logo table the engine reads from resource 0x18
// (+0x10). The order is the binary's own.
enum class Language : int { kEn = 0, kFr = 1, kIt = 2, kGe = 3, kSp = 4 };

// "Data\loadscreen\Nokia_logo_XX.tc" for `lang`.
const char* NokiaLogoPath(Language lang);

// "Data\loadscreen\Nokia_splash_XX.tc" for `lang` -- the publisher screen.
const char* PublisherSplashPath(Language lang);

// Draw a texture over the whole surface, ignoring alpha. Splash images are
// screen-sized and opaque; anything smaller is centred.
void DrawFullScreen(Surface& dst, const Texture& tex);

// Cross-fade the screen from its current contents to `target` over 16 frames
// of 20 ms, presenting each. Blocks (via Host::Sleep) for ~320 ms.
void CrossFade(Host& host, const Surface& target);

// The loading the sequence interleaves, split at the points the original
// splits it. Each runs while its screen is up.
struct BootLoad {
    std::function<void()> Fonts;     // before anything is shown
    std::function<void()> Strings;   // behind the Nokia logo
    std::function<void()> Rest;      // behind the ESRB screen
    std::function<void()> Cutscene;  // behind the title, which stays up until
                                     // the intro's first frame overwrites it
};

// Run the whole sequence above. Returns early if the host asks to quit.
void RunBootScreens(Host& host, TextureCache& textures, Language lang,
                    const BootLoad& load);

}  // namespace bb
