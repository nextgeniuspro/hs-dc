#include "platform/DataFiles.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#include <SDL.h>

#include "shim/Log.h"
#include "shim/Pak.h"

namespace bb {
namespace {

// The organisation half of SDL's preference path is left empty: this is one
// game rather than a publisher's shelf of them, and an extra directory level
// with nothing in it but another directory helps nobody.
constexpr const char* kPrefOrg = "";
constexpr const char* kPrefApp = "Blackbeard";

// The name the imported copy is given, whatever the player's copy was called.
constexpr const char* kPakName = "data.pak";

// One asset that is in this game's pak and would not be in another RedLynx
// title's -- the palette every screen is drawn through (game/Palette.h).
constexpr const char* kSentinelAsset = "Data\\palette.pal";

// 256 KiB a time. Big enough that the syscall overhead disappears against a
// twenty-five megabyte file, small enough that the progress bar moves about a
// hundred times on the way -- and that a cancel is noticed promptly.
constexpr std::size_t kCopyChunk = 256 * 1024;

std::string WithSlash(std::string dir) {
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir.push_back('/');
    return dir;
}

}  // namespace

std::string UserDataDir() {
    // Asked for once: SDL creates the directory on the way, and the answer
    // cannot change while the process runs.
    static const std::string dir = [] {
        char* pref = SDL_GetPrefPath(kPrefOrg, kPrefApp);
        if (!pref) {
            LogError("no user data directory: %s\n", SDL_GetError());
            return std::string();
        }
        std::string out = pref;
        SDL_free(pref);
        return WithSlash(std::move(out));
    }();
    return dir;
}

std::string ImportedPakPath() {
    const std::string dir = UserDataDir();
    return dir.empty() ? std::string() : dir + kPakName;
}

std::vector<std::string> PakSearchPaths(const char* explicitPath) {
    std::vector<std::string> out;
    if (explicitPath && *explicitPath) out.push_back(explicitPath);
    if (const char* env = std::getenv("BB_PAK")) {
        if (*env) out.push_back(env);
    }
    // The copy this port imported, which is the one a player who went through
    // the import screen has and the reason that screen only appears once.
    if (const std::string imported = ImportedPakPath(); !imported.empty())
        out.push_back(imported);
    // Beside the executable, for a build that ships with its data -- someone
    // else's packaging, or a developer's own build directory.
    if (char* base = SDL_GetBasePath()) {
        const std::string dir = base;  // SDL guarantees a trailing separator
        SDL_free(base);
        out.push_back(dir + kPakName);
        out.push_back(dir + "DATA.PAK");  // as it comes off the phone's card
        out.push_back(dir + "data/" + kPakName);
        // A macOS bundle: the executable is Contents/MacOS/blackbeard and
        // anything shipped with it is one level over in Contents/Resources.
        out.push_back(dir + "../Resources/" + kPakName);
    }
    // And last the working directory, which is how it has always been run
    // from a source checkout.
    out.push_back(kPakName);
    return out;
}

std::string FindPak(const char* explicitPath) {
    for (const std::string& candidate : PakSearchPaths(explicitPath)) {
        if (!IsGamePak(candidate)) continue;
        return candidate;
    }
    return {};
}

std::string PakProblem(const std::string& path) {
    if (path.empty()) return "is not a path at all";
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
    } else {
        return "cannot be opened";
    }
    Pak pak;
    if (!pak.Open(path)) return "is not a RedLynx game archive";
    if (!pak.Exists(kSentinelAsset)) return "is not Blackbeard's data";
    return {};
}

bool IsGamePak(const std::string& path) { return PakProblem(path).empty(); }

std::string ResolveDroppedPath(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) return path;
    // A directory: the pak is usually right there, and on an MMC dump it is
    // four levels down under System/Apps. One level is what a drag of "the
    // folder I extracted it into" means; anything deeper is a search, and a
    // search that guesses wrong is worse than asking again.
    for (const char* name : {kPakName, "DATA.PAK"}) {
        const std::filesystem::path candidate =
            std::filesystem::path(path) / name;
        if (std::filesystem::is_regular_file(candidate, ec))
            return candidate.string();
    }
    return {};
}

std::string ImportPak(const std::string& src, const ImportProgressFn& progress,
                      std::string& error) {
    error.clear();
    const std::string dest = ImportedPakPath();
    if (dest.empty()) {
        error = "There is nowhere to keep it.";
        return {};
    }

    // Already the copy this port keeps: the player has pointed at the file the
    // import screen would have written, which is not a copy, it is a no-op.
    std::error_code ec;
    if (std::filesystem::exists(dest, ec) &&
        std::filesystem::equivalent(src, dest, ec)) {
        return dest;
    }

    FILE* in = std::fopen(src.c_str(), "rb");
    if (!in) {
        error = "That file could not be read.";
        return {};
    }
    std::fseek(in, 0, SEEK_END);
    const long total = std::ftell(in);
    std::fseek(in, 0, SEEK_SET);

    // Written under a temporary name and renamed at the end, so an import that
    // does not finish leaves the previous state -- no pak, usually -- rather
    // than a truncated one that opens far enough to fail later.
    const std::string partial = dest + ".part";
    FILE* out = std::fopen(partial.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        error = "The copy could not be written.";
        LogError("import: cannot write %s\n", partial.c_str());
        return {};
    }

    std::vector<uint8_t> buffer(kCopyChunk);
    long done = 0;
    bool ok = true;
    bool cancelled = false;
    for (;;) {
        const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), in);
        if (got == 0) {
            ok = std::feof(in) != 0;
            break;
        }
        if (std::fwrite(buffer.data(), 1, got, out) != got) {
            ok = false;
            break;
        }
        done += static_cast<long>(got);
        if (progress && !progress(total > 0 ? double(done) / double(total) : 0.0)) {
            cancelled = true;
            break;
        }
    }
    // A stream that only reports its failure at close is the one that matters
    // here: this is where a full disk shows up.
    if (std::fclose(out) != 0) ok = false;
    std::fclose(in);

    if (!ok || cancelled) {
        std::filesystem::remove(partial, ec);
        if (!cancelled) {
            error = "The copy did not finish -- is the disk full?";
            LogError("import: copy failed after %ld of %ld bytes\n", done, total);
        }
        return {};
    }

    std::filesystem::rename(partial, dest, ec);
    if (ec) {
        std::filesystem::remove(partial, ec);
        error = "The copy could not be put in place.";
        LogError("import: rename to %s: %s\n", dest.c_str(),
                 ec.message().c_str());
        return {};
    }
    LogInfo("import: %s -> %s (%ld bytes)\n", src.c_str(), dest.c_str(), total);
    return dest;
}

}  // namespace bb
