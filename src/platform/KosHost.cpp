#include "platform/KosHost.h"

#include <cstdio>
#include <cstring>
#include <malloc.h>

#include <arch/timer.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/keyboard.h>
#include <dc/pvr.h>
#include <dc/sound/sound.h>
#include <dc/sound/stream.h>
#include <dc/video.h>
#include <kos/thread.h>

#include "platform/ScreenLayout.h"
#include "platform/VmuStorage.h"
#include "shim/Log.h"

namespace bb {

namespace {

// The game field on a 640x480 screen, worked out by the same function the
// desktop build uses on a window: fitted to the height, centred, its 11:13
// shape kept. That comes to 406x480 at x=117, and the 117 columns either side
// are the device frame's.
constexpr int kScreenW = 640;
constexpr int kScreenH = 480;
constexpr ScreenRect kField = FitScreen(kScreenW, kScreenH);

// The PVR wants power-of-two textures, so 176x208 lives in the corner of a
// 256x256 one and the quad's UVs stop where the frame does. 128 KB of the
// 8 MB of texture memory; the two frame halves take 128 KB each.
constexpr int kTexW = 256;
constexpr int kTexH = 256;
constexpr int kFrameTexH = 512;  // the halves are 480 tall

// One bit per Host::Key, so a frame of input is a single word.
constexpr uint32_t Bit(Key k) { return 1u << static_cast<int>(k); }

// The AICA asks for bytes; 16-bit mono means two per sample.
constexpr int kStreamBytes = 4096;  // 256 ms at 8 kHz -- room, not latency

// The one host, for the stream callback: snd_stream's callback carries a
// handle and no user pointer, and there is exactly one Host on this hardware.
KosHost* g_host = nullptr;

// One textured quad in screen pixels, sampling [0,u1] x [0,v1] of `texture`.
// Everything on this screen is one of these: the game field and, either side
// of it, the two halves of the device frame.
void Quad(void* texture, int format, int TexW, int texH, float x0, float y0,
          float x1, float y1, float u1, float v1) {
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, format, TexW, texH,
                     static_cast<pvr_ptr_t>(texture), PVR_FILTER_NONE);
    // Everything else is left as pvr_poly_cxt_txr sets it: the default is
    // modulation and these vertices are white, so a texel comes through as
    // its own colour.
    pvr_poly_hdr_t hdr;
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    pvr_vertex_t v{};
    v.flags = PVR_CMD_VERTEX;
    v.argb = 0xffffffff;
    v.oargb = 0;
    v.z = 1.0f;
    v.x = x0; v.y = y0; v.u = 0.0f; v.v = 0.0f; pvr_prim(&v, sizeof(v));
    v.x = x1; v.y = y0; v.u = u1;   v.v = 0.0f; pvr_prim(&v, sizeof(v));
    v.x = x0; v.y = y1; v.u = 0.0f; v.v = v1;   pvr_prim(&v, sizeof(v));
    v.flags = PVR_CMD_VERTEX_EOL;
    v.x = x1; v.y = y1; v.u = u1;   v.v = v1;   pvr_prim(&v, sizeof(v));
}

uint16_t GetU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t GetU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

KosHost::~KosHost() {
    if (m_Stream >= 0) {
        snd_stream_stop(m_Stream);
        snd_stream_destroy(m_Stream);
        snd_stream_shutdown();
    }
    if (m_Texture) {
        for (void*& tex : m_FrameTex) {
            if (tex) pvr_mem_free(static_cast<pvr_ptr_t>(tex));
            tex = nullptr;
        }
        pvr_mem_free(static_cast<pvr_ptr_t>(m_Texture));
        m_Texture = nullptr;
        pvr_shutdown();
    }
    if (g_host == this) g_host = nullptr;
}

bool KosHost::Init() {
    g_host = this;

    vid_set_mode(DM_640x480, PM_RGB565);

    // One opaque quad a frame and nothing else: the opaque list is the only
    // one that gets bins, and the vertex buffer only ever holds a header and
    // four vertices. The PVR clears to the background colour every frame,
    // which is what keeps the black either side of the field black.
    pvr_init_params_t params{};
    params.opb_sizes[0] = PVR_BINSIZE_16;  // opaque polygons
    params.opb_sizes[1] = PVR_BINSIZE_0;   // opaque modifiers
    params.opb_sizes[2] = PVR_BINSIZE_0;   // translucent polygons
    params.opb_sizes[3] = PVR_BINSIZE_0;   // translucent modifiers
    params.opb_sizes[4] = PVR_BINSIZE_0;   // punch-through
    params.vertex_buf_size = 64 * 1024;
    if (pvr_init(&params) < 0) return false;
    pvr_set_bg_color(0.0f, 0.0f, 0.0f);

    m_Texture = pvr_mem_malloc(kTexW * kTexH * 2);
    if (!m_Texture) {
        pvr_shutdown();
        return false;
    }

    m_Ring.assign(kRingSamples, 0);
    m_Stage.assign(kStreamBytes / 2, 0);
    LogDebug("video: %dx%d, field %dx%d at %d,%d\n", kScreenW, kScreenH,
             kField.W, kField.H, kField.X, kField.Y);
    return true;
}

// ---------------------------------------------------------------------------
// Presentation

void KosHost::Flip() {
    // Wait for the previous frame to be done with the texture before the next
    // one is written over it.
    pvr_wait_ready();

    // Row by row, because the frame is 176 wide and the texture is 256. A row
    // is 352 bytes -- eleven whole store queues -- and every row of a 256-wide
    // 16-bit texture starts 32-byte aligned, which is what pvr_txr_load wants.
    const uint16_t* src = m_Screen.Pixels();
    uint8_t* dst = static_cast<uint8_t*>(m_Texture);
    for (int y = 0; y < Surface::kHeight; ++y) {
        pvr_txr_load(src + y * Surface::kWidth, dst + y * kTexW * 2,
                     Surface::kWidth * 2);
    }

    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);

