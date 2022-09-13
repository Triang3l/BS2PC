#include "bs2pclib.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace bs2pc {

bool is_id_texture_special(char const * const name, bool const is_valve) {
	// In Quake, FindTexinfo marks the texture as special for the following prefixes (not full names - using
	// Q_strncasecmp):
	// - *
	// - sky
	// In Half-Life, TexinfoForBrushTexture marks the texture as special for the following prefixes (also using
	// Q_strncasecmp):
	// - *
	// - sky
	// - clip
	// - origin
	// - aaatrigger
	if (name[0] == '*' || !bs2pc_strncasecmp(name, "sky", sizeof("sky") - 1)) {
		return true;
	}
	if (is_valve) {
		if (!bs2pc_strncasecmp(name, "clip", sizeof("clip") - 1) ||
				!bs2pc_strncasecmp(name, "origin", sizeof("origin") - 1) ||
				!bs2pc_strncasecmp(name, "aaatrigger", sizeof("aaatrigger") - 1)) {
			return true;
		}
	}
	return false;
}

void id_face::calculate_extents(
		id_texinfo const & face_texinfo, surfedge const * const map_surfedges, edge const * const map_edges,
		vector3 const * const map_vertexes, int16_t * const texture_mins_out, int16_t * const extents_out) const {
	std::array<float, 2> mins, maxs;
	for (size_t axis = 0; axis < 2; ++axis) {
		mins[axis] = float(INT16_MAX / 16 * 16);
		maxs[axis] = float(INT16_MIN / 16 * 16);
	}
	for (size_t face_edge_number = 0; face_edge_number < edge_count; ++face_edge_number) {
		surfedge const face_surfedge = map_surfedges[first_edge + face_edge_number];
		vector3 const & vertex = map_vertexes[map_edges[std::abs(face_surfedge)].vertexes[size_t(face_surfedge < 0)]];
		for (size_t axis = 0; axis < 2; ++axis) {
			vector4 const & texinfo_vector = face_texinfo.vectors[axis];
			float const value =
					vertex.v[0] * texinfo_vector.v[0] +
					vertex.v[1] * texinfo_vector.v[1] +
					vertex.v[2] * texinfo_vector.v[2] +
					texinfo_vector.v[3];
			mins[axis] = std::min(mins[axis], value);
			maxs[axis] = std::max(maxs[axis], value);
		}
	}
	for (size_t axis = 0; axis < 2; ++axis) {
		int16_t const axis_min = int16_t(std::floor(mins[axis] / 16.0f));
		int16_t const axis_max = int16_t(std::ceil(maxs[axis] / 16.0f));
		if (texture_mins_out) {
			texture_mins_out[axis] = axis_min * 16;
		}
		if (extents_out) {
			extents_out[axis] = int16_t((int32_t(axis_max) - int32_t(axis_min)) * 16);
		}
	}
}

