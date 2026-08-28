// Minimal Symbian descriptor shim.
//
// Symbian strings/buffers are "descriptors": a length + data view (TDesC),
// a modifiable view (TDes), a pointer view (TPtrC/TPtr), and inline buffers
// (TBuf<N>). The game uses the 8-bit and 16-bit families heavily. This is a
// starter subset covering the 8-bit side; it grows as decompiled code needs it.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace bb {

// Non-owning constant view (Symbian TDesC8).
class TDesC8 {
public:
    TDesC8() : m_Ptr(nullptr), m_Len(0) {}
    TDesC8(const uint8_t* p, int32_t n) : m_Ptr(p), m_Len(n) {}

    int32_t Length() const { return m_Len; }
    int32_t Size() const { return m_Len; }  // 8-bit: size == length
    const uint8_t* Ptr() const { return m_Ptr; }
    uint8_t operator[](int32_t i) const { return m_Ptr[i]; }

    int Compare(const TDesC8& o) const {
        const int32_t n = m_Len < o.m_Len ? m_Len : o.m_Len;
        const int c = n ? std::memcmp(m_Ptr, o.m_Ptr, n) : 0;
        if (c) return c;
        return m_Len - o.m_Len;
    }
    bool operator==(const TDesC8& o) const { return Compare(o) == 0; }

    std::string ToStdString() const {
        return std::string(reinterpret_cast<const char*>(m_Ptr), m_Len);
    }

protected:
    const uint8_t* m_Ptr;
    int32_t m_Len;
};

// Modifiable view (Symbian TDes8): data + length, bounded by a max length.
class TDes8 : public TDesC8 {
public:
    TDes8(uint8_t* p, int32_t len, int32_t max) : TDesC8(p, len), m_Max(max) {}

    int32_t MaxLength() const { return m_Max; }
    uint8_t* WritePtr() { return const_cast<uint8_t*>(m_Ptr); }

    void SetLength(int32_t n) { m_Len = n < m_Max ? n : m_Max; }
    void Zero() { m_Len = 0; }

    void Copy(const TDesC8& src) {
        const int32_t n = src.Length() < m_Max ? src.Length() : m_Max;
        std::memcpy(WritePtr(), src.Ptr(), n);
        m_Len = n;
    }
    void Append(uint8_t c) {
        if (m_Len < m_Max) const_cast<uint8_t*>(m_Ptr)[m_Len++] = c;
    }

protected:
    int32_t m_Max;
};

// Inline fixed-capacity buffer (Symbian TBuf8<N>).
template <int32_t N>
class TBuf8 : public TDes8 {
public:
    TBuf8() : TDes8(m_Store, 0, N) {}
    explicit TBuf8(const TDesC8& src) : TDes8(m_Store, 0, N) { Copy(src); }

private:
    uint8_t m_Store[N];
};

// Build a view over a C string literal (helper, not in the Symbian API).
inline TDesC8 Literal8(const char* s) {
    return TDesC8(reinterpret_cast<const uint8_t*>(s),
                  static_cast<int32_t>(std::strlen(s)));
}

}  // namespace bb
