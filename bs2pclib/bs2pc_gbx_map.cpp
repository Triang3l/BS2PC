#include "bs2pclib.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

namespace bs2pc {

float gbx_face_texinfo_vectors_area(vector3 const s, vector3 const t) {
	// Same range as in the original maps - anything below 0.01 (but not 0.01 itself) changed to 1, clamped to 128.
	float const area =
			std::sqrt(s.v[0] * s.v[0] + s.v[1] * s.v[1] + s.v[2] * s.v[2]) *
			std::sqrt(t.v[0] * t.v[0] + t.v[1] * t.v[1] + t.v[2] * t.v[2]);
	if (!(area >= 0.01f)) {
		return 1.0f;
	}
	// Clamped to 128 in the original maps.
	return std::min(128.0f, area);
}

char const * gbx_map::deserialize_textures(
		void const * const map, size_t const map_size,
		size_t const textures_offset, size_t const textures_lump_length, size_t const texture_count,
		palette_set const & quake_palette) {
	textures.clear();
	if (!texture_count) {
		return nullptr;
	}
	if (textures_offset > map_size || map_size - textures_offset < textures_lump_length) {
		return "The textures lump is out of bounds";
	}
	if (texture_count > textures_lump_length / sizeof(gbx_texture)) {
		return "The number of textures exceeds the lump length";
	}
	char const * textures_lump_data = reinterpret_cast<char const *>(map) + textures_offset;
	textures.resize(texture_count);
	for (uint32_t texture_number = 0; texture_number < texture_count; ++texture_number) {
		gbx_texture_deserialized & texture_deserialized = textures[texture_number];
		char const * const texture_deserialize_error = texture_deserialized.deserialize_with_anim_offsets(
				map, map_size, textures_offset + sizeof(gbx_texture) * texture_number, true, quake_palette);
		if (texture_deserialize_error) {
			return texture_deserialize_error;
		}
		// Convert from texture offsets to texture numbers.
		if (texture_deserialized.anim_next != UINT32_MAX) {
			if (texture_deserialized.anim_next < textures_offset) {
				return "The offset of the next texture in an animation sequence is outside the textures lump";
			}
			texture_deserialized.anim_next -= textures_offset;
			if (texture_deserialized.anim_next % sizeof(gbx_texture)) {
				return "The offset of the next texture in an animation sequence is not a multiple of the texture size";
			}
			texture_deserialized.anim_next /= sizeof(gbx_texture);
			if (texture_deserialized.anim_next >= texture_count) {
				return "The offset of the next texture in an animation sequence is beyond the texture count";
			}
		}
		if (texture_deserialized.alternate_anims != UINT32_MAX) {
			if (texture_deserialized.alternate_anims < textures_offset) {
				return "The offset of an alternate texture animation sequence is outside the textures lump";
			}
			texture_deserialized.alternate_anims -= textures_offset;
			if (texture_deserialized.alternate_anims % sizeof(gbx_texture)) {
				return "The offset of an alternate texture animation sequence is not a multiple of the texture size";
			}
			texture_deserialized.alternate_anims /= sizeof(gbx_texture);
			if (texture_deserialized.alternate_anims >= texture_count) {
				return "The offset of an alternate texture animation sequence is beyond the texture count";
			}
		}
	}
	return nullptr;
}

char const * gbx_map::deserialize(void const * const map, size_t const map_size, palette_set const & quake_palette) {
	// Version and lumps (arrays of offsets, lengths, counts, and then unknown - zeros - for lumps).
	std::array<uint32_t, gbx_lump_count> lump_offsets, lump_lengths, lump_counts;
	{
		if (map_size < sizeof(uint32_t) + sizeof(uint32_t) * gbx_lump_count * 4) {
			return "Map version and lumps are out of bounds";
		}
		uint32_t version;
		std::memcpy(&version, map, sizeof(uint32_t));
		if (version != gbx_map_version) {
			return "Map has the wrong version number";
		}
		std::memcpy(
				lump_offsets.data(),
				reinterpret_cast<char const *>(map) + sizeof(uint32_t),
				sizeof(uint32_t) * gbx_lump_count);
		std::memcpy(
				lump_lengths.data(),
				reinterpret_cast<char const *>(map) + sizeof(uint32_t) * (1 + gbx_lump_count),
				sizeof(uint32_t) * gbx_lump_count);
		std::memcpy(
				lump_counts.data(),
				reinterpret_cast<char const *>(map) + sizeof(uint32_t) * (1 + gbx_lump_count * 2),
				sizeof(uint32_t) * gbx_lump_count);
		for (size_t lump_number = 0; lump_number < gbx_lump_count; ++lump_number) {
			if (lump_lengths[lump_number] &&
					(lump_offsets[lump_number] > map_size ||
							map_size - lump_offsets[lump_number] < lump_lengths[lump_number])) {
				return "Lump is out of bounds";
			}
		}
	}

	// Planes.
	{
		uint32_t const plane_count = lump_counts[gbx_lump_number_planes];
		if (plane_count > lump_lengths[gbx_lump_number_planes] / sizeof(gbx_plane)) {
			return "The number of planes exceeds the lump length";
		}
		planes.clear();
		if (plane_count) {
			planes.resize(plane_count);
			std::memcpy(
					planes.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_planes],
					sizeof(gbx_plane) * plane_count);
		}
	}

