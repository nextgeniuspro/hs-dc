// The screen a desktop build shows when it has no game data.
//
// No game code, binaries or assets ship with this port (see the README), so a
// desktop build with nothing beside it is the ordinary first-run state rather
// than a broken install. The console has nowhere to put this -- a Dreamcast is
// handed a disc or it is not, and DcMain says so and stops -- but a desktop
// can ask, and a player who has their own copy of the game should not have to
// find a command line to hand it over on.
//
// So this is the first thing a fresh install draws: a window that says what is
// missing, takes the file dragged onto it or opens the platform's file picker,
// checks it really is the game's archive, and copies it into the port's own
// directory (platform/DataFiles.h). Every launch after that finds it there and
// this screen is never seen again.
//
// It is drawn with the compiled-in font (platform/BootFont.h) for the obvious
// reason: the game's fonts are inside the file being asked for.
#pragma once

#include <string>

namespace bb {

class SdlHost;

// Ask the player for their copy of data.pak. Returns where the imported copy
// landed -- ready to mount -- or an empty string if they closed the window
// instead. Blocks until one or the other happens.
std::string RunImportScreen(SdlHost& host);

}  // namespace bb
