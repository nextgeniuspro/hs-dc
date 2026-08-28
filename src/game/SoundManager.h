// SoundManager — the game's sound front end (engine resource slot 0xd6).
//
// The engine keeps sounds in numbered *banks*, each a text file listing the
// `.spc` files it contains, and plays them by (bank, index) rather than by
// path. `Data\Battle\sfx\menu.dat` is the UI bank, `Data\anim\snd\<name>.dat`
// is a cutscene's own bank, and there are banks for battle, ambience, units,
// perks and per-language voiceover.
//
// Bank file (the brace format, see ConfigFile.h):
//
//     sound0 {
//         filename Data\\Battle\\sfx\\menu\\menu_move.spc
//         music 0
//         loop 0
//         cache 0
//         destroy 0
//     }
//
// The `\\` is an escape for a single backslash. `music` marks a sound as
// belonging to the music mix rather than effects, and `cache`/`destroy`
// controlled when the device released the decoded data -- irrelevant here,
// where everything stays decoded, but they are parsed and kept so the
// distinction is visible.
//
// Reversed from 0x10081520 (bank load), 0x10081700 (entry), 0x1008140c
// (play) and 0x10081394 (stop).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/Mixer.h"
#include "game/SpcAudio.h"

namespace bb {

class FilePack;
class Host;
class Settings;

class SoundManager {
public:
    // Which of the five recorded armies a commander shouts for. The order is
    // the one 0x1005ec18 parses `<nationality>` into, and it is load-bearing
    // twice over: the engine's voice-over bank id is `nation + 6`, and each
    // nation's own file lists the *other four* in this same order (see
    // AttackVoice below).
    enum Nation : int {
        kNationEnglish = 0,
        kNationDutch = 1,
        kNationSpanish = 2,
        kNationFrench = 3,
        kNationPirates = 4,
        kNationCount = 5,
    };

    // Banks the port names. The ids are the port's own -- the original passes
    // a bank index around that only its own call sites agree on -- except that
    // the five voice-over banks are kept contiguous and in nation order,
    // because both the engine and every call site index them that way.
    enum Bank : int {
        kBankMenu = 0,
        kBankCutscene = 1,
        // The engine's bank 1: `Data\Battle\sfx\misc.dat`, the odds and
        // ends a battle makes -- the turn chimes, the cursor, and the noises
        // the actions make. Indices are positions in the file, and the file
        // starts at `sound2`, so they are two lower than the names suggest.
        kBankBattleMisc = 2,
        // `Data\\Battle\\sfx\\perks.dat`: one entry per perk, twenty-six of
        // them over five `perk_effect` samples. Loaded by the battle screen
        // when a battle starts and given back when it ends -- a commander with
        // no perks never pays for it.
        kBankPerks = 3,
        // The engine's bank 0: `Data\Battle\sfx\battle.dat`, which is what a
        // unit sounds like. Fifteen attack samples, three fight-animation hits
        // and a run of movement noises, all addressed through the table
        // `Data\Battle\sfx\units.dat` builds -- see LoadUnitSounds.
        kBankBattle = 4,
        // The engine's bank 5: `Data\Battle\sfx\travel.dat`, three looping
        // tracks for the chart and three stings for confirming a destination.
        // Held only while the chart is up; it is a megabyte of music.
        kBankTravel = 5,
        // The engine's banks 6..10: `Data\Battle\sfx\voiceover_<nation>.dat`,
        // what a commander's men shout. Addressed as
        // `kBankVoiceFirst + nation`, which is the engine's own arithmetic
        // (0x100875f8's `iVar12 + 6`). A megabyte each, so the port loads only
        // the nations actually seated -- the original loads all five up front
        // because it streams them off the card a block at a time instead of
        // decoding them into memory. See LoadVoiceBanks.
        kBankVoiceFirst = 6,
        kBankVoiceLast = kBankVoiceFirst + kNationCount - 1,
        kBankCount = kBankVoiceLast + 1,
    };

    struct Entry {
        std::string Filename;
        bool Music = false;
        bool Loop = false;
        bool Cache = false;
        bool Destroy = false;
    };

    // The UI bank, loaded by the startup path ("Menu: Start sound %d").
    static constexpr const char* kMenuBankPath = "Data\\Battle\\sfx\\menu.dat";
    static constexpr const char* kBattleMiscBankPath =
        "Data\\Battle\\sfx\\misc.dat";
    static constexpr const char* kPerkBankPath = "Data\\Battle\\sfx\\perks.dat";
    static constexpr const char* kBattleBankPath =
        "Data\\Battle\\sfx\\battle.dat";
    // Not a bank: a table of indices *into* the battle bank, one row per unit
    // type. 0x10050900 reads it into the 21x3 array 0x10050bb0 then looks
    // sounds up in, which is why the port keeps the same shape.
    static constexpr const char* kUnitSoundPath = "Data\\Battle\\sfx\\units.dat";
    static constexpr const char* kTravelBankPath =
        "Data\\Battle\\sfx\\travel.dat";
    // `voiceover_<nation>.dat`, by Nation.
    static const char* VoiceBankPath(int nation);
    static Bank VoiceBank(int nation) {
        return static_cast<Bank>(kBankVoiceFirst + nation);
    }

