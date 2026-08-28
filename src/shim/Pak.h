// RedLynx data.pak virtual filesystem (Blackbeard, N-Gage).
//
// Reimplements the original FilePack2/FileEPOC seam: the game opens assets by
// path (e.g. "Data\\Battle\\gfx\\buildings.tc") and the pak resolves them by a
// 31-bit name hash. Format and hash were reverse-engineered from main.dll; see
// tools/pakextract.py and tools/e32.py for the archaeology.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace bb {

// The name hash the original engine uses to key the archive directory.
// Canonical form: UPPERCASE, forward slashes, leading "DATA/".
uint32_t PakHash(const std::string& path);

class Pak {
public:
    // Open an archive file. Returns false if the header is invalid.
    bool Open(const std::string& pakPath);

    // Look up by original path (any case / slash style, with or without DATA/).
    bool Exists(const std::string& path) const;

    // Read+decompress a file into `out`. Returns false if absent or corrupt.
    bool Read(const std::string& path, std::vector<uint8_t>& out) const;

    std::size_t FileCount() const { return m_Entries.size(); }

private:
    struct Entry {
        uint32_t CompSize;
        uint32_t UncompSize;
        uint8_t  Flags;     // 1 = zlib, 0 = stored
        uint32_t Offset;
    };
    std::string m_Path;
    std::unordered_map<uint32_t, Entry> m_Entries;

    bool ReadEntry(const Entry& e, std::vector<uint8_t>& out) const;
};

}  // namespace bb
