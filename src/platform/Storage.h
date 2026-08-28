// Storage — where saved games live. One named slot, one blob of bytes.
//
// The original has this seam already. Everything that persists goes through a
// file-server object hanging off the engine at +0x41c: 0x2c opens a file by
// name and 0x4c reports the free space, and the save code checks the second
// before doing the first (0x1007834c refuses below 32000 bytes and puts up a
// dialog instead). Nothing else in the game touches a filesystem. This is the
// same seam, narrowed to what saving actually needs.
//
// **It is deliberately not a filesystem.** No open, no seek, no append, no
// directories -- a slot name goes in and a whole blob comes back, or a whole
// blob goes in and replaces what was there. That is not a simplification for
// its own sake: it is the shape a Dreamcast VMU can honour, and the shape
// stdio can honour trivially. Anything richer would be a desktop-only API that
// the memory card could not implement.
//
// **What the VMU imposes**, since that is the next backend:
//
//   * 128 KB of flash as 256 blocks of 512 bytes, of which 200 are user data.
//     A VMS file carries a header -- description and icon -- in front of its
//     payload, and the two are rounded up to blocks *separately*, because the
//     VMU filesystem pads the payload out to whole blocks before the header is
//     prepended to it (fs_vmu.c: `data_len = fh->filesize * 512`). So a payload
//     of n bytes costs `ceil(header / 512) + ceil(n / 512)`, and the header is
//     128 bytes plus 512 for every frame of its 32x32 icon -- which is why
//     `HeaderBytes` is virtual, and why a backend that draws an icon really
//     does cost a block more per file than one that does not. Saving is billed
//     in blocks, never in bytes, which is why `BlocksFor` exists and why the UI
//     quotes blocks.
//   * Filenames are twelve characters, and the console's own file manager only
//     renders uppercase, digits and underscore legibly. `ValidSlotName`
//     enforces exactly that, on every backend, so a name that works on the
//     desktop cannot fail once it reaches a card.
//   * A write is whole-file: the card cannot be seeked into and a partial
//     write leaves a corrupt file. Hence `Write` takes the entire blob, and
//     backends are expected to make the replacement atomic.
//   * Flash wears out and writing is slow. Saves are taken at checkpoints, not
//     continuously, and the save data is kept small enough to be a handful of
//     blocks rather than tens.
//
// So the budget is real and the game asks about it: `Capacity` is the largest
// blob this backend will accept, and the save code refuses -- with the same
// kind of message the original shows -- rather than writing something that
// cannot fit.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bb {

class Storage {
public:
    virtual ~Storage() = default;

    // The Dreamcast's twelve-character limit, applied everywhere so a slot
    // name that works on one backend works on all of them.
    static constexpr std::size_t kMaxSlotName = 12;
    // A VMU block. Backends with no block structure report 1 and every blob
    // costs its own size.
    static constexpr std::size_t kVmuBlock = 512;

    // Uppercase letters, digits and underscore, 1..kMaxSlotName of them.
    static bool ValidSlotName(const std::string& name);

    // Read a slot whole. False if it is missing or unreadable; `out` is
    // cleared either way, so a caller cannot mistake a failure for empty data.
    virtual bool Read(const std::string& name, std::vector<uint8_t>& out) = 0;

    // Replace a slot whole. Backends make this atomic: either the old blob
    // survives intact or the new one does, never a half-written mixture.
    virtual bool Write(const std::string& name, const uint8_t* data,
                       std::size_t size) = 0;

    virtual bool Remove(const std::string& name) = 0;
    virtual bool Exists(const std::string& name) = 0;

    // Every slot this backend currently holds, sorted. Used by "Erase game
    // data" and by the tests.
    virtual std::vector<std::string> List() = 0;

    // The largest blob `Write` will accept. The save code checks this before
    // it serialises anything and reports a shortage rather than truncating.
    virtual std::size_t Capacity() const = 0;

    // The allocation unit. 512 on a memory card, 1 where size is size.
    virtual std::size_t BlockSize() const { return 1; }

    // What this backend puts in front of a payload. The default is the fixed
    // part of a VMS header with no icon on it, which is what a backend that is
    // only pretending to be a memory card -- MemoryStorage, in the tests --
    // should be billed for.
    static constexpr std::size_t kVmsHeader = 128;
    virtual std::size_t HeaderBytes() const { return kVmsHeader; }

    // What this backend wraps *around* the payload, inside the file. Unlike the
    // header this shares the payload's blocks, so it only costs anything when
    // it pushes the payload over a boundary.
    virtual std::size_t RecordBytes() const { return 0; }

    // What a blob of `bytes` actually costs, header and icon included. This is
    // the number worth showing a player, because it is the number the console's
    // file manager will show them -- and it has been checked against a card the
    // console actually wrote, which is the only way to be sure of the rounding.
    std::size_t BlocksFor(std::size_t bytes) const {
        const std::size_t block = BlockSize();
        if (block <= 1) return bytes;
        const std::size_t payload = bytes + RecordBytes();
        return (HeaderBytes() + block - 1) / block +
               (payload + block - 1) / block;
    }

    // Convenience wrapper for the common case.
    bool Write(const std::string& name, const std::vector<uint8_t>& blob) {
        return Write(name, blob.data(), blob.size());
    }
};

}  // namespace bb
