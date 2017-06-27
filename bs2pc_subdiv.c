#include "bs2pc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float bs2pc_subdivideSize;

#define BS2PC_MAX_POLY_VERTS 1024
static float bs2pc_polyPositions[BS2PC_MAX_POLY_VERTS][3];
#define BS2PC_POLY_POSITION_EPSILON 0.001f
static unsigned int bs2pc_polyVertCount;

#define BS2PC_MAX_POLY_TRIS BS2PC_MAX_POLY_VERTS
static unsigned short bs2pc_polyTris[BS2PC_MAX_POLY_TRIS][3];
static unsigned char bs2pc_polyTrisUsed[BS2PC_MAX_POLY_TRIS];
static unsigned int bs2pc_polyTriCount;

#define BS2PC_MAX_POLY_STRIP_INDICES BS2PC_MAX_POLY_VERTS
#define BS2PC_MAX_POLY_STRIPS 256
static unsigned short bs2pc_polyStripIndices[BS2PC_MAX_POLY_STRIP_INDICES];
static unsigned int bs2pc_polyStripIndexCount;
static unsigned int bs2pc_polyStrips[BS2PC_MAX_POLY_STRIPS][2];
static unsigned int bs2pc_polyStripCount;

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
	unsigned short *triIndices, triFirstIndex, triPrevIndex;

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

	if (bs2pc_polyTriCount + (numverts - 2) > BS2PC_MAX_POLY_TRIS) {
		fputs("Too many triangles after face subdivision.\n", stderr);
		exit(EXIT_FAILURE);
	}
	triIndices = bs2pc_polyTris[bs2pc_polyTriCount];
	triFirstIndex = BS2PC_AddPolyVertex(verts);
	triPrevIndex = BS2PC_AddPolyVertex(verts + 3);
	verts += 6;
	for (i = 2; i < numverts; ++i, verts += 3) {
		*(triIndices++) = triFirstIndex;
		*(triIndices++) = triPrevIndex;
		triPrevIndex = BS2PC_AddPolyVertex(verts);
		*(triIndices++) = triPrevIndex;
	}
	bs2pc_polyTriCount += numverts - 2;
}

static void BS2PC_BuildStrip(unsigned int starttri, unsigned int startv) {
	unsigned int m1, m2;
	unsigned int j;
	const unsigned short *check;
	unsigned int k;
	unsigned int newvert;

	bs2pc_polyTrisUsed[starttri] = 2;

	check = bs2pc_polyTris[starttri];
	bs2pc_polyCurrentStripIndices[0] = check[startv % 3];
	bs2pc_polyCurrentStripIndices[1] = m2 = check[(startv + 1) % 3];
	bs2pc_polyCurrentStripIndices[2] = m1 = check[(startv + 2) % 3];

	bs2pc_polyCurrentStripTris[0] = starttri;
	bs2pc_polyCurrentStripLength = 1;

nexttri:
	for (j = starttri + 1; j < bs2pc_polyTriCount; ++j) {
		check = bs2pc_polyTris[j];
		for (k = 0; k < 3; ++k) {
			if (check[k] != m1) {
				continue;
			}
			if (check[(k + 1) % 3] != m2) {
				continue;
			}

			if (bs2pc_polyTrisUsed[j]) {
				goto done;
			}

			newvert = check[(k + 2) % 3];
			if (bs2pc_polyCurrentStripLength & 1) {
				m2 = newvert;
			} else {
				m1 = newvert;
			}

			bs2pc_polyCurrentStripIndices[bs2pc_polyCurrentStripLength + 2] = newvert;
			bs2pc_polyCurrentStripTris[bs2pc_polyCurrentStripLength] = j;
			++bs2pc_polyCurrentStripLength;

			bs2pc_polyTrisUsed[j] = 2;
			goto nexttri;
		}
	}

done:
	for (j = starttri + 1; j < bs2pc_polyTriCount; ++j) {
		if (bs2pc_polyTrisUsed[j] == 2) {
			bs2pc_polyTrisUsed[j] = 0;
		}
	}
}

static void BS2PC_BuildStrips() {
	unsigned int i, j;
	unsigned int startv;
	unsigned int bestlen;
	unsigned short bestindices[BS2PC_MAX_POLY_STRIP_INDICES];
	unsigned short besttris[BS2PC_MAX_POLY_STRIP_INDICES];

	memset(bs2pc_polyTrisUsed, 0, bs2pc_polyTriCount);
	for (i = 0; i < bs2pc_polyTriCount; ++i) {
		if (bs2pc_polyTrisUsed[i]) {
			continue;
		}

		bestlen = 0;
		for (startv = 0; startv < 3; ++startv) {
			BS2PC_BuildStrip(i, startv);
			if (bs2pc_polyCurrentStripLength > bestlen) {
				bestlen = bs2pc_polyCurrentStripLength;
				memcpy(bestindices, bs2pc_polyCurrentStripIndices, (bestlen + 2) * sizeof(unsigned short));
				memcpy(besttris, bs2pc_polyCurrentStripTris, bestlen * sizeof(unsigned short));
			}
		}

		for (j = 0; j < bestlen; ++j) {
			bs2pc_polyTrisUsed[besttris[j]] = 1;
		}

		printf("%sbestlen = %u\n", bestlen != 0 ? "!!! " : "", bestlen);

		bs2pc_polyStrips[bs2pc_polyStripCount][0] = bs2pc_polyStripIndexCount;
		bs2pc_polyStrips[bs2pc_polyStripCount][1] = bestlen + 2;
		++bs2pc_polyStripCount;
		for (j = 0; j < bestlen + 2; ++j) {
			bs2pc_polyStripIndices[bs2pc_polyStripIndexCount++] = bestindices[j];
		}
	}
}

