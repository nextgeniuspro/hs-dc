#include "game/SaveGame.h"

#include <cstring>

#include <zlib.h>

#include "game/SaveStream.hpp"
#include "game/Settings.h"
#include "platform/Storage.h"

namespace bb {
namespace {

constexpr char kMagic[4] = {'B', 'B', 'S', 'V'};
constexpr std::size_t kHeaderBytes = 16;
constexpr uint8_t kKindGame = 0;
constexpr uint8_t kKindSettings = 1;
constexpr uint8_t kFlagDeflated = 1;

// Ceilings for anything that sizes a container. These are not tuning knobs:
// they are the point past which a length has to be damage rather than data, so
// a rotted blob cannot talk the game into a huge allocation before the CRC
// would have caught it. Generous against the real numbers -- the largest
// shipped level is well under a hundred units on a grid of a few hundred
// cells.
constexpr std::size_t kMaxUnits = 4096;
constexpr std::size_t kMaxProperties = 4096;
constexpr std::size_t kMaxCells = 1 << 18;
constexpr std::size_t kMaxRuns = kMaxCells;
constexpr std::size_t kMaxTriggers = 4096;
constexpr std::size_t kMaxVariables = 512;
constexpr std::size_t kMaxLocations = 1024;
// The captain's log: the whole game has fewer than fifty pages to write, so
// anything near this is a rotted file rather than a long voyage.
constexpr std::size_t kMaxLogPages = 256;
// Four commanders with twenty-six perks each is the ceiling; anything near it
// is a rotted file.
constexpr std::size_t kMaxActivePerks = 128;
constexpr std::size_t kMaxName = 48;
constexpr std::size_t kMaxPath = 128;

// --- header ------------------------------------------------------------------

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 24));
}

uint32_t GetU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

// Wrap a payload: deflate it if that helps, checksum whatever ends up stored.
std::vector<uint8_t> Seal(const std::vector<uint8_t>& payload, uint8_t kind) {
    std::vector<uint8_t> stored;
    uint8_t flags = 0;
    if (!payload.empty()) {
        uLongf bound = compressBound(uLong(payload.size()));
        std::vector<uint8_t> packed(bound);
        const int rc = compress2(packed.data(), &bound, payload.data(),
                                 uLong(payload.size()), Z_BEST_COMPRESSION);
        // Only worth it if it actually shrank. Small payloads -- the settings,
        // a campaign between missions -- often deflate to more than they were.
        if (rc == Z_OK && bound < payload.size()) {
            packed.resize(bound);
            stored = std::move(packed);
            flags |= kFlagDeflated;
        }
    }
    if ((flags & kFlagDeflated) == 0) stored = payload;

    std::vector<uint8_t> out;
    out.reserve(kHeaderBytes + stored.size());
    out.insert(out.end(), kMagic, kMagic + 4);
    out.push_back(kSaveVersion);
    out.push_back(kind);
    out.push_back(flags);
    out.push_back(0);  // reserved
    PutU32(out, uint32_t(payload.size()));
    PutU32(out, uint32_t(crc32(0, stored.data(), uInt(stored.size()))));
    out.insert(out.end(), stored.begin(), stored.end());
    return out;
}

// Undo Seal. Every way this can fail is a corrupt slot, and the caller only
// needs to know that much.
bool Unseal(const std::vector<uint8_t>& blob, uint8_t kind,
            std::vector<uint8_t>& payload) {
    payload.clear();
    if (blob.size() < kHeaderBytes) return false;
    if (std::memcmp(blob.data(), kMagic, 4) != 0) return false;
    if (blob[4] != kSaveVersion || blob[5] != kind) return false;
    const uint8_t flags = blob[6];
    const uint32_t rawSize = GetU32(&blob[8]);
    const uint32_t wantCrc = GetU32(&blob[12]);
    const uint8_t* stored = blob.data() + kHeaderBytes;
    const std::size_t storedSize = blob.size() - kHeaderBytes;
    if (crc32(0, stored, uInt(storedSize)) != wantCrc) return false;
    // A length that would not fit a slot is damage; check before allocating.
    if (rawSize > kMaxCells * 8) return false;

    if ((flags & kFlagDeflated) != 0) {
        payload.resize(rawSize);
        uLongf got = rawSize;
        if (rawSize != 0 &&
            uncompress(payload.data(), &got, stored, uLong(storedSize)) != Z_OK)
            return false;
        if (got != rawSize) return false;
    } else {
        if (storedSize != rawSize) return false;
        payload.assign(stored, stored + storedSize);
    }
    return true;
}

