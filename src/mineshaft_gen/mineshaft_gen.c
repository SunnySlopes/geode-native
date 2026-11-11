/* ============================================================================
 * mineshaft_gen.c -- 废弃矿井(Mineshaft)生成器库
 *
 * 从 1.21.11 (mojmap) 反编译源码 1:1 复刻, 核对到 26.3:
 *   MineshaftPieces / MineshaftStructure / BoundingBox /
 *   StructurePiecesBuilder / WorldgenRandom / LegacyRandomSource
 *
 * 四个版本(1.21.11 / 26.1.2 / 26.2 / 26.3)的生成语义完全相同,
 * 版本差异考证见 mineshaft_gen.h 文件头。
 *
 * 本文件只含生成逻辑与公开 API，不含命令行入口。
 * ==========================================================================*/

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>

#include "mineshaft_gen.h"

/* ---------------------------------------------------------------------------
 * 1. 随机数: LegacyRandomSource (== java.util.Random 的 48-bit LCG)
 * -------------------------------------------------------------------------*/

#define MC_MULT 25214903917ULL
#define MC_ADD 11ULL
#define MC_MASK ((1ULL << 48) - 1ULL)
/* BitRandomSource.DOUBLE_MULTIPLIER = 1.110223E-16F (float 字面量!) */
#define MC_DOUBLE_MULT ((double)(float)1.110223e-16f)

/* 诊断用: 置 1 时把每次 nextInt(bound) 打到 stderr, 用于与 Java 参考实现
 * (tools/Bisect.java) 逐行对照 RNG 消耗序列。默认关闭, 无运行时开销。 */
int ms_trace_rng = 0;
typedef MsRng McRng;

static void mc_rng_set_seed(McRng *r, uint64_t s)
{
    r->seed = (s ^ MC_MULT) & MC_MASK;
    r->count = 0;
}

/* LegacyRandomSource.next(bits) */
static int32_t mc_next_bits(McRng *r, int bits)
{
    r->seed = (r->seed * MC_MULT + MC_ADD) & MC_MASK;
    r->count++;
    return (int32_t)(uint32_t)(r->seed >> (48 - bits));
}

/* BitRandomSource.nextInt() */
#define MC_UNUSED(x) ((void)(x))

/* BitRandomSource.nextInt(bound) */
static int32_t mc_next_int(McRng *r, int32_t bound)
{
    int32_t sample, modulo;
    if (bound <= 0) { fprintf(stderr, "fatal: nextInt bound <= 0\n"); exit(2); }
    if ((bound & (bound - 1)) == 0)  /* 2 的幂 */
    {
        int32_t v = (int32_t)(((int64_t)bound * (int64_t)mc_next_bits(r, 31)) >> 31);
        if (ms_trace_rng) fprintf(stderr, "nextInt(%d) = %d\n", bound, (int)v);
        return v;
    }
    do {
        sample = mc_next_bits(r, 31);
        modulo = sample % bound;
    } while ((int64_t)sample - (int64_t)modulo + (int64_t)(bound - 1) < 0);
    if (ms_trace_rng) fprintf(stderr, "nextInt(%d) = %d\n", bound, (int)modulo);
    return modulo;
}

/* BitRandomSource.nextLong()
 *   int upper = next(32); int lower = next(32);
 *   long shifted = (long) upper << 32;
 *   return shifted + lower;      <-- lower 是 int, 相加时符号扩展(!)
 */
static int64_t mc_next_long(McRng *r)
{
    int32_t upper = mc_next_bits(r, 32);
    int32_t lower = mc_next_bits(r, 32);
    int64_t shifted = (int64_t)upper << 32;
    return shifted + (int64_t)lower;   /* 有符号加法, 与原版一致 */
}

/* BitRandomSource.nextBoolean() */
static int mc_next_boolean(McRng *r) { return mc_next_bits(r, 1) != 0; }

/* BitRandomSource.nextDouble(): long * float -> 在 float 精度上相乘, 再拓宽 */
static double mc_next_double(McRng *r)
{
    int32_t upper = mc_next_bits(r, 26);
    int32_t lower = mc_next_bits(r, 27);
    int64_t combined = ((int64_t)upper << 27) + (int64_t)lower;
    return (double)((float)combined * (float)1.110223e-16f);
}

/* WorldgenRandom.setLargeFeatureSeed(seed, chunkX, chunkZ) */
static void mc_set_large_feature_seed(McRng *r, uint64_t seed, int32_t chunkX, int32_t chunkZ)
{
    uint64_t xScale, zScale, result;
    mc_rng_set_seed(r, seed);
    xScale = (uint64_t)mc_next_long(r);
    zScale = (uint64_t)mc_next_long(r);
    result = (uint64_t)((int64_t)chunkX * (int64_t)xScale)
           ^ (uint64_t)((int64_t)chunkZ * (int64_t)zScale)
           ^ seed;
    mc_rng_set_seed(r, result);
}

