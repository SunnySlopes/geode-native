# 紫水晶母岩引擎

## 功能与边界

独立计算晶洞、局部地形影响和母岩热点。支持游戏随机刻范围与自定义水平圆/方形，结果为模型预计值；有限候选预算可能漏热点。支持1.18至26.3，GPU仅用于可选OpenCL粗筛，精排使用CPU。地形模型在有限预算下启用两段式精确裁剪：先用纯几何模型算出母岩数上界，只对仍可能进榜的候选跑地形模拟，并自检回退，最终榜单与单遍一致；详见使用说明。

### 精确复核（A/B/C）

榜单前 N 名可按 `--write-order` 做逐块复核。**复核不改变排名**，只在原有数值上补充更紧的下限。

#### 界面上的三个数

| 界面显示 | JSON 字段 | 含义 |
|---|---|---|
| **母岩上限数** | `estimatedBudding` | 母岩数的**上界**。搜索阶段已扣掉被要塞/矿井顶掉的母岩，但不裁剪跨区块写入、也不计地牢/岩浆湖覆写，因此只会偏高、不会偏低。 |
| **常见下限** | `buddingLowerRealistic` | **同批加载**（相邻区块一起生成）时大致能拿到的最少母岩数。把「写入无晶洞目标区块」的跨区块母岩也算作落地，其余口径同「下限」。 |
| **下限** | `buddingLower` | **任何**加载顺序都不会低于的数。只承认源区块内必然落地的母岩，以及可证必然落地的跨区块写入。 |

数值关系固定：

```
母岩上限数 ≥ 常见下限 ≥ 下限
```

桌面卡片：第一行 `母岩上限数 N`（复核前就是该条的排序值），
第二行 `常见下限 B / 下限 C`（仅复核后显示）。

#### 三者为什么不同

| 差异 | 原因 |
|---|---|
| 母岩上限数最高 | 它假设所有跨区块写入都落地，也不扣地牢/岩浆湖覆写。它是**上界**，真实数量只会更少。 |
| 常见下限 < 母岩上限数 | 除结构顶替外，跨区块写入里「写进另一座晶洞所在区块」的那部分按所有顺序取最小值；只有写进无晶洞区块的才一律算落地。 |
| 下限 < 常见下限 | 若玩家分批加载或回访已生成区域，目标区块可能**先**升到 FULL 并被包成 `ImposterProtoChunk(allowWrites=false)`，此时源区块的跨区块写入被静默丢弃。 |

一句话：**下限 = 你无论如何都不会低于的数**；**常见下限 = 正常同批加载时大概能拿到的数**。
两者之差就是加载方式带来的不确定性。

#### 内部字段

复核还会算出 **`buddingUpper`（顺序模型确定值）**：在母岩上限数的基础上，
再按 `--write-order` 裁定跨区块写入是否落地，并扣除岩浆湖/地牢覆写。
它比母岩上限数更紧，但只有在覆写模型真的顶掉母岩时才不同（实测 70 个中心里
64 个完全相等，其余差 1–7），因此**不单独展示**，只作为诊断字段输出。

| 字段 | 含义 |
|---|---|
| `buddingUpper` | 顺序模型 + 覆写后的上界（界面不展示） |
| `buddingLowerOwn` | 下限里「源区块内母岩」的基数；`buddingLower - buddingLowerOwn` 即子集 DP 的增量 |
| `buddingLowerExact` | 下限是否用上子集 DP；`false` 表示参与统计的晶洞区块超过 16 个，回退到「仅源区块」（仍是合法下界，但偏保守） |
| `structureVersion` | 本版本实际使用的要塞模型 id |
| `listedBudding` | 复核前的榜单值；仅当复核后数值有变动时才有，`-1` 表示未变 |
| `verified` | 该条是否已完成精确复核 |

**下限怎么求**：源区块内的母岩恒落地；跨区块写入按全序求精确最小值
（加权最小反馈弧集，子集 DP 求解，块级去重避免多写者重复计入）。

#### 结构顶替在哪里扣

| 结构 | 搜索阶段（两段式的两遍） | 复核阶段 |
|---|---|---|
| 古城 / 试炼密室 | ✅ 地形雕刻 | ✅ |
| 要塞 | ✅ | ✅ |
| 废弃矿井 | ✅ | ✅ |
| 地牢 / 岩浆湖 | ❌ | ✅ |

搜索阶段就扣除要塞与矿井，所以**母岩上限数与榜单排序值天然一致**；
地牢/岩浆湖只在复核阶段扣除，正是 `buddingUpper` 与母岩上限数可能差几个数的原因。

#### 三层机制

- **A 写入可见性**：原版 FEATURES 的 `blockStateWriteRadius=1`，跨区块写入是否落地取决于目标区块此刻是否已被包成 `ImposterProtoChunk`。
- **B 顺序模型**：`max` / `parallel` / `lexicographic` / `reverse-lexicographic` / `isolated`。
- **C 前后特征覆写**：岩浆湖（晶洞之前）与地牢（之后）会改变母岩存在性。

