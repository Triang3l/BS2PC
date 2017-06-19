#pragma once
#include "bs2pc_lumps.h"
#include <stdbool.h>
#include <stdlib.h>
#ifdef _MSC_VER
#include <malloc.h>
#define bs2pc_alloca _alloca
#define bs2pc_strcasecmp _stricmp
#define bs2pc_strncasecmp _strnicmp
#else
#include <alloca.h>
#define bs2pc_alloca alloca
#define bs2pc_strcasecmp strcasecmp
#define bs2pc_strncasecmp strncasecmp
#endif

// Utility

void *BS2PC_Alloc(size_t size, bool zeroed);
void BS2PC_AllocReplace(void **memory, size_t size, bool zeroed);
inline void BS2PC_Free(void *memory) { free(memory); }

void *BS2PC_LoadFile(const char *fileName, unsigned int *fileSize);
void BS2PC_WriteFile(const char *fileName, void *data, unsigned int size);

void BS2PC_Decompress(const void *source, unsigned int sourceSize, void *target, unsigned int targetSize);

void BS2PC_ResampleTexture(const unsigned char *in, int inwidth, int inheight, unsigned char *out, int outwidth, int outheight);

// Map

extern unsigned char *bs2pc_idMap, *bs2pc_gbxMap;
extern unsigned int bs2pc_idMapSize, bs2pc_gbxMapSize;
inline unsigned int BS2PC_IdLumpOffset(unsigned int lump) {
	return ((const dheader_id_t *) bs2pc_idMap)->lumps[lump].fileofs;
}
inline unsigned char *BS2PC_IdLump(unsigned int lump) {
	return bs2pc_idMap + BS2PC_IdLumpOffset(lump);
}
inline unsigned int BS2PC_IdLumpSize(unsigned int lump) {
	return ((const dheader_id_t *) bs2pc_idMap)->lumps[lump].filelen;
}
inline unsigned int BS2PC_GbxLumpOffset(unsigned int lump) {
	return ((const dheader_gbx_t *) bs2pc_gbxMap)->lumpofs[lump];
}
inline unsigned char *BS2PC_GbxLump(unsigned int lump) {
	return bs2pc_gbxMap + BS2PC_GbxLumpOffset(lump);
}
inline unsigned int BS2PC_GbxLumpSize(unsigned int lump) {
	return ((const dheader_gbx_t *) bs2pc_gbxMap)->lumplen[lump];
}
inline unsigned int BS2PC_GbxLumpCount(unsigned int lump) {
	return ((const dheader_gbx_t *) bs2pc_gbxMap)->lumpnum[lump];
}
inline unsigned int BS2PC_GbxOffsetToIndex(bspoffset_t offset, unsigned int lump, unsigned int lumpSize) {
	return (offset - BS2PC_GbxLumpOffset(lump)) / lumpSize;
}
inline bspoffset_t BS2PC_GbxIndexToOffset(unsigned int index, unsigned int lump, unsigned int lumpSize) {
	return BS2PC_GbxLumpOffset(lump) + index * lumpSize;
}

// Conversion

void BS2PC_ConvertGbxToId();
void BS2PC_ConvertIdToGbx();

// WAD texture management

void BS2PC_SetWadDirectory(const char *directory);
void BS2PC_LoadWadsFromEntities(const char *entities, unsigned int entitiesSize);
unsigned char *BS2PC_LoadTextureFromWad(const char *name);