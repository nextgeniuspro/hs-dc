#include "shim/Resources.h"

#include <vfspp/VirtualFileSystem.hpp>
#include <vfspp/NativeFileSystem.hpp>

#include "shim/PakFileSystem.hpp"

namespace bb {

namespace {
// Game code addresses assets the Symbian way ("Data\\Battle\\gfx\\x.tc").
// vfspp routes by virtual path, so translate to a rooted unix path before it
// sees anything: backslashes -> slashes, guarantee a leading '/'.
std::string ToVirtual(const std::string& path) {
    std::string s = path;
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    if (s.empty() || s.front() != '/') s.insert(s.begin(), '/');
    return s;
}
}  // namespace

Resources::Resources() : m_VFS(std::make_shared<vfspp::VirtualFileSystem>()) {}
Resources::~Resources() = default;

bool Resources::MountPak(const std::string& pakPath, const std::string& alias) {
    auto fs = m_VFS->CreateFileSystem<PakFileSystem>(alias, pakPath);
    return fs.has_value();
}

bool Resources::MountDir(const std::string& dirPath, const std::string& alias) {
    auto fs = m_VFS->CreateFileSystem<vfspp::NativeFileSystem>(alias, dirPath);
    return fs.has_value();
}

bool Resources::Exists(const std::string& path) const {
    return m_VFS->IsFileExists(ToVirtual(path));
}

bool Resources::ReadAll(const std::string& path, std::vector<uint8_t>& out) const {
    auto file = m_VFS->OpenFile(ToVirtual(path), vfspp::IFile::FileMode::Read);
    if (!file) return false;
    const uint64_t n = file->Size();
    out.clear();
    file->Read(out, n);
    file->Close();
    return true;
}

}  // namespace bb
