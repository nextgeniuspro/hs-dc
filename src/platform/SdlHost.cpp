#include "platform/SdlHost.h"

#include <SDL.h>

#include "platform/ScreenLayout.h"
#include "shim/Log.h"

namespace bb {
namespace {

// Keyboard -> N-Gage keypad. The device has a numeric keypad the game reads as
// both digits and a d-pad, plus two soft keys and C.
struct KeyBinding {
    SDL_Scancode Sc;
    Key Mapped;
};
constexpr KeyBinding kBindings[] = {
    {SDL_SCANCODE_UP, Key::kUp},         {SDL_SCANCODE_KP_8, Key::kUp},
    {SDL_SCANCODE_DOWN, Key::kDown},     {SDL_SCANCODE_KP_2, Key::kDown},
    {SDL_SCANCODE_LEFT, Key::kLeft},     {SDL_SCANCODE_KP_4, Key::kLeft},
    {SDL_SCANCODE_RIGHT, Key::kRight},   {SDL_SCANCODE_KP_6, Key::kRight},
    {SDL_SCANCODE_SPACE, Key::kSelect},  {SDL_SCANCODE_KP_5, Key::kSelect},
    // Enter is the left soft key (the device's 7): it opens the in-game menu
    // and the travel chart, and confirms an Ok/Cancel board. Space confirms.
    {SDL_SCANCODE_RETURN, Key::kSoftLeft},
    {SDL_SCANCODE_KP_ENTER, Key::kSoftLeft},
    {SDL_SCANCODE_Z, Key::kSoftLeft},
    // Escape is Back. Backspace is deliberately *not* bound: it rubs out in
    // the commander-name field, and a key cannot do both.
    {SDL_SCANCODE_ESCAPE, Key::kBack},
    // The five battlefield keys the phone had digits for. There is no keypad
    // to press 6 or 4 on, so they go where a keyboard player's hands already
    // are: Tab for the cell board, Q and E to walk between your own units,
    // either Shift held for a unit's reach, M held for the overview.
    {SDL_SCANCODE_TAB, Key::kInfo},
    {SDL_SCANCODE_Q, Key::kPrevUnit},    {SDL_SCANCODE_E, Key::kNextUnit},
    {SDL_SCANCODE_LSHIFT, Key::kRange},  {SDL_SCANCODE_RSHIFT, Key::kRange},
    {SDL_SCANCODE_M, Key::kMap},
};

// Pad -> N-Gage keypad. A confirms like 5, B cancels like C, Start and the
// left bumper stand in for the left soft key (the in-game menu).
//
// **The right soft key is not bound at all**, on either the keyboard or the
// pad. It is the phone's `#`, and off the phone it is dead weight: it only
// ever cancels, and every screen in the game that reads it reads Back in the
// same breath -- so Escape and B already do everything it did. The buttons it
// was holding are wanted for the battlefield keys instead.
struct PadBinding {
    SDL_GameControllerButton Button;
    Key Mapped;
};
constexpr PadBinding kPadBindings[] = {
    {SDL_CONTROLLER_BUTTON_DPAD_UP, Key::kUp},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, Key::kDown},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, Key::kLeft},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Key::kRight},
    {SDL_CONTROLLER_BUTTON_A, Key::kSelect},
    {SDL_CONTROLLER_BUTTON_B, Key::kBack},
    {SDL_CONTROLLER_BUTTON_START, Key::kSoftLeft},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Key::kSoftLeft},
    // The battlefield keys: Y is the cell board, X is held for a unit's
    // reach, the right bumper is held for the overview.
    {SDL_CONTROLLER_BUTTON_Y, Key::kInfo},
    {SDL_CONTROLLER_BUTTON_X, Key::kRange},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Key::kMap},
};

constexpr int kStickThreshold = 16000;
// The triggers walk between your own units. They are axes, not buttons, so
// they get the same crossing-is-a-press treatment as the stick -- at a third
// of the way down, which is past any resting slop and short of a firm pull.
constexpr int kTriggerThreshold = 10000;

// The window opens 16:9 -- the shape of the screen it will sit on, rather than
// the shape of the phone. It is resizable, so this is only where it starts.
constexpr int kWindowAspectW = 16;
constexpr int kWindowAspectH = 9;

SDL_Rect ToSdl(const ScreenRect& r) { return SDL_Rect{r.X, r.Y, r.W, r.H}; }

}  // namespace

