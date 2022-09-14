# BS2PC — Half-Life PlayStation 2 map converter

Converts maps between the PlayStation 2 and the PC versions of Half-Life, also supporting converting Quake maps to Half-Life for the PS2 or the PC.

The Windows 32-bit x86 binary (built using Microsoft Visual Studio 2019, so may require the [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x86.exe)) is available in the [https://github.com/Triang3l/BS2PC/releases](Releases).

## Features

- Use of the original PC textures from WADs for PS2 to PC conversion when they're identical to the PS2 conversions, restoring pixel-perfect details lost due to resampling to powers of two for the PS2, and full 24-bit colors to replace the 21-bit palettes in PS2 textures.
- Use of the original PS2 conversions gathered from map files for PC to PS2 conversion when they're identical to the PC textures, eliminating texture popping when moving between converted and the original PS2 maps.
- Converting Quake maps to both the PS2 and the PC versions of Half-Life, with subdivision of liquid surfaces to limit their size to prevent the "bad surface extents" error on the software renderer.
- Changing model and sprite paths and file extensions in the entities between the versions of Half-Life and Quake.
- Removal of `NODRAW`-textured surfaces from PS2 maps during conversion to the PC.
- Reconstruction of [randomly tiling texture sets](https://youtu.be/NNv17T02WlY) on maps converted from the PS2 for the PC software renderer.
- Fixup (deinterleaving and inversion) of incorrectly converted randomly tiling textures from certain PS2 Half-Life maps, such as the thick blue strip in the top of Stalkyard that is supposed to be a thin yellow line.
- Bicubic resampling of textures to powers of two for the PS2 and back.
- Extracting textures from PS2 maps to .tga images.
- Research and debug functionality: decompression and compression of PS2 map files, extracting subdivided liquid and transparent surface polygons to .obj files.

## Usage

You can specify one or multiple input files (multiple are recommended for speed since texture data can be shared between them) as arguments.

To specify the output path, use the `-o "path"` or `-output "path"` option. For a single map, it will be treated as the file path by default (unless the directory with the specified path already exists), for multiple, it's the directory path. If no output path is provided, the generated maps will be placed in the same location, but with the target file extension.

This page describes only the basic use cases. For all available options, run the application without any input files to see a list of them, or see the location where they're printed in [bs2pc.cpp](bs2pc.cpp).

### Useful tools

- [Apache3](https://www.romhacking.net/utilities/584/) — a disc image editor capable of extracting files, as well as replacing files in PS2 disc images without changing their layout. You can modify `pak0.pak` as long as you don't make it larger, and then replace it in the disc image by right-clicking it in Apache3 and pressing "Replace Selected File", with "Update TOC" unchecked, and "Ignore File Size Differences" checked.
- [PakScape](https://gamebanana.com/tools/2548) — a browser and editor of .pak files used by Quake and Half-Life on both the PC and the PS2, that can be used to extract maps and other files from Half-Life .pak files, as well as to replace files in them.
- [PS2 Half-Life tools](https://github.com/supadupaplex/ps2-hl-tools) — a collection of converters and other tools for various file formats used in the PS2 version of Half-Life, including .dol models, .spz sprites, and many more.
- [Yet another PS2 Half-Life PC port](https://www.moddb.com/mods/yet-another-ps2-half-life-pc-port) — a Half-Life PC mod with converted PS2 resources and implementations of PS2 entities.
- [PCSX2](https://pcsx2.net/) — a PlayStation 2 emulator for Windows, GNU/Linux and macOS.
- [AetherSX2](https://aethersx2.com/) — a PlayStation 2 emulator for Android.

### PS2 maps to PC

For the highest texture quality, during conversion, specify the paths to the WAD files used by the maps — the `valve` game directory, and if needed, the mod directory — using the `-waddir "path_to_wads" -waddir "path_to_other_wads"` arguments. This will allow BS2PC to locate the original texture data, compare it with the textures in the map file, and where possible, use the original PC texture with 24-bit color and with pixel-perfect details preserved instead of converting 21-bit PS2 textures resampled to powers of two. Also, WAD files are required for restoring randomized tiling of textures on the software renderer.

Provide the input `.bs2` files as the argument. The resulting maps will have the `.bsp` file extension.

### PC Half-Life or Quake maps to PS2

First, it's highly recommended to create a file containing the original Gearbox's conversions of the PC textures. BS2PC's texture conversion is not exactly the same as used by Gearbox, resampling textures to powers of two differently, including choosing their size.

To do that, you need to extract all Half-Life and Decay maps from `valve/pak0.pak` and `decay/pak0.pak` on the PS2 Half-Life disc, and then run the following command (assuming the maps are in the working directory):

`bs2pc -mode createps2texturefile basement.bs2 c0a0.bs2 c0a0a.bs2 c0a0b.bs2 c0a0c.bs2 c0a0d.bs2 c0a0e.bs2 c1a0.bs2 c1a0a.bs2 c1a0b.bs2 c1a0c.bs2 c1a0d.bs2 c1a0e.bs2 c1a1.bs2 c1a1a.bs2 c1a1b.bs2 c1a1c.bs2 c1a1d.bs2 c1a1f.bs2 c1a2.bs2 c1a2a.bs2 c1a2b.bs2 c1a2c.bs2 c1a2d.bs2 c1a3.bs2 c1a3a.bs2 c1a3b.bs2 c1a3c.bs2 c1a3d.bs2 c1a4.bs2 c1a4b.bs2 c1a4d.bs2 c1a4e.bs2 c1a4f.bs2 c1a4g.bs2 c1a4i.bs2 c1a4j.bs2 c1a4k.bs2 c2a1.bs2 c2a1a.bs2 c2a1b.bs2 c2a2.bs2 c2a2a.bs2 c2a2b1.bs2 c2a2b2.bs2 c2a2c.bs2 c2a2d.bs2 c2a2e.bs2 c2a2f.bs2 c2a2g.bs2 c2a2h.bs2 c2a3.bs2 c2a3a.bs2 c2a3b.bs2 c2a3c.bs2 c2a3d.bs2 c2a3e.bs2 c2a4.bs2 c2a4a.bs2 c2a4b.bs2 c2a4c.bs2 c2a4d.bs2 c2a4e.bs2 c2a4f.bs2 c2a4g.bs2 c2a5.bs2 c2a5a.bs2 c2a5b.bs2 c2a5c.bs2 c2a5d.bs2 c2a5e.bs2 c2a5f.bs2 c2a5g.bs2 c2a5w.bs2 c2a5x.bs2 c3a1.bs2 c3a1a.bs2 c3a1b.bs2 c3a2.bs2 c3a2a.bs2 c3a2b.bs2 c3a2c.bs2 c3a2d.bs2 c3a2e.bs2 c3a2f.bs2 c4a1.bs2 c4a1a.bs2 c4a1b.bs2 c4a1c.bs2 c4a1d.bs2 c4a1e.bs2 c4a1f.bs2 c4a2.bs2 c4a2a.bs2 c4a2b.bs2 c4a3.bs2 c5a1.bs2 datacore2.bs2 debris.bs2 ht01accident.bs2 ht01accident2.bs2 ht02hazard.bs2 ht03uplink.bs2 ht04dampen.bs2 ht05dorms.bs2 ht07signal.bs2 ht10focus.bs2 ht11lasers.bs2 ht12fubar.bs2 ht91alien.bs2 htoutro.bs2 office.bs2 signal.bs2 skirmish.bs2 snark_pit2.bs2 stalkyard2.bs2 t0a0.bs2 t0a0a.bs2 t0a0b1.bs2 t0a0b2.bs2 t0a0d.bs2 water_canal.bs2 waypoint.bs2`

This will create the file containing the original texture data from all the maps, `hlps2.bs2pcwad`, in the working directory. You can override the destination file path via `-o "path_to_texture_file.bs2pcwad"` or `-ps2texturefile "path_to_texture_file.bs2pcwad"`.

You can also run this command multiple times for different maps (especially if you're hitting the command length limit of your operating system, primarily if the paths to the maps specified include directories), textures from multiple invocations will be accumulated in the file.

BS2PC will automatically be using that file when converting maps to the PS2 later. If you've placed it in a different location, you'll need to specify it via `-ps2texturefile "path_to_texture_file.bs2pcwad"` when converting maps.

During conversion, you must specify the paths to the WAD files used by the maps — the `valve` game directory, and if needed, the mod directory — using the `-waddir "path_to_wads" -waddir "path_to_other_wads"` arguments. This is mandatory for maps that don't have all of their textures included in the map file, so BS2PC can obtain the texture data.

Provide the input `.bsp` files as the arguments. The resulting maps will have the `.bs2` file extension.

To convert Half-Life alpha version 0.52 maps (which are Half-Life maps, but still have version 29 instead of 30 specified in the header), provide the `-v29asv30` argument.

### Quake maps to PC Half-Life

Run BS2PC with the `-quaketov30` argument specifying the `.bsp` files to upgrade as arguments.

**Important note:** By default, **BS2PC will overwrite the input** Quake map files when converting them to the PC version of Half-Life, because both Quake and Half-Life PC maps have the `.bsp` extension. To prevent them, specify a different file name or directory path using `-o "output_path"`.

## Building

A C++ compiler capable of building standard C++17 supporting `__builtin_ffs` or `BitScanForward` is required.

The target machine must be little-endian.

1. Create the `zlib` directory in the repository directory, download the [zlib source code](https://zlib.net/) (tested with version 1.2.12), and extract it into the `zlib` directory so that it contains files such as `deflate.c`.
2. Download or build [Premake 5](https://premake.github.io/) (tested with version 5.0.0-beta1).
3. [Run Premake](https://premake.github.io/docs/Using-Premake) to generate the project files for your C++ build system or IDE.
4. Use the generated files in the `build` directory (the `bs2pc` solution) to build zlib and BS2PC. The resulting executable will be placed in the configuration directory (`Debug` or `Release`) inside `build/bin`.

## `.bs2` format information

`.bs2` files contain DEFLATE-compressed map data preceded by the size of the uncompressed map.

The header of an uncompressed map starts with the 32-bit number 40 (decimal), and stores three arrays (in a structure of arrays way) containing the information about the lumps — an array of offsets of each lump, then an array of lump lengths in bytes, and then the numbers of the objects stored in each lump (except for the visibility, lighting and entities lumps).

Unlike maps in Quake and Half-Life on the PC (and Half-Life on the Dreamcast 🦔), which store compact structures (prefixed with `d`, for "disk", in the Quake source code) and require some processing on load (such as computing lightmap extents of surfaces), the PS2 version of Half-Life stores the structures the engine works directly with at runtime (like the `m`-prefixed — "memory" — structures in Quake, though the structures themselves are different in Half-Life on the PS2), with space reserved for runtime fields, and the needed preprocessing already performed.

PS2 maps share most of the lump types with PC maps, with a few differences. The texinfo lump is merged into the surfaces, and two new lumps are added — drawing hull clipnodes (generated by `Mod_MakeHull0` in Quake), and subdivided polygons.

Some surfaces have the `NODRAW` texture, those are fully stored within the map, but not drawn by the PS2 engine, however the PC engine .

### Textures

All textures used on a map are stored entirely in the map file.

Just like on the PC, textures are stored with a 256-color palette. The colors in the palette are slightly reordered (this doesn't have effect on the color indexes stored for the texture pixels, however — textures that already had power-of-two sizes in Half-Life and were converted to the PS2 have exactly the same color indexes in the pixels of their base mip level): in every 32 colors, the ranges 8…15 and 16…23 are swapped.

Unlike the PC version of Half-Life, which always stores 3-byte 8-bits-per-channel colors in the palettes of the textures, the PS2 version has 4-byte colors, with the fourth always being 0x80, except for the cutout color (255) of transparent (`{`-prefixed) textures that has bytes 0, 0, 0, 0 in PS2 textures (unlike on the PC, where it's also blue rather than black).

The bit depth of the colors in the palette depends on the texture type:

- Transparent surfaces ('{'-prefixed) and `!`-prefixed liquids (but not liquids without the `!` prefix, such as `water`-prefixed ones) have 8 bits per channel. Those are not always exactly the same colors as on the PC though — see `gbx_24_bit_color_from_id` in [bs2pc/bs2pclib.hpp](bs2pc/bs2pclib.hpp).
- Other surfaces use textures with 7 bits per channel, with values ranging from 0 to 0x7F — see `gbx_21_bit_color_from_id`. Some textures have conversions slightly different than the result of `gbx_21_bit_color_from_id`, however.

Texture data is scaled to power of two sizes (the width and the height may be different) with filtering (possibly bicubic), but the palette is not changed by the filtering (the closest color is chosen among the existing ones).

While PC textures always have 4 mip levels including the base, mips in the PS2 version are created relatively to the power of two size, and the smallest mip on the PS2 has the size of 8 along the shortest axis.

The PS2 engine doesn't support randomly tiling textures. Usually, the `-` prefix is removed from those textures, and a single tile is drawn as a normal surface. However, some maps (`c2a5c` and `stalkyard2` specifically) contain full sets of randomly tiled textures with links between those textures stored in their structures. This is likely a leftover of some legacy functionality. Those textures are stored in an interleaved way (one row from the lower half, then one row from the upper), with the colors in the palette inverted before the conversion to 21-bit, and also with incorrect data in the mips (one half nearly duplicated into the other in the first mip, then something mixed even worse).

On some maps, certain animated textures also have the `+` prefix removed to select a specific frame.

### Subdivided polygons

Liquids and transparent surfaces are subdivided in texture coordinate space, into polygons with a world-space size of up to `32 / length(texinfo T vector)` units along the direction of the texinfo S vector and up to `32 / length(texinfo S vector)` units along the texinfo T vector. The subdivision differs heavily from `GL_SubdivideSurface` used for liquids in GLQuake, which subdivides the surface along a grid aligned with the X, Y and Z axes of the world.

The resulting polygons contain vertex positions, texture coordinates, and coordinates of a single lightmap pixel. It's possible that this subdivision, at least for transparent surfaces, is done to apply vertex lighting from some of the lightmap pixels rather than the lightmap surface itself, possibly due to the limitations of the very basic shading functionality on the PS2 which likely made it complicated to draw alpha cutout surfaces with multiple textures (if transparent surfaces are drawn without the subdivided polygons, it can be seen how they darken the image behind them, likely as a result of applying the lightmap as a second layer with blending). For liquids, the subdivision is done to implement the effect of turbulence.

Polygons are chained into triangle strips. Some surfaces have multiple strips. Degenerate triangles are inserted only to change the order of the vertexes in one edge for the next two triangles (sequences like 0, **1**, 2, **1**, 3, 4 to create triangles 2, 1, 3 and 1, 3, 4 instead of 1, 2, 3 and 2, 3, 4). The winding order doesn't seem to matter.

## License

Due to a large amount of code being used from the Quake engine and its tools, the project is available under the [GNU General Public License (GPL) version 2](gpl-2.0.md).

However, you may consider the information about the Half-Life PS2 `.bs2` maps obtained via analysis of the existing map files, as well as code not directly derived from Quake or Half-Life code, in public domain.

Certain parts of the project were taken from the source code of the Half-Life tools available under the [Half-Life 1 SDK License](https://github.com/ValveSoftware/halflife/blob/master/LICENSE). These are also heavily based on the Quake engine source code, being modifications of it made by Valve Corporation under a separate, proprietary Quake engine license, however, the Quake code they're derived from is available publicly under the GNU General Public License version 2.

Bicubic resampling is based on the code in [https://github.com/Atrix256](Alan Wolfe)'s blog post "[Resizing Images With Bicubic Interpolation](https://blog.demofox.org/2015/08/15/resizing-images-with-bicubic-interpolation/)", provided under the [MIT License](https://blog.demofox.org/license/).

The project uses [zlib](https://zlib.net/), available under the [zlib License](https://zlib.net/zlib_license.html).
