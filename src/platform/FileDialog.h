// "Where is your copy of the game?" -- the desktop's own file picker.
//
// Wanted by exactly one screen (platform/ImportScreen.h) and no game code, so
// the interface is one function and it is allowed to fail: a host with no
// picker is not an error, it is a host where the player drags the file onto
// the window instead.
//
// Deliberately unfiltered. What makes a file the game's data is its contents
// -- DataFiles.h checks the archive opens and holds the game's palette -- not
// its name, and a player whose copy came off an MMC as `DATA.PAK`, or out of a
// backup as `data.pak.bak`, should be able to point at it.
#pragma once

#include <string>

namespace bb {

// Open the platform's file-open dialog, blocking until the player answers.
// Returns the chosen path, or an empty string if they cancelled -- or if this
// platform has no picker to open.
//
// `title` names the window; `message` is the line above the file list on the
// platforms that have somewhere to put one.
std::string OpenFileDialog(const std::string& title, const std::string& message);

// Whether the above can do anything at all, so a screen can offer the player
// the key rather than a key that does nothing. On Linux this looks for the
// helper it would run, which is why it is worth asking rather than assuming.
bool FileDialogAvailable();

}  // namespace bb