char const * id_map::deserialize(
		void const * const map, size_t const map_size, bool const quake_as_valve,
		id_texture_deserialized_palette const & quake_palette) {
	// Version and lump offsets and length.
	std::array<id_header_lump, id_lump_count> lumps;
	{
		if (map_size < sizeof(uint32_t) + sizeof(id_header_lump) * id_lump_count) {
			return "Map version and lumps are out of bounds";
		}
		std::memcpy(&version, map, sizeof(uint32_t));
		if (version != id_map_version_valve && version != id_map_version_quake) {
			return "Map has the wrong version number, only Half-Life and Quake maps are supported";
		}
		if (quake_as_valve) {
			version = id_map_version_valve;
		}
		std::memcpy(
				lumps.data(),
				reinterpret_cast<char const *>(map) + sizeof(uint32_t),
				sizeof(id_header_lump) * id_lump_count);
		for (id_header_lump const & lump : lumps) {
			if (lump.length && (lump.offset > map_size || map_size - lump.offset < lump.length)) {
				return "Lump is out of bounds";
			}
		}
	}

	// Entities.
	{
		id_header_lump const & lump_entities = lumps[id_lump_number_entities];
		if (!lump_entities.length) {
			return "The entities lump is empty";
		}
		if (reinterpret_cast<char const *>(map)[lump_entities.offset + lump_entities.length - 1]) {
			return "The entities lump is not null-terminated";
		}
		entities = deserialize_entities(reinterpret_cast<char const *>(map) + lump_entities.offset);
	}

	// Planes.
	{
		id_header_lump const & lump_planes = lumps[id_lump_number_planes];
		if (lump_planes.length % sizeof(id_plane)) {
			return "The size of the plane lump is not a multiple of the size of a plane";
		}
		planes.clear();
		uint32_t const plane_count = lump_planes.length / sizeof(id_plane);
		if (plane_count) {
			planes.resize(plane_count);
			std::memcpy(
					planes.data(),
					reinterpret_cast<char const *>(map) + lump_planes.offset,
					sizeof(id_plane) * plane_count);
		}
	}

	// Textures.
	// If the lump is not present at all (length is 0), there are no textures, and texinfo texture indexes must be
	// ignored.
	{
		id_header_lump const & lump_textures = lumps[id_lump_number_textures];
		textures.clear();
		if (lump_textures.length) {
			char const * textures_lump_data = reinterpret_cast<char const *>(map) + lump_textures.offset;
			uint32_t const textures_length = lump_textures.length;
			if (textures_length < sizeof(uint32_t)) {
				return "The textures lump is too small to store the texture count";
			}
			uint32_t texture_count;
			std::memcpy(&texture_count, textures_lump_data, sizeof(uint32_t));
			if ((textures_length - sizeof(uint32_t)) / sizeof(uint32_t) < texture_count) {
				return "The textures lump is too small to store the texture offsets";
			}
			// `textures` are initialized to non-existent, so textures with UINT32_MAX offset (checkerboards) can be
			// skipped via `continue`.
			textures.resize(texture_count);
			for (uint32_t texture_number = 0; texture_number < texture_count; ++texture_number) {
				uint32_t texture_offset;
				std::memcpy(
					&texture_offset,
					textures_lump_data + sizeof(uint32_t) + sizeof(uint32_t) * texture_number,
					sizeof(uint32_t));
				if (texture_offset == UINT32_MAX) {
					continue;
				}
				if (texture_offset > textures_length) {
					return "Texture is out of bounds of the textures lump";
				}
				char const * const texture_deserialize_error = textures[texture_number].deserialize(
						textures_lump_data + texture_offset,
						textures_length - texture_offset,
						version >= id_map_version_valve,
						quake_palette);
				if (texture_deserialize_error) {
					return texture_deserialize_error;
				}
			}
		}
	}

	// Vertexes.
	{
		id_header_lump const & lump_vertexes = lumps[id_lump_number_vertexes];
		if (lump_vertexes.length % sizeof(vector3)) {
			return "The size of the vertexes lump is not a multiple of the size of a vertex";
		}
		vertexes.clear();
		uint32_t const vertex_count = lump_vertexes.length / sizeof(vector3);
		if (vertex_count) {
			vertexes.resize(vertex_count);
			std::memcpy(
					vertexes.data(),
					reinterpret_cast<char const *>(map) + lump_vertexes.offset,
					sizeof(vector3) * vertex_count);
		}
	}

	// Visibility.
	{
		id_header_lump const & lump_visibility = lumps[id_lump_number_visibility];
		visibility.clear();
		if (lump_visibility.length) {
			visibility.resize(lump_visibility.length);
			std::memcpy(
					visibility.data(),
					reinterpret_cast<char const *>(map) + lump_visibility.offset,
					lump_visibility.length);
		}
	}

	// Nodes.
	{
		id_header_lump const & lump_nodes = lumps[id_lump_number_nodes];
		if (lump_nodes.length % sizeof(id_node)) {
			return "The size of the nodes lump is not a multiple of the size of a node";
		}
		nodes.clear();
		uint32_t const node_count = lump_nodes.length / sizeof(id_node);
		if (node_count) {
			nodes.resize(node_count);
			std::memcpy(
					nodes.data(),
					reinterpret_cast<char const *>(map) + lump_nodes.offset,
					sizeof(id_node) * node_count);
		}
	}

	// Texinfo.
	{
		id_header_lump const & lump_texinfo = lumps[id_lump_number_texinfo];
		if (lump_texinfo.length % sizeof(id_texinfo)) {
			return "The size of the texinfo lump is not a multiple of the size of texinfo";
		}
		texinfo.clear();
		uint32_t const texinfo_count = lump_texinfo.length / sizeof(id_texinfo);
		if (texinfo_count) {
			texinfo.resize(texinfo_count);
			std::memcpy(
					texinfo.data(),
					reinterpret_cast<char const *>(map) + lump_texinfo.offset,
					sizeof(id_texinfo) * texinfo_count);
		}
	}

	// Faces.
	{
		id_header_lump const & lump_faces = lumps[id_lump_number_faces];
		if (lump_faces.length % sizeof(id_face)) {
			return "The size of the faces lump is not a multiple of the size of a face";
		}
		faces.clear();
		uint32_t const face_count = lump_faces.length / sizeof(id_face);
		if (face_count) {
			faces.resize(face_count);
			std::memcpy(
					faces.data(),
					reinterpret_cast<char const *>(map) + lump_faces.offset,
					sizeof(id_face) * face_count);
		}
	}

	// Lighting.
	{
		id_header_lump const & lump_lighting = lumps[id_lump_number_lighting];
		lighting.clear();
		uint32_t const lighting_length = lump_lighting.length;
		if (lighting_length) {
			lighting.resize(lighting_length);
			std::memcpy(
					lighting.data(),
					reinterpret_cast<char const *>(map) + lump_lighting.offset,
					lighting_length);
		}
	}

	// Clipnodes.
	{
		id_header_lump const & lump_clipnodes = lumps[id_lump_number_clipnodes];
		if (lump_clipnodes.length % sizeof(clipnode)) {
			return "The size of the clipnodes lump is not a multiple of the size of a clipnode";
		}
		clipnodes.clear();
		uint32_t const clipnode_count = lump_clipnodes.length / sizeof(clipnode);
		if (clipnode_count) {
			clipnodes.resize(clipnode_count);
			std::memcpy(
					clipnodes.data(),
					reinterpret_cast<char const *>(map) + lump_clipnodes.offset,
					sizeof(clipnode) * clipnode_count);
		}
	}

	// Leafs.
	{
		id_header_lump const & lump_leafs = lumps[id_lump_number_leafs];
		if (lump_leafs.length % sizeof(id_leaf)) {
			return "The size of the leafs lump is not a multiple of the size of a leaf";
		}
		leafs.clear();
		uint32_t const leaf_count = lump_leafs.length / sizeof(id_leaf);
		if (leaf_count) {
			leafs.resize(leaf_count);
			std::memcpy(
					leafs.data(),
					reinterpret_cast<char const *>(map) + lump_leafs.offset,
					sizeof(id_leaf) * leaf_count);
		}
	}

	// Marksurfaces.
	{
		id_header_lump const & lump_marksurfaces = lumps[id_lump_number_marksurfaces];
		if (lump_marksurfaces.length % sizeof(id_marksurface)) {
			return "The size of the marksurfaces lump is not a multiple of the size of a marksurface";
		}
		marksurfaces.clear();
		uint32_t const marksurface_count = lump_marksurfaces.length / sizeof(id_marksurface);
		if (marksurface_count) {
			marksurfaces.resize(marksurface_count);
			std::memcpy(
					marksurfaces.data(),
					reinterpret_cast<char const *>(map) + lump_marksurfaces.offset,
					sizeof(id_marksurface) * marksurface_count);
		}
	}

	// Edges.
	{
		id_header_lump const & lump_edges = lumps[id_lump_number_edges];
		if (lump_edges.length % sizeof(edge)) {
			return "The size of the edges lump is not a multiple of the size of an edge";
		}
		edges.clear();
		uint32_t const edge_count = lump_edges.length / sizeof(edge);
		if (edge_count) {
			edges.resize(edge_count);
			std::memcpy(
					edges.data(),
					reinterpret_cast<char const *>(map) + lump_edges.offset,
					sizeof(edge) * edge_count);
		}
	}

	// Surfedges.
	{
		id_header_lump const & lump_surfedges = lumps[id_lump_number_surfedges];
		if (lump_surfedges.length % sizeof(surfedge)) {
			return "The size of the surfedges lump is not a multiple of the size of a surfedge";
		}
		surfedges.clear();
		uint32_t const surfedge_count = lump_surfedges.length / sizeof(surfedge);
		if (surfedge_count) {
			surfedges.resize(surfedge_count);
			std::memcpy(
					surfedges.data(),
					reinterpret_cast<char const *>(map) + lump_surfedges.offset,
					sizeof(surfedge) * surfedge_count);
		}
	}

	// Models.
	{
		id_header_lump const & lump_models = lumps[id_lump_number_models];
		if (lump_models.length % sizeof(id_model)) {
			return "The size of the models lump is not a multiple of the size of a model";
		}
		models.clear();
		uint32_t const model_count = lump_models.length / sizeof(id_model);
		if (model_count) {
			models.resize(model_count);
			std::memcpy(
					models.data(),
					reinterpret_cast<char const *>(map) + lump_models.offset,
					sizeof(id_model) * model_count);
		}
	}

	return nullptr;
}