    // The device frame first, though nothing overlaps: its halves are butted
    // against the field's edges and run outward, and the art was cropped at
    // build time to the strip that fits, so what is drawn is what is seen.
    if (m_Frame >= 0 && m_FrameTex[0] && m_FrameTex[1]) {
        const DeviceFrame& f = m_Frames[static_cast<std::size_t>(m_Frame)];
        const int fmt = PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED;
        const float vh = static_cast<float>(kScreenH) / kFrameTexH;
        if (f.LeftW > 0) {
            Quad(m_FrameTex[0], fmt, f.TexW, kFrameTexH,
                 static_cast<float>(kField.X - f.LeftW), 0.0f,
                 static_cast<float>(kField.X), static_cast<float>(kScreenH),
                 static_cast<float>(f.LeftW) / f.TexW, vh);
        }
        if (f.RightW > 0) {
            const float rightEdge = static_cast<float>(kField.X + kField.W);
            Quad(m_FrameTex[1], fmt, f.TexW, kFrameTexH, rightEdge, 0.0f,
                 rightEdge + f.RightW, static_cast<float>(kScreenH),
                 static_cast<float>(f.RightW) / f.TexW, vh);
        }
    }

    Quad(m_Texture, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED, kTexW, kTexH,
         static_cast<float>(kField.X), static_cast<float>(kField.Y),
         static_cast<float>(kField.X + kField.W),
         static_cast<float>(kField.Y + kField.H),
         static_cast<float>(Surface::kWidth) / kTexW,
         static_cast<float>(Surface::kHeight) / kTexH);

    pvr_list_finish();
    pvr_scene_finish();
    ++m_Flips;
    WatchHeap();

    if (m_Stream >= 0) snd_stream_poll(m_Stream);
    Poll();
}

