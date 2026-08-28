// Commanders — who is behind each seat in a battle.
//
// A seat is not just a colour: it is a named commander with a face, a
// paragraph of biography and a perk. The engine attaches one to every seat
// while the level is being loaded (0x1003be80, run from 0x1003bd34 right after
// the `.ndl` is parsed): it takes the *level's own player name*, strips the
// spaces out of it, and opens `Data\commanders\<Name>.xml`. "Black Barlow"
// becomes `Data\commanders\BlackBarlow.xml`. All twenty-one names the shipped
// levels use resolve to a file that is really in the pak.
//
// The file is a small XML document:
//
//     <commander>
//       <type>1</type>              the character id -- what every string
//       <name>Black Barlow</name>   base below is keyed by
//       <nationality>pirates</nationality>
//       <skills>...</skills>        per-unit-type modifiers, not read here
//       <perks><perk>Black Spot</perk></perks>
//       <desc>5201</desc>           unused: the engine derives it from <type>
//     </commander>
//
// **The type is the key to everything the info boards show.** 0x1005f9f8 reads
// the name back as string `5100 + type` and 0x1010105c the biography as
// `5200 + type`; the short form the player list uses is `5000 + type`. Only
// types 0..18 and 28 have any of those, which is exactly the guard both
// functions apply (`type <= 0x11 || type == 0x12 || type == 0x1c`).
//
// A commander also carries a one-line summary of what their skill table does
// to their army -- `5300 + type` normally and `5400 + type` in a team game,
// because the numbers differ. Type 13 is Player Stevenson, the seat the player
// holds, and its two slots are the literal `[player]` and an empty string: the
// board shows the player's *skill chart* there instead. See BattleInfo.h.
//
// **Perk names are matched against a table baked into the binary** (the
// thirty 32-byte strings at 0x10115825), not against the string table the
// menus read. Three of them are older names for perks that were renamed before
// release -- Plunder, Scorch Effect and Berserk are Plundering Blitz, Flaming
// Fandango and Fit of Rage -- so matching against the shipped strings would
// silently lose Bloodshot Mary's, Rodriguez's and Ezekiel Wiggins's perk.
// Player Stevenson's reads "Unknown" and matches nothing at all, which is
// right: the campaign hands the player's seat its own perks afterwards.
//
// Not read here: `<skills>`, the per-unit-type attack/defence/movement table
// the engine folds into combat (0x1005ea08). The port's rules do not have
// commander modifiers yet, and reading the numbers in without applying them
// would be worse than not reading them.
#pragma once

#include <string>
#include <vector>

namespace bb {

class FilePack;

struct CommanderDef {
    // The character id, `<type>`. -1 when the file named no type.
    int Type = -1;
    std::string Name;            // `<name>`, the file's own spelling
    // `<nationality>` as an index, or -1. This is what decides which of the
    // five recorded armies shouts for this commander: the engine reads it off
    // the commander (0x1005dcf0) and adds six to get the voice-over bank. See
    // SoundManager::Nation.
    int Nationality = -1;
    // By perk id, kPerkSlots wide. Empty when the file listed none the table
    // knows.
    std::vector<bool> Perks;
    bool Valid = false;
};

namespace commanders {

// `Data\commanders\<player name without spaces>.xml`, as 0x1003be80 builds it.
std::string PathFor(const std::string& playerName);

// Read one. False when the pak has no such entry or it is not a commander.
bool Load(FilePack& pack, const std::string& playerName, CommanderDef& out);

// `<nationality>` as an index, or -1 for a name the engine would not know
// either. The order is 0x1005ec18's, and it is also voice-over bank order.
int NationId(const std::string& name);

// String ids keyed by the character id, or 0 where this type has none.
// `Named` is the guard 0x1005f9f8 and 0x1010105c share.
bool Named(int type);
int NameString(int type);        // 5100 + type: "Black Barlow"
int ShortNameString(int type);   // 5000 + type: "Stevenson"
int BioString(int type);         // 5200 + type: the paragraph and the portrait
// 5300 + type, or 5400 + type in a team game: what this commander's skills do
// to their army. Zero for the seat the player holds (type 13), whose slots
// hold `[player]` and an empty string -- there the board shows skills instead.
int BonusString(int type, bool teamGame);

// A perk id for one of the names a commander file uses, or -1. The table is
// the binary's own, which is not the same list the menus show.
int PerkId(const std::string& name);

}  // namespace commanders
}  // namespace bb
