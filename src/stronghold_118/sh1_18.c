/*
 * sh1_18.c — stronghold End portal + layout simulation for Minecraft 1.18+.
 *
 * Two halves:
 *
 *   vendor/stronghold_pieces.c  the authoritative StrongholdPieces layout
 *                               generator, vendored unmodified from cubiomes
 *                               (vanilla-faithful, per-version salt included)
 *   this file                   placement (concentric rings + the 112-block
 *                               biome relocation), portal-room geometry, eye
 *                               bits, and the planar footprint
 *
 * Self-contained by necessity: geode's cubiomes snapshot ships only the
 * biomes/noise half (no finders.c, no features/), so the stronghold-specific
 * code lives here instead of in cubiomes. The only cubiomes service used is the
 * biome generator, which geode already links, plus rng.h's static inline helpers.
 *
 * Scope is 1.18+: geode's terrain model starts there, and the 1.18+ placement
 * only needs the modern (xoroshiro) code paths.
 */

#include "sh1_18.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "biomes.h"
#include "generator.h"
#include "rng.h"
#include "sh_types.h"
#include "vendor/stronghold_pieces.h"

/* ------------------------------------------------------------------------- */
/* Names                                                                     */
/* ------------------------------------------------------------------------- */

/* geode's cubiomes snapshot has no util.h, so the version names this module
 * needs live here. Only the 1.18+ range matters. */
static const char *versionName(int mc)
{
    switch (mc)
    {
    case MC_1_18_2:  return "1.18";
    case MC_1_19_2:  return "1.19.2";
    case MC_1_19_4:  return "1.19";
    case MC_1_20_6:  return "1.20";
    case MC_1_21_1:  return "1.21.1";
    case MC_1_21_3:  return "1.21.3";
    case MC_1_21_4:  return "1.21.4";
    case MC_1_21_5:  return "1.21.5";
    case MC_1_21_6:  return "1.21.6";
    case MC_1_21_9:  return "1.21.9";
    case MC_1_21_11: return "1.21.11";
    case MC_26_1:    return "26.1";
    case MC_26_2:    return "26.2";
    case MC_26_3:    return "26.3";
    default:         return "?";
    }
}

/* ------------------------------------------------------------------------- */
/* Names                                                                     */
/* ------------------------------------------------------------------------- */

static const char *const PIECE_NAMES[SH118_TYPE_COUNT] = {
    "Straight corridor", "Prison hall", "Left turn", "Right turn",
    "Room crossing", "Straight stairs down", "Stairs down", "Five-way crossing",
    "Chest corridor", "Library", "Portal room", "Filler corridor",
};

static const char *const PIECE_CODES[SH118_TYPE_COUNT] = {
    "SHS", "SHPH", "SHLT", "SHRT", "SHRC", "SHSSD", "SHSD", "SH5C", "SHCC",
    "SHLi", "SHPR", "SHFC",
};

int sh118_minVersion(void)
{
    return MC_1_18;
}

int sh118_versionSupported(int mc)
{
    return mc >= MC_1_18 && mc <= MC_NEWEST;
}

const char *sh118_versionName(int mc)
{
    const char *name = versionName(mc);
    return (name && name[0] != '?') ? name : "?";
}

const char *sh118_pieceTypeName(int type)
{
    if (type < 0 || type >= SH118_TYPE_COUNT) return "Unknown";
    return PIECE_NAMES[type];
}

const char *sh118_pieceTypeCode(int type)
{
    if (type < 0 || type >= SH118_TYPE_COUNT) return "????";
    return PIECE_CODES[type];
}

/* ------------------------------------------------------------------------- */
/* Rectangle union                                                          */
/* ------------------------------------------------------------------------- */

