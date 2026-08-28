#include "platform/VmuStorage.h"

#include <algorithm>
#include <cstring>

#include <arch/timer.h>
#include <dc/fs_vmu.h>
#include <dc/vmu_pkg.h>
#include <kos/fs.h>

#include "platform/AudioKeepalive.h"
#include "shim/Log.h"

namespace bb {

static_assert(sizeof(vmu_hdr_t) == kVmsHeaderBytes,
              "the VMS header is 128 bytes and the block arithmetic says so");

namespace {

// Sixteen characters is what the file manager lists and thirty-two what it
// shows when a file is opened. KOS copies both in with `strlen` and no bound
// (vmu_pkg.c's `memcpy(hdr->DescShort, src->DescShort, strlen(...))`), so
// anything longer would run into the next field of the header; they are cut
// here instead. The application id is `strcpy`d into sixteen bytes including
// its terminator, so it gets fifteen.
constexpr std::size_t kDescShort = 16;
constexpr std::size_t kDescLong = 32;
constexpr char kAppId[] = "BLACKBEARD";
static_assert(sizeof(kAppId) <= 16, "the VMS app id is fifteen characters");

void CopyBounded(char* dst, std::size_t room, const std::string& src) {
    const std::size_t n = src.size() < room ? src.size() : room;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

// The record the card actually holds: our own four-byte magic and the exact
// payload length, then the payload. The length is what earns its keep -- a
// card stores whole 512-byte blocks and hands them all back, and a save's CRC
// covers every byte after its own header, so a blob padded on the way out and
// unpadded on the way in would fail to load every time.
constexpr char kMagic[4] = {'B', 'B', 'V', 'M'};
constexpr std::size_t kRecordHeader = 8;

void PutU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

uint32_t GetU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Read a whole VMU file. KOS has already parsed and skipped the VMS header, so
// what comes back is the record written below, rounded up to a whole block.
bool ReadFile(const std::string& path, std::vector<uint8_t>& raw) {
    raw.clear();
    const file_t fd = fs_open(path.c_str(), O_RDONLY);
    if (fd == FILEHND_INVALID) return false;
    const ssize_t total = fs_total(fd);
    if (total <= 0) {
        fs_close(fd);
        return false;
    }
    raw.resize(static_cast<std::size_t>(total));
    const ssize_t got = fs_read(fd, raw.data(), raw.size());
    fs_close(fd);
    if (got != total) {
        raw.clear();
        return false;
    }
    return true;
}

bool LooksLikeOurs(const std::vector<uint8_t>& raw) {
    return raw.size() >= kRecordHeader &&
           std::memcmp(raw.data(), kMagic, sizeof(kMagic)) == 0 &&
           GetU32(raw.data() + 4) <= raw.size() - kRecordHeader;
}

}  // namespace

VmuStorage::VmuStorage(std::string dir, const Labels* labels)
    : m_Dir(std::move(dir)), m_Labels(labels) {
    // Without a header a file is a nameless lump in the console's file
    // manager, which is where a player goes to see how much of their card the
    // game is using. Slots that have a header of their own get it at write
    // time; this is the fallback for any that do not, and KOS attaches it as
    // each such file is closed.
    vmu_pkg_t pkg{};
    CopyBounded(pkg.desc_short, kDescShort, "Blackbeard");
    CopyBounded(pkg.desc_long, kDescLong, "Blackbeard saved game");
    CopyBounded(pkg.app_id, sizeof(kAppId) - 1, kAppId);
    pkg.icon_cnt = 0;
    pkg.icon_anim_speed = 0;
    pkg.eyecatch_type = VMUPKG_EC_NONE;
    fs_vmu_set_default_header(&pkg);
}

const VmsLabel* VmuStorage::LabelFor(const std::string& name) const {
    if (!m_Labels) return nullptr;
    const auto it = m_Labels->find(name);
    return it == m_Labels->end() ? nullptr : &it->second;
}

std::size_t VmuStorage::HeaderBytes() const {
    // The largest of them, because the figure is quoted before a slot is
    // named and quoting the small one would promise a block the game may not
    // be able to keep.
    std::size_t most = kVmsHeaderBytes;
    if (m_Labels) {
        for (const auto& [name, label] : *m_Labels) {
            const std::size_t bytes = label.HeaderBytes();
            if (bytes > most) most = bytes;
        }
    }
    return most;
}

std::size_t VmuStorage::RecordBytes() const { return kRecordHeader; }

// KOS copies the package and everything it points at before this returns
// (fs_vmu.c's IOCTL_VMU_SET_HDR calls vmu_pkg_dup), so the label may be a
// temporary as far as the filesystem is concerned -- and `icon_data` is a
// non-const pointer in a struct KOS only ever reads through, which is what the
// cast is for.
void VmuStorage::ApplyLabel(int fd, const std::string& name) const {
    const VmsLabel* label = LabelFor(name);
    if (!label) return;  // the default header covers it

    vmu_pkg_t pkg{};
    CopyBounded(pkg.desc_short, kDescShort, label->DescShort);
    CopyBounded(pkg.desc_long, kDescLong, label->DescLong);
    CopyBounded(pkg.app_id, sizeof(kAppId) - 1, kAppId);
    pkg.icon_cnt = label->Frames();
    pkg.icon_anim_speed = label->AnimSpeed;
    pkg.eyecatch_type = VMUPKG_EC_NONE;
    std::memcpy(pkg.icon_pal, label->Palette, sizeof(pkg.icon_pal));
    pkg.icon_data = const_cast<uint8_t*>(label->Bitmap.data());
    fs_vmu_set_header(static_cast<file_t>(fd), &pkg);
}

std::string VmuStorage::PathFor(const std::string& name) const {
    return m_Dir + "/" + name;
}

bool VmuStorage::Read(const std::string& name, std::vector<uint8_t>& out) {
    out.clear();
    if (!ValidSlotName(name)) return false;
    // Reading is quicker than writing -- no flash cycle -- but it is still the
    // card's whole directory and then the file, which is long enough for the
    // stream to run dry. Every path that touches the bus takes the guard.
    const AudioKeepalive alive(m_Keepalive);
    std::vector<uint8_t> raw;
    if (!ReadFile(PathFor(name), raw)) return false;
    if (!LooksLikeOurs(raw)) return false;  // someone else's file, or rotted
    const uint32_t len = GetU32(raw.data() + 4);
    out.assign(raw.begin() + kRecordHeader,
               raw.begin() + kRecordHeader + len);
    return true;
}

bool VmuStorage::Write(const std::string& name, const uint8_t* data,
                       std::size_t size) {
    if (!ValidSlotName(name)) return false;
    if (size > kCapacity) return false;
    if (!data && size) return false;

    std::vector<uint8_t> raw(kRecordHeader + size);
    std::memcpy(raw.data(), kMagic, sizeof(kMagic));
    PutU32(raw.data() + 4, static_cast<uint32_t>(size));
    if (size) std::memcpy(raw.data() + kRecordHeader, data, size);

    // The long one: a couple of dozen blocks, each a bus exchange and a flash
    // commit, all of it inside fs_close. The music would stop dead here
    // without the guard -- see AudioKeepalive.h.
    const AudioKeepalive alive(m_Keepalive);
    const uint64_t started = timer_ms_gettime64();

    // O_TRUNC so the slot is replaced rather than overwritten in place: the
    // card's directory entry is rewritten once, at close, after the new blocks
    // are down. That is as close to atomic as flash gets -- a card pulled
    // mid-write loses the save, but cannot leave half of the new one wearing
    // the old one's name.
    const file_t fd = fs_open(PathFor(name).c_str(), O_WRONLY | O_TRUNC);
    if (fd == FILEHND_INVALID) return false;
    // Before the bytes, so the header that goes down with them is this slot's
    // and not the last slot's.
    ApplyLabel(static_cast<int>(fd), name);
    const ssize_t wrote = fs_write(fd, raw.data(), raw.size());
    const int closed = fs_close(fd);
    const bool ok = wrote == static_cast<ssize_t>(raw.size()) && closed >= 0;
    // How long a card actually takes is the whole reason the guard is here,
    // so the figure that justifies it is worth a line of the log.
    LogDebug("card: %s '%s' (%u blocks) in %u ms%s\n", ok ? "wrote" : "failed",
             name.c_str(), static_cast<unsigned>(BlocksFor(size)),
             static_cast<unsigned>(timer_ms_gettime64() - started),
             alive.Running() ? "" : ", sound stalled");
    return ok;
}

bool VmuStorage::Remove(const std::string& name) {
    if (!ValidSlotName(name)) return false;
    // Unlinking is a directory rewrite, which is flash: shorter than a save
    // and long enough to hear.
    const AudioKeepalive alive(m_Keepalive);
    return fs_unlink(PathFor(name).c_str()) == 0;
}

bool VmuStorage::Exists(const std::string& name) {
    if (!ValidSlotName(name)) return false;
    const AudioKeepalive alive(m_Keepalive);
    const file_t fd = fs_open(PathFor(name).c_str(), O_RDONLY);
    if (fd == FILEHND_INVALID) return false;
    fs_close(fd);
    return true;
}

std::vector<std::string> VmuStorage::List() {
    std::vector<std::string> names;
    // The longest read there is -- every candidate file on the card, whole --
    // and it runs behind the load and erase screens, which are exactly where
    // the menu theme is playing.
    const AudioKeepalive alive(m_Keepalive);
    const file_t dir = fs_open(m_Dir.c_str(), O_RDONLY | O_DIR);
    if (dir == FILEHND_INVALID) return names;

    // A card is shared with every other game the player owns, so a name alone
    // proves nothing: each candidate is opened and checked for our magic
    // before it is called ours. There are at most 200 files on a card and this
    // runs when a player asks to see or erase their saves, never in a frame.
    std::vector<std::string> candidates;
    while (const dirent_t* ent = fs_readdir(dir)) {
        if (ent->size < 0) continue;  // a subdirectory; a card has none
        const std::string name = ent->name;
        if (ValidSlotName(name)) candidates.push_back(name);
    }
    fs_close(dir);

    std::vector<uint8_t> raw;
    for (const std::string& name : candidates) {
        if (ReadFile(PathFor(name), raw) && LooksLikeOurs(raw))
            names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace bb
