#pragma once
#include <cstdint>
#include <memory>

namespace geode {
class Structures {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Structures(uint64_t seed, int generation);
    ~Structures();
    bool trial_written(int x, int y, int z);
    double beard(int x, int y, int z);
};
}
