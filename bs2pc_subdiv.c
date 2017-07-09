#include "bs2pc.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float bs2pc_subdivideSize;

#define BS2PC_MAX_POLY_VERTS 1024
static float bs2pc_polyPositions[BS2PC_MAX_POLY_VERTS][3];
#define BS2PC_POLY_POSITION_EPSILON 0.001f
static unsigned int bs2pc_polyVertCount;

#define BS2PC_MAX_POLY_STRIP_INDICES BS2PC_MAX_POLY_VERTS
#define BS2PC_MAX_POLY_STRIPS 256
typedef struct {
	unsigned short indices[BS2PC_MAX_POLY_STRIP_INDICES];
	unsigned int indexCount;
	unsigned int strips[BS2PC_MAX_POLY_STRIPS][2];
	unsigned int stripCount;
} bs2pc_polyStripSet_t;
static bs2pc_polyStripSet_t bs2pc_polyStripSets[2];
static unsigned int bs2pc_currentPolyStripSet;

static unsigned short bs2pc_polyCurrentStripIndices[BS2PC_MAX_POLY_STRIP_INDICES];
static unsigned short bs2pc_polyCurrentStripTris[BS2PC_MAX_POLY_STRIP_INDICES];
static unsigned int bs2pc_polyCurrentStripLength;

static void BS2PC_BoundPoly(unsigned int numverts, float *verts, float mins[3], float maxs[3]) {
	unsigned int i, j;
	float *v;

	mins[0] = mins[1] = mins[2] = 9999.0f;
	maxs[0] = maxs[1] = maxs[2] = -9999.0f;
	v = verts;
	for (i = 0; i < numverts; ++i) {
		for (j = 0; j < 3; ++j, ++v) {
			if (*v < mins[j]) {
				mins[j] = *v;
			}
			if (*v > maxs[j]) {
				maxs[j] = *v;
			}
		}
	}
}

static unsigned int BS2PC_AddPolyVertex(float vert[3]) {
	unsigned int vertIndex;
	float *otherVert;
	for (vertIndex = 0, otherVert = &bs2pc_polyPositions[0][0]; vertIndex < bs2pc_polyVertCount; ++vertIndex, otherVert += 3) {
		if (fabsf(vert[0] - otherVert[0]) <= BS2PC_POLY_POSITION_EPSILON &&
				fabsf(vert[1] - otherVert[1]) <= BS2PC_POLY_POSITION_EPSILON &&
				fabsf(vert[2] - otherVert[2]) <= BS2PC_POLY_POSITION_EPSILON) {
			return vertIndex;
		}
	}
	if (bs2pc_polyVertCount >= BS2PC_MAX_POLY_VERTS) {
		fputs("Too many vertices after face subdivision.\n", stderr);
		exit(EXIT_FAILURE);
	}
	otherVert[0] = vert[0];
	otherVert[1] = vert[1];
	otherVert[2] = vert[2];
	return bs2pc_polyVertCount++;
}