SdlHost::~SdlHost() {
    if (m_Pad) SDL_GameControllerClose(m_Pad);
    if (m_AudioDev) SDL_CloseAudioDevice(m_AudioDev);
    FreeDeviceFrames(m_Frames);
    if (m_Tex) SDL_DestroyTexture(m_Tex);
    if (m_Ren) SDL_DestroyRenderer(m_Ren);
    if (m_Win) SDL_DestroyWindow(m_Win);
    SDL_Quit();
}

bool SdlHost::Init(int scale, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        LogError("SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    // `scale` sets the height, as it always did; the width is whatever 16:9
    // makes of it, and the frame fills the difference.
    if (height <= 0) height = Surface::kHeight * scale;
    if (width <= 0) width = height * kWindowAspectW / kWindowAspectH;
    m_Win = SDL_CreateWindow("Blackbeard (port)", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, width, height,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!m_Win) {
        LogError("SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    m_Ren = SDL_CreateRenderer(m_Win, -1, SDL_RENDERER_ACCELERATED);
    if (!m_Ren) m_Ren = SDL_CreateRenderer(m_Win, -1, SDL_RENDERER_SOFTWARE);
    if (!m_Ren) {
        LogError("SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    m_Tex = SDL_CreateTexture(m_Ren, SDL_PIXELFORMAT_ARGB8888,
                             SDL_TEXTUREACCESS_STREAMING, Surface::kWidth,
                             Surface::kHeight);
    return m_Tex != nullptr;
}

void SdlHost::OpenFrames(const std::string& dir) {
    if (!m_Ren) return;
    FreeDeviceFrames(m_Frames);
    m_Frames = LoadDeviceFrames(m_Ren, dir);
    m_Frame = -1;
}

const char* SdlHost::FrameId(int index) const {
    if (index < 0 || index >= FrameCount()) return "";
    return m_Frames[static_cast<std::size_t>(index)].ID.c_str();
}

const char* SdlHost::FrameLabel(int index) const {
    if (index < 0 || index >= FrameCount()) return "";
    return m_Frames[static_cast<std::size_t>(index)].Label.c_str();
}

const char* SdlHost::Frame() const { return FrameId(m_Frame); }

void SdlHost::SetFrame(const char* id) {
    m_Frame = -1;
    if (!id || !*id) return;
    // An id we do not have is a frame that is not installed, not an error: the
    // settings file keeps naming it, so putting the art back restores it.
    for (std::size_t i = 0; i < m_Frames.size(); ++i) {
        if (m_Frames[i].ID != id) continue;
        m_Frame = static_cast<int>(i);
        return;
    }
}

// The whole window, drawn back to front. The output size is asked for every
// frame rather than tracked through SDL_WINDOWEVENT, which costs nothing and
// means a resize, a display change and a move between a Retina screen and an
// ordinary one all just work.
void SdlHost::Render() const {
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(m_Ren, &ow, &oh);
    const SDL_Rect field = ToSdl(FitScreen(ow, oh));

    SDL_SetRenderDrawColor(m_Ren, 0, 0, 0, 255);
    SDL_RenderClear(m_Ren);
    if (m_Frame >= 0) {
        const DeviceFrame& f = m_Frames[static_cast<std::size_t>(m_Frame)];
        // Each half is drawn at the field's height with its own aspect kept,
        // butted against the field's edge. Both run off the side of the
        // window, and SDL clips them to it -- that overhang is the point:
        // you are looking at the middle of a device too big for the screen.
        const int lw = FrameHalfWidth(field.h, f.LeftW, f.LeftH);
        const int rw = FrameHalfWidth(field.h, f.RightW, f.RightH);
        const SDL_Rect left{field.x - lw, field.y, lw, field.h};
        const SDL_Rect right{field.x + field.w, field.y, rw, field.h};
        SDL_RenderCopy(m_Ren, f.Left, nullptr, &left);
        SDL_RenderCopy(m_Ren, f.Right, nullptr, &right);
    }
    // The game goes last, so it wins any overlap a frame's art might have.
    SDL_RenderCopy(m_Ren, m_Tex, nullptr, &field);
}

void SdlHost::Flip() {
    m_Present.CopyFrom(m_Screen);
    if (m_Tex && m_Ren) {
        SDL_UpdateTexture(m_Tex, nullptr, m_Present.Pixels(),
                          Framebuffer::kWidth * sizeof(uint32_t));
        Render();
        SDL_RenderPresent(m_Ren);
    }
    ++m_Flips;
    if (m_FlipLimit >= 0 && m_Flips >= m_FlipLimit) m_Quit = true;
    for (const auto& [flip, key] : m_Scheduled)
        if (flip == m_Flips) m_Pressed[static_cast<int>(key)] = true;
    PumpEvents();
}

void SdlHost::Sleep(int ms) {
    // Yield-and-pump, in slices, so a long sleep still sees quit and keys.
    // The original's sleep() runs a nested active scheduler for exactly this
    // reason -- blocking states must stay responsive.
    const uint32_t end = SDL_GetTicks() + static_cast<uint32_t>(ms < 0 ? 0 : ms);
    do {
        PumpEvents();
        if (m_Quit) return;
        const int32_t left = static_cast<int32_t>(end - SDL_GetTicks());
        if (left <= 0) break;
        SDL_Delay(static_cast<uint32_t>(left > 5 ? 5 : left));
    } while (static_cast<int32_t>(end - SDL_GetTicks()) > 0);
}

uint32_t SdlHost::TickCount() { return SDL_GetTicks(); }

void SdlHost::PumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                m_Quit = true;
                break;
            case SDL_KEYDOWN:
                m_ActiveInput = InputDevice::kKeyboard;
                // Fullscreen belongs to the window, not to the game. Swallow
                // the keys before the bindings below see them: Enter is the
                // left soft key, and Alt+Enter must not also open the in-game
                // menu on the way past.
                if (e.key.keysym.scancode == SDL_SCANCODE_F11 ||
                    ((e.key.keysym.scancode == SDL_SCANCODE_RETURN ||
                      e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) &&
                     (e.key.keysym.mod & KMOD_ALT))) {
                    if (!e.key.repeat) ToggleFullscreen();
                    break;
                }
                // Backspace rubs out in the commander-name field. SDL sends it
                // as a key rather than as text, and unlike the other keys it
                // repeats, so holding it keeps deleting.
                if (e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE)
                    m_Text += '\b';
                if (e.key.repeat) break;
                for (const auto& b : kBindings) {
                    if (e.key.keysym.scancode != b.Sc) continue;
                    m_Pressed[static_cast<int>(b.Mapped)] = true;
                    m_Held[static_cast<int>(b.Mapped)] = true;
                }
                break;
            case SDL_KEYUP:
                for (const auto& b : kBindings)
                    if (e.key.keysym.scancode == b.Sc)
                        m_Held[static_cast<int>(b.Mapped)] = false;
                break;
            case SDL_TEXTINPUT:
                m_Text += e.text.text;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!m_Pad) m_Pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (m_Pad && e.cdevice.which ==
                                SDL_JoystickInstanceID(
                                    SDL_GameControllerGetJoystick(m_Pad))) {
                    SDL_GameControllerClose(m_Pad);
                    m_Pad = nullptr;
                    m_ActiveInput = InputDevice::kKeyboard;
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                m_ActiveInput = InputDevice::kGamepad;
                for (const auto& b : kPadBindings) {
                    if (e.cbutton.button != b.Button) continue;
                    m_Pressed[static_cast<int>(b.Mapped)] = true;
                    m_Held[static_cast<int>(b.Mapped)] = true;
                }
                break;
            case SDL_CONTROLLERBUTTONUP:
                for (const auto& b : kPadBindings)
                    if (e.cbutton.button == b.Button)
                        m_Held[static_cast<int>(b.Mapped)] = false;
                break;
            case SDL_CONTROLLERAXISMOTION: {
                // The left stick acts as the d-pad: crossing the threshold is
                // one press, returning to centre is the release.
                const auto axis = e.caxis.axis;
                if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                    axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    const bool down = e.caxis.value >= kTriggerThreshold;
                    const bool left = axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
                    bool& state = left ? m_TriggerL : m_TriggerR;
                    if (down == state) break;
                    state = down;
                    const Key k = left ? Key::kPrevUnit : Key::kNextUnit;
                    m_Held[static_cast<int>(k)] = down;
                    if (down) {
                        m_ActiveInput = InputDevice::kGamepad;
                        m_Pressed[static_cast<int>(k)] = true;
                    }
                    break;
                }
                if (axis != SDL_CONTROLLER_AXIS_LEFTX &&
                    axis != SDL_CONTROLLER_AXIS_LEFTY)
                    break;
                const int v = e.caxis.value;
                const int dir = v <= -kStickThreshold ? -1
                                : v >= kStickThreshold ? 1
                                                       : 0;
                int& state = axis == SDL_CONTROLLER_AXIS_LEFTX ? m_StickX
                                                               : m_StickY;
                if (dir == state) break;
                const Key neg = axis == SDL_CONTROLLER_AXIS_LEFTX ? Key::kLeft
                                                                  : Key::kUp;
                const Key pos = axis == SDL_CONTROLLER_AXIS_LEFTX
                                    ? Key::kRight
                                    : Key::kDown;
                if (state != 0)
                    m_Held[static_cast<int>(state < 0 ? neg : pos)] = false;
                if (dir != 0) {
                    m_ActiveInput = InputDevice::kGamepad;
                    const Key k = dir < 0 ? neg : pos;
                    m_Pressed[static_cast<int>(k)] = true;
                    m_Held[static_cast<int>(k)] = true;
                }
                state = dir;
                break;
            }
            default:
                break;
        }
    }
}