static int cmpIntAsc(const void *a, const void *b)
{
    const int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

long long sh118_unionArea(const int *rects, int count, int *scratch)
{
    if (!rects || !scratch || count <= 0) return 0;

    int *xs = scratch;
    int nx = 0;
    for (int i = 0; i < count; i++)
    {
        const int minX = rects[4 * i + 0], maxX = rects[4 * i + 2];
        if (minX > maxX) continue;
        xs[nx++] = minX;
        xs[nx++] = maxX + 1; /* half-open upper edge */
    }
    if (nx == 0) return 0;
    qsort(xs, (size_t)nx, sizeof(int), cmpIntAsc);

    int uniq = 0;
    for (int i = 0; i < nx; i++)
        if (i == 0 || xs[i] != xs[i - 1]) xs[uniq++] = xs[i];

    int *zspan = scratch + 2 * count;
    long long total = 0;
    for (int k = 0; k + 1 < uniq; k++)
    {
        const int x0 = xs[k], x1 = xs[k + 1];
        if (x0 >= x1) continue;

        int nz = 0;
        for (int i = 0; i < count; i++)
        {
            const int minX = rects[4 * i + 0], maxX = rects[4 * i + 2];
            if (minX > maxX) continue;
            if (minX > x0 || maxX < x1 - 1) continue; /* must span the slab */
            zspan[nz++] = rects[4 * i + 1];
            zspan[nz++] = rects[4 * i + 3];
        }
        if (nz == 0) continue;

        for (int i = 0; i + 1 < nz; i += 2)
            for (int j = i + 2; j + 1 < nz; j += 2)
                if (zspan[j] < zspan[i])
                {
                    const int tm = zspan[i], tx = zspan[i + 1];
                    zspan[i] = zspan[j];
                    zspan[i + 1] = zspan[j + 1];
                    zspan[j] = tm;
                    zspan[j + 1] = tx;
                }

        long long covered = 0;
        int curMin = 0, curMax = 0, started = 0;
        for (int i = 0; i + 1 < nz; i += 2)
        {
            const int a = zspan[i], b = zspan[i + 1];
            if (a > b) continue;
            if (!started) { curMin = a; curMax = b; started = 1; }
            else if (a <= curMax + 1) { if (b > curMax) curMax = b; }
            else { covered += (long long)curMax - curMin + 1; curMin = a; curMax = b; }
        }
        if (started) covered += (long long)curMax - curMin + 1;
        total += covered * (long long)(x1 - x0);
    }
    return total;
}

/* ------------------------------------------------------------------------- */
/* Stronghold salt                                                          */
/* ------------------------------------------------------------------------- */

/* Per-version salt for Stronghold, matching cubiomes' getStructureSaltConfig:
 *   1.16.1 - 1.19.1  {5,  0}
 *   1.19.2 - 1.19.3  {4,  8}
 *   1.19.4 - 26.2    {4, 19}
 *   26.3+            {4, 37}
 * 1.18 belongs to the first family. */
static void strongholdSalt(int mc, StructureSaltConfig *out)
{
    if (mc < MC_1_19_2)      { out->generationStep = 5; out->decoratorIndex = 0;  }
    else if (mc < MC_1_19_4) { out->generationStep = 4; out->decoratorIndex = 8;  }
    else if (mc < MC_26_3)   { out->generationStep = 4; out->decoratorIndex = 19; }
    else                     { out->generationStep = 4; out->decoratorIndex = 37; }
}

/* Map a portal-room-local (x, z) into world coordinates for a piece rotation. */
static void rotPos(const Piece *p, int lx, int lz, int *wx, int *wz)
{
    switch (p->rot)
    {
    case 0: *wx = p->bb0.x + lx; *wz = p->bb1.z - lz; break;
    case 1: *wx = p->bb0.x + lz; *wz = p->bb0.z + lx; break;
    case 2: *wx = p->bb0.x + lx; *wz = p->bb0.z + lz; break;
    default: *wx = p->bb1.x - lz; *wz = p->bb0.z + lx; break;
    }
}

/* ------------------------------------------------------------------------- */
/* Main entry point                                                         */
/* ------------------------------------------------------------------------- */

int sh118_compute(Sh118Stronghold *out, Sh118Piece *pieces, int maxPieces,
                  int mc, uint64_t seed, int chunkX, int chunkZ)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->mc = mc;
    out->seed = seed;
    out->chunkX = chunkX;
    out->chunkZ = chunkZ;
    out->portalRoomIndex = -1;
    out->startX = (chunkX << 4) + 4;
    out->startZ = (chunkZ << 4) + 4;

    if (!sh118_versionSupported(mc)) return 0;

    if (maxPieces < 0) maxPieces = 0;
    if (maxPieces > SH118_PIECE_CAPACITY) maxPieces = SH118_PIECE_CAPACITY;

    StructureSaltConfig salt;
    strongholdSalt(mc, &salt);

    /* Generation always uses the full capacity: the layout is sensitive to the
     * buffer size, so a smaller caller request only limits the copy-out. */
    static Piece list[SH118_PIECE_CAPACITY];
    uint64_t layoutRng = 0;

    /* 眼睛: 直接复用 cubiomes 的 getStrongholdLoot —— 它已把 12 个眼框的
     * roll 按 per-chunk 重播并合并好。自己重算需要精确复现它对区块范围与
     * 眼位归属的处理, 没必要。 */
    const int count = getStrongholdLoot(list, SH118_PIECE_CAPACITY, salt, mc, seed,
                                        chunkX, chunkZ);
    if (count <= 0) return 0;

    /* 精确 Y 需要布局结束时的 RNG 状态。用一块独立缓冲区跑 Ex 版本单独取状态,
     * 避免覆盖上面的 list(room 指针依赖它)。布局是 (seed, chunk) 的纯函数,
     * 两次结果必然相同; 若 piece 数不一致则放弃精确 Y。 */
    {
        static Piece tmp[SH118_PIECE_CAPACITY];
        if (getStrongholdPiecesEx(tmp, SH118_PIECE_CAPACITY, mc, seed, chunkX, chunkZ,
                                  &layoutRng) != count)
            layoutRng = 0;
    }

    /* --- 精确 Y: 复现原版 moveBelowSeaLevel -------------------------------
     *
     * 依据原版 StrongholdStructure.generatePieces (1.21.11 / 26.1.2 / 26.2 /
     * 26.3 四个版本逐字节相同, 第 42 行):
     *
     *     builder.moveBelowSeaLevel(seaLevel, minY, random, 10);
     *
     * 该调用在每轮 do-while 里**无条件执行**, 且发生在 portalRoomPiece 检查
     * 之前。因此要塞的竖直位置**可以精确算出** —— 它和地牢 / 矿井 normal 类型
     * 走同一条 moveBelowSeaLevel 路径, 不依赖任何地形高度图。
     *
     * cubiomes 的 features/stronghold.c 把它写成
     *     if (mc <= MC_1_12_2 && !env.portal)
     * 只在 1.12 及更早、且尚未找到传送门房时执行 —— 对 1.13+ 是错的。
     * 这正是"cubiomes 给不出要塞 Y"这一看法的来源。
     *
     * moveBelowSeaLevel 的实现 (StructurePiecesBuilder:29-40):
     *     maxY  = seaLevel - offset        // offset = 10
     *     y1Pos = box.getYSpan() + minY + 1
     *     if (y1Pos < maxY) y1Pos += random.nextInt(maxY - y1Pos)
     *     dy    = y1Pos - box.maxY()
     *     offsetPiecesVertically(dy)
     *
     * RNG: 这次 nextInt 发生在布局完全生成之后, 取自布局 RNG 的**下一个**
     * word。getStrongholdPiecesEx 已把该状态交回(layoutRng), 所以能精确复现。
     * ------------------------------------------------------------------- */
    {
        int lminY = list[0].bb0.y;
        int lmaxY = list[0].bb1.y;
        for (int i = 1; i < count; i++)
        {
            if (list[i].bb0.y < lminY) lminY = list[i].bb0.y;
            if (list[i].bb1.y > lmaxY) lmaxY = list[i].bb1.y;
        }

        const int seaLevel = 63;   /* chunkGenerator.getSeaLevel() */
        const int worldMinY = -64; /* chunkGenerator.getMinY() (1.18+) */
        const int offset = 10;
        const int ySpan = lmaxY - lminY + 1;
        const int maxAllowed = seaLevel - offset;

        int y1Pos = ySpan + worldMinY + 1;
        if (y1Pos < maxAllowed)
        {
            /* 等价于 java.util.Random.nextInt(maxAllowed - y1Pos)。
             * LegacyRandomSource 是 48-bit LCG, next(31) 取高 31 位。 */
            const int bound = maxAllowed - y1Pos;
            uint64_t r = layoutRng;
            int32_t sample, modulo;
            if (bound > 0)
            {
                do {
                    r = (r * 25214903917ULL + 11ULL) & ((1ULL << 48) - 1ULL);
                    sample = (int32_t)(uint32_t)(r >> 17); /* next(31) */
                    modulo = sample % bound;
                } while ((int64_t)sample - (int64_t)modulo + (int64_t)(bound - 1) < 0);
                y1Pos += modulo;
            }
        }

        out->dy = y1Pos - lmaxY;
        out->dyExact = 1;
        out->layoutMinY = lminY;
        out->layoutMaxY = lmaxY;
        out->layoutYSpan = ySpan;

        /* 相对 Y -> 真实世界 Y */
        for (int i = 0; i < count; i++)
        {
            list[i].bb0.y += out->dy;
            list[i].bb1.y += out->dy;
        }
    }

    /* Locate the portal room. */
    const Piece *room = NULL;
    int roomIndex = -1;
    for (int i = 0; i < count; i++)
        if (list[i].type == SH118_PORTAL_ROOM) { room = &list[i]; roomIndex = i; break; }
    /* Never derive coordinates from a truncated layout. */
    if (!room) return count;

    out->pieces = count;
    out->truncated = (count >= SH118_PIECE_CAPACITY) ? 1 : 0;

    out->ok = 1;
    out->truncated = 0;
    out->portalRoomIndex = roomIndex;

    out->roomMinX = room->bb0.x;
    out->roomMinZ = room->bb0.z;
    out->roomMaxX = room->bb1.x;
    out->roomMaxZ = room->bb1.z;
    out->roomNominalMinY = room->bb0.y;
    out->roomNominalMaxY = room->bb1.y;

    /* The 3x3 end_portal area: local (4,9)..(6,11), centre (5,10). */
    {
        int ax, az, bx, bz, cx, cz;
        rotPos(room, 4, 9, &ax, &az);
        rotPos(room, 6, 11, &bx, &bz);
        rotPos(room, 5, 10, &cx, &cz);
        out->frameMinX = ax < bx ? ax : bx;
        out->frameMaxX = ax < bx ? bx : ax;
        out->frameMinZ = az < bz ? az : bz;
        out->frameMaxZ = az < bz ? bz : az;
        out->portalX = cx;
        out->portalZ = cz;
    }

    /* 眼睛: getStrongholdLoot 已按 per-chunk 重播合并好, 直接取位数即可。 */
    out->eyeMask = (unsigned)room->additionalData & 0xFFFu;
    out->eyes = 0;
    for (int j = 0; j < SH118_EYE_COUNT; j++)
        if (out->eyeMask & (1u << j)) out->eyes++;

    /* Planar footprint over every piece. */
    {
        int minX = list[0].bb0.x, maxX = list[0].bb1.x;
        int minZ = list[0].bb0.z, maxZ = list[0].bb1.z;
        for (int i = 0; i < count; i++)
        {
            if (list[i].bb0.x < minX) minX = list[i].bb0.x;
            if (list[i].bb1.x > maxX) maxX = list[i].bb1.x;
            if (list[i].bb0.z < minZ) minZ = list[i].bb0.z;
            if (list[i].bb1.z > maxZ) maxZ = list[i].bb1.z;
            if (list[i].type >= 0 && list[i].type < SH118_TYPE_COUNT)
                out->typeCounts[list[i].type]++;
        }
        out->footprintMinX = minX;
        out->footprintMaxX = maxX;
        out->footprintMinZ = minZ;
        out->footprintMaxZ = maxZ;
        out->footprintWidth = maxX - minX + 1;
        out->footprintDepth = maxZ - minZ + 1;

        int rects[4 * SH118_PIECE_CAPACITY];
        int scratch[4 * SH118_PIECE_CAPACITY];
        for (int i = 0; i < count; i++)
        {
            rects[4 * i + 0] = list[i].bb0.x;
            rects[4 * i + 1] = list[i].bb0.z;
            rects[4 * i + 2] = list[i].bb1.x;
            rects[4 * i + 3] = list[i].bb1.z;
        }
        out->footprintArea = (int)sh118_unionArea(rects, count, scratch);
    }

    /* Optional piece export. */
    if (pieces && maxPieces > 0)
    {
        const int exportCount = count < maxPieces ? count : maxPieces;
        for (int i = 0; i < exportCount; i++)
        {
            Sh118Piece *dst = &pieces[i];
            memset(dst, 0, sizeof(*dst));
            dst->type = list[i].type;
            dst->code = sh118_pieceTypeCode(list[i].type);
            dst->rot = list[i].rot;
            dst->depth = list[i].depth;
            dst->minX = list[i].bb0.x;
            dst->maxX = list[i].bb1.x;
            dst->minZ = list[i].bb0.z;
            dst->maxZ = list[i].bb1.z;
            dst->nominalMinY = list[i].bb0.y;
            dst->nominalMaxY = list[i].bb1.y;
            dst->height = list[i].bb1.y - list[i].bb0.y + 1;
            int chests = list[i].chestCount;
            if (chests < 0) chests = 0;
            if (chests > 2) chests = 2;
            dst->chestCount = chests;
            for (int c = 0; c < chests; c++)
            {
                dst->chestX[c] = list[i].chestPoses[c].x;
                dst->chestZ[c] = list[i].chestPoses[c].z;
            }
        }
    }

    return count;
}

