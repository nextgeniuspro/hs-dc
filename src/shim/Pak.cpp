#include "shim/Pak.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include <zlib.h>

namespace bb {

namespace {
constexpr std::size_t kEntrySize = 17;  // packed, unaligned

std::string Canonical(const std::string& path) {
    std::string s = path;
    for (char& c : s) {
        if (c == '\\') c = '/';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    if (s.compare(0, 5, "DATA/") != 0) s = "DATA/" + s;
    return s;
}

uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

uint32_t PakHash(const std::string& path) {
    const std::string s = Canonical(path);
    uint32_t h = 0;
    uint32_t k = static_cast<uint32_t>(s.size());
    for (unsigned char c : s) {
        h += c * k;
        k = 18000u * (k & 0xFFFFu) + (k >> 16);  // multiply-with-carry step
    }
    return h & 0x7FFFFFFFu;
}

bool Pak::Open(const std::string& pakPath) {
    m_Path = pakPath;
    m_Entries.clear();

    FILE* f = std::fopen(pakPath.c_str(), "rb");
    if (!f) return false;

    uint8_t hdr[8];
    if (std::fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fclose(f);
        return false;
    }
    const uint32_t dirSize = ReadU32(hdr);
    const uint32_t count = ReadU32(hdr + 4);
    // The count is bounded before it is multiplied by anything.
    //
    // `count * kEntrySize` is done in `std::size_t`, so on a 64-bit host it
    // cannot wrap and the self-check below already turns away a count near
    // 2^32. On a 32-bit target -- the Dreamcast -- it can: 0xF0F0F0F1 entries
    // multiply out to 1, and a header claiming a nine-byte directory would
    // then size the buffer from the wrapped product and read four billion
    // entries out of it.
    //
    // Worth bounding on both, because "is this file the game's data?" is now a
    // question asked of whatever a player pointed at (platform/DataFiles.h),
    // and an answer that depends on the width of a size_t is not one.
    if (count > (0xFFFFFFFFu - 8) / kEntrySize) {
        std::fclose(f);
        return false;
    }
    if (dirSize != 8 + count * kEntrySize) {
        std::fclose(f);
        return false;  // header self-check failed => not this format
    }

    std::vector<uint8_t> dir(count * kEntrySize);
    if (std::fread(dir.data(), 1, dir.size(), f) != dir.size()) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    m_Entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = dir.data() + i * kEntrySize;
        const uint32_t hash = ReadU32(p);
        Entry e;
        e.CompSize = ReadU32(p + 4);
        e.UncompSize = ReadU32(p + 8);
        e.Flags = p[12];
        e.Offset = ReadU32(p + 13);
        m_Entries.emplace(hash, e);
    }
    return true;
}

bool Pak::Exists(const std::string& path) const {
    return m_Entries.count(PakHash(path)) != 0;
}

bool Pak::ReadEntry(const Entry& e, std::vector<uint8_t>& out) const {
    FILE* f = std::fopen(m_Path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, static_cast<long>(e.Offset), SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    std::vector<uint8_t> comp(e.CompSize);
    const bool ok = std::fread(comp.data(), 1, comp.size(), f) == comp.size();
    std::fclose(f);
    if (!ok) return false;

    if (e.Flags == 0) {  // stored
        out = std::move(comp);
        return out.size() == e.UncompSize;
    }

    out.assign(e.UncompSize, 0);
    uLongf dst = e.UncompSize;
    const int z = uncompress(out.data(), &dst, comp.data(), comp.size());
    return z == Z_OK && dst == e.UncompSize;
}

bool Pak::Read(const std::string& path, std::vector<uint8_t>& out) const {
    auto it = m_Entries.find(PakHash(path));
    if (it == m_Entries.end()) return false;
    return ReadEntry(it->second, out);
}

}  // namespace bb
