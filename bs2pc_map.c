#include "bs2pc.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char *bs2pc_idMap, *bs2pc_gbxMap;
unsigned int bs2pc_idMapSize, bs2pc_gbxMapSize;

// BS2 pre-processing

static unsigned int bs2pc_idTextureLumpSize;
static bool *bs2pc_gbxTexturesSpecial = NULL; // Whether textures are special - texinfo needs this.

static void BS2PC_ProcessGbxTextureLump() {
	const dmiptex_gbx_t *texture = (const dmiptex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_TEXTURES);
	unsigned int index, count = BS2PC_GbxLumpCount(LUMP_GBX_TEXTURES);

	if (count == 0) {
		fputs("Map has no textures.\n", stderr);
		exit(EXIT_FAILURE);
	}

	BS2PC_AllocReplace(&bs2pc_gbxTexturesSpecial, count * sizeof(bool), true);

	bs2pc_idTextureLumpSize = sizeof(unsigned int) /* texture count */ +
			count * (sizeof(bspoffset_t) /* offset */ + sizeof(dmiptex_id_t) + (2 + 768 + 2));

	for (index = 0; index < count; ++index) {
		const char *name = texture->name;
		unsigned int width, height;

		if (name[0] == '*' ||
				bs2pc_strncasecmp(name, "sky", 3) == 0 ||
				bs2pc_strncasecmp(name, "clip", 4) == 0 ||
				bs2pc_strncasecmp(name, "origin", 6) == 0 ||
				bs2pc_strncasecmp(name, "aaatrigger", 10) == 0) {
			bs2pc_gbxTexturesSpecial[index] = true;
		}

		width = texture->width;
		height = texture->height;
		bs2pc_idTextureLumpSize += width * height +
				(width >> 1) * (height >> 1) +
				(width >> 2) * (height >> 2) +
				(width >> 3) * (height >> 3);

		++texture;
	}
}

static unsigned int BS2PC_TextureDimensionToGbx(unsigned int npot) {
	if (npot < 24) {
		return 16;
	}
	if (npot < 48) {
		return 32;
	}
	if (npot < 96) {
		return 64;
	}
	if (npot < 192) {
		return 128;
	}
	return 256;
}

typedef struct {
	unsigned int faceFlags;
	unsigned int animTotal, animNext, alternateAnims;
} bs2pc_idTextureAdditionalInfo_t;

static unsigned int bs2pc_idValidTextureCount = 0;
static unsigned int *bs2pc_idValidTextureMap = NULL;
static const dmiptex_id_t **bs2pc_idTextures = NULL;
static bs2pc_idTextureAdditionalInfo_t *bs2pc_idTextureAdditionalInfo = NULL;
static unsigned int bs2pc_idAnimatedStart = UINT_MAX, bs2pc_idAnimatedCount = 0;
static unsigned int bs2pc_gbxTextureDataSize = 0;

static unsigned int BS2PC_FindAnimatedIdTexture(const char *sequenceName, unsigned char frame) {
	char name[16];
	unsigned int low, mid, high;
	int difference;

	if (bs2pc_idAnimatedCount == 0) {
		return UINT_MAX;
	}

	name[0] = '+';
	name[1] = frame;
	strncpy(name + 2, sequenceName, sizeof(name) - 3);
	name[sizeof(name) - 1] = '\0';

	low = bs2pc_idAnimatedStart;
	high = bs2pc_idAnimatedStart + bs2pc_idAnimatedCount - 1;
	while (low <= high) {
		mid = low + ((high - low) >> 1);
		difference = bs2pc_strncasecmp(bs2pc_idTextures[mid]->name, name, 15);
		if (difference == 0) {
			return mid;
		}
		if (difference > 0) {
			high = mid - 1;
		} else {
			low = mid + 1;
		}
	}

	return UINT_MAX;
}

