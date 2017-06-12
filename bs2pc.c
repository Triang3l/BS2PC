/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2017 Triang3l

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include <malloc.h>
#define alloca _alloca
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <alloca.h>
#endif
#include "zlib.h"
#ifdef _WIN32
#include <Windows.h>
#endif

/*************
 * Structures
 *************/

typedef unsigned int bspoffset_t;

#define BSPVERSION_ID 30
#define BSPVERSION_GBX 40

enum {
	LUMP_ID_ENTITIES,
	LUMP_ID_PLANES,
	LUMP_ID_TEXTURES,
	LUMP_ID_VERTEXES,
	LUMP_ID_VISIBILITY,
	LUMP_ID_NODES,
	LUMP_ID_TEXINFO,
	LUMP_ID_FACES,
	LUMP_ID_LIGHTING,
	LUMP_ID_CLIPNODES,
	LUMP_ID_LEAFS,
	LUMP_ID_MARKSURFACES,
	LUMP_ID_EDGES,
	LUMP_ID_SURFEDGES,
	LUMP_ID_MODELS,

	LUMP_ID_COUNT
};

enum {
	LUMP_GBX_PLANES,
	LUMP_GBX_NODES,
	LUMP_GBX_LEAFS,
	LUMP_GBX_EDGES,
	LUMP_GBX_SURFEDGES,
	LUMP_GBX_VERTEXES,
	LUMP_GBX_UNKNOWN1,
	LUMP_GBX_CLIPNODES,
	LUMP_GBX_MODELS,
	LUMP_GBX_FACES,
	LUMP_GBX_MARKSURFACES,
	LUMP_GBX_VISIBILITY,
	LUMP_GBX_LIGHTING,
	LUMP_GBX_TEXTURES,
	LUMP_GBX_ENTITIES,
	LUMP_GBX_UNKNOWN2,

	LUMP_GBX_COUNT
};

#pragma pack(push, 4)

typedef struct {
	bspoffset_t fileofs;
	unsigned int filelen;
} dlump_id_t;

typedef struct {
	unsigned int version;
	dlump_id_t lumps[LUMP_ID_COUNT];
} dheader_id_t;

typedef struct {
	unsigned int version;
	bspoffset_t lumpofs[LUMP_GBX_COUNT];
	unsigned int lumplen[LUMP_GBX_COUNT];
} dheader_gbx_t;

typedef struct {
	float normal[3];
	float dist;
	unsigned int type;
} dplane_id_t;

typedef struct {
	float normal[3];
	float dist;
	unsigned char type;
	unsigned char signbits;
	unsigned char pad[2];
} dplane_gbx_t;

#define MIPLEVELS_ID 4

typedef struct {
	char name[16];
	unsigned int width, height;
	unsigned int offsets[MIPLEVELS_ID];
} dmiptex_id_t;

typedef struct {
	bspoffset_t offset;
	bspoffset_t palette;
	unsigned short width, height;
	unsigned short scaled_width, scaled_height;
	char name[16];
	unsigned char unknown1[3];
	unsigned char miplevels;
	unsigned char unknown2[8]; // gl_texturenum, texturechain (runtime)?
	unsigned int anim_total;
	unsigned int anim_min, anim_max;
	bspoffset_t anim_next, alternate_anims;
} dmiptex_gbx_t;

typedef struct {
	float point[3];
} dvertex_id_t;

typedef struct {
	float point[4];
} dvertex_gbx_t;

typedef struct {
	unsigned int planenum;
	short children[2];
	short mins[3], maxs[3];
	unsigned short firstface, numfaces;
} dnode_id_t;

typedef struct {
	int contents; // 0
	bspoffset_t parent;
	unsigned int visframe;
	bspoffset_t plane;
	float mins[4], maxs[4];
	bspoffset_t children[2];
	unsigned short firstface, numfaces;
	unsigned char unknown1[4];
} dnode_gbx_t;

#define TEX_SPECIAL 1

typedef struct {
	float vecs[2][4];
	unsigned int miptex;
	unsigned int flags;
} dtexinfo_id_t;

#define MAX_LIGHTMAPS 4

typedef struct {
	unsigned short planenum;
	unsigned short side;
	unsigned int firstedge;
	unsigned short numedges;
	unsigned short texinfo;
	unsigned char styles[MAX_LIGHTMAPS];
	unsigned int lightofs;
} dface_id_t;

typedef struct {
	float vecs[2][4];
	unsigned short side;
	unsigned char unknown1[2];
	bspoffset_t miptex;
	bspoffset_t lightofs;
	bspoffset_t plane;
	unsigned char unknown2[4];
	unsigned int firstedge;
	unsigned int numedges;
	unsigned char unknown3[40];
	unsigned char styles[MAX_LIGHTMAPS];
	unsigned char unknown4[40];
} dface_gbx_t;

#define NUM_AMBIENTS 4

typedef struct {
	int contents;
	unsigned int visofs;
	short mins[3], maxs[3];
	unsigned short firstmarksurface, nummarksurfaces;
	unsigned char ambient_level[NUM_AMBIENTS];
} dleaf_id_t;

