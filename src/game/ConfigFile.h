// ConfigFile — the brace-and-keyword text format RedLynx used for data files.
//
// The engine has one generic parser (0x1009c494) that it points at every text
// asset: the subtitle scripts, the sound cue lists, the mission tables, the
// battle attribute `.ini`s, and the `.xml` files (which are only XML by
// extension -- the same parser reads them). The grammar is:
//
//     NAME {
//         key value
//         key { | v | v | v | }
//     }
//
// Values run to the end of the line, trimmed; a value wrapped in double quotes
// has them stripped, which is load-bearing rather than cosmetic -- the unit
// table writes `internalName "Swordsman"` and the damage matrix keys its rows
// `"Swordsman"`, and both have to resolve to the same `Swordsman` the engine's
// name->id table holds.
//
// A key whose value opens *and* closes a brace on one line is a list: the
// pipes are separators, and an empty list is written `{| |}`. Every list in
// the shipped data fits on its line, and no section nests inside another --
// checked across all 36 brace-format files rather than assumed.
//
// Section names are ignored by some readers; entries are consumed in file
// order and matched up positionally across sidecar files, which is how
// `sub\01-Intro.dat` (who says what) and `sub\01-Intro.sub` (when they say
// it) describe the same fourteen subtitles.
//
// Lines whose first non-space character is `#` are comments. Blank lines are
// skipped, and line endings may be CRLF or LF.
#pragma once

#include <string>
#include <vector>

namespace bb {

class FilePack;

class ConfigFile {
public:
    struct Entry {
        std::string Key;
        std::string Value;               // scalar text, empty for a list
        std::vector<std::string> List;   // set only when `IsList`
        bool IsList = false;
    };

    struct Section {
        std::string Name;
        std::vector<Entry> Entries;

        // Value for `key`, or an empty string when absent.
        const std::string& Get(const std::string& key) const;
        // Value for `key` parsed as an integer, or `fallback`. Accepts the
        // `0x...` the bitmask tables are written in.
        int GetInt(const std::string& key, int fallback = 0) const;
        // `true`/`false` as the attribute tables spell them; anything else
        // falls back.
        bool GetBool(const std::string& key, bool fallback = false) const;
        // Value for `key` parsed as a float, or `fallback`.
        float GetFloat(const std::string& key, float fallback = 0.0f) const;
        // The list for `key`, or an empty vector.
        const std::vector<std::string>& GetList(const std::string& key) const;
        bool Has(const std::string& key) const;
        const Entry* Find(const std::string& key) const;
    };

    // Read and parse `path` from the pak. Returns false if it is missing.
    bool Load(FilePack& pack, const std::string& path);

    // Parse text already in memory. Always succeeds; a malformed file simply
    // yields fewer sections, matching the original's forgiving parser.
    void Parse(const std::string& text);

    const std::vector<Section>& Sections() const { return m_Sections; }
    const Section* Find(const std::string& name) const;
    std::size_t Count() const { return m_Sections.size(); }

    // Shared with the value parser: strips surrounding quotes and reads the
    // `0x` prefix the bitmask tables use.
    static std::string Unquote(const std::string& s);
    static int ParseInt(const std::string& s, int fallback = 0);

private:
    std::vector<Section> m_Sections;
};

}  // namespace bb