/* ------------------------------------------------------------------------- */
/* Placement                                                                */
/* ------------------------------------------------------------------------- */

/* Whether a biome may host a stronghold (vanilla isStrongholdBiome, 1.18+). */
static int biomeAllowed(int mc, int id)
{
    if (!isOverworld(mc, id)) return 0;
    if (isOceanic(id)) return 0;
    switch (id)
    {
    case plains:
    case mushroom_fields:
    case taiga_hills:
        return mc >= MC_1_7;
    case swamp:
        // cubiomes isStrongholdBiome: swamp 只在 1.6 及以前可承载要塞。
        // 这一行曾被移植时遗漏，导致 1.18+ 把 swamp 当成合法目标，
        // 使重定位停在原版不会选的位置（约影响 2% 的要塞）。
        return mc <= MC_1_6;
    case river:
    case frozen_river:
    case beach:
    case snowy_beach:
    case swamp_hills:
        return 0;
    case mushroom_field_shore:
        return mc >= MC_1_13;
    case stone_shore:
        return mc <= MC_1_17;
    case bamboo_jungle:
    case bamboo_jungle_hills:
        return mc <= MC_1_15 || mc >= MC_1_18;
    case cherry_grove:
        return mc >= MC_1_21_9;
    case mangrove_swamp:
    case deep_dark:
    case dappled_forest:
        return 0;
    default:
        return 1;
    }
}

