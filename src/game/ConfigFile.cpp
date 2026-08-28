#include "game/ConfigFile.h"

#include <cstdlib>

#include "game/FileInputStream.hpp"
#include "game/FilePack.hpp"

namespace bb {
namespace {

const std::string kEmpty;
const std::vector<std::string> kNoList;

bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && Space(s[a])) ++a;
    while (b > a && Space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

}  // namespace

std::string ConfigFile::Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

int ConfigFile::ParseInt(const std::string& s, int fallback) {
    if (s.empty()) return fallback;
    return int(std::strtol(s.c_str(), nullptr, 0));
}

const ConfigFile::Entry* ConfigFile::Section::Find(const std::string& key) const {
    for (const auto& e : Entries)
        if (e.Key == key) return &e;
    return nullptr;
}

const std::string& ConfigFile::Section::Get(const std::string& key) const {
    const Entry* e = Find(key);
    return e ? e->Value : kEmpty;
}

bool ConfigFile::Section::Has(const std::string& key) const {
    return Find(key) != nullptr;
}

int ConfigFile::Section::GetInt(const std::string& key, int fallback) const {
    const Entry* e = Find(key);
    if (!e || e->Value.empty()) return fallback;
    return ParseInt(e->Value, fallback);
}

bool ConfigFile::Section::GetBool(const std::string& key, bool fallback) const {
    const Entry* e = Find(key);
    if (!e) return fallback;
    if (e->Value == "true") return true;
    if (e->Value == "false") return false;
    return fallback;
}

float ConfigFile::Section::GetFloat(const std::string& key, float fallback) const {
    const Entry* e = Find(key);
    if (!e || e->Value.empty()) return fallback;
    return float(std::atof(e->Value.c_str()));
}

const std::vector<std::string>& ConfigFile::Section::GetList(
    const std::string& key) const {
    const Entry* e = Find(key);
    return e && e->IsList ? e->List : kNoList;
}

const ConfigFile::Section* ConfigFile::Find(const std::string& name) const {
    for (const auto& s : m_Sections)
        if (s.Name == name) return &s;
    return nullptr;
}

bool ConfigFile::Load(FilePack& pack, const std::string& path) {
    m_Sections.clear();
    auto file = pack.Open(path);
    if (!file) return false;
    const auto& bytes = file->Data();
    Parse(std::string(bytes.begin(), bytes.end()));
    return true;
}

void ConfigFile::Parse(const std::string& text) {
    m_Sections.clear();
    // Sections are collected by index rather than by pointer: push_back on the
    // vector would dangle a `Section*` the moment it reallocates.
    std::size_t open = std::string::npos;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string line =
            Trim(text.substr(pos, nl == std::string::npos ? nl : nl - pos));
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '}') {
            open = std::string::npos;
            continue;
        }
        const std::size_t brace = line.find('{');
        const std::size_t close = brace == std::string::npos
                                      ? std::string::npos
                                      : line.find('}', brace + 1);

        if (brace != std::string::npos && close == std::string::npos) {
            m_Sections.push_back(Section{Trim(line.substr(0, brace)), {}});
            open = m_Sections.size() - 1;
            continue;
        }
        if (open == std::string::npos) continue;

        Entry entry;
        if (brace != std::string::npos) {
            // `key { | a | b | }` -- pipes separate, and an empty list is
            // written `{| |}`, which yields one blank field that is dropped.
            entry.Key = Unquote(Trim(line.substr(0, brace)));
            entry.IsList = true;
            const std::string body = line.substr(brace + 1, close - brace - 1);
            std::size_t at = 0;
            while (at < body.size()) {
                const std::size_t bar = body.find('|', at);
                const std::string field =
                    Trim(body.substr(at, bar == std::string::npos ? bar : bar - at));
                if (!field.empty()) entry.List.push_back(Unquote(field));
                if (bar == std::string::npos) break;
                at = bar + 1;
            }
        } else {
            // `key value`, where the value is the rest of the line.
            const std::size_t sep = line.find_first_of(" \t");
            entry.Key = Unquote(sep == std::string::npos ? line
                                                         : line.substr(0, sep));
            if (sep != std::string::npos)
                entry.Value = Unquote(Trim(line.substr(sep + 1)));
        }
        m_Sections[open].Entries.push_back(std::move(entry));
    }
}

}  // namespace bb
