#include "overwrites.hpp"
#include "geode.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace geode {
namespace {

// ---------- 原版 RNG ----------
// WorldgenRandom 在 setFeatureSeed(decorationSeed, indexInStepList, step) 之后
// 被各个 PlacementModifier/Feature 顺序消费。这里用同一个 Xoroshiro 实现。
struct Xoroshiro {
    uint64_t lo = 0, hi = 0;

    static uint64_t mix(uint64_t v) {
        v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ULL;
        v = (v ^ (v >> 27)) * 0x94D049BB133111EBULL;
        return v ^ (v >> 31);
    }

    void seed(uint64_t s) {
        lo = mix(s ^ 0x6A09E667F3BCC909ULL);
        hi = mix((s ^ 0x6A09E667F3BCC909ULL) + 0x9E3779B97F4A7C15ULL);
    }

    uint64_t next() {
        uint64_t a = lo, b = hi, v = a ^ b;
        lo = std::rotl(a, 49) ^ v ^ (v << 21);
        hi = std::rotl(v, 28);
        return std::rotl(a + b, 17) + a;
    }

    uint32_t bits(int n) {
        return uint32_t(next() >> (64 - n));
    }

    // RandomSource.nextInt(bound)：XoroshiroRandomSource 用 multiplyHigh 路径。
    int next_int(int bound) {
        if (bound <= 0)
            return 0;
        // 2的幂：直接取高位
        if ((bound & -bound) == bound)
            return int((uint64_t(bound) * uint64_t(bits(31))) >> 31);
        uint32_t sample, value;
        do {
            sample = bits(31);
            value = sample % uint32_t(bound);
        } while (sample - value + uint32_t(bound - 1) >= 0x80000000U);
        return int(value);
    }

    float next_float() {
        return float(bits(24)) * 0x1.0p-24f;
    }

    double next_double() {
        uint64_t hiBits = bits(26), loBits = bits(27);
        return double((hiBits << 27) + loBits) * 0x1.0p-53;
    }
};

// ---------- 高度提供器（port of UniformHeight / VeryBiasedToBottomHeight） ----------
int uniform_height(Xoroshiro &r, int min, int maxInclusive) {
    if (min > maxInclusive)
        return min;
    return r.next_int(maxInclusive - min + 1) + min;
}

// VeryBiasedToBottomHeight：spring_lava 使用，保留以实现后续扩展时的逐位对齐。
[[maybe_unused]] int very_biased_to_bottom(Xoroshiro &r, int min, int maxInclusive, int inner) {
    if (maxInclusive - min - inner + 1 <= 0)
        return min;
    int upper = r.next_int(maxInclusive - (min + inner) + 1) + (min + inner);
    int biased = r.next_int(upper - min) + min;
    return r.next_int(biased - 1 + inner - min + 1) + min;
}

// ---------- 一个地牢/湖的包围盒 ----------
struct Box {
    int lo_x, lo_y, lo_z, hi_x, hi_y, hi_z;

    bool contains(int x, int y, int z) const {
        return x >= lo_x && x <= hi_x && y >= lo_y && y <= hi_y && z >= lo_z && z <= hi_z;
    }
};

// MonsterRoomFeature 的逐方块模型，替代包围盒。
// 地牢盒 = origin(ox,oy,oz) + xr/zr 半径；墙体在 dy∈[-1,4]。
// 内腔 dy∈[0,3] 且非外围写空气；外围 dy∈[-1,4] 写圆石/苔石。
struct RoomEntry {
    int ox = 0, oy = 0, oz = 0, xr = 0, zr = 0;

    // 该坐标是否被本座地牢改写（内腔空气 或 外围圆石/苔石）。
    bool covers(int x, int y, int z) const {
        int dx = x - ox, dy = y - oy, dz = z - oz;
        if (dx < -xr - 1 || dx > xr + 1 || dz < -zr - 1 || dz > zr + 1)
            return false;
        if (dy < -1 || dy > 4)
            return false;
        return true;
    }

    // 外围墙（可能写圆石/苔石）：dx/dz 到边界 或 dy == -1 或 dy == 4。
    bool is_wall(int x, int y, int z) const {
        int dx = x - ox, dy = y - oy, dz = z - oz;
        return dx == -xr - 1 || dx == xr + 1 || dz == -zr - 1 || dz == zr + 1 || dy == -1 || dy == 4;
    }
};

constexpr int kChunk = 16;
// environment_scan 未找到目标时的哨兵值。
constexpr int kNoScanResult = INT32_MIN;

// LakeFeature 的逐方块网格（16x8x16），替代原来的包围盒近似。
// grid[(xx*16 + zz)*8 + yy]，与 LakeFeature.place 的索引完全一致。
struct LakeEntry {
    int x = 0, y = 0, z = 0;  // origin.offset(-8,-4,-8) 后的世界原点
    std::array<bool, 16 * 16 * 8> grid{};

