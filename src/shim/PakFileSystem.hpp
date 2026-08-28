// vfspp backend for the RedLynx data.pak archive.
//
// Adapts bb::Pak (our reverse-engineered reader) to vfspp's IFileSystem so the
// original single-file archive can be mounted into a VirtualFileSystem and
// overlaid with loose dev files via vfspp's DLC/patch feature. Read-only:
// files are decompressed on open and served as in-memory MemoryFiles.
//
// The pak stores only 31-bit path hashes, not names, so directory enumeration
// (GetEntriesList) is unavailable; lookup by known path works because bb::Pak
// hashes the requested path. This keeps the shipped asset identical to the
// original game — no pre-extraction required.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <vfspp/IFileSystem.h>
#include <vfspp/MemoryFile.hpp>

#include "shim/Pak.h"

namespace bb {

class PakFileSystem final : public vfspp::IFileSystem {
public:
    // Signature required by vfspp::VirtualFileSystem::CreateFileSystem<T>.
    PakFileSystem(const std::string& alias, const std::string& pakPath)
        : m_Alias(alias), m_PakPath(pakPath) {}

    bool Initialize() override {
        if (m_Initialized) return true;
        m_Initialized = m_Pak.Open(m_PakPath);
        return m_Initialized;
    }
    void Shutdown() override { m_Initialized = false; }
    bool IsInitialized() const override { return m_Initialized; }

    const std::string& BasePath() const override { return m_PakPath; }
    const std::string& VirtualPath() const override { return m_Alias; }
    bool IsReadOnly() const override { return true; }

    EntriesList GetEntriesList(bool) const override { return {}; }  // hashes only

    bool IsFileExists(const std::string& vpath) const override {
        return m_Initialized && m_Pak.Exists(vpath);
    }
    bool IsDirectoryExists(const std::string&) const override { return false; }

    std::optional<vfspp::EntryInfo> GetEntryInfo(
        const std::string& vpath) const override {
        if (!IsFileExists(vpath)) return std::nullopt;
        return vfspp::EntryInfo(m_Alias, m_PakPath, vpath);
    }

    vfspp::IFilePtr OpenFile(const std::string& vpath,
                             vfspp::IFile::FileMode mode) override {
        if (vfspp::IFile::ModeHasFlag(mode, vfspp::IFile::FileMode::Write))
            return nullptr;  // read-only archive
        std::vector<uint8_t> data;
        if (!m_Pak.Read(vpath, data)) return nullptr;

        vfspp::EntryInfo info(m_Alias, m_PakPath, vpath);
        auto object = std::make_shared<vfspp::MemoryFileObject>();
        *object->GetWritableData() = std::move(data);
        auto file = std::make_shared<vfspp::MemoryFile>(info, std::move(object));
        if (!file->Open(vfspp::IFile::FileMode::Read)) return nullptr;
        return file;
    }
    void CloseFile(vfspp::IFilePtr file) override {
        if (file) file->Close();
    }

    // --- writable-only operations: unsupported on a read-only archive --------
    vfspp::IFilePtr CreateFile(const std::string&) override { return nullptr; }
    bool RemoveFile(const std::string&) override { return false; }
    bool CopyFile(const std::string&, const std::string&, bool) override { return false; }
    bool RenameFile(const std::string&, const std::string&) override { return false; }
    bool MakeDirectory(const std::string&) override { return false; }
    bool DeleteDirectory(const std::string&, bool) override { return false; }
    bool RenameDirectory(const std::string&, const std::string&) override { return false; }

private:
    std::string m_Alias;
    std::string m_PakPath;
    Pak m_Pak;
    bool m_Initialized = false;
};

}  // namespace bb