void id_map::serialize(std::vector<char> & map, id_texture_deserialized_palette const & quake_palette) const {
	map.clear();
	// As a result of the clear, all padding created by resizing will be zero-initialized.

	// Reserve aligned space for the header.
	map.resize(
			(sizeof(uint32_t) + sizeof(id_header_lump) * id_lump_count + (id_lump_alignment - 1)) &
					~size_t(id_lump_alignment - 1));

	// Version.
	static_assert(sizeof(version) == sizeof(uint32_t));
	std::memcpy(map.data(), &version, sizeof(uint32_t));

	std::array<id_header_lump, id_lump_count> lumps;

	auto const finish_lump = [&map, &lumps](id_lump_number lump_number) {
		size_t const map_current_size = map.size();
		lumps[lump_number].length = uint32_t(map_current_size - lumps[lump_number].offset);
		// Align the end of the lump.
		map.resize(map_current_size + (id_lump_alignment - 1) & ~size_t(id_lump_alignment - 1));
	};

	// Planes.
	{
		size_t const planes_offset = map.size();
		size_t const plane_count = planes.size();
		lumps[id_lump_number_planes].offset = uint32_t(planes_offset);
		if (plane_count) {
			map.resize(planes_offset + sizeof(id_plane) * plane_count);
			std::memcpy(map.data() + planes_offset, planes.data(), sizeof(id_plane) * plane_count);
		}
		finish_lump(id_lump_number_planes);
	}

	// Leafs.
	{
		size_t const leafs_offset = map.size();
		size_t const leaf_count = leafs.size();
		lumps[id_lump_number_leafs].offset = uint32_t(leafs_offset);
		if (leaf_count) {
			map.resize(leafs_offset + sizeof(id_leaf) * leaf_count);
			std::memcpy(map.data() + leafs_offset, leafs.data(), sizeof(id_leaf) * leaf_count);
		}
		finish_lump(id_lump_number_leafs);
	}

	// Vertexes.
	{
		size_t const vertexes_offset = map.size();
		size_t const vertex_count = vertexes.size();
		lumps[id_lump_number_vertexes].offset = uint32_t(vertexes_offset);
		if (vertex_count) {
			map.resize(vertexes_offset + sizeof(vector3) * vertex_count);
			std::memcpy(map.data() + vertexes_offset, vertexes.data(), sizeof(vector3) * vertex_count);
		}
		finish_lump(id_lump_number_vertexes);
	}

	// Nodes.
	{
		size_t const nodes_offset = map.size();
		size_t const node_count = nodes.size();
		lumps[id_lump_number_nodes].offset = uint32_t(nodes_offset);
		if (node_count) {
			map.resize(nodes_offset + sizeof(id_node) * node_count);
			std::memcpy(map.data() + nodes_offset, nodes.data(), sizeof(id_node) * node_count);
		}
		finish_lump(id_lump_number_nodes);
	}

	// Texinfo.
	{
		size_t const texinfo_offset = map.size();
		size_t const texinfo_count = texinfo.size();
		lumps[id_lump_number_texinfo].offset = uint32_t(texinfo_offset);
		if (texinfo_count) {
			map.resize(texinfo_offset + sizeof(id_texinfo) * texinfo_count);
			std::memcpy(map.data() + texinfo_offset, texinfo.data(), sizeof(id_texinfo) * texinfo_count);
		}
		finish_lump(id_lump_number_texinfo);
	}

	// Faces.
	{
		size_t const faces_offset = map.size();
		size_t const face_count = faces.size();
		lumps[id_lump_number_faces].offset = uint32_t(faces_offset);
		if (face_count) {
			map.resize(faces_offset + sizeof(id_face) * face_count);
			std::memcpy(map.data() + faces_offset, faces.data(), sizeof(id_face) * face_count);
		}
		finish_lump(id_lump_number_faces);
	}

	// Clipnodes.
	{
		size_t const clipnodes_offset = map.size();
		size_t const clipnode_count = clipnodes.size();
		lumps[id_lump_number_clipnodes].offset = uint32_t(clipnodes_offset);
		if (clipnode_count) {
			map.resize(clipnodes_offset + sizeof(clipnode) * clipnode_count);
			std::memcpy(map.data() + clipnodes_offset, clipnodes.data(), sizeof(clipnode) * clipnode_count);
		}
		finish_lump(id_lump_number_clipnodes);
	}

	// Marksurfaces.
	{
		size_t const marksurfaces_offset = map.size();
		size_t const marksurface_count = marksurfaces.size();
		lumps[id_lump_number_marksurfaces].offset = uint32_t(marksurfaces_offset);
		if (marksurface_count) {
			map.resize(marksurfaces_offset + sizeof(id_marksurface) * marksurface_count);
			std::memcpy(
					map.data() + marksurfaces_offset,
					marksurfaces.data(),
					sizeof(id_marksurface) * marksurface_count);
		}
		finish_lump(id_lump_number_marksurfaces);
	}

	// Surfedges.
	{
		size_t const surfedges_offset = map.size();
		size_t const surfedge_count = surfedges.size();
		lumps[id_lump_number_surfedges].offset = uint32_t(surfedges_offset);
		if (surfedge_count) {
			map.resize(surfedges_offset + sizeof(surfedge) * surfedge_count);
			std::memcpy(map.data() + surfedges_offset, surfedges.data(), sizeof(surfedge) * surfedge_count);
		}
		finish_lump(id_lump_number_surfedges);
	}

	// Edges.
	{
		size_t const edges_offset = map.size();
		size_t const edge_count = edges.size();
		lumps[id_lump_number_edges].offset = uint32_t(edges_offset);
		if (edge_count) {
			map.resize(edges_offset + sizeof(edge) * edge_count);
			std::memcpy(map.data() + edges_offset, edges.data(), sizeof(edge) * edge_count);
		}
		finish_lump(id_lump_number_edges);
	}

	// Models.
	{
		size_t const models_offset = map.size();
		size_t const model_count = models.size();
		lumps[id_lump_number_models].offset = uint32_t(models_offset);
		if (model_count) {
			map.resize(models_offset + sizeof(id_model) * model_count);
			std::memcpy(map.data() + models_offset, models.data(), sizeof(id_model) * model_count);
		}
		finish_lump(id_lump_number_models);
	}

	// Lighting.
	{
		size_t const lighting_offset = map.size();
		size_t const lighting_length = lighting.size();
		lumps[id_lump_number_lighting].offset = uint32_t(lighting_offset);
		if (lighting_length) {
			map.resize(lighting_offset + lighting_length);
			std::memcpy(map.data() + lighting_offset, lighting.data(), lighting_length);
		}
		finish_lump(id_lump_number_lighting);
	}

	// Visibility.
	{
		size_t const visibility_offset = map.size();
		size_t const visibility_length = visibility.size();
		lumps[id_lump_number_visibility].offset = uint32_t(visibility_offset);
		if (visibility_length) {
			map.resize(visibility_offset + visibility_length);
			std::memcpy(map.data() + visibility_offset, visibility.data(), visibility_length);
		}
		finish_lump(id_lump_number_visibility);
	}

	// Entities.
	{
		size_t const entities_offset = map.size();
		std::string entities_string = serialize_entities(entities.data(), entities.size());
		// Null-terminated.
		size_t const entities_length = entities_string.size() + 1;
		lumps[id_lump_number_entities].offset = uint32_t(entities_offset);
		map.resize(entities_offset + entities_length);
		std::memcpy(map.data() + entities_offset, entities_string.c_str(), entities_length);
		finish_lump(id_lump_number_entities);
	}

	// Textures.
	{
		size_t const textures_offset = map.size();
		uint32_t const texture_count = uint32_t(textures.size());
		lumps[id_lump_number_textures].offset = uint32_t(textures_offset);
		// If no textures, a special case (lump length 0).
		// Makes the engine use the checkerboard for all textures, ignoring the texinfo texture numbers.
		if (texture_count) {
			// Reserve space for the texture count and the texture offsets relative to the lump.
			map.resize(textures_offset + sizeof(uint32_t) + sizeof(uint32_t) * texture_count);
			// Write the texture count.
			static_assert(sizeof(texture_count) == sizeof(uint32_t));
			std::memcpy(map.data() + textures_offset, &texture_count, sizeof(uint32_t));
			for (uint32_t texture_number = 0; texture_number < texture_count; ++texture_number) {
				uint32_t texture_offset;
				id_texture_deserialized const & texture = textures[texture_number];
				if (!texture.empty()) {
					size_t const texture_map_offset = map.size();
					texture_offset = uint32_t(texture_map_offset - textures_offset);
					// Reserve space for the texture header.
					map.resize(texture_map_offset + sizeof(id_texture));
					id_texture texture_serialized;
					// Clear the unused name characters, and initialize the offsets to 0 for a WAD texture.
					std::memset(&texture_serialized, 0, sizeof(id_texture));
					std::memcpy(
							texture_serialized.name,
							texture.name.data(),
							std::min(size_t(texture_name_max_length), texture.name.size()));
					texture_serialized.width = texture.width;
					texture_serialized.height = texture.height;
					if (texture.pixels) {
						// Not a WAD texture.
						size_t const texture_pixel_count =
								texture_pixel_count_with_mips(texture.width, texture.height, id_texture_mip_levels);
						assert(texture.pixels->size() == texture_pixel_count);
						size_t const texture_pixels_offset = map.size();
						map.resize(texture_pixels_offset + texture_pixel_count);
						std::memcpy(
								map.data() + texture_pixels_offset,
								texture.pixels->data(),
								texture_pixel_count);
						if (version >= id_map_version_valve) {
							id_texture_deserialized_palette const & texture_palette =
									(texture.palette ? *texture.palette : quake_palette);
							uint16_t const texture_palette_color_count = uint16_t(texture_palette.size() / 3);
							size_t const texture_palette_offset = map.size();
							map.resize(
									texture_palette_offset +
									sizeof(uint16_t) + 3 * size_t(texture_palette_color_count));
							static_assert(sizeof(texture_palette_color_count) == sizeof(uint16_t));
							std::memcpy(
									map.data() + texture_palette_offset,
									&texture_palette_color_count,
									sizeof(uint16_t));
							std::memcpy(
									map.data() + texture_palette_offset + sizeof(uint16_t),
									texture_palette.data(),
									3 * size_t(texture_palette_color_count));
						}
						// Textures are 4-aligned (2 padding bytes after a 256-color palette prefixed with color count
						// uint16_t, for example).
						map.resize((map.size() + (sizeof(uint32_t) - 1)) & ~(sizeof(uint32_t) - 1));
						// Mip offsets.
						size_t texture_mip_offset = sizeof(id_texture);
						for (uint32_t texture_mip_level = 0;
								texture_mip_level < id_texture_mip_levels;
								++texture_mip_level) {
							texture_serialized.offsets[texture_mip_level] = uint32_t(texture_mip_offset);
							texture_mip_offset +=
									size_t(texture.width >> texture_mip_level) *
									size_t(texture.height >> texture_mip_level);
						}
					}
					std::memcpy(map.data() + texture_map_offset, &texture_serialized, sizeof(id_texture));
				} else {
					texture_offset = UINT32_MAX;
				}
				// Write the texture offset to the header of the lump.
				static_assert(sizeof(texture_offset) == sizeof(uint32_t));
				std::memcpy(
						map.data() + textures_offset + sizeof(uint32_t) + sizeof(uint32_t) * texture_number,
						&texture_offset,
						sizeof(uint32_t));
			}
		}
		finish_lump(id_lump_number_textures);
	}

	// Lumps header.
	std::memcpy(map.data() + sizeof(uint32_t), lumps.data(), sizeof(id_header_lump) * id_lump_count);
}