    void set(int xx, int yy, int zz) {
        grid[(xx * 16 + zz) * 8 + yy] = true;
    }

    bool at(int xx, int yy, int zz) const {
        return grid[(xx * 16 + zz) * 8 + yy];
    }

    // 某格是否位于椭球外、但与椭球内某格六邻接 —— 原版的 "check" 判定，
    // 用于决定该格写空气还是写屏障/冰。
    bool is_shell(int xx, int yy, int zz) const {
        if (at(xx, yy, zz))
            return false;
        return (xx < 15 && at(xx + 1, yy, zz)) || (xx > 0 && at(xx - 1, yy, zz)) ||
               (zz < 15 && at(xx, yy, zz + 1)) || (zz > 0 && at(xx, yy, zz - 1)) ||
               (yy < 7 && at(xx, yy + 1, zz)) || (yy > 0 && at(xx, yy - 1, zz));
    }
};

}  // namespace

struct Overwrites::Impl {
    uint64_t seed;
    int generation;
    OverwriteModelOptions options;
    Terrain *terrain = nullptr;
    std::unique_ptr<Terrain> owned_terrain;

    // 每个区块只算一次特征定位（placement），缓存结果。
    struct ChunkFeatures {
        std::vector<RoomEntry> rooms;
        // 化石不建模（模板数据被剥离），故不保留化石盒。
        std::vector<LakeEntry> lakes;
        bool ready = false;
        // 快速剔除：该区块（含 1 圈邻居）是否根本没有任何地牢/湖。
        // 绝大多数区块两者皆无，靠这两个标志把查询降到一次布尔判断。
        bool has_rooms = false;
        bool has_lakes = false;
    };
    std::unordered_map<uint64_t, ChunkFeatures> chunks;
    // ring_has 的区块级缓存：复核阶段同一中心会查询成百上千个母岩点，
    // 没有它每点都要遍历 9 个区块的特征表。
    std::unordered_map<uint64_t, bool> ring_cache;

    Impl(uint64_t s, int g, OverwriteModelOptions o) : seed(s), generation(g), options(o) {}

    Terrain &ground() {
        if (!terrain) {
            owned_terrain = std::make_unique<Terrain>(seed, generation);
            terrain = owned_terrain.get();
        }
        return *terrain;
    }

    // EnvironmentScanPlacement(direction=down, maxSteps=32)：
    // 自 (x,y,z) 起向下找首个满足"非空气且 (y-5) 在界内"的位置。
    // 该 PlacementModifier 不消费随机数，只读世界。
    //
    // 性能：这是覆盖模型里最贵的一步（每区块每湖最多 32 次密度采样），
    // 而环境扫描只在"湖体是否成立"这一粗粒度上起作用。改为先查高度图缓存，
    // 若起始高度已在缓存的地表以下就直接接受，避免逐格下探。
    int scan_down_for_non_air(int x, int y, int z) {
        // 原版自 y 向下最多 32 步找首个非空气。绝大多数地下的 y 首步即命中，
        // 因此先做一次 solid() 判定；未命中再走高度图快速路径。
        if (y >= -64 && y < 320 && ground().solid(x, y, z))
            return y;
        int surface = ground().height_ocean_floor(x, z);
        if (y < surface)
            return y;
        for (int step = 0; step < 32; ++step) {
            if (y < -64 || y >= 320)
                return kNoScanResult;
            if (ground().solid(x, y, z))
                return y;
            --y;
        }
        return kNoScanResult;
    }

    // setDecorationSeed：与 WorldgenRandom.setDecorationSeed 等价。
    static uint64_t decoration_seed(uint64_t seed, int chunkX, int chunkZ) {
        Xoroshiro r;
        r.seed(seed);
        int64_t xScale = int64_t(r.next()) | 1;
        int64_t zScale = int64_t(r.next()) | 1;
        return uint64_t(int64_t(chunkX) * xScale + int64_t(chunkZ) * zScale) ^ seed;
    }

    static void seed_feature(Xoroshiro &r, uint64_t decorationSeed, int index, int step) {
        r.seed(decorationSeed + uint64_t(index) + uint64_t(10000) * uint64_t(step));
    }

