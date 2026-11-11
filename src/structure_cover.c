/*
 * structure_cover.c — 结构覆盖接口实现（要塞 / 废弃矿井）
 *
 * 约束见 docs/说明/要塞与矿井接入约束.md。关键设计：
 *
 *   §4 惰性求值
 *       建表阶段只跑 RNG 复算与盒级登记。这里更进一步：模型按区块惰性创建，
 *       由调用方在真正需要时触发；创建后也只登记平面/三维范围，不查地形。
 *
 *   §3 只暴露 covers()
 *       句柄内部持有范围表，外部看不到 piece 明细。
 *
 *   要塞的 Y
 *       无法给出精确 Y（见头文件说明），covers() 忽略 y，按平面判定。
 *       这是保守方向：宁可多报覆盖，也不能漏报。
 */

#include "structure_cover.h"

#include <stdlib.h>
#include <string.h>

#include "mineshaft_gen/mineshaft_gen.h"
#include "stronghold_118/sh1_18.h"

/* ========================================================================= */
/* 通用                                                                      */
/* ========================================================================= */

/* 要塞 piece 的范围。geode 的要塞模块能算出精确世界 Y，所以这里带 Y；
 * 若某次计算拿不到精确 Y（dyExact=0），hasY 会置 0 并退化为平面判定。 */
typedef struct {
    int32_t minX, minZ, maxX, maxZ;
    int32_t minY, maxY; /* 已是世界 Y（模块内部已施加 dy） */
} ScRect;

/* 三维盒（矿井用）。 */
typedef struct {
    int32_t minX, minY, minZ, maxX, maxY, maxZ;
} Box3;

static inline int rect_contains(const ScRect *r, int x, int z)
{
    return x >= r->minX && x <= r->maxX && z >= r->minZ && z <= r->maxZ;
}

static inline int box_contains(const Box3 *b, int x, int y, int z)
{
    return x >= b->minX && x <= b->maxX &&
           y >= b->minY && y <= b->maxY &&
           z >= b->minZ && z <= b->maxZ;
}

/* ========================================================================= */
/* 要塞                                                                      */
/* ========================================================================= */

/* 覆盖模型。持有一份**全量** piece 范围表，而不仅是本区块的：
 * 因为同一座要塞会跨多个区块，紫水晶又会对一个中心连查数百个区块，
 * 按区块重建会把定位成本重复上百次。 */
struct ScStronghold {
    ScRect *rects;
    int count;
    int hasY;
};

/* 要塞世界缓存（**惰性**）。
 *
 * 实测成本分布（128 座要塞，一次全量）：
 *   环几何 + 112 格生物群系重定位   ~1010 ms   ← 几乎全部开销
 *   128 次布局生成（piece 盒）        ~46 ms
 *
 * 因此这里把重定位与布局都做成**按需**：先只枚举环几何（几乎免费），
 * 只有当某座要塞的近似起点落到查询区块附近（SC_SH_NEAR_BLOCKS 内）时，
 * 才补做重定位 + 布局。搜索范围越小，省下的越多；全图搜索时退化为全量。
 *
 * 这是进程级单槽缓存，按约束文档 §4；切换 (seed, mc) 时整体重建。
 */
#define SC_SH_NEAR_BLOCKS 448

typedef struct {
    uint64_t seed;
    int mc;
    int valid;
    int count;

    Sh118Approx *pos;        /* [count] 起点（未重定位时是近似值） */
    Sh118Stronghold *layout; /* [count] 布局结果（只为邻近的填） */
    ScRect **rects;          /* [count] 每座要塞的全量 piece 范围 */
    int *rectCount;          /* [count] */
    int *layoutDone;         /* [count] 0 = 尚未计算 */

    int allExact; /* 已算过的那些是否都拿到了精确 Y */
    /* 位置表建好后置 1（release 语义）。搜索阶段每个方块都要问一次
     * "附近有没有要塞"，若每次都走全局锁会被线程争用拖慢；建好之后
     * pos[] 只读，可以无锁快路径。 */
    int positionsReady;
} StrongholdWorldCache;

static StrongholdWorldCache gWorld;