/* ---------------------------------------------------------------------------
 * 2. BoundingBox (整型闭区间盒, 源码同名语义)
 * -------------------------------------------------------------------------*/

/* 内部沿用短名, 实际类型由 mineshaft_gen.h 定义, 保证只有一份定义。 */
typedef MsBox BBox;

static BBox bbox_make(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1)
{
    BBox b;
    b.minX = x0; b.minY = y0; b.minZ = z0;
    b.maxX = x1; b.maxY = y1; b.maxZ = z1;
    if (x1 < x0 || y1 < y0 || z1 < z0) { /* 源码会 logAndPauseIfInIde 并纠正 */
        int32_t a, c;
        a = b.minX; c = b.maxX; b.minX = a < c ? a : c; b.maxX = a < c ? c : a;
        a = b.minY; c = b.maxY; b.minY = a < c ? a : c; b.maxY = a < c ? c : a;
        a = b.minZ; c = b.maxZ; b.minZ = a < c ? a : c; b.maxZ = a < c ? c : a;
    }
    return b;
}

static int32_t bbox_xspan(const BBox *b) { return b->maxX - b->minX + 1; }
static int32_t bbox_yspan(const BBox *b) { return b->maxY - b->minY + 1; }
static int32_t bbox_zspan(const BBox *b) { return b->maxZ - b->minZ + 1; }

static void bbox_move(BBox *b, int32_t dx, int32_t dy, int32_t dz)
{
    b->minX += dx; b->maxX += dx;
    b->minY += dy; b->maxY += dy;
    b->minZ += dz; b->maxZ += dz;
}

/* BoundingBox.intersects -- 闭区间, 六轴全部含等号 */
static int bbox_intersects(const BBox *a, const BBox *b)
{
    return a->maxX >= b->minX && a->minX <= b->maxX
        && a->maxZ >= b->minZ && a->minZ <= b->maxZ
        && a->maxY >= b->minY && a->minY <= b->maxY;
}

static void bbox_encapsulate(BBox *acc, const BBox *o)
{
    if (o->minX < acc->minX) acc->minX = o->minX;
    if (o->minY < acc->minY) acc->minY = o->minY;
    if (o->minZ < acc->minZ) acc->minZ = o->minZ;
    if (o->maxX > acc->maxX) acc->maxX = o->maxX;
    if (o->maxY > acc->maxY) acc->maxY = o->maxY;
    if (o->maxZ > acc->maxZ) acc->maxZ = o->maxZ;
}

/* ---------------------------------------------------------------------------
 * 3. 方向与各 piece 的盒模板 (逐字对照源码 switch 分支)
 * -------------------------------------------------------------------------*/

enum { DIR_NORTH = 0, DIR_SOUTH = 1, DIR_WEST = 2, DIR_EAST = 3 };
static const char *DIR_NAME[4] = { "north", "south", "west", "east" };

/* piece 类型 */
enum { PT_ROOM = 0, PT_CORRIDOR = 1, PT_CROSSING = 2, PT_STAIRS = 3, PT_ENTRANCE = 4, PT_STRUCTURE = 5 };
static const char *PT_NAME[6] = { "room", "corridor", "crossing", "stairs", "entrance", "structure" };

/* flags 位 */
#define FLAG_RAILS      0x01 /* 走廊有铁轨 */
#define FLAG_SPIDER     0x02 /* 走廊刷怪笼(洞穴蜘蛛) */
#define FLAG_TWOFLOOR   0x04 /* 十字为双层 */
#define FLAG_HAS_X      0x08 /* 房间入口轴向 X */
#define FLAG_HAS_Z      0x10 /* 房间入口轴向 Z */

/* MineShaftCorridor.findCorridorSize 的盒模板 */
static BBox tpl_corridor(int dir, int32_t blockLength)
{
    switch (dir) {
    default:
    case DIR_NORTH: return bbox_make(0, 0, -(blockLength - 1), 2, 2, 0);
    case DIR_SOUTH: return bbox_make(0, 0, 0, 2, 2, blockLength - 1);
    case DIR_WEST:  return bbox_make(-(blockLength - 1), 0, 0, 0, 2, 2);
    case DIR_EAST:  return bbox_make(0, 0, 0, blockLength - 1, 2, 2);
    }
}

/* MineShaftCrossing.findCrossing 的盒模板 (y1 = 2 或 6) */
static BBox tpl_crossing(int dir, int32_t y1)
{
    switch (dir) {
    default:
    case DIR_NORTH: return bbox_make(-1, 0, -4, 3, y1, 0);
    case DIR_SOUTH: return bbox_make(-1, 0, 0, 3, y1, 4);
    case DIR_WEST:  return bbox_make(-4, 0, -1, 0, y1, 3);
    case DIR_EAST:  return bbox_make(0, 0, -1, 4, y1, 3);
    }
}