**版本支持**：精确复核对**全部受支持版本**开放（1.18 ~ 1.21.11、26.1 ~ 26.3）。
版本之间只有两处生成差异，且都已在引擎内建模：

| 分界线 | 差异 |
|---|---|
| 1.18 ~ 1.19.2 → 1.19.3 | 要塞重定位从「共用环几何随机流」改为「派生独立随机流」 |
| 1.21.8 → 1.21.9 | `cherry_grove` 加入 `stronghold_biased_to`，可承载要塞 |

废弃矿井在 1.19.2 / 1.20.1 / 1.21.5 / 1.21.11 间 RNG 序列完全一致，全版本共用同一套规则。

### 结构覆盖

要塞与矿井顶掉的母岩在**搜索阶段就已扣除**，所以「母岩上限数」与榜单排序值天然一致：

- **要塞**：同心环放置 + 原版 `StrongholdPieces` 移植，世界 Y 精确（复现 `moveBelowSeaLevel`，不查地形高度图）。25000 方块外不计算要塞。
- **矿井**：`random_spread` 逐区块判定，无半径限制；piece 生成层已与真版 `MineshaftPieces` 逐 piece 对拍通过。
- **化石不建模**：反编译源码的 `structure/fossil/*.snbt` 调色板块数据被剥离（全为 air），无法确定实际写入范围，因此既不参与区块最终状态判定，也不从下限中扣除。

结构只能让母岩数变小（排除被顶掉的母岩），不会抬高它。地牢与岩浆湖还没有上移到搜索阶段，只在复核时扣除。详见[加载顺序与写入可见性](docs/说明/加载顺序与写入可见性.md)。

#### 要塞模型是惰性的

128 次 112 格生物群系重定位是整条路径上最贵的一步（实测约 1.0 s），而布局本身只要约 46 ms。
因此要塞世界模型拆成两步：

- 先只枚举**环几何**（几乎零成本）；
- 只有当某座要塞的近似起点落到查询区块 448 格以内时，才补做重定位 + 布局。

实测收益：`--refine-top 1` 从 1413 ms 降到 463 ms，近原点搜索从 1220 ms 降到 249 ms。
全图搜索时退化为全量计算，行为不变。

## 构建

需要CMake、Ninja与C++20编译器，Windows使用MinGW。已有build目录沿用缓存生成器。普通构建不需要Java、Python、tests或开发工具。

```powershell
.\build.ps1
# 仅完整本地开发环境使用：
.\build.ps1 -Test
```

生成`build/geode-finder.exe`。本引擎独立构建，不依赖相邻仓库。

### 自测

四个自测默认不构建，按需开启：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe `
  -DGEODE_BUILD_COVER_SELFTEST=ON `
  -DGEODE_BUILD_STRONGHOLD_SELFTEST=ON `
  -DGEODE_BUILD_MINESHAFT_SELFTEST=ON
cmake --build build --parallel
```

| 目标 | 校验内容 |
|---|---|
| `cover-selftest` | 结构覆盖接口（25000 半径剔除、要塞/矿井覆盖） |
| `sh118-selftest` | 要塞布局：14 个版本 × 32 座的不变式 |
| `sh118-ycheck` | 要塞世界 Y 的精确性断言 |
| `ms-selftest` | 矿井生成：四版本一致性与盒合法性 |

## 运行

在本项目根目录执行：

```powershell
.\run.bat
.\run.bat --seed 12345 --radius 10000 --limit 20
.\build\geode-finder.exe --config finder.json --output results
.\build\geode-finder.exe --help
```

`run.bat`读取本目录存在的finder.json，再追加命令行覆盖值；程序本身只在明确传入`--config`时读取配置，不会暗中查找文件。所有引擎使用`--limit`控制结果条数，默认100；`--output`始终为目录，默认results，由后端生成CSV/TXT/JSON并返回实际文件路径。

## 配置

统一使用UTF-8 JSON。键名就是命令行参数名去掉前面的`--`，不是桌面JSONL报文；种子必须写为字符串。

```json
{
  "seed": "0",
  "version": "26.3",
  "radius": 10000,
  "shape": "circle",
  "threads": 8,
  "limit": 100,
  "output": "results"
}
```

优先级为**命令行覆盖值 → JSON配置 → 引擎内置默认值**。相对路径以启动工作目录为准；run.bat会先切到本项目目录。配置开关使用true/false。格/区块由后端换算，外半径用radius与radius-unit；算法专属参数见[使用说明](docs/说明/使用说明.md)。finder.json保留本项目已有默认搜索偏好，示例中的版本不代表所有项目文件都被改成同一版本。

## 桌面接口

三个引擎统一使用`--desktop`，通过stdin/stdout交换协议v3的UTF-8 JSON Lines。finder为`geode`。桌面直接发送完整任务配置，不经过run.bat、不读取finder.json；长期设置与进程调度由桌面壳负责，正式计数、排名和文件内容由C++负责。配置文件与JSONL请求分别用于独立CLI和桌面调用，不互相套用。

