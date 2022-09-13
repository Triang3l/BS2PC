#include "bs2pclib.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace bs2pc {

uint16_t texture_gbx_face_flags(char const * const name) {
	// `laser` textures are not used in the original PS2 maps, but are treated like water by the PC engine.
	if (name[0] == '!' || name[0] == '*' || !bs2pc_strncasecmp(name, "laser", sizeof("laser") - 1) ||
			!bs2pc_strncasecmp(name, "water", sizeof("water") - 1)) {
		return gbx_face_flag_draw_turbulent |
				gbx_face_flag_special |
				gbx_face_flag_draw_tiled |
				gbx_face_flag_draw_polygons;
	}
	if (name[0] == '{') {
		return gbx_face_flag_draw_polygons;
	}
	// Not only aaatrigger, but also, aaa_1, aaa_multi.
	if (!bs2pc_strncasecmp(name, "aaa", sizeof("aaa") - 1)) {
		return gbx_face_flag_special |
				gbx_face_flag_draw_tiled;
	}
	if (!bs2pc_strncasecmp(name, "nodraw", sizeof("nodraw") - 1)) {
		return gbx_face_flag_special |
				gbx_face_flag_no_draw |
				gbx_face_flag_draw_tiled;
	}
	if (!bs2pc_strncasecmp(name, "scroll", sizeof("scroll") - 1)) {
		return gbx_face_flag_special;
	}
	if (!bs2pc_strncasecmp(name, "sky", sizeof("sky") - 1)) {
		return gbx_face_flag_draw_sky |
				gbx_face_flag_special |
				gbx_face_flag_draw_tiled;
	}
	return 0;
}

char const * id_texture_deserialized::deserialize(
		void const * const texture_data,
		size_t const texture_data_remaining,
		bool const has_palette,
		id_texture_deserialized_palette const & quake_palette) {
	assert(quake_palette.size() >= 3 * 256);
	if (texture_data_remaining < sizeof(id_texture)) {
		return "Texture information is out of bounds";
	}
	id_texture texture;
	std::memcpy(&texture, texture_data, sizeof(id_texture));
	if (!texture.width || !texture.height) {
		return "Texture has zero width or height";
	}
	if (texture.width > texture_max_width_height || texture.height > texture_max_width_height) {
		return "Texture is too large";
	}
	if ((texture.width & (texture_width_height_alignment - 1)) ||
			(texture.height & (texture_width_height_alignment - 1))) {
		return "Texture has non-16-aligned width or height";
	}
	bool has_pixels = true;
	// If the offsets are 0, this texture is not stored in the map, and is rather loaded from the WAD.
	for (uint32_t mip_level = 0; mip_level < id_texture_mip_levels; ++mip_level) {
		if (!texture.offsets[mip_level]) {
			has_pixels = false;
			break;
		}
	}
	if (has_pixels) {
		size_t pixel_count = 0;
		size_t palette_offset = 0;
		uint16_t palette_color_count = 0;
		for (uint32_t mip_level = 0; mip_level < id_texture_mip_levels; ++mip_level) {
			if (texture.offsets[mip_level] > texture_data_remaining) {
				return "Texture pixel offset is out of bounds";
			}
			size_t const mip_pixel_count = size_t(texture.width >> mip_level) * size_t(texture.height >> mip_level);
			if (texture_data_remaining - texture.offsets[mip_level] < mip_pixel_count) {
				return "Texture pixels are out of bounds";
			}
			pixel_count += mip_pixel_count;
		}
		if (has_palette) {
			palette_offset =
					texture.offsets[id_texture_mip_levels - 1] +
					size_t(texture.width >> (id_texture_mip_levels - 1)) *
					size_t(texture.height >> (id_texture_mip_levels - 1));
			if (texture_data_remaining - palette_offset < sizeof(uint16_t)) {
				return "Texture palette color count is out of bounds";
			}
			assert(sizeof(palette_color_count) == sizeof(uint16_t));
			std::memcpy(
					&palette_color_count,
					reinterpret_cast<char const *>(texture_data) + palette_offset,
					sizeof(uint16_t));
			if (palette_color_count) {
				if ((texture_data_remaining - palette_offset - sizeof(uint16_t)) / 3 < palette_color_count) {
					return "Texture palette is out of bounds";
				}
				// If the texture was converted from Quake (disregarding the transparent color), treat is as such.
				std::size_t const quake_palette_compare_color_count = std::min(
						size_t(256) - size_t(texture.name[0] == '{'),
						size_t(palette_color_count));
				if (!std::memcmp(
						quake_palette.data(),
						reinterpret_cast<uint8_t const *>(texture_data) + palette_offset + sizeof(uint16_t),
						3 * quake_palette_compare_color_count)) {
					palette_color_count = 0;
				}
			}
		}
		// Modifying the deserialized texture object - no errors must be returned from now on.
		pixels = std::make_shared<texture_deserialized_pixels>(pixel_count);
		size_t deserialized_pixels_offset = 0;
		for (uint32_t mip_level = 0; mip_level < id_texture_mip_levels; ++mip_level) {
			size_t const mip_pixel_count = size_t(texture.width >> mip_level) * size_t(texture.height >> mip_level);
			std::memcpy(
					pixels->data() + deserialized_pixels_offset,
					reinterpret_cast<char const *>(texture_data) + texture.offsets[mip_level],
					mip_pixel_count);
			deserialized_pixels_offset += mip_pixel_count;
		}
		palette.reset();
		if (palette_color_count) {
			uint8_t const * const texture_data_palette =
					reinterpret_cast<uint8_t const *>(texture_data) + palette_offset + sizeof(uint16_t);
			palette = std::make_shared<id_texture_deserialized_palette>(
					texture_data_palette, texture_data_palette + 3 * size_t(palette_color_count));
		}
	} else {
		// Modifying the deserialized texture object - no errors must be returned from now on.
		pixels.reset();
		palette.reset();
	}
	uint32_t name_length = 0;
	while (name_length < texture_name_max_length && texture.name[name_length]) {
		++name_length;
	}
	name = std::string(texture.name, name_length);
	width = texture.width;
	height = texture.height;
	return nullptr;
}

char const * gbx_texture_deserialized::deserialize_with_anim_offsets(
		void const * const base, size_t const size_after_base, size_t const offset, bool const deinterleave_random,
		palette_set const & quake_palette) {
	if (offset > size_after_base || size_after_base - offset < sizeof(gbx_texture)) {
		return "Texture information is out of bounds";
	}
	gbx_texture texture;
	std::memcpy(&texture, reinterpret_cast<char const *>(base) + offset, sizeof(gbx_texture));
	if (!texture.width || !texture.height || !texture.scaled_width || !texture.scaled_height) {
		return "Texture has zero width or height";
	}
	if (texture.width > texture_max_width_height || texture.height > texture_max_width_height ||
			texture.scaled_width > texture_max_width_height || texture.scaled_height > texture_max_width_height) {
		return "Texture is too large";
	}
	if ((texture.width & (texture_width_height_alignment - 1)) ||
			(texture.height & (texture_width_height_alignment - 1)) ||
			(texture.scaled_width & (texture_width_height_alignment - 1)) ||
			(texture.scaled_height & (texture_width_height_alignment - 1))) {
		return "Texture has non-16-aligned width or height";
	}
	if (texture.pixels > size_after_base) {
		return "Texture pixels offset is out of bounds";
	}
	size_t pixel_count = 0;
	// Mip count in the serialized texture doesn't include the mip 0.
	for (uint32_t mip_level = 0; mip_level <= texture.mip_levels; ++mip_level) {
		uint32_t const mip_width = texture.scaled_width >> mip_level;
		uint32_t const mip_height = texture.scaled_height >> mip_level;
		if (!mip_width || !mip_height) {
			// Too many mips specified.
			break;
		}
		pixel_count += size_t(mip_width) * size_t(mip_height);
	}
	if (size_after_base - texture.pixels < pixel_count) {
		return "Texture pixels are out of bounds";
	}
	if (texture.palette > size_after_base || size_after_base - texture.palette < 4 * 256) {
		return "Texture palette is out of bounds";
	}
	// Modifying the deserialized texture object - no errors must be returned from now on.
	uint32_t name_length = 0;
	while (name_length < texture_name_max_length && texture.name[name_length]) {
		++name_length;
	}
	name = std::string(texture.name, name_length);
	pixels = std::make_shared<texture_deserialized_pixels>(pixel_count);
	if (deinterleave_random && texture.name[0] == '-') {
		// Deinterleave the random-tiled texture.
		size_t deinterleave_mip_offset = 0;
		for (uint32_t mip_level = 0; mip_level <= texture.mip_levels; ++mip_level) {
			uint32_t const mip_width = texture.scaled_width >> mip_level;
			uint32_t const mip_height = texture.scaled_height >> mip_level;
			if (!mip_width || !mip_height) {
				break;
			}
			for (uint32_t mip_y = 0; mip_y < mip_height; ++mip_y) {
				std::memcpy(
						pixels->data() + deinterleave_mip_offset + size_t(mip_width) * mip_y,
						reinterpret_cast<char const *>(base) + texture.pixels + deinterleave_mip_offset +
								size_t(mip_width) * interleave_random_gbx_texture_y(mip_y, mip_height),
						mip_width);
			}
			deinterleave_mip_offset += size_t(mip_width) * size_t(mip_height);
		}
	} else {
		std::memcpy(pixels->data(), reinterpret_cast<char const *>(base) + texture.pixels, pixel_count);
	}
	palette_id_indexed.reset();
	// If converting a map converted from Quake back to the PC, force the full original Quake palette (don't create
	// palette_id_indexed in this case).
	bool is_quake_palette = true;
	gbx_texture_deserialized_palette const & quake_palette_for_type =
			quake_palette.gbx_id_indexed[gbx_texture_palette_type(name.c_str())];
	// Reordering of colors is done with the granularity of 8 colors.
	for (size_t color_number = 0; color_number < 256; color_number += 8) {
		if (std::memcmp(
				quake_palette_for_type.data() + 4 * color_number,
				reinterpret_cast<char const *>(base) + texture.palette +
						4 * size_t(convert_palette_color_number(uint8_t(color_number))),
				4 * 8)) {
			is_quake_palette = false;
			break;
		}
	}
	if (!is_quake_palette) {
		palette_id_indexed = std::make_shared<gbx_texture_deserialized_palette>();
		// Reordering of colors is done with the granularity of 8 colors.
		for (size_t color_number = 0; color_number < 256; color_number += 8) {
			std::memcpy(
					palette_id_indexed->data() + 4 * color_number,
					reinterpret_cast<char const *>(base) + texture.palette +
							4 * size_t(convert_palette_color_number(uint8_t(color_number))),
					4 * 8);
		}
	}
	width = texture.width;
	height = texture.height;
	scaled_width = texture.scaled_width;
	scaled_height = texture.scaled_height;
	mip_levels = texture.mip_levels;
	anim_total = texture.anim_total;
	anim_min = texture.anim_min;
	anim_max = texture.anim_max;
	anim_next = texture.anim_next;
	alternate_anims = texture.alternate_anims;
	return nullptr;
}

