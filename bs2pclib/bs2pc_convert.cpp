#include "bs2pclib.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace bs2pc {

id_plane::id_plane(gbx_plane const & gbx) :
		normal(gbx.normal),
		distance(gbx.distance),
		type(gbx.type) {}

gbx_plane::gbx_plane(id_plane const & id) :
		normal(id.normal),
		distance(id.distance),
		type(id.type),
		signbits(id.signbits()),
		padding(0) {}

id_node::id_node(gbx_node const & gbx) :
		plane_number(gbx.plane),
		first_face(gbx.first_face),
		face_count(gbx.face_count) {
	children[0] = int16_t(gbx.children[0]);
	children[1] = int16_t(gbx.children[1]);
	// VectorCopy (simple assignment) conversion from float to int16_t.
	for (size_t component = 0; component < 3; ++component) {
		mins[component] = int16_t(gbx.mins.v[component]);
		maxs[component] = int16_t(gbx.maxs.v[component]);
	}
}

gbx_node::gbx_node(id_node const & id, uint32_t const parent) :
		leaf_contents(contents_node),
		parent(parent),
		visibility_frame(0),
		plane(id.plane_number),
		first_face(id.first_face),
		face_count(id.face_count),
		unknown_0(0) {
	for (size_t component = 0; component < 3; ++component) {
		mins.v[component] = float(id.mins[component]);
		maxs.v[component] = float(id.maxs[component]);
	}
	mins.v[3] = 0.0f;
	maxs.v[3] = 0.0f;
	children[0] = id.children[0];
	children[1] = id.children[1];
}

id_leaf::id_leaf(gbx_leaf const & gbx) :
		leaf_contents(gbx.leaf_contents),
		visibility_offset(gbx.visibility_offset),
		first_marksurface(uint16_t(gbx.first_marksurface)),
		marksurface_count(uint16_t(gbx.marksurface_count)) {
	// VectorCopy (simple assignment) conversion from float to int16_t.
	for (size_t component = 0; component < 3; ++component) {
		mins[component] = int16_t(gbx.mins.v[component]);
		maxs[component] = int16_t(gbx.maxs.v[component]);
	}
	std::memcpy(ambient_level, gbx.ambient_level, sizeof(uint8_t) * ambient_count);
}

gbx_leaf::gbx_leaf(id_leaf const & id, uint32_t const parent) :
		leaf_contents(id.leaf_contents),
		parent(parent),
		visibility_frame(0),
		unknown_0(0),
		visibility_offset(id.visibility_offset),
		first_marksurface(id.first_marksurface),
		marksurface_count(id.marksurface_count) {
	for (size_t component = 0; component < 3; ++component) {
		mins.v[component] = float(id.mins[component]);
		maxs.v[component] = float(id.maxs[component]);
	}
	mins.v[3] = 0.0f;
	maxs.v[3] = 0.0f;
	std::memcpy(ambient_level, id.ambient_level, sizeof(uint8_t) * ambient_count);
}

id_model::id_model(gbx_model const & gbx) :
		mins(gbx.mins),
		maxs(gbx.maxs),
		origin(gbx.origin),
		visibility_leafs(gbx.visibility_leafs),
		first_face(gbx.first_face),
		face_count(gbx.face_count) {
	std::memcpy(head_nodes, gbx.head_nodes, sizeof(int32_t) * max_hulls);
}

gbx_model::gbx_model(id_model const & id) :
		mins(id.mins),
		maxs(id.maxs),
		origin(id.origin),
		visibility_leafs(id.visibility_leafs),
		first_face(id.first_face),
		face_count(id.face_count),
		unknown_0(0) {
	std::memcpy(head_nodes, id.head_nodes, sizeof(int32_t) * max_hulls);
}

id_texinfo::id_texinfo(gbx_face const & gbx, /* id_texinfo_flags */ uint32_t const flags) :
		texture_number(gbx.texture),
		flags(flags) {
	vectors[0] = gbx.texinfo_vectors[0];
	vectors[1] = gbx.texinfo_vectors[1];
}

id_face::id_face(gbx_face const & gbx, uint16_t const texinfo_number) :
		plane_number(gbx.plane),
		side(gbx.side),
		first_edge(gbx.first_edge),
		edge_count(gbx.edge_count),
		texinfo_number(texinfo_number),
		lighting_offset(gbx.lighting_offset) {
	std::memcpy(styles, gbx.styles, sizeof(uint8_t) * max_lightmaps);
}

