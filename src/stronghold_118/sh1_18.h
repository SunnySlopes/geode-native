/*
 * sh1_18.h — stronghold End portal + layout simulation for Minecraft 1.18+.
 *
 * This module is embedded in the geode submodule and is intentionally
 * self-contained: geode's cubiomes snapshot ships only the biomes/noise half
 * (no finders.c, no features/), so everything stronghold-specific lives here
 * rather than in cubiomes. It needs nothing from cubiomes beyond the biome
 * generator that geode already links.
 *
 * Scope: 1.18 through the newest version geode knows. Older versions are
 * rejected on purpose — geode's terrain model starts at 1.18, and this module
 * exists to serve that model.
 *
 * Coordinates
 * -----------
 * X/Z is exact block coordinates: the layout code is a faithful port of
 * vanilla's StrongholdPieces, and the concentric-ring placement matches
 * vanilla 1.18+ (which relocates each stronghold to the nearest valid biome
 * within 112 blocks).
 *
 * There is no world Y. The layout is anchored at a nominal y = SH118_NOMINAL_Y
 * (64) and the vertical placement vanilla applies afterwards is not modelled,
 * so every Y field is named `nominal*`. Vertical *deltas* between pieces are
 * exact. World Y = nominalY + (realStartY - SH118_NOMINAL_Y).
 */

#ifndef SH118_H_
#define SH118_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Vanilla caps strongholds at 128 per world. */
#define SH118_MAX_STRONGHOLDS 128

/** Capacity of the piece buffer passed to the layout generator. */
#define SH118_PIECE_CAPACITY 400

/** End eye frames in a portal room. */
#define SH118_EYE_COUNT 12

/** Nominal Y the layout anchors the start piece at. */
#define SH118_NOMINAL_Y 64

/** Piece type ids (mirror of vanilla's StrongholdPieceType order). */
enum
{
    SH118_STRAIGHT = 0,
    SH118_PRISON_HALL,
    SH118_LEFT_TURN,
    SH118_RIGHT_TURN,
    SH118_ROOM_CROSSING,
    SH118_STRAIGHT_STAIRS_DOWN,
    SH118_STAIRS_DOWN,
    SH118_FIVE_CROSSING,
    SH118_CHEST_CORRIDOR,
    SH118_LIBRARY,
    SH118_PORTAL_ROOM,
    SH118_FILLER_CORRIDOR,
    SH118_TYPE_COUNT
};

/** One generated piece, in world-space block coordinates (X/Z exact). */
typedef struct
{
    int type;         /* SH118_* */
    const char *code; /* short name, e.g. "SHPR" */
    int rot;          /* 0..3 => 0 / 90 / 180 / 270 degrees */
    int depth;

    int minX, maxX; /* inclusive, exact */
    int minZ, maxZ;

    int nominalMinY, nominalMaxY; /* relative to SH118_NOMINAL_Y */
    int height;

    int chestCount;
    int chestX[2];
    int chestZ[2];
} Sh118Piece;

/** Portal + layout summary for one stronghold. */
typedef struct
{
    int mc;
    uint64_t seed;

    /* the stronghold start (vanilla puts the staircase at (4,4) in its chunk) */
    int startX, startZ;
    int chunkX, chunkZ;

    int ok;        /* 1 when the portal room was generated */
    int truncated; /* 1 when the piece cap was hit first */
    int pieces;
    int portalRoomIndex;

    /* portal room bounds (Y nominal) */
    int roomMinX, roomMinZ, roomMaxX, roomMaxZ;
    int roomNominalMinY, roomNominalMaxY;

    /* the 3x3 End portal: inclusive bounds and its centre */
    int frameMinX, frameMinZ, frameMaxX, frameMaxZ;
    int portalX, portalZ;

    /* 眼睛已生成数量 (0..12) */
    int eyes;
    unsigned eyeMask;

    /* --- 世界 Y (精确) ----------------------------------------------------
     *
     * 原版 StrongholdStructure.generatePieces 在布局后无条件调用
     * builder.moveBelowSeaLevel(seaLevel=63, minY=-64, random, 10),
     * 这与地牢 / 矿井 normal 类型是同一路径, 不依赖地形高度图,
     * 因此**要塞的世界 Y 是可以精确算出的**。
     *
     * 布局完成后所有 piece 的相对 Y 会被加上 dy, 于是 pieces[] 里的 Y
     * 就是真实世界 Y。下面三个字段记录这一步的结果:
     *
     *   dy         施加的竖直偏移 (y1Pos - layoutMaxY)
     *   dyExact    恒为 1 (保留字段, 便于调用方断言)
     *   layout*    施加 dy 之前的相对范围 (起点件恒为 y=64)
     */
    int32_t dy;
    int dyExact;
    int32_t layoutMinY, layoutMaxY, layoutYSpan;

    /* whole-stronghold planar coverage (true rectangle union) */
    int footprintMinX, footprintMinZ, footprintMaxX, footprintMaxZ;
    int footprintWidth, footprintDepth;
    int footprintArea;

    int typeCounts[SH118_TYPE_COUNT];
} Sh118Stronghold;

