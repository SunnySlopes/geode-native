#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace geode {
class Terrain;
constexpr int world_min_chunk = -1875000, world_max_chunk = 1874999;
constexpr bool uses_118_terrain(int generation) {
    return generation == -2 || generation == -1;
}
int floor_div(int x, int divisor);
uint64_t key(int x, int z);
int key_x(uint64_t value);
int key_z(uint64_t value);
bool in_world(int cx, int cz);

struct Block {
    int x, y, z;
    bool operator==(const Block &) const = default;
};

// 区块在世界生成流水线中的可写状态。
// 原版 FEATURES 步骤 blockStateWriteRadius=1，跨区块写入是否真正落地取决于目标区块
// 此刻是否仍是 ProtoChunk：一旦目标区块先升到 FULL，它会被包成
// ImposterProtoChunk(allowWrites=false)，其 setBlockState 静默返回 null。
enum class ChunkWrite {
    // 仅到 TERRAIN：可读地形、尚未装饰，仍可被中心区块写入。
    Terrain,
    // 正在本步骤装饰：ProtoChunk，可写。
    Features,
    // 已升到 FULL：写保护，跨界写入被丢弃。
    Sealed,
};

// 加载顺序模型。晶洞形状与母岩选点与顺序无关，差异只来自跨界写入能否落地。
enum class WriteOrder {
    // 全部相关区块同批次装饰（双方都是 ProtoChunk）：双向写入均落地，后写者胜。
    Parallel,
    // 按坐标字典序分批加载：字典序大者后装饰，先装饰者已 Sealed，其跨界写入丢失。
    Lexicographic,
    // 分批加载但方向相反（字典序小者后装饰）。
    ReverseLexicographic,
    // 单区块孤立加载：所有跨界写入全部丢失。
    Isolated,
};

// 一个模拟值的三态结果：lower 为最保守（跨界写入全丢），upper 为最乐观
// （跨界写入全落地），value 为按指定顺序模型算出的确定值。
struct Bounds {
    int lower = 0, upper = 0, value = 0;
};

struct Simulation {
    bool exists = false, rejected = false;
    int x = 0, y = 0, z = 0;
    std::vector<Block> budding;
    // A/B：跨界写入可见性。own_* 为该区块自身范围内的母岩，
    // spilled_* 为写入邻居区块的母岩；两者按顺序模型合并后才是真实落地集合。
    std::vector<Block> own_budding;
    std::vector<Block> spilled_budding;
    Bounds budding_bounds;
};

struct DistributionPoint {
    int x = 0, y = 0, z = 0, offset = 0;
};

struct GeodeShape {
    bool exists = false, cracked = false;
    int x = 0, y = 0, z = 0, count = 0, crack_x = 0, crack_z = 0;
    double crack_limit = 0;
    std::array<DistributionPoint, 4> points;
};
enum class Material { Unchanged, Air, Solid };

class Random {
    uint64_t low_ = 0, high_ = 0;

  public:
    explicit Random(uint64_t seed = 0) {
        seed_with(seed);
    }

    void seed_with(uint64_t seed);
    uint32_t bits(int count);
    int bounded(int bound);
    uint64_t java_long();
    float next_float();
    double next_double();
};

class Simulator {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Simulator(uint64_t seed, int generation = 1);
    ~Simulator();
    Simulator(Simulator &&) noexcept;
    Simulator &operator=(Simulator &&) noexcept;
    // 旧Java粗筛的阈值少了一个随机数取值。
    bool check(int cx, int cz, bool java_coarse = false);
    uint32_t placement_bits(int cx, int cz);
    void placement_row(int cx, int cz, int count, uint32_t *output) const;
    GeodeShape describe(int cx, int cz);
    Material material(const GeodeShape &shape, Block point) const;
    // split=true 时额外填充 own_budding / spilled_budding 与 budding_bounds，
    // 供可见性裁定使用；默认 false 保持既有开销与既有取值（＝上界）。
    Simulation generate(int cx, int cz, Terrain *terrain = nullptr, bool split = false);

    // A：判定某坐标的跨界写入在给定目标状态下能否落地。
    static bool writable(ChunkWrite target);
    // A：原版 FEATURES 的 blockStateWriteRadius。超出该半径的写入一律丢弃。
    static constexpr int kWriteRadius = 1;
    // A：给定源区块与目标坐标，返回该面是否在写区半径内。
    static bool in_write_zone(int source_x, int source_z, int x, int z);

    // B：按顺序模型把源区块的跨界贡献合并成确定值。
    Simulation resolve(int cx, int cz, WriteOrder order, Terrain *terrain = nullptr);
};

// B：按顺序模型对手头已算好的 Simulation 做一次可见性裁定。
// 供上层在只对少量候选做精确复核时调用，不必重算几何。
// cx/cz 必须是产生该 Simulation 的源区块坐标（决定谁是"溢出方"）。
Simulation apply_write_order(const Simulation &source, int cx, int cz, WriteOrder order);
const char *write_order_name(WriteOrder order);
// 把名称解析为顺序模型；未知名称返回 false。
bool parse_write_order(const std::string &name, WriteOrder &order);
}
