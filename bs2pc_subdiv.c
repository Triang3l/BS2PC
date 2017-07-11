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
	unsigned short firstIndex;
	unsigned short indexCount;
	bool hasDegenerates;
} bs2pc_polyStrip_t;
typedef struct {
	unsigned short indices[BS2PC_MAX_POLY_STRIP_INDICES];
	unsigned int indexCount;
	bs2pc_polyStrip_t strips[BS2PC_MAX_POLY_STRIPS];
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
	unsigned short indicesOfVerts[64];
	bs2pc_polyStripSet_t *stripSet;
	bs2pc_polyStrip_t *strip;
	unsigned short *indices;

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

	for (i = 0; i < numverts; ++i) {
		indicesOfVerts[i] = BS2PC_AddPolyVertex(verts + i * 3);
	}

	stripSet = &bs2pc_polyStripSets[bs2pc_currentPolyStripSet];
	if (stripSet->stripCount >= BS2PC_MAX_POLY_STRIPS || stripSet->indexCount + numverts > BS2PC_MAX_POLY_STRIP_INDICES) {
		fputs("Too many triangle strips or vertex indices after face subdivision.\n", stderr);
		exit(EXIT_FAILURE);
	}
	strip = &stripSet->strips[stripSet->stripCount++];
	strip->firstIndex = stripSet->indexCount;
	strip->indexCount = numverts;
	strip->hasDegenerates = false;
	indices = &stripSet->indices[stripSet->indexCount];
	for (i = 0; i < numverts; ++i) {
		*(indices++) = indicesOfVerts[(i & 1) ? (i >> 1) : (numverts - 1 - (i >> 1))];
	}
	stripSet->indexCount += numverts;
}