	// Nodes.
	{
		uint32_t const node_count = lump_counts[gbx_lump_number_nodes];
		if (node_count > lump_lengths[gbx_lump_number_nodes] / sizeof(gbx_node)) {
			return "The number of nodes exceeds the lump length";
		}
		nodes.clear();
		if (node_count) {
			nodes.resize(node_count);
			uint32_t const nodes_offset = lump_offsets[gbx_lump_number_nodes];
			std::memcpy(
					nodes.data(),
					reinterpret_cast<char const *>(map) + nodes_offset,
					sizeof(gbx_node) * node_count);
			// Convert the offsets to indexes.
			uint32_t const planes_offset = lump_offsets[gbx_lump_number_planes];
			uint32_t const plane_count = lump_counts[gbx_lump_number_planes];
			uint32_t const leafs_offset = lump_offsets[gbx_lump_number_leafs];
			uint32_t const leaf_count = lump_counts[gbx_lump_number_leafs];
			for (gbx_node & node : nodes) {
				// Parent node offset to parent node number.
				if (node.parent != UINT32_MAX) {
					if (node.parent < nodes_offset) {
						return "The parent node offset of a node is outside the nodes lump";
					}
					node.parent -= nodes_offset;
					if (node.parent % sizeof(gbx_node)) {
						return "The parent node offset of a node is not a multiple of the node size";
					}
					node.parent /= sizeof(gbx_node);
					if (node.parent >= node_count) {
						return "The parent node offset of a node is beyond the node count";
					}
				}
				// Plane offset to plane number.
				if (node.plane < planes_offset) {
					return "The node plane offset is outside the planes lump";
				}
				node.plane -= planes_offset;
				if (node.plane % sizeof(gbx_plane)) {
					return "The node plane offset is not a multiple of the plane size";
				}
				node.plane /= sizeof(gbx_plane);
				if (node.plane >= plane_count) {
					return "The node plane offset is beyond the plane count";
				}
				// Children offsets to node numbers (non-negative) or leaf numbers (negative).
				for (size_t child_number = 0; child_number < 2; ++child_number) {
					uint32_t const child_offset = uint32_t(node.children[child_number]);
					if (child_offset >= nodes_offset && (child_offset - nodes_offset) / sizeof(gbx_node) < node_count) {
						// The child is a node.
						uint32_t const child_node_offset = child_offset - nodes_offset;
						if (child_node_offset % sizeof(gbx_node)) {
							return "The node child offset is not a multiple of the node size";
						}
						node.children[child_number] = int32_t(child_node_offset / sizeof(gbx_node));
					} else if (child_offset >= leafs_offset &&
							(child_offset - leafs_offset) / sizeof(gbx_leaf) < leaf_count) {
						// The child is a leaf.
						uint32_t const child_leaf_offset = child_offset - leafs_offset;
						if (child_leaf_offset % sizeof(gbx_leaf)) {
							return "The node child offset is not a multiple of the leaf size";
						}
						node.children[child_number] = INT32_C(-1) - int32_t(child_leaf_offset / sizeof(gbx_leaf));
					} else {
						return "The node child is neither a node nor a leaf";
					}
				}
			}
		}
	}

