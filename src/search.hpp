#pragma once
#include "geode.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace geode {
// 比较器、距离参照或Top K截断时机变化时提升版本。
constexpr int kRankingRuleVersion = 3;
constexpr char kRankingRule[] = "primary-desc-distance-asc-x-asc-z-asc";

struct Options {
    uint64_t seed = 0;
    std::string seed_text = "0", version = "1.21.1", shape = "circle", scan_backend = "auto";
    std::string coverage = "game";
    int inner_radius = 128;
    int center_x = 0, center_z = 0, radius = 10000, tick_radius = 128, simulation_distance = 10;
    int min_geodes = 1, min_budding = 1, prefilter = 0, limit = 100, threads = 8;
    // search_rect = true 时使用精确四至（闭区间），忽略 center/radius/shape。
    // 四至的排名中心取 (min+max)/2，与协议 bounds 的 center 一致。
    bool search_rect = false;
    int min_x = 0, max_x = 0, min_z = 0, max_z = 0;
    // 0 = 与 threads 相同；大于 threads 时只提高粗筛并行度，模拟仍用 threads。
    int scan_threads = 0;
    // 0 = 逐 512 区块瓦片记录断点（旧行为）；>0 = 按该区块边长的区域聚合断点。
    // 全图搜索应设为 8192（131,072 方块），避免 5,000 万瓦片的断点爆炸。
    int checkpoint_region = 0;
    bool modern = false, terrain = true, full_world = false;
    int generation = 1;
    // A/B/C 精确复核：只对最终榜单的前 refine_top 名重算。
    // 原取值（budding）对齐的是"最大取值"＝所有跨界写入都落地，
    // 因此默认不改变既有输出；refine_top>0 时才对前若干名降压。
    int refine_top = 10;
    // 顺序模型：max（默认，＝原最大取值）/ parallel / lexicographic /
    // reverse-lexicographic / isolated。max 之外才会真正减少母岩。
    std::string write_order = "max";
    // 是否在复核时启用 C 的覆盖模型（岩浆湖 / 地牢）。
    bool precise_overwrites = true;
    // 本版本是否支持 A/B/C 精确搜索。
    // 精确模型只对 1.21.11 及以后校验过；更低版本仍按旧的"最大取值"方式计算。
    // 注意不能用 generation 判断：1.21.9/1.21.10 同样是 generation 4，
    // 但未纳入精确搜索范围，必须按版本号本身判定。
    bool precise_available() const;
    std::function<void()> checkpoint;
    std::string checkpoint_directory;
    std::function<double()> paused_ms;
    std::function<void(double, uint64_t, uint64_t)> restored;
    bool contains(int x, int z) const;
    bool intersects_chunk(int cx, int cz) const;
    int activity_radius() const;
    bool chunk_constant() const { return coverage == "game" && modern; }
    bool custom() const { return coverage != "game"; }
};

const std::vector<std::string> &versions();
void set_version(Options &o, const std::string &version);
// 全图简写必须先展开，再计算排名距离，
// 确保实际榜单中心为原点，而不是用户输入但未生效的中心。
void expand_full_world(Options &o);

// 协作式暂停/取消：工作线程在检查点阻塞或抛出 Cancelled。
// 与旧的 Options::checkpoint 相比，暂停延迟为一个工作单位，且取消可正常收敛。
struct Cancelled {};

class Control {
    std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<bool> paused_{};
    std::atomic<bool> cancelled_{};
    std::atomic<int> waiting_{};
    std::chrono::steady_clock::time_point paused_at_{};
    std::chrono::steady_clock::duration paused_time_{};

  public:
    void checkpoint();
    void pause();
    void resume();
    void cancel();
    bool paused() const {
        return paused_.load(std::memory_order_relaxed);
    }
    bool cancelled() const {
        return cancelled_.load(std::memory_order_relaxed);
    }
    // 当前阻塞在检查点等待恢复的工作线程数；宿主可用它确认暂停已生效。
    int waiting() const {
        return waiting_.load(std::memory_order_relaxed);
    }
    // 累计暂停毫秒，含当前尚未恢复的暂停段。
    double paused_ms();
};

