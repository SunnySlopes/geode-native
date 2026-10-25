#pragma once
#include <cstdint>
#include <memory>

namespace geode {
struct Simulation;

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
};
}
