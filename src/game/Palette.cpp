#include "game/Palette.h"

#include "game/FileInputStream.hpp"

namespace bb {

namespace {
constexpr size_t kHeaderBytes = 2;
}  // namespace

bool Palette::Load(FileInputStream& in) {
    m_Entries.clear();
    if (in.Size() <= kHeaderBytes) return false;
    in.Seek(kHeaderBytes);
    const size_t count = (in.Size() - kHeaderBytes) / 2;
    m_Entries.reserve(count);
    for (size_t i = 0; i < count; ++i) m_Entries.push_back(in.ReadU16());
    return !m_Entries.empty();
}

bool Palette::LooksPaletted(const TcTexture::Image& img) const {
    if (m_Entries.empty()) return false;
    int maxIndex = -1;
    for (uint16_t v : img.Pixels) {
        if (((v >> 12) & 0xF) == 0) continue;  // transparent carries no index
        const int idx = v & 0x0FFF;
        if (idx > maxIndex) maxIndex = idx;
    }
    return maxIndex >= 0 && static_cast<size_t>(maxIndex) < m_Entries.size();
}

void Palette::Resolve(TcTexture::Image& img) const {
    if (!LooksPaletted(img)) return;
    for (uint16_t& v : img.Pixels) {
        const uint16_t a = v & 0xF000;
        if (a == 0) { v = 0; continue; }
        v = static_cast<uint16_t>(a | (Rgb444(v & 0x0FFF) & 0x0FFF));
    }
}

}  // namespace bb
