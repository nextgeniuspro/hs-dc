// KallistiOS implementation of the Host seam — the Dreamcast backend.
//
// The second backend Host.h was written for. Nothing above this file changes
// to get the game onto a console: the engine still draws into one 176x208
// ARGB4444 Surface and calls Screen/Flip/Sleep/TickCount, exactly as it did
// through `CEpoc32`'s vtable on the N-Gage.
//
// What the hardware makes of that:
//
//   * **Flip** uploads the screen to the PVR as a texture and draws one
//     textured quad with it. ARGB4444 *is* a PVR texture format -- the format
//     RedLynx picked for a Symbian phone in 2005 is the one this GPU wants --
//     so the frame goes across with no conversion at all, and the hardware
//     does the scaling for free.
//
//     Presenting through the PVR rather than writing the framebuffer directly
//     is not a preference. A CPU that writes VRAM and never asks the GPU to
//     draw produces a black screen on any emulator that does not emulate the
//     VRAM framebuffer (Flycast's "Full Framebuffer Emulation", off by
//     default, and slow when on). One textured quad a frame costs less than
//     the 2x software blit it replaced and works everywhere.
//
//     176x208 is portrait and a television is not, so the field is fitted to
//     the full height and centred -- 406x480, its shape kept, which is what
//     ScreenLayout's FitScreen says for a 640x480 window and therefore the
//     same answer the desktop build gets. The space either side is the device
//     frame's, exactly as it is there.
//   * **Sleep** is the load-bearing one, as on Symbian. There it ran a nested
//     active scheduler; here it scans the maple bus and pumps the sound stream,
//     because the game's blocking states call it and nothing else runs.
//   * **Audio** is one 8 kHz mono stream (Mixer::kRate) -- the game mixes
//     everything itself, exactly as it did with one MEDIACLIENTAUDIOSTREAM
//     import. The AICA pulls, the Host seam pushes, so a ring buffer sits
//     between them and Sleep/Flip turn the crank.
//   * **Device frames** -- the picture of the phone drawn either side of the
//     field -- are the desktop build's art, cut for this screen at disc-build
//     time by scripts/mkframes.py and drawn as two more quads. The console
//     does no image decoding: the halves arrive already scaled to the field's
//     height, already cropped to the strip that fits beside it, and already
//     composited over the black they sit on.
//
//   * **Saves** go to the VMU (see VmuStorage.h), which is the memory card
//     Storage.h was shaped around in the first place.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "platform/Host.h"
#include "platform/Surface.h"
#include "platform/VmuStorage.h"

namespace bb {

class KosHost : public Host {
public:
    ~KosHost() override;

    // Sets the video mode and starts the sound system. Returns false only if
    // video could not be brought up, which on this hardware does not happen.
    bool Init();

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

    // Where the mixed samples come from. The host swallows blocks and knows
    // nothing about who makes them, exactly as the memory line knows nothing
    // about textures -- so the entry point hands it a way to ask for more, and
    // that is what lets the host keep the stream running on its own when the
    // game thread is blocked somewhere that cannot ask (see PumpAudio).
    using AudioPump = std::function<void()>;
    void SetAudioPump(AudioPump pump) { m_AudioPump = std::move(pump); }

    // One turn of the audio crank: mix what the game owes, then hand the AICA
    // whatever it has room for. The game loop does this itself every frame --
    // this is for the calls that block it, which on this machine means the
    // memory card. Only ever called from a thread the mixer is not otherwise
    // being used from; VmuStorage's guard is what arranges that.
    void PumpAudio();

    // Saved games on the memory card in the given controller/slot. Absent card
    // is not an error: the host then reports no storage and the game refuses
    // to save politely, which is the behaviour a player with no VMU should get.
    // Re-checking is cheap, so this may be called again after a card is
    // plugged in.
    //
    // With no arguments this takes the first card it finds anywhere, which is
    // what the entry point does at boot so the settings file can be read
    // before anything is drawn. The *game* saves are not left to that: the
    // player picks a bay explicitly (SetSaveBay, driven by game/CardPicker.h)
    // the moment they start or load one.
    void OpenSaves(int port = -1, int unit = -1);
    Storage* Saves() override;

    // What the console's file manager will show for each slot: a name and a
    // 32x32 icon (VmuStorage.h). Set once at startup, from the icon pak; the
    // table is held here rather than in the storage because the storage is
    // thrown away and rebuilt every time the player changes cards, and the
    // labels are the same on all of them.
    void SetSaveLabels(VmuStorage::Labels labels);

    // The four controller ports, each of which may have a card in one of its
    // two expansion slots. A port is the unit the player thinks in -- they
    // know which pad is theirs, not which slot of it a card went into -- so
    // the picker offers ports and this takes the first card it finds in one.
    static constexpr int kSaveBays = 4;