static int idMatches(int id, uint64_t validB, uint64_t validM)
{
    const uint64_t mask = 1ULL << (id & 63);
    return (id & 128) ? (validM & mask) != 0 : (validB & mask) != 0;
}

/* Relocate to a valid biome within `radius`, exactly like vanilla: collect
 * candidates in 4-block steps, pick one by reservoir sampling.
 *
 * `rng` is a Java LCG seed word (cubiomes passes uint64_t* here and uses
 * nextInt, not the xoroshiro helpers) — the reservoir roll must consume the
 * same stream vanilla does. */
static void relocateBiome(const Generator *g, int *px, int *pz, int radius, uint64_t *rng)
{
    const int mc = g->mc;
    uint64_t validB = 0, validM = 0;
    for (int i = 0; i < 64; i++)
    {
        if (biomeAllowed(mc, i)) validB |= 1ULL << i;
        if (biomeAllowed(mc, i + 128)) validM |= 1ULL << i;
    }

    int x = *px >> 2, z = *pz >> 2;
    const int r = radius >> 2;
    int found = 0, bx = x, bz = z;
    uint64_t dat = 0;

    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
        {
            const int id = sampleBiomeNoise(&g->bn, NULL, x + i, 0, z + j, &dat, 0);
            if (!idMatches(id, validB, validM)) continue;
            if (found == 0 || nextInt(rng, found + 1) == 0)
            {
                bx = x + i;
                bz = z + j;
            }
            found++;
        }

    *px = bx * 4;
    *pz = bz * 4;
}

