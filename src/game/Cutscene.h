// Cutscene — the animation player (AnimationPlayer, 0x10001b74 / 0x10001f14).
//
// Every story scene in Blackbeard is this one player fed a different .lap: a
// few painted stills, panned and zoomed by a keyframed camera, with subtitles
// underneath. So there is one player here too, and playing a cutscene is
// `Play(host, "01-Intro")`.
//
// Per frame (0x10002638 and its caller 0x100028b8), in this order:
//
//   1. advance every keyframed object, which also decides what is visible
//   2. fire any effect scheduled for this frame
//   3. draw the visible sprites back-to-front, clipped above the subtitle bar
//   4. step the active effect over the whole screen
//   5. update and draw the subtitle panel -- deliberately after the effect,
//      so a fade to black does not take the text with it
//   6. fire any sound cue scheduled for this frame
//
// Sound comes from two more sidecars: `snd\<name>.dat` is the scene's own
// bank (usually one entry, a music track exactly as long as the scene) and
// `snd\<name>.snd` is the cue timeline. A cue is 12 bytes -- frame, sound
// index, type, and two parameters -- and the types are play (0, with the
// volume in param2), stop (1), and a two-parameter ramp (2). Reversed from
// 0x100e6a44 and 0x100e6890.
//
// Three deliberate divergences from the device, all the same cause -- the
// original is fighting a 104 MHz ARM9 and this machine is not:
//
//   * Effect kind 3 toggles the engine's half-screen update, where alternate
//     scanlines are left over from the previous frame ("OPTIMIZING..." in its
//     own debug log). The port ignores it and always draws every line.
//   * The screen is cleared each frame. The original relies on the artwork
//     covering it, which every shipped cutscene does, but clearing costs
//     nothing here and cannot leave stale pixels behind.
//   * Frame pacing is scheduled against the start time rather than the
//     original's every-fourth-frame drift correction, so a slow frame does not
//     stretch the scene.
#pragma once

#include <string>

#include "game/LapAnimation.h"
#include "game/SoundManager.h"
#include "game/Subtitles.h"

namespace bb {

class FilePack;
class Font;
class Host;
class Strings;
class TextureCache;
struct Texture;

class Cutscene {
public:
    // One entry of `snd\<name>.snd` (0x100e69ac).
    struct SoundCue {
        int Time = 0;     // frame it fires on
        int Sound = 0;    // index into the scene's bank
        int Type = 0;     // 0 play, 1 stop, 2 ramp, 3 end-of-list sentinel
        int Param1 = 0;
        int Param2 = 0;   // the volume, for a play cue
    };

    enum CueType : int { kCuePlay = 0, kCueStop = 1, kCueRamp = 2, kCueEnd = 3 };

    Cutscene(FilePack& pack, TextureCache& textures, const Strings& strings,
             const Font& font)
        : m_Pack(pack), m_Textures(textures), m_Strings(strings), m_Font(font) {}

    // Give the player a sound manager to play its cues through. Optional --
    // without one the scene plays silent.
    void SetSound(SoundManager* sound) { m_Sound = sound; }

    // The commander's name, for the lines whose speaker is string 5113 --
    // literally "[player]". The engine reads it out of resource 0xc1, where
    // the New game screen parked it. Set this before Load(): with no name
    // those lines come up with no plate at all.
    void SetPlayerName(std::string name) {
        m_Subtitles.SetPlayerName(std::move(name));
    }

    const std::vector<SoundCue>& Cues() const { return m_Cues; }

    ~Cutscene() { Unload(); }

    // Read `name`'s .lap, its textures and its subtitles. Returns false if the
    // animation itself is missing; missing subtitles are fine.
    bool Load(const std::string& name);