struct Weight {
    int x, z, budding;
};

struct Center {
    int x = 0, z = 0, budding = -1, geodes = 0;
};

using Weights = std::unordered_map<uint64_t, int>;
bool active(const Options &o, int player_x, int player_z, int cx, int cz);
bool active_point(const Options &o, int player_x, int player_z, int x, int z);
std::pair<int, int> coarse_span(const Options &o, int dz);
Center optimize(const Options &o, int cx, int cz, const std::vector<Weight> &weights,
                const std::vector<Weight> &origins = {});
bool same_hotspot(const Options &o, Center a, Center b);
Center evaluate(const Options &o, int x, int z);

// A/B/C 精确复核：对给定候选中心重算母岩数，返回确定值与上下界。
// 与 evaluate() 的区别是它按 write_order 裁定跨界写入是否落地，
// 并可启用覆盖模型（岩浆湖/地牢）修正控制点与事后覆写。
// 单独成接口，便于上层只对榜单前 N 名调用，避免全量搜索的额外开销。
struct RefinedCenter {
    int x = 0, z = 0;
    // 兼容旧字段：等于 upper（顺序模型确定值）。
    int budding = 0, geodes = 0;
    // own_budding：只统计必然落在源区块内的母岩，任何加载顺序下都成立（下界）。
    int lower = 0;
    // 常见加载口径：把「写入无晶洞目标区块」也算成功。典型值，不是下界。
    int lower_realistic = 0;
    // 带 &&bounded 的顺序模型确定值（裁剪不可落地的跨区块写入、计入岩浆湖/地牢覆盖）（上界）。
    int upper = 0;
    // 不带 &&bounded 的最大取值，即未开启精确搜索时会输出的原预测值。
    int estimate = 0;
    // 下限是否用上了跨区块精确最小化（子集 DP）；false 表示回退到 own-only。
    bool lower_dp = false;
    // 下限里的 own-only 基数（诊断用：lower - lower_own 即 DP 增量）。
    int lower_own = 0;
    bool exact = false;
};

// 单点复核。budding 为按 write_order 的确定值，lower/upper 为顺序不敏感区间。
RefinedCenter refine_center(const Options &o, int x, int z);

/**
 * 版本字符串 → 结构模型使用的 cubiomes 版本 id（MC_*）。
 * 返回 -1 表示该版本没有可用的结构模型。
 * 对外暴露是为了让诊断输出能显示"这个版本实际走了哪套要塞规则"。
 */
int structure_version_id(const std::string &version);

/**
 * 单区块晶洞的三个母岩口径（地图上的晶洞图标用）。
 *
 * 与 refine_center 的口径区别：这里只统计**该区块自己的晶洞**，不扩展到
 * 活跃半径；因为地图图标代表的就是这一座晶洞。
 *
 *   max       该晶洞的全部母岩（= "最大取值"，不做任何裁剪）
 *   realistic 同批加载下会落地的数量（扣除被岩浆湖/地牢覆写、被结构顶掉的）
 *   lower     任何加载顺序下都必然落地的数量（仅源区块内 + 未被顶掉）
 */
struct ChunkBuddingBounds {
    bool exists = false;
    int max = 0;
    int realistic = 0;
    int lower = 0;
};

ChunkBuddingBounds chunk_budding_bounds(const Options &o, int chunkX, int chunkZ);
// 复用外部 Terrain 的重载：批量复核前 N 名时传入同一个实例，
// 让噪声表与含水层/表面缓存保持热态。逐个中心新建 Terrain 会让缓存全冷。
// 传入的 Terrain 会被启用覆盖模型（若 precise_overwrites 生效），调用方不应再并行使用。
RefinedCenter refine_center(const Options &o, int x, int z, class Terrain *shared);
// 最终榜单使用水平距离；世界边界内的坐标保证
// 距离平方小于2^53，JSON可精确携带该整数。
int64_t center_distance_sq(int center_x, int center_z, int x, int z);
// 从精确距离平方计算十分之一格显示值：round(10*sqrt(distanceSq))。
int64_t distance_tenths(int center_x, int center_z, int x, int z);
// 正式榜单规则v3：母岩数降序，距离平方、X、Z依次升序。
// 与粗筛、候选池和组内几何比较器分开，后者保留原有语义；
// 只有此规则决定最终名次、Top K截断和正式导出顺序。
bool final_better(int center_x, int center_z, Center a, Center b);