    // Menu bank indices, in the order menu.dat lists them. It runs to ten
    // entries, not four: past the blips are the game's *music* tracks, and
    // they are why the front end is not silent in the original.
    enum MenuSound : int {
        kSoundMove = 0,
        kSoundEnter = 1,
        kSoundCancel = 2,
        kSoundAdjust = 3,
        kSoundEnterMp = 4,   // and 5, the same file twice
        // `menu_music.spc`, `music 1 loop 1` -- the front end's theme, and the
        // one piece of music that is never deliberately stopped: a battle only
        // ducks it (see Duck).
        kSoundMenuMusic = 6,
        // `scene_music0.spc`, commented "ENd credits" in the file itself.
        kSoundCreditsMusic = 7,
        // The two the mission briefing board raises, commented "Mission
        // briefing" and "Mission briefing encounter". Both are the travel
        // bank's tracks under another name, so they cost nothing extra to hold
        // -- decoded sounds are shared by path.
        kSoundBriefingMusic = 8,
        kSoundBriefingEncounterMusic = 9,
    };

    // Travel bank indices, in the order travel.dat lists them: three looping
    // tracks, then the three stings a confirmed destination picks between.
    enum TravelSound : int {
        kTravelMusicFirst = 0,
        kTravelMusicCount = 3,
        kTravelMoveFirst = 3,
        kTravelMoveCount = 3,
    };

    // Voice-over bank layout, all forty-eight entries of it. Recovered from
    // the two call sites that index it: 0x100875f8's case 0xc plays
    // `unitType + 0x1b` when a unit is produced, and its case 0x15 plays
    // `rand()%6 + 9` when a capture completes.
    enum VoiceSound : int {
        kVoiceGenericAttackFirst = 0,   // generic_attack1..9
        kVoiceGenericAttackCount = 9,
        kVoiceCaptureFirst = 9,         // capture_vo1..6
        kVoiceCaptureCount = 6,
        kVoiceAttackFirst = 15,         // three per rival nation, in order
        kVoiceProducedFirst = 27,       // + unit type, 0..20
    };
    // 27 + 21 = 48, which is exactly what every one of the five files holds.
    // One of those forty-eight is filler and names a sample that is not in the
    // pak: the Spanish bank's entry 44 is unit type 17, the Cannon Tower,
    // which no building can produce, and it points at a Spaniard shouting at
    // Spaniards. The loader keeps an empty slot for it rather than retrying,
    // so the "not in the pak" line it prints once is expected.

    // Which of the twelve `attack_<nation>` lines a commander of `attacker`
    // uses against `defender`, or -1 if they share a nationality (in which
    // case the generic block is used instead). Each nation's file lists the
    // *other four* in nation order, so the rival's index has to skip the
    // attacker's own slot -- which is the `if (atk < def) n -= 3` at the tail
    // of 0x1006bb4c.
    static int AttackVoice(int attacker, int defender, int variant);

    explicit SoundManager(FilePack& pack) : m_Pack(pack) {}

    // Open the host's audio device. Returns false if it has none, in which
    // case every other call is a no-op and the game runs silent.
    bool Open(Host& host);
    bool Enabled() const { return m_Enabled; }

    // Read a bank definition and decode every sound it names. Whatever the
    // bank held before is given back first, so reading a new one costs its own
    // size rather than its own size on top of the last one's.
    bool LoadBank(Bank bank, const std::string& path);

    // Bytes of decoded audio currently held. What a 16 MB machine wants to
    // know, and the number that used to climb without limit.
    std::size_t Bytes() const;

    // Empty a bank and free the audio only it was holding.
    //
    // A cutscene's music is around a megabyte of decoded PCM, and the campaign
    // plays thirty-one of them. Without this they accumulate: the decoded
    // sounds are shared between banks and were never released, so by the first
    // battle a Dreamcast had several megabytes of finished cutscenes in memory
    // and nowhere to put the battle's own.
    void UnloadBank(Bank bank);
    std::size_t BankSize(Bank bank) const;
    const std::vector<Entry>& Entries(Bank bank) const;

    // Which of the three Sound-settings sliders scales a sound. The engine
    // has no such enum -- every call site simply passes the figure it wants --
    // but it amounts to this, and naming it keeps the choice at the call site
    // where the binary puts it. See ScaledVolume for why the bank file's own
    // `music` flag cannot be used for this.
    enum class Slider { kSfx, kMusic, kCutscene };

