#include "game/SoundManager.h"

#include <cstdio>

#include "game/ConfigFile.h"
#include "game/Settings.h"
#include "game/SpcAudio.h"
#include "platform/Host.h"
#include "shim/Log.h"

namespace bb {
namespace {

const std::vector<SoundManager::Entry> kNoEntries;

// Bank files write a single backslash as `\\`. Nothing else is escaped.
std::string Unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        out.push_back(s[i]);
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\\') ++i;
    }
    return out;
}

// The five voice-over banks, in Nation order -- which is also bank order, so
// this table is indexed by both.
const char* const kVoiceBankPaths[SoundManager::kNationCount] = {
    "Data\\Battle\\sfx\\voiceover_english.dat",
    "Data\\Battle\\sfx\\voiceover_dutch.dat",
    "Data\\Battle\\sfx\\voiceover_spanish.dat",
    "Data\\Battle\\sfx\\voiceover_french.dat",
    "Data\\Battle\\sfx\\voiceover_pirates.dat",
};

}  // namespace

const char* SoundManager::VoiceBankPath(int nation) {
    if (nation < 0 || nation >= kNationCount) return kVoiceBankPaths[0];
    return kVoiceBankPaths[nation];
}

// 0x1006bb4c's tail. Each nation's file carries three lines for each of the
// four *other* nations, listed in nation order with its own slot left out, so
// the rival's block is its nation index minus one when it sorts after the
// speaker. Returns -1 when the two share a nationality: the engine picks from
// the generic block in that case rather than looking for a line that is not
// there.
int SoundManager::AttackVoice(int attacker, int defender, int variant) {
    if (attacker == defender) return -1;
    if (attacker < 0 || attacker >= kNationCount) return -1;
    if (defender < 0 || defender >= kNationCount) return -1;
    if (variant < 0) variant = 0;
    const int block = defender - (attacker < defender ? 1 : 0);
    return kVoiceAttackFirst + block * 3 + variant % 3;
}

bool SoundManager::Open(Host& host) {
    m_Enabled = host.AudioOpen(Mixer::kRate);
    if (m_Enabled)
        LogDebug("audio: 8 kHz mono stream open\n");
    else
        LogError("audio: no device, running silent\n");
    return m_Enabled;
}

bool SoundManager::LoadBank(Bank bank, const std::string& path) {
    if (bank < 0 || bank >= kBankCount) return false;
    // Before, not after: a cutscene bank is a megabyte of decoded music, and
    // reading the next one while the last one is still held needs two of them
    // at once -- which is the allocation that failed on a 16 MB console.
    UnloadBank(bank);
    std::vector<Entry>& out = m_Banks[bank];

    ConfigFile cfg;
    if (!cfg.Load(m_Pack, path)) {
        LogError("audio: bank '%s' not in the pak\n", path.c_str());
        return false;
    }
    int decoded = 0;
    for (const auto& section : cfg.Sections()) {
        Entry e;
        e.Filename = Unescape(section.Get("filename"));
        e.Music = section.GetInt("music") != 0;
        e.Loop = section.GetInt("loop") != 0;
        e.Cache = section.GetInt("cache") != 0;
        e.Destroy = section.GetInt("destroy") != 0;
        if (Acquire(e.Filename)) ++decoded;
        out.push_back(std::move(e));
    }
    // A sound that would not decode is worth a line of its own; a bank that
    // decoded whole is not.
    if (decoded != int(out.size()))
        LogError("audio: bank '%s' -- only %d of %zu sounds decoded\n",
                 path.c_str(), decoded, out.size());
    else
        LogDebug("audio: bank '%s' -- %zu sounds\n", path.c_str(), out.size());
    return !out.empty();
}

std::size_t SoundManager::BankSize(Bank bank) const {
    if (bank < 0 || bank >= kBankCount) return 0;
    return m_Banks[bank].size();
}

const std::vector<SoundManager::Entry>& SoundManager::Entries(Bank bank) const {
    if (bank < 0 || bank >= kBankCount) return kNoEntries;
    return m_Banks[bank];
}

const SpcSound* SoundManager::Acquire(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = m_Sounds.find(path);
    if (it != m_Sounds.end()) {
        ++it->second.Claims;
        return it->second.Sound->Valid() ? it->second.Sound.get() : nullptr;
    }

    auto sound = std::make_unique<SpcSound>();
    const bool ok = sound->Load(m_Pack, path);
    SpcSound* raw = sound.get();
    // A sound that would not decode is kept as an empty entry all the same, so
    // that a bank naming it does not try again on every load, and so that the
    // claim it took has something to give back.
    m_Sounds.emplace(path, Cached{std::move(sound), 1});
    return ok ? raw : nullptr;
}

