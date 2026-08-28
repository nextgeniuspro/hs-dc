#include "game/TcTexture.h"

#include "game/FileInputStream.hpp"

namespace bb {

bool TcTexture::Parse(FileInputStream& in) {
    if (in.Remaining() < 12) return false;
    m_Format = in.ReadU16();
    m_Width = in.ReadU16();
    m_Height = in.ReadU16();
    m_DeclaredFrames = in.ReadU16();
    const uint32_t dataSize = in.ReadU32();

    // Bounds mirror the original header validation (FUN_100b62c0).
    if (m_Format != kNonLossy) return false;               // only 2bpp supported
    if (m_Width > 1024 || m_Height > 512 || m_DeclaredFrames > 256) return false;
    if (dataSize > 0x100000 || dataSize > in.Remaining()) return false;

    m_Frames.clear();
    m_Frames.reserve(m_DeclaredFrames);
    uint32_t consumed = 0;
    for (uint16_t f = 0; f < m_DeclaredFrames; ++f) {
        if (consumed + 4 > dataSize) return false;
        const uint32_t rec = in.ReadU32();
        consumed += 4;
        if (rec > dataSize - consumed) return false;
        Frame frame;
        frame.Data.resize(rec);
        if (in.Read(frame.Data.data(), rec) != rec) return false;
        consumed += rec;
        m_Frames.push_back(std::move(frame));
    }
    return true;
}

namespace {
constexpr uint16_t kRowEnd = 0xFFFF;  // also the fully-transparent-row marker

// Longest transparent run the encoder puts in one segment. See Decode().
constexpr int kMaxSkip = 254;
}  // namespace

TcTexture::DecodeStatus TcTexture::Decode(uint16_t frame, Image& out,
                                          Image* surf) const {
    const DecodeStatus strict = DecodeVariant(frame, out, 0);
    if (strict != DecodeStatus::kUnsupportedStream) {
        if (surf) *surf = Image{};
        return strict;
    }
    return DecodeVariant(frame, out, kSlackWords, surf);
}

bool TcTexture::IsWaterFrame(uint16_t frame) const {
    if (frame >= m_Frames.size()) return false;
    Image scratch;
    return DecodeVariant(frame, scratch, 0) == DecodeStatus::kUnsupportedStream;
}

TcTexture::DecodeStatus TcTexture::DecodeVariant(uint16_t frame, Image& out,
                                                 int slack, Image* surf) const {
    if (frame >= m_Frames.size()) return DecodeStatus::kBadFrameIndex;
    const auto& data = m_Frames[frame].Data;
    const size_t n = data.size() / 2;  // stream is 16-bit words
    auto word = [&data](size_t i) -> uint16_t {
        return static_cast<uint16_t>(data[i * 2] | (data[i * 2 + 1] << 8));
    };

    out.Width = m_Width;
    out.Height = m_Height;
    out.Pixels.assign(size_t(m_Width) * m_Height, 0);
    // Only a water frame has two layers to separate; an ordinary one's runs
    // are the same picture, so the caller gets nothing back.
    const bool split = surf != nullptr && slack > 0;
    if (surf) {
        surf->Width = split ? m_Width : 0;
        surf->Height = split ? m_Height : 0;
        surf->Pixels.assign(split ? size_t(m_Width) * m_Height : 0, 0);
    }
    Image& run1Into = split ? *surf : out;

    size_t p = 0;
    for (uint16_t y = 0; y < m_Height; ++y) {
        if (p >= n) return DecodeStatus::kUnsupportedStream;
        int x = 0;
        uint16_t ctrl = word(p++);
        if (ctrl == kRowEnd) continue;  // whole row transparent
        for (;;) {
            const int skip = ctrl & 0xFF;   // A: transparent pixels
            const int lit = ctrl >> 8;      // B: literal pixels that follow
            x += skip;
            // A water frame's first run carries four columns of slop either
            // side of it, so the unshifted pixels start four words in.
            const int held = lit > 0 ? lit + slack : 0;
            const size_t body = p + size_t(slack ? kSlackLeft : 0);
            if (p + held > n) return DecodeStatus::kUnsupportedStream;
            for (int i = 0; i < lit; ++i) {
                if (x + i < m_Width)
                    run1Into.Pixels[size_t(y) * m_Width + x + i] = word(body + i);
            }
            p += held;
            x += lit;

            if (p >= n) return DecodeStatus::kUnsupportedStream;
            const int run2 = word(p++);     // C: further pixels
            if (p + run2 > n) return DecodeStatus::kUnsupportedStream;
            for (int i = 0; i < run2; ++i, ++p) {
                if (x + i < m_Width) out.Pixels[size_t(y) * m_Width + x + i] = word(p);
            }
            x += run2;

            // A pure-skip segment (no pixels either side) closes the row: it
            // encodes the trailing transparent tail. Without this, a row with
            // a trailing gap swallows the next one (e.g. Data\Menu\board.tc).
            //
            // Except at the encoder's maximum gap. `skip` is one byte, so a
            // transparent run longer than that is split, and each piece but
            // the last carries the maximum -- which is 254, not 255. Reading
            // those as row ends is what used to break every wide sprite with
            // a big transparent middle: no shipped frame contains a pure-skip
            // between 194 and 253, so the two cases never collide.
            if (lit == 0 && run2 == 0 && skip < kMaxSkip) break;
            if (x >= m_Width) break;
            if (p >= n) return DecodeStatus::kUnsupportedStream;
            ctrl = word(p++);
            if (ctrl == kRowEnd) break;
        }
    }
    // The encoder may leave one padding word; anything more means we mis-parsed.
    if (n - p > 1) return DecodeStatus::kUnsupportedStream;
    return DecodeStatus::kOk;
}

}  // namespace bb