    // Play entry `index` of `bank`. `volume` is 0..256 as the cue files give
    // it; it is scaled by `slider` before it reaches the mixer (a cutscene
    // bank always follows the cut-scene slider). Returns a handle, or
    // kNoHandle.
    Mixer::Handle Play(Bank bank, int index, int volume = Mixer::kUnitVolume,
                       Slider slider = Slider::kSfx);
    void Stop(Mixer::Handle h);
    void StopAll();
    // Ramp whatever is playing entry `index` of `bank`, the way 0x10050d1c
    // addresses a fade: by bank and index rather than by voice. Reaching zero
    // stops it. This is how the cutaway's bed is taken off at the end of a
    // fight (0x1006ced4).
    void FadeSound(Bank bank, int index, int step, int target);

    // The misc bank's indices, counted the way the loader stores them -- the
    // file starts at `sound2` and skips `sound12`, so a name's number in the
    // file is not its index. All eighteen, with the call site that asks for
    // each; the ones without a call site named are the two the shipped binary
    // never plays.
    enum BattleSound : int {
        kSoundMissionSuccess = 0,   // 0x1008a770, the result board
        kSoundMissionFailed = 1,    // ditto
        kSoundTurnStartPlayer = 2,  // 0x100875f8 case 4, your turn
        kSoundTurnStartOther = 3,   // ditto, anybody else's
        kSoundPerkReady = 4,        // 0x100875f8 case 0x34
        kSoundSuperPerkReady = 5,   // ditto, the Master version
        kSoundEnemyPerk = 6,        // never played by the shipped binary
        kSoundEnemySuperPerk = 7,   // ditto -- and its .spc is not in the pak
        kSoundUnitSelect = 8,       // 0x1009b028
        kSoundCursor = 9,           // 0x1009b028 / 0x100cdcc0
        kSoundCursorUnit = 10,      // 0x10097cf8, cursor over one of your own
        kSoundInfo = 11,            // 0x1009b494, the info key
        kSoundSupply = 12,          // 0x100875f8 case 0x1b
        kSoundLoad = 13,            // case 0x18
        kSoundUnload = 14,          // case 0x19
        kSoundUnitDestroyed = 15,   // cases 0xd and 0x36
        // `generic_battle_good1.spc`, which the file itself labels "FIGHT ANIM
        // BG" -- the bed the cutaway plays over, started by 0x1006bb4c and
        // faded out by 0x1006ced4. (`Data\Battle\sfx\fightanim.dat` names the
        // same sample and is dead data: nothing in the binary loads it.)
        kSoundFightScene = 16,
        kSoundHurryUp = 17,         // 10 seconds left on a timed turn
    };

    // What a unit does, as `units.dat` names the three columns. The engine
    // asks for these by number (0x10050bb0's `type`), and the order is the
    // order the file writes them.
    enum UnitSound : int { kUnitAttack = 0, kUnitWalk = 1, kUnitDamaged = 2 };

    // --- music -------------------------------------------------------------
    //
    // The engine treats music as one voice with a latch beside it. Every menu
    // screen's constructor asks for the theme (0x10038f90's last argument ->
    // 0x10039500) and only the first ask actually starts it, because
    // 0x10039500 returns early when the latch is set; the attract reel and the
    // briefing board clear the latch by stopping or fading it, and then the
    // next screen brings it back. That is why one call per screen is right and
    // not a bug: it is how the original keeps the theme running across the
    // whole front end without any one screen owning it.
    //
    // Start `index` of `bank` as the music voice. Idempotent: asking for the
    // track that is already playing only re-asserts its volume, which is
    // exactly what the original does (0x1008140c sets the volume and then the
    // mixer refuses to re-add a voice it already holds).
    // `fadeInStep` non-zero starts the track silent and ramps it up at that
    // many volume steps per block, which is how the chart brings its music in
    // (0x100dcbdc plays at volume 0 and 0x100f1774 immediately ramps to the
    // music volume at 0x10 a block). Zero starts at full, as the front end's
    // theme does.
    Mixer::Handle StartMusic(Bank bank, int index, int fadeInStep = 0);
    void StopMusic();
    // Walk the music voice toward `target` at `step` per 2048 samples. A fade
    // to zero stops it, so this is also how the engine turns it off gently.
    void FadeMusic(int step, int target);
    bool MusicPlaying() const;
    // What the music slider comes to on the mixer's 0..256 scale. The engine's
    // own figure is `16 * slider` on a 0..9 slider; the port's slider is 0..5,
    // so this is the port's scale, not the original's number.
    int MusicVolume() const;