static void BS2PC_ProcessIdTextureLump() {
	const unsigned char *lump = BS2PC_IdLump(LUMP_ID_TEXTURES);
	const unsigned int *offsets = (const unsigned int *) lump + 1;
	unsigned int index, count = *((const unsigned int *) lump), validCount = 0;
	unsigned int nodrawIndex = UINT_MAX;

	if (count == 0) {
		fputs("Map has no textures.\n", stderr);
		exit(EXIT_FAILURE);
	}

	// Replacing textures with UINT_MAX offset with nodraw.
	BS2PC_AllocReplace(&bs2pc_idValidTextureMap, count * sizeof(unsigned int), true);
	BS2PC_AllocReplace((void **) &bs2pc_idTextures, (count + 1) * sizeof(dmiptex_id_t *), false); // + 1 for nodraw.
	// Check if we have any invalid textures at all.
	for (index = 0; index < count; ++index) {
		if (offsets[index] == UINT_MAX) {
			break;
		}
	}
	if (index < count) {
		for (index = 0; index < count; ++index) {
			unsigned int offset = offsets[index];
			const dmiptex_id_t *validTexture;
			int comparison;
			if (offset == UINT_MAX) {
				bs2pc_idValidTextureMap[index] = nodrawIndex;
				continue;
			}
			bs2pc_idValidTextureMap[index] = validCount;
			validTexture = (const dmiptex_id_t *) (lump + offset);
			if (nodrawIndex == UINT_MAX) {
				// Insert nodraw if needed.
				comparison = bs2pc_strncasecmp("nodraw", validTexture->name, sizeof(validTexture->name) - 1);
				if (comparison <= 0) {
					nodrawIndex = validCount;
					if (comparison < 0) {
						bs2pc_idTextures[validCount++] = (const dmiptex_id_t *) bs2pc_nodrawIdTexture;
					}
				}
			}
			bs2pc_idTextures[validCount++] = validTexture;
		}
		if (nodrawIndex == UINT_MAX) {
			// In case all textures are named alphabetically prior to nodraw.
			nodrawIndex = validCount;
			bs2pc_idTextures[validCount++] = (const dmiptex_id_t *) bs2pc_nodrawIdTexture;
		}
		for (index = 0; index < count; ++index) {
			if (bs2pc_idValidTextureMap[index] == UINT_MAX) {
				bs2pc_idValidTextureMap[index] = nodrawIndex;
			}
		}
	} else {
		for (index = 0; index < count; ++index) {
			bs2pc_idValidTextureMap[index] = index;
			bs2pc_idTextures[index] = (const dmiptex_id_t *) (lump + offsets[index]);
		}
		validCount = count;
	}
	bs2pc_idValidTextureCount = validCount;

	BS2PC_AllocReplace(&bs2pc_idTextureAdditionalInfo, validCount * sizeof(bs2pc_idTextureAdditionalInfo_t), false);
	bs2pc_idAnimatedStart = UINT_MAX;
	bs2pc_idAnimatedCount = 0;
	bs2pc_gbxTextureDataSize = 0;

	for (index = 0; index < validCount; ++index) {
		const dmiptex_id_t *texture = bs2pc_idTextures[index];
		const char *name = texture->name;
		bs2pc_idTextureAdditionalInfo_t *info = &bs2pc_idTextureAdditionalInfo[index];
		unsigned int scaledWidth, scaledHeight;

		unsigned int flags = 0;
		if (name[0] == '+') {
			if (bs2pc_idAnimatedStart == UINT_MAX) {
				bs2pc_idAnimatedStart = index;
			}
			++bs2pc_idAnimatedCount;
		} else if (name[0] == '!' || name[0] == '*' || bs2pc_strncasecmp(name, "water", 5) == 0) {
			flags = SURF_DRAWTURB | SURF_DRAWTILED | SURF_SPECIAL | SURF_HASPOLYS;
		} else if (name[0] == '{') {
			flags = SURF_HASPOLYS;
		} else if (bs2pc_strncasecmp(name, "aaatrigger", 10) == 0) {
			flags = SURF_DRAWTILED | SURF_SPECIAL;
		} else if (bs2pc_strncasecmp(name, "nodraw", 6) == 0) {
			flags = SURF_DRAWTILED | SURF_NODRAW | SURF_SPECIAL;
		} else if (bs2pc_strncasecmp(name, "scroll", 6) == 0) {
			flags = SURF_DRAWTILED;
		} else if (bs2pc_strncasecmp(name, "sky", 3) == 0) {
			flags = SURF_DRAWTILED | SURF_SPECIAL | SURF_DRAWSKY;
		}

		info->faceFlags = flags;
		info->animTotal = 0;
		info->animNext = UINT_MAX;
		info->alternateAnims = UINT_MAX;

		scaledWidth = BS2PC_TextureDimensionToGbx(texture->width);
		scaledHeight = BS2PC_TextureDimensionToGbx(texture->height);
		while (true) {
			bs2pc_gbxTextureDataSize += scaledWidth * scaledHeight;
			if (scaledWidth <= 8 || scaledHeight <= 8) {
				break;
			}
			scaledWidth >>= 1;
			scaledHeight >>= 1;
		}
	}

	if (bs2pc_idAnimatedCount != 0) {
		unsigned int firstNonAnimated = bs2pc_idAnimatedStart + bs2pc_idAnimatedCount;
		for (index = bs2pc_idAnimatedStart; index < firstNonAnimated; ++index) {
			const char *name = bs2pc_idTextures[index]->name;
			unsigned int seq[10], altSeq[10], count = 1, altCount = 0, frame, found;
			bs2pc_idTextureAdditionalInfo_t *info;

			if (name[0] != '+') {
				continue; // Shouldn't happen, but still, reject.
			}
			if (name[1] != '0') {
				break; // Only the beginnings of the chains are needed, the rest will be found with binary search.
			}

			// Build the sequences.
			seq[0] = index;
			altSeq[0] = UINT_MAX;
			for (frame = 1; frame < 10; ++frame) {
				found = BS2PC_FindAnimatedIdTexture(name + 2, frame + '0');
				if (found == UINT_MAX) {
					break;
				}
				seq[count++] = found;
			}
			for (frame = 0; frame < 10; ++frame) {
				found = BS2PC_FindAnimatedIdTexture(name + 2, frame + 'A');
				if (found == UINT_MAX) {
					break;
				}
				altSeq[altCount++] = found;
			}

			// Link the frames.
			for (frame = 0; frame < count; ++frame) {
				info = &bs2pc_idTextureAdditionalInfo[seq[frame]];
				info->animTotal = count;
				info->animNext = seq[frame + 1 < count ? frame + 1 : 0];
				info->alternateAnims = altSeq[0];
			}
			for (frame = 0; frame < altCount; ++frame) {
				info = &bs2pc_idTextureAdditionalInfo[altSeq[frame]];
				info->animTotal = altCount;
				info->animNext = altSeq[frame + 1 < altCount ? frame + 1 : 0];
				info->alternateAnims = seq[0];
			}
		}
	}
}

static unsigned int *bs2pc_nodrawFaceMap = NULL;
static unsigned int bs2pc_faceCountWithoutNodraw;
static unsigned int *bs2pc_nodrawMarksurfaceMap = NULL;
static unsigned int bs2pc_marksurfaceCountWithoutNodraw;
static dmarksurface_id_t *bs2pc_idMarksurfaceLumpWithoutNodraw = NULL;

static void BS2PC_BuildGbxNodrawSkippingInfo() {
	unsigned int faceCount = BS2PC_GbxLumpCount(LUMP_GBX_FACES);
	unsigned int marksurfaceCount = BS2PC_GbxLumpCount(LUMP_GBX_MARKSURFACES);
	unsigned int faceIndex, marksurfaceIndex;
	const dface_gbx_t *faces, *face;
	const dmarksurface_gbx_t *marksurface;

	if (faceCount == 0 || marksurfaceCount == 0) {
		fputs("Map has no surfaces.\n", stderr);
		exit(EXIT_FAILURE);
	}

	BS2PC_AllocReplace(&bs2pc_nodrawFaceMap, faceCount * sizeof(unsigned int), false);
	bs2pc_faceCountWithoutNodraw = 0;
	BS2PC_AllocReplace(&bs2pc_nodrawMarksurfaceMap, marksurfaceCount * sizeof(unsigned int), false);
	bs2pc_marksurfaceCountWithoutNodraw = 0;
	BS2PC_AllocReplace(&bs2pc_idMarksurfaceLumpWithoutNodraw, marksurfaceCount * sizeof(dmarksurface_id_t), false);

	faces = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES);

	for (faceIndex = 0, face = faces; faceIndex < faceCount; ++faceIndex, ++face) {
		bs2pc_nodrawFaceMap[faceIndex] = bs2pc_faceCountWithoutNodraw;
		if (!(face->flags & SURF_NODRAW)) {
			++bs2pc_faceCountWithoutNodraw;
		}
	}

	marksurface = (const dmarksurface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_MARKSURFACES);
	for (marksurfaceIndex = 0; marksurfaceIndex < marksurfaceCount; ++marksurfaceIndex, ++marksurface) {
		bs2pc_nodrawMarksurfaceMap[marksurfaceIndex] = bs2pc_marksurfaceCountWithoutNodraw;
		if (!(faces[*marksurface].flags & SURF_NODRAW)) {
			bs2pc_idMarksurfaceLumpWithoutNodraw[bs2pc_marksurfaceCountWithoutNodraw] = (unsigned short) bs2pc_nodrawFaceMap[*marksurface];
			++bs2pc_marksurfaceCountWithoutNodraw;
		}
	}
}

