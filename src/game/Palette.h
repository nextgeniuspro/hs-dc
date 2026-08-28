// Colour palette for indexed .tc textures (Blackbeard, N-Gage).
//
// The engine has two texture classes, chosen by the third argument of the
// loader FUN_100b5750:
//   0x100b572c: `mov r2,#0` -> class FUN_100b3324 -- pixels are direct ARGB4444
//   0x100b573c: `mov r2,#1` -> class FUN_100b6450 -- pixels are palette indices,
//                              and the caller stores a palette at obj+0x48
//
// A .pal file is a 2-byte header followed by RGB444 entries (0x0RGB).
// DATA/PALETTE.PAL holds 272 of them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "game/TcTexture.h"

namespace bb {

class FileInputStream;

class Palette {
public:
    bool Load(FileInputStream& in);

    bool Empty() const { return m_Entries.empty(); }
    size_t Size() const { return m_Entries.size(); }
    // RGB444 (0x0RGB); out-of-range indices return 0.
    uint16_t Rgb444(size_t i) const { return i < m_Entries.size() ? m_Entries[i] : 0; }

    // Does this decoded frame hold palette indices rather than direct colour?
    // Index images never exceed the palette size, direct-colour ones always
    // blow past it -- measured across all 418 shipped .tc assets, paletted
    // frames top out at 271 while the lowest direct-colour frame reaches 1132,
    // so the two groups are separated by a wide unambiguous gap.
    bool LooksPaletted(const TcTexture::Image& img) const;

    // Rewrite index pixels in place as direct ARGB4444, keeping the alpha
    // nibble. No-op for a frame that isn't indexed.
    void Resolve(TcTexture::Image& img) const;

private:
    std::vector<uint16_t> m_Entries;
};

}  // namespace bb
