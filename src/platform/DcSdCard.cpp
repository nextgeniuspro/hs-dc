#include "platform/DcSdCard.h"

#include <cstdint>

#include <dc/sd.h>
#include <fat/fs_fat.h>
#include <kos/blockdev.h>

#include "shim/Log.h"

namespace bb {

namespace {

constexpr const char* kMountPoint = "/sd";

bool g_mounted = false;

// Get the card talking. 
bool StartCard() {
    const sd_init_params_t readers[] = {
        {SD_IF_SCIF, true},
#ifdef BB_SD_SCI
        {SD_IF_SCI, true},
#endif
    };
    for (const sd_init_params_t& params : readers) {
        if (sd_init_ex(&params) == 0) {
            LogInfo("sd: card on %s\n",
                    params.interface == SD_IF_SCIF ? "SCIF" : "SCI");
            return true;
        }
    }
    return false;
}

// Find the filesystem on the card and put it at /sd.
bool MountCard() {
    kos_blockdev_t dev;
    uint8_t type = 0;

    // A card as a PC formats it: an MBR, with the filesystem in one of its
    // four slots and nearly always the first.
    for (int partition = 0; partition < 4; ++partition) {
        if (sd_blockdev_for_partition(partition, &dev, &type) < 0) continue;
        if (fs_fat_mount(kMountPoint, &dev, FS_FAT_MOUNT_READONLY) == 0) {
            LogInfo("sd: partition %d (type 0x%02x) at %s\n", partition, type,
                    kMountPoint);
            return true;
        }
    }

    // And a card as a camera formats it: no partition table at all, the
    // filesystem starting at the first block.
    if (sd_blockdev_for_device(&dev) == 0 &&
        fs_fat_mount(kMountPoint, &dev, FS_FAT_MOUNT_READONLY) == 0) {
        LogInfo("sd: unpartitioned card at %s\n", kMountPoint);
        return true;
    }

    return false;
}

}  // namespace

bool MountSdCard() {
    if (g_mounted) return true;

    if (!StartCard()) {
        LogInfo("sd: no card\n");
        return false;
    }
    if (fs_fat_init() < 0 || !MountCard()) {
        LogError("sd: card reads, but carries no FAT filesystem\n");
        sd_shutdown();
        return false;
    }

    g_mounted = true;
    return true;
}

void UnmountSdCard() {
    if (!g_mounted) return;
    fs_fat_unmount(kMountPoint);
    sd_shutdown();
    g_mounted = false;
}

}  // namespace bb
