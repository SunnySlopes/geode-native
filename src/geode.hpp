#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace geode {
class Terrain;
constexpr int world_min_chunk = -1875000, world_max_chunk = 1874999;
int floor_div(int x, int divisor);
uint64_t key(int x, int z);
int key_x(uint64_t value);
int key_z(uint64_t value);
bool in_world(int cx, int cz);

struct Block {
    int x, y, z;
    bool operator==(const Block &) const = default;
};

struct Simulation {
    bool exists = false, rejected = false;
    int x = 0, y = 0, z = 0;
    std::vector<Block> budding;
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
    Simulation generate(int cx, int cz, Terrain *terrain = nullptr);
};
}