typedef struct {
	int contents;
	bspoffset_t parent;
	unsigned int visframe;
	unsigned char unknown1[4];
	float mins[4], maxs[4];
	bspoffset_t visofs;
	unsigned int firstmarksurface, nummarksurfaces;
	unsigned char ambient_level[NUM_AMBIENTS];
} dleaf_gbx_t;

typedef unsigned short dmarksurface_id_t;
typedef unsigned int dmarksurface_gbx_t;

#define	MAX_MAP_HULLS 4

typedef struct {
	float mins[3], maxs[3];
	float origin[3];
	unsigned int headnode[MAX_MAP_HULLS];
	unsigned int visleafs;
	unsigned int firstface, numfaces;
} dmodel_id_t;

typedef struct {
	float mins[4], maxs[4];
	float origin[4];
	unsigned int headnode[MAX_MAP_HULLS];
	unsigned int visleafs;
	unsigned int firstface, numfaces;
	unsigned char unknown1[4];
} dmodel_gbx_t;

#pragma pack(pop)

/*********
 * Conversion
 *********/

static unsigned char *bspfile_gbx;
static const dheader_gbx_t *header_gbx;

inline unsigned int BS2PC_GBXOffsetToIndex(bspoffset_t offset, unsigned int lump, unsigned int lumpSize) {
	return (offset - header_gbx->lumpofs[lump]) / lumpSize;
}

static unsigned char *bspfile_id;
static unsigned int bspfile_size_id;
static dheader_id_t *header_id;

static unsigned int texture_count;
static unsigned int texture_lump_size;
static bool *textures_special; // Whether textures are special - texinfo needs this.
static unsigned int texture_nodraw = UINT_MAX;

// Nodraw skipping.
static unsigned int *nodraw_face_map;
static unsigned int nodraw_face_count;
static dmarksurface_gbx_t *nodraw_marksurface_map;
static dmarksurface_id_t *nodraw_marksurface_lump_id;
static unsigned int nodraw_marksurface_count;

static void BS2PC_PreProcessTextureLump() {
	const dmiptex_gbx_t *textureGbx = (const dmiptex_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_TEXTURES]);
	unsigned int textureIndex;

	// The first texture's bytes follow the last texture info.
	texture_count = (textureGbx->offset - header_gbx->lumpofs[LUMP_GBX_TEXTURES]) / sizeof(dmiptex_gbx_t);
	textures_special = calloc(texture_count, sizeof(bool));
	if (textures_special == NULL) {
		fputs("Couldn't allocate special texture flags.\n", stderr);
		exit(EXIT_FAILURE);
	}

	texture_lump_size = sizeof(unsigned int) /* texture count */ +
			texture_count * (sizeof(bspoffset_t) /* offset */ + sizeof(dmiptex_id_t) + (2 + 768 + 2));

	texture_nodraw = UINT_MAX;
	for (textureIndex = 0; textureIndex < texture_count; ++textureIndex) {
		const char *textureName = textureGbx->name;
		unsigned int width, height;

		if (texture_nodraw == UINT_MAX && strncasecmp(textureName, "nodraw", 6) == 0) {
			texture_nodraw = textureIndex;
		} else if (textureName[0] == '*' ||
				strncasecmp(textureName, "sky", 3) == 0 ||
				strncasecmp(textureName, "clip", 4) == 0 ||
				strncasecmp(textureName, "origin", 6) == 0 ||
				strncasecmp(textureName, "aaatrigger", 10) == 0) {
			textures_special[textureIndex] = true;
		}

		width = textureGbx->width;
		height = textureGbx->height;
		if (width == 0 || height == 0 || (width & 15) != 0 || (height & 15) != 0) {
			fprintf(stderr, "Texture %s has non-16-aligned width or height.\n", textureName);
			exit(EXIT_FAILURE);
		}
		texture_lump_size += width * height +
				(width >> 1) * (height >> 1) +
				(width >> 2) * (height >> 2) +
				(width >> 3) * (height >> 3);

		++textureGbx;
	}
}

static void BS2PC_InitializeNodraw() {
	unsigned int faceCount = header_gbx->lumplen[LUMP_GBX_FACES] / sizeof(dface_gbx_t);
	unsigned int marksurfaceCount = header_gbx->lumplen[LUMP_GBX_MARKSURFACES] / sizeof(dmarksurface_gbx_t);
	unsigned int faceIndex, marksurfaceIndex;
	const dface_gbx_t *faces, *face;
	const dmarksurface_gbx_t *marksurface;

	if (texture_nodraw == UINT_MAX) {
		return;
	}

	nodraw_face_map = (unsigned int *) malloc(faceCount * sizeof(unsigned int));
	nodraw_face_count = 0;
	nodraw_marksurface_map = (dmarksurface_gbx_t *) malloc(marksurfaceCount * sizeof(dmarksurface_gbx_t));
	nodraw_marksurface_count = 0;
	nodraw_marksurface_lump_id = (dmarksurface_id_t *) malloc(marksurfaceCount * sizeof(dmarksurface_id_t));

	if (nodraw_face_map == NULL || nodraw_marksurface_map == NULL || nodraw_marksurface_lump_id == NULL) {
		fputs("Couldn't allocate non-nodraw face and marksurface maps.\n", stderr);
		exit(EXIT_FAILURE);
	}

	faces = (const dface_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_FACES]);

	for (faceIndex = 0, face = faces; faceIndex < faceCount; ++faceIndex, ++face) {
		nodraw_face_map[faceIndex] = nodraw_face_count;
		if (BS2PC_GBXOffsetToIndex(face->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) != texture_nodraw) {
			++nodraw_face_count;
		}
	}

	marksurface = (const dmarksurface_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_MARKSURFACES]);
	for (marksurfaceIndex = 0; marksurfaceIndex < marksurfaceCount; ++marksurfaceIndex, ++marksurface) {
		nodraw_marksurface_map[marksurfaceIndex] = nodraw_marksurface_count;
		if (BS2PC_GBXOffsetToIndex(faces[*marksurface].miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) != texture_nodraw) {
			nodraw_marksurface_lump_id[nodraw_marksurface_count] = (unsigned short) nodraw_face_map[*marksurface];
			++nodraw_marksurface_count;
		}
	}
}

