#pragma once
#include <cstdint>
#include <memory>

namespace geode {
struct Simulation;
class Overwrites;

class Terrain {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Terrain(uint64_t seed, int generation = 1);
    ~Terrain();
    double density(int x, int y, int z);
    double raw_density(int x, int y, int z);
    double noise(int id, double x, double y, double z);
    bool solid(int x, int y, int z);
    double beard(int x, int y, int z);
    bool bedrock(int x, int y, int z);
    bool base_solid(int x, int y, int z);
    bool carved(int x, int y, int z);
    bool overwritten(int x, int y, int z);
    bool overwritten(int x, int y, int z, int source_x, int source_z);
    void filter_budding(Simulation &result, int source_x, int source_z);
    // 原版 Heightmap.WORLD_SURFACE_WG：自顶向下找首个非空气方块的 y+1。
    // 供覆盖模型定位 lake_lava_surface 的放置高度。
    int height_world_surface(int x, int z);
    // 原版 Heightmap.OCEAN_FLOOR_WG：自顶向下找首个"阻挡移动"方块的 y+1。
    // 供 lake_lava_underground 的 surface_relative_threshold 判定。
    int height_ocean_floor(int x, int z);
    // 该坐标的流体是否为空（用于 environment_scan 的"非空气"判定）。
    bool fluid_empty(int x, int y, int z);
    // 该坐标是否为空方块（原版 level.isEmptyBlock：空气或流体）。
    bool isEmptyBlock(int x, int y, int z);

    // C：启用/禁用"晶洞之前"与"晶洞之后"的特征覆盖模型。
    // 默认关闭，保持既有取值与既有性能（＝最大取值）。
    void enable_overwrites(bool enabled, bool lava_lakes = true, bool monster_rooms = true);
    // 该坐标是否被晶洞之前的特征填入 invalid_blocks（岩浆/水）。
    bool overwritten_before(int x, int y, int z);
    // 该坐标是否被晶洞之后的地牢覆写。
    bool overwritten_after(int x, int y, int z);
    // 原版 geode_invalid_blocks + isAir 的控制点否定判定：
    // 返回 true 表示该位置会让 GeodeFeature 记一个 invalid 控制点。
    bool geode_invalid(int x, int y, int z);
};
}