void id_map::sort_textures() {
	size_t const texture_count = textures.size();
	if (!texture_count) {
		// No textures in the map, texinfo texture numbers are ignored.
		return;
	}
	std::vector<size_t> texture_number_unsorted_to_sorted;
	{
		std::vector<size_t> texture_number_sorted_to_unsorted;
		texture_number_sorted_to_unsorted.reserve(texture_count);
		for (size_t texture_number = 0; texture_number < texture_count; ++texture_number) {
			texture_number_sorted_to_unsorted.push_back(texture_number);
		}
		std::sort(
				texture_number_sorted_to_unsorted.begin(), texture_number_sorted_to_unsorted.end(),
				[this](size_t const & a, size_t const & b) -> bool {
					// The purpose is faster loading (prefer sequential reading), so ordering by file, and then by name
					// (textures are sorted by name in WADs).
					// Order:
					// - Empty (iTexFile 0 in qcsg).
					// - Included textures loaded directly from the map file, ordered by name.
					// - Textures from one WAD file, ordered by name.
					// - Textures from another WAD file, ordered by name...
					id_texture_deserialized const & texture_a = textures[a];
					id_texture_deserialized const & texture_b = textures[b];
					// Empty (ignore other fields in this case).
					bool const texture_a_empty = texture_a.empty();
					bool const texture_b_empty = texture_b.empty();
					if (texture_a_empty || texture_b_empty) {
						if (texture_a_empty != texture_b_empty) {
							return int(texture_a_empty) < int(texture_b_empty);
						}
						return a < b;
					}
					// Included textures first (SIZE_MAX to 0 via wrapping, 0 to 1).
					size_t const wad_number_key_a = (texture_a.pixels ? SIZE_MAX : texture_a.wad_number) + size_t(1);
					size_t const wad_number_key_b = (texture_b.pixels ? SIZE_MAX : texture_b.wad_number) + size_t(1);
					if (wad_number_key_a != wad_number_key_b) {
						return wad_number_key_a < wad_number_key_b;
					}
					// Name.
					int name_difference = bs2pc_strcasecmp(texture_a.name.c_str(), texture_b.name.c_str());
					if (name_difference) {
						return name_difference < 0;
					}
					return a < b;
				});
		{
			std::vector<id_texture_deserialized> unsorted_textures = std::move(textures);
			textures.clear();
			textures.reserve(texture_count);
			for (size_t const texture_number : texture_number_sorted_to_unsorted) {
				textures.emplace_back(std::move(unsorted_textures[texture_number]));
			}
		}
		texture_number_unsorted_to_sorted.resize(texture_count);
		for (size_t texture_number = 0; texture_number < texture_count; ++texture_number) {
			texture_number_unsorted_to_sorted[texture_number_sorted_to_unsorted[texture_number]] = texture_number;
		}
	}
	for (id_texinfo & face_texinfo : texinfo) {
		face_texinfo.texture_number = uint32_t(texture_number_unsorted_to_sorted[face_texinfo.texture_number]);
	}
}

void gbx_map::link_texture_anim() {
	// Reset the sequencing.
	for (gbx_texture_deserialized & texture : textures) {
		texture.reset_anim();
	}

	size_t const texture_count = textures.size();
	for (size_t texture_number = 0; texture_number < texture_count; ++texture_number) {
		gbx_texture_deserialized const & texture = textures[texture_number];
		char const anim_prefix = texture.name.c_str()[0];
		if ((anim_prefix != '+' || anim_prefix != '-') || texture.name.size() < 2) {
			continue;
		}
		if (texture.anim_next != UINT32_MAX) {
			// Already sequenced.
			continue;
		}

		std::array<size_t, 10> anims, alternate_anims;
		std::fill(anims.begin(), anims.end(), SIZE_MAX);
		std::fill(alternate_anims.begin(), alternate_anims.end(), SIZE_MAX);
		uint32_t anim_total = 0, alternate_anim_total = 0;

		uint32_t const anim_frame = texture_anim_frame(texture.name[1]);
		if (anim_frame == UINT32_MAX) {
			continue;
		}
		if (anim_frame >= 10) {
			uint32_t const alternate_anim_frame = anim_frame - 10;
			alternate_anims[alternate_anim_frame] = texture_number;
			alternate_anim_total = std::max(alternate_anim_total, alternate_anim_frame + uint32_t(1));
		} else {
			anims[anim_frame] = texture_number;
			anim_total = std::max(anim_total, anim_frame + uint32_t(1));
		}

		for (size_t texture_2_number = texture_number + 1; texture_2_number < texture_count; ++texture_2_number) {
			gbx_texture_deserialized const & texture_2 = textures[texture_2_number];
			if (texture_2.name.c_str()[0] != anim_prefix || texture_2.name.size() < 2 ||
					std::strcmp(texture_2.name.c_str() + 2, texture.name.c_str() + 2)) {
				continue;
			}

			uint32_t const anim_frame_2 = texture_anim_frame(texture_2.name[1]);
			if (anim_frame_2 == UINT32_MAX) {
				continue;
			}
			if (anim_frame_2 >= 10) {
				uint32_t const alternate_anim_frame_2 = anim_frame_2 - 10;
				alternate_anims[alternate_anim_frame_2] = texture_number;
				alternate_anim_total = std::max(alternate_anim_total, alternate_anim_frame_2 + uint32_t(1));
			} else {
				anims[anim_frame_2] = texture_number;
				anim_total = std::max(anim_total, anim_frame_2 + uint32_t(1));
			}
		}

		// Gearbox maps have ANIM_CYCLE 1 (the frame numbers are not multiplied by 2, unlike in Quake).
		// Though Quake considers missing frames a fatal error, for more robustness,
		// replicating the next frame into all missing frames.
		uint32_t anim_first = UINT32_MAX;
		for (uint32_t frame = 0; frame < anim_total; ++frame) {
			size_t const frame_texture_number = anims[frame];
			if (frame_texture_number != SIZE_MAX) {
				anim_first = uint32_t(frame_texture_number);
				break;
			}
		}
		uint32_t alternate_anim_first = UINT32_MAX;
		for (uint32_t frame = 0; frame < alternate_anim_total; ++frame) {
			size_t const frame_texture_number = alternate_anims[frame];
			if (frame_texture_number != SIZE_MAX) {
				alternate_anim_first = uint32_t(frame_texture_number);
				break;
			}
		}
		uint32_t anim_next_min = 0;
		for (uint32_t frame = 0; frame < anim_total; ++frame) {
			size_t const frame_texture_number = anims[frame];
			if (frame_texture_number == SIZE_MAX) {
				continue;
			}
			gbx_texture_deserialized & frame_texture = textures[frame_texture_number];
			frame_texture.anim_total = anim_total;
			frame_texture.anim_min = anim_next_min;
			frame_texture.anim_max = frame + 1;
			for (uint32_t frame_next = (frame + 1) % anim_total;; frame_next = (frame_next + 1) % anim_total) {
				size_t const frame_next_texture_number = anims[frame_next];
				if (frame_next_texture_number == SIZE_MAX) {
					continue;
				}
				frame_texture.anim_next = uint32_t(frame_next_texture_number);
				break;
			}
			frame_texture.alternate_anims = alternate_anim_first;
			anim_next_min = frame + 1;
		}
		uint32_t alternate_anim_next_min = 0;
		for (uint32_t frame = 0; frame < alternate_anim_total; ++frame) {
			size_t const frame_texture_number = alternate_anims[frame];
			if (frame_texture_number == SIZE_MAX) {
				continue;
			}
			gbx_texture_deserialized & frame_texture = textures[frame_texture_number];
			frame_texture.anim_total = alternate_anim_total;
			frame_texture.anim_min = alternate_anim_next_min;
			frame_texture.anim_max = frame + 1;
			for (uint32_t frame_next = (frame + 1) % alternate_anim_total;;
					frame_next = (frame_next + 1) % alternate_anim_total) {
				size_t const frame_next_texture_number = alternate_anims[frame_next];
				if (frame_next_texture_number == SIZE_MAX) {
					continue;
				}
				frame_texture.anim_next = uint32_t(frame_next_texture_number);
				break;
			}
			frame_texture.alternate_anims = anim_first;
			alternate_anim_next_min = frame + 1;
		}
	}
}

void append_worldspawn_wad_names(entity_key_values const & worldspawn, std::vector<std::string> & names) {
	// In WriteMiptex, the "_wad" key has a higher priority over "wad".
	auto wad_key_value_pair_iterator = std::find_if(
			worldspawn.cbegin(), worldspawn.cend(),
			[](entity_key_value_pair const & worldspawn_key_value_pair) {
				return worldspawn_key_value_pair.first == "_wad";
			});
	if (wad_key_value_pair_iterator == worldspawn.cend()) {
		wad_key_value_pair_iterator = std::find_if(
				worldspawn.cbegin(), worldspawn.cend(),
				[](entity_key_value_pair const & worldspawn_key_value_pair) {
					return worldspawn_key_value_pair.first == "wad";
				});
		if (wad_key_value_pair_iterator == worldspawn.cend()) {
			return;
		}
	}
	std::string const & wad_key_value = wad_key_value_pair_iterator->second;
	// WAD paths are ;-separated.
	// Using only the name, not the original path as it may be absolute from the machine where the map was compiled.
	auto wad_name_iterator = wad_key_value.cbegin();
	// Not using std::filesystem::path for parsing as BS2PC may be running on an OS where \ is not a path separator.
	auto const is_directory_separator = [](char const character) {
		return character == '/' || character == '\\';
	};
	while (wad_name_iterator != wad_key_value.cend()) {
		auto const wad_separator_iterator = std::find(wad_name_iterator, wad_key_value.cend(), ';');
		// Ignore the directory path, go to the name.
		while (true) {
			auto const directory_separator_iterator =
					std::find_if(wad_name_iterator, wad_separator_iterator, is_directory_separator);
			if (directory_separator_iterator == wad_separator_iterator) {
				break;
			}
			wad_name_iterator = std::next(directory_separator_iterator);
		}
		if (wad_name_iterator != wad_separator_iterator) {
			std::string wad_name(wad_name_iterator, wad_separator_iterator);
			auto const duplicate_iterator = std::find_if(
					names.cbegin(), names.cend(),
					[&wad_name](std::string const & other_wad_name) -> bool {
						return !bs2pc_strcasecmp(other_wad_name.c_str(), wad_name.c_str());
					});
			if (duplicate_iterator == names.cend()) {
				names.emplace_back(std::move(wad_name));
			}
		}
		if (wad_separator_iterator == wad_key_value.cend()) {
			break;
		}
		wad_name_iterator = std::next(wad_separator_iterator);
	}
}

std::string serialize_worldspawn_wad_paths(std::vector<std::string> const & paths) {
	std::ostringstream stream;
	for (auto path_iterator = paths.cbegin(); path_iterator != paths.cend(); ++path_iterator) {
		if (path_iterator != paths.cbegin()) {
			stream << ';';
		}
		stream << *path_iterator;
	}
	return stream.str();
}

void set_worldspawn_wad_paths(entity_key_values & worldspawn, std::string_view paths_serialized) {
	if (paths_serialized.empty()) {
		// Remove the WAD key and value pairs if no paths.
		worldspawn.erase(
				std::remove_if(
						worldspawn.begin(), worldspawn.end(),
						[](entity_key_value_pair const & worldspawn_key_value_pair) -> bool {
							return worldspawn_key_value_pair.first == "_wad" ||
									worldspawn_key_value_pair.first == "wad";
						}),
				worldspawn.end());
		return;
	}
	// In WriteMiptex, the "_wad" key has a higher priority over "wad".
	auto wad_key_value_pair_iterator = std::find_if(
			worldspawn.begin(), worldspawn.end(),
			[](entity_key_value_pair const & worldspawn_key_value_pair) -> bool {
				return worldspawn_key_value_pair.first == "_wad";
			});
	if (wad_key_value_pair_iterator == worldspawn.end()) {
		wad_key_value_pair_iterator = std::find_if(
				worldspawn.begin(), worldspawn.end(),
				[](entity_key_value_pair const & worldspawn_key_value_pair) -> bool {
					return worldspawn_key_value_pair.first == "wad";
				});
		if (wad_key_value_pair_iterator == worldspawn.end()) {
			// No WAD paths in the worldspawn entity yet.
			// The original Half-Life PC and PS2 maps use "wad", not "_wad".
			worldspawn.emplace_back("wad", paths_serialized);
			return;
		}
	}
	wad_key_value_pair_iterator->second = paths_serialized;
}