gbx_face::gbx_face(
		struct id_face const & face,
		struct id_texinfo const & texinfo,
		/* gbx_face_flags */ uint16_t const texture_flags,
		int16_t const * texture_mins,
		int16_t const * extents,
		uint32_t const polygons) {
	// Initialize all unknown fields to 0.
	std::memset(this, 0, sizeof(*this));
	// The flags may be modified later in the constructor.
	flags = texture_flags;
	set_texinfo_vectors(texinfo.vectors[0], texinfo.vectors[1]);
	set_side(face.side);
	texture = texinfo.texture_number;
	lighting_offset = face.lighting_offset;
	plane = face.plane_number;
	first_edge = face.first_edge;
	edge_count = face.edge_count;
	// Unlike in Quake, these are valid even for liquids.
	this->texture_mins[0] = texture_mins[0];
	this->texture_mins[1] = texture_mins[1];
	this->extents[0] = extents[0];
	this->extents[1] = extents[1];
	std::memcpy(styles, face.styles, sizeof(uint8_t) * max_lightmaps);
	set_polygons(polygons);
}

void id_map::from_gbx_no_texture_pixels(gbx_map const & gbx) {
	version = id_map_version_valve;

	// Entities.
	entities = gbx.entities;

	// Planes.
	planes.clear();
	planes.reserve(gbx.planes.size());
	std::copy(gbx.planes.cbegin(), gbx.planes.cend(), std::back_inserter(planes));

	// Textures (without the pixels, those must to be set externally if embedded textures are needed).
	textures.clear();
	textures.reserve(gbx.textures.size());
	for (gbx_texture_deserialized const & texture_gbx : gbx.textures) {
		id_texture_deserialized & texture_id = textures.emplace_back();
		texture_id.name = texture_gbx.name;
		texture_id.width = texture_gbx.width;
		texture_id.height = texture_gbx.height;
	}

	// Vertexes.
	vertexes.clear();
	vertexes.reserve(gbx.vertexes.size());
	std::copy(gbx.vertexes.cbegin(), gbx.vertexes.cend(), std::back_inserter(vertexes));

	// Visibility.
	visibility = gbx.visibility;

	// Nodes.
	nodes.clear();
	nodes.reserve(gbx.nodes.size());
	std::copy(gbx.nodes.cbegin(), gbx.nodes.cend(), std::back_inserter(nodes));

	// Texinfo and faces.
	texinfo.clear();
	faces.clear();
	faces.reserve(gbx.faces.size());
	size_t const texture_count = gbx.textures.size();
	std::vector<bool> textures_special(texture_count);
	for (size_t texture_number = 0; texture_number < texture_count; ++texture_number) {
		textures_special[texture_number] = is_id_texture_special(gbx.textures[texture_number].name.c_str());
	}
	for (gbx_face const & face : gbx.faces) {
		id_texinfo face_texinfo(
				face,
				(texture_count && textures_special[face.texture]) ? id_texinfo_flag_special : 0);
		// Try to reuse an existing texinfo.
		size_t const texinfo_current_count = texinfo.size();
		size_t texinfo_number = 0;
		while (texinfo_number < texinfo_current_count) {
			if (!std::memcmp(&texinfo[texinfo_number], &face_texinfo, sizeof(id_texinfo))) {
				break;
			}
			++texinfo_number;
		}
		assert(texinfo_number <= texinfo_current_count);
		if (texinfo_number >= texinfo_current_count) {
			texinfo.emplace_back(face_texinfo);
		}
		faces.emplace_back(face, uint16_t(texinfo_number));
	}

	// Lighting.
	lighting = gbx.lighting;

	// Clipnodes.
	clipnodes = gbx.clipnodes;

	// Leafs.
	leafs.clear();
	leafs.reserve(gbx.leafs.size());
	std::copy(gbx.leafs.cbegin(), gbx.leafs.cend(), std::back_inserter(leafs));

	// Marksurfaces.
	marksurfaces.clear();
	marksurfaces.reserve(gbx.marksurfaces.size());
	std::copy(gbx.marksurfaces.cbegin(), gbx.marksurfaces.cend(), std::back_inserter(marksurfaces));

	// Edges.
	edges = gbx.edges;

	// Surfedges.
	surfedges = gbx.surfedges;

	// Models.
	models.clear();
	models.reserve(gbx.models.size());
	std::copy(gbx.models.cbegin(), gbx.models.cend(), std::back_inserter(models));
}

void gbx_map::set_node_or_leaf_parent(int32_t const node_or_leaf_number, uint32_t const parent) {
	if (node_or_leaf_number >= 0) {
		gbx_node & node = nodes[node_or_leaf_number];
		node.parent = uint32_t(parent);
		set_node_or_leaf_parent(node.children[0], node_or_leaf_number);
		set_node_or_leaf_parent(node.children[1], node_or_leaf_number);
	} else {
		leafs[INT32_C(-1) - node_or_leaf_number].parent = uint32_t(parent);
	}
}

