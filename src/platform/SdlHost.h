// SDL2 implementation of the Host seam.
//
// Owns the window and the presentation texture, converts the game's ARGB4444
// surface to ARGB8888 on Flip(), and pumps the SDL event queue inside Sleep()
// -- which is what makes the game's blocking states cooperate with the desktop
// window, the same way the original's nested active scheduler did on Symbian.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "platform/FileStorage.h"
#include "platform/Framebuffer.h"
#include "platform/Host.h"
#include "platform/SdlFrames.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct _SDL_GameController;

namespace bb {

class SdlHost : public Host {
public:
    ~SdlHost() override;

    // Opens the window. `scale` is the integer zoom of the 176x208 screen,
    // which sets the window's *height*; the window itself is 16:9 and
    // resizable, and the space either side of the screen is the frame's.
    //
    // `width`/`height` override that outright, for a display the default shape
    // suits badly -- and for checking the presentation, since a test can ask
    // for the odd window shapes it would otherwise have to drag one into.
    bool Init(int scale, int width = 0, int height = 0);

    Surface& Screen() override { return m_Screen; }
    void Flip() override;
    void Sleep(int ms) override;
    uint32_t TickCount() override;
    bool QuitRequested() const override { return m_Quit; }
    bool KeyPressed(Key k) override;
    bool KeyHeld(Key k) const override;
    void FlushKeys() override;
    InputDevice ActiveInput() const override { return m_ActiveInput; }
    std::string PollText() override;

    bool AudioOpen(int rate) override;
    void AudioQueue(const int16_t* samples, int count) override;
    int AudioQueued() const override;
    void AudioFlush() override;

    // Saved games land in `dir`, one file per slot. Until this is called the
    // host reports no storage and the game politely refuses to save, which is
    // also what a Dreamcast with no memory card in the port should do.
    void OpenSaves(const std::string& dir) {
        m_Saves = std::make_unique<FileStorage>(dir);
    }
    Storage* Saves() override { return m_Saves.get(); }

    // Device frames are loaded from `dir` -- a frames.txt manifest and a
    // directory of art per frame. Absent is fine and silent: the port then
    // draws plain black beside the screen, which is what a source checkout
    // without the art and the headless smoke tests both get.
    void OpenFrames(const std::string& dir);

    int FrameCount() const override { return static_cast<int>(m_Frames.size()); }
    const char* FrameId(int index) const override;
    const char* FrameLabel(int index) const override;
    const char* Frame() const override;
    void SetFrame(const char* id) override;

    // Present into an offscreen framebuffer only (no window). Used by the
    // headless smoke test and for screenshots.
    const Framebuffer& Presented() const { return m_Present; }

    // Stop after this many Flip()s; -1 (default) means run until quit. Lets a
    // smoke test drive the real game flow without a human.
    void SetFlipLimit(long flips) { m_FlipLimit = flips; }

    // Queue a key press to be injected at the Nth Flip(), for smoke tests
    // that have to get past a screen (BB_KEYS="40:select,80:softleft").
    void ScheduleKey(long flip, Key k) { m_Scheduled.push_back({flip, k}); }

    // Write the last presented frame as a BMP. Returns false on failure.
    bool SaveScreenshot(const char* path) const;

    // The path of a file dragged onto the window since this was last asked,
    // or an empty string. *Taken*, not read: a drop is an action, and asking
    // twice must not perform it twice.
    //
    // The only caller is the import screen (platform/ImportScreen.h) -- the
    // game itself has nothing to do with files the desktop hands it -- which
    // is why this is on SdlHost rather than on the Host seam the game sees.
    std::string TakeDroppedFile();

private:
    std::string m_Text;   // characters typed since the last PollText
    std::string m_Dropped;  // ...and the last file dragged onto the window
    void PumpEvents();
    void ToggleFullscreen();

    // Draw the window: the frame's two halves, then the game field over them.
    // Split out of Flip() so a screenshot can render one without presenting it.
    void Render() const;

    SDL_Window* m_Win = nullptr;
    SDL_Renderer* m_Ren = nullptr;
    SDL_Texture* m_Tex = nullptr;
    std::vector<DeviceFrame> m_Frames;
    int m_Frame = -1;  // index into m_Frames; -1 is none
    Surface m_Screen;
    Framebuffer m_Present;
    std::unique_ptr<FileStorage> m_Saves;
    _SDL_GameController* m_Pad = nullptr;
    InputDevice m_ActiveInput = InputDevice::kKeyboard;
    // The left stick as a d-pad: what direction each axis is currently held
    // to, so crossing the threshold presses once and returning releases.
    int m_StickX = 0, m_StickY = 0;
    // The two analogue triggers, likewise: whether each is currently past the
    // point that counts as pressed.
    bool m_TriggerL = false, m_TriggerR = false;
    bool m_Quit = false;
    bool m_Pressed[static_cast<int>(Key::kCount)] = {};
    bool m_Held[static_cast<int>(Key::kCount)] = {};
    long m_Flips = 0;
    long m_FlipLimit = -1;
    std::vector<std::pair<long, Key>> m_Scheduled;
    uint32_t m_AudioDev = 0;  // SDL_AudioDeviceID; 0 means none opened
};

}  // namespace bb
