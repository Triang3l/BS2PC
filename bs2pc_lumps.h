#pragma once

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
	LUMP_GBX_HULL0, // Drawing hull as a clipping hull.
	LUMP_GBX_CLIPNODES,
	LUMP_GBX_MODELS,
	LUMP_GBX_FACES,
	LUMP_GBX_MARKSURFACES,
	LUMP_GBX_VISIBILITY,
	LUMP_GBX_LIGHTING,
	LUMP_GBX_TEXTURES,
	LUMP_GBX_ENTITIES,
	LUMP_GBX_POLYS,

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

typedef struct {
	unsigned int planenum;
	short children[2];
} dclipnode_t;

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
	float unknown3;
	short texturemins[2];
	short extents[2];
	unsigned char unknown4[28];
	unsigned char styles[MAX_LIGHTMAPS];
	unsigned char unknown5[24];
	bspoffset_t polys;
	unsigned char unknown6[12];
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
	unsigned char pad[4];
} dmodel_gbx_t;

#pragma pack(pop)