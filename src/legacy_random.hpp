#pragma once
#include <bit>
#include <cstdint>

namespace geode {
struct LegacyRandom {
    uint64_t state;

    explicit LegacyRandom(uint64_t seed) : state((seed ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1)) {}

    uint32_t bits(int n) {
        state = (state * 0x5DEECE66DULL + 11) & ((1ULL << 48) - 1);
        return uint32_t(state >> (48 - n));
    }

    int bounded(int n) {
        if ((n & -n) == n)
            return int((uint64_t(n) * bits(31)) >> 31);
        uint32_t s, r;
        do {
            s = bits(31);
            r = s % uint32_t(n);
        } while (s - r + uint32_t(n - 1) >= 0x80000000U);
        return int(r);
    }

    uint64_t next_long() {
        uint64_t hi = uint64_t(bits(32)) << 32;
        return hi + uint64_t(int64_t(std::bit_cast<int32_t>(bits(32))));
    }

    float next_float() {
        return bits(24) * 0x1.0p-24f;
    }

    double next_double() {
        uint64_t hi = bits(26), lo = bits(27);
        return ((hi << 27) + lo) * 0x1.0p-53;
    }

    LegacyRandom fork() {
        return LegacyRandom(440898198ULL ^ next_long());
    }
};
}