static void BS2PC_SubdividePolygon(unsigned int numverts, float *verts) {
	unsigned int i, j, k;
	float mins[3], maxs[3];
	float m;
	float *v;
	float front[64 * 3], back[64 * 3], *dest;
	unsigned int f, b;
	float dist[64];
	float frac;
	bs2pc_polyStripSet_t *stripSet;
	unsigned int *strip;
	unsigned short *indices;
	unsigned int vertexIndex;

	if (numverts > 60) {
		fputs("Too many vertices in a face subdivision step\n.", stderr);
		exit(EXIT_FAILURE);
	}

	BS2PC_BoundPoly(numverts, verts, mins, maxs);

	for (i = 0; i < 3; ++i) {
		m = (mins[i] + maxs[i]) * 0.5f;
		m = bs2pc_subdivideSize * floorf(m * (1.0f / bs2pc_subdivideSize) + 0.5f);
		if ((maxs[i] - m < 8.0f) || (m - mins[i] < 8.0f)) {
			continue;
		}

		// Cut it.
		v = verts + i;
		for (j = 0; j < numverts; ++j, v += 3) {
			dist[j] = *v - m;
		}

		// Wrap cases.
		dist[j] = dist[0];
		v -= i;
		v[0] = verts[0];
		v[1] = verts[1];
		v[2] = verts[2];

		f = b = 0;
		v = verts;
		for (j = 0; j < numverts; ++j, v += 3) {
			if (dist[j] >= 0.0f) {
				dest = front + f * 3;
				dest[0] = v[0];
				dest[1] = v[1];
				dest[2] = v[2];
				++f;
			}
			if (dist[j] <= 0.0f) {
				dest = back + b * 3;
				dest[0] = v[0];
				dest[1] = v[1];
				dest[2] = v[2];
				++b;
			}
			if (dist[j] == 0.0f || dist[j + 1] == 0.0f) {
				continue;
			}
			if ((dist[j] > 0.0f) != (dist[j + 1] > 0.0f)) {
				// Clip point.
				frac = dist[j] / (dist[j] - dist[j + 1]);
				for (k = 0; k < 3; ++k) {
					front[f * 3 + k] = back[b * 3 + k] = v[k] + frac * (v[3 + k] - v[k]);
				}
				++f;
				++b;
			}
		}

		BS2PC_SubdividePolygon(f, front);
		BS2PC_SubdividePolygon(b, back);
		return;
	}

	if (numverts < 3) {
		// Shouldn't happen, but still.
		return;
	}

	stripSet = &bs2pc_polyStripSets[bs2pc_currentPolyStripSet];
	if (stripSet->stripCount >= BS2PC_MAX_POLY_STRIPS || stripSet->indexCount + numverts > BS2PC_MAX_POLY_STRIP_INDICES) {
		fputs("Too many triangle strips or vertex indices after face subdivision.\n", stderr);
		exit(EXIT_FAILURE);
	}
	strip = stripSet->strips[stripSet->stripCount++];
	strip[0] = stripSet->indexCount;
	strip[1] = numverts;
	indices = &stripSet->indices[stripSet->indexCount];
	for (i = 0; i < numverts; ++i) {
		if ((i & 1) == 0) {
			vertexIndex = 1 + (i >> 1);
		} else if (i == 1) {
			vertexIndex = 0;
		} else {
			vertexIndex = numverts - (i >> 1);
		}
		*(indices++) = BS2PC_AddPolyVertex(verts + vertexIndex * 3);
	}
	stripSet->indexCount += numverts;
}