// --- campaign -----------------------------------------------------------------

void PutTravel(SaveWriter& w, const TravelState& t) {
    w.Bool(t.Started);
    w.Int(t.ShipX);
    w.Int(t.ShipY);
    w.Uint(uint64_t(t.Heading & 0x3FF));
    for (const std::vector<int>* list : {&t.Done, &t.Open, &t.Spent}) {
        w.Uint(list->size());
        for (const int id : *list) w.Int(id);
    }
    for (const int n : t.Sailed) w.Uint(uint64_t(n < 0 ? 0 : n));
    uint8_t known = 0;
    for (int i = 0; i < TravelWorld::kAreas; ++i)
        if (t.Known[i]) known |= uint8_t(1u << i);
    w.U8(known);
}

void GetTravel(SaveReader& r, TravelState& t) {
    t.Started = r.Bool();
    t.ShipX = r.I32();
    t.ShipY = r.I32();
    t.Heading = int(r.Uint() & 0x3FF);
    for (std::vector<int>* list : {&t.Done, &t.Open, &t.Spent}) {
        const std::size_t n = r.Count(kMaxLocations);
        list->clear();
        list->reserve(n);
        for (std::size_t i = 0; i < n; ++i) list->push_back(r.I32());
    }
    for (int& n : t.Sailed) n = int(r.Uint());
    const uint8_t known = r.U8();
    for (int i = 0; i < TravelWorld::kAreas; ++i)
        t.Known[i] = (known & (1u << i)) != 0;
}

void PutCampaign(SaveWriter& w, const Campaign& c) {
    w.Str(c.Commander, 31);
    w.Uint(uint64_t(c.Colour < 0 ? 0 : c.Colour));
    // Thirty perk slots, one bit each: five bytes of varint rather than thirty
    // bytes of bool.
    uint64_t perks = 0;
    for (int i = 0; i < Campaign::kPerkSlots; ++i)
        if (std::size_t(i) < c.Perks.size() && c.Perks[std::size_t(i)])
            perks |= uint64_t(1) << i;
    w.Uint(perks);
    // Thirty-seven skills need more than a word, so they go as two.
    uint64_t skills = 0;
    for (int i = 0; i < Campaign::kSkillSlots; ++i)
        if (std::size_t(i) < c.Skills.size() && c.Skills[std::size_t(i)])
            skills |= uint64_t(1) << i;
    w.Uint(skills);
    w.Int(c.SkillPoints);
    // The captain's log, as the string ids of its pages. There are fewer than
    // fifty in the whole game, so the list is written whole.
    w.Uint(uint64_t(c.Log.size()));
    for (int id : c.Log) w.Int(id);
    PutTravel(w, c.Travel);
}

void GetCampaign(SaveReader& r, Campaign& c) {
    c.Commander = r.Str(31);
    c.Colour = int(r.Uint());
    const uint64_t perks = r.Uint();
    c.Perks.assign(Campaign::kPerkSlots, false);
    for (int i = 0; i < Campaign::kPerkSlots; ++i)
        c.Perks[std::size_t(i)] = (perks & (uint64_t(1) << i)) != 0;
    const uint64_t skills = r.Uint();
    c.Skills.assign(Campaign::kSkillSlots, false);
    for (int i = 0; i < Campaign::kSkillSlots; ++i)
        c.Skills[std::size_t(i)] = (skills & (uint64_t(1) << i)) != 0;
    c.SkillPoints = r.Int();
    c.Log.clear();
    const std::size_t pages = r.Count(kMaxLogPages);
    c.Log.reserve(pages);
    for (std::size_t i = 0; i < pages; ++i) c.Log.push_back(r.I32());
    GetTravel(r, c.Travel);
}

