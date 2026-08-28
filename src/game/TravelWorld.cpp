#include "game/TravelWorld.h"

#include <cctype>
#include <cstdlib>

#include "game/FileInputStream.hpp"
#include "game/FilePack.hpp"
#include "shim/Log.h"

namespace bb {
namespace {

std::string Trim(const std::string& s) {
    std::size_t i = 0, j = s.size();
    while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) --j;
    return s.substr(i, j - i);
}

// The text between <tag> and </tag> in [from, to), trimmed. False if the tag
// is not there. Matching includes the closing angle bracket, so <log> never
// picks up <loginfo> and <desc> never picks up <description>.
bool Tag(const std::string& s, const char* name, std::size_t from,
         std::size_t to, std::string& out) {
    const std::string open = std::string("<") + name + ">";
    const std::string close = std::string("</") + name + ">";
    const std::size_t a = s.find(open, from);
    if (a == std::string::npos || a >= to) return false;
    const std::size_t b = s.find(close, a);
    if (b == std::string::npos || b > to) return false;
    out = Trim(s.substr(a + open.size(), b - a - open.size()));
    return true;
}

std::string TagStr(const std::string& s, const char* name, std::size_t from,
                   std::size_t to) {
    std::string v;
    return Tag(s, name, from, to, v) ? v : std::string();
}

int TagInt(const std::string& s, const char* name, std::size_t from,
           std::size_t to, int fallback = 0) {
    std::string v;
    if (!Tag(s, name, from, to, v) || v.empty()) return fallback;
    return std::atoi(v.c_str());
}

bool HasTag(const std::string& s, const char* name, std::size_t from,
            std::size_t to) {
    const std::string open = std::string("<") + name + ">";
    const std::size_t a = s.find(open, from);
    return a != std::string::npos && a < to;
}

// The value of `name="..."` in the tag that starts at `at`.
int Attr(const std::string& s, std::size_t at, const char* name,
         int fallback = 0) {
    const std::string key = std::string(name) + "=\"";
    const std::size_t end = s.find('>', at);
    const std::size_t a = s.find(key, at);
    if (a == std::string::npos || (end != std::string::npos && a > end))
        return fallback;
    return std::atoi(s.c_str() + a + key.size());
}

// <position><x>..</x><y>..</y></position>, or (0,0).
TravelWorld::Point Position(const std::string& s, std::size_t from,
                            std::size_t to) {
    TravelWorld::Point p;
    const std::size_t a = s.find("<position>", from);
    if (a == std::string::npos || a >= to) return p;
    const std::size_t b = s.find("</position>", a);
    if (b == std::string::npos || b > to) return p;
    p.X = TagInt(s, "x", a, b);
    p.Y = TagInt(s, "y", a, b);
    return p;
}

}  // namespace

int TravelWorld::AreaOf(Point p) {
    // 0x100ddc6c walks the four cells and keeps the last one the point is
    // inside, so a point exactly on a dividing line belongs to the higher
    // cell; a point outside the box altogether stays at 0.
    // The engine halves the extent in 16.16, so the dividing line can land on
    // a half pixel -- and 677 across means it does. Working in doubled
    // integers keeps that half, which is what puts the far corner inside
    // quadrant 3 instead of outside every one of them.
    const int spanX = kMaxX - kMinX, spanY = kMaxY - kMinY;
    int area = 0;
    for (int iy = 0; iy < 2; ++iy) {
        for (int ix = 0; ix < 2; ++ix) {
            const int x0 = 2 * kMinX + spanX * ix;
            const int y0 = 2 * kMinY + spanY * iy;
            if (2 * p.X >= x0 && 2 * p.X <= x0 + spanX && 2 * p.Y >= y0 &&
                2 * p.Y <= y0 + spanY)
                area = ix + iy * 2;
        }
    }
    return area;
}