/* -------------------------------------------------------------------------
 * 并发保护。
 *
 * geode 的复核阶段会在多个线程里并行调用 refine_center()，
 * 而本文件的 gWorld / gen 是进程级缓存（建表会 realloc）。
 * 若不加锁，两个线程可能同时重建缓存并互相覆盖。
 * 用一把粗粒度锁把"取句柄 + 查询"整段串行化：
 * 开销可忽略（单次复核只查百余个方块），且避免了锁粒度设计。
 * ------------------------------------------------------------------------- */
#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION gCoverLock;
static LONG gCoverLockInit = 0;

static void coverLockAcquire(void)
{
    /* 双检查初始化：首个调用方建立锁，之后直接用。 */
    if (InterlockedCompareExchange(&gCoverLockInit, 1, 0) == 0) {
        InitializeCriticalSection(&gCoverLock);
        InterlockedExchange(&gCoverLockInit, 2);
    } else {
        while (InterlockedCompareExchange(&gCoverLockInit, 2, 2) != 2) {
            Sleep(0);
        }
    }
    EnterCriticalSection(&gCoverLock);
}

static void coverLockRelease(void)
{
    LeaveCriticalSection(&gCoverLock);
}
#else
#include <pthread.h>
static pthread_mutex_t gCoverLock = PTHREAD_MUTEX_INITIALIZER;
static void coverLockAcquire(void) { pthread_mutex_lock(&gCoverLock); }
static void coverLockRelease(void) { pthread_mutex_unlock(&gCoverLock); }
#endif

static void strongholdWorldReset(void)
{
    int i;
    if (gWorld.rects) {
        for (i = 0; i < gWorld.count; i++)
            free(gWorld.rects[i]);
    }
    free(gWorld.rects);
    free(gWorld.rectCount);
    free(gWorld.layoutDone);
    free(gWorld.layout);
    free(gWorld.pos);
    gWorld.rects = NULL;
    gWorld.rectCount = NULL;
    gWorld.layoutDone = NULL;
    gWorld.layout = NULL;
    gWorld.pos = NULL;
    gWorld.count = 0;
    gWorld.allExact = 1;
    __atomic_store_n(&gWorld.positionsReady, 0, __ATOMIC_RELEASE);
    gWorld.valid = 0;
}

/* 建立（或复用）位置表。只算环几何，几乎零成本。 */
static int strongholdWorldInit(uint64_t seed, int mc)
{
    if (gWorld.valid && gWorld.seed == seed && gWorld.mc == mc)
        return gWorld.count;

    strongholdWorldReset();

    gWorld.pos = (Sh118Approx *)calloc(SH118_MAX_STRONGHOLDS, sizeof(Sh118Approx));
    if (!gWorld.pos) return 0;

    {
        const int n = sh118_listApprox(gWorld.pos, SH118_MAX_STRONGHOLDS, mc, seed);
        if (n <= 0) {
            free(gWorld.pos);
            gWorld.pos = NULL;
            gWorld.valid = 1;
            gWorld.seed = seed;
            gWorld.mc = mc;
            return 0;
        }
        gWorld.count = n;
    }

    gWorld.layout = (Sh118Stronghold *)calloc((size_t)gWorld.count, sizeof(Sh118Stronghold));
    gWorld.rects = (ScRect **)calloc((size_t)gWorld.count, sizeof(ScRect *));
    gWorld.rectCount = (int *)calloc((size_t)gWorld.count, sizeof(int));
    gWorld.layoutDone = (int *)calloc((size_t)gWorld.count, sizeof(int));
    if (!gWorld.layout || !gWorld.rects || !gWorld.rectCount || !gWorld.layoutDone) {
        strongholdWorldReset();
        return 0;
    }

    gWorld.allExact = 1;
    gWorld.seed = seed;
    gWorld.mc = mc;
    gWorld.valid = 1;
    __atomic_store_n(&gWorld.positionsReady, 1, __ATOMIC_RELEASE);
    return gWorld.count;
}

