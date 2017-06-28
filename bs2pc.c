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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, const char * const *argv) {
	int argi;
	bool parsingGame = false;
	const char *sourceFileName = NULL, *targetFileName = NULL;
	unsigned char *sourceFile;
	long sourceFileSize;
	unsigned int beginning;
	size_t targetFileNameLength;
	char *targetFileNameBuffer;

	fputs("BS2PC build 3 - Half-Life PlayStation 2 map converter.\n", stderr);
	for (argi = 1; argi < argc; ++argi) {
		const char *arg = argv[argi];
		if (parsingGame) {
			BS2PC_AddWadDirectory(arg);
			parsingGame = false;
		} else {
			if (bs2pc_strcasecmp(arg, "-game") == 0) {
				parsingGame = true;
			} else if (targetFileName == NULL) {
				if (sourceFileName != NULL) {
					targetFileName = arg;
				} else {
					sourceFileName = arg;
				}
			}
		}
	}
	if (sourceFileName == NULL) {
		fputs("Usage: [-game \"path to base WAD directory for .bsp to .bs2\"] [-game \"path to mod WAD directory\"] \"source file name\" [\"target file name\"].\n", stderr);
		return EXIT_SUCCESS;
	}

	fputs("Loading the source file...\n", stderr);
	sourceFile = (unsigned char *) BS2PC_LoadFile(sourceFileName, &sourceFileSize);
	if (sourceFileSize <= sizeof(unsigned int)) {
		fputs("Source file size is invalid.\n", stderr);
		return EXIT_FAILURE;
	}

	beginning = *((const unsigned int *) sourceFile);

	if (beginning == BSPVERSION_ID) {
		void *targetFile;
		unsigned int targetFileSize;

		bs2pc_idMapSize = sourceFileSize;
		bs2pc_idMap = sourceFile;

		fputs("WARNING: .bsp to .bs2 is INCOMPLETE! Polygon subdivision is missing! DO NOT share any PS2 maps produced by this build!\n", stderr);
		BS2PC_ConvertIdToGbx();

		fputs("Compressing .bs2...\n", stderr);
		targetFile = BS2PC_CompressWithSize(bs2pc_gbxMap, bs2pc_gbxMapSize, &targetFileSize);

		fputs("Writing the .bs2 file...\n", stderr);
		if (targetFileName == NULL) {
			targetFileNameLength = strlen(sourceFileName);
			targetFileNameBuffer = bs2pc_alloca(targetFileNameLength + 5);
			strcpy(targetFileNameBuffer, sourceFileName);
			if (targetFileNameLength >= 4 && bs2pc_strcasecmp(targetFileNameBuffer + targetFileNameLength - 4, ".bsp") == 0) {
				targetFileNameBuffer[targetFileNameLength - 1] = '2';
			} else {
				strcpy(targetFileNameBuffer + targetFileNameLength, ".bs2");
			}
			targetFileName = targetFileNameBuffer;
		}
		BS2PC_WriteFile(targetFileName, targetFile, targetFileSize);

		fprintf(stderr, "%s converted to %s.\n", sourceFileName, targetFileName);
	} else {
		if (beginning != BSPVERSION_GBX) {
			fputs("Decompressing .bs2...\n", stderr);
			bs2pc_gbxMapSize = beginning;
			bs2pc_gbxMap = (unsigned char *) BS2PC_Alloc(bs2pc_gbxMapSize, false);
			BS2PC_Decompress(sourceFile + sizeof(unsigned int), sourceFileSize - sizeof(unsigned int), bs2pc_gbxMap, bs2pc_gbxMapSize);
			BS2PC_Free(sourceFile);
			sourceFile = NULL;
		} else {
			bs2pc_gbxMapSize = sourceFileSize;
			bs2pc_gbxMap = sourceFile;
		}

		BS2PC_ConvertGbxToId();

		fputs("Writing the .bsp file...\n", stderr);
		if (targetFileName == NULL) {
			targetFileNameLength = strlen(sourceFileName);
			targetFileNameBuffer = bs2pc_alloca(targetFileNameLength + 5);
			strcpy(targetFileNameBuffer, sourceFileName);
			if (targetFileNameLength >= 4 && bs2pc_strcasecmp(targetFileNameBuffer + targetFileNameLength - 4, ".bs2") == 0) {
				targetFileNameBuffer[targetFileNameLength - 1] = (targetFileNameBuffer[targetFileNameLength - 3] == 'B' ? 'P' : 'p');
			} else {
				strcpy(targetFileNameBuffer + targetFileNameLength, ".bsp");
			}
			targetFileName = targetFileNameBuffer;
		}
		BS2PC_WriteFile(targetFileName, bs2pc_idMap, bs2pc_idMapSize);

		fprintf(stderr, "%s converted to %s.\n", sourceFileName, targetFileName);
	}

	return EXIT_SUCCESS;
}