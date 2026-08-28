#include "platform/ImportScreen.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/BootFont.h"
#include "platform/DataFiles.h"
#include "platform/FileDialog.h"
#include "platform/Host.h"
#include "platform/SdlHost.h"
#include "platform/Surface.h"
#include "shim/Log.h"

namespace bb {
namespace {

// ARGB4444, like everything else the port draws. Deliberately not the game's
// palette: this screen is not the game, and it should not pretend to be one
// the player has not given us yet.
constexpr uint16_t kBackground = 0xF012;  // deep sea blue
constexpr uint16_t kBar = 0xF235;         // the strip along the top
constexpr uint16_t kRule = 0xF457;        // hairlines
constexpr uint16_t kInk = 0xFDDE;         // body text
constexpr uint16_t kDim = 0xF89A;         // labels and paths
constexpr uint16_t kHeading = 0xFEC5;     // brass
constexpr uint16_t kBad = 0xFF97;         // something went wrong
constexpr uint16_t kGood = 0xF7D8;        // the progress bar

// The layout, in the screen's own 176x208. Everything is on a grid of the
// font's cell so that nothing has to be measured twice.
constexpr int kBarHeight = 13;
constexpr int kHeadingY = 22;
constexpr int kBodyY = 50;
constexpr int kLineStep = 10;
constexpr int kMargin = 6;
// The two keys this screen answers to, anchored down here rather than left to
// follow the paragraph above them: they are the same two lines wherever the
// paragraph ends, and a complaint growing by a line should not shift them.
constexpr int kKeysY = 150;
constexpr int kFooterY = 190;
constexpr int kColumns = Surface::kWidth / kBootGlyphW;  // 29
// ...less the margin either side, which is what a paragraph is laid out in.
constexpr int kBodyColumns = kColumns - 2 * kMargin / kBootGlyphW;  // 27

// How the last line of a long path is drawn when there is no room for the
// rest of it: the tail is what tells one candidate from another, so the front
// is what gets cut.
std::string Elide(const std::string& text, std::size_t columns) {
    if (text.size() <= columns) return text;
    return "..." + text.substr(text.size() - (columns - 3));
}

void DrawRule(Surface& screen, int y) {
    screen.FillRect(kMargin, y, Surface::kWidth - 2 * kMargin, 1, kRule);
}

// The parts every state of this screen shares: the strip along the top, a
// heading under it, and the two hints along the bottom.
void DrawChrome(Surface& screen, const std::string& heading, uint16_t colour) {
    screen.Fill(kBackground);
    screen.FillRect(0, 0, Surface::kWidth, kBarHeight, kBar);
    DrawBootTextCentred(screen, 3, "HS", kDim);
    DrawBootTextCentred(screen, kHeadingY, heading, colour, 2);
    DrawRule(screen, kHeadingY + 22);
}

void DrawHints(Surface& screen, const std::string& left,
               const std::string& right) {
    DrawRule(screen, kFooterY - 6);
    if (!left.empty()) DrawBootText(screen, kMargin, kFooterY, left, kDim);
    if (!right.empty()) {
        const int w = BootTextWidth(right) - 1;  // less the trailing gap
        DrawBootText(screen, Surface::kWidth - kMargin - w, kFooterY, right,
                     kDim);
    }
}

// Wrapped body text from `y` down, one line per cell. Returns the y it ended
// at, so a caller can carry on underneath.
int DrawBody(Surface& screen, int y, const std::string& text, uint16_t colour) {
    for (const std::string& line : WrapBootText(text, kBodyColumns)) {
        DrawBootText(screen, kMargin, y, line, colour);
        y += kLineStep;
    }
    return y;
}

}  // namespace

std::string RunImportScreen(SdlHost& host) {
    // Whether this platform has a file picker to offer. Dragging always works
    // -- it is SDL's, not the desktop environment's, so there is nothing to be
    // missing -- but the Enter key has nothing to open without one.
    const bool canBrowse = FileDialogAvailable();

    const std::string prompt =
        "This port ships without game data. It needs the data.pak from your "
        "own copy of High Seize.\n\nDrag the file onto this window.";

    LogInfo("no game data; asking for a copy of data.pak\n");

    // A device frame, if any are installed, so the first thing a fresh install
    // draws looks like the rest of the port rather than a dialog box. Put back
    // the way it was found on the way out -- the game reads the player's own
    // choice out of the settings a moment later.
    const std::string frameWas = host.Frame();
    if (frameWas.empty() && host.FrameCount() > 0)
        host.SetFrame(host.FrameId(0));

    std::string failure;  // what went wrong with the last attempt, if anything
    std::string imported;
    bool dropped = false;  // whether BB_DROP has had its one turn
    while (!host.QuitRequested() && imported.empty()) {
        Surface& screen = host.Screen();
        DrawChrome(screen, "NO GAME DATA", failure.empty() ? kHeading : kBad);
        const int y = DrawBody(screen, kBodyY, prompt, kInk);
        if (!failure.empty()) DrawBody(screen, y + kLineStep / 2, failure, kBad);
        // The two keys, spelled out rather than left to the footer strip: this
        // is the first screen of a fresh install and the player has not seen
        // the game's soft-key chrome yet, so it says what to press in words.
        int keyLine = kKeysY;
        if (canBrowse) {
            DrawBootText(screen, kMargin, keyLine, "Press Enter to browse", kInk);
            keyLine += kLineStep;
        }
        keyLine += 2.5 * kLineStep;
        DrawBootText(screen, kMargin, keyLine, "Press Esc to quit", kInk);
        host.Flip();
        host.Sleep(16);

        if (host.KeyPressed(Key::kBack)) break;

        std::string chosen = host.TakeDroppedFile();
        // BB_DROP=/path/to/data.pak pretends that file was dragged onto the
        // window, once. A drag is not something a test can perform and a
        // modal file picker is not something it can answer, so without this
        // the only part of this screen a smoke test could reach is the
        // question -- and the copy, the progress bar and every complaint
        // below would go unphotographed. The same hook BB_KEYS is for the
        // screens after this one (platform/SdlMain.cpp).
        if (chosen.empty() && !dropped) {
            if (const char* drop = std::getenv("BB_DROP")) {
                chosen = drop;
                dropped = true;
            }
        }
        const bool asked =
            host.KeyPressed(Key::kSelect) || host.KeyPressed(Key::kSoftLeft);
        if (chosen.empty() && asked && canBrowse) {
            // The picker is modal and SDL's event loop stops for it, so
            // whatever was held when it opened is still held when it closes.
            chosen = OpenFileDialog("Choose High Seize's data.pak",
                                    "Select the data.pak from your own copy of "
                                    "the game.");
            host.FlushKeys();
        }
        if (chosen.empty()) continue;

        const std::string resolved = ResolveDroppedPath(chosen);
        if (resolved.empty()) {
            failure = "There is no data.pak in that folder.";
            LogError("import: no pak in %s\n", chosen.c_str());
            continue;
        }
        if (const std::string why = PakProblem(resolved); !why.empty()) {
            failure = "That file " + why + ".";
            LogError("import: %s %s\n", resolved.c_str(), why.c_str());
            continue;
        }

        // Twenty-five megabytes, and off a card reader that is long enough for
        // a still window to look like a hung one. The copy calls back with how
        // far it has got and this draws it, which is also what keeps the
        // window answering the desktop while it runs.
        const std::string source = Elide(resolved, kColumns - 2);
        std::string error;
        imported = ImportPak(
            resolved,
            [&](double fraction) {
                Surface& busy = host.Screen();
                DrawChrome(busy, "IMPORTING", kHeading);
                DrawBody(busy, kBodyY, "Copying your game data.", kInk);
                DrawBootText(busy, kMargin, kBodyY + 2 * kLineStep, source, kDim);

                const int barX = kMargin;
                const int barW = Surface::kWidth - 2 * kMargin;
                const int barY = kBodyY + 5 * kLineStep;
                busy.FillRect(barX, barY, barW, 9, kRule);
                busy.FillRect(barX + 1, barY + 1, barW - 2, 7, kBackground);
                const int filled =
                    static_cast<int>((barW - 2) * (fraction < 0   ? 0
                                                   : fraction > 1 ? 1
                                                                  : fraction));
                busy.FillRect(barX + 1, barY + 1, filled, 7, kGood);

                char percent[8];
                std::snprintf(percent, sizeof(percent), "%d%%",
                              static_cast<int>(fraction * 100));
                DrawBootTextCentred(busy, barY + 16, percent, kDim);
                DrawHints(busy, "", "Esc  Cancel");

                host.Flip();
                host.Sleep(1);
                // Closing the window or pressing Escape abandons the copy;
                // ImportPak then clears up the half-written file itself.
                return !host.QuitRequested() && !host.KeyPressed(Key::kBack);
            },
            error);
        if (imported.empty() && !error.empty()) failure = error;
        if (imported.empty()) host.FlushKeys();
    }

    host.SetFrame(frameWas.c_str());
    return imported;
}

}  // namespace bb