/* The concentric-ring stronghold iterator.
 *
 * The whole iterator (ring geometry *and* the relocation reservoir roll) runs
 * on one Java LCG seed word, exactly as vanilla and cubiomes do; `rnds` is that
 * word. Do not mix in xoroshiro here. */
typedef struct
{
    int index, ringnum, ringmax, ringidx;
    double angle, dist;
    uint64_t rnds; /* Java LCG seed word (48-bit) */
    int approxX, approxZ;
} RingIter;

static int roundHalfUp(double v)
{
    return (int)(v + (v < 0 ? -0.5 : 0.5));
}

static void ringInit(RingIter *it, uint64_t seed)
{
    const double PI = 3.14159265358979323846;
    setSeed(&it->rnds, seed);
    const double angle = 2.0 * PI * nextDouble(&it->rnds);
    const double dist = (4.0 * 32.0) + (nextDouble(&it->rnds) - 0.5) * 32 * 2.5;

    it->index = 0;
    it->ringnum = 0;
    it->ringmax = 3;
    it->ringidx = 0;
    it->angle = angle;
    it->dist = dist;
    it->approxX = roundHalfUp(cos(angle) * dist) * 16 + 8;
    it->approxZ = roundHalfUp(sin(angle) * dist) * 16 + 8;
}