	// Leafs.
	{
		uint32_t const leaf_count = lump_counts[gbx_lump_number_leafs];
		if (leaf_count > lump_lengths[gbx_lump_number_leafs] / sizeof(gbx_leaf)) {
			return "The number of leafs exceeds the lump length";
		}
		leafs.clear();
		if (leaf_count) {
			leafs.resize(leaf_count);
			std::memcpy(
					leafs.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_leafs],
					sizeof(gbx_leaf) * leaf_count);
			// Convert the offsets to indexes or relative offsets.
			uint32_t const nodes_offset = lump_offsets[gbx_lump_number_nodes];
			uint32_t const node_count = lump_counts[gbx_lump_number_nodes];
			uint32_t const visibility_offset = lump_offsets[gbx_lump_number_visibility];
			uint32_t const visibility_length = lump_lengths[gbx_lump_number_visibility];
			for (gbx_leaf & leaf : leafs) {
				// Parent node offset to parent node index.
				if (leaf.parent != UINT32_MAX) {
					if (leaf.parent < nodes_offset) {
						return "The parent node offset of a leaf is outside the nodes lump";
					}
					leaf.parent -= nodes_offset;
					if (leaf.parent % sizeof(gbx_node)) {
						return "The parent node offset of a leaf is not a multiple of the node size";
					}
					leaf.parent /= sizeof(gbx_node);
					if (leaf.parent >= node_count) {
						return "The parent node offset of a leaf is beyond the node count";
					}
				}
				// Visibility (may be empty, for instance, for the leaf 0, so expecting it to be just <= length,
				// not strictly < length).
				if (leaf.visibility_offset != UINT32_MAX) {
					if (leaf.visibility_offset < visibility_offset) {
						return "The visibility offset of a leaf is outside the visibility lump";
					}
					leaf.visibility_offset -= visibility_offset;
					if (leaf.visibility_offset > visibility_length) {
						return "The visibility offset of a leaf is beyond the visibility lump length";
					}
				}
			}
		}
	}

	// Edges.
	{
		uint32_t const edge_count = lump_counts[gbx_lump_number_edges];
		if (edge_count > lump_lengths[gbx_lump_number_edges] / sizeof(edge)) {
			return "The number of edges exceeds the lump length";
		}
		edges.clear();
		if (edge_count) {
			edges.resize(edge_count);
			std::memcpy(
					edges.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_edges],
					sizeof(edge) * edge_count);
		}
	}

	// Surfedges.
	{
		uint32_t const surfedge_count = lump_counts[gbx_lump_number_surfedges];
		if (surfedge_count > lump_lengths[gbx_lump_number_surfedges] / sizeof(surfedge)) {
			return "The number of surfedges exceeds the lump length";
		}
		surfedges.clear();
		if (surfedge_count) {
			surfedges.resize(surfedge_count);
			std::memcpy(
					surfedges.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_surfedges],
					sizeof(surfedge) * surfedge_count);
		}
	}

	// Vertexes.
	{
		uint32_t const vertex_count = lump_counts[gbx_lump_number_vertexes];
		if (vertex_count > lump_lengths[gbx_lump_number_vertexes] / sizeof(vector4)) {
			return "The number of vertexes exceeds the lump length";
		}
		vertexes.clear();
		if (vertex_count) {
			vertexes.resize(vertex_count);
			std::memcpy(
					vertexes.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_vertexes],
					sizeof(vector4) * vertex_count);
		}
	}

	// Drawing hull as clipping hull (hull 0).
	{
		uint32_t const hull_0_clipnode_count = lump_counts[gbx_lump_number_hull_0];
		if (hull_0_clipnode_count > lump_lengths[gbx_lump_number_hull_0] / sizeof(clipnode)) {
			return "The number of hull 0 clipnodes exceeds the lump length";
		}
		hull_0.clear();
		if (hull_0_clipnode_count) {
			hull_0.resize(hull_0_clipnode_count);
			std::memcpy(
					hull_0.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_hull_0],
					sizeof(clipnode) * hull_0_clipnode_count);
		}
	}

	// Clipnodes.
	{
		uint32_t const clipnode_count = lump_counts[gbx_lump_number_clipnodes];
		if (clipnode_count > lump_lengths[gbx_lump_number_clipnodes] / sizeof(clipnode)) {
			return "The number of clipnodes exceeds the lump length";
		}
		clipnodes.clear();
		if (clipnode_count) {
			clipnodes.resize(clipnode_count);
			std::memcpy(
					clipnodes.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_clipnodes],
					sizeof(clipnode) * clipnode_count);
		}
	}

	// Models.
	{
		uint32_t const model_count = lump_counts[gbx_lump_number_models];
		if (model_count > lump_lengths[gbx_lump_number_models] / sizeof(gbx_model)) {
			return "The number of models exceeds the lump length";
		}
		models.clear();
		if (model_count) {
			models.resize(model_count);
			std::memcpy(
					models.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_models],
					sizeof(gbx_model) * model_count);
		}
	}

	// Faces.
	// Before polygons because polygons deserialization will set the polygon indexes in the faces.
	uint32_t face_count_with_polygons = 0;
	{
		uint32_t const face_count = lump_counts[gbx_lump_number_faces];
		if (face_count > lump_lengths[gbx_lump_number_faces] / sizeof(gbx_face)) {
			return "The number of faces exceeds the lump length";
		}
		faces.clear();
		if (face_count) {
			faces.resize(face_count);
			std::memcpy(
					faces.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_faces],
					sizeof(gbx_face) * face_count);
			// Convert the offsets to indexes.
			uint32_t const textures_offset = lump_offsets[gbx_lump_number_textures];
			uint32_t const texture_count = lump_counts[gbx_lump_number_textures];
			uint32_t const lighting_offset = lump_offsets[gbx_lump_number_lighting];
			uint32_t const lighting_length = lump_lengths[gbx_lump_number_lighting];
			uint32_t const planes_offset = lump_offsets[gbx_lump_number_planes];
			uint32_t const plane_count = lump_counts[gbx_lump_number_planes];
			for (gbx_face & face : faces) {
				// Texture.
				if (face.texture < textures_offset) {
					return "The texture offset of a face is outside the textures lump";
				}
				face.texture -= textures_offset;
				if (face.texture % sizeof(gbx_texture)) {
					return "The texture offset of a face is not a multiple of the texture size";
				}
				face.texture /= sizeof(gbx_texture);
				if (face.texture >= texture_count) {
					return "The texture offset of a face is beyond the texture count";
				}
				// Lighting (may be empty, so expecting it to be just <= length, not strictly < length).
				if (face.lighting_offset != UINT32_MAX) {
					if (face.lighting_offset < lighting_offset) {
						return "The lighting offset of a face is outside the lighting lump";
					}
					face.lighting_offset -= lighting_offset;
					if (face.lighting_offset > lighting_length) {
						return "The lighting offset of a face is beyond the lighting lump length";
					}
				}
				// Plane.
				if (face.plane < planes_offset) {
					return "The plane offset of a face is outside the planes lump";
				}
				face.plane -= planes_offset;
				if (face.plane % sizeof(gbx_plane)) {
					return "The plane offset of a face is not a multiple of the plane size";
				}
				face.plane /= sizeof(gbx_plane);
				if (face.plane >= plane_count) {
					return "The plane offset of a face is beyond the plane count";
				}
				// Polygons will be linked later since polygons contain face numbers, just count for validation.
				if (face.polygons != UINT32_MAX) {
					++face_count_with_polygons;
				}
			}
		}
	}

	// Marksurfaces.
	{
		uint32_t const marksurface_count = lump_counts[gbx_lump_number_marksurfaces];
		if (marksurface_count > lump_lengths[gbx_lump_number_marksurfaces] / sizeof(gbx_marksurface)) {
			return "The number of marksurfaces exceeds the lump length";
		}
		marksurfaces.clear();
		if (marksurface_count) {
			marksurfaces.resize(marksurface_count);
			std::memcpy(
					marksurfaces.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_marksurfaces],
					sizeof(gbx_marksurface) * marksurface_count);
		}
	}

	// Visibility.
	// The count is not stored, only the length.
	{
		uint32_t const visibility_length = lump_lengths[gbx_lump_number_visibility];
		visibility.clear();
		if (visibility_length) {
			visibility.resize(visibility_length);
			std::memcpy(
					visibility.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_visibility],
					visibility_length);
		}
	}

	// Lighting.
	// The count is not stored, only the length.
	{
		uint32_t const lighting_length = lump_lengths[gbx_lump_number_lighting];
		lighting.clear();
		if (lighting_length) {
			lighting.resize(lighting_length);
			std::memcpy(
					lighting.data(),
					reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_lighting],
					lighting_length);
		}
	}

	// Textures.
	{
		char const * const textures_deserialize_error = deserialize_textures(
				map, map_size,
				lump_offsets[gbx_lump_number_textures],
				lump_lengths[gbx_lump_number_textures],
				lump_counts[gbx_lump_number_textures],
				quake_palette);
		if (textures_deserialize_error) {
			return textures_deserialize_error;
		}
	}

	// Entities.
	// The count is not stored, only the length.
	{
		uint32_t const entities_length = lump_lengths[gbx_lump_number_entities];
		if (!entities_length) {
			return "The entities lump is empty";
		}
		uint32_t const entities_offset = lump_offsets[gbx_lump_number_entities];
		if (reinterpret_cast<char const *>(map)[entities_offset + entities_length - 1]) {
			return "The entities lump is not null-terminated";
		}
		entities = deserialize_entities(reinterpret_cast<char const *>(map) + entities_offset);
	}

	// Polygons.
	// After faces because the polygon numbers in the faces will be set.
	{
		uint32_t const polygon_count = lump_counts[gbx_lump_number_polygons];
		if (polygon_count != face_count_with_polygons) {
			return "The counts of faces with polygons and the polygons themselves don't match";
		}
		polygons.clear();
		polygons.resize(polygon_count);
		uint32_t const polygons_length = lump_lengths[gbx_lump_number_polygons];
		char const * const polygons_lump_data =
				reinterpret_cast<char const *>(map) + lump_offsets[gbx_lump_number_polygons];
		uint32_t polygons_current_offset = 0;
		uint32_t const polygons_offset = lump_offsets[gbx_lump_number_polygons];
		for (uint32_t polygon_number = 0; polygon_number < polygon_count; ++polygon_number) {
			uint32_t const polygon_map_offset = polygons_offset + polygons_current_offset;
			gbx_polygons_deserialized & polygon = polygons[polygon_number];

			// Face number and vertex count.
			uint32_t polygon_face_number, polygon_vertex_count;
			if (polygons_length - polygons_current_offset < sizeof(uint32_t) * 2) {
				return "Polygon face number and vertex count are stored out of bounds of the polygons lump";
			}
			std::memcpy(&polygon_face_number, polygons_lump_data + polygons_current_offset, sizeof(uint32_t));
			polygons_current_offset += sizeof(uint32_t);
			std::memcpy(&polygon_vertex_count, polygons_lump_data + polygons_current_offset, sizeof(uint32_t));
			polygons_current_offset += sizeof(uint32_t);

			// Link the face to the polygon.
			polygon.face_number = polygon_face_number;
			if (polygon.face_number >= faces.size()) {
				return "Polygons face number is beyond the face count";
			}
			gbx_face & polygon_face = faces[polygon.face_number];
			if (polygon_face.polygons != polygon_map_offset) {
				return "The face of polygons doesn't reference those polygons";
			}
			polygon_face.polygons = polygon_number;

			// Vertexes.
			if ((polygons_length - polygons_current_offset) / sizeof(gbx_polygon_vertex) < polygon_vertex_count) {
				return "Polygons vertexes are stored out of bounds of the polygons lump";
			}
			polygon.vertexes.resize(polygon_vertex_count);
			std::memcpy(
					polygon.vertexes.data(),
					polygons_lump_data + polygons_current_offset,
					sizeof(gbx_polygon_vertex) * polygon_vertex_count);
			polygons_current_offset += sizeof(gbx_polygon_vertex) * polygon_vertex_count;

			// Strips.
			uint32_t polygon_strip_count;
			if (polygons_length - polygons_current_offset < sizeof(uint32_t)) {
				return "Polygon strip count is stored out of bounds of the polygons lump";
			}
			std::memcpy(&polygon_strip_count, polygons_lump_data + polygons_current_offset, sizeof(uint32_t));
			polygons_current_offset += sizeof(uint32_t);
			polygon.strips.resize(polygon_strip_count);
			for (std::vector<uint16_t> & polygon_strip : polygon.strips) {
				uint16_t polygon_strip_vertex_count;
				if (polygons_length - polygons_current_offset < sizeof(uint16_t)) {
					return "Polygon strip vertex count is stored out of bounds of the polygons lump";
				}
				std::memcpy(
						&polygon_strip_vertex_count, polygons_lump_data + polygons_current_offset,
						sizeof(uint16_t));
				polygons_current_offset += sizeof(uint16_t);
				if ((polygons_length - polygons_current_offset) / sizeof(uint16_t) < polygon_strip_vertex_count) {
					return "Polygon strip vertex indexes are stored out of bounds of the polygons lump";
				}
				polygon_strip.resize(polygon_strip_vertex_count);
				std::memcpy(
						polygon_strip.data(),
						polygons_lump_data + polygons_current_offset,
						sizeof(uint16_t) * polygon_strip_vertex_count);
				polygons_current_offset += sizeof(uint16_t) * polygon_strip_vertex_count;
				// Skip the alignment padding.
				if (polygons_current_offset & (sizeof(uint32_t) - 1)) {
					uint32_t polygon_strip_alignment_padding =
							sizeof(uint32_t) - (polygons_current_offset & (sizeof(uint32_t) - 1));
					if (polygons_length - polygons_current_offset < polygon_strip_alignment_padding) {
						return "Polygon strip alignment padding is stored out of bounds of the polygons lump";
					}
					polygons_current_offset += polygon_strip_alignment_padding;
				}
			}
		}
	}

	return nullptr;
}