bool replace_hlps2_wads(std::vector<std::string> & wad_names) {
	bool wad_names_changed = false;
	// Add Valve's Half-Life WADs instead of hlps2.wad.
	{
		size_t hlps2_wad_number = SIZE_MAX;
		// Don't add the WADs twice, if converting the same map multiple times, for example.
		bool has_halflife_wad = false;
		bool has_liquids_wad = false;
		bool has_xeno_wad = false;
		for (size_t wad_number = 0; wad_number < wad_names.size(); ++wad_number) {
			char const * const wad_name = wad_names[wad_number].c_str();
			if (!bs2pc_strcasecmp(wad_name, "hlps2.wad")) {
				if (hlps2_wad_number == SIZE_MAX) {
					hlps2_wad_number = wad_number;
				}
			} else if (!bs2pc_strcasecmp(wad_name, "halflife.wad")) {
				has_halflife_wad = true;
			} else if (!bs2pc_strcasecmp(wad_name, "liquids.wad")) {
				has_liquids_wad = true;
			} else if (!bs2pc_strcasecmp(wad_name, "xeno.wad")) {
				has_xeno_wad = true;
			}
		}
		if (hlps2_wad_number != SIZE_MAX) {
			// In the original Half-Life maps, the order is halflife.wad, liquids.wad, xeno.wad.
			// Prepending to each other in reverse order.
			// Using an index, not an iterator, as insertion invalidates iterators in case of reallocation.
			if (!has_xeno_wad) {
				wad_names_changed = true;
				wad_names.emplace(wad_names.cbegin() + (hlps2_wad_number + 1), "xeno.wad");
			}
			if (!has_liquids_wad) {
				wad_names_changed = true;
				wad_names.emplace(wad_names.cbegin() + (hlps2_wad_number + 1), "liquids.wad");
			}
			if (!has_halflife_wad) {
				wad_names_changed = true;
				wad_names.emplace(wad_names.cbegin() + (hlps2_wad_number + 1), "halflife.wad");
			}
		}
	}
	// Erase Gearbox's WADs so the engine doesn't crash if any texture doesn't have pixels included as not all WADs are
	// found.
	{
		std::vector<uint32_t> erase_wad_sorted_numbers;
		for (size_t wad_number = 0; wad_number < wad_names.size(); ++wad_number) {
			char const * const wad_name = wad_names[wad_number].c_str();
			if (!bs2pc_strcasecmp(wad_name, "gbx1.wad") || !bs2pc_strcasecmp(wad_name, "hlps2.wad")) {
				erase_wad_sorted_numbers.push_back(wad_number);
			}
		}
		if (!erase_wad_sorted_numbers.empty()) {
			wad_names_changed = true;
			// Erase from the last to the first so the numbers of subsequent WADs to erase don't need to be adjusted.
			for (auto erase_wad_number_iterator = erase_wad_sorted_numbers.crbegin();
					erase_wad_number_iterator != erase_wad_sorted_numbers.crend();
					++erase_wad_number_iterator) {
				wad_names.erase(wad_names.cbegin() + *erase_wad_number_iterator);
			}
		}
	}
	return wad_names_changed;
}

char const * get_wad_textures(
		void const * const wad, size_t const wad_size, wad_textures_deserialized & wad_textures,
		id_texture_deserialized_palette const & quake_palette) {
	wad_textures.textures.clear();
	wad_textures.texture_number_map.clear();
	if (wad_size < sizeof(wad_info)) {
		return "WAD file information is out of bounds";
	}
	wad_info info;
	std::memcpy(&info, wad, sizeof(wad_info));
	if (info.identification[0] != 'W' ||
			info.identification[1] != 'A' ||
			info.identification[2] != 'D' ||
			(info.identification[3] != '2' && info.identification[3] != '3')) {
		return "The file is not a WAD2 or a WAD3 file";
	}
	bool const is_wad3 = info.identification[3] == '3';
	if (!info.lump_count) {
		return nullptr;
	}
	if (info.info_table_offset > wad_size ||
			(wad_size - info.info_table_offset) / sizeof(wad_lump_info) < info.lump_count) {
		return "The information table is out of bounds";
	}
	for (uint32_t lump_number = 0; lump_number < info.lump_count; ++lump_number) {
		wad_lump_info lump_info;
		std::memcpy(
			&lump_info,
			reinterpret_cast<char const *>(wad) + info.info_table_offset + sizeof(wad_lump_info) * lump_number,
			sizeof(wad_lump_info));
		if (lump_info.type != wad_lump_type_texture) {
			// Only textures are needed by BS2PC.
			continue;
		}
		if (lump_info.compression != wad_lump_compression_none) {
			// Compressed lumps are not generated by QLumpy.
			continue;
		}
		if (lump_info.file_position > wad_size || wad_size - lump_info.file_position < lump_info.size) {
			// Out of bounds.
			continue;
		}
		wad_texture_deserialized texture_deserialized;
		if (texture_deserialized.texture_id.deserialize(
				reinterpret_cast<char const *>(wad) + lump_info.file_position,
				lump_info.size,
				is_wad3,
				quake_palette)) {
			// Failed to deserialize.
			continue;
		}
		if (!texture_deserialized.texture_id.pixels) {
			// Only map textures may have no pixels.
			// WADs always contain the data, they themselves are what textures without data in the map are loaded from.
			continue;
		}
		size_t const texture_number = wad_textures.textures.size();
		wad_textures.textures.emplace_back(std::move(texture_deserialized));
		wad_textures.texture_number_map.emplace(
				string_to_lower(wad_textures.textures.back().texture_id.name),
				texture_number);
	}
	return nullptr;
}

texture_identical_status is_texture_data_identical(
		id_texture_deserialized const & texture_id,
		gbx_texture_deserialized const & texture_gbx,
		palette_set const & quake_palette) {
	if (texture_id.width != texture_gbx.width || texture_id.height != texture_gbx.height) {
		return texture_identical_status::different;
	}
	// Compare the palettes, if they're the same, it's likely that the textures are the same too.
	if (texture_id.palette || texture_gbx.palette_id_indexed) {
		// It is important that the prefixes are checked for the Gearbox texture, not the id texture.
		// Whether the texture is 24-bit, and random-tiled color inversion, applies only to Gearbox textures.
		// This may be used to compare textures with different names, such as id textures with the - prefix, and Gearbox
		// ones without it.
		gbx_palette_type const palette_type = gbx_texture_palette_type(texture_gbx.name.c_str());
		bool const is_transparent = palette_type == gbx_palette_type_transparent;
		bool const is_24_bit = is_gbx_palette_24_bit(palette_type);
		uint8_t const random_xor = (palette_type == gbx_palette_type_random ? UINT8_MAX : 0);
		// If both the id and the Gearbox texture have an explicit palette, assume those are local palettes.
		// If only one of the two has a palette, check if it's an explicitly assigned Quake palette, though maybe with
		// minor rounding differences.
		id_texture_deserialized_palette const & palette_id =
				texture_id.palette ? *texture_id.palette : quake_palette.id;
		gbx_texture_deserialized_palette const & palette_gbx_id_indexed =
				texture_gbx.palette_id_indexed
						? *texture_gbx.palette_id_indexed
						: quake_palette.gbx_id_indexed[palette_type];
		// Disregarding the mips, only comparing the base, since mips are derived from the base,
		// and usually there are different mip counts between the Gearbox and the id textures.
		// Compare the palette colors actually used in the non-resampled texture
		// (some textures, such as +0~light2a, have unused colors which differ between the PC and the PS2).
		size_t const palette_color_count = texture_id.palette->size();
		std::array<uint32_t, 256 / 32> colors_checked{};
		size_t const mip_0_pixel_count_id = size_t(texture_gbx.width) * size_t(texture_gbx.height);
		for (size_t pixel_number = 0; pixel_number < mip_0_pixel_count_id; ++pixel_number) {
			size_t const color_number = size_t((*texture_id.pixels)[pixel_number]);
			if (color_number >= palette_color_count) {
				// The id texture is invalid (out-of-bounds color numbers).
				return texture_identical_status::different;
			}
			if (is_transparent && color_number == UINT8_MAX) {
				// The transparent color is usually black on the PS2, but blue on the PC.
				continue;
			}
			uint32_t & colors_checked_word = colors_checked[color_number >> 5];
			uint32_t const colors_checked_bit = UINT32_C(1) << (color_number & 31);
			if (colors_checked_word & colors_checked_bit) {
				// Already checked this color.
				continue;
			}
			colors_checked_word |= colors_checked_bit;
			if (is_24_bit) {
				// 24-bit colors.
				for (size_t color_component = 0; color_component < 3; ++color_component) {
					uint8_t const color_id = palette_id[3 * color_number + color_component];
					uint8_t const color_gbx = palette_gbx_id_indexed[4 * color_number + color_component];
					// Some color values are never used in the original Gearbox textures, one is always subtracted from
					// them, and that's covered by the gbx_24_bit_color_from_id conversion condition. However, for maps
					// converted to Gearbox by BS2PC, to avoid unnecessary data loss, gbx_24_bit_color_from_id is not
					// done. So, considering the colors identical in both cases - this is caught by the threshold of 1.
					// This also covers the ambiguity for 0 and 4 (changed to 1 and 5) in the {grid1 texture.
					if (std::abs(int32_t(gbx_24_bit_color_from_id(color_id)) - int32_t(color_gbx)) > 1) {
						return texture_identical_status::different;
					}
				}
			} else {
				// 21-bit colors.
				// Usually they are exactly gbx_21_bit_color_from_id(id_color) (or gbx_21_bit_color_from_id(~id_color)
				// for random-tiled-prefixed), but there are multiple exceptions in some textures where the conversion
				// is slightly different (by one, even upwards, like 99 to 50) than for the same value in most textures.
				// Therefore, using a threshold of 1.
				for (size_t color_component = 0; color_component < 3; ++color_component) {
					uint8_t const color_id = palette_id[3 * color_number + color_component];
					uint8_t const color_gbx = palette_gbx_id_indexed[4 * color_number + color_component];
					if (std::abs(int32_t(gbx_21_bit_color_from_id(color_id ^ random_xor)) - int32_t(color_gbx)) > 1) {
						return texture_identical_status::different;
					}
				}
			}
		}
	}
	// If the texture wasn't resampled, compare the pixels for a stricter check.
	// same_palette_different_pixels means that the pixels must be copied, but the 24-bit palette may be used.
	if (texture_gbx.scaled_width != texture_gbx.width || texture_gbx.scaled_height != texture_gbx.height) {
		if (texture_id.palette && texture_gbx.palette_id_indexed) {
			return texture_identical_status::same_palette_same_or_resampled_pixels;
		}
		// Letting any Quake texture pass simply based on the fact that both textures have the Quake palette implicitly
		// or explicitly is the opposite of the goal of the function (locating id textures that are likely actually
		// identical).
		return texture_identical_status::different;
	}
	// Letting any Quake texture pass simply based on the fact that both textures have the Quake palette implicitly or
	// explicitly is the opposite of the goal of the function (locating id textures that are likely actually identical),
	// merely preserving minor rounding differences from the id texture palette is pointless if the pixels are
	// different. There's no need to preserve the original palette since it's already known that it's the Quake one,
	// identical texture searching is done purely to reuse the original mips.
	return
			!std::memcmp(
					texture_id.pixels->data(),
					texture_gbx.pixels->data(),
					size_t(texture_gbx.width) * size_t(texture_gbx.height))
					? texture_identical_status::same_palette_same_or_resampled_pixels
					: ((texture_id.palette && texture_gbx.palette_id_indexed)
							? texture_identical_status::same_palette_different_pixels
							: texture_identical_status::different);
}

