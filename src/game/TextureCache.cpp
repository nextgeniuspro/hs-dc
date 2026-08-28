#include "game/TextureCache.h"

#include <iterator>

#include "game/FilePack.hpp"
#include "game/Palette.h"
#include "game/PaletteFlags.h"
#include "game/ResourceTable.h"
#include "shim/Log.h"

namespace bb {

const Texture* TextureCache::Load(const std::string& path) {
    return LoadInternal(path, false);
}

const Texture* TextureCache::LoadIndexed(const std::string& path) {
    return LoadInternal(path, true);
}

const Texture* TextureCache::LoadInternal(const std::string& path,
                                          bool keepIndices) {
    // Indexed copies live under a key that cannot collide with a real path.
    const std::string key = keepIndices ? "#idx#" + path : path;
    if (auto it = m_ByPath.find(key); it != m_ByPath.end()) {
        ++it->second.Claims;
        return &it->second.Tex;
    }

    // A known miss stays a miss: draw code asks for dangling world.xml
    // references every frame, and the first failure already said everything
    // there is to say.
    if (m_Missing.count(key)) return nullptr;

    auto stream = m_Pack.Open(path);
    if (!stream) {
        LogError("texture: '%s' not in pak\n", path.c_str());
        m_Missing.insert(key);
        return nullptr;
    }
    TcTexture tc;
    if (!tc.Parse(*stream)) {
        LogError("texture: '%s' has an unsupported header\n", path.c_str());
        m_Missing.insert(key);
        return nullptr;
    }

    // Runtime truth first; the pixel-value heuristic only covers assets the
    // probe never saw load.
    const std::optional<bool> observed = RuntimePaletted(path);

    Texture tex;
    tex.Path = path;
    tex.Width = tc.Width();
    tex.Height = tc.Height();
    tex.Frames.resize(tc.FrameCount());
    tex.Complete = true;
    // A water sprite keeps its surf apart from its land, because the two are
    // drawn differently -- see TcTexture.h.
    tex.Water = tc.FrameCount() > 0 && tc.IsWaterFrame(0);
    if (tex.Water) tex.SurfFrames.resize(tc.FrameCount());
    for (uint16_t i = 0; i < tc.FrameCount(); ++i) {
        TcTexture::Image* surf = tex.Water ? &tex.SurfFrames[i] : nullptr;
        if (tc.Decode(i, tex.Frames[i], surf) != TcTexture::DecodeStatus::kOk) {
            // A stream we cannot parse at all: leave the frame empty, because
            // a wrong image is worse than a missing one.
            tex.Frames[i] = TcTexture::Image{};
            if (surf) *surf = TcTexture::Image{};
            tex.Complete = false;
            continue;
        }
        const bool indexed =
            observed ? *observed : m_Palette.LooksPaletted(tex.Frames[i]);
        tex.Indexed = indexed;
        if (indexed && !keepIndices) m_Palette.Resolve(tex.Frames[i]);
    }

    auto [it, _] = m_ByPath.emplace(key, Entry{std::move(tex), 1, false});
    return &it->second.Tex;
}

void TextureCache::MarkBase() {
    for (auto& [key, entry] : m_ByPath) entry.Base = true;
}

void TextureCache::Release(const std::string& path, bool indexed) {
    const std::string key = indexed ? "#idx#" + path : path;
    auto it = m_ByPath.find(key);
    if (it == m_ByPath.end() || it->second.Base) return;
    if (--it->second.Claims > 0) return;

    // The last claim: the pixels go, and so does any resource slot still
    // pointing at them. A slot pointing at freed pixels is the one way this
    // could hand out a dangling texture, and slots only ever hold the base
    // set, so this is belt and braces rather than a real case.
    for (auto slot = m_Slots.begin(); slot != m_Slots.end();) {
        slot = slot->second == &it->second.Tex ? m_Slots.erase(slot)
                                               : std::next(slot);
    }
    m_ByPath.erase(it);
}

std::size_t TextureCache::Bytes() const {
    std::size_t bytes = 0;
    for (const auto& [key, entry] : m_ByPath) {
        for (const TcTexture::Image& img : entry.Tex.Frames)
            bytes += img.Pixels.size() * sizeof(uint16_t);
        for (const TcTexture::Image& img : entry.Tex.SurfFrames)
            bytes += img.Pixels.size() * sizeof(uint16_t);
    }
    return bytes;
}

const Texture* TextureCache::Register(uint16_t id, const std::string& path) {
    const Texture* t = Load(path);
    if (t) m_Slots[id] = t;
    return t;
}

const Texture* TextureCache::Slot(uint16_t id) const {
    auto it = m_Slots.find(id);
    return it == m_Slots.end() ? nullptr : it->second;
}

int TextureCache::LoadStartupTextures(int* attempted) {
    // Fonts (slots 0x22 and 0xc2) are loaded here in the original too, but they
    // are a separate class that isn't ported yet.
    static const TextureSlot kExtra[] = {
        {kResFullboard, "Data\\Menu\\fullboard.tc"},
    };
    const int n = static_cast<int>(kMenuTextureCount) +
                  static_cast<int>(sizeof(kExtra) / sizeof(kExtra[0]));
    if (attempted) *attempted = n;

    int ok = 0;
    for (std::size_t i = 0; i < kMenuTextureCount; ++i) {
        const Texture* t = Register(kMenuTextures[i].ID, kMenuTextures[i].Path);
        if (t && t->Complete) ++ok;
    }
    for (const TextureSlot& s : kExtra) {
        const Texture* t = Register(s.ID, s.Path);
        if (t && t->Complete) ++ok;
    }
    return ok;
}

}  // namespace bb