/* MineShaftStairs.findStairs 的盒模板 (固定 -5..2 / 8 长) */
static BBox tpl_stairs(int dir)
{
    switch (dir) {
    default:
    case DIR_NORTH: return bbox_make(0, -5, -8, 2, 2, 0);
    case DIR_SOUTH: return bbox_make(0, -5, 0, 2, 2, 8);
    case DIR_WEST:  return bbox_make(-8, -5, 0, 0, 2, 2);
    case DIR_EAST:  return bbox_make(0, -5, 0, 8, 2, 2);
    }
}

/* ---------------------------------------------------------------------------
 * 4. 生成器状态
 * -------------------------------------------------------------------------*/

#define MAX_PIECES     4096
#define MAX_DEPTH_MC   8      /* MineshaftPieces.MAX_DEPTH */
#define MAX_HORIZONTAL 80     /* generateAndAddPiece 的 80 格水平限制 */
#define MAGIC_START_Y  50     /* MineshaftPieces.MAGIC_START_Y */

typedef MsPiece Piece;

typedef MineshaftGen Gen;

static Piece *gen_room(Gen *g) { return &g->pieces[0]; }

static Piece *gen_add_piece(Gen *g, int type, int orient, int genDepth, int flags, BBox bb)
{
    Piece *p;
    if (g->count >= MAX_PIECES) { g->overflow = 1; return NULL; }
    p = &g->pieces[g->count++];
    p->type = type;
    p->orient = orient;
    p->genDepth = genDepth;
    p->flags = flags;
    p->sections = 0;
    p->bb = bb;
    return p;
}

static void gen_add_entrance(Gen *g, int orient, int genDepth, BBox bb)
{
    Piece *p;
    if (g->entranceCount >= MAX_PIECES) return;
    p = &g->entrances[g->entranceCount++];
    p->type = PT_ENTRANCE;
    p->orient = orient;
    p->genDepth = genDepth;
    p->flags = 0;
    p->sections = 0;
    p->bb = bb;
}

/* StructurePiece.findCollisionPiece(所有已加入的 piece) */
static int gen_collides(Gen *g, const BBox *box)
{
    int i;
    for (i = 0; i < g->count; i++)
        if (bbox_intersects(&g->pieces[i].bb, box)) return 1;
    return 0;
}

static int gen_add_child(Gen *g, int footX, int footY, int footZ, int dir, int depth);

/* ===== 房间: MineShaftRoom.addChildren ===================================== */
static void room_add_children(Gen *g)
{
    Piece *room = gen_room(g);
    int depth = room->genDepth;
    int xspan = bbox_xspan(&room->bb);
    int zspan = bbox_zspan(&room->bb);
    int heightSpace = bbox_yspan(&room->bb) - 3 - 1;
    int pos, child;
    Piece childPiece;

    if (heightSpace <= 0) heightSpace = 1;

    /* --- 北侧 (-Z) --- */
    pos = 0;
    while (pos < xspan) {
        pos += mc_next_int(&g->rng, xspan);
        if (pos + 3 > xspan) break;
        child = gen_add_child(g, room->bb.minX + pos,
                                 room->bb.minY + mc_next_int(&g->rng, heightSpace) + 1,
                                 room->bb.minZ - 1, DIR_NORTH, depth);
        if (child >= 0) {
            childPiece = g->pieces[child];
            gen_add_entrance(g, DIR_NORTH, depth, bbox_make(
                childPiece.bb.minX, childPiece.bb.minY, room->bb.minZ,
                childPiece.bb.maxX, childPiece.bb.maxY, room->bb.minZ + 1));
            room = gen_room(g); /* pieces[] 可能因扩容被搬动, 指针重新取 */
        }
        pos += 4;
    }

    /* --- 南侧 (+Z) --- */
    pos = 0;
    while (pos < xspan) {
        pos += mc_next_int(&g->rng, xspan);
        if (pos + 3 > xspan) break;
        child = gen_add_child(g, room->bb.minX + pos,
                                 room->bb.minY + mc_next_int(&g->rng, heightSpace) + 1,
                                 room->bb.maxZ + 1, DIR_SOUTH, depth);
        if (child >= 0) {
            childPiece = g->pieces[child];
            gen_add_entrance(g, DIR_SOUTH, depth, bbox_make(
                childPiece.bb.minX, childPiece.bb.minY, room->bb.maxZ - 1,
                childPiece.bb.maxX, childPiece.bb.maxY, room->bb.maxZ));
        }
        pos += 4;
    }

    /* --- 西侧 (-X) --- */
    pos = 0;
    while (pos < zspan) {
        pos += mc_next_int(&g->rng, zspan);
        if (pos + 3 > zspan) break;
        child = gen_add_child(g, room->bb.minX - 1,
                                 room->bb.minY + mc_next_int(&g->rng, heightSpace) + 1,
                                 room->bb.minZ + pos, DIR_WEST, depth);
        if (child >= 0) {
            childPiece = g->pieces[child];
            gen_add_entrance(g, DIR_WEST, depth, bbox_make(
                room->bb.minX, childPiece.bb.minY, childPiece.bb.minZ,
                room->bb.minX + 1, childPiece.bb.maxY, childPiece.bb.maxZ));
        }
        pos += 4;
    }

    /* --- 东侧 (+X) --- */
    pos = 0;
    while (pos < zspan) {
        pos += mc_next_int(&g->rng, zspan);
        if (pos + 3 > zspan) break;
        child = gen_add_child(g, room->bb.maxX + 1,
                                 room->bb.minY + mc_next_int(&g->rng, heightSpace) + 1,
                                 room->bb.minZ + pos, DIR_EAST, depth);
        if (child >= 0) {
            childPiece = g->pieces[child];
            gen_add_entrance(g, DIR_EAST, depth, bbox_make(
                room->bb.maxX - 1, childPiece.bb.minY, childPiece.bb.minZ,
                room->bb.maxX, childPiece.bb.maxY, childPiece.bb.maxZ));
        }
        pos += 4;
    }
}