// A 16 MB machine running a game written against a desktop's memory is worth
// watching, and three separate out-of-memory faults were found by reading
// these lines. Four numbers, because each alone misleads:
//
//   heap  what is held now -- what the next load has to fit beside
//   peak  what nearly did not fit
//   tex   decoded textures, and snd the sound banks -- held compressed, a
//         block decoded at a time, so this is roughly a quarter of what the
//         samples would come to. The two that grow, and
//         between them most of the heap. Whatever the heap holds *beyond*
//         these two is the port's own overhead, and if that is what is
//         climbing, no amount of releasing artwork will help.
//
// A quarter of a megabyte of new peak reports at once; otherwise it reports
// every few hundred frames, so the line before an abort is always recent.
//
// It is commentary rather than a fault, so a quiet build does not print it at
// all -- and a verbose one does not print the same four numbers twice running,
// which is what a game sitting on a menu used to produce.
void KosHost::WatchHeap() {
    if (!LogEnabled(LogLevel::kDebug)) return;
    static std::size_t peak = 0;
    static std::size_t said = 0;
    const std::size_t used = static_cast<std::size_t>(mallinfo().uordblks);
    const bool climbed = used >= peak + 256 * 1024;
    if (climbed) peak = used;
    if (!climbed && m_Flips % 512 != 0) return;
    if (used == said) return;
    said = used;

    std::size_t textures = 0, sounds = 0;
    if (m_Probe) m_Probe(textures, sounds);
    LogDebug("mem: heap %u KB, peak %u KB (tex %u KB, snd %u KB)\n",
             static_cast<unsigned>(used / 1024),
             static_cast<unsigned>(peak / 1024),
             static_cast<unsigned>(textures / 1024),
             static_cast<unsigned>(sounds / 1024));
}

uint32_t KosHost::TickCount() {
    return static_cast<uint32_t>(timer_ms_gettime64());
}

void KosHost::Sleep(int ms) {
    // The same yield-and-pump the SDL backend does, and for the same reason
    // the original ran a nested active scheduler here: game states block in
    // this call, so input and audio have to be serviced from inside it.
    if (m_Stream >= 0) snd_stream_poll(m_Stream);
    Poll();
    if (ms <= 0) {
        thd_pass();
        return;
    }
    const uint64_t end = timer_ms_gettime64() + static_cast<uint64_t>(ms);
    for (;;) {
        const uint64_t now = timer_ms_gettime64();
        if (now >= end) break;
        const uint64_t left = end - now;
        thd_sleep(static_cast<int>(left > 5 ? 5 : left));
        if (m_Stream >= 0) snd_stream_poll(m_Stream);
        Poll();
    }
}

// ---------------------------------------------------------------------------
// Input

void KosHost::Poll() {
    PollController();
    PollKeyboard();
}

void KosHost::Latch(uint32_t mask) {
    m_Pressed |= mask & ~m_Held;
    m_Held = mask;
}

void KosHost::PollController() {
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev) return;
    const cont_state_t* st =
        static_cast<const cont_state_t*>(maple_dev_status(dev));
    if (!st) return;

    // The mapping follows the desktop build's controller layout: A confirms,
    // B goes back, Start is the left soft key -- and the four the phone kept
    // on its numeric keypad take the buttons a Dreamcast pad has spare. Y is
    // the cell board, X held shows a unit's reach, and the analogue triggers
    // walk between your own units, half-pressed counting as pressed.
    //
    // There is nothing left for the overview, which is the one battlefield key
    // this machine cannot offer: a Dreamcast pad has six buttons and this
    // needs seven. It stays on the battle menu's Map row, where it has always
    // also been.
    //
    // The right soft key is not bound here either, and nothing misses it: it
    // is the phone's `#`, it only ever cancels, and every screen that reads it
    // reads Back in the same breath.
    uint32_t mask = 0;
    if (st->buttons & CONT_DPAD_UP) mask |= Bit(Key::kUp);
    if (st->buttons & CONT_DPAD_DOWN) mask |= Bit(Key::kDown);
    if (st->buttons & CONT_DPAD_LEFT) mask |= Bit(Key::kLeft);
    if (st->buttons & CONT_DPAD_RIGHT) mask |= Bit(Key::kRight);
    if (st->buttons & CONT_A) mask |= Bit(Key::kSelect);
    if (st->buttons & CONT_B) mask |= Bit(Key::kBack);
    if (st->buttons & CONT_START) mask |= Bit(Key::kSoftLeft);
    if (st->buttons & CONT_Y) mask |= Bit(Key::kInfo);
    if (st->buttons & CONT_X) mask |= Bit(Key::kRange);
    if (st->ltrig > 64) mask |= Bit(Key::kPrevUnit);
    if (st->rtrig > 64) mask |= Bit(Key::kNextUnit);
    // The stick doubles as the d-pad, deadzoned at half deflection so a
    // resting stick cannot walk a menu on its own.
    if (st->joyy < -64) mask |= Bit(Key::kUp);
    if (st->joyy > 64) mask |= Bit(Key::kDown);
    if (st->joyx < -64) mask |= Bit(Key::kLeft);
    if (st->joyx > 64) mask |= Bit(Key::kRight);

    if (mask && mask != m_Held) m_ActiveInput = InputDevice::kGamepad;
    Latch(mask);
}

