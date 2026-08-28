// Whether a given .tc asset holds palette indices or direct colour.
//
// The file format doesn't record this — the engine decides per call site — so
// the port carries the flags observed in the running original. See
// PaletteFlags.cpp for how they were captured and why guessing isn't enough.
#pragma once

#include <optional>
#include <string>

namespace bb {

// The indexing flag the original passed when it loaded this asset, or nullopt
// if the asset was never seen loading (fall back to Palette::LooksPaletted).
// `gamePath` may be in any case, with either slash style.
std::optional<bool> RuntimePaletted(const std::string& gamePath);

}  // namespace bb
