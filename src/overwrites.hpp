#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace geode {
// C：把"会把晶洞成品改掉的特征"建成独立模型。
//
// 原版里 GeodeFeature 在 step 2 (salt 20002) 执行，而下列特征分别在它之前或之后
// 读写同一批方块，因此会改变母岩的"存在性"：
//   step 1  lake_lava_underground (10000) / lake_lava_surface (10001)
//           在晶洞之前填岩浆，构成 geode_invalid_blocks 命中源。
//   step 3  monster_room (30002) / monster_room_deep (30003)
//           fossil_upper (30000) / fossil_lower (30001)
//           在晶洞之后覆写方块，但地牢读到的地形也会决定控制点有效性。
//   step 7/8/9 各类 spring：在晶洞之后把空气换成流体。
//
// 本模型只回答"某坐标最终是什么"这一件事，供 Terrain 与可见性裁定共享。
// 方块层面的分类，供覆盖模型内部使用（避免与 geode::Block 坐标结构重名）。
enum class BlockKind : uint8_t {
    Unknown = 0,
    Air,
    Solid,
    SolidNoReplace,  // features_cannot_replace：晶洞不会覆盖
    Lava,
    Water,
};

struct OverwriteModelOptions {
    bool lava_lakes = true;
    bool monster_rooms = true;
    bool springs = true;
    // 化石（fossil_upper 30000 / fossil_lower 30001）**不建模**：
    // 反编译源码的 structure/fossil/*.snbt 调色板块数据已被剥离（全是 air），
    // 无法确定它实际写入哪些方块。因此它既不参与"最终是什么"的判定，
    // 也不从母岩下限中扣除——下限一律按"化石不影响"处理。
    bool fossils = false;
    // 覆盖模型需要自己查询地形（heightmap、固体判定），复用调用方的 Terrain 可避免重算。
    bool owns_terrain = true;
};
class Overwrites {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Overwrites(uint64_t seed, int generation, OverwriteModelOptions options = {});
    ~Overwrites();
    // 复用外部 Terrain（调用方已持有），避免重复建表；传 nullptr 则内部自建。
    void attach_terrain(class Terrain *terrain);

    // 该坐标是否被"晶洞之前"的特征填入 invalid_blocks（岩浆/水）。
    // 命中会让 GeodeFeature 的控制点判定提前作废整座晶洞。
    bool blocks_geode_before(int x, int y, int z) const;

    // 该坐标是否被"晶洞之后"的特征覆写（地牢/化石/泉水）。
    // 命中会让已落地的母岩被替换掉。
    bool overwrites_after(int x, int y, int z) const;

    // 合并视图：晶洞之前的地形是否为可被晶洞覆盖的固体。
    bool geode_placeable(int x, int y, int z) const;

    // 该坐标是否落在某个地牢盒内（用于解释 control-point 失效）。
    bool inside_monster_room(int x, int y, int z) const;
    // 该坐标是否落在某个岩浆湖盒内。
    bool inside_lava_lake(int x, int y, int z) const;
};
}
