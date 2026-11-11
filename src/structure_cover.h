/*
 * structure_cover.h — 结构覆盖接口（要塞 / 废弃矿井）
 *
 * 供 geode 紫水晶主引擎回答唯一一个问题：
 *
 *     某个方块坐标是否会被该结构写入？
 *
 * 语义与约束见 docs/说明/要塞与矿井接入约束.md。摘要：
 *
 *   §3 只暴露"是否覆盖某方块"，不暴露 piece 列表 / 方块类型 / RNG 状态。
 *   §4 必须惰性求值：建表阶段只做 RNG 复算与盒级登记，不查地形。
 *   §5 覆盖语义按原版宽松替换谓词：
 *        replaceable = s -> !s.is(BlockTags.FEATURES_CANNOT_REPLACE)
 *      即结构可以顶掉紫水晶，但顶不掉 基岩/刷怪笼/箱子/末地传送门框架/
 *      强化深板岩/trial_spawner/vault。
 *
 * 要塞的 Y 坐标
 * ---------------
 * **要塞的世界 Y 可以精确计算。** 原版 StrongholdStructure.generatePieces
 * (1.21.11 / 26.1.2 / 26.2 / 26.3 逐字节相同) 在每轮 do-while 中无条件调用
 * `builder.moveBelowSeaLevel(seaLevel=63, minY=-64, random, 10)`, 该调用只依赖
 * 常量 seaLevel/minY, 不查地形高度图。geode 的要塞模块通过
 * getStrongholdPiecesEx() 取得布局结束时的 RNG 状态, 复现这一步, 于是
 * pieces[] 的 Y 就是真实世界 Y。
 *
 * 因此 covers() 做**三维**判定。仅当某座要塞算不出 Y (dyExact=0, 理论上
 * 不发生) 时才退化为平面判定 —— 保守方向, 宁可多报覆盖, 不能漏报,
 * 否则紫水晶下限会被高估。
 */

#ifndef STRUCTURE_COVER_H_
#define STRUCTURE_COVER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* 要塞                                                                      */
/* ========================================================================= */

/**
 * 要塞搜索半径上限（方块）。见约束文档 §1。
 *
 * 原版同心环最外环（ring 7）中心最远 24192 方块，加自身半径与生物群系漂移
 * 后最坏外缘 ≈ 24416，距 25000 余 584 方块，因此 25000 是安全阈值。
 * 超过此距离的候选点直接跳过，不计算同心环。
 */
#define SC_STRONGHOLD_MAX_RADIUS 25000

/** 要塞 piece 数上限（原版深度上限 8，实践中远低于此）。 */
#define SC_STRONGHOLD_MAX_PIECES 512

typedef struct ScStronghold ScStronghold;

/**
 * 判定某候选中是否需要考虑要塞（约束文档 §1 的快速剔除）。
 *
 * @return 1 = 在 25000 方块内，需要计算；0 = 太远，跳过
 */
int sc_stronghold_in_range(int64_t x, int64_t z);

/**
 * 为某区块建立要塞覆盖模型。
 *
 * 该区块若没有任何要塞 piece 覆盖，返回 NULL（绝大多数情况）。
 * 成功时返回的句柄需用 sc_stronghold_free() 释放。
 *
 * **不查地形、不做 Y 定位**：只跑同心环 + 布局复算，登记平面范围。
 *
 * @param seed  世界种子
 * @param mc    cubiomes 版本 id（1.18+）
 * @param chunkX/chunkZ  要检查的区块
 */
ScStronghold *sc_stronghold_for_chunk(uint64_t seed, int mc, int chunkX, int chunkZ);

/** 释放句柄（传 NULL 安全）。 */
void sc_stronghold_free(ScStronghold *ss);

/**
 * 这批方块范围内是否可能被要塞覆盖（0 = 一定没有）。
 *
 * 搜索阶段对每个候选窗口都逐个方块判要塞代价过高；先用整块矩形问一次，
 * 范围里没有任何要塞就能整片跳过。只读位置表，不需要加锁。
 */
int sc_stronghold_may_reach(uint64_t seed, int mc, int x0, int z0, int x1, int z1);

/**
 * 该方块坐标是否被这座要塞写入。
 *
 * 三维判定。要塞的世界 Y 由 moveBelowSeaLevel 精确复现（见文件头）。
 * 极端情况下（某座要塞拿不到精确 Y）退化为平面判定，保持保守方向。
 */
int sc_stronghold_covers(const ScStronghold *ss, int x, int y, int z);

/**
 * 该区块是否可能被这座要塞触及（盒级快速剔除）。
 *
 * 用于在逐方块查询之前先花一次判定把整批查询挡掉。
 */
int sc_stronghold_may_touch_chunk(const ScStronghold *ss, int chunkX, int chunkZ);

/* ========================================================================= */
/* 废弃矿井                                                                  */
/* ========================================================================= */

/** 矿井 piece 数上限。 */
#define SC_MINESHAFT_MAX_PIECES 4096

typedef struct ScMineshaft ScMineshaft;

/**
 * 为某区块建立矿井覆盖模型。
 *
 * 矿井用 random_spread 逐区块独立判定（约束文档 §2），**没有半径限制**，
 * 任何距离都要正常计算。
 *
 * @param seed  世界种子
 * @param version  MsVersion（见 mineshaft_gen.h）
 * @param chunkX/chunkZ  要检查的区块
 * @return 句柄，或 NULL（该区块没有矿井）
 */
ScMineshaft *sc_mineshaft_for_chunk(uint64_t seed, int version, int chunkX, int chunkZ);

/** 释放句柄（传 NULL 安全）。 */
void sc_mineshaft_free(ScMineshaft *ms);

/**
 * 该方块坐标是否被这座矿井写入。
 *
 * 矿井有完整的三维 piece 盒（含 Y），因此这里做**三维**判定。
 */
int sc_mineshaft_covers(const ScMineshaft *ms, int x, int y, int z);

/** 该区块是否可能被这座矿井触及（盒级快速剔除）。 */
int sc_mineshaft_may_touch_chunk(const ScMineshaft *ms, int chunkX, int chunkZ);

/**
 * 矿井覆盖模型所需的垂直偏移是否已知。
 *
 * normal 类型只需 seaLevel/minY（常量），恒为 1。
 * mesa 类型需要真实地表高度；本接口不建地形，若调用方未提供则返回 0，
 * 此时 covers() 仍会保守返回 1（宁可多报）。
 */
int sc_mineshaft_vertical_known(const ScMineshaft *ms);

#ifdef __cplusplus
}
#endif

#endif /* STRUCTURE_COVER_H_ */