// --- battle -------------------------------------------------------------------

void PutPlayer(SaveWriter& w, const BattleField::Player& p) {
    w.Str(p.Name, 24);
    uint8_t flags = 0;
    if (p.Computer) flags |= 1;
    if (p.Alive) flags |= 2;
    if (p.Present) flags |= 4;
    w.U8(flags);
    w.Int(p.Cash);
    w.Uint(uint64_t(p.Team < 0 ? 0 : p.Team));
    // Which perks this commander brought, and what is left of this turn's
    // allowance for filling the bar.
    uint64_t carried = 0;
    for (int i = 0; i < kPerkCount; ++i)
        if (std::size_t(i) < p.Perks.size() && p.Perks[std::size_t(i)])
            carried |= uint64_t(1) << i;
    w.Uint(carried);
    w.Int(p.PerkAllowance);
    w.Uint(uint64_t(p.Colour < 0 ? 0 : p.Colour));
    w.Uint(uint64_t(p.PerkPoints < 0 ? 0 : p.PerkPoints));
    w.Int(p.Stats.UnitsBuilt);
    w.Int(p.Stats.UnitsDestroyed);
    w.Int(p.Stats.UnitsLost);
    w.Int(p.Stats.PropertiesCaptured);
    w.Int(p.Stats.PropertiesLost);
    w.Int(p.Stats.GoldCollected);
    // Load-bearing, not a scoreboard line: this is half of the rule that ends
    // a side when its last headquarters is taken. Drop it from the blob and a
    // reloaded battle forgets that one was ever captured.
    w.Int(p.Stats.HeadquartersLost);
}

void GetPlayer(SaveReader& r, BattleField::Player& p) {
    p.Name = r.Str(24);
    const uint8_t flags = r.U8();
    p.Computer = (flags & 1) != 0;
    p.Alive = (flags & 2) != 0;
    p.Present = (flags & 4) != 0;
    p.Cash = r.I32();
    p.Team = int(r.Uint());
    const uint64_t carried = r.Uint();
    p.Perks.assign(kPerkCount, false);
    for (int i = 0; i < kPerkCount; ++i)
        p.Perks[std::size_t(i)] = (carried & (uint64_t(1) << i)) != 0;
    p.PerkAllowance = r.I32();
    p.Colour = int(r.Uint());
    p.PerkPoints = int(r.Uint());
    p.Stats.UnitsBuilt = r.I32();
    p.Stats.UnitsDestroyed = r.I32();
    p.Stats.UnitsLost = r.I32();
    p.Stats.PropertiesCaptured = r.I32();
    p.Stats.PropertiesLost = r.I32();
    p.Stats.GoldCollected = r.I32();
    p.Stats.HeadquartersLost = r.I32();
}

