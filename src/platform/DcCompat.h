// One gap between newlib's C library and the C++ standard library built on
// top of it, force-included into every translation unit of the Dreamcast
// build (see the CMakeLists) so no game source has to know about it.
//
// `std::snprintf` is missing. `::snprintf` is there -- newlib declares it, and
// the linker has it -- but libstdc++ only pulls the C99 stdio functions into
// namespace std when it was configured with `_GLIBCXX_USE_C99_STDIO`, and the
// newlib build in this toolchain was not. (std::to_string survives because
// GCC 12 and later route it through std::to_chars instead.)
//
// The port uses `std::snprintf` throughout because that is what the desktop
// build wants and what the rest of the code looks like; adding the name here
// is what keeps those two hundred call sites identical on both targets.
#pragma once

#include <cstdio>

#if !defined(_GLIBCXX_USE_C99_STDIO) || !_GLIBCXX_USE_C99_STDIO
namespace std {
using ::snprintf;
using ::vsnprintf;
}  // namespace std
#endif