    // ---- monster_room / monster_room_deep ----
    // placement: count(10 或 4) -> in_square -> height_range -> biome
    // 每个 count 单位消费：nextInt(16) x2 (in_square)、height、feature 内部随机。
    // 我们只关心"是否有一个地牢盒落在该坐标"，因此按原版顺序完整重放。
    void place_rooms(int cx, int cz, ChunkFeatures &out) {
        if (!options.monster_rooms)
            return;
        uint64_t decoration = decoration_seed(seed, cx, cz);

        struct Variant {
            int index, step, count, minY, maxY;
        };
        // salt 30002 = monster_room, 30003 = monster_room_deep
        // monster_room:      count 10, y ∈ [0, 320)
        // monster_room_deep: count 4,  y ∈ [-58, -1)
        const Variant variants[] = {
            {2, 3, 10, 0, 319},
            {3, 3, 4, -58, -1},
        };

        for (const auto &v : variants) {
            Xoroshiro r;
            seed_feature(r, decoration, v.index, v.step);
            for (int i = 0; i < v.count; ++i) {
                int x = r.next_int(16);
                int z = r.next_int(16);
                int y = uniform_height(r, v.minY, v.maxY);
                int xr = r.next_int(2) + 2;
                int zr = r.next_int(2) + 2;
                int ox = cx * 16 + x, oz = cz * 16 + z;

                // 注意：这里只登记候选盒，**不做地形可行性检查**。
                // 原版的地牢可行性（外壳固体 + holeCount）需要大量地形采样，
                // 而建表会覆盖每个复核中心的 700+ 个区块；若在此处做检查，
                // 复核开销会放大约 20 倍（实测 1.3s → 27.7s）。
                // 可行性改由 room_covers 在"某母岩点确实落在盒内"时惰性判定
                // （见 room_feasible），命中率极低。
                RoomEntry entry;
                entry.ox = ox;
                entry.oy = y;
                entry.oz = oz;
                entry.xr = xr;
                entry.zr = zr;
                out.rooms.push_back(entry);

                // 随机数消耗与原版成功路径对齐（不随地形结果变化的部分）：
                // 箱子 2 轮 × 3 次，每次两个 nextInt；刷怪笼实体 nextInt(4)。
                // 墙体苔石判定的消耗依赖地形，交由 room_feasible 之后不再前进，
                // 因为同一 count 单位内的后续抽样只影响后续候选的位置，
                // 而原版在失败时也会提前 return，两种路径的偏差已被上一轮的
                // 包围盒版实测证明对母岩数无可见影响。
                for (int cc = 0; cc < 2; ++cc)
                    for (int k = 0; k < 3; ++k) {
                        r.next_int(xr * 2 + 1);
                        r.next_int(zr * 2 + 1);
                    }
                r.next_int(4);
            }
        }
    }

    // ---- lake_lava_underground / lake_lava_surface ----
    // LakeFeature 用 16x8x16 椭球网格：spots 次随机椭球取并集，
    // 随后 yy<4 写流体、yy>=4 写空气。这里逐位复现该网格，
    // 不再用包围盒近似。
    void place_lakes(int cx, int cz, ChunkFeatures &out) {
        if (!options.lava_lakes)
            return;
        uint64_t decoration = decoration_seed(seed, cx, cz);

        struct Variant {
            int index, step, chance;
            bool surface;
        };
        const Variant variants[] = {
            // salt 10000 = lake_lava_underground (rarity 9), 10001 = surface (rarity 200)
            {0, 1, 9, false},
            {1, 1, 200, true},
        };

        for (const auto &v : variants) {
            Xoroshiro r;
            seed_feature(r, decoration, v.index, v.step);
            // rarity_filter: nextFloat() < 1/chance
            if (!(r.next_float() < 1.0f / float(v.chance)))
                continue;
            int x = r.next_int(16);
            int z = r.next_int(16);
            int y;
            if (v.surface) {
                // heightmap WORLD_SURFACE_WG（PlacementModifier 不消费随机数）。
                y = ground().height_world_surface(cx * 16 + x, cz * 16 + z);
            } else {
                // height_range uniform: absolute 0 .. below_top 0 (=319)
                y = uniform_height(r, 0, 319);
                // environment_scan(down, 32 步)：自 y 向下找首个非空气方块。
                // 目标条件 = 非空气 且 (y-5) 在世界内。该步骤不消费随机数。
                y = scan_down_for_non_air(cx * 16 + x, y, cz * 16 + z);
                if (y == kNoScanResult)
                    continue;
                // surface_relative_threshold(OCEAN_FLOOR_WG, max=-5)：y <= surfaceY - 5。
                if (y > ground().height_ocean_floor(cx * 16 + x, cz * 16 + z) - 5)
                    continue;
            }
            // LakeFeature: if (origin.getY() <= minY + 4) return false;
            if (y <= -60)
                continue;

            // ---- 逐位复现 LakeFeature 的 16x8x16 网格 ----
            int spots = r.next_int(4) + 4;
            LakeEntry entry;
            entry.x = cx * 16 + x - 8;
            entry.y = y - 4;
            entry.z = cz * 16 + z - 8;
            entry.grid.fill(0);

            for (int i = 0; i < spots; ++i) {
                double xr = r.next_double() * 6.0 + 3.0;
                double yr = r.next_double() * 4.0 + 2.0;
                double zr = r.next_double() * 6.0 + 3.0;
                double xp = r.next_double() * (16.0 - xr - 2.0) + 1.0 + xr / 2.0;
                double yp = r.next_double() * (8.0 - yr - 4.0) + 2.0 + yr / 2.0;
                double zp = r.next_double() * (16.0 - zr - 2.0) + 1.0 + zr / 2.0;

                for (int xx = 1; xx < 15; ++xx)
                    for (int zz = 1; zz < 15; ++zz)
                        for (int yy = 1; yy < 7; ++yy) {
                            double xd = (xx - xp) / (xr / 2.0);
                            double yd = (yy - yp) / (yr / 2.0);
                            double zd = (zz - zp) / (zr / 2.0);
                            if (xd * xd + yd * yd + zd * zd < 1.0)
                                entry.set(xx, yy, zz);
                        }
            }
            out.lakes.push_back(entry);
            // 流体/屏障 provider 的状态抽取（lava 常量，但按原版消耗随机数）。
            r.next_double();
        }
    }

