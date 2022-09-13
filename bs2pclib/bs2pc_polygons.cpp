#include "bs2pclib.hpp"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <utility>
#include <vector>

namespace bs2pc {

void gbx_map::make_polygons(gbx_polygons_deserialized * const polygons_start, size_t const polygons_count) {
	// ON_EPSILON.
	static constexpr float epsilon = 0.01f;

	std::vector<vector3> subdivision_vertexes;
	auto add_subdivision_vertex = [&subdivision_vertexes](vector3 const vertex) -> size_t {
		for (size_t vertex_number = 0; vertex_number < subdivision_vertexes.size(); ++vertex_number) {
			vector3 const & existing_vertex = subdivision_vertexes[vertex_number];
			if (std::abs(existing_vertex.v[0] - vertex.v[0]) < epsilon &&
					std::abs(existing_vertex.v[1] - vertex.v[1]) < epsilon &&
					std::abs(existing_vertex.v[2] - vertex.v[2]) < epsilon) {
				return vertex_number;
			}
		}
		subdivision_vertexes.push_back(vertex);
		return subdivision_vertexes.size() - 1;
	};

	struct subdivision_face {
		// Empty if removed after having been split.
		std::vector<size_t> vertexes;
		// SIZE_MAX if no next face.
		size_t next = SIZE_MAX;

		size_t chain_number = SIZE_MAX;
		// <Edge first to first + 1 in this face connected to the other chain, face number>, or SIZE_MAX if not
		// connected on one side.
		std::pair<size_t, size_t> chain_prev{SIZE_MAX, SIZE_MAX};
		std::pair<size_t, size_t> chain_next{SIZE_MAX, SIZE_MAX};

		// <This face edge, other face edge>, or SIZE_MAX if not found.
		std::pair<size_t, size_t> find_chaining_edge(subdivision_face const & other) const {
			assert(this != &other);
			if ((chain_prev.first != SIZE_MAX && chain_next.first != SIZE_MAX) ||
					(other.chain_prev.first != SIZE_MAX && other.chain_next.first != SIZE_MAX)) {
				return std::pair<size_t, size_t>(SIZE_MAX, SIZE_MAX);
			}
			size_t const edge_count = vertexes.size();
			size_t const other_edge_count = other.vertexes.size();
			for (size_t edge_number = 0; edge_number < edge_count; ++edge_number) {
				if (edge_number == chain_prev.first || edge_number == chain_next.first) {
					// Already connected.
					continue;
				}
				size_t const edge_vertex_1 = vertexes[edge_number];
				size_t const edge_vertex_2 = vertexes[(edge_number + 1) % edge_count];
				for (size_t other_edge_number = 0; other_edge_number < other_edge_count; ++other_edge_number) {
					if (other_edge_number == other.chain_prev.first || other_edge_number == other.chain_next.first) {
						// Already connected.
						continue;
					}
					size_t const other_edge_vertex_1 = other.vertexes[other_edge_number];
					size_t const other_edge_vertex_2 = other.vertexes[(other_edge_number + 1) % other_edge_count];
					if ((edge_vertex_1 == other_edge_vertex_1 && edge_vertex_2 == other_edge_vertex_2) ||
							(edge_vertex_1 == other_edge_vertex_2 && edge_vertex_2 == other_edge_vertex_1)) {
						return std::make_pair(edge_number, other_edge_number);
					}
				}
			}
			return std::pair<size_t, size_t>(SIZE_MAX, SIZE_MAX);
		}
	};

	std::vector<subdivision_face> subdivision_faces;

	std::vector<float> plane_distances;
	// -1 for in the back, 0 for on the plane, 1 for in the front.
	std::vector<int> plane_sides;

	// <First face, last face>, or both SIZE_MAX if the chain has been fully consumed by another.
	std::vector<std::pair<size_t, size_t>> chains;

	for (size_t polygons_number = 0; polygons_number < polygons_count; ++polygons_number) {
		gbx_polygons_deserialized & face_polygons = polygons_start[polygons_number];
		face_polygons.vertexes.clear();
		face_polygons.strips.clear();

		gbx_face const & face = faces[face_polygons.face_number];
		if (face.edge_count < 3) {
			continue;
		}

		// Texture-space subdivision, similar to qbsp2 SubdivideFace, closer to what's done in the original PS2 maps.
		// However, faces placed next to each other will be subdivided continuously like in Quake GL_SubdivideSurface,
		// not with the mins being the origin.

		subdivision_vertexes.clear();
		subdivision_faces.clear();

		// Construct the original face to start subdividing.
		size_t polygon_face_number = subdivision_faces.size();
		{
			subdivision_face & initial_face = subdivision_faces.emplace_back();
			initial_face.vertexes.reserve(face.edge_count);
			for (size_t face_edge_number = 0; face_edge_number < face.edge_count; ++face_edge_number) {
				surfedge const face_surfedge = surfedges[face.first_edge + face_edge_number];
				initial_face.vertexes.push_back(add_subdivision_vertex(
						vertexes[edges[std::abs(face_surfedge)].vertexes[size_t(face_surfedge < 0)]]));
			}
		}

		vector4 texinfo_vectors_normal[2];
		static constexpr float subdivide_size = 32.0f;
		float axis_subdivide_sizes[2];
		for (size_t axis = 0; axis < 2; ++axis) {
			vector4 const & texinfo_vector = face.texinfo_vectors[axis];
			float const texinfo_vector_length = std::sqrt(
					texinfo_vector.v[0] * texinfo_vector.v[0] +
					texinfo_vector.v[1] * texinfo_vector.v[1] +
					texinfo_vector.v[2] * texinfo_vector.v[2]);
			for (size_t texinfo_vector_axis = 0; texinfo_vector_axis < 4; ++texinfo_vector_axis) {
				texinfo_vectors_normal[axis].v[texinfo_vector_axis] =
						texinfo_vector.v[texinfo_vector_axis] / texinfo_vector_length;
			}
			// The step in world space for the axis depends on the other axis in the original maps, not on that axis.
			axis_subdivide_sizes[axis ^ 1] = subdivide_size / texinfo_vector_length;
		}

		// If SIZE_MAX, the previous face is polygon_face_number, and it needs to be updated.
		// Otherwise, the previous face is the next of subdivision_faces[previous_face_link_number].
		size_t previous_face_link_number = SIZE_MAX;
		while (true) {
			size_t current_face_number =
					previous_face_link_number != SIZE_MAX
							? subdivision_faces[previous_face_link_number].next
							: polygon_face_number;
			if (current_face_number == SIZE_MAX) {
				break;
			}

			for (size_t axis = 0; axis < 2; ++axis) {
				vector4 const & texinfo_vector_normal = texinfo_vectors_normal[axis];
				float const axis_subdivide_size = axis_subdivide_sizes[axis];
				while (true) {
					subdivision_face * current_face = &subdivision_faces[current_face_number];

					assert(current_face->vertexes.size() >= 3);
					if (current_face->vertexes.size() < 3) {
						break;
					}

					float axis_position_min = FLT_MAX, axis_position_max = -FLT_MAX;
					for (size_t const face_vertex_index : current_face->vertexes) {
						vector3 const & face_vertex = subdivision_vertexes[face_vertex_index];
						float const axis_position =
								face_vertex.v[0] * texinfo_vector_normal.v[0] +
								face_vertex.v[1] * texinfo_vector_normal.v[1] +
								face_vertex.v[2] * texinfo_vector_normal.v[2];
						axis_position_min = std::min(axis_position_min, axis_position);
						axis_position_max = std::max(axis_position_max, axis_position);
					}
					// The origin of the subdivision grid along the axis is possibly not the same as in the original
					// Gearbox maps, but the texinfo vector translation is continuous along liquids consisting of
					// multiple faces, and also makes sure handrails that are 32 units tall are not subdivided
					// vertically as long as they have the texture correctly aligned, just like in the original Gearbox
					// maps.
					float subdivide_axis_position =
							axis_subdivide_size *
									std::floor(
											(texinfo_vector_normal.v[3] +
													(axis_position_min + axis_position_max) * 0.5f) /
											axis_subdivide_size) -
							texinfo_vector_normal.v[3];
					if (!(subdivide_axis_position - axis_position_min >= epsilon)) {
						// Flooring might have resulted in the position being below the minimum, try the next split.
						subdivide_axis_position += axis_subdivide_size;
					}
					if (!(subdivide_axis_position - axis_position_min >= epsilon) ||
							!(axis_position_max - subdivide_axis_position >= epsilon)) {
						break;
					}

					size_t front_face_number, back_face_number;

					plane_distances.clear();
					plane_distances.reserve(current_face->vertexes.size() + 1);
					plane_sides.clear();
					plane_sides.reserve(current_face->vertexes.size() + 1);
					bool any_in_front = false, any_in_back = false;
					for (size_t const face_vertex_index : current_face->vertexes) {
						vector3 const & face_vertex = subdivision_vertexes[face_vertex_index];
						float const plane_distance =
								face_vertex.v[0] * texinfo_vector_normal.v[0] +
								face_vertex.v[1] * texinfo_vector_normal.v[1] +
								face_vertex.v[2] * texinfo_vector_normal.v[2] -
								subdivide_axis_position;
						plane_distances.push_back(plane_distance);
						int plane_side;
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
							vector3 const & face_vertex = subdivision_vertexes[face_vertex_index];

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
							vector3 const & face_vertex_next = subdivision_vertexes[
									current_face->vertexes[(face_vertex_number + 1) % current_face->vertexes.size()]];
							float const dot =
									plane_distances[face_vertex_number] /
									(plane_distances[face_vertex_number] - plane_distances[face_vertex_number + 1]);
							vector3 split_vertex;
							for (size_t split_vertex_component = 0;
									split_vertex_component < 3;
									++split_vertex_component) {
								// Avoid roundoff error when possible.
								if (texinfo_vector_normal.v[split_vertex_component] == 1.0f) {
									split_vertex.v[split_vertex_component] = subdivide_axis_position;
								} else if (texinfo_vector_normal.v[split_vertex_component] == -1.0f) {
									split_vertex.v[split_vertex_component] = -subdivide_axis_position;
								} else {
									split_vertex.v[split_vertex_component] =
											face_vertex.v[split_vertex_component] +
													dot *
													(face_vertex_next.v[split_vertex_component] -
															face_vertex.v[split_vertex_component]);
								}
							}
							size_t const split_vertex_index = add_subdivision_vertex(split_vertex);
							back_face.vertexes.push_back(split_vertex_index);
							front_face.vertexes.push_back(split_vertex_index);
						}

						// Remove the subdivided face.
						current_face->vertexes.clear();
					}

					if (front_face_number == SIZE_MAX || back_face_number == SIZE_MAX) {
						// Didn't split the polygon.
						break;
					}

					(previous_face_link_number != SIZE_MAX
							? subdivision_faces[previous_face_link_number].next
							: polygon_face_number) =
							back_face_number;
					subdivision_faces[back_face_number].next = front_face_number;
					subdivision_faces[front_face_number].next = current_face->next;

					current_face_number = back_face_number;
				}
			}

			previous_face_link_number =
					previous_face_link_number != SIZE_MAX
							? subdivision_faces[previous_face_link_number].next
							: polygon_face_number;
		}

		// Write the vertexes.
		face_polygons.vertexes.reserve(subdivision_vertexes.size());
		float face_texture_size[2];
		if (!textures.empty()) {
			gbx_texture_deserialized const & face_texture = textures[face.texture];
			face_texture_size[0] = float(face_texture.width);
			face_texture_size[1] = float(face_texture.height);
		} else {
			// No textures in the original id map, just a checkerboard.
			face_texture_size[0] = 16.0f;
			face_texture_size[1] = 16.0f;
		}
		float face_light_st_max[2];
		for (size_t axis = 0; axis < 2; ++axis) {
			// Exactly the lightmap size minus 1.
			face_light_st_max[axis] =
					float(std::min(int16_t(UINT8_MAX), int16_t(std::max(int16_t(0), face.extents[axis]) >> 4)));
		}
		for (vector3 const & vertex : subdivision_vertexes) {
			gbx_polygon_vertex & polygon_vertex = face_polygons.vertexes.emplace_back();
			polygon_vertex.xyz = vertex;
			for (size_t axis = 0; axis < 2; ++axis) {
				vector4 const & texinfo_vector = face.texinfo_vectors[axis];
				float const st =
						vertex.v[0] * texinfo_vector.v[0] +
						vertex.v[1] * texinfo_vector.v[1] +
						vertex.v[2] * texinfo_vector.v[2] +
						texinfo_vector.v[3];
				polygon_vertex.st[axis] = st / face_texture_size[axis];
				// Rounding to the nearest (+ 8, / 16), similar to how it's done in BuildSurfaceDisplayList.
				// Not the exact formula used by Gearbox, but no differences on many original Gearbox maps, most
				// differences being at 16 * n + 8 (with this code rounding upwards in cases where in the original maps
				// it's rounded down sometimes), however, with 7 rather than 8 rounding offsets, there are many more
				// discrepancies. Pre-converting (st - face.texture_mins) to an integer and using >> 4 gives the same
				// differences in the original Gearbox maps as using floating-point until the end.
				polygon_vertex.light_st[axis] = uint8_t(std::min(
						face_light_st_max[axis],
						std::max(
								0.0f,
								(st - float(int32_t(face.texture_mins[axis]) - 8)) * (1.0f / 16.0f))));
			}
			polygon_vertex.padding = 0;
		}

		// Chain subdivided faces with common edges to construct triangle strips.
		// Individual subdivided faces are convex and have ordered vertexes constituting an edge loop.
		// First, construct chains containing only individual faces.
		chains.clear();
		{
			size_t next_subdivision_face_number = polygon_face_number;
			while (next_subdivision_face_number != SIZE_MAX) {
				size_t const subdivision_face_number = next_subdivision_face_number;
				subdivision_face & new_chain_subdivision_face = subdivision_faces[subdivision_face_number];
				next_subdivision_face_number = new_chain_subdivision_face.next;
				// The vertex count is 0 for removed faces that have been subdivided.
				if (new_chain_subdivision_face.vertexes.size() < 3) {
					continue;
				}
				new_chain_subdivision_face.chain_number = chains.size();
				chains.emplace_back(subdivision_face_number, subdivision_face_number);
			}
		}
		// Merge the chains.
		bool any_chains_merged;
		// For simplicity, chains merged into other chains, are not removed from the vector, and only replaced with
		// SIZE_MAX, so chains.size never changes.
		size_t const chain_count = chains.size();
		do {
			any_chains_merged = false;
			for (size_t chain_number = 0; chain_number < chain_count; ++chain_number) {
				std::pair<size_t, size_t> & chain = chains[chain_number];
				assert((chain.first == SIZE_MAX) == (chain.second == SIZE_MAX));
				if (chain.first == SIZE_MAX) {
					continue;
				}
				for (size_t chain_2_number = chain_number + 1; chain_2_number < chain_count; ++chain_2_number) {
					std::pair<size_t, size_t> & chain_2 = chains[chain_2_number];
					assert((chain_2.first == SIZE_MAX) == (chain_2.second == SIZE_MAX));
					if (chain_2.first == SIZE_MAX) {
						continue;
					}
					// chain.first and chain.second must be reloaded at every chain_2 iteration since the inner loop
					// doesn't stop after merging.
					// Chain 1 beginning to chain 2 end.
					subdivision_face & chain_beginning = subdivision_faces[chain.first];
					subdivision_face & chain_2_end = subdivision_faces[chain_2.second];
					{
						std::pair<size_t, size_t> const chaining_edge =
								chain_beginning.find_chaining_edge(chain_2_end);
						if (chaining_edge.first != SIZE_MAX) {
							chain_beginning.chain_prev.first = chaining_edge.first;
							chain_beginning.chain_prev.second = chain_2.second;
							chain_2_end.chain_next.first = chaining_edge.second;
							chain_2_end.chain_next.second = chain.first;
							size_t chain_2_merge_face_number = chain_2.second;
							while (chain_2_merge_face_number != SIZE_MAX) {
								subdivision_face & chain_2_merge_face =
										subdivision_faces[chain_2_merge_face_number];
								chain_2_merge_face.chain_number = chain_number;
								chain_2_merge_face_number = chain_2_merge_face.chain_prev.second;
							}
							chain.first = chain_2.first;
							chain_2.first = SIZE_MAX;
							chain_2.second = SIZE_MAX;
							any_chains_merged = true;
							continue;
						}
					}
					// Chain 1 beginning to chain 2 beginning, reversing the chain 2.
					subdivision_face & chain_2_beginning = subdivision_faces[chain_2.first];
					if (chain_2.second != chain_2.first) {
						std::pair<size_t, size_t> const chaining_edge =
								chain_beginning.find_chaining_edge(chain_2_beginning);
						if (chaining_edge.first != SIZE_MAX) {
							chain_beginning.chain_prev.first = chaining_edge.first;
							chain_beginning.chain_prev.second = chain_2.first;
							chain_2_beginning.chain_prev.first = chaining_edge.second;
							chain_2_beginning.chain_prev.second = chain.first;
							size_t chain_2_merge_face_number = chain_2.first;
							while (chain_2_merge_face_number != SIZE_MAX) {
								subdivision_face & chain_2_merge_face =
										subdivision_faces[chain_2_merge_face_number];
								chain_2_merge_face.chain_number = chain_number;
								std::swap(chain_2_merge_face.chain_prev, chain_2_merge_face.chain_next);
								chain_2_merge_face_number = chain_2_merge_face.chain_prev.second;
							}
							chain.first = chain_2.second;
							chain_2.first = SIZE_MAX;
							chain_2.second = SIZE_MAX;
							any_chains_merged = true;
							continue;
						}
					}
					if (chain.second != chain.first) {
						// Chain 1 end to chain 2 beginning.
						subdivision_face & chain_end = subdivision_faces[chain.second];
						{
							std::pair<size_t, size_t> const chaining_edge =
									chain_end.find_chaining_edge(chain_2_beginning);
							if (chaining_edge.first != SIZE_MAX) {
								chain_end.chain_next.first = chaining_edge.first;
								chain_end.chain_next.second = chain_2.first;
								chain_2_beginning.chain_prev.first = chaining_edge.second;
								chain_2_beginning.chain_prev.second = chain.second;
								size_t chain_2_merge_face_number = chain_2.first;
								while (chain_2_merge_face_number != SIZE_MAX) {
									subdivision_face & chain_2_merge_face =
											subdivision_faces[chain_2_merge_face_number];
									chain_2_merge_face.chain_number = chain_number;
									chain_2_merge_face_number = chain_2_merge_face.chain_next.second;
								}
								chain.second = chain_2.second;
								chain_2.first = SIZE_MAX;
								chain_2.second = SIZE_MAX;
								any_chains_merged = true;
								continue;
							}
						}
						// Chain 1 end to chain 2 end, reversing the chain 2.
						if (chain_2.second != chain_2.first) {
							std::pair<size_t, size_t> const chaining_edge =
									chain_end.find_chaining_edge(chain_2_end);
							if (chaining_edge.first != SIZE_MAX) {
								chain_end.chain_next.first = chaining_edge.first;
								chain_end.chain_next.second = chain_2.second;
								chain_2_end.chain_next.first = chaining_edge.second;
								chain_2_end.chain_next.second = chain.second;
								size_t chain_2_merge_face_number = chain_2.second;
								while (chain_2_merge_face_number != SIZE_MAX) {
									subdivision_face & chain_2_merge_face =
											subdivision_faces[chain_2_merge_face_number];
									chain_2_merge_face.chain_number = chain_number;
									std::swap(chain_2_merge_face.chain_prev, chain_2_merge_face.chain_next);
									chain_2_merge_face_number = chain_2_merge_face.chain_next.second;
								}
								chain.second = chain_2.first;
								chain_2.first = SIZE_MAX;
								chain_2.second = SIZE_MAX;
								any_chains_merged = true;
								continue;
							}
						}
					}
				}
			}
		} while (any_chains_merged);

		// Generate triangle strips for the chains.
		for (std::pair<size_t, size_t> const & chain : chains) {
			if (chain.first == SIZE_MAX) {
				continue;
			}
			size_t const strip_number = face_polygons.strips.size();
			std::vector<uint16_t> & strip = face_polygons.strips.emplace_back();
			// First subdivision face - from the fixed edge in reverse.
			subdivision_face const & chain_first_face = subdivision_faces[chain.first];
			{
				size_t const chain_first_face_vertex_count = chain_first_face.vertexes.size();
				size_t const chain_first_face_end_edge =
						chain_first_face.chain_next.first != SIZE_MAX
								? chain_first_face.chain_next.first
								: chain_first_face_vertex_count - 2;
				strip.resize(chain_first_face_vertex_count);
				for (size_t chain_first_face_vertex_number = 0;
						chain_first_face_vertex_number < chain_first_face_vertex_count;
						++chain_first_face_vertex_number) {
					// Before subtracting, adding chain_first_face_vertex_count to avoid overflow.
					strip[chain_first_face_vertex_count - 1 - chain_first_face_vertex_number] =
							chain_first_face.vertexes[
									(chain_first_face_end_edge +
											((chain_first_face_vertex_number & 1)
													? chain_first_face_vertex_count -
															(chain_first_face_vertex_number >> 1)
													: (1 + (chain_first_face_vertex_number >> 1)))) %
											chain_first_face_vertex_count];
				}
			}
			// Subdivision faces in the middle of the chain.
			size_t chain_next_face_number = chain_first_face.chain_next.second;
			if (chain_next_face_number != SIZE_MAX) {
				while (subdivision_faces[chain_next_face_number].chain_next.second != SIZE_MAX) {
					// Construct a triangle strip bridging the two arcs between the two edges shared with other faces.
					// Try to make it look the most uniform, but since the arcs may have different lengths, there may be
					// triangle fans inside the strip, created with degenerate triangles (with index sequences like
					// 01213 - the original Gearbox maps contain such sequences too).
					subdivision_face const & chain_face = subdivision_faces[chain_next_face_number];
					size_t const chain_face_vertex_count = chain_face.vertexes.size();
					size_t const chain_face_start_edge_2 = (chain_face.chain_prev.first + 1) % chain_face_vertex_count;
					size_t const chain_face_end_edge_2 = (chain_face.chain_next.first + 1) % chain_face_vertex_count;
					// Positive arc (start 2 to end 1) - adding vertexes with ascending numbers.
					size_t const chain_face_vertexes_positive =
							((chain_face.chain_next.first < chain_face_start_edge_2)
									? chain_face_vertex_count
									: 0) +
							chain_face.chain_next.first - chain_face_start_edge_2;
					// Negative arc (start 1 to end 2) - adding vertexes with descending numbers.
					size_t const chain_face_vertexes_negative =
							((chain_face.chain_prev.first < chain_face_end_edge_2)
									? chain_face_vertex_count
									: 0) +
							chain_face.chain_prev.first - chain_face_end_edge_2;
					size_t const chain_face_vertexes_to_add =
							chain_face_vertexes_positive + chain_face_vertexes_negative;
					strip.reserve(strip.size() + chain_face_vertexes_to_add);
					// In cases like:
					// 0--1--2
					// | /| /
					// |/ |/
					// 4--3
					//
					// 0--1--2
					// |\ | /
					// | \|/
					// 4--3
					// (assuming 0 to 4 is the first edge, and 2 to 3 is the last - not 1 to 2)
					// treat the last vertex separately, so regardless of the direction of the strip, in case of n
					// vertices on one side and n + 1 on another the spacing will be the same, otherwise, while in one
					// direction the strip would be 04132, in another, without the separate handling of the last odd
					// vertex, it'd be something like 40302, with the vertex 1 skipped completely, thus lowering the
					// resolution - instead, in this case it should be 403132, with a degenerate triangle.
					size_t const chain_face_vertex_even_mask = ~(chain_face_vertexes_to_add & size_t(1));
					size_t const chain_face_vertexes_positive_even =
							chain_face_vertexes_positive & chain_face_vertex_even_mask;
					size_t const chain_face_vertexes_negative_even =
							chain_face_vertexes_negative & chain_face_vertex_even_mask;
					size_t const chain_face_vertex_pairs_to_add =
							std::max(chain_face_vertexes_positive_even, chain_face_vertexes_negative_even);
					// Forward:
					// The direction of the currently last edge of the strip matches the winding order of face.
					// One edge in the positive direction is already added, the first new vertex should be in the
					// negative direction.
					// Backward:
					// The direction of the currently last edge of the strip opposes the winding order of face.
					// One edge in the negative direction is already added, the first new vertex should be in the
					// positive direction.
					bool chain_face_add_forward =
							strip[strip.size() - 2] == chain_face.vertexes[chain_face.chain_prev.first] &&
									strip.back() == chain_face.vertexes[chain_face_start_edge_2];
					assert(
							chain_face_add_forward ||
							(strip[strip.size() - 2] == chain_face.vertexes[chain_face_start_edge_2] &&
									strip.back() == chain_face.vertexes[chain_face.chain_prev.first]));
					for (size_t chain_face_vertex_pair_to_add = 0;
							chain_face_vertex_pair_to_add < chain_face_vertex_pairs_to_add;
							++chain_face_vertex_pair_to_add) {
						size_t const chain_face_new_vertexes_wrapping[2] = {
							// Along the positive arc.
							// First for backward, second for forward.
							chain_face_start_edge_2 +
									(1 + chain_face_vertex_pair_to_add) * chain_face_vertexes_positive_even /
											chain_face_vertex_pairs_to_add,
							// Along the negative arc.
							// First for forward, second for backward.
							// Before subtracting, adding chain_face_vertex_count to avoid overflow.
							chain_face_vertex_count + chain_face.chain_prev.first -
									(1 + chain_face_vertex_pair_to_add) * chain_face_vertexes_negative_even /
											chain_face_vertex_pairs_to_add,
						};
						for (size_t chain_face_vertex_side = 0; chain_face_vertex_side < 2; ++chain_face_vertex_side) {
							strip.push_back(chain_face.vertexes[
									(chain_face_new_vertexes_wrapping[
											chain_face_vertex_side ^ size_t(chain_face_add_forward)]) %
											chain_face_vertex_count]);
						}
					}
					if (chain_face_vertexes_to_add & 1) {
						// Add the last odd vertex, with a degenerate triangle if needed to make sure the last two
						// vertexes are the first two vertexes of the next subdivision face in the chain.
						// Forward:
						// Last added vertex along the positive arc.
						// Can add an odd vertex from the negative arc immediately, from positive with a degenerate.
						// Backward:
						// Last added vertex along the negative arc.
						// Can add an odd vertex from the positive arc immediately, from negative with a degenerate.
						if (chain_face_vertexes_positive & 1) {
							// Need to add an odd vertex from the positive arc.
							if (chain_face_add_forward) {
								strip.push_back(strip[strip.size() - 2]);
							}
							strip.push_back(chain_face.vertexes[
									(chain_face_start_edge_2 + chain_face_vertexes_positive) %
											chain_face_vertex_count]);
						} else {
							// Need to add an odd vertex from the negative arc.
							if (!chain_face_add_forward) {
								strip.push_back(strip[strip.size() - 2]);
							}
							// Before subtracting, adding chain_face_vertex_count to avoid overflow.
							strip.push_back(chain_face.vertexes[
									(chain_face_vertex_count + chain_face.chain_prev.first -
											chain_face_vertexes_negative) %
											chain_face_vertex_count]);
						}
					}
					chain_next_face_number = chain_face.chain_next.second;
				}
			}
			// Last subdivision face - from the fixed edge.
			if (chain.second != chain.first) {
				subdivision_face const & chain_last_face = subdivision_faces[chain.second];
				size_t const chain_last_face_vertex_count = chain_last_face.vertexes.size();
				size_t const chain_last_face_start_edge_2 =
						(chain_last_face.chain_prev.first + 1) % chain_last_face_vertex_count;
				bool chain_last_face_add_forward =
						strip[strip.size() - 2] == chain_last_face.vertexes[chain_last_face.chain_prev.first] &&
								strip.back() == chain_last_face.vertexes[chain_last_face_start_edge_2];
				// Forward:
				// Last vertex added towards increasing vertex number in the face.
				// The first new vertex must be added from decreasing vertex numbers.
				// Backward:
				// Last vertex added towards decreasing vertex number in the face.
				// The first new vertex must be added from increasing vertex numbers.
				assert(
						chain_last_face_add_forward ||
						(strip[strip.size() - 2] == chain_last_face.vertexes[chain_last_face_start_edge_2] &&
								strip.back() == chain_last_face.vertexes[chain_last_face.chain_prev.first]));
				strip.reserve(strip.size() + (chain_last_face_vertex_count - 2));
				for (size_t chain_last_face_vertex_to_add = 2;
						chain_last_face_vertex_to_add < chain_last_face_vertex_count;
						++chain_last_face_vertex_to_add) {
					size_t const chain_last_face_vertex_pair_to_add = chain_last_face_vertex_to_add >> 1;
					size_t chain_last_face_new_vertex_wrapping;
					if ((chain_last_face_vertex_to_add & 1) ^ size_t(chain_last_face_add_forward)) {
						// Before subtracting, adding chain_last_face_vertex_count to avoid overflow.
						chain_last_face_new_vertex_wrapping =
								chain_last_face_vertex_count + chain_last_face.chain_prev.first -
								chain_last_face_vertex_pair_to_add;
					} else {
						chain_last_face_new_vertex_wrapping =
								chain_last_face_start_edge_2 + chain_last_face_vertex_pair_to_add;
					}
					strip.push_back(chain_last_face.vertexes[
							chain_last_face_new_vertex_wrapping % chain_last_face_vertex_count]);
				}
			}
			// Make sure the vertex counts in all strips can fit in 16 bits.
			size_t const strip_vertex_count = strip.size();
			// The `strip` reference may become outdated due to reallocations when adding to `face_polygons.strips`.
			for (size_t strip_vertex_number = UINT16_MAX;
					strip_vertex_number < strip_vertex_count;
					strip_vertex_number += UINT16_MAX - 2) {
				std::vector<uint16_t> & new_strip = face_polygons.strips.emplace_back();
				size_t const strip_copy_vertex_count =
						std::min(size_t(UINT16_MAX - 2), strip_vertex_count - strip_vertex_number);
				new_strip.reserve(2 + strip_copy_vertex_count);
				std::vector<uint16_t> const & full_strip = face_polygons.strips[strip_number];
				new_strip.push_back(full_strip[strip_vertex_number - 2]);
				new_strip.push_back(full_strip[strip_vertex_number - 1]);
				std::copy(
						full_strip.cbegin() + strip_vertex_number,
						full_strip.cbegin() + strip_vertex_number + strip_copy_vertex_count,
						std::back_inserter(new_strip));
			}
			strip.resize(std::min(size_t(UINT16_MAX), strip_vertex_count));
		}
	}
}

