// Game resource layer, backed by vfspp.
//
// This is the port's replacement for RedLynx's FilePack2/FileInputStream seam:
// decompiled asset-loading code calls bb::Resources instead of touching the
// pak or the OS directly. A PakFileSystem serves the original data.pak; a
// native directory can be overlaid on top (vfspp DLC/patch feature) so loose
// files override archived ones during development.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vfspp { class VirtualFileSystem; }

namespace bb {

class Resources {
public:
    Resources();
    ~Resources();

    // Mount the RedLynx data.pak at `alias` (default root). Returns false if the
    // archive can't be opened.
    bool MountPak(const std::string& pakPath, const std::string& alias = "/");

    // Overlay a native directory at `alias`; files here override the pak.
    bool MountDir(const std::string& dirPath, const std::string& alias = "/");

    bool Exists(const std::string& path) const;

    // Read a whole asset (decompressed) into `out`.
    bool ReadAll(const std::string& path, std::vector<uint8_t>& out) const;

    // Escape hatch for code that wants the raw VFS (custom mounts, etc.).
    vfspp::VirtualFileSystem& Vfs() { return *m_VFS; }

private:
    std::shared_ptr<vfspp::VirtualFileSystem> m_VFS;
};

}  // namespace bb
