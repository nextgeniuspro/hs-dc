#include "game/SpcAudio.h"

#include <algorithm>

#include "game/FileInputStream.hpp"
#include "game/FilePack.hpp"
#include "shim/Log.h"

namespace bb {
namespace {

constexpr int kHeaderBytes = 32;
constexpr int kDefaultN = 4;  // 0x100ac388 substitutes this when the field is 0

int16_t Clamp16(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return static_cast<int16_t>(v);
}

}  // namespace

bool SpcSound::Load(FilePack& pack, const std::string& path) {
    m_Path = path;
    m_Data.clear();
    m_Blocks = m_BlockBytes = m_BlockSamples = m_Body = m_Total = 0;
    m_Codes = 0;

    auto file = pack.Open(path);
    if (!file) {
        LogError("audio: '%s' not in the pak\n", path.c_str());
        return false;
    }
    std::vector<uint8_t> d = file->TakeData();
    if (d.size() < kHeaderBytes) return false;

    auto u16 = [&d](std::size_t o) {
        return static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
    };
    auto s16 = [&u16](std::size_t o) { return static_cast<int16_t>(u16(o)); };

    const int blocks = u16(2);
    const int headerBlockSamples = u16(4);
    const int rate = s16(6);
    int n = u16(10);
    uint32_t total = uint32_t(d[12]) | (uint32_t(d[13]) << 8) |
                     (uint32_t(d[14]) << 16) | (uint32_t(d[15]) << 24);
    if (n == 0) n = kDefaultN;
    if (total == 0) total = uint32_t(blocks) * uint32_t(headerBlockSamples);

    // The original checks the codebook size against the rate and refuses
    // anything else rather than guessing (0x100ac46c).
    const int codes = n * n;
    if (!((codes == 16 && rate == 8000) || (codes == 4 && rate == 4000))) {
        LogError("audio: '%s' has an unsupported %d Hz / %d-entry format\n",
                 path.c_str(), rate, codes);
        return false;
    }

    const std::size_t body = std::size_t(n) * 256;
    // Two samples per byte at 8 kHz (one per nibble); at 4 kHz a byte carries
    // four 2-bit codes and each makes two samples, so eight.
    const std::size_t blockSamples = codes == 16 ? body * 2 : body * 8;
    const std::size_t blockBytes = 2 + std::size_t(codes) * 2 + body;

    // Only count blocks the file actually carries, so a truncated stream ends
    // early rather than reading past itself.
    std::size_t whole = 0;
    while (kHeaderBytes + (whole + 1) * blockBytes <= d.size() &&
           whole < std::size_t(blocks))
        ++whole;
    if (whole == 0) return false;
    if (total > whole * blockSamples) total = whole * blockSamples;

    m_Data = std::move(d);
    m_Blocks = whole;
    m_BlockBytes = blockBytes;
    m_BlockSamples = blockSamples;
    m_Body = body;
    m_Total = total;
    m_Codes = codes;
    return true;
}

// 0x100ac46c, one block. Each block restarts from its own stored sample, so
// this needs nothing from the block before it -- which is exactly what makes
// the sound seekable and lets the mixer hold one block instead of the lot.
std::size_t SpcSound::DecodeBlock(std::size_t block, int16_t* out) const {
    if (!out || block >= m_Blocks) return 0;
    const std::size_t first = block * m_BlockSamples;
    if (first >= m_Total) return 0;

    const std::size_t o = kHeaderBytes + block * m_BlockBytes;
    auto s16 = [this](std::size_t at) {
        return static_cast<int16_t>(m_Data[at] | (m_Data[at + 1] << 8));
    };

    int acc = s16(o);
    int16_t table[16];
    for (int i = 0; i < m_Codes; ++i) table[i] = s16(o + 2 + std::size_t(i) * 2);
    const std::size_t data = o + 2 + std::size_t(m_Codes) * 2;

    // The last block is short when totalSamples does not land on a boundary.
    const std::size_t want = std::min(m_BlockSamples, m_Total - first);
    std::size_t at = 0;
    auto emit = [&](int v) {
        if (at < want) out[at] = Clamp16(v);
        ++at;
    };

    if (m_Codes == 16) {
        for (std::size_t i = 0; i < m_Body && at < want; ++i) {
            const uint8_t byte = m_Data[data + i];
            acc += table[byte & 0xF];
            emit(acc);
            acc += table[byte >> 4];
            emit(acc);
        }
    } else {
        // 4000 Hz: two output samples per code, the delta applied in halves,
        // which upsamples to 8000 as it goes.
        for (std::size_t i = 0; i < m_Body && at < want; ++i) {
            const uint8_t byte = m_Data[data + i];
            for (int shift = 0; shift < 8 && at < want; shift += 2) {
                const int delta = table[(byte >> shift) & 3] / 2;
                acc += delta;
                emit(acc);
                acc += delta;
                emit(acc);
            }
        }
    }
    return want;
}

void SpcSound::DecodeAll(std::vector<int16_t>& out) const {
    out.assign(m_Total, 0);
    if (m_Total == 0) return;
    for (std::size_t b = 0; b < m_Blocks; ++b) {
        const std::size_t first = b * m_BlockSamples;
        if (first >= m_Total) break;
        DecodeBlock(b, out.data() + first);
    }
}

}  // namespace bb