static void BS2PC_RemapNodrawFaceRange(unsigned int inFirst, unsigned int inCount, unsigned int *outFirst, unsigned int *outCount) {
	unsigned int index, count;
	const dface_gbx_t *gbxFace;

	if (texture_nodraw == UINT_MAX) {
		*outFirst = inFirst;
		*outCount = inCount;
		return;
	}

	*outFirst = nodraw_face_map[inFirst];
	count = 0;
	gbxFace = (const dface_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_FACES]) + inFirst;
	for (index = 0; index < inCount; ++index, ++gbxFace) {
		if (BS2PC_GBXOffsetToIndex(gbxFace->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) != texture_nodraw) {
			++count;
		}
	}
	*outCount = count;
}

static void BS2PC_RemapMarksurfaceFaceRange(unsigned int inFirst, unsigned int inCount, unsigned int *outFirst, unsigned int *outCount) {
	unsigned int index, count;
	const dmarksurface_gbx_t *gbxMarksurface;
	const dface_gbx_t *gbxFace;

	if (texture_nodraw == UINT_MAX) {
		*outFirst = inFirst;
		*outCount = inCount;
		return;
	}

	*outFirst = nodraw_marksurface_map[inFirst];
	count = 0;
	gbxMarksurface = (const dmarksurface_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_MARKSURFACES]) + inFirst;
	for (index = 0; index < inCount; ++index, ++gbxMarksurface) {
		gbxFace = (const dface_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_FACES]) + *gbxMarksurface;
		if (BS2PC_GBXOffsetToIndex(gbxFace->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) != texture_nodraw) {
			++count;
		}
	}
	*outCount = count;
}