static void BS2PC_SkipNodrawInGbxFaceRange(unsigned int inFirst, unsigned int inCount, unsigned int *outFirst, unsigned int *outCount) {
	unsigned int index, count = 0;
	const dface_gbx_t *face = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES) + inFirst;
	*outFirst = bs2pc_nodrawFaceMap[inFirst];
	for (index = 0; index < inCount; ++index, ++face) {
		if (!(face->flags & SURF_NODRAW)) {
			++count;
		}
	}
	*outCount = count;
}

static void BS2PC_SkipNodrawInGbxMarksurfaceRange(unsigned int inFirst, unsigned int inCount, unsigned int *outFirst, unsigned int *outCount) {
	unsigned int index, count = 0;
	const dmarksurface_gbx_t *marksurface = (const dmarksurface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_MARKSURFACES) + inFirst;
	const dface_gbx_t *faces = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES);
	*outFirst = bs2pc_nodrawMarksurfaceMap[inFirst];
	for (index = 0; index < inCount; ++index, ++marksurface) {
		if (!(faces[*marksurface].flags & SURF_NODRAW)) {
			++count;
		}
	}
	*outCount = count;
}

static void BS2PC_PreProcessGbxMap() {
	fputs("Processing the texture lump...\n", stderr);
	BS2PC_ProcessGbxTextureLump();
	fputs("Building nodraw skipping info...\n", stderr);
	BS2PC_BuildGbxNodrawSkippingInfo();
}

static void BS2PC_PreProcessIdMap() {
	fputs("Processing the texture lump...\n", stderr);
	BS2PC_ProcessIdTextureLump();
}

// Conversion

