// Strings — the localised text table (loader at 0x100b97a0).
//
// The engine picks `Data\localization\m_Strings{EN,FR,IT,GE,SP}.txt` by the
// same language index that selects the Nokia logo, and every piece of UI text
// is looked up by a numeric id. The file is plain text, one entry per CRLF
// line: `<id>\t<text>`, with `#`-prefixed comment lines throughout. English
// has 2659 entries.
//
// The text is Latin-1, not UTF-8, and stays that way: the fonts have a frame
// per byte value from 0x21 to 0xFF, so bytes index glyphs directly. "N-Gage\x99
// Arena" renders its trademark sign because 0x99 is a real glyph.
#pragma once

#include <map>
#include <string>

#include "game/Boot.h"  // Language

namespace bb {

class FilePack;

class Strings {
public:
    // Load the table for `lang`. Returns false if the file is missing.
    bool Load(FilePack& pack, Language lang);

    // The text for `id`, or an empty string if there is no such entry.
    const std::string& Get(int id) const;

    bool Empty() const { return m_Entries.empty(); }
    std::size_t Count() const { return m_Entries.size(); }

    // "Data\localization\strings_XX.txt" for `lang`.
    static std::string PathFor(Language lang);

private:
    std::map<int, std::string> m_Entries;
};

}  // namespace bb