void PutUnit(SaveWriter& w, const BattleField::Unit& u) {
    w.Uint(uint64_t(u.ID < 0 ? 0 : u.ID));
    w.Uint(uint64_t(u.Type < 0 ? 0 : u.Type));
    w.Uint(uint64_t(u.Owner < 0 ? 0 : u.Owner));
    w.Uint(uint64_t(u.X < 0 ? 0 : u.X));
    w.Uint(uint64_t(u.Y < 0 ? 0 : u.Y));
    w.Uint(uint64_t(u.HP < 0 ? 0 : u.HP));
    w.Uint(uint64_t(u.Movement < 0 ? 0 : u.Movement));
    w.Uint(uint64_t(u.Rations < 0 ? 0 : u.Rations));
    w.Uint(uint64_t(u.Ammo < 0 ? 0 : u.Ammo));
    w.Uint(uint64_t(u.Facing & 0xF));
    uint8_t flags = 0;
    if (u.Done) flags |= 1;
    if (u.Hidden) flags |= 2;
    if (u.Alive) flags |= 4;
    // Both matter to a reloaded battle: `moved` is what stops artillery
    // firing on the turn it repositioned, and `capturing` is the flag badge
    // over a unit halfway through taking a building.
    if (u.Moved) flags |= 8;
    if (u.Capturing) flags |= 16;
    w.U8(flags);
    w.Int(u.Carrier);
    w.Uint(u.Cargo.size());
    for (const int c : u.Cargo) w.Int(c);
}

void GetUnit(SaveReader& r, BattleField::Unit& u) {
    u.ID = int(r.Uint());
    u.Type = int(r.Uint());
    u.Owner = int(r.Uint());
    u.X = int(r.Uint());
    u.Y = int(r.Uint());
    u.HP = int(r.Uint());
    u.Movement = int(r.Uint());
    u.Rations = int(r.Uint());
    u.Ammo = int(r.Uint());
    u.Facing = int(r.Uint());
    const uint8_t flags = r.U8();
    u.Done = (flags & 1) != 0;
    u.Hidden = (flags & 2) != 0;
    u.Alive = (flags & 4) != 0;
    u.Moved = (flags & 8) != 0;
    u.Capturing = (flags & 16) != 0;
    u.Carrier = r.I32();
    const std::size_t n = r.Count(kMaxUnits);
    u.Cargo.clear();
    u.Cargo.reserve(n);
    for (std::size_t i = 0; i < n; ++i) u.Cargo.push_back(r.I32());
}

void PutProperty(SaveWriter& w, const BattleField::Property& p) {
    w.Uint(uint64_t(p.ID < 0 ? 0 : p.ID));
    w.Uint(uint64_t(p.Type < 0 ? 0 : p.Type));
    w.Uint(uint64_t(p.Owner < 0 ? 0 : p.Owner));
    w.Uint(uint64_t(p.X < 0 ? 0 : p.X));
    w.Uint(uint64_t(p.Y < 0 ? 0 : p.Y));
    w.Int(p.CapturePoints);
    w.Bool(p.BeingCaptured);
    w.Int(p.HP);
}

void GetProperty(SaveReader& r, BattleField::Property& p) {
    p.ID = int(r.Uint());
    p.Type = int(r.Uint());
    p.Owner = int(r.Uint());
    p.X = int(r.Uint());
    p.Y = int(r.Uint());
    p.CapturePoints = r.I32();
    p.BeingCaptured = r.Bool();
    p.HP = r.I32();
}

// Terrain hit points are one int per cell and almost all of them are the same
// number -- only breakable walls ever move off their maximum. Run-length
// encoding turns a few hundred cells into a handful of pairs, which is the
// difference between a battle save costing one VMU block and costing four.
void PutTerrain(SaveWriter& w, const std::vector<int>& hp) {
    w.Uint(hp.size());
    std::size_t at = 0;
    std::vector<std::pair<std::size_t, int>> runs;
    while (at < hp.size()) {
        std::size_t end = at + 1;
        while (end < hp.size() && hp[end] == hp[at]) ++end;
        runs.emplace_back(end - at, hp[at]);
        at = end;
    }
    w.Uint(runs.size());
    for (const auto& [len, value] : runs) {
        w.Uint(len);
        w.Int(value);
    }
}

