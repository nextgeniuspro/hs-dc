#include "platform/BootScreen.h"

#include "platform/BootFont.h"
#include "platform/Surface.h"

namespace bb {

void DrawBootRule(Surface& screen, int y) {
    screen.FillRect(kBootMargin, y, Surface::kWidth - 2 * kBootMargin, 1,
                    kBootRule);
}

void DrawBootChrome(Surface& screen, const std::string& heading,
                    uint16_t colour) {
    screen.Fill(kBootBackground);
    screen.FillRect(0, 0, Surface::kWidth, kBootBarHeight, kBootBar);
    DrawBootTextCentred(screen, 3, "HS", kBootDim);
    DrawBootTextCentred(screen, kBootHeadingY, heading, colour, 2);
    DrawBootRule(screen, kBootHeadingY + 22);
}

int DrawBootBody(Surface& screen, int y, const std::string& text,
                 uint16_t colour) {
    for (const std::string& line : WrapBootText(text, kBootBodyColumns)) {
        DrawBootText(screen, kBootMargin, y, line, colour);
        y += kBootLineStep;
    }
    return y;
}

void DrawBootKeys(Surface& screen, const std::string& first,
                  const std::string& second) {
    int y = kBootKeysY;
    if (!first.empty()) {
        DrawBootText(screen, kBootMargin, y, first, kBootInk);
        y += kBootLineStep;
    }
    // Two and a half lines of air under the first key, so the one that leaves
    // does not read as another way of doing the one that stays.
    y += 5 * kBootLineStep / 2;
    if (!second.empty()) DrawBootText(screen, kBootMargin, y, second, kBootInk);
}

void DrawBootHints(Surface& screen, const std::string& left,
                   const std::string& right) {
    DrawBootRule(screen, kBootFooterY - 6);
    if (!left.empty())
        DrawBootText(screen, kBootMargin, kBootFooterY, left, kBootDim);
    if (!right.empty()) {
        const int w = BootTextWidth(right) - 1;  // less the trailing gap
        DrawBootText(screen, Surface::kWidth - kBootMargin - w, kBootFooterY,
                     right, kBootDim);
    }
}

}  // namespace bb
