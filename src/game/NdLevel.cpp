#include "game/NdLevel.h"

#include <cstdio>
#include <cstring>

#include "game/FileInputStream.hpp"
#include "game/FilePack.hpp"
#include "shim/Log.h"

namespace bb {
namespace {

// The seven chunk masks the shipped levels use. The engine looks handlers up
// by (mask, kind, subtype) and skips anything it has no entry for, so an
// unknown mask is not an error.
enum ChunkMask : uint32_t {
    kChunkTerrain = 0x1,
    kChunkProperties = 0x10,
    kChunkUnits = 0x100,
    kChunkTriggers = 0x1000,
    kChunkRegions = 0x10000,
    kChunkDiscarded = 0x100000,
    kChunkPlayers = 0x1000000,
};

constexpr std::size_t kChunkHeader = 11;

uint16_t Rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

// A bounds-checked walk over one chunk payload. Every reader below ends by
// asserting it consumed the payload exactly, which is what proved the format:
// all 66 levels parse with nothing left over.
class Cursor {
public:
    Cursor(const uint8_t* p, std::size_t n) : m_P(p), m_N(n) {}
    bool Ok() const { return m_Ok; }
    std::size_t Left() const { return m_Ok ? m_N - m_At : 0; }
    bool Done() const { return m_Ok && m_At == m_N; }