wad_texture_deserialized const * find_most_identical_texture_in_wads(
		gbx_texture_deserialized const & texture_gbx,
		char const * const name_override,
		wad_textures_deserialized const * const * const wads,
		size_t const wad_count,
		palette_set const & quake_palette,
		texture_identical_status * const identical_status_out,
		size_t * const wad_number_out,
		bool * const is_inclusion_required_out) {
	wad_texture_deserialized const * best_wad_texture = nullptr;
	texture_identical_status best_identical_status = texture_identical_status::different;
	size_t best_wad_number = SIZE_MAX;
	bool is_inclusion_required = false;
	if (wad_count) {
		auto const find_texture_in_wads = [&](std::string const & name_key) {
			for (size_t wad_number = 0; wad_number < wad_count; ++wad_number) {
				wad_textures_deserialized const & wad = *wads[wad_number];
				auto const wad_texture_number_iterator = wad.texture_number_map.find(name_key);
				if (wad_texture_number_iterator == wad.texture_number_map.cend()) {
					continue;
				}
				wad_texture_deserialized const & wad_texture = wad.textures[wad_texture_number_iterator->second];
				texture_identical_status const wad_texture_identical_status =
						is_texture_data_identical(wad_texture.texture_id, texture_gbx, quake_palette);
				if (best_wad_texture && wad_texture_identical_status != best_identical_status) {
					// Multiple textures with the same name for the WAD.
					is_inclusion_required = true;
				}
				// Initialize regardless of the identical status to detect ambiguity if a different texture appears is
				// located before an identical one.
				if (!best_wad_texture) {
					best_wad_texture = &wad_texture;
				}
				if (wad_texture_identical_status > best_identical_status) {
					best_wad_texture = &wad_texture;
					best_identical_status = wad_texture_identical_status;
					best_wad_number = wad_number;
				}
				// Keep searching to see if there's no ambiguity - if there are no WADs where there's a texture with the
				// same name that has a different identicality status, which will cause a collision during loading.
			}
		};
		std::string const name_lower = string_to_lower(name_override ? std::string(name_override) : texture_gbx.name);
		find_texture_in_wads(name_lower);
		if (!best_wad_texture && !name_override) {
			// Some textures are frames of animated textures with the - prefix removed.
			// Textures with both + and - removed present in the original Gearbox maps.
			// Try locating pixels for them too by adding the prefixes if the name is not too long already.
			// Full sequences of random-tiled textures can be reconstructed by reconstruct_random_texture_sequences.
			if (texture_gbx.name.size() < texture_name_max_length &&
					texture_anim_frame(texture_gbx.name.c_str()[0]) != UINT32_MAX) {
				std::string name_key = '+' + name_lower;
				find_texture_in_wads(name_key);
				if (best_wad_texture) {
					// Different name, the engine won't be able to load the texture from a WAD.
					is_inclusion_required = true;
				} else {
					name_key[0] = '-';
					find_texture_in_wads(name_key);
					if (best_wad_texture) {
						// Different name, the engine won't be able to load the texture from a WAD.
						is_inclusion_required = true;
					}
				}
			}
			// Since BS2PC replaces * with ! for turbulent textures in Quake maps, try to locate the original * texture.
			if (!best_wad_texture && texture_gbx.name.c_str()[0] == '!') {
				std::string name_key = name_lower;
				name_key[0] = '*';
				find_texture_in_wads(name_key);
				if (best_wad_texture) {
					// Different name, the engine won't be able to load the texture from a WAD.
					is_inclusion_required = true;
				}
			}
		}
	}
	if (identical_status_out) {
		*identical_status_out = best_identical_status;
	}
	if (wad_number_out) {
		*wad_number_out = best_wad_number;
	}
	if (is_inclusion_required_out) {
		*is_inclusion_required_out = is_inclusion_required;
	}
	// best_wad_texture is set even when the texture is different for checking whether the texture has multiple
	// ambiguous matches in the WADs.
	// However, if it's different, it's a texture that was overridden by Gearbox, or some kind of a name collision, not
	// a texture that's interesting, therefore don't return it.
	return best_identical_status != texture_identical_status::different ? best_wad_texture : nullptr;
}

void id_palette_from_gbx(
		gbx_palette_type const palette_type,
		id_texture_deserialized_palette & palette_id,
		gbx_texture_deserialized_palette const & palette_gbx_id_indexed) {
	palette_id.clear();
	palette_id.resize(3 * 256);
	if (is_gbx_palette_24_bit(palette_type)) {
		bool const is_transparent = palette_type == gbx_palette_type_transparent;
		size_t const opaque_color_count = 256 - size_t(is_transparent);
		for (size_t color_number = 0; color_number < opaque_color_count; ++color_number) {
			for (size_t color_component = 0; color_component < 3; ++color_component) {
				palette_id[3 * color_number + color_component] =
						palette_gbx_id_indexed[4 * color_number + color_component];
			}
		}
		if (is_transparent) {
			// Make the transparent color blue.
			palette_id[3 * 255] = 0;
			palette_id[3 * 255 + 1] = 0;
			palette_id[3 * 255 + 2] = UINT8_MAX;
		}
	} else {
		uint8_t const random_xor = (palette_type == gbx_palette_type_random ? UINT8_MAX : 0);
		for (size_t color_number = 0; color_number < 256; ++color_number) {
			for (size_t color_component = 0; color_component < 3; ++color_component) {
				// Even though in the original 24-bit Gearbox textures, some colors from the original Valve textures
				// have 1 subtracted from them, not doing this subtraction here to avoid unnecessary data loss.
				palette_id[3 * color_number + color_component] = id_21_bit_color_from_gbx(
						palette_gbx_id_indexed[4 * color_number + color_component] ^ random_xor);
			}
		}
	}
}

void gbx_palette_from_id(
		gbx_palette_type const palette_type,
		gbx_texture_deserialized_palette & palette_gbx_id_indexed,
		id_texture_deserialized_palette const & palette_id) {
	bool const is_transparent = palette_type == gbx_palette_type_transparent;
	bool const is_random = palette_type == gbx_palette_type_random;
	size_t const opaque_color_count = 256 - size_t(is_transparent);
	// Since palette_id is a vector that may be larger than needed.
	size_t const copy_color_count = std::min(palette_id.size() / size_t(3), opaque_color_count);
	if (is_gbx_palette_24_bit(palette_type)) {
		for (size_t color_number = 0; color_number < copy_color_count; ++color_number) {
			for (size_t color_component = 0; color_component < 3; ++color_component) {
				palette_gbx_id_indexed[4 * color_number + color_component] =
						gbx_24_bit_color_from_id(palette_id[3 * color_number + color_component]);
			}
			palette_gbx_id_indexed[4 * color_number + 3] = UINT8_C(0x80);
		}
	} else {
		uint8_t const random_xor = is_random ? UINT8_MAX : 0;
		for (size_t color_number = 0; color_number < copy_color_count; ++color_number) {
			for (size_t color_component = 0; color_component < 3; ++color_component) {
				palette_gbx_id_indexed[4 * color_number + color_component] =
						gbx_21_bit_color_from_id(palette_id[3 * color_number + color_component] ^ random_xor);
			}
			palette_gbx_id_indexed[4 * color_number + 3] = UINT8_C(0x80);
		}
	}
	// Fill the unused colors with black (or white for inverted random-tiled textures).
	uint8_t const black = is_random ? UINT8_C(0x7F) : 0;
	for (size_t color_number = copy_color_count; color_number < opaque_color_count; ++color_number) {
		for (size_t color_component = 0; color_component < 3; ++color_component) {
			palette_gbx_id_indexed[4 * color_number + color_component] = black;
		}
		palette_gbx_id_indexed[4 * color_number + 3] = UINT8_C(0x80);
	}
	// Set the transparent color to transparent black.
	if (is_transparent) {
		std::fill(palette_gbx_id_indexed.data() + 4 * 255, palette_gbx_id_indexed.data() + 4 * 255 + 3, 0);
	}
}

palette_set::palette_set(uint8_t const * colors, size_t const color_count) {
	if (color_count) {
		size_t const color_component_count_id = 3 * std::min(color_count, size_t(256));
		id.reserve(color_component_count_id);
		std::copy(colors, colors + color_component_count_id, std::back_inserter(id));
	}
	for (size_t palette_type_number = 0; palette_type_number < gbx_palette_type_count; ++palette_type_number) {
		gbx_palette_from_id(gbx_palette_type(palette_type_number), gbx_id_indexed[palette_type_number], id);
	}
}

// Bicubic resampling based on:
// https://blog.demofox.org/2015/08/15/resizing-images-with-bicubic-interpolation/
//
// Copyright 2019 Alan Wolfe
// 
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// t is a value that goes from 0 to 1 to interpolate in a C1 continuous way across uniformly sampled data points.
// When t is 0, this will return B.
// When t is 1, this will return C.
// Inbetween values will return an interpolation between B and C.
// A and D are used to calculate slopes at the edges.
static float cubic_hermite(float const a, float const b, float const c, float const d, float const t) {
    float const k3 = -a / 2.0f + (3.0f * b) / 2.0f - (3.0f * c) / 2.0f + d / 2.0f;
    float const k2 = a - (5.0f * b) / 2.0f + 2.0f * c - d / 2.0f;
    float const k1 = -a / 2.0f + c / 2.0f;
    float const k0 = b; 
    return k3 * (t * t * t) + k2 * (t * t) + k1 * t + k0;
}