bool GetTerrain(SaveReader& r, std::vector<int>& hp) {
    const std::size_t cells = r.Count(kMaxCells);
    const std::size_t runs = r.Count(kMaxRuns);
    hp.clear();
    hp.reserve(cells);
    for (std::size_t i = 0; i < runs; ++i) {
        const std::size_t len = r.Count(kMaxCells);
        const int value = r.I32();
        if (!r.Ok() || hp.size() + len > cells) return false;
        hp.insert(hp.end(), len, value);
    }
    // The runs have to add up to exactly the cell count they claimed.
    return r.Ok() && hp.size() == cells;
}

void PutTriggers(SaveWriter& w, const TriggerRunner::State& s) {
    w.Uint(s.Enabled.size());
    // One bit per trigger, low bit first.
    uint8_t bits = 0;
    for (std::size_t i = 0; i < s.Enabled.size(); ++i) {
        if (s.Enabled[i]) bits |= uint8_t(1u << (i % 8));
        if (i % 8 == 7) {
            w.U8(bits);
            bits = 0;
        }
    }
    if (s.Enabled.size() % 8 != 0) w.U8(bits);

    w.Uint(s.Variables.size());
    for (const auto& [id, value] : s.Variables) {
        w.Int(id);
        w.Int(value);
    }
    w.Uint(s.Winners);
    w.Int(s.TurnLimit);
}

void GetTriggers(SaveReader& r, TriggerRunner::State& s) {
    const std::size_t n = r.Count(kMaxTriggers);
    s.Enabled.assign(n, 0);
    for (std::size_t i = 0; i < n; i += 8) {
        const uint8_t bits = r.U8();
        for (std::size_t b = 0; b < 8 && i + b < n; ++b)
            s.Enabled[i + b] = (bits & (1u << b)) != 0 ? 1 : 0;
    }
    const std::size_t vars = r.Count(kMaxVariables);
    s.Variables.clear();
    s.Variables.reserve(vars);
    for (std::size_t i = 0; i < vars; ++i) {
        const int id = r.I32();
        const int value = r.I32();
        s.Variables.emplace_back(id, value);
    }
    s.Winners = uint32_t(r.Uint());
    s.TurnLimit = r.I32();
}

void PutBattle(SaveWriter& w, const SavedBattle& b) {
    w.Str(b.Level, kMaxPath);
    w.Str(b.Name, kMaxName);
    w.Str(b.Mission, 16);
    w.Uint(uint64_t(b.Viewer < 0 ? 0 : b.Viewer));
    w.Bool(b.Encounter);

    const BattleField::Snapshot& f = b.Field;
    w.Uint(uint64_t(f.Current < 0 ? 0 : f.Current));
    w.Uint(uint64_t(f.Round < 0 ? 0 : f.Round));
    w.Uint(uint64_t(f.Turn < 0 ? 0 : f.Turn));
    w.Uint(uint64_t(f.NextID < 0 ? 0 : f.NextID));
    w.Bool(f.Fog);
    w.Uint(uint64_t(f.ElapsedMs < 0 ? 0 : f.ElapsedMs));

    w.Uint(f.Players.size());
    for (const auto& p : f.Players) PutPlayer(w, p);
    w.Uint(f.Units.size());
    for (const auto& u : f.Units) PutUnit(w, u);
    w.Uint(f.Properties.size());
    for (const auto& p : f.Properties) PutProperty(w, p);
    PutTerrain(w, f.TerrainHP);
    // The perks still running. Four fields each, and there are never many.
    w.Uint(f.Perks.size());
    for (const auto& a : f.Perks) {
        w.Int(a.Perk);
        w.Int(a.Seat);
        w.Bool(a.Master);
        w.Int(a.Turns);
    }
    PutTriggers(w, b.Triggers);
}