struct Stats {
    uint64_t scanned = 0, candidates = 0, simulated = 0, weights = 0, pruned = 0, rejected = 0, hotspots = 0,
             optimized = 0, resumed_scan = 0, resumed_simulation = 0;
    // 两段式裁剪：石模型（无地形）先算母岩数上界，只有上界仍可能进榜的候选才跑地形。
    // stone_simulated 是上界那一遍的模拟次数，simulated 只统计地形那一遍。
    uint64_t stone_simulated = 0, two_pass_kept = 0;
    // 模拟阶段因被要塞覆盖而扣掉的母岩数（与 Terrain 里的古城/试炼密室同属
    // 地下结构顶替，所以在模拟阶段而不是复核阶段处理）。
    uint64_t stronghold_excluded = 0;
    // 模拟阶段因被废弃矿井覆盖而扣掉的母岩数。
    uint64_t mineshaft_excluded = 0;
    int two_pass_excluded_bound = 0, two_pass_floor_bound = 0;
    bool two_pass = false, two_pass_retried = false;
    double scan_ms = 0, simulate_ms = 0, rank_ms = 0, total_ms = 0, resumed_ms = 0;
    std::string scan_backend = "cpu";
};

struct Results {
    std::vector<Center> centers;
    // 与 centers 下标一一对应的精确复核明细。
    // 长度等于 refined_count；不在其中的条目仍为估值。
    // 三个口径的大小顺序：estimate >= upper >= lower。
    struct Refined {
        int estimate = 0;  // 不带 &&bounded 的原预测值
        int upper = 0;     // 带 &&bounded 的确定值
        int lower = 0;     // own_budding，必然落地的下界
        // 常见加载口径（写入无晶洞目标区块也算成功）；典型值，不是下界。
        int lower_realistic = 0;
        // 下限是否用上跨区块精确最小化；false = 回退到 own-only。
        bool lower_dp = false;
        // 下限里的 own-only 基数（诊断用：lower - lower_own 即 DP 增量）。
        int lower_own = 0;
    };
    std::vector<Refined> refined;
    Stats stats;
    std::string checkpoint_path;
    // G-UP1：Control 取消时正常返回，不抛异常。
    bool cancelled = false;
    // G-UP5：引擎直接给出结果语义，宿主不再推断。
    bool approximate = false;
    int ranking_rule_version = kRankingRuleVersion;
    std::string scan_backend = "cpu";
    // A/B/C：前 refine_top 名是否已按 write_order 复核，以及实际使用的顺序模型。
    int refined_count = 0;
    std::string write_order = "max";
    // 精确搜索是否因版本过低而被禁用（低版本仍用旧的最大取值口径）。
    bool precise_disabled = false;
};

// 进度回调：阶段名、阶段内完成量、阶段内总量、已扫描区块数。
// 第四个参数让界面在运行中显示真实扫描量，而不是等结果才知道。
using ProgressFn = std::function<void(const std::string &, uint64_t, uint64_t, uint64_t)>;
using Progress = ProgressFn;
// 后端回调：本次运行实际使用的粗筛后端（如 cpu / gpu）与人类可读说明。
using Backend = std::function<void(const std::string &, const std::string &)>;

Results search(const Options &o, const ProgressFn &progress = {}, const Backend &backend = {});
// 可暂停/可取消的重载；旧 Options::checkpoint 仍会在此路径生效。
Results search(const Options &o, Control &control, const ProgressFn &progress = {}, const Backend &backend = {});
}