bool TravelWorld::Load(FilePack& pack) {
    m_Islands.clear();
    m_Locations.clear();
    m_Encounters.clear();
    m_Lanes.clear();
    m_Start = Point{};

    auto stream = pack.Open(kPath);
    if (!stream) {
        LogError("travel: '%s' not in pak\n", kPath);
        return false;
    }
    const std::vector<uint8_t>& buf = stream->Data();
    const std::string s(reinterpret_cast<const char*>(buf.data()), buf.size());

    // --- the water the whole map floats on.
    {
        const std::size_t a = s.find("<watertexture");
        if (a != std::string::npos) {
            const std::size_t open = s.find('>', a);
            const std::size_t b = s.find("</watertexture>", a);
            if (open != std::string::npos && b != std::string::npos && b > open) {
                m_Water.Texture = Trim(s.substr(open + 1, b - open - 1));
                m_Water.Radius = Attr(s, a, "radius", m_Water.Radius);
                m_Water.Height = Attr(s, a, "height", m_Water.Height);
                m_Water.Density = Attr(s, a, "density", m_Water.Density);
                m_Water.Interval = Attr(s, a, "interval", m_Water.Interval);
                m_Water.Windx = Attr(s, a, "windx", m_Water.Windx);
                m_Water.Windy = Attr(s, a, "windy", m_Water.Windy);
            }
        }
    }

    // --- the objects, in the order the file lists them. Blocks never nest, so
    // whichever opening tag comes next is the next object.
    const std::size_t objEnd = s.find("</objects>");
    std::size_t at = s.find("<objects>");
    if (at == std::string::npos) at = 0;
    while (at < s.size()) {
        struct Kind { const char* Tag; int Which; };
        static const Kind kinds[] = {{"island", 0}, {"mission", 1},
                                     {"encounter", 2}};
        std::size_t best = std::string::npos;
        int which = -1;
        for (const Kind& k : kinds) {
            const std::size_t p = s.find(std::string("<") + k.Tag + ">", at);
            if (p < best) {
                best = p;
                which = k.Which;
            }
        }
        if (which < 0 || best == std::string::npos) break;
        if (objEnd != std::string::npos && best > objEnd) break;
        const char* tag = kinds[which].Tag;
        const std::size_t end = s.find(std::string("</") + tag + ">", best);
        if (end == std::string::npos) break;

        if (which == 0) {
            Island is;
            is.Name = TagStr(s, "name", best, end);
            is.Texture = TagStr(s, "texture", best, end);
            is.Pos = Position(s, best, end);
            const std::size_t pa = s.find("<polygon>", best);
            const std::size_t pb = s.find("</polygon>", best);
            if (pa != std::string::npos && pb != std::string::npos && pb < end) {
                std::size_t v = pa;
                for (;;) {
                    v = s.find("<vertex ", v);
                    if (v == std::string::npos || v > pb) break;
                    is.Polygon.push_back({Attr(s, v, "x"), Attr(s, v, "y")});
                    ++v;
                }
            }
            if (!is.Texture.empty()) m_Islands.push_back(std::move(is));
        } else if (which == 1) {
            Location loc;
            loc.ID = TagInt(s, "id", best, end, -1);
            loc.Key = TagStr(s, "filename", best, end);
            loc.Texture = TagStr(s, "texture", best, end);
            loc.Pos = Position(s, best, end);
            loc.Open = TagInt(s, "open", best, end) != 0;
            loc.Sub = TagInt(s, "subbattle", best, end) != 0;
            loc.PlayerStart = HasTag(s, "playerstart", best, end);
            loc.Desc = TagInt(s, "desc", best, end);
            loc.Log = TagInt(s, "log", best, end);
            loc.Skill = TagInt(s, "skillpoints", best, end);
            loc.Before = TagStr(s, "animationbefore", best, end);
            loc.Complete = TagStr(s, "animationcomplete", best, end);
            loc.Fail = TagStr(s, "animationfail", best, end);
            std::size_t c = best;
            for (;;) {
                c = s.find("<connection ", c);
                if (c == std::string::npos || c > end) break;
                const std::size_t v = s.find('>', c);
                const std::size_t ve = s.find("</connection>", c);
                if (v == std::string::npos || ve == std::string::npos) break;
                Connection conn;
                conn.Type = Attr(s, c, "type");
                conn.ID = std::atoi(Trim(s.substr(v + 1, ve - v - 1)).c_str());
                loc.Connections.push_back(conn);
                c = ve;
            }
            if (loc.PlayerStart) m_Start = loc.Pos;
            m_Locations.push_back(std::move(loc));
        } else {
            Encounter e;
            e.ID = TagInt(s, "id", best, end);
            e.Key = TagStr(s, "filename", best, end);
            e.Area = TagInt(s, "area", best, end);
            e.Distance = TagInt(s, "distance", best, end);
            e.Chance = TagInt(s, "chance", best, end);
            e.Rate = TagInt(s, "rate", best, end);
            e.Log = TagInt(s, "log", best, end);
            e.Skill = TagInt(s, "skillpoints", best, end);
            if (e.Area >= 0 && e.Area < kAreas && !e.Key.empty())
                m_Encounters.push_back(std::move(e));
        }
        at = end + 1;
    }

    // --- the sea lanes: one <node> per undirected edge, two points each.
    {
        const std::size_t pa = s.find("<pathing>");
        const std::size_t pb = s.find("</pathing>");
        std::size_t n = pa;
        while (pa != std::string::npos && pb != std::string::npos) {
            n = s.find("<node>", n == pa ? pa : n);
            if (n == std::string::npos || n > pb) break;
            const std::size_t ne = s.find("</node>", n);
            if (ne == std::string::npos || ne > pb) break;
            std::size_t p1 = s.find("<point ", n);
            std::size_t p2 = p1 == std::string::npos
                                 ? std::string::npos
                                 : s.find("<point ", p1 + 1);
            if (p1 != std::string::npos && p2 != std::string::npos && p2 < ne) {
                Lane lane;
                lane.A = {Attr(s, p1, "x"), Attr(s, p1, "y")};
                lane.B = {Attr(s, p2, "x"), Attr(s, p2, "y")};
                m_Lanes.push_back(lane);
            }
            n = ne;
        }
    }

    LogDebug("travel: %zu islands, %zu locations, %zu encounters, %zu lanes\n",
             m_Islands.size(), m_Locations.size(), m_Encounters.size(),
             m_Lanes.size());
    return !m_Locations.empty();
}

std::vector<const TravelWorld::Location*> TravelWorld::Campaign() const {
    std::vector<const Location*> out;
    // The campaign proper is SP1..SP17, in the order the file lists them --
    // which is the order the map opens them up.
    for (const Location& m : m_Locations)
        if (m.Key.size() > 2 && m.Key.compare(0, 2, "SP") == 0 &&
            std::isdigit(static_cast<unsigned char>(m.Key[2])))
            out.push_back(&m);
    return out;
}

const TravelWorld::Location* TravelWorld::ByKey(const std::string& key) const {
    for (const Location& m : m_Locations)
        if (m.Key == key) return &m;
    return nullptr;
}

const TravelWorld::Location* TravelWorld::ById(int id) const {
    for (const Location& m : m_Locations)
        if (m.ID == id) return &m;
    return nullptr;
}

}  // namespace bb