/* 按需补齐第 i 座要塞的重定位 + 布局，返回它写下的 piece 盒数量。 */
static int strongholdEnsureLayout(int i)
{
    static Sh118Piece pieces[SH118_PIECE_CAPACITY];
    int n, kept;
    ScRect *rs;

    if (gWorld.layoutDone[i])
        return gWorld.rectCount[i];

    /* 先标记，避免重复计算（即使下面失败也不再重试）。 */
    gWorld.layoutDone[i] = 1;

    if (!sh118_relocateApprox(&gWorld.pos[i], gWorld.mc, gWorld.seed, NULL))
        return 0;

    /* 原版把楼梯放在区块 (4,4)，与 sh118_locateAll 的口径一致。 */
    gWorld.pos[i].x = (gWorld.pos[i].x & ~15) + 4;
    gWorld.pos[i].z = (gWorld.pos[i].z & ~15) + 4;

    n = sh118_compute(&gWorld.layout[i], pieces, SH118_PIECE_CAPACITY, gWorld.mc,
                      gWorld.seed, gWorld.pos[i].x >> 4, gWorld.pos[i].z >> 4);
    if (n <= 0)
        return 0;
    if (!gWorld.layout[i].dyExact)
        gWorld.allExact = 0;

    rs = (ScRect *)malloc((size_t)n * sizeof(ScRect));
    if (!rs)
        return 0;

    kept = 0;
    for (int k = 0; k < n; k++) {
        rs[kept].minX = pieces[k].minX;
        rs[kept].maxX = pieces[k].maxX;
        rs[kept].minZ = pieces[k].minZ;
        rs[kept].maxZ = pieces[k].maxZ;
        rs[kept].minY = pieces[k].nominalMinY;
        rs[kept].maxY = pieces[k].nominalMaxY;
        kept++;
    }
    gWorld.rects[i] = rs;
    gWorld.rectCount[i] = kept;
    return kept;
}

int sc_stronghold_in_range(int64_t x, int64_t z)
{
    /* 水平欧氏距离与原点比较；用 int64 避免 25000^2 溢出 int。 */
    const int64_t r = SC_STRONGHOLD_MAX_RADIUS;
    return (x * x + z * z) <= r * r;
}

/* 无锁快路径：位置表已就绪且 seed/mc 一致时，只读 pos[] 判断"附近有没有要塞"。
 * pos[] 在布局计算时会被原地改写（近似值 -> 最终值，位移不超过 112 格），
 * 但两侧都用同一个 SC_SH_NEAR_BLOCKS 阈值判定，448 已覆盖该位移，
 * 因此读到旧值也只会"多算不会漏算"，不会漏掉要塞。 */
static int strongholdAnyNear(uint64_t seed, int mc, int cxw, int czw)
{
    int i;
    if (__atomic_load_n(&gWorld.positionsReady, __ATOMIC_ACQUIRE) == 0)
        return -1; /* 尚未建表，走加锁路径 */
    if (gWorld.seed != seed || gWorld.mc != mc)
        return -1; /* 需要重建，走加锁路径 */
    for (i = 0; i < gWorld.count; i++) {
        if (abs(gWorld.pos[i].x - cxw) <= SC_SH_NEAR_BLOCKS &&
            abs(gWorld.pos[i].z - czw) <= SC_SH_NEAR_BLOCKS)
            return 1;
    }
    return 0;
}

/* 矩形级别的粗判：范围外扩 SC_SH_NEAR_BLOCKS 后仍没有任何要塞，就一定覆盖不到。
 * 只读位置表（不需要锁）；positionsReady 未就绪时保守返回 1（走正常加锁路径）。 */
int sc_stronghold_may_reach(uint64_t seed, int mc, int x0, int z0, int x1, int z1)
{
    int i;
    if (__atomic_load_n(&gWorld.positionsReady, __ATOMIC_ACQUIRE) == 0)
        return 1;
    if (gWorld.seed != seed || gWorld.mc != mc)
        return 1;
    x0 -= SC_SH_NEAR_BLOCKS; x1 += SC_SH_NEAR_BLOCKS;
    z0 -= SC_SH_NEAR_BLOCKS; z1 += SC_SH_NEAR_BLOCKS;
    for (i = 0; i < gWorld.count; i++) {
        const int px = gWorld.pos[i].x, pz = gWorld.pos[i].z;
        if (px >= x0 && px <= x1 && pz >= z0 && pz <= z1)
            return 1;
    }
    return 0;
}