static void BS2PC_AllocateIDBSP() {
	const dheader_gbx_t *header_gbx = (const dheader_gbx_t *) bspfile_gbx;
	dheader_id_t headerId;
	unsigned int bspSize;
	unsigned int faceCount;

	headerId.version = BSPVERSION_ID;
	bspSize = (sizeof(dheader_id_t) + 3) & ~3;

	if (texture_nodraw != UINT_MAX) {
		faceCount = nodraw_face_count;
	} else {
		faceCount = header_gbx->lumplen[LUMP_GBX_FACES] / sizeof(dface_gbx_t);
	}

	headerId.lumps[LUMP_ID_PLANES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_PLANES].filelen = header_gbx->lumplen[LUMP_GBX_PLANES] / sizeof(dplane_gbx_t) * sizeof(dplane_id_t);
	bspSize += (headerId.lumps[LUMP_ID_PLANES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_LEAFS].fileofs = bspSize;
	headerId.lumps[LUMP_ID_LEAFS].filelen = header_gbx->lumplen[LUMP_GBX_LEAFS] / sizeof(dleaf_gbx_t) * sizeof(dleaf_id_t);
	bspSize += (headerId.lumps[LUMP_ID_LEAFS].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_VERTEXES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_VERTEXES].filelen = header_gbx->lumplen[LUMP_GBX_VERTEXES] / sizeof(dvertex_gbx_t) * sizeof(dvertex_id_t);
	bspSize += (headerId.lumps[LUMP_ID_VERTEXES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_NODES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_NODES].filelen = header_gbx->lumplen[LUMP_GBX_NODES] / sizeof(dnode_gbx_t) * sizeof(dnode_id_t);
	bspSize += (headerId.lumps[LUMP_ID_NODES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_TEXINFO].fileofs = bspSize;
	headerId.lumps[LUMP_ID_TEXINFO].filelen = faceCount * sizeof(dtexinfo_id_t);
	bspSize += (headerId.lumps[LUMP_ID_TEXINFO].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_FACES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_FACES].filelen = faceCount * sizeof(dface_id_t);
	bspSize += (headerId.lumps[LUMP_ID_FACES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_CLIPNODES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_CLIPNODES].filelen = header_gbx->lumplen[LUMP_GBX_CLIPNODES];
	bspSize += (headerId.lumps[LUMP_ID_CLIPNODES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_MARKSURFACES].fileofs = bspSize;
	if (texture_nodraw != UINT_MAX) {
		headerId.lumps[LUMP_ID_MARKSURFACES].filelen = nodraw_marksurface_count * sizeof(dmarksurface_id_t);
	} else {
		headerId.lumps[LUMP_ID_MARKSURFACES].filelen = header_gbx->lumplen[LUMP_GBX_MARKSURFACES] / sizeof(dmarksurface_gbx_t) * sizeof(dmarksurface_id_t);
	}
	bspSize += (headerId.lumps[LUMP_ID_MARKSURFACES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_SURFEDGES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_SURFEDGES].filelen = header_gbx->lumplen[LUMP_GBX_SURFEDGES];
	bspSize += (headerId.lumps[LUMP_ID_SURFEDGES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_EDGES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_EDGES].filelen = header_gbx->lumplen[LUMP_GBX_EDGES];
	bspSize += (headerId.lumps[LUMP_ID_EDGES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_MODELS].fileofs = bspSize;
	headerId.lumps[LUMP_ID_MODELS].filelen = header_gbx->lumplen[LUMP_GBX_MODELS] / sizeof(dmodel_gbx_t) * sizeof(dmodel_id_t);
	bspSize += (headerId.lumps[LUMP_ID_MODELS].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_LIGHTING].fileofs = bspSize;
	headerId.lumps[LUMP_ID_LIGHTING].filelen = header_gbx->lumplen[LUMP_GBX_LIGHTING];
	bspSize += (headerId.lumps[LUMP_ID_LIGHTING].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_VISIBILITY].fileofs = bspSize;
	headerId.lumps[LUMP_ID_VISIBILITY].filelen = header_gbx->lumplen[LUMP_GBX_VISIBILITY];
	bspSize += (headerId.lumps[LUMP_ID_VISIBILITY].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_ENTITIES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_ENTITIES].filelen = header_gbx->lumplen[LUMP_GBX_ENTITIES];
	bspSize += (headerId.lumps[LUMP_ID_ENTITIES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_TEXTURES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_TEXTURES].filelen = texture_lump_size;
	bspSize += headerId.lumps[LUMP_ID_TEXTURES].filelen;

	bspfile_size_id = bspSize;
	bspfile_id = (unsigned char *) calloc(1, bspfile_size_id);
	if (bspfile_id == NULL) {
		fputs("Couldn't allocate the .bsp file contents.\n", stderr);
		exit(EXIT_FAILURE);
	}
	header_id = (dheader_id_t *) bspfile_id;
	memcpy(header_id, &headerId, sizeof(dheader_id_t));
}

static void BS2PC_CopyLump(unsigned int gbxLump, unsigned int idLump) {
	memcpy(bspfile_id + header_id->lumps[idLump].fileofs,
			bspfile_gbx + header_gbx->lumpofs[gbxLump],
			header_gbx->lumplen[gbxLump]);
}

static void BS2PC_ConvertPlanes() {
	const dplane_gbx_t *gbx = (const dplane_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_PLANES]);
	dplane_id_t *id = (dplane_id_t *) (bspfile_id + header_id->lumps[LUMP_ID_PLANES].fileofs);
	unsigned int index, count = header_gbx->lumplen[LUMP_GBX_PLANES] / sizeof(dplane_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		id->normal[0] = gbx->normal[0];
		id->normal[1] = gbx->normal[1];
		id->normal[2] = gbx->normal[2];
		id->dist = gbx->dist;
		id->type = gbx->type;
	}
}

static void BS2PC_ConvertLeafs() {
	const dleaf_gbx_t *gbx = (const dleaf_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_LEAFS]);
	dleaf_id_t *id = (dleaf_id_t *) (bspfile_id + header_id->lumps[LUMP_ID_LEAFS].fileofs);
	unsigned int index, count = header_gbx->lumplen[LUMP_GBX_LEAFS] / sizeof(dleaf_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		unsigned int firstMarksurface, marksurfaceCount;
		id->contents = gbx->contents;
		id->mins[0] = (short) gbx->mins[0];
		id->mins[1] = (short) gbx->mins[1];
		id->mins[2] = (short) gbx->mins[2];
		id->maxs[0] = (short) gbx->maxs[0];
		id->maxs[1] = (short) gbx->maxs[1];
		id->maxs[2] = (short) gbx->maxs[2];
		id->visofs = gbx->visofs - (gbx->visofs != UINT_MAX ? header_gbx->lumpofs[LUMP_GBX_VISIBILITY] : 0);
		BS2PC_RemapMarksurfaceFaceRange(gbx->firstmarksurface, gbx->nummarksurfaces, &firstMarksurface, &marksurfaceCount);
		id->firstmarksurface = (unsigned short) firstMarksurface;
		id->nummarksurfaces = (unsigned short) marksurfaceCount;
		memcpy(id->ambient_level, gbx->ambient_level, sizeof(id->ambient_level));
	}
}

static void BS2PC_ConvertVertexes() {
	const dvertex_gbx_t *gbx = (const dvertex_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_VERTEXES]);
	dvertex_id_t *id = (dvertex_id_t *) (bspfile_id + header_id->lumps[LUMP_ID_VERTEXES].fileofs);
	unsigned int index, count = header_gbx->lumplen[LUMP_GBX_VERTEXES] / sizeof(dvertex_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		id->point[0] = gbx->point[0];
		id->point[1] = gbx->point[1];
		id->point[2] = gbx->point[2];
	}
}

static void BS2PC_ConvertNodes() {
	const dnode_gbx_t *gbx = (const dnode_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_NODES]);
	dnode_id_t *id = (dnode_id_t *) (bspfile_id + header_id->lumps[LUMP_ID_NODES].fileofs);
	unsigned int index, count = header_gbx->lumplen[LUMP_GBX_NODES] / sizeof(dnode_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		const dnode_gbx_t *child;
		unsigned int firstFace, faceCount;

		id->planenum = BS2PC_GBXOffsetToIndex(gbx->plane, LUMP_GBX_PLANES, sizeof(dplane_gbx_t));

		child = (const dnode_gbx_t *) (bspfile_gbx + gbx->children[0]);
		if (child->contents == 0) {
			id->children[0] = (short) BS2PC_GBXOffsetToIndex(gbx->children[0], LUMP_GBX_NODES, sizeof(dnode_gbx_t)); 
		} else {
			id->children[0] = -1 - (short) BS2PC_GBXOffsetToIndex(gbx->children[0], LUMP_GBX_LEAFS, sizeof(dleaf_gbx_t));
		}

		child = (const dnode_gbx_t *) (bspfile_gbx + gbx->children[1]);
		if (child->contents == 0) {
			id->children[1] = (short) BS2PC_GBXOffsetToIndex(gbx->children[1], LUMP_GBX_NODES, sizeof(dnode_gbx_t)); 
		} else {
			id->children[1] = -1 - (short) BS2PC_GBXOffsetToIndex(gbx->children[1], LUMP_GBX_LEAFS, sizeof(dleaf_gbx_t));
		}

		id->mins[0] = (short) gbx->mins[0];
		id->mins[1] = (short) gbx->mins[1];
		id->mins[2] = (short) gbx->mins[2];
		id->maxs[0] = (short) gbx->maxs[0];
		id->maxs[1] = (short) gbx->maxs[1];
		id->maxs[2] = (short) gbx->maxs[2];
		BS2PC_RemapNodrawFaceRange(gbx->firstface, gbx->numfaces, &firstFace, &faceCount);
		id->firstface = (unsigned short) firstFace;
		id->numfaces = (unsigned short) faceCount;
	}
}

static void BS2PC_ConvertTexinfo() {
	const dface_gbx_t *gbx = (const dface_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_FACES]);
	dtexinfo_id_t *id = (dtexinfo_id_t *) (bspfile_id + header_id->lumps[LUMP_ID_TEXINFO].fileofs);
	unsigned int index, count = header_gbx->lumplen[LUMP_GBX_FACES] / sizeof(dface_gbx_t);
	for (index = 0; index < count; ++index, ++gbx) {
		unsigned int miptex = BS2PC_GBXOffsetToIndex(gbx->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t));
		if (texture_nodraw != UINT_MAX && miptex == texture_nodraw) {
			continue;
		}
		memcpy(id->vecs, gbx->vecs, sizeof(id->vecs));
		id->miptex = miptex;
		id->flags = (textures_special[miptex] ? TEX_SPECIAL : 0);
		++id;
	}
}

static void BS2PC_ConvertFaces() {
	const dface_gbx_t *gbx = (const dface_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_FACES]);
	dface_id_t *id = (dface_id_t *) (bspfile_id + header_id->lumps[LUMP_ID_FACES].fileofs);
	unsigned int index, count = header_gbx->lumplen[LUMP_GBX_FACES] / sizeof(dface_gbx_t);
	unsigned int idIndex = 0;
	for (index = 0; index < count; ++index, ++gbx) {
		if (texture_nodraw != UINT_MAX && BS2PC_GBXOffsetToIndex(gbx->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) == texture_nodraw) {
			continue;
		}
		id->planenum = (unsigned short) BS2PC_GBXOffsetToIndex(gbx->plane, LUMP_GBX_PLANES, sizeof(dplane_gbx_t));
		id->side = gbx->side;
		id->firstedge = gbx->firstedge;
		id->numedges = (unsigned short) gbx->numedges;
		id->texinfo = (unsigned short) idIndex;
		memcpy(id->styles, gbx->styles, sizeof(id->styles));
		id->lightofs = gbx->lightofs - (gbx->lightofs != UINT_MAX ? header_gbx->lumpofs[LUMP_GBX_LIGHTING] : 0);
		++idIndex;
		++id;
	}
}

static void BS2PC_ConvertMarksurfaces() {
	const dmarksurface_gbx_t *gbx;
	dmarksurface_id_t *id = (dmarksurface_id_t *) (bspfile_id + header_id->lumps[LUMP_ID_MARKSURFACES].fileofs);
	unsigned int index, count;

	if (texture_nodraw != UINT_MAX) {
		memcpy(id, nodraw_marksurface_lump_id, nodraw_marksurface_count * sizeof(dmarksurface_id_t));
		return;
	}

	gbx = (const dmarksurface_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_MARKSURFACES]);
	count = header_gbx->lumplen[LUMP_GBX_MARKSURFACES] / sizeof(dmarksurface_gbx_t);
	for (index = 0; index < count; ++index, ++id) {
		*id = (dmarksurface_id_t) gbx[index];
	}
}

static void BS2PC_ConvertModels() {
	const dmodel_gbx_t *gbx = (const dmodel_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_MODELS]);
	dmodel_id_t *id = (dmodel_id_t *) (bspfile_id + header_id->lumps[LUMP_ID_MODELS].fileofs);
	unsigned int index, count = header_gbx->lumplen[LUMP_GBX_MODELS] / sizeof(dmodel_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		memcpy(id->mins, gbx->mins, 3 * sizeof(float));
		memcpy(id->maxs, gbx->maxs, 3 * sizeof(float));
		memcpy(id->origin, gbx->origin, 3 * sizeof(float));
		memcpy(id->headnode, gbx->headnode, sizeof(id->headnode));
		id->visleafs = gbx->visleafs;
		BS2PC_RemapNodrawFaceRange(gbx->firstface, gbx->numfaces, &id->firstface, &id->numfaces);
	}
}

