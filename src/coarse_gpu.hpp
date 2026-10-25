#pragma once
#include "search.hpp"

namespace geode {
bool gpu_scan(const Options &options, int minx, int maxx, int minz, int maxz, const std::function<int()> &threshold,
              const std::function<void(int, int, int, int)> &offer,
              const std::function<void(uint64_t, uint64_t)> &progress,
              const std::function<bool(uint64_t)> &completed = {},
              const std::function<void(uint64_t, uint64_t)> &commit = {});
}
