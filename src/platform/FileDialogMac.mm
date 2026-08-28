// The file picker on macOS.
//
// AppKit's panel is Objective-C, so this is the port's one Objective-C++
// translation unit; everything it exposes is the plain C++ declared in
// FileDialog.h, and nothing above it knows the difference.
//
// SDL has already made the process an application and pumped its event loop by
// the time this runs, so there is an NSApplication for -runModal to nest
// inside. That ordering is the whole reason this is safe to call from the
// import screen and would not be from, say, main() before the window opens.
#import <AppKit/AppKit.h>

#include <string>

#include "platform/FileDialog.h"

namespace bb {

bool FileDialogAvailable() { return true; }

std::string OpenFileDialog(const std::string& title, const std::string& message) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = [NSString stringWithUTF8String:title.c_str()];
        panel.message = [NSString stringWithUTF8String:message.c_str()];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.treatsFilePackagesAsDirectories = YES;
        // No type filter: what makes a file the game's data is its contents,
        // not its extension, and a copy that came off the phone's card as
        // DATA.PAK or out of a backup under another name is still the file the
        // player means. DataFiles.h does the checking.
        panel.showsHiddenFiles = YES;

        // A game that is running full-screen on a space of its own keeps the
        // key window, and the panel would open behind it. Asking for the front
        // first is what puts the panel where the player is looking.
        [NSApp activateIgnoringOtherApps:YES];
        if ([panel runModal] != NSModalResponseOK) return {};

        NSURL* url = panel.URLs.firstObject;
        if (!url) return {};
        return std::string(url.fileSystemRepresentation);
    }
}

}  // namespace bb