void SdlHost::ToggleFullscreen() {
    if (!m_Win) return;
    const bool on =
        (SDL_GetWindowFlags(m_Win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    // Desktop fullscreen rather than a mode switch: the window keeps whatever
    // shape the display has and Render() fits the field to it either way.
    if (SDL_SetWindowFullscreen(m_Win, on ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP))
        LogError("SDL_SetWindowFullscreen: %s\n", SDL_GetError());
}

bool SdlHost::KeyPressed(Key k) {
    const int i = static_cast<int>(k);
    const bool was = m_Pressed[i];
    m_Pressed[i] = false;
    return was;
}

bool SdlHost::KeyHeld(Key k) const { return m_Held[static_cast<int>(k)]; }

void SdlHost::FlushKeys() {
    for (auto& p : m_Pressed) p = false;
    m_Text.clear();
}

std::string SdlHost::PollText() {
    std::string out;
    out.swap(m_Text);
    return out;
}

// The engine pushes finished blocks at the OS rather than being called back
// for them, so SDL_QueueAudio is the direct equivalent: no callback thread,
// no locking, and the same "how much is still buffered" question to answer.
bool SdlHost::AudioOpen(int rate) {
    if (m_AudioDev) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        LogError("SDL_InitSubSystem(AUDIO): %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want{};
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;  // the engine's own block
    want.callback = nullptr;
    SDL_AudioSpec got{};
    m_AudioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (!m_AudioDev) {
        LogError("SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(m_AudioDev, 0);
    return true;
}

void SdlHost::AudioQueue(const int16_t* samples, int count) {
    if (!m_AudioDev || !samples || count <= 0) return;
    SDL_QueueAudio(m_AudioDev, samples, Uint32(count) * sizeof(int16_t));
}

int SdlHost::AudioQueued() const {
    if (!m_AudioDev) return 0;
    return int(SDL_GetQueuedAudioSize(m_AudioDev) / sizeof(int16_t));
}

void SdlHost::AudioFlush() {
    if (m_AudioDev) SDL_ClearQueuedAudio(m_AudioDev);
}

bool SdlHost::SaveScreenshot(const char* path) const {
    // With a window, capture what the window shows -- frame and all, at window
    // resolution -- rather than the bare 176x208 buffer, so a screenshot is
    // worth something for checking the presentation. The frame is re-rendered
    // and read back *before* presenting: reading the back buffer after
    // SDL_RenderPresent is undefined on several backends.
    if (m_Ren && m_Tex) {
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(m_Ren, &ow, &oh);
        if (ow > 0 && oh > 0) {
            SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(
                0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
            if (shot) {
                Render();
                const bool read =
                    SDL_RenderReadPixels(m_Ren, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                         shot->pixels, shot->pitch) == 0;
                const bool ok = read && SDL_SaveBMP(shot, path) == 0;
                SDL_FreeSurface(shot);
                if (ok) return true;
                // Fall through to the framebuffer below: a backend that will
                // not read back should still produce a picture.
            }
        }
    }
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t*>(m_Present.Pixels()), Framebuffer::kWidth,
        Framebuffer::kHeight, 32, Framebuffer::kWidth * sizeof(uint32_t),
        SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    const bool ok = SDL_SaveBMP(s, path) == 0;
    SDL_FreeSurface(s);
    return ok;
}

}  // namespace bb