void gbx_map::serialize(std::vector<char> & map, palette_set const & quake_palette) const {
	map.clear();
	// As a result of the clear, all padding created by resizing will be zero-initialized.

	// Reserve aligned space for the header, and fill the unknown fourth array of per-lump values with zeros.
	map.resize(
			(sizeof(uint32_t) + sizeof(uint32_t) * gbx_lump_count * 4 + (gbx_lump_alignment - 1)) &
					~size_t(gbx_lump_alignment - 1));

	// Version.
	constexpr uint32_t version = gbx_map_version;
	std::memcpy(map.data(), &version, sizeof(uint32_t));

	// Write the lumps that don't have offsets depending on subsequent lumps, or allocate space for those that do.

	std::array<uint32_t, gbx_lump_count> lump_offsets{}, lump_lengths{}, lump_counts{};

	auto const finish_lump = [&map, &lump_offsets, &lump_lengths](gbx_lump_number lump_number) {
		size_t const map_current_size = map.size();
		lump_lengths[lump_number] = uint32_t(map_current_size - lump_offsets[lump_number]);
		// Align the end of the lump.
		map.resize(map_current_size + (gbx_lump_alignment - 1) & ~size_t(gbx_lump_alignment - 1));
	};

	// Planes.
	size_t const planes_offset = map.size();
	{
		size_t const plane_count = planes.size();
		lump_offsets[gbx_lump_number_planes] = uint32_t(planes_offset);
		lump_counts[gbx_lump_number_planes] = uint32_t(plane_count);
		if (plane_count) {
			map.resize(planes_offset + sizeof(gbx_plane) * plane_count);
			std::memcpy(map.data() + planes_offset, planes.data(), sizeof(gbx_plane) * plane_count);
		}
		finish_lump(gbx_lump_number_planes);
	}

	// Nodes - only allocate as leafs (as well as planes, preceding nodes) are needed.
	size_t const nodes_offset = map.size();
	size_t const node_count = nodes.size();
	{
		lump_offsets[gbx_lump_number_nodes] = uint32_t(nodes_offset);
		lump_counts[gbx_lump_number_nodes] = uint32_t(node_count);
		map.resize(nodes_offset + sizeof(gbx_node) * node_count);
		finish_lump(gbx_lump_number_nodes);
	}

	// Leafs - only allocate as visibility (as well as nodes, preceding leafs) is needed.
	size_t const leafs_offset = map.size();
	size_t const leaf_count = leafs.size();
	{
		lump_offsets[gbx_lump_number_leafs] = uint32_t(leafs_offset);
		lump_counts[gbx_lump_number_leafs] = uint32_t(leaf_count);
		map.resize(leafs_offset + sizeof(gbx_leaf) * leaf_count);
		finish_lump(gbx_lump_number_leafs);
	}

	// Edges.
	{
		size_t const edges_offset = map.size();
		size_t const edge_count = edges.size();
		lump_offsets[gbx_lump_number_edges] = uint32_t(edges_offset);
		lump_counts[gbx_lump_number_edges] = uint32_t(edge_count);
		if (edge_count) {
			map.resize(edges_offset + sizeof(edge) * edge_count);
			std::memcpy(map.data() + edges_offset, edges.data(), sizeof(edge) * edge_count);
		}
		finish_lump(gbx_lump_number_edges);
	}

	// Surfedges.
	{
		size_t const surfedges_offset = map.size();
		size_t const surfedge_count = surfedges.size();
		lump_offsets[gbx_lump_number_surfedges] = uint32_t(surfedges_offset);
		lump_counts[gbx_lump_number_surfedges] = uint32_t(surfedge_count);
		if (surfedge_count) {
			map.resize(surfedges_offset + sizeof(surfedge) * surfedge_count);
			std::memcpy(map.data() + surfedges_offset, surfedges.data(), sizeof(surfedge) * surfedge_count);
		}
		finish_lump(gbx_lump_number_surfedges);
	}

	// Vertexes.
	{
		size_t const vertexes_offset = map.size();
		size_t const vertex_count = vertexes.size();
		lump_offsets[gbx_lump_number_vertexes] = uint32_t(vertexes_offset);
		lump_counts[gbx_lump_number_vertexes] = uint32_t(vertex_count);
		if (vertex_count) {
			map.resize(vertexes_offset + sizeof(vector4) * vertex_count);
			std::memcpy(map.data() + vertexes_offset, vertexes.data(), sizeof(vector4) * vertex_count);
		}
		finish_lump(gbx_lump_number_vertexes);
	}

	// Drawing hull as clipping hull (hull 0).
	{
		size_t const hull_0_offset = map.size();
		size_t const hull_0_clipnode_count = hull_0.size();
		lump_offsets[gbx_lump_number_hull_0] = uint32_t(hull_0_offset);
		lump_counts[gbx_lump_number_hull_0] = uint32_t(hull_0_clipnode_count);
		if (hull_0_clipnode_count) {
			map.resize(hull_0_offset + sizeof(clipnode) * hull_0_clipnode_count);
			std::memcpy(map.data() + hull_0_offset, hull_0.data(), sizeof(clipnode) * hull_0_clipnode_count);
		}
		finish_lump(gbx_lump_number_hull_0);
	}

	// Clipnodes.
	{
		size_t const clipnodes_offset = map.size();
		size_t const clipnode_count = clipnodes.size();
		lump_offsets[gbx_lump_number_clipnodes] = uint32_t(clipnodes_offset);
		lump_counts[gbx_lump_number_clipnodes] = uint32_t(clipnode_count);
		if (clipnode_count) {
			map.resize(clipnodes_offset + sizeof(clipnode) * clipnode_count);
			std::memcpy(map.data() + clipnodes_offset, clipnodes.data(), sizeof(clipnode) * clipnode_count);
		}
		finish_lump(gbx_lump_number_clipnodes);
	}

	// Models.
	{
		size_t const models_offset = map.size();
		size_t const model_count = models.size();
		lump_offsets[gbx_lump_number_models] = uint32_t(models_offset);
		lump_counts[gbx_lump_number_models] = uint32_t(model_count);
		if (model_count) {
			map.resize(models_offset + sizeof(gbx_model) * model_count);
			std::memcpy(map.data() + models_offset, models.data(), sizeof(gbx_model) * model_count);
		}
		finish_lump(gbx_lump_number_models);
	}

	// Faces - only allocate as textures, lighting and polygons (as well as planes, preceding faces) are needed.
	size_t const faces_offset = map.size();
	size_t const face_count = faces.size();
	{
		lump_offsets[gbx_lump_number_faces] = uint32_t(faces_offset);
		lump_counts[gbx_lump_number_faces] = uint32_t(face_count);
		map.resize(faces_offset + sizeof(gbx_face) * face_count);
		finish_lump(gbx_lump_number_faces);
	}

	// Marksurfaces.
	{
		size_t const marksurfaces_offset = map.size();
		size_t const marksurface_count = marksurfaces.size();
		lump_offsets[gbx_lump_number_marksurfaces] = uint32_t(marksurfaces_offset);
		lump_counts[gbx_lump_number_marksurfaces] = uint32_t(marksurface_count);
		if (marksurface_count) {
			map.resize(marksurfaces_offset + sizeof(gbx_marksurface) * marksurface_count);
			std::memcpy(
					map.data() + marksurfaces_offset,
					marksurfaces.data(),
					sizeof(gbx_marksurface) * marksurface_count);
		}
		finish_lump(gbx_lump_number_marksurfaces);
	}

	// Visibility.
	// The count is not stored, only the length.
	size_t const visibility_offset = map.size();
	{
		size_t const visibility_length = visibility.size();
		lump_offsets[gbx_lump_number_visibility] = uint32_t(visibility_offset);
		if (visibility_length) {
			map.resize(visibility_offset + visibility_length);
			std::memcpy(map.data() + visibility_offset, visibility.data(), visibility_length);
		}
		finish_lump(gbx_lump_number_visibility);
	}

	// Lighting.
	// The count is not stored, only the length.
	size_t const lighting_offset = map.size();
	{
		size_t const lighting_length = lighting.size();
		lump_offsets[gbx_lump_number_lighting] = uint32_t(lighting_offset);
		if (lighting_length) {
			map.resize(lighting_offset + lighting_length);
			std::memcpy(map.data() + lighting_offset, lighting.data(), lighting_length);
		}
		finish_lump(gbx_lump_number_lighting);
	}

	// Textures.
	size_t const textures_offset = map.size();
	{
		size_t const texture_count = textures.size();
		lump_offsets[gbx_lump_number_textures] = uint32_t(textures_offset);
		lump_counts[gbx_lump_number_textures] = uint32_t(texture_count);
		if (texture_count) {
			// In the original maps, the textures lump contains first the texture information, then the pixels, then the
			// palettes.
			size_t const textures_pixels_offset = textures_offset + sizeof(gbx_texture) * texture_count;
			size_t textures_information_and_pixels_size = textures_pixels_offset;
			for (gbx_texture_deserialized const & texture : textures) {
				// texture.mip_levels doesn't include the base level.
				for (uint32_t texture_mip_level = 0; texture_mip_level <= texture.mip_levels; ++texture_mip_level) {
					uint32_t const texture_mip_width = texture.scaled_width >> texture_mip_level;
					uint32_t const texture_mip_height = texture.scaled_height >> texture_mip_level;
					if (!texture_mip_width || !texture_mip_height) {
						break;
					}
					textures_information_and_pixels_size += size_t(texture_mip_width) * size_t(texture_mip_height);
				}
			}
			map.resize(textures_information_and_pixels_size);
			size_t next_texture_pixels_offset = textures_pixels_offset;
			std::array<size_t, gbx_palette_type_count> texture_quake_palette_offsets;
			std::fill(
					texture_quake_palette_offsets.begin(),
					texture_quake_palette_offsets.end(),
					SIZE_MAX);
			std::array<size_t, gbx_palette_type_count> texture_checkerboard_palette_offsets;
			std::fill(
					texture_checkerboard_palette_offsets.begin(),
					texture_checkerboard_palette_offsets.end(),
					SIZE_MAX);
			for (size_t texture_number = 0; texture_number < texture_count; ++texture_number) {
				gbx_texture_deserialized const & texture = textures[texture_number];
				gbx_palette_type const texture_palette_type = gbx_texture_palette_type(texture.name.c_str());
				// Pixels.
				size_t const texture_pixels_offset = next_texture_pixels_offset;
				bool const texture_is_random = texture_palette_type == gbx_palette_type_random;
				size_t texture_mip_pixels_offset = 0;
				// texture.mip_levels doesn't include the base level.
				for (uint32_t texture_mip_level = 0; texture_mip_level <= texture.mip_levels; ++texture_mip_level) {
					uint32_t const texture_mip_width = texture.scaled_width >> texture_mip_level;
					uint32_t const texture_mip_height = texture.scaled_height >> texture_mip_level;
					if (!texture_mip_width || !texture_mip_height) {
						break;
					}
					if (!texture.pixels) {
						// Checkerboard.
						char * const texture_mip = map.data() + texture_pixels_offset + texture_mip_pixels_offset;
						uint32_t const checkerboard_cell_size = 8 >> texture_mip_level;
						uint32_t const checkerboard_two_cells_mask = (checkerboard_cell_size << 1) - 1;
						for (uint32_t texture_mip_y = 0; texture_mip_y < texture_mip_height; ++texture_mip_y) {
							for (uint32_t texture_mip_x = 0; texture_mip_x < texture_mip_width; ++texture_mip_x) {
								uint32_t checkerboard_y =
										texture_is_random
												? deinterleave_random_gbx_texture_y(texture_mip_y, texture_mip_height)
												: texture_mip_y;
								texture_mip[texture_mip_y * size_t(texture_mip_width) + texture_mip_x] = char(
										(((checkerboard_y & checkerboard_two_cells_mask) < checkerboard_cell_size) !=
										((texture_mip_x & checkerboard_two_cells_mask) < checkerboard_cell_size))
												? 0
												: 255);
							}
						}
					}
					texture_mip_pixels_offset += size_t(texture_mip_width) * size_t(texture_mip_height);
				}
				if (texture.pixels) {
					assert(texture_mip_pixels_offset <= texture.pixels->size());
					if (texture_is_random) {
						// Interleave the random-tiled texture.
						size_t texture_interleave_mip_offset = 0;
						for (uint32_t texture_mip_level = 0;
								texture_mip_level <= texture.mip_levels;
								++texture_mip_level) {
							uint32_t const texture_mip_width = texture.scaled_width >> texture_mip_level;
							uint32_t const texture_mip_height = texture.scaled_height >> texture_mip_level;
							if (!texture_mip_width || !texture_mip_height) {
								break;
							}
							for (uint32_t texture_mip_y = 0; texture_mip_y < texture_mip_height; ++texture_mip_y) {
								std::memcpy(
										map.data() + texture_pixels_offset + texture_interleave_mip_offset +
												size_t(texture_mip_width) * texture_mip_y,
										texture.pixels->data() + texture_interleave_mip_offset +
												size_t(texture_mip_width) *
														deinterleave_random_gbx_texture_y(
																texture_mip_y, texture_mip_height),
										texture_mip_width);
							}
							texture_interleave_mip_offset += size_t(texture_mip_width) * size_t(texture_mip_height);
						}
					} else {
						// Copy all mips at once.
						std::memcpy(
								map.data() + texture_pixels_offset,
								texture.pixels->data(),
								texture_mip_pixels_offset);
					}
				}
				next_texture_pixels_offset += texture_mip_pixels_offset;
				// Palette.
				// Fixed palettes (Quake, checkerboard) are written only once, for the first use.
				size_t texture_palette_offset = SIZE_MAX;
				char * const texture_palette = map.data() + texture_palette_offset;
				if (texture.pixels) {
					if (!texture.palette_id_indexed) {
						texture_palette_offset = texture_quake_palette_offsets[texture_palette_type];
					}
					if (texture_palette_offset == SIZE_MAX) {
						texture_palette_offset = map.size();
						if (!texture.palette_id_indexed) {
							texture_quake_palette_offsets[texture_palette_type] = texture_palette_offset;
						}
						map.resize(texture_palette_offset + 4 * 256);
						char * const texture_palette_serialized = map.data() + texture_palette_offset;
						gbx_texture_deserialized_palette const & texture_palette_id_indexed =
								texture.palette_id_indexed
										? *texture.palette_id_indexed
										: quake_palette.gbx_id_indexed[texture_palette_type];
						// Reordering of colors is done with the granularity of 8 colors.
						for (size_t color_number = 0; color_number < 256; color_number += 8) {
							std::memcpy(
									texture_palette_serialized +
											4 * size_t(convert_palette_color_number(uint8_t(color_number))),
									texture_palette_id_indexed.data() + 4 * color_number,
									4 * 8);
						}
					}
				} else {
					texture_palette_offset = texture_checkerboard_palette_offsets[texture_palette_type];
					if (texture_palette_offset == SIZE_MAX) {
						texture_palette_offset = map.size();
						texture_checkerboard_palette_offsets[texture_palette_type] = texture_palette_offset;
						map.resize(texture_palette_offset + 4 * 256);
						char * const texture_palette_serialized = map.data() + texture_palette_offset;
						if (texture_is_random) {
							// Inverted colors.
							for (uint32_t color_number = 0; color_number < 255; ++color_number) {
								texture_palette_serialized[4 * color_number] = 0x7F;
								texture_palette_serialized[4 * color_number + 1] = 0x7F;
								texture_palette_serialized[4 * color_number + 2] = 0x7F;
								texture_palette_serialized[4 * color_number + 3] = char(0x80);
							}
							texture_palette_serialized[4 * 255] = 0;
							texture_palette_serialized[4 * 255 + 1] = 0x7F;
							texture_palette_serialized[4 * 255 + 2] = 0;
							texture_palette_serialized[4 * 255 + 3] = char(0x80);
						} else {
							for (uint32_t color_number = 0; color_number < 255; ++color_number) {
								texture_palette_serialized[4 * color_number] = 0;
								texture_palette_serialized[4 * color_number + 1] = 0;
								texture_palette_serialized[4 * color_number + 2] = 0;
								texture_palette_serialized[4 * color_number + 3] = char(0x80);
							}
							if (is_gbx_palette_24_bit(texture_palette_type)) {
								if (texture_palette_type == gbx_palette_type_transparent) {
									texture_palette_serialized[4 * 255] = 0;
									texture_palette_serialized[4 * 255 + 1] = 0;
									texture_palette_serialized[4 * 255 + 2] = 0;
									texture_palette_serialized[4 * 255 + 3] = 0;
								} else {
									texture_palette_serialized[4 * 255] = char(0xFF);
									texture_palette_serialized[4 * 255 + 1] = 0;
									texture_palette_serialized[4 * 255 + 2] = char(0xFF);
									texture_palette_serialized[4 * 255 + 3] = char(0x80);
								}
							} else {
								texture_palette_serialized[4 * 255] = 0x7F;
								texture_palette_serialized[4 * 255 + 1] = 0;
								texture_palette_serialized[4 * 255 + 2] = 0x7F;
								texture_palette_serialized[4 * 255 + 3] = char(0x80);
							}
						}
					}
				}
				// Texture information.
				gbx_texture texture_serialized;
				texture_serialized.pixels = uint32_t(texture_pixels_offset);
				texture_serialized.palette = uint32_t(texture_palette_offset);
				texture_serialized.width = texture.width;
				texture_serialized.height = texture.height;
				texture_serialized.scaled_width = texture.scaled_width;
				texture_serialized.scaled_height = texture.scaled_height;
				size_t const texture_name_length = std::min(size_t(texture_name_max_length), texture.name.size());
				std::memcpy(
						texture_serialized.name,
						texture.name.c_str(),
						texture_name_length);
				std::memset(
						texture_serialized.name + texture_name_length,
						0,
						texture_name_max_length + 1 - texture_name_length);
				std::memset(&texture_serialized.unknown_0, 0, sizeof(texture_serialized.unknown_0));
				texture_serialized.mip_levels = texture.mip_levels;
				std::memset(&texture_serialized.unknown_1, 0, sizeof(texture_serialized.unknown_1));
				texture_serialized.anim_total = texture.anim_total;
				texture_serialized.anim_min = texture.anim_min;
				texture_serialized.anim_max = texture.anim_max;
				texture_serialized.anim_next =
						texture.anim_next != UINT32_MAX
								? uint32_t(textures_offset + sizeof(gbx_texture) * texture.anim_next)
								: UINT32_MAX;
				texture_serialized.alternate_anims =
						texture.alternate_anims != UINT32_MAX
								? uint32_t(textures_offset + sizeof(gbx_texture) * texture.alternate_anims)
								: UINT32_MAX;
				std::memcpy(
						map.data() + textures_offset + sizeof(gbx_texture) * texture_number,
						&texture_serialized,
						sizeof(gbx_texture));
			}
		} else {
			// Single checkerboard texture of the smallest possible size (16x16) with a single 8x8 mip.
			lump_counts[gbx_lump_number_textures] = 1;
			size_t const notexture_mip_0_offset = textures_offset + sizeof(gbx_texture);
			size_t const notexture_mip_1_offset = notexture_mip_0_offset + 16 * 16;
			size_t const notexture_palette_offset = notexture_mip_1_offset + 8 * 8;
			map.resize(notexture_palette_offset + 4 * 256);
			gbx_texture notexture;
			notexture.pixels = uint32_t(notexture_mip_0_offset);
			notexture.palette = uint32_t(notexture_palette_offset);
			notexture.width = 16;
			notexture.height = 16;
			notexture.scaled_width = 16;
			notexture.scaled_height = 16;
			static char const notexture_name[texture_name_max_length + 1] = "notexture";
			static_assert(sizeof(notexture.name) == sizeof(notexture_name));
			std::memcpy(&notexture.name, &notexture_name, sizeof(notexture_name));
			std::memset(&notexture.unknown_0, 0, sizeof(notexture.unknown_0));
			notexture.mip_levels = 1;
			std::memset(&notexture.unknown_1, 0, sizeof(notexture.unknown_1));
			notexture.anim_total = 0;
			notexture.anim_min = 0;
			notexture.anim_max = 0;
			notexture.anim_next = UINT32_MAX;
			notexture.alternate_anims = UINT32_MAX;
			std::memcpy(map.data() + textures_offset, &notexture, sizeof(gbx_texture));
			for (size_t notexture_y = 0; notexture_y < 8; ++notexture_y) {
				uint8_t const notexture_row_left = ((notexture_y < 4) ? UINT8_MAX : 0);
				uint8_t const notexture_row_right = notexture_row_left ^ UINT8_MAX;
				char * const notexture_mip_0_row =
						map.data() + notexture_mip_0_offset + (16 * 2) * notexture_y;
				char * const notexture_mip_1_row =
						map.data() + notexture_mip_1_offset + 8 * notexture_y;
				std::memset(notexture_mip_0_row, notexture_row_left, 8);
				std::memset(notexture_mip_0_row + 8, notexture_row_right, 8);
				std::memset(notexture_mip_0_row + 16, notexture_row_left, 8);
				std::memset(notexture_mip_0_row + 24, notexture_row_right, 8);
				std::memset(notexture_mip_1_row, notexture_row_left, 4);
				std::memset(notexture_mip_1_row + 4, notexture_row_right, 4);
			}
			char * const notexture_palette = map.data() + notexture_palette_offset;
			for (uint32_t color_number = 0; color_number < 255; ++color_number) {
				notexture_palette[4 * color_number] = 0;
				notexture_palette[4 * color_number + 1] = 0;
				notexture_palette[4 * color_number + 2] = 0;
				notexture_palette[4 * color_number + 3] = char(0x80);
			}
			notexture_palette[4 * 255] = 0x7F;
			notexture_palette[4 * 255 + 1] = 0;
			notexture_palette[4 * 255 + 2] = 0x7F;
			notexture_palette[4 * 255 + 3] = char(0x80);
		}
		finish_lump(gbx_lump_number_textures);
	}

	// Entities.
	// The count is not stored, only the length.
	{
		size_t const entities_offset = map.size();
		std::string entities_string = serialize_entities(entities.data(), entities.size());
		// Null-terminated.
		size_t const entities_length = entities_string.size() + 1;
		lump_offsets[gbx_lump_number_entities] = uint32_t(entities_offset);
		map.resize(entities_offset + entities_length);
		std::memcpy(map.data() + entities_offset, entities_string.c_str(), entities_length);
		finish_lump(gbx_lump_number_entities);
	}

	// Polygons.
	size_t const polygons_offset = map.size();
	std::vector<size_t> face_polygons_offsets;
	{
		size_t const polygon_count = polygons.size();
		lump_offsets[gbx_lump_number_polygons] = uint32_t(polygons_offset);
		lump_counts[gbx_lump_number_polygons] = uint32_t(polygon_count);
		face_polygons_offsets.reserve(polygon_count);
		for (gbx_polygons_deserialized const & face_polygons : polygons) {
			size_t const face_polygons_offset = map.size();
			face_polygons_offsets.push_back(face_polygons_offset);
			size_t const face_polygons_vertexes_size = sizeof(gbx_polygon_vertex) * face_polygons.vertexes.size();
			map.resize(face_polygons_offset + sizeof(uint32_t) * 2 + face_polygons_vertexes_size + sizeof(uint32_t));
			uint32_t const face_polygons_face_number = uint32_t(face_polygons.face_number);
			std::memcpy(
					map.data() + face_polygons_offset,
					&face_polygons_face_number,
					sizeof(uint32_t));
			uint32_t const face_polygons_vertex_count = uint32_t(face_polygons.vertexes.size());
			std::memcpy(
					map.data() + face_polygons_offset + sizeof(uint32_t),
					&face_polygons_vertex_count,
					sizeof(uint32_t));
			std::memcpy(
					map.data() + face_polygons_offset + sizeof(uint32_t) * 2,
					face_polygons.vertexes.data(),
					face_polygons_vertexes_size);
			uint32_t const face_polygons_strip_count = uint32_t(face_polygons.strips.size());
			std::memcpy(
					map.data() + face_polygons_offset + sizeof(uint32_t) * 2 + face_polygons_vertexes_size,
					&face_polygons_strip_count,
					sizeof(uint32_t));
			for (std::vector<uint16_t> const & face_polygons_strip : face_polygons.strips) {
				size_t const face_polygons_strip_offset = map.size();
				size_t const face_polygons_strip_size = sizeof(uint16_t) * face_polygons_strip.size();
				map.resize(face_polygons_strip_offset + sizeof(uint16_t) + face_polygons_strip_size);
				uint16_t const face_polygons_strip_vertex_count = uint16_t(face_polygons_strip.size());
				std::memcpy(
						map.data() + face_polygons_strip_offset,
						&face_polygons_strip_vertex_count,
						sizeof(uint16_t));
				std::memcpy(
						map.data() + face_polygons_strip_offset + sizeof(uint16_t),
						face_polygons_strip.data(),
						face_polygons_strip_size);
				while (map.size() & 3) {
					map.push_back(char(gbx_polygon_strip_alignment_byte));
				}
			}
		}
		finish_lump(gbx_lump_number_polygons);
	}

	// Write the lumps with absolute addresses inside subsequent lumps.

	// Nodes.
	for (size_t node_number = 0; node_number < node_count; ++node_number) {
		gbx_node node_serialized(nodes[node_number]);
		if (node_serialized.parent != UINT32_MAX) {
			node_serialized.parent = uint32_t(nodes_offset + sizeof(gbx_node) * node_serialized.parent);
		}
		node_serialized.plane = uint32_t(planes_offset + sizeof(gbx_plane) * node_serialized.plane);
		for (size_t child_number = 0; child_number < 2; ++child_number) {
			int32_t const child = node_serialized.children[child_number];
			node_serialized.children[child_number] = int32_t(
					child >= 0
							? uint32_t(nodes_offset + sizeof(gbx_node) * uint32_t(child))
							: uint32_t(leafs_offset + sizeof(gbx_leaf) * uint32_t(INT32_C(-1) - child)));
		}
		std::memcpy(map.data() + nodes_offset + sizeof(gbx_node) * node_number, &node_serialized, sizeof(gbx_node));
	}

	// Leafs.
	for (size_t leaf_number = 0; leaf_number < leaf_count; ++leaf_number) {
		gbx_leaf leaf_serialized(leafs[leaf_number]);
		if (leaf_serialized.parent != UINT32_MAX) {
			leaf_serialized.parent = uint32_t(nodes_offset + sizeof(gbx_node) * leaf_serialized.parent);
		}
		if (leaf_serialized.visibility_offset != UINT32_MAX) {
			leaf_serialized.visibility_offset += uint32_t(visibility_offset);
		}
		std::memcpy(map.data() + leafs_offset + sizeof(gbx_leaf) * leaf_number, &leaf_serialized, sizeof(gbx_leaf));
	}

	// Faces.
	for (size_t face_number = 0; face_number < face_count; ++face_number) {
		gbx_face face_serialized(faces[face_number]);
		face_serialized.texture =
				uint32_t(textures_offset + sizeof(gbx_texture) * (textures.empty() ? 0 : face_serialized.texture));
		if (face_serialized.lighting_offset != UINT32_MAX) {
			face_serialized.lighting_offset += uint32_t(lighting_offset);
		}
		face_serialized.plane = uint32_t(planes_offset + sizeof(gbx_plane) * face_serialized.plane);
		if (face_serialized.polygons != UINT32_MAX) {
			face_serialized.polygons = uint32_t(face_polygons_offsets[face_serialized.polygons]);
		}
		std::memcpy(map.data() + faces_offset + sizeof(gbx_face) * face_number, &face_serialized, sizeof(gbx_face));
	}

	// Lumps header.
	std::memcpy(
			map.data() + sizeof(uint32_t),
			lump_offsets.data(),
			sizeof(uint32_t) * gbx_lump_count);
	std::memcpy(
			map.data() + sizeof(uint32_t) * (1 + gbx_lump_count),
			lump_lengths.data(),
			sizeof(uint32_t) * gbx_lump_count);
	std::memcpy(
			map.data() + sizeof(uint32_t) * (1 + gbx_lump_count * 2),
			lump_counts.data(),
			sizeof(uint32_t) * gbx_lump_count);
}