static void BS2PC_MergeStrips() {
	typedef enum {
		MERGE_NONE,
		MERGE_COMBINE, // 3 0 2 1 + 5 4 3 0 = 5 4 3 0 2 1
		MERGE_COMBINE_FLIP, // 10 11 8 9... + 11 10 13... = ...13 10 11 8 9...
		MERGE_DEGENERATE_COMBINE, // ...17 15 16 + 16 15 19... = ...17 15 16 15 19...
		MERGE_DEGENERATE_COMBINE_FLIP,
		MERGE_DEGENERATE_INVERT // 9 6 8 4 7 5 + 9 8 11 10 13 12 15 14 = 5 4 7 6 8 9 8 11 10 13 12 15 14
	} mergeType_t;
	mergeType_t mergeType;

	const bs2pc_polyStripSet_t *setSource;
	bs2pc_polyStripSet_t *setTarget;
	const unsigned short *indicesSource, *indicesTo = NULL, *indicesFrom = NULL;
	unsigned int indexCountTo, indexCountFrom;
	unsigned short *indicesTarget;

	unsigned int stripIndexTo = 0, stripIndexFrom = 0;
	const bs2pc_polyStrip_t *stripTo = NULL, *stripFrom = NULL;

	const bs2pc_polyStrip_t *stripSource;
	bs2pc_polyStrip_t *stripTarget;
	unsigned int stripIndex;
	unsigned int vertexIndex;

	while (true) {
		mergeType = MERGE_NONE;

		// Find two strips to merge.

		setSource = &bs2pc_polyStripSets[bs2pc_currentPolyStripSet];
		indicesSource = setSource->indices;

		for (stripIndexTo = 0; stripIndexTo < setSource->stripCount; ++stripIndexTo) {
			stripTo = &setSource->strips[stripIndexTo];
			indicesTo = &indicesSource[stripTo->firstIndex];
			indexCountTo = stripTo->indexCount;
			for (stripIndexFrom = 0; stripIndexFrom < setSource->stripCount; ++stripIndexFrom) {
				if (stripIndexFrom == stripIndexTo) {
					continue;
				}
				stripFrom = &setSource->strips[stripIndexFrom];
				indicesFrom = &indicesSource[stripFrom->firstIndex];
				indexCountFrom = stripFrom->indexCount;

				if (indicesTo[0] == indicesFrom[indexCountFrom - 2] && indicesTo[1] == indicesFrom[indexCountFrom - 1]) {
					mergeType = MERGE_COMBINE;
				} else if (indicesTo[0] == indicesFrom[1] && indicesTo[1] == indicesFrom[0]) {
					mergeType = MERGE_COMBINE_FLIP;
				} else if (indicesTo[indexCountTo - 2] == indicesFrom[1] && indicesTo[indexCountTo - 1] == indicesFrom[0]) {
					mergeType = MERGE_DEGENERATE_COMBINE;
				} else if (indicesTo[0] == indicesFrom[0] && indicesTo[1] == indicesFrom[1]) {
					mergeType = MERGE_DEGENERATE_COMBINE_FLIP;
				} else if (indicesTo[0] == indicesFrom[0] && indicesTo[1] == indicesFrom[2] && !stripFrom->hasDegenerates) {
					mergeType = MERGE_DEGENERATE_INVERT;
				}

				if (mergeType != MERGE_NONE) {
					break;
				}
			}
			if (mergeType != MERGE_NONE) {
				break;
			}
		}

		if (mergeType == MERGE_NONE) {
			break;
		}

		// Write the new strips to the set.

		setTarget = &bs2pc_polyStripSets[bs2pc_currentPolyStripSet ^ 1];
		setTarget->indexCount = setTarget->stripCount = 0;
		for (stripIndex = 0; stripIndex < setSource->stripCount; ++stripIndex) {
			if (stripIndex == stripIndexFrom) {
				continue;
			}
			stripSource = &setSource->strips[stripIndex];
			indicesSource = &setSource->indices[0];
			stripTarget = &setTarget->strips[setTarget->stripCount];
			stripTarget->firstIndex = setTarget->indexCount;
			indicesTarget = &setTarget->indices[stripTarget->firstIndex];
			if (stripIndex == stripIndexTo) {
				if (mergeType == MERGE_COMBINE) {
					memcpy(indicesTarget, indicesFrom, indexCountFrom * sizeof(unsigned short));
					memcpy(&indicesTarget[indexCountFrom], &indicesTo[2], (indexCountTo - 2) * sizeof(unsigned short));
					stripTarget->indexCount = indexCountFrom + indexCountTo - 2;
					stripTarget->hasDegenerates = (stripFrom->hasDegenerates || stripTo->hasDegenerates);
				} else if (mergeType == MERGE_COMBINE_FLIP) {
					for (vertexIndex = 0; vertexIndex < indexCountFrom; ++vertexIndex) {
						indicesTarget[vertexIndex] = indicesFrom[indexCountFrom - 1 - vertexIndex];
					}
					memcpy(&indicesTarget[indexCountFrom], &indicesTo[2], (indexCountTo - 2) * sizeof(unsigned short));
					stripTarget->indexCount = indexCountFrom + indexCountTo - 2;
					stripTarget->hasDegenerates = (stripFrom->hasDegenerates || stripTo->hasDegenerates);
				} else if (mergeType == MERGE_DEGENERATE_COMBINE) {
					memcpy(indicesTarget, indicesTo, indexCountTo * sizeof(unsigned short));
					memcpy(&indicesTarget[indexCountTo], &indicesFrom[1], (indexCountFrom - 1) * sizeof(unsigned short));
					stripTarget->indexCount = indexCountTo + indexCountFrom - 1;
					stripTarget->hasDegenerates = true;
				} else if (mergeType == MERGE_DEGENERATE_COMBINE_FLIP) {
					for (vertexIndex = 0; vertexIndex < indexCountFrom; ++vertexIndex) {
						indicesTarget[vertexIndex] = indicesFrom[indexCountFrom - 1 - vertexIndex];
					}
					memcpy(&indicesTarget[indexCountFrom], &indicesTo[1], (indexCountTo - 1) * sizeof(unsigned short));
					stripTarget->indexCount = indexCountFrom + indexCountTo - 1;
					stripTarget->hasDegenerates = true;
				} else if (mergeType == MERGE_DEGENERATE_INVERT) {
					stripTarget->indexCount = 0;
					if ((indexCountFrom & 1) == 0) {
						indicesTarget[stripTarget->indexCount++] = indicesFrom[indexCountFrom - 1];
					}
					for (vertexIndex = indexCountFrom - 1 - ((indexCountFrom & 1) ^ 1); vertexIndex >= 2; vertexIndex -= 2) {
						indicesTarget[stripTarget->indexCount++] = indicesFrom[vertexIndex - 1];
						indicesTarget[stripTarget->indexCount++] = indicesFrom[vertexIndex];
					}
					memcpy(&indicesTarget[stripTarget->indexCount], indicesTo, indexCountTo * sizeof(unsigned short));
					stripTarget->indexCount += indexCountTo;
					stripTarget->hasDegenerates = true;
				}
			} else {
				memcpy(indicesTarget, &indicesSource[stripSource->firstIndex], stripSource->indexCount * sizeof(unsigned short));
				stripTarget->indexCount = stripSource->indexCount;
				stripTarget->hasDegenerates = stripSource->hasDegenerates;
			}
			setTarget->indexCount += stripTarget->indexCount;
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

	const bs2pc_polyStripSet_t *stripSet;
	unsigned int subdivSize;
	unsigned char *subdiv;
	unsigned int subdivPosition;
	unsigned int subdivVertIndex;
	unsigned int subdivStripIndex;
	const bs2pc_polyStrip_t *subdivStrip;

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
		subdivSize += (1 + stripSet->strips[subdivStripIndex].indexCount) * sizeof(unsigned short);
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

	for (subdivStripIndex = 0, subdivStrip = stripSet->strips; subdivStripIndex < stripSet->stripCount; ++subdivStripIndex, ++subdivStrip) {
		*((unsigned short *) (subdiv + subdivPosition)) = subdivStrip->indexCount;
		subdivPosition += sizeof(unsigned short);
		memcpy(subdiv + subdivPosition, &stripSet->indices[subdivStrip->firstIndex],
				subdivStrip->indexCount * sizeof(unsigned short));
		subdivPosition += subdivStrip->indexCount * sizeof(unsigned short);
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