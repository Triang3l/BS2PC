#include "bs2pc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const float *bs2pc_warptexvecs;
static float bs2pc_warptexscale[2], bs2pc_warptexoffset[2];

static bs2pc_poly_t *bs2pc_warppoly;

#define BS2PC_SUBDIVIDE_SIZE 64.0f

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
	bs2pc_polyvert_t *polyvert;

	if (numverts > 60) {
		fputs("Too many subdivision vertices\n.", stderr);
		exit(EXIT_FAILURE);
	}

	BS2PC_BoundPoly(numverts, verts, mins, maxs);

	for (i = 0; i < 3; ++i) {
		m = (mins[i] + maxs[i]) * 0.5f;
		m = BS2PC_SUBDIVIDE_SIZE * floorf(m * (1.0f / BS2PC_SUBDIVIDE_SIZE) + 0.5f);
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

	poly = (bs2pc_poly_t *) BS2PC_Alloc(sizeof(bs2pc_poly_t) + (numverts - 4) * sizeof(bs2pc_polyvert_t), false);
	poly->next = bs2pc_warppoly;
	bs2pc_warppoly = poly;
	poly->numverts = numverts;
	polyvert = poly->verts;
	for (i = 0; i < numverts; ++i, verts += 3, ++polyvert) {
		polyvert->xyz[0] = verts[0];
		polyvert->xyz[1] = verts[1];
		polyvert->xyz[2] = verts[2];
		polyvert->st[0] = (BS2PC_DotProduct(verts, bs2pc_warptexvecs) + bs2pc_warptexvecs[3]) * bs2pc_warptexscale[0] - bs2pc_warptexoffset[0];
		polyvert->st[1] = (BS2PC_DotProduct(verts, bs2pc_warptexvecs + 4) + bs2pc_warptexvecs[7]) * bs2pc_warptexscale[1] - bs2pc_warptexoffset[1];
		// TODO: Numbers in the subdivision.
		polyvert->f = 0;
		polyvert->b = 0;
		polyvert->pad[0] = polyvert->pad[1] = 0;
	}
}

bs2pc_poly_t *BS2PC_SubdivideGbxSurface(const dface_gbx_t *face) {
	const int *mapSurfedges = (const int *) BS2PC_GbxLump(LUMP_GBX_SURFEDGES);
	const dedge_t *mapEdges = (const dedge_t *) BS2PC_GbxLump(LUMP_GBX_EDGES);
	const dvertex_gbx_t *mapVertexes = (const dvertex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_VERTEXES), *mapVertex;
	const dmiptex_gbx_t *texture = (const dmiptex_gbx_t *) (bs2pc_gbxMap + face->miptex);
	float verts[64 * 3];
	unsigned int numverts = 0;
	unsigned int i;
	int lindex;

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

	bs2pc_warptexvecs = &face->vecs[0][0];
	bs2pc_warptexscale[0] = 1.0f / (float) texture->width;
	bs2pc_warptexscale[1] = 1.0f / (float) texture->height;
	bs2pc_warptexoffset[0] = (float) (int) ((BS2PC_DotProduct(verts, bs2pc_warptexvecs) + bs2pc_warptexvecs[3]) * bs2pc_warptexscale[0]);
	bs2pc_warptexoffset[1] = (float) (int) ((BS2PC_DotProduct(verts, bs2pc_warptexvecs + 4) + bs2pc_warptexvecs[7]) * bs2pc_warptexscale[1]);
	bs2pc_warppoly = NULL;
	BS2PC_SubdividePolygon(numverts, verts);
	return bs2pc_warppoly;
}