char const * gbx_map::deserialize_only_textures(
		void const * const map, size_t const map_size, palette_set const & quake_palette) {
	if (map_size < sizeof(uint32_t) + sizeof(uint32_t) * gbx_lump_count * 4) {
		return "Map version and lumps are out of bounds";
	}
	uint32_t version;
	std::memcpy(&version, map, sizeof(uint32_t));
	if (version != gbx_map_version) {
		return "Map has the wrong version number";
	}
	uint32_t textures_offset, textures_lump_length, texture_count;
	std::memcpy(
			&textures_offset,
			reinterpret_cast<char const *>(map) +
					sizeof(uint32_t) * (1 + gbx_lump_number_textures),
			sizeof(uint32_t));
	std::memcpy(
			&textures_lump_length,
			reinterpret_cast<char const *>(map) +
					sizeof(uint32_t) * (1 + gbx_lump_count + gbx_lump_number_textures),
			sizeof(uint32_t));
	std::memcpy(
			&texture_count,
			reinterpret_cast<char const *>(map) +
					sizeof(uint32_t) * (1 + gbx_lump_count * 2 + gbx_lump_number_textures),
			sizeof(uint32_t));
	return deserialize_textures(map, map_size, textures_offset, textures_lump_length, texture_count, quake_palette);
}

}
