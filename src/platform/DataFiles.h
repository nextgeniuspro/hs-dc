// Where a desktop build's game data lives, and how it gets there.
//
// No game code, binaries or assets ship with this port, so every desktop
// player arrives with a copy of `data.pak` of their own and it is somewhere
// nobody can predict -- an MMC dump, a downloads folder, a memory card reader.
// A console solves this by being handed a disc; a desktop has to ask.
//
// So the port keeps a copy of its own, in the per-user directory the platform
// sets aside for exactly this, and everything here is either finding that copy
// or making it:
//
//   UserDataDir()    ~/Library/Application Support/Blackbeard (and the
//                    equivalents), where the imported copy lives
//   PakSearchPaths() everywhere a pak might be, in the order they are tried
//   FindPak()        the first of those that really is one
//   ImportPak()      copy the player's file into UserDataDir()
//
// The check is always on the contents (IsGamePak) and never on the name: a
// copy off the phone's card is `DATA.PAK`, a copy out of an emulator's tree
// might be anything, and all of them are the file the player means.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bb {

// The per-user directory this build keeps its data in, with a trailing
// separator, created if it was not there. Empty if the platform will not say
// where that is -- a machine with no home directory, which is rare enough that
// the caller is allowed to treat it as "no import possible".
std::string UserDataDir();

// Where the imported copy lands, and what FindPak looks for first.
std::string ImportedPakPath();

// Every candidate, in order: the imported copy, then beside the executable
// (how a packaged build ships with its data), then the working directory.
// `explicitPath`, when given, goes in front of all of them.
std::vector<std::string> PakSearchPaths(const char* explicitPath = nullptr);

// The first candidate that opens as this game's archive, or an empty string if
// none does. Candidates that are missing or are some other file entirely are
// skipped in silence: that is the normal state of most of the list.
std::string FindPak(const char* explicitPath = nullptr);

// Why `path` cannot be played, as a phrase that follows "that file" -- or an
// empty string if it can be. Three answers, and the difference between them is
// worth keeping: a path that is not there is a typo, a file that will not open
// as an archive is the wrong file, and an archive without the game's palette
// is some other RedLynx title's pak, which would otherwise be found out three
// screens into the boot flow.
//
// One phrasing for both callers, because they are asking the same question:
// the command line prints it after the path, the import screen shows it to the
// player who just chose the file.
std::string PakProblem(const std::string& path);

// The same question with the answer thrown away.
bool IsGamePak(const std::string& path);

// What the player actually meant by `path`. A file is itself; a directory is
// searched one level for a pak, so dropping the folder a dump was extracted
// into works as well as dropping the file. Empty if there is nothing usable.
std::string ResolveDroppedPath(const std::string& path);

// Copy `src` into UserDataDir() as `data.pak` and return where it landed, or
// an empty string on failure with `error` set to something a player can read.
//
// `progress` is called every few hundred kilobytes with a fraction from 0 to
// 1; returning false from it abandons the copy. Twenty-five megabytes off a
// card reader is a few seconds of a window that would otherwise look hung.
//
// The copy goes to a temporary name and is renamed into place, so a copy that
// is interrupted -- by the callback, by a full disk, by the power going out --
// cannot leave a half-written pak looking like a whole one.
using ImportProgressFn = std::function<bool(double fraction)>;
std::string ImportPak(const std::string& src, const ImportProgressFn& progress,
                      std::string& error);

}  // namespace bb