/* ===== 走廊: MineShaftCorridor.addChildren ================================= */
static void corridor_add_children(Gen *g, int idx)
{
    Piece self = g->pieces[idx];
    const BBox *bb = &self.bb;
    int depth = self.genDepth;
    int endSelection = mc_next_int(&g->rng, 4);
    int orient = self.orient;

    switch (orient) {
    case DIR_NORTH:
    default:
        if (endSelection <= 1)
            gen_add_child(g, bb->minX, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->minZ - 1, DIR_NORTH, depth);
        else if (endSelection == 2)
            gen_add_child(g, bb->minX - 1, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->minZ, DIR_WEST, depth);
        else
            gen_add_child(g, bb->maxX + 1, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->minZ, DIR_EAST, depth);
        break;
    case DIR_SOUTH:
        if (endSelection <= 1)
            gen_add_child(g, bb->minX, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->maxZ + 1, DIR_SOUTH, depth);
        else if (endSelection == 2)
            gen_add_child(g, bb->minX - 1, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->maxZ - 3, DIR_WEST, depth);
        else
            gen_add_child(g, bb->maxX + 1, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->maxZ - 3, DIR_EAST, depth);
        break;
    case DIR_WEST:
        if (endSelection <= 1)
            gen_add_child(g, bb->minX - 1, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->minZ, DIR_WEST, depth);
        else if (endSelection == 2)
            gen_add_child(g, bb->minX, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->minZ - 1, DIR_NORTH, depth);
        else
            gen_add_child(g, bb->minX, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->maxZ + 1, DIR_SOUTH, depth);
        break;
    case DIR_EAST:
        if (endSelection <= 1)
            gen_add_child(g, bb->maxX + 1, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->minZ, DIR_EAST, depth);
        else if (endSelection == 2)
            gen_add_child(g, bb->maxX - 3, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->minZ - 1, DIR_NORTH, depth);
        else
            gen_add_child(g, bb->maxX - 3, bb->minY - 1 + mc_next_int(&g->rng, 3), bb->maxZ + 1, DIR_SOUTH, depth);
        break;
    }

    /* 侧向岔道: 只在 depth < 8 时, 沿主轴每 5 格判一次 */
    if (depth < 8) {
        if (orient != DIR_NORTH && orient != DIR_SOUTH) {
            int32_t x;
            for (x = bb->minX + 3; x + 3 <= bb->maxX; x += 5) {
                int selection = mc_next_int(&g->rng, 5);
                if (selection == 0)
                    gen_add_child(g, x, bb->minY, bb->minZ - 1, DIR_NORTH, depth + 1);
                else if (selection == 1)
                    gen_add_child(g, x, bb->minY, bb->maxZ + 1, DIR_SOUTH, depth + 1);
            }
        } else {
            int32_t z;
            for (z = bb->minZ + 3; z + 3 <= bb->maxZ; z += 5) {
                int selection = mc_next_int(&g->rng, 5);
                if (selection == 0)
                    gen_add_child(g, bb->minX - 1, bb->minY, z, DIR_WEST, depth + 1);
                else if (selection == 1)
                    gen_add_child(g, bb->maxX + 1, bb->minY, z, DIR_EAST, depth + 1);
            }
        }
    }
}

