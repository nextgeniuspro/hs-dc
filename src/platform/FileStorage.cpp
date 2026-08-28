#include "platform/FileStorage.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace bb {
namespace {

// The extension is for the human looking at the directory; the slot name is
// the whole of the identity, and List() hands back exactly what Write() was
// given.
constexpr const char* kExt = ".sav";

}  // namespace

bool Storage::ValidSlotName(const std::string& name) {
    if (name.empty() || name.size() > kMaxSlotName) return false;
    for (const char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_';
        if (!ok) return false;
    }
    return true;
}

std::string FileStorage::PathFor(const std::string& name) const {
    return m_Dir + "/" + name + kExt;
}

bool FileStorage::Read(const std::string& name, std::vector<uint8_t>& out) {
    out.clear();
    if (!ValidSlotName(name)) return false;
    std::FILE* f = std::fopen(PathFor(name).c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0 || std::size_t(size) > m_Capacity) {
        std::fclose(f);
        return false;
    }
    out.resize(std::size_t(size));
    const std::size_t got =
        size == 0 ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) {
        out.clear();
        return false;
    }
    return true;
}

bool FileStorage::Write(const std::string& name, const uint8_t* data,
                        std::size_t size) {
    if (!ValidSlotName(name)) return false;
    // The budget is checked here as well as in the save code, so no backend
    // can be talked into storing something a card would refuse.
    if (size > m_Capacity) return false;

    std::error_code ec;
    std::filesystem::create_directories(m_Dir, ec);

    // Temporary then rename: a torn write leaves the previous save intact,
    // which is the behaviour the card's block map gives for free.
    const std::string finalPath = PathFor(name);
    const std::string tempPath = finalPath + ".tmp";
    std::FILE* f = std::fopen(tempPath.c_str(), "wb");
    if (!f) return false;
    const bool wrote = size == 0 || std::fwrite(data, 1, size, f) == size;
    const bool closed = std::fclose(f) == 0;
    if (!wrote || !closed) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    std::filesystem::rename(tempPath, finalPath, ec);
    if (ec) {
        // Windows will not rename over an existing file; clear the way and
        // retry before giving up.
        std::filesystem::remove(finalPath, ec);
        ec.clear();
        std::filesystem::rename(tempPath, finalPath, ec);
    }
    if (ec) {
        std::error_code drop;
        std::filesystem::remove(tempPath, drop);
        return false;
    }
    return true;
}

bool FileStorage::Remove(const std::string& name) {
    if (!ValidSlotName(name)) return false;
    std::error_code ec;
    return std::filesystem::remove(PathFor(name), ec) && !ec;
}

bool FileStorage::Exists(const std::string& name) {
    if (!ValidSlotName(name)) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(PathFor(name), ec);
}

std::vector<std::string> FileStorage::List() {
    std::vector<std::string> names;
    std::error_code ec;
    std::filesystem::directory_iterator it(m_Dir, ec);
    if (ec) return names;
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string file = entry.path().filename().string();
        const std::size_t ext = file.size() < 4 ? std::string::npos
                                                : file.rfind(kExt);
        if (ext == std::string::npos || ext + 4 != file.size()) continue;
        const std::string name = file.substr(0, ext);
        if (ValidSlotName(name)) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace bb
