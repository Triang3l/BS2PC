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

#include "bs2pc.h"
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
	const dmiptex_gbx_t *textureGbx = (const dmiptex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_TEXTURES);
	unsigned int textureIndex;

	// The first texture's bytes follow the last texture info.
	texture_count = BS2PC_GbxOffsetToIndex(textureGbx->offset, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t));
	textures_special = BS2PC_Alloc(texture_count * sizeof(bool), true);

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
		texture_lump_size += width * height +
				(width >> 1) * (height >> 1) +
				(width >> 2) * (height >> 2) +
				(width >> 3) * (height >> 3);

		++textureGbx;
	}
}

static void BS2PC_InitializeNodraw() {
	unsigned int faceCount = BS2PC_GbxLumpSize(LUMP_GBX_FACES) / sizeof(dface_gbx_t);
	unsigned int marksurfaceCount = BS2PC_GbxLumpSize(LUMP_GBX_MARKSURFACES) / sizeof(dmarksurface_gbx_t);
	unsigned int faceIndex, marksurfaceIndex;
	const dface_gbx_t *faces, *face;
	const dmarksurface_gbx_t *marksurface;

	if (texture_nodraw == UINT_MAX) {
		return;
	}

	nodraw_face_map = (unsigned int *) BS2PC_Alloc(faceCount * sizeof(unsigned int), false);
	nodraw_face_count = 0;
	nodraw_marksurface_map = (dmarksurface_gbx_t *) BS2PC_Alloc(marksurfaceCount * sizeof(dmarksurface_gbx_t), false);
	nodraw_marksurface_count = 0;
	nodraw_marksurface_lump_id = (dmarksurface_id_t *) BS2PC_Alloc(marksurfaceCount * sizeof(dmarksurface_id_t), false);

	faces = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES);

	for (faceIndex = 0, face = faces; faceIndex < faceCount; ++faceIndex, ++face) {
		nodraw_face_map[faceIndex] = nodraw_face_count;
		if (BS2PC_GbxOffsetToIndex(face->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) != texture_nodraw) {
			++nodraw_face_count;
		}
	}

	marksurface = (const dmarksurface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_MARKSURFACES);
	for (marksurfaceIndex = 0; marksurfaceIndex < marksurfaceCount; ++marksurfaceIndex, ++marksurface) {
		nodraw_marksurface_map[marksurfaceIndex] = nodraw_marksurface_count;
		if (BS2PC_GbxOffsetToIndex(faces[*marksurface].miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) != texture_nodraw) {
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
	gbxFace = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES) + inFirst;
	for (index = 0; index < inCount; ++index, ++gbxFace) {
		if (BS2PC_GbxOffsetToIndex(gbxFace->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) != texture_nodraw) {
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
	gbxMarksurface = (const dmarksurface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_MARKSURFACES) + inFirst;
	for (index = 0; index < inCount; ++index, ++gbxMarksurface) {
		gbxFace = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES) + *gbxMarksurface;
		if (BS2PC_GbxOffsetToIndex(gbxFace->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) != texture_nodraw) {
			++count;
		}
	}
	*outCount = count;
}

static void BS2PC_AllocateIdBSP() {
	dheader_id_t headerId;
	unsigned int bspSize;
	unsigned int faceCount;

	headerId.version = BSPVERSION_ID;
	bspSize = (sizeof(dheader_id_t) + 3) & ~3;

	if (texture_nodraw != UINT_MAX) {
		faceCount = nodraw_face_count;
	} else {
		faceCount = BS2PC_GbxLumpSize(LUMP_GBX_FACES) / sizeof(dface_gbx_t);
	}

	headerId.lumps[LUMP_ID_PLANES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_PLANES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_PLANES) / sizeof(dplane_gbx_t) * sizeof(dplane_id_t);
	bspSize += (headerId.lumps[LUMP_ID_PLANES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_LEAFS].fileofs = bspSize;
	headerId.lumps[LUMP_ID_LEAFS].filelen = BS2PC_GbxLumpSize(LUMP_GBX_LEAFS) / sizeof(dleaf_gbx_t) * sizeof(dleaf_id_t);
	bspSize += (headerId.lumps[LUMP_ID_LEAFS].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_VERTEXES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_VERTEXES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_VERTEXES) / sizeof(dvertex_gbx_t) * sizeof(dvertex_id_t);
	bspSize += (headerId.lumps[LUMP_ID_VERTEXES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_NODES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_NODES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_NODES) / sizeof(dnode_gbx_t) * sizeof(dnode_id_t);
	bspSize += (headerId.lumps[LUMP_ID_NODES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_TEXINFO].fileofs = bspSize;
	headerId.lumps[LUMP_ID_TEXINFO].filelen = faceCount * sizeof(dtexinfo_id_t);
	bspSize += (headerId.lumps[LUMP_ID_TEXINFO].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_FACES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_FACES].filelen = faceCount * sizeof(dface_id_t);
	bspSize += (headerId.lumps[LUMP_ID_FACES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_CLIPNODES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_CLIPNODES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_CLIPNODES);
	bspSize += (headerId.lumps[LUMP_ID_CLIPNODES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_MARKSURFACES].fileofs = bspSize;
	if (texture_nodraw != UINT_MAX) {
		headerId.lumps[LUMP_ID_MARKSURFACES].filelen = nodraw_marksurface_count * sizeof(dmarksurface_id_t);
	} else {
		headerId.lumps[LUMP_ID_MARKSURFACES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_MARKSURFACES) / sizeof(dmarksurface_gbx_t) * sizeof(dmarksurface_id_t);
	}
	bspSize += (headerId.lumps[LUMP_ID_MARKSURFACES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_SURFEDGES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_SURFEDGES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_SURFEDGES);
	bspSize += (headerId.lumps[LUMP_ID_SURFEDGES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_EDGES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_EDGES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_EDGES);
	bspSize += (headerId.lumps[LUMP_ID_EDGES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_MODELS].fileofs = bspSize;
	headerId.lumps[LUMP_ID_MODELS].filelen = BS2PC_GbxLumpSize(LUMP_GBX_MODELS) / sizeof(dmodel_gbx_t) * sizeof(dmodel_id_t);
	bspSize += (headerId.lumps[LUMP_ID_MODELS].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_LIGHTING].fileofs = bspSize;
	headerId.lumps[LUMP_ID_LIGHTING].filelen = BS2PC_GbxLumpSize(LUMP_GBX_LIGHTING);
	bspSize += (headerId.lumps[LUMP_ID_LIGHTING].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_VISIBILITY].fileofs = bspSize;
	headerId.lumps[LUMP_ID_VISIBILITY].filelen = BS2PC_GbxLumpSize(LUMP_GBX_VISIBILITY);
	bspSize += (headerId.lumps[LUMP_ID_VISIBILITY].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_ENTITIES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_ENTITIES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_ENTITIES);
	bspSize += (headerId.lumps[LUMP_ID_ENTITIES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_TEXTURES].fileofs = bspSize;
	headerId.lumps[LUMP_ID_TEXTURES].filelen = texture_lump_size;
	bspSize += (headerId.lumps[LUMP_ID_TEXTURES].filelen + 3) & ~3;

	bs2pc_idMapSize = bspSize;
	bs2pc_idMap = (unsigned char *) BS2PC_Alloc(bspSize, true);
	memcpy(bs2pc_idMap, &headerId, sizeof(dheader_id_t));
}

static void BS2PC_CopyLumpToId(unsigned int gbxLump, unsigned int idLump) {
	memcpy(BS2PC_IdLump(idLump), BS2PC_GbxLump(gbxLump), BS2PC_GbxLumpSize(gbxLump));
}

static void BS2PC_CopyLumpToGbx(unsigned int idLump, unsigned int gbxLump) {
	memcpy(BS2PC_GbxLump(gbxLump), BS2PC_IdLump(idLump), BS2PC_IdLumpSize(idLump));
}

static void BS2PC_ConvertPlanesToId() {
	const dplane_gbx_t *gbx = (const dplane_gbx_t *) BS2PC_GbxLump(LUMP_GBX_PLANES);
	dplane_id_t *id = (dplane_id_t *) BS2PC_IdLump(LUMP_ID_PLANES);
	unsigned int index, count = BS2PC_GbxLumpSize(LUMP_GBX_PLANES) / sizeof(dplane_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		id->normal[0] = gbx->normal[0];
		id->normal[1] = gbx->normal[1];
		id->normal[2] = gbx->normal[2];
		id->dist = gbx->dist;
		id->type = gbx->type;
	}
}

static void BS2PC_ConvertPlanesToGbx() {
	const dplane_id_t *id = (const dplane_id_t *) BS2PC_IdLump(LUMP_ID_PLANES);
	dplane_gbx_t *gbx = (dplane_gbx_t *) BS2PC_GbxLump(LUMP_GBX_PLANES);
	unsigned int index, count = BS2PC_IdLumpSize(LUMP_ID_PLANES) / sizeof(dplane_id_t);
	for (index = 0; index < count; ++index, ++id, ++gbx) {
		gbx->normal[0] = id->normal[0];
		gbx->normal[1] = id->normal[1];
		gbx->normal[2] = id->normal[2];
		gbx->dist = id->dist;
		gbx->type = id->type;
		gbx->signbits = (id->normal[0] < 0.0f ? 1 : 0) | (id->normal[1] < 0.0f ? 2 : 0) | (id->normal[2] < 0.0f ? 4 : 0);
		gbx->pad[0] = gbx->pad[1] = 0;
	}
}

static void BS2PC_ConvertLeafsToId() {
	const dleaf_gbx_t *gbx = (const dleaf_gbx_t *) BS2PC_GbxLump(LUMP_GBX_LEAFS);
	dleaf_id_t *id = (dleaf_id_t *) BS2PC_IdLump(LUMP_ID_LEAFS);
	unsigned int index, count = BS2PC_GbxLumpSize(LUMP_GBX_LEAFS) / sizeof(dleaf_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		unsigned int firstMarksurface, marksurfaceCount;
		id->contents = gbx->contents;
		id->mins[0] = (short) gbx->mins[0];
		id->mins[1] = (short) gbx->mins[1];
		id->mins[2] = (short) gbx->mins[2];
		id->maxs[0] = (short) gbx->maxs[0];
		id->maxs[1] = (short) gbx->maxs[1];
		id->maxs[2] = (short) gbx->maxs[2];
		id->visofs = gbx->visofs - (gbx->visofs != UINT_MAX ? BS2PC_GbxLumpOffset(LUMP_GBX_VISIBILITY) : 0);
		BS2PC_RemapMarksurfaceFaceRange(gbx->firstmarksurface, gbx->nummarksurfaces, &firstMarksurface, &marksurfaceCount);
		id->firstmarksurface = (unsigned short) firstMarksurface;
		id->nummarksurfaces = (unsigned short) marksurfaceCount;
		memcpy(id->ambient_level, gbx->ambient_level, sizeof(id->ambient_level));
	}
}

static void BS2PC_MakeGbxHull0() {
	const dnode_id_t *id = (const dnode_id_t *) BS2PC_IdLump(LUMP_ID_NODES);
	const dleaf_id_t *idLeafs = (const dleaf_id_t *) BS2PC_IdLump(LUMP_ID_LEAFS);
	dclipnode_t *gbx = (dclipnode_t *) BS2PC_GbxLump(LUMP_GBX_HULL0);
	unsigned int index, count = BS2PC_IdLumpSize(LUMP_ID_NODES) / sizeof(dnode_id_t);
	for (index = 0; index < count; ++index, ++id, ++gbx) {
		gbx->planenum = id->planenum;
		if (id->children[0] >= 0) {
			gbx->children[0] = id->children[0];
		} else {
			gbx->children[0] = (short) idLeafs[-(id->children[0] + 1)].contents;
		}
		if (id->children[1] >= 0) {
			gbx->children[1] = id->children[1];
		} else {
			gbx->children[1] = (short) idLeafs[-(id->children[1] + 1)].contents;
		}
	}
}

static void BS2PC_ConvertVertexesToId() {
	const dvertex_gbx_t *gbx = (const dvertex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_VERTEXES);
	dvertex_id_t *id = (dvertex_id_t *) BS2PC_IdLump(LUMP_ID_VERTEXES);
	unsigned int index, count = BS2PC_GbxLumpSize(LUMP_GBX_VERTEXES) / sizeof(dvertex_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		id->point[0] = gbx->point[0];
		id->point[1] = gbx->point[1];
		id->point[2] = gbx->point[2];
	}
}

static void BS2PC_ConvertVertexesToGbx() {
	const dvertex_id_t *id = (const dvertex_id_t *) BS2PC_IdLump(LUMP_ID_VERTEXES);
	dvertex_gbx_t *gbx = (dvertex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_VERTEXES);
	unsigned int index, count = BS2PC_IdLumpSize(LUMP_ID_VERTEXES) / sizeof(dvertex_id_t);
	for (index = 0; index < count; ++index, ++id, ++gbx) {
		gbx->point[0] = id->point[0];
		gbx->point[1] = id->point[1];
		gbx->point[2] = id->point[2];
		gbx->point[3] = 0.0f;
	}
}

static void BS2PC_ConvertNodesToId() {
	const dnode_gbx_t *gbx = (const dnode_gbx_t *) BS2PC_GbxLump(LUMP_GBX_NODES);
	dnode_id_t *id = (dnode_id_t *) BS2PC_IdLump(LUMP_ID_NODES);
	unsigned int index, count = BS2PC_GbxLumpSize(LUMP_GBX_NODES) / sizeof(dnode_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		const dnode_gbx_t *child;
		unsigned int firstFace, faceCount;

		id->planenum = BS2PC_GbxOffsetToIndex(gbx->plane, LUMP_GBX_PLANES, sizeof(dplane_gbx_t));

		child = (const dnode_gbx_t *) (bs2pc_gbxMap + gbx->children[0]);
		if (child->contents == 0) {
			id->children[0] = (short) BS2PC_GbxOffsetToIndex(gbx->children[0], LUMP_GBX_NODES, sizeof(dnode_gbx_t)); 
		} else {
			id->children[0] = -1 - (short) BS2PC_GbxOffsetToIndex(gbx->children[0], LUMP_GBX_LEAFS, sizeof(dleaf_gbx_t));
		}

		child = (const dnode_gbx_t *) (bs2pc_gbxMap + gbx->children[1]);
		if (child->contents == 0) {
			id->children[1] = (short) BS2PC_GbxOffsetToIndex(gbx->children[1], LUMP_GBX_NODES, sizeof(dnode_gbx_t)); 
		} else {
			id->children[1] = -1 - (short) BS2PC_GbxOffsetToIndex(gbx->children[1], LUMP_GBX_LEAFS, sizeof(dleaf_gbx_t));
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

static void BS2PC_ConvertTexinfoToId() {
	const dface_gbx_t *gbx = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES);
	dtexinfo_id_t *id = (dtexinfo_id_t *) BS2PC_IdLump(LUMP_ID_TEXINFO);
	unsigned int index, count = BS2PC_GbxLumpSize(LUMP_GBX_FACES) / sizeof(dface_gbx_t);
	for (index = 0; index < count; ++index, ++gbx) {
		unsigned int miptex = BS2PC_GbxOffsetToIndex(gbx->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t));
		if (texture_nodraw != UINT_MAX && miptex == texture_nodraw) {
			continue;
		}
		memcpy(id->vecs, gbx->vecs, sizeof(id->vecs));
		id->miptex = miptex;
		id->flags = (textures_special[miptex] ? TEX_SPECIAL : 0);
		++id;
	}
}

static void BS2PC_ConvertFacesToId() {
	const dface_gbx_t *gbx = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES);
	dface_id_t *id = (dface_id_t *) BS2PC_IdLump(LUMP_ID_FACES);
	unsigned int index, count = BS2PC_GbxLumpSize(LUMP_GBX_FACES) / sizeof(dface_gbx_t);
	unsigned int idIndex = 0;
	for (index = 0; index < count; ++index, ++gbx) {
		if (texture_nodraw != UINT_MAX && BS2PC_GbxOffsetToIndex(gbx->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) == texture_nodraw) {
			continue;
		}
		id->planenum = (unsigned short) BS2PC_GbxOffsetToIndex(gbx->plane, LUMP_GBX_PLANES, sizeof(dplane_gbx_t));
		id->side = gbx->side;
		id->firstedge = gbx->firstedge;
		id->numedges = (unsigned short) gbx->numedges;
		id->texinfo = (unsigned short) idIndex;
		memcpy(id->styles, gbx->styles, sizeof(id->styles));
		id->lightofs = gbx->lightofs - (gbx->lightofs != UINT_MAX ? BS2PC_GbxLumpOffset(LUMP_GBX_LIGHTING) : 0);
		++idIndex;
		++id;
	}
}

static void BS2PC_ConvertMarksurfacesToId() {
	const dmarksurface_gbx_t *gbx;
	dmarksurface_id_t *id = (dmarksurface_id_t *) BS2PC_IdLump(LUMP_ID_MARKSURFACES);
	unsigned int index, count;

	if (texture_nodraw != UINT_MAX) {
		memcpy(id, nodraw_marksurface_lump_id, nodraw_marksurface_count * sizeof(dmarksurface_id_t));
		return;
	}

	gbx = (const dmarksurface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_MARKSURFACES);
	count = BS2PC_GbxLumpSize(LUMP_GBX_MARKSURFACES) / sizeof(dmarksurface_gbx_t);
	for (index = 0; index < count; ++index, ++id) {
		*id = (dmarksurface_id_t) gbx[index];
	}
}

static void BS2PC_ConvertMarksurfacesToGbx() {
	const dmarksurface_id_t *id = (const dmarksurface_id_t *) BS2PC_IdLump(LUMP_ID_MARKSURFACES);
	dmarksurface_gbx_t *gbx = (dmarksurface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_MARKSURFACES);
	unsigned int index, count = BS2PC_IdLumpSize(LUMP_ID_MARKSURFACES) / sizeof(dmarksurface_id_t);
	for (index = 0; index < count; ++index, ++id, ++gbx) {
		*gbx = (dmarksurface_gbx_t) *id;
	}
}

static void BS2PC_ConvertModelsToId() {
	const dmodel_gbx_t *gbx = (const dmodel_gbx_t *) BS2PC_GbxLump(LUMP_GBX_MODELS);
	dmodel_id_t *id = (dmodel_id_t *) BS2PC_IdLump(LUMP_ID_MODELS);
	unsigned int index, count = BS2PC_GbxLumpSize(LUMP_GBX_MODELS) / sizeof(dmodel_gbx_t);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		memcpy(id->mins, gbx->mins, 3 * sizeof(float));
		memcpy(id->maxs, gbx->maxs, 3 * sizeof(float));
		memcpy(id->origin, gbx->origin, 3 * sizeof(float));
		memcpy(id->headnode, gbx->headnode, sizeof(id->headnode));
		id->visleafs = gbx->visleafs;
		BS2PC_RemapNodrawFaceRange(gbx->firstface, gbx->numfaces, &id->firstface, &id->numfaces);
	}
}

static void BS2PC_ConvertModelsToGbx() {
	const dmodel_id_t *id = (const dmodel_id_t *) BS2PC_IdLump(LUMP_ID_MODELS);
	dmodel_gbx_t *gbx = (dmodel_gbx_t *) BS2PC_GbxLump(LUMP_GBX_MODELS);
	unsigned int index, count = BS2PC_IdLumpSize(LUMP_ID_MODELS) / sizeof(dmodel_id_t);
	for (index = 0; index < count; ++index, ++id, ++gbx) {
		memcpy(gbx->mins, id->mins, 3 * sizeof(float));
		gbx->mins[3] = 0.0f;
		memcpy(gbx->maxs, id->maxs, 3 * sizeof(float));
		gbx->maxs[3] = 0.0f;
		memcpy(gbx->origin, id->origin, 3 * sizeof(float));
		gbx->origin[3] = 0.0f;
		memcpy(gbx->headnode, id->headnode, sizeof(id->headnode));
		gbx->visleafs = id->visleafs;
		gbx->firstface = id->firstface;
		gbx->numfaces = id->numfaces;
		memset(gbx->pad, 0, sizeof(gbx->pad));
	}
}

static void BS2PC_ConvertEntitiesToId() {
	const char *gbx = (const char *) BS2PC_GbxLump(LUMP_GBX_ENTITIES);
	char *id = (char *) BS2PC_IdLump(LUMP_ID_ENTITIES);
	unsigned int index, count = BS2PC_GbxLumpSize(LUMP_GBX_ENTITIES);

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

static void BS2PC_ConvertEntitiesToGbx() {
	const char *id = (const char *) BS2PC_IdLump(LUMP_ID_ENTITIES);
	char *gbx = (char *) BS2PC_GbxLump(LUMP_GBX_ENTITIES);
	unsigned int index, count = BS2PC_IdLumpSize(LUMP_ID_ENTITIES);

	char *stringStart = NULL;
	unsigned int stringLength;

	for (index = 0; index < count; ++index, ++id, ++gbx) {
		char character = *id;
		*gbx = character;

		if (character == '"') {
			if (stringStart != NULL) {
				if (stringLength >= 11 &&
						strncasecmp(stringStart, "models/", 7) == 0 &&
						strncasecmp(stringStart + stringLength - 4, ".mdl", 4) == 0) {
					stringStart[stringLength - 3] += 'd' - 'm';
					stringStart[stringLength - 2] += 'o' - 'd';
					// stringStart[stringLength - 1] += 'l' - 'l';
				} else if (stringLength >= 12 &&
						strncasecmp(stringStart, "sprites/", 8) == 0 &&
						strncasecmp(stringStart + stringLength - 4, ".dol", 4) == 0) {
					// stringStart[stringLength - 3] += 's' - 's';
					// stringStart[stringLength - 2] += 'p' - 'p';
					stringStart[stringLength - 1] += 'z' - 'r';
				}
				stringStart = NULL;
			} else {
				stringStart = gbx + 1;
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

void BS2PC_ConvertTexturesToId() {
	const dmiptex_gbx_t *texturesGbx = (const dmiptex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_TEXTURES);
	unsigned char *lumpId = BS2PC_IdLump(LUMP_ID_TEXTURES);
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
			memcpy(textureId + headerId->offsets[0], bs2pc_gbxMap + textureGbx->offset, width * height);
		} else {
			BS2PC_ResampleTexture(bs2pc_gbxMap + textureGbx->offset, textureGbx->scaled_width, textureGbx->scaled_height,
					textureId + headerId->offsets[0], width, height);
		}
		BS2PC_ResampleTexture(textureId + headerId->offsets[0], width, height,
				textureId + headerId->offsets[1], width >> 1, height >> 1);
		BS2PC_ResampleTexture(textureId + headerId->offsets[1], width >> 1, height >> 1,
				textureId + headerId->offsets[2], width >> 2, height >> 2);
		BS2PC_ResampleTexture(textureId + headerId->offsets[2], width >> 2, height >> 2,
				textureId + headerId->offsets[3], width >> 3, height >> 3);

		paletteGbx = bs2pc_gbxMap + textureGbx->palette;
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

int main(int argc, const char **argv) {
	unsigned char *bs2File;
	long bs2FileSize;
	size_t fileNameLength;
	char *idFileName;

	fputs("BS2PC build 1 - Half-Life PlayStation 2 to PC map converter.\n", stderr);
	if (argc < 2) {
		fputs("Specify the .bs2 file path when launching.\n", stderr);
		return EXIT_SUCCESS;
	}

	fputs("Loading the .bs2 file...\n", stderr);
	bs2File = (unsigned char *) BS2PC_LoadFile(argv[1], &bs2FileSize);
	if (bs2FileSize <= sizeof(unsigned int)) {
		fputs(".bs2 file size is invalid.\n", stderr);
		return EXIT_FAILURE;
	}

	fputs("Decompressing .bs2...\n", stderr);
	bs2pc_gbxMapSize = *((unsigned int *) bs2File);
	bs2pc_gbxMap = (unsigned char *) BS2PC_Alloc(bs2pc_gbxMapSize, false);
	BS2PC_Decompress(bs2File + sizeof(unsigned int), bs2FileSize - sizeof(unsigned int), bs2pc_gbxMap, bs2pc_gbxMapSize);
	BS2PC_Free(bs2File);

	if (((const dheader_gbx_t *) bs2pc_gbxMap)->version != BSPVERSION_GBX) {
		fputs("Invalid .bs2 version.\n", stderr);
		return EXIT_FAILURE;
	}

	fputs("Processing the texture lump...\n", stderr);
	BS2PC_PreProcessTextureLump();
	fputs("Building nodraw skipping info...\n", stderr);
	BS2PC_InitializeNodraw();
	fputs("Initializing the .bsp header...\n", stderr);
	BS2PC_AllocateIdBSP();
	fputs("Converting planes...\n", stderr);
	BS2PC_ConvertPlanesToId();
	fputs("Converting leaves...\n", stderr);
	BS2PC_ConvertLeafsToId();
	fputs("Converting vertices...\n", stderr);
	BS2PC_ConvertVertexesToId();
	fputs("Converting nodes...\n", stderr);
	BS2PC_ConvertNodesToId();
	fputs("Converting texture info...\n", stderr);
	BS2PC_ConvertTexinfoToId();
	fputs("Converting faces...\n", stderr);
	BS2PC_ConvertFacesToId();
	fputs("Copying clipnodes...\n", stderr);
	BS2PC_CopyLumpToId(LUMP_GBX_CLIPNODES, LUMP_ID_CLIPNODES);
	fputs("Converting marksurfaces...\n", stderr);
	BS2PC_ConvertMarksurfacesToId();
	fputs("Copying surfedges...\n", stderr);
	BS2PC_CopyLumpToId(LUMP_GBX_SURFEDGES, LUMP_ID_SURFEDGES);
	fputs("Copying edges...\n", stderr);
	BS2PC_CopyLumpToId(LUMP_GBX_EDGES, LUMP_ID_EDGES);
	fputs("Converting models...\n", stderr);
	BS2PC_ConvertModelsToId();
	fputs("Copying lighting...\n", stderr);
	BS2PC_CopyLumpToId(LUMP_GBX_LIGHTING, LUMP_ID_LIGHTING);
	fputs("Copying visibility...\n", stderr);
	BS2PC_CopyLumpToId(LUMP_GBX_VISIBILITY, LUMP_ID_VISIBILITY);
	fputs("Converting entities...\n", stderr);
	BS2PC_ConvertEntitiesToId();
	fputs("Converting textures...\n", stderr);
	BS2PC_ConvertTexturesToId();

	fputs("Writing the .bsp file...\n", stderr);

	fileNameLength = strlen(argv[1]);
	idFileName = alloca(fileNameLength + 5);
	strcpy(idFileName, argv[1]);
	if (fileNameLength >= 4 && strcasecmp(idFileName + fileNameLength - 4, ".bs2") == 0) {
		idFileName[fileNameLength - 1] = (idFileName[fileNameLength - 3] == 'B' ? 'P' : 'p');
	} else {
		strcpy(idFileName + fileNameLength, ".bsp");
	}

	BS2PC_WriteFile(idFileName, bs2pc_idMap, bs2pc_idMapSize);

	fprintf(stderr, "%s converted.\n", argv[1]);

	return EXIT_SUCCESS;
}