    int SaveBayCount() const override { return kSaveBays; }
    bool SaveBayReady(int bay) const override;
    const char* SaveBayLabel(int bay) const override;
    int SaveBay() const override { return m_SaveBay; }
    bool SetSaveBay(int bay) override;

    // The device frames, from the container scripts/mkframes.py builds.
    // Absent is fine and silent: the sides of the screen then stay black and
    // the settings list loses its Frame row, which is what `FrameCount() == 0`
    // has always meant.
    void OpenFrames(const char* path);

    int FrameCount() const override;
    const char* FrameId(int index) const override;
    const char* FrameLabel(int index) const override;
    const char* Frame() const override;
    void SetFrame(const char* id) override;

    // Frames presented since Init(). Only diagnostics use it.
    uint32_t Flips() const { return m_Flips; }

    // What the game is holding, for the memory line this host prints. The host
    // has no business knowing what a texture or a sound is, so the entry point
    // hands it a way to ask -- bytes of decoded textures and of decoded audio,
    // the two things that grow on a 16 MB machine.
    using MemoryProbe = std::function<void(std::size_t& textures,
                                           std::size_t& sounds)>;
    void SetMemoryProbe(MemoryProbe probe) { m_Probe = std::move(probe); }

private:
    // Read the maple bus: controller buttons into the key edges, keyboard
    // queue into the text buffer. Called from Sleep() and Flip().
    void Poll();
    void PollController();
    void PollKeyboard();
    // Latch one frame's worth of buttons: edges for KeyPressed, level for
    // KeyHeld. `mask` is the game's key bits, not the controller's.
    void Latch(uint32_t mask);

    // Put the selected frame's two halves into texture memory. Called when a
    // frame is chosen, not per draw.
    void UploadFrame();

    // Print what the heap holds, and what of it the game can account for.
    void WatchHeap();

    MemoryProbe m_Probe;
    AudioPump m_AudioPump;

    // Audio callback plumbing. The AICA asks for bytes; the ring holds
    // samples the game has already mixed.
    static void* StreamCallback(int hnd, int smpReq, int* smpRecv);
    void* FillStream(int bytesReq, int* bytesRecv);

    // Point the saves at one card, by its maple address. Takes a
    // `maple_device_t*` as a void pointer so this header stays free of KOS
    // includes, exactly as `m_Texture` does.
    void BindCard(void* device);

    Surface m_Screen;
    VmuStorage::Labels m_SaveLabels;
    std::unique_ptr<VmuStorage> m_Saves;
    int m_SaveBay = -1;  // the controller port `m_Saves` is on; -1 is none

    // The screen, in texture memory: a 256x256 ARGB4444 texture with the
    // 176x208 frame in its top-left corner. `pvr_ptr_t` is a void pointer, and
    // keeping it as one is what lets this header stay free of KOS includes.
    void* m_Texture = nullptr;

    // One device frame as the container describes it: two strips of RGB565,
    // each already the height of the field, each padded out to a power-of-two
    // width the PVR can hold. The pixels are *not* kept here -- they go from
    // the disc into texture memory and the copy in RAM is dropped. Three
    // frames would be 720 KB of a 16 MB machine to hold two of them that are
    // already in VRAM, and the only thing that reads them again is a player
    // changing frames in the settings screen.
    struct DeviceFrame {
        std::string ID;
        std::string Label;
        int LeftW = 0, RightW = 0;  // what is drawn
        int TexW = 0;                // what is stored
        uint32_t LeftOff = 0, RightOff = 0;  // where, in the container
    };
    std::string m_FramesPath;
    std::vector<DeviceFrame> m_Frames;
    int m_Frame = -1;                        // index into m_Frames; -1 is none
    void* m_FrameTex[2] = {nullptr, nullptr};  // left and right, in VRAM
    int m_FrameTexW = 0;                   // what those two were allocated for

    bool m_Quit = false;
    uint32_t m_Flips = 0;
    InputDevice m_ActiveInput = InputDevice::kGamepad;

    uint32_t m_Held = 0;     // game key bits currently down
    uint32_t m_Pressed = 0;  // game key bits that went down since last read
    std::string m_Text;      // keyboard characters, for the name field

    // The mixed stream, waiting for the AICA. Sized for a second at 8 kHz --
    // far more than the ~128 ms SoundManager keeps ahead, so a long frame
    // never starves the stream.
    static constexpr int kRingSamples = 8192;
    std::vector<int16_t> m_Ring;
    int m_RingHead = 0;  // next sample to play
    int m_RingTail = 0;  // next free slot
    int m_RingCount = 0;
    // What the callback hands the AICA: a contiguous run copied out of the
    // ring, padded with silence when the game is behind.
    std::vector<int16_t> m_Stage;
    int m_Stream = -1;  // snd_stream_hnd_t; -1 is none
};

}  // namespace bb