void write_polygons_to_obj(std::ostream & obj, gbx_map const & map) {
	size_t last_plane_number = 0;
	size_t next_vertex_number = 1;
	for (gbx_polygons_deserialized const & polygon : map.polygons) {
		gbx_face const & face = map.faces[polygon.face_number];
		if (!map.textures.empty()) {
			obj << "# " << map.textures[face.texture].name << std::endl;
		}
		obj << "# s " << face.texinfo_vectors[0].v[0] << ' ' <<
				face.texinfo_vectors[0].v[1] << ' ' <<
				face.texinfo_vectors[0].v[2] << ' ' <<
				face.texinfo_vectors[0].v[3] << std::endl;
		obj << "# |s| " <<
				std::sqrt(
						face.texinfo_vectors[0].v[0] * face.texinfo_vectors[0].v[0] +
						face.texinfo_vectors[0].v[1] * face.texinfo_vectors[0].v[1] +
						face.texinfo_vectors[0].v[2] * face.texinfo_vectors[0].v[2]) <<
				std::endl;
		obj << "# t " << face.texinfo_vectors[1].v[0] << ' ' <<
				face.texinfo_vectors[1].v[1] << ' ' <<
				face.texinfo_vectors[1].v[2] << ' ' <<
				face.texinfo_vectors[1].v[3] << std::endl;
		obj << "# |t| " <<
				std::sqrt(
						face.texinfo_vectors[1].v[0] * face.texinfo_vectors[1].v[0] +
						face.texinfo_vectors[1].v[1] * face.texinfo_vectors[1].v[1] +
						face.texinfo_vectors[1].v[2] * face.texinfo_vectors[1].v[2]) <<
				std::endl;
		++last_plane_number;
		gbx_plane const & plane = map.planes[face.plane];
		obj << "vn " << plane.normal.v[0] << ' ' << plane.normal.v[1] << ' ' << plane.normal.v[2] << std::endl;
		size_t const polygon_first_vertex_number = next_vertex_number;
		for (gbx_polygon_vertex const & vertex : polygon.vertexes) {
			obj << "v " << vertex.xyz.v[0] << ' ' << vertex.xyz.v[1] << ' ' << vertex.xyz.v[2] << std::endl;
			obj << "vt " << vertex.st[0] << ' ' << vertex.st[1] << std::endl;
		}
		next_vertex_number += polygon.vertexes.size();
		for (std::vector<uint16_t> const & strip : polygon.strips) {
			obj << '#';
			for (uint16_t strip_vertex_index : strip) {
				obj << ' ' << strip_vertex_index;
			}
			obj << std::endl;
			for (size_t strip_vertex_index = 2; strip_vertex_index < strip.size(); ++strip_vertex_index) {
				if (strip[strip_vertex_index - 2] == strip[strip_vertex_index]) {
					// Degenerate triangle reversing the strip.
					continue;
				}
				obj << 'f';
				for (size_t polygon_vertex_number = 0; polygon_vertex_number < 3; ++polygon_vertex_number) {
					size_t const face_vertex_number =
							polygon_first_vertex_number + strip[strip_vertex_index - 2 + polygon_vertex_number];
					obj << ' ' << face_vertex_number << '/' << face_vertex_number << '/' << last_plane_number;
				}
				obj << std::endl;
			}
		}
	}
}

}