ScStronghold *sc_stronghold_for_chunk(uint64_t seed, int mc, int chunkX, int chunkZ)
{
    ScStronghold *ss;
    ScRect *rects;
    int i, kept, total;
    int ss_hasY;
    const int x0 = chunkX * 16, z0 = chunkZ * 16;
    const int x1 = x0 + 15, z1 = z0 + 15;
    const int cxw = x0 + 8, czw = z0 + 8;

    if (!sh118_versionSupported(mc)) return NULL;

    /* §1 快速剔除：先在区块中心做 25000 半径判定，避免为远处候选做任何计算。 */
    if (!sc_stronghold_in_range(cxw, czw)) return NULL;

    /* 快路径：绝大多数区块附近没有要塞，无需抢全局锁。 */
    if (strongholdAnyNear(seed, mc, cxw, czw) == 0) return NULL;

    /* gWorld 是进程级缓存（会 realloc），复核阶段多线程并行调用本函数，
     * 因此建表 + 汇集这一段必须串行。查询阶段（covers）不再碰全局状态。 */
    coverLockAcquire();

    if (strongholdWorldInit(seed, mc) <= 0) {
        coverLockRelease();
        return NULL;
    }

    /* 只为"近似起点落在本区块附近"的要塞付重定位 + 布局的代价。
     * 半径取 448：重定位最多偏移 112、piece 足迹半径按 256 计、区块半宽 8。 */
    total = 0;
    for (i = 0; i < gWorld.count; i++) {
        const int dx = abs(gWorld.pos[i].x - cxw);
        const int dz = abs(gWorld.pos[i].z - czw);
        if (dx > SC_SH_NEAR_BLOCKS || dz > SC_SH_NEAR_BLOCKS)
            continue;
        total += strongholdEnsureLayout(i);
    }
    if (total == 0) {
        coverLockRelease();
        return NULL;
    }

    rects = (ScRect *)malloc((size_t)total * sizeof(ScRect));
    if (!rects) {
        coverLockRelease();
        return NULL;
    }

    kept = 0;
    for (i = 0; i < gWorld.count; i++) {
        const int dx = abs(gWorld.pos[i].x - cxw);
        const int dz = abs(gWorld.pos[i].z - czw);
        if (dx > SC_SH_NEAR_BLOCKS || dz > SC_SH_NEAR_BLOCKS)
            continue;
        for (int k = 0; k < gWorld.rectCount[i]; k++) {
            const ScRect *r = &gWorld.rects[i][k];
            if (r->maxX < x0 || r->minX > x1 || r->maxZ < z0 || r->minZ > z1)
                continue;
            rects[kept++] = *r;
        }
    }
    ss_hasY = gWorld.allExact;

    coverLockRelease();

    if (kept == 0) {
        free(rects);
        return NULL;
    }

    ss = (ScStronghold *)calloc(1, sizeof(*ss));
    if (!ss) {
        free(rects);
        return NULL;
    }
    ss->rects = rects;
    ss->count = kept;
    ss->hasY = ss_hasY;
    return ss;
}

void sc_stronghold_free(ScStronghold *ss)
{
    if (!ss) return;
    free(ss->rects);
    free(ss);
}

int sc_stronghold_covers(const ScStronghold *ss, int x, int y, int z)
{
    if (!ss) return 0;

    for (int i = 0; i < ss->count; i++)
    {
        const ScRect *r = &ss->rects[i];
        if (x < r->minX || x > r->maxX || z < r->minZ || z > r->maxZ) continue;

        /* 有精确 Y 时做三维判定；否则按平面（保守，宁可多报）。 */
        if (!ss->hasY) return 1;
        if (y >= r->minY && y <= r->maxY) return 1;
    }
    return 0;
}

int sc_stronghold_may_touch_chunk(const ScStronghold *ss, int chunkX, int chunkZ)
{
    if (!ss) return 0;
    const int x0 = chunkX * 16, z0 = chunkZ * 16;
    const int x1 = x0 + 15, z1 = z0 + 15;
    for (int i = 0; i < ss->count; i++)
    {
        const ScRect *r = &ss->rects[i];
        if (r->maxX >= x0 && r->minX <= x1 && r->maxZ >= z0 && r->minZ <= z1) return 1;
    }
    return 0;
}

/* ========================================================================= */
/* 废弃矿井                                                                  */
/* ========================================================================= */

struct ScMineshaft {
    Box3 *boxes;
    int count;
    int verticalKnown;
};