static void BS2PC_ConvertEntities() {
	const char *gbx = (const char *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_ENTITIES]);
	char *id = (char *) (bspfile_id + header_id->lumps[LUMP_ID_ENTITIES].fileofs);
	unsigned int index, count = header_gbx->lumplen[LUMP_GBX_ENTITIES];

	char *stringStart = NULL;
	unsigned int stringLength;

	for (index = 0; index < count; ++index, ++gbx, ++id) {
		char character = *gbx;
		*id = character;

		if (character == '"') {
			if (stringStart != NULL) {
				if (stringLength >= 11 &&
						strncasecmp(stringStart, "models/", 7) == 0 &&
						strncasecmp(stringStart + stringLength - 4, ".dol", 4) == 0) {
					stringStart[stringLength - 3] += 'm' - 'd';
					stringStart[stringLength - 2] += 'd' - 'o';
					// stringStart[stringLength - 1] += 'l' - 'l';
				} else if (stringLength >= 12 &&
						strncasecmp(stringStart, "sprites/", 8) == 0 &&
						strncasecmp(stringStart + stringLength - 4, ".spz", 4) == 0) {
					// stringStart[stringLength - 3] += 's' - 's';
					// stringStart[stringLength - 2] += 'p' - 'p';
					stringStart[stringLength - 1] += 'r' - 'z';
				}
				stringStart = NULL;
			} else {
				stringStart = id + 1;
				stringLength = 0;
			}
		} else {
			if (stringStart != NULL) {
				++stringLength;
			}
		}
	}
}

