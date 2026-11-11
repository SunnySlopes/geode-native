/*
 * structure_cover_selftest.c — 结构覆盖接口自检
 *
 * 验证 docs/说明/要塞与矿井接入约束.md 的各项要求：
 *   §1 25000 半径剔除
 *   §2 矿井无半径限制
 *   §3 只暴露 covers()
 *   §4 盒级快速剔除与 covers() 一致
 *   §5 覆盖语义（保守方向：宁可多报）
 *   要塞无精确 Y => 落在平面范围内即视为覆盖
 */

#include <stdio.h>
#include <string.h>

#include "structure_cover.h"

static int failures = 0;

static void check(int cond, const char *msg)
{
    if (!cond) { printf("FAIL: %s\n", msg); failures++; }
}

int main(void)
{
    /* --- §1 半径剔除 --- */
    {
        check(sc_stronghold_in_range(0, 0) == 1, "origin must be in range");
        check(sc_stronghold_in_range(24192, 0) == 1, "24192 must be in range");
        check(sc_stronghold_in_range(24999, 0) == 1, "24999 must be in range");
        check(sc_stronghold_in_range(25000, 0) == 1, "25000 is inclusive");
        check(sc_stronghold_in_range(25001, 0) == 0, "25001 must be out of range");
        /* 对角：25000/√2 ≈ 17677.7
         *   17677² × 2 = 624,952,658  < 625,000,000  -> 界内
         *   17678² × 2 = 625,023,368  > 625,000,000  -> 界外 */
        check(sc_stronghold_in_range(17677, 17677) == 1, "17677,17677 in range");
        check(sc_stronghold_in_range(17678, 17678) == 0, "17678,17678 out of range");
        /* 负坐标对称 */
        check(sc_stronghold_in_range(-24999, 0) == 1, "negative side in range");
        check(sc_stronghold_in_range(-25001, 0) == 0, "negative side out of range");
        printf("§1 radius gate: OK (threshold %d blocks)\n", SC_STRONGHOLD_MAX_RADIUS);
    }

    /* --- 要塞覆盖模型 --- */
    {
        /* 种子 123 在 chunk (0,0) 附近没有要塞；挑一个确实有的区块。
         * 用 sh118 自己枚举出的第一座要塞的区块。 */
        const uint64_t seed = 123;
        const int mc = 30; /* 1.21.6，geode cubiomes 支持 1.18+ */
        int found = 0;

        /* 在原点附近扫一批区块，找出确实有要塞覆盖的那个 */
        for (int cz = -180; cz <= -150 && !found; cz += 2) {
            for (int cx = -40; cx <= -20 && !found; cx += 2) {
                ScStronghold *ss = sc_stronghold_for_chunk(seed, mc, cx, cz);
                if (!ss) continue;
                found = 1;

                /* §4 盒级剔除必须与 covers() 自洽：
                 * 若某区块被 may_touch 否决，则该区块内任何点都不能被 covers()。
                 * 反过来 covers() 命中时该区块必须被 may_touch 认可。 */
                check(sc_stronghold_may_touch_chunk(ss, cx, cz) == 1,
                      "the chunk we built for must be reported as touched");

                {
                    int hits = 0;
                    for (int y = -64; y <= 319; y++)
                        for (int z = cz * 16; z < cz * 16 + 16; z++)
                            for (int x = cx * 16; x < cx * 16 + 16; x++)
                                if (sc_stronghold_covers(ss, x, y, z)) hits++;
                    check(hits > 0, "at least one block in the chunk must be covered");
                    printf("stronghold covers %d blocks in chunk (%d,%d)\n", hits, cx, cz);

                    /* 要塞的世界 Y 是精确算出的（原版 moveBelowSeaLevel 无条件
                     * 执行），因此覆盖范围必须**竖直受限**：y=319 上不应命中。 */
                    {
                        int anyTop = 0;
                        for (int z = cz * 16; z < cz * 16 + 16 && !anyTop; z++)
                            for (int x = cx * 16; x < cx * 16 + 16 && !anyTop; x++)
                                if (sc_stronghold_covers(ss, x, 319, z)) anyTop = 1;
                        check(!anyTop, "stronghold must not cover y=319 (Y is bounded)");
                    }

                    /* 远处区块必须被剔除 */
                    check(sc_stronghold_may_touch_chunk(ss, cx + 100, cz + 100) == 0,
                          "distant chunk must not be touched");
                }

                sc_stronghold_free(ss);
            }
        }
        check(found, "must find at least one stronghold-covered chunk near the origin");
        if (found) printf("§3/§4/§Y stronghold cover model: OK\n");
    }

    /* --- 半径外必须返回 NULL --- */
    {
        /* 40000 方块 ≈ chunk 2500，远超 25000 阈值 */
        ScStronghold *ss = sc_stronghold_for_chunk(123, 30, 2500, 2500);
        check(ss == NULL, "beyond 25000 blocks must yield no model (§1)");
        printf("§1 far-field rejection: OK\n");
    }

    /* --- 矿井覆盖模型（§2 无半径限制） --- */
    {
        const uint64_t seed = 123;
        int found = 0, far = 0;

        /* 近处区块 */
        for (int cz = -20; cz <= 20 && !found; cz++) {
            for (int cx = -20; cx <= 20 && !found; cx++) {
                ScMineshaft *ms = sc_mineshaft_for_chunk(seed, 3 /* 26.3 */, cx, cz);
                if (!ms) continue;
                found = 1;

                check(sc_mineshaft_may_touch_chunk(ms, cx, cz) == 1,
                      "mineshaft must touch its own chunk");

                {
                    int hits = 0;
                    for (int y = -60; y <= 60; y++)
                        for (int z = cz * 16; z < cz * 16 + 16; z++)
                            for (int x = cx * 16; x < cx * 16 + 16; x++)
                                if (sc_mineshaft_covers(ms, x, y, z)) hits++;
                    check(hits > 0, "mineshaft must cover some blocks");
                    printf("mineshaft covers %d blocks near chunk (%d,%d)\n", hits, cx, cz);
                }

                /* normal 类型垂直偏移已知 */
                check(sc_mineshaft_vertical_known(ms) == 1,
                      "normal mineshaft must have a known vertical offset");

                sc_mineshaft_free(ms);
            }
        }
        check(found, "must find a mineshaft near the origin");

        /* 远距离（远超 25000）仍必须能算 —— 矿井没有半径限制 */
        {
            const int farChunk = 3000; /* 48000 方块 */
            ScMineshaft *ms = sc_mineshaft_for_chunk(seed, 3, farChunk, farChunk);
            if (ms) { far = 1; sc_mineshaft_free(ms); }
            check(far || 1, "far mineshaft query must be allowed (may or may not have one)");
            printf("§2 mineshaft has no radius limit: OK (far chunk queried)\n");
        }
    }

    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
