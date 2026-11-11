/*
 * ms_selftest.c — 验证 geode 能通过公开 API 取到矿井的具体生成。
 *
 * 这是集成冒烟测试: 证明链接、接口、数据流可用。**不**验证与游戏的
 * 逐 piece 一致性 —— 那一项仍在待办(见 README)。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mineshaft_gen.h"

static int failures = 0;

static void check(int cond, const char *msg)
{
    if (!cond) { printf("FAIL: %s\n", msg); failures++; }
}

int main(void)
{
    MineshaftGen *gen;
    MsGenOpts opts;
    int n, i;
    int firstCount = -1;
    MsBox firstRoom;
    int versions[] = { MS_VERSION_1_21_11, MS_VERSION_26_1_2,
                       MS_VERSION_26_2, MS_VERSION_26_3 };

    /* 版本表 */
    check(ms_version_parse("1.21.11") == MS_VERSION_1_21_11, "parse 1.21.11");
    check(ms_version_parse("26.1.2") == MS_VERSION_26_1_2, "parse 26.1.2");
    check(ms_version_parse("26.2") == MS_VERSION_26_2, "parse 26.2");
    check(ms_version_parse("26.3") == MS_VERSION_26_3, "parse 26.3");
    check(ms_version_parse("1.20") < 0, "reject 1.20");

    gen = (MineshaftGen *)malloc(sizeof(MineshaftGen));
    if (!gen) { printf("FAIL: OOM\n"); return 1; }

    printf("%-9s %8s %8s %6s %6s %5s\n",
           "version", "pieces", "entrance", "dy", "minX", "minZ");

    for (i = 0; i < 4; i++) {
        memset(&opts, 0, sizeof(opts));
        opts.version  = versions[i];
        opts.type     = MS_TYPE_NORMAL;
        opts.seaLevel = ms_version_sea_level((MsVersion)versions[i]);
        opts.minY     = ms_version_min_y((MsVersion)versions[i]);

        n = ms_generate(gen, 123, 0, 0, &opts);
        check(n >= 0, "ms_generate must not fail");

        printf("%-9s %8d %8d %6d %6d %5d\n",
               ms_version_name((MsVersion)versions[i]),
               gen->count, gen->entranceCount, (int)gen->result.dy,
               gen->startRoom.minX, gen->startRoom.minZ);

        /* 四个版本的生成规则相同, 必须给出完全一致的结果。
         * 逐个与第 0 个版本比较。 */
        if (i == 0) {
            firstCount = gen->count;
            firstRoom = gen->pieces[0].bb;
        } else {
            check(firstCount == gen->count,
                  "all versions must agree on piece count");
            check(memcmp(&firstRoom, &gen->pieces[0].bb, sizeof(MsBox)) == 0,
                  "all versions must agree on the room box");
        }

        if (n > 0) {
            int k;
            /* 起点房间必须在 pieces[0], 且盒型合法 */
            check(gen->pieces[0].type == MS_PT_ROOM, "pieces[0] must be the room");
            check(gen->pieces[0].genDepth == 0, "room depth must be 0");
            check(ms_span_is_valid(MS_PT_ROOM,
                                   ms_box_xspan(&gen->pieces[0].bb),
                                   ms_box_yspan(&gen->pieces[0].bb),
                                   ms_box_zspan(&gen->pieces[0].bb)),
                  "room span must be valid");

            /* 所有 piece 的盒型都必须落在源码公式允许的集合内 */
            for (k = 0; k < gen->count; k++) {
                const MsPiece *p = &gen->pieces[k];
                if (!ms_span_is_valid(p->type,
                                      ms_box_xspan(&p->bb),
                                      ms_box_yspan(&p->bb),
                                      ms_box_zspan(&p->bb))) {
                    printf("FAIL: piece %d (type %d) has an invalid span\n", k, p->type);
                    failures++;
                }
                if (p->bb.minX > p->bb.maxX || p->bb.minY > p->bb.maxY ||
                    p->bb.minZ > p->bb.maxZ) {
                    printf("FAIL: piece %d has an inverted box\n", k);
                    failures++;
                }
            }

            /* 整体并集必须包住每个 piece */
            for (k = 0; k < gen->count; k++) {
                const MsPiece *p = &gen->pieces[k];
                if (p->bb.minX < gen->result.structureBox.minX ||
                    p->bb.maxX > gen->result.structureBox.maxX ||
                    p->bb.minY < gen->result.structureBox.minY ||
                    p->bb.maxY > gen->result.structureBox.maxY ||
                    p->bb.minZ < gen->result.structureBox.minZ ||
                    p->bb.maxZ > gen->result.structureBox.maxZ) {
                    printf("FAIL: piece %d escapes the structure box\n", k);
                    failures++;
                }
            }
        }
    }

    /* ms_probe 轻量路径 */
    {
        MsBox box;
        int pieces = 0;
        int r = ms_probe(123, 0, 0, NULL, &box, &pieces);
        check(r == 1, "ms_probe must find a mineshaft at (0,0) seed 123");
        check(pieces > 0, "ms_probe must report pieces");
        check(box.minX <= box.maxX, "probe box must be well formed");
        printf("\nprobe(123, 0, 0): pieces=%d box=(%d,%d,%d)..(%d,%d,%d)\n",
               pieces, box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ);
    }

    free(gen);
    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("\nPASS\n");
    return 0;
}
