// Subtitles — the sliding text panel (class constructed at 0x100e73f0).
//
// A parchment panel rises from the bottom of the screen with a line or two of
// text on it, and a wooden name plate above it saying who is speaking. It is
// the game's only long-form text presentation, so although the cutscene player
// is what drives it here, nothing about it is cutscene-specific: give it lines
// and drive Update(), and it works anywhere.
//
// Seven textures make it up, all loaded by the startup loader:
//
//   0xbf sub-bg.tc         176x117  the panel body; the text is drawn onto it
//   0xc0 sub-bga.tc        176x5    the torn top edge, above the body
//   0xdb sub-name.tc       141x13   the name plate; the name is drawn onto it
//   0xdc sub-name-end.tc    28x17   right cap of the plate
//   0xdd sub-name-start.tc   9x17   left cap of the plate
//   0xde sub-name-top.tc   141x3    highlight along the plate's top edge
//   0xea sub-name-secondary.tc      a wider plate variant the engine loads but
//                                   the subtitle class never looks up
//
// Timing is in animation frames, and the panel moves 4 px per frame toward
// wherever it should be, so it slides rather than appears. It rises far enough
// to show the whole message but never less than two lines (0x100e7688).
#pragma once

#include <string>
#include <vector>

#include "platform/Surface.h"

namespace bb {

class FilePack;
class Font;
class Strings;
class TextureCache;
struct Texture;

class Subtitles {
public:
    struct Line {
        int Time = 0;      // frame it appears on
        int Delay = 0;     // how many frames it stays up
        std::string Talker;
        std::string Text;
    };

    // Look up the seven textures and remember the font to draw with. Returns
    // false if any of them is missing, in which case Draw() does nothing.
    bool Init(TextureCache& textures, const Font& font);

    // Read "Data\anim\sub\<name>.dat" (who says which string id) and
    // "Data\anim\sub\<name>.sub" (when, and for how long), resolving ids
    // through `strings`. Missing files are not an error -- several cutscenes
    // legitimately have no subtitles -- but they leave the panel empty.
    bool Load(FilePack& pack, const Strings& strings, const std::string& name);

    // Drive the panel directly, for callers that are not playing a .lap.
    void SetLines(std::vector<Line> lines);

    // The name to show when a line's speaker is the "[player]" placeholder.
    // The engine takes it from the player's profile (resource 0xc1); until
    // that screen is ported there is no profile, and an empty name means the
    // line simply gets no plate. Set this before Load().
    void SetPlayerName(std::string name) { m_PlayerName = std::move(name); }

    // Put the panel back below the screen with nothing showing.
    void Reset();

    // Advance to `frame`: pick up any line due now, then move the panel one
    // step toward where it should be. Call once per frame, in order.
    void Update(int frame);

    void Draw(Surface& dst) const;

    // Top edge of the panel in screen pixels. The cutscene player clips its
    // artwork to this so nothing draws over the text.
    int Top() const { return m_Y; }

    bool Empty() const { return m_Lines.empty(); }
    std::size_t Count() const { return m_Lines.size(); }
    const std::vector<Line>& Lines() const { return m_Lines; }

    // Whether a line is currently up (as opposed to sliding away).
    bool Showing() const { return m_Current >= 0 && m_Delay > 0; }

private:
    void Present(const Line& line);  // rebuild the two scratch panels

    const Font* m_Font = nullptr;
    const Texture* m_Bg = nullptr;
    const Texture* m_Edge = nullptr;
    const Texture* m_Plate = nullptr;
    const Texture* m_PlateEnd = nullptr;
    const Texture* m_PlateStart = nullptr;
    const Texture* m_PlateTop = nullptr;

    // The engine draws the panel art and its text into scratch buffers once
    // per line, then blits the result every frame; same here.
    Surface m_BgScratch;
    Surface m_PlateScratch;

    std::vector<Line> m_Lines;
    std::string m_PlayerName;
    int m_Next = 0;       // next line to pick up
    int m_Current = -1;   // line being shown, or -1
    int m_Delay = 0;      // frames left on it
    int m_Y = 0;          // current top edge
    int m_TargetY = 0;   // where it is heading
    int m_PlateWidth = 0;
};

}  // namespace bb