// GL_Resample8BitTexture from Quake.
void BS2PC_ResampleTexture(unsigned char *in, int inwidth, int inheight, unsigned char *out, int outwidth, int outheight)
{
	int i, j;
	unsigned char *inrow;
	unsigned frac, fracstep;

	fracstep = inwidth * 0x10000 / outwidth;
	for (i = 0; i < outheight; ++i, out += outwidth)
	{
		inrow = in + inwidth * (i * inheight / outheight);
		frac = fracstep >> 1;
		for (j = 0; j < outwidth; j += 4)
		{
			out[j] = inrow[frac >> 16];
			frac += fracstep;
			out[j + 1] = inrow[frac >> 16];
			frac += fracstep;
			out[j + 2] = inrow[frac >> 16];
			frac += fracstep;
			out[j + 3] = inrow[frac >> 16];
			frac += fracstep;
		}
	}
}

void BS2PC_ConvertTextures() {
	const dmiptex_gbx_t *texturesGbx = (const dmiptex_gbx_t *) (bspfile_gbx + header_gbx->lumpofs[LUMP_GBX_TEXTURES]);
	unsigned char *lumpId = bspfile_id + header_id->lumps[LUMP_ID_TEXTURES].fileofs;
	unsigned int *miptexOffsets, miptexOffset;
	unsigned int textureIndex;

	// Miptex table
	*((unsigned int *) lumpId) = texture_count;
	miptexOffsets = (unsigned int *) (lumpId + sizeof(unsigned int));
	miptexOffset = (texture_count + 1) * sizeof(unsigned int);
	for (textureIndex = 0; textureIndex < texture_count; ++textureIndex) {
		const dmiptex_gbx_t *textureGbx = &texturesGbx[textureIndex];
		miptexOffsets[textureIndex] = miptexOffset;
		miptexOffset += sizeof(dmiptex_id_t) +
				textureGbx->width * textureGbx->height +
				(textureGbx->width >> 1) * (textureGbx->height >> 1) +
				(textureGbx->width >> 2) * (textureGbx->height >> 2) +
				(textureGbx->width >> 3) * (textureGbx->height >> 3) +
				(2 + 768 + 2);
	}

	// Texture data
	for (textureIndex = 0; textureIndex < texture_count; ++textureIndex) {
		const dmiptex_gbx_t *textureGbx = &texturesGbx[textureIndex];
		unsigned char *textureId = lumpId + miptexOffsets[textureIndex];
		dmiptex_id_t *headerId = (dmiptex_id_t *) textureId;
		unsigned int width, height;
		const unsigned char *paletteGbx;
		unsigned char *paletteId;
		bool liquid;
		unsigned int colorIndex;

		memcpy(headerId->name, textureGbx->name, sizeof(headerId->name));
		width = textureGbx->width;
		height = textureGbx->height;
		headerId->width = width;
		headerId->height = height;

		headerId->offsets[0] = sizeof(dmiptex_id_t);
		headerId->offsets[1] = headerId->offsets[0] + width * height;
		headerId->offsets[2] = headerId->offsets[1] + (width >> 1) * (height >> 1);
		headerId->offsets[3] = headerId->offsets[2] + (width >> 2) * (height >> 2);

		if (textureGbx->scaled_width == width && textureGbx->scaled_height == height) {
			memcpy(textureId + headerId->offsets[0], bspfile_gbx + textureGbx->offset, width * height);
		} else {
			BS2PC_ResampleTexture(bspfile_gbx + textureGbx->offset, textureGbx->scaled_width, textureGbx->scaled_height,
					textureId + headerId->offsets[0], width, height);
		}
		BS2PC_ResampleTexture(textureId + headerId->offsets[0], width, height,
				textureId + headerId->offsets[1], width >> 1, height >> 1);
		BS2PC_ResampleTexture(textureId + headerId->offsets[1], width >> 1, height >> 1,
				textureId + headerId->offsets[2], width >> 2, height >> 2);
		BS2PC_ResampleTexture(textureId + headerId->offsets[2], width >> 2, height >> 2,
				textureId + headerId->offsets[3], width >> 3, height >> 3);

		paletteGbx = bspfile_gbx + textureGbx->palette;
		paletteId = textureId + headerId->offsets[3] + (width >> 3) * (height >> 3);
		*((unsigned short *) paletteId) = 256;
		paletteId += sizeof(unsigned short);
		liquid = (textureGbx->name[0] == '!') ||
				(textureGbx->name[0] >= '0' && textureGbx->name[0] <= '9' && textureGbx->name[1] == '!') ||
				((textureGbx->name[0] == '+' || textureGbx->name[0] == '-') && textureGbx->name[2] == '!');
		for (colorIndex = 0; colorIndex < 256; ++colorIndex) {
			unsigned int colorIndexGbx, colorIndexLow;
			const unsigned char *colorGbx;
			
			colorIndexGbx = colorIndex;
			colorIndexLow = colorIndex & 0x1f;
			if (colorIndexLow >= 8 && colorIndexLow <= 15) {
				colorIndexGbx += 8;
			} else if (colorIndexLow >= 16 && colorIndexLow <= 23) {
				colorIndexGbx -= 8;
			}

			colorGbx = paletteGbx + (colorIndexGbx * 4);
			if (liquid) {
				*(paletteId++) = colorGbx[0];
				*(paletteId++) = colorGbx[1];
				*(paletteId++) = colorGbx[2];
			} else {
				*(paletteId++) = (unsigned char) (((unsigned int) min(colorGbx[0], 127)) * 255 / 127);
				*(paletteId++) = (unsigned char) (((unsigned int) min(colorGbx[1], 127)) * 255 / 127);
				*(paletteId++) = (unsigned char) (((unsigned int) min(colorGbx[2], 127)) * 255 / 127);
			}
		}
	}
}

