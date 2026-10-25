#pragma once
#include <cstdint>
#include <memory>

namespace geode {
class Carver {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Carver(uint64_t seed);
    ~Carver();
    bool contains(int x, int y, int z);
};
}
