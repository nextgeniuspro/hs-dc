// TextureCache — port of the engine's texture manager.
//
// The original (created at 0x100b56e4, resource slot 0x21) loads a .tc, keeps
// it, and registers it in a numbered resource slot via FUN_1008d1d8. Draw code
// then asks for a slot rather than a path. Same idea here, minus the manual
// reference counting: decoded frames are owned by the cache and handed out by
// const pointer.
//
// Decoding happens up front rather than at draw time. The original does it both
// ways (Basic textures expand during the blit, Adv/Zoom at load), but since both
// decoders are already validated to produce identical pixels, eagerly expanding
// is simpler.
//
// **The reference counting is back.** It was left out while the port was
// desktop-only, where holding every texture ever decoded costs nothing anyone
// notices. A Dreamcast has 16 MB for everything, and the arithmetic there is
// unforgiving: the intro cutscene's artwork is 1.1 MB, the attract reel's is
// 3.3 MB, a battle's own is 3 MB, and the sound banks are another 3.7 MB. Keep
// all of it and the first battle after the attract reel dies on an
// out-of-memory abort -- which is exactly what happened.
//
// So a texture is counted while it is wanted: every Load hands out one claim
// and Release gives it back, with the last one out freeing the pixels. What
// the startup loader brings up -- the menu set, the fonts, the water, the
// things the engine parks in resource slots for the whole run -- is marked as
// the base set and never freed, so only scene-scoped art is ever in question.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "game/TcTexture.h"

namespace bb {

class FilePack;
class Palette;

struct Texture {
    std::string Path;
    int Width = 0;
    int Height = 0;
    // Decoded ARGB4444 frames, palette already resolved where applicable.
    // Frames the decoder can't handle are left empty rather than filled with
    // guesses, so `Complete` says whether anything was dropped.
    std::vector<TcTexture::Image> Frames;
    bool Complete = false;
    // Whether the source pixels were palette indices. When the texture was
    // loaded through LoadIndexed they still are.
    bool Indexed = false;

    // A water sprite (see TcTexture.h) is two layers: `Frames` is the land,
    // which is drawn straight, and `SurfFrames` the shallow water round it, which
    // the travel map blends and the ripple field shoves sideways per row.
    // Empty for everything else.
    std::vector<TcTexture::Image> SurfFrames;
    bool Water = false;

    bool Valid() const { return !Frames.empty() && !Frames[0].Pixels.empty(); }
    const TcTexture::Image* Frame(int i) const {
        if (i < 0 || i >= static_cast<int>(Frames.size())) return nullptr;
        return Frames[i].Pixels.empty() ? nullptr : &Frames[i];
    }
    const TcTexture::Image* Surf(int i) const {
        if (i < 0 || i >= static_cast<int>(SurfFrames.size())) return nullptr;
        return SurfFrames[i].Pixels.empty() ? nullptr : &SurfFrames[i];
    }
};

class TextureCache {
public:
    TextureCache(FilePack& pack, const Palette& palette)
        : m_Pack(pack), m_Palette(palette) {}

    // Load (or return an already-loaded) texture by game path. Returns nullptr
    // if the asset is missing or its header is unsupported.
    const Texture* Load(const std::string& path);

    // The same, but leaving palette indices in place instead of resolving
    // them. The battlefield needs this: buildings and unit sprites are indexed
    // and the engine recolours them per player by swapping sixteen palette
    // entries at draw time (0x1005a164), which is only possible while the
    // pixels are still indices. Cached separately from the resolved copy.
    const Texture* LoadIndexed(const std::string& path);

    // Load `path` and register it in engine resource slot `id`.
    const Texture* Register(uint16_t id, const std::string& path);

    // The texture in a resource slot, or nullptr.
    const Texture* Slot(uint16_t id) const;

    // Load every texture the startup loader registers (0x1008d478): the
    // kMenuTextures table, plus the menu background, which the original takes
    // through a different call before storing it in slot 0x32. Returns how many
    // decoded completely; `attempted` receives how many were tried.
    int LoadStartupTextures(int* attempted = nullptr);

    // Everything loaded up to here is the base set: never freed, however often
    // it is released. Called once, when the startup loader has finished.
    void MarkBase();

    // Give back one claim from Load/LoadIndexed. The pixels go when the last
    // claim does. A path that was never loaded, or is in the base set, is
    // quietly ignored -- releasing more than you took is not an error a screen
    // should have to reason about.
    void Release(const std::string& path, bool indexed = false);

    std::size_t Count() const { return m_ByPath.size(); }

    // Bytes the decoded frames are holding. What a 16 MB machine wants to know.
    std::size_t Bytes() const;

private:
    struct Entry {
        Texture Tex;
        int Claims = 0;    // outstanding Loads
        bool Base = false;  // the startup set: kept whatever the count says
    };

    const Texture* LoadInternal(const std::string& path, bool keepIndices);

    FilePack& m_Pack;
    const Palette& m_Palette;
    std::map<std::string, Entry> m_ByPath;
    // Paths that already failed, so a miss is reported once and not per frame.
    std::set<std::string> m_Missing;
    std::map<uint16_t, const Texture*> m_Slots;
};

// The textures one screen asked for, handed back when that screen goes.
//
// A scene loads a couple of dozen and finishes with all of them at once. Doing
// that by hand means keeping a list in step with the loads, which is the kind
// of bookkeeping that rots the first time someone adds a sprite. This *is* the
// list: load through it and it releases exactly what it took, including the
// loads that failed -- a miss still takes a claim, and the counts have to
// balance either way.
class TextureSet {
public:
    explicit TextureSet(TextureCache& cache) : m_Cache(&cache) {}
    ~TextureSet() { ReleaseAll(); }

    TextureSet(const TextureSet&) = delete;
    TextureSet& operator=(const TextureSet&) = delete;

    const Texture* Load(const std::string& path) {
        m_Claims.emplace_back(path, false);
        return m_Cache->Load(path);
    }

    const Texture* LoadIndexed(const std::string& path) {
        m_Claims.emplace_back(path, true);
        return m_Cache->LoadIndexed(path);
    }

    // Load and register in an engine resource slot. The slot is dropped with
    // the texture, so a screen that has gone cannot leave a slot pointing at
    // freed pixels.
    const Texture* Register(uint16_t id, const std::string& path) {
        m_Claims.emplace_back(path, false);
        return m_Cache->Register(id, path);
    }

    // Give everything back now rather than at destruction -- for a screen that
    // outlives its own artwork, like the chart while a battle is being fought
    // on top of it.
    void ReleaseAll() {
        for (const auto& [path, indexed] : m_Claims)
            m_Cache->Release(path, indexed);
        m_Claims.clear();
    }

    bool Empty() const { return m_Claims.empty(); }

private:
    TextureCache* m_Cache;
    std::vector<std::pair<std::string, bool>> m_Claims;
};

}  // namespace bb