static void BS2PC_MergeStrips() {
	bool merge;

	const bs2pc_polyStripSet_t *setSource;
	bs2pc_polyStripSet_t *setTarget;
	const unsigned short *indicesSource;
	unsigned short *indicesTarget;

	unsigned int stripIndexTo = 0, stripIndexFrom = 0;
	const unsigned int *stripTo = NULL, *stripFrom = NULL;
	unsigned short endsTo[2][3]; // { 0, 1, 2 }, { l-1, l-2, l-3 }
	unsigned short endsFrom[6]; // 0, 1, 2, l-1, l-2, l-3

	unsigned int mergeToSecond = 0; // 1 or 2. The first is considered 0 because 10=10 is 01=01 as well and 10=01 is 01=10.
	bool mergeFlipTo = false;
	unsigned int mergeFrom[2] = { 0, 0 }; // to[0] == from[mergeFrom[0]] && to[mergeToSecond] == from[mergeFrom[1]]
	unsigned int mergeFromOffset = 0;

	const unsigned int *stripSource;
	unsigned int *stripTarget;
	unsigned int stripIndex, vertexIndex, vertexCount;

	// Merging is always performed by attaching stripFrom to the beginning or the end of stripFrom.
	// So many cases can be implemented only for one direction as the loop will swap stripFrom and stripTo.
	while (true) {
		merge = false;

		// Finding two strips to merge.
		// For two strips to be merged, 01 or 02 side of each must be in the first triangle of the other.
		setSource = &bs2pc_polyStripSets[bs2pc_currentPolyStripSet];
		indicesSource = setSource->indices;
		for (stripIndexTo = 0; stripIndexTo < setSource->stripCount; ++stripIndexTo) {
			stripTo = &setSource->strips[stripIndexTo][0];
			memcpy(endsTo[0], &indicesSource[stripTo[0]], 3 * sizeof(unsigned short));
			endsTo[1][0] = indicesSource[stripTo[0] + stripTo[1] - 1];
			endsTo[1][1] = indicesSource[stripTo[0] + stripTo[1] - 2];
			endsTo[1][2] = indicesSource[stripTo[0] + stripTo[1] - 3];

			for (stripIndexFrom = 0; stripIndexFrom < setSource->stripCount; ++stripIndexFrom) {
				if (stripIndexFrom == stripIndexTo) {
					continue;
				}
				stripFrom = &setSource->strips[stripIndexFrom][0];
				memcpy(endsFrom, &indicesSource[stripFrom[0]], 3 * sizeof(unsigned short));
				endsFrom[3] = indicesSource[stripFrom[0] + stripFrom[1] - 1];
				endsFrom[4] = indicesSource[stripFrom[0] + stripFrom[1] - 2];
				endsFrom[5] = indicesSource[stripFrom[0] + stripFrom[1] - 3];

				mergeFlipTo = false;
				while (true) {
					// Finding the match for the first vertex of stripTo.
					for (mergeFrom[0] = 0; mergeFrom[0] <= 5; ++mergeFrom[0]) {
						if (endsTo[mergeFlipTo][0] == endsFrom[mergeFrom[0]]) {
							break;
						}
					}
					mergeFromOffset = (mergeFrom[0] >= 3 ? 3 : 0);

					// Finding the second vertex of the edge to merge.
					merge = true;
					switch (mergeFrom[0] - mergeFromOffset) {
					case 0: // 0* = 0*: 01 = 01 (01 = 02 and 02 = 02 seem impossible so not handled).
						if (endsTo[mergeFlipTo][1] == endsFrom[mergeFromOffset + 1]) {
							mergeToSecond = 1;
							mergeFrom[1] = mergeFromOffset + 1;
						} else {
							merge = false;
						}
						break;
					case 1: // 0* = 1*: 01 = 10, 02 = 10.
					case 2: // 0* = 2*: 01 = 20, 02 = 20.
						mergeFrom[1] = mergeFromOffset;
						if (endsTo[mergeFlipTo][1] == endsFrom[mergeFromOffset]) {
							mergeToSecond = 1;
						} else if (endsTo[mergeFlipTo][2] == endsFrom[mergeFromOffset]) {
							mergeToSecond = 2;
						} else {
							merge = false;
						}
						break;
					default: // 6 - first match not found.
						merge = false;
						break;
					}

					// Break if merged or completely failed to find common vertices.
					if (merge || mergeFlipTo) {
						break;
					}
					mergeFlipTo = true;
				}

				if (merge) {
					break;
				}
			}
			if (merge) {
				break;
			}
		}

		if (!merge) {
			break;
		}

		// Write the new strips to the set.
		setTarget = &bs2pc_polyStripSets[bs2pc_currentPolyStripSet ^ 1];
		setTarget->indexCount = setTarget->stripCount = 0;
		for (stripIndex = 0; stripIndex < setSource->stripCount; ++stripIndex) {
			if (stripIndex == stripIndexFrom) {
				continue;
			}
			stripSource = &setSource->strips[stripIndex][0];
			indicesSource = &setSource->indices[0];
			stripTarget = &setTarget->strips[setTarget->stripCount][0];
			stripTarget[0] = setTarget->indexCount;
			indicesTarget = &setTarget->indices[stripTarget[0]];
			if (stripIndex == stripIndexTo) {
				// Stop using mergeFromOffset and mergeFlipTo for simplicity as we don't need two sets anymore.
				if (mergeFromOffset != 0) {
					memcpy(endsFrom, &endsFrom[mergeFromOffset], 3 * sizeof(unsigned short));
					mergeFrom[0] -= mergeFromOffset;
					mergeFrom[1] -= mergeFromOffset;
				}
				if (mergeFlipTo) {
					memcpy(endsTo[0], endsTo[1], 3 * sizeof(unsigned short));
				}

				// Unpermuted triangles of the "from" strip in the beginning.
				vertexCount = stripFrom[1] - 3;
				if (mergeFromOffset == 0) {
					for (vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
						indicesTarget[vertexIndex] = indicesSource[stripFrom[0] + stripFrom[1] - 1 - vertexIndex];
					}
				} else {
					memcpy(indicesTarget, &indicesSource[stripFrom[0]], vertexCount * sizeof(unsigned short));
				}
				stripTarget[1] = vertexCount;

				// Bridge.
				if (mergeToSecond <= mergeFrom[0]) {
					// Continuation (01-10, 01-20, 02-20).
					indicesTarget[stripTarget[1]++] = endsFrom[(mergeFrom[0] == 1 || mergeFrom[1] == 1) ? 2 : 1];
					indicesTarget[stripTarget[1]++] = endsTo[0][0];
					indicesTarget[stripTarget[1]++] = endsTo[0][mergeToSecond];
					indicesTarget[stripTarget[1]++] = endsTo[0][mergeToSecond ^ 3];
				} else {
					// Flip across degenerate (01-01, 02-10).
					if (mergeFrom[0] != 0) {
						// 02-10.
						indicesTarget[stripTarget[1]++] = endsFrom[mergeFrom[0] ^ 3];
						indicesTarget[stripTarget[1]++] = endsFrom[mergeFrom[0]];
						indicesTarget[stripTarget[1]++] = endsFrom[0];
						indicesTarget[stripTarget[1]++] = endsFrom[mergeFrom[0]];
						indicesTarget[stripTarget[1]++] = endsTo[0][mergeToSecond];
					} else {
						// 01-01.
						indicesTarget[stripTarget[1]++] = endsFrom[mergeFrom[1] ^ 3];
						indicesTarget[stripTarget[1]++] = endsFrom[mergeFrom[1]];
						indicesTarget[stripTarget[1]++] = endsFrom[0];
						indicesTarget[stripTarget[1]++] = endsFrom[mergeFrom[1]];
						indicesTarget[stripTarget[1]++] = endsTo[0][mergeToSecond ^ 3];
					}
				}

				// Unpermuted triangles of the "to" strip in the end.
				vertexCount = stripTo[1] - 3;
				if (mergeFlipTo) {
					for (vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
						indicesTarget[stripTarget[1] + vertexIndex] = indicesSource[stripTo[0] + stripTo[1] - 4 - vertexIndex];
					}
				} else {
					memcpy(&indicesTarget[stripTarget[1]], &indicesSource[stripTo[0] + 3], vertexCount * sizeof(unsigned short));
				}
				stripTarget[1] += vertexCount;
			} else {
				memcpy(indicesTarget, &indicesSource[stripSource[0]], stripSource[1] * sizeof(unsigned short));
				stripTarget[1] = stripSource[1];
			}
			setTarget->indexCount += stripTarget[1];
			++setTarget->stripCount;
		}
		bs2pc_currentPolyStripSet ^= 1;
	}
}