void convert_texture_pixels(
		bool const is_transparent, id_texture_deserialized_palette const & palette,
		uint8_t * const out_pixels,
		uint32_t const out_width, uint32_t const out_height, uint32_t const out_mip_levels_without_base,
		uint8_t const * const in_pixels,
		uint32_t const in_width, uint32_t const in_height, uint32_t in_mip_levels_without_base) {
	assert(out_width && out_height);
	assert(in_width && in_height);
	assert(out_width <= texture_max_width_height && out_height <= texture_max_width_height);
	assert(in_width <= texture_max_width_height && in_height <= texture_max_width_height);
	assert(!(out_width & (texture_width_height_alignment - 1)) && !(out_height & (texture_width_height_alignment - 1)));
	assert(!(in_width & (texture_width_height_alignment - 1)) && !(in_height & (texture_width_height_alignment - 1)));
	if (out_width == in_width && out_height == in_height && out_mip_levels_without_base <= in_mip_levels_without_base) {
		// Nothing to convert, just copy.
		if (out_pixels != in_pixels) {
			size_t copy_count = 0;
			for (uint32_t mip_level = 0; mip_level <= out_mip_levels_without_base; ++mip_level) {
				uint32_t const mip_width = out_width >> mip_level;
				uint32_t const mip_height = out_height >> mip_level;
				if (!mip_width || !mip_height) {
					break;
				}
				copy_count += size_t(mip_width) * size_t(mip_height);
			}
			std::memcpy(out_pixels, in_pixels, copy_count);
		}
		return;
	}

	// Need to resample the texture or to generate mips (if it's resampled, all mips must be regenerated).
	// Gather the used mip 0 colors to pick the closest colors from for resampling and for generating mips.
	std::array<uint32_t, 256 / 32> mip_0_opaque_colors_used{};
	size_t const in_mip_0_pixel_count = size_t(in_width) * size_t(in_height);
	for (size_t pixel_number = 0; pixel_number < in_mip_0_pixel_count; ++pixel_number) {
		uint8_t const color_number = in_pixels[pixel_number];
		mip_0_opaque_colors_used[color_number >> 5] |= UINT32_C(1) << (color_number & 31);
	}
	// Convert the used colors from gamma to linear.
	std::array<vector4, 256> linear_palette{};
	if (is_transparent) {
		mip_0_opaque_colors_used[255 >> 5] &= ~(UINT32_C(1) << (255 & 31));
		// Initialize the transparent color to black instead of blue for better filtering, and set its alpha to zero.
		std::fill(linear_palette[255].v, linear_palette[255].v + 4, 0.0f);
	}
	// Initialize the fallback color for safety if mip_0_opaque_colors_used is empty (fully transparent texture).
	std::fill(linear_palette[0].v, linear_palette[0].v + 3, 0.0f);
	linear_palette[0].v[3] = 1.0f;
	for (size_t colors_word_number = 0; colors_word_number < mip_0_opaque_colors_used.size(); ++colors_word_number) {
		size_t const color_word_first = size_t(32) * colors_word_number;
		uint32_t colors_word_remaining = mip_0_opaque_colors_used[colors_word_number];
		while (colors_word_remaining) {
			uint32_t const color_word_bit_number(bit_scan_forward(colors_word_remaining));
			colors_word_remaining &= ~(UINT32_C(1) << color_word_bit_number);
			size_t const color_number = color_word_first + color_word_bit_number;
			vector4 & linear_color = linear_palette[color_number];
			if (3 * color_number + 2 < palette.size()) {
				for (size_t component = 0; component < 3; ++component) {
					linear_color.v[component] = std::pow(float(palette[3 * color_number + component]) / 255.0f, 2.2f);
				}
			} else {
				std::fill(linear_color.v, linear_color.v + 3, 0.0f);
			}
			linear_color.v[3] = 1.0f;
		}
	}

	// Like in qlumpy GrabMip.
	constexpr float max_transparent_coverage = 0.4f;

	if (out_width == in_width && out_height == in_height) {
		// Copy the mips that don't need to be generated.
		if (out_pixels != in_pixels) {
			uint32_t const copy_mip_levels = std::min(out_mip_levels_without_base, in_mip_levels_without_base);
			size_t copy_count = 0;
			for (uint32_t mip_level = 0; mip_level <= copy_mip_levels; ++mip_level) {
				uint32_t const mip_width = out_width >> mip_level;
				uint32_t const mip_height = out_height >> mip_level;
				if (!mip_width || !mip_height) {
					break;
				}
				copy_count += size_t(mip_width) * size_t(mip_height);
			}
			std::memcpy(out_pixels, in_pixels, copy_count);
		}
	} else {
		// Need to resample, and then to regenerate the mips.
		assert(out_pixels != in_pixels);

		in_mip_levels_without_base = 0;

		float const out_to_in_x = float(in_width) / float(out_width);
		float const out_to_in_y = float(in_height) / float(out_height);

		vector3 diffused_error;
		std::fill(diffused_error.v, diffused_error.v + 3, 0.0f);

		for (uint32_t out_y = 0; out_y < out_height; ++out_y) {
			uint8_t * const out_row = out_pixels + size_t(out_width) * out_y;
			float const in_y = (float(out_y) + 0.5f) * out_to_in_y - 0.5f;
			uint32_t const in_y_b = uint32_t(std::min(float(in_height - 1), std::max(0.0f, in_y)));
			float const in_y_t = in_y - float(in_y_b);
			for (uint32_t out_x = 0; out_x < out_width; ++out_x) {
				float const in_x = (float(out_x) + 0.5f) * out_to_in_x - 0.5f;
				uint32_t const in_x_b = uint32_t(std::min(float(in_width - 1), std::max(0.0f, in_x)));
				float const in_x_t = in_x - float(in_x_b);

				std::array<vector4, 4> samples_y_filtered;
				for (uint32_t y_sample_number = 0; y_sample_number < 4; ++y_sample_number) {
					std::array<vector4, 4> samples_x;
					uint8_t const * const in_sample_row =
							in_pixels + in_width *
							std::min(
									size_t(in_height - 1),
									size_t(std::max(
											int32_t(0),
											int32_t(in_y_b) + (int32_t(y_sample_number) - int32_t(1)))));
					for (uint32_t x_sample_number = 0; x_sample_number < 4; ++x_sample_number) {
						samples_x[x_sample_number] = linear_palette[in_sample_row[
								std::min(
									size_t(in_width - 1),
									size_t(std::max(
											int32_t(0),
											int32_t(in_x_b) + (int32_t(x_sample_number) - int32_t(1)))))]];
					}
					for (size_t component = 0; component < 4; ++component) {
						samples_y_filtered[y_sample_number].v[component] = cubic_hermite(
								samples_x[0].v[component],
								samples_x[1].v[component],
								samples_x[2].v[component],
								samples_x[3].v[component],
								in_x_t);
					}
				}
				vector4 pixel_linear;
				for (size_t component = 0; component < 4; ++component) {
					pixel_linear.v[component] = std::min(1.0f, std::max(0.0f, cubic_hermite(
							samples_y_filtered[0].v[component],
							samples_y_filtered[1].v[component],
							samples_y_filtered[2].v[component],
							samples_y_filtered[3].v[component],
							in_y_t)));
				}

				vector3 pixel_with_diffused_error;
				for (size_t component = 0; component < 3; ++component) {
					pixel_with_diffused_error.v[component] = pixel_linear.v[component];
				}
				uint8_t out_pixel;
				if (pixel_linear.v[3] <= max_transparent_coverage) {
					out_pixel = 255;
				} else {
					out_pixel = 0;
					float best_distortion = float(INFINITY);
					for (size_t colors_word_number = 0;
							colors_word_number < mip_0_opaque_colors_used.size();
							++colors_word_number) {
						size_t const color_word_first = size_t(32) * colors_word_number;
						uint32_t colors_word_remaining = mip_0_opaque_colors_used[colors_word_number];
						while (colors_word_remaining) {
							uint32_t const color_word_bit_number(bit_scan_forward(colors_word_remaining));
							colors_word_remaining &= ~(UINT32_C(1) << color_word_bit_number);
							size_t const color_number = color_word_first + color_word_bit_number;
							vector4 const & linear_palette_color = linear_palette[color_number];
							float distortion = 0.0f;
							for (size_t component = 0; component < 3; ++component) {
								float const component_distortion =
										pixel_with_diffused_error.v[component] - linear_palette_color.v[component];
								distortion += component_distortion * component_distortion;
							}
							if (distortion < best_distortion) {
								best_distortion = distortion;
								out_pixel = uint8_t(color_number);
							}
						}
					}
				}
				// Error diffusion regardless of whether the pixel is transparent (black),
				// so the error doesn't jump over transparent areas.
				// Also because transparent samples are still included in bicubic filtering, unlike in mip generation.
				vector4 const & best_linear_color = linear_palette[out_pixel];
				for (size_t component = 0; component < 3; ++component) {
					diffused_error.v[component] =
							pixel_with_diffused_error.v[component] - best_linear_color.v[component];
				}

				out_row[out_x] = out_pixel;
			}
		}
	}

	// Generate the needed mips.
	size_t mip_offset = 0;
	for (uint32_t mip_level = 0; mip_level <= out_mip_levels_without_base; ++mip_level) {
		uint32_t const mip_width = out_width >> mip_level;
		uint32_t const mip_height = out_height >> mip_level;
		if (!mip_width || !mip_height) {
			break;
		}
		if (mip_level > in_mip_levels_without_base) {
			uint32_t const mip_step = UINT32_C(1) << mip_level;
			vector3 diffused_error;
			std::fill(diffused_error.v, diffused_error.v + 3, 0.0f);
			for (uint32_t y = 0; y < out_height; y += mip_step) {
				uint8_t * const out_row = out_pixels + mip_offset + size_t(mip_width) * (y >> mip_level);
				uint32_t const sample_y_end = y + std::min(mip_step, out_height - y);
				for (uint32_t x = 0; x < out_width; x += mip_step) {
					vector3 sample_sum;
					std::fill(sample_sum.v, sample_sum.v + 3, 0.0f);
					uint32_t sample_count = 0;
					uint32_t const sample_x_end = x + std::min(mip_step, out_width - x);
					for (uint32_t sample_y = y; sample_y < sample_y_end; ++sample_y) {
						uint8_t const * const sample_row = out_pixels + size_t(out_width) * sample_y;
						for (uint32_t sample_x = x; sample_x < sample_x_end; ++sample_x) {
							uint8_t const sample_color_number = sample_row[sample_x];
							if (is_transparent && sample_color_number == 255) {
								continue;
							}
							vector4 const & sample_linear_color = linear_palette[sample_color_number];
							for (size_t component = 0; component < 3; ++component) {
								sample_sum.v[component] += sample_linear_color.v[component];
							}
							++sample_count;
						}
					}
					uint8_t mip_pixel;
					if (sample_count <= uint32_t(float(mip_step * mip_step) * max_transparent_coverage)) {
						mip_pixel = 255;
					} else {
						mip_pixel = 0;
						vector3 pixel_with_diffused_error;
						for (size_t component = 0; component < 3; ++component) {
							pixel_with_diffused_error.v[component] =
									sample_sum.v[component] / float(sample_count) + diffused_error.v[component];
						}
						float best_distortion = float(INFINITY);
						for (size_t colors_word_number = 0;
								colors_word_number < mip_0_opaque_colors_used.size();
								++colors_word_number) {
							size_t const color_word_first = size_t(32) * colors_word_number;
							uint32_t colors_word_remaining = mip_0_opaque_colors_used[colors_word_number];
							while (colors_word_remaining) {
								uint32_t const color_word_bit_number(bit_scan_forward(colors_word_remaining));
								colors_word_remaining &= ~(UINT32_C(1) << color_word_bit_number);
								size_t const color_number = color_word_first + color_word_bit_number;
								vector4 const & linear_palette_color = linear_palette[color_number];
								float distortion = 0.0f;
								for (size_t component = 0; component < 3; ++component) {
									float const component_distortion =
											pixel_with_diffused_error.v[component] - linear_palette_color.v[component];
									distortion += component_distortion * component_distortion;
								}
								if (distortion < best_distortion) {
									best_distortion = distortion;
									mip_pixel = uint8_t(color_number);
								}
							}
						}
						vector4 const & best_linear_color = linear_palette[mip_pixel];
						for (size_t component = 0; component < 3; ++component) {
							diffused_error.v[component] =
									pixel_with_diffused_error.v[component] - best_linear_color.v[component];
						}
					}
					out_row[x >> mip_level] = mip_pixel;
				}
			}
		}
		mip_offset += size_t(mip_width) * size_t(mip_height);
	}
}

void id_texture_deserialized::pixels_and_palette_from_gbx(
		gbx_texture_deserialized const & gbx,
		std::optional<std::shared_ptr<id_texture_deserialized_palette>> const override_palette,
		id_texture_deserialized_palette const & quake_palette) {
	if (override_palette) {
		palette = *override_palette;
	} else {
		if (gbx.palette_id_indexed) {
			if (!palette) {
				palette = std::make_shared<id_texture_deserialized_palette>();
			}
			id_palette_from_gbx(gbx_texture_palette_type(gbx.name.c_str()), *palette, *gbx.palette_id_indexed);
		} else {
			// Use the Quake palette.
			palette.reset();
		}
	}
	width = gbx.width;
	height = gbx.height;
	pixels = std::make_shared<texture_deserialized_pixels>(
			texture_pixel_count_with_mips(width, height, id_texture_mip_levels));
	// Mips are incorrect for random-tiled ('-'-prefixed) textures in the original PS2 maps.
	convert_texture_pixels(
			gbx.name.c_str()[0] == '{', palette ? *palette : quake_palette,
			pixels->data(), width, height, id_texture_mip_levels - 1,
			gbx.pixels->data(), gbx.scaled_width, gbx.scaled_height, gbx.name.c_str()[0] == '-' ? 0 : gbx.mip_levels);
}

