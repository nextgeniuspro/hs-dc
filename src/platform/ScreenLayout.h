// Where the game's screen lands in a host window, and where the device frame
// goes beside it.
//
// The game is 176x208 -- an 11:13 portrait shape no desktop display has -- so
// a window is never the screen's own size and something has to decide how the
// two relate. That decision is this file, kept as plain arithmetic on ints so
// it lives in `bb_platform` with the rest of the SDL-free code and the tests
// can reach it: getting it wrong stretches the whole game by a few percent,
// which is the kind of thing nobody sees and everybody feels.
#pragma once

#include "platform/Surface.h"

namespace bb {

struct ScreenRect {
    int X = 0, Y = 0, W = 0, H = 0;
};

// Fit the game's screen inside a `windowW` x `windowH` window and centre it.
//
// Fitted, never filled: the screen keeps its shape and is never cropped, so on
// a window wider than the screen -- which is every realistic one, the game
// being narrower than anything it will be shown on -- this comes out as the
// full window height, and the space left either side is the frame's.
constexpr ScreenRect FitScreen(int windowW, int windowH) {
    if (windowW <= 0 || windowH <= 0) return ScreenRect{};
    int w, h;
    if (windowW * Surface::kHeight >= windowH * Surface::kWidth) {
        h = windowH;
        w = windowH * Surface::kWidth / Surface::kHeight;
    } else {
        w = windowW;
        h = windowW * Surface::kHeight / Surface::kWidth;
    }
    return ScreenRect{(windowW - w) / 2, (windowH - h) / 2, w, h};
}

// How wide a frame half of `artW` x `artH` comes out when drawn at the
// screen's height, which is the height both halves are always drawn at.
constexpr int FrameHalfWidth(int screenH, int artW, int artH) {
    if (artH <= 0) return 0;
    return screenH * artW / artH;
}

}  // namespace bb