    // Duck everything that is playing to `volume` -- 0x10096b10, which walks
    // the four voices and sets each one's volume. A battle (0x1008b028) and a
    // mission briefing (0x1008a1a0) both open by ducking to an eighth of the
    // music volume, which is what leaves the front-end theme audible but out
    // of the way underneath.
    void Duck(int volume);
    // The eighth the two call sites above use, floored at 1 the way
    // 0x1008c2a8 floors it.
    int DuckedMusicVolume() const;

    // Read the five voice-over banks for `nations` (a Nation-indexed set of
    // flags) and give back any that are loaded and not wanted. Each is about a
    // megabyte decoded, so a battle takes only the ones its commanders need
    // and BattleScreen hands them back when it ends.
    void LoadVoiceBanks(const bool (&wanted)[kNationCount]);
    void UnloadVoiceBanks();

    // Convenience for the front end.
    void PlayMenu(MenuSound which);
    void PlayBattle(BattleSound which);
    // A shout from `nation`'s bank. Silent when that bank is not loaded, which
    // is the normal state outside a battle.
    Mixer::Handle PlayVoice(int nation, int index);
    // The noise perk `perk` makes. Silent if the bank is not loaded.
    void PlayPerk(int perk);

    // Read `units.dat` into the (unit type, UnitSound) -> battle bank index
    // table. The battle bank has to be loaded for the indices to mean
    // anything; loading it is the caller's business, as with every other bank.
    bool LoadUnitSounds(const std::string& path);
    // Play what unit type `type` sounds like doing `which`. Silent if either
    // the table or the bank is missing, or if this unit has no entry -- the
    // Swordsman has no movement noise, and `units.dat` says so with a 0 that
    // 0x10050bb0 reads as "index 0" but the port keeps as absent.
    Mixer::Handle PlayUnit(int type, UnitSound which,
                           int volume = Mixer::kUnitVolume);
    // The three `fight\fa_*_hit.spc` samples, which live in the battle bank
    // at fixed indices and are what the cutaway plays when a blow lands. The
    // engine reaches them through the same table (every unit's `damaged`
    // column names one of these three), so this is that column with the unit
    // taken out of it.
    enum FightHit : int { kHitMelee = 15, kHitCannon = 16, kHitProjectile = 17 };
    void PlayFightHit(FightHit which);

    // Where the volume sliders come from. Not owned; may be null, in which
    // case everything plays at full volume.
    void SetSettings(const Settings* settings) { m_Settings = settings; }

    // Render and hand blocks to the host until it has `aheadMs` buffered.
    // Call once a frame from any loop that wants sound to keep flowing.
    void Pump(Host& host, int aheadMs = kBufferMs);

    Mixer& GetMixer() { return m_Mixer; }

    // How far ahead to keep the host's queue. Two of the engine's own 64 ms
    // blocks, which survives a frame or two of jitter without adding audible
    // lag to a menu blip.
    static constexpr int kBufferMs = 128;

private:
    // Decode `path` if it is not already decoded, and take a claim on it.
    // Only loading a bank does this.
    const SpcSound* Acquire(const std::string& path);
    // The decoded sound, or null. Takes no claim: playing is not owning.
    const SpcSound* Find(const std::string& path) const;
    // Give back one claim on a decoded sound; the last one out frees it, after
    // stopping anything still playing from it.
    void ReleaseSound(const std::string& path);
    int ScaledVolume(Bank bank, Slider slider, int volume) const;

    // One decoded sound and the banks holding it. Counted for the same reason
    // the textures are: several banks name the same file, so "the bank that
    // loaded it is finished" is not the same as "nobody wants it".
    struct Cached {
        std::unique_ptr<SpcSound> Sound;
        int Claims = 0;
    };

    // -1 where the file gave no entry; only meaningful once loaded.
    static constexpr int kUnitSoundTypes = 21;
    int m_UnitSounds[kUnitSoundTypes][3] = {};
    bool m_UnitSoundsLoaded = false;

    FilePack& m_Pack;
    const Settings* m_Settings = nullptr;
    Mixer m_Mixer;
    bool m_Enabled = false;
    // The engine's latch, as a handle rather than a flag: which voice is the
    // music, and what it is playing so that asking for the same track twice is
    // free. See StartMusic.
    Mixer::Handle m_Music = Mixer::kNoHandle;
    int m_MusicBank = -1;
    int m_MusicIndex = -1;
    // A track that has been asked to fade out and is still on its way down.
    // The latch above is already clear -- fading the music is how the engine
    // says "somebody else may have this now" -- but the voice is still there,
    // so whoever takes over stops it rather than playing over the top. The
    // original does play over the top for the several seconds a ramp takes;
    // it gets away with it because the only thing that follows one is a
    // battle, which ducks everything anyway.
    Mixer::Handle m_Fading = Mixer::kNoHandle;
    std::vector<Entry> m_Banks[kBankCount];
    std::map<std::string, Cached> m_Sounds;
    std::vector<int16_t> m_Block;
};

}  // namespace bb
