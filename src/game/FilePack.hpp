// FilePack — port of RedLynx's FilePack (main.dll @ 0x10005c80).
//
// The original opens data.pak, reads the directory (256 hash buckets keyed by
// the low byte of the 31-bit path hash; 24-byte in-memory entries
// {offset,compSize,uncompSize,flags,hash,owner}), and hands out readers for
// entries by name. That on-disk layout is exactly what bb::Pak parses and
// bb::Resources serves, so this facade delegates to Resources and returns a
// FileInputStream over the decompressed bytes — the seam the asset parsers use.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "game/FileInputStream.hpp"
#include "shim/Resources.h"

namespace bb {

class FilePack {
public:
    explicit FilePack(Resources& resources) : m_Resources(resources) {}

    bool Exists(const std::string& name) const {
        return m_Resources.Exists(name);
    }

    // Open an asset by its game path ("Data\\Battle\\gfx\\buildings.tc").
    // Returns nullopt if the entry isn't present.
    std::optional<FileInputStream> Open(const std::string& name) const {
        std::vector<uint8_t> buf;
        if (!m_Resources.ReadAll(name, buf)) return std::nullopt;
        return FileInputStream(std::move(buf));
    }

private:
    Resources& m_Resources;
};

}  // namespace bb