    // Give the scene's artwork back to the texture cache.
    //
    // This matters more than it looks. A cutscene's images are its own -- 25
    // paintings for the attract reel, 3.3 MB of them -- and nothing else ever
    // asks for them again, so holding them past the last frame is 3.3 MB of a
    // Dreamcast's 16 spent on a scene that has finished. The destructor does
    // this, which covers every scene played through PlayCutscene; the intro is
    // the one that outlives its own playback (RunGame builds it before the
    // boot screens and the state machine runs from inside it), so IntroState
    // says so explicitly.
    void Unload();

    bool Valid() const { return m_Anim.Valid(); }
    const LapAnimation& Animation() const { return m_Anim; }
    Subtitles& SubtitlePanel() { return m_Subtitles; }

    // Play from the start, blocking until it ends, the user presses a key, or
    // the host asks to quit. Returns false only in the last case.
    bool Play(Host& host);

    // Compose one frame into `dst`. Public so tests can render without a host.
    void RenderFrame(Surface& dst, int frame);

    // Put playback back to frame 0.
    void Rewind();

private:
    // The screen-wide fade effect (0x100e6e60 / 0x100e6f88).
    struct Fade {
        bool Active = false;
        int32_t Level = 0;   // 16.16; the whole number is the 0..15 blend step
        int32_t Step = 0;
        uint16_t Colour = 0;
    };

    void FireEffects(int frame);
    void FireSounds(int frame);
    void ApplyFade(Surface& dst);

    FilePack& m_Pack;
    TextureCache& m_Textures;
    const Strings& m_Strings;
    const Font& m_Font;
    SoundManager* m_Sound = nullptr;

    LapAnimation m_Anim;
    Subtitles m_Subtitles;
    std::vector<const Texture*> m_Images;
    // The paths behind `m_Images`, kept so they can be given back one for one.
    // Its own list rather than the animation's, because a second Load replaces
    // that and the previous scene's claims still have to be settled.
    std::vector<std::string> m_Loaded;
    std::vector<SoundCue> m_Cues;
    std::string m_Name;

    Fade m_Fade;
    int m_NextFx = 0;
    int m_NextCue = 0;
    Mixer::Handle m_Voice = Mixer::kNoHandle;
};

// Load and play `name`, then return. Missing cutscenes are skipped rather than
// fatal, so a partial asset set still boots. Returns false if the host quit.
bool PlayCutscene(Host& host, FilePack& pack, TextureCache& textures,
                  const Strings& strings, const Font& font,
                  const std::string& name, SoundManager* sound = nullptr,
                  const std::string& player = {});

// Attract mode — the 32nd cutscene, and the only one that isn't part of the
// story. `Data\anim\Attract.lap` is a 35-second montage of scenes from across
// the campaign with a spyglass iris wipe and no subtitles: a trailer.
//
// Every front-end page arms this timer as it opens (0x100393b0 sets a 30
// second timeout and the literal name "Attract"), and the menu frame loop
// polls it every frame (0x1003aa84 -> 0x100393e4). Any keypress restarts the
// countdown; when it expires the reel plays, and any key during it drops
// straight back to the menu.
class AttractTimer {
public:
    static constexpr int kIdleSeconds = 30;  // 0x1e at 0x100393b0
    static constexpr const char* kAnimation = "Attract";

    // Start the countdown. Call as a page opens.
    void Arm(Host& host, int seconds = kIdleSeconds,
             const char* name = kAnimation);

    // Restart it. Call whenever the user touches a key.
    void Poke(Host& host);

    // Play the reel if the countdown has expired, and restart it. Returns
    // true if it played, in which case the caller should redraw.
    bool Poll(Host& host, FilePack& pack, TextureCache& textures,
              const Strings& strings, const Font& font,
              SoundManager* sound = nullptr);

    // Whether the countdown is running at all.
    bool Armed() const { return m_Seconds > 0 && m_Name != nullptr; }

private:
    uint32_t m_LastInput = 0;
    int m_Seconds = 0;
    const char* m_Name = nullptr;
};

}  // namespace bb
