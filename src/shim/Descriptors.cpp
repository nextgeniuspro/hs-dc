#include "shim/Descriptors.h"

namespace bb {

// Out-of-line anchor + a helper the game's file layer needs: normalize a
// Symbian path (backslashes, mixed case) to the canonical pak key form.
std::string NormalizePath(const TDesC8& d) {
    std::string s = d.ToStdString();
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    return s;
}

}  // namespace bb
