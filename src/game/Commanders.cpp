#include "game/Commanders.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "game/Campaign.h"
#include "game/FilePack.hpp"

namespace bb {
namespace {

// String id bases, keyed by the character id (`<type>`).
constexpr int kShortNameBase = 5000;
constexpr int kNameBase = 5100;
constexpr int kBioBase = 5200;
constexpr int kBonusBase = 5300;
constexpr int kBonusTeamBase = 5400;
// The highest type with a full set of strings, and the one loose id above it:
// 18 is Van den Horn and 28 the Anonymous Pirate, which several levels seat.
constexpr int kLastNamedType = 18;
constexpr int kAnonymousType = 28;
// Player Stevenson: the seat the player holds. Its bonus strings are the
// `[player]` placeholder and an empty line, which is the engine's way of
// saying "show the skill chart here instead".
constexpr int kPlayerType = 13;

// The perk names a commander file may use, indexed by perk id -- the thirty
// 32-byte strings at 0x10115825, which 0x1005eccc compares each `<perk>`
// against in order. Entries 1, 3, 4 and 23 are the names these perks had
// before release; the menus show the string table's spelling instead.
const char* const kPerkNames[Campaign::kPerkCount] = {
    "Horse Whisperer", "Plunder", "Supreme Spy-Glasses", "Scorch Effect",
    "Berserk", "Technology Break", "Guts of Gold", "Superior Supply",
    "Vermin Infestation", "Poison", "Doctor's Orders", "Enforced Action",
    "Rum Delivery", "Black Spot", "Gambling", "Smooth Sailing",
    "Tactical Genius", "Basker Confusion", "Super Soldiers", "For the Cause",
    "Hoard Up", "Golden Age", "Supreme Logistics", "Blazing Rowing Boats",
    "Keen Sight", "Second Wind",
};

// The one piece of XML handling this needs: the text between <tag> and </tag>.
// The commander files are machine-written and flat -- no attributes on the
// nodes read here, no nesting past <perks> -- so a scan for the tags beats
// pulling in a parser for four fields.
std::string Node(const std::string& xml, const std::string& tag,
                 std::size_t& at) {
    const std::string open = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    const std::size_t a = xml.find(open, at);
    if (a == std::string::npos) return {};
    const std::size_t b = xml.find(close, a + open.size());
    if (b == std::string::npos) return {};
    at = b + close.size();
    std::string text = xml.substr(a + open.size(), b - a - open.size());
    // The files indent their contents; the perk names must match exactly.
    std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string Node(const std::string& xml, const std::string& tag) {
    std::size_t at = 0;
    return Node(xml, tag, at);
}

}  // namespace

namespace commanders {

// `<nationality>`, in the order 0x1005ec18 tests the five literals -- which is
// also the order of the voice-over banks, so the answer doubles as
// `SoundManager::Nation`. -1 for a file that names none, or names one the
// engine would not recognise either.
int NationId(const std::string& name) {
    static const char* const kNations[] = {"english", "dutch", "spanish",
                                           "french", "pirates"};
    for (int i = 0; i < int(sizeof(kNations) / sizeof(kNations[0])); ++i)
        if (name == kNations[i]) return i;
    return -1;
}

std::string PathFor(const std::string& playerName) {
    std::string bare;
    for (const char c : playerName)
        if (c != ' ') bare.push_back(c);
    if (bare.empty()) return {};
    return "Data\\commanders\\" + bare + ".xml";
}

bool Load(FilePack& pack, const std::string& playerName, CommanderDef& out) {
    out = CommanderDef{};
    const std::string path = PathFor(playerName);
    if (path.empty()) return false;
    std::optional<FileInputStream> in = pack.Open(path);
    if (!in) return false;
    std::string xml(in->Size(), '\0');
    if (in->Size() != 0) in->Read(&xml[0], in->Size());

    const std::string type = Node(xml, "type");
    if (type.empty()) return false;
    out.Type = std::atoi(type.c_str());
    out.Name = Node(xml, "name");
    out.Nationality = NationId(Node(xml, "nationality"));

    // <perks> is the only nesting: walk the <perk> nodes inside it and stop
    // at its close, so a later tag with the same name could not be picked up.
    const std::size_t perksAt = xml.find("<perks>");
    const std::size_t perksEnd = xml.find("</perks>");
    if (perksAt != std::string::npos && perksEnd != std::string::npos) {
        out.Perks.assign(Campaign::kPerkSlots, false);
        std::size_t at = perksAt;
        for (;;) {
            const std::string name = Node(xml, "perk", at);
            if (name.empty() || at > perksEnd) break;
            const int id = PerkId(name);
            if (id >= 0) out.Perks[std::size_t(id)] = true;
        }
    }
    out.Valid = true;
    return true;
}

bool Named(int type) {
    return (type >= 0 && type <= kLastNamedType) || type == kAnonymousType;
}

int NameString(int type) { return Named(type) ? kNameBase + type : 0; }
int ShortNameString(int type) { return Named(type) ? kShortNameBase + type : 0; }
int BioString(int type) { return Named(type) ? kBioBase + type : 0; }

int BonusString(int type, bool teamGame) {
    // Only the eighteen commanders with a skill table have one, and not the
    // player's own seat -- 0x1010105c checks the id against both ranges and
    // against the two placeholder slots by hand.
    if (type < 0 || type > kLastNamedType || type == kPlayerType) return 0;
    return (teamGame ? kBonusTeamBase : kBonusBase) + type;
}

int PerkId(const std::string& name) {
    for (int i = 0; i < Campaign::kPerkCount; ++i)
        if (name == kPerkNames[i]) return i;
    return -1;
}

}  // namespace commanders
}  // namespace bb
