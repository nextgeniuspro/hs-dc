// Engine resource slots — the id -> asset table the original builds at startup.
//
// main.dll keeps one array of resource pointers on the engine object and hands
// them out by integer id (System::getResource, FUN_100673d8 / setResource,
// FUN_100673e4). The loader at 0x1008d478 registers these 76 menu textures via
// FUN_1008d1d8(texMgr, id, path), which loads the .tc and stores it in the slot.
//
// The ids are the engine's own, so keeping them lets ported draw code look
// resources up exactly the way the original does. Paths and ids were extracted
// straight from the binary, and every one resolves to a real pak entry.
//
// This file is generated; see tools/README.md.
#include "game/ResourceTable.h"

namespace bb {

const TextureSlot kMenuTextures[] = {
    {0x56, "Data\\Menu\\bv_prop_bg.tc"},
    {0xcf, "Data\\Menu\\angle_arrow.tc"},
    {0xc7, "Data\\Menu\\select_small.tc"},
    {0xc8, "Data\\Menu\\select_big.tc"},
    {0xc9, "Data\\Menu\\select_name.tc"},
    {0xca, "Data\\Menu\\commander_bg.tc"},
    {0xcb, "Data\\Menu\\terrain_bg.tc"},
    {0xbf, "Data\\anim\\sub-bg.tc"},
    {0xc0, "Data\\anim\\sub-bga.tc"},
    {0xdb, "Data\\anim\\sub-name.tc"},
    {0xdc, "Data\\anim\\sub-name-end.tc"},
    {0xde, "Data\\anim\\sub-name-top.tc"},
    {0xdd, "Data\\anim\\sub-name-start.tc"},
    {0xea, "Data\\anim\\sub-name-secondary.tc"},
    {0xbb, "Data\\Menu\\perkpointer.tc"},
    {0xb7, "Data\\Menu\\bv_square.tc"},
    {0x25, "Data\\Menu\\bv_board.tc"},
    {0xb6, "Data\\Menu\\bv_arrow.tc"},
    {0x2c, "Data\\Menu\\sword.tc"},
    {0x2f, "Data\\Menu\\bv_small_board.tc"},
    {0x27, "Data\\Menu\\volume_tab.tc"},
    {0x4d, "Data\\icons\\amount.tc"},
    {0x29, "Data\\Menu\\name_board.tc"},
    {0x2a, "Data\\Menu\\scrollup.tc"},
    {0x2b, "Data\\Menu\\scrolldown.tc"},
    {0xcc, "Data\\Menu\\buildscrollup.tc"},
    {0xcd, "Data\\Menu\\buildscrolldown.tc"},
    {0xb4, "Data\\Menu\\scroll_bg.tc"},
    {0x28, "Data\\Menu\\pointer.tc"},
    {0x33, "Data\\Menu\\routearrow.tc"},
    {0x30, "Data\\Menu\\board.tc"},
    {0xb0, "Data\\Menu\\left_sb.tc"},
    {0xb1, "Data\\Menu\\right_sb.tc"},
    {0x31, "Data\\icons\\marker.tc"},
    {0x34, "Data\\icons\\defense.tc"},
    {0x35, "Data\\icons\\ammo.tc"},
    {0x36, "Data\\icons\\health.tc"},
    {0x37, "Data\\icons\\money.tc"},
    {0x38, "Data\\icons\\rations.tc"},
    {0x39, "Data\\icons\\capture.tc"},
    {0x3a, "Data\\icons\\vision.tc"},
    {0x3b, "Data\\icons\\movement.tc"},
    {0x3c, "Data\\icons\\movement_bonus.tc"},
    {0x3d, "Data\\icons\\unit_type\\foot.tc"},
    {0x3e, "Data\\icons\\unit_type\\horse.tc"},
    {0x3f, "Data\\icons\\unit_type\\carriage.tc"},
    {0x40, "Data\\icons\\unit_type\\sails.tc"},
    {0x41, "Data\\icons\\unit_type\\rowing.tc"},
    {0x4b, "Data\\cursor\\cursor.tc"},
    {0x4c, "Data\\cursor\\attack_cursor.tc"},
    {0x42, "Data\\cursor\\attack.tc"},
    {0x43, "Data\\cursor\\build.tc"},
    {0x44, "Data\\cursor\\cancel.tc"},
    {0x45, "Data\\cursor\\capture.tc"},
    {0x46, "Data\\cursor\\invalid.tc"},
    {0x47, "Data\\cursor\\join.tc"},
    {0x48, "Data\\cursor\\load.tc"},
    {0x49, "Data\\cursor\\selection.tc"},
    {0x4a, "Data\\cursor\\unload.tc"},
    {0x9a, "Data\\icons\\unit_class\\infantry.tc"},
    {0x9b, "Data\\icons\\unit_class\\artillery.tc"},
    {0x9c, "Data\\icons\\unit_class\\cavalry.tc"},
    {0x9d, "Data\\icons\\unit_class\\cannon_towers.tc"},
    {0x9e, "Data\\icons\\unit_class\\small_sea.tc"},
    {0x9f, "Data\\icons\\unit_class\\large_ships.tc"},
    {0xa4, "Data\\icons\\combat_type\\capture.tc"},
    {0xa5, "Data\\icons\\combat_type\\close.tc"},
    {0xa6, "Data\\icons\\combat_type\\indirect.tc"},
    {0xa7, "Data\\icons\\combat_type\\transport.tc"},
    {0xa8, "Data\\icons\\combat_type\\stationary.tc"},
    {0xa9, "Data\\icons\\combat_type\\production.tc"},
    {0xa0, "Data\\icons\\unit_class\\attack.tc"},
    {0xa1, "Data\\icons\\unit_class\\defense.tc"},
    {0xa2, "Data\\icons\\range_close.tc"},
    {0xa3, "Data\\icons\\range_indirect.tc"},
    {0xd1, "Data\\Menu\\header_bg.tc"},
};

const std::size_t kMenuTextureCount =
    sizeof(kMenuTextures) / sizeof(kMenuTextures[0]);

}  // namespace bb
