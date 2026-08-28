// Device frames — the art drawn either side of the game field.
//
// The game's screen is 176x208 and a desktop window is not, so the leftover
// space is filled with a picture of the machine the game shipped on: the left
// half of an N-Gage on the left, the right half on the right, both scaled to
// the height of the field and cropped by the window.
//
// A frame is two PNGs and a line in a manifest. They are the port's own art
// rather than the game's, so they do not go through data.pak or the .tc
// decoder -- and they must not, because the pak's textures are ARGB4444 and a
// photograph would band badly at four bits a channel. They are loaded at
// window resolution into their own SDL textures instead, which is also why
// this lives beside SdlHost rather than in `bb_platform`: it is the one part
// of the port that needs both SDL and a real image decoder.
#pragma once

#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace bb {

// One frame: the two halves of the device, with the screen cut out between.
struct DeviceFrame {
    std::string ID;     // "ngage-qd" -- what the settings file stores
    std::string Label;  // "N-Gage QD" -- what the settings list shows
    SDL_Texture* Left = nullptr;
    SDL_Texture* Right = nullptr;
    // The source art's size, kept so each half can be drawn at its own aspect.
    int LeftW = 0, LeftH = 0;
    int RightW = 0, RightH = 0;
};

// Load every frame `dir`/frames.txt lists, in the order it lists them. Returns
// what it managed to load: a missing directory, a missing manifest and a frame
// whose art will not decode are all "that frame is not installed", not errors.
// A port with no frames draws plain black beside the screen.
std::vector<DeviceFrame> LoadDeviceFrames(SDL_Renderer* ren,
                                          const std::string& dir);

void FreeDeviceFrames(std::vector<DeviceFrame>& frames);

}  // namespace bb
