// An SD card, mounted at /sd — where a disc with no game data on it looks.
#pragma once

namespace bb {

// Bring up an SD card and mount its FAT filesystem read-only at /sd. True if
// /sd is usable when this returns, including when an earlier call had already
// mounted it.
bool MountSdCard();

// Let go of the card: unmount /sd and shut the reader down
void UnmountSdCard();

}  // namespace bb
