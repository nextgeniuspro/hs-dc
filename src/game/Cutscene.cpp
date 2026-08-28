#include "game/Cutscene.h"

#include "game/ConfigFile.h"
#include "game/FilePack.hpp"
#include "game/Font.h"
#include "game/Strings.h"
#include "game/TextureCache.h"
#include "platform/Host.h"
#include "shim/Log.h"

namespace bb {
namespace {

constexpr int32_t kOne = LapAnimation::kOne;

// Rounded 16.16 -> whole pixels.
int Round16(int32_t v) { return static_cast<int>((int64_t(v) + 0x8000) >> 16); }

// Distance from a sprite's origin to the edge of its drawn rectangle, in
// 16.16. The origin is the texture's centre shifted by the pivot (0x100007e8),
// and because the pivot is stored in source pixels it scales with the sprite.
int32_t OriginOffset(int32_t centre16, int srcSize, int dstSize) {
    if (srcSize <= 0) return 0;
    return static_cast<int32_t>(int64_t(centre16) * dstSize / srcSize);
}

}  // namespace

void Cutscene::Unload() {
    if (m_Loaded.empty()) return;
    // Say what went back. A scene's artwork is the biggest thing the game
    // holds after a battle's, and "released 0 KB" is how you find out that
    // something else is still holding a claim on it.
    const std::size_t before = m_Textures.Bytes();
    for (const std::string& path : m_Loaded) m_Textures.Release(path);
    LogDebug("cutscene: %s released %u KB of art\n", m_Name.c_str(),
             unsigned((before - m_Textures.Bytes()) / 1024));
    m_Loaded.clear();
    m_Images.clear();
    // And the scene's music, which is the larger half of what it was holding:
    // around a megabyte of decoded PCM per cutscene, and the campaign plays
    // thirty-one of them.
    if (m_Sound) m_Sound->UnloadBank(SoundManager::kBankCutscene);
    m_Voice = Mixer::kNoHandle;
}

bool Cutscene::Load(const std::string& name) {
    Unload();  // a second scene through the same player pays for the first
    m_Name = name;
    if (!m_Anim.Load(m_Pack, name)) return false;

    m_Images.reserve(m_Anim.Images().size());
    m_Loaded.reserve(m_Anim.Images().size());
    int missing = 0;
    for (const std::string& path : m_Anim.Images()) {
        const Texture* tex = m_Textures.Load(path);
        if (!tex || !tex->Valid()) ++missing;
        m_Images.push_back(tex);
        // Recorded whether or not it decoded: a failed load still took a
        // claim, and the counts have to balance either way.
        m_Loaded.push_back(path);
    }

    m_Subtitles.Init(m_Textures, m_Font);
    m_Subtitles.Load(m_Pack, m_Strings, name);

    // The scene's own sound bank and cue list. Several scenes have neither.
    m_Cues.clear();
    if (m_Sound) m_Sound->LoadBank(SoundManager::kBankCutscene,
                                 "Data\\anim\\snd\\" + name + ".dat");
    ConfigFile cues;
    if (cues.Load(m_Pack, "Data\\anim\\snd\\" + name + ".snd")) {
        for (const auto& section : cues.Sections()) {
            SoundCue c;
            c.Time = section.GetInt("time");
            c.Sound = section.GetInt("soundId");
            c.Type = section.GetInt("type");
            c.Param1 = section.GetInt("param1");
            c.Param2 = section.GetInt("param2");
            m_Cues.push_back(c);
        }
    }

    LogDebug("cutscene: %s -- %d frames at %d fps, %zu sprites, %zu images "
             "(%d missing), %zu subtitles, %zu sound cues\n",
             name.c_str(), m_Anim.Duration(), m_Anim.Fps(),
             m_Anim.Sprites().size(), m_Anim.Images().size(), missing,
             m_Subtitles.Count(), m_Cues.size());
    Rewind();
    return true;
}

void Cutscene::Rewind() {
    m_Anim.Reset();
    m_Subtitles.Reset();
    m_Fade = Fade{};
    m_NextFx = 0;
    m_NextCue = 0;
    if (m_Sound && m_Voice != Mixer::kNoHandle) m_Sound->Stop(m_Voice);
    m_Voice = Mixer::kNoHandle;
}

// 0x100e6890. Cues are in frame order and consumed one at a time, so a cue
// that a skipped frame passed still fires on the next one.
void Cutscene::FireSounds(int frame) {
    if (!m_Sound) return;
    while (m_NextCue < static_cast<int>(m_Cues.size()) &&
           m_Cues[m_NextCue].Time <= frame) {
        const SoundCue& c = m_Cues[m_NextCue++];
        switch (c.Type) {
            case kCuePlay:
                // param2 carries the volume in the mixer's own 0..256.
                m_Voice = m_Sound->Play(SoundManager::kBankCutscene, c.Sound,
                                      c.Param2);
                break;
            case kCueStop:
                m_Sound->Stop(m_Voice);
                m_Voice = Mixer::kNoHandle;
                break;
            case kCueRamp:
                // The original ramps over param1 frames toward param2; the
                // port sets the target directly, which no shipped cutscene
                // can tell apart -- none of them uses this cue.
                m_Sound->GetMixer().SetVolume(m_Voice, c.Param2);
                break;
            default:
                break;  // 3 is the end-of-list sentinel the loader appends
        }
    }
}

// 0x10002204 builds the effect objects; only kind 0 does anything visible that
// the port needs. Kinds 1 and 2 exist in the binary but no shipped cutscene
// schedules one -- kind 2 is the screen shake, which *is* ported, because the
// fight animation uses the same class (see ScreenShake.h) -- and kind 3 is the
// half-screen hack the port drops.
void Cutscene::FireEffects(int frame) {
    const std::vector<LapAnimation::Fx>& fx = m_Anim.Effects();
    while (m_NextFx < static_cast<int>(fx.size()) && fx[m_NextFx].Time <= frame) {
        const LapAnimation::Fx& f = fx[m_NextFx++];
        if (f.Kind != LapAnimation::kFxFade) continue;
        m_Fade.Active = true;
        m_Fade.Step = f.Param;
        // A negative step fades up from the colour, a positive one down to it.
        m_Fade.Level = f.Param < 0 ? 0 : 15 * kOne;
        m_Fade.Colour = static_cast<uint16_t>(f.Colour & 0xFFFF);
    }
}

// 0x100e6f88. One step per frame; the effect retires when the level walks off
// either end of 0..15.
void Cutscene::ApplyFade(Surface& dst) {
    if (!m_Fade.Active) return;
    m_Fade.Level -= m_Fade.Step;
    if (static_cast<uint32_t>(m_Fade.Level) > 15u * kOne) {
        m_Fade.Active = false;
        return;
    }
    const int a = m_Fade.Level >> 16;
    const int inv = 15 - a;
    const int cr = (m_Fade.Colour >> 8) & 0xF;
    const int cg = (m_Fade.Colour >> 4) & 0xF;
    const int cb = m_Fade.Colour & 0xF;
    uint16_t* p = dst.Pixels();
    const std::size_t n = std::size_t(dst.Width()) * dst.Height();
    for (std::size_t i = 0; i < n; ++i) {
        const uint16_t d = p[i];
        const int r = (((d >> 8) & 0xF) * a + cr * inv) / 15;
        const int g = (((d >> 4) & 0xF) * a + cg * inv) / 15;
        const int b = ((d & 0xF) * a + cb * inv) / 15;
        p[i] = static_cast<uint16_t>(0xF000u | (r << 8) | (g << 4) | b);
    }
}

void Cutscene::RenderFrame(Surface& dst, int frame) {
    m_Anim.Evaluate(frame);
    FireEffects(frame);

    dst.Fill(0xF000u);

    // Artwork stops two pixels above the subtitle panel (0x10002638).
    Surface::Rect clip;
    clip.X0 = 0;
    clip.Y0 = 0;
    clip.X1 = dst.Width();
    clip.Y1 = m_Subtitles.Top() - 2;
    if (clip.Y1 > dst.Height()) clip.Y1 = dst.Height();

    // Back to front: the engine walks the sprite array downwards, so sprite 0
    // is drawn last and ends up on top.
    const std::vector<LapAnimation::Sprite>& sprites = m_Anim.Sprites();
    const std::vector<LapAnimation::SpriteState>& states = m_Anim.States();
    for (int i = static_cast<int>(sprites.size()) - 1; i >= 0; --i) {
        const LapAnimation::SpriteState& st = states[i];
        if (!st.Visible) continue;
        const Texture* tex = m_Images[sprites[i].Image];
        if (!tex) continue;
        // Multi-frame sprites animate themselves: the counter advances once
        // per rendered frame, at whatever rate the sprite carries.
        const int cell = m_Anim.NextCell(std::size_t(i), int(tex->Frames.size()));
        const TcTexture::Image* img = tex->Frame(cell);
        if (!img) img = tex->Frame(0);
        if (!img) continue;

        // A kBasic sprite goes down at its texture's own size, whatever its
        // record or its keyframes say: the plain texture class's setSize and
        // setRotation are both a bare `bx lr` (0x100ffe60, 0x100ffe20), so it
        // can neither scale nor turn. Only kAdvanced and kZoom go through the
        // transformable sampler.
        const bool plain = sprites[i].Kind == LapAnimation::kBasic;
        const int32_t dw = plain ? int32_t(img->Width) << 16 : st.Width;
        const int32_t dh = plain ? int32_t(img->Height) << 16 : st.Height;
        const int w = Round16(dw);
        const int h = Round16(dh);
        if (w <= 0 || h <= 0) continue;

        // Positions name the sprite's origin, and y is measured up from the
        // bottom of the view area, so it flips here and nowhere else.
        const int32_t ox = (int32_t(img->Width) << 15) - sprites[i].PivotX;
        const int32_t oy = (int32_t(img->Height) << 15) + sprites[i].PivotY;
        const int32_t sx = st.X - OriginOffset(ox, img->Width, w);
        const int32_t sy = (int32_t(m_Anim.ViewHeight()) << 16) - st.Y -
                           OriginOffset(oy, img->Height, h);

        const int alpha = st.Alpha >> 20;  // 16.16 of 0..255 -> a 0..15 nibble
        if (plain) {
            dst.BlitScaled(img->Pixels.data(), img->Width, img->Height,
                           Round16(sx), Round16(sy), w, h,
                           sprites[i].Filter == 0, alpha, &clip);
            continue;
        }

        // Position *and* extent stay in 16.16 here: the sampler works off the
        // fractional size, which is what keeps a creeping zoom smooth instead
        // of letting it step one axis at a time.
        // A Zoom sprite is stored linearly and takes the sampler's own inset
        // corners, which is also why it cannot turn: 0x100b2690 overwrites
        // whatever rotation set up. Only the padded Adv class turns, and every
        // Adv sprite in the game does -- that is what it is there for.
        const int turn =
            sprites[i].Kind == LapAnimation::kZoom ? 0 : st.Rotation >> 16;
        const Surface::Affine uv =
            sprites[i].Kind == LapAnimation::kZoom
                ? Surface::StretchLinear(img->Width, img->Height)
                : turn ? Surface::Rotate(img->Width, img->Height, turn)
                       : Surface::Stretch(img->Width, img->Height);
        dst.BlitAffine(img->Pixels.data(), img->Width, img->Height, sx, sy, dw,
                       dh, uv, sprites[i].Filter == 0, alpha, &clip);
    }

    ApplyFade(dst);

    m_Subtitles.Update(frame);
    m_Subtitles.Draw(dst);

    FireSounds(frame);
}

bool Cutscene::Play(Host& host) {
    if (!m_Anim.Valid()) return !host.QuitRequested();
    Rewind();
    host.FlushKeys();

    const int fps = m_Anim.Fps() > 0 ? m_Anim.Fps() : 21;
    const int frameMs = 1000 / fps;
    const uint32_t start = host.TickCount();

    for (int frame = 0; frame < m_Anim.Duration(); ++frame) {
        if (host.QuitRequested()) return false;

        // Any key skips the rest, as the original does on its first keypress.
        for (int k = 0; k < static_cast<int>(Key::kCount); ++k) {
            if (host.KeyPressed(static_cast<Key>(k))) {
                LogDebug("cutscene: %s skipped at frame %d\n", m_Name.c_str(),
                         frame);
                if (m_Sound) {
                    // Cut the music with the picture, rather than letting a
                    // minute of it play on over the menu.
                    m_Sound->Stop(m_Voice);
                    m_Voice = Mixer::kNoHandle;
                    host.AudioFlush();
                }
                return true;
            }
        }

        RenderFrame(host.Screen(), frame);
        host.Flip();
        if (m_Sound) m_Sound->Pump(host);

        // Hold the frame against the clock rather than sleeping a fixed slice,
        // so a slow frame is absorbed instead of stretching the scene.
        const uint32_t due = start + uint32_t(frame + 1) * uint32_t(frameMs);
        const uint32_t now = host.TickCount();
        host.Sleep(now < due ? static_cast<int>(due - now) : 0);
    }
    return !host.QuitRequested();
}

bool PlayCutscene(Host& host, FilePack& pack, TextureCache& textures,
                  const Strings& strings, const Font& font,
                  const std::string& name, SoundManager* sound,
                  const std::string& player) {
    Cutscene scene(pack, textures, strings, font);
    scene.SetSound(sound);
    scene.SetPlayerName(player);
    if (!scene.Load(name)) {
        LogError("cutscene: '%s' unavailable, skipping\n", name.c_str());
        return !host.QuitRequested();
    }
    return scene.Play(host);
}

// 0x100393b0, which also resets the clock through 0x10039388.
void AttractTimer::Arm(Host& host, int seconds, const char* name) {
    m_Seconds = seconds;
    m_Name = name;
    Poke(host);
}

// 0x10039388.
void AttractTimer::Poke(Host& host) { m_LastInput = host.TickCount(); }

// 0x100393e4. The comparison is unsigned in the original too, so a tick
// counter that wraps does not fire the reel early.
bool AttractTimer::Poll(Host& host, FilePack& pack, TextureCache& textures,
                        const Strings& strings, const Font& font,
                        SoundManager* sound) {
    if (!Armed()) return false;
    if (host.TickCount() - m_LastInput < uint32_t(m_Seconds) * 1000u) return false;

    LogDebug("attract: idle for %ds, playing '%s'\n", m_Seconds, m_Name);
    // 0x100393e4 stops the menu theme before the reel and starts it again
    // afterwards -- the reel has a score of its own, and the two would
    // otherwise play over each other.
    if (sound) sound->StopMusic();
    PlayCutscene(host, pack, textures, strings, font, m_Name, sound);
    if (sound) {
        // The reel's own bank is unloaded by now, so the theme's samples are
        // the only ones left and this is the same Play the menu screen would
        // have made.
        sound->StartMusic(SoundManager::kBankMenu,
                          SoundManager::kSoundMenuMusic);
    }
    // The keypress that ended the reel was for the reel, not for the menu
    // underneath it.
    host.FlushKeys();
    Poke(host);
    return true;
}

}  // namespace bb
