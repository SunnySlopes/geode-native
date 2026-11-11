#ifndef STRONGHOLD_H_
#define STRONGHOLD_H_

#include "../sh_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generate the structure pieces of a Stronghold. The maximum number of pieces
 * that are generated is limited to 'n'. A buffer length of around 400 should
 * be sufficient in practice, but a stronghold can in theory contain many more
 * than that. The number of generated pieces is given by the return value.
 *
 * This code does not adjust the Y-coordinate of the pieces, so the starting
 * piece will always be at Y=64. This can be enabled by removing the
 * `mc <= MC_1_12_2 && !env.portal` check, and additionally removing
 * `&& !env.generationStopped` from the `while (list->next)` loop. The reason
 * this code is not executed by default is that the Y-coordinate is not
 * relevant (for the most part) for loot/eye generation.
 *
 * This function does not compute loot seeds or portal room eye count, use
 * `getStrongholdLoot` for that.
 *
 * @param list the pieces list
 * @param n the maximum number of pieces to generate
 * @param mc the Minecraft version
 * @param seed the world seed (lower 48 bits suffice)
 * @param chunkX the chunk X-coordinate
 * @param chunkZ the chunk Z-coordinate
 * @return the number of pieces that were generated
 */
int getStrongholdPieces(Piece *list, int n, int mc, uint64_t seed, int chunkX, int chunkZ);

/**
 * 同 getStrongholdPieces, 但额外输出布局结束时的 RNG 状态。
 *
 * 原版 StrongholdStructure.generatePieces 在布局生成后立即调用
 * builder.moveBelowSeaLevel(..., random, 10), 消耗该 RNG 的下一个 nextInt
 * 来确定竖直偏移。要复现精确的世界 Y, 必须能续用这个状态。
 *
 * @param outRng 非 NULL 时写入布局结束的 RNG 种子字; 传 NULL 与
 *               getStrongholdPieces 完全等价。
 */
int getStrongholdPiecesEx(Piece *list, int n, int mc, uint64_t seed, int chunkX, int chunkZ,
                          uint64_t *outRng);

enum {
    SH_STRAIGHT = 0,
    SH_PRISON_HALL,
    SH_LEFT_TURN,
    SH_RIGHT_TURN,
    SH_ROOM_CROSSING,
    SH_STRAIGHT_STAIRS_DOWN,
    SH_STAIRS_DOWN,
    SH_FIVE_CROSSING,
    SH_CHEST_CORRIDOR,
    SH_LIBRARY,
    SH_PORTAL_ROOM,
    SH_FILLER_CORRIDOR,
    SH_PIECE_COUNT,
};

/**
 * First generates the Stronghold's pieces using getStrongholdPieces. Then
 * for each pieces with chests, the chest's position, loot table and loot
 * seed is written. For the portal room, the eye final eye count is written
 * to p.additionalData using a bit array (e.g. 0b101011011100 would mean
 * 7 eyes are generated).
 *
 * @param list the pieces list
 * @param n the maximum number of pieces to generate
 * @param ssconf the salt config, use getStructureSaltConfig to obtain it
 * @param mc the Minecraft version
 * @param seed the world seed (lower 48 bits suffice)
 * @param chunkX the chunk X-coordinate
 * @param chunkZ the chunk Z-coordinate
 * @return the number of pieces that were generated
 */
int getStrongholdLoot(Piece *list, int n, StructureSaltConfig ssconf, int mc, uint64_t seed, int chunkX, int chunkZ);

#ifdef __cplusplus
}
#endif

#endif //STRONGHOLD_H_