const SpcSound* SoundManager::Find(const std::string& path) const {
    auto it = m_Sounds.find(path);
    if (it == m_Sounds.end() || !it->second.Sound) return nullptr;
    return it->second.Sound->Valid() ? it->second.Sound.get() : nullptr;
}

void SoundManager::ReleaseSound(const std::string& path) {
    auto it = m_Sounds.find(path);
    if (it == m_Sounds.end()) return;
    if (--it->second.Claims > 0) return;
    // A voice holds a bare pointer into the decoded samples, so anything still
    // playing this has to be silenced before the samples go.
    if (it->second.Sound) m_Mixer.StopSource(*it->second.Sound);
    m_Sounds.erase(it);
}

// What the loaded sounds cost, which since they are held compressed is about a
// quarter of what their samples would come to.
std::size_t SoundManager::Bytes() const {
    std::size_t bytes = 0;
    for (const auto& [path, cached] : m_Sounds)
        if (cached.Sound) bytes += cached.Sound->Bytes();
    return bytes;
}

void SoundManager::UnloadBank(Bank bank) {
    if (bank < 0 || bank >= kBankCount || m_Banks[bank].empty()) return;
    // The music voice points into this bank's samples; ReleaseSound will
    // silence it, so drop the latch first or the next screen will believe the
    // theme is still up and never restart it.
    if (m_MusicBank == bank) StopMusic();
    const std::size_t before = Bytes();
    for (const Entry& e : m_Banks[bank]) ReleaseSound(e.Filename);
    m_Banks[bank].clear();
    LogDebug("audio: bank %d released %u KB\n", int(bank),
             unsigned((before - Bytes()) / 1024));
}

// The cue files give a volume in the mixer's own 0..256; the player's sliders
// then scale it. Sound settings has three of them -- cut scene, music and sfx
// (string ids 1530-1532).
//
// CORRECTED against the binary: the bank file's own `music` flag does NOT pick
// the slider, and using it was wrong. `battle.dat` sets `music 1` on all
// thirty-five unit attack and movement samples, and misc.dat sets it on the
// supply and load noises, yet every one of those call sites passes
// 0x10086950's figure -- `16 * settings[8]`, the *sfx* slider. The flag is a
// property of the sample (it tells the engine's cache which pool it belongs
// in), not of the mix. Only the call site knows which slider applies, so the
// caller says: `slider` is what the engine passes explicitly, and a cutscene's
// bank overrides it because its cues are the cut-scene slider by definition.
int SoundManager::ScaledVolume(Bank bank, Slider slider, int volume) const {
    if (!m_Settings) return volume;
    if (!m_Settings->SoundOn) return 0;
    if (bank == kBankCutscene) slider = Slider::kCutscene;
    int level;
    switch (slider) {
        case Slider::kMusic: level = m_Settings->MusicVolume; break;
        case Slider::kCutscene: level = m_Settings->CutsceneVolume; break;
        default: level = m_Settings->SfxVolume; break;
    }
    return volume * level / Settings::kVolumeMax;
}

Mixer::Handle SoundManager::Play(Bank bank, int index, int volume,
                                 Slider slider) {
    if (!m_Enabled || bank < 0 || bank >= kBankCount) return Mixer::kNoHandle;
    const std::vector<Entry>& entries = m_Banks[bank];
    if (index < 0 || index >= static_cast<int>(entries.size()))
        return Mixer::kNoHandle;

    const Entry& e = entries[index];
    // Look it up, do not claim it. Playing a sound is not owning one: the bank
    // holds the claim for as long as the bank is loaded, and a claim taken
    // here would never be given back -- which pinned every sound the player
    // ever heard, cutscene music included, for the rest of the run.
    const SpcSound* sound = Find(e.Filename);
    if (!sound) return Mixer::kNoHandle;
    const int scaled = ScaledVolume(bank, slider, volume);
    if (scaled <= 0) return Mixer::kNoHandle;
    return m_Mixer.Play(*sound, scaled, e.Loop);
}