static void BS2PC_AllocateIdMapFromGbx() {
	dheader_id_t headerId;
	unsigned int mapSize;

	headerId.version = BSPVERSION_ID;
	mapSize = (sizeof(dheader_id_t) + 3) & ~3;

	headerId.lumps[LUMP_ID_PLANES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_PLANES].filelen = BS2PC_GbxLumpCount(LUMP_GBX_PLANES) * sizeof(dplane_id_t);
	mapSize += (headerId.lumps[LUMP_ID_PLANES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_LEAFS].fileofs = mapSize;
	headerId.lumps[LUMP_ID_LEAFS].filelen = BS2PC_GbxLumpCount(LUMP_GBX_LEAFS) * sizeof(dleaf_id_t);
	mapSize += (headerId.lumps[LUMP_ID_LEAFS].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_VERTEXES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_VERTEXES].filelen = BS2PC_GbxLumpCount(LUMP_GBX_VERTEXES) * sizeof(dvertex_id_t);
	mapSize += (headerId.lumps[LUMP_ID_VERTEXES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_NODES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_NODES].filelen = BS2PC_GbxLumpCount(LUMP_GBX_NODES) * sizeof(dnode_id_t);
	mapSize += (headerId.lumps[LUMP_ID_NODES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_TEXINFO].fileofs = mapSize;
	headerId.lumps[LUMP_ID_TEXINFO].filelen = bs2pc_faceCountWithoutNodraw * sizeof(dtexinfo_id_t);
	mapSize += (headerId.lumps[LUMP_ID_TEXINFO].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_FACES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_FACES].filelen = bs2pc_faceCountWithoutNodraw * sizeof(dface_id_t);
	mapSize += (headerId.lumps[LUMP_ID_FACES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_CLIPNODES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_CLIPNODES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_CLIPNODES);
	mapSize += (headerId.lumps[LUMP_ID_CLIPNODES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_MARKSURFACES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_MARKSURFACES].filelen = bs2pc_marksurfaceCountWithoutNodraw * sizeof(dmarksurface_id_t);
	mapSize += (headerId.lumps[LUMP_ID_MARKSURFACES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_SURFEDGES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_SURFEDGES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_SURFEDGES);
	mapSize += (headerId.lumps[LUMP_ID_SURFEDGES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_EDGES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_EDGES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_EDGES);
	mapSize += (headerId.lumps[LUMP_ID_EDGES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_MODELS].fileofs = mapSize;
	headerId.lumps[LUMP_ID_MODELS].filelen = BS2PC_GbxLumpCount(LUMP_GBX_MODELS) * sizeof(dmodel_id_t);
	mapSize += (headerId.lumps[LUMP_ID_MODELS].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_LIGHTING].fileofs = mapSize;
	headerId.lumps[LUMP_ID_LIGHTING].filelen = BS2PC_GbxLumpSize(LUMP_GBX_LIGHTING);
	mapSize += (headerId.lumps[LUMP_ID_LIGHTING].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_VISIBILITY].fileofs = mapSize;
	headerId.lumps[LUMP_ID_VISIBILITY].filelen = BS2PC_GbxLumpSize(LUMP_GBX_VISIBILITY);
	mapSize += (headerId.lumps[LUMP_ID_VISIBILITY].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_ENTITIES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_ENTITIES].filelen = BS2PC_GbxLumpSize(LUMP_GBX_ENTITIES);
	mapSize += (headerId.lumps[LUMP_ID_ENTITIES].filelen + 3) & ~3;

	headerId.lumps[LUMP_ID_TEXTURES].fileofs = mapSize;
	headerId.lumps[LUMP_ID_TEXTURES].filelen = bs2pc_idTextureLumpSize;
	mapSize += (headerId.lumps[LUMP_ID_TEXTURES].filelen + 3) & ~3;

	bs2pc_idMapSize = mapSize;
	bs2pc_idMap = (unsigned char *) BS2PC_Alloc(mapSize, true);
	memcpy(bs2pc_idMap, &headerId, sizeof(dheader_id_t));
}

static void BS2PC_AllocateGbxMapFromId() {
	dheader_gbx_t headerGbx;
	unsigned int mapSize;

	memset(&headerGbx, 0, sizeof(headerGbx));
	headerGbx.version = BSPVERSION_GBX;
	mapSize = (sizeof(headerGbx) + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_PLANES] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_PLANES] = BS2PC_IdLumpSize(LUMP_ID_PLANES) / sizeof(dplane_id_t);
	headerGbx.lumplen[LUMP_GBX_PLANES] = headerGbx.lumpnum[LUMP_GBX_PLANES] * sizeof(dplane_gbx_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_PLANES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_NODES] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_NODES] = BS2PC_IdLumpSize(LUMP_ID_NODES) / sizeof(dnode_id_t);
	headerGbx.lumplen[LUMP_GBX_NODES] = headerGbx.lumpnum[LUMP_GBX_NODES] * sizeof(dnode_gbx_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_NODES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_LEAFS] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_LEAFS] = BS2PC_IdLumpSize(LUMP_ID_LEAFS) / sizeof(dleaf_id_t);
	headerGbx.lumplen[LUMP_GBX_LEAFS] = headerGbx.lumpnum[LUMP_GBX_LEAFS] * sizeof(dleaf_gbx_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_LEAFS] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_EDGES] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_EDGES] = BS2PC_IdLumpSize(LUMP_ID_EDGES) / sizeof(dedge_t);
	headerGbx.lumplen[LUMP_GBX_EDGES] = headerGbx.lumpnum[LUMP_GBX_EDGES] * sizeof(dedge_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_EDGES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_SURFEDGES] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_SURFEDGES] = BS2PC_IdLumpSize(LUMP_ID_SURFEDGES) / sizeof(unsigned int);
	headerGbx.lumplen[LUMP_GBX_SURFEDGES] = headerGbx.lumpnum[LUMP_GBX_SURFEDGES] * sizeof(unsigned int);
	mapSize += (headerGbx.lumplen[LUMP_GBX_SURFEDGES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_VERTEXES] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_VERTEXES] = BS2PC_IdLumpSize(LUMP_ID_VERTEXES) / sizeof(dvertex_id_t);
	headerGbx.lumplen[LUMP_GBX_VERTEXES] = headerGbx.lumpnum[LUMP_GBX_VERTEXES] * sizeof(dvertex_gbx_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_VERTEXES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_HULL0] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_HULL0] = BS2PC_IdLumpSize(LUMP_ID_NODES) / sizeof(dnode_id_t);
	headerGbx.lumplen[LUMP_GBX_HULL0] = headerGbx.lumpnum[LUMP_GBX_HULL0] * sizeof(dclipnode_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_HULL0] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_CLIPNODES] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_CLIPNODES] = BS2PC_IdLumpSize(LUMP_ID_CLIPNODES) / sizeof(dclipnode_t);
	headerGbx.lumplen[LUMP_GBX_CLIPNODES] = headerGbx.lumpnum[LUMP_GBX_CLIPNODES] * sizeof(dclipnode_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_CLIPNODES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_MODELS] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_MODELS] = BS2PC_IdLumpSize(LUMP_ID_MODELS) / sizeof(dmodel_id_t);
	headerGbx.lumplen[LUMP_GBX_MODELS] = headerGbx.lumpnum[LUMP_GBX_MODELS] * sizeof(dmodel_gbx_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_MODELS] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_FACES] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_FACES] = BS2PC_IdLumpSize(LUMP_ID_FACES) / sizeof(dface_id_t);
	headerGbx.lumplen[LUMP_GBX_FACES] = headerGbx.lumpnum[LUMP_GBX_FACES] * sizeof(dface_gbx_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_FACES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_MARKSURFACES] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_MARKSURFACES] = BS2PC_IdLumpSize(LUMP_ID_MARKSURFACES) / sizeof(dmarksurface_id_t);
	headerGbx.lumplen[LUMP_GBX_MARKSURFACES] = headerGbx.lumpnum[LUMP_GBX_MARKSURFACES] * sizeof(dmarksurface_gbx_t);
	mapSize += (headerGbx.lumplen[LUMP_GBX_MARKSURFACES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_VISIBILITY] = mapSize;
	headerGbx.lumplen[LUMP_GBX_VISIBILITY] = BS2PC_IdLumpSize(LUMP_ID_VISIBILITY);
	mapSize += (headerGbx.lumplen[LUMP_GBX_VISIBILITY] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_LIGHTING] = mapSize;
	headerGbx.lumplen[LUMP_GBX_LIGHTING] = BS2PC_IdLumpSize(LUMP_ID_LIGHTING);
	mapSize += (headerGbx.lumplen[LUMP_GBX_LIGHTING] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_TEXTURES] = mapSize;
	headerGbx.lumpnum[LUMP_GBX_TEXTURES] = bs2pc_idValidTextureCount;
	headerGbx.lumplen[LUMP_GBX_TEXTURES] = headerGbx.lumpnum[LUMP_GBX_TEXTURES] * (sizeof(dmiptex_gbx_t) + 256 * 4) + bs2pc_gbxTextureDataSize;
	mapSize += (headerGbx.lumplen[LUMP_GBX_TEXTURES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_ENTITIES] = mapSize;
	headerGbx.lumplen[LUMP_GBX_ENTITIES] = BS2PC_IdLumpSize(LUMP_ID_ENTITIES);
	mapSize += (headerGbx.lumplen[LUMP_GBX_ENTITIES] + 15) & ~15;

	headerGbx.lumpofs[LUMP_GBX_POLYS] = mapSize;
	// TODO: Polys.
	mapSize += (headerGbx.lumplen[LUMP_GBX_POLYS] + 15) & ~15;

	bs2pc_gbxMapSize = mapSize;
	bs2pc_gbxMap = (unsigned char *) BS2PC_Alloc(mapSize, true);
	memcpy(bs2pc_gbxMap, &headerGbx, sizeof(dheader_gbx_t));
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
	unsigned int index, count = BS2PC_GbxLumpCount(LUMP_GBX_PLANES);
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
	unsigned int index, count = BS2PC_GbxLumpCount(LUMP_GBX_LEAFS);
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
		BS2PC_SkipNodrawInGbxMarksurfaceRange(gbx->firstmarksurface, gbx->nummarksurfaces, &firstMarksurface, &marksurfaceCount);
		id->firstmarksurface = (unsigned short) firstMarksurface;
		id->nummarksurfaces = (unsigned short) marksurfaceCount;
		memcpy(id->ambient_level, gbx->ambient_level, sizeof(id->ambient_level));
	}
}

