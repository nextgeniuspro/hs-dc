// Engine resource slots.
//
// The engine object carries one array of resource pointers addressed by integer
// id (System::getResource at 0x100673d8, setResource at 0x100673e4). Ported
// code keeps the original ids so it can look things up the way the game does,
// and so traces from EKA2L1 line up with the port without translation.
//
// The named ids below are the ones the startup path (0x1008d478) fills in;
// kMenuTextures is the bulk texture table it registers, generated from the
// binary into ResourceTable.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace bb {

// Slots the port refers to by name. All are set up by the startup loader.
enum ResourceId : uint16_t {
    kResSomeFlagA = 0x16,       // 1-byte flag, cleared at startup
    kResBattleSettings = 0x17,  // 0x30 bytes (FUN_10050660)
    kResSystemConfig = 0x18,    // holds the language index at +0x10
    kResSomeObject1F = 0x1f,    // 0x1c bytes (FUN_1009c14c)
    kResSettings = 0x1d,        // 0x2ac bytes (FUN_100626fc)
    kResTextureManager = 0x21,  // the .tc loader/cache (FUN_100b56e4)
    kResFontSmall = 0x22,       // Data\font-small.tc
    kResFullboard = 0x32,       // Data\Menu\fullboard.tc (loaded separately)
    kResFontBig = 0xc2,         // Data\font-big.tc
    kResSound = 0xd6,           // sound manager (FUN_100963dc)
    kResMenuContext = 0xd7,     // passed to every menu state as its context
    kResAppFilePack = 0xd8,     // FilePack over e:\system\apps\6R36
};

struct TextureSlot {
    uint16_t ID;
    const char* Path;  // game path, backslash-separated as the engine stores it
};

// The 76 menu textures the startup loader registers, in load order.
extern const TextureSlot kMenuTextures[];
extern const std::size_t kMenuTextureCount;

}  // namespace bb
