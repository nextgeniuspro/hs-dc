// FileStorage — the desktop backend for Storage: one file per slot.
//
// Deliberately dull. The interesting constraints all live in Storage.h and
// this obeys them rather than relaxing them: the same twelve-character names,
// the same per-blob budget, the same whole-file replacement. A save written
// here is one a memory card would have accepted, which is the point -- the
// desktop is where the format gets exercised, so it must not be the permissive
// one.
//
// Atomicity is a temporary file and a rename, which is as close as a host
// filesystem gets to the card's "the block map updates or it doesn't".
//
// This uses <filesystem> and is therefore a *desktop* backend. The Dreamcast
// gets its own implementation over KallistiOS's `fs_vmu` when that lands; it
// implements the same interface and nothing above this line has to change.
#pragma once

#include <string>

#include "platform/Storage.h"

namespace bb {

class FileStorage : public Storage {
public:
    // Eight kilobytes: sixteen VMU blocks plus the header block a VMS file
    // spends on its description and icon, so a full game slot costs 17 of a
    // card's 200. Three game slots and the settings come to about a quarter of
    // a card, which is a fair share for one game to take.
    static constexpr std::size_t kDefaultCapacity = 8 * 1024;

    // `dir` is created on first write if it does not exist.
    explicit FileStorage(std::string dir) : m_Dir(std::move(dir)) {}

    // The base class's whole-vector convenience overload would otherwise be
    // hidden by the pointer-and-size one below.
    using Storage::Write;

    bool Read(const std::string& name, std::vector<uint8_t>& out) override;
    bool Write(const std::string& name, const uint8_t* data,
               std::size_t size) override;
    bool Remove(const std::string& name) override;
    bool Exists(const std::string& name) override;
    std::vector<std::string> List() override;
    std::size_t Capacity() const override { return m_Capacity; }

    // Pretend to be a memory card, so the block arithmetic and the budget can
    // be exercised on the desktop. Off by default: on a real filesystem a byte
    // is a byte and quoting blocks would be a fiction.
    void SetBlockSize(std::size_t block) { m_Block = block; }
    std::size_t BlockSize() const override { return m_Block; }
    void SetCapacity(std::size_t bytes) { m_Capacity = bytes; }

    const std::string& Dir() const { return m_Dir; }

private:
    std::string PathFor(const std::string& name) const;

    std::string m_Dir;
    std::size_t m_Capacity = kDefaultCapacity;
    std::size_t m_Block = 1;
};

}  // namespace bb