void id_map::upgrade_from_quake_without_model_paths(bool const subdivide_turbulent) {
	if (version != id_map_version_quake) {
		return;
	}

	version = id_map_version_valve;

	size_t const texture_count = textures.size();
	// Zero lump length is a special case with the texture numbers in texinfo being ignored, and all being
	// checkerboards.
	if (texture_count) {
		// Subdivide liquids that won't have the special flag anymore to prevent the "bad surface extents" error on the
		// PC software renderer.
		// Similar to SubdivideFace in qbsp2.
		if (subdivide_turbulent) {
			size_t const old_face_count = faces.size();

			std::vector<id_face> new_faces;
			new_faces.reserve(old_face_count);
			std::vector<std::pair<size_t, size_t>> new_face_numbers_and_counts;
			new_face_numbers_and_counts.reserve(old_face_count);
			bool faces_changed = false;

			struct subdivision_face {
				// Empty if removed after having been split.
				std::vector<size_t> vertexes;
				// SIZE_MAX if no next face.
				size_t next = SIZE_MAX;
			};
			std::vector<subdivision_face> subdivision_faces;

			static constexpr float subdivide_size = 240.0f;

			std::vector<float> plane_distances;
			// -1 for in the back, 0 for on the plane, 1 for in the front.
			std::vector<int> plane_sides;

			struct edge_face {
				size_t face_number = SIZE_MAX;
				// is_new_face_number = false - not subdivided, face_number is the number within the old face lump.
				// is_new_face_number = true - after subdivision, face_number is the number within the new face lump.
				// Faces present in both the old and new are not subdivided, and thus must have is_new_face_number set
				// to false.
				bool is_new_face_number = false;
				contents face_contents = contents_node;
			};
			// [0] - forward (plus marksurface sign), [1] - backward (minus marksurface sign).
			std::vector<std::array<edge_face, 2>> map_edge_faces;

			size_t const old_vertex_count = vertexes.size();
			size_t const old_edge_count = edges.size();

			for (size_t face_number = 0; face_number < old_face_count; ++face_number) {
				id_face & face = faces[face_number];
				if (face.edge_count < 3) {
					continue;
				}
				id_texinfo const & face_texinfo = texinfo[face.texinfo_number];
				uint32_t const face_texture_number = face_texinfo.texture_number;
				id_texture_deserialized const & texture = textures[face_texture_number];
				if (texture.empty() || texture.name.c_str()[0] != '*') {
					new_face_numbers_and_counts.emplace_back(new_faces.size(), 1);
					new_faces.push_back(face);
					continue;
				}

				subdivision_faces.clear();

				// Construct the original face to start subdividing.
				size_t face_subdivision_face_number = subdivision_faces.size();
				{
					subdivision_face & initial_subdivision_face = subdivision_faces.emplace_back();
					initial_subdivision_face.vertexes.reserve(face.edge_count);
					for (size_t face_edge_number = 0; face_edge_number < face.edge_count; ++face_edge_number) {
						surfedge const face_surfedge = surfedges[face.first_edge + face_edge_number];
						initial_subdivision_face.vertexes.push_back(
								edges[std::abs(face_surfedge)].vertexes[size_t(face_surfedge < 0)]);
					}
				}

				float texinfo_vector_lengths[2];
				vector4 texinfo_vectors_normal[2];
				for (size_t axis = 0; axis < 2; ++axis) {
					vector4 const & texinfo_vector = face_texinfo.vectors[axis];
					float const texinfo_vector_length = std::sqrt(
							texinfo_vector.v[0] * texinfo_vector.v[0] +
							texinfo_vector.v[1] * texinfo_vector.v[1] +
							texinfo_vector.v[2] * texinfo_vector.v[2]);
					texinfo_vector_lengths[axis] = texinfo_vector_length;
					for (size_t texinfo_vector_axis = 0; texinfo_vector_axis < 4; ++texinfo_vector_axis) {
						texinfo_vectors_normal[axis].v[texinfo_vector_axis] =
								texinfo_vector.v[texinfo_vector_axis] / texinfo_vector_length;
					}
				}

				bool face_subdivided = false;
				// If SIZE_MAX, the previous face is face_subdivision_face_number, and it needs to be updated.
				// Otherwise, the previous face is the next of subdivision_faces[face_subdivision_face_number].
				size_t previous_face_link_number = SIZE_MAX;
				while (true) {
					size_t current_face_number =
							previous_face_link_number != SIZE_MAX
									? subdivision_faces[previous_face_link_number].next
									: face_subdivision_face_number;
					if (current_face_number == SIZE_MAX) {
						break;
					}

					for (size_t axis = 0; axis < 2; ++axis) {
						vector4 const & texinfo_vector = face_texinfo.vectors[axis];
						float const texinfo_vector_length = texinfo_vector_lengths[axis];
						vector4 const & texinfo_vector_normal = texinfo_vectors_normal[axis];
						while (true) {
							subdivision_face * current_face = &subdivision_faces[current_face_number];

							assert(current_face->vertexes.size() >= 3);
							if (current_face->vertexes.size() < 3) {
								break;
							}

							float axis_position_min = FLT_MAX, axis_position_max = -FLT_MAX;
							for (size_t const face_vertex_index : current_face->vertexes) {
								vector3 const & face_vertex = vertexes[face_vertex_index];
								float const axis_position =
										face_vertex.v[0] * texinfo_vector.v[0] +
										face_vertex.v[1] * texinfo_vector.v[1] +
										face_vertex.v[2] * texinfo_vector.v[2];
								axis_position_min = std::min(axis_position_min, axis_position);
								axis_position_max = std::max(axis_position_max, axis_position);
							}
							if (axis_position_max - axis_position_min <= subdivide_size) {
								break;
							}
							float const subdivide_plane_distance =
									(axis_position_min + subdivide_size - 16.0f) / texinfo_vector_length;

							size_t front_face_number, back_face_number;

							plane_distances.clear();
							plane_distances.reserve(current_face->vertexes.size() + 1);
							plane_sides.clear();
							plane_sides.reserve(current_face->vertexes.size() + 1);
							bool any_in_front = false, any_in_back = false;
							for (size_t const face_vertex_index : current_face->vertexes) {
								vector3 const & face_vertex = vertexes[face_vertex_index];
								float const plane_distance =
										face_vertex.v[0] * texinfo_vector_normal.v[0] +
										face_vertex.v[1] * texinfo_vector_normal.v[1] +
										face_vertex.v[2] * texinfo_vector_normal.v[2] -
										subdivide_plane_distance;
								plane_distances.push_back(plane_distance);
								int plane_side;
								// ON_EPSILON.
								static constexpr float epsilon = 0.01f;
								if (plane_distance > epsilon) {
									plane_side = 1;
									any_in_front = true;
								} else if (plane_distance < -epsilon) {
									plane_side = -1;
									any_in_back = true;
								} else {
									plane_side = 0;
								}
								plane_sides.push_back(plane_side);
							}
							if (!any_in_front) {
								front_face_number = SIZE_MAX;
								back_face_number = current_face_number;
							} else if (!any_in_back) {
								front_face_number = current_face_number;
								back_face_number = SIZE_MAX;
							} else {
								face_subdivided = true;

								plane_distances.push_back(plane_distances.front());
								plane_sides.push_back(plane_sides.front());

								back_face_number = subdivision_faces.size();
								subdivision_faces.emplace_back();
								front_face_number = subdivision_faces.size();
								subdivision_faces.emplace_back();
								// Update the pointer after potential reallocations.
								current_face = &subdivision_faces[current_face_number];
								subdivision_face & back_face = subdivision_faces[back_face_number];
								subdivision_face & front_face = subdivision_faces[front_face_number];

								// Distribute the points and generate splits.
								for (size_t face_vertex_number = 0;
										face_vertex_number < current_face->vertexes.size();
										++face_vertex_number) {
									size_t const face_vertex_index = current_face->vertexes[face_vertex_number];
									vector3 const & face_vertex = vertexes[face_vertex_index];

									int const plane_side = plane_sides[face_vertex_number];
									if (!plane_side) {
										back_face.vertexes.push_back(face_vertex_index);
										front_face.vertexes.push_back(face_vertex_index);
										continue;
									}
									if (plane_side > 0) {
										front_face.vertexes.push_back(face_vertex_index);
									} else {
										back_face.vertexes.push_back(face_vertex_index);
									}
									int const plane_side_next = plane_sides[face_vertex_number + 1];
									if (!plane_side_next || plane_side_next == plane_side) {
										continue;
									}

									// Generate a split point.
									vector3 const & face_vertex_next =
											vertexes[
													current_face->vertexes[(face_vertex_number + 1) %
													current_face->vertexes.size()]];
									float const dot =
											plane_distances[face_vertex_number] /
											(plane_distances[face_vertex_number] -
													plane_distances[face_vertex_number + 1]);
									vector3 split_vertex;
									for (size_t split_vertex_component = 0;
											split_vertex_component < 3;
											++split_vertex_component) {
										// Avoid roundoff error when possible.
										if (texinfo_vector_normal.v[split_vertex_component] == 1.0f) {
											split_vertex.v[split_vertex_component] = subdivide_plane_distance;
										} else if (texinfo_vector_normal.v[split_vertex_component] == -1.0f) {
											split_vertex.v[split_vertex_component] = -subdivide_plane_distance;
										} else {
											split_vertex.v[split_vertex_component] =
													face_vertex.v[split_vertex_component] +
															dot *
															(face_vertex_next.v[split_vertex_component] -
																	face_vertex.v[split_vertex_component]);
										}
									}
									size_t const split_vertex_index = add_vertex(vertexes, split_vertex);
									back_face.vertexes.push_back(split_vertex_index);
									front_face.vertexes.push_back(split_vertex_index);
								}

								// Remove the subdivided face.
								current_face->vertexes.clear();
							}

							if (front_face_number == SIZE_MAX || back_face_number == SIZE_MAX) {
								// Didn't split the face.
								break;
							}

							(previous_face_link_number != SIZE_MAX
									? subdivision_faces[previous_face_link_number].next
									: face_subdivision_face_number) =
									back_face_number;
							subdivision_faces[back_face_number].next = front_face_number;
							subdivision_faces[front_face_number].next = current_face->next;

							current_face_number = back_face_number;
						}
					}

					previous_face_link_number =
							previous_face_link_number != SIZE_MAX
									? subdivision_faces[previous_face_link_number].next
									: face_subdivision_face_number;
				}

				if (!face_subdivided) {
					new_face_numbers_and_counts.emplace_back(new_faces.size(), 1);
					new_faces.push_back(face);
					continue;
				}

				if (!faces_changed) {
					// Gather edges used on the map for reuse of them and for correct surface caching in the software
					// renderer.
					map_edge_faces.resize(old_edge_count);
					for (id_leaf const & leaf : leafs) {
						for (size_t leaf_marksurface_number = 0;
								leaf_marksurface_number < leaf.marksurface_count;
								++leaf_marksurface_number) {
							size_t const leaf_face_number =
									marksurfaces[leaf.first_marksurface + leaf_marksurface_number];
							id_face const & leaf_face = faces[leaf_face_number];
							for (size_t leaf_face_surfedge_number = 0;
									leaf_face_surfedge_number < leaf_face.edge_count;
									++leaf_face_surfedge_number) {
								surfedge const leaf_face_surfedge =
										surfedges[leaf_face.first_edge + leaf_face_surfedge_number];
								edge_face & lump_edge_face =
										map_edge_faces[std::abs(leaf_face_surfedge)][size_t(leaf_face_surfedge < 0)];
								lump_edge_face.face_number = leaf_face_number;
								lump_edge_face.is_new_face_number = false;
								lump_edge_face.face_contents = contents(leaf.leaf_contents);
							}
						}
					}

					faces_changed = true;
				}

				// Detach the face being removed from the edges, and also get the contents for the new edges.
				contents face_contents = contents_water;
				for (size_t face_surfedge_number = 0; face_surfedge_number < face.edge_count; ++face_surfedge_number) {
					surfedge const face_surfedge = surfedges[face.first_edge + face_surfedge_number];
					edge_face & face_edge_face = map_edge_faces[std::abs(face_surfedge)][size_t(face_surfedge < 0)];
					face_contents = face_edge_face.face_contents;
					face_edge_face.face_number = SIZE_MAX;
				}

				// Write the subdivided faces.
				new_face_numbers_and_counts.emplace_back(new_faces.size(), 0);
				size_t next_subdivision_face_number = face_subdivision_face_number;
				while (next_subdivision_face_number != SIZE_MAX) {
					size_t const subdivision_face_number = next_subdivision_face_number;
					subdivision_face & new_face_subdivision_face = subdivision_faces[subdivision_face_number];
					next_subdivision_face_number = new_face_subdivision_face.next;
					// The vertex count is 0 for removed faces that have been subdivided.
					if (new_face_subdivision_face.vertexes.size() < 3) {
						continue;
					}
					size_t const new_face_number = new_faces.size();
					size_t const new_face_vertex_count = new_face_subdivision_face.vertexes.size();
					id_face & new_face = new_faces.emplace_back(face);
					new_face.first_edge = uint32_t(surfedges.size());
					new_face.edge_count = uint16_t(new_face_vertex_count);
					surfedges.reserve(surfedges.size() + new_face_vertex_count);
					for (size_t subdivision_face_vertex_number = 0;
							subdivision_face_vertex_number < new_face_vertex_count;
							++subdivision_face_vertex_number) {
						size_t const new_face_edge_vertexes[2] = {
							new_face_subdivision_face.vertexes[subdivision_face_vertex_number],
							new_face_subdivision_face.vertexes[
									(subdivision_face_vertex_number + 1) % new_face_subdivision_face.vertexes.size()],
						};
						ptrdiff_t new_face_surfedge = 0;
						// Try to find an existing edge.
						// If any vertex is new, search only among the newly added edges.
						// Skipping the edge 0 because it's not possible for a surfedge to be -0.
						for (size_t edge_number =
								((std::max(new_face_edge_vertexes[0], new_face_edge_vertexes[1]) >=
										old_vertex_count)
										? old_edge_count
										: 1);
									edge_number < edges.size();
									++edge_number) {
							edge const & try_edge = edges[edge_number];
							std::array<edge_face, 2> const & try_edge_face_pair = map_edge_faces[edge_number];
							if (new_face_edge_vertexes[0] == try_edge.vertexes[0] &&
									new_face_edge_vertexes[1] == try_edge.vertexes[1] &&
									try_edge_face_pair[0].face_number == SIZE_MAX &&
									(try_edge_face_pair[1].face_number == SIZE_MAX ||
											try_edge_face_pair[1].face_contents == face_contents)) {
								new_face_surfedge = ptrdiff_t(edge_number);
								break;
							}
							if (new_face_edge_vertexes[0] == try_edge.vertexes[1] &&
									new_face_edge_vertexes[1] == try_edge.vertexes[0] &&
									try_edge_face_pair[1].face_number == SIZE_MAX &&
									(try_edge_face_pair[0].face_number == SIZE_MAX ||
											try_edge_face_pair[0].face_contents == face_contents)) {
								new_face_surfedge = -ptrdiff_t(edge_number);
								break;
							}
						}
						if (!new_face_surfedge) {
							new_face_surfedge = ptrdiff_t(edges.size());
							edge & new_map_edge = edges.emplace_back();
							new_map_edge.vertexes[0] = uint16_t(new_face_edge_vertexes[0]);
							new_map_edge.vertexes[1] = uint16_t(new_face_edge_vertexes[1]);
							map_edge_faces.emplace_back();
						}
						edge_face & new_face_edge_face =
								map_edge_faces[std::abs(new_face_surfedge)][size_t(new_face_surfedge < 0)];
						new_face_edge_face.face_number = new_face_number;
						new_face_edge_face.is_new_face_number = true;
						new_face_edge_face.face_contents = face_contents;
						surfedges.push_back(surfedge(new_face_surfedge));
					}
					std::fill(face.styles, face.styles + max_lightmaps, UINT8_MAX);
					face.lighting_offset = UINT32_MAX;
					++new_face_numbers_and_counts.back().second;
				}
			}

			if (faces_changed) {
				faces = std::move(new_faces);

				for (id_node & node : nodes) {
					size_t node_new_face_count = 0;
					for (size_t node_face_number = 0; node_face_number < node.face_count; ++node_face_number) {
						node_new_face_count += new_face_numbers_and_counts[node.first_face + node_face_number].second;
					}
					node.face_count = uint16_t(node_new_face_count);
					node.first_face = uint16_t(new_face_numbers_and_counts[node.first_face].first);
				}

				for (id_model & model : models) {
					size_t model_new_face_count = 0;
					for (size_t model_face_number = 0; model_face_number < model.face_count; ++model_face_number) {
						model_new_face_count +=
								new_face_numbers_and_counts[model.first_face + model_face_number].second;
					}
					model.face_count = uint16_t(model_new_face_count);
					model.first_face = uint16_t(new_face_numbers_and_counts[model.first_face].first);
				}

				std::vector<id_marksurface> old_marksurfaces = std::move(marksurfaces);
				marksurfaces.clear();
				marksurfaces.reserve(old_marksurfaces.size());
				std::vector<std::pair<size_t, size_t>> new_marksurface_numbers_and_counts;
				new_marksurface_numbers_and_counts.reserve(old_marksurfaces.size());
				for (id_marksurface const old_marksurface : old_marksurfaces) {
					std::pair<size_t, size_t> const new_face_number_and_count =
							new_face_numbers_and_counts[old_marksurface];
					new_marksurface_numbers_and_counts.emplace_back(
							marksurfaces.size(), new_face_number_and_count.second);
					for (size_t new_marksurface_face_number = 0;
							new_marksurface_face_number < new_face_number_and_count.second;
							++new_marksurface_face_number) {
						marksurfaces.push_back(
								id_marksurface(new_face_number_and_count.first + new_marksurface_face_number));
					}
				}

				for (id_leaf & leaf : leafs) {
					size_t leaf_new_marksurface_count = 0;
					for (size_t leaf_marksurface_number = 0;
							leaf_marksurface_number < leaf.marksurface_count; ++leaf_marksurface_number) {
						leaf_new_marksurface_count +=
								new_marksurface_numbers_and_counts[
										leaf.first_marksurface + leaf_marksurface_number].second;
					}
					leaf.marksurface_count = uint16_t(leaf_new_marksurface_count);
					leaf.first_marksurface = uint16_t(new_marksurface_numbers_and_counts[leaf.first_marksurface].first);
				}
			}
		}

		// Add a single-value lightmap to turbulent textures - needed for the PS2 engine, otherwise they're displayed as
		// black, unlike on the PC Half-Life engine, but for consistency between id > Gearbox and id > Valve > Gearbox
		// conversion, add the lightmap even if just upgrading from id to Valve.
		{
			std::vector<bool> textures_turbulent_bright(texture_count);
			for (size_t texture_number = 0; texture_number < textures.size(); ++texture_number) {
				// If the texture has any bright pixels (with one shade in the original Quake colormap), consider it
				// bright. Quake maps always have all textures included, the engine doesn't load them from WADs.
				id_texture_deserialized const & texture = textures[texture_number];
				if (texture.empty() || texture.name.c_str()[0] != '*' || !texture.pixels) {
					continue;
				}
				size_t const texture_size = size_t(texture.width) * size_t(texture.height);
				for (size_t texture_pixel_number = 0; texture_pixel_number < texture_size; ++texture_pixel_number) {
					if ((*texture.pixels)[texture_pixel_number] >= 0xE0) {
						textures_turbulent_bright[texture_number] = true;
						break;
					}
				}
			}
			// Corresponds to the identity Quake colormap value (turbulent surfaces are drawn without lighting in
			// Quake).
			// Rows 31 and 32 have the same contents in the original Quake colormap.
			// The formula for the colormap row index is ((255 * 256 - lightmap * scale) >> (8 - colormap bits)) >> 8.
			// Assuming that the lightmap scale is 256, 0x7F results in the row 32, and 0x80 results in the row 31.
			// GLQuake, however, uses the min((lightmap * scale) >> 7, 255) formula, and with the scale 256, 0x80
			// becomes 256 clamped to 255 (maximum lighting in GLQuake, which doesn't support overbright lighting),
			// while 0x7F becomes 254.
			constexpr uint8_t turbulent_lighting_value = 0x80;
			// Make lava bright when converted to Half-Life.
			// Similar to the default qrad behavior without -noclamp (clamping to 192).
			constexpr uint8_t turbulent_lighting_value_bright = 0xC0;
			size_t turbulent_lightmap_size = 0, turbulent_lightmap_size_bright = 0;
			std::vector<size_t> turbulent_lightmap_faces, turbulent_lightmap_faces_bright;
			for (size_t face_number = 0; face_number < faces.size(); ++face_number) {
				id_face & face = faces[face_number];
				id_texinfo const & face_texinfo = texinfo[face.texinfo_number];
				uint32_t const face_texture_number = face_texinfo.texture_number;
				id_texture_deserialized const & texture = textures[face_texture_number];
				if (texture.empty() || texture.name.c_str()[0] != '*') {
					continue;
				}
				int16_t face_extents[2];
				face.calculate_extents(
						face_texinfo, surfedges.data(), edges.data(), vertexes.data(), nullptr, face_extents);
				if (face_extents[0] < 0 || face_extents[1] < 0) {
					continue;
				}
				bool const face_texture_is_bright = textures_turbulent_bright[face_texture_number];
				size_t const face_lightmap_size =
						size_t((face_extents[0] >> 4) + 1) * size_t((face_extents[1] >> 4) + 1);
				if (face_texture_is_bright) {
					turbulent_lightmap_size_bright = std::max(turbulent_lightmap_size_bright, face_lightmap_size);
					turbulent_lightmap_faces_bright.push_back(face_number);
				} else {
					turbulent_lightmap_size = std::max(turbulent_lightmap_size, face_lightmap_size);
					turbulent_lightmap_faces.push_back(face_number);
				}
				// Use only one lightmap, and disregard light style animation.
				face.styles[0] = 0;
				std::fill(face.styles + 1, face.styles + max_lightmaps, UINT8_MAX);
			}
			if (turbulent_lightmap_size) {
				for (size_t const face_number : turbulent_lightmap_faces) {
					faces[face_number].lighting_offset = uint32_t(lighting.size());
				}
				lighting.resize(lighting.size() + turbulent_lightmap_size, turbulent_lighting_value);
			}
			if (turbulent_lightmap_size_bright) {
				for (size_t const face_number : turbulent_lightmap_faces_bright) {
					faces[face_number].lighting_offset = uint32_t(lighting.size());
				}
				lighting.resize(lighting.size() + turbulent_lightmap_size_bright, turbulent_lighting_value_bright);
			}
		}

		// Upgrade turbulent texture names, and gather the special textures and update the special flag in texinfo.
		{
			std::vector<bool> textures_special(texture_count);
			for (size_t texture_number = 0; texture_number < texture_count; ++texture_number) {
				id_texture_deserialized const & texture = textures[texture_number];
				if (texture.empty()) {
					continue;
				}
				std::string & texture_name = textures[texture_number].name;
				if (texture_name.c_str()[0] == '*') {
					texture_name[0] = '!';
				}
				textures_special[texture_number] = is_id_texture_special(texture_name.c_str());
			}
			for (id_texinfo & current_texinfo : texinfo) {
				if (textures_special[current_texinfo.texture_number]) {
					current_texinfo.flags |= id_texinfo_flag_special;
				} else {
					current_texinfo.flags &= ~uint32_t(id_texinfo_flag_special);
				}
			}
		}
	}

	// Upgrade lighting from luminance to RGB.
	for (id_face & face : faces) {
		if (face.lighting_offset != UINT32_MAX) {
			face.lighting_offset *= 3;
		}
	}
	size_t const lighting_luminance_count = lighting.size();
	lighting.resize(3 * lighting_luminance_count);
	for (size_t lighting_reverse_index = 0;
			lighting_reverse_index < lighting_luminance_count;
			++lighting_reverse_index) {
		uint32_t const lighting_index = lighting_luminance_count - 1 - lighting_reverse_index;
		uint8_t const lighting_luminance = lighting[lighting_index];
		size_t const lighting_rgb_offset = 3 * size_t(lighting_index);
		lighting[lighting_rgb_offset] = lighting_luminance;
		lighting[lighting_rgb_offset + 1] = lighting_luminance;
		lighting[lighting_rgb_offset + 2] = lighting_luminance;
	}
}