/* Produce the next stronghold start position. Returns 0 when exhausted. */
/* 推进环几何，但**不**做生物群系重定位。
 *
 * modern 路径（1.19.3+）的重定位只依赖每座要塞独立派生的随机种子，
 * 不消耗迭代器流，因此可以延后到"确实需要"时再算；
 * legacy 路径的重定位直接消耗 it->rnds，必须就地完成。
 */
static int ringNextGeom(RingIter *it, const Generator *g, uint64_t *outLbr,
                        int *approxX, int *approxZ)
{
    const double PI = 3.14159265358979323846;

    if (g->mc > MC_1_19_2)
    {
        /* 1.19.3+: the relocation roll uses its own LCG word derived from the
         * iterator's, so the ring geometry stream is left untouched. */
        setSeed(outLbr, nextLong(&it->rnds));
    }
    else
    {
        /* Older path: the roll advances the iterator word itself. */
        *outLbr = 0;
        relocateBiome(g, &it->approxX, &it->approxZ, 112, &it->rnds);
    }

    *approxX = it->approxX;
    *approxZ = it->approxZ;

    it->ringidx++;
    it->angle += 2 * PI / it->ringmax;

    if (it->ringidx == it->ringmax)
    {
        it->ringnum++;
        it->ringidx = 0;
        it->ringmax = it->ringmax + 2 * it->ringmax / (it->ringnum + 1);
        if (it->ringmax > 128 - it->index) it->ringmax = 128 - it->index;
        it->angle += nextDouble(&it->rnds) * PI * 2.0;
    }

    it->dist = (4.0 * 32.0) + (6.0 * it->ringnum * 32.0) +
               (nextDouble(&it->rnds) - 0.5) * 32 * 2.5;
    it->approxX = roundHalfUp(cos(it->angle) * it->dist) * 16 + 8;
    it->approxZ = roundHalfUp(sin(it->angle) * it->dist) * 16 + 8;

    it->index++;
    return 128 - (it->index - 1) > 0;
}