void SoundManager::Stop(Mixer::Handle h) {
    if (h == m_Music) {
        m_Music = Mixer::kNoHandle;
        m_MusicBank = m_MusicIndex = -1;
    }
    if (h == m_Fading) m_Fading = Mixer::kNoHandle;
    m_Mixer.Stop(h);
}

void SoundManager::FadeSound(Bank bank, int index, int step, int target) {
    if (bank < 0 || bank >= kBankCount) return;
    const std::vector<Entry>& entries = m_Banks[bank];
    if (index < 0 || index >= static_cast<int>(entries.size())) return;
    if (const SpcSound* sound = Find(entries[index].Filename))
        m_Mixer.FadeSource(*sound, step, target);
}

void SoundManager::StopAll() {
    m_Music = m_Fading = Mixer::kNoHandle;
    m_MusicBank = m_MusicIndex = -1;
    m_Mixer.StopAll();
}

// --- music -----------------------------------------------------------------

bool SoundManager::MusicPlaying() const {
    return m_Music != Mixer::kNoHandle && m_Mixer.Playing(m_Music);
}

int SoundManager::MusicVolume() const {
    if (!m_Settings) return Mixer::kUnitVolume;
    if (!m_Settings->SoundOn) return 0;
    return Mixer::kUnitVolume * m_Settings->MusicVolume / Settings::kVolumeMax;
}

// 0x1008c2a8(vol / 8, 1) -- an eighth, but never all the way to nothing,
// because a voice that reaches zero volume stops.
int SoundManager::DuckedMusicVolume() const {
    const int v = MusicVolume() / 8;
    return v < 1 ? 1 : v;
}

// 0x10039500, with the latch expressed as "is the music voice still ours and
// still playing the same thing". Asking for the track that is already up costs
// a volume write and nothing else, which is what lets every screen ask.
Mixer::Handle SoundManager::StartMusic(Bank bank, int index, int fadeInStep) {
    if (!m_Enabled) return Mixer::kNoHandle;
    if (bank < 0 || bank >= kBankCount) return Mixer::kNoHandle;
    const std::vector<Entry>& entries = m_Banks[bank];
    if (index < 0 || index >= static_cast<int>(entries.size()))
        return Mixer::kNoHandle;

    if (MusicPlaying() && m_MusicBank == bank && m_MusicIndex == index) {
        const int scaled =
            ScaledVolume(bank, Slider::kMusic, Mixer::kUnitVolume);
        if (scaled <= 0) {
            StopMusic();
            return Mixer::kNoHandle;
        }
        // Cancels any fade that was running, exactly as 0x100ac708 does.
        m_Mixer.SetVolume(m_Music, scaled);
        return m_Music;
    }

    StopMusic();
    const int target = ScaledVolume(bank, Slider::kMusic, Mixer::kUnitVolume);
    if (target <= 0) return Mixer::kNoHandle;

    Mixer::Handle h;
    if (fadeInStep > 0) {
        // Start silent and climb. Play() refuses a zero volume, so this goes
        // round it -- the voice is meant to exist at zero for exactly one
        // block, which is what the ramp then lifts.
        const SpcSound* sound = Find(entries[index].Filename);
        if (!sound) return Mixer::kNoHandle;
        h = m_Mixer.Play(*sound, 0, entries[index].Loop);
        if (h != Mixer::kNoHandle) m_Mixer.Fade(h, fadeInStep, target);
    } else {
        h = Play(bank, index, Mixer::kUnitVolume, Slider::kMusic);
    }
    if (h == Mixer::kNoHandle) return h;
    m_Music = h;
    m_MusicBank = bank;
    m_MusicIndex = index;
    LogDebug("audio: music '%s' (bank %d entry %d) at %d/%d\n",
             entries[index].Filename.c_str(), int(bank), index, target,
             Mixer::kUnitVolume);
    return h;
}

void SoundManager::StopMusic() {
    if (m_Music != Mixer::kNoHandle) m_Mixer.Stop(m_Music);
    // And anything still on its way out, so a new track never plays over the
    // tail of the last one.
    if (m_Fading != Mixer::kNoHandle) m_Mixer.Stop(m_Fading);
    m_Music = m_Fading = Mixer::kNoHandle;
    m_MusicBank = m_MusicIndex = -1;
}