/* ===== 十字: MineShaftCrossing.addChildren ================================= */
static void crossing_add_children(Gen *g, int idx)
{
    Piece self = g->pieces[idx];
    const BBox *bb = &self.bb;
    int depth = self.genDepth;
    int isTwoFloored = (bbox_yspan(bb) > 3);

    switch (self.orient) {
    case DIR_NORTH:
    default:
        gen_add_child(g, bb->minX + 1, bb->minY, bb->minZ - 1, DIR_NORTH, depth);
        gen_add_child(g, bb->minX - 1, bb->minY, bb->minZ + 1, DIR_WEST,  depth);
        gen_add_child(g, bb->maxX + 1, bb->minY, bb->minZ + 1, DIR_EAST,  depth);
        break;
    case DIR_SOUTH:
        gen_add_child(g, bb->minX + 1, bb->minY, bb->maxZ + 1, DIR_SOUTH, depth);
        gen_add_child(g, bb->minX - 1, bb->minY, bb->minZ + 1, DIR_WEST,  depth);
        gen_add_child(g, bb->maxX + 1, bb->minY, bb->minZ + 1, DIR_EAST,  depth);
        break;
    case DIR_WEST:
        gen_add_child(g, bb->minX + 1, bb->minY, bb->minZ - 1, DIR_NORTH, depth);
        gen_add_child(g, bb->minX + 1, bb->minY, bb->maxZ + 1, DIR_SOUTH, depth);
        gen_add_child(g, bb->minX - 1, bb->minY, bb->minZ + 1, DIR_WEST,  depth);
        break;
    case DIR_EAST:
        gen_add_child(g, bb->minX + 1, bb->minY, bb->minZ - 1, DIR_NORTH, depth);
        gen_add_child(g, bb->minX + 1, bb->minY, bb->maxZ + 1, DIR_SOUTH, depth);
        gen_add_child(g, bb->maxX + 1, bb->minY, bb->minZ + 1, DIR_EAST,  depth);
        break;
    }

    if (isTwoFloored) {
        if (mc_next_boolean(&g->rng))
            gen_add_child(g, bb->minX + 1, bb->minY + 3 + 1, bb->minZ - 1, DIR_NORTH, depth);
        if (mc_next_boolean(&g->rng))
            gen_add_child(g, bb->minX - 1, bb->minY + 3 + 1, bb->minZ + 1, DIR_WEST,  depth);
        if (mc_next_boolean(&g->rng))
            gen_add_child(g, bb->maxX + 1, bb->minY + 3 + 1, bb->minZ + 1, DIR_EAST,  depth);
        if (mc_next_boolean(&g->rng))
            gen_add_child(g, bb->minX + 1, bb->minY + 3 + 1, bb->maxZ + 1, DIR_SOUTH, depth);
    }
}

/* ===== 楼梯: MineShaftStairs.addChildren =================================== */
static void stairs_add_children(Gen *g, int idx)
{
    Piece self = g->pieces[idx];
    const BBox *bb = &self.bb;
    int depth = self.genDepth;

    switch (self.orient) {
    case DIR_NORTH:
    default:
        gen_add_child(g, bb->minX, bb->minY, bb->minZ - 1, DIR_NORTH, depth);
        break;
    case DIR_SOUTH:
        gen_add_child(g, bb->minX, bb->minY, bb->maxZ + 1, DIR_SOUTH, depth);
        break;
    case DIR_WEST:
        gen_add_child(g, bb->minX - 1, bb->minY, bb->minZ, DIR_WEST, depth);
        break;
    case DIR_EAST:
        gen_add_child(g, bb->maxX + 1, bb->minY, bb->minZ, DIR_EAST, depth);
        break;
    }
}

static void gen_add_children(Gen *g, int idx)
{
    switch (g->pieces[idx].type) {
    case PT_ROOM:     room_add_children(g);          break;
    case PT_CORRIDOR: corridor_add_children(g, idx); break;
    case PT_CROSSING: crossing_add_children(g, idx); break;
    case PT_STAIRS:   stairs_add_children(g, idx);   break;
    default: break;
    }
}

