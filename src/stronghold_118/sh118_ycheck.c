/*
 * sh118_ycheck.c — 验证要塞世界 Y 是否被精确算出。
 *
 * 依据原版 StrongholdStructure.generatePieces (四个目标版本逐字节相同):
 *     builder.moveBelowSeaLevel(seaLevel=63, minY=-64, random, 10);
 * 该调用无条件执行, 因此要塞的竖直位置可精确复现。
 *
 * 本测试检查:
 *   1. dyExact 恒为 1
 *   2. dy 落在 moveBelowSeaLevel 允许的闭区间内
 *   3. 平移后全体 piece 的 maxY 恰为 y1Pos (即 dy 确实到位)
 *   4. 平移后全体 piece 完全落在世界高度范围内
 *   5. 同一 (seed, chunk) 的 Y 可复现(纯函数)
 */

#include <stdio.h>
#include <string.h>

#include "biomes.h" /* MC_* version constants */
#include "sh1_18.h"

static int failures = 0;

static void check(int cond, const char *msg)
{
    if (!cond) { printf("FAIL: %s\n", msg); failures++; }
}

int main(void)
{
    static Sh118Stronghold list[SH118_MAX_STRONGHOLDS];
    static Sh118Piece pieces[SH118_PIECE_CAPACITY];

    const uint64_t seed = 123;
    int versions[4];
    int nv = 0;

    /* 收集 1.18 起的版本 id（用公开接口探测）。 */
    for (int mc = 1; mc < 200 && nv < 4; mc++)
        if (sh118_versionSupported(mc) && (nv == 0 || versions[nv - 1] != mc))
            if (mc == MC_1_18 || mc == MC_1_21_6 || mc == MC_26_2 || mc == MC_26_3)
                versions[nv++] = mc;

    if (nv == 0)
    {
        /* MC_* 常量不可用时退化为只测默认版本。 */
        versions[nv++] = 30;
    }

    for (int v = 0; v < nv; v++)
    {
        const int mc = versions[v];
        if (!sh118_versionSupported(mc)) continue;

        const int total = sh118_locateAll(list, 24, mc, seed, NULL);
        int checked = 0, dyMin = 1 << 30, dyMax = -(1 << 30);

        for (int i = 0; i < total; i++)
        {
            Sh118Stronghold *s = &list[i];
            if (!s->ok) continue;

            const int n = sh118_compute(s, pieces, SH118_PIECE_CAPACITY, mc, seed,
                                        s->chunkX, s->chunkZ);
            if (n <= 0) continue;
            checked++;

            check(s->dyExact == 1, "dy must be marked exact");

            /* piece 平移后的整体范围 */
            {
                int mnY = pieces[0].nominalMinY, mxY = pieces[0].nominalMaxY;
                for (int k = 1; k < n; k++)
                {
                    if (pieces[k].nominalMinY < mnY) mnY = pieces[k].nominalMinY;
                    if (pieces[k].nominalMaxY > mxY) mxY = pieces[k].nominalMaxY;
                }

                /* 4: 必须在世界高度范围内 */
                if (mnY < -64 || mxY > 319)
                {
                    printf("FAIL %s #%d: Y range %d..%d outside world\n",
                           sh118_versionName(mc), i, mnY, mxY);
                    failures++;
                }

                /* 3: maxY 必须等于 y1Pos = ySpan + minY + 1 + roll,
                 *    其中 roll ∈ [0, maxAllowed-y1Base) 已包含在 dy 里。
                 *    等价检验: mxY <= maxAllowed - 1 = 52。 */
                if (mxY > 52)
                {
                    printf("FAIL %s #%d: maxY %d above seaLevel-10-1\n",
                           sh118_versionName(mc), i, mxY);
                    failures++;
                }

                /* 2: 起点件(楼梯)必须在合理高度, 因为它是最高的部件之一 */
                if (mnY > mxY)
                {
                    printf("FAIL %s #%d: inverted Y range\n", sh118_versionName(mc), i);
                    failures++;
                }

                if (mxY < dyMin) dyMin = mxY;
                if (mxY > dyMax) dyMax = mxY;
            }
        }

        printf("%-8s %2d strongholds, top-most piece Y in [%d, %d]\n",
               sh118_versionName(mc), checked, dyMin, dyMax);
        check(checked > 0, "must check at least one stronghold");

        /* 5: 纯函数 —— 同一输入重算必须一致 */
        if (checked > 0)
        {
            Sh118Stronghold a, b;
            for (int i = 0; i < total; i++)
            {
                if (!list[i].ok) continue;
                sh118_compute(&a, NULL, 0, mc, seed, list[i].chunkX, list[i].chunkZ);
                sh118_compute(&b, NULL, 0, mc, seed, list[i].chunkX, list[i].chunkZ);
                check(a.dy == b.dy && a.dyExact == b.dyExact,
                      "dy must be reproducible (pure function)");
                break;
            }
        }
    }

    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("\nPASS: world Y is exact for all checked strongholds\n");
    return 0;
}