void id_texture_deserialized::pixels_and_palette_from_wads_or_gbx(
		struct gbx_texture_deserialized const & gbx,
		wad_textures_deserialized const * const * const wads,
		size_t const wad_count,
		bool const include_all_textures,
		palette_set const & quake_palette) {
	texture_identical_status wad_texture_identical_status;
	size_t wad_texture_wad_number;
	bool wad_texture_inclusion_required;
	wad_texture_deserialized const * wad_texture = find_most_identical_texture_in_wads(
			gbx, nullptr, wads, wad_count, quake_palette,
			&wad_texture_identical_status, &wad_texture_wad_number, &wad_texture_inclusion_required);
	if (wad_texture) {
		if (wad_texture_identical_status == texture_identical_status::same_palette_same_or_resampled_pixels) {
			if (wad_texture_inclusion_required || include_all_textures) {
				pixels = wad_texture->texture_id.pixels;
				palette = wad_texture->texture_id.palette;
			}
		} else {
			assert(wad_texture_identical_status ==
					texture_identical_status::same_palette_different_pixels);
			// Use the 24-bit palette from the WAD.
			pixels_and_palette_from_gbx(gbx, wad_texture->texture_id.palette, quake_palette.id);
		}
		// Even if the pixels are included, or only the palette is reused, still mark the WAD as used so the WAD, for
		// instance, isn't removed from the list if that's the case for all textures there, and the pixels and the
		// palette are located again in case of a round trip Gearbox > id (with included textures) > Gearbox > id
		// conversion.
		wad_number = wad_texture_wad_number;
	} else {
		pixels_and_palette_from_gbx(gbx, std::nullopt, quake_palette.id);
	}
}

void gbx_texture_deserialized::pixels_and_palette_from_id(
		id_texture_deserialized const & id,
		id_texture_deserialized_palette const & quake_palette) {
	assert(!id.empty());
	width = id.width;
	height = id.height;
	scaled_width = gbx_texture_scaled_size(width);
	scaled_height = gbx_texture_scaled_size(height);
	mip_levels = gbx_texture_mip_levels_without_base(scaled_width, scaled_height);
	if (id.pixels) {
		if (id.palette) {
			if (!palette_id_indexed) {
				palette_id_indexed = std::make_shared<gbx_texture_deserialized_palette>();
			}
			gbx_palette_from_id(gbx_texture_palette_type(name.c_str()), *palette_id_indexed, *id.palette);
		} else {
			// Use the Quake palette.
			palette_id_indexed.reset();
		}
		pixels = std::make_shared<texture_deserialized_pixels>(
				texture_pixel_count_with_mips(scaled_width, scaled_height, 1 + mip_levels));
		convert_texture_pixels(
				name.c_str()[0] == '{', id.palette ? *id.palette : quake_palette,
				pixels->data(), scaled_width, scaled_height, mip_levels,
				id.pixels->data(), id.width, id.height, id_texture_mip_levels - 1);
	} else {
		// Texture not found on the map and in any WAD, write a checkerboard during serialization.
		remove_pixels();
	}
}

void gbx_texture_deserialized::pixels_and_palette_from_wad(
		wad_texture_deserialized & wad_texture,
		id_texture_deserialized_palette const & quake_palette) {
	width = wad_texture.texture_id.width;
	height = wad_texture.texture_id.height;
	scaled_width = gbx_texture_scaled_size(width);
	scaled_height = gbx_texture_scaled_size(height);
	mip_levels = gbx_texture_mip_levels_without_base(scaled_width, scaled_height);
	gbx_palette_type const palette_type = gbx_texture_palette_type(name.c_str());
	// Try reusing the conversion from a previously converted map if exists.
	if (wad_texture.texture_id.palette) {
		std::shared_ptr<gbx_texture_deserialized_palette> & palette_gbx_ref =
				wad_texture.palettes_id_indexed_gbx[palette_type];
		if (!palette_gbx_ref) {
			palette_gbx_ref = std::make_shared<gbx_texture_deserialized_palette>();
			gbx_palette_from_id(palette_type, *palette_gbx_ref, *wad_texture.texture_id.palette);
		}
		palette_id_indexed = palette_gbx_ref;
	} else {
		// Use the Quake palette.
		palette_id_indexed.reset();
	}
	std::shared_ptr<texture_deserialized_pixels> & pixels_gbx_ref =
			name.c_str()[0] == '-'
					? wad_texture.default_scaled_size_pixels_random_gbx
					: wad_texture.default_scaled_size_pixels_gbx;
	if (!pixels_gbx_ref) {
		pixels_gbx_ref = std::make_shared<texture_deserialized_pixels>(
				texture_pixel_count_with_mips(scaled_width, scaled_height, 1 + mip_levels));
		convert_texture_pixels(
				name.c_str()[0] == '{',
				wad_texture.texture_id.palette ? *wad_texture.texture_id.palette : quake_palette,
				pixels_gbx_ref->data(), scaled_width, scaled_height, mip_levels,
				wad_texture.texture_id.pixels->data(), wad_texture.texture_id.width, wad_texture.texture_id.height,
				id_texture_mip_levels - 1);
	}
	pixels = pixels_gbx_ref;
}