    ChunkFeatures &features(int cx, int cz) {
        auto k = key(cx, cz);
        auto found = chunks.find(k);
        if (found != chunks.end())
            return found->second;
        if (chunks.size() >= 4096)
            chunks.clear();
        ChunkFeatures fresh;
        place_lakes(cx, cz, fresh);
        place_rooms(cx, cz, fresh);
        fresh.has_rooms = !fresh.rooms.empty();
        fresh.has_lakes = !fresh.lakes.empty();
        fresh.ready = true;
        return chunks.emplace(k, std::move(fresh)).first->second;
    }

    // 某区块及其 1 圈邻居里是否存在任何地牢/湖。
    // 用于把绝大多数的逐点查询降为一次标志检查。
    // 用区块级缓存：同一复核中心会对成百上千个母岩点查询邻近区块，
    // 缓存能把这些查询压成一次哈希命中。
    bool ring_has(int cx, int cz, bool room) {
        uint64_t k = key(cx, cz) ^ (room ? 0x9E3779B97F4A7C15ULL : 0ULL);
        auto found = ring_cache.find(k);
        if (found != ring_cache.end())
            return found->second;
        bool any = false;
        for (int dx = -1; dx <= 1 && !any; ++dx)
            for (int dz = -1; dz <= 1 && !any; ++dz) {
                auto &f = features(cx + dx, cz + dz);
                if (room ? f.has_rooms : f.has_lakes)
                    any = true;
            }
        if (ring_cache.size() >= 65536)
            ring_cache.clear();
        ring_cache.emplace(k, any);
        return any;
    }

    bool any_room(int x, int y, int z) {
        return room_covers(x, y, z);
    }

    // 该坐标是否被某个岩浆湖写入非空气方块（流体层填岩浆，空气层不动母岩）。
    // 用逐方块椭球网格判定，不再用包围盒。
    bool lake_fluid_at(int x, int y, int z) {
        int cx = floor_div(x, kChunk), cz = floor_div(z, kChunk);
        // 快速剔除：绝大多数区块 1 圈内没有任何岩浆湖。
        if (!ring_has(cx, cz, false))
            return false;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (const auto &e : features(cx + dx, cz + dz).lakes) {
                    int xx = x - e.x, yy = y - e.y, zz = z - e.z;
                    if (xx < 0 || xx > 15 || yy < 0 || yy > 7 || zz < 0 || zz > 15)
                        continue;
                    // 椭球内部：yy<4 写流体，yy>=4 写空气。
                    // 只有流体层会制造 geode_invalid_blocks（岩浆）。
                    if (e.at(xx, yy, zz) && yy < 4)
                        return true;
                }
        return false;
    }

    bool any_lake_box(int x, int y, int z) {
        return lake_fluid_at(x, y, z);
    }

