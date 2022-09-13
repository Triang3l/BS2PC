#ifndef BS2PC_INCLUDED_BS2PCLIB_BS2PCLIB_HPP
#define BS2PC_INCLUDED_BS2PCLIB_BS2PCLIB_HPP

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// For implementations of functions like bs2pc_strcasecmp.
#if defined(_WIN32)
#include <string.h>
#else
#include <strings.h>
#endif

// For BitScanForward.
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace bs2pc {

// Non-standard function wrappers and additional string functions.

inline int bs2pc_strcasecmp(char const * const string_1, char const * const string_2) {
	#if defined(_WIN32)
	return _stricmp(string_1, string_2);
	#else
	return strcasecmp(string_1, string_2);
	#endif
}

inline int bs2pc_strncasecmp(char const * const string_1, char const * const string_2, size_t const character_count) {
	#if defined(_WIN32)
	return _strnicmp(string_1, string_2, character_count);
	#else
	return strncasecmp(string_1, string_2, character_count);
	#endif
}

inline std::string string_to_lower(std::string_view const view) {
	std::string lowercase(view);
	std::transform(lowercase.cbegin(), lowercase.cend(), lowercase.begin(), tolower);
	return lowercase;
}

inline int32_t bit_scan_forward(uint32_t const value) {
	#if defined(_MSC_VER)
	unsigned long bit_index;
	if (!_BitScanForward(&bit_index, static_cast<unsigned long>(value))) {
		return -1;
	}
	return int32_t(bit_index);
	#else
	return int32_t(__builtin_ffs(value) - 1);
	#endif
}

// Common types.

// Can be used for map file identification even for a compressed Gearbox map, because the first 4 bytes of one are the
// uncompressed size, which can't be smaller than 4 + 16 * gbx_lump_count, or 260 - larger than both the id and the
// Gearbox version numbers.
constexpr uint32_t id_map_version_quake = 29;
constexpr uint32_t id_map_version_valve = 30;
constexpr uint32_t gbx_map_version = 40;

struct vector3 {
	float v[3];

	vector3() = default;
	vector3(struct vector4 vec);
};
static_assert(sizeof(vector3) == 0xC);

struct vector4 {
	// [3] is usually 0.0f.
	float v[4];

	vector4() = default;
	vector4(vector3 vec) {
		v[0] = vec.v[0];
		v[1] = vec.v[1];
		v[2] = vec.v[2];
		v[3] = 0.0f;
	}
};
static_assert(sizeof(vector4) == 0x10);

inline vector3::vector3(vector4 vec) {
	v[0] = vec.v[0];
	v[1] = vec.v[1];
	v[2] = vec.v[2];
}

enum plane_type {
	plane_type_x,
	plane_type_y,
	plane_type_z,
	plane_type_any_x,
	plane_type_any_y,
	plane_type_any_z,
};

constexpr uint8_t plane_signbits(vector3 const normal) {
	return (normal.v[0] < 0.0f ? 0b001 : 0) | (normal.v[1] < 0.0f ? 0b010 : 0) | (normal.v[2] < 0.0f ? 0b100 : 0);
}

enum contents {
	// A node, not a leaf.
	contents_node = 0,

	contents_empty = -1,
	contents_solid = -2,
	contents_water = -3,
	contents_slime = -4,
	contents_lava = -5,
	contents_sky = -6,
	contents_origin = -7,
	contents_clip = -8,

	contents_current_0 = -9,
	contents_current_90 = -10,
	contents_current_180 = -11,
	contents_current_270 = -12,
	contents_current_up = -13,
	contents_current_down = -14,

	contents_translucent = -15,
};

constexpr uint32_t max_lightmaps = 4;

enum ambient {
	ambient_water,
	ambient_sky,
	ambient_slime,
	ambient_lava,

	ambient_count,
};

// Note that edges must be either for one surface or between two surfaces with the same contents, in two different
// directions, not reused by three or more surfaces.
// The software renderer stores two cached surface pointers for each edge.
struct edge {
	uint16_t vertexes[2];
};
static_assert(sizeof(edge) == 0x4);

using surfedge = int32_t;

struct clipnode {
	uint32_t plane_number;
	int16_t child_clipnodes_or_contents[2];
};
static_assert(sizeof(clipnode) == 0x8);

constexpr uint32_t max_hulls = 4;

// Textures.

// The name must be null-terminated (the engine uses C string functions without an explicit size limit), the buffer size
// is thus the max length plus 1.
constexpr uint32_t texture_name_max_length = 15;

// From various arrays in Quake, for integer overflow safety.
constexpr uint32_t texture_max_width_height = 1024;

constexpr uint32_t texture_width_height_alignment = 16;

constexpr uint32_t id_texture_mip_levels = 4;

inline size_t texture_pixel_count_with_mips(uint32_t const width, uint32_t const height, uint32_t const mip_count) {
	size_t pixel_count = 0;
	for (uint32_t mip_level = 0; mip_level < mip_count; ++mip_level) {
		uint32_t const mip_width = width >> mip_level;
		uint32_t const mip_height = height >> mip_level;
		if (!mip_width || !mip_height) {
			break;
		}
		pixel_count += size_t(mip_width) * size_t(mip_height);
	}
	return pixel_count;
}

constexpr uint32_t gbx_texture_scaled_size(uint32_t const size) {
	// Accurate for square textures, but non-square ones have strange cases like:
	// 48x64 -> 64x64, while 48x48 -> 32x32
	// 176x160 -> 128x256 (the longest dimension becomes different)
	// 240x112 -> 256x128, but 240x208 -> 128x128
	// so the exact formula is not known.
	if (size >= 192) {
		return 256;
	}
	if (size >= 96) {
		return 128;
	}
	// 48x48 becomes 32x32, unlike 96x96 and 192x192 which become smaller.
	if (size > 48) {
		return 64;
	}
	// Texture width and height must be 16-aligned, so between 16 and 32 there are no values.
	if (size > 16) {
		return 32;
	}
	return 16;
}

inline uint32_t gbx_texture_mip_levels_without_base(uint32_t const scaled_width, uint32_t const scaled_height) {
	uint32_t mip_levels = 0;
	uint32_t mip_width = scaled_width, mip_height = scaled_height;
	while (mip_width >= 8 && mip_height >= 8) {
		++mip_levels;
		mip_width >>= 1;
		mip_height >>= 1;
	}
	return mip_levels ? mip_levels - 1 : 0;
}

// The Quake palette (id1/gfx/palette.lmp), as R8, G8, B8.
extern uint8_t const quake_default_palette[3 * 256];

enum gbx_palette_type {
	// 7.7.7-bit.
	gbx_palette_type_opaque,
	// Random-tiled ('-'-prefixed) - 7.7.7-bit, but with the original 8-bit color having been XOR 0xFF first.
	gbx_palette_type_random,
	// Liquids ('!'-prefixed specifically, but not non-'!'-prefixed like water4b) - 8.8.8-bit.
	gbx_palette_type_liquid,
	// Transparent ('{'-prefixed) - 8.8.8-bit.
	gbx_palette_type_transparent,

	gbx_palette_type_count,
};

inline gbx_palette_type gbx_texture_palette_type(char const * const name) {
	switch (name[0]) {
		case '-':
			return gbx_palette_type_random;
		case '!':
			return gbx_palette_type_liquid;
		case '{':
			return gbx_palette_type_transparent;
		default:
			return gbx_palette_type_opaque;
	}
}

constexpr bool is_gbx_palette_24_bit(gbx_palette_type const palette_type) {
	return palette_type == gbx_palette_type_liquid || palette_type == gbx_palette_type_transparent;
}

constexpr uint8_t gbx_24_bit_color_from_id(uint8_t const color) {
	// Based on how the colors in the original PS2 textures are computed, by checking for which colors in the original
	// palette the color is different than the id texture color.
	// One texture, {grid1, has 2 ambiguities (0 to 1 while normally it stays 0, and 4 to 5 while normally it stays 4).
	return
			color -
			uint8_t(
					((color << (uint32_t(color < 0x40) + uint32_t(color < 0x80))) & UINT8_C(0b10001111)) ==
					UINT8_C(0b10000100));
}

constexpr uint8_t gbx_21_bit_color_from_id(uint8_t const color) {
	// Based on how the colors in the original PS2 textures are computed, by checking for which colors in the original
	// palette the color is different than (id_color >> 1).
	// There are some textures that have exceptions (smaller by 1, or sometimes larger by 1), but they collide with
	// other textures where the conversion for the same Valve's 24-bit color follows this formula, with the latter being
	// hundreds of times more common. Most common cases (with more than 2 instances) are 99 (normally and usually 49,
	// but sometimes larger than 99 >> 1, 50), 156 (normally 78, rarely 77), 161 (normally 80, rarely 79), 214 (normally
	// 107, rarely 106).
	return gbx_24_bit_color_from_id(color) >> 1;
}

constexpr uint8_t id_21_bit_color_from_gbx(uint8_t const color) {
	uint8_t const color_7_bit = std::min(uint8_t(UINT8_C(0x7F)), color);
	// So that 0x7F becomes 0xFF.
	// Also making sure gbx_21_bit_color_from_id(id_21_bit_color_from_gbx(color)) == color.
	uint8_t const color_8_bit = (color_7_bit << 1) | (color_7_bit >> 6);
	return color_8_bit + uint8_t(gbx_21_bit_color_from_id(color_8_bit) < color_7_bit);
}

// Converts between PC and PS2 color indices in the palette.
// PS2 color indexes stored in texture pixels match those on the PC, but the palette has colors reordered.
constexpr uint8_t convert_palette_color_number(uint8_t const index) {
	// 0-7, 16-23, 8-15, 24-31...
	return (index & UINT8_C(0b11100111)) | ((index & (UINT8_C(1) << 3)) << 1) | ((index & (UINT8_C(1) << 4)) >> 1);
}

// All Gearbox textures, both 24bpp and 21bpp, have alpha (the fourth palette byte) of 0x80 for every color,
// except for transparent ('{'-prefixed) textures, that have the alpha of 0 for the transparent color.
// The transparent color is also black (0, 0, 0) in Gearbox textures, not blue unlike on the PC.

// Random-tiled ('-'-prefixed) textures are stored in Gearbox maps with rows of the two vertical halves interleaved,
// lower half first, upper half second.
// Mips of those textures in the original PS2 maps are incorrect though, with the first mip only having the correct
// lower half when deinterleaved, and the upper half being almost the same as the lower, but with slightly different
// colors, and there's no other data in the texture beyond the mips that could contain the actual upper half.
// Overall, rendering of them in the PS2 engine is broken, they're displayed as is, with the interleaving and the
// inversion visible in the game.
constexpr uint32_t deinterleave_random_gbx_texture_y(uint32_t const y, uint32_t const mip_height) {
	return ((y & 1) ? 0 : (mip_height >> 1)) + (y >> 1);
}
constexpr uint32_t interleave_random_gbx_texture_y(uint32_t const y, uint32_t const mip_height) {
	uint32_t const mip_half_height = mip_height >> 1;
	bool const is_lower_half = y >= mip_half_height;
	return ((y - (is_lower_half ? mip_half_height : 0)) << 1) | uint32_t(is_lower_half ^ 1);
}

constexpr uint32_t texture_anim_frame(char const frame_character) {
	if (frame_character >= '0' && frame_character <= '9') {
		return uint32_t(frame_character - '0');
	}
	// Alternate animation set.
	if (frame_character >= 'A' && frame_character <= 'A' + 9) {
		return 10 + uint32_t(frame_character - 'A');
	}
	if (frame_character >= 'a' && frame_character <= 'a' + 9) {
		return 10 + uint32_t(frame_character - 'a');
	}
	return UINT32_MAX;
}

enum class texture_identical_status {
	// The list is ordered, higher values are more preferable than lower.

	// Both the palette and (if not resampled) pixels don't match.
	different,
	// Can use the 24-bit palette from the id texture instead of the 21-bit one from the Gearbox texture.
	// However, pixels are different, and need to be copied.
	same_palette_different_pixels,
	// Can use the 24-bit palette from the id texture, as well as the pixels from the better source.
	// Pixels are either the same, or weren't checked since an accurate comparison is impossible as the texture was
	// resampled.
	same_palette_same_or_resampled_pixels,
};

// Entities.

// Like COM_Parse from Quake, but returning the com_token and modifying the data pointer.
std::string parse_token(char const * & data);

// Keys are case-sensitive (ValueForKey uses Q_strcmp).
using entity_key_value_pair = std::pair<std::string, std::string>;
using entity_key_values = std::vector<entity_key_value_pair>;

std::vector<entity_key_values> deserialize_entities(char const * entities_string);
std::string serialize_entities(entity_key_values const * entities, size_t entity_count);

void convert_model_paths(entity_key_values * entities, size_t entity_count, uint32_t version_from, uint32_t version_to);

// id (disk structures) and Gearbox (memory structures stored on the disc) map file structures.

struct id_plane {
	vector3 normal;
	float distance;
	/* plane_type */ uint32_t type;

	id_plane() = default;
	id_plane(struct gbx_plane const & gbx);

	constexpr uint8_t signbits() const { return plane_signbits(normal); }
};
static_assert(sizeof(id_plane) == 0x14);

struct gbx_plane {
	vector3 normal;
	float distance;
	/* plane_type */ uint8_t type;
	uint8_t signbits;
	uint16_t padding;

	gbx_plane() = default;
	gbx_plane(struct id_plane const & id);
};
static_assert(sizeof(gbx_plane) == 0x14);

struct id_node {
	uint32_t plane_number;
	// Negative numbers are -(leafs + 1), not nodes.
	int16_t children[2];
	int16_t mins[3];
	int16_t maxs[3];
	uint16_t first_face;
	uint16_t face_count;

	id_node() = default;
	id_node(struct gbx_node const & gbx);
};
static_assert(sizeof(id_node) == 0x18);

struct gbx_node {
	// 0 (contents_node), to differentiate from leafs.
	/* contents */ int32_t leaf_contents;
	// In the map, byte offset inside the map file.
	// When deserialized in BS2PC, node number.
	// UINT32_MAX if no parent.
	uint32_t parent;
	// 0.
	uint32_t visibility_frame;
	// In the map, byte offset inside the map file.
	// When deserialized in BS2PC, plane number.
	uint32_t plane;
	vector4 mins;
	vector4 maxs;
	// In the map, byte offset (unsigned) inside the map file.
	// When deserialized in BS2PC, node number if non-negative, -(leaf number + 1) if negative.
	int32_t children[2];
	uint16_t first_face;
	uint16_t face_count;
	// 0.
	uint32_t unknown_0;

	gbx_node() = default;
	gbx_node(struct id_node const & id, uint32_t parent = UINT32_MAX);
};
static_assert(sizeof(gbx_node) == 0x40);

struct id_leaf {
	/* contents */ int32_t leaf_contents;
	// UINT32_MAX if no visibility info.
	uint32_t visibility_offset;
	int16_t mins[3];
	int16_t maxs[3];
	uint16_t first_marksurface;
	uint16_t marksurface_count;
	uint8_t ambient_level[ambient_count];

	id_leaf() = default;
	id_leaf(struct gbx_leaf const & gbx);
};
static_assert(sizeof(id_leaf) == 0x1C);

struct gbx_leaf {
	/* contents */ int32_t leaf_contents;
	// In the map, byte offset inside the map file.
	// When deserialized in BS2PC, node number.
	// UINT32_MAX if no parent.
	uint32_t parent;
	// 0.
	uint32_t visibility_frame;
	// 0.
	uint32_t unknown_0;
	vector4 mins;
	vector4 maxs;
	// In the map, byte offset inside the map file.
	// When deserialized in BS2PC, byte offset in the visibility.
	// UINT32_MAX if no visibility info.
	uint32_t visibility_offset;
	uint32_t first_marksurface;
	uint32_t marksurface_count;
	uint8_t ambient_level[ambient_count];

	gbx_leaf() = default;
	gbx_leaf(struct id_leaf const & id, uint32_t parent = UINT32_MAX);
};
static_assert(sizeof(gbx_leaf) == 0x40);

// Similar to qbsp2 GetVertex, but without hashing.
// The resulting vertex may be different (rounded, or selected from an existing one with a threshold).
template<typename vertex_type>
size_t add_vertex(std::vector<vertex_type> & vertexes, vector3 const vertex) {
	vector3 vertex_rounded;
	for (size_t axis = 0; axis < 3; ++axis) {
		float const component = vertex.v[axis];
		float const component_rounded = std::floor(component + 0.5f);
		vertex_rounded.v[axis] = ((std::abs(component - component_rounded) < 0.001f) ? component_rounded : component);
	}
	// POINT_EPSILON, that is ON_EPSILON.
	static constexpr float epsilon = 0.01f;
	for (size_t vertex_number = 0; vertex_number < vertexes.size(); ++vertex_number) {
		vector3 const & existing_vertex = vertexes[vertex_number];
		if (std::abs(existing_vertex.v[0] - vertex_rounded.v[0]) < epsilon &&
				std::abs(existing_vertex.v[1] - vertex_rounded.v[1]) < epsilon &&
				std::abs(existing_vertex.v[2] - vertex_rounded.v[2]) < epsilon) {
			return vertex_number;
		}
	}
	vertexes.push_back(vertex_rounded);
	return vertexes.size() - 1;
}

struct id_model {
	vector3 mins;
	vector3 maxs;
	vector3 origin;
	int32_t head_nodes[max_hulls];
	uint32_t visibility_leafs;
	uint32_t first_face;
	uint32_t face_count;

	id_model() = default;
	id_model(struct gbx_model const & gbx);
};
static_assert(sizeof(id_model) == 0x40);

struct gbx_model {
	vector4 mins;
	vector4 maxs;
	vector4 origin;
	int32_t head_nodes[max_hulls];
	uint32_t visibility_leafs;
	uint32_t first_face;
	uint32_t face_count;
	// 0.
	uint32_t unknown_0;

	gbx_model() = default;
	gbx_model(struct id_model const & id);
};
static_assert(sizeof(gbx_model) == 0x50);

enum id_texinfo_flags : uint32_t {
	// In Quake, means sky or slime, no lightmap or 256 subdivision.
	// In Half-Life on the PS2 though, liquids have lightmaps.
	id_texinfo_flag_special = UINT32_C(1) << 0,
};

bool is_id_texture_special(char const * name, bool is_valve = true);

struct id_texinfo {
	// [s/t][xyz, offset]
	vector4 vectors[2];
	// Ignored (using a checkerboard texture) if there are no textures in the map at all (the textures lump has a length
	// of 0).
	uint32_t texture_number;
	/* id_texinfo_flags */ uint32_t flags;

	id_texinfo() = default;
	id_texinfo(struct gbx_face const & gbx, /* id_texinfo_flags */ uint32_t flags);
};
static_assert(sizeof(id_texinfo) == 0x28);

struct id_face {
	uint16_t plane_number;
	uint16_t side;
	uint32_t first_edge;
	uint16_t edge_count;
	uint16_t texinfo_number;
	uint8_t styles[max_lightmaps];
	// Byte offset inside the lighting lump.
	// UINT32_MAX if no lighting.
	uint32_t lighting_offset;

	id_face() = default;
	id_face(struct gbx_face const & gbx, uint16_t texinfo_number);

	void calculate_extents(
			id_texinfo const & face_texinfo, surfedge const * map_surfedges, edge const * map_edges,
			vector3 const * map_vertexes, int16_t * texture_mins_out, int16_t * extents_out) const;
};
static_assert(sizeof(id_face) == 0x14);

enum gbx_face_flags : uint16_t {
	gbx_face_flag_plane_back = UINT16_C(1) << 1,
	gbx_face_flag_draw_sky = UINT16_C(1) << 2,
	gbx_face_flag_draw_turbulent = UINT16_C(1) << 4,
	// Any gbx_face_flag_draw_tiled textures, as well as scrolling.
	gbx_face_flag_special = UINT16_C(1) << 5,
	gbx_face_flag_no_draw = UINT16_C(1) << 8,
	// In Quake, no lightmaps for the texture.
	// In Half-Life on the PS2 though, liquids have lightmaps.
	gbx_face_flag_draw_tiled = UINT16_C(1) << 9,
	gbx_face_flag_draw_polygons = UINT16_C(1) << 10,
};

// Also indicates whether the face should be subdivided into polygons (via gbx_face_flag_draw_polygons).
uint16_t texture_gbx_face_flags(char const * name);

float gbx_face_texinfo_vectors_area(vector3 s, vector3 t);

struct gbx_face {
	vector4 texinfo_vectors[2];
	uint16_t side;
	/* gbx_face_flags */ uint16_t flags;
	// In the map, byte offset inside the map file.
	// When deserialized in BS2PC, byte offset inside the textures lump.
	uint32_t texture;
	// In the map, byte offset inside the map file.
	// When deserialized in BS2PC, byte offset inside the lighting lump.
	// UINT32_MAX if no lighting.
	uint32_t lighting_offset;
	// In the map, byte offset inside the map file.
	// When deserialized in BS2PC, plane number.
	uint32_t plane;
	uint32_t unknown_0;
	uint32_t first_edge;
	uint32_t edge_count;
	float texinfo_vectors_area;
	int16_t texture_mins[2];
	int16_t extents[2];
	uint32_t unknown_1[7];
	uint8_t styles[max_lightmaps];
	uint32_t unknown_2[6];
	// In the map, byte offset inside the map file.
	// When deserialized in BS2PC, polygons number.
	// UINT32_MAX if no polygons.
	uint32_t polygons;
	uint32_t unknown_3[3];

	gbx_face() = default;
	gbx_face(
			struct id_face const & face,
			struct id_texinfo const & texinfo,
			/* gbx_face_flags */ uint16_t texture_flags,
			int16_t const * texture_mins,
			int16_t const * extents,
			uint32_t polygons = UINT32_MAX);

	void set_texinfo_vectors(vector4 const s, vector4 const t) {
		texinfo_vectors[0] = s;
		texinfo_vectors[1] = t;
		texinfo_vectors_area = gbx_face_texinfo_vectors_area(s, t);
	}

	void set_side(uint16_t const side) {
		this->side = side;
		if (side) {
			flags |= gbx_face_flag_plane_back;
		} else {
			flags &= ~uint16_t(gbx_face_flag_plane_back);
		}
	}

	void set_polygons(uint32_t const polygons) {
		this->polygons = polygons;
		if (polygons != UINT32_MAX) {
			flags |= gbx_face_flag_draw_polygons;
		} else {
			flags &= ~uint16_t(gbx_face_flag_draw_polygons);
		}
	}
};
static_assert(sizeof(gbx_face) == 0x90);

using id_marksurface = uint16_t;
using gbx_marksurface = uint32_t;

using texture_deserialized_pixels = std::vector<uint8_t>;
using id_texture_deserialized_palette = std::vector<uint8_t>;
using gbx_texture_deserialized_palette = std::array<uint8_t, 4 * 256>;

void id_palette_from_gbx(
		gbx_palette_type palette_type,
		id_texture_deserialized_palette & palette_id,
		gbx_texture_deserialized_palette const & palette_gbx_id_indexed);
void gbx_palette_from_id(
		gbx_palette_type palette_type,
		gbx_texture_deserialized_palette & palette_gbx_id_indexed,
		id_texture_deserialized_palette const & palette_id);

struct palette_set {
	id_texture_deserialized_palette id;
	std::array<gbx_texture_deserialized_palette, gbx_palette_type_count> gbx_id_indexed;

	explicit palette_set(uint8_t const * colors, size_t color_count = 256);
};

struct id_texture {
	char name[texture_name_max_length + 1];
	uint32_t width;
	uint32_t height;
	// Relative to this id_texture.
	// The palette is stored after the last mip.
	uint32_t offsets[id_texture_mip_levels];
};
static_assert(sizeof(id_texture) == 0x28);

struct id_texture_deserialized {
	std::string name;
	// Width and height must be 16-aligned.
	// Using 0 width and height for texture numbers without a mip texture (that a checkerboard texture is used instead).
	uint32_t width = 0;
	uint32_t height = 0;
	// id_texture_mip_levels mip levels stored.
	// If this is nullptr, the texture must be loaded from a WAD (0 offsets in the map file).
	std::shared_ptr<texture_deserialized_pixels> pixels;
	// Only for Valve maps. 3 * number of colors.
	std::shared_ptr<id_texture_deserialized_palette> palette;
	// For textures inside maps only, for tracking used WADs and sorting.
	size_t wad_number = SIZE_MAX;

	// On success, returns nullptr.
	// On failure, returns the error description string, and the object is left unchanged.
	char const * deserialize(
			void const * texture_data,
			size_t texture_data_remaining,
			bool has_palette,
			id_texture_deserialized_palette const & quake_palette);

	bool empty() const { return !width || !height; }

	void pixels_and_palette_from_gbx(
			struct gbx_texture_deserialized const & gbx,
			std::optional<std::shared_ptr<id_texture_deserialized_palette>> override_palette,
			id_texture_deserialized_palette const & quake_palette);

	void pixels_and_palette_from_wads_or_gbx(
			struct gbx_texture_deserialized const & gbx,
			struct wad_textures_deserialized const * const * wads,
			size_t wad_count,
			bool include_all_textures,
			palette_set const & quake_palette);

	void remove_pixels() {
		pixels.reset();
		palette.reset();
	}
};

struct gbx_texture {
	// Byte offset inside the map file.
	uint32_t pixels;
	// Byte offset inside the map file.
	uint32_t palette;
	// The largest original width or height on Half-Life and Decay maps is 384.
	uint16_t width;
	uint16_t height;
	// Powers of two.
	// The WAD texture is resampled to this size using a high-quality filter (possibly bicubic) in the original Gearbox
	// maps.
	// The largest scaled width or height on Half-Life and Decay maps is 256
	// (though GLQuake supports 1024x512 pixels total and up to 1024 per side, limiting to 256x256 only on 3dfx).
	// Interesting cases:
	// - With 48:
	//   - 48x48 -> 32x32
	//   - 48x64 -> 64x64
	// - With 160:
	//   - 128x160 -> 128x128
	//   - 160x128 -> 128x128
	//   - 160x160 -> 128x128
	//   - 176x160 -> 128x256 (the longest dimension becomes different)
	//   - 256x160 -> 256x128
	// - With 240:
	//   - 240x112 -> 256x128
	//   - 240x208 -> 128x128
	uint16_t scaled_width;
	uint16_t scaled_height;
	char name[texture_name_max_length + 1];
	char unknown_0[3];
	// From min(scaled_width, scaled_height) (not max - mip_levels is 1 for 64x16, for example).
	// Until x8 along the shortest dimension - the base level is not included in this count.
	uint8_t mip_levels;
	uint32_t unknown_1[2];
	// Minus-prefixed (random-tiled) textures also have linked animation frames.
	// 0 if no animation.
	uint32_t anim_total;
	// 0 if no animation.
	uint32_t anim_min;
	// 0 if no animation.
	uint32_t anim_max;
	// Texture byte offset inside the map file, or UINT32_MAX if not available.
	uint32_t anim_next;
	// Texture byte offset inside the map file, or UINT32_MAX if not available.
	uint32_t alternate_anims;
};
static_assert(sizeof(gbx_texture) == 0x40);

struct gbx_texture_deserialized {
	// For random-tiled textures, rows are deinterleaved in deserialized textures.
	std::shared_ptr<texture_deserialized_pixels> pixels;
	// The color numbers are the same as those the pixels actually use (like in id textures),
	// with convert_palette_color_number applied to the serialized version.
	// If pixels are not nullptr, but the palette is nullptr, the Quake palette must be used.
	std::shared_ptr<gbx_texture_deserialized_palette> palette_id_indexed;
	uint16_t width;
	uint16_t height;
	uint16_t scaled_width;
	uint16_t scaled_height;
	std::string name;
	uint8_t mip_levels;
	uint32_t anim_total;
	uint32_t anim_min;
	uint32_t anim_max;
	// Texture number, or UINT32_MAX if not available.
	uint32_t anim_next;
	// Texture number, or UINT32_MAX if not available.
	uint32_t alternate_anims;

	// On success, returns nullptr.
	// On failure, returns the error description string, and the object is left unchanged.
	// anim_next and alternate_anims are kept as addresses, conversion to a number must be done externally if needed.
	char const * deserialize_with_anim_offsets(
			void const * base, size_t size_after_base, size_t offset, bool deinterleave_random,
			palette_set const & quake_palette);

	void pixels_and_palette_from_id(
			struct id_texture_deserialized const & id,
			id_texture_deserialized_palette const & quake_palette);

	void pixels_and_palette_from_wad(
			struct wad_texture_deserialized & wad_texture,
			id_texture_deserialized_palette const & quake_palette);

	void remove_pixels() {
		pixels.reset();
		palette_id_indexed.reset();
	}

	// Clears the animation-related fields.
	void reset_anim() {
		anim_total = 0;
		anim_min = 0;
		anim_max = 0;
		anim_next = UINT32_MAX;
		alternate_anims = UINT32_MAX;
	}
};

struct gbx_polygon_vertex {
	vector3 xyz;
	float st[2];
	uint8_t light_st[2];
	uint16_t padding;
};
static_assert(sizeof(gbx_polygon_vertex) == 0x18);

constexpr uint8_t gbx_polygon_strip_alignment_byte = UINT8_C(0xFE);

struct gbx_polygons_deserialized {
	// Serialized structure:
	// - uint32_t face_number
	// - uint32_t vertex_count
	// - gbx_polygon_vertex vertexes[vertex_count]
	// - uint32_t strip_count
	// - strips[strip_count]
	//   - uint16_t strip_vertex_count
	//   - uint16_t strip_vertex_indexes[strip_vertex_count]
	//   - uint16_t strip_alignment_padding[(1 + strip_vertex_count) & 1] = gbx_polygon_strip_alignment_padding_byte...
	//     (to align each strip, including the length, to 4 bytes)
	uint32_t face_number;
	std::vector<gbx_polygon_vertex> vertexes;
	std::vector<std::vector<uint16_t>> strips;
};

// Even if a lump is the last, its size is still aligned (by AddLump in common/bspfile.c and common/bsplib.c).
constexpr uint32_t id_lump_alignment = 4;
constexpr uint32_t gbx_lump_alignment = 16;

enum id_lump_number {
	id_lump_number_entities,
	id_lump_number_planes,
	id_lump_number_textures,
	id_lump_number_vertexes,
	id_lump_number_visibility,
	id_lump_number_nodes,
	id_lump_number_texinfo,
	id_lump_number_faces,
	id_lump_number_lighting,
	id_lump_number_clipnodes,
	id_lump_number_leafs,
	id_lump_number_marksurfaces,
	id_lump_number_edges,
	id_lump_number_surfedges,
	id_lump_number_models,

	id_lump_count,

	// While the order the lumps are stored in doesn't matter, in Valve's maps, it's:
	// - Planes
	// - Leafs
	// - Vertexes
	// - Nodes
	// - Texinfo
	// - Faces
	// - Clipnodes
	// - Marksurfaces
	// - Surfedges
	// - Edges
	// - Models
	// - Lighting
	// - Visibility
	// - Entities
	// - Textures
};

enum gbx_lump_number {
	gbx_lump_number_planes,
	gbx_lump_number_nodes,
	gbx_lump_number_leafs,
	gbx_lump_number_edges,
	gbx_lump_number_surfedges,
	gbx_lump_number_vertexes,
	gbx_lump_number_hull_0,
	gbx_lump_number_clipnodes,
	gbx_lump_number_models,
	gbx_lump_number_faces,
	gbx_lump_number_marksurfaces,
	// The count is not stored, only the length.
	gbx_lump_number_visibility,
	// The count is not stored, only the length.
	gbx_lump_number_lighting,
	gbx_lump_number_textures,
	// The count is not stored, only the length.
	gbx_lump_number_entities,
	gbx_lump_number_polygons,

	gbx_lump_count,

	// While the order the lumps are stored in doesn't matter, the lumps are stored in the map file in the same order.
};

struct id_header_lump {
	uint32_t offset;
	uint32_t length;
};
static_assert(sizeof(id_header_lump) == 0x8);

struct id_map {
	uint32_t version = id_map_version_valve;
	std::vector<entity_key_values> entities;
	std::vector<id_plane> planes;
	std::vector<id_texture_deserialized> textures;
	std::vector<vector3> vertexes;
	std::vector<uint8_t> visibility;
	std::vector<id_node> nodes;
	std::vector<id_texinfo> texinfo;
	std::vector<id_face> faces;
	std::vector<uint8_t> lighting;
	std::vector<clipnode> clipnodes;
	std::vector<id_leaf> leafs;
	std::vector<id_marksurface> marksurfaces;
	std::vector<edge> edges;
	std::vector<surfedge> surfedges;
	std::vector<id_model> models;

	// On success, returns nullptr.
	// On failure, returns the error description string, and the object is left in an indeterminate state.
	char const * deserialize(
			void const * map, size_t map_size, bool quake_as_valve,
			id_texture_deserialized_palette const & quake_palette);

	// During the conversion, lumps that have equivalents in the other format won't be reindexed,
	// and nothing will be erased from them, so iterating both at once afterwards is possible.
	void from_gbx_no_texture_pixels(struct gbx_map const & gbx);

	void serialize(std::vector<char> & map, id_texture_deserialized_palette const & quake_palette) const;

	// Does not add the Quake palette to the textures to prevent duplication.
	// Empty palette in id_texture, even for Valve maps, should be treated as the Quake palette being used.
	// Doesn't update the model paths, as that's expected to be done externally,
	// as a Quake to Gearbox conversion may be needed, so the conversion isn't done twice.
	void upgrade_from_quake_without_model_paths(bool subdivide_turbulent = true);

	// Removes nodraw textures used on Gearbox maps, and all faces referencing them.
	// Returns if any changes were made.
	bool remove_nodraw();

	// Sorts textures by the file they're loaded from and then by the name for the most efficient loading in the engine.
	// Similar to the goal of sorting in qcsg.
	void sort_textures();
};

struct gbx_map {
	std::vector<gbx_plane> planes;
	std::vector<gbx_node> nodes;
	std::vector<gbx_leaf> leafs;
	std::vector<edge> edges;
	std::vector<surfedge> surfedges;
	std::vector<vector4> vertexes;
	std::vector<clipnode> hull_0;
	std::vector<clipnode> clipnodes;
	std::vector<gbx_model> models;
	std::vector<gbx_face> faces;
	std::vector<gbx_marksurface> marksurfaces;
	std::vector<uint8_t> visibility;
	std::vector<uint8_t> lighting;
	std::vector<gbx_texture_deserialized> textures;
	std::vector<entity_key_values> entities;
	std::vector<gbx_polygons_deserialized> polygons;

	void set_node_or_leaf_parent(int32_t node_or_leaf_number, uint32_t parent);

	void make_hull_0_from_nodes_and_leafs();

	// Relinks all animated textures.
	void link_texture_anim();

	void make_polygons(gbx_polygons_deserialized * polygons_start, size_t polygons_count);

	// On success, returns nullptr.
	// On failure, returns the error description string, and the object is left in an indeterminate state.
	char const * deserialize(void const * map, size_t map_size, palette_set const & quake_palette);

	char const * deserialize_only_textures(void const * map, size_t map_size, palette_set const & quake_palette);

	// During the conversion, lumps that have equivalents in the other format won't be reindexed,
	// and nothing will be erased from them, so iterating both at once afterwards is possible.
	void from_id_no_texture_pixels_and_polygons(struct id_map const & id);

	void serialize(std::vector<char> & map, palette_set const & quake_palette) const;

private:
	char const * deserialize_textures(
			void const * map, size_t map_size,
			size_t textures_offset, size_t textures_lump_length, size_t texture_count,
			palette_set const & quake_palette);
};

void write_polygons_to_obj(std::ostream & obj, gbx_map const & map);

// Texture WADs.

enum wad_lump_type : uint8_t {
	wad_lump_type_texture = 0x43,
};

enum wad_lump_compression : uint8_t {
	wad_lump_compression_none,
	wad_lump_compression_lzss,
};

// The name must be null-terminated (the engine uses C string functions without an explicit size limit), the buffer size
// is thus the max length plus 1.
constexpr uint32_t wad_lump_name_max_length = 15;

struct wad_lump_info {
	uint32_t file_position;
	uint32_t disk_size;
	// Uncompressed.
	uint32_t size;
	/* wad_lump_type_texture */ uint8_t type;
	/* wad_lump_compression */ uint8_t compression;
	uint16_t padding;
	char name[wad_lump_name_max_length + 1];
};
static_assert(sizeof(wad_lump_info) == 0x20);

struct wad_info {
	char identification[4];
	uint32_t lump_count;
	uint32_t info_table_offset;
};
static_assert(sizeof(wad_info) == 0xC);

struct wad_texture_deserialized {
	id_texture_deserialized texture_id;
	// "default_scaled_size" means for gbx_texture_scaled_size (for conversion done by BS2PC, not from WADG).
	std::shared_ptr<texture_deserialized_pixels> default_scaled_size_pixels_gbx;
	std::shared_ptr<texture_deserialized_pixels> default_scaled_size_pixels_random_gbx;
	std::array<std::shared_ptr<gbx_texture_deserialized_palette>, gbx_palette_type_count> palettes_id_indexed_gbx;
};

struct wad_textures_deserialized {
	std::vector<wad_texture_deserialized> textures;
	// The keys are string_to_lower(name).
	std::unordered_map<std::string, size_t> texture_number_map;
};

void append_worldspawn_wad_names(entity_key_values const & worldspawn, std::vector<std::string> & names);
std::string serialize_worldspawn_wad_paths(std::vector<std::string> const & paths);
void set_worldspawn_wad_paths(entity_key_values & worldspawn, std::string_view paths_serialized);
inline void set_worldspawn_wad_paths(entity_key_values & worldspawn, std::vector<std::string> const & paths) {
	set_worldspawn_wad_paths(worldspawn, serialize_worldspawn_wad_paths(paths));
}
// Adds PC Half-Life WAD files to the WAD names if there's hlps2.wad, and removes gbx1.wad.
// On the PC, any texture without pixels included means search in all WADs and crash if any WAD is not loaded.
// Returns if any changes were made.
bool replace_hlps2_wads(std::vector<std::string> & wad_names);

// Writes textures that have been successfully loaded from the WAD.
// On success, returns nullptr.
// On failure to load the WAD file itself, returns the error description string, and the resulting object will be empty.
// For a Quake WAD (WAD2), the textures will be without a palette.
char const * get_wad_textures(
		void const * wad, size_t wad_size, wad_textures_deserialized & wad_textures,
		id_texture_deserialized_palette const & quake_palette);

// Returns whether the id and the Gearbox textures are likely identical (the Gearbox texture was converted from an id
// one), thus the WAD version can be used instead of converting (lossily due to resampling from/to power of two).
texture_identical_status is_texture_data_identical(
		id_texture_deserialized const & texture_id,
		gbx_texture_deserialized const & texture_gbx,
		palette_set const & quake_palette);

wad_texture_deserialized const * find_most_identical_texture_in_wads(
		gbx_texture_deserialized const & texture_gbx,
		char const * name_override,
		wad_textures_deserialized const * const * wads,
		size_t wad_count,
		palette_set const & quake_palette,
		texture_identical_status * identical_status_out,
		size_t * wad_number_out,
		bool * is_inclusion_required_out);
inline wad_texture_deserialized * find_most_identical_texture_in_wads(
		gbx_texture_deserialized const & texture_gbx,
		char const * const name_override,
		wad_textures_deserialized * const * const wads,
		size_t const wad_count,
		palette_set const & quake_palette,
		texture_identical_status * const identical_status_out,
		size_t * const wad_number_out,
		bool * const is_inclusion_required_out) {
	return const_cast<wad_texture_deserialized *>(find_most_identical_texture_in_wads(
			texture_gbx, name_override,
			const_cast<wad_textures_deserialized const * const *>(wads), wad_count, quake_palette,
			identical_status_out, wad_number_out, is_inclusion_required_out));
}

// Resamples the texture if the output size is different than the input size, and generates mips.
// If only mips need to be generated, the output pointer may be the same as the input one.
void convert_texture_pixels(
		bool is_transparent, id_texture_deserialized_palette const & palette,
		uint8_t * out_pixels, uint32_t out_width, uint32_t out_height, uint32_t out_mip_levels_without_base,
		uint8_t const * in_pixels, uint32_t in_width, uint32_t in_height, uint32_t in_mip_levels_without_base);

// textures_gbx must contain the original Gearbox textures corresponding to the id map textures (but possibly at
// different indexes).
// It's not necessary for the id map to contain the pixels converted or loaded from the WAD though, they will be located
// in the WADs if not already if needed.
void reconstruct_random_texture_sequences(
		id_map & map,
		gbx_texture_deserialized const * textures_gbx,
		size_t textures_gbx_count,
		wad_textures_deserialized const * const * wads,
		size_t wad_count,
		bool include_all_textures,
		palette_set const & quake_palette);

// BS2PC-internal storage of exported original Gearbox textures ("WADG").
// For more visual consistency with the original maps, especially between level changes, so the filtering and the sizes
// of the textures when converting from id to Gearbox are the same as in the original conversions.
// The map key is string_to_lower(gbx_texture.name).

// On success, returns nullptr.
// On failure to load the WADG file itself, returns the error description string, and no textures will be added.
template<typename map_type>
char const * add_wadg_textures(
		void const * const wadg, size_t const wadg_size, map_type & wadg_textures,
		palette_set const & quake_palette) {
	if (wadg_size < sizeof(wad_info)) {
		return "BS2PC WADG file information is out of bounds";
	}
	wad_info info;
	std::memcpy(&info, wadg, sizeof(wad_info));
	if (info.identification[0] != 'W' ||
			info.identification[1] != 'A' ||
			info.identification[2] != 'D' ||
			info.identification[3] != 'G') {
		return "The file is not a BS2PC WADG file";
	}
	if (!info.lump_count) {
		return nullptr;
	}
	if (info.info_table_offset > wadg_size ||
			(wadg_size - info.info_table_offset) / sizeof(wad_lump_info) < info.lump_count) {
		return "The information table is out of bounds";
	}
	for (uint32_t lump_number = 0; lump_number < info.lump_count; ++lump_number) {
		wad_lump_info lump_info;
		std::memcpy(
			&lump_info,
			reinterpret_cast<char const *>(wadg) + info.info_table_offset + sizeof(wad_lump_info) * lump_number,
			sizeof(wad_lump_info));
		if (lump_info.type != wad_lump_type_texture || lump_info.compression != wad_lump_compression_none) {
			// Not produced by BS2PC.
			continue;
		}
		if (lump_info.file_position > wadg_size || wadg_size - lump_info.file_position < lump_info.size) {
			// Out of bounds.
			continue;
		}
		gbx_texture_deserialized texture_deserialized;
		if (texture_deserialized.deserialize_with_anim_offsets(
				reinterpret_cast<char const *>(wadg) + lump_info.file_position,
				lump_info.size,
				0,
				false,
				quake_palette)) {
			// Failed to deserialize.
			continue;
		}
		// Clean up the animation fields as they will be reconstructed for every map.
		texture_deserialized.reset_anim();
		wadg_textures.emplace(string_to_lower(texture_deserialized.name), std::move(texture_deserialized));
	}
	return nullptr;
}

// The output stream must be in the binary mode.
void write_wadg(
		std::ofstream & output_stream,
		std::map<std::string, gbx_texture_deserialized> & textures,
		palette_set const & quake_palette);

// The resulting texture may have a different name than requested.
template<typename map_type>
typename map_type::const_iterator find_identical_wadg_texture(
		map_type const & wadg_textures,
		std::string_view const name,
		id_texture_deserialized const & texture_id,
		palette_set const & quake_palette) {
	std::string key = string_to_lower(name);
	auto const wadg_texture_iterator = wadg_textures.find(key);
	if (wadg_texture_iterator != wadg_textures.cend() &&
			bs2pc::is_texture_data_identical(texture_id, wadg_texture_iterator->second, quake_palette) ==
					bs2pc::texture_identical_status::same_palette_same_or_resampled_pixels) {
		return wadg_texture_iterator;
	}
	// Some animated textures have one specific frame selected on certain maps with the + prefix removed.
	// Try finding the pixels in the frame with the prefix toggled.
	// Not doing the same for random-tiled ('-'-prefixed) textures as their palettes have inverted colors (in the
	// original Valve 24-bit texture, not even in 21 bits directly), can't simply copy the palette between a
	// `-`-prefixed texture and one without the prefix. Using pixels from a '-'-prefixed texture for one without the
	// prefix is even worse, as they have incorrect mips in the original Gearbox maps, thus the pixels can't be reused
	// directly. The PS2 engine doesn't display the '-'-prefixed textures correctly at all though, with the interleaving
	// and the inversion being visible to the player. Therefore, it's recommended to remove the '-' prefix completely
	// when converting from Valve maps to Gearbox.
	if (key.c_str()[0] == '+') {
		key = key.substr(1);
		auto const wadg_texture_iterator_without_animation = wadg_textures.find(key);
		if (wadg_texture_iterator_without_animation != wadg_textures.cend() &&
				bs2pc::is_texture_data_identical(
						texture_id, wadg_texture_iterator_without_animation->second, quake_palette) ==
						bs2pc::texture_identical_status::same_palette_same_or_resampled_pixels) {
			return wadg_texture_iterator_without_animation;
		}
	} else if (key.size() < texture_name_max_length && texture_anim_frame(key.c_str()[0]) != UINT32_MAX) {
		key = '+' + key;
		auto const wadg_texture_iterator_with_animation = wadg_textures.find(key);
		if (wadg_texture_iterator_with_animation != wadg_textures.cend() &&
				bs2pc::is_texture_data_identical(
						texture_id, wadg_texture_iterator_with_animation->second, quake_palette) ==
						bs2pc::texture_identical_status::same_palette_same_or_resampled_pixels) {
			return wadg_texture_iterator_with_animation;
		}
	}
	return wadg_textures.cend();
}

// Compression.

// Best compression level (level 9, FLEVEL 3 in the FLG).
constexpr int gbx_map_zlib_level = 9;
// 32 KB window (CINFO 7 in the CMF).
constexpr int gbx_map_zlib_window_bits = 15;
// Two bytes in the beginning of the zlib stream.
constexpr uint8_t gbx_map_zlib_cmf = 0x78;
constexpr uint8_t gbx_map_zlib_flg = 0xDA;

// Checks whether the map file is compressed with the compression settings used by Gearbox.
// Does not, however, check if the map is actually a Gearbox map - must decompress and check externally.
bool is_gbx_map_compressed(void const * map_file, size_t map_file_size);
bool compress_gbx_map(void const * uncompressed, size_t uncompressed_size, std::vector<char> & compressed);
bool decompress_gbx_map(void const * compressed, size_t compressed_size, std::vector<char> & uncompressed);

}

#endif