void gbx_map::make_hull_0_from_nodes_and_leafs() {
	hull_0.clear();
	hull_0.reserve(nodes.size());
	for (gbx_node const & node : nodes) {
		clipnode & hull_0_clipnode = hull_0.emplace_back();
		hull_0_clipnode.plane_number = node.plane;
		for (size_t child_number = 0; child_number < 2; ++child_number) {
			int32_t child = node.children[child_number];
			hull_0_clipnode.child_clipnodes_or_contents[child_number] =
					child >= 0 ? int16_t(child) : int16_t(leafs[INT16_C(-1) - child].leaf_contents);
		}
	}
}

void gbx_map::from_id_no_texture_pixels_and_polygons(id_map const & id) {
	// Planes.
	planes.clear();
	planes.reserve(id.planes.size());
	std::copy(id.planes.cbegin(), id.planes.cend(), std::back_inserter(planes));

	// Nodes.
	// The parents will be set later.
	// Required for the hull 0.
	nodes.clear();
	nodes.reserve(id.nodes.size());
	std::copy(id.nodes.cbegin(), id.nodes.cend(), std::back_inserter(nodes));

	// Leafs.
	// The parents will be set later.
	// Required for the hull 0.
	leafs.clear();
	leafs.reserve(id.leafs.size());
	std::copy(id.leafs.cbegin(), id.leafs.cend(), std::back_inserter(leafs));

	// Node and leaf parents.
	if (!nodes.empty()) {
		set_node_or_leaf_parent(0, UINT32_MAX);
	}

	// Edges.
	edges = id.edges;

	// Surfedges.
	surfedges = id.surfedges;

	// Vertexes.
	vertexes.clear();
	vertexes.reserve(id.vertexes.size());
	std::copy(id.vertexes.cbegin(), id.vertexes.cend(), std::back_inserter(vertexes));

	// Drawing hull as clipping hull (hull 0).
	// Requires nodes and leafs.
	make_hull_0_from_nodes_and_leafs();

	// Clipnodes.
	clipnodes = id.clipnodes;

	// Models.
	models.clear();
	models.reserve(id.models.size());
	std::copy(id.models.cbegin(), id.models.cend(), std::back_inserter(models));

	// Faces.
	// Also prepare the face numbers in the polygons.
	faces.clear();
	polygons.clear();
	size_t const face_count = id.faces.size();
	faces.reserve(face_count);
	for (size_t face_number = 0; face_number < face_count; ++face_number) {
		id_face const & face = id.faces[face_number];
		id_texinfo face_texinfo = id.texinfo[face.texinfo_number];
		uint16_t face_texture_flags = 0;
		if (!id.textures.empty()) {
			id_texture_deserialized const & face_texture = id.textures[face_texinfo.texture_number];
			if (!face_texture.empty()) {
				face_texture_flags = texture_gbx_face_flags(face_texture.name.c_str());
			}
		} else {
			// Single checkerboard created during serialization.
			face_texinfo.texture_number = 0;
		}
		std::array<int16_t, 2> face_texture_mins, face_extents;
		face.calculate_extents(
				face_texinfo, id.surfedges.data(), id.edges.data(), id.vertexes.data(),
				face_texture_mins.data(), face_extents.data());
		// Reserve an entry in the polygons from the face to build them later.
		uint32_t face_polygons_number = UINT32_MAX;
		if (face_texture_flags & gbx_face_flag_draw_polygons) {
			face_polygons_number = uint32_t(polygons.size());
			polygons.emplace_back().face_number = uint32_t(face_number);
		}
		faces.emplace_back(
				face, face_texinfo, face_texture_flags, face_texture_mins.data(), face_extents.data(),
				face_polygons_number);
	}

	// Marksurfaces.
	marksurfaces.clear();
	marksurfaces.reserve(id.marksurfaces.size());
	std::copy(id.marksurfaces.cbegin(), id.marksurfaces.cend(), std::back_inserter(marksurfaces));

	// Visibility.
	visibility = id.visibility;

	// Lighting.
	lighting = id.lighting;

	// Textures.
	textures.clear();
	textures.reserve(id.textures.size());
	for (id_texture_deserialized const & texture_id : id.textures) {
		gbx_texture_deserialized & texture_gbx = textures.emplace_back();
		if (!texture_id.empty()) {
			texture_gbx.width = texture_id.width;
			texture_gbx.height = texture_id.height;
			texture_gbx.name = texture_id.name;
		} else {
			// Fill with a unnamed checkerboard of the minimum size during serialization.
			texture_gbx.width = texture_width_height_alignment;
			texture_gbx.height = texture_width_height_alignment;
		}
		texture_gbx.scaled_width = gbx_texture_scaled_size(texture_gbx.width);
		texture_gbx.scaled_height = gbx_texture_scaled_size(texture_gbx.height);
		texture_gbx.mip_levels =
				gbx_texture_mip_levels_without_base(texture_gbx.scaled_width, texture_gbx.scaled_height);
		texture_gbx.reset_anim();
	}
	link_texture_anim();

	// Entities.
	entities = id.entities;
}

}
