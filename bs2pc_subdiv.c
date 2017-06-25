#include "bs2pc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static bs2pc_poly_t *bs2pc_warpPoly;

static float bs2pc_subdivideSize;

#define BS2PC_MAX_POLY_VERTS 1024
static float bs2pc_polyVertData[BS2PC_MAX_POLY_VERTS][3];
static unsigned int bs2pc_polyVertCount;
#define BS2PC_POLY_VERT_EPSILON 0.001f

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
	for (vertIndex = 0, otherVert = &bs2pc_polyVertData[0][0]; vertIndex < bs2pc_polyVertCount; ++vertIndex, otherVert += 3) {
		if (fabsf(vert[0] - otherVert[0]) <= BS2PC_POLY_VERT_EPSILON &&
				fabsf(vert[1] - otherVert[1]) <= BS2PC_POLY_VERT_EPSILON &&
				fabsf(vert[2] - otherVert[2]) <= BS2PC_POLY_VERT_EPSILON) {
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
	bs2pc_poly_t *poly;

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
	}

	poly = (bs2pc_poly_t *) BS2PC_Alloc(sizeof(bs2pc_poly_t) + (numverts - 4) * sizeof(unsigned int), false);
	poly->next = bs2pc_warpPoly;
	bs2pc_warpPoly = poly;
	poly->numindices = numverts;
	for (i = 0; i < numverts; ++i, verts += 3) {
		poly->indices[i] = BS2PC_AddPolyVertex(verts);
	}
}

bs2pc_subdiv_t *BS2PC_SubdivideGbxSurface(const dface_gbx_t *face) {
	const int *mapSurfedges = (const int *) BS2PC_GbxLump(LUMP_GBX_SURFEDGES);
	const dedge_t *mapEdges = (const dedge_t *) BS2PC_GbxLump(LUMP_GBX_EDGES);
	const dvertex_gbx_t *mapVertexes = (const dvertex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_VERTEXES), *mapVertex;
	const dmiptex_gbx_t *texture = (const dmiptex_gbx_t *) (bs2pc_gbxMap + face->miptex);
	bs2pc_subdiv_t *subdiv;
	float verts[64 * 3];
	unsigned int numverts = 0;
	unsigned int i;
	int lindex;
	const float *vecs = &face->vecs[0][0];
	float scaleS = 1.0f / (float) texture->width, scaleT = 1.0f / (float) texture->height;
	float offsetS, offsetT;
	float textureMinS, textureMinT;
	unsigned int subdivVertIndex;
	bs2pc_polyvert_t *subdivVert;

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

	bs2pc_warpPoly = NULL;
	bs2pc_subdivideSize = ((face->flags & SURF_DRAWTURB) ? 64.0f : 32.0f);
	bs2pc_polyVertCount = 0;
	BS2PC_SubdividePolygon(numverts, verts);

	subdiv = (bs2pc_subdiv_t *) BS2PC_Alloc(sizeof(bs2pc_subdiv_t), false);
	subdiv->numverts = bs2pc_polyVertCount;
	subdiv->verts = (bs2pc_polyvert_t *) BS2PC_Alloc(bs2pc_polyVertCount * sizeof(bs2pc_polyvert_t), false);
	for (subdivVertIndex = 0, subdivVert = subdiv->verts; subdivVertIndex < bs2pc_polyVertCount; ++subdivVertIndex, ++subdivVert) {
		const float *data = bs2pc_polyVertData[subdivVertIndex];
		float s, t;
		subdivVert->xyz[0] = data[0];
		subdivVert->xyz[1] = data[1];
		subdivVert->xyz[2] = data[2];
		s = BS2PC_DotProduct(data, vecs) + vecs[3];
		t = BS2PC_DotProduct(data, vecs + 4) + vecs[7];
		subdivVert->st[0] = s * scaleS - offsetS;
		subdivVert->st[1] = t * scaleT - offsetT;
		subdivVert->stoffset[0] = (unsigned char) ((s - textureMinS) * (1.0f / 16.0f) + 0.5f);
		subdivVert->stoffset[1] = (unsigned char) ((t - textureMinT) * (1.0f / 16.0f) + 0.5f);
		subdivVert->pad[0] = subdivVert->pad[1] = 0;
	}
	subdiv->poly = bs2pc_warpPoly;
	return subdiv;
}