void KosHost::PollKeyboard() {
    // A keyboard is optional, and the one screen that wants one is the
    // commander-name field: the original edits it by multi-tap on the numeric
    // keypad, which is the one input idiom that does not survive the move off
    // the device. With a keyboard plugged in you type the name instead; with
    // none, PollText returns nothing and the field keeps its default.
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);
    if (!dev) return;
    for (;;) {
        const int k = kbd_queue_pop(dev, 1);
        if (k == KBD_QUEUE_END) break;
        m_ActiveInput = InputDevice::kKeyboard;
        if (k == 8 || k == 127) {
            m_Text.push_back('\b');
        } else if (k >= 32 && k < 256) {
            m_Text.push_back(static_cast<char>(k));
        }
    }
}

bool KosHost::KeyPressed(Key k) {
    const uint32_t bit = Bit(k);
    if (!(m_Pressed & bit)) return false;
    m_Pressed &= ~bit;
    return true;
}

bool KosHost::KeyHeld(Key k) const { return (m_Held & Bit(k)) != 0; }

void KosHost::FlushKeys() {
    m_Pressed = 0;
    m_Text.clear();
}

std::string KosHost::PollText() {
    std::string out;
    out.swap(m_Text);
    return out;
}

// ---------------------------------------------------------------------------
// Audio
//
// The game hands over finished blocks of signed 16-bit mono; the AICA asks for
// them when it wants them. The ring buffer is what turns one into the other.
// Both ends run on this thread -- the callback is reached from
// snd_stream_poll(), which Flip() and Sleep() call -- so no locking is needed.

bool KosHost::AudioOpen(int rate) {
    if (m_Stream >= 0) return true;
    if (snd_stream_init() < 0) return false;
    m_Stream = snd_stream_alloc(&KosHost::StreamCallback, kStreamBytes);
    if (m_Stream < 0) {
        snd_stream_shutdown();
        return false;
    }
    m_RingHead = m_RingTail = m_RingCount = 0;
    snd_stream_start(m_Stream, static_cast<uint32_t>(rate), 0);
    return true;
}

void KosHost::AudioQueue(const int16_t* samples, int count) {
    if (m_Stream < 0 || !samples) return;
    for (int i = 0; i < count; ++i) {
        if (m_RingCount >= kRingSamples) break;  // ahead of the AICA; drop
        m_Ring[m_RingTail] = samples[i];
        m_RingTail = (m_RingTail + 1) % kRingSamples;
        ++m_RingCount;
    }
}

int KosHost::AudioQueued() const { return m_RingCount; }

void KosHost::AudioFlush() {
    m_RingHead = m_RingTail = m_RingCount = 0;
}

// Both halves of the crank in one call, for the times the game thread cannot
// turn it: the mix that fills the ring and the poll that empties it. Flip()
// and Sleep() only do the second, because a running game loop does the first
// itself every frame -- this exists for the calls that stop the loop dead, and
// on this machine that means the memory card (see AudioKeepalive.h).
void KosHost::PumpAudio() {
    if (m_Stream < 0) return;
    if (m_AudioPump) m_AudioPump();
    snd_stream_poll(m_Stream);
}

void* KosHost::StreamCallback(int hnd, int smpReq, int* smpRecv) {
    (void)hnd;
    if (!g_host) {
        *smpRecv = 0;
        return nullptr;
    }
    return g_host->FillStream(smpReq, smpRecv);
}

void* KosHost::FillStream(int bytesReq, int* bytesRecv) {
    int want = bytesReq / 2;  // samples
    if (want > static_cast<int>(m_Stage.size())) want = static_cast<int>(m_Stage.size());

    int have = want < m_RingCount ? want : m_RingCount;
    for (int i = 0; i < have; ++i) {
        m_Stage[i] = m_Ring[m_RingHead];
        m_RingHead = (m_RingHead + 1) % kRingSamples;
        --m_RingCount;
    }
    // Silence rather than a short read when the game is behind: the stream
    // keeps running and picks the mix back up, where a starved stream would
    // have to be restarted.
    for (int i = have; i < want; ++i) m_Stage[i] = 0;

    *bytesRecv = want * 2;
    return m_Stage.data();
}

