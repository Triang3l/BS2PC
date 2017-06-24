#include "bs2pc.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct bs2pc_wadDirectory_s {
	char *path;
	size_t pathLength;
	struct bs2pc_wadDirectory_s *next;
} bs2pc_wadDirectory_t;
static bs2pc_wadDirectory_t *bs2pc_wadDirectories = NULL;
static size_t bs2pc_longestWadDirectoryPath = 0;

static unsigned int bs2pc_maxWadLumpSize = 0;

void BS2PC_AddWadDirectory(const char *path) {
	size_t length = strlen(path);
	bs2pc_wadDirectory_t *directory;
	if (length == 0) {
		return;
	}
	directory = BS2PC_Alloc(sizeof(bs2pc_wadDirectory_t), false);
	directory->path = BS2PC_Alloc(length + 1, false);
	strcpy(directory->path, path);
	directory->pathLength = length;
	directory->next = bs2pc_wadDirectories;
	bs2pc_wadDirectories = directory;
	if (length > bs2pc_longestWadDirectoryPath) {
		bs2pc_longestWadDirectoryPath = length;
	}
}

#pragma pack(push, 4)

typedef struct {
	char identification[4];
	unsigned int numlumps;
	unsigned int infotableofs;
} bs2pc_wadHeader_t;

typedef struct {
	unsigned int filepos;
	unsigned int disksize;
	unsigned int size;
	char type;
	char compression;
	char pad[2];
	char name[16];
} bs2pc_wadLumpInfo_t;

#pragma pack(pop)

typedef struct bs2pc_wad_s {
	FILE *file;
	unsigned int lumpCount;
	bs2pc_wadLumpInfo_t *lumps;
	struct bs2pc_wad_s *next;
} bs2pc_wad_t;

static bs2pc_wad_t *bs2pc_wads = NULL;

static void BS2PC_LoadWad(const char *fileName /* Assuming / slash */) {
	FILE *file;
	char *fileNameWithGame;
	bs2pc_wadHeader_t header;
	bs2pc_wad_t *wad;
	unsigned int index;
	const bs2pc_wadLumpInfo_t *lump;

	file = fopen(fileName, "rb");
	if (file != NULL) {
		fprintf(stderr, "%s loaded.\n", fileName);
	} else if (bs2pc_wadDirectories != NULL) {
		const bs2pc_wadDirectory_t *wadDirectory;
		const char *fileNameRelative = strrchr(fileName, '/');
		fileNameRelative = (fileNameRelative != NULL ? fileNameRelative + 1 : fileName);
		fileNameWithGame = bs2pc_alloca(bs2pc_longestWadDirectoryPath + 1 + strlen(fileNameRelative) + 1);
		for (wadDirectory = bs2pc_wadDirectories; wadDirectory != NULL; wadDirectory = wadDirectory->next) {
			strcpy(fileNameWithGame, wadDirectory->path);
			fileNameWithGame[wadDirectory->pathLength] = '/';
			strcpy(fileNameWithGame + wadDirectory->pathLength + 1, fileNameRelative);
			file = fopen(fileNameWithGame, "rb");
			if (file != NULL) {
				fprintf(stderr, "%s loaded.\n", fileNameWithGame);
				break;
			}
		}
	}

	if (file == NULL) {
		fprintf(stderr, "Couldn't find %s.\n", fileName);
		return;
	}

	if (fread(&header, sizeof(bs2pc_wadHeader_t), 1, file) == 0) {
		fprintf(stderr, "Couldn't read the header of %s.\n", fileName);
		fclose(file);
		return;
	}

	if (header.identification[0] != 'W' || header.identification[1] != 'A' ||
			header.identification[2] != 'D' || header.identification[3] != '3' ||
			header.numlumps == 0) {
		fprintf(stderr, "%s is invalid or empty.\n", fileName);
		fclose(file);
		return;
	}

	if (fseek(file, header.infotableofs, SEEK_SET) != 0) {
		fprintf(stderr, "Couldn't seek to the information table of %s.\n", fileName);
		fclose(file);
		return;
	}

	wad = BS2PC_Alloc(sizeof(bs2pc_wad_t), false);
	wad->file = file;
	wad->lumpCount = header.numlumps;
	wad->lumps = (bs2pc_wadLumpInfo_t *) BS2PC_Alloc(header.numlumps * sizeof(bs2pc_wadLumpInfo_t), false);
	if (fread(wad->lumps, header.numlumps * sizeof(bs2pc_wadLumpInfo_t), 1, file) == 0) {
		fprintf(stderr, "Couldn't read the information table of %s.\n", fileName);
		BS2PC_Free(wad->lumps);
		BS2PC_Free(wad);
		fclose(file);
		return;
	}
	wad->next = bs2pc_wads;
	bs2pc_wads = wad;

	lump = wad->lumps;
	for (index = 0; index < header.numlumps; ++index, ++lump) {
		if (lump->size > bs2pc_maxWadLumpSize) {
			bs2pc_maxWadLumpSize = lump->size;
		}
	}
}