#ifdef _WIN32
#define BS2PC_ZLIB_IMPORT WINAPI
#else
#define BS2PC_ZLIB_IMPORT
#endif
typedef int (BS2PC_ZLIB_IMPORT *qinflateInit__t)(z_streamp strm, const char *version, int stream_size);
static qinflateInit__t qinflateInit_;
typedef int (BS2PC_ZLIB_IMPORT *qinflate_t)(z_streamp strm, int flush);
static qinflate_t qinflate;
typedef int (BS2PC_ZLIB_IMPORT *qinflateEnd_t)(z_streamp strm);
static qinflateEnd_t qinflateEnd;

void BS2PC_InitializeZlib() {
	#ifdef _WIN32
	HMODULE module;
	module = LoadLibrary(TEXT("zlibwapi.dll"));
	if (module == NULL) {
		fputs("Couldn't open zlibwapi.dll.\n", stderr);
		exit(EXIT_FAILURE);
	}

	qinflateInit_ = (qinflateInit__t) GetProcAddress(module, "inflateInit_");
	qinflate = (qinflate_t) GetProcAddress(module, "inflate");
	qinflateEnd = (qinflateEnd_t) GetProcAddress(module, "inflateEnd");
	#else
	#error No zlib loading code for this platform.
	#endif

	if (qinflateInit_ == NULL || qinflate == NULL || qinflateEnd == NULL) {
		fputs("Couldn't get a zlib function.\n", stderr);
		exit(EXIT_FAILURE);
	}
}