bool GetBattle(SaveReader& r, SavedBattle& b) {
    b.Level = r.Str(kMaxPath);
    b.Name = r.Str(kMaxName);
    b.Mission = r.Str(16);
    b.Viewer = int(r.Uint());
    b.Encounter = r.Bool();

    BattleField::Snapshot& f = b.Field;
    f.Current = int(r.Uint());
    f.Round = int(r.Uint());
    f.Turn = int(r.Uint());
    f.NextID = int(r.Uint());
    f.Fog = r.Bool();
    f.ElapsedMs = int(r.Uint());

    const std::size_t players = r.Count(BattleField::kMaxPlayers + 1);
    f.Players.assign(players, BattleField::Player{});
    for (auto& p : f.Players) GetPlayer(r, p);
    const std::size_t units = r.Count(kMaxUnits);
    f.Units.assign(units, BattleField::Unit{});
    for (auto& u : f.Units) GetUnit(r, u);
    const std::size_t props = r.Count(kMaxProperties);
    f.Properties.assign(props, BattleField::Property{});
    for (auto& p : f.Properties) GetProperty(r, p);
    if (!GetTerrain(r, f.TerrainHP)) return false;
    const std::size_t perks = r.Count(kMaxActivePerks);
    f.Perks.clear();
    f.Perks.reserve(perks);
    for (std::size_t i = 0; i < perks; ++i) {
        BattleField::ActivePerk a;
        a.Perk = r.I32();
        a.Seat = r.I32();
        a.Master = r.Bool();
        a.Turns = r.I32();
        f.Perks.push_back(a);
    }
    GetTriggers(r, b.Triggers);
    return r.Ok();
}

}  // namespace

// --- public -------------------------------------------------------------------

const char* SlotName(SaveKind kind) {
    switch (kind) {
        case SaveKind::kTutorial: return "HSGAME_TUTO";
        case SaveKind::kHotSeat: return "HSGAME_HOTS";
        case SaveKind::kCampaign: break;
    }
    return "HSGAME_CAMP";
}

const char* SaveStatusText(SaveStatus s) {
    switch (s) {
        case SaveStatus::kOk: return "Game saved.";
        case SaveStatus::kNoStorage: return "There is nowhere to save to.";
        case SaveStatus::kTooBig: return "Not enough space to save.";
        case SaveStatus::kWriteFailed: return "Saving failed.";
        case SaveStatus::kMissing: return "There is no saved game.";
        case SaveStatus::kCorrupt: return "The saved game is damaged.";
    }
    return "Saving failed.";
}

std::vector<uint8_t> EncodeGame(const SavedGame& game) {
    SaveWriter w;
    PutCampaign(w, game.CampaignData);
    w.Bool(game.InBattle);
    if (game.InBattle) PutBattle(w, game.Battle);
    return Seal(w.Data(), kKindGame);
}

bool DecodeGame(const std::vector<uint8_t>& blob, SavedGame& out) {
    std::vector<uint8_t> payload;
    if (!Unseal(blob, kKindGame, payload)) return false;
    SaveReader r(payload);
    out = SavedGame{};
    GetCampaign(r, out.CampaignData);
    out.InBattle = r.Bool();
    if (out.InBattle && !GetBattle(r, out.Battle)) return false;
    // Trailing bytes mean the writer and the reader disagree about the layout,
    // which is exactly as bad as a checksum failure.
    return r.Complete();
}

SaveStatus WriteGame(Storage& store, SaveKind kind, const SavedGame& game) {
    const std::vector<uint8_t> blob = EncodeGame(game);
    if (blob.size() > store.Capacity()) return SaveStatus::kTooBig;
    if (!store.Write(SlotName(kind), blob)) return SaveStatus::kWriteFailed;
    return SaveStatus::kOk;
}

SaveStatus ReadGame(Storage& store, SaveKind kind, SavedGame& out) {
    std::vector<uint8_t> blob;
    if (!store.Read(SlotName(kind), blob)) return SaveStatus::kMissing;
    if (!DecodeGame(blob, out)) return SaveStatus::kCorrupt;
    return SaveStatus::kOk;
}

bool HasGame(Storage& store, SaveKind kind) {
    return store.Exists(SlotName(kind));
}