    uint8_t U8() {
        if (m_At + 1 > m_N) return Fail();
        return m_P[m_At++];
    }
    uint16_t U16() {
        if (m_At + 2 > m_N) return Fail();
        const uint16_t v = Rd16(m_P + m_At);
        m_At += 2;
        return v;
    }
    uint32_t U32() {
        if (m_At + 4 > m_N) return Fail();
        const uint32_t v = Rd32(m_P + m_At);
        m_At += 4;
        return v;
    }
    std::string Str(std::size_t len) {
        if (m_At + len > m_N) {
            Fail();
            return std::string();
        }
        std::string s(reinterpret_cast<const char*>(m_P + m_At), len);
        m_At += len;
        return s;
    }
    void Blob(std::vector<uint8_t>& out, std::size_t len) {
        if (m_At + len > m_N) {
            Fail();
            return;
        }
        out.assign(m_P + m_At, m_P + m_At + len);
        m_At += len;
    }
    std::size_t Tell() const { return m_At; }

private:
    uint8_t Fail() {
        m_Ok = false;
        m_At = m_N;
        return 0;
    }
    const uint8_t* m_P;
    std::size_t m_N;
    std::size_t m_At = 0;
    bool m_Ok = true;
};

}  // namespace

int NdLevel::PassengerOf(int type) {
    switch (type) {
        case 19: return 1;  // Rowing-boat-Swordsman  -> Swordsman
        case 20: return 2;  // Rowing-boat-Pistoleer  -> Pistoleer
        case 21: return 3;  // Rowing-boat-Musketeer  -> Musketeer
        default: return 0;
    }
}

bool NdLevel::Load(FilePack& pack, const std::string& path) {
    m_Path = path;
    auto file = pack.Open(path);
    if (!file) {
        LogError("level: '%s' not in the pak\n", path.c_str());
        return false;
    }
    return Parse(file->Data());
}

bool NdLevel::Parse(const std::vector<uint8_t>& d) {
    m_Width = m_Height = 0;
    m_Tiles.clear();
    m_Properties.clear();
    m_Units.clear();
    m_Regions.clear();
    m_Triggers.clear();
    m_Params.clear();
    m_Variables.clear();
    m_Players.clear();

    if (d.size() < 20 || std::memcmp(d.data(), "NDL", 3) != 0) return false;
    if (Rd16(d.data() + 3) != 0x0100) return false;
    m_Width = Rd16(d.data() + 14);
    m_Height = Rd16(d.data() + 16);
    if (m_Width <= 0 || m_Height <= 0) return false;
    m_Tiles.assign(std::size_t(m_Width) * m_Height, Tile{});

    std::size_t at = 20;
    while (at + kChunkHeader <= d.size()) {
        const uint32_t size = Rd32(d.data() + at);
        const uint32_t mask = Rd32(d.data() + at + 4);
        if (size < kChunkHeader || at + size > d.size()) return false;
        const uint8_t* body = d.data() + at + kChunkHeader;
        const std::size_t len = size - kChunkHeader;
        bool ok = true;
        switch (mask) {
            case kChunkTerrain: ok = ReadTerrain(body, len); break;
            case kChunkProperties: ok = ReadPlacements(body, len, m_Properties, false); break;
            case kChunkUnits: ok = ReadPlacements(body, len, m_Units, true); break;
            case kChunkTriggers: ok = ReadTriggers(body, len); break;
            case kChunkRegions: ok = ReadRegions(body, len); break;
            case kChunkPlayers: ok = ReadPlayers(body, len); break;
            default: break;  // including 0x100000, which the engine also drops
        }
        if (!ok) {
            LogError("level: chunk %#x in '%s' did not parse\n", mask,
                     m_Path.c_str());
            return false;
        }
        at += size;
    }
    return at == d.size();
}

// 0x100830ac reads the whole grid raw; 0x100404cc then splits each word.
bool NdLevel::ReadTerrain(const uint8_t* p, std::size_t n) {
    const std::size_t count = std::size_t(m_Width) * m_Height;
    if (n != count * 2) return false;
    for (std::size_t i = 0; i < count; ++i) {
        uint16_t v = Rd16(p + i * 2);
        if (v != 0) --v;
        m_Tiles[i].Terrain = uint8_t(v & 0xff);
        m_Tiles[i].Variant = uint8_t(v >> 8);
    }
    return true;
}

// 0x10083984 (properties) and 0x10083740 (units) read the same shape; only
// the units layer keeps the top two bits, which are a Cannon Tower's facing.
bool NdLevel::ReadPlacements(const uint8_t* p, std::size_t n,
                             std::vector<Placement>& out, bool units) {
    const std::size_t count = std::size_t(m_Width) * m_Height;
    if (n != count * 4) return false;
    for (std::size_t i = 0; i < count; ++i) {
        const uint32_t v = Rd32(p + i * 4);
        if (v == 0) continue;
        Placement e;
        e.X = uint8_t(i % std::size_t(m_Width));
        e.Y = uint8_t(i / std::size_t(m_Width));
        e.Type = uint8_t(v & 0xff);
        e.Owner = uint8_t((v >> 8) & 0xff);
        e.ID = uint16_t((v & 0x3fffffff) >> 16);
        e.Facing = units ? uint8_t(v >> 30) : 0;
        out.push_back(e);
    }
    return true;
}

// 0x10083a94. Two record types share the chunk: type 1 assigns attributes to
// a trigger parameter, type 2 is the trigger script itself. Each record
// declares its own length, and the engine bails out if a reader does not land
// on it exactly -- so do we.
bool NdLevel::ReadTriggers(const uint8_t* p, std::size_t n) {
    Cursor c(p, n);
    const uint16_t count = c.U16();
    for (uint16_t i = 0; i < count && c.Ok(); ++i) {
        const std::size_t start = c.Tell();
        const uint16_t recSize = c.U16();
        const uint16_t recType = c.U16();
        if (recType == 1) {
            Param param;
            param.Target = c.U32();
            const uint8_t attrs = c.U8();
            for (uint8_t a = 0; a < attrs && c.Ok(); ++a) {
                const std::string key = c.Str(c.U8());
                const std::string value = c.Str(c.U8());
                param.Attrs.emplace_back(key, value);
            }
            NoteVariable(param);
            m_Params.push_back(std::move(param));
        } else if (recType == 2) {
            Trigger t;
            t.ID = c.U16();
            std::vector<std::string>* lists[3] = {&t.Events, &t.Conditions,
                                                  &t.Actions};
            for (int part = 0; part < 3 && c.Ok(); ++part) {
                const uint16_t lines = c.U16();
                for (uint16_t l = 0; l < lines && c.Ok(); ++l)
                    lists[part]->push_back(c.Str(c.U16()));
            }
            m_Triggers.push_back(std::move(t));
        } else {
            return false;
        }
        if (!c.Ok() || c.Tell() - start != recSize) return false;
    }
    return c.Done();
}

// 0x10083118. A fixed number of slots, most of them empty; a used slot names
// its bounding box and then a byte per cell inside it.
// The editor's object list, seen the way `variable(N)` sees it. 0x10083a94
// acts on three attribute keys and reads past the rest; the rest are still
// worth keeping, because knowing that variable 85 was declared a UnitType is
// how an unresolvable handle can be reported instead of silently answering
// "no such unit".
//
// A region's value is `x1,y1,x2,y2` -- the engine hands it to the same
// comma tokeniser the point parser uses and takes items 0, 2, 4 and 6, so a
// string that does not split into four numbers is ignored, which is exactly
// what happens to the second RegionType attribute (an editor object hash).
void NdLevel::NoteVariable(const Param& p) {
    Variable v;
    for (const auto& [key, value] : p.Attrs) {
        if (key == "PointType") {
            int x = 0, y = 0;
            if (std::sscanf(value.c_str(), "%d,%d", &x, &y) == 2) {
                v.X = x;
                v.Y = y;
                v.HasPoint = true;
            }
            continue;
        }
        if (key == "RegionType") {
            int a = 0, b = 0, c = 0, d = 0;
            if (std::sscanf(value.c_str(), "%d,%d,%d,%d", &a, &b, &c, &d) == 4) {
                v.Kind = Variable::kRegion;
                v.X1 = a;
                v.Y1 = b;
                v.X2 = c;
                v.Y2 = d;
            } else if (v.Kind == Variable::kNone) {
                v.Kind = Variable::kRegion;
            }
            continue;
        }
        if (key == "BooleanType") {
            v.Kind = Variable::kBoolean;
            v.Flag = value == "true" || value == "1" || value == "TRUE";
            continue;
        }
        if (v.Kind != Variable::kNone) continue;
        if (key == "PropertyType") v.Kind = Variable::kProperty;
        else if (key == "UnitType") v.Kind = Variable::kUnit;
        else if (key == "TriggerType") v.Kind = Variable::kTrigger;
        else if (key == "ObjectType") v.Kind = Variable::kObject;
    }
    if (v.Kind != Variable::kNone || v.HasPoint)
        m_Variables[int(p.Target)] = v;
}

const NdLevel::Variable* NdLevel::ScriptVariable(int id) const {
    const auto it = m_Variables.find(id);
    return it == m_Variables.end() ? nullptr : &it->second;
}

bool NdLevel::ReadRegions(const uint8_t* p, std::size_t n) {
    Cursor c(p, n);
    const uint16_t count = c.U16();
    for (uint16_t i = 0; i < count && c.Ok(); ++i) {
        if (c.U8() == 0) continue;
        Region r;
        r.Index = i;
        r.Kind = c.U8();
        r.Flags = c.U8();
        r.X = c.U8();
        r.Y = c.U8();
        r.W = c.U8();
        r.H = c.U8();
        c.Blob(r.Mask, std::size_t(r.W) * r.H);
        m_Regions.push_back(std::move(r));
    }
    return c.Done();
}

// 0x100834bc. A continue byte, then human/computer and an optional name; four
// slots, the unused ones present but nameless.
bool NdLevel::ReadPlayers(const uint8_t* p, std::size_t n) {
    Cursor c(p, n);
    while (c.Ok() && c.U8() != 0) {
        Player pl;
        pl.Computer = c.U8() != 0;
        pl.Name = c.Str(c.U8());
        m_Players.push_back(std::move(pl));
    }
    return c.Done();
}

}  // namespace bb