static void BS2PC_ConvertLeafsToGbx() {
	const dleaf_id_t *id = (const dleaf_id_t *) BS2PC_IdLump(LUMP_ID_LEAFS);
	dleaf_gbx_t *gbx = (dleaf_gbx_t *) BS2PC_GbxLump(LUMP_GBX_LEAFS);
	unsigned int index, count = BS2PC_IdLumpSize(LUMP_ID_LEAFS) / sizeof(dleaf_id_t);
	for (index = 0; index < count; ++index, ++id, ++gbx) {
		gbx->contents = id->contents;
		gbx->parent = UINT_MAX;
		gbx->visframe = 0;
		gbx->unknown1 = 0;
		gbx->mins[0] = (float) id->mins[0];
		gbx->mins[1] = (float) id->mins[1];
		gbx->mins[2] = (float) id->mins[2];
		gbx->mins[3] = 0.0f;
		gbx->maxs[0] = (float) id->maxs[0];
		gbx->maxs[1] = (float) id->maxs[1];
		gbx->maxs[2] = (float) id->maxs[2];
		gbx->maxs[3] = 0.0f;
		gbx->visofs = id->visofs + (id->visofs != UINT_MAX ? BS2PC_GbxLumpOffset(LUMP_GBX_VISIBILITY) : 0);
		gbx->firstmarksurface = (unsigned short) id->firstmarksurface;
		gbx->nummarksurfaces = (unsigned short) id->nummarksurfaces;
		memcpy(gbx->ambient_level, id->ambient_level, sizeof(gbx->ambient_level));
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
	unsigned int index, count = BS2PC_GbxLumpCount(LUMP_GBX_VERTEXES);
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
	unsigned int index, count = BS2PC_GbxLumpCount(LUMP_GBX_NODES);
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
		BS2PC_SkipNodrawInGbxFaceRange(gbx->firstface, gbx->numfaces, &firstFace, &faceCount);
		id->firstface = (unsigned short) firstFace;
		id->numfaces = (unsigned short) faceCount;
	}
}

static void BS2PC_ConvertNodesToGbx() {
	const dnode_id_t *id = (const dnode_id_t *) BS2PC_IdLump(LUMP_ID_NODES);
	dnode_gbx_t *gbx = (dnode_gbx_t *) BS2PC_GbxLump(LUMP_GBX_NODES);
	unsigned int index, count = BS2PC_IdLumpSize(LUMP_ID_NODES) / sizeof(dnode_id_t);
	dleaf_gbx_t *gbxLeafs = (dleaf_gbx_t *) BS2PC_GbxLump(LUMP_GBX_LEAFS);
	unsigned int leafCount = BS2PC_IdLumpSize(LUMP_ID_LEAFS) / sizeof(dleaf_id_t);
	unsigned int offset = BS2PC_GbxLumpOffset(LUMP_GBX_NODES);

	for (index = 0; index < count; ++index, ++id, ++gbx, offset += sizeof(dnode_gbx_t)) {
		gbx->contents = 0;
		gbx->parent = UINT_MAX;
		gbx->visframe = 0;
		gbx->plane = BS2PC_GbxIndexToOffset(id->planenum, LUMP_GBX_PLANES, sizeof(dplane_gbx_t));
		gbx->mins[0] = (float) id->mins[0];
		gbx->mins[1] = (float) id->mins[1];
		gbx->mins[2] = (float) id->mins[2];
		gbx->mins[3] = 0.0f;
		gbx->maxs[0] = (float) id->maxs[0];
		gbx->maxs[1] = (float) id->maxs[1];
		gbx->maxs[2] = (float) id->maxs[2];
		gbx->maxs[3] = 0.0f;

		if (id->children[0] >= 0) {
			gbx->children[0] = BS2PC_GbxIndexToOffset(id->children[0], LUMP_GBX_NODES, sizeof(dnode_gbx_t));
		} else {
			gbx->children[0] = BS2PC_GbxIndexToOffset(-(id->children[0] + 1), LUMP_GBX_LEAFS, sizeof(dleaf_gbx_t));
		}

		if (id->children[1] >= 0) {
			gbx->children[1] = BS2PC_GbxIndexToOffset(id->children[1], LUMP_GBX_NODES, sizeof(dnode_gbx_t));
		} else {
			gbx->children[1] = BS2PC_GbxIndexToOffset(-(id->children[1] + 1), LUMP_GBX_LEAFS, sizeof(dleaf_gbx_t));
		}

		gbx->firstface = id->firstface;
		gbx->numfaces = id->numfaces;
	}
}

static void BS2PC_SetGbxNodeParent(bspoffset_t nodeOffset, bspoffset_t parentOffset) {
	dnode_gbx_t *node = (dnode_gbx_t *) (bs2pc_gbxMap + nodeOffset);
	if (node->contents < 0) {
		((dleaf_gbx_t *) node)->parent = parentOffset;
		return;
	}
	node->parent = parentOffset;
	BS2PC_SetGbxNodeParent(node->children[0], nodeOffset);
	BS2PC_SetGbxNodeParent(node->children[1], nodeOffset);
}

static void BS2PC_ConvertTexinfoToId() {
	const dface_gbx_t *gbx = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES);
	dtexinfo_id_t *id = (dtexinfo_id_t *) BS2PC_IdLump(LUMP_ID_TEXINFO);
	unsigned int index, count = BS2PC_GbxLumpCount(LUMP_GBX_FACES);
	for (index = 0; index < count; ++index, ++gbx) {
		if (gbx->flags & SURF_NODRAW) {
			continue;
		}
		memcpy(id->vecs, gbx->vecs, sizeof(id->vecs));
		id->miptex = BS2PC_GbxOffsetToIndex(gbx->miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t));
		id->flags = (bs2pc_gbxTexturesSpecial[id->miptex] ? TEX_SPECIAL : 0);
		++id;
	}
}

