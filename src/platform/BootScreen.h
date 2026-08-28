// The look of the screens the port draws before there is a game to draw.
#pragma once

#include <cstdint>
#include <string>

#include "platform/BootFont.h"  // the glyphs these are laid out in
#include "platform/Surface.h"

namespace bb {

// ARGB4444, like everything else the port draws.
constexpr uint16_t kBootBackground = 0xF012;  // deep sea blue
constexpr uint16_t kBootBar = 0xF235;         // the strip along the top
constexpr uint16_t kBootRule = 0xF457;        // hairlines
constexpr uint16_t kBootInk = 0xFDDE;         // body text
constexpr uint16_t kBootDim = 0xF89A;         // labels and paths
constexpr uint16_t kBootHeading = 0xFEC5;     // brass
constexpr uint16_t kBootBad = 0xFF97;         // something went wrong
constexpr uint16_t kBootGood = 0xF7D8;        // the progress bar

// The layout, in the screen's own 176x208. 
constexpr int kBootBarHeight = 13;
constexpr int kBootHeadingY = 22;
constexpr int kBootBodyY = 50;
constexpr int kBootLineStep = 10;
constexpr int kBootMargin = 6;
constexpr int kBootKeysY = 150;
constexpr int kBootFooterY = 190;
constexpr int kBootColumns = Surface::kWidth / kBootGlyphW;  // 29
constexpr int kBootBodyColumns = kBootColumns - 2 * kBootMargin / kBootGlyphW;

// A hairline across the screen, inside the margins.
void DrawBootRule(Surface& screen, int y);

// The parts every one of these screens shares: the strip along the top, a
// heading under it, and a rule beneath that. Clears the surface first.
void DrawBootChrome(Surface& screen, const std::string& heading,
                    uint16_t colour);

// Wrapped body text from `y` down, one line per cell. Returns the y it ended
// at, so a caller can carry on underneath.
int DrawBootBody(Surface& screen, int y, const std::string& text,
                 uint16_t colour);

// The keys the screen answers to, spelled out in words rather than left to the
// game's soft-key chrome -- the player reading this has not seen that chrome
// yet. `first` may be empty, for a screen with only one key to offer.
void DrawBootKeys(Surface& screen, const std::string& first,
                  const std::string& second);

// The footer strip: a rule, and a hint pinned to each end of the line under it.
// Either may be empty.
void DrawBootHints(Surface& screen, const std::string& left,
                   const std::string& right);

}  // namespace bb
