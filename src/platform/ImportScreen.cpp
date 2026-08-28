#include "platform/ImportScreen.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/BootFont.h"
#include "platform/BootScreen.h"
#include "platform/DataFiles.h"
#include "platform/FileDialog.h"
#include "platform/Host.h"
#include "platform/SdlHost.h"
#include "platform/Surface.h"
#include "shim/Log.h"

namespace bb {
namespace {

// How the last line of a long path is drawn when there is no room for the
// rest of it: the tail is what tells one candidate from another, so the front
// is what gets cut.
std::string Elide(const std::string& text, std::size_t columns) {
    if (text.size() <= columns) return text;
    return "..." + text.substr(text.size() - (columns - 3));
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
        DrawBootChrome(screen, "NO GAME DATA",
                       failure.empty() ? kBootHeading : kBootBad);
        const int y = DrawBootBody(screen, kBootBodyY, prompt, kBootInk);
        if (!failure.empty())
            DrawBootBody(screen, y + kBootLineStep / 2, failure, kBootBad);
        // The keys, spelled out rather than left to the footer strip: this is
        // the first screen of a fresh install and the player has not seen the
        // game's soft-key chrome yet, so it says what to press in words.
        DrawBootKeys(screen, canBrowse ? "Press Enter to browse" : "",
                     "Press Esc to quit");
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
        const std::string source = Elide(resolved, kBootColumns - 2);
        std::string error;
        imported = ImportPak(
            resolved,
            [&](double fraction) {
                Surface& busy = host.Screen();
                DrawBootChrome(busy, "IMPORTING", kBootHeading);
                DrawBootBody(busy, kBootBodyY, "Copying your game data.",
                             kBootInk);
                DrawBootText(busy, kBootMargin,
                             kBootBodyY + 2 * kBootLineStep, source, kBootDim);

                const int barX = kBootMargin;
                const int barW = Surface::kWidth - 2 * kBootMargin;
                const int barY = kBootBodyY + 5 * kBootLineStep;
                busy.FillRect(barX, barY, barW, 9, kBootRule);
                busy.FillRect(barX + 1, barY + 1, barW - 2, 7, kBootBackground);
                const int filled =
                    static_cast<int>((barW - 2) * (fraction < 0   ? 0
                                                   : fraction > 1 ? 1
                                                                  : fraction));
                busy.FillRect(barX + 1, barY + 1, filled, 7, kBootGood);

                char percent[8];
                std::snprintf(percent, sizeof(percent), "%d%%",
                              static_cast<int>(fraction * 100));
                DrawBootTextCentred(busy, barY + 16, percent, kBootDim);
                DrawBootHints(busy, "", "Esc  Cancel");

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