unsigned char *BS2PC_SubdivideIdSurface(unsigned int faceIndex, unsigned int faceFlags, const dmiptex_id_t *texture, unsigned int *outSize) {
	const dface_id_t *face = ((const dface_id_t *) BS2PC_IdLump(LUMP_ID_FACES)) + faceIndex;
	const int *mapSurfedges = (const int *) BS2PC_IdLump(LUMP_ID_SURFEDGES);
	const dedge_t *mapEdges = (const dedge_t *) BS2PC_IdLump(LUMP_ID_EDGES);
	const dvertex_id_t *mapVertexes = (const dvertex_id_t *) BS2PC_IdLump(LUMP_ID_VERTEXES), *mapVertex;
	const dtexinfo_id_t *texinfo = ((const dtexinfo_id_t *) BS2PC_IdLump(LUMP_ID_TEXINFO)) + face->texinfo;

	float verts[64 * 3], *vert;
	unsigned int numverts = 0;
	unsigned int i;
	int lindex;
	const float *vecs = &texinfo->vecs[0][0];

	float scaleS = 1.0f / (float) texture->width, scaleT = 1.0f / (float) texture->height;
	float offsetS, offsetT;
	short textureMins[2];

	bs2pc_polyStripSet_t *stripSet;
	unsigned int subdivSize;
	unsigned char *subdiv;
	unsigned int subdivPosition;
	unsigned int subdivVertIndex;
	unsigned int subdivStripIndex, subdivStripVertexCount;

	for (i = 0, vert = verts; i < face->numedges; ++i) {
		lindex = mapSurfedges[face->firstedge + i];
		if (lindex > 0) {
			mapVertex = mapVertexes + mapEdges[lindex].v[0];
		} else {
			mapVertex = mapVertexes + mapEdges[-lindex].v[1];
		}
		*(vert++) = mapVertex->point[0];
		*(vert++) = mapVertex->point[1];
		*(vert++) = mapVertex->point[2];
		++numverts;
	}

	offsetS = (float) (int) ((BS2PC_DotProduct(verts, vecs) + vecs[3]) * scaleS);
	offsetT = (float) (int) ((BS2PC_DotProduct(verts, vecs + 4) + vecs[7]) * scaleT);
	BS2PC_CalcIdSurfaceExtents(face, textureMins, NULL);

	bs2pc_subdivideSize = ((faceFlags & SURF_DRAWTURB) ? 64.0f : 32.0f);
	bs2pc_currentPolyStripSet = 0;
	bs2pc_polyStripSets[0].indexCount = 0;
	bs2pc_polyStripSets[0].stripCount = 0;
	bs2pc_polyVertCount = 0;
	BS2PC_SubdividePolygon(numverts, verts);
	BS2PC_MergeStrips();
	stripSet = &bs2pc_polyStripSets[bs2pc_currentPolyStripSet];

	subdivSize = 2 * sizeof(unsigned int) /* face index, vertex count */ +
			bs2pc_polyVertCount * sizeof(bs2pc_polyvert_t) + sizeof(unsigned int) /* mesh count */;
	for (subdivStripIndex = 0; subdivStripIndex < stripSet->stripCount; ++subdivStripIndex) {
		subdivSize += (1 + stripSet->strips[subdivStripIndex][1]) * sizeof(unsigned short);
		if (subdivSize & 3) {
			subdivSize += sizeof(unsigned short);
		}
	}

	subdiv = (unsigned char *) BS2PC_Alloc(subdivSize, false);
	*((unsigned int *) subdiv) = faceIndex;
	*((unsigned int *) subdiv + 1) = bs2pc_polyVertCount;
	subdivPosition = 2 * sizeof(unsigned int);

	for (subdivVertIndex = 0; subdivVertIndex < bs2pc_polyVertCount; ++subdivVertIndex) {
		bs2pc_polyvert_t *subdivVert = (bs2pc_polyvert_t *) (subdiv + subdivPosition);
		const float *position = bs2pc_polyPositions[subdivVertIndex];
		float s, t;
		subdivVert->xyz[0] = position[0];
		subdivVert->xyz[1] = position[1];
		subdivVert->xyz[2] = position[2];
		s = BS2PC_DotProduct(position, vecs) + vecs[3];
		t = BS2PC_DotProduct(position, vecs + 4) + vecs[7];
		subdivVert->st[0] = s * scaleS - offsetS;
		subdivVert->st[1] = t * scaleT - offsetT;
		subdivVert->stoffset[0] = (unsigned char) ((s - (float) textureMins[0]) * (1.0f / 16.0f) + 0.5f);
		subdivVert->stoffset[1] = (unsigned char) ((t - (float) textureMins[1]) * (1.0f / 16.0f) + 0.5f);
		subdivVert->pad[0] = subdivVert->pad[1] = 0;
		subdivPosition += sizeof(bs2pc_polyvert_t);
	}

	*((unsigned int *) (subdiv + subdivPosition)) = stripSet->stripCount;
	subdivPosition += sizeof(unsigned int);
	
	for (subdivStripIndex = 0; subdivStripIndex < stripSet->stripCount; ++subdivStripIndex) {
		subdivStripVertexCount = stripSet->strips[subdivStripIndex][1];
		*((unsigned short *) (subdiv + subdivPosition)) = subdivStripVertexCount;
		subdivPosition += sizeof(unsigned short);
		memcpy(subdiv + subdivPosition, &stripSet->indices[stripSet->strips[subdivStripIndex][0]],
				subdivStripVertexCount * sizeof(unsigned short));
		subdivPosition += subdivStripVertexCount * sizeof(unsigned short);
		if (subdivPosition & 3) {
			*((unsigned short *) (subdiv + subdivPosition)) = /*cov*/0xfefe;
			subdivPosition += sizeof(unsigned short);
		}
	}

	if (outSize != NULL) {
		*outSize = subdivSize;
	}
	return subdiv;
}