void SoundManager::FadeMusic(int step, int target) {
    if (!MusicPlaying()) return;
    m_Mixer.Fade(m_Music, step, target);
    // A fade to silence is a stop: the voice frees itself when it arrives, and
    // the latch has to come off with it or the next screen will think the
    // theme is still up. The engine does the same thing in 0x10050d1c, which
    // clears the latch whenever it ramps the menu theme.
    if (target <= 0) {
        m_Fading = m_Music;
        m_Music = Mixer::kNoHandle;
        m_MusicBank = m_MusicIndex = -1;
    }
}

void SoundManager::Duck(int volume) { m_Mixer.SetAllVolumes(volume); }

// --- voice-over ------------------------------------------------------------

void SoundManager::LoadVoiceBanks(const bool (&wanted)[kNationCount]) {
    for (int n = 0; n < kNationCount; ++n) {
        const Bank bank = VoiceBank(n);
        const bool have = !m_Banks[bank].empty();
        if (wanted[n] && !have)
            LoadBank(bank, VoiceBankPath(n));
        else if (!wanted[n] && have)
            UnloadBank(bank);
    }
}

void SoundManager::UnloadVoiceBanks() {
    for (int n = 0; n < kNationCount; ++n) UnloadBank(VoiceBank(n));
}

void SoundManager::PlayMenu(MenuSound which) {
    Play(kBankMenu, static_cast<int>(which));
}

void SoundManager::PlayBattle(BattleSound which) {
    Play(kBankBattleMisc, static_cast<int>(which));
}

Mixer::Handle SoundManager::PlayVoice(int nation, int index) {
    if (nation < 0 || nation >= kNationCount) return Mixer::kNoHandle;
    return Play(VoiceBank(nation), index);
}

// `units.dat` is a row per unit -- `u7 { id 7  attack 0  walk 18  damaged 16 }`
// -- naming positions in the battle bank. The engine flattens it into a 21x3
// array of indices (0x10050900) and every unit noise in the game goes through
// it, which is why the section name is ignored and the `id` inside is what
// says which unit the row is about.
bool SoundManager::LoadUnitSounds(const std::string& path) {
    for (auto& row : m_UnitSounds) row[0] = row[1] = row[2] = -1;
    m_UnitSoundsLoaded = false;
    ConfigFile cfg;
    if (!cfg.Load(m_Pack, path)) {
        LogError("audio: unit sound table '%s' not in the pak\n", path.c_str());
        return false;
    }
    int rows = 0;
    for (const auto& section : cfg.Sections()) {
        const int id = section.GetInt("id", -1);
        if (id < 0 || id >= kUnitSoundTypes) continue;
        m_UnitSounds[id][kUnitAttack] = section.GetInt("attack", -1);
        m_UnitSounds[id][kUnitWalk] = section.GetInt("walk", -1);
        m_UnitSounds[id][kUnitDamaged] = section.GetInt("damaged", -1);
        ++rows;
    }
    m_UnitSoundsLoaded = rows > 0;
    if (rows != kUnitSoundTypes)
        LogError("audio: unit sound table -- %d of %d unit types\n", rows,
                 kUnitSoundTypes);
    else
        LogDebug("audio: unit sound table -- all %d unit types\n", rows);
    return m_UnitSoundsLoaded;
}

Mixer::Handle SoundManager::PlayUnit(int type, UnitSound which, int volume) {
    if (!m_UnitSoundsLoaded || type < 0 || type >= kUnitSoundTypes)
        return Mixer::kNoHandle;
    const int index = m_UnitSounds[type][static_cast<int>(which)];
    if (index < 0) return Mixer::kNoHandle;
    return Play(kBankBattle, index, volume);
}

void SoundManager::PlayFightHit(FightHit which) {
    Play(kBankBattle, static_cast<int>(which));
}

// perks.dat lists its entries as `sound0`..`sound25`, one per perk, so the
// perk id is the index.
void SoundManager::PlayPerk(int perk) {
    if (perk < 0) return;
    Play(kBankPerks, perk);
}

void SoundManager::Pump(Host& host, int aheadMs) {
    if (!m_Enabled) return;
    const int want = Mixer::kRate * aheadMs / 1000;
    // Cap the catch-up so a long stall cannot make one frame render minutes
    // of audio; the queue simply restarts from where it is.
    int budget = want;
    while (host.AudioQueued() < want && budget > 0) {
        m_Block.resize(Mixer::kBlockSamples);
        m_Mixer.Render(m_Block.data(), Mixer::kBlockSamples);
        host.AudioQueue(m_Block.data(), Mixer::kBlockSamples);
        budget -= Mixer::kBlockSamples;
    }
}

}  // namespace bb
