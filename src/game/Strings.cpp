#include "game/Strings.h"

#include <cstdlib>
#include <vector>

#include "game/FilePack.hpp"
#include "shim/Log.h"

namespace bb {
namespace {

const std::string kEmpty;

// Language index -> file suffix, in the binary's order (0x100b97a0).
const char* SuffixFor(Language lang) {
    switch (lang) {
        case Language::kFr: return "FR";
        case Language::kIt: return "IT";
        case Language::kGe: return "GE";
        case Language::kSp: return "SP";
        case Language::kEn: break;
    }
    return "EN";  // the original's `default:` case too
}

}  // namespace

std::string Strings::PathFor(Language lang) {
    return std::string("Data\\localization\\strings_") + SuffixFor(lang) +
           ".txt";
}

bool Strings::Load(FilePack& pack, Language lang) {
    m_Entries.clear();
    const std::string path = PathFor(lang);
    auto stream = pack.Open(path);
    if (!stream) {
        LogError("strings: '%s' not in pak\n", path.c_str());
        return false;
    }
    const std::vector<uint8_t>& buf = stream->Data();

    std::size_t i = 0;
    while (i < buf.size()) {
        std::size_t end = i;
        while (end < buf.size() && buf[end] != '\n' && buf[end] != '\r') ++end;
        const char* line = reinterpret_cast<const char*>(buf.data()) + i;
        const std::size_t len = end - i;
        i = end;
        while (i < buf.size() && (buf[i] == '\n' || buf[i] == '\r')) ++i;

        if (len == 0 || line[0] == '#') continue;
        // `<id>\t<text>`; anything without a numeric key is a stray comment.
        std::size_t tab = 0;
        while (tab < len && line[tab] != '\t') ++tab;
        if (tab == 0 || tab >= len) continue;
        bool numeric = true;
        for (std::size_t k = 0; k < tab; ++k)
            if (line[k] < '0' || line[k] > '9') numeric = false;
        if (!numeric) continue;

        const int id = std::atoi(std::string(line, tab).c_str());
        m_Entries[id] = std::string(line + tab + 1, len - tab - 1);
    }
    return !m_Entries.empty();
}

const std::string& Strings::Get(int id) const {
    auto it = m_Entries.find(id);
    return it == m_Entries.end() ? kEmpty : it->second;
}

}  // namespace bb
