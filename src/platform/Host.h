// Host — the platform seam the game code is allowed to touch.
//
// RedLynx already had this seam: main.dll's engine object carries one big
// vtable of platform services (the `CEpoc32` class in their assert strings),
// and the game calls through it rather than touching Symbian. The port keeps
// the same shape, so a second backend (KallistiOS/Dreamcast) is a matter of
// implementing this interface. Mapping to the original vtable at 0x1015042c:
//
//   +0x50 (slot 20)  screen()       -> Screen()
//   +0x54 (slot 21)  flip()         -> Flip()
//   +0x58 (slot 22)  sleep(ms)      -> Sleep()      (FUN_1006790c)
//   +0x64 (slot 25)  tickCount()    -> TickCount()  (FUN_100679d8)
//
// Sleep() is load-bearing: on Symbian it arms a CTimer and runs a *nested*
// active scheduler, so the window server keeps drawing and key events keep
// arriving while the game blocks. Game states are written as blocking loops
// that call it, so a host must pump its event queue there or nothing responds.
#pragma once

#include <cstdint>
#include <string>

#include "platform/Surface.h"

namespace bb {

class Storage;

// The N-Gage keypad. The d-pad doubles as 2/4/6/8 and the game reads both.
//
// The engine reads the keypad through a table of *logical* keys rather than
// scan codes, so the left-handed layout can rotate the whole thing without
// touching the game code. That table is written out at 0x10079070 and it is
// where the last five entries below come from:
//
//   0 -> '5'   1 -> '7'   2 -> '6'   3 -> '3'   4 -> '1'   5 -> '4'
//   6 -> '2'   7..10 -> left, right, up, down
//
// The first eight are what the port has always had. The last five are the
// battlefield keys that only exist on a numeric keypad, and every one of them
// is read somewhere in the browse state: '6' opens the cell board
// (0x1009b494), '1' and '3' walk the cursor between your own units
// (0x1009b028), '4' shows a unit's reach while it is *held* (0x1009ad88) and
// '2' the overview, likewise held (0x1009b494's tail).
enum class Key {
    kUp, kDown, kLeft, kRight,
    kSelect,      // centre / 5
    kSoftLeft,    // 7
    kSoftRight,   // #
    kBack,        // C
    kInfo,        // 6 -- the cell board
    kNextUnit,    // 3
    kPrevUnit,    // 1
    kRange,       // 4 -- held
    kMap,         // 2 -- held
    kCount
};

// What the player last touched, so the soft-key hints can show the right
// physical button: a keyboard keycap or a pad face button.
enum class InputDevice { kKeyboard, kGamepad };

class Host {
public:
    virtual ~Host() = default;

    // The 176x208 ARGB4444 buffer the game draws into.
    virtual Surface& Screen() = 0;

    // Present Screen() to the display.
    virtual void Flip() = 0;

    // Block for `ms`, pumping host events. Passing 0 yields once, matching the
    // engine's yield() (FUN_100678b4), which completes a request immediately
    // and lets the scheduler run one pass.
    virtual void Sleep(int ms) = 0;

    // Milliseconds since start. The original reads the HAL tick counter.
    virtual uint32_t TickCount() = 0;

    // True once the user has closed the window / the app must exit. Blocking
    // states must check this so a quit request can unwind them.
    virtual bool QuitRequested() const = 0;

    // Edge-triggered: true once per press. Consumes the event.
    virtual bool KeyPressed(Key k) = 0;

    // True while held.
    virtual bool KeyHeld(Key k) const = 0;

    // Consume any pending presses (used when entering a state, so a keypress
    // meant for the previous screen doesn't immediately dismiss the next one).
    virtual void FlushKeys() = 0;

    // The device the last input came from. Hosts with only one kind of input
    // never change their answer.
    virtual InputDevice ActiveInput() const { return InputDevice::kKeyboard; }

    // Characters typed since the last call, for the commander-name field. The
    // original edits that field with multi-tap on the numeric keypad
    // (0x100b112c), which is the one input idiom that does not survive the
    // move off the device -- a keyboard types directly instead. '\b' means
    // rub out. Hosts with no keyboard return nothing and the field keeps its
    // default, which is what the headless tests want.
    virtual std::string PollText() { return {}; }