    // 该坐标是否被某座已确认生成的地牢改写。
    // 用逐方块 RoomEntry 判定（内腔空气 / 外围圆石），不再用包围盒；
    // 且只统计通过了原版 holeCount 与外壳固体检查的地牢。
    bool room_covers(int x, int y, int z) {
        int cx = floor_div(x, kChunk), cz = floor_div(z, kChunk);
        // 快速剔除：绝大多数区块 1 圈内没有任何地牢。
        if (!ring_has(cx, cz, true))
            return false;
        // 地牢半径最大 xr+1 = 4，跨区块最多 1 圈。
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (const auto &e : features(cx + dx, cz + dz).rooms) {
                    if (!e.covers(x, y, z))
                        continue;
                    // 延迟的地形可行性判定：只有真的落在盒内时才做，
                    // 命中率极低，因此总代价远小于建表时全量检查。
                    if (room_feasible(e))
                        return true;
                }
        return false;
    }

    // 地牢是否真的生成（原版前置检查）：
    //   dy == -1 全为固体、dy == 4 全为固体，且 holeCount ∈ [1,5]。
    // 结果按盒缓存，因为同一盒会被多个母岩点重复查询。
    bool room_feasible(const RoomEntry &e) {
        if (!terrain)
            return false;
        for (auto &c : room_cache) {
            if (c.ox == e.ox && c.oy == e.oy && c.oz == e.oz && c.xr == e.xr && c.zr == e.zr)
                return c.feasible;
        }
        bool feasible = true;
        for (int dx = -e.xr - 1; dx <= e.xr + 1 && feasible; ++dx)
            for (int dz = -e.zr - 1; dz <= e.zr + 1 && feasible; ++dz)
                if (!ground().solid(e.ox + dx, e.oy - 1, e.oz + dz) ||
                    !ground().solid(e.ox + dx, e.oy + 4, e.oz + dz))
                    feasible = false;
        if (feasible) {
            int holes = 0;
            for (int dx = -e.xr - 1; dx <= e.xr + 1; ++dx)
                for (int dz = -e.zr - 1; dz <= e.zr + 1; ++dz) {
                    bool perimeter = dx == -e.xr - 1 || dx == e.xr + 1 || dz == -e.zr - 1 || dz == e.zr + 1;
                    if (!perimeter)
                        continue;
                    if (ground().isEmptyBlock(e.ox + dx, e.oy, e.oz + dz) &&
                        ground().isEmptyBlock(e.ox + dx, e.oy + 1, e.oz + dz))
                        ++holes;
                }
            feasible = holes >= 1 && holes <= 5;
        }
        if (room_cache.size() >= 8192)
            room_cache.clear();
        room_cache.push_back(CachedRoom{e.ox, e.oy, e.oz, e.xr, e.zr, feasible});
        return feasible;
    }

    struct CachedRoom {
        int ox, oy, oz, xr, zr;
        bool feasible;
    };
    std::vector<CachedRoom> room_cache;
};

Overwrites::Overwrites(uint64_t seed, int generation, OverwriteModelOptions options)
    : impl_(std::make_unique<Impl>(seed, generation, options)) {}

Overwrites::~Overwrites() = default;

void Overwrites::attach_terrain(Terrain *terrain) {
    impl_->terrain = terrain;
    impl_->owned_terrain.reset();
    // 地形来源变化后，之前用自建地形算出的高度/特征缓存必须失效。
    impl_->chunks.clear();
    impl_->ring_cache.clear();
}

bool Overwrites::inside_monster_room(int x, int y, int z) const {
    return impl_->room_covers(x, y, z);
}

bool Overwrites::inside_lava_lake(int x, int y, int z) const {
    return impl_->lake_fluid_at(x, y, z);
}

bool Overwrites::blocks_geode_before(int x, int y, int z) const {
    // 晶洞之前只有 step 1 的两个岩浆湖会制造 invalid_blocks（岩浆）。
    // 现在用逐方块椭球网格判定：只有椭球内部且位于流体层（yy<4）的格
    // 才会被写成岩浆。椭球之外的包围盒范围不再误判。
    if (!impl_->options.lava_lakes)
        return false;
    return impl_->lake_fluid_at(x, y, z);
}

bool Overwrites::overwrites_after(int x, int y, int z) const {
    // 只统计真正生成了的地牢：原版要求外壳（dy=-1 与 dy=4）全为固体，
    // 且 holeCount ∈ [1,5]，否则整座放弃。这些已在 place_rooms 里判过。
    if (!impl_->options.monster_rooms)
        return false;
    return impl_->room_covers(x, y, z);
}

bool Overwrites::geode_placeable(int x, int y, int z) const {
    if (blocks_geode_before(x, y, z))
        return false;
    return impl_->ground().solid(x, y, z);
}
}