static void BS2PC_LoadWadsFromList(const char *list, unsigned int listLength) {
	char *fileName;
	size_t fileNameLength = 0;
	size_t index;

	fileName = (char *) bs2pc_alloca(listLength + 1);
	for (index = 0; index <= listLength /* For +1 */; ++index) {
		int character = list[index];
		if (character == ';' || character == '"') {
			if (fileNameLength != 0) {
				fileName[fileNameLength] = '\0';
				BS2PC_LoadWad(fileName);
			}
			fileNameLength = 0;
		} else {
			fileName[fileNameLength++] = (character == '\\' ? '/' : character);
		}
	}
}

void BS2PC_LoadWadsFromEntities(const char *entities, unsigned int entitiesSize) {
	unsigned int index;
	bool isKey = false; // Will be flipped next quote.
	bool isWadList = false;
	const char *stringStart = NULL;
	unsigned int stringLength;
	for (index = 0; index < entitiesSize; ++index) {
		char character = entities[index];
		if (stringStart != NULL) {
			if (character == '"') {
				if (isKey) {
					isWadList = (stringLength == 3 && bs2pc_strncasecmp(stringStart, "wad", 3) == 0) ||
							(stringLength == 4 && bs2pc_strncasecmp(stringStart, "_wad", 4) == 0);
				} else if (isWadList) {
					BS2PC_LoadWadsFromList(stringStart, stringLength);
				}
				stringStart = NULL;
			} else {
				++stringLength;
			}
		} else {
			if (character == '}') {
				// Only parse worldspawn.
				break;
			}
			if (character == '"') {
				isKey = !isKey;
				stringStart = &entities[index + 1];
				stringLength = 0;
			}
		}
	}
}

static int BS2PC_WadSearchComparison(const void *keyLump, const void *lump) {
	return bs2pc_strncasecmp(((const bs2pc_wadLumpInfo_t *) keyLump)->name,
			((const bs2pc_wadLumpInfo_t *) lump)->name,
			sizeof(((const bs2pc_wadLumpInfo_t *) keyLump)->name) - 1);
}

static unsigned char *bs2pc_wadLumpBuffer = NULL;
static unsigned int bs2pc_wadLumpBufferSize = 0;

unsigned char *BS2PC_LoadTextureFromWad(const char *name) {
	const bs2pc_wad_t *wad;
	const bs2pc_wadLumpInfo_t *lumpInfo = NULL;

	if (bs2pc_maxWadLumpSize == 0) {
		return NULL;
	}

	for (wad = bs2pc_wads; wad != NULL; wad = wad->next) {
		const bs2pc_wadLumpInfo_t *lumps = wad->lumps;
		unsigned int low = 0, mid, high = wad->lumpCount;
		int difference;
		while (low <= high) {
			mid = low + ((high - low) >> 1);
			difference = BS2PC_CompareTextureNames(lumps[mid].name, name);
			if (difference == 0) {
				lumpInfo = &lumps[mid];
				break;
			}
			if (difference > 0) {
				high = mid - 1;
			} else {
				low = mid + 1;
			}
		}

		if (lumpInfo == NULL) {
			continue;
		}
		if (lumpInfo->type != (64 + 3) || lumpInfo->compression != 0) {
			fprintf(stderr, "Lump %s is not a texture or compressed in one of the WADs, skipping.\n", name);
			lumpInfo = NULL;
			continue;
		}
		break;
	}

	if (lumpInfo == NULL) {
		return NULL;
	}


	if (bs2pc_wadLumpBufferSize < bs2pc_maxWadLumpSize) {
		bs2pc_wadLumpBufferSize = bs2pc_maxWadLumpSize;
		BS2PC_AllocReplace(&bs2pc_wadLumpBuffer, bs2pc_wadLumpBufferSize, false);
	}

	if (fseek(wad->file, lumpInfo->filepos, SEEK_SET) != 0) {
		fprintf(stderr, "Couldn't seek to texture %s in a WAD file.\n", name);
		return NULL;
	}

	if (fread(bs2pc_wadLumpBuffer, lumpInfo->disksize, 1, wad->file) == 0) {
		fprintf(stderr, "Couldn't read texture %s from a WAD file.\n", name);
		return NULL;
	}

	return bs2pc_wadLumpBuffer;
}