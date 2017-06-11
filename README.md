BS2PC
=====

Converts Half-Life PlayStation 2 (`.bs2`) maps to PC (`.bsp`).

Usage: `bs2pc path_to_map.bs2` or drag and drop. The `.bsp` file will be placed in the `.bs2` directory.

See the [/releases Releases section] for the Windows binary.

Features:
* Converts all sections of PlayStation 2 maps.
* Automatically decompresses the map.
* Changes `.dol` and `.spz` file extensions to `.mdl` and `.spr` in the entities.
* Removes `nodraw`-textured surfaces such as the edges of the handrails near the Sector C entrance.

Half-Life on PS2 uses DEFLATE compression for its maps and stores its internal structures (like the `m`-prefixed structures in GLQuake as opposed to the `d`-prefixed ones in the software-rendered engines and that are used in Quake and Half-Life `.bsp`) in the map files. This is part of the reason why the PlayStation 2 version loads levels much faster than the Dreamcast one. You can see some differences in the source file, the PC structures have `_id_t` in the name, and the PlayStation 2 ones have `_gbx_t`. Many fields (such as texture animation linked lists) were omitted though because they are not needed in the PC format. Two sections, very similar to clipnodes, are also skipped.

On the PC maps after playing the PS2 ones or vice versa, some textures may look different (from minor power of 2 scaling artifacts to completely different images) as the textures have common names. If you want to switch between the two versions, restart the game so everything looks correct.

Licensed under GPLv2 (except for zlib) as it's derived from the Quake engine source.
