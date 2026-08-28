#include "shim/Log.h"

#include <cstdarg>
#include <cstdio>

namespace bb {
namespace {

// A Dreamcast build has no command line to pass --verbose on, so the build
// decides: -DBB_VERBOSE=ON is how a developer gets the commentary back.
#ifdef BB_VERBOSE
LogLevel g_level = LogLevel::kDebug;
#else
LogLevel g_level = LogLevel::kInfo;
#endif

// Flushed line by line, both streams, for two reasons: a log read after a
// crash has to end with the line the game died on, and stdout being buffered
// while stderr is not would otherwise file every error above the work it
// interrupted.
void Emit(std::FILE* out, const char* fmt, std::va_list args) {
    if (out != stdout) std::fflush(stdout);
    std::vfprintf(out, fmt, args);
    std::fflush(out);
}

}  // namespace

void SetLogLevel(LogLevel level) { g_level = level; }

LogLevel CurrentLogLevel() { return g_level; }

bool LogEnabled(LogLevel level) { return level <= g_level; }

// Errors go to stderr so that a desktop run can keep them apart from a tool's
// own output (`--texture` prints what it found on stdout). On the console both
// streams land in the same place, which is the point of splitting them here
// and nowhere else.
void LogError(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    Emit(stderr, fmt, args);
    va_end(args);
}

void LogInfo(const char* fmt, ...) {
    if (!LogEnabled(LogLevel::kInfo)) return;
    std::va_list args;
    va_start(args, fmt);
    Emit(stdout, fmt, args);
    va_end(args);
}

void LogDebug(const char* fmt, ...) {
    if (!LogEnabled(LogLevel::kDebug)) return;
    std::va_list args;
    va_start(args, fmt);
    Emit(stdout, fmt, args);
    va_end(args);
}

}  // namespace bb