// ---------------------------------------------------------------------------
// Device frames
//
// The container is scripts/mkframes.py's work: the desktop build's art, scaled
// to the field's height, cropped to the strip that fits beside it, composited
// over black and packed to RGB565 with each row padded out to a power-of-two
// width. All this has to do is read it and hand two strips to the PVR.

void KosHost::OpenFrames(const char* path) {
    m_Frames.clear();
    m_FramesPath.clear();
    m_Frame = -1;
    if (!path) return;

    std::FILE* f = std::fopen(path, "rb");
    if (!f) return;

    uint8_t header[12];
    if (std::fread(header, 1, sizeof(header), f) != sizeof(header) ||
        std::memcmp(header, "BBDF", 4) != 0 || GetU16(header + 4) != 1) {
        std::fclose(f);
        return;
    }
    const int count = GetU16(header + 6);
    const int height = GetU16(header + 8);
    if (height != kScreenH || count <= 0) {
        // Cut for a different screen than this one: better no frame than a
        // stretched one.
        std::fclose(f);
        return;
    }

    constexpr int kRecord = 56;
    std::vector<uint8_t> table(static_cast<std::size_t>(count) * kRecord);
    if (std::fread(table.data(), 1, table.size(), f) != table.size()) {
        std::fclose(f);
        return;
    }

    for (int i = 0; i < count; ++i) {
        const uint8_t* r = table.data() + static_cast<std::size_t>(i) * kRecord;
        DeviceFrame frame;
        frame.ID.assign(reinterpret_cast<const char*>(r),
                        strnlen(reinterpret_cast<const char*>(r), 16));
        frame.Label.assign(reinterpret_cast<const char*>(r + 16),
                           strnlen(reinterpret_cast<const char*>(r + 16), 24));
        frame.LeftW = GetU16(r + 40);
        frame.RightW = GetU16(r + 42);
        frame.TexW = GetU16(r + 44);
        frame.LeftOff = GetU32(r + 48);
        frame.RightOff = GetU32(r + 52);
        if (frame.ID.empty() || frame.TexW <= 0) continue;
        m_Frames.push_back(std::move(frame));
    }
    std::fclose(f);
    m_FramesPath = path;
    LogDebug("frames: %s (%d)\n", path, FrameCount());
}

int KosHost::FrameCount() const { return static_cast<int>(m_Frames.size()); }

const char* KosHost::FrameId(int index) const {
    if (index < 0 || index >= FrameCount()) return "";
    return m_Frames[static_cast<std::size_t>(index)].ID.c_str();
}

const char* KosHost::FrameLabel(int index) const {
    if (index < 0 || index >= FrameCount()) return "";
    return m_Frames[static_cast<std::size_t>(index)].Label.c_str();
}

const char* KosHost::Frame() const { return FrameId(m_Frame); }

void KosHost::SetFrame(const char* id) {
    m_Frame = -1;
    // An id this host does not have is a frame that is not installed, not an
    // error: the settings file keeps naming it, so putting the art back on the
    // disc restores it.
    if (id && *id) {
        for (std::size_t i = 0; i < m_Frames.size(); ++i) {
            if (m_Frames[i].ID != id) continue;
            m_Frame = static_cast<int>(i);
            break;
        }
    }
    UploadFrame();
}