ScMineshaft *sc_mineshaft_for_chunk(uint64_t seed, int version, int chunkX, int chunkZ)
{
    static MineshaftGen gen;
    ScMineshaft *ms;
    Box3 *boxes;
    MsGenOpts opts;
    int n, kept;

    if (version < 0 || version >= MS_VERSION_COUNT) return NULL;

    /* 放置门控：只有被放置为矿井的区块才可能有 piece。
     * 缺了这一步会把每个区块都当成有矿井（实测命中率 100% vs 0.4%），
     * 既多排除母岩，又让这里每次都要跑完整的 piece 生成。 */
    if (!ms_is_feature_chunk(seed, chunkX, chunkZ)) return NULL;

    memset(&opts, 0, sizeof(opts));
    opts.version = version;
    opts.type = MS_TYPE_NORMAL; /* 主引擎只需要 normal；mesa 由调用方决定 */
    opts.seaLevel = ms_version_sea_level((MsVersion)version);
    opts.minY = ms_version_min_y((MsVersion)version);

    /* 矿井逐区块独立判定，没有半径限制（约束文档 §2）。
     * gen 是 static 工作区，多线程并行复核时必须串行使用。 */
    coverLockAcquire();
    n = ms_generate(&gen, seed, chunkX, chunkZ, &opts);
    if (n <= 0) { coverLockRelease(); return NULL; }

    boxes = (Box3 *)calloc((size_t)n, sizeof(Box3));
    if (!boxes) { coverLockRelease(); return NULL; }

    /* 只登记与目标区块相交的 piece（盒级剔除，§4）。 */
    kept = 0;
    {
        const int bx0 = chunkX * 16, bz0 = chunkZ * 16;
        const int bx1 = bx0 + 15, bz1 = bz0 + 15;
        for (int i = 0; i < n; i++)
        {
            const MsPiece *p = &gen.pieces[i];
            if (p->bb.maxX < bx0 || p->bb.minX > bx1 ||
                p->bb.maxZ < bz0 || p->bb.minZ > bz1) continue;
            boxes[kept].minX = p->bb.minX;
            boxes[kept].maxX = p->bb.maxX;
            boxes[kept].minY = p->bb.minY;
            boxes[kept].maxY = p->bb.maxY;
            boxes[kept].minZ = p->bb.minZ;
            boxes[kept].maxZ = p->bb.maxZ;
            kept++;
        }
    }
    coverLockRelease();
    if (kept == 0) { free(boxes); return NULL; }

    ms = (ScMineshaft *)calloc(1, sizeof(*ms));
    if (!ms) { free(boxes); return NULL; }
    ms->boxes = boxes;
    ms->count = kept;
    /* normal 类型只用常量 seaLevel/minY，垂直偏移是确定的。 */
    ms->verticalKnown = (opts.type == MS_TYPE_NORMAL) ? 1 : 0;
    return ms;
}

void sc_mineshaft_free(ScMineshaft *ms)
{
    if (!ms) return;
    free(ms->boxes);
    free(ms);
}

int sc_mineshaft_covers(const ScMineshaft *ms, int x, int y, int z)
{
    if (!ms) return 0;
    /* 垂直偏移未知时保守认为覆盖（宁可多报，§6 下限只算可证明安全的）。 */
    if (!ms->verticalKnown) {
        for (int i = 0; i < ms->count; i++)
        {
            const Box3 *b = &ms->boxes[i];
            if (x >= b->minX && x <= b->maxX && z >= b->minZ && z <= b->maxZ) return 1;
        }
        return 0;
    }
    for (int i = 0; i < ms->count; i++)
        if (box_contains(&ms->boxes[i], x, y, z)) return 1;
    return 0;
}

int sc_mineshaft_may_touch_chunk(const ScMineshaft *ms, int chunkX, int chunkZ)
{
    if (!ms) return 0;
    const int x0 = chunkX * 16, z0 = chunkZ * 16;
    const int x1 = x0 + 15, z1 = z0 + 15;
    for (int i = 0; i < ms->count; i++)
    {
        const Box3 *b = &ms->boxes[i];
        if (b->maxX >= x0 && b->minX <= x1 && b->maxZ >= z0 && b->minZ <= z1) return 1;
    }
    return 0;
}

int sc_mineshaft_vertical_known(const ScMineshaft *ms)
{
    return ms ? ms->verticalKnown : 0;
}