int main(int argc, const char **argv) {
	FILE *bs2File, *idFile;
	long bs2FileSize;
	unsigned char *bs2FileContents;
	unsigned int gbxFileSize;
	z_stream stream;
	size_t fileNameLength;
	char *idFileName;

	fputs("BS2PC build 1 - Half-Life PlayStation 2 to PC map converter.\n", stderr);
	if (argc < 2) {
		fputs("Specify the .bs2 file path when launching.\n", stderr);
		return EXIT_SUCCESS;
	}

	fputs("Initializing zlib...\n", stderr);
	BS2PC_InitializeZlib();

	fputs("Loading the .bs2 file...\n", stderr);

	bs2File = fopen(argv[1], "rb");
	if (bs2File == NULL) {
		fputs("Couldn't open the .bs2 file.\n", stderr);
		return EXIT_FAILURE;
	}

	fseek(bs2File, 0, SEEK_END);
	bs2FileSize = ftell(bs2File);
	if (bs2FileSize <= 4) {
		fputs("Couldn't get the .bs2 file size or it's invalid.\n", stderr);
		return EXIT_FAILURE;
	}
	rewind(bs2File);

	bs2FileContents = (unsigned char *) malloc(bs2FileSize);
	if (bs2FileContents == NULL) {
		fputs("Couldn't allocate the .bs2 file contents.\n", stderr);
		return EXIT_FAILURE;
	}

	if (fread(bs2FileContents, bs2FileSize, 1, bs2File) == 0) {
		fputs("Couldn't read the .bs2 file.\n", stderr);
		return EXIT_FAILURE;
	}

	fclose(bs2File);

	fputs("Decompressing .bs2...\n", stderr);

	gbxFileSize = *((unsigned int *) bs2FileContents);
	bspfile_gbx = (unsigned char *) malloc(gbxFileSize);
	if (bspfile_gbx == NULL) {
		fputs("Couldn't allocate the uncompressed .bs2 file contents.\n", stderr);
		return EXIT_FAILURE;
	}

	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;
	stream.avail_in = bs2FileSize - sizeof(unsigned int);
	stream.next_in = (Bytef *) (bs2FileContents + sizeof(unsigned int));
	stream.avail_out = gbxFileSize;
	stream.next_out = (Bytef *) bspfile_gbx;
	if (qinflateInit_(&stream, ZLIB_VERSION, sizeof(stream)) != Z_OK) {
		fputs("Couldn't initialize decompression.\n", stderr);
		return EXIT_FAILURE;
	}
	if (qinflate(&stream, Z_NO_FLUSH) != Z_STREAM_END) {
		fputs("Couldn't decompress the map.\n", stderr);
		return EXIT_FAILURE;
	}
	qinflateEnd(&stream);

	free(bs2FileContents);

	header_gbx = (const dheader_gbx_t *) bspfile_gbx;
	if (header_gbx->version != BSPVERSION_GBX) {
		fputs("Invalid .bs2 version.\n", stderr);
		return EXIT_FAILURE;
	}

	fputs("Processing the texture lump...\n", stderr);
	BS2PC_PreProcessTextureLump();
	fputs("Building nodraw skipping info...\n", stderr);
	BS2PC_InitializeNodraw();
	fputs("Initializing the .bsp header...\n", stderr);
	BS2PC_AllocateIDBSP();
	fputs("Converting planes...\n", stderr);
	BS2PC_ConvertPlanes();
	fputs("Converting leaves...\n", stderr);
	BS2PC_ConvertLeafs();
	fputs("Converting vertices...\n", stderr);
	BS2PC_ConvertVertexes();
	fputs("Converting nodes...\n", stderr);
	BS2PC_ConvertNodes();
	fputs("Converting texture info...\n", stderr);
	BS2PC_ConvertTexinfo();
	fputs("Converting faces...\n", stderr);
	BS2PC_ConvertFaces();
	fputs("Copying clipnodes...\n", stderr);
	BS2PC_CopyLump(LUMP_GBX_CLIPNODES, LUMP_ID_CLIPNODES);
	fputs("Converting marksurfaces...\n", stderr);
	BS2PC_ConvertMarksurfaces();
	fputs("Copying surfedges...\n", stderr);
	BS2PC_CopyLump(LUMP_GBX_SURFEDGES, LUMP_ID_SURFEDGES);
	fputs("Copying edges...\n", stderr);
	BS2PC_CopyLump(LUMP_GBX_EDGES, LUMP_ID_EDGES);
	fputs("Converting models...\n", stderr);
	BS2PC_ConvertModels();
	fputs("Copying lighting...\n", stderr);
	BS2PC_CopyLump(LUMP_GBX_LIGHTING, LUMP_ID_LIGHTING);
	fputs("Copying visibility...\n", stderr);
	BS2PC_CopyLump(LUMP_GBX_VISIBILITY, LUMP_ID_VISIBILITY);
	fputs("Converting entities...\n", stderr);
	BS2PC_ConvertEntities();
	fputs("Converting textures...\n", stderr);
	BS2PC_ConvertTextures();

	fputs("Writing the .bsp file...\n", stderr);

	fileNameLength = strlen(argv[1]);
	idFileName = alloca(fileNameLength + 5);
	strcpy(idFileName, argv[1]);
	if (fileNameLength >= 4 && strcasecmp(idFileName + fileNameLength - 4, ".bs2") == 0) {
		idFileName[fileNameLength - 1] = (idFileName[fileNameLength - 3] == 'B' ? 'P' : 'p');
	} else {
		strcpy(idFileName + fileNameLength, ".bsp");
	}

	idFile = fopen(idFileName, "wb");
	if (idFile == NULL) {
		fputs("Couldn't open the .bsp file.\n", stderr);
		return EXIT_FAILURE;
	}

	if (fwrite(bspfile_id, bspfile_size_id, 1, idFile) == 0) {
		fputs("Couldn't write the .bsp file.\n", stderr);
		return EXIT_FAILURE;
	}

	fclose(idFile);

	fprintf(stderr, "%s converted.\n", argv[1]);

	return EXIT_SUCCESS;
}