/* ===== createRandomShaftPiece: 决定下一块的类型与盒 ======================== */
static int gen_create_random_piece(Gen *g, int footX, int footY, int footZ, int dir, int genDepth)
{
    int selection = mc_next_int(&g->rng, 100);
    Piece *p;

    if (selection >= 80) {
        /* --- 十字 --- */
        int y1 = (mc_next_int(&g->rng, 4) == 0) ? 6 : 2;
        BBox box = tpl_crossing(dir, y1);
        bbox_move(&box, footX, footY, footZ);
        if (gen_collides(g, &box)) return -1;
        p = gen_add_piece(g, PT_CROSSING, dir, genDepth,
                          (bbox_yspan(&box) > 3) ? FLAG_TWOFLOOR : 0, box);
        return p ? (int)(p - g->pieces) : -1;
    }

    if (selection >= 70) {
        /* --- 楼梯 --- */
        BBox box = tpl_stairs(dir);
        bbox_move(&box, footX, footY, footZ);
        if (gen_collides(g, &box)) return -1;
        p = gen_add_piece(g, PT_STAIRS, dir, genDepth, 0, box);
        return p ? (int)(p - g->pieces) : -1;
    }

    /* --- 走廊: 长度 4/3/2 段, 逐级回退重试 --- */
    {
        int corridorLength;
        for (corridorLength = mc_next_int(&g->rng, 3) + 2; corridorLength > 0; corridorLength--) {
            int32_t blockLength = (int32_t)corridorLength * 5;
            BBox box = tpl_corridor(dir, blockLength);
            int hasRails, spiderCorridor = 0, numSections, flags;

            bbox_move(&box, footX, footY, footZ);
            if (gen_collides(g, &box)) continue;

            hasRails = (mc_next_int(&g->rng, 3) == 0);
            if (!hasRails) spiderCorridor = (mc_next_int(&g->rng, 23) == 0);

            numSections = ((dir == DIR_NORTH || dir == DIR_SOUTH)
                           ? bbox_zspan(&box) : bbox_xspan(&box)) / 5;

            flags = (hasRails ? FLAG_RAILS : 0) | (spiderCorridor ? FLAG_SPIDER : 0);
            p = gen_add_piece(g, PT_CORRIDOR, dir, genDepth, flags, box);
            if (!p) return -1;
            p->sections = numSections;
            return (int)(p - g->pieces);
        }
    }
    return -1;
}

/* ===== generateAndAddPiece: 递归 + 80 格限制 + 深度上限 ==================== */
static int gen_add_child(Gen *g, int footX, int footY, int footZ, int dir, int depth)
{
    const BBox *startBox;
    int idx;

    if (depth > MAX_DEPTH_MC) return -1;

    startBox = &gen_room(g)->bb;   /* Java: startPiece 恒为起点房间 */
    if (abs(footX - startBox->minX) > MAX_HORIZONTAL) return -1;
    if (abs(footZ - startBox->minZ) > MAX_HORIZONTAL) return -1;

    idx = gen_create_random_piece(g, footX, footY, footZ, dir, depth + 1);
    if (idx >= 0) {
        /* 注意: 必须在本 piece 的 children 展开前保存其盒(递归中 pieces[] 只追加) */
        gen_add_children(g, idx);
    }
    return idx;
}

/* ---------------------------------------------------------------------------
 * 5. 顶层: MineshaftStructure.findGenerationPoint + generatePiecesAndAdjust
 * -------------------------------------------------------------------------*/

typedef MsGenOpts GenOpts;

typedef MsGenResult GenResult;

static void gen_structure(Gen *g, const GenOpts *o, uint64_t seed, int32_t chunkX, int32_t chunkZ, GenResult *out)
{
    Piece *room;
    int i;
    BBox acc;

    /* 只重置计数, 不必把整个 pieces 数组清零 */
    g->count = 0;
    g->entranceCount = 0;
    g->overflow = 0;
    mc_set_large_feature_seed(&g->rng, seed, chunkX, chunkZ);

    /* context.random().nextDouble();  -- 结果丢弃, 但消耗 2 次 next() */
    (void)mc_next_double(&g->rng);

    /* new MineShaftRoom(0, random, chunkPos.getBlockX(2), chunkPos.getBlockZ(2), type)
     *
     * 关键: Java 的实参求值严格从左到右, 而 C 的求值顺序未定义。把三个
     * nextInt(6) 内联进 bbox_make(...) 会让编译器按任意顺序取数, 导致
     * x/y/z 三个跨度分配到不同的随机值 —— 这曾造成 157 vs 196 pieces 的偏差。
     * 必须先取到临时变量, 再构造盒。 */
    {
        int32_t west  = chunkX * 16 + 2;
        int32_t north = chunkZ * 16 + 2;
        int32_t rx, ry, rz;
        BBox box;

        rx = mc_next_int(&g->rng, 6);
        ry = mc_next_int(&g->rng, 6);
        rz = mc_next_int(&g->rng, 6);

        box = bbox_make(west, MAGIC_START_Y, north,
                        west + 7 + rx,
                        54 + ry,
                        north + 7 + rz);
        room = gen_add_piece(g, PT_ROOM, -1, 0, 0, box);
    }
    if (!room) { out->pieceCount = 0; return; }

    room_add_children(g);

    /* --- 整体包围盒 --- */
    acc = g->pieces[0].bb;
    for (i = 1; i < g->count; i++) bbox_encapsulate(&acc, &g->pieces[i].bb);

    /* --- 垂直定位 --- */
    if (o->type == 1) {
        /* MESA: targetY = surface<=seaLevel ? seaLevel : randomBetweenInclusive(random, seaLevel, surface) */
        int32_t target = o->mesaSurface;
        int32_t centerY = acc.minY + (acc.maxY - acc.minY + 1) / 2;
        if (target > o->seaLevel) {
            int32_t span = target - o->seaLevel + 1;
            if (span <= 0) span = 1;
            target = o->seaLevel + mc_next_int(&g->rng, span);
        } else {
            target = o->seaLevel;
        }
        out->dy = target - centerY;
    } else {
        /* 其他: builder.moveBelowSeaLevel(seaLevel, minY, random, 10) */
        int32_t maxY = o->seaLevel - 10;
        int32_t y1Pos = bbox_yspan(&acc) + o->minY + 1;
        if (y1Pos < maxY) y1Pos += mc_next_int(&g->rng, maxY - y1Pos);
        out->dy = y1Pos - acc.maxY;
    }

    for (i = 0; i < g->count; i++)         bbox_move(&g->pieces[i].bb, 0, out->dy, 0);
    for (i = 0; i < g->entranceCount; i++) bbox_move(&g->entrances[i].bb, 0, out->dy, 0);
    bbox_move(&acc, 0, out->dy, 0);

    out->pieceCount = g->count;
    out->entranceCount = g->entranceCount;
    out->overflow = g->overflow;
    g->startRoom = g->pieces[0].bb;
    out->structureBox = acc;
}

