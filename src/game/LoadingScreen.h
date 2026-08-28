// LoadingScreen — the board that covers a level load (0x100865e4 / 0x10086604).
//
// It is a System service in the original: the engine object holds one of these
// at +0x43c, `showLoading` builds it (0x1006876c) and `stepLoading` advances it
// (0x100687c0), so any code that is about to block can leave a progress bar up.
//
// The picture is `Data\loadscreen\loader.tc` -- a full 176x208 image with
// **27 frames**, one per step of the bar -- drawn over `Data\Menu\fullboard.tc`
// with string 62150 "Loading..." centred in the big font at y = 89. Progress
// comes in as a percentage and is divided by 3.846 (0x100867d8) to pick the
// frame, and the original *animates through* every frame it skipped rather
// than jumping, sleeping 10 ms between each, which is why a fast load still
// looks like a load.
#pragma once

#include <cstdint>

namespace bb {

struct GameContext;
struct Texture;

class LoadingScreen {
public:
    static constexpr int kStringId = 62150;   // "Loading..."
    static constexpr int kTextY = 0x59;       // 89
    static constexpr int kFrames = 27;
    // 100 / 26: the divisor the original stores as a float.
    static constexpr float kPercentPerFrame = 3.846153736114502f;
    static constexpr int kStepMs = 10;

    explicit LoadingScreen(GameContext& ctx);

    // Advance to `percent` (0..100), drawing every frame in between.
    void Step(int percent);

    // Run the whole bar from where it is to 100.
    void Finish() { Step(100); }

    int Frame() const { return m_Frame; }

private:
    void DrawFrame(int frame);

    GameContext& m_Ctx;
    const Texture* m_Loader = nullptr;
    const Texture* m_Board = nullptr;
    int m_Frame = 0;
};

}  // namespace bb
