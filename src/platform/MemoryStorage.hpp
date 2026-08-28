// MemoryStorage — Storage in a std::map, for tests and for a host with nowhere
// to put anything.
//
// It is also how the memory card's arithmetic gets exercised without a memory
// card: set the block size to 512 and a small capacity and the save code takes
// the same branches it will take on the console, including the one where it
// refuses because the blob will not fit.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "platform/Storage.h"

namespace bb {

class MemoryStorage : public Storage {
public:
    explicit MemoryStorage(std::size_t capacity = 8 * 1024)
        : m_Capacity(capacity) {}

    // As in FileStorage: keep the base class's vector overload visible.
    using Storage::Write;

    bool Read(const std::string& name, std::vector<uint8_t>& out) override {
        out.clear();
        if (!ValidSlotName(name)) return false;
        const auto it = m_Slots.find(name);
        if (it == m_Slots.end()) return false;
        out = it->second;
        return true;
    }

    bool Write(const std::string& name, const uint8_t* data,
               std::size_t size) override {
        if (!ValidSlotName(name) || size > m_Capacity) return false;
        m_Slots[name].assign(data, data + size);
        return true;
    }

    bool Remove(const std::string& name) override {
        return m_Slots.erase(name) != 0;
    }

    bool Exists(const std::string& name) override {
        return m_Slots.count(name) != 0;
    }

    std::vector<std::string> List() override {
        std::vector<std::string> names;
        names.reserve(m_Slots.size());
        for (const auto& [name, blob] : m_Slots) names.push_back(name);
        return names;  // std::map already keeps them sorted
    }

    std::size_t Capacity() const override { return m_Capacity; }
    std::size_t BlockSize() const override { return m_Block; }
    std::size_t HeaderBytes() const override { return m_Header; }
    std::size_t RecordBytes() const override { return m_Record; }

    void SetBlockSize(std::size_t block) { m_Block = block; }
    void SetCapacity(std::size_t bytes) { m_Capacity = bytes; }
    // What a file's header costs, for standing in for a card that draws an
    // icon: 128 bytes bare, 640 with one 32x32 frame on it. See VmuStorage.h.
    void SetHeaderBytes(std::size_t bytes) { m_Header = bytes; }
    // ... and what the card wraps round the payload itself.
    void SetRecordBytes(std::size_t bytes) { m_Record = bytes; }

    // How much of a card this would occupy, for the tests that care.
    std::size_t TotalBlocks() {
        std::size_t n = 0;
        for (const auto& [name, blob] : m_Slots) n += BlocksFor(blob.size());
        return n;
    }

private:
    std::map<std::string, std::vector<uint8_t>> m_Slots;
    std::size_t m_Capacity;
    std::size_t m_Block = 1;
    std::size_t m_Header = kVmsHeader;
    std::size_t m_Record = 0;
};

}  // namespace bb
