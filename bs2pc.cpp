#include "bs2pclib/bs2pclib.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static bool bs2pc_load_file(
		std::filesystem::path const & path,
		std::vector<char> & data,
		bool const print_if_failed_to_open,
		size_t const exact_size = SIZE_MAX) {
	assert(exact_size == SIZE_MAX ||
			(exact_size == std::streamoff(exact_size) && exact_size == std::streamsize(exact_size)));
	std::ifstream stream(path, std::ios_base::binary | std::ios_base::in | std::ios_base::ate);
	if (!stream.is_open()) {
		if (print_if_failed_to_open) {
			std::cerr << "Failed to open " << path.string() << " for reading." << std::endl;
		}
		return false;
	}
	std::streamoff const size(stream.tellg());
	if (size < 0) {
		std::cerr << "Failed to get the size of " << path.string() << "." << std::endl;
		return false;
	}
	if (exact_size != SIZE_MAX && size < exact_size) {
		std::cerr << path.string() << " is smaller than required (" << exact_size << ")." << std::endl;
		return false;
	}
	if (size > UINT32_MAX) {
		std::cerr << path.string() << " is too large, Half-Life uses 32-bit offsets and sizes." << std::endl;
		return false;
	}
	if (size > SIZE_MAX || size > std::numeric_limits<std::streamsize>::max()) {
		std::cerr << path.string() << " is too large." << std::endl;
		return false;
	}
	stream.seekg(0, std::ios_base::beg);
	if (!stream.good()) {
		std::cerr << "Failed to seek to the beginning of " << path.string() << "." << std::endl;
		return false;
	}
	size_t const read_size = (exact_size != SIZE_MAX ? exact_size : size_t(size));
	data.resize(read_size);
	stream.read(data.data(), std::streamsize(read_size));
	if (!stream.good()) {
		std::cerr << "Failed to read " << path.string() << "." << std::endl;
		return false;
	}
	return true;
}

