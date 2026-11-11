/*
 * sh118_selftest.c — cross-check the geode stronghold module against the
 * frozen reference values in this file (produced once by an independent
 * implementation and kept as the contract this module must meet for 1.18+).
 *
 * The numbers below are the reference values for seed 123.
 *
 * The test deliberately uses only the public sh1_18.h surface, so it does not
 * need cubiomes headers.
 */

#include <stdio.h>
#include <string.h>

#include "sh1_18.h"

static int failures = 0;

static void expectInt(const char *what, int got, int want)
{
    if (got != want)
    {
        printf("FAIL %-26s got %d, want %d\n", what, got, want);
        failures++;
    }
}

/* Resolve a version id by name over the supported range. The module caps and
 * floors its range, so probing outward from the floor is enough. */
static int versionByName(const char *name)
{
    /* MC_1_18 through MC_NEWEST is a contiguous enum range; scan generously. */
    for (int mc = 1; mc < 200; mc++)
    {
        if (!sh118_versionSupported(mc)) continue;
        const char *n = sh118_versionName(mc);
        if (n && strcmp(n, name) == 0) return mc;
    }
    return -1;
}

int main(void)
{
    /* --- 1.21.6, seed 123, first stronghold ------------------------------- */
    {
        const int mc = versionByName("1.21.6");
        if (mc < 0) { printf("FAIL cannot resolve 1.21.6\n"); return 1; }

        Sh118Stronghold p;
        if (!sh118_locateFirst(&p, mc, 123, NULL))
        {
            printf("FAIL 1.21.6: no stronghold located\n");
            return 1;
        }
        expectInt("1.21.6 startX", p.startX, -476);
        expectInt("1.21.6 startZ", p.startZ, -2620);
        expectInt("1.21.6 portalX", p.portalX, -521);
        expectInt("1.21.6 portalZ", p.portalZ, -2651);
        expectInt("1.21.6 eyes", p.eyes, 4);
        expectInt("1.21.6 pieces", p.pieces, 227);
        expectInt("1.21.6 frameMinX", p.frameMinX, -522);
        expectInt("1.21.6 frameMaxX", p.frameMaxX, -520);
        expectInt("1.21.6 frameMinZ", p.frameMinZ, -2652);
        expectInt("1.21.6 frameMaxZ", p.frameMaxZ, -2650);
        expectInt("1.21.6 footprintArea", p.footprintArea, 6185);
        expectInt("1.21.6 footprintWidth", p.footprintWidth, 132);
        expectInt("1.21.6 footprintDepth", p.footprintDepth, 108);
        printf("1.21.6   : start(%d,%d) portal(%d,%d) eyes %d pieces %d\n",
               p.startX, p.startZ, p.portalX, p.portalZ, p.eyes, p.pieces);
    }

    /* --- 1.18, seed 123, first stronghold --------------------------------- */
    {
        const int mc = versionByName("1.18");
        if (mc < 0) { printf("FAIL cannot resolve 1.18\n"); return 1; }

        Sh118Stronghold p;
        if (!sh118_locateFirst(&p, mc, 123, NULL))
        {
            printf("FAIL 1.18: no stronghold located\n");
            return 1;
        }
        expectInt("1.18 startX", p.startX, -444);
        expectInt("1.18 startZ", p.startZ, -2556);
        expectInt("1.18 portalX", p.portalX, -401);
        expectInt("1.18 portalZ", p.portalZ, -2528);
        expectInt("1.18 eyes", p.eyes, 1);
        expectInt("1.18 pieces", p.pieces, 363);
        printf("1.18     : start(%d,%d) portal(%d,%d) eyes %d pieces %d\n",
               p.startX, p.startZ, p.portalX, p.portalZ, p.eyes, p.pieces);
    }

    /* --- 26.3: different salt, so a different eye count for the same seed -- */
    {
        const int mc = versionByName("26.3");
        if (mc < 0)
        {
            printf("note: 26.3 not available in this cubiomes snapshot, skipped\n");
        }
        else
        {
            Sh118Stronghold p;
            if (!sh118_locateFirst(&p, mc, 123, NULL))
            {
                printf("FAIL 26.3: no stronghold located\n");
                return 1;
            }
            expectInt("26.3 portalX", p.portalX, -521);
            expectInt("26.3 portalZ", p.portalZ, -2651);
            expectInt("26.3 eyes", p.eyes, 2);
            printf("26.3     : start(%d,%d) portal(%d,%d) eyes %d pieces %d\n",
                   p.startX, p.startZ, p.portalX, p.portalZ, p.eyes, p.pieces);
        }
    }

    /* --- invariants over many strongholds and versions -------------------- */
    {
        int checked = 0;
        int versions = 0;
        for (int mc = 1; mc < 200; mc++)
        {
            if (!sh118_versionSupported(mc)) continue;
            versions++;
            static Sh118Stronghold list[SH118_MAX_STRONGHOLDS];
            const int n = sh118_locateAll(list, 32, mc, 123, NULL);
            for (int i = 0; i < n; i++)
            {
                const Sh118Stronghold *p = &list[i];
                if (!p->ok) continue;
                checked++;
                if (p->frameMaxX - p->frameMinX != 2 || p->frameMaxZ - p->frameMinZ != 2)
                {
                    printf("FAIL %s #%d: frame not 3x3\n", sh118_versionName(mc), i);
                    failures++;
                }
                if (p->portalX != (p->frameMinX + p->frameMaxX) / 2 ||
                    p->portalZ != (p->frameMinZ + p->frameMaxZ) / 2)
                {
                    printf("FAIL %s #%d: portal not the frame centre\n", sh118_versionName(mc), i);
                    failures++;
                }
                if (p->eyes < 0 || p->eyes > SH118_EYE_COUNT)
                {
                    printf("FAIL %s #%d: eyes out of range\n", sh118_versionName(mc), i);
                    failures++;
                }
                if (p->footprintArea <= 0 ||
                    (long long)p->footprintArea > (long long)p->footprintWidth * p->footprintDepth)
                {
                    printf("FAIL %s #%d: footprint area out of range\n", sh118_versionName(mc), i);
                    failures++;
                }
                if (p->portalX < p->footprintMinX || p->portalX > p->footprintMaxX ||
                    p->portalZ < p->footprintMinZ || p->portalZ > p->footprintMaxZ)
                {
                    printf("FAIL %s #%d: portal outside footprint\n", sh118_versionName(mc), i);
                    failures++;
                }
                int sum = 0;
                for (int t = 0; t < SH118_TYPE_COUNT; t++) sum += p->typeCounts[t];
                if (sum != p->pieces)
                {
                    printf("FAIL %s #%d: type counts %d != pieces %d\n",
                           sh118_versionName(mc), i, sum, p->pieces);
                    failures++;
                }
                if (p->typeCounts[SH118_PORTAL_ROOM] != 1)
                {
                    printf("FAIL %s #%d: %d portal rooms\n", sh118_versionName(mc), i,
                           p->typeCounts[SH118_PORTAL_ROOM]);
                    failures++;
                }
            }
        }
        printf("invariants: %d strongholds across %d versions\n", checked, versions);
    }

    if (failures)
    {
        printf("\n%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("\nPASS\n");
    return 0;
}