bool id_map::remove_nodraw() {
	if (textures.empty()) {
		// No textures, all checkerboards, texinfo texture numbers are ignored.
		return false;
	}

	// Skip the faces referencing nodraw textures and the dependencies of those faces.
	// Edges, vertexes may be reused by different faces potentially not nodraw, not removing them.
	// Moving only backwards in a forward loop.

	std::vector<size_t> texture_new_numbers;
	texture_new_numbers.reserve(textures.size());
	size_t texture_new_count = 0;
	for (id_texture_deserialized const & texture : textures) {
		texture_new_numbers.push_back(
				(!texture.empty() && !bs2pc_strncasecmp(texture.name.c_str(), "nodraw", sizeof("nodraw") - 1))
						? SIZE_MAX
						: texture_new_count++);
	}
	if (texture_new_count == textures.size()) {
		// No nodraw textures in the map.
		return false;
	}
	for (size_t texture_old_number = 0; texture_old_number < textures.size(); ++texture_old_number) {
		size_t const texture_new_number = texture_new_numbers[texture_old_number];
		if (texture_new_number != SIZE_MAX && texture_new_number != texture_old_number) {
			textures[texture_new_number] = std::move(textures[texture_old_number]);
		}
	}
	textures.resize(texture_new_count);

	std::vector<size_t> texinfo_new_numbers;
	texinfo_new_numbers.reserve(texinfo.size());
	size_t texinfo_new_count = 0;
	for (id_texinfo const & face_texinfo : texinfo) {
		texinfo_new_numbers.push_back(
				(texture_new_numbers[face_texinfo.texture_number] == SIZE_MAX)
						? SIZE_MAX
						: texinfo_new_count++);
	}
	for (size_t texinfo_old_number = 0; texinfo_old_number < texinfo.size(); ++texinfo_old_number) {
		size_t const texinfo_new_number = texinfo_new_numbers[texinfo_old_number];
		if (texinfo_new_number != SIZE_MAX) {
			id_texinfo & face_texinfo = texinfo[texinfo_new_number];
			if (texinfo_new_number != texinfo_old_number) {
				face_texinfo = std::move(texinfo[texinfo_old_number]);
			}
			face_texinfo.texture_number = uint32_t(texture_new_numbers[face_texinfo.texture_number]);
		}
	}
	texinfo.resize(texinfo_new_count);

	std::vector<size_t> face_new_numbers;
	face_new_numbers.reserve(faces.size());
	size_t face_new_count = 0;
	for (id_face const & face : faces) {
		face_new_numbers.push_back(
				(texinfo_new_numbers[face.texinfo_number] == SIZE_MAX)
						? SIZE_MAX
						: face_new_count++);
	}
	for (size_t face_old_number = 0; face_old_number < faces.size(); ++face_old_number) {
		size_t const face_new_number = face_new_numbers[face_old_number];
		if (face_new_number != SIZE_MAX) {
			id_face & face = faces[face_new_number];
			if (face_new_number != face_old_number) {
				face = std::move(faces[face_old_number]);
			}
			face.texinfo_number = uint16_t(texinfo_new_numbers[face.texinfo_number]);
		}
	}
	faces.resize(face_new_count);

	std::vector<size_t> marksurface_new_numbers;
	marksurface_new_numbers.reserve(marksurfaces.size());
	size_t marksurface_new_count = 0;
	for (id_marksurface const & marksurface : marksurfaces) {
		marksurface_new_numbers.push_back(
				(face_new_numbers[marksurface] == SIZE_MAX)
						? SIZE_MAX
						: marksurface_new_count++);
	}
	for (size_t marksurface_old_number = 0; marksurface_old_number < marksurfaces.size(); ++marksurface_old_number) {
		size_t const marksurface_new_number = marksurface_new_numbers[marksurface_old_number];
		if (marksurface_new_number != SIZE_MAX) {
			marksurfaces[marksurface_new_number] =
					id_marksurface(face_new_numbers[marksurfaces[marksurface_old_number]]);
		}
	}
	marksurfaces.resize(marksurface_new_count);

	for (auto leaf_iterator = leafs.begin(); leaf_iterator != leafs.end(); ++leaf_iterator) {
		id_leaf & leaf = *leaf_iterator;
		size_t leaf_new_first_marksurface = SIZE_MAX;
		size_t leaf_new_marksurface_count = 0;
		for (size_t leaf_marksurface_number = 0;
				leaf_marksurface_number < leaf.marksurface_count;
				++leaf_marksurface_number) {
			size_t leaf_new_marksurface = marksurface_new_numbers[leaf.first_marksurface + leaf_marksurface_number];
			if (leaf_new_marksurface == SIZE_MAX) {
				continue;
			}
			if (leaf_new_first_marksurface == SIZE_MAX) {
				leaf_new_first_marksurface = leaf_new_marksurface;
			}
			++leaf_new_marksurface_count;
		}
		if (leaf_new_first_marksurface != SIZE_MAX) {
			leaf.first_marksurface = uint16_t(leaf_new_first_marksurface);
		} else {
			if (leaf_iterator != leafs.begin()) {
				// Similar to what WriteDrawLeaf does in qbsp2
				// (initializes firstmarksurface of the new leaf to the total nummarksurfaces after the previous one).
				id_leaf const & previous_leaf = *std::prev(leaf_iterator);
				leaf.first_marksurface = previous_leaf.first_marksurface + previous_leaf.marksurface_count;
			} else {
				leaf.first_marksurface = 0;
			}
		}
		leaf.marksurface_count = uint16_t(leaf_new_marksurface_count);
	}

	for (auto node_iterator = nodes.begin(); node_iterator != nodes.end(); ++node_iterator) {
		id_node & node = *node_iterator;
		size_t node_new_first_face = SIZE_MAX;
		size_t node_new_face_count = 0;
		for (size_t node_face_number = 0; node_face_number < node.face_count; ++node_face_number) {
			size_t node_new_face = face_new_numbers[node.first_face + node_face_number];
			if (node_new_face == SIZE_MAX) {
				continue;
			}
			if (node_new_first_face == SIZE_MAX) {
				node_new_first_face = node_new_face;
			}
			++node_new_face_count;
		}
		if (node_new_first_face != SIZE_MAX) {
			node.first_face = uint16_t(node_new_first_face);
		} else {
			if (node_iterator != nodes.begin()) {
				// Similar to what WriteDrawNodes_r does in qbsp2
				// (initializes firstface of the new node to the total numfaces after the previous one).
				id_node const & previous_node = *std::prev(node_iterator);
				node.first_face = previous_node.first_face + previous_node.face_count;
			} else {
				node.first_face = 0;
			}
		}
		node.face_count = uint16_t(node_new_face_count);
	}

	for (auto model_iterator = models.begin(); model_iterator != models.end(); ++model_iterator) {
		id_model & model = *model_iterator;
		size_t model_new_first_face = SIZE_MAX;
		size_t model_new_face_count = 0;
		for (size_t model_face_number = 0; model_face_number < model.face_count; ++model_face_number) {
			size_t model_new_face = face_new_numbers[model.first_face + model_face_number];
			if (model_new_face == SIZE_MAX) {
				continue;
			}
			if (model_new_first_face == SIZE_MAX) {
				model_new_first_face = model_new_face;
			}
			++model_new_face_count;
		}
		if (model_new_first_face != SIZE_MAX) {
			model.first_face = uint16_t(model_new_first_face);
		} else {
			if (model_iterator != models.begin()) {
				// Similar to what ProcessModel does in qbsp2
				// (initializes firstface of the new model to the total numfaces after the previous one).
				id_model const & previous_model = *std::prev(model_iterator);
				model.first_face = previous_model.first_face + previous_model.face_count;
			} else {
				model.first_face = 0;
			}
		}
		model.face_count = uint16_t(model_new_face_count);
	}

	return true;
}

}