void EraseAll(Storage& store) {
    for (const SaveKind kind : {SaveKind::kCampaign, SaveKind::kTutorial,
                                SaveKind::kHotSeat})
        store.Remove(SlotName(kind));
    store.Remove(kSettingsSlot);
}

// --- settings -----------------------------------------------------------------

SaveStatus WriteSettings(Storage& store, const Settings& settings) {
    SaveWriter w;
    w.Bool(settings.SoundOn);
    w.Bool(settings.FightAnimation);
    w.Uint(uint64_t(settings.CutsceneVolume < 0 ? 0 : settings.CutsceneVolume));
    w.Uint(uint64_t(settings.MusicVolume < 0 ? 0 : settings.MusicVolume));
    w.Uint(uint64_t(settings.SfxVolume < 0 ? 0 : settings.SfxVolume));
    w.Uint(uint64_t(int(settings.CurrentLanguage)));
    w.Uint(uint64_t(Settings::kActionCount));
    for (int i = 0; i < Settings::kActionCount; ++i)
        w.U8(uint8_t(settings.Binding(static_cast<Settings::Action>(i))));
    // Appended after the binding table, which is where every optional field
    // has to go -- see the note in ReadSettings.
    w.Str(settings.Frame, Settings::kFrameIdMax);

    const std::vector<uint8_t> blob = Seal(w.Data(), kKindSettings);
    if (blob.size() > store.Capacity()) return SaveStatus::kTooBig;
    if (!store.Write(kSettingsSlot, blob)) return SaveStatus::kWriteFailed;
    return SaveStatus::kOk;
}

SaveStatus ReadSettings(Storage& store, Settings& out) {
    std::vector<uint8_t> blob;
    if (!store.Read(kSettingsSlot, blob)) return SaveStatus::kMissing;
    std::vector<uint8_t> payload;
    if (!Unseal(blob, kKindSettings, payload)) return SaveStatus::kCorrupt;

    SaveReader r(payload);
    Settings s;
    s.SoundOn = r.Bool();
    s.FightAnimation = r.Bool();
    s.CutsceneVolume = int(r.Uint());
    s.MusicVolume = int(r.Uint());
    s.SfxVolume = int(r.Uint());
    const int language = int(r.Uint());
    const std::size_t actions = r.Count(Settings::kActionCount);
    for (std::size_t i = 0; i < actions; ++i) {
        const uint8_t key = r.U8();
        if (key < uint8_t(Key::kCount))
            s.Bind(static_cast<Settings::Action>(i), static_cast<Key>(key));
    }
    // The device frame was added after the first settings files were written,
    // so an older one simply stops here. Reading it only if there is something
    // to read keeps those files loading with the default frame, instead of
    // running off the end, failing Complete() below, and throwing away the
    // player's volumes, language and key bindings along with it.
    //
    // That is the rule for this file from now on: an optional field goes after
    // the binding table and is guarded by Remaining(). Anything that changes
    // the meaning or the order of a field already here still needs the version
    // bumped -- and note that kSaveVersion is shared with saved games, so a
    // bump costs every campaign in the process.
    if (r.Remaining() > 0) s.Frame = r.Str(Settings::kFrameIdMax);
    if (!r.Complete()) return SaveStatus::kCorrupt;

    // Clamp rather than reject: a volume out of range is the one field a
    // player could plausibly have edited by hand, and losing every setting
    // over it would be a poor trade.
    const auto clamp = [](int v) {
        return v < 0 ? 0 : (v > Settings::kVolumeMax ? Settings::kVolumeMax : v);
    };
    s.CutsceneVolume = clamp(s.CutsceneVolume);
    s.MusicVolume = clamp(s.MusicVolume);
    s.SfxVolume = clamp(s.SfxVolume);
    if (language >= int(Language::kEn) && language <= int(Language::kSp))
        s.CurrentLanguage = static_cast<Language>(language);
    out = s;
    return SaveStatus::kOk;
}

}  // namespace bb
