/*
 * sh_types.h — the minimal cubiomes surface the vendored stronghold layout
 * generator needs.
 *
 * geode's cubiomes snapshot ships only the biomes/noise half: there is no
 * finders.h, and no features/. The vendored generator
 * (vendor/stronghold_pieces.c) is the authoritative implementation — a faithful
 * port of vanilla's StrongholdPieces — so rather than reimplementing the layout
 * we vendor it and supply just these declarations.
 *
 * Everything here is either a verbatim copy of the corresponding cubiomes
 * declaration or a thin forwarding macro, so the vendored code needs no edits
 * beyond its own #include lines.
 *
 * NOTE: rng.h already defines STRUCT(), RandomSource, the abs* helpers and the
 * JAVA_RANDOM/XOROSHIRO_J enum, so this header must not redefine them.
 */

#ifndef SH_TYPES_H_
#define SH_TYPES_H_

#include <stdint.h>

#include "biomes.h" /* MC_* version enum, biome ids (geode already links this) */
#include "rng.h"    /* RandomSource, abs* helpers, Xoroshiro, STRUCT, RNG enums */

#ifdef __cplusplus
extern "C" {
#endif

/* The vendored generator keeps cubiomes' older-version branches (they are dead
 * for a 1.18+ build but must still compile), and one of them references
 * chunkGenerateRnd(). Verbatim from cubiomes' finders.h. */
static inline uint64_t chunkGenerateRnd(uint64_t worldSeed, int chunkX, int chunkZ)
{
    uint64_t rnd;
    setSeed(&rnd, worldSeed);
    rnd = (nextLong(&rnd) * chunkX) ^ (nextLong(&rnd) * chunkZ) ^ worldSeed;
    setSeed(&rnd, rnd);
    return rnd;
}

typedef struct { int x, z; } Pos;
typedef struct { int x, y, z; } Pos3;

/* Verbatim from cubiomes' finders.h: orientBox() rotates a piece's local box
 * into world space. */
static inline void orientBox(Pos3 pos, Pos3 offset, Pos3 size, int facing, Pos3 *b0, Pos3 *b1)
{
    *b0 = pos;
    *b1 = pos;
    Pos3 d0 = offset, d1 = size;
    b0->y += d0.y;
    b1->y += d0.y + d1.y - 1;

    switch (facing)
    {
    case 0: /* north */
        b0->x += d0.x;            b0->z += d0.z - d1.z + 1;
        b1->x += d0.x + d1.x - 1; b1->z += d0.z;
        break;
    case 1: /* east */
        b0->x += d0.z;            b0->z += d0.x;
        b1->x += d0.z + d1.z - 1; b1->z += d0.x + d1.x - 1;
        break;
    case 2: /* south */
        b0->x += d0.x;            b0->z += d0.z;
        b1->x += d0.x + d1.x - 1; b1->z += d0.z + d1.z - 1;
        break;
    case 3: /* west */
        b0->x += d0.z - d1.z + 1; b0->z += d0.x;
        b1->x += d0.z;            b1->z += d0.x + d1.x - 1;
        break;
    }
}

static inline int hasIntersection(Pos3 bmin0, Pos3 bmax0, Pos3 bmin1, Pos3 bmax1)
{
    return bmax0.x >= bmin1.x && bmin0.x <= bmax1.x &&
           bmax0.z >= bmin1.z && bmin0.z <= bmax1.z &&
           bmax0.y >= bmin1.y && bmin0.y <= bmax1.y;
}

/* One generated structure piece. Field order and types match cubiomes'
 * finders.h Piece exactly; the vendored generator fills them. */
STRUCT(Piece)
{
    const char *name;
    Pos3 pos, bb0, bb1;
    uint8_t rot;
    int8_t depth;
    int8_t type;
    int chestCount;
    Pos chestPoses[4];
    uint64_t lootSeeds[4];
    const char *lootTables[4];
    int additionalData;
    Piece *next;
};

/* Matches cubiomes' StructureSaltConfig: the per-version RNG salts that loot
 * and eye rolls depend on. The values live in the driver (sh1_18.c), which
 * knows the version families. */
STRUCT(StructureSaltConfig)
{
    int32_t generationStep;
    int32_t decoratorIndex;
};

/* Population seed. cubiomes' version also handles pre-1.13; this module is
 * 1.18+ only, so the xoroshiro branch is the only path. */
static inline uint64_t sh_populationSeed(uint64_t ws, int x, int z)
{
    Xoroshiro xr;
    xSetSeed(&xr, ws);
    uint64_t a = xNextLongJ(&xr);
    uint64_t b = xNextLongJ(&xr);
    return ((uint64_t)x * (a | 1) + (uint64_t)z * (b | 1)) ^ ws;
}

/* The vendored code calls getPopulationSeed(mc, seed, x, z). */
#define getPopulationSeed(mc, ws, x, z) sh_populationSeed((ws), (x), (z))

#ifdef __cplusplus
}
#endif

#endif /* SH_TYPES_H_ */
