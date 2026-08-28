// VmuStorage — saved games on a Dreamcast memory card.
//
// Storage.h was written against this backend before it existed: twelve-
// character slot names, whole-blob reads and writes, a budget quoted in
// 512-byte blocks. So there is nothing to relax here and nothing to bolt on --
// this is the interface arriving at the hardware it was cut for.
//
// What the card adds, and this file handles:
//
//   * **A VMS header.** A file with none is invisible to the console's file
//     manager, so `fs_vmu`'s default header is set once at construction with
//     the game's name and description. KOS attaches it on close and strips it
//     on open, so everything below deals only in payload bytes.
//   * **512-byte granularity.** A file on a card is a whole number of blocks,
//     so a payload is written padded and comes back padded -- and a save's
//     CRC covers everything after its own header, so padding read back as
//     payload would fail every load. A four-byte magic and a length go in
//     front of the blob to give the exact size back, which costs eight bytes
//     of a 512-byte block and nothing in practice.
//   * **A card that isn't there.** Every operation checks; a missing or full
//     card fails the write and the game says so, which is the behaviour the
//     original has when its file server reports no room (0x1007834c).
//   * **A name and a picture per slot.** The console's own file manager -- the
//     screen a player reaches by booting with no disc in the drive -- lists
//     every file on the card by its VMS header's short description and draws
//     the header's 32x32 icon beside it. A file with a bare header is a blank
//     tile with the same name as the last one, which is how a player deletes
//     the wrong save. So each slot gets its own header: see VmsLabel.
//   * **A bus that is slower than the music.** Every operation here is seconds
//     of maple traffic and flash commits inside one call, and the game thread
//     is what feeds the sound stream, so a save used to leave the AICA looping
//     its last quarter-second for the whole of it. Each one runs under an
//     AudioKeepalive, which lends the mixer to a thread of its own while this
//     thread is asleep in the driver. See platform/AudioKeepalive.h.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "platform/Storage.h"

namespace bb {

// One VMS icon frame: 32x32 at four bits a pixel.
inline constexpr std::size_t kVmsIconBytes = 512;
// The fixed part of a VMS header, before any icon or eyecatch. `sizeof
// (vmu_hdr_t)` in KOS, which this must agree with -- there is a static_assert
// in the .cpp.
inline constexpr std::size_t kVmsHeaderBytes = 128;
// How many frames a VMS icon may animate over.
inline constexpr int kVmsMaxIconFrames = 3;

// What the console's file manager shows for one slot.
//
// The two descriptions are stored space-padded at sixteen and thirty-two
// characters, and KOS copies them in with `strlen` and no bound of its own
// (vmu_pkg.c), so anything longer would run into the next field. They are cut
// to fit here instead.
//
// The icon is the console's format, not one of ours: sixteen ARGB4444 palette
// entries and 512 bytes a frame, two pixels to a byte with the left one in the
// high nibble, rows top down. `tools/makeicons.py` draws them and writes
// exactly those bytes into `icons.pak`, so the Dreamcast does nothing to them
// but hand them to the BIOS -- the same bargain the device frames strike, where
// the picture arrives already in the shape the hardware wants.
struct VmsLabel {
    std::string DescShort;  // what the file manager lists
    std::string DescLong;   // what it shows when the file is opened
    uint16_t Palette[16] = {};
    std::vector<uint8_t> Bitmap;  // kVmsIconBytes per frame; empty for none
    int AnimSpeed = 0;

    int Frames() const {
        return static_cast<int>(Bitmap.size() / kVmsIconBytes);
    }

    // Take a blob as makeicons.py writes it: the sixteen palette entries, then
    // the frames. False -- and the icon left alone -- if it is not that shape.
    //
    // Defined here rather than beside the rest of the card so that the desktop
    // build, which has no KallistiOS to link against, can still test it. The
    // parsing is the part with anything to get wrong.
    bool SetIcon(const uint8_t* data, std::size_t size) {
        if (!data || size < sizeof(Palette)) return false;
        const std::size_t pixels = size - sizeof(Palette);
        if (pixels == 0 || pixels % kVmsIconBytes != 0) return false;
        if (pixels / kVmsIconBytes >
            static_cast<std::size_t>(kVmsMaxIconFrames))
            return false;
        // Little-endian on the card and little-endian here, but reading it a
        // byte at a time costs nothing and does not assume that.
        for (std::size_t i = 0; i < 16; ++i)
            Palette[i] = static_cast<uint16_t>(
                data[i * 2] | (static_cast<uint16_t>(data[i * 2 + 1]) << 8));
        Bitmap.assign(data + sizeof(Palette), data + size);
        return true;
    }

    // What this label puts in front of a payload. A card bills in whole
    // 512-byte blocks and a file is one run of them, so the icon is not free:
    // one frame turns a 128-byte header into a 640-byte one, which is a block
    // more per file.
    std::size_t HeaderBytes() const {
        return kVmsHeaderBytes +
               kVmsIconBytes * static_cast<std::size_t>(Frames());
    }
};

class VmuStorage : public Storage {
public:
    // The same eight-kilobyte budget the desktop backend uses: sixteen blocks
    // plus the header block a VMS spends, so a full game slot costs 17 of a
    // card's 200. Three saves and the settings come to about a quarter of a
    // card, which is a fair share for one game to take.
    static constexpr std::size_t kCapacity = 8 * 1024;

    // One header per slot name, plus whatever a slot with no entry of its own
    // falls back to.
    using Labels = std::map<std::string, VmsLabel>;

    // `dir` is the card's VFS directory, e.g. "/vmu/a1". `labels` may be null,
    // in which case every file gets the same bare header and the file manager
    // shows blank tiles; it is a borrowed pointer, and the caller -- KosHost,
    // which holds the table and rebuilds this object whenever the player
    // changes cards -- outlives every VmuStorage it makes.
    explicit VmuStorage(std::string dir, const Labels* labels = nullptr);

    using Storage::Write;

    bool Read(const std::string& name, std::vector<uint8_t>& out) override;
    bool Write(const std::string& name, const uint8_t* data,
               std::size_t size) override;
    bool Remove(const std::string& name) override;
    bool Exists(const std::string& name) override;
    std::vector<std::string> List() override;
    std::size_t Capacity() const override { return kCapacity; }
    std::size_t BlockSize() const override { return kVmuBlock; }
    // The fattest header this card will write, so the block figure the game
    // quotes is the one the console's file manager will show.
    std::size_t HeaderBytes() const override;
    // The magic and length this backend puts in front of every payload.
    std::size_t RecordBytes() const override;

    const std::string& Dir() const { return m_Dir; }

    // What to call, over and over, while a card operation has this thread
    // blocked -- KosHost::PumpAudio, which mixes a block and feeds the AICA.
    // The host sets it whenever it binds a card; unset means the operation
    // simply blocks, which is what it did before and what a silent build gets.
    using Keepalive = std::function<void()>;
    void SetKeepalive(Keepalive tick) { m_Keepalive = std::move(tick); }

    // The header a slot will be written with, for the tests and for anything
    // that wants to know what a file will look like. Null if the slot falls
    // back to the bare one.
    const VmsLabel* LabelFor(const std::string& name) const;

private:
    std::string PathFor(const std::string& name) const;
    // Attach `name`'s header to an open file. KOS copies everything out of the
    // package immediately, so nothing here has to outlive the call.
    void ApplyLabel(int fd, const std::string& name) const;

    std::string m_Dir;
    const Labels* m_Labels = nullptr;
    Keepalive m_Keepalive;
};

}  // namespace bb
