// SaveStream — the byte encoding saved games are written in.
//
// Two rules shape it, and both come from the memory card the port is aimed at
// next.
//
// **Small.** A Dreamcast VMU bills in 512-byte blocks, so every byte saved is
// worth saving: a battle is a few hundred units and buildings, and encoding
// each field as a machine word would triple the size for nothing. Numbers go
// out as LEB128 varints, so the values the game actually holds -- a hit-point
// count, a tile coordinate, a unit type -- cost one byte each, and a value
// only costs more when it is genuinely bigger. Signed numbers zigzag first, so
// -1 costs one byte rather than ten.
//
// **Unforgiving of damage.** Flash goes bad and a half-written card is a real
// state, so a reader must not be able to wander off the end of a truncated
// blob and hand the game plausible nonsense. `SaveReader` carries a sticky
// `ok` flag: the first read past the end sets it, every later read returns
// zero without touching memory, and the caller checks once at the end instead
// of after every field. Sizes are checked against a caller-supplied limit
// before anything is resized, so a corrupt length cannot ask for a gigabyte.
//
// Everything is little-endian by construction -- varints have no byte order --
// so a save written on a desktop is readable on the SH-4 and vice versa.
//
// Header-only, hence `.hpp`: the whole encoding is a few dozen lines and every
// one of them wants to be inlined into the field lists that call it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bb {

class SaveWriter {
public:
    void U8(uint8_t v) { m_Bytes.push_back(v); }
    void Bool(bool v) { U8(v ? 1 : 0); }

    // LEB128: seven bits per byte, high bit means "another follows".
    void Uint(uint64_t v) {
        while (v >= 0x80) {
            m_Bytes.push_back(uint8_t(v) | 0x80u);
            v >>= 7;
        }
        m_Bytes.push_back(uint8_t(v));
    }

    // Zigzag, so small negatives stay one byte.
    void Int(int64_t v) { Uint((uint64_t(v) << 1) ^ uint64_t(v >> 63)); }

    // Length-prefixed, no terminator. Truncated to `limit` bytes -- a
    // commander name is not allowed to grow the save without bound.
    void Str(const std::string& s, std::size_t limit = 64) {
        const std::size_t n = s.size() < limit ? s.size() : limit;
        Uint(n);
        m_Bytes.insert(m_Bytes.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
    }

    void Bytes(const uint8_t* data, std::size_t n) {
        m_Bytes.insert(m_Bytes.end(), data, data + n);
    }

    const std::vector<uint8_t>& Data() const { return m_Bytes; }
    std::vector<uint8_t> Take() { return std::move(m_Bytes); }
    std::size_t Size() const { return m_Bytes.size(); }

private:
    std::vector<uint8_t> m_Bytes;
};

class SaveReader {
public:
    SaveReader(const uint8_t* data, std::size_t size)
        : m_Data(data), m_Size(size) {}
    explicit SaveReader(const std::vector<uint8_t>& blob)
        : m_Data(blob.data()), m_Size(blob.size()) {}

    uint8_t U8() {
        if (m_At >= m_Size) return Fail();
        return m_Data[m_At++];
    }
    bool Bool() { return U8() != 0; }

    uint64_t Uint() {
        uint64_t v = 0;
        int shift = 0;
        for (;;) {
            if (m_At >= m_Size) return Fail();
            const uint8_t b = m_Data[m_At++];
            // Ten groups of seven bits is the most a 64-bit value can need;
            // more than that is a corrupt stream, not a big number.
            if (shift > 63) return Fail();
            v |= uint64_t(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) break;
            shift += 7;
        }
        return v;
    }

    int64_t Int() {
        const uint64_t u = Uint();
        return int64_t(u >> 1) ^ -int64_t(u & 1);
    }

    // Narrowing helpers, so call sites read as the types they are storing
    // into rather than as a wall of casts.
    int I32() { return int(Int()); }
    unsigned U32() { return unsigned(Uint()); }

    std::string Str(std::size_t limit = 64) {
        const std::size_t n = std::size_t(Uint());
        if (!m_Ok || n > limit || n > Remaining()) {
            Fail();
            return {};
        }
        std::string s(reinterpret_cast<const char*>(m_Data + m_At), n);
        m_At += n;
        return s;
    }

    // A count about to size a container. Anything past `limit` is treated as
    // corruption rather than trusted, so a damaged blob cannot make the game
    // try to allocate its way out of memory.
    std::size_t Count(std::size_t limit) {
        const std::size_t n = std::size_t(Uint());
        if (!m_Ok || n > limit) {
            Fail();
            return 0;
        }
        return n;
    }

    bool Ok() const { return m_Ok; }
    std::size_t Remaining() const { return m_At < m_Size ? m_Size - m_At : 0; }
    // True when the whole blob was consumed and nothing went wrong -- the
    // check a loader wants, because trailing bytes mean the two sides
    // disagree about the format.
    bool Complete() const { return m_Ok && m_At == m_Size; }

private:
    uint8_t Fail() {
        m_Ok = false;
        m_At = m_Size;  // every later read short-circuits
        return 0;
    }

    const uint8_t* m_Data;
    std::size_t m_Size;
    std::size_t m_At = 0;
    bool m_Ok = true;
};

}  // namespace bb