    // --- audio ------------------------------------------------------------
    //
    // main.dll imports one function from MEDIACLIENTAUDIOSTREAM, so the game
    // mixes everything itself and pushes a single PCM stream. That is the
    // whole seam: the host swallows finished blocks of signed 16-bit mono and
    // reports how much it still has buffered. Defaults run the game silent,
    // which is what the headless test host wants.

    // Open the output at `rate` Hz. Returns false if there is no device.
    virtual bool AudioOpen(int rate) { return false; }

    // Hand over `count` signed 16-bit mono samples.
    virtual void AudioQueue(const int16_t* samples, int count) {}

    // Samples still waiting to be played.
    virtual int AudioQueued() const { return 0; }

    // Drop anything queued but not yet played -- for a hard cut, like the
    // user skipping a cutscene.
    virtual void AudioFlush() {}

    // --- the device frame ---------------------------------------------------
    //
    // The game field is 176x208 and a desktop window is not. The space either
    // side of it is filled with a picture of the device the game shipped on --
    // the left half of an N-Gage on the left, the right half on the right --
    // scaled to the height of the field and cropped by the window.
    //
    // That art is the host's, not the game's: it is drawn at window resolution,
    // outside the 176x208 surface, by whatever owns the window. So the game
    // only ever *names* a frame. A host with none reports zero, the settings
    // screen loses the row, and nothing else notices -- which is what the
    // headless tests and a Dreamcast with a 4:3 tube both want.
    //
    // Frames are named, not numbered, because the number would mean a
    // different frame the moment one is installed or removed and the name is
    // what gets written to the settings file.

    virtual int FrameCount() const { return 0; }

    // Stable id ("ngage-qd") and the label the settings list shows.
    virtual const char* FrameId(int index) const { return ""; }
    virtual const char* FrameLabel(int index) const { return ""; }

    // The frame in use; "" is none.
    virtual const char* Frame() const { return ""; }

    // Select by id. "" and any id this host does not have both mean none --
    // a settings file naming a frame that is not installed is not an error,
    // it is a frame that is not installed.
    virtual void SetFrame(const char* id) {}

    // --- persistence --------------------------------------------------------
    //
    // Where saved games go. The original hangs the same thing off its engine
    // object at +0x41c and every save in the game goes through it. Null means
    // this host has nowhere to put anything: the game still runs, saving is
    // refused politely, and the headless tests get that for free.
    virtual Storage* Saves() { return nullptr; }

    // --- memory card bays ---------------------------------------------------
    //
    // A phone has one filesystem and the original never asks which one. A
    // console is the other way round: the storage is removable, there may be
    // none of it or four of it, and which card a player wants their campaign
    // on is not something the machine can work out. So a host whose saves live
    // on removable media reports its **bays** -- the four controller ports on
    // a Dreamcast -- and the game puts a picker up before it starts or loads a
    // game, and again before it writes a save it has nowhere to put. See
    // game/CardPicker.h.
    //
    // A host that reports no bays is never asked and never draws a picker,
    // which is what the desktop and the headless tests get: `Saves()` there is
    // a directory that is simply always present.

    virtual int SaveBayCount() const { return 0; }

    // Is a card in that bay *now*? The picker asks every frame it is up, so a
    // card pushed in while the player is looking at the screen lights up
    // without them having to back out and come in again.
    virtual bool SaveBayReady(int bay) const { return false; }

    // What to call it on screen: "A".."D", one per controller port.
    virtual const char* SaveBayLabel(int bay) const { return ""; }

    // Which bay `Saves()` is pointed at, or -1 for none -- either because no
    // card has been chosen yet or because the player chose to play without
    // one.
    virtual int SaveBay() const { return -1; }

    // Point `Saves()` at a bay's card, or at nothing with -1. Returns whether
    // there is somewhere to save afterwards, so a card that has been pulled
    // between the picker drawing it and the player confirming it fails here
    // rather than at the first write.
    virtual bool SetSaveBay(int bay) { return false; }
};

}  // namespace bb