void KosHost::UploadFrame() {
    if (m_Frame < 0 || m_FramesPath.empty()) return;
    const DeviceFrame& f = m_Frames[static_cast<std::size_t>(m_Frame)];

    // One pair of textures, reused: switching frames in the settings screen
    // overwrites them rather than allocating again.
    if (m_FrameTexW != f.TexW) {
        for (void*& tex : m_FrameTex) {
            if (tex) pvr_mem_free(static_cast<pvr_ptr_t>(tex));
            tex = pvr_mem_malloc(f.TexW * kFrameTexH * 2);
        }
        m_FrameTexW = f.TexW;
    }
    if (!m_FrameTex[0] || !m_FrameTex[1]) {
        m_Frame = -1;
        return;
    }

    // Straight off the disc into texture memory, one half at a time, and the
    // staging buffer goes away with this function. Whole-strip transfers: the
    // rows were padded to a power of two at build time precisely so each half
    // is one 32-byte-aligned run.
    std::FILE* file = std::fopen(m_FramesPath.c_str(), "rb");
    if (!file) {
        m_Frame = -1;
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(f.TexW) * kScreenH * 2;
    std::vector<uint8_t> strip(bytes);
    const uint32_t offsets[2] = {f.LeftOff, f.RightOff};
    for (int half = 0; half < 2; ++half) {
        if (std::fseek(file, static_cast<long>(offsets[half]), SEEK_SET) != 0 ||
            std::fread(strip.data(), 1, bytes, file) != bytes) {
            m_Frame = -1;  // truncated: a frame that is not installed
            break;
        }
        pvr_txr_load(strip.data(), static_cast<pvr_ptr_t>(m_FrameTex[half]),
                     bytes);
    }
    std::fclose(file);
}

// ---------------------------------------------------------------------------
// Saves

namespace {

// The first memory card in a controller port, or nothing.
//
// A port is not a slot: unit 0 is usually the controller and the units above
// it its two expansion slots, but a card plugged straight into the bus answers
// at unit 0 instead. So every unit is asked and the first that says it is a
// memory card wins, which is what KOS's own vmufs does when it builds `/vmu`.
// That is also the right unit for the player: they know which pad is theirs,
// not which of its slots a card went into, so the picker offers ports.
maple_device_t* CardInPort(int port) {
    for (int unit = 0; unit < MAPLE_UNIT_COUNT; ++unit) {
        maple_device_t* dev = maple_enum_dev(port, unit);
        if (dev && (dev->info.functions & MAPLE_FUNC_MEMCARD)) return dev;
    }
    return nullptr;
}

}  // namespace

void KosHost::SetSaveLabels(VmuStorage::Labels labels) {
    m_SaveLabels = std::move(labels);
    // The table arrives at startup, by which time the boot has already bound a
    // card to read the settings off; without this that card would go on
    // writing bare headers. Re-binding the same bay is the whole fix.
    if (m_Saves) SetSaveBay(m_SaveBay);
}

void KosHost::BindCard(void* device) {
    maple_device_t* dev = static_cast<maple_device_t*>(device);
    char path[16];
    std::snprintf(path, sizeof(path), "/vmu/%c%d",
                  static_cast<char>('a' + dev->port), dev->unit);
    m_Saves = std::make_unique<VmuStorage>(path, &m_SaveLabels);
    // A card operation is seconds of maple traffic with the game thread asleep
    // inside it, and the game thread is what feeds the AICA. This is the card
    // being told how to keep the music going without it.
    m_Saves->SetKeepalive([this] { PumpAudio(); });
    m_SaveBay = dev->port;
}

void KosHost::OpenSaves(int port, int unit) {
    m_Saves.reset();
    m_SaveBay = -1;
    maple_device_t* dev = nullptr;
    if (port >= 0 && unit >= 0) {
        dev = maple_enum_dev(port, unit);
        if (dev && !(dev->info.functions & MAPLE_FUNC_MEMCARD)) dev = nullptr;
    } else {
        dev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
    }
    if (!dev) return;  // no card: the game will refuse to save, politely
    BindCard(dev);
}

Storage* KosHost::Saves() { return m_Saves.get(); }

bool KosHost::SaveBayReady(int bay) const {
    return bay >= 0 && bay < kSaveBays && CardInPort(bay) != nullptr;
}

const char* KosHost::SaveBayLabel(int bay) const {
    // The letters printed on the machine, above the ports themselves.
    static const char* const kNames[kSaveBays] = {"A", "B", "C", "D"};
    return bay >= 0 && bay < kSaveBays ? kNames[bay] : "";
}

// Choosing a bay is the one way game saves get a home, and -1 -- the player
// asking to play without a card -- is a real answer rather than a failure: it
// leaves `Saves()` null, which is what stops the between-mission checkpoint
// from quietly writing somewhere they did not choose.
bool KosHost::SetSaveBay(int bay) {
    m_Saves.reset();
    m_SaveBay = -1;
    if (bay < 0 || bay >= kSaveBays) return false;
    maple_device_t* dev = CardInPort(bay);
    if (!dev) return false;  // pulled between being drawn and being chosen
    BindCard(dev);
    return true;
}

}  // namespace bb