/* ---------------------------------------------------------------------------
 * 7. 模板合法性自检: 观测到的 span 三元组是否落在源码公式允许集合内
 * -------------------------------------------------------------------------*/

/* 模板合法性自检: 观测到的 span 三元组是否落在源码公式允许集合内
 *
 * 走廊注意: findCorridorSize 里是
 *     for (int len = random.nextInt(3) + 2; len > 0; len--)
 * nextInt(3) 只调用一次, 之后递减, 所以 corridorLength ∈ {4,3,2} 的循环会
 * 依次尝试 4/3/2 或 3/2/1 或 2/1 —— **blockLength 可以是 5**(即 1 段走廊)。
 * 因此走廊合法 x/z 跨度集合是 {5,10,15,20}, 不是只有 {10,15,20}。
 */
static int span_is_valid(int type, int32_t xs, int32_t ys, int32_t zs)
{
    switch (type) {
    case PT_ROOM:
        /* new BoundingBox(w,50,n, w+7+r6, 54+r6, n+7+r6) => 8..13 / 5..10 / 8..13 */
        return xs >= 8 && xs <= 13 && ys >= 5 && ys <= 10 && zs >= 8 && zs <= 13;
    case PT_CORRIDOR:
        /* 3 x 3 x (len*5), 或转置; len ∈ {1,2,3,4} */
        if (ys != 3) return 0;
        if (xs == 3) return zs == 5 || zs == 10 || zs == 15 || zs == 20;
        if (zs == 3) return xs == 5 || xs == 10 || xs == 15 || xs == 20;
        return 0;
    case PT_CROSSING:
        /* (-1,0,-4)..(3,y1,0) => 5 x (3|7) x 5 */
        return xs == 5 && zs == 5 && (ys == 3 || ys == 7);
    case PT_STAIRS:
        /* (0,-5,-8)..(2,2,0) => 3 x 8 x 9, 或转置 */
        return ys == 8 && ((xs == 3 && zs == 9) || (xs == 9 && zs == 3));
    case PT_ENTRANCE:
        /* 房间入口盒: 沿主轴 2 格厚, 另两轴跟随子块 */
        return (ys >= 3) && (zs == 2 || xs == 2);
    case PT_STRUCTURE:
        return 1;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------------------
 * 9. 公开 API
 * -------------------------------------------------------------------------*/

/* 版本表: 四个版本的生成语义相同, 这里记录名称与该版本的维度常量。 */
static const char *const MS_VERSION_NAMES[MS_VERSION_COUNT] = {
    "1.21.11", "26.1.2", "26.2", "26.3"
};

const char *ms_version_name(MsVersion v)
{
    if (v < 0 || v >= MS_VERSION_COUNT) return "?";
    return MS_VERSION_NAMES[v];
}

int ms_version_parse(const char *text)
{
    int v;
    if (!text) return -1;
    for (v = 0; v < MS_VERSION_COUNT; v++)
        if (!strcmp(text, MS_VERSION_NAMES[v])) return v;
    return -1;
}

/* 1.18+ 的世界高度范围与海平面在四个版本间没有变化
 * (server.properties / dimension 默认值: min_y=-64, height=384, sea_level=63)。 */
int ms_version_min_y(MsVersion v)
{
    (void)v;
    return -64;
}

int ms_version_sea_level(MsVersion v)
{
    (void)v;
    return 63;
}

/* placement 层判定，与 cubiomes getMineshafts() 的 1.13+ 分支逐位一致：
 *
 *   s = seed; a = nextLong(s); b = nextLong(s);
 *   aix = (uint64_t)chunkX * a ^ seed;
 *   setSeed(aix ^ (uint64_t)chunkZ * b);
 *   命中 <=> nextDouble() < 0.004
 *
 * 这是 vanilla RandomSpreadStructurePlacement(spacing=1, separation=0,
 * frequency=0.004, legacy_type_3) 的等价形式。受支持版本均为 1.18+，
 * 因此不需要 cubiomes 里 <=1.12 的另一条分支。
 *
 * nextDouble 这里按 Java 的精确语义算（26+27 位拼成 53 位整数后乘 2^-53），
 * 不走 mc_next_double() 的 float 精度路径 —— 后者是为复现旧版晶洞差异而保留的。 */
int ms_is_feature_chunk(uint64_t seed, int32_t chunkX, int32_t chunkZ)
{
    McRng r;
    uint64_t a, b, aix, hi, lo;

    mc_rng_set_seed(&r, seed);
    a = (uint64_t)mc_next_long(&r);
    b = (uint64_t)mc_next_long(&r);

    aix = (uint64_t)chunkX * a ^ seed;
    mc_rng_set_seed(&r, aix ^ (uint64_t)chunkZ * b);

    hi = (uint64_t)(uint32_t)mc_next_bits(&r, 26);
    lo = (uint64_t)(uint32_t)mc_next_bits(&r, 27);

    return ((double)(hi << 27 | lo) * (1.0 / 9007199254740992.0)) < 0.004;
}

const char *ms_piece_type_name(int type)
{
    if (type < 0 || type > PT_STRUCTURE) return "?";
    return PT_NAME[type];
}

const char *ms_dir_name(int orient)
{
    if (orient < 0 || orient > 3) return "none";
    return DIR_NAME[orient];
}

int32_t ms_box_xspan(const MsBox *b) { return bbox_xspan(b); }
int32_t ms_box_yspan(const MsBox *b) { return bbox_yspan(b); }
int32_t ms_box_zspan(const MsBox *b) { return bbox_zspan(b); }

int ms_box_intersects(const MsBox *a, const MsBox *b) { return bbox_intersects(a, b); }

int ms_span_is_valid(int type, int32_t xs, int32_t ys, int32_t zs)
{
    return span_is_valid(type, xs, ys, zs);
}

int32_t ms_rng_next_int(MineshaftGen *gen, int32_t bound)
{
    if (!gen) return 0;
    return mc_next_int(&gen->rng, bound);
}

void ms_rng_set_large_feature_seed(MineshaftGen *gen, uint64_t seed,
                                   int32_t chunkX, int32_t chunkZ)
{
    if (!gen) return;
    mc_set_large_feature_seed(&gen->rng, seed, chunkX, chunkZ);
}

/* 选项缺省值填充。 */
static void ms_fill_defaults(const MsGenOpts *in, GenOpts *out)
{
    MsVersion v = MS_VERSION_1_21_11;
    int type = MS_TYPE_NORMAL;

    if (in) {
        if (in->version >= 0 && in->version < MS_VERSION_COUNT) v = (MsVersion)in->version;
        if (in->type == MS_TYPE_MESA) type = MS_TYPE_MESA;
    }

    /* 先清零, 再写入明确值, 避免调用方只填了部分字段时读到未初始化数据。 */
    memset(out, 0, sizeof(*out));
    out->version    = (int)v;
    out->type       = type;
    out->seaLevel   = (in && in->seaLevel != 0) ? in->seaLevel : ms_version_sea_level(v);
    out->minY       = (in && in->minY != 0) ? in->minY : ms_version_min_y(v);
    out->mesaSurface = in ? in->mesaSurface : 0;
}

int ms_generate(MineshaftGen *gen, uint64_t seed, int32_t chunkX, int32_t chunkZ,
                const MsGenOpts *opts)
{
    GenOpts o;

    if (!gen) return -1;
    /* 巨型结构(> 400KB)每次全新初始化, 避免残留上一次的 piece。 */
    memset(gen, 0, sizeof(*gen));

    ms_fill_defaults(opts, &o);

    /* 与原版一致: 先播种(largeFeatureSeed), 然后 findGenerationPoint 开头
     * 丢弃一次 nextDouble —— 它不改变结果, 但会推进 RNG, 必须复刻。 */
    gen_structure(gen, &o, seed, chunkX, chunkZ, &gen->result);

    return gen->result.pieceCount;
}

int ms_probe(uint64_t seed, int32_t chunkX, int32_t chunkZ, const MsGenOpts *opts,
             MsBox *outBox, int *outPieces)
{
    MineshaftGen *gen;
    int n;

    gen = (MineshaftGen *)malloc(sizeof(MineshaftGen));
    if (!gen) return -1;

    n = ms_generate(gen, seed, chunkX, chunkZ, opts);
    if (n >= 0) {
        if (outBox)    *outBox = gen->result.structureBox;
        if (outPieces) *outPieces = n;
    }
    free(gen);
    return n > 0 ? 1 : 0;
}