static void BS2PC_ConvertFacesToId() {
	const dface_gbx_t *gbx = (const dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES);
	dface_id_t *id = (dface_id_t *) BS2PC_IdLump(LUMP_ID_FACES);
	unsigned int index, count = BS2PC_GbxLumpCount(LUMP_GBX_FACES);
	unsigned int idIndex = 0;
	for (index = 0; index < count; ++index, ++gbx) {
		if (gbx->flags & SURF_NODRAW) {
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

static void BS2PC_CalcSurfaceExtents(const dface_id_t *face, short *outTextureMins, short *outExtents) {
	const dtexinfo_id_t *tex = (const dtexinfo_id_t *) BS2PC_IdLump(LUMP_ID_TEXINFO) + face->texinfo;
	const unsigned int *surfedges = (const unsigned int *) BS2PC_IdLump(LUMP_ID_SURFEDGES);
	const dedge_t *edges = (const dedge_t *) BS2PC_IdLump(LUMP_ID_EDGES);
	const dvertex_id_t *vertexes = (const dvertex_id_t *) BS2PC_IdLump(LUMP_ID_VERTEXES), *v;
	float mins[] = { 999999.0f, 999999.0f }, maxs[] = { -999999.0f, -999999.0f }, val;
	unsigned int i, j;
	int e, bmins[2], bmaxs[2];

	for (i = 0; i < face->numedges; ++i) {
		e = surfedges[face->firstedge + i];
		if (e >= 0) {
			v = &vertexes[edges[e].v[0]];
		} else {
			v = &vertexes[edges[-e].v[1]];
		}
		
		for (j = 0; j < 2; ++j) {
			val = v->point[0] * tex->vecs[j][0] +
				v->point[1] * tex->vecs[j][1] +
				v->point[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j]) {
				mins[j] = val;
			}
			if (val > maxs[j]) {
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; ++i)
	{	
		bmins[i] = (int) floorf(mins[i] * (1.0f / 16.0f));
		bmaxs[i] = (int) ceilf(maxs[i] * (1.0f / 16.0f));

		outTextureMins[i] = (short) (bmins[i] * 16.0f);
		outExtents[i] = (short) ((bmaxs[i] - bmins[i]) * 16.0f);
	}
}

static void BS2PC_ConvertFacesToGbx() {
	const dface_id_t *id = (const dface_id_t *) BS2PC_IdLump(LUMP_ID_FACES);
	const dtexinfo_id_t *texinfoLump = (const dtexinfo_id_t *) BS2PC_IdLump(LUMP_ID_TEXINFO);
	dface_gbx_t *gbx = (dface_gbx_t *) BS2PC_GbxLump(LUMP_GBX_FACES);
	unsigned int index, count = BS2PC_GbxLumpCount(LUMP_GBX_FACES);
	for (index = 0; index < count; ++index, ++id, ++gbx) {
		const dtexinfo_id_t *texinfo = &texinfoLump[id->texinfo];
		unsigned int miptex = bs2pc_idValidTextureMap[texinfo->miptex];
		float len;
		memset(gbx, 0, sizeof(dface_gbx_t)); // Lots of unknown fields that are probably runtime-only and 0 in the file.
		memcpy(gbx->vecs, texinfo->vecs, sizeof(gbx->vecs));
		gbx->side = id->side;
		// TODO: Don't skip SURF_HASPOLYS when subdivision is done.
		gbx->flags = (gbx->side != 0 ? SURF_PLANEBACK : 0) | (bs2pc_idTextureAdditionalInfo[miptex].faceFlags & ~SURF_HASPOLYS);
		gbx->miptex = BS2PC_GbxIndexToOffset(miptex, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t));
		gbx->lightofs = id->lightofs + (id->lightofs != UINT_MAX ? BS2PC_GbxLumpOffset(LUMP_GBX_LIGHTING) : 0);
		gbx->plane = BS2PC_GbxIndexToOffset(id->planenum, LUMP_GBX_PLANES, sizeof(dplane_gbx_t));
		gbx->firstedge = id->firstedge;
		gbx->numedges = id->numedges;
		len = sqrtf(gbx->vecs[0][0] * gbx->vecs[0][0] + gbx->vecs[0][1] * gbx->vecs[0][1] + gbx->vecs[0][2] * gbx->vecs[0][2]) *
				sqrtf(gbx->vecs[1][0] * gbx->vecs[1][0] + gbx->vecs[1][1] * gbx->vecs[1][1] + gbx->vecs[1][2] * gbx->vecs[1][2]);
		if (len < 0.001f) {
			len = 1.0f;
		}
		gbx->len = len;
		BS2PC_CalcSurfaceExtents(id, gbx->texturemins, gbx->extents);
		memcpy(gbx->styles, id->styles, sizeof(gbx->styles));
		// TODO: Polys.
		gbx->polys = UINT_MAX;
	}
}

static void BS2PC_ConvertMarksurfacesToId() {
	memcpy(BS2PC_IdLump(LUMP_ID_MARKSURFACES), bs2pc_idMarksurfaceLumpWithoutNodraw,
			bs2pc_marksurfaceCountWithoutNodraw * sizeof(dmarksurface_id_t));
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
	unsigned int index, count = BS2PC_GbxLumpCount(LUMP_GBX_MODELS);
	for (index = 0; index < count; ++index, ++gbx, ++id) {
		memcpy(id->mins, gbx->mins, 3 * sizeof(float));
		memcpy(id->maxs, gbx->maxs, 3 * sizeof(float));
		memcpy(id->origin, gbx->origin, 3 * sizeof(float));
		memcpy(id->headnode, gbx->headnode, sizeof(id->headnode));
		id->visleafs = gbx->visleafs;
		BS2PC_SkipNodrawInGbxFaceRange(gbx->firstface, gbx->numfaces, &id->firstface, &id->numfaces);
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
						bs2pc_strncasecmp(stringStart, "models/", 7) == 0 &&
						bs2pc_strncasecmp(stringStart + stringLength - 4, ".dol", 4) == 0) {
					stringStart[stringLength - 3] += 'm' - 'd';
					stringStart[stringLength - 2] += 'd' - 'o';
					// stringStart[stringLength - 1] += 'l' - 'l';
				} else if (stringLength >= 12 &&
						bs2pc_strncasecmp(stringStart, "sprites/", 8) == 0 &&
						bs2pc_strncasecmp(stringStart + stringLength - 4, ".spz", 4) == 0) {
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
						bs2pc_strncasecmp(stringStart, "models/", 7) == 0 &&
						bs2pc_strncasecmp(stringStart + stringLength - 4, ".mdl", 4) == 0) {
					stringStart[stringLength - 3] += 'd' - 'm';
					stringStart[stringLength - 2] += 'o' - 'd';
					// stringStart[stringLength - 1] += 'l' - 'l';
				} else if (stringLength >= 12 &&
						bs2pc_strncasecmp(stringStart, "sprites/", 8) == 0 &&
						bs2pc_strncasecmp(stringStart + stringLength - 4, ".spr", 4) == 0) {
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

void BS2PC_ConvertTexturesToId() {
	const dmiptex_gbx_t *texturesGbx = (const dmiptex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_TEXTURES);
	unsigned char *lumpId = BS2PC_IdLump(LUMP_ID_TEXTURES);
	unsigned int *miptexOffsets, miptexOffset;
	unsigned int textureIndex, textureCount = BS2PC_GbxLumpCount(LUMP_GBX_TEXTURES);

	// Miptex table
	*((unsigned int *) lumpId) = textureCount;
	miptexOffsets = (unsigned int *) (lumpId + sizeof(unsigned int));
	miptexOffset = (textureCount + 1) * sizeof(unsigned int);
	for (textureIndex = 0; textureIndex < textureCount; ++textureIndex) {
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
	for (textureIndex = 0; textureIndex < textureCount; ++textureIndex) {
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
				(textureGbx->name[0] >= '0' && textureGbx->name[0] <= '9' && textureGbx->name[1] == '!');
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

			colorGbx = paletteGbx + colorIndexGbx * 4;
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

void BS2PC_ConvertTexturesToGbx() {
	unsigned int textureIndex, textureCount = bs2pc_idValidTextureCount;
	dmiptex_gbx_t *textureGbx = (dmiptex_gbx_t *) BS2PC_GbxLump(LUMP_GBX_TEXTURES);
	const bs2pc_idTextureAdditionalInfo_t *additionalInfo = bs2pc_idTextureAdditionalInfo;
	bspoffset_t gbxMipOffset = BS2PC_GbxLumpOffset(LUMP_GBX_TEXTURES) + textureCount * sizeof(dmiptex_gbx_t);
	bspoffset_t gbxPaletteOffset = gbxMipOffset + bs2pc_gbxTextureDataSize;

	for (textureIndex = 0; textureIndex < textureCount; ++textureIndex, ++textureGbx, ++additionalInfo) {
		const dmiptex_id_t *textureId = bs2pc_idTextures[textureIndex];
		// Save for the case if a WAD has a differently sized texture.
		unsigned int width = textureId->width, height = textureId->height;
		unsigned int scaledWidth = BS2PC_TextureDimensionToGbx(width);
		unsigned int scaledHeight = BS2PC_TextureDimensionToGbx(height);
		unsigned int tempWidth, tempHeight;
		unsigned int mipLevels = 0, mip = 1;
		const unsigned char *textureData;
		bspoffset_t lastGbxMipOffset = gbxMipOffset;
		unsigned int colorIndex;
		unsigned char *colorGbx;
		bool liquid = (textureId->name[0] == '!');

		textureGbx->offset = gbxMipOffset;
		textureGbx->palette = gbxPaletteOffset;
		textureGbx->width = width;
		textureGbx->height = height;
		textureGbx->scaled_width = scaledWidth;
		textureGbx->scaled_height = scaledHeight;
		memcpy(textureGbx->name, textureId->name, sizeof(textureGbx->name));
		memset(textureGbx->unknown1, 0, sizeof(textureGbx->unknown1));

		tempWidth = scaledWidth;
		tempHeight = scaledHeight;
		while (true) {
			++mipLevels;
			if (tempWidth <= 8 || tempHeight <= 8) {
				break;
			}
			tempWidth >>= 1;
			tempHeight >>= 1;
		}
		textureGbx->miplevels = mipLevels - 1;

		textureGbx->anim_total = additionalInfo->animTotal;
		if (textureGbx->anim_total != 0) {
			unsigned int animMin = 0;
			unsigned int frame = textureGbx->name[1];
			if (frame >= '0' && frame <= '9') {
				animMin = frame - '0';
			} else if (frame >= 'A' && frame <= 'J') {
				animMin = frame - 'A';
			} else if (frame >= 'a' && frame <= 'j') {
				animMin = frame - 'a';
			}
			textureGbx->anim_min = animMin;
			textureGbx->anim_max = animMin + 1;
			textureGbx->anim_next = BS2PC_GbxIndexToOffset(additionalInfo->animNext, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t));
			textureGbx->alternate_anims = (additionalInfo->alternateAnims != UINT_MAX ?
					BS2PC_GbxIndexToOffset(additionalInfo->alternateAnims, LUMP_GBX_TEXTURES, sizeof(dmiptex_gbx_t)) : UINT_MAX);
		} else {
			textureGbx->anim_min = textureGbx->anim_max = 0;
			textureGbx->anim_next = textureGbx->alternate_anims = UINT_MAX;
		}

		if (textureId->offsets[0] == 0 || textureId->offsets[1] == 0 ||
				textureId->offsets[2] == 0 || textureId->offsets[3] == 0) {
			textureId = (const dmiptex_id_t *) BS2PC_LoadTextureFromWad(textureId->name);
			if (textureId != NULL && (textureId->offsets[0] == 0 || textureId->offsets[1] == 0 ||
					textureId->offsets[2] == 0 || textureId->offsets[3] == 0)) {
				textureId = NULL;
			}
		}

		textureData = (const unsigned char *) textureId;

		tempWidth = scaledWidth;
		tempHeight = scaledHeight;
		if (textureId != NULL) {
			unsigned int wadWidth = textureId->width, wadHeight = textureId->height;
			if (wadWidth == tempWidth && wadHeight == tempHeight) {
				for (mip = 0; mip < 4 && mip < mipLevels; ++mip) {
					memcpy(bs2pc_gbxMap + gbxMipOffset, textureData + textureId->offsets[mip], tempWidth * tempHeight);
					lastGbxMipOffset = gbxMipOffset;
					gbxMipOffset += tempWidth * tempHeight;
					tempWidth >>= 1;
					tempHeight >>= 1;
				}
			} else {
				BS2PC_ResampleTexture(textureData + textureId->offsets[0], wadWidth, wadHeight, bs2pc_gbxMap + gbxMipOffset, tempWidth, tempHeight);
				gbxMipOffset += tempWidth * tempHeight;
				tempWidth >>= 1;
				tempHeight >>= 1;
			}
		} else {
			unsigned int x, y;
			char *dest = bs2pc_gbxMap + gbxMipOffset;
			fprintf(stderr, "Texture %s not found or is incomplete in WADs, replacing with a checkerboard.\n", textureGbx->name);
			for (y = 0; y < tempHeight; ++y) {
				for (x = 0; x < tempWidth; ++x) {
					*(dest++) = (((((y & 15)) < 8) ^ (((x & 15)) < 8)) ? 0 : 255);
				}
			}
			gbxMipOffset += tempWidth * tempHeight;
			tempWidth >>= 1;
			tempHeight >>= 1;
		}
		for (; mip < mipLevels; ++mip) {
			BS2PC_ResampleTexture(bs2pc_gbxMap + lastGbxMipOffset, tempWidth << 1, tempHeight << 1, bs2pc_gbxMap + gbxMipOffset, tempWidth, tempHeight);
			lastGbxMipOffset = gbxMipOffset;
			gbxMipOffset += tempWidth * tempHeight;
			tempWidth >>= 1;
			tempHeight >>= 1;
		}

		if (textureId != NULL) {
			const unsigned char *colorId = textureData + textureId->offsets[3] + (textureId->width >> 3) * (textureId->height >> 3);
			unsigned int colorCount = *((const unsigned short *) colorId);
			colorId += sizeof(unsigned short);
			for (colorIndex = 0; colorIndex < 256; ++colorIndex) {
				unsigned int colorIndexGbx, colorIndexLow;
			
				colorIndexGbx = colorIndex;
				colorIndexLow = colorIndex & 0x1f;
				if (colorIndexLow >= 8 && colorIndexLow <= 15) {
					colorIndexGbx += 8;
				} else if (colorIndexLow >= 16 && colorIndexLow <= 23) {
					colorIndexGbx -= 8;
				}
				colorGbx = bs2pc_gbxMap + gbxPaletteOffset + colorIndexGbx * 4;

				if (colorIndex < colorCount) {
					if (liquid) {
						*(colorGbx++) = *(colorId++);
						*(colorGbx++) = *(colorId++);
						*(colorGbx++) = *(colorId++);
					} else {
						*(colorGbx++) = *(colorId++) >> 1;
						*(colorGbx++) = *(colorId++) >> 1;
						*(colorGbx++) = *(colorId++) >> 1;
					}
				} else {
					*(colorGbx++) = 0;
					*(colorGbx++) = 0;
					*(colorGbx++) = 0;
				}
				*(colorGbx++) = 0x80;
			}
		} else {
			colorGbx = bs2pc_gbxMap + gbxPaletteOffset;
			for (colorIndex = 0; colorIndex < 255; ++colorIndex) {
				*(colorGbx++) = 0;
				*(colorGbx++) = 0;
				*(colorGbx++) = 0;
				*(colorGbx++) = 0x80;
			}
			*(colorGbx++) = (liquid ? 0xff : 0x7f);
			*(colorGbx++) = 0;
			*(colorGbx++) = (liquid ? 0xff : 0x7f);
			*(colorGbx++) = 0x80;
		}
		gbxPaletteOffset += 256 * 4;
	}
}

void BS2PC_ConvertGbxToId() {
	if (((const dheader_gbx_t *) bs2pc_gbxMap)->version != BSPVERSION_GBX) {
		fputs("Invalid .bs2 version.\n", stderr);
		exit(EXIT_FAILURE);
	}
	BS2PC_PreProcessGbxMap();
	fputs("Initializing the .bsp header...\n", stderr);
	BS2PC_AllocateIdMapFromGbx();
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
}

void BS2PC_ConvertIdToGbx() {
	if (((const dheader_id_t *) bs2pc_idMap)->version != BSPVERSION_ID) {
		fputs("Invalid .bsp version.\n", stderr);
		exit(EXIT_FAILURE);
	}
	BS2PC_PreProcessIdMap();
	fputs("Initializing the .bsp header...\n", stderr);
	BS2PC_AllocateGbxMapFromId();
	fputs("Converting planes...\n", stderr);
	BS2PC_ConvertPlanesToGbx();
	fputs("Converting nodes...\n", stderr);
	BS2PC_ConvertNodesToGbx();
	fputs("Converting leaves...\n", stderr);
	BS2PC_ConvertLeafsToGbx();
	fputs("Linking nodes...\n", stderr);
	BS2PC_SetGbxNodeParent(BS2PC_GbxLumpOffset(LUMP_GBX_NODES), UINT_MAX);
	fputs("Copying edges...\n", stderr);
	BS2PC_CopyLumpToGbx(LUMP_ID_EDGES, LUMP_GBX_EDGES);
	fputs("Copying surfedges...\n", stderr);
	BS2PC_CopyLumpToGbx(LUMP_ID_SURFEDGES, LUMP_GBX_SURFEDGES);
	fputs("Converting vertices...\n", stderr);
	BS2PC_ConvertVertexesToGbx();
	fputs("Building world clipping hull...\n", stderr);
	BS2PC_MakeGbxHull0();
	fputs("Copying clipnodes...\n", stderr);
	BS2PC_CopyLumpToGbx(LUMP_ID_CLIPNODES, LUMP_GBX_CLIPNODES);
	fputs("Converting models...\n", stderr);
	BS2PC_ConvertModelsToGbx();
	fputs("Converting faces...\n", stderr);
	BS2PC_ConvertFacesToGbx();
	fputs("Converting marksurfaces...\n", stderr);
	BS2PC_ConvertMarksurfacesToGbx();
	fputs("Copying visibility...\n", stderr);
	BS2PC_CopyLumpToGbx(LUMP_ID_VISIBILITY, LUMP_GBX_VISIBILITY);
	fputs("Copying lighting...\n", stderr);
	BS2PC_CopyLumpToGbx(LUMP_ID_LIGHTING, LUMP_GBX_LIGHTING);
	fputs("Converting textures...\n", stderr);
	BS2PC_ConvertTexturesToGbx();
	fputs("Converting entities...\n", stderr);
	BS2PC_ConvertEntitiesToGbx();
	// TODO: Polys.
}