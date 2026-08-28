// FileInputStream — port of RedLynx's FileInputStream (main.dll @ 0x1007057c).
//
// The original is a byte-oriented reader over an EPOC file or a FilePack entry,
// with a one-byte peek (seek +1 / -1). Asset parsers read fields off it; the
// N-Gage is little-endian ARM, so multi-byte values are little-endian. Here it
// reads from an in-memory buffer (a fully-decompressed pak entry from
// bb::Resources), which matches observable behavior of the streaming original.
#pragma once

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace bb {

class FileInputStream {
public:
    enum class Origin { Begin, Current, End };

    FileInputStream() = default;
    explicit FileInputStream(std::vector<uint8_t> data) : m_Data(std::move(data)) {}

    bool IsOpen() const { return !m_Data.empty(); }
    uint32_t Size() const { return static_cast<uint32_t>(m_Data.size()); }
    uint32_t Tell() const { return m_Pos; }
    bool Eof() const { return m_Pos >= m_Data.size(); }
    uint32_t Remaining() const { return Size() - m_Pos; }

    void Seek(int64_t off, Origin origin = Origin::Begin) {
        int64_t base = origin == Origin::Begin ? 0
                     : origin == Origin::Current ? int64_t(m_Pos)
                     : int64_t(m_Data.size());
        int64_t np = base + off;
        if (np < 0) np = 0;
        if (np > int64_t(m_Data.size())) np = int64_t(m_Data.size());
        m_Pos = static_cast<uint32_t>(np);
    }
    void Skip(int64_t n) { Seek(n, Origin::Current); }

    // Raw read; returns bytes actually copied (clamped to remaining).
    uint32_t Read(void* dst, uint32_t n) {
        const uint32_t avail = Remaining();
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, m_Data.data() + m_Pos, n);
        m_Pos += n;
        return n;
    }

    // Peek the next byte without advancing; -1 at EOF (matches ::peek()).
    int Peek() const { return m_Pos < m_Data.size() ? m_Data[m_Pos] : -1; }

    uint8_t ReadU8() { return m_Pos < m_Data.size() ? m_Data[m_Pos++] : 0; }
    uint16_t ReadU16() {  // little-endian
        uint16_t v = ReadU8();
        v |= uint16_t(ReadU8()) << 8;
        return v;
    }
    uint32_t ReadU32() {  // little-endian
        uint32_t v = ReadU8();
        v |= uint32_t(ReadU8()) << 8;
        v |= uint32_t(ReadU8()) << 16;
        v |= uint32_t(ReadU8()) << 24;
        return v;
    }
    int8_t ReadS8() { return int8_t(ReadU8()); }
    int16_t ReadS16() { return int16_t(ReadU16()); }
    int32_t ReadS32() { return int32_t(ReadU32()); }

    const std::vector<uint8_t>& Data() const { return m_Data; }

    // Hand the bytes over instead of copying them. For a reader that wants to
    // keep the file rather than parse it and let it go -- SpcAudio holds its
    // whole compressed stream and decodes out of it a block at a time, and on
    // a console the copy this saves is the difference between fitting and not.
    // The stream is empty afterwards.
    std::vector<uint8_t> TakeData() {
        m_Pos = 0;
        return std::move(m_Data);
    }

private:
    std::vector<uint8_t> m_Data;
    uint32_t m_Pos = 0;
};

}  // namespace bb
