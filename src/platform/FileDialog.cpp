// The file picker, everywhere except macOS -- which needs Objective-C++ and
// gets a translation unit of its own (FileDialogMac.mm).
#include "platform/FileDialog.h"

#if !defined(__APPLE__)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "shim/Log.h"

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#endif

namespace bb {

#if defined(_WIN32)

namespace {
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), n);
    return out;
}

std::string Narrow(const wchar_t* s) {
    if (!s || !*s) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr,
                                      nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    return out;
}
}  // namespace

bool FileDialogAvailable() { return true; }

std::string OpenFileDialog(const std::string& title, const std::string& message) {
    (void)message;  // the common dialog has nowhere to put a second line
    wchar_t chosen[MAX_PATH] = {};
    const std::wstring wideTitle = Widen(title);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = chosen;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = wideTitle.empty() ? nullptr : wideTitle.c_str();
    // Two filters, the archive first and everything second: a copy that has
    // been renamed is still the file we want, and the check that matters
    // happens on the contents afterwards.
    ofn.lpstrFilter = L"Game data (*.pak)\0*.pak\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    // OFN_NOCHANGEDIR because the dialog would otherwise leave the process
    // sitting in whichever directory the player browsed to, and the port
    // resolves relative paths of its own after this returns.
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return Narrow(chosen);
}

#else  // Linux, BSD, anything else with a shell

namespace {
// The desktop helpers, in the order they are tried. There is no portable file
// dialog on X11 or Wayland -- the toolkits own them -- so the port asks
// whichever of these is installed, which is what every other SDL application
// without a toolkit does.
//
// %s is the window title, quoted by the caller.
const char* const kPickers[] = {
    "zenity --file-selection --title='%s' 2>/dev/null",
    "kdialog --getopenfilename . --title '%s' 2>/dev/null",
    "qarma --file-selection --title='%s' 2>/dev/null",
    "matedialog --file-selection --title='%s' 2>/dev/null",
};

bool HaveCommand(const char* line) {
    // The command name is everything up to the first space.
    std::string name(line, std::strcspn(line, " "));
    const std::string probe = "command -v " + name + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

// Single quotes are what the commands above wrap the title in, so a title
// containing one would end the quoting. The titles this port passes are
// literals with no quote in them; this is here so that stays true by
// construction rather than by inspection.
std::string SingleQuoteSafe(const std::string& s) {
    std::string out;
    for (const char c : s) out.push_back(c == '\'' ? ' ' : c);
    return out;
}
}  // namespace

bool FileDialogAvailable() {
    for (const char* picker : kPickers)
        if (HaveCommand(picker)) return true;
    return false;
}

std::string OpenFileDialog(const std::string& title, const std::string& message) {
    (void)message;
    for (const char* picker : kPickers) {
        if (!HaveCommand(picker)) continue;
        char command[512];
        std::snprintf(command, sizeof(command), picker,
                      SingleQuoteSafe(title).c_str());
        FILE* pipe = popen(command, "r");
        if (!pipe) continue;
        std::string out;
        char buf[512];
        while (std::fgets(buf, sizeof(buf), pipe)) out += buf;
        pclose(pipe);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        return out;  // empty if they cancelled, which is the right answer too
    }
    LogDebug("file dialog: no zenity, kdialog or qarma installed\n");
    return {};
}

#endif

}  // namespace bb

#endif  // !__APPLE__
