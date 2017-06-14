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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, const char **argv) {
	unsigned char *bs2File;
	long bs2FileSize;
	size_t fileNameLength;
	char *idFileName;

	fputs("BS2PC build 2 - Half-Life PlayStation 2 to PC map converter.\n", stderr);
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

	BS2PC_ConvertGbxToId();

	fputs("Writing the .bsp file...\n", stderr);
	fileNameLength = strlen(argv[1]);
	idFileName = bs2pc_alloca(fileNameLength + 5);
	strcpy(idFileName, argv[1]);
	if (fileNameLength >= 4 && bs2pc_strcasecmp(idFileName + fileNameLength - 4, ".bs2") == 0) {
		idFileName[fileNameLength - 1] = (idFileName[fileNameLength - 3] == 'B' ? 'P' : 'p');
	} else {
		strcpy(idFileName + fileNameLength, ".bsp");
	}
	BS2PC_WriteFile(idFileName, bs2pc_idMap, bs2pc_idMapSize);

	fprintf(stderr, "%s converted to %s.\n", argv[1], idFileName);

	return EXIT_SUCCESS;
}