/* ------------------------------------------------------------------------- */
/* Version support                                                           */
/* ------------------------------------------------------------------------- */

/** Lowest supported version id (1.18). */
int sh118_minVersion(void);

/** 1 when `mc` is >= 1.18 and <= the newest this build knows. */
int sh118_versionSupported(int mc);

/** "1.18.2", "1.21.6", ... or "?" when unknown. */
const char *sh118_versionName(int mc);

/** English name for a piece type. */
const char *sh118_pieceTypeName(int type);

/** Short code for a piece type ("SHPR"). */
const char *sh118_pieceTypeCode(int type);

/* ------------------------------------------------------------------------- */
/* Simulation                                                                */
/* ------------------------------------------------------------------------- */

/**
 * Compute one stronghold from its start chunk.
 *
 * `pieces` may be NULL to skip the piece export; `maxPieces` then limits only
 * the copy-out (generation always uses the full capacity, because the layout
 * is sensitive to that cap). `maxPieces` caps how many pieces are copied out.
 *
 * Returns the number of pieces generated, or 0 on failure/unsupported version.
 */
int sh118_compute(Sh118Stronghold *out, Sh118Piece *pieces, int maxPieces,
                  int mc, uint64_t seed, int chunkX, int chunkZ);

/**
 * Enumerate strongholds for a seed and compute each portal.
 *
 * `gen` is the biome generator geode already created (must be seeded for the
 * overworld with the same `seed`); it is used to relocate each stronghold to a
 * valid biome exactly as vanilla does. Pass NULL to have the module build and
 * discard its own generator (slower, but no setup needed).
 *
 * Returns the number written to `out` (<= SH118_MAX_STRONGHOLDS).
 */
int sh118_locateAll(Sh118Stronghold *out, int maxCount, int mc, uint64_t seed, void *gen);

/** Portal of the stronghold nearest to the origin. Returns 1 on success. */
int sh118_locateFirst(Sh118Stronghold *out, int mc, uint64_t seed, void *gen);

/**
 * 延迟重定位的位置枚举。
 *
 * 128 次 112 格生物群系重定位是整套要塞模型里最贵的一步（实测约 1.0 s），
 * 而布局本身只要约 46 ms。调用方通常只关心搜索矩形附近的要塞，因此把
 * "环几何"与"重定位"拆开：
 *
 *   sh118_listApprox()      只算环几何。1.19.3+ 顺带取出该要塞独立的
 *                           重定位种子，**不**做生物群系搜索。
 *   sh118_relocateApprox()  需要时再补做重定位。
 *
 * 1.19.2 及更早的重定位直接消耗迭代器随机流，无法延后，因此那一路
 * listApprox 会就地完成重定位并置 relocated=1。
 */
typedef struct
{
    int x, z;      /* 起点（区块对齐前的近似值；已重定位时即最终值） */
    uint64_t lbr;  /* 该要塞独立的重定位随机种子（仅 1.19.3+ 有意义） */
    int relocated;
} Sh118Approx;

/** 只枚举环几何，不重定位。返回写出的要塞数（<= 128）。 */
int sh118_listApprox(Sh118Approx *out, int maxCount, int mc, uint64_t seed);

/**
 * 对 listApprox 产出的条目补做生物群系重定位（幂等）。
 * 返回 1 表示该条目已处于"已重定位"状态。
 */
int sh118_relocateApprox(Sh118Approx *p, int mc, uint64_t seed, void *gen);

/**
 * Exact union area of axis-aligned inclusive X/Z rectangles.
 *
 * `rects` holds 4 ints per rectangle (minX, minZ, maxX, maxZ); `scratch` needs
 * 4 * count ints. Returns the covered block count.
 */
long long sh118_unionArea(const int *rects, int count, int *scratch);

#ifdef __cplusplus
}
#endif

#endif /* SH118_H_ */