/* 几何 + 重定位 + 区块对齐，一次给出最终起点。 */
static int ringNext(RingIter *it, const Generator *g, int *outX, int *outZ)
{
    uint64_t lbr = 0;
    int ax = 0, az = 0;

    if (!ringNextGeom(it, g, &lbr, &ax, &az))
        return 0;

    if (g->mc > MC_1_19_2)
    {
        int x = ax, z = az;
        relocateBiome(g, &x, &z, 112, &lbr);
        ax = x;
        az = z;
    }

    /* Vanilla puts the staircase at (4,4) in the chunk. */
    *outX = (ax & ~15) + 4;
    *outZ = (az & ~15) + 4;
    return 1;
}

int sh118_locateAll(Sh118Stronghold *out, int maxCount, int mc, uint64_t seed, void *gen)
{
    if (!out || maxCount <= 0) return 0;
    if (!sh118_versionSupported(mc)) return 0;
    if (maxCount > SH118_MAX_STRONGHOLDS) maxCount = SH118_MAX_STRONGHOLDS;

    Generator local;
    const Generator *g = (const Generator *)gen;
    if (!g)
    {
        setupGenerator(&local, mc, 0);
        applySeed(&local, DIM_OVERWORLD, seed);
        g = &local;
    }

    RingIter it;
    ringInit(&it, seed);

    int written = 0;
    for (int i = 0; i < maxCount; i++)
    {
        int sx, sz;
        if (!ringNext(&it, g, &sx, &sz)) break;
        sh118_compute(&out[written], NULL, 0, mc, seed, sx >> 4, sz >> 4);
        out[written].startX = sx;
        out[written].startZ = sz;
        written++;
    }
    return written;
}

int sh118_locateFirst(Sh118Stronghold *out, int mc, uint64_t seed, void *gen)
{
    if (!out) return 0;
    if (!sh118_versionSupported(mc)) return 0;

    Generator local;
    const Generator *g = (const Generator *)gen;
    if (!g)
    {
        setupGenerator(&local, mc, 0);
        applySeed(&local, DIM_OVERWORLD, seed);
        g = &local;
    }

    RingIter it;
    ringInit(&it, seed);
    int sx, sz;
    if (!ringNext(&it, g, &sx, &sz)) return 0;

    sh118_compute(out, NULL, 0, mc, seed, sx >> 4, sz >> 4);
    out->startX = sx;
    out->startZ = sz;
    return out->ok;
}

int sh118_listApprox(Sh118Approx *out, int maxCount, int mc, uint64_t seed)
{
    if (!out || maxCount <= 0) return 0;
    if (!sh118_versionSupported(mc)) return 0;
    if (maxCount > SH118_MAX_STRONGHOLDS) maxCount = SH118_MAX_STRONGHOLDS;

    Generator local;
    setupGenerator(&local, mc, 0);
    applySeed(&local, DIM_OVERWORLD, seed);

    RingIter it;
    ringInit(&it, seed);

    int n = 0;
    for (int i = 0; i < maxCount; i++)
    {
        uint64_t lbr = 0;
        int ax = 0, az = 0;

        if (!ringNextGeom(&it, &local, &lbr, &ax, &az))
            break;

        out[n].lbr = lbr;
        out[n].x = ax;
        out[n].z = az;
        /* legacy 路径的重定位在 ringNextGeom 里已经就地完成。 */
        out[n].relocated = (mc > MC_1_19_2) ? 0 : 1;
        n++;
    }
    return n;
}

int sh118_relocateApprox(Sh118Approx *p, int mc, uint64_t seed, void *gen)
{
    if (!p) return 0;
    if (p->relocated) return 1;
    if (mc <= MC_1_19_2) return 0; /* legacy 必须就地完成，不能延后 */

    Generator local;
    const Generator *g = (const Generator *)gen;
    if (!g)
    {
        setupGenerator(&local, mc, 0);
        applySeed(&local, DIM_OVERWORLD, seed);
        g = &local;
    }

    uint64_t lbr = p->lbr;
    int x = p->x, z = p->z;
    relocateBiome(g, &x, &z, 112, &lbr);
    p->x = x;
    p->z = z;
    p->relocated = 1;
    return 1;
}