void reconstruct_random_texture_sequences(
		id_map & map,
		gbx_texture_deserialized const * const textures_gbx,
		size_t const textures_gbx_count,
		wad_textures_deserialized const * const * const wads,
		size_t const wad_count,
		bool const include_all_textures,
		palette_set const & quake_palette) {
	// Gearbox maps have two kinds textures that were random-tiled, but no random tiling functionality:
	// - The ones that have the - prefix, very rare as their rendering is broken (interleaved lines, inverted colors).
	//   For them, the full animation sequence, including the links themselves, is stored in the map.
	//   BS2PC converts them as is, without searching for the textures in the WADs.
	// - Originally random-tiled textures, but with the - prefix removed (but the frame number kept).
	//   These are the common kind of textures in the original maps.
	//   BS2PC handles them two ways:
	//   - If the map already has a sequence of textures with the same name, but with the - prefix, name collision must
	//     be prevented. The texture will be upgraded to random-tiled if the pixels match between the two textures
	//     existing in the map.
	//   - Otherwise, an attempt to load the sequence from the WADs will be made. The texture will be upgraded to
	//     random-tiled if the Gearbox texture is identical to the WAD texture for the specific frame to be upgraded.

	struct sequence {
		bool from_wad;
		// Whether the sequence already existed in the map or has had any textures upgraded to random-tiled.
		bool used;
		std::array<id_texture_deserialized const *, 10> frames{};
		std::array<size_t, 10> frame_wad_numbers;
		// For textures that have already been inserted or replaced, texture numbers for each of the frames in the map.
		std::array<size_t, 10> frame_texture_numbers;
		explicit sequence(bool const is_from_wad) : from_wad(is_from_wad), used(!is_from_wad) {
			std::fill(frame_texture_numbers.begin(), frame_texture_numbers.end(), SIZE_MAX);
			std::fill(frame_wad_numbers.begin(), frame_wad_numbers.end(), SIZE_MAX);
		}
	};
	// The key is string_to_lower(name.substr(size_t(name.c_str()[0] == '-') + 1)).
	// [0] is the primary sequence (-0...), [1] is the alternate sequence (-a...).
	std::array<std::map<std::string, sequence>, 2> sequences;

	// Gather sequences already present in the map with the - prefix.
	for (size_t texture_number = 0; texture_number < map.textures.size(); ++texture_number) {
		id_texture_deserialized const & texture = map.textures[texture_number];
		if (texture.empty()) {
			continue;
		}
		if (texture.name.c_str()[0] != '-') {
			continue;
		}
		uint32_t const frame_number = texture_anim_frame(texture.name.c_str()[1]);
		if (frame_number == UINT32_MAX) {
			continue;
		}
		// Find or create a sequence.
		sequence & texture_sequence =
				sequences[frame_number / 10].emplace(
						std::piecewise_construct,
						std::forward_as_tuple(string_to_lower(texture.name.substr(2))),
						std::forward_as_tuple(false))
						.first->second;
		id_texture_deserialized const * pixels_texture = texture.pixels ? &texture : nullptr;
		size_t pixels_texture_wad_number = texture.wad_number;
		if (!pixels_texture) {
			// Try to locate the pixels in the WAD.
			std::string const wad_texture_key = string_to_lower(texture.name);
			for (size_t wad_number = 0; wad_number < wad_count; ++wad_number) {
				wad_textures_deserialized const & wad = *wads[wad_number];
				auto const wad_texture_number_iterator = wad.texture_number_map.find(wad_texture_key);
				if (wad_texture_number_iterator != wad.texture_number_map.cend()) {
					pixels_texture = &wad.textures[wad_texture_number_iterator->second].texture_id;
					pixels_texture_wad_number = wad_number;
					break;
				}
			}
		}
		// Even if failed to load the pixels, still reserve the sequence name as already present in the map to avoid
		// adding WAD sequences with colliding names.
		uint32_t const sequence_frame_number = frame_number % 10;
		texture_sequence.frames[sequence_frame_number] = pixels_texture;
		texture_sequence.frame_wad_numbers[sequence_frame_number] = pixels_texture_wad_number;
		texture_sequence.frame_texture_numbers[sequence_frame_number] = texture_number;
	}

	std::vector<std::pair<size_t, size_t>> texture_sorted_remaps;

	// Obtain the potentially needed sequences for textures with the - prefix removed from the WADs.
	// Starting from the first frame with no gaps, as the engine crashes if there are missing frames.
	// Then try remapping the frame to the random-tiled texture.
	for (size_t texture_number = 0; texture_number < map.textures.size(); ++texture_number) {
		id_texture_deserialized & texture = map.textures[texture_number];
		if (texture.empty()) {
			continue;
		}
		if (texture.name.size() >= texture_name_max_length) {
			// Can't add the - prefix.
			continue;
		}
		uint32_t const frame_number = texture_anim_frame(texture.name.c_str()[0]);
		if (frame_number == UINT32_MAX) {
			continue;
		}

		std::map<std::string, sequence> & set_sequences = sequences[frame_number / 10];
		std::string frame_key = '-' + string_to_lower(texture.name);
		std::string const sequence_key = frame_key.substr(2);
		// Check if the sequence already exists for this name.
		// Not checking if the textures are ambiguous between multiple WADs though, an very unlikely case that would
		// hugely overcomplicate the logic that will also become different for the frames used in the Gearbox map (would
		// need to locate them all in the Gearbox map), and for the additional frames found in the WADs (requiring
		// choosing the specific WAD to include the data from, possibly the same WAD as where some other frame identical
		// to one actually used in the Gearbox map was found, though it's not even necessary that it will be present in
		// the same WAD at all).
		auto sequence_iterator = set_sequences.find(sequence_key);
		if (sequence_iterator == set_sequences.end()) {
			// Try finding the first frame in the WADs.
			frame_key[1] = ((frame_number >= 10) ? 'a' : '0');
			id_texture_deserialized const * wad_frame_0 = nullptr;
			size_t wad_frame_0_wad_number = SIZE_MAX;
			for (size_t wad_number = 0; wad_number < wad_count; ++wad_number) {
				wad_textures_deserialized const & wad = *wads[wad_number];
				auto const wad_texture_number_iterator = wad.texture_number_map.find(frame_key);
				if (wad_texture_number_iterator != wad.texture_number_map.cend()) {
					wad_frame_0 = &wad.textures[wad_texture_number_iterator->second].texture_id;
					wad_frame_0_wad_number = wad_number;
					break;
				}
			}
			if (wad_frame_0) {
				// The sequence exists in a WAD - create it and find other textures from it in the WAD.
				sequence_iterator = set_sequences.emplace(
						std::piecewise_construct,
						std::forward_as_tuple(sequence_key),
						std::forward_as_tuple(true)).first;
				sequence & new_sequence = sequence_iterator->second;
				new_sequence.frames[0] = wad_frame_0;
				new_sequence.frame_wad_numbers[0] = wad_frame_0_wad_number;
				for (size_t wad_frame_number = 1; wad_frame_number < 10; ++wad_frame_number) {
					id_texture_deserialized const * wad_frame = nullptr;
					size_t wad_frame_wad_number = SIZE_MAX;
					++frame_key[1];
					for (size_t wad_number = 0; wad_number < wad_count; ++wad_number) {
						wad_textures_deserialized const & wad = *wads[wad_number];
						auto const wad_texture_number_iterator = wad.texture_number_map.find(frame_key);
						if (wad_texture_number_iterator != wad.texture_number_map.cend()) {
							wad_frame = &wad.textures[wad_texture_number_iterator->second].texture_id;
							wad_frame_wad_number = wad_number;
							break;
						}
					}
					if (wad_frame) {
						new_sequence.frames[wad_frame_number] = wad_frame;
						new_sequence.frame_wad_numbers[wad_frame_number] = wad_frame_wad_number;
					} else {
						break;
					}
				}
			}
		}
		if (sequence_iterator == set_sequences.end()) {
			// No minus-prefixed sequence that remapping of this texture may be attempted for.
			continue;
		}

		// Check if this texture can be replaced with the random-tiled sequence entry, if it's identical to it.
		sequence & texture_sequence = sequence_iterator->second;
		uint32_t const sequence_frame_number = frame_number % 10;
		id_texture_deserialized const * frame_texture = texture_sequence.frames[sequence_frame_number];
		if (!frame_texture) {
			// No frame, or a frame from the map without the pixels in either the map or a WAD.
			continue;
		}
		// Find the original Gearbox texture with the current name.
		// Likely at the same index as in the id map, so try this first, otherwise search by name.
		gbx_texture_deserialized const * texture_gbx = nullptr;
		if (texture_number < textures_gbx_count &&
				!bs2pc_strcasecmp(textures_gbx[texture_number].name.c_str(), texture.name.c_str())) {
			texture_gbx = &textures_gbx[texture_number];
		} else {
			for (size_t texture_gbx_number = 0; texture_gbx_number < textures_gbx_count; ++texture_gbx_number) {
				if (!bs2pc_strcasecmp(textures_gbx[texture_gbx_number].name.c_str(), texture.name.c_str())) {
					texture_gbx = &textures_gbx[texture_gbx_number];
					break;
				}
			}
		}
		if (!texture_gbx) {
			// No Gearbox texture found for the id texture to attempt upgrading.
			// Can't check if the potential random-tiled replacement is identical.
			continue;
		}
		if (is_texture_data_identical(*frame_texture, *texture_gbx, quake_palette) !=
				texture_identical_status::same_palette_same_or_resampled_pixels) {
			// The Gearbox texture is different than the potential replacement.
			// Keep the Gearbox texture.
			continue;
		}
		// Safety check even though normally the id texture in the map should be either converted or loaded from a WAD.
		if (texture.pixels &&
				is_texture_data_identical(texture, *texture_gbx, quake_palette) !=
						texture_identical_status::same_palette_same_or_resampled_pixels) {
			continue;
		}
		texture_sequence.used = true;
		size_t & frame_texture_number_ref = texture_sequence.frame_texture_numbers[sequence_frame_number];
		if (frame_texture_number_ref == SIZE_MAX) {
			// Replace the id texture in the map with the random-tiled one.
			frame_texture_number_ref = texture_number;
			// Both the name and optionally the pixels (which, before the replacement of 0name with -0name, might have
			// not been found in any WAD and thus potentially resampled from the PS2, but now the original PC texture
			// from the WAD has been found).
			texture = *frame_texture;
			if (!include_all_textures) {
				// The texture was loaded from the WAD, therefore the engine will load it from there too.
				texture.remove_pixels();
			}
			// Even if the pixels are included, still mark the WAD as used so the WAD, for instance, isn't removed from
			// the list if that's the case for all textures there, and the identical texture is located again in case of
			// a round trip Gearbox > id (with included textures) > Gearbox > id conversion.
			texture.wad_number = texture_sequence.frame_wad_numbers[sequence_frame_number];
		} else {
			// The replacement already exists in the map, remap to it.
			texture_sorted_remaps.emplace_back(texture_number, frame_texture_number_ref);
		}
	}

	// Perform the remaps and remove the textures that remapping was has been done from.
	if (!texture_sorted_remaps.empty()) {
		std::vector<size_t> texture_new_numbers;
		texture_new_numbers.reserve(map.textures.size());
		size_t texture_new_count = 0;
		for (std::pair<size_t, size_t> const & remap : texture_sorted_remaps) {
			for (id_texinfo & face_texinfo : map.texinfo) {
				if (face_texinfo.texture_number == remap.first) {
					face_texinfo.texture_number = uint32_t(remap.second);
				}
			}
			while (texture_new_numbers.size() < remap.first) {
				texture_new_numbers.push_back(texture_new_count++);
			}
			texture_new_numbers.push_back(SIZE_MAX);
		}
		while (texture_new_numbers.size() < map.textures.size()) {
			texture_new_numbers.push_back(texture_new_count++);
		}
		for (size_t texture_old_number = 0; texture_old_number < map.textures.size(); ++texture_old_number) {
			size_t const texture_new_number = texture_new_numbers[texture_old_number];
			if (texture_new_number != SIZE_MAX && texture_new_number != texture_old_number) {
				map.textures[texture_new_number] = std::move(map.textures[texture_old_number]);
			}
		}
		map.textures.resize(texture_new_count);
		for (id_texinfo & face_texinfo : map.texinfo) {
			face_texinfo.texture_number = uint32_t(texture_new_numbers[face_texinfo.texture_number]);
		}
	}

	// Add all the new frames from the sequences.
	for (std::map<std::string, sequence> & set_sequences : sequences) {
		for (std::pair<std::string const, sequence> & set_sequence_pair : set_sequences) {
			sequence & set_sequence = set_sequence_pair.second;
			if (!set_sequence.from_wad || !set_sequence.used) {
				// Already in the map entirely, or not needed.
				continue;
			}
			for (uint32_t sequence_frame_number = 0; sequence_frame_number < 10; ++sequence_frame_number) {
				id_texture_deserialized const * sequence_frame = set_sequence.frames[sequence_frame_number];
				if (!sequence_frame) {
					// End of the sequence.
					break;
				}
				size_t & frame_texture_number_ref = set_sequence.frame_texture_numbers[sequence_frame_number];
				if (frame_texture_number_ref != SIZE_MAX) {
					// Already added.
					continue;
				}
				frame_texture_number_ref = map.textures.size();
				map.textures.push_back(*sequence_frame);
				id_texture_deserialized & map_frame_texture = map.textures.back();
				if (!include_all_textures) {
					// The texture was loaded from the WAD, therefore the engine will load it from there too.
					map_frame_texture.remove_pixels();
				}
				// Even if the pixels are included, still mark the WAD as used so the WAD, for instance, isn't removed
				// from the list if that's the case for all textures there, and the identical texture is located again
				// in case of a round trip Gearbox > id (with included textures) > Gearbox > id conversion.
				map_frame_texture.wad_number = set_sequence.frame_wad_numbers[sequence_frame_number];
			}
		}
	}
}

void write_wadg(
		std::ofstream & output_stream,
		std::map<std::string, gbx_texture_deserialized> & textures,
		palette_set const & quake_palette) {
	size_t wadg_info_table_offset = sizeof(wad_info);
	for (std::pair<std::string const, gbx_texture_deserialized> const & texture_pair : textures) {
		gbx_texture_deserialized const & texture = texture_pair.second;
		wadg_info_table_offset += sizeof(gbx_texture) + texture.pixels->size() + texture.palette_id_indexed->size();
	}
	wad_info wadg_info;
	wadg_info.identification[0] = 'W';
	wadg_info.identification[1] = 'A';
	wadg_info.identification[2] = 'D';
	// BS2PC-specific type containing Gearbox textures, not a PC WAD3.
	// Also lumps are not 4-aligned for simplicity.
	wadg_info.identification[3] = 'G';
	wadg_info.lump_count = uint32_t(textures.size());
	wadg_info.info_table_offset = uint32_t(wadg_info_table_offset);
	output_stream.write(reinterpret_cast<char const *>(&wadg_info), std::streamsize(sizeof(wadg_info)));
	// Write the textures.
	gbx_texture wadg_texture_serialized;
	wadg_texture_serialized.pixels = sizeof(gbx_texture);
	std::memset(&wadg_texture_serialized.unknown_0, 0, sizeof(wadg_texture_serialized.unknown_0));
	std::memset(&wadg_texture_serialized.unknown_1, 0, sizeof(wadg_texture_serialized.unknown_1));
	wadg_texture_serialized.anim_total = 0;
	wadg_texture_serialized.anim_min = 0;
	wadg_texture_serialized.anim_max = 0;
	wadg_texture_serialized.anim_next = UINT32_MAX;
	wadg_texture_serialized.alternate_anims = UINT32_MAX;
	for (std::pair<std::string const, gbx_texture_deserialized> const & texture_pair : textures) {
		gbx_texture_deserialized const & texture = texture_pair.second;
		wadg_texture_serialized.palette = uint32_t(sizeof(gbx_texture) + texture.pixels->size());
		wadg_texture_serialized.width = texture.width;
		wadg_texture_serialized.height = texture.height;
		wadg_texture_serialized.scaled_width = texture.scaled_width;
		wadg_texture_serialized.scaled_height = texture.scaled_height;
		size_t const texture_name_length = std::min(size_t(texture_name_max_length), texture.name.size());
		std::memcpy(
				wadg_texture_serialized.name,
				texture.name.c_str(),
				texture_name_length);
		std::memset(
				wadg_texture_serialized.name + texture_name_length,
				0,
				texture_name_max_length + 1 - texture_name_length);
		wadg_texture_serialized.mip_levels = texture.mip_levels;
		output_stream.write(
				reinterpret_cast<char const *>(&wadg_texture_serialized),
				std::streamsize(sizeof(wadg_texture_serialized)));
		// For simplicity, to avoid working with mip levels, not interleaving random-tiled textures back.
		output_stream.write(
				reinterpret_cast<char const *>(texture.pixels->data()),
				texture.pixels->size());
		gbx_texture_deserialized_palette const & texture_palette_id_indexed =
				texture.palette_id_indexed
						? *texture.palette_id_indexed
						: quake_palette.gbx_id_indexed[gbx_texture_palette_type(texture.name.c_str())];
		// Reordering of colors is done with the granularity of 8 colors.
		for (size_t texture_color_number = 0; texture_color_number < 256; texture_color_number += 8) {
			output_stream.write(
					reinterpret_cast<char const *>(texture_palette_id_indexed.data()) +
							4 * size_t(convert_palette_color_number(uint8_t(texture_color_number))),
					4 * 8);
		}
	}
	// Write the lump information.
	size_t wadg_lump_offset = sizeof(wadg_info);
	wad_lump_info wadg_lump_info;
	wadg_lump_info.type = wad_lump_type_texture;
	wadg_lump_info.compression = wad_lump_compression_none;
	wadg_lump_info.padding = 0;
	for (std::pair<std::string const, gbx_texture_deserialized> const & texture_pair : textures) {
		gbx_texture_deserialized const & texture = texture_pair.second;
		wadg_lump_info.file_position = uint32_t(wadg_lump_offset);
		size_t const texture_lump_size =
				sizeof(gbx_texture) + texture.pixels->size() + texture.palette_id_indexed->size();
		wadg_lump_info.disk_size = uint32_t(texture_lump_size);
		wadg_lump_info.size = uint32_t(texture_lump_size);
		size_t const texture_name_length = std::min(size_t(wad_lump_name_max_length), texture.name.size());
		std::memcpy(
				wadg_lump_info.name,
				texture.name.c_str(),
				texture_name_length);
		std::memset(
				wadg_lump_info.name + texture_name_length,
				0,
				wad_lump_name_max_length + 1 - texture_name_length);
		output_stream.write(reinterpret_cast<char const *>(&wadg_lump_info), std::streamsize(sizeof(wadg_lump_info)));
		wadg_lump_offset += texture_lump_size;
	}
}

