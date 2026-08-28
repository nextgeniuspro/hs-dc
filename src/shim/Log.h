#pragma once

// Console logging: one place, three levels.
//
// The port used to printf() everything it did -- every bank decoded, every
// texture claimed, the heap after every screen. On a Dreamcast that is not
// free: the console's stdout goes down a serial line, so a hundred lines of
// startup chatter is time the game spends not drawing. It also buries the two
// or three lines that matter -- an asset that is not in the pak, a save that
// would not fit -- in a page of things that went right.
//
//   LogError   something failed, or an asset the game asked for is not there.
//              Always printed, to stderr.
//   LogInfo    the few lines a healthy run is worth: which binary, which pak,
//              which memory cards. Printed by default.
//   LogDebug   the running commentary -- assets loaded, banks released, heap
//              watermarks, screen flow. Off unless asked for.
//
// Turning the commentary back on:
//
//   desktop     blackbeard data.pak --verbose   (or BB_VERBOSE=1)
//   Dreamcast   cmake ... -DBB_VERBOSE=ON       (no command line on a console)

namespace bb {

// Ordered: a message prints when its level is at or below the current one.
enum class LogLevel { kError = 0, kInfo = 1, kDebug = 2 };

void SetLogLevel(LogLevel level);
LogLevel CurrentLogLevel();

// For the callers that would have to build a string -- or walk a list -- just
// to hand it to a line nobody is going to read.
bool LogEnabled(LogLevel level);

#ifdef __GNUC__
#define BB_LOG_FORMAT __attribute__((format(printf, 1, 2)))
#else
#define BB_LOG_FORMAT
#endif

void LogError(const char* fmt, ...) BB_LOG_FORMAT;
void LogInfo(const char* fmt, ...) BB_LOG_FORMAT;
void LogDebug(const char* fmt, ...) BB_LOG_FORMAT;

#undef BB_LOG_FORMAT

}  // namespace bb