int main(int const argument_count, char const * const * const arguments) {
	// Parse the arguments.

	enum class convert_mode {
		convert,
		compress,
		decompress,
		create_gbx_texture_wadg,
		extract_gbx_textures,
		write_gbx_polygon_objs,
	};
	convert_mode argument_convert_mode = convert_mode::convert;

	bool deserialize_quake_maps_as_valve = false;

	bool convert_quake_maps_to_valve_id = false;
	bool subdivide_quake_turbulent = true;

	bool compress = true;

	bool keep_nodraw = false;

	bool include_all_textures = false;
	bool do_reconstruct_random_texture_sequences = true;

	bool keep_random_prefix = false;

	std::filesystem::path quake_palette_path;

	std::vector<std::filesystem::path> wad_search_paths;

	char const * const wadg_default_path = "hlps2.bs2pcwad";
	std::filesystem::path wadg_path(wadg_default_path);
	bool overwrite_wadg = false;

	uint32_t extract_gbx_texture_mip = 0;

	std::filesystem::path argument_output_path;

	std::vector<std::filesystem::path> input_paths;

	enum class argument_type {
		option_or_input,
		convert_mode,
		output,
		extract_gbx_texture_mip,
		quake_palette_path,
		wad_search_path,
		wadg_path,
	};
	argument_type next_argument_type = argument_type::option_or_input;

	for (int argument_index = 1; argument_index < argument_count; ++argument_index) {
		char const * const argument = arguments[argument_index];
		if (next_argument_type == argument_type::option_or_input) {
			if (argument[0] == '-') {
				char const * const option = argument + 1;
				if (!std::strcmp(option, "mode")) {
					next_argument_type = argument_type::convert_mode;
				} else if (!std::strcmp(option, "o") || !std::strcmp(option, "output")) {
					next_argument_type = argument_type::output;
				} else if (!std::strcmp(option, "extractps2texturemip")) {
					next_argument_type = argument_type::extract_gbx_texture_mip;
				} else if (!std::strcmp(option, "ps2texturefile")) {
					next_argument_type = argument_type::wadg_path;
				} else if (!std::strcmp(option, "quakepalette")) {
					next_argument_type = argument_type::quake_palette_path;
				} else if (!std::strcmp(option, "waddir")) {
					next_argument_type = argument_type::wad_search_path;
				} else if (!std::strcmp(option, "includealltextures")) {
					include_all_textures = true;
				} else if (!std::strcmp(option, "keepnodraw")) {
					keep_nodraw = true;
				} else if (!std::strcmp(option, "keeprandomprefix")) {
					keep_random_prefix = true;
				} else if (!std::strcmp(option, "nocompress")) {
					compress = false;
				} else if (!std::strcmp(option, "noreconstructrandom")) {
					do_reconstruct_random_texture_sequences = false;
				} else if (!std::strcmp(option, "nosubdividequaketurbulent")) {
					subdivide_quake_turbulent = false;
				} else if (!std::strcmp(option, "overwriteps2texturefile")) {
					overwrite_wadg = true;
				} else if (!std::strcmp(option, "quaketov30")) {
					convert_quake_maps_to_valve_id = true;
				} else if (!std::strcmp(option, "v29asv30")) {
					deserialize_quake_maps_as_valve = true;
				} else {
					std::cerr << "Unknown option " << argument << '.' << std::endl;
					return EXIT_FAILURE;
				}
			} else {
				input_paths.emplace_back(argument);
			}
		} else {
			switch (next_argument_type) {
				case argument_type::output:
					argument_output_path = argument;
					break;
				case argument_type::convert_mode:
					if (!std::strcmp(argument, "convert")) {
						argument_convert_mode = convert_mode::convert;
					} else if (!std::strcmp(argument, "compress")) {
						argument_convert_mode = convert_mode::compress;
					} else if (!std::strcmp(argument, "decompress")) {
						argument_convert_mode = convert_mode::decompress;
					} else if (!std::strcmp(argument, "createps2texturefile")) {
						argument_convert_mode = convert_mode::create_gbx_texture_wadg;
					} else if (!std::strcmp(argument, "extractps2textures")) {
						argument_convert_mode = convert_mode::extract_gbx_textures;
					} else if (!std::strcmp(argument, "writepolygonobj")) {
						argument_convert_mode = convert_mode::write_gbx_polygon_objs;
					} else {
						std::cerr << "Unknown conversion mode " << argument << '.' << std::endl;
						return EXIT_FAILURE;
					}
					break;
				case argument_type::extract_gbx_texture_mip:
					extract_gbx_texture_mip = uint32_t(std::strtoul(argument, nullptr, 0));
					break;
				case argument_type::quake_palette_path:
					quake_palette_path = argument;
					break;
				case argument_type::wad_search_path:
					wad_search_paths.emplace_back(argument);
					break;
				case argument_type::wadg_path:
					wadg_path = argument;
					break;
				default:
					break;
			}
			next_argument_type = argument_type::option_or_input;
		}
	}

	if (input_paths.empty()) {
		std::cerr <<
				"BS2PC - Half-Life PlayStation 2 map converter.\n"
				"\n"
				"No input files specified.\n"
				"\n"
				"Usage: " << std::filesystem::path(arguments[0]).stem().string() <<
				" -option -option value input_file input_file\n"
				"\n"
				"Input files can be PC Half-Life and Quake .bsp maps and compressed (.bs2) or uncompressed PS2 "
				"Half-Life maps.\n"
				"\n"
				"For PC to PS2 conversion, WAD files (see `-waddir`) used on the map are necessary if the map doesn't "
				"have all textures included, and the original PS2 conversions of Half-Life textures (see `-mode "
				"createps2texturefile` and `-ps2texturefile`) are heavily recommended for visual consistency.\n"
				"For PS2 to PC conversion, Half-Life WAD files (see `-waddir`) are heavily recommended to restore the "
				"original detail and color depth of the textures that were lossily converted for the PS2, and also to "
				"reconstruct randomized tiling of textures on the software renderer.\n"
				"\n"
				"Options:\n"
				" -mode conversion_mode\n"
				"  Action to perform for the input files.\n"
				"  Possible values:\n"
				"  * convert\n"
				"    Default - convert maps between game versions.\n"
				"  * compress\n"
				"    Only compress uncompressed PS2 maps instead of converting.\n"
				"  * decompress\n"
				"    Only decompress PS2 maps (creating files with .bs2uz extension by default) instead of "
				"converting.\n"
				"  * createps2texturefile\n"
				"    Dump original conversions of Half-Life textures from the PS2 version into a file to be used later "
				"for PC to PS2 conversion specified via -ps2texturefile (for creation, the destination file can also "
				"be specified via -o), by default hlps2.bs2pcwad in the working directory.\n"
				"    This is especially useful for converting maps that have level changes to the original Gearbox "
				"maps so there's no noticeable texture switching between different maps.\n"
				"    All PS2 maps in Half-Life and Decay should be provided as the input files in a single or multiple "
				"invocations (if the file already exists, new textures will be added to it alongside the existing "
				"ones).\n"
				"  * extractps2textures\n"
				"    Extract a single mip level (specified via -extractps2texturemip, the base level by default) of "
				"all textures as .tga images from the PS2 maps specified as the input files.\n"
				"    The file name will contain the original size of the texture used for texture coordinate "
				"calculation, without resampling to powers of two.\n"
				"  * writepolygonobj\n"
				"    Create .obj files containing subdivided polygons of liquid and transparent surfaces from the PS2 "
				"maps specified as the input files.\n"
				"    The coordinate system matches the engine.\n"
				"    Normals and texture coordinates will be written, but the materials themselves will not.\n"
				" -o output_path (or -output)\n"
				"  Path where to store the generated file or files.\n"
				"  For conversion, compression/decompression and subdivided polygon .obj extraction, by default, this "
				"will be treated as a file path if there's only one input file (but as a directory path if the "
				"specified path points to an existing directory), and as a directory path for multiple input files.\n"
				"  If not specified, the resulting files will be in the original directory, but with the extension "
				"changed to the target one.\n"
				"  For creation of a file with the original PS2 texture data, this is the destination file path.\n"
				"  For extraction of texture images from PS2 maps, this is the destination directory path.\n"
				" -extractps2texturemip mip_level\n"
				"  For extraction of texture images from PS2 maps, the mip level to extract.\n"
				"  0 is the base level (full resolution).\n"
				" -includealltextures\n"
				"  When converting PS2 maps to the PC, include the pixels of all textures directly in the resulting "
				"map file regardless of whether they were found in a WAD file.\n"
				" -keepnodraw\n"
				"  When converting PS2 maps to the PC, don't remove NODRAW-textured surfaces (with a crossed circle "
				"symbol) that are not visible in the PS2 version, but displayed by the PC engine.\n"
				" -keeprandomprefix\n"
				"  When converting PC maps to the PS2, don't remove the hyphen prefix from the names of random-tiled "
				"textures, and also interleave and invert them similarly to how they're stored in the original Gearbox "
				"map files.\n"
				"  With this option, they will be displayed incorrectly by the PS2 engine since it tries to draw "
				"surfaces with them as usual, the interleaved and inverted storage is likely legacy functionality not "
				"supported by released version of the engine, present only on two maps, with the rest of random-tiled "
				"textures having the hyphen prefix removed and stored as normal.\n"
				"  Note that the PS2 version doesn't support randomized tiling, only the PC software renderer does.\n"
				" -nocompress\n"
				"  When converting PC maps to the PS2, don't compress the resulting maps.\n"
				"  This feature is purely for debugging BS2PC itself, as the engine is only able to load compressed "
				"map files.\n"
				"  Uncompressed maps will be written with the custom .bs2uz extension instead of .bs2 by default.\n"
				" -nosubdividequaketurbulent\n"
				"  When converting Quake maps, don't subdivide turbulent surfaces into 240-unit faces.\n"
				"  Enabling this may result in the \"bad surface extents\" error when launching the converted maps on "
				"the Half-Life PC software renderer.\n"
				" -noreconstructrandom\n"
				"  When converting PS2 maps to the PC, don't try to reconstruct randomized tiling of textures on the "
				"software renderer by adding the hyphen prefix and searching for all textures in the sets in the WADs, "
				"instead always displaying the specific tile selected by Gearbox.\n"
				" -ps2texturefile bs2pcwad_file_path\n"
				"  When converting PC maps to the PS2, use the specified path to the file generated using `-mode "
				"createps2texturefile` instead of hlps2.bs2pcwad from the working directory to load the original PS2 "
				"conversions of the textures from instead of resampling during conversion.\n"
				"  See `-mode createps2texturefile` documentation for more info about this file.\n"
				" -overwriteps2texturefile\n"
				"  When creating the original PS2 texture conversions file, if the file already exists, ignore it "
				"instead of adding new textures to it.\n"
				" -quakepalette palette_lmp_path\n"
				"  When converting in either direction, use the specified palette file (in palette.lmp format, 256 "
				"3-byte R8G8B8 values) instead of the default Quake palette.\n"
				"  For PC to PS2 conversion, the Quake palette will be written to the generated map for textures from "
				"Quake maps or WAD2 files.\n"
				"  For PS2 to PC conversion, primarily in BS2PC round trip cases (PC to PS2 to PC), the Quake palette "
				"will be used to get the full-precision 24-bit colors for textures that were converted to 21-bit if "
				"they match the Quake palette colors.\n"
				" -quaketov30\n"
				"  Instead of converting Quake version 29 maps to PS2, just upgrade them to the PC version of "
				"Half-Life (to BSP version 30).\n"
				"  Since both Quake and the PC version of Half-Life use the .bsp extension, by default this will cause "
				"the input files to be overwritten, it's important to use the -o option to specify a different output "
				"path if needed.\n"
				"  Ignored if -v29asv30 is specified.\n"
				" -v29asv30\n"
				"  Treat PC version 29 maps as Half-Life maps with colored lighting and local texture palettes, not as "
				"Quake maps.\n"
				"  Half-Life maps with version number 29 are present in the alpha version 0.52 of Half-Life.\n"
				" -waddir wad_search_path\n"
				"  When converting in either direction, paths to search for texture WAD files used on the maps in.\n"
				"  Multiple paths (for example, the game and the mod directory) can be specified with multiple -waddir "
				"options.\n"
				"  For PC to PS2 conversion, this is required for conversion of maps that don't have all their "
				"textures included directly in the map file to locate the texture pixels, as they all must be written "
				"in the PS2 map file.\n"
				"  For PS2 to PC conversion, BS2PC searches for the original pixels and 24-bit palette of the textures "
				"in the WAD files to revert the quality loss caused by resampling the textures to powers of two and "
				"reducing the colors to 21-bit for the PS2." << std::endl;
		return EXIT_SUCCESS;
	}

	// Handle the output path as a file or a directory.
	bool argument_output_path_is_directory = false;
	if (argument_convert_mode == convert_mode::create_gbx_texture_wadg && argument_output_path.empty()) {
		// Allow using -ps2texturefile for both input and output.
		argument_output_path = wadg_path;
	}
	if (!argument_output_path.empty()) {
		if (argument_convert_mode == convert_mode::create_gbx_texture_wadg) {
			if (std::filesystem::is_directory(argument_output_path)) {
				std::cerr << "If the output path is specified, for creating the BS2PC texture WAD, it must not point "
						"to a directory." << std::endl;
				return EXIT_FAILURE;
			}
		} else if (argument_convert_mode == convert_mode::extract_gbx_textures) {
			if (!std::filesystem::create_directories(argument_output_path)) {
				if (!std::filesystem::is_directory(argument_output_path)) {
					std::cerr << "Failed to create the output directory " << argument_output_path.string() << '.' <<
							std::endl;
					return EXIT_FAILURE;
				}
			}
			argument_output_path_is_directory = true;
		} else {
			argument_output_path_is_directory = std::filesystem::is_directory(argument_output_path);
			if (!argument_output_path_is_directory && input_paths.size() > 1) {
				if (!std::filesystem::create_directories(argument_output_path)) {
					std::cerr << "Failed to create the output directory " << argument_output_path.string() << "\n."
							"When multiple input files are specified, the output path, if specified, must point to a "
							"directory." << std::endl;
					return EXIT_FAILURE;
				}
				argument_output_path_is_directory = true;
			}
		}
	}

	bool any_errors = false;

	bs2pc::palette_set quake_palette(bs2pc::quake_default_palette);
	if (!quake_palette_path.empty()) {
		std::vector<char> quake_override_palette;
		if (bs2pc_load_file(quake_palette_path, quake_override_palette, true, 3 * 256)) {
			quake_palette = bs2pc::palette_set(reinterpret_cast<uint8_t const *>(quake_override_palette.data()));
		} else {
			any_errors = true;
		}
	}

	// Convert.
	// Note that the output file may be the same as the input file, so all input files must be loaded fully before
	// converting.

	// The key is bs2pc::string_to_lower(WAD name).
	std::unordered_map<std::string, std::optional<bs2pc::wad_textures_deserialized>> loaded_wads;
	std::vector<std::string> map_wad_names;
	std::vector<bs2pc::wad_textures_deserialized *> map_wads;
	std::vector<std::pair<size_t, bool>> map_wad_name_numbers_and_used;
	auto const load_map_wads = [&]() {
		map_wad_name_numbers_and_used.clear();
		map_wads.clear();
		for (size_t map_wad_name_number = 0; map_wad_name_number < map_wad_names.size(); ++map_wad_name_number) {
			std::string const & wad_name = map_wad_names[map_wad_name_number];
			std::string const wad_name_lower = bs2pc::string_to_lower(wad_name);
			auto const loaded_wad_iterator = loaded_wads.find(wad_name_lower);
			if (loaded_wad_iterator != loaded_wads.cend()) {
				if (loaded_wad_iterator->second.has_value()) {
					map_wads.emplace_back(&loaded_wad_iterator->second.value());
					map_wad_name_numbers_and_used.emplace_back(map_wad_name_number, false);
				}
				continue;
			}
			bool wad_loaded = false;
			for (std::filesystem::path const & wad_search_path : wad_search_paths) {
				// Use the original case from worldspawn if the file system is case-sensitive.
				std::filesystem::path wad_path = wad_search_path / wad_name;
				std::vector<char> wad_file_data;
				if (!bs2pc_load_file(wad_path, wad_file_data, false)) {
					continue;
				}
				bs2pc::wad_textures_deserialized wad;
				char const * const wad_deserialize_error =
						bs2pc::get_wad_textures(wad_file_data.data(), wad_file_data.size(), wad, quake_palette.id);
				if (wad_deserialize_error) {
					std::cerr << "Failed to deserialize " << wad_path.string() << ": " << wad_deserialize_error <<
							'.' << std::endl;
					continue;
				}
				map_wads.emplace_back(&loaded_wads.emplace(wad_name_lower, std::move(wad)).first->second.value());
				map_wad_name_numbers_and_used.emplace_back(map_wad_name_number, false);
				wad_loaded = true;
				break;
			}
			if (wad_loaded) {
				continue;
			}
			std::cerr <<
					"WAD file " << wad_name << " not loaded from any search directory specified via -waddir.\n"
					"This is fine in some cases (gbx1.wad and hlps2.wad in PS2 Half-Life, sample.wad in PC Half-Life, "
					"Quake), but other WADs not being found may indicate that the -waddir arguments are not set up "
					"correctly." << std::endl;
			// Don't search for the WAD again.
			loaded_wads.emplace(wad_name_lower, std::nullopt);
		}
	};

	// For WADG creation and texture extraction, the textures gathered from the maps.
	// The key is bs2pc::string_to_lower(texture.name).
	std::map<std::string, bs2pc::gbx_texture_deserialized> gathered_gbx_textures;
	if (argument_convert_mode == convert_mode::create_gbx_texture_wadg && !overwrite_wadg) {
		// Load the existing WADG to append new textures to it so the command can be executed multiple times (it may
		// become too long on some operating systems especially with paths that include directories).
		std::vector<char> wadg_file;
		if (bs2pc_load_file(wadg_path, wadg_file, false)) {
			bs2pc::add_wadg_textures(wadg_file.data(), wadg_file.size(), gathered_gbx_textures, quake_palette);
		}
	}

	std::vector<char> input_file_data;
	std::vector<char> input_decompressed_data;
	std::vector<char> output_data;
	std::vector<char> output_uncompressed_data;
	bs2pc::id_map map_id;
	bs2pc::gbx_map map_gbx;
	std::vector<std::string> map_wad_names_used;
	bool wadg_load_attempted = false;
	// For conversion from id to Gearbox, the textures loaded from the WADG.
	// The key is bs2pc::string_to_lower(texture.name).
	std::unordered_map<std::string, bs2pc::gbx_texture_deserialized> loaded_wadg_textures;
	bool last_file_errored = false;
	for (std::filesystem::path const & input_path : input_paths) {
		if (last_file_errored) {
			any_errors = true;
		}
		// Make sure that any `continue` means an error.
		last_file_errored = true;

		if (!bs2pc_load_file(input_path, input_file_data, true)) {
			continue;
		}

		if (argument_convert_mode == convert_mode::create_gbx_texture_wadg ||
				argument_convert_mode == convert_mode::extract_gbx_textures ||
				argument_convert_mode == convert_mode::write_gbx_polygon_objs) {
			// Extract Gearbox textures or write polygon .obj files.
			if (input_file_data.size() < sizeof(uint32_t) + sizeof(uint16_t)) {
				std::cerr << input_path.string() << " is too small to identify its type." << std::endl;
				continue;
			}
			uint32_t map_version;
			std::memcpy(&map_version, input_file_data.data(), sizeof(uint32_t));
			std::vector<char> * input_data = nullptr;
			if (map_version == bs2pc::gbx_map_version) {
				std::cerr << "Processing an uncompressed Half-Life PS2 map " << input_path.string() << "..." <<
						std::endl;
				input_data = &input_file_data;
			} else if (bs2pc::is_gbx_map_compressed(input_file_data.data(), input_file_data.size())) {
				if (!bs2pc::decompress_gbx_map(
						input_file_data.data(), input_file_data.size(), input_decompressed_data)) {
					std::cerr << "Failed to decompress " << input_path.string() << "." << std::endl;
					continue;
				}
				std::memcpy(&map_version, input_decompressed_data.data(), sizeof(uint32_t));
				if (map_version == bs2pc::gbx_map_version) {
					std::cerr << "Processing a compressed Half-Life PS2 map " << input_path.string() << "..." <<
							std::endl;
					input_data = &input_decompressed_data;
				}
			}
			if (!input_data) {
				std::cerr << input_path.string() << " is not a map of a supported type." << std::endl;
				continue;
			}

			char const * const deserialize_error =
					((argument_convert_mode == convert_mode::create_gbx_texture_wadg ||
							argument_convert_mode == convert_mode::extract_gbx_textures)
							? map_gbx.deserialize_only_textures(input_data->data(), input_data->size(), quake_palette)
							: map_gbx.deserialize(input_data->data(), input_data->size(), quake_palette));
			if (deserialize_error) {
				std::cerr << "Failed to deserialize " << input_path.string() << ": " << deserialize_error << '.' <<
						std::endl;
				continue;
			}

			if (argument_convert_mode == convert_mode::create_gbx_texture_wadg ||
					argument_convert_mode == convert_mode::extract_gbx_textures) {
				for (bs2pc::gbx_texture_deserialized const & deserialized_texture : map_gbx.textures) {
					auto const texture_gbx_emplaced = gathered_gbx_textures.emplace(
							bs2pc::string_to_lower(deserialized_texture.name),
							deserialized_texture);
					// Even if adding new textures to the WADG, there's no need to overwrite existing textures there as
					// the data stored is the original Gearbox's conversions, which don't depend on the algorithms used
					// in BS2PC, only on the details of storage within BS2PC - and if they're changed in a future
					// version of BS2PC, the header of the WADG just needs to be changed.
					if (texture_gbx_emplaced.second) {
						// Don't need texture numbers from some map in the .bs2pcwad.
						texture_gbx_emplaced.first->second.reset_anim();
					}
				}
			} else if (argument_convert_mode == convert_mode::write_gbx_polygon_objs) {
				std::filesystem::path output_path(argument_output_path.empty() ? input_path : argument_output_path);
				if (argument_output_path_is_directory || argument_output_path.empty()) {
					if (argument_output_path_is_directory) {
						output_path /= input_path.filename();
					}
					output_path.replace_extension(".obj");
				}
				{
					std::ofstream output_stream(output_path, std::ios_base::out);
					if (!output_stream.is_open()) {
						std::cerr << "Failed to open " << output_path.string() << " for writing." << std::endl;
						continue;
					}
					bs2pc::write_polygons_to_obj(output_stream, map_gbx);
					if (!output_stream.good()) {
						std::cerr << "Failed to write " << output_path.string() << "." << std::endl;
						continue;
					}
				}
			}
		} else {
			// Make sure all potential padding is filled with zeros, not by the previous output contents.
			output_data.clear();

			char const * output_extension = "";

			switch (argument_convert_mode) {
				case convert_mode::convert: {
					// Convert the map.
					if (input_file_data.size() < sizeof(uint32_t) + sizeof(uint16_t)) {
						std::cerr << input_path.string() << " is too small to identify its type." << std::endl;
						continue;
					}
					uint32_t map_original_version;
					std::memcpy(&map_original_version, input_file_data.data(), sizeof(uint32_t));
					if (map_original_version == bs2pc::id_map_version_quake ||
							map_original_version == bs2pc::id_map_version_valve) {
						// An id map.
						if (map_original_version == bs2pc::id_map_version_quake) {
							if (deserialize_quake_maps_as_valve) {
								std::cerr << "Converting Half-Life Alpha v0.52 or Quake map " << input_path.string() <<
										" as a Half-Life map..." << std::endl;
							} else {
								std::cerr << "Converting Quake map " << input_path.string() << "..." << std::endl;
							}
						} else if (map_original_version == bs2pc::id_map_version_valve) {
							std::cerr << "Converting Half-Life PC map " << input_path.string() << "..." << std::endl;
						}

						char const * const deserialize_error = map_id.deserialize(
								input_file_data.data(),
								input_file_data.size(),
								deserialize_quake_maps_as_valve,
								quake_palette.id);
						if (deserialize_error) {
							std::cerr << "Failed to deserialize " << input_path.string() << ": " << deserialize_error <<
									'.' << std::endl;
							continue;
						}

						map_id.upgrade_from_quake_without_model_paths(subdivide_quake_turbulent);

						if (convert_quake_maps_to_valve_id && map_original_version == bs2pc::id_map_version_quake) {
							// Upgrade from v29 to v30, don't convert to a Gearbox map.
							map_id.version = bs2pc::id_map_version_valve;

							bs2pc::convert_model_paths(
									map_id.entities.data(),
									map_id.entities.size(),
									map_original_version,
									bs2pc::id_map_version_valve);

							map_id.serialize(output_data, quake_palette.id);

							output_extension = ".bsp";
						} else {
							map_gbx.from_id_no_texture_pixels_and_polygons(map_id);

							bs2pc::convert_model_paths(
									map_gbx.entities.data(),
									map_gbx.entities.size(),
									map_original_version,
									bs2pc::gbx_map_version);

							// If any map needs to be converted from id to Gearbox, load the file containing the
							// original textures extracted from the maps for more visual consistency with them so the
							// filtering and the sizes are the same as in the original conversions.
							if (!wadg_load_attempted) {
								wadg_load_attempted = true;
								std::vector<char> wadg_file;
								if (bs2pc_load_file(wadg_path, wadg_file, true)) {
									char const * const wadg_deserialize_error = bs2pc::add_wadg_textures(
											wadg_file.data(), wadg_file.size(), loaded_wadg_textures, quake_palette);
									if (wadg_deserialize_error) {
										std::cerr << "Failed to deserialize " << wadg_path.string() << ": " <<
												wadg_deserialize_error << '.' << std::endl;
										continue;
									}
								}
							}

							// If there are textures without pixels stored in the map, load the WAD files for it.
							// Not doing this unconditionally because some original Valve's maps have all textures
							// embedded into the map, and also reference the non-existent sample.wad.
							map_wad_names.clear();
							for (bs2pc::id_texture_deserialized const & texture : map_id.textures) {
								if (texture.empty() || texture.pixels) {
									continue;
								}
								if (!map_id.entities.empty()) {
									bs2pc::append_worldspawn_wad_names(map_id.entities.front(), map_wad_names);
								}
								break;
							}
							// If no textures to load from WADs, just clear the vectors.
							load_map_wads();

							// Convert the textures, or load an existing conversion.
							// Also remove the random tiling prefix from textures similar to how that's done in the
							// original Gearbox maps, as the PS2 engine displays them incorrectly without deinterleaving
							// and inverting.
							bool random_removed = false;
							assert(map_id.textures.size() == map_gbx.textures.size());
							for (size_t texture_number = 0; texture_number < map_id.textures.size(); ++texture_number) {
								bs2pc::id_texture_deserialized const & map_texture_id = map_id.textures[texture_number];
								if (map_texture_id.empty()) {
									continue;
								}
								std::string texture_id_name_lower = bs2pc::string_to_lower(map_texture_id.name);
								// Use the pixels from either the map (if provided there) or a WAD.
								bs2pc::id_texture_deserialized const * pixels_texture_id =
										map_texture_id.pixels ? &map_texture_id : nullptr;
								bs2pc::wad_texture_deserialized * pixels_wad_texture = nullptr;
								if (!pixels_texture_id) {
									for (bs2pc::wad_textures_deserialized * const wad : map_wads) {
										auto const wad_texture_number_iterator =
												wad->texture_number_map.find(texture_id_name_lower);
										if (wad_texture_number_iterator == wad->texture_number_map.cend()) {
											continue;
										}
										bs2pc::wad_texture_deserialized & wad_texture =
												wad->textures[wad_texture_number_iterator->second];
										if (wad_texture.texture_id.width == map_texture_id.width &&
												wad_texture.texture_id.height == map_texture_id.height) {
											pixels_texture_id = &wad_texture.texture_id;
											pixels_wad_texture = &wad_texture;
											break;
										}
									}
									if (!pixels_texture_id) {
										// Don't set the pixels, let serialization write a checkerboard for the texture.
										continue;
									}
								}
								bs2pc::gbx_texture_deserialized & texture_gbx = map_gbx.textures[texture_number];
								if (!keep_random_prefix && texture_gbx.name.c_str()[0] == '-') {
									random_removed = true;
									texture_gbx.name = texture_gbx.name.substr(1);
								}
								auto const wadg_texture_iterator = bs2pc::find_identical_wadg_texture(
										loaded_wadg_textures, texture_gbx.name, *pixels_texture_id, quake_palette);
								if (wadg_texture_iterator != loaded_wadg_textures.cend()) {
									// The pixels might have been found under a different name of the texture.
									// Store it, and restore after copying all the fields.
									std::string texture_gbx_map_name = std::move(texture_gbx.name);
									texture_gbx = wadg_texture_iterator->second;
									texture_gbx.name = std::move(texture_gbx_map_name);
								} else if (pixels_wad_texture) {
									// Reuse conversions of WAD textures between maps.
									texture_gbx.pixels_and_palette_from_wad(*pixels_wad_texture, quake_palette.id);
								} else {
									texture_gbx.pixels_and_palette_from_id(*pixels_texture_id, quake_palette.id);
								}
							}
							if (random_removed) {
								// Update animation links since random-tiled textures contain them.
								map_gbx.link_texture_anim();
							}

							map_gbx.make_polygons(map_gbx.polygons.data(), map_gbx.polygons.size());

							std::vector<char> & output_serialized_data =
									compress ? output_uncompressed_data : output_data;
							map_gbx.serialize(output_serialized_data, quake_palette);

							if (compress) {
								if (!bs2pc::compress_gbx_map(
										output_serialized_data.data(),
										output_serialized_data.size(),
										output_data)) {
									std::cerr << "Failed to compress " << input_path.string() << "." << std::endl;
									continue;
								}
							}
							// .bs2uz is a BS2PC addition, not an extension used by Gearbox.
							output_extension = compress ? "bs2" : "bs2uz";
						}
					} else {
						// Possibly a Gearbox map.
						std::vector<char> * input_data = nullptr;
						if (map_original_version == bs2pc::gbx_map_version) {
							std::cerr << "Converting uncompressed Half-Life PS2 map " << input_path.string() << "..." <<
									std::endl;
							input_data = &input_file_data;
						} else if (bs2pc::is_gbx_map_compressed(input_file_data.data(), input_file_data.size())) {
							if (!bs2pc::decompress_gbx_map(
									input_file_data.data(), input_file_data.size(), input_decompressed_data)) {
								std::cerr << "Failed to decompress " << input_path.string() << "." << std::endl;
								continue;
							}
							std::memcpy(&map_original_version, input_decompressed_data.data(), sizeof(uint32_t));
							if (map_original_version == bs2pc::gbx_map_version) {
								std::cerr << "Converting compressed Half-Life PS2 map " << input_path.string() <<
										"..." << std::endl;
								input_data = &input_decompressed_data;
							}
						}
						if (!input_data) {
							std::cerr << input_path.string() << " is not a map of a supported type." << std::endl;
							continue;
						}

						char const * const deserialize_error =
								map_gbx.deserialize(input_data->data(), input_data->size(), quake_palette);
						if (deserialize_error) {
							std::cerr << "Failed to deserialize " << input_path.string() << ": " << deserialize_error <<
									'.' << std::endl;
							continue;
						}

						map_id.from_gbx_no_texture_pixels(map_gbx);

						bs2pc::convert_model_paths(
								map_id.entities.data(),
								map_id.entities.size(),
								map_original_version,
								bs2pc::id_map_version_valve);

						// Process WAD paths for the map.
						map_wad_names.clear();
						if (!map_id.entities.empty()) {
							bs2pc::append_worldspawn_wad_names(map_id.entities.front(), map_wad_names);
							// Replace Gearbox's WADs with the PC Half-Life WADs.
							bs2pc::replace_hlps2_wads(map_wad_names);
						}
						// Load the WADs to use the original textures, with 24-bit rather than 21-bit colors, and not
						// resampled to a power of two, thus still having all the original details. If no WAD list in
						// worldspawn, just clear the vectors.
						load_map_wads();

						// Convert the textures if needed, or let the engine use the original texures from the WADs.
						// Before doing anything (such as removing nodraw) that may change the texture numbers.
						assert(map_gbx.textures.size() == map_id.textures.size());
						for (size_t texture_number = 0; texture_number < map_gbx.textures.size(); ++texture_number) {
							map_id.textures[texture_number].pixels_and_palette_from_wads_or_gbx(
									map_gbx.textures[texture_number], map_wads.data(), map_wads.size(),
									include_all_textures, quake_palette);
						}

						if (!keep_nodraw) {
							map_id.remove_nodraw();
						}

						if (do_reconstruct_random_texture_sequences) {
							bs2pc::reconstruct_random_texture_sequences(
									map_id, map_gbx.textures.data(), map_gbx.textures.size(),
									map_wads.data(), map_wads.size(), include_all_textures, quake_palette);
						}

						map_id.sort_textures();

						// Keep only the WADs containing textures used by the map for faster loading.
						// Even if the pixels are included, or only the palette is reused, still consider the WAD used
						// so the WAD, for instance, isn't removed from the list if that's the case for all textures
						// there, and the pixels and the palette are located again in case of a round trip Gearbox > id
						// (with included textures) > Gearbox > id conversion.
						if (!map_id.entities.empty()) {
							for (bs2pc::id_texture_deserialized const & texture : map_id.textures) {
								if (texture.empty() || texture.wad_number == SIZE_MAX) {
									continue;
								}
								map_wad_name_numbers_and_used[texture.wad_number].second = true;
							}
							map_wad_names_used.clear();
							for (std::pair<size_t, bool> const & map_wad_name_number_and_used :
									map_wad_name_numbers_and_used) {
								if (!map_wad_name_number_and_used.second) {
									continue;
								}
								map_wad_names_used.push_back(map_wad_names[map_wad_name_number_and_used.first]);
							}
							bs2pc::set_worldspawn_wad_paths(map_id.entities.front(), map_wad_names_used);
						}

						map_id.serialize(output_data, quake_palette.id);

						output_extension = "bsp";
					}
				}
				break;

				case convert_mode::compress: {
					std::cerr << "Compressing " << input_path.string() << "..." << std::endl;
					if (!bs2pc::compress_gbx_map(input_file_data.data(), input_file_data.size(), output_data)) {
						std::cerr << "Failed to compress " << input_path.string() << "." << std::endl;
						continue;
					}
					output_extension = "bs2";
				}
				break;

				case convert_mode::decompress: {
					std::cerr << "Decompressing " << input_path.string() << "..." << std::endl;
					if (!bs2pc::decompress_gbx_map(input_file_data.data(), input_file_data.size(), output_data)) {
						std::cerr << "Failed to decompress " << input_path.string() << "." << std::endl;
						continue;
					}
					// .bs2uz is a BS2PC addition, not an extension used by Gearbox.
					output_extension = "bs2uz";
				}
				break;
			}

			// Write the output file.
			if (output_data.size() > std::numeric_limits<std::streamsize>::max()) {
				std::cerr << "The output for " << input_path.string() << " is too large." << std::endl;
				continue;
			}
			{
				std::filesystem::path output_path(argument_output_path.empty() ? input_path : argument_output_path);
				if (argument_output_path_is_directory || argument_output_path.empty()) {
					if (argument_output_path_is_directory) {
						output_path /= input_path.filename();
					}
					assert(output_extension[0]);
					output_path.replace_extension(output_extension);
				}
				{
					std::ofstream output_stream(output_path, std::ios_base::binary | std::ios_base::out);
					if (!output_stream.is_open()) {
						std::cerr << "Failed to open " << output_path.string() << " for writing." << std::endl;
						continue;
					}
					output_stream.write(output_data.data(), std::streamsize(output_data.size()));
					if (!output_stream.good()) {
						std::cerr << "Failed to write " << output_path.string() << "." << std::endl;
						continue;
					}
				}
			}
		}

		// Converted successfully.
		last_file_errored = false;
	}
	if (last_file_errored) {
		any_errors = true;
	}

	if (argument_convert_mode == convert_mode::create_gbx_texture_wadg) {
		// The fallback for argument_output_path should have been set up earlier if needed.
		std::ofstream output_stream(argument_output_path, std::ios_base::binary | std::ios_base::out);
		if (!output_stream.is_open()) {
			std::cerr << "Failed to open " << argument_output_path.string() << " for writing." << std::endl;
			any_errors = true;
		} else {
			bs2pc::write_wadg(output_stream, gathered_gbx_textures, quake_palette);
			if (!output_stream.good()) {
				std::cerr << "Failed to write " << argument_output_path.string() << "." << std::endl;
				any_errors = true;
			}
		}
	} else if (argument_convert_mode == convert_mode::extract_gbx_textures) {
		uint8_t tga_header[] = {
			// 0 identification field characters.
			0,
			// A color map is included.
			1,
			// Uncompressed, color-mapped.
			1,
			// First color map entry.
			0, 0,
			// Color map length.
			256 & UINT8_MAX, 256 >> 8,
			// Color map entry size (will be set later).
			0,
			// X origin.
			0, 0,
			// Y origin.
			0, 0,
			// Width (will be set later).
			0, 0,
			// Height (will be set later).
			0, 0,
			// Pixel depth.
			8,
			// Image descriptor (lower-left origin, non-interleaved).
			0,
		};
		for (std::pair<std::string const, bs2pc::gbx_texture_deserialized> const & texture_pair :
				gathered_gbx_textures) {
			bs2pc::gbx_texture_deserialized const & texture = texture_pair.second;
			// texture.mip_levels doesn't include the base level.
			if (extract_gbx_texture_mip > texture.mip_levels) {
				continue;
			}
			uint32_t texture_mip_width = texture.scaled_width, texture_mip_height = texture.scaled_height;
			size_t texture_mip_offset = 0;
			for (uint32_t texture_mip = 0;
					texture_mip < extract_gbx_texture_mip && texture_mip_width && texture_mip_height;
					++texture_mip) {
				texture_mip_offset += size_t(texture_mip_width) * size_t(texture_mip_height);
				texture_mip_width >>= 1;
				texture_mip_height >>= 1;
			}
			if (!texture_mip_width || !texture_mip_height) {
				continue;
			}
			std::filesystem::path output_path(
					argument_output_path.empty()
							? std::filesystem::path(texture.name)
							: (argument_output_path / texture.name));
			output_path += '.' + std::to_string(texture.width) + 'x' + std::to_string(texture.height) + ".tga";
			std::ofstream output_stream(output_path, std::ios_base::binary | std::ios_base::out);
			if (output_stream.is_open()) {
				bs2pc::gbx_palette_type const texture_palette_type =
						bs2pc::gbx_texture_palette_type(texture.name.c_str());
				bool const texture_is_transparent = texture_palette_type == bs2pc::gbx_palette_type_transparent;
				tga_header[17] &= ~UINT8_C(0b1111);
				if (texture_is_transparent) {
					tga_header[7] = 32;
					// 8 attribute bits.
					tga_header[17] |= 8;
				} else {
					tga_header[7] = 24;
				}
				tga_header[12] = texture_mip_width & UINT8_MAX;
				tga_header[13] = texture_mip_width >> 8;
				tga_header[14] = texture_mip_height & UINT8_MAX;
				tga_header[15] = texture_mip_height >> 8;
				output_stream.write(reinterpret_cast<char const *>(&tga_header), sizeof(tga_header));
				std::array<uint8_t, 4 * 256> texture_palette_id_bgr;
				bs2pc::gbx_texture_deserialized_palette const & texture_palette_id_indexed =
						texture.palette_id_indexed
								? *texture.palette_id_indexed
								: quake_palette.gbx_id_indexed[texture_palette_type];
				if (texture_is_transparent) {
					for (size_t color_number = 0; color_number < 256; ++color_number) {
						size_t const color_offset = 4 * color_number;
						texture_palette_id_bgr[color_offset] =
								texture_palette_id_indexed[color_offset + 2];
						texture_palette_id_bgr[color_offset + 1] =
								texture_palette_id_indexed[color_offset + 1];
						texture_palette_id_bgr[color_offset + 2] =
								texture_palette_id_indexed[color_offset];
						texture_palette_id_bgr[color_offset + 3] =
								texture_palette_id_indexed[color_offset + 3] ? UINT8_MAX : 0;
					}
				} else {
					if (bs2pc::is_gbx_palette_24_bit(texture_palette_type)) {
						for (size_t color_number = 0; color_number < 256; ++color_number) {
							size_t const color_offset_id = 3 * color_number;
							size_t const color_offset_gbx = 4 * color_number;
							texture_palette_id_bgr[color_offset_id] =
									texture_palette_id_indexed[color_offset_gbx + 2];
							texture_palette_id_bgr[color_offset_id + 1] =
									texture_palette_id_indexed[color_offset_gbx + 1];
							texture_palette_id_bgr[color_offset_id + 2] =
									texture_palette_id_indexed[color_offset_gbx];
						}
					} else {
						uint8_t const texture_random_xor =
								(texture_palette_type == bs2pc::gbx_palette_type_random ? UINT8_MAX : 0);
						for (size_t color_number = 0; color_number < 256; ++color_number) {
							size_t const color_offset_id = 3 * color_number;
							size_t const color_offset_gbx = 4 * color_number;
							texture_palette_id_bgr[color_offset_id] = bs2pc::id_21_bit_color_from_gbx(
									texture_palette_id_indexed[color_offset_gbx + 2]) ^ texture_random_xor;
							texture_palette_id_bgr[color_offset_id + 1] = bs2pc::id_21_bit_color_from_gbx(
									texture_palette_id_indexed[color_offset_gbx + 1]) ^ texture_random_xor;
							texture_palette_id_bgr[color_offset_id + 2] = bs2pc::id_21_bit_color_from_gbx(
									texture_palette_id_indexed[color_offset_gbx]) ^ texture_random_xor;
						}
					}
				}
				output_stream.write(
						reinterpret_cast<char const *>(texture_palette_id_bgr.data()),
						texture_is_transparent ? (4 * 256) : (3 * 256));
				for (uint32_t reverse_y = 0; reverse_y < texture.scaled_height; ++reverse_y) {
					output_stream.write(
							reinterpret_cast<char const *>(
									texture.pixels->data() + texture_mip_offset +
									size_t(texture_mip_width) * (texture_mip_height - 1 - reverse_y)),
							texture_mip_width);
				}
				if (!output_stream.good()) {
					any_errors = true;
				}
			}
		}
	}

	return any_errors ? EXIT_FAILURE : EXIT_SUCCESS;
}