uint8_t const quake_default_palette[3 * 256] = {
	0x00, 0x00, 0x00, 0x0F, 0x0F, 0x0F, 0x1F, 0x1F, 0x1F, 0x2F, 0x2F, 0x2F,
	0x3F, 0x3F, 0x3F, 0x4B, 0x4B, 0x4B, 0x5B, 0x5B, 0x5B, 0x6B, 0x6B, 0x6B,
	0x7B, 0x7B, 0x7B, 0x8B, 0x8B, 0x8B, 0x9B, 0x9B, 0x9B, 0xAB, 0xAB, 0xAB,
	0xBB, 0xBB, 0xBB, 0xCB, 0xCB, 0xCB, 0xDB, 0xDB, 0xDB, 0xEB, 0xEB, 0xEB,
	0x0F, 0x0B, 0x07, 0x17, 0x0F, 0x0B, 0x1F, 0x17, 0x0B, 0x27, 0x1B, 0x0F,
	0x2F, 0x23, 0x13, 0x37, 0x2B, 0x17, 0x3F, 0x2F, 0x17, 0x4B, 0x37, 0x1B,
	0x53, 0x3B, 0x1B, 0x5B, 0x43, 0x1F, 0x63, 0x4B, 0x1F, 0x6B, 0x53, 0x1F,
	0x73, 0x57, 0x1F, 0x7B, 0x5F, 0x23, 0x83, 0x67, 0x23, 0x8F, 0x6F, 0x23,
	0x0B, 0x0B, 0x0F, 0x13, 0x13, 0x1B, 0x1B, 0x1B, 0x27, 0x27, 0x27, 0x33,
	0x2F, 0x2F, 0x3F, 0x37, 0x37, 0x4B, 0x3F, 0x3F, 0x57, 0x47, 0x47, 0x67,
	0x4F, 0x4F, 0x73, 0x5B, 0x5B, 0x7F, 0x63, 0x63, 0x8B, 0x6B, 0x6B, 0x97,
	0x73, 0x73, 0xA3, 0x7B, 0x7B, 0xAF, 0x83, 0x83, 0xBB, 0x8B, 0x8B, 0xCB,
	0x00, 0x00, 0x00, 0x07, 0x07, 0x00, 0x0B, 0x0B, 0x00, 0x13, 0x13, 0x00,
	0x1B, 0x1B, 0x00, 0x23, 0x23, 0x00, 0x2B, 0x2B, 0x07, 0x2F, 0x2F, 0x07,
	0x37, 0x37, 0x07, 0x3F, 0x3F, 0x07, 0x47, 0x47, 0x07, 0x4B, 0x4B, 0x0B,
	0x53, 0x53, 0x0B, 0x5B, 0x5B, 0x0B, 0x63, 0x63, 0x0B, 0x6B, 0x6B, 0x0F,
	0x07, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x17, 0x00, 0x00, 0x1F, 0x00, 0x00,
	0x27, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x37, 0x00, 0x00, 0x3F, 0x00, 0x00,
	0x47, 0x00, 0x00, 0x4F, 0x00, 0x00, 0x57, 0x00, 0x00, 0x5F, 0x00, 0x00,
	0x67, 0x00, 0x00, 0x6F, 0x00, 0x00, 0x77, 0x00, 0x00, 0x7F, 0x00, 0x00,
	0x13, 0x13, 0x00, 0x1B, 0x1B, 0x00, 0x23, 0x23, 0x00, 0x2F, 0x2B, 0x00,
	0x37, 0x2F, 0x00, 0x43, 0x37, 0x00, 0x4B, 0x3B, 0x07, 0x57, 0x43, 0x07,
	0x5F, 0x47, 0x07, 0x6B, 0x4B, 0x0B, 0x77, 0x53, 0x0F, 0x83, 0x57, 0x13,
	0x8B, 0x5B, 0x13, 0x97, 0x5F, 0x1B, 0xA3, 0x63, 0x1F, 0xAF, 0x67, 0x23,
	0x23, 0x13, 0x07, 0x2F, 0x17, 0x0B, 0x3B, 0x1F, 0x0F, 0x4B, 0x23, 0x13,
	0x57, 0x2B, 0x17, 0x63, 0x2F, 0x1F, 0x73, 0x37, 0x23, 0x7F, 0x3B, 0x2B,
	0x8F, 0x43, 0x33, 0x9F, 0x4F, 0x33, 0xAF, 0x63, 0x2F, 0xBF, 0x77, 0x2F,
	0xCF, 0x8F, 0x2B, 0xDF, 0xAB, 0x27, 0xEF, 0xCB, 0x1F, 0xFF, 0xF3, 0x1B,
	0x0B, 0x07, 0x00, 0x1B, 0x13, 0x00, 0x2B, 0x23, 0x0F, 0x37, 0x2B, 0x13,
	0x47, 0x33, 0x1B, 0x53, 0x37, 0x23, 0x63, 0x3F, 0x2B, 0x6F, 0x47, 0x33,
	0x7F, 0x53, 0x3F, 0x8B, 0x5F, 0x47, 0x9B, 0x6B, 0x53, 0xA7, 0x7B, 0x5F,
	0xB7, 0x87, 0x6B, 0xC3, 0x93, 0x7B, 0xD3, 0xA3, 0x8B, 0xE3, 0xB3, 0x97,
	0xAB, 0x8B, 0xA3, 0x9F, 0x7F, 0x97, 0x93, 0x73, 0x87, 0x8B, 0x67, 0x7B,
	0x7F, 0x5B, 0x6F, 0x77, 0x53, 0x63, 0x6B, 0x4B, 0x57, 0x5F, 0x3F, 0x4B,
	0x57, 0x37, 0x43, 0x4B, 0x2F, 0x37, 0x43, 0x27, 0x2F, 0x37, 0x1F, 0x23,
	0x2B, 0x17, 0x1B, 0x23, 0x13, 0x13, 0x17, 0x0B, 0x0B, 0x0F, 0x07, 0x07,
	0xBB, 0x73, 0x9F, 0xAF, 0x6B, 0x8F, 0xA3, 0x5F, 0x83, 0x97, 0x57, 0x77,
	0x8B, 0x4F, 0x6B, 0x7F, 0x4B, 0x5F, 0x73, 0x43, 0x53, 0x6B, 0x3B, 0x4B,
	0x5F, 0x33, 0x3F, 0x53, 0x2B, 0x37, 0x47, 0x23, 0x2B, 0x3B, 0x1F, 0x23,
	0x2F, 0x17, 0x1B, 0x23, 0x13, 0x13, 0x17, 0x0B, 0x0B, 0x0F, 0x07, 0x07,
	0xDB, 0xC3, 0xBB, 0xCB, 0xB3, 0xA7, 0xBF, 0xA3, 0x9B, 0xAF, 0x97, 0x8B,
	0xA3, 0x87, 0x7B, 0x97, 0x7B, 0x6F, 0x87, 0x6F, 0x5F, 0x7B, 0x63, 0x53,
	0x6B, 0x57, 0x47, 0x5F, 0x4B, 0x3B, 0x53, 0x3F, 0x33, 0x43, 0x33, 0x27,
	0x37, 0x2B, 0x1F, 0x27, 0x1F, 0x17, 0x1B, 0x13, 0x0F, 0x0F, 0x0B, 0x07,
	0x6F, 0x83, 0x7B, 0x67, 0x7B, 0x6F, 0x5F, 0x73, 0x67, 0x57, 0x6B, 0x5F,
	0x4F, 0x63, 0x57, 0x47, 0x5B, 0x4F, 0x3F, 0x53, 0x47, 0x37, 0x4B, 0x3F,
	0x2F, 0x43, 0x37, 0x2B, 0x3B, 0x2F, 0x23, 0x33, 0x27, 0x1F, 0x2B, 0x1F,
	0x17, 0x23, 0x17, 0x0F, 0x1B, 0x13, 0x0B, 0x13, 0x0B, 0x07, 0x0B, 0x07,
	0xFF, 0xF3, 0x1B, 0xEF, 0xDF, 0x17, 0xDB, 0xCB, 0x13, 0xCB, 0xB7, 0x0F,
	0xBB, 0xA7, 0x0F, 0xAB, 0x97, 0x0B, 0x9B, 0x83, 0x07, 0x8B, 0x73, 0x07,
	0x7B, 0x63, 0x07, 0x6B, 0x53, 0x00, 0x5B, 0x47, 0x00, 0x4B, 0x37, 0x00,
	0x3B, 0x2B, 0x00, 0x2B, 0x1F, 0x00, 0x1B, 0x0F, 0x00, 0x0B, 0x07, 0x00,
	0x00, 0x00, 0xFF, 0x0B, 0x0B, 0xEF, 0x13, 0x13, 0xDF, 0x1B, 0x1B, 0xCF,
	0x23, 0x23, 0xBF, 0x2B, 0x2B, 0xAF, 0x2F, 0x2F, 0x9F, 0x2F, 0x2F, 0x8F,
	0x2F, 0x2F, 0x7F, 0x2F, 0x2F, 0x6F, 0x2F, 0x2F, 0x5F, 0x2B, 0x2B, 0x4F,
	0x23, 0x23, 0x3F, 0x1B, 0x1B, 0x2F, 0x13, 0x13, 0x1F, 0x0B, 0x0B, 0x0F,
	0x2B, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x4B, 0x07, 0x00, 0x5F, 0x07, 0x00,
	0x6F, 0x0F, 0x00, 0x7F, 0x17, 0x07, 0x93, 0x1F, 0x07, 0xA3, 0x27, 0x0B,
	0xB7, 0x33, 0x0F, 0xC3, 0x4B, 0x1B, 0xCF, 0x63, 0x2B, 0xDB, 0x7F, 0x3B,
	0xE3, 0x97, 0x4F, 0xE7, 0xAB, 0x5F, 0xEF, 0xBF, 0x77, 0xF7, 0xD3, 0x8B,
	0xA7, 0x7B, 0x3B, 0xB7, 0x9B, 0x37, 0xC7, 0xC3, 0x37, 0xE7, 0xE3, 0x57,
	0x7F, 0xBF, 0xFF, 0xAB, 0xE7, 0xFF, 0xD7, 0xFF, 0xFF, 0x67, 0x00, 0x00,
	0x8B, 0x00, 0x00, 0xB3, 0x00, 0x00, 0xD7, 0x00, 0x00, 0xFF, 0x00, 0x00,
	0xFF, 0xF3, 0x93, 0xFF, 0xF7, 0xC7, 0xFF, 0xFF, 0xFF, 0x9F, 0x5B, 0x53,
};

}