## 作为C++算法库使用

公开入口是`<minecraft_finders/geode.hpp>`，目标为`MinecraftFinders::Geode`。将完整源码仓库作为子目录加入你的CMake项目即可；这里是源码集成接口，不承诺跨编译器二进制ABI。

```cmake
add_subdirectory(path/to/geode-native geode-build)
target_link_libraries(my_program PRIVATE MinecraftFinders::Geode)
```

直接调用示例见[examples/basic.cpp](examples/basic.cpp)。嵌入其他项目时默认只构建算法库；独立构建默认保留CLI和桌面协议。`GEODE_BUILD_CLI`可显式切换，`GEODE_BUILD_EXAMPLE=ON`可构建示例；`BUILD_TESTING`默认关闭。不需要tests、tools、Java或相邻Cubiomes仓库，也不要求程序经由JSON或子进程调用算法。

本引擎选择Cubiomes模块：**Biomes + Java263**。`third_party/cubiomes/`是固定提交的最小源码快照，锁文件和revision文件一并保留；升级时从集中维护仓库导出所需模块，将ZIP内容放入`third_party/`，整体替换旧快照，勿在副本中修库。各引擎可以锁定不同已验收提交。

> **为什么本引擎必须自带上锁快照**：本引擎依赖该分支的专有扩展
> （`sampleBiomeClimate`、`SAMPLE_NO_TEMPERATURE`、`SAMPLE_NO_HUMIDITY` 等
> 26.3 气候/噪声接口），上游 `xpple/cubiomes` 没有这些符号；`noise.c`、
> `biomenoise.c` 等文件也带 26.3 浮点 Perlin 补丁。因此**不能**改用仓库根
> `tools/cubiomes`，否则 26.3 的跨版本行为会漂移。

发布构建启用函数/数据分节、链接裁剪和符号剥离，保留`-O3`与原有浮点顺序；`GEODE_ENABLE_IPO`控制可用时的Release跨文件优化。Windows静态链接编译器运行库，不需要Cubiomes DLL。不使用fast-math、UPX或本机专属指令集。

## 目录与说明

`include/`为公开入口，`examples/`为直接调用示例，`src/`为算法和入口，`third_party/`为固定依赖，`build/`为本地构建，`results/`为CLI输出，`docs/说明/`为现行说明。已有的data目录保存必要编译常量。`test/`、`docs/记录/`、`docs/计划/`仅本地保留（已在 `.gitignore` 中），不参与普通源码构建，也不作为桌面运行依赖。

`test/` 存放开发期的验证与基准脚本，不入库：

| 脚本 | 用途 |
|---|---|
| `feature_order.py` | 从原版 biome JSON 反推特征 salt |
| `stronghold_rings.py` | 复算要塞同心环最远半径（25000 阈值依据） |
| `validate_lower.py` | 校验 `own <= lower <= realistic <= upper <= estimate` |
| `verify_bounds.py` | 经桌面协议端到端校验三个下限字段 |
| `bench_default.py` / `bench_marginal.py` / `bench_refine.py` | 搜索与复核的基准 |
| `bench_stronghold_cost.c` | 拆分要塞模型成本（环几何 / 重定位 / 布局） |
| `extract_fossils.py` | 化石模板提取尝试（数据被剥离，未采用） |

`src/`内新增的三个模块：

| 路径 | 内容 |
|---|---|
| `src/structure_cover.c/.h` | 结构覆盖接口：只回答"某方块是否被要塞/矿井写入" |
| `src/stronghold_118/` | 要塞布局与传送门模拟（自包含，`vendor/` 为 cubiomes stronghold 副本 + 一处 RNG 导出扩展） |
| `src/mineshaft_gen/` | 矿井 piece 生成（纯 C11 + Java LCG，不依赖 cubiomes） |

`src/mineshaft_gen/reference/` 是对拍工具链，不参与构建：`MineshaftDump.java`（跑真版 Minecraft 类）、`ms_dump.c`（C 端对照）、`crosscheck.py`（逐 piece 比对）、`stub/`（精简替身）。

> 该目录**本地保留、不入库**（已在 `.gitignore` 中）：`stub/` 是 Minecraft 反编译源码的副本，
> 原源码树声明不可再分发。

[使用说明](docs/说明/使用说明.md) · [第三方来源](THIRD_PARTY.md)


Cubiomes使用[集中维护分支](https://github.com/qk-yiyihehe/cubiomes/tree/minecraft-finders)的固定源码子集；[依赖锁文件](third_party/cubiomes-lock.json)记录提交、补丁和SHA256。普通构建不联网，结果以`cubiomesCommit`回显依赖身份；升级不等于原版精度已验证。

## 许可证

自有代码采用GNU GPL第3版（GPL-3.0-only），完整条款见[LICENSE](LICENSE)。第三方代码保留各自版权和许可，详见[THIRD_PARTY.md](THIRD_PARTY.md)。