unsigned char *BS2PC_SubdivideGbxSurface(unsigned int faceIndex, unsigned int *outSize) {
	const dface_gbx_t *face = ((const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES)) + faceIndex;
	const int *mapSurfedges = (const int *) BS2PC_GbxLump(LUMP_GBX_SURFEDGES);
	const dedge_t *mapEdges = (const dedge_t *) BS2PC_GbxLump(LUMP_GBX_EDGES);
	const dvertex_gbx_t *mapVertexes = (const dvertex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_VERTEXES), *mapVertex;
	const dmiptex_gbx_t *texture = (const dmiptex_gbx_t *) (bs2pc_gbxMap + face->miptex);

	float verts[64 * 3];
	unsigned int numverts = 0;
	unsigned int i;
	int lindex;
	const float *vecs = &face->vecs[0][0];

	float scaleS = 1.0f / (float) texture->width, scaleT = 1.0f / (float) texture->height;
	float offsetS, offsetT;
	float textureMinS, textureMinT;

	unsigned int subdivSize;
	unsigned char *subdiv;
	unsigned int subdivPosition;
	unsigned int subdivVertIndex;
	unsigned int subdivStripIndex, subdivStripVertexCount;

	for (i = 0; i < face->numedges; ++i) {
		lindex = mapSurfedges[face->firstedge + i];
		if (lindex > 0) {
			mapVertex = mapVertexes + mapEdges[lindex].v[0];
		} else {
			mapVertex = mapVertexes + mapEdges[-lindex].v[1];
		}
		verts[numverts * 3] = mapVertex->point[0];
		verts[numverts * 3 + 1] = mapVertex->point[1];
		verts[numverts * 3 + 2] = mapVertex->point[2];
		++numverts;
	}

	offsetS = (float) (int) ((BS2PC_DotProduct(verts, vecs) + vecs[3]) * scaleS);
	offsetT = (float) (int) ((BS2PC_DotProduct(verts, vecs + 4) + vecs[7]) * scaleT);
	textureMinS = (float) face->texturemins[0];
	textureMinT = (float) face->texturemins[1];

	bs2pc_subdivideSize = ((face->flags & SURF_DRAWTURB) ? 64.0f : 32.0f);
	bs2pc_polyVertCount = 0;
	bs2pc_polyTriCount = 0;
	bs2pc_polyStripIndexCount = 0;
	bs2pc_polyStripCount = 0;
	BS2PC_SubdividePolygon(numverts, verts);
	BS2PC_BuildStrips();

	subdivSize = 2 * sizeof(unsigned int) /* face index, vertex count */ + bs2pc_polyVertCount * sizeof(bs2pc_polyvert_t) + sizeof(unsigned int) /* mesh count */;
	for (subdivStripIndex = 0; subdivStripIndex < bs2pc_polyStripCount; ++subdivStripIndex) {
		subdivSize += (1 + bs2pc_polyStrips[subdivStripIndex][1]) * sizeof(unsigned short);
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
		subdivVert->stoffset[0] = (unsigned char) ((s - textureMinS) * (1.0f / 16.0f) + 0.5f);
		subdivVert->stoffset[1] = (unsigned char) ((t - textureMinT) * (1.0f / 16.0f) + 0.5f);
		subdivVert->pad[0] = subdivVert->pad[1] = 0;
		subdivPosition += sizeof(bs2pc_polyvert_t);
	}

	*((unsigned int *) (subdiv + subdivPosition)) = bs2pc_polyStripCount;
	subdivPosition += sizeof(unsigned int);
	
	for (subdivStripIndex = 0; subdivStripIndex < bs2pc_polyStripCount; ++subdivStripIndex) {
		subdivStripVertexCount = bs2pc_polyStrips[subdivStripIndex][1];
		*((unsigned short *) (subdiv + subdivPosition)) = subdivStripVertexCount;
		subdivPosition += sizeof(unsigned short);
		memcpy(subdiv + subdivPosition, &bs2pc_polyStripIndices[bs2pc_polyStrips[subdivStripIndex][0]],
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