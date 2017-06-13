#pragma once
#include <stdbool.h>
#include <stdlib.h>

void *BS2PC_Alloc(size_t size, bool zeroed);
inline void BS2PC_Free(void *memory) { free(memory); }
void *BS2PC_LoadFile(const char *fileName, unsigned int *fileSize);
void BS2PC_WriteFile(const char *fileName, void *data, unsigned int size);

void BS2PC_Decompress(const void *source, unsigned int sourceSize, void *target, unsigned int